# AgentFramework Unreal Engine Plugin (v2.0.0 for UE 5.9)

This is the Unreal Engine plugin component of **UE-AgentFramework** for **Unreal Engine 5.9**. It integrates with UE 5.9's built-in `ModelContextProtocol` Streamable HTTP server (`http://127.0.0.1:8000/mcp`) and `ToolsetRegistry`, extending UE 5.9's native editor toolsets with 6 complementary native `UToolsetDefinition` classes under `AgentFrameworkActions`:

- `UAgentFrameworkCppToolset` (`AgentFrameworkActions.AgentFrameworkCppToolset`)
- `UAgentFrameworkBuildToolset` (`AgentFrameworkActions.AgentFrameworkBuildToolset`)
- `UAgentFrameworkEnhancedInputAuthoringToolset` (`AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset`)
- `UAgentFrameworkAnimationAuthoringToolset` (`AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset`)
- `UAgentFrameworkGASAuthoringToolset` (`AgentFrameworkActions.AgentFrameworkGASAuthoringToolset`)
- `UAgentFrameworkPIERuntimeToolset` (`AgentFrameworkActions.AgentFrameworkPIERuntimeToolset`)

> **Engine Compatibility:**
> - **Unreal Engine 5.9:** Supported by `main` (`v2.0.0`).
> - **Unreal Engine 5.7 / 5.8:** Check out git tag `v1.1.0-ue5.8` (`git checkout v1.1.0-ue5.8`) for the pre-5.9 standalone HTTP loopback server architecture.

*(Note: The MCP stdio bridge and External Python AST/RAG Server are located in the `../UnrealEngine` directory of this repository.)*

## Installation

1. Create a `Plugins` directory at the root of your Unreal Engine 5.9 project if it doesn't already exist.
2. Copy this `AgentFramework` folder inside your project's `Plugins` directory:
   ```
   YourProject/
   ├── Plugins/
   │   └── AgentFramework/
   ```
3. Open your project in Unreal Editor 5.9. If prompted to rebuild missing modules, click **Yes**.
4. Make sure **AgentFramework** (alongside UE 5.9's required **ModelContextProtocol** and **ToolsetRegistry** plugins) is enabled in **Edit > Plugins**.

---

## Building and Distribution

To distribute the plugin or upload it to Epic's **Fab** marketplace, it needs to be rebuilt and packaged cleanly. A helper PowerShell script `build_plugin.ps1` is provided at the root of this repository.

### Using the Packaging Script

1. Open PowerShell at the repository root.
2. Run the build script:
   ```powershell
   $env:uebp_UATMutexNoWait = '1'
   powershell -ExecutionPolicy Bypass -File .\build_plugin.ps1 -UEVersion "5.9"
   ```
   *(Pass `-NoZip` when iterating locally without generating `Packaged/AgentFramework.zip`.)*

This script will:
- Clean any previous `Binaries/`, `Intermediate/`, and `Saved/` folders inside `AgentFramework/`.
- Use the **Unreal Automation Tool (UAT)** with `-Rocket` to compile and package the plugin against Unreal Engine 5.9.
- Create a staged build folder in `Packaged/AFBuild_<timestamp>/AgentFramework` and copy binaries into configured target projects (`-TargetProjects`).
- Zip the staged plugin into `Packaged/AgentFramework.zip` for distribution unless `-NoZip` is specified.

### Customizing Paths

If Unreal Engine 5.9 is installed in a custom location, use `-EnginePath`:
```powershell
powershell -ExecutionPolicy Bypass -File .\build_plugin.ps1 -EnginePath "D:\CustomPath\UE_5.9"
```

---

## Distribution on Fab Marketplace

Once the build finishes successfully without `-NoZip`, you can upload `Packaged/AgentFramework.zip` to the Fab portal:

1. **Log in**: Sign in to the [Fab Publisher Portal](https://publisher.fab.com/).
2. **Add Product**: Create or edit your product listing. Set the type to **Unreal Engine Plugin**.
3. **Add Version**: In the *Versions* tab, create a new version (e.g., `2.0.0` matching `VersionName` in `AgentFramework.uplugin`).
4. **Upload Package**: Upload `Packaged/AgentFramework.zip` (it contains the required root `AgentFramework/` folder with `.uplugin` and source files).
5. **Metadata**: Specify Unreal Engine `5.9` and supported platforms (`Win64`, `Linux`, `Mac`), then click **Submit for Review**.

