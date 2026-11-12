# Agent Error Diagnostics & Remediation Plan

> [!NOTE]
> **Historical Record (`v1.1.0-ue5.8` / UE 5.8 Architecture):** This diagnostic and remediation report documents benchmark runs and internal legacy server files (`AgentFrameworkActionRouter.cpp`, `AgentFrameworkBlueprintActions.cpp`, etc.) from the **UE 5.8 (`v1.1.0-ue5.8`)** architecture. Current `main` (**v2.0.0 for UE 5.9**) exposes tools through UE 5.9's native `ModelContextProtocol` (`/mcp`, port `8000`) and `UToolsetDefinition` registry instead.

This document details the forensic log analysis of agent execution errors, Unreal Engine crashes, editor popups, and friction points observed during the advanced benchmark runs. Future agents assigned to resolve these issues should use this report as an empirical guide.

> **Status: all four categories remediated, plus one further regression found while verifying.** See [Section 4 — Remediation Log](#4-remediation-log) for what shipped, including two corrections to the fixes originally proposed here and a Category E that was discovered by the test suite. The root-cause analysis in Sections 1–2 is kept as the historical record.

---

## 1. Primary Log & Transcript Evidence Index

All findings in this assessment are grounded in the following log files and subagent trajectory transcripts:

### Unreal Engine Editor Logs
* **Primary Assertion & Crash Log:**  
  `<TargetProject>/Saved/Logs/AgentFrameworkTest-backup-2026.07.28-12.41.07.log`  
  *Contains assertion crashes in `Kismet2.cpp`, SEH exceptions in `ActionRouter`, and package creation warnings.*
* **Live Coding Lock Log:**  
  `<TargetProject>/Saved/Logs/AgentFrameworkTest-backup-2026.07.29-11.14.34.log`  
  *Contains Live Coding compile lockouts and full UBT fallback events.*
* **Active Session Log:**  
  `<TargetProject>/Saved/Logs/AgentFrameworkTest.log`  
  *Current active editor log.*

### Benchmark Subagent Trajectory Transcripts
* **Task 1 (`adv_task_01` - Interactive Trap):**  
  `<AppDataDir>/brain/883272f1-27e7-47e6-b44f-c19ce73c5c4e/.system_generated/logs/transcript.jsonl`
* **Task 2 (`adv_task_02` - UMG Health Bar):**  
  `<AppDataDir>/brain/c1427aaa-c0ce-45ca-93b1-58342ac22312/.system_generated/logs/transcript.jsonl`
* **Task 3 (`adv_task_03` - C++ Base Character & Input):**  
  `<AppDataDir>/brain/abda208b-ecfa-4cc1-9a8b-4029401334d8/.system_generated/logs/transcript.jsonl`
* **Task 4 (`adv_task_04` - Niagara Spawner Lifecycle):**  
  `<AppDataDir>/brain/699598a6-e38f-4c85-bf5b-be27f9266307/.system_generated/logs/transcript.jsonl`
* **Task 5 (`adv_task_05` - Data Assets & BP Interface):**  
  `<AppDataDir>/brain/b033c301-a1e3-4b3a-8e91-6a6082fd0589/.system_generated/logs/transcript.jsonl`
* **Plugin Edit Loop Subagent (`db7e78e4`):**  
  `<AppDataDir>/brain/db7e78e4-a83b-418a-bca7-678060e1fd80/.system_generated/logs/transcript.jsonl`  
  *Shows subagent getting stuck trying to modify plugin C++ source code when package creation failed.*

---

## 2. Categorized Error Analysis & Root Causes

### Category A: Engine Assertion Crash in `create_blueprint_actor`
* **Log Lines:** Lines 2880–2889 in `AgentFrameworkTest-backup-2026.07.28-12.41.07.log`:
  ```text
  LogWindows: Error: appError called: Assertion failed: BpCdo->HasAnyFlags(RF_ClassDefaultObject) && BlueprintClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint) [File:D:\build\++UE5\Sync\Engine\Source\Editor\UnrealEd\Private\Kismet2\Kismet2.cpp] [Line: 834] 
  LogAgentFramework: Error: ActionRouter: Intercepted SEH exception 0x00004000 during tool 'create_blueprint_actor'
  ```
* **Root Cause:** In `AgentFramework/Source/AgentFrameworkActions/Private/Blueprint/AgentFrameworkBlueprintActions.cpp#L1068` (historical `v1.1.0-ue5.8`), `ExecuteCreateBlueprint` calls `FKismetEditorUtilities::CreateBlueprint`. If a Blueprint package already exists in memory or on disk at that package path, calling `CreateBlueprint` on top of an existing memory package triggers an assertion failure inside Unreal Engine's `Kismet2.cpp`.
* **Actionable Fix:** Before calling `FKismetEditorUtilities::CreateBlueprint`, check if `UEditorAssetLibrary::DoesAssetExist(PackagePath)`. If it exists, call `UEditorAssetLibrary::DeleteAsset` or load the existing `UBlueprint` object instead of calling `CreateBlueprint` on a dirty package.

---

### Category B: Package Name Split & Short Package Name Warning
* **Log Lines:** Lines 2870–2881 in `AgentFrameworkTest-backup-2026.07.28-12.41.07.log`:
  ```text
  LogUObjectGlobals: Warning: Attempted to create a package with a short package name: /Game/BenchAdv/Traps/BP_Trap.BP_Trap Outer: Package /Game/BenchAdv/Traps/BP_Trap
  LogStreaming: Warning: LoadPackage: SkipPackage: /Game/BenchAdv/Traps/BP_Trap - The package to load does not exist on disk or in the loader
  ```
* **Root Cause:** When an agent passes an object path (e.g. `/Game/BenchAdv/Traps/BP_Trap.BP_Trap`) into asset creation tools, `FPackageName::GetLongPackagePath` and `FPackageName::GetShortName` fail to split package paths containing dots (`.`), passing the full string into `CreatePackage()`.
* **Actionable Fix:** In `AgentFramework/Source/AgentFrameworkActions/Private/AgentFrameworkActionUtils.cpp#L478` (historical `v1.1.0-ue5.8`), enforce strict splitting: extract package path (before `.`) and asset name (after `.`) before calling `CreatePackage()`.

---

### Category C: Live Coding Structural Lockout
* **Log Lines:** `AgentFrameworkTest-backup-2026.07.29-11.14.34.log`:
  ```text
  Live Coding compilation is already in progress. Please wait for it to complete.
  ```
* **Root Cause:** Creating new C++ classes with `UCLASS()` / `UPROPERTY()` macros via `macro_create_cpp_class` and triggering `trigger_compile` immediately causes a race condition if UHT (UnrealHeaderTool) is still re-indexing disk source files.
* **Actionable Fix:** In `AgentFramework/Source/AgentFrameworkActions/Private/Cpp/AgentFrameworkCppActions.cpp#L370` (historical `v1.1.0-ue5.8`), verify `ILiveCodingModule::Get().IsCompiling()` is false and trigger a synchronous file system flush before invoking `LiveCoding->Compile()`.

---

### Category D: Agent Error Blindness (Unreported Engine Logs)
* **Problem:** Agents primarily evaluate tool responses based on the JSON payload (`{ bSuccess: true }`). If the tool call succeeds partially but Unreal Engine emits a `LogOutputDevice` warning, an unsaved package popup, or a Blueprint compiler warning, the agent remains blind to the background error unless it calls `read_message_log`.
* **Actionable Fix:** Update `AgentFramework/Source/AgentFrameworkEngine/Private/AgentFrameworkActionRouter.cpp` (historical `v1.1.0-ue5.8`) to capture `GWarn` and recent `FMessageLog` entries produced during tool execution and append them directly to the `warnings` array in the JSON response returned to the LLM agent.

---

## 3. Recommended Remediation Task Order for Next Agent

1. **Modify `AgentFrameworkBlueprintActions.cpp`**: Add `UEditorAssetLibrary::DoesAssetExist` check & clean package split in `ExecuteCreateBlueprint`.
2. **Modify `AgentFrameworkActionRouter.cpp`**: Capture `GWarn` / engine log warnings into the tool response payload.
3. **Modify `AgentFrameworkCppActions.cpp`**: Enhance Live Coding compilation lock checks.
4. **Rebuild & Verify**: Run `build_plugin.ps1` and execute `run_tests.ps1`.

---

## 4. Remediation Log

All four categories above are addressed. Two of the originally proposed fixes turned out to be
insufficient once the code was inspected; those deviations are called out explicitly below.

### Shared helper: `UAgentFrameworkActionUtils::SplitAssetPath`

New helper in `AgentFrameworkActionUtils.h/.cpp` that splits either a package path
(`/Game/UI/WBP`) or a full object path (`/Game/UI/WBP.WBP`) into a clean package name,
containing directory, and asset name. It is the inverse of the existing
`NormalizeAssetObjectPath` and is the sanctioned way to prepare a path for `CreatePackage()`
or `IAssetTools::CreateAsset`. Covered by the `AgentFramework.AssetPathSplit` automation test.

### Category A — assertion crash in `create_blueprint_actor` → FIXED

**Deviation from the proposed fix.** The plan called for `UEditorAssetLibrary::DoesAssetExist`
plus `DeleteAsset`. Deleting is destructive on a tool an agent retries routinely, so
`ExecuteCreateBlueprint` is now *idempotent* instead:

* Existence is checked with `FindObject` (in-memory packages assert too, not just on-disk ones)
  plus `FPackageName::DoesPackageExist` — no new module dependency required.
* If a Blueprint already exists with the requested parent class, it is **reused**, and inline
  components/variables are applied to it. Duplicate components are skipped rather than
  silently renamed to `Mesh_1`.
* If the existing parent class differs, the call fails with guidance to call `delete_asset`.
  It never silently reparents and never deletes the agent's work.
* `AssetTools.CreateUniqueAssetName` was being called and its results discarded — dead code that
  masked the collision. Removed.

The same treatment was applied to `ExecuteCreateWidgetBlueprint`
(`AgentFrameworkWidgetActions.cpp`), which reached the identical `Kismet2.cpp` assertion via
`UWidgetBlueprintFactory`. On reuse it preserves the existing widget tree and root widget rather
than orphaning previously added widgets. Covered by `AgentFramework.CreateBlueprintIdempotency`.

### Category B — short package name warning → FIXED

Both Blueprint creation paths now split through `SplitAssetPath`. Note the actual root cause is
narrower than recorded above: `FPackageName::GetLongPackagePath` handles a dotted path correctly;
it is `FPackageName::GetShortName` that returns `BP_Trap.BP_Trap` because it only strips at the
last `/`. `NormalizeAssetObjectPath` was not the culprit and is unchanged.

The same defect existed at roughly 18 other asset-creation sites that used the raw
`GetLongPackagePath`/`GetShortName` pair or passed a possibly-dotted path straight into
`CreatePackage()`. These were swept in a follow-up pass; `SplitAssetPath` is now used across
`Animation`, `BehaviorTree`, `Input`, `Level`, `Material`, `Media`, `Mesh`, `Niagara`, `PCG`,
`Sequencer`, `Widget`, and `Blueprint` actions. To check for new offenders:
`grep -rn "GetShortName\|CreatePackage(" --include=*.cpp AgentFramework/Source`
(the remaining `GetShortName` uses in `Diagnostics/AgentFrameworkAutomationTests.cpp` build object
paths deliberately and are correct).

### Category C — Live Coding structural lockout → FIXED

`ExecuteTriggerCompile` now uses the richer engine overload
`ILiveCodingModule::Compile(ELiveCodingCompileFlags, ELiveCodingCompileResult*)` instead of the
fire-and-forget `Compile()`:

* Defaults to `WaitForCompletion`, so the tool returns the **actual** compile outcome. This
  removes the poll-then-bounce-off-the-lock loop that produced the lockouts — there is nothing to
  poll. Blocking the game thread is safe here: the engine's wait loop self-pumps
  `AttemptSyncLivePatching()`, which is exactly what the built-in `LiveCoding.Compile` console
  command does. Overridable via a new `wait_for_completion: false` parameter.
* Each `ELiveCodingCompileResult` maps to a distinct, actionable message. `NoChanges` in
  particular now warns that the edit is probably one Live Coding cannot patch, rather than
  reading as success.
* A pre-existing compile is reported with explicit "do not retry immediately" guidance, and
  `CanEnableForSession()` / `GetEnableErrorText()` are used to explain an unavailable session.

**Deviation from the proposed fix.** A "synchronous file system flush" before `Compile()` was not
added: `WriteFileWithBackup` already closes its handles synchronously, and `Compile()` itself
calls `UpdateModules(false)` immediately rather than waiting for a tick. The real defect was
elsewhere — `ExecuteMacroCreateCppClass` was telling agents to run `trigger_compile` on a
brand-new file containing `UCLASS`/`GENERATED_BODY`, which Live Coding fundamentally cannot patch.
That hint now names the only path that works (`regenerate_project_files` + editor restart + IDE
build), matching what `ExecuteCreateCppClass` already did.

### Category D — agent error blindness → FIXED

The proposed fix (append to the `warnings` array) would have had **no effect**: the MCP bridge
was reading only `bSuccess`, `ResultMessage`, and `Errors` from the HTTP response and discarding
`Warnings` entirely. The fix therefore spans both sides:

* `FAgentFrameworkLogCapture::GetLogDeltaEntries` replaces `GetLogDeltaFormatted` and returns the
  actual **text** of warnings, not just a count. Entries are de-duplicated, truncated to 300
  chars, capped per severity, and filtered against a deny-list of high-volume categories
  (`LogSlate`, `LogShaderCompilers`, `LogHttpServer`, …) so the signal is not buried.
* `AgentFrameworkActionRouter.cpp` folds captured errors and warnings into both `Result.Warnings`
  and `ResultMessage`, de-duplicated against what the executor already reported. When a tool
  returns `bSuccess: true` while the engine logged an error, an explicit hint tells the agent to
  verify the state actually changed.
* `bridge/main.py` now surfaces `Warnings` to the model — appended to the text block, or as a
  second content block for the `capture_*` image tools.

**Payload-corruption guard.** Several tools return a JSON document or base64 image data in
`ResultMessage` (`extract_ui_state`, `get_blueprint_info`, `capture_widget`, …) and callers parse
it verbatim. `FAgentFrameworkDiagnostics::IsOpaquePayload` suppresses the `ResultMessage` append
for those, leaving the `Warnings` array as the channel. Without this, adding diagnostics would
have broken every JSON-returning tool — the pre-existing error-append path had this latent bug too.

### Category E (found during verification) — the Smart Sentinel was inert → FIXED

Running the automation suite surfaced two failing tests (`AgentFramework.Sentinel`,
`AgentFramework.TokenEfficiency`) that were **already red before this work**, and tracing them
found a live safety regression introduced when `NormalizeAssetObjectPath` was wired into
`ExpandBlueprintAssetPath`.

`ExpandBlueprintAssetPath` returns an **object** path (`/Game/X.X`), but three package-level APIs
were being handed that value:

| Call site | Symptom |
| --- | --- |
| `FindPackage(nullptr, "/Game/X.X")` in the sentinel | Returns `nullptr` → the dirty check never ran → **the sentinel never blocked anything** |
| `FindPackage` in `check_asset_state` | Always reported `bIsDirty: false` |
| `USourceControlHelpers::PackageFilename("/Game/X.X")` | Wrong filename → source-control lock check ineffective |

On top of that, `AgentDirtiedPackages` was keyed inconsistently: entries were **added** as object
paths but **removed** by `UPackage::GetFName()` (a package name) in
`FAgentFrameworkActionsModule::OnPackageDirtyStateChanged`, so entries never cleared.

Net effect: the guard that stops the agent from overwriting the user's unsaved editor work did
nothing at all.

Fixes:

* New file-local helpers `AssetPathToPackageName` / `AssetPathToPackageFName` in
  `AgentFrameworkBlueprintActions.cpp` expand a relative path and strip the object suffix. All
  `FindPackage`, `PackageFilename`, and `AgentDirtiedPackages` uses now route through them, so the
  set is keyed consistently with the code that clears it.
* The post-dispatch authorship claim is now conditional on the package actually being dirty.
  It runs *after* the tool completes, and tools that save leave the package clean — an
  unconditional claim outlived the edit and made the sentinel treat a *later user edit* as the
  agent's own. It now removes the claim instead when the package came back clean.
* The two `TokenEfficiency` assertions were asserting the pre-normalization return value. Since
  normalizing to an object path is the intended behavior, the test now expects the object-path
  form, with a comment pointing at `AssetPathToPackageName` for the package-level counterpart.

### Verification

* `build_plugin.ps1 -NoZip` — plugin compiles clean (all three modules).
* `Tests/run_all_tests.ps1` — C++ headless automation tests plus the Python suite.
* New automation tests: `AgentFramework.AssetPathSplit`, `AgentFramework.CreateBlueprintIdempotency`.
* Tool schemas updated so agents learn the new contracts: `create_blueprint_actor` and
  `create_widget_blueprint` document their idempotency, and `trigger_compile` documents that it
  blocks and that Live Coding cannot patch new reflected types.
* Live end-to-end probe through the real MCP transport: `create_blueprint_actor` on a fresh path
  created the asset, and a second call using the **dotted object-path form** returned
  `Reused existing Blueprint ...` instead of asserting in `Kismet2.cpp`. Categories A and B
  confirmed against a running editor.

Warning propagation is confirmed live. An initial probe showed no warnings only because the MCP
bridge process predated the deployed `bridge/main.py`; after the server restarted and re-imported
it, a double-create returned:

```
Reused existing Blueprint 'BP_WarningCheck' (parent: Actor). Compile: SUCCESS.
--- Warnings ---
  - Blueprint '/Game/AgentFrameworkVerify/BP_WarningCheck' already existed with the requested
    parent class, so it was reused rather than recreated. ...
  - WARNING: EventTick detected. ...
```

Both an executor-authored warning and an unrelated analysis warning reached the model in the same
response — the exact blindness Category D described.

Engine-generated output reaches the agent too, not just executor-authored warnings. Calling
`get_blueprint_info` on a non-existent asset returns:

```
Blueprint not found: '/Game/DoesNotExistAnywhere/BP_NoSuchAsset.BP_NoSuchAsset'
--- Warnings ---
  - [LogUObjectGlobals] Failed to find object 'Blueprint /Game/DoesNotExistAnywhere/BP_NoSuchAsset.BP_NoSuchAsset'
```

That warning is emitted inside `LoadObject` by the engine, captured by the log device, carried in
`Result.Warnings`, and surfaced by the bridge — the full path Category D was missing.

**Operational note:** `bridge/main.py` is imported once per MCP server process, so any future
bridge change needs an MCP server restart before it takes effect.

**Testing note:** log lines emitted with `UE_LOG` inside an automation test do not reach attached
output devices synchronously, so `AgentFramework.LogDeltaCounts` drives
`FAgentFrameworkLogCapture::Serialize` directly instead. (Logging an *error* in a test would also
fail that test outright, since the automation framework records logged errors as failures.) The
live probe above is what proves the `GLog` path itself.

## 5. Follow-Up Hardening

### F1 — `modify_cpp_file` could write into the plugin's own source → FIXED

The path check accepted anything under `FPaths::ProjectDir()`, which is far wider than "project
source": it also covers `Config/`, `Saved/`, `Content/`, the `.uproject`, and every installed
plugin — including AgentFramework's own. A benchmark run hit exactly that, leaving
`AMyBenchActor.h/.cpp` in the installed plugin's `AgentFrameworkEngine` module where they were
compiled into the plugin. (Those strays were removed during deployment.)

`IsWritableSourcePath` in `AgentFrameworkCppActions.cpp` now requires the target to be a real C++
source file (`.h/.hpp/.inl/.cpp/.c/.cc`, plus `.cs` for module rules) under either the project's
own `Source/` or a project plugin's `Source/`, rejects `..` traversal, and never allows a write
inside the AgentFramework plugin's own directory — an agent must not be able to edit, or disable
the safety checks in, the code executing its own tool calls.

The check lives inside `WriteFileWithBackup`, the single choke point all five write sites share,
so no caller can bypass it whether the path came from the agent (`modify_cpp_file`) or was derived
from tool parameters (`create_cpp_class`, `macro_create_cpp_class`). That function now reports a
specific reason through an out-parameter instead of a bare bool, so refusals reach the agent.
Covered by `AgentFramework.CppPathContainment`.

### F2 — `SaveAssetPackage` filename argument → NOT A DEFECT

Flagged during review, then disproved. Every `UPackage::SavePackage` call in the plugin converts
the package name to a filename first via `FPackageName::TryConvertLongPackageNameToFilename` or
`LongPackageNameToFilename`. The `SaveAssetPackage(Package, NewAction, PackageName)` calls in
`AgentFrameworkInputActions.cpp` route through a file-local static helper of the same name that
performs the conversion before delegating to `UAgentFrameworkActionUtils::SaveAssetPackage`.
No change made.

(Separately noted, not acted on: `AgentFrameworkMediaActions.cpp` creates packages but never saves
them, so media assets live in memory until the user saves manually. That may be intentional.)

### F3 — stale `bridge.exe` → REMOVED

`bridge.exe` was not a stale build of the current bridge; it was the **retired C++ implementation**.
Commit `5591b0c` (2026-06-30) transitioned the bridge to Python and deleted both `src/main.cpp`
and `src/build_bridge.bat`. The `.exe` and a `main.obj` survived as untracked build artifacts, and
the docs still advertised them as the live architecture — so an assistant wired up from the README
would have run a months-old bridge that silently discards the `Warnings` array (Category D).

Removed the orphaned artifacts and corrected every reference: `UnrealEngine/README.md`
(architecture, config example, client setup, and the Compilation section, which documented a build
script that no longer exists), `AGENTS.md`, and `UnrealEngine/.githooks/post-checkout.ps1`, which
was copying the stale binary into every new worktree. The `rm -f UnrealEngine/bridge.exe` guard in
`.github/workflows/release.yml` was already correct and is kept. The deleted source remains
recoverable from `5591b0c^` if it is ever needed.

### F4 — `EventTick detected` false positive → FIXED

Now that warnings actually reach the agent, `DetectInfiniteLoopRisk` fired on every
`create_blueprint_actor` call: it flagged the presence of a `ReceiveTick` node, and a freshly
created Blueprint already contains a stub Tick node that drives nothing. The warning is about work
performed each frame, so it now requires the Tick node to be enabled **and** have its exec output
actually linked to logic.
