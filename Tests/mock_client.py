import json
import time
import requests
import queue
import threading

class PipeReader:
    def __init__(self, stream):
        self.stream = stream
        self.queue = queue.Queue()
        self.thread = threading.Thread(target=self._read_loop, daemon=True)
        self.thread.start()

    def _read_loop(self):
        try:
            for line in iter(self.stream.readline, ''):
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
    def __init__(self, python_process=None, cpp_url="http://127.0.0.1:18777"):
        """
        Mock Agent Client for E2E testing.
        :param python_process: The subprocess.Popen object running the Python MCP server.
        :param cpp_url: The base HTTP URL of the C++ MCP server in Unreal Engine.
        """
        self.cpp_url = cpp_url
        self.python_process = python_process
        self.python_reader = None
        self._request_id = 1
        
        if python_process and python_process.stdout:
            self.python_reader = PipeReader(python_process.stdout)
            
            # Perform MCP handshake
            init_request = {
                "jsonrpc": "2.0",
                "id": self._request_id,
                "method": "initialize",
                "params": {
                    "protocolVersion": "2024-11-05",
                    "capabilities": {},
                    "clientInfo": {
                        "name": "MockAgentClient",
                        "version": "1.0.0"
                    }
                }
            }
            self._request_id += 1
            
            payload = json.dumps(init_request) + "\n"
            python_process.stdin.write(payload)
            python_process.stdin.flush()
            
            # Read initialization response line-by-line
            init_response = None
            start_wait = time.time()
            while time.time() - start_wait < 30.0:
                line = self.python_reader.readline(timeout=5.0)
                if line is None:
                    if python_process.poll() is not None:
                        stderr_out = python_process.stderr.read() if python_process.stderr else ""
                        raise RuntimeError(f"Python MCP server exited prematurely with code {python_process.returncode}: {stderr_out}")
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
                raise RuntimeError(f"Failed to receive initialization response from Python MCP server within 30s. Stderr: {stderr_out}")
                
            initialized_notification = {
                "jsonrpc": "2.0",
                "method": "notifications/initialized"
            }
            payload = json.dumps(initialized_notification) + "\n"
            python_process.stdin.write(payload)
            python_process.stdin.flush()

    def call_cpp_tool(self, tool_name, arguments):
        """
        Sends an HTTP POST to /api/execute_tool on the C++ internal server.
        """
        url = f"{self.cpp_url}/api/execute_tool"
        payload = {
            "name": tool_name,
            "arguments": arguments
        }
        response = requests.post(url, json=payload, timeout=30.0)
        response.raise_for_status()
        return response.json()

    def call_python_tool(self, tool_name, arguments):
        """
        Sends a JSON-RPC 2.0 request over stdin to the Python MCP server.
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
                "arguments": arguments
            },
            "id": req_id
        }

        # Write request to stdin
        payload = json.dumps(request) + "\n"
        self.python_process.stdin.write(payload)
        self.python_process.stdin.flush()

        # Read response from reader
        response_line = self.python_reader.readline(timeout=120.0)
        if response_line is None:
            raise TimeoutError("Timed out waiting for response from Python MCP server.")

        return json.loads(response_line)
