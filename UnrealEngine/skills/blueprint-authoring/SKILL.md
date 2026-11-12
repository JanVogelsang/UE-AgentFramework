---
name: blueprint-authoring
description: Use this skill when creating, modifying, compiling, or inspecting Unreal Engine Blueprints (`UBlueprint`, `UWidgetBlueprint`, `UAnimBlueprint`) in UE 5.9. Covers declarative Blueprint DSL authoring (`BlueprintToolsets.BlueprintDslToolset`), structured graph/variable operations (`BlueprintToolsets.BlueprintToolset`), component management (`EditorToolset.ActorToolset`), and UMG Widget Blueprint authoring (`UMGEditor.WidgetBlueprintEditorSubsystem`, `WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset`).
---

# Blueprint & UMG Authoring (Unreal Engine 5.9)

In Unreal Engine 5.9, Blueprint and UMG authoring uses Epic's native toolsets exposed over `/mcp` (pass module-qualified `toolset_name` values to `describe_toolsets`, `describe_tools`, and `call_tool`): `BlueprintToolsets.BlueprintDslToolset`, `BlueprintToolsets.BlueprintToolset`, `EditorToolset.ActorToolset`, `EditorToolset.ObjectToolset`, `EditorToolset.AssetToolset`, `UMGEditor.WidgetBlueprintEditorSubsystem`, and `WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset`. Legacy clipboard T3D paste injection is retired.

## 1. Declarative Blueprint Graph Authoring (`BlueprintToolsets.BlueprintDslToolset`)

Prefer `BlueprintToolsets.BlueprintDslToolset` whenever reading or editing EventGraphs, Function graphs, or Macro graphs:
1. **Discover Syntax & Node Signatures:**
   - Call `BlueprintToolsets.BlueprintDslToolset.Docs` or `BlueprintToolsets.BlueprintDslToolset.Schema` (`AssetPath`, `NodeType`) if you need DSL syntax reference.
   - Call `BlueprintToolsets.BlueprintDslToolset.Read` (`AssetPath`, optional `Scope`, `bIncludePositions`) on the target Blueprint asset path (e.g., `/Game/Blueprints/BP_Player`) to inspect existing graphs, variables, and node connections in compact text DSL format.
2. **Surgical Graph Edits:**
   - Call `BlueprintToolsets.BlueprintDslToolset.Edit` (`AssetPath`, `Edits`, `Revision`, optional `Scope`) to apply targeted changes to an existing graph, or `BlueprintToolsets.BlueprintDslToolset.Write` (`AssetPath`, `Dsl`, optional `Scope`) to author a full graph definition.
   - Use `BlueprintToolsets.BlueprintDslToolset.Diff` (`OldAssetPath`, `NewAssetPath`) to compare versions and `BlueprintToolsets.BlueprintDslToolset.FindNodeReferences` (`AssetPath`, `Query`) to locate usages across graphs before refactoring.

## 2. Structured Asset, Variable, Component & Node Operations (`BlueprintToolsets.BlueprintToolset`, `EditorToolset.ActorToolset`, `EditorToolset.ObjectToolset`)

Use `BlueprintToolsets.BlueprintToolset`, `EditorToolset.ActorToolset`, and `EditorToolset.ObjectToolset` for asset lifecycle, variables, components, and explicit pin wiring:
- **Create & Compile (`BlueprintToolsets.BlueprintToolset`):**
  - `BlueprintToolsets.BlueprintToolset.Create` (`FolderPath`, `AssetName`, `AssetType`) — Create a new Blueprint asset from a parent class (`Actor`, `Pawn`, `Character`, `UserWidget`, etc.).
  - `BlueprintToolsets.BlueprintToolset.CompileBlueprint` (`Blueprint`, `bWarningsAsErrors`) — Compile the Blueprint and return structured compiler diagnostics. Always compile after graph or variable changes, then save via `EditorToolset.AssetToolset.SaveAsset`.
- **Variables, Functions & Interfaces (`BlueprintToolsets.BlueprintToolset`):**
  - `BlueprintToolsets.BlueprintToolset.AddVariable`, `AddStructVariable`, `AddObjectVariable`, `ListVariables`, `RemoveVariable`, `SetVariableReplication`
  - `BlueprintToolsets.BlueprintToolset.AddFunctionGraph`, `RemoveFunctionGraph`, `AddFunctionParam`, `AddStructFunctionParam`, `AddObjectFunctionParam`
  - `BlueprintToolsets.BlueprintToolset.AddInterface`, `RemoveInterface`, `ListInterfaces`
