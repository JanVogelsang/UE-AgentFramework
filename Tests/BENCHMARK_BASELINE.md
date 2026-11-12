# UE-AgentFramework Benchmark Comparison Report: Baseline vs Post-Optimization

> **Historical Record (`v1.1.0-ue5.8`):** This benchmark report was recorded against the pre-5.9 standalone HTTP server architecture (`AgentFrameworkHttpServer.cpp`, `v1.1.0-ue5.8`). On `main` (`v2.0.0` for Unreal Engine 5.9), tool discovery and execution run through UE 5.9's native `ModelContextProtocol` (`/mcp`, port `8000`) and `UToolsetDefinition` registry.

## Executive Summary

Following the implementation of 5 core plugin optimizations across `UE-AgentFramework`, a complete post-optimization audit of the 42-task Unreal Engine benchmark suite was executed.

### Key Performance Telemetry Comparison

| Telemetry Metric | Baseline (Pre-Optimization) | Post-Optimization | Delta / Improvement |
| :--- | :--- | :--- | :--- |
| **Immediate (First-Try) Pass Rate** | 28.6% (12 / 42) | **64.3% (27 / 42)** | **+35.7% (+125% relative increase)** |
| **Self-Corrected Pass Rate** | 71.4% (30 / 42) | **33.3% (14 / 42)** | -38.1% (Fewer retries needed) |
| **Overall Task Success Rate** | 100.0% (42 / 42) | **97.6% (41 / 42)** | -2.4% (Task 04 strict failure on DLL lock) |
| **Total Tool Calls** | 621 | **564** | **-57 calls (-9.2% token overhead reduction)** |
| **Average Tool Calls per Task** | 14.8 | **13.4** | **-1.4 calls / task** |
| **Total Audit Duration** | 2586.0s (~43.1m) | **3120.0s (~52.0m)** | +534.0s (Driven by deep Niagara / PIE verification) |
| **Average Duration per Task** | 61.6s | **74.3s** | +12.7s / task |

---

## Key Optimization Impacts Analysis

1. **Massive Boost in First-Try Success Rate (+125% Relative)**:
   - **Schema Coverage**: Expanding native C++ schema coverage from 5 to 14 core categories in `AgentFrameworkHttpServer.cpp` eliminated schema resolution failures. Subagents constructed valid MCP arguments on turn 1 without guessing property layouts.
   - **C++ Creation Safeguards**: Standardizing `macro_create_cpp_class` and asset saving (`UAgentFrameworkActionUtils::SaveAssetPackage`) prevented package corruption and editor memory crashes.

2. **Reduction in Tool Overheads**:
   - Total tool calls dropped from **621 to 564**, saving 57 turns/tool invocations across the test suite. Tasks like `task_06` (MCP Discovery), `task_29` (CPP RAG), and `task_38` (Asset Generation) executed in under 8 tool calls each.

3. **Analysis of Non-Immediate Tasks**:
   - **Task 04 (FAIL)**: Live C++ compilation triggered via `trigger_compile` reported Live Coding disabled for the unattended Editor session. Falling back to terminal UBT build failed due to `UnrealEditor.exe` holding open DLL locks (`LNK1104`).
   - **Self-Corrected Tasks (14 Tasks)**: Tasks requiring multi-stage Blueprint connection (`task_36`, `task_37`), Niagara compilation verification (`task_39`), or high-res viewport rendering (`task_25`) required secondary self-correction turns to handle graph topological ordering or asset loading.

---

## Detailed Task Breakdown (Post-Optimization)

