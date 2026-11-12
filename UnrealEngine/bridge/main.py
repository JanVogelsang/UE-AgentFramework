import asyncio
import hashlib
import json
import os
import sys
from pathlib import Path
from typing import Any, Awaitable, Callable, Dict, List, Optional

import httpx
from httpx_sse import EventSource

PROFILE_DIR = Path(__file__).resolve().parent.parent / "profiles"
CACHE_PATH = PROFILE_DIR / "discovered_tools_cache.json"
UE_MCP_PROTOCOL_VERSION = "2025-11-25"
POLL_INTERVAL = float(os.environ.get("BRIDGE_POLL_INTERVAL", "2.0"))
MAX_POLL_INTERVAL = 30.0

discover_lock = asyncio.Lock()
last_tools_hash = ""
tool_owners: Dict[str, str] = {}
_last_discover_online = False


def load_profile(client_name: str = "") -> Dict[str, Any]:
    profile_name = os.environ.get("BRIDGE_PROFILE")
    if profile_name:
        path = PROFILE_DIR / f"{profile_name}.json"
        if path.exists():
            with open(path, encoding="utf-8") as f:
                return json.load(f)

    if client_name:
        path = PROFILE_DIR / f"{client_name.lower()}.json"
        if path.exists():
            with open(path, encoding="utf-8") as f:
                return json.load(f)

    path = PROFILE_DIR / "default.json"
    if path.exists():
        with open(path, encoding="utf-8") as f:
            return json.load(f)

    return {}


def resolve_mcp_url(profile: Dict[str, Any]) -> str:
    explicit_url = os.environ.get("UE_MCP_URL")
    if explicit_url:
        return explicit_url
    port = os.environ.get("UE_MCP_PORT") or os.environ.get("BRIDGE_HTTP_PORT") or profile.get("ue_native_mcp_port", 8000)
    return f"http://127.0.0.1:{int(port)}/mcp"


