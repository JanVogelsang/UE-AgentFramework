import http.server
import json
import os
import queue
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

import pytest

PROJECT_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_ROOT))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from mock_client import MockAgentClient

CACHE_FILE = PROJECT_ROOT / "UnrealEngine" / "profiles" / "discovered_tools_cache.json"
BACKUP_FILE = PROJECT_ROOT / "UnrealEngine" / "profiles" / "discovered_tools_cache.json.bak"


def _free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class _TimedLineReader:
    def __init__(self, stream):
        self._stream = stream
        self._queue: queue.Queue[str | None] = queue.Queue()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        try:
            for line in iter(self._stream.readline, ""):
                if line:
                    self._queue.put(line)
        finally:
            self._queue.put(None)

    def readline(self, timeout: float = 5.0) -> str:
        try:
            item = self._queue.get(timeout=timeout)
        except queue.Empty as exc:
            raise TimeoutError(f"Timed out waiting {timeout}s for bridge stdout line") from exc
        if item is None:
            raise EOFError("Bridge closed stdout unexpectedly")
        return item

    def read_until_id(self, expected_id, timeout: float = 5.0) -> tuple[dict, list[dict]]:
        notifications: list[dict] = []
        deadline = time.monotonic() + timeout
        while True:
            remaining = max(0.05, deadline - time.monotonic())
            if time.monotonic() > deadline:
                raise TimeoutError(f"Timed out waiting for response id={expected_id}")
            msg = json.loads(self.readline(timeout=remaining))
            if msg.get("id") == expected_id:
                return msg, notifications
            notifications.append(msg)


@pytest.fixture(autouse=True)
def setup_cache_backup():
    if BACKUP_FILE.exists():
        try:
            BACKUP_FILE.unlink()
        except OSError:
            pass

    backup_made = False
    if CACHE_FILE.exists():
        try:
            CACHE_FILE.rename(BACKUP_FILE)
            backup_made = True
        except OSError:
            try:
                CACHE_FILE.unlink()
            except OSError:
                pass

    if CACHE_FILE.exists():
        try:
            CACHE_FILE.unlink()
        except OSError:
            pass

    yield

    if CACHE_FILE.exists():
        try:
            CACHE_FILE.unlink()
        except OSError:
            pass
    if backup_made and BACKUP_FILE.exists():
        try:
            BACKUP_FILE.rename(CACHE_FILE)
        except OSError:
            pass
    elif BACKUP_FILE.exists():
        try:
            BACKUP_FILE.unlink()
        except OSError:
            pass


def _spawn_bridge(port: int, poll_interval: float = 10.0, profile: str | None = None):
    cmd = [sys.executable, "-u", str(PROJECT_ROOT / "UnrealEngine" / "bridge" / "main.py")]
    env = {
        **os.environ,
        "UE_MCP_PORT": str(port),
        "BRIDGE_POLL_INTERVAL": str(poll_interval),
    }
    if profile:
        env["BRIDGE_PROFILE"] = profile
    else:
        env.pop("BRIDGE_PROFILE", None)
    return subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
        env=env,
    )


def _send(proc, payload: dict) -> None:
    proc.stdin.write(json.dumps(payload) + "\n")
    proc.stdin.flush()


