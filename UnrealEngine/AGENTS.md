# Unreal Engine 5.9 Agent Guidelines

You are an expert Unreal Engine 5.9 AI agent connected to UE 5.9's built-in **ModelContextProtocol** server (`http://127.0.0.1:8000/mcp`, proxied by `unrealengine`), UE 5.9's **ToolsetRegistry** (70+ built-in Engine Toolsets + 6 `AgentFrameworkActions` Toolsets), and the external **Clang AST & Documentation RAG Server** (`cpp-ast-rag`).

---

## 1. UE 5.9 Toolset Discovery & Invocation (`ToolsetRegistry`)

UE 5.9 uses lazy toolset discovery (`bEnableToolSearch = true`) via four MCP meta-tools:
1. **`list_toolsets`**: Lists all registered module-qualified toolset names (`<Module>.<Toolset>`, e.g., `BlueprintToolsets.BlueprintDslToolset`, `EditorToolset.ActorToolset`, `AgentFrameworkActions.AgentFrameworkCppToolset`) in the running Unreal Editor.
2. **`describe_toolsets`**: Returns the tool summaries inside one or more toolsets (`toolset_names`, `include_schemas`).
3. **`describe_tools`**: Returns the full JSON input/output schema and access mode (`Read`, `Write`, `Execute`) for specific qualified tools (`tool_names` formatted as `<Module>.<Toolset>.<Tool>`).
4. **`call_tool`**: Executes a tool by `toolset_name` (`<Module>.<Toolset>`), `tool_name`, and `arguments`. Typed USTRUCT / async string responses are returned wrapped in `{"returnValue": ...}`.

### Key UE 5.9 Built-in Toolsets (Always Prefer Over Raw Scripting)
- **Blueprints (`BlueprintToolsets` plugin):**
  - `BlueprintToolsets.BlueprintDslToolset`: Textual Blueprint DSL (`Read`, `Write`, `Edit`, `Docs`, `Schema`, `Diff`, `FindNodeReferences`) — read and edit entire Blueprint graphs in a compact human/LLM-readable DSL!
  - `BlueprintToolsets.BlueprintToolset`: Fine-grained Blueprint manipulation (`Create`, `CompileBlueprint`, `GetDefaultObject`, `ListGraphs`, `GetGraph`, `AddVariable`, `AddStructVariable`, `AddObjectVariable`, `ListVariables`, `SetVariableReplication`, `AddFunctionGraph`, `AddFunctionParam`, `AddInterface`, `CreateNode`, `AddEvent`, `AddEventDispatcher`, `AddComponentBoundEvent`, `ConnectPins`, `DisconnectPins`, `SetPinValue`, `ArrangeGraphLayered`, `ArrangeNodes`, `FindNodes`, etc.).
- **UMG, CommonUI & MVVM:**
  - `UMGEditor.WidgetBlueprintEditorSubsystem`: `CreateWidgetBlueprint`, `CreateAndAddWidget`, `RemoveWidget`, `MoveWidget`, `WrapWidgets`, `RenameWidget`, `ReplaceWidgetWithTemplate`, `ReplaceWidgetWithNamedSlot`, `ReplaceWidgetWithChild`, `GetWidgets`, `GetNamedSlots`, `SetNamedSlotContent`, `BindToEventProperty`, `ToggleWidgetAsVariable`, `ListWidgetBlueprints`, `ListWidgetClasses`, `CompileWidgetBlueprint`.
  - `UMGEditor.UMGDebuggerSubsystem`: Runtime PIE widget lifetime recorder (`StartWidgetLifetimeLogging`, `StopWidgetLifetimeLogging`, `ClearWidgetLifetimeLog`, `GetWidgetLifetimeLogStatus`, `GetWidgetLifetimeEvents`, `ShowWidgetLifetimeLogWindow`).
  - `WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset` (`DescribeWidget`, `GetWidgetTreeDepth`, `TraceWidgetPropertyOverrides`, `GetWidgetConnectionMap`, `GetViewModelConnectionMap`), `WidgetAnimationToolset.WidgetAnimationToolset`, `MVVMToolset.MVVMToolset`, `CommonUIToolset.CommonUIToolset`.
- **StateTree, BehaviorTree, EQS & NavMesh:**
  - `StateTreeToolset` plugin (`state_tree_toolset.state_tree.StateTreeToolset` Python toolset): `compile`, `mark_as_modified`, `request_ui_refresh`, `get_editor_data`, `get_root_states`, `get_all_states`, `get_children`, `get_tasks`, `get_enter_conditions`, `get_considerations`, `get_transitions`, `get_global_tasks`, `get_evaluators`, `add_sub_tree`, `add_child_state`, `add_task`, `add_blueprint_task`, `add_enter_condition`, `add_evaluator`, etc.
  - `AIModuleToolset` plugin: `AIModuleToolset.BehaviorTreeToolset`, `AIModuleToolset.EnvironmentQueryToolset`; `NavMeshToolset.NavMeshToolset`.