class UE59StreamableHttpClient:
    """Client for Unreal Engine 5.9's built-in ModelContextProtocol Streamable HTTP server (/mcp)."""

    def __init__(
        self,
        url: str,
        protocol_version: str = UE_MCP_PROTOCOL_VERSION,
        on_notification: Optional[Callable[[Dict[str, Any]], Awaitable[None]]] = None,
    ) -> None:
        self.url = url
        self.session_id: Optional[str] = None
        self._initialized: bool = False
        self.requested_version: str = protocol_version or UE_MCP_PROTOCOL_VERSION
        self.negotiated_version: str = self.requested_version
        self.server_instructions: Optional[str] = None
        self.server_info: Optional[Dict[str, Any]] = None
        self.server_capabilities: Optional[Dict[str, Any]] = None
        self._on_notification = on_notification
        self._req_id: int = 0
        self._lock = asyncio.Lock()
        self._client: Optional[httpx.AsyncClient] = None

    def _next_id(self) -> int:
        self._req_id += 1
        return self._req_id

    def _get_client(self) -> httpx.AsyncClient:
        if self._client is None or self._client.is_closed:
            self._client = httpx.AsyncClient()
        return self._client

    def _reset_session(self) -> None:
        self.session_id = None
        self._initialized = False
        self.server_instructions = None
        self.server_info = None
        self.server_capabilities = None

    def _headers(self, include_session: bool = True) -> Dict[str, str]:
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if include_session:
            if self.session_id:
                headers["Mcp-Session-Id"] = self.session_id
            if self._initialized or self.session_id:
                headers["MCP-Protocol-Version"] = self.negotiated_version
        return headers

    async def ensure_session(self) -> bool:
        if self._initialized:
            return True
        async with self._lock:
            if self._initialized:
                return True
            return await self._initialize_unlocked()

    async def _read_rpc_response(
        self,
        resp: httpx.Response,
        req_id: Any = None,
    ) -> Dict[str, Any]:
        if resp.status_code in (202, 204):
            await resp.aread()
            return {}

        content_type = resp.headers.get("content-type", "").lower()
        if "text/event-stream" in content_type and resp.status_code < 400:
            final_frame: Optional[Dict[str, Any]] = None
            async for sse in EventSource(resp).aiter_sse():
                raw_data = sse.data.strip() if sse.data else ""
                if not raw_data or raw_data == "[DONE]":
                    continue
                try:
                    frame = json.loads(raw_data)
                except json.JSONDecodeError as e:
                    print(f"Skipping malformed SSE JSON frame from UE 5.9 MCP: {e}", file=sys.stderr)
                    continue
                if not isinstance(frame, dict):
                    continue
                if "method" in frame and "id" not in frame:
                    if self._on_notification is not None:
                        await self._on_notification(frame)
                else:
                    final_frame = frame

            if final_frame is not None:
                return final_frame
            return {
                "jsonrpc": "2.0",
                "id": req_id,
                "error": {
                    "code": -32603,
                    "message": "SSE stream closed without a final JSON-RPC response.",
                },
            }

        raw_body = await resp.aread()
        if not raw_body.strip():
            if resp.status_code >= 400:
                return {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "error": {
                        "code": -32603,
                        "message": f"HTTP {resp.status_code} from UE 5.9 MCP Server.",
                    },
                }
            return {}

        try:
            data = json.loads(raw_body)
        except json.JSONDecodeError:
            snippet = raw_body.decode("utf-8", errors="replace")[:300]
            return {
                "jsonrpc": "2.0",
                "id": req_id,
                "error": {
                    "code": -32700,
                    "message": f"Invalid JSON response (HTTP {resp.status_code}): {snippet}",
                },
            }

        if isinstance(data, dict):
            if resp.status_code >= 400 and "error" not in data and "result" not in data:
                snippet = raw_body.decode("utf-8", errors="replace")[:300]
                return {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "error": {
                        "code": -32603,
                        "message": f"HTTP {resp.status_code}: {snippet}",
                    },
                }
            return data

        return {
            "jsonrpc": "2.0",
            "id": req_id,
            "error": {
                "code": -32603,
                "message": f"Unexpected JSON response type ({type(data).__name__}).",
            },
        }

    async def _initialize_unlocked(self) -> bool:
        init_req_id = self._next_id()
        init_payload = {
            "jsonrpc": "2.0",
            "id": init_req_id,
            "method": "initialize",
            "params": {
                "protocolVersion": self.requested_version,
                "capabilities": {},
                "clientInfo": {
                    "name": "UnrealEngine-Bridge",
                    "version": "2.0.0",
                },
            },
        }
        try:
            client = self._get_client()
            async with client.stream(
                "POST",
                self.url,
                json=init_payload,
                headers=self._headers(include_session=False),
                timeout=2.0,
            ) as resp:
                if resp.status_code != 200:
                    await resp.aread()
                    self._reset_session()
                    return False

                session_id = resp.headers.get("Mcp-Session-Id") or resp.headers.get("mcp-session-id")
                data = await self._read_rpc_response(resp, req_id=init_req_id)

            if "error" in data or not isinstance(data.get("result"), dict):
                self._reset_session()
                return False

            result = data["result"]
            self.negotiated_version = result.get("protocolVersion") or self.requested_version
            instructions = result.get("instructions")
            self.server_instructions = instructions if isinstance(instructions, str) and instructions else None
            server_info = result.get("serverInfo")
            self.server_info = server_info if isinstance(server_info, dict) else None
            capabilities = result.get("capabilities")
            self.server_capabilities = capabilities if isinstance(capabilities, dict) else None
            self.session_id = session_id or None
            self._initialized = True

            notif_payload = {
                "jsonrpc": "2.0",
                "method": "notifications/initialized",
            }
            notif_resp = await client.post(
                self.url,
                json=notif_payload,
                headers=self._headers(include_session=True),
                timeout=2.0,
            )
            if notif_resp.status_code >= 400:
                print(
                    f"UE 5.9 MCP rejected notifications/initialized (HTTP {notif_resp.status_code}).",
                    file=sys.stderr,
                )
                self._reset_session()
                return False

            return True
        except Exception:
            self._reset_session()
            return False

    async def request(
        self,
        method: str,
        params: Optional[Dict[str, Any]] = None,
        timeout: float = 60.0,
        on_request_id: Optional[Callable[[int], None]] = None,
    ) -> Optional[Dict[str, Any]]:
        if not await self.ensure_session():
            return None

        client = self._get_client()
        for attempt in range(2):
            req_id = self._next_id()
            if on_request_id is not None:
                on_request_id(req_id)
            payload: Dict[str, Any] = {
                "jsonrpc": "2.0",
                "id": req_id,
                "method": method,
            }
            if params is not None:
                payload["params"] = params

            try:
                async with client.stream(
                    "POST",
                    self.url,
                    json=payload,
                    headers=self._headers(include_session=True),
                    timeout=timeout,
                ) as resp:
                    if resp.status_code == 404:
                        await resp.aread()
                        self._reset_session()
                        if attempt == 0 and await self.ensure_session():
                            continue
                        return None

                    return await self._read_rpc_response(resp, req_id=req_id)
            except (httpx.ConnectError, httpx.RemoteProtocolError):
                self._reset_session()
                return None
            except httpx.TimeoutException:
                return {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "error": {
                        "code": -32001,
                        "message": f"Request '{method}' timed out after {timeout}s.",
                    },
                }
            except Exception as e:
                print(f"Error calling UE 5.9 MCP ({method}): {e}", file=sys.stderr)
                return {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "error": {
                        "code": -32603,
                        "message": f"Bridge transport error calling '{method}': {e}",
                    },
                }
        return None

    async def notify(
        self,
        method: str,
        params: Optional[Dict[str, Any]] = None,
        timeout: float = 5.0,
    ) -> bool:
        if not await self.ensure_session():
            return False

        payload: Dict[str, Any] = {
            "jsonrpc": "2.0",
            "method": method,
        }
        if params is not None:
            payload["params"] = params

        client = self._get_client()
        try:
            resp = await client.post(
                self.url,
                json=payload,
                headers=self._headers(include_session=True),
                timeout=timeout,
            )
            if resp.status_code == 404:
                self._reset_session()
                return False
            return resp.status_code < 400
        except Exception as e:
            print(f"Error sending notification '{method}' to UE 5.9 MCP: {e}", file=sys.stderr)
            return False

    async def close(self) -> None:
        client = self._client
        session_id = self.session_id
        if client is not None and not client.is_closed:
            if session_id:
                try:
                    await client.delete(
                        self.url,
                        headers=self._headers(include_session=True),
                        timeout=2.0,
                    )
                except Exception:
                    pass
            try:
                await client.aclose()
            except Exception:
                pass
        self._client = None
        self._reset_session()