def test_bridge_caching_and_fallback():
    offline_port = _free_port()
    proc = _spawn_bridge(offline_port, poll_interval=0.4)
    reader = _TimedLineReader(proc.stdout)

    try:
        _send(
            proc,
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {
                    "protocolVersion": "2025-11-25",
                    "capabilities": {},
                    "clientInfo": {"name": "TestClient"},
                },
            },
        )

        init_resp = json.loads(reader.readline(timeout=5.0))
        assert init_resp.get("id") == 1
        capabilities = init_resp.get("result", {}).get("capabilities", {})
        assert capabilities.get("tools", {}).get("listChanged") is True

        _send(proc, {"jsonrpc": "2.0", "method": "notifications/initialized"})
        _send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})

        list_resp, _ = reader.read_until_id(2, timeout=5.0)
        tools = list_resp.get("result", {}).get("tools", [])
        assert len(tools) == 0, f"Expected 0 tools when offline with no cache, got: {tools}"

        mock_cache = {
            "tools": [
                {
                    "name": "mock_test_tool",
                    "description": "Mocked UE 5.9 bridge tool",
                    "inputSchema": {"type": "object", "properties": {}},
                }
            ],
            "tool_owners": {
                "mock_test_tool": "native",
            },
        }
        with open(CACHE_FILE, "w", encoding="utf-8") as f:
            json.dump(mock_cache, f)

        got_list_changed = False
        deadline = time.monotonic() + 8.0
        while time.monotonic() < deadline:
            msg = json.loads(reader.readline(timeout=max(0.5, deadline - time.monotonic())))
            if msg.get("method") == "notifications/tools/list_changed":
                got_list_changed = True
                break

        assert got_list_changed is True, "Should have received notifications/tools/list_changed after cache file was created"

        _send(proc, {"jsonrpc": "2.0", "id": 3, "method": "tools/list"})
        list_resp_2, _ = reader.read_until_id(3, timeout=5.0)
        tools_2 = list_resp_2.get("result", {}).get("tools", [])
        assert len(tools_2) == 1
        assert tools_2[0]["name"] == "mock_test_tool"
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()


def test_bridge_ue59_streamable_http_session_and_call():
    """Verifies /mcp Mcp-Session-Id handshake, instructions/serverInfo forwarding, 404 session recovery, and DELETE cleanup."""
    state = {"count": 0, "expire_next": False, "deleted_sessions": []}

    class MockUE59MCPHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def do_DELETE(self):
            if self.path == "/mcp":
                sid = self.headers.get("Mcp-Session-Id")
                if sid:
                    state["deleted_sessions"].append(sid)
                self.send_response(202)
                self.end_headers()
                return
            self.send_response(404)
            self.end_headers()

        def do_POST(self):
            if self.path != "/mcp":
                self.send_response(404)
                self.end_headers()
                return

            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            method = body.get("method")
            req_id = body.get("id")

            if method == "initialize":
                state["count"] += 1
                sid = f"ue59-session-{state['count']}"
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "protocolVersion": "2025-11-25",
                        "capabilities": {"tools": {"listChanged": True}},
                        "serverInfo": {"name": "UnrealEngine59Mock", "version": "5.9.0"},
                        "instructions": "Use list_toolsets and call_tool to discover UE 5.9 tools.",
                    },
                }
                payload = json.dumps(resp).encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Mcp-Session-Id", sid)
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
                return

            if method == "notifications/initialized":
                self.send_response(202)
                self.end_headers()
                return

            if state["expire_next"]:
                state["expire_next"] = False
                self.send_response(404)
                self.end_headers()
                return

            expected_sid = f"ue59-session-{state['count']}"
            if self.headers.get("Mcp-Session-Id") != expected_sid:
                self.send_response(404)
                self.end_headers()
                return

            if method == "tools/list":
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "tools": [
                            {
                                "name": "BlueprintDslToolset_Read",
                                "description": "Read Blueprint DSL",
                                "inputSchema": {"type": "object", "properties": {"AssetPath": {"type": "string"}}},
                            },
                            {
                                "name": "AgentFrameworkCppToolset_ScaffoldCppClass",
                                "description": "Scaffold C++ class",
                                "inputSchema": {"type": "object", "properties": {"ClassName": {"type": "string"}}},
                            },
                        ]
                    },
                }
            elif method == "tools/call":
                params = body.get("params", {})
                tname = params.get("name")
                targs = params.get("arguments", {})
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "content": [{"type": "text", "text": f"Executed {tname} with {json.dumps(targs)}"}],
                        "isError": False,
                    },
                }
            else:
                resp = {"jsonrpc": "2.0", "id": req_id, "result": {}}

            payload = json.dumps(resp).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), MockUE59MCPHandler)
    port = server.server_port
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()

    proc = _spawn_bridge(port, poll_interval=10.0)
    reader = _TimedLineReader(proc.stdout)

    try:
        _send(
            proc,
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "Test"}},
            },
        )
        init_resp = json.loads(reader.readline(timeout=5.0))
        assert init_resp["id"] == 1
        assert state["count"] == 1
        assert init_resp["result"]["serverInfo"]["name"] == "UnrealEngine59Mock"
        assert "ToolSearch" not in ""  # check forwarded UE 5.9 instructions verbatim
        assert init_resp["result"]["instructions"] == "Use list_toolsets and call_tool to discover UE 5.9 tools."

        _send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        list_resp, _ = reader.read_until_id(2, timeout=5.0)
        tools = [t["name"] for t in list_resp["result"]["tools"]]
        assert "BlueprintDslToolset_Read" in tools
        assert "AgentFrameworkCppToolset_ScaffoldCppClass" in tools

        # Simulate Editor restart -> HTTP 404 triggers re-initialize once
        state["expire_next"] = True
        _send(
            proc,
            {
                "jsonrpc": "2.0",
                "id": 3,
                "method": "tools/call",
                "params": {
                    "name": "BlueprintDslToolset_Read",
                    "arguments": {"AssetPath": "/Game/BP_Test"},
                },
            },
        )
        call_resp, _ = reader.read_until_id(3, timeout=5.0)
        assert call_resp["id"] == 3
        assert call_resp["result"]["isError"] is False
        assert "BlueprintDslToolset_Read" in call_resp["result"]["content"][0]["text"]
        assert state["count"] == 2

        # Clean stdin EOF should trigger DELETE /mcp for active session
        proc.stdin.close()
        proc.wait(timeout=5)
        assert "ue59-session-2" in state["deleted_sessions"]
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        server.shutdown()
        server.server_close()