- **GAS & Gameplay Tags:**
  - `GameplayTagsToolset.GameplayTagsToolset`; `GASToolsets` plugin: `GASToolsets.GameplayAbilityToolset`, `GASToolsets.GameplayCueToolset`, `GASToolsets.AttributeSetToolset`, `GASToolsets.AbilitySystemInspectorToolset`.
- **Niagara, MetaSound, PCG, Materials & TextureGraph:**
  - `NiagaraToolsets` plugin: `NiagaraToolsets.NiagaraToolset_Assets`, `NiagaraToolsets.NiagaraToolset_System`, `NiagaraToolsets.NiagaraToolset_Component`, `NiagaraToolsets.NiagaraToolset_Blueprint`, `NiagaraToolsets.NiagaraToolset_Info`.
  - `MetaSoundToolset` plugin: `MetaSoundToolset.MetaSoundAuthoringToolset`, `MetaSoundToolset.MetaSoundDiscoveryToolset`, `MetaSoundToolset.MetaSoundInspectionToolset`.
  - `PCGToolset` plugin: `PCGToolset.PCGToolset`, `PCGToolset.PCGSpatialToolset`.
  - `EditorToolset` plugin: `EditorToolset.MaterialToolset`, `EditorToolset.MaterialInstanceToolset`; `TextureGraphToolset.TextureGraphTools`.
- **Level, Actors, Components, Meshes, Physics & Geometry:**
  - `EditorToolset` plugin: `EditorToolset.LevelToolset` (`AddToSceneFromClass`, `AddToSceneFromAsset`, `FindActors`, `RemoveFromScene`, etc.), `EditorToolset.ActorToolset` (`AddComponent`, `RemoveComponent`, `GetComponents`, `SetParentComponent`, `GetActorTransform`, `SetActorTransform`, etc.), `EditorToolset.ObjectToolset` (`ListProperties`, `GetPropertySchema`, `GetProperties`, `SetProperties`, `ResetProperties`), `EditorToolset.AssetToolset` (`FindAssets`, `LoadAsset`, `SaveAsset`, `ValidateAssets`, etc.).
  - `EditorToolset.StaticMeshToolset`, `EditorToolset.SkeletalMeshToolset`, `EditorToolset.SoundWaveToolset`, `EditorToolset.TextureToolset`, `EditorToolset.DataAssetToolset`, `EditorToolset.DataTableToolset`, `EditorToolset.UserDefinedEnumToolset`, `EditorToolset.UserDefinedStructToolset`.
  - `PhysicsToolsets.PhysicsAssetToolset`; `GeometryToolset` plugin (`geometry_toolset.geometry_ops.GeometryTools`).
- **Sequencer, ControlRig & Movie Render Pipeline:**
  - `AnimationAssistantToolset.SequencerDirectorToolset`, `AnimationAssistantToolset` Python (`animation_toolset.toolsets.controlrig.ControlRigTools`), `SequencerAnimMixerToolset` Python (`sequencer_animmixer_toolset.toolsets.mixer.SequencerAnimMixerTools`), `MovieRenderPipelineToolset` (AgentSkills for Movie Render Queue/Graph).
- **PIE, Runtime UI & Input Verification:**
  - `EditorToolset.EditorAppToolset` (`StartPIE`, `StopPIE`, `IsPIERunning`, `CaptureEditorImage`, `CaptureAssetImage`), `InputToolset.InputToolset` (`ApplyInput`, `IsInputApplied`), `SlateInspectorToolset.SlateInspectorToolset` (`Snapshot`, `GetUMGInfo`, `Screenshot`, `Click`, `Hover`, `Type`, `PressKey`, `Focus`, `WaitFor`, etc.), `SceneCaptureToolset.SceneCaptureToolset` (`Capture`, `CaptureGameViewport`, `CaptureActiveEditorView`, `CaptureEditorViewport`).
- **Live Coding, Automation Tests, Console, Logs & Profiling:**
  - `LiveCodingToolset.LiveCodingToolset` (`CompileLiveCoding`, `IsLiveCodingReady`, `EnableLiveCoding`), `AutomationTestToolset.AutomationTestToolset` (`DiscoverTests`, `ListTests`, `RunTests`, `RunTestsByFilter`, `GetTestResults`, `GetTestStatus`, `StopTests`), `ConsoleToolset.ConsoleToolset`, `ConsoleToolset.LogsToolset`, `PerfToolset.CsvProfilerToolset`, `PerfToolset.GpuProfilerToolset`, `PerfToolset.StatsToolset`, `RewindDebuggerToolset.RewindDebuggerToolset`.
