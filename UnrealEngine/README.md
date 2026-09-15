# AgentFramework UnrealEngine MCP Plugin

This is the AgentFramework agent plugin component of the **UE-AgentFramework** integration. It provides the Model Context Protocol (MCP) server configuration that allows AI assistants to communicate with the Unreal Engine Editor.

## Architecture

This plugin encompasses two main components:
1. **Python MCP Bridge (`bridge/main.py`)**: A lightweight proxy that translates standard input/output (`stdin`/`stdout`) JSON-RPC messages from any MCP-compatible AI client to the Unreal Engine Editor's HTTP loopback server. It is launched as `python -m bridge.main`, with `PYTHONPATH` pointing at the installed plugin directory.
2. **External Python Server (`ExternalServer`)**: An AST Indexing and RAG server that runs as a standalone Python process. It uses `libclang` to parse C++ files, maintains an SQLite database cache, and performs semantic documentation search.

### LLM Profile System

The bridge supports a **profile-based configuration** that automatically adapts tool schemas, image handling, and tool visibility to the capabilities of the active LLM.

**Resolution order:**
1. `BRIDGE_PROFILE` environment variable (highest priority)
2. Auto-detection from MCP `clientInfo.name` (e.g., Antigravity is detected automatically)
3. `profiles/default.json` (fallback)

#### Available Profiles

| Profile | Image Support | Tool Overrides | Use Case |
|---|---|---|---|
| `default` | No | None | Safe fallback for unknown clients |
| `antigravity` | Yes | None | Auto-detected when using Antigravity |
| `deepseek-v4` | No | CoT triggers, strict schemas | DeepSeek V4 via Kilo Code |
| `kimi-k2` | No | Bilingual CoT (EN/ZH) | Kimi K2.6 via Kilo Code |
| `claude` | Yes | None | Claude via Kilo Code or Claude Desktop |

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
  }
}
```

- **`image_support`**: When `true`, image-returning tools (e.g., `capture_widget`) emit standard MCP `{"type": "image"}` content blocks. When `false`, they emit plain text.
- **`disabled_tools`**: Array of tool names to completely hide from the client's `tools/list` response.
- **`tool_overrides`**: Per-tool overrides for `description` and `inputSchema`. Useful for injecting Chain-of-Thought reasoning parameters or stricter validation for models that need it.

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

Copy the skill file to `.kilocode/rules/unreal-workflow.md` so the LLM follows the standard operating procedures.

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
2. Compile the bridge if needed.
3. Copy the plugin files to `.agents/plugins/UnrealEngine/`.
4. Let you select one or more AI assistants (Antigravity 2.0, Kilo Code, Claude Code, and/or OpenAI Codex) — enter a comma-separated list such as `0,2`, or `all`.
5. Link skills to the appropriate locations and generate the necessary MCP configuration for each selected assistant (`mcp_config.json`, `kilo.jsonc`, `.mcp.json`, and/or `.codex/config.toml`).

Re-running the script later lets you add additional assistants; existing configurations are left untouched.

### Manual Installation

#### Workspace Level

Copy the `UnrealEngine` folder into the `.agents/plugins/` directory at the root of your project:

```bash
mkdir -p .agents/plugins
cp -r UnrealEngine .agents/plugins/
```

#### Global Level

Copy to your user profile's global configuration directory:

* **Windows:** `C:\Users\<Username>\.gemini\config\plugins\UnrealEngine`
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

### Client / Bridge ↔ Internal C++ Server
- **Protocol**: JSON-RPC over stdio (Bridge) mapped to HTTP POST `/api/execute_tool` (Editor port 18777 by default, configurable via `-AgentFrameworkPort=` / `AGENTFRAMEWORK_HTTP_PORT`, or auto-discovered via Saved endpoint file).
- **Standard Payload**:
  - Request: `{"name": "<tool_name>", "arguments": { ... }}`
  - Response: `{"bSuccess": true, "ResultMessage": "...", "Errors": [], "Warnings": []}`

### Client ↔ External Python Server
- **Protocol**: Standard MCP (JSON-RPC 2.0 over stdio).
- **Exposed Tools**:
  - `query_cpp_ast`
  - `generate_compile_commands`
  - `search_vector_db`
  - `search_similar_blueprints`
  - `index_all_blueprints`
  - `format_t3d_layout`
