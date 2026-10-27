import sys
import json
import asyncio
import httpx
import os
import hashlib

from pathlib import Path
from contextlib import AsyncExitStack

# Add user's pip packages if needed
import site
if sys.platform == "win32":
    site_dirs = site.getsitepackages()
    if hasattr(site, "getusersitepackages"):
        site_dirs.append(site.getusersitepackages())

try:
    from mcp.client.sse import sse_client
    from mcp.client.session import ClientSession
except ImportError:
    print("mcp package not found. Please install it.", file=sys.stderr)
    sys.exit(1)

PROFILE_DIR = Path(__file__).resolve().parent.parent / "profiles"
CACHE_PATH = PROFILE_DIR / "discovered_tools_cache.json"
tool_owners = {}
ue_session = None
ue_stack = None
discover_lock = asyncio.Lock()
last_tools_hash = ""



def load_profile(client_name=""):
    profile_name = os.environ.get("BRIDGE_PROFILE")
    if profile_name:
        path = PROFILE_DIR / f"{profile_name}.json"
        if path.exists():
            with open(path) as f: return json.load(f)
            
    if client_name:
        path = PROFILE_DIR / f"{client_name.lower()}.json"
        if path.exists():
            with open(path) as f: return json.load(f)
            
    path = PROFILE_DIR / "default.json"
    if path.exists():
        with open(path) as f: return json.load(f)
        
    return {}

def are_tools_similar(tool1, tool2):
    return tool1.get("name", "").lower() == tool2.get("name", "").lower()

async def is_port_open_async(port):
    try:
        _, writer = await asyncio.wait_for(asyncio.open_connection("127.0.0.1", port), timeout=0.2)
        writer.close()
        await writer.wait_closed()
        return True
    except Exception:
        return False

async def fetch_agentframework_tools(port=18777):
    if not await is_port_open_async(port):
        return []
    async with httpx.AsyncClient() as client:
        try:
            resp = await client.get(f"http://127.0.0.1:{port}/api/tools", timeout=2.0)
            if resp.status_code == 200:
                data = resp.json()
                schemas = data.get("tools", [])
                flat_tools = []
                for schema in schemas:
                    if isinstance(schema, dict) and "tools" in schema:
                        for tool in schema["tools"]:
                            if isinstance(tool, dict) and "name" in tool:
                                flat_tools.append(tool)
                    elif isinstance(schema, dict) and "name" in schema:
                        flat_tools.append(schema)
                return flat_tools
        except Exception:
            return []
    return []

async def init_ue58_client(port):
    if not await is_port_open_async(port):
        return None, None
    stack = AsyncExitStack()
    try:
        streams = await stack.enter_async_context(sse_client(f"http://127.0.0.1:{port}/sse", timeout=30.0))
        session = await stack.enter_async_context(ClientSession(streams[0], streams[1]))
        await session.initialize()
        return stack, session
    except Exception as e:
        await stack.aclose()
        return None, None

def save_tools_cache(tools, owners):
    try:
        cache_data = {
            "tools": tools,
            "tool_owners": owners
        }
        with open(CACHE_PATH, "w") as f:
            json.dump(cache_data, f, indent=2)
    except Exception as e:
        print(f"Error saving tools cache: {e}", file=sys.stderr)

def load_tools_cache():
    if CACHE_PATH.exists():
        try:
            with open(CACHE_PATH) as f:
                return json.load(f)
        except Exception as e:
            print(f"Error loading tools cache: {e}", file=sys.stderr)
    return None

