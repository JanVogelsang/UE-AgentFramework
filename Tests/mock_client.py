import json
import queue
import threading
import time
import requests


class PipeReader:
    def __init__(self, stream):
        self.stream = stream
        self.queue = queue.Queue()
        self.thread = threading.Thread(target=self._read_loop, daemon=True)
        self.thread.start()

    def _read_loop(self):
        try:
            for line in iter(self.stream.readline, ""):
                if line:
                    self.queue.put(line.strip())
        except Exception:
            pass

    def readline(self, timeout=5.0):
        try:
            return self.queue.get(timeout=timeout)
        except queue.Empty:
            return None


class MockAgentClient:
    def __init__(self, python_process=None, cpp_url="http://127.0.0.1:8000/mcp", endpoint_file=None):
        """
        Mock Agent Client for UE 5.9 E2E testing.
        :param python_process: The subprocess.Popen object running the Python AST MCP server.
        :param cpp_url: The Streamable HTTP URL of the UE 5.9 ModelContextProtocol server (/mcp).
        """
        self.cpp_url = cpp_url if cpp_url.endswith("/mcp") else f"{cpp_url.rstrip('/')}/mcp"
        self.endpoint_file = endpoint_file
        self.python_process = python_process
        self.python_reader = None
        self._request_id = 1
        self._mcp_session_id = None
        self._mcp_initialized = False
        self._protocol_version = "2025-11-25"
        self._cached_cpp_tools = None

        if python_process and python_process.stdout:
            self.python_reader = PipeReader(python_process.stdout)

            init_request = {
                "jsonrpc": "2.0",
                "id": self._request_id,
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-11-25",
                    "capabilities": {},
                    "clientInfo": {
                        "name": "MockAgentClient",
                        "version": "2.0.0",
                    },
                },
            }
            self._request_id += 1

            payload = json.dumps(init_request) + "\n"
            python_process.stdin.write(payload)
            python_process.stdin.flush()

            init_response = None
            start_wait = time.time()
            while time.time() - start_wait < 30.0:
                line = self.python_reader.readline(timeout=5.0)
                if line is None:
                    if python_process.poll() is not None:
                        stderr_out = python_process.stderr.read() if python_process.stderr else ""
                        raise RuntimeError(
                            f"Python MCP server exited prematurely with code {python_process.returncode}: {stderr_out}"
                        )
                    continue
                try:
                    data = json.loads(line)
                    if data.get("id") == init_request["id"]:
                        init_response = data
                        break
                except Exception:
                    pass

            if not init_response:
                stderr_out = python_process.stderr.read() if python_process.stderr else ""
                raise RuntimeError(
                    f"Failed to receive initialization response from Python MCP server within 30s. Stderr: {stderr_out}"
                )

            initialized_notification = {
                "jsonrpc": "2.0",
                "method": "notifications/initialized",
            }
            python_process.stdin.write(json.dumps(initialized_notification) + "\n")
            python_process.stdin.flush()

    def _cpp_headers(self, include_session=True):
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if include_session:
            if self._mcp_session_id:
                headers["Mcp-Session-Id"] = self._mcp_session_id
            if self._mcp_initialized or self._mcp_session_id:
                headers["MCP-Protocol-Version"] = self._protocol_version
        return headers

    @staticmethod
    def _parse_mcp_http_response(response):
        if response.status_code in (202, 204) or not response.text.strip():
            if response.status_code >= 400:
                response.raise_for_status()
            return {}

        ctype = (response.headers.get("Content-Type") or "").lower()
        if "text/event-stream" in ctype and response.status_code < 400:
            frames = []
            data_lines = []
            for line in response.text.splitlines():
                if line == "":
                    if data_lines:
                        raw = "\n".join(data_lines).strip()
                        data_lines.clear()
                        if raw and raw != "[DONE]":
                            try:
                                parsed = json.loads(raw)
                                if isinstance(parsed, dict):
                                    frames.append(parsed)
                            except json.JSONDecodeError:
                                pass
                elif line.startswith("data:"):
                    data_lines.append(line[5:].lstrip())
            if data_lines:
                raw = "\n".join(data_lines).strip()
                if raw and raw != "[DONE]":
                    try:
                        parsed = json.loads(raw)
                        if isinstance(parsed, dict):
                            frames.append(parsed)
                    except json.JSONDecodeError:
                        pass
            for frame in reversed(frames):
                if "id" in frame or "result" in frame or "error" in frame:
                    return frame
            return {"error": {"code": -32603, "message": "Empty SSE stream"}}

        try:
            data = response.json()
        except Exception:
            response.raise_for_status()
            raise

        if isinstance(data, dict) and ("error" in data or "result" in data):
            return data
        if response.status_code >= 400:
            response.raise_for_status()
        return data

    def _reset_cpp_session(self):
        self._mcp_session_id = None
        self._mcp_initialized = False
        self._cached_cpp_tools = None

    def _ensure_cpp_mcp_session(self):
        if self._mcp_initialized:
            return
        init_payload = {
            "jsonrpc": "2.0",
            "id": self._request_id,
            "method": "initialize",
            "params": {
                "protocolVersion": self._protocol_version,
                "capabilities": {},
                "clientInfo": {"name": "MockAgentClient", "version": "2.0.0"},
            },
        }
        self._request_id += 1
        resp = requests.post(
            self.cpp_url,
            json=init_payload,
            headers=self._cpp_headers(include_session=False),
            timeout=10.0,
        )
        data = self._parse_mcp_http_response(resp)
        if "error" in data:
            raise RuntimeError(f"UE 5.9 MCP initialize error: {data['error']}")
        result = data.get("result", {})
        if isinstance(result, dict) and result.get("protocolVersion"):
            self._protocol_version = result["protocolVersion"]
        self._mcp_session_id = resp.headers.get("Mcp-Session-Id") or resp.headers.get("mcp-session-id")
        self._mcp_initialized = True
        requests.post(
            self.cpp_url,
            json={"jsonrpc": "2.0", "method": "notifications/initialized"},
            headers=self._cpp_headers(include_session=True),
            timeout=5.0,
        )

    def cpp_request(self, method, params=None, timeout=30.0):
        """Sends an MCP JSON-RPC 2.0 POST to UE 5.9's `/mcp` endpoint with 404 session recovery."""
        for attempt in range(2):
            self._ensure_cpp_mcp_session()
            req_id = self._request_id
            self._request_id += 1
            payload = {
                "jsonrpc": "2.0",
                "id": req_id,
                "method": method,
            }
            if params is not None:
                payload["params"] = params
            response = requests.post(
                self.cpp_url,
                json=payload,
                headers=self._cpp_headers(include_session=True),
                timeout=timeout,
            )
            if response.status_code == 404 and attempt == 0:
                self._reset_cpp_session()
                continue
            return self._parse_mcp_http_response(response)
        raise RuntimeError(f"UE 5.9 MCP request '{method}' failed after session retry.")

    def list_cpp_tools(self):
        if self._cached_cpp_tools is not None:
            return self._cached_cpp_tools
        tools = []
        cursor = None
        seen = set()
        for _ in range(50):
            params = {"cursor": cursor} if cursor else {}
            resp = self.cpp_request("tools/list", params, timeout=15.0)
            result = resp.get("result", {})
            tools.extend(result.get("tools", []))
            next_cursor = result.get("nextCursor")
            if not next_cursor or next_cursor in seen:
                break
            seen.add(next_cursor)
            cursor = next_cursor
        self._cached_cpp_tools = tools
        return tools

    def call_cpp_tool(self, tool_name, arguments=None, timeout=30.0, toolset_name=None):
        """
        Sends an MCP JSON-RPC 2.0 `tools/call` POST to UE 5.9's `/mcp` endpoint.
        Automatically routes Toolset tools through UE 5.9 ToolSearch `call_tool`
        (`toolset_name` + `tool_name` + `arguments`) when ToolSearch is active.
        """
        top_level_names = {t.get("name") for t in self.list_cpp_tools() if isinstance(t, dict)}
        if "call_tool" in top_level_names and tool_name not in top_level_names:
            inner_tool = tool_name
            resolved_toolset = toolset_name
            if resolved_toolset is None and "." in tool_name:
                resolved_toolset, _, inner_tool = tool_name.rpartition(".")
            call_args = {
                "tool_name": inner_tool,
                "arguments": arguments or {},
            }
            if resolved_toolset:
                call_args["toolset_name"] = resolved_toolset
            return self.cpp_request(
                "tools/call",
                {"name": "call_tool", "arguments": call_args},
                timeout=timeout,
            )
        return self.cpp_request(
            "tools/call",
            {"name": tool_name, "arguments": arguments or {}},
            timeout=timeout,
        )

    def close_cpp_session(self):
        if self._mcp_session_id:
            try:
                requests.delete(
                    self.cpp_url,
                    headers=self._cpp_headers(include_session=True),
                    timeout=2.0,
                )
            except Exception:
                pass
        self._reset_cpp_session()

    def call_python_tool(self, tool_name, arguments):
        """
        Sends a JSON-RPC 2.0 request over stdin to the Python AST MCP server.
        """
        if not self.python_process or not self.python_process.stdin or not self.python_reader:
            raise RuntimeError("Python MCP subprocess not initialized or pipes not available.")

        req_id = self._request_id
        self._request_id += 1

        request = {
            "jsonrpc": "2.0",
            "method": "tools/call",
            "params": {
                "name": tool_name,
                "arguments": arguments,
            },
            "id": req_id,
        }

        payload = json.dumps(request) + "\n"
        self.python_process.stdin.write(payload)
        self.python_process.stdin.flush()

        response_line = self.python_reader.readline(timeout=120.0)
        if response_line is None:
            raise TimeoutError("Timed out waiting for response from Python MCP server.")

        return json.loads(response_line)