- **Components & CDO Defaults (`EditorToolset.ActorToolset` & `EditorToolset.ObjectToolset`):**
  - `EditorToolset.ActorToolset.AddComponent` (`Owner`, `ComponentType`, `Name`) — Attach a `UActorComponent` or `USceneComponent` to a Blueprint or actor instance.
  - `EditorToolset.ActorToolset.GetComponents`, `GetRootComponent`, `GetParentComponent`, `SetParentComponent`, `RemoveComponent` — Inspect and reorganize component hierarchies.
  - Use `BlueprintToolsets.BlueprintToolset.GetDefaultObject` paired with `EditorToolset.ObjectToolset.GetProperties` / `EditorToolset.ObjectToolset.SetProperties` (`Instance`, `Values` JSON string) to read or modify default properties on the Blueprint CDO or component templates.
- **Granular Node Wiring & Layout (`BlueprintToolsets.BlueprintToolset`):**
  - `BlueprintToolsets.BlueprintToolset.FindNodeTypes`, `GetNodeTypePins`, `CreateNode`, `DeleteNode`
  - `BlueprintToolsets.BlueprintToolset.AddEvent`, `AddEventDispatcher`, `AddComponentBoundEvent`
  - `BlueprintToolsets.BlueprintToolset.ConnectPins`, `DisconnectPins`, `SetPinValue`, `GetPinValue`
  - `BlueprintToolsets.BlueprintToolset.ArrangeGraphLayered` / `ArrangeNodes` — Automatically organize node layout using UE 5.9's native graph layout tools.

## 3. UMG Widget Blueprints (`UMGEditor.WidgetBlueprintEditorSubsystem` & `WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset`)

When building or modifying `UWidgetBlueprint` (`WBP_*`) UI assets:
1. **Create & Inspect Hierarchy:**
   - Call `UMGEditor.WidgetBlueprintEditorSubsystem.CreateWidgetBlueprint` (`ListWidgetClasses` / `GetWidgetClassInfo` help pick widget classes) to create a new widget Blueprint.
   - Call `UMGEditor.WidgetBlueprintEditorSubsystem.GetWidgets` or `WidgetStaticAnalysisToolset.WidgetStaticAnalysisToolset.DescribeWidget` (plus `GetWidgetTreeDepth`, `GetWidgetConnectionMap`, `GetViewModelConnectionMap`) to inspect the widget tree, slot metadata, and bindings.
2. **Add & Arrange Widgets:**
   - Call `UMGEditor.WidgetBlueprintEditorSubsystem.CreateAndAddWidget` to add child widgets (`CanvasPanel`, `VerticalBox`, `Overlay`, `TextBlock`, `Button`, `Image`, etc.) to a parent panel, or `UMGEditor.WidgetBlueprintEditorSubsystem.SetNamedSlotContent` for named slots (`GetNamedSlots`).
   - Call `UMGEditor.WidgetBlueprintEditorSubsystem.MoveWidget`, `WrapWidgets`, `RenameWidget`, `ReplaceWidgetWithTemplate` / `ReplaceWidgetWithNamedSlot` / `ReplaceWidgetWithChild`, or `RemoveWidget` to refactor layout containers.
3. **Configure Layout Slots & Properties:**
   - Use `EditorToolset.ObjectToolset.ListProperties` / `EditorToolset.ObjectToolset.GetPropertySchema` / `EditorToolset.ObjectToolset.SetProperties` on the widget or its `Slot` object (`Widget->Slot`) to set anchors, offsets, alignment, padding, size rules, visibility, and styling.
4. **Compile & Bind:**
   - Call `UMGEditor.WidgetBlueprintEditorSubsystem.CompileWidgetBlueprint` (or `BlueprintToolsets.BlueprintToolset.CompileBlueprint`) and bind widget events with `UMGEditor.WidgetBlueprintEditorSubsystem.BindToEventProperty` (plus `BlueprintToolsets.BlueprintToolset.AddComponentBoundEvent` or `BlueprintToolsets.BlueprintDslToolset.Edit`).