def save_tools_cache(tools: List[Dict[str, Any]], owners: Dict[str, str]) -> None:
    try:
        PROFILE_DIR.mkdir(parents=True, exist_ok=True)
        cache_data = {
            "tools": tools,
            "tool_owners": owners,
        }
        with open(CACHE_PATH, "w", encoding="utf-8") as f:
            json.dump(cache_data, f, indent=2)
    except Exception as e:
        print(f"Error saving tools cache: {e}", file=sys.stderr)


def load_tools_cache() -> Optional[Dict[str, Any]]:
    if CACHE_PATH.exists():
        try:
            with open(CACHE_PATH, encoding="utf-8") as f:
                return json.load(f)
        except Exception as e:
            print(f"Error loading tools cache: {e}", file=sys.stderr)
    return None


def apply_profile_filters(
    raw_tools: List[Dict[str, Any]],
    raw_owners: Dict[str, str],
    profile: Dict[str, Any],
) -> List[Dict[str, Any]]:
    global tool_owners, last_tools_hash

    disabled_tools = set(profile.get("disabled_tools", []))
    tool_overrides = profile.get("tool_overrides", {})

    final_tools: List[Dict[str, Any]] = []
    tool_owners.clear()

    for t in raw_tools:
        name = t.get("name")
        if not name or name in disabled_tools:
            continue
        tool_copy = dict(t)
        if name in tool_overrides and isinstance(tool_overrides[name], dict):
            tool_copy.update(tool_overrides[name])
        tool_owners[name] = raw_owners.get(name, "native")
        final_tools.append(tool_copy)

    sorted_tools = sorted(final_tools, key=lambda x: x["name"])
    last_tools_hash = hashlib.md5(json.dumps(sorted_tools, sort_keys=True).encode("utf-8")).hexdigest()
    return final_tools


def _write_stdout(payload: Dict[str, Any]) -> None:
    print(json.dumps(payload), flush=True)


