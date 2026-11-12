# AgentFramework UnrealEngine MCP Plugin (v2.0.0 for UE 5.9)

This is the AI assistant plugin component of **UE-AgentFramework** for **Unreal Engine 5.9**. It provides the MCP server configuration, stdio proxy bridge, Clang AST/RAG server, and UE 5.9 workflow skills allowing AI coding assistants to inspect C++ symbols cleanly and drive Unreal Editor's built-in MCP server.

> **Engine Compatibility:**
> - **Unreal Engine 5.9:** Use `main` (`v2.0.0`).
> - **Unreal Engine 5.7 / 5.8:** Check out git tag `v1.1.0-ue5.8` (`git checkout v1.1.0-ue5.8`).

## Architecture

This plugin encompasses two MCP servers configured automatically by `install.ps1`:
1. **Python MCP Bridge (`bridge/main.py` -> `unrealengine`)**: A lightweight stdio MCP proxy (`UE59StreamableHttpClient`) that translates standard input/output (`stdin`/`stdout`) JSON-RPC messages from MCP clients to **Unreal Engine 5.9's built-in `ModelContextProtocol` Streamable HTTP server** (`http://127.0.0.1:8000/mcp`, spec `2025-11-25`). When `bEnableToolSearch = true` (UE 5.9 default), the upstream server exposes `list_toolsets`, `describe_toolsets`, `describe_tools`, and `call_tool` covering UE 5.9's native toolsets plus the 6 custom `AgentFrameworkActions.*` toolsets. Launched as `python -X utf8 -u -m bridge.main` with `PYTHONPATH` pointing at the installed plugin directory.
2. **External Python Server (`ExternalServer/src/main.py` -> `cpp-ast-rag`)**: A standalone stdio MCP server using `libclang` (auto-discovering Clang/LLVM or installing `libclang`) to index project C++ ASTs into a SQLite database (`ast_cache.db`), inspect macro expansions, render multi-file call graphs, regenerate `compile_commands.json`, and search UE 5.9 engine API documentation via optional ChromaDB RAG.

### LLM Profile System

The bridge supports a **profile-based configuration** that adapts image content handling, optional tool filtering, tool overrides, and native UE 5.9 MCP port overrides to the active client/model.

**Resolution order:**
1. `BRIDGE_PROFILE` environment variable (highest priority)
2. Auto-detection from MCP `clientInfo.name` (e.g., `antigravity`, `claude` via lowercase match)
3. `profiles/default.json` (fallback)

#### Available Profiles

| Profile | Image Support | Tool Overrides | Use Case |
|---|---|---|---|
| `default` | No | None | Safe fallback for unknown clients |
| `antigravity` | Yes | None | Auto-detected when using Antigravity |
| `deepseek-v4` | No | None | DeepSeek V4 via Kilo Code |
| `kimi-k2` | No | None | Kimi K2.6 via Kilo Code |
| `claude` | Yes | None | Claude via Kilo Code or Claude Code |

#### Profile Structure

Each profile JSON file supports the following fields:

```json
{
  "image_support": true,
  "disabled_tools": ["tool_name_to_hide"],
  "tool_overrides": {
    "tool_name": {
      "description": "Override description",
      "inputSchema": { }
    }
  },
  "ue_native_mcp_port": 8000
}
```

- **`image_support`**: When `true`, MCP `image` content items returned by UE 5.9 tools (such as `SceneCaptureToolset` captures or visual inspection tools) are passed through cleanly. When `false`, image blocks are omitted or summarized as text so text-only models do not error.
- **`disabled_tools`**: Array of tool names to omit from `tools/list`.
- **`tool_overrides`**: Per-tool overrides for `description` and `inputSchema` (applies to flattened tools when `bEnableToolSearch = false`, or meta-tools).
- **`ue_native_mcp_port`**: Optional port override for UE 5.9's `/mcp` Streamable HTTP endpoint (overridden by `UE_MCP_URL`, `UE_MCP_PORT`, or `BRIDGE_HTTP_PORT`).

#### Creating Custom Profiles

To add support for a new LLM, create a new JSON file in the `profiles/` directory (e.g., `profiles/my-model.json`) following the structure above. Set `BRIDGE_PROFILE=my-model` in your client configuration.