def test_bridge_http400_sse_pagination_resources_and_cancellation():
    """Covers UE 5.9 HTTP 400 JSON-RPC error parsing, SSE progress streaming, _meta, pagination, resources, image filtering, and notifications/cancelled."""
    recorded = {"cancelled": [], "meta": [], "no_stateless_sid": True}

    class RichMockUE59Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def do_DELETE(self):
            self.send_response(202)
            self.end_headers()

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            method = body.get("method")
            req_id = body.get("id")
            params = body.get("params", {})

            if self.headers.get("Mcp-Session-Id") == "ue59-stateless":
                recorded["no_stateless_sid"] = False

            if method == "initialize":
                resp = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "protocolVersion": "2025-11-25",
                        "capabilities": {"tools": {"listChanged": True}, "resources": {"listChanged": False}},
                        "serverInfo": {"name": "UE59RichMock", "version": "5.9.0"},
                        "instructions": "Paginated UE 5.9 server",
                    },
                }
                self._write_json(200, resp, headers={"Mcp-Session-Id": "rich-sid-1"})
                return

            if method == "notifications/initialized":
                self.send_response(202)
                self.end_headers()
                return

            if method == "notifications/cancelled":
                recorded["cancelled"].append(params.get("requestId"))
                self.send_response(202)
                self.end_headers()
                return

            if method == "tools/list":
                cursor = params.get("cursor")
                if not cursor:
                    result = {
                        "tools": [
                            {"name": "ToolA", "description": "A", "inputSchema": {"type": "object", "properties": {}}},
                            {"name": "ToolErr", "description": "Err", "inputSchema": {"type": "object", "properties": {}}},
                        ],
                        "nextCursor": "page-2",
                    }
                else:
                    result = {
                        "tools": [
                            {"name": "ToolImg", "description": "Img", "inputSchema": {"type": "object", "properties": {}}},
                        ]
                    }
                self._write_json(200, {"jsonrpc": "2.0", "id": req_id, "result": result})
                return

            if method == "resources/list":
                cursor = params.get("cursor")
                if not cursor:
                    result = {
                        "resources": [{"uri": "ue://doc/1", "name": "Doc1"}],
                        "nextCursor": "res-page-2",
                    }
                else:
                    result = {"resources": [{"uri": "ue://doc/2", "name": "Doc2"}]}
                self._write_json(200, {"jsonrpc": "2.0", "id": req_id, "result": result})
                return

            if method == "resources/read":
                uri = params.get("uri")
                if uri == "ue://missing":
                    self._write_json(
                        400,
                        {"jsonrpc": "2.0", "id": req_id, "error": {"code": -32002, "message": "Resource not found: ue://missing"}},
                    )
                    return
                self._write_json(
                    200,
                    {"jsonrpc": "2.0", "id": req_id, "result": {"contents": [{"uri": uri, "text": "doc body"}]}},
                )
                return

            if method == "tools/call":
                name = params.get("name")
                if "_meta" in params:
                    recorded["meta"].append(params["_meta"])
                if name == "ToolErr":
                    # UE 5.9 returns HTTP 400 BadRequest with JSON-RPC error payload
                    self._write_json(
                        400,
                        {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "error": {"code": -32602, "message": "Execution blocked by modal dialog"},
                        },
                    )
                    return
                if name == "ToolA":
                    # Stream SSE progress + final result
                    progress_frame = json.dumps(
                        {
                            "jsonrpc": "2.0",
                            "method": "notifications/progress",
                            "params": {"progressToken": "tok-42", "progress": 50, "total": 100},
                        }
                    )
                    final_frame = json.dumps(
                        {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {"content": [{"type": "text", "text": "sse-ok"}], "isError": False},
                        }
                    )
                    sse_body = (
                        f"event: message\r\ndata: {progress_frame}\r\n\r\n"
                        f"event: message\r\ndata: {final_frame}\r\n\r\n"
                    ).encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/event-stream")
                    self.send_header("Content-Length", str(len(sse_body)))
                    self.end_headers()
                    self.wfile.write(sse_body)
                    self.wfile.flush()
                    return
                if name == "ToolImg":
                    self._write_json(
                        200,
                        {
                            "jsonrpc": "2.0",
                            "id": req_id,
                            "result": {
                                "content": [
                                    {"type": "text", "text": "caption"},
                                    {"type": "image", "data": "AAAA", "mimeType": "image/png"},
                                ],
                                "isError": False,
                            },
                        },
                    )
                    return

            self._write_json(400, {"jsonrpc": "2.0", "id": req_id, "error": {"code": -32601, "message": "Unknown"}})

        def _write_json(self, status: int, obj: dict, headers: dict | None = None):
            payload = json.dumps(obj).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            for k, v in (headers or {}).items():
                self.send_header(k, v)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), RichMockUE59Handler)
    port = server.server_port
    threading.Thread(target=server.serve_forever, daemon=True).start()

    # Use default profile -> image_support=false
    proc = _spawn_bridge(port, poll_interval=10.0, profile="default")
    reader = _TimedLineReader(proc.stdout)

    try:
        _send(
            proc,
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "RichTest"}},
            },
        )
        init_resp = json.loads(reader.readline(timeout=5.0))
        assert init_resp["result"]["instructions"] == "Paginated UE 5.9 server"

        # Paginated tools/list
        _send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        list_resp, _ = reader.read_until_id(2, timeout=5.0)
        names = [t["name"] for t in list_resp["result"]["tools"]]
        assert names == ["ToolA", "ToolErr", "ToolImg"]

        # HTTP 400 JSON-RPC error propagation
        _send(proc, {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "ToolErr", "arguments": {}}})
        err_resp, _ = reader.read_until_id(3, timeout=5.0)
        assert err_resp["result"]["isError"] is True
        assert "Execution blocked by modal dialog" in err_resp["result"]["content"][0]["text"]
        assert "not connected" not in err_resp["result"]["content"][0]["text"]

        # SSE response + _meta.progressToken forwarding
        _send(
            proc,
            {
                "jsonrpc": "2.0",
                "id": 4,
                "method": "tools/call",
                "params": {"name": "ToolA", "arguments": {}, "_meta": {"progressToken": "tok-42"}},
            },
        )
        sse_resp, sse_notifs = reader.read_until_id(4, timeout=5.0)
        assert sse_resp["result"]["content"][0]["text"] == "sse-ok"
        assert recorded["meta"] == [{"progressToken": "tok-42"}]
        assert any(n.get("method") == "notifications/progress" and n["params"]["progressToken"] == "tok-42" for n in sse_notifs)

        # image_support=false sanitization
        _send(proc, {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "ToolImg", "arguments": {}}})
        img_resp, _ = reader.read_until_id(5, timeout=5.0)
        assert img_resp["result"]["content"][0] == {"type": "text", "text": "caption"}
        assert img_resp["result"]["content"][1]["type"] == "text"
        assert "image_support=false" in img_resp["result"]["content"][1]["text"]

        # Paginated resources/list & resources/read + error propagation
        _send(proc, {"jsonrpc": "2.0", "id": 6, "method": "resources/list"})
        res_list, _ = reader.read_until_id(6, timeout=5.0)
        assert [r["uri"] for r in res_list["result"]["resources"]] == ["ue://doc/1", "ue://doc/2"]

        _send(proc, {"jsonrpc": "2.0", "id": 7, "method": "resources/read", "params": {"uri": "ue://missing"}})
        res_err, _ = reader.read_until_id(7, timeout=5.0)
        assert res_err["error"]["code"] == -32002
        assert "ue://missing" in res_err["error"]["message"]

        # notifications/cancelled forwarding + ping sync check
        _send(proc, {"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 999}})
        _send(proc, {"jsonrpc": "2.0", "id": 8, "method": "ping"})
        ping_resp, _ = reader.read_until_id(8, timeout=5.0)
        assert ping_resp["result"] == {}

        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline and 999 not in recorded["cancelled"]:
            time.sleep(0.05)
        assert 999 in recorded["cancelled"]
        assert recorded["no_stateless_sid"] is True
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        server.shutdown()
        server.server_close()


def test_mock_client_ue59_http400_sse_and_404_recovery():
    """Verifies MockAgentClient parses HTTP 400 JSON-RPC errors, SSE streams, ToolSearch call_tool routing, and 404 session recovery."""
    state = {"session": 0, "expire_once": False}

    class MockServerHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def do_DELETE(self):
            self.send_response(202)
            self.end_headers()

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            method = body.get("method")
            req_id = body.get("id")
            params = body.get("params", {})

            if method == "initialize":
                state["session"] += 1
                self._send_json(
                    200,
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "result": {"protocolVersion": "2025-11-25", "capabilities": {}, "serverInfo": {"name": "Mock", "version": "1.0"}},
                    },
                    sid=f"sid-{state['session']}",
                )
                return

            if method == "notifications/initialized":
                self.send_response(202)
                self.end_headers()
                return

            if state["expire_once"]:
                state["expire_once"] = False
                self.send_response(404)
                self.end_headers()
                return

            if method == "tools/list":
                self._send_json(
                    200,
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "result": {"tools": [{"name": "call_tool", "description": "ToolSearch dispatcher"}]},
                    },
                )
                return

            if method == "tools/call":
                # UE 5.9 ToolSearch routes through call_tool(toolset_name, tool_name, arguments)
                inner_name = (
                    params.get("arguments", {}).get("tool_name")
                    if params.get("name") == "call_tool"
                    else params.get("name")
                )
                if inner_name == "ErrTool":
                    self._send_json(
                        400,
                        {"jsonrpc": "2.0", "id": req_id, "error": {"code": -32602, "message": "Bad params"}},
                    )
                    return
                if inner_name == "SseTool":
                    frame = json.dumps({"jsonrpc": "2.0", "id": req_id, "result": {"content": [{"type": "text", "text": "from-sse"}], "isError": False}})
                    body_bytes = f"event: message\r\ndata: {frame}\r\n\r\n".encode("utf-8")
                    self.send_response(200)
                    self.send_header("Content-Type", "text/event-stream")
                    self.send_header("Content-Length", str(len(body_bytes)))
                    self.end_headers()
                    self.wfile.write(body_bytes)
                    return

            self._send_json(200, {"jsonrpc": "2.0", "id": req_id, "result": {"content": [], "isError": False}})

        def _send_json(self, code, data, sid=None):
            raw = json.dumps(data).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            if sid:
                self.send_header("Mcp-Session-Id", sid)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), MockServerHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    client = MockAgentClient(cpp_url=f"http://127.0.0.1:{server.server_port}/mcp")
    try:
        err = client.call_cpp_tool("AgentFrameworkActions.AgentFrameworkCppToolset.ErrTool", {})
        assert err["error"]["message"] == "Bad params"

        state["expire_once"] = True
        sse = client.call_cpp_tool("AgentFrameworkActions.AgentFrameworkCppToolset.SseTool", {})
        assert sse["result"]["content"][0]["text"] == "from-sse"
        assert state["session"] == 2
        client.close_cpp_session()
    finally:
        server.shutdown()
        server.server_close()