def sanitize_tool_result(result_obj: Any, profile: Dict[str, Any]) -> Any:
    if profile.get("image_support", True) is not False or not isinstance(result_obj, dict):
        return result_obj
    content = result_obj.get("content")
    if not isinstance(content, list):
        return result_obj

    filtered_content: List[Any] = []
    changed = False
    for item in content:
        if isinstance(item, dict) and item.get("type") == "image":
            changed = True
            mime = item.get("mimeType", "image")
            filtered_content.append(
                {
                    "type": "text",
                    "text": f"[Image omitted: client profile has image_support=false ({mime})]",
                }
            )
        else:
            filtered_content.append(item)

    if not changed:
        return result_obj
    updated = dict(result_obj)
    updated["content"] = filtered_content
    return updated


async def _request_paginated(
    ue_client: UE59StreamableHttpClient,
    method: str,
    list_key: str,
    params: Optional[Dict[str, Any]] = None,
    timeout: float = 10.0,
    max_pages: int = 50,
) -> Dict[str, Any]:
    """Fetch one page if cursor is explicitly requested, or collect all pages across nextCursor."""
    req_params = dict(params or {})
    if req_params.get("cursor"):
        resp = await ue_client.request(method, req_params, timeout=timeout)
        if resp is None:
            return {"_offline": True}
        if "error" in resp:
            return {"error": resp["error"]}
        return {"result": resp.get("result", {list_key: []})}

    items: List[Any] = []
    seen_cursors = set()
    cursor: Optional[str] = None

    for _ in range(max_pages):
        page_params = {"cursor": cursor} if cursor else {}
        resp = await ue_client.request(method, page_params, timeout=timeout)
        if resp is None:
            return {"_offline": True}
        if "error" in resp:
            err = resp["error"]
            if isinstance(err, dict) and err.get("code") == -32001:
                return {"_offline": True, "error": err}
            return {"error": err}

        result = resp.get("result")
        if not isinstance(result, dict):
            return {"error": {"code": -32603, "message": f"Malformed {method} result from UE 5.9 MCP."}}

        page_items = result.get(list_key, [])
        if isinstance(page_items, list):
            items.extend(page_items)

        next_cursor = result.get("nextCursor")
        if not next_cursor or next_cursor in seen_cursors:
            break
        seen_cursors.add(next_cursor)
        cursor = next_cursor

    return {"result": {list_key: items}}


async def discover_tools(ue_client: UE59StreamableHttpClient, profile: Dict[str, Any]) -> List[Dict[str, Any]]:
    global last_tools_hash, _last_discover_online

    async with discover_lock:
        paged = await _request_paginated(ue_client, "tools/list", "tools", timeout=5.0)

        if paged.get("_offline") or ("error" in paged and "result" not in paged):
            _last_discover_online = False
            cache = load_tools_cache()
            if cache:
                cached_tools = cache.get("tools", [])
                cached_owners = cache.get("tool_owners", {})
                return apply_profile_filters(cached_tools, cached_owners, profile)

            last_tools_hash = hashlib.md5(b"[]").hexdigest()
            tool_owners.clear()
            return []

        _last_discover_online = True
        native_tools: List[Dict[str, Any]] = []
        for t in paged.get("result", {}).get("tools", []):
            if isinstance(t, dict) and "name" in t:
                native_tools.append(
                    {
                        "name": t["name"],
                        "description": t.get("description", ""),
                        "inputSchema": t.get("inputSchema", {"type": "object", "properties": {}}),
                    }
                )

        raw_owners = {t["name"]: "native" for t in native_tools}
        save_tools_cache(native_tools, raw_owners)
        return apply_profile_filters(native_tools, raw_owners, profile)


async def poll_tools_loop(ue_client: UE59StreamableHttpClient, profile: Dict[str, Any]) -> None:
    global last_tools_hash
    delay = POLL_INTERVAL
    while True:
        await asyncio.sleep(delay)
        try:
            old_hash = last_tools_hash
            await discover_tools(ue_client, profile)
            new_hash = last_tools_hash

            if _last_discover_online:
                delay = POLL_INTERVAL
            else:
                delay = min(max(POLL_INTERVAL, delay * 2.0), MAX_POLL_INTERVAL)

            if new_hash != old_hash:
                _write_stdout(
                    {
                        "jsonrpc": "2.0",
                        "method": "notifications/tools/list_changed",
                    }
                )
        except asyncio.CancelledError:
            raise
        except Exception as e:
            print(f"Error in background polling: {e}", file=sys.stderr)