- **KnowledgeBase, FileSystem, Config, Plugins & Fab:**
  - `KnowledgeBaseToolset.KnowledgeBaseTools` (`ListKnowledgeBases`, `ListKnowledgeBaseTags`, `QueryKnowledgeBase`, `GetKnowledgeEntry`, `WriteKnowledge`, `CreateMarkdownKnowledgeBase`), `FileSystemToolset.FileSystemToolset`, `ConfigSettingsToolset.ConfigSettingsToolset`, `PluginToolset.PluginToolset`, `GameFeaturesToolset.GameFeaturesToolset`, `FabToolset.FabToolset` (`SearchFabListings`, `GetFabListingDetails`, `SearchFabMyLibrary`, `InstallFabListing`, `FabSignIn`).

---

## 2. AgentFramework UE 5.9 Extension Toolsets (6 Custom Toolsets)

`AgentFramework` registers 6 additional `UToolsetDefinition` classes in `UToolsetRegistry` (qualified under `AgentFrameworkActions.<ClassNameWithoutU>`) for workflows not covered by UE 5.9's built-in toolsets:
1. **`AgentFrameworkActions.AgentFrameworkCppToolset`**:
   - `CreateCppClass`, `ScaffoldCppClass`, `RegenerateProjectFiles` (async), `GetCppReflectionInfo`.
2. **`AgentFrameworkActions.AgentFrameworkBuildToolset`**:
   - `BuildLighting`, `PackageProject` (async UAT `BuildCookRun`).
3. **`AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset`**:
   - `CreateInputAction`, `CreateInputMappingContext`, `AddInputMapping`, `ConfigureInputMappingModifiersTriggers`.
4. **`AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset`**:
   - `CreateAnimBlueprint`, `CreateAnimMontage`, `ConfigureAnimMontage`, `CreateBlendSpace`, `GetSkeletonAnimationInfo`, `CreateIKRig`, `CreateIKRetargeter`, `ConfigureMotionMatchingDatabase`.
5. **`AgentFrameworkActions.AgentFrameworkGASAuthoringToolset`**:
   - `CreateAttributeSetClass`, `CreateGameplayEffectBlueprint`.
6. **`AgentFrameworkActions.AgentFrameworkPIERuntimeToolset`**:
   - `CallPIEActorFunction` (invokes a `BlueprintCallable` or `Exec` `UFunction` on a live PIE actor with optional `ParametersJson` and returns `ReturnValueJson` and `OutParametersJson`).

---

## 3. Clang AST & Documentation RAG Tools (`cpp-ast-rag`)

- **`query_cpp_ast`**: Query cached C++ AST for class/symbol declarations, method signatures, `UPROPERTY` metadata, inheritance, and transitive callees.
- **`inspect_macro_expansion` / `query_macro_expansion`**: Inspect preprocessor `#define` macros and Unreal reflection macros (`UCLASS`, `UPROPERTY`, `UFUNCTION`).
- **`visualize_call_graph` / `query_call_graph`**: Generate multi-file call graphs and Mermaid `graph TD` diagrams.
- **`generate_compile_commands`**: Regenerate `compile_commands.json` via UBT and re-index the C++ AST cache.
- **`search_vector_db`**: Query the local Unreal Engine documentation vector database.

---

## 4. Coding & Safety Guardrails

1. **Minimal Module Dependencies Directive:**
   - Keep `AgentFramework` plugin `.Build.cs` dependencies strictly minimal. Add project-specific feature modules (`GameplayAbilities`, `StateTree`, `MassGameplay`, etc.) to the **host game project's `.Build.cs`**, never to `AgentFramework`.
2. **Ticking Guardrails:**
   - Never place `UGameplayStatics::GetAllActorsOfClass`, `LoadObject`, or unchecked array indexing inside `Tick()` or `NativeTick()`. Prefer event-driven delegates and timers.
3. **Compilation & Live Coding:**
   - After modifying existing `.cpp` function bodies, check `LiveCodingToolset.LiveCodingToolset.IsLiveCodingReady` and call `LiveCodingToolset.LiveCodingToolset.CompileLiveCoding`.
   - After adding new C++ classes, headers, or `UPROPERTY`/`UFUNCTION` reflection macros, call `AgentFrameworkActions.AgentFrameworkCppToolset.RegenerateProjectFiles` and `generate_compile_commands` (recompile offline via UBT when Editor binaries must update).
4. **Autonomous Verification:**
   - Verify UI layouts with `WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.DescribeWidget` or `EditorToolset.EditorAppToolset.CaptureAssetImage` / `SceneCaptureToolset.SceneCaptureToolset.CaptureEditorViewport`.
   - Verify runtime gameplay in PIE using `EditorToolset.EditorAppToolset.StartPIE`, `InputToolset.InputToolset.ApplyInput`, `SlateInspectorToolset.SlateInspectorToolset` (`Snapshot`, `GetUMGInfo`, `Click`, `Screenshot`), `AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction`, `UMGEditor.UMGDebuggerSubsystem`, and `ConsoleToolset.LogsToolset.GetLogEntries`.