def test_bridge_inflight_cancellation_translates_request_id():
    """Verifies notifications/cancelled translates stdio requestId to UE 5.9's active upstream JSON-RPC id."""
    slow_started = threading.Event()
    cancel_released = threading.Event()
    recorded = {"upstream_slow_id": None, "cancelled_ids": []}

    class CancellableMCPHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def do_DELETE(self):
            self.send_response(202)
            self.end_headers()

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            method = body.get("method")
            req_id = body.get("id")
            params = body.get("params", {})

            if method == "initialize":
                self._send_json(
                    200,
                    {"jsonrpc": "2.0", "id": req_id, "result": {"protocolVersion": "2025-11-25", "capabilities": {}}},
                    sid="cancel-sid-1",
                )
                return

            if method == "notifications/initialized":
                self.send_response(202)
                self.end_headers()
                return

            if method == "notifications/cancelled":
                cancelled_id = params.get("requestId")
                recorded["cancelled_ids"].append(cancelled_id)
                if cancelled_id == recorded["upstream_slow_id"]:
                    cancel_released.set()
                self.send_response(202)
                self.end_headers()
                return

            if method == "tools/list":
                self._send_json(
                    200,
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "result": {
                            "tools": [
                                {"name": "SlowTool", "description": "Blocks until cancelled", "inputSchema": {"type": "object", "properties": {}}}
                            ]
                        },
                    },
                )
                return

            if method == "tools/call":
                recorded["upstream_slow_id"] = req_id
                slow_started.set()
                was_cancelled = cancel_released.wait(timeout=3.0)
                if was_cancelled:
                    self._send_json(
                        400,
                        {"jsonrpc": "2.0", "id": req_id, "error": {"code": -32800, "message": "Request cancelled"}},
                    )
                else:
                    self._send_json(
                        200,
                        {"jsonrpc": "2.0", "id": req_id, "result": {"content": [{"type": "text", "text": "completed"}], "isError": False}},
                    )

        def _send_json(self, code, data, sid=None):
            raw = json.dumps(data).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            if sid:
                self.send_header("Mcp-Session-Id", sid)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), CancellableMCPHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    proc = _spawn_bridge(server.server_port, poll_interval=10.0)
    reader = _TimedLineReader(proc.stdout)

    try:
        _send(
            proc,
            {
                "jsonrpc": "2.0",
                "id": 1,
                "method": "initialize",
                "params": {"protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "CancelTest"}},
            },
        )
        assert json.loads(reader.readline(timeout=5.0))["id"] == 1

        _send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        list_resp, _ = reader.read_until_id(2, timeout=5.0)
        assert [t["name"] for t in list_resp["result"]["tools"]] == ["SlowTool"]

        _send(proc, {"jsonrpc": "2.0", "id": 77, "method": "tools/call", "params": {"name": "SlowTool", "arguments": {}}})
        assert slow_started.wait(timeout=3.0), "SlowTool did not start in flight"

        _send(proc, {"jsonrpc": "2.0", "method": "notifications/cancelled", "params": {"requestId": 77}})
        slow_resp, _ = reader.read_until_id(77, timeout=2.0)

        assert slow_resp["result"]["isError"] is True
        assert "Request cancelled" in slow_resp["result"]["content"][0]["text"]
        assert recorded["upstream_slow_id"] is not None
        assert recorded["upstream_slow_id"] != 77
        assert recorded["cancelled_ids"] == [recorded["upstream_slow_id"]]
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        server.shutdown()
        server.server_close()