async def _handle_tools_call(
    msg_id: Any,
    params: Dict[str, Any],
    ue_client: UE59StreamableHttpClient,
    profile: Dict[str, Any],
    mcp_url: str,
    active_request_ids: Dict[Any, int],
    pending_requests: set[Any],
    pending_cancellations: Dict[Any, Dict[str, Any]],
) -> None:
    name = params.get("name", "")
    args = params.get("arguments", {})

    try:
        if not tool_owners or name not in tool_owners:
            await discover_tools(ue_client, profile)

        if name not in tool_owners:
            result_obj: Dict[str, Any] = {
                "content": [{"type": "text", "text": f"Tool '{name}' not found."}],
                "isError": True,
            }
        else:
            call_params: Dict[str, Any] = {"name": name, "arguments": args}
            if isinstance(params.get("_meta"), dict):
                call_params["_meta"] = params["_meta"]

            cancel_tasks: List[asyncio.Task[Any]] = []

            def _record_upstream_id(upstream_id: int) -> None:
                if msg_id is None:
                    return
                active_request_ids[msg_id] = upstream_id
                queued_cancel = pending_cancellations.pop(msg_id, None)
                if queued_cancel is not None:
                    cancel_payload = dict(queued_cancel)
                    cancel_payload["requestId"] = upstream_id
                    cancel_tasks.append(
                        asyncio.create_task(ue_client.notify("notifications/cancelled", cancel_payload))
                    )

            mcp_resp = await ue_client.request(
                "tools/call",
                call_params,
                timeout=120.0,
                on_request_id=_record_upstream_id,
            )
            if cancel_tasks:
                await asyncio.gather(*cancel_tasks, return_exceptions=True)

            if mcp_resp is None:
                result_obj = {
                    "content": [
                        {
                            "type": "text",
                            "text": f"Unreal Engine 5.9 MCP Server ({mcp_url}) is not connected.",
                        }
                    ],
                    "isError": True,
                }
            elif "error" in mcp_resp:
                err = mcp_resp["error"]
                err_msg = err.get("message", str(err)) if isinstance(err, dict) else str(err)
                result_obj = {
                    "content": [{"type": "text", "text": f"MCP Error calling '{name}': {err_msg}"}],
                    "isError": True,
                }
            else:
                result_obj = sanitize_tool_result(
                    mcp_resp.get("result", {"content": [], "isError": False}),
                    profile,
                )
    finally:
        if msg_id is not None:
            active_request_ids.pop(msg_id, None)
            pending_requests.discard(msg_id)
            pending_cancellations.pop(msg_id, None)

    _write_stdout(
        {
            "jsonrpc": "2.0",
            "id": msg_id,
            "result": result_obj,
        }
    )