| Task ID | Target Feature | Post-Opt Status | Tool Calls | Duration (s) | Key Execution Summary |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `task_01` | `Find the skeleton asset used b...` | `SELF_CORRECTED_PASS` | 15 | 32s | Successfully retrieved skeleton asset path '/Game/EntityRepresentations/Meshes/Characters/TODO/Walking_Import0003_Skeleton.Walking_Import0003_Skeleton' for animation sequence 'Walking_Import0003_Anim' using native MCP inspect_uobject_properties. |
| `task_02` | `Create a Behavior Tree named '...` | `IMMEDIATE_PASS` | 5 | 10.0s | Successfully created Behavior Tree asset 'BT_Enemy' at '/Game/AI/BT_Enemy' using unreal.BehaviorTreeFactory and verified asset existence. |
| `task_03` | `Create a new Blueprint class n...` | `IMMEDIATE_PASS` | 7 | 10s | Successfully created BP_TestActor inheriting from Actor with float variable 'Speed' on the first attempt. |
| `task_04` | `Trigger a live C++ compilation...` | `FAIL` | 35 | 137s | Attempted live compilation via native trigger_compile MCP tool and terminal Build.bat. Native tool reported Live Coding unavailable for session, while UBT terminal build failed due to active Editor DLL file locks. |
| `task_05` | `Get the list of currently sele...` | `IMMEDIATE_PASS` | 5 | 9s | Successfully queried currently selected actors in the Editor viewport using unreal.EditorActorSubsystem via execute_python_script. Current selection list is empty ([]). |
| `task_06` | `Retrieve the list of all avail...` | `IMMEDIATE_PASS` | 7 | 12s | Successfully listed all tools from Unreal Engine MCP servers (cpp-ast-rag and unrealengine), verifying expected tools such as query_cpp_ast and various blueprint actions (e.g., create_blueprint_actor, add_blueprint_component, inject_blueprint_nodes_t3d). |
| `task_07` | `Create a new C++ class 'AMyBen...` | `IMMEDIATE_PASS` | 9 | 12s | Successfully created AMyBenchActor C++ class inheriting from AActor using native MCP tool macro_create_cpp_class and regenerated compile_commands.json. |
| `task_08` | `Import a CSV file to create a ...` | `IMMEDIATE_PASS` | 14 | 30s | Successfully imported CSV file into Unreal Engine to create a Data Table asset 'DT_Items' under '/Game/Data'. Verified asset existence and DataTable class type. |
| `task_09` | `Fetch the last 50 lines of the...` | `IMMEDIATE_PASS` | 4 | 12s | Fetched last 50 lines from Unreal Editor output log via read_message_log tool successfully. |
| `task_10` | `Create a new Gameplay Ability ...` | `IMMEDIATE_PASS` | 6 | 8s | Created Gameplay Ability Blueprint 'GA_Fireball' with parent class GameplayAbility (UGameplayAbility) using native create_blueprint_actor MCP tool and verified via get_blueprint_info. |
| `task_11` | `Create an Input Mapping Contex...` | `IMMEDIATE_PASS` | 10 | 15s | Created IA_Jump (InputAction) and IMC_Default (InputMappingContext) assets using native Unreal Engine MCP tools and verified their existence. |
| `task_12` | `Spawn a Directional Light acto...` | `IMMEDIATE_PASS` | 10 | 20s | Successfully spawned a Directional Light actor into the active level at location (0, 0, 1000) using Unreal Engine's EditorActorSubsystem python API and verified its position. |
| `task_13` | `Create a new Material named 'M...` | `IMMEDIATE_PASS` | 5 | 10s | Created Material asset M_Red in /Game and added a Vector Parameter node named Color connected to Base Color using unreal.MaterialEditingLibrary. |
| `task_14` | `Create a Media Player asset na...` | `IMMEDIATE_PASS` | 9 | 16.0s | Successfully created Media Player asset 'MP_Video' in '/Game/Media' via Editor Python Script API and verified existence. |
| `task_15` | `Get the vertex and triangle co...` | `SELF_CORRECTED_PASS` | 14 | 25s | Successfully obtained mesh metrics for Static Mesh '/Game/Meshes/SM_Cube': 54 vertices and 48 triangles (LOD 0). |
| `task_16` | `Create a new Niagara System na...` | `SELF_CORRECTED_PASS` | 34 | 1208s | Created Niagara System 'NS_Sparks' at /Game/NS_Sparks. Encountered an editor crash during emitter addition, self-corrected by restarting the Editor, and verified that NS_Sparks exists as a valid Niagara System asset on disk and in the editor asset registry. |
| `task_17` | `Create a Procedural Content Ge...` | `IMMEDIATE_PASS` | 6 | 15s | Created PCG Graph asset named PCG_Forest at /Game/PCG_Forest using PCGGraphFactory via Unreal Engine Python API, and confirmed existence using search_assets. |
| `task_18` | `Start a Play In Editor (PIE) s...` | `IMMEDIATE_PASS` | 10 | 21.0s | Successfully launched PIE session using start_pie_session, scheduled and waited for 5 seconds, verified PIE world state via query_world_state, and stopped the PIE session cleanly without errors. |
| `task_19` | `Run an Unreal Insights perform...` | `IMMEDIATE_PASS` | 9 | 28.0s | Successfully started and stopped an Unreal Insights performance trace for 10 seconds via in-engine console commands in python. Verified trace file 20260727_183410_07C830.utrace (34.3 MB) was generated in Saved/Profiling. |
| `task_20` | `Execute a Python script to pri...` | `IMMEDIATE_PASS` | 6 | 10s | Successfully executed Python script to output 'Hello from Benchmarker' to Unreal Editor log and verified log entry via read_message_log. |
| `task_21` | `Create a Level Sequence asset ...` | `SELF_CORRECTED_PASS` | 6 | 12.0s | Level Sequence asset 'LS_Cinematic' at '/Game/Cinematics/LS_Cinematic' verified successfully as valid LevelSequence asset. |
| `task_22` | `Retrieve the Default Game Mode...` | `SELF_CORRECTED_PASS` | 8 | 15s | Retrieved Default Game Mode class setting '/Script/Engine.GameModeBase' from UGameMapsSettings project configuration. |
| `task_23` | `Query the source control statu...` | `SELF_CORRECTED_PASS` | 7 | 20s | Queried source control status of '/Game/Maps/MainMap.umap' using unreal.SourceControl API. Source control is currently disabled in the editor session and file status was correctly retrieved (is_checked_out=False, is_source_controlled=False, is_current=False). |
| `task_24` | `Run the Data Validation plugin...` | `IMMEDIATE_PASS` | 20 | 30s | Successfully executed native UE Data Validation tool (validate_assets) and retrieved detailed validation output for project assets, identifying valid assets (e.g., BP_RoundPawn, BP_TestActor, BP_SmartObjectActor, IMC_Default, IA_Jump) and invalid assets with validation errors (e.g., BPEG_CoverPositionsGenerator, ST_EnemyLogic, SO_TestDefinition). |
| `task_25` | `Capture a screenshot of the ac...` | `SELF_CORRECTED_PASS` | 12 | 25s | Successfully captured active editor viewport screenshot. Returned file path 'C:\Users\janv1\Documents\Unreal Projects\AgentFrameworkTest\Saved\Screenshots\WindowsEditor\HighresScreenshot00001.png' and base64 image data snippet. |
| `task_26` | `Create a UMG Widget Blueprint ...` | `IMMEDIATE_PASS` | 10 | 18.0s | Created UMG Widget Blueprint 'WBP_HUD' with a Canvas Panel as root widget. |
| `task_27` | `Create a Primary Data Asset na...` | `SELF_CORRECTED_PASS` | 16 | 23.5s | Successfully created Primary Data Asset 'PDA_Weapon' of class 'UWeaponData' at /Game/PDA_Weapon using DataAssetFactory and verified its existence and class inheritance. |
| `task_28` | `Query the Epic Games AI Assist...` | `SELF_CORRECTED_PASS` | 5 | 6.5s | Successfully queried the vector DB for UInstancedStaticMeshComponent documentation. |
| `task_29` | `Query the C++ AST for the decl...` | `IMMEDIATE_PASS` | 3 | 4s | Successfully queried the C++ AST for AActor declaration using query_cpp_ast tool, returning full class metadata, base class UObject, properties, and methods. |
| `task_30` | `Generate compile_commands.json...` | `IMMEDIATE_PASS` | 4 | 10s | Successfully triggered compile_commands.json generation using cpp-ast-rag generate_compile_commands MCP tool and verified compile_commands.json was created/updated in the project directory. |
| `task_31` | `Search the vector database for...` | `IMMEDIATE_PASS` | 3 | 10s | Successfully executed search_vector_db tool on cpp-ast-rag MCP server for 'How to use Enhanced Input'. Returned 5 relevant documentation snippets about Enhanced Input. |
| `task_32` | `Search for similar blueprints ...` | `SELF_CORRECTED_PASS` | 25 | 240s | Fixed missing time import and module path in main.py, indexed BP_RoundPawn into vector store, and successfully retrieved /Game/Blueprint/Player/BP_RoundPawn matching player health search query. |
| `task_33` | `Format a given block of T3D no...` | `SELF_CORRECTED_PASS` | 37 | 135s | Organized T3D Blueprint node blocks into a clean grid layout with updated, non-overlapping NodePosX and NodePosY coordinates. |
| `task_34` | `Use the add-component skill to...` | `IMMEDIATE_PASS` | 11 | 20s | Successfully located BP_TestActor asset at /Game/BP_TestActor, added a BoxComponent named BoxComponent using add_blueprint_component MCP tool, and verified using get_blueprint_info. |
| `task_35` | `Use the blueprint-authoring sk...` | `IMMEDIATE_PASS` | 17 | 30.0s | Successfully added a Print String node connected to Event BeginPlay in BP_TestActor using T3D injection and compiled cleanly. |
| `task_36` | `Use the create-actor skill to ...` | `SELF_CORRECTED_PASS` | 28 | 45s | Successfully created interactable door Blueprint 'BP_Door' with components (DoorFrame, DoorMesh, InteractionBox), variables (bIsOpen, OpenSpeed), and fully wired functions (OpenDoor, CloseDoor, ToggleDoor) for opening and closing logic. |
| `task_37` | `Use the create-interface skill...` | `SELF_CORRECTED_PASS` | 26 | 48.0s | Created Blueprint Interface asset 'BPI_Interactable' with function signature 'Interact' under /Game/ and generated C++ backing header IBPI_Interactable.h. |
| `task_38` | `Use the generate-assets skill ...` | `IMMEDIATE_PASS` | 8 | 16s | Successfully created folder '/Game/Core' containing Blueprint assets 'BP_GameMode' (GameModeBase) and 'BP_PlayerController' (PlayerController) using native UE MCP tools. |
| `task_39` | `Use the niagara-authoring skil...` | `SELF_CORRECTED_PASS` | 45 | 655s | Successfully set up a burst spawn effect in 'NS_Sparks' by instantiating the DirectionalBurst template system with configured burst spawn modules, compiling the system, and capturing isolated keyframe images. |
| `task_40` | `Use the pie-verifier skill to ...` | `IMMEDIATE_PASS` | 12 | 20s | Executed pie-verifier workflow: launched PIE using start_pie_session, inspected runtime message logs using read_message_log, stopped PIE using stop_pie_session, and gathered log summary (found 2 non-critical errors related to PixelStreaming2RTC window target and CommonUI viewport client). |
| `task_41` | `Use the setup-input skill to m...` | `IMMEDIATE_PASS` | 30 | 75s | Successfully created the Axis2D IA_Move Input Action and mapped the WASD hardware keys (W, A, S, D) with appropriate Swizzle and Negate modifiers to IMC_Default using native Unreal Engine MCP tools. |
| `task_42` | `Use the setup-replication skil...` | `IMMEDIATE_PASS` | 11 | 22s | Successfully configured replication for 'Speed' variable on 'BP_TestActor' to Replicated with COND_None and compiled the Blueprint cleanly. |