"""Minimal stdlib helper for calling UE 5.9's built-in Streamable HTTP /mcp endpoint.

Resolves ``UE_MCP_URL`` or ``UE_MCP_PORT`` / ``BRIDGE_HTTP_PORT`` (defaulting to
``8000``) and implements the UE 5.9 MCP handshake (``initialize``,
``Mcp-Session-Id``, ``MCP-Protocol-Version``, ``notifications/initialized``),
JSON-RPC HTTP 4xx error parsing, ``text/event-stream`` response decoding, and
``ToolSearch`` ``call_tool`` fallback vs direct tool invocation.
"""

import json
import os
import urllib.error
import urllib.request
from typing import Any, Dict, List, Optional

DEFAULT_UE_MCP_PORT = 8000
UE_MCP_PROTOCOL_VERSION = "2025-11-25"


def mcp_port() -> int:
    """Return the UE 5.9 ModelContextProtocol HTTP port."""
    raw = os.environ.get("UE_MCP_PORT") or os.environ.get("BRIDGE_HTTP_PORT") or DEFAULT_UE_MCP_PORT
    try:
        port = int(raw)
    except (TypeError, ValueError):
        return DEFAULT_UE_MCP_PORT
    if not 1024 <= port <= 65535:
        return DEFAULT_UE_MCP_PORT
    return port


def mcp_url() -> str:
    """Return the full UE 5.9 Streamable HTTP /mcp URL."""
    explicit = os.environ.get("UE_MCP_URL")
    if explicit:
        return explicit
    return f"http://127.0.0.1:{mcp_port()}/mcp"


def _parse_sse_frames(raw_text: str) -> List[Dict[str, Any]]:
    frames: List[Dict[str, Any]] = []
    data_lines: List[str] = []
    for line in raw_text.splitlines():
        if line == "":
            if data_lines:
                payload = "\n".join(data_lines).strip()
                data_lines.clear()
                if payload and payload != "[DONE]":
                    try:
                        parsed = json.loads(payload)
                        if isinstance(parsed, dict):
                            frames.append(parsed)
                    except json.JSONDecodeError:
                        pass
        elif line.startswith("data:"):
            data_lines.append(line[5:].lstrip())
    if data_lines:
        payload = "\n".join(data_lines).strip()
        if payload and payload != "[DONE]":
            try:
                parsed = json.loads(payload)
                if isinstance(parsed, dict):
                    frames.append(parsed)
            except json.JSONDecodeError:
                pass
    return frames


def _post_jsonrpc(
    url: str,
    payload: Dict[str, Any],
    headers: Dict[str, str],
    timeout: float = 30.0,
) -> tuple[int, Any, Dict[str, Any]]:
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode("utf-8"),
        headers=headers,
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            status = resp.status
            resp_headers = resp.headers
            raw_body = resp.read().decode("utf-8", errors="replace")
            content_type = (resp.headers.get("Content-Type") or "").lower()
    except urllib.error.HTTPError as exc:
        status = exc.code
        resp_headers = exc.headers
        raw_body = exc.read().decode("utf-8", errors="replace") if exc.fp else ""
        content_type = (exc.headers.get("Content-Type") or "").lower() if exc.headers else ""

    if not raw_body.strip():
        return status, resp_headers, {}

    if "text/event-stream" in content_type:
        frames = _parse_sse_frames(raw_body)
        for frame in reversed(frames):
            if "id" in frame or "result" in frame or "error" in frame:
                return status, resp_headers, frame
        return status, resp_headers, {"error": {"code": -32603, "message": "Empty SSE response"}}

    try:
        return status, resp_headers, json.loads(raw_body)
    except json.JSONDecodeError:
        return status, resp_headers, {"error": {"code": -32700, "message": raw_body[:300]}}


class UE59McpClient:
    """Minimal synchronous client for UE 5.9's built-in /mcp Streamable HTTP endpoint."""

    def __init__(self, url: Optional[str] = None, timeout: float = 30.0) -> None:
        self.url = url or mcp_url()
        self.timeout = timeout
        self.session_id: Optional[str] = None
        self.protocol_version = UE_MCP_PROTOCOL_VERSION
        self._initialized = False
        self._req_id = 0

    def _next_id(self) -> int:
        self._req_id += 1
        return self._req_id

    def _headers(self, include_session: bool = True) -> Dict[str, str]:
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if include_session:
            if self.session_id:
                headers["Mcp-Session-Id"] = self.session_id
            if self._initialized or self.session_id:
                headers["MCP-Protocol-Version"] = self.protocol_version
        return headers

    def ensure_session(self) -> None:
        if self._initialized:
            return
        init_payload = {
            "jsonrpc": "2.0",
            "id": self._next_id(),
            "method": "initialize",
            "params": {
                "protocolVersion": self.protocol_version,
                "capabilities": {},
                "clientInfo": {"name": "UE-Antigravity-Script", "version": "2.0.0"},
            },
        }
        status, resp_headers, body = _post_jsonrpc(
            self.url,
            init_payload,
            self._headers(include_session=False),
            timeout=self.timeout,
        )
        if status != 200 or "error" in body:
            raise RuntimeError(f"Failed to initialize UE 5.9 MCP session ({status}): {body}")

        result = body.get("result", {})
        self.protocol_version = result.get("protocolVersion", self.protocol_version)
        self.session_id = resp_headers.get("Mcp-Session-Id") or resp_headers.get("mcp-session-id")
        self._initialized = True

        _post_jsonrpc(
            self.url,
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            self._headers(include_session=True),
            timeout=self.timeout,
        )

    def request(self, method: str, params: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        self.ensure_session()
        payload: Dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": self._next_id(),
            "method": method,
        }
        if params is not None:
            payload["params"] = params

        status, _, body = _post_jsonrpc(
            self.url,
            payload,
            self._headers(include_session=True),
            timeout=self.timeout,
        )
        if status == 404:
            self.session_id = None
            self._initialized = False
            self.ensure_session()
            _, _, body = _post_jsonrpc(
                self.url,
                payload,
                self._headers(include_session=True),
                timeout=self.timeout,
            )
        return body

    def list_tools(self) -> List[Dict[str, Any]]:
        tools: List[Dict[str, Any]] = []
        cursor: Optional[str] = None
        seen = set()
        for _ in range(50):
            params = {"cursor": cursor} if cursor else {}
            resp = self.request("tools/list", params)
            result = resp.get("result", {})
            tools.extend(result.get("tools", []))
            next_cursor = result.get("nextCursor")
            if not next_cursor or next_cursor in seen:
                break
            seen.add(next_cursor)
            cursor = next_cursor
        return tools

    def call_tool(
        self,
        tool_name: str,
        arguments: Optional[Dict[str, Any]] = None,
        toolset_name: Optional[str] = None,
    ) -> Dict[str, Any]:
        """Call a tool directly or via UE 5.9's ToolSearch `call_tool` wrapper when enabled."""
        available = {t.get("name") for t in self.list_tools() if isinstance(t, dict)}
        if tool_name in available or "call_tool" not in available:
            return self.request(
                "tools/call",
                {"name": tool_name, "arguments": arguments or {}},
            )
        inner_tool = tool_name
        resolved_toolset = toolset_name
        if resolved_toolset is None and "." in tool_name:
            resolved_toolset, _, inner_tool = tool_name.rpartition(".")
        call_args: Dict[str, Any] = {
            "tool_name": inner_tool,
            "arguments": arguments or {},
        }
        if resolved_toolset:
            call_args["toolset_name"] = resolved_toolset
        return self.request(
            "tools/call",
            {"name": "call_tool", "arguments": call_args},
        )

    def close(self) -> None:
        if not self.session_id:
            self._initialized = False
            return
        req = urllib.request.Request(
            self.url,
            headers=self._headers(include_session=True),
            method="DELETE",
        )
        try:
            with urllib.request.urlopen(req, timeout=2.0):
                pass
        except Exception:
            pass
        self.session_id = None
        self._initialized = False
