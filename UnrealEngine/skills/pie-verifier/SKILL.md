---
name: pie-verifier
description: Use this skill to autonomously start Play-In-Editor (PIE), inspect live Slate/UMG widgets and runtime world state, inject input or invoke actor UFunctions, and verify gameplay behavior in Unreal Engine 5.9.
---

# Play-In-Editor (PIE) Runtime Verifier (Unreal Engine 5.9)

Always verify gameplay, UI, and AI changes inside a live Play-In-Editor (PIE) session using UE 5.9's native runtime toolsets and `AgentFrameworkActions.AgentFrameworkPIERuntimeToolset` via `/mcp` (`describe_toolsets` + `call_tool`).

## 1. Starting & Stopping PIE (`EditorToolset.EditorAppToolset`)

- **Check Status:** Call `EditorToolset.EditorAppToolset.IsPIERunning` to see if a session is already active.
- **Start PIE:** Call `EditorToolset.EditorAppToolset.StartPIE` to launch Play-In-Editor.
- **Stop PIE:** Always call `EditorToolset.EditorAppToolset.StopPIE` once verification is complete so assets can be saved and modified cleanly.

## 2. Inspecting & Interacting with Live UI (`SlateInspectorToolset.SlateInspectorToolset` & `UMGEditor.UMGDebuggerSubsystem`)

While PIE is running:
- **Discover & Inspect Live Widgets (`SlateInspectorToolset.SlateInspectorToolset`):**
  - `Snapshot` (`Ref`, `MaxDepth`, `bIncludeSourceLocations`) — Capture the live Slate widget hierarchy and element references.
  - `GetUMGInfo` (`Ref`) — Inspect UMG widget backing metadata for a captured Slate ref.
  - `Screenshot` (`Ref`) — Capture an image of a widget/window.
  - `UMGEditor.UMGDebuggerSubsystem` (`StartWidgetLifetimeLogging`, `GetWidgetLifetimeLogStatus`, `GetWidgetLifetimeEvents`, `StopWidgetLifetimeLogging`) — Record widget Constructed/Destructed/Rebuilt/Invalidated events during PIE.
- **Simulate UI Interaction (`SlateInspectorToolset.SlateInspectorToolset`):**
  - `Click` (`Ref`, `Params`), `Hover` (`Ref`, `Params`), `HitTest` (`Ref`, `Params`), `Scroll` (`Ref`, `Params`)
  - `Type` (`Ref`, `Text`, `Submit`), `PressKey` (`Key`, `UserIndex`, `RepeatCount`), `Focus` (`Ref`, `UserIndex`), `SelectOption` (`Ref`, `Value`), `Drag` (`StartRef`, `EndRef`, `Params`), `WaitFor` (`Text`, `TextGone`)
  - Call `LastRefusal` if an interaction fails to diagnose why a widget could not be clicked/focused.

## 3. Simulating Player Input (`InputToolset.InputToolset`)

- **Inject Enhanced Input / Keys:**
  - Call `InputToolset.InputToolset.ApplyInput` (`Entries` array targeting either `Action` or `Key` + `Value` vector, and `DurationSeconds`) to drive Enhanced Input actions or legacy keys on the active PIE local player.
  - Call `InputToolset.InputToolset.IsInputApplied` (`Entries`) to verify active key state.

## 4. Invoking Live PIE Actor Functions (`AgentFrameworkActions.AgentFrameworkPIERuntimeToolset`)

Use `AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction` to execute `BlueprintCallable` or `Exec` `UFunction`s on actors (or components) running inside the live PIE world (`GEditor->PlayWorld`):
- **`CallPIEActorFunction`**:
  - `ActorIdentifier` — Matches actor name or actor label in the active PIE world.
  - `FunctionName` — Target `UFunction` on the actor (or attached component).
  - Optional `ParametersJson` — JSON object mapping parameter names to values.
  - Returns `FAgentFrameworkPIEFunctionResult` (`ActorName`, `ActorLabel`, `ActorClass`, `FunctionName`, `ReturnValueJson`, `OutParametersJson`).
- Use `EditorToolset.LevelToolset.FindActors` and `EditorToolset.ObjectToolset.GetProperties` for level actor discovery and property reads.

## 5. Checking Runtime Logs & Visuals (`ConsoleToolset.LogsToolset` & `SceneCaptureToolset.SceneCaptureToolset`)

- **Runtime Logs (`ConsoleToolset.LogsToolset`):** Call `GetLogEntries` (`LogCategory`, `Pattern`, `MaxEntries`, `LogFilePath`, `MaxCharsPerEntry`) or `GetLogCategories` to confirm no script errors or failed assertions occurred (`LogBlueprintUserMessages`, `LogTemp`, `LogAbilitySystem`, etc.).
- **Visual Capture (`SceneCaptureToolset.SceneCaptureToolset` & `EditorToolset.EditorAppToolset`):** Call `SceneCaptureToolset.SceneCaptureToolset.CaptureGameViewport` (`World`, `MaxResolution`), `SceneCaptureToolset.SceneCaptureToolset.CaptureActiveEditorView`, or `EditorToolset.EditorAppToolset.CaptureEditorImage` when visual confirmation is helpful.
