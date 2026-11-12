import http.server
import json
import sys
import threading
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent.parent / "UnrealEngine" / "src" / "scripts"
sys.path.insert(0, str(SCRIPTS_DIR))

import editor_endpoint


def test_ue59_mcp_client_direct_and_toolsearch_and_errors(monkeypatch):
    recorded = {"calls": [], "deleted": False}

    class MockHandler(http.server.BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def do_DELETE(self):
            recorded["deleted"] = True
            self.send_response(202)
            self.end_headers()

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            body = json.loads(self.rfile.read(length).decode("utf-8"))
            method = body.get("method")
            req_id = body.get("id")
            params = body.get("params", {})

            if method == "initialize":
                self._write(200, {"jsonrpc": "2.0", "id": req_id, "result": {"protocolVersion": "2025-11-25"}}, sid="ep-1")
                return
            if method == "notifications/initialized":
                self.send_response(202)
                self.end_headers()
                return
            if method == "tools/list":
                self._write(
                    200,
                    {
                        "jsonrpc": "2.0",
                        "id": req_id,
                        "result": {"tools": [{"name": "call_tool", "description": "dispatch"}]},
                    },
                )
                return
            if method == "tools/call":
                recorded["calls"].append(params)
                if params.get("arguments", {}).get("tool_name") == "FailTool":
                    self._write(
                        400,
                        {"jsonrpc": "2.0", "id": req_id, "error": {"code": -32602, "message": "HTTP 400 JSON-RPC err"}},
                    )
                    return
                self._write(
                    200,
                    {"jsonrpc": "2.0", "id": req_id, "result": {"content": [{"type": "text", "text": "ok"}], "isError": False}},
                )
                return

        def _write(self, status, data, sid=None):
            raw = json.dumps(data).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            if sid:
                self.send_header("Mcp-Session-Id", sid)
            self.send_header("Content-Length", str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), MockHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    monkeypatch.setenv("UE_MCP_PORT", str(server.server_port))

    client = editor_endpoint.UE59McpClient()
    try:
        assert editor_endpoint.mcp_port() == server.server_port
        ok = client.call_tool("AgentFrameworkActions.AgentFrameworkCppToolset.ScaffoldCppClass", {"ClassName": "AMyActor"})
        assert ok["result"]["isError"] is False
        assert recorded["calls"][0]["name"] == "call_tool"
        assert recorded["calls"][0]["arguments"]["toolset_name"] == "AgentFrameworkActions.AgentFrameworkCppToolset"
        assert recorded["calls"][0]["arguments"]["tool_name"] == "ScaffoldCppClass"

        err = client.call_tool("FailTool", {})
        assert err["error"]["message"] == "HTTP 400 JSON-RPC err"
        client.close()
        assert recorded["deleted"] is True
    finally:
        server.shutdown()
        server.server_close()