## Supported Clients

### Antigravity (Auto-detected)

No additional configuration is needed. The bridge automatically detects Antigravity via the MCP `clientInfo` handshake and loads the `antigravity` profile.

### Kilo Code (JetBrains IDEs)

Configure the MCP server in your project's `kilo.jsonc`:

```jsonc
{
  "mcpServers": {
    "unrealengine": {
      "type": "stdio",
      "command": "C:\\path\\to\\project\\.agents\\plugins\\UnrealEngine\\ExternalServer\\.venv\\Scripts\\python.exe",
      "args": ["-X", "utf8", "-u", "-m", "bridge.main"],
      "env": {
        "PYTHONPATH": "C:\\path\\to\\project\\.agents\\plugins\\UnrealEngine",
        "BRIDGE_PROFILE": "deepseek-v4"
      },
      "enabled": true
    }
  }
}
```

Copy the skill files from `skills/*/SKILL.md` into `.kilocode/rules/*.md` (handled automatically by `install.ps1`) so the LLM follows the standard operating procedures.

### Claude Code (CLI)

Configure the MCP servers in your project's `.mcp.json` file (this is generated automatically by running the `install.ps1` script):

```json
{
  "mcpServers": {
    "unrealengine": {
      "command": "C:\\path\\to\\.venv\\Scripts\\python.exe",
      "args": ["-X", "utf8", "-u", "-m", "bridge.main"],
      "env": {
        "PYTHONPATH": "C:\\path\\to\\UnrealEngine"
      }
    },
    "cpp-ast-rag": {
      "command": "C:\\path\\to\\.venv\\Scripts\\python.exe",
      "args": ["-u", "-m", "ExternalServer.src.main"],
      "env": {
        "PYTHONPATH": "C:\\path\\to\\UnrealEngine"
      }
    }
  }
}
```

Claude Code will automatically detect `.mcp.json` and prompt for approval when starting a session in the repository. The installer also automatically copies your custom guidelines to `CLAUDE.md` and links relevant skills to `.claude/skills/`.

### OpenAI Codex (CLI / Desktop)

Configure the MCP servers in your project's `.codex/config.toml` file (this is generated automatically by running the `install.ps1` script):

```toml
[mcp_servers.unrealengine]
command = "C:\\path\\to\\.venv\\Scripts\\python.exe"
args = ["-X", "utf8", "-u", "-m", "bridge.main"]

[mcp_servers.unrealengine.env]
PYTHONPATH = "C:\\path\\to\\UnrealEngine"

[mcp_servers.cpp-ast-rag]
command = "C:\\path\\to\\.venv\\Scripts\\python.exe"
args = ["-u", "-m", "ExternalServer.src.main"]

[mcp_servers.cpp-ast-rag.env]
PYTHONPATH = "C:\\path\\to\\UnrealEngine"
```

The Codex CLI will automatically discover project-specific servers configured in `.codex/config.toml`. It also natively supports the standard `AGENTS.md` instructions file at the workspace root, and will load relevant workspace skills under `.codex/skills/`.

### Other MCP Clients (Claude Desktop, Cursor, etc.)

Any client that supports the MCP stdio transport can use this bridge. Point the server command at the installed venv's `python.exe` with `-m bridge.main` (see the example above), set `PYTHONPATH` to the installed plugin directory, and set the `BRIDGE_PROFILE` environment variable to match your model.

## Installation

### Quick Install (Recommended)

Run the installation script from the plugin directory:

```powershell
.\install.ps1
```

The script will:
1. Ask for your target project root directory.
2. Copy `../AgentFramework/` into `<TargetProject>/Plugins/AgentFramework/` and `UnrealEngine/` into `<TargetProject>/.agents/plugins/UnrealEngine/`.
3. Create `<TargetProject>/.agents/plugins/UnrealEngine/ExternalServer/.venv` and install both `ExternalServer/requirements.txt` and `bridge/requirements.txt`.
4. Let you select one or more AI assistants (Antigravity 2.0, Kilo Code, Claude Code, and/or OpenAI Codex) — enter a comma-separated list such as `0,2`, or `all`.
5. Hard-link all skills under `skills/*/SKILL.md` and generate the MCP/permission configuration for each selected assistant (`mcp_config.json`, `kilo.jsonc`, `.mcp.json`, and/or `.codex/config.toml`).

