# UE-AgentFramework (v2.0 for Unreal Engine 5.9)

**UE-AgentFramework** is an Agentic AI extension plugin and Clang AST MCP toolkit built natively on top of **Unreal Engine 5.9's** `ModelContextProtocol` and `ToolsetRegistry` architecture.

> **Engine Compatibility:**
> - **Unreal Engine 5.9:** Use the `main` branch (`v2.0.0`).
> - **Unreal Engine 5.7 / 5.8:** Check out git tag **`v1.1.0-ue5.8`** (`git checkout v1.1.0-ue5.8`), which contains the legacy standalone HTTP/JSON action router for pre-5.9 engines.

---

## Architecture Overview

In Unreal Engine 5.9, Epic Games introduced a native in-editor **ModelContextProtocol** server (`http://127.0.0.1:8000/mcp`) and **ToolsetRegistry** with built-in C++ and Python toolsets (`AllToolsets` + editor subsystems). When `bEnableToolSearch = true` (UE 5.9 default), MCP exposes four meta-tools (`list_toolsets`, `describe_toolsets`, `describe_tools`, `call_tool`) dispatching `<Module>.<Toolset>` names.

**UE-AgentFramework v2.0** embraces UE 5.9's native infrastructure 100%:
1. **Automatically Enables UE 5.9's Full Agentic Stack:**
   - `ModelContextProtocol` (Streamable HTTP MCP server at `http://127.0.0.1:8000/mcp`, MCP spec `2025-11-25`)
   - Core UE 5.9 **Plugins** and registered **Toolsets** (registered in `ToolsetRegistry` under module-qualified `<Module>.<Toolset>` names returned by `list_toolsets`, e.g., `EditorToolset.ActorToolset`, `BlueprintToolsets.BlueprintToolset`):
     - `ToolsetRegistry` & `AllToolsets`
     - `BlueprintToolsets` (`BlueprintDslToolset`, `BlueprintToolset`)
     - `UMGEditor` (`WidgetBlueprintEditorSubsystem`, `UMGDebuggerSubsystem`), `WidgetStaticAnalysisToolset`, `WidgetAnimationToolset`, `MVVMToolset`, `CommonUIToolset`
     - `StateTreeToolset` (`StateTreeToolset`), `AIModuleToolset` (`BehaviorTreeToolset`, `EnvironmentQueryToolset`), `NavMeshToolset`
     - `NiagaraToolsets` (`NiagaraToolset_Assets`, `NiagaraToolset_System`, `NiagaraToolset_Component`, `NiagaraToolset_Blueprint`, `NiagaraToolset_Info`), `MetaSoundToolset`, `PCGToolset`, `TextureGraphToolset`
     - `EditorToolset` (`ActorToolset`, `AssetToolset`, `LevelToolset`, `ObjectToolset`, `EditorAppToolset`, `MaterialToolset`, `MaterialInstanceToolset`, `StaticMeshToolset`, `SkeletalMeshToolset`, `SoundWaveToolset`, `TextureToolset`, etc.), `PhysicsToolsets` (`PhysicsAssetToolset`), `GeometryToolset` (`GeometryTools`), `AnimationAssistantToolset` (`SequencerDirectorToolset`, `ControlRigTools`), `SequencerAnimMixerToolset` (`SequencerAnimMixerTools`)
     - `GASToolsets` (`GameplayAbilityToolset`, `GameplayCueToolset`, `AttributeSetToolset`, `AbilitySystemInspectorToolset`), `GameplayTagsToolset`
     - `SlateInspectorToolset`, `InputToolset`, `LiveCodingToolset`, `AutomationTestToolset`, `ConsoleToolset` (`ConsoleToolset`, `LogsToolset`), `PerfToolset`, `RewindDebuggerToolset`, `SceneCaptureToolset`, `FileSystemToolset`, `ConfigSettingsToolset`, `PluginToolset`, `GameFeaturesToolset`, `KnowledgeBaseToolset` (`KnowledgeBaseTools`), `FabToolset`
   - `AIAssistant`, `AgentCapability`, `AgentLook`, `UnrealCanvasAnnotations`, `UnrealCanvasBridge` (optional editor integration plugins)