async def main_loop() -> None:
    loop = asyncio.get_running_loop()

    while True:
        line = await loop.run_in_executor(None, sys.stdin.readline)
        if not line:
            return
        if line.strip():
            break

    try:
        req = json.loads(line)
    except json.JSONDecodeError as e:
        _write_stdout(
            {
                "jsonrpc": "2.0",
                "id": None,
                "error": {"code": -32700, "message": f"Invalid initialize JSON: {e}"},
            }
        )
        return

    init_params = req.get("params", {}) if isinstance(req, dict) else {}
    client_name = init_params.get("clientInfo", {}).get("name", "") if isinstance(init_params, dict) else ""
    requested_version = (
        init_params.get("protocolVersion") or UE_MCP_PROTOCOL_VERSION
        if isinstance(init_params, dict)
        else UE_MCP_PROTOCOL_VERSION
    )

    profile = load_profile(client_name)
    mcp_url = resolve_mcp_url(profile)

    async def _forward_notification(notif: Dict[str, Any]) -> None:
        _write_stdout(notif)

    ue_client = UE59StreamableHttpClient(
        mcp_url,
        protocol_version=requested_version,
        on_notification=_forward_notification,
    )

    connected = await ue_client.ensure_session()

    init_result: Dict[str, Any] = {
        "protocolVersion": ue_client.negotiated_version if connected else requested_version,
        "capabilities": {
            "tools": {"listChanged": True},
            "resources": {"listChanged": False},
        },
        "serverInfo": ue_client.server_info or {"name": "UnrealEngine-Bridge", "version": "2.0.0"},
    }
    if ue_client.server_instructions:
        init_result["instructions"] = ue_client.server_instructions

    _write_stdout(
        {
            "jsonrpc": "2.0",
            "id": req.get("id") if isinstance(req, dict) else None,
            "result": init_result,
        }
    )

    poll_task = asyncio.create_task(poll_tools_loop(ue_client, profile))
    in_flight: set[asyncio.Task[Any]] = set()
    active_request_ids: Dict[Any, int] = {}
    pending_requests: set[Any] = set()
    pending_cancellations: Dict[Any, Dict[str, Any]] = {}

    def _track_task(coro: Any) -> None:
        task = asyncio.create_task(coro)
        in_flight.add(task)
        task.add_done_callback(in_flight.discard)

    try:
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line:
                break
            if not line.strip():
                continue

            try:
                msg = json.loads(line)
            except Exception as e:
                print(f"Skipping malformed JSON line from stdin: {e}", file=sys.stderr)
                continue

            if not isinstance(msg, dict):
                continue

            method = msg.get("method")
            msg_id = msg.get("id")
            params = msg.get("params", {}) if isinstance(msg.get("params"), dict) else {}

            if method == "tools/list":
                final_tools = await discover_tools(ue_client, profile)
                _write_stdout(
                    {
                        "jsonrpc": "2.0",
                        "id": msg_id,
                        "result": {"tools": final_tools},
                    }
                )

            elif method == "tools/call":
                if msg_id is not None:
                    pending_requests.add(msg_id)
                _track_task(
                    _handle_tools_call(
                        msg_id,
                        params,
                        ue_client,
                        profile,
                        mcp_url,
                        active_request_ids,
                        pending_requests,
                        pending_cancellations,
                    )
                )

            elif method == "resources/list":
                paged = await _request_paginated(
                    ue_client,
                    "resources/list",
                    "resources",
                    params=params,
                    timeout=10.0,
                )
                if paged.get("_offline"):
                    _write_stdout(
                        {
                            "jsonrpc": "2.0",
                            "id": msg_id,
                            "error": {
                                "code": -32002,
                                "message": f"Unreal Engine 5.9 MCP Server ({mcp_url}) is not connected.",
                            },
                        }
                    )
                elif "error" in paged:
                    _write_stdout(
                        {
                            "jsonrpc": "2.0",
                            "id": msg_id,
                            "error": paged["error"],
                        }
                    )
                else:
                    _write_stdout(
                        {
                            "jsonrpc": "2.0",
                            "id": msg_id,
                            "result": paged["result"],
                        }
                    )

            elif method == "resources/read":
                mcp_resp = await ue_client.request("resources/read", params, timeout=30.0)
                if mcp_resp is None:
                    _write_stdout(
                        {
                            "jsonrpc": "2.0",
                            "id": msg_id,
                            "error": {
                                "code": -32002,
                                "message": f"Unreal Engine 5.9 MCP Server ({mcp_url}) is not connected.",
                            },
                        }
                    )
                elif "error" in mcp_resp:
                    _write_stdout(
                        {
                            "jsonrpc": "2.0",
                            "id": msg_id,
                            "error": mcp_resp["error"],
                        }
                    )
                else:
                    _write_stdout(
                        {
                            "jsonrpc": "2.0",
                            "id": msg_id,
                            "result": mcp_resp.get("result", {"contents": []}),
                        }
                    )

            elif method == "notifications/initialized":
                pass

            elif method == "notifications/cancelled":
                raw_req_id = params.get("requestId")
                if raw_req_id in active_request_ids:
                    cancel_params = dict(params)
                    cancel_params["requestId"] = active_request_ids[raw_req_id]
                    _track_task(ue_client.notify("notifications/cancelled", cancel_params))
                elif raw_req_id in pending_requests:
                    pending_cancellations[raw_req_id] = dict(params)
                else:
                    _track_task(ue_client.notify("notifications/cancelled", dict(params)))

            elif method == "ping":
                _write_stdout({"jsonrpc": "2.0", "id": msg_id, "result": {}})

            else:
                if msg_id is not None:
                    _write_stdout(
                        {
                            "jsonrpc": "2.0",
                            "id": msg_id,
                            "error": {"code": -32601, "message": f"Method '{method}' not found"},
                        }
                    )
    finally:
        if in_flight:
            await asyncio.gather(*list(in_flight), return_exceptions=True)
        poll_task.cancel()
        try:
            await poll_task
        except asyncio.CancelledError:
            pass
        await ue_client.close()


if __name__ == "__main__":
    try:
        asyncio.run(main_loop())
    except KeyboardInterrupt:
        pass
