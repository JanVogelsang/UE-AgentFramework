# UE-AgentFramework (v2.0 for Unreal Engine 5.9) - Project Overview & Agent Instructions

## Plugin Nature & Context
**IMPORTANT:** This repository contains the source code for a **plugin**, not a standalone game project. The purpose of this codebase is to be compiled and installed into *other* target Unreal Engine 5.9 game projects to give them AgentFramework AI capabilities alongside UE 5.9's built-in `ModelContextProtocol` and `ToolsetRegistry`.
> **Version Compatibility:** `main` targets **Unreal Engine 5.9** exclusively (`v2.0.0`). For Unreal Engine 5.7 and 5.8 projects, check out git tag `v1.1.0-ue5.8`.

## Architecture: Native UE 5.9 MCP + Clang AST Bridge
1. **In-Editor UE 5.9 Toolsets (`AgentFramework/`)**:
   - Registers 6 native `UToolsetDefinition` classes (`AgentFrameworkActions.<ClassNameWithoutU>`) with UE 5.9's `UToolsetRegistry`, exposed automatically via UE 5.9's built-in `ModelContextProtocol` Streamable HTTP server (`http://127.0.0.1:8000/mcp`, discovered/invoked via `list_toolsets`, `describe_toolsets`, `describe_tools`, `call_tool` when `bEnableToolSearch = true`):
     - `UAgentFrameworkCppToolset` (`CreateCppClass`, `ScaffoldCppClass`, `RegenerateProjectFiles` [async], `GetCppReflectionInfo`)
     - `UAgentFrameworkBuildToolset` (`BuildLighting`, `PackageProject` [async])
     - `UAgentFrameworkEnhancedInputAuthoringToolset` (`CreateInputAction`, `CreateInputMappingContext`, `AddInputMapping`, `ConfigureInputMappingModifiersTriggers`)
     - `UAgentFrameworkAnimationAuthoringToolset` (`CreateAnimBlueprint`, `CreateAnimMontage`, `ConfigureAnimMontage`, `CreateBlendSpace`, `GetSkeletonAnimationInfo`, `CreateIKRig`, `CreateIKRetargeter`, `ConfigureMotionMatchingDatabase`)
     - `UAgentFrameworkGASAuthoringToolset` (`CreateAttributeSetClass`, `CreateGameplayEffectBlueprint`)
     - `UAgentFrameworkPIERuntimeToolset` (`CallPIEActorFunction`)
   - All other editor domains (Blueprints & Blueprint DSL, UMG/MVVM/CommonUI/WidgetAnimation/WidgetStaticAnalysis/UMGDebugger, Niagara, MetaSound, PCG, Materials & TextureGraph, Level & Actors/Components, DataTables/DataAssets/Structs/Enums, Static/Skeletal Meshes & Physics & GeometryTools, Sequencer/ControlRig/SequencerAnimMixer, BehaviorTree/EQS/StateTree/NavMesh, GAS Tags/Abilities/Cues/Inspector, PIE Start/Stop, Runtime Slate Inspection & Input Injection, Console/Logs/Profilers/RewindDebugger, Live Coding, FileSystem, ConfigSettings, Plugins/GameFeatures, KnowledgeBase, Fab, SceneCapture, and Automation Tests) are handled **100% by UE 5.9's built-in Toolsets**.
2. **Dual-MCP External Bridge & Clang AST Server (`UnrealEngine/`)**:
   - `UnrealEngine/bridge/main.py`: Stdio MCP proxy (`unrealengine` server entry in `UnrealEngine/install.ps1`) that forwards tool discovery and calls directly to UE 5.9's in-editor Streamable HTTP server (`http://127.0.0.1:8000/mcp`).
   - `UnrealEngine/ExternalServer/src/main.py`: Separate stdio MCP server (`cpp-ast-rag` server entry in `UnrealEngine/install.ps1`) providing local `libclang` C++ AST symbol resolution, macro expansion inspection, multi-file call graphs, `generate_compile_commands`, and ChromaDB documentation RAG. Static instructions and `SKILL.md` guides live under `UnrealEngine/AGENTS.md`, `UnrealEngine/CLAUDE.md`, and `UnrealEngine/skills/`.

## Common Workflows

### 1. Building the Plugin
Unreal Engine compilation relies on UBT. Running builds concurrently can trigger a conflicting UAT instance error. Always run the build script from the repository root using the UAT mutex bypass:
```powershell
$env:uebp_UATMutexNoWait = '1'
powershell -ExecutionPolicy Bypass -File .\build_plugin.ps1 -UEVersion "5.9" -NoZip
```
This builds the plugin against UE 5.9 and copies the binaries to the target test game project.

### 2. Running Automated Tests
Always use the PowerShell wrapper to ensure the correct Python interpreter (which has `pip` and `pytest`) is used, avoiding conflicts with MSYS2 environments.
*   To run Python tests: `powershell -File .\Tests\run_tests.ps1`
*   To run all C++ and Python tests sequentially: `powershell -File .\Tests\run_all_tests.ps1`

### 3. Agent Coding Guidelines & Constraints
Before writing code or utilizing MCP tools, you **MUST** read the detailed guidelines at `UnrealEngine/AGENTS.md`:
*   **Always Favor UE 5.9 Native Toolsets**: Never duplicate functionality that UE 5.9's built-in `Toolsets` or `ToolsetRegistry` already provide.
*   **Minimal Module Dependencies Directive**: The `AgentFramework` plugin C++ code must maintain the absolute minimum set of module dependencies required for core Editor integration. If a task or feature for a host project requires additional UE feature modules (e.g., `Foliage`, `StateTree`, `GeometryCore`, `GameplayAbilities`), add that module dependency to the **host game project's `.Build.cs`**, NOT to the `AgentFramework` plugin.
*   **Ticking Guardrails**: Never place high-overhead logic or unchecked array accesses inside `Tick` or `NativeTick` functions.
*   **AST Updates**: Whenever new C++ files or headers are added, invoke `generate_compile_commands` to regenerate `compile_commands.json` so the Python AST server remains accurate.
*   **Automation Test Run Safeguards**: Semicolon-chain `; Quit` to test commands to avoid hanging standalone processes.

### 4. Target Project Context vs. Plugin Context
Do not confuse the UE-AgentFramework plugin development context with the context of the game project using it. **When operating inside a target game project**, the auto-generated `project-index` skill (`.agents/skills/project-index/`) is used to understand the host game's gameplay specifications and systems. Do not look for a `project-index` skill to understand the UE-AgentFramework plugin architecture itself.

### 5. Installing & Testing on a Target Project
See `Documentation/INSTALL_AND_VERIFY.md` for the full install, verification, and E2E test procedure against the standard `AgentFrameworkTest` target project.

### 6. Build & Deployment Hygiene (DLL Locks & Path Portability)
1. **Editor DLL File Locks:**
   The Unreal Editor locks the compiled plugin DLLs (`UnrealEditor-AgentFrameworkActions.dll`, etc.) while running.
   *   **Rule:** Before verifying new plugin features in a live editor, always ensure the Editor was **fully closed** when the binaries were copied.
   *   **Rule:** If a copy step fails with an "Access Denied" or "Used by another process" error, explicitly ask the user to close the Editor and retry.
2. **Dynamic Home Directory Resolution:**
   Avoid using hardcoded user home directories (e.g. `C:\Users\<username>`) in scripts, configurations, or paths.
   *   **Rule:** Always resolve the user profile path dynamically (`$env:USERPROFILE` in PowerShell, `os.path.expanduser('~')` in Python, `FPaths::ProjectDir()` in C++).