Re-running the script later lets you add additional assistants; existing configurations are left untouched.

### Manual Installation

#### Workspace Level

Copy `AgentFramework` into `Plugins/` and `UnrealEngine` into `.agents/plugins/` at the root of your UE 5.9 project:

```bash
mkdir -p Plugins .agents/plugins
cp -r AgentFramework Plugins/
cp -r UnrealEngine .agents/plugins/
```

#### Global Level

Copy to your user profile's global Antigravity configuration directory:

* **Windows:** `$env:USERPROFILE\.gemini\config\plugins\UnrealEngine`
* **Mac/Linux:** `~/.gemini/config/plugins/UnrealEngine`

### Run the Setup Conversation (One-time Setup)

Once the plugin is installed and your Unreal Editor is running:
1. Open a new conversation with your AI coding assistant in the project workspace.
2. Instruct the assistant to run the setup and index the project:
   ```
   Run project setup and index the project
   ```
3. The assistant will trigger the `unreal-setup` skill, update the machine-specific local environment configuration in the `unreal-instructions` skill, perform a full scan of the C++ codebase, project settings, and active editor assets, and write a permanent OKF project-indexing skill at `.agents/skills/project-index/SKILL.md`.
4. **Restart the assistant session** or reload the window to ensure the new `project-index` skill is registered. This is required because the assistant platform only scans and loads workspace skills during conversation startup. Every subsequent session will benefit from this sophisticated, token-efficient architectural and asset map.

## Compilation

The bridge requires no compilation — it is the Python module `bridge/main.py`, run from the virtual environment that `install.ps1` creates. There is no build step.

> **Note:** the bridge was a compiled C++ executable (`bridge.exe`, built via `src/build_bridge.bat`) until commit `5591b0c` (2026-06-30) replaced it with the Python implementation. Both the source and the build script were removed then. If you find a stray `bridge.exe`, it is a leftover build artifact: delete it rather than pointing an assistant config at it, or that assistant will silently run a months-old bridge.

Because `bridge/main.py` is imported once per MCP server process, changes to it only take effect after the MCP server is restarted.

---

## Interface Contracts

### Client / Bridge ↔ UE 5.9 Native MCP Server
- **Upstream Transport**: `bridge/main.py` (`UE59StreamableHttpClient`) bridges MCP JSON-RPC 2.0 over stdio to UE 5.9's built-in `ModelContextProtocol` Streamable HTTP endpoint at `http://127.0.0.1:8000/mcp` (MCP protocol `2025-11-25`).
- **Endpoint Configuration**: Override via `UE_MCP_URL` (full URL), `UE_MCP_PORT` / `BRIDGE_HTTP_PORT`, or `ue_native_mcp_port` inside the active profile JSON. Optional `BRIDGE_POLL_INTERVAL` controls online/offline tool-list polling cadence (default `2.0`s, backing off up to `30.0`s while offline).
- **Session & Streaming**: Negotiates `MCP-Session-Id` on `initialize`, preserves SSE `text/event-stream` notifications (`_meta` / progress) and `nextCursor` pagination, translates `notifications/cancelled` to upstream `requestId`, and sends `DELETE /mcp` on bridge shutdown.
- **Tool Discovery (`bEnableToolSearch = true`)**: Exposes `list_toolsets`, `describe_toolsets`, `describe_tools`, and `call_tool` dispatching module-qualified `<Module>.<Toolset>` names across UE 5.9's native toolsets and the 6 custom `AgentFrameworkActions.*` toolsets. *(For UE 5.7/5.8 port `18777` / `/api/execute_tool` docs, check out git tag `v1.1.0-ue5.8`.)*

### Client ↔ External Python Server (`cpp-ast-rag`)
- **Protocol**: Standard MCP (JSON-RPC 2.0 over stdio via `python -u -m ExternalServer.src.main`).
- **Exposed Tools**:
  - `query_cpp_ast`
  - `generate_compile_commands`
  - `search_vector_db`
  - `inspect_macro_expansion` / `query_macro_expansion`
  - `visualize_call_graph` / `query_call_graph`
