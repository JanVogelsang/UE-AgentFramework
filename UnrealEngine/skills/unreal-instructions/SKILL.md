---
name: unreal-instructions
description: REQUIRED ENTRY POINT for ALL Unreal Engine tasks. Must trigger for UE5, Blueprints, C++, UMG, Editor, UObject, multiplayer, compilation, or any engine interaction.
---
# Unreal Engine 5.9 MCP Guide

## 1. Dual-MCP Architecture & Tool Routing

- **`unrealengine` (Proxies UE 5.9 `/mcp` at `http://127.0.0.1:8000/mcp` via `UnrealEngine/bridge/main.py`)**:
  - Uses UE 5.9's lazy toolset discovery (`bEnableToolSearch = true`) via four MCP meta-tools: `list_toolsets`, `describe_toolsets`, `describe_tools`, and `call_tool(toolset_name, tool_name, arguments)`.
  - Pass exact module-qualified toolset names returned by `list_toolsets` (e.g., `BlueprintToolsets.BlueprintDslToolset`, `BlueprintToolsets.BlueprintToolset`, `UMGEditor.WidgetBlueprintEditorSubsystem`, `EditorToolset.LevelToolset`, `EditorToolset.ActorToolset`, `EditorToolset.ObjectToolset`, `EditorToolset.AssetToolset`, `AgentFrameworkActions.AgentFrameworkCppToolset`).
  - *Constraint*: ALWAYS use Unreal package paths (`/Game/...`). NEVER use Windows paths or shell commands (`rm`, `mv`) for `.uasset` files.
  - *Widget & Object Parameters*: When editing UMG widget properties or layout slots via `EditorToolset.ObjectToolset.SetProperties`, inspect schemas first with `describe_tools` / `EditorToolset.ObjectToolset.GetPropertySchema` rather than guessing field structures.
- **`cpp-ast-rag` (External Clang AST & RAG Server via `UnrealEngine/ExternalServer/src/main.py`)**:
  - Separate stdio MCP server configured alongside `unrealengine` by `UnrealEngine/install.ps1`.
  - ALWAYS use its specialized tools (`query_cpp_ast`, `inspect_macro_expansion` / `query_macro_expansion`, `visualize_call_graph` / `query_call_graph`, `generate_compile_commands`, `search_vector_db`) for C++ semantic lookups and Unreal Engine documentation search instead of generic grep/file searches.
- **Engine Class AST Fallback**: The local project C++ AST (`query_cpp_ast`) indexes your project's custom C++ source code. For built-in Unreal Engine classes (e.g. `UInstancedStaticMeshComponent`, `AActor`, `UCharacterMovementComponent`), `query_cpp_ast` may not find a local declaration. If `query_cpp_ast` returns no results for an Engine class, immediately query Unreal Engine documentation via `search_vector_db`.
- **Python API Documentation Lookup**: Unreal Engine Python API signatures (`unreal.pyi`) are indexed in ChromaDB. When writing Python scripts for Unreal Engine, use `search_vector_db` (e.g., `"unreal.SourceControl"` or `"take screenshot python"`) to verify exact class and method signatures before writing code.
- **Compilation & C++ Creation**:
  - For `.cpp` body changes while the Editor is open, call `LiveCodingToolset.LiveCodingToolset.CompileLiveCoding` via `call_tool` (checking `IsLiveCodingReady` first). NEVER run manual terminal UBT/MSBuild builds while the Editor is running (the Editor locks plugin and game DLLs).
  - When adding new C++ classes or `UCLASS`/`UPROPERTY`/`UFUNCTION` reflection headers, use `AgentFrameworkActions.AgentFrameworkCppToolset` (`CreateCppClass` / `ScaffoldCppClass` / `RegenerateProjectFiles`) and refresh Clang indexing with `generate_compile_commands`.
- **Python Execution Safeguard**: When running multi-line Python scripts, ALWAYS write the script to a temporary `.py` scratch file (e.g., `scratch/script.py`) before running it with your Python interpreter. NEVER pass inline multi-line Python strings (`python -c "..."`) inside terminal shell commands to prevent Windows PowerShell string escaping failures.

<!-- LOCAL_ENV_START -->
## 2. Local Environment & Workflows

> Run `UnrealEngine/src/generate_env_skill.ps1` (executed automatically by `UnrealEngine/install.ps1`) to populate machine-specific engine, project, and `libclang.dll` paths between `LOCAL_ENV_START` and `LOCAL_ENV_END`.

### Startup Requirement (Sandbox Pre-Authorization)
Launching the Unreal Editor spawns a long-lived GUI process outside the terminal sandbox. How to authorize this depends on your agent harness:
- **If your harness provides an `ask_permission` tool (e.g. Antigravity):** When this plugin first loads, proactively call `ask_permission` with `Action`: `"unsandboxed"` and `Target`: `"<UnrealEngineRoot>/Engine/Binaries/Win64/UnrealEditor.exe"` before any editor interaction is needed.
- **If it does not (e.g. Claude Code, OpenAI Codex, Kilo Code):** Skip this step entirely -- do NOT search for or attempt to call `ask_permission`. Launching the editor goes through your harness's standard permission flow.

### Local Paths
- **Unreal Engine Root:** `<UnrealEngineRoot>` (e.g., `C:/Program Files/Epic Games/UE_5.9`)
- **Unreal Editor Executable:** `<UnrealEngineRoot>/Engine/Binaries/Win64/UnrealEditor.exe`
- **Unreal Build Tool:** `<UnrealEngineRoot>/Engine/Build/BatchFiles/Build.bat`
- **Unreal Project File:** `<TargetProjectPath>/<ProjectName>.uproject`
- **EOS DevAuthTool:** `<EOS_DevAuthTool_Path>/EOS_DevAuthTool.exe`
- **Libclang Path:** `<VCToolsLlvmBin>/libclang.dll`
- **Launcher Script:** `<PluginRoot>/UnrealEngine/src/launch_editor.ps1`