async def discover_tools(ue_port, profile):
    global tool_owners, ue_session, ue_stack, last_tools_hash
    
    async with discover_lock:
        native_tools = []
        if ue_session is None:
            ue_stack, ue_session = await init_ue58_client(ue_port)
            
        if ue_session:
            try:
                native_resp = await ue_session.list_tools()
                for t in native_resp.tools:
                    native_tools.append({
                        "name": t.name,
                        "description": t.description or "",
                        "inputSchema": t.inputSchema or {}
                    })
            except Exception as e:
                print(f"Error fetching native tools (connection lost?): {e}", file=sys.stderr)
                ue_session = None
                if ue_stack:
                    await ue_stack.aclose()
                    ue_stack = None
                    
        ag_port = int(os.environ.get("BRIDGE_HTTP_PORT", profile.get("ue_http_mcp_port", 18777)))
        ag_tools = await fetch_agentframework_tools(ag_port)
        
        if not native_tools and not ag_tools:
            cache = load_tools_cache()
            if cache:
                cached_tools = cache.get("tools", [])
                cached_owners = cache.get("tool_owners", {})
                
                disabled_tools = profile.get("disabled_tools", [])
                tool_overrides = profile.get("tool_overrides", {})
                
                final_tools = []
                tool_owners.clear()
                for t in cached_tools:
                    name = t["name"]
                    if name in disabled_tools: continue
                    tool_owners[name] = cached_owners.get(name, "native")
                    if name in tool_overrides:
                        t.update(tool_overrides[name])
                    final_tools.append(t)
                
                # Update hash based on the tools to return to the client
                sorted_tools = sorted(final_tools, key=lambda x: x["name"])
                last_tools_hash = hashlib.md5(json.dumps(sorted_tools, sort_keys=True).encode()).hexdigest()
                return final_tools
        
        filtered_ag_tools = []
        for ag_tool in ag_tools:
            is_duplicate = False
            for nat_tool in native_tools:
                if are_tools_similar(ag_tool, nat_tool):
                    is_duplicate = True
                    break
            if not is_duplicate:
                filtered_ag_tools.append(ag_tool)
                
        raw_tools = []
        raw_owners = {}
        for t in native_tools:
            raw_tools.append(t)
            raw_owners[t["name"]] = "native"
        for t in filtered_ag_tools:
            raw_tools.append(t)
            raw_owners[t["name"]] = "agentframework"
            
        if raw_tools:
            save_tools_cache(raw_tools, raw_owners)
            
        final_tools = []
        tool_owners.clear()
        
        disabled_tools = profile.get("disabled_tools", [])
        tool_overrides = profile.get("tool_overrides", {})
        
        for t in native_tools:
            if t["name"] in disabled_tools: continue
            tool_owners[t["name"]] = "native"
            if t["name"] in tool_overrides:
                t.update(tool_overrides[t["name"]])
            final_tools.append(t)
            
        for t in filtered_ag_tools:
            if t["name"] in disabled_tools: continue
            tool_owners[t["name"]] = "agentframework"
            if t["name"] in tool_overrides:
                t.update(tool_overrides[t["name"]])
            final_tools.append(t)
            
        # Update hash based on the tools to return to the client
        sorted_tools = sorted(final_tools, key=lambda x: x["name"])
        last_tools_hash = hashlib.md5(json.dumps(sorted_tools, sort_keys=True).encode()).hexdigest()
        return final_tools

AGENTFRAMEWORK_PORT = 18777

def read_published_token(port):
    """Per-launch token from the endpoint.json the editor publishes, or None.

    Only returned when the file names `port`, so a stale file never hands a token to another
    editor. An editor that publishes no token is called without the header, as before.
    """
    try:
        with open(Path.cwd() / "Saved" / "AgentFramework" / "endpoint.json") as f:
            endpoint = json.load(f)
        if int(endpoint.get("port", 0)) == int(port):
            return endpoint.get("token") or None
    except Exception:
        pass
    return None

async def call_agentframework_tool(name, arguments, profile):
    payload = {"name": name, "arguments": arguments}
    headers = {}
    token = read_published_token(AGENTFRAMEWORK_PORT)
    if token:
        headers["X-AgentFramework-Token"] = token
    async with httpx.AsyncClient(timeout=None) as client:
        try:
            resp = await client.post(f"http://127.0.0.1:{AGENTFRAMEWORK_PORT}/api/execute_tool", json=payload, headers=headers, timeout=None)
            if resp.status_code in (400, 401, 403):
                print(f"WARNING: editor rejected {name} (HTTP {resp.status_code}): {resp.text[:200]}. Check Saved/AgentFramework/endpoint.json token.", file=sys.stderr)
            ue_result = resp.json()
            bSuccess = ue_result.get("bSuccess", False)
            bRequiresHumanVerification = ue_result.get("bRequiresHumanVerification", False)
            msg = ue_result.get("ResultMessage", "")
            errors = ue_result.get("Errors", [])
            warnings = ue_result.get("Warnings", [])

            if bRequiresHumanVerification:
                err_details = " | ".join(errors) if errors else msg
                verification_text = f"[REQUIRES_HUMAN_VERIFICATION] Human verification (CAPTCHA) required: {err_details}. Please notify the user to complete verification in Unreal Editor."
                return {
                    "content": [{"type": "text", "text": verification_text}],
                    "isError": True
                }
            
            content = []
            image_support = profile.get("image_support", False)
            image_tools = ["capture_widget", "capture_viewport", "capture_niagara_system_isolated"]
            
            if bSuccess and name in image_tools and image_support:
                base64_data = msg
                if "base64," in base64_data:
                    base64_data = base64_data.split("base64,")[1]
                content.append({
                    "type": "image",
                    "data": base64_data,
                    "mimeType": "image/png"
                })
                # Image payloads cannot carry the diagnostics inline, so warnings ride
                # alongside as a second block rather than being dropped.
                if warnings:
                    content.append({
                        "type": "text",
                        "text": "\n".join(["--- Warnings ---", *(f"  - {w}" for w in warnings)])
                    })
            else:
                out_text = msg if bSuccess else (" | ".join(errors) if errors else msg)
                # Warnings were previously dropped here, which left the agent blind to
                # partial failures on a bSuccess:true call (unsaved packages, compiler
                # warnings, engine log errors). Append them so they reach the model, unless
                # the C++ layer already embedded a Diagnostics Log block in the text.
                if warnings and "--- Diagnostics Log" not in out_text:
                    out_text = "\n".join(
                        filter(None, [out_text, "--- Warnings ---", *(f"  - {w}" for w in warnings)])
                    )
                content.append({
                    "type": "text",
                    "text": out_text
                })

            return {
                "content": content,
                "isError": not bSuccess
            }
        except Exception as e:
            return {
                "content": [{"type": "text", "text": str(e)}],
                "isError": True
            }

