---
name: unreal-testing-sops
description: Standard Operating Procedures (SOPs) for running automated C++ tests, functional tests, and interactive PIE verification in Unreal Engine 5.9 using `AutomationTestToolset`, `EditorAppToolset`, `InputToolset`, `SlateInspectorToolset`, `ConsoleToolset`, `LogsToolset`, and `AgentFrameworkPIERuntimeToolset`.
---

# Unreal Engine 5.9 Testing & Verification SOPs

Use this guide whenever verifying C++ systems, Blueprint logic, Enhanced Input, or UI flows in Unreal Engine 5.9 via `/mcp` (`describe_toolsets` + `call_tool`).

## 1. Running Unreal Automation Tests (`AutomationTestToolset.AutomationTestToolset`)

When verifying `IMPLEMENT_SIMPLE_AUTOMATION_TEST` or Functional Tests:
1. Call `AutomationTestToolset.AutomationTestToolset.DiscoverTests` (if newly compiled) and `ListTests` (`NameFilter`, `TagFilter`, `Limit`) to locate matching tests (e.g., `AgentFramework.`, `Project.`).
2. Call `AutomationTestToolset.AutomationTestToolset.RunTests` (`TestNames`) or `RunTestsByFilter` (`FilterExpression`), poll with `GetTestStatus` if needed, and retrieve structured pass/fail diagnostics via `GetTestResults`.
3. If running headless from the command line, always semicolon-chain `; Quit` to `-ExecCmds="Automation RunTests <Filter>; Quit"` so the Editor process exits cleanly after tests complete.

## 2. Interactive PIE Verification Loop

When verifying gameplay actors, abilities, inputs, or UI in a live level:
1. **Launch PIE:**
   - Call `EditorToolset.EditorAppToolset.StartPIE` and confirm `EditorToolset.EditorAppToolset.IsPIERunning` returns `true`.
2. **Inspect Initial State:**
   - Call `AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction` to run read/state-inspection `UFunction`s on live actors in the PIE world (`GEditor->PlayWorld`).
   - Call `SlateInspectorToolset.SlateInspectorToolset.Snapshot` and `UMGEditor.UMGDebuggerSubsystem.StartWidgetLifetimeLogging` (when debugging dynamic widgets) to observe live UI hierarchies.
3. **Simulate Actions:**
   - **Player Input:** Call `InputToolset.InputToolset.ApplyInput` (`Entries`, `DurationSeconds`) to trigger Enhanced Input actions or legacy keys.
   - **Direct Actor Calls:** Call `AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction` (`ActorIdentifier`, `FunctionName`, optional `ParametersJson`) to invoke `BlueprintCallable` / `Exec` functions on live PIE actors.
   - **UI Interaction:** Call `SlateInspectorToolset.SlateInspectorToolset.Click`, `Hover`, `Type`, `PressKey`, `Focus`, or `SelectOption` targeting captured widget refs from `Snapshot`.
   - **Console Commands:** Call `ConsoleToolset.ConsoleToolset.RunConsoleCommand` (`Command`, e.g., `stat unit`, `showdebug abilitysystem`) or `SetCVarValue`.
4. **Assert Post-Conditions, Visuals & Logs:**
   - Re-verify runtime actor state via `AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction`.
   - Check `ConsoleToolset.LogsToolset.GetLogEntries` (`LogCategory`, `Pattern`, `MaxEntries`) for any `Error` or `Warning` entries emitted during the PIE session.
   - Optionally capture viewport visuals with `SceneCaptureToolset.SceneCaptureToolset.CaptureGameViewport` or `SlateInspectorToolset.SlateInspectorToolset.Screenshot`.
5. **Teardown:**
   - Always call `EditorToolset.EditorAppToolset.StopPIE` when finished.