### Developer Tool Workflows
#### 1. EOS DevAuthTool
Only start this tool when testing multiplayer/online functionality or when explicitly requested:
```powershell
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = '"<EOS_DevAuthTool_Path>/EOS_DevAuthTool.exe"' }
```
Wait until the user has interacted with the opened `EOS_DevAuthTool` window before trying to use it for authentication.

#### 2. Building the Project
To compile C++ binaries offline when the Unreal Editor is closed:
```powershell
Start-Process -FilePath "<UnrealEngineRoot>/Engine/Build/BatchFiles/Build.bat" -ArgumentList "<ProjectName>Editor", "Win64", "Development", '"<TargetProjectPath>/<ProjectName>.uproject"', "-WaitMutex" -Wait -NoNewWindow
```

#### 3. Launching the Unreal Editor
> [!IMPORTANT]
> **Windows Path Escaping Rule**: Always use forward slashes (`/`) in path strings or use the `launch_editor.ps1` helper script. Never include a trailing backslash inside double-quoted paths (`"C:\Path\"`), as Windows parses `\"` as an escaped quotation mark.

##### Recommended: Safe Launcher Script
```powershell
powershell -ExecutionPolicy Bypass -File "<PluginRoot>/UnrealEngine/src/launch_editor.ps1" -ProjectPath "<TargetProjectPath>/<ProjectName>.uproject"
```

##### Direct Launch (Forward Slashes)
```powershell
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = '"<UnrealEngineRoot>/Engine/Binaries/Win64/UnrealEditor.exe" "<TargetProjectPath>/<ProjectName>.uproject"' }
```

##### Multiplayer / EOS Launch
```powershell
Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = '"<UnrealEngineRoot>/Engine/Binaries/Win64/UnrealEditor.exe" "<TargetProjectPath>/<ProjectName>.uproject" -CustomConfig=EOS -AUTH_TYPE=developer -AUTH_LOGIN=localhost:8080 -AUTH_PASSWORD=<DevPassword>' }
```
<!-- LOCAL_ENV_END -->

## 3. Coding Guardrails & Engine Invariants

### Strict Pointer & Lifecycle Safety
- **Data Asset Pointer Validation**: Always verify `IsValid()` or check against `nullptr` before dereferencing Data Asset pointers (e.g., `GunDataAsset->VisualizationAsset`, `Sprite->GetFName()`). Unassigned or broken references in `.uasset` files will hard-crash the editor, particularly in Mass ECS and Niagara subsystems.
- **Avoid Pointer Chaining (Early Init)**: Never chain pointer calls (e.g. `GetMatchInstance()->GetMapGridSize()`) without validating intermediate pointers. Subsystems or game states may be uninitialized during early lifecycle or standalone UI testing. Always provide fallback defaults when pointers are null.
- **UI Dynamic Hover & Synthetic Events**: Handlers receiving data asset pointers from UI events (e.g., `OnUpgradeSlotHovered`, `AreUnitAttributesMatchingUpgradeRequirements`) MUST verify the pointer before accessing properties. Slate synthetic mouse-move events frequently pass `nullptr` for unpopulated slots.

### Config & INI Serialization Rules
- **TMap Property Delimiters**: Unreal's `FMapProperty::ImportText_Internal` fails to parse multi-line `+MapKey=` entries. Multi-entry TMaps in `DefaultGame.ini` must be formatted on a single line using parentheses pairs:
  `MapProperty=((Key1,Val1),(Key2,Val2))`

## 4. Skills Directory
Read the corresponding file with your harness's native file-reading tool when performing these tasks:
- [unreal-setup](../unreal-setup/SKILL.md): Discovery and scaffolding of target project `project-index` and workspace guidelines.
- [python-env](../python-env/SKILL.md): Python environment, plugin build, and automated test suite execution.
- [blueprint-authoring](../blueprint-authoring/SKILL.md): Authoring `.uasset` Blueprints (`BlueprintDslToolset`, `BlueprintToolset`, `WidgetBlueprintEditorSubsystem`).
- [setup-replication](../setup-replication/SKILL.md): Network replication, RepNotify, and RPC configuration.
- [add-component](../add-component/SKILL.md): Attaching and configuring `UActorComponent` / `USceneComponent` hierarchies (`ActorToolset`, `ObjectToolset`).
- [setup-input](../setup-input/SKILL.md): Enhanced Input `UInputAction`, `UInputMappingContext`, modifiers, and triggers.
- [niagara-authoring](../niagara-authoring/SKILL.md): Niagara VFX creation and modification (`NiagaraToolsets`).
- [generate-assets](../generate-assets/SKILL.md): Generative 3D mesh/audio pipelines and UE 5.9 asset import (`StaticMeshToolset`, `TextureToolset`, `SoundWaveToolset`, `MaterialToolset`).
- [create-actor](../create-actor/SKILL.md): Spawning level actors and creating Blueprint or C++ Actor classes.
- [create-interface](../create-interface/SKILL.md): Creating C++ and Blueprint interfaces (`UINTERFACE`, `AddInterface`).
- [unreal-testing-sops](../unreal-testing-sops/SKILL.md): Running `AutomationTestToolset` and interactive PIE verification loops.
- [pie-verifier](../pie-verifier/SKILL.md): Play-In-Editor UI inspection (`SlateInspectorToolset`, `UMGDebuggerSubsystem`), input injection, and `CallPIEActorFunction`.