async def poll_tools_loop(ue_port, profile):
    global last_tools_hash
    while True:
        await asyncio.sleep(3.0)
        try:
            old_hash = last_tools_hash
            # discover_tools updates last_tools_hash
            tools = await discover_tools(ue_port, profile)
            new_hash = last_tools_hash
            
            if new_hash != old_hash:
                notification = {
                    "jsonrpc": "2.0",
                    "method": "notifications/tools/list_changed"
                }
                print(json.dumps(notification), flush=True)
        except Exception as e:
            print(f"Error in background polling: {e}", file=sys.stderr)

async def main_loop():
    global ue_stack, ue_session, tool_owners
    loop = asyncio.get_event_loop()
    
    # Read initialize
    line = await loop.run_in_executor(None, sys.stdin.readline)
    if not line: return
    req = json.loads(line)
    
    client_name = req.get("params", {}).get("clientInfo", {}).get("name", "")
    profile = load_profile(client_name)
    ue_port = profile.get("ue_native_mcp_port", 8000)
    
    ue_stack, ue_session = await init_ue58_client(ue_port)
    
    resp = {
        "jsonrpc": "2.0",
        "id": req["id"],
        "result": {
            "protocolVersion": "2024-11-05",
            "capabilities": {"tools": {"listChanged": True}},
            "serverInfo": {"name": "UnrealEngineFederated", "version": "2.1.0"}
        }
    }
    print(json.dumps(resp), flush=True)

    poll_task = asyncio.create_task(poll_tools_loop(ue_port, profile))

    try:
        while True:
            line = await loop.run_in_executor(None, sys.stdin.readline)
            if not line: break
            
            try:
                msg = json.loads(line)
            except:
                continue
                
            method = msg.get("method")
            msg_id = msg.get("id")
            
            if method == "tools/list":
                final_tools = await discover_tools(ue_port, profile)
                out = {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": {"tools": final_tools}
                }
                print(json.dumps(out), flush=True)
                
            elif method == "tools/call":
                params = msg.get("params", {})
                name = params.get("name", "")
                args = params.get("arguments", {})
                
                # Lazy load tool owners if not populated or if requested tool is missing
                if not tool_owners or name not in tool_owners:
                    await discover_tools(ue_port, profile)
                    
                owner = tool_owners.get(name)
                result_obj = None
                
                if owner == "native":
                    if ue_session is None:
                        async with discover_lock:
                            if ue_session is None:
                                ue_stack, ue_session = await init_ue58_client(ue_port)
                        
                    if ue_session:
                        try:
                            res = await ue_session.call_tool(name, arguments=args)
                            content = []
                            for c in res.content:
                                if c.type == "text":
                                    content.append({"type": "text", "text": c.text})
                                elif c.type == "image":
                                    content.append({"type": "image", "data": c.data, "mimeType": c.mimeType})
                            result_obj = {
                                "content": content,
                                "isError": res.isError
                            }
                        except Exception as e:
                            ue_session = None
                            if ue_stack:
                                await ue_stack.aclose()
                                ue_stack = None
                            result_obj = {
                                "content": [{"type": "text", "text": f"Error calling native tool: {e}"}],
                                "isError": True
                            }
                    else:
                        result_obj = {
                            "content": [{"type": "text", "text": "Native UE 5.8 server is not connected."}],
                            "isError": True
                        }
                elif owner == "agentframework":
                    result_obj = await call_agentframework_tool(name, args, profile)
                else:
                    result_obj = {
                        "content": [{"type": "text", "text": f"Tool '{name}' not found."}],
                        "isError": True
                    }
                    
                out = {
                    "jsonrpc": "2.0",
                    "id": msg_id,
                    "result": result_obj
                }
                print(json.dumps(out), flush=True)
                
            elif method == "notifications/initialized":
                pass
                
            elif method == "ping":
                out = {"jsonrpc": "2.0", "id": msg_id, "result": {}}
                print(json.dumps(out), flush=True)
                
            else:
                if msg_id is not None:
                    out = {"jsonrpc": "2.0", "id": msg_id, "error": {"code": -32601, "message": f"Method '{method}' not found"}}
                    print(json.dumps(out), flush=True)
    finally:
        poll_task.cancel()
        try:
            await poll_task
        except asyncio.CancelledError:
            pass
        if ue_stack:
            await ue_stack.aclose()

if __name__ == "__main__":
    try:
        asyncio.run(main_loop())
    except KeyboardInterrupt:
        pass
