# AgentFramework Unreal Engine Plugin

This is the Unreal Engine plugin component of the **UE-AgentFramework** integration. It exposes an in-process HTTP loopback server (listening on port `18777` by default, configurable via `-AgentFrameworkPort=` or `AGENTFRAMEWORK_HTTP_PORT`) to execute commands safely on the Game Thread, allowing the AgentFramework AI coding assistant to natively interact with your Unreal Editor.

*(Note: The MCP servers that communicate with the AI assistant, including the C++ Bridge and the External Python AST Server, are located in the `UnrealEngine` directory of this repository.)*

## Installation

1. Create a `Plugins` directory at the root of your Unreal Engine project if it doesn't already exist.
2. Copy this `AgentFramework` folder inside your project's `Plugins` directory:
   ```
   YourProject/
   ├── Plugins/
   │   └── AgentFramework/
   ```
3. Open your project in Unreal Editor. If prompted to rebuild missing modules, click **Yes**.
4. Make sure the plugin is enabled in **Edit > Plugins**.

---

## Building and Distribution

To distribute the plugin or upload it to Epic's **Fab** marketplace, it needs to be rebuilt and packaged cleanly. A helper PowerShell script `build_plugin.ps1` is provided at the root of this repository.

### Using the Packaging Script

1. Open PowerShell at the repository root.
2. Run the build script:
   ```powershell
   .\build_plugin.ps1 -UEVersion "5.8"
   ```
   *Replace `"5.8"` with your installed Unreal Engine version (e.g., `"5.7"`, `"5.8"`).*

This script will:
- Clean any previous `Binaries/`, `Intermediate/`, and `Saved/` folders.
- Use the **Unreal Automation Tool (UAT)** with the `-Rocket` parameter to compile and build the plugin for all target platforms.
- Create a packaged folder in the `Packaged/AgentFramework` directory.
- Zip the folder into `Packaged/AgentFramework.zip` for marketplace upload.

### Customizing Paths

If your Unreal Engine is installed in a custom location, use the `-EnginePath` parameter:
```powershell
.\build_plugin.ps1 -EnginePath "D:\CustomPath\UE_5.4"
```

---

## Distribution on Fab Marketplace

Once the build finishes successfully, you can upload `Packaged/AgentFramework.zip` to the Fab portal:

1. **Log in**: Sign in to the [Fab Publisher Portal](https://publisher.fab.com/).
2. **Add Product**: Create or edit your product listing. Set the type to **Unreal Engine Plugin**.
3. **Add Version**: In the *Versions* tab, create a new version (e.g., `1.1.0` matching `VersionName` in `AgentFramework.uplugin`).
4. **Upload Package**: Upload `Packaged/AgentFramework.zip` (it contains the required root `AgentFramework/` folder with `.uplugin` and source files).
5. **Metadata**: Specify supported engine versions (e.g. `5.3-5.5`) and platforms, then click **Submit for Review**.

