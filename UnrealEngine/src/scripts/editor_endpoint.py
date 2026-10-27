"""Shared resolution of the editor's AgentFramework HTTP endpoint.

Mirrors the resolution in ``bridge/main.py``: the ``BRIDGE_HTTP_PORT``
environment variable, then the default 18777. Absent the variable the
behaviour is unchanged, so existing invocations keep talking to the default
editor.

These scripts deliberately do not import the bridge package to reuse its
helper: they run on a bare Python interpreter with only ``urllib``, while
``bridge.main`` pulls in ``mcp`` and ``httpx``.
"""

import json
import os
from pathlib import Path

DEFAULT_AGENTFRAMEWORK_PORT = 18777


def editor_port():
    """The AgentFramework HTTP port this process should talk to."""
    raw = os.environ.get("BRIDGE_HTTP_PORT") or DEFAULT_AGENTFRAMEWORK_PORT
    try:
        port = int(raw)
    except (TypeError, ValueError):
        return DEFAULT_AGENTFRAMEWORK_PORT
    if not 1024 <= port <= 65535:
        return DEFAULT_AGENTFRAMEWORK_PORT
    return port


def execute_tool_url():
    return f"http://127.0.0.1:{editor_port()}/api/execute_tool"


def tools_url():
    return f"http://127.0.0.1:{editor_port()}/api/tools"


def editor_token():
    """Per-launch token the editor publishes in Saved/AgentFramework/endpoint.json, or None.

    Only used when the published file names the port this process talks to, so a stale or
    foreign file never leaks a token to the wrong editor. An editor that publishes no token
    (older build) gets no header, as before.
    """
    try:
        with open(Path.cwd() / "Saved" / "AgentFramework" / "endpoint.json") as f:
            endpoint = json.load(f)
        if int(endpoint.get("port", 0)) == editor_port():
            return endpoint.get("token") or None
    except Exception:
        pass
    return None


def json_headers():
    """Headers for a POST to /api/execute_tool: JSON content type plus the token when there is one."""
    headers = {"Content-Type": "application/json"}
    token = editor_token()
    if token:
        headers["X-AgentFramework-Token"] = token
    return headers