2. **Registers 6 Native `UToolsetDefinition` Extension Toolsets (`AgentFrameworkActions`):**
   - `UAgentFrameworkCppToolset` (`AgentFrameworkActions.AgentFrameworkCppToolset`): C++ class creation (`CreateCppClass`), rich scaffolding (`ScaffoldCppClass`), async UBT/UAT project file regeneration (`RegenerateProjectFiles`), and C++ UHT reflection queries (`GetCppReflectionInfo`).
   - `UAgentFrameworkBuildToolset` (`AgentFrameworkActions.AgentFrameworkBuildToolset`): Editor lighting builds (`BuildLighting`) and async cross-platform UAT `BuildCookRun` project packaging (`PackageProject`).
   - `UAgentFrameworkEnhancedInputAuthoringToolset` (`AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset`): Authoring `UInputAction` (`CreateInputAction`) and `UInputMappingContext` (`CreateInputMappingContext`) assets, key mappings (`AddInputMapping`), and `UInputModifier` / `UInputTrigger` configuration (`ConfigureInputMappingModifiersTriggers`).
   - `UAgentFrameworkAnimationAuthoringToolset` (`AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset`): Authoring `UAnimBlueprint` (`CreateAnimBlueprint`), `UAnimMontage` (`CreateAnimMontage`, `ConfigureAnimMontage`), `UBlendSpace` (`CreateBlendSpace`), skeleton/mesh/sequence inspection (`GetSkeletonAnimationInfo`), `UIKRigDefinition` (`CreateIKRig`), `UIKRetargeter` (`CreateIKRetargeter`), and Motion Matching `UPoseSearchDatabase` (`ConfigureMotionMatchingDatabase`).
   - `UAgentFrameworkGASAuthoringToolset` (`AgentFrameworkActions.AgentFrameworkGASAuthoringToolset`): Generating C++ `UAttributeSet` classes with `ATTRIBUTE_ACCESSORS` and replication boilerplate (`CreateAttributeSetClass`), and authoring `UGameplayEffect` Blueprints (`CreateGameplayEffectBlueprint`).
   - `UAgentFrameworkPIERuntimeToolset` (`AgentFrameworkActions.AgentFrameworkPIERuntimeToolset`): Invoking `BlueprintCallable` or `Exec` `UFunction`s on live actors in the active PIE world with optional JSON arguments and structured return/out-parameter serialization (`CallPIEActorFunction`).
3. **Dual-MCP External Bridge & Clang AST Server (`UnrealEngine/`):**
   - `UnrealEngine/bridge/main.py`: Stdio MCP proxy (`unrealengine` server configured by `UnrealEngine/install.ps1`) forwarding requests to UE 5.9's in-editor Streamable HTTP MCP server (`http://127.0.0.1:8000/mcp`).
   - `UnrealEngine/ExternalServer/src/main.py`: Separate stdio MCP server (`cpp-ast-rag` server configured by `UnrealEngine/install.ps1`) providing offline/online `libclang` C++ AST symbol resolution (`query_cpp_ast`), macro expansion inspection (`inspect_macro_expansion`), multi-file Mermaid call graphs (`visualize_call_graph`), `generate_compile_commands`, and ChromaDB documentation search (`search_vector_db`).

---

## Building the Plugin

Run the PowerShell build script from the repository root:

```powershell
$env:uebp_UATMutexNoWait = '1'
powershell -ExecutionPolicy Bypass -File .\build_plugin.ps1 -UEVersion "5.9" -NoZip
```

---

## Running Automated Tests

```powershell
powershell -File .\Tests\run_tests.ps1
```
