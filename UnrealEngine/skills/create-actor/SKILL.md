---
name: create-actor
description: Use this skill to spawn, transform, inspect, or delete Actors in the active Unreal Engine 5.9 Level (`EditorToolset.LevelToolset`, `EditorToolset.ActorToolset`), create Actor Blueprints (`BlueprintToolsets.BlueprintToolset`), or scaffold C++ Actor classes (`AgentFrameworkActions.AgentFrameworkCppToolset`).
---

# Create & Spawn Actors (Unreal Engine 5.9)

Choose the appropriate UE 5.9 toolset (via `call_tool` on `/mcp`) depending on whether you are spawning an actor instance in the level, creating a reusable Blueprint Actor asset, or scaffolding a C++ `AActor` subclass:

## 1. Spawn or Manipulate Actor Instances in a Level (`EditorToolset.LevelToolset`, `EditorToolset.ActorToolset`, `EditorToolset.ObjectToolset`)

- **Spawn Actor:**
  - Call `EditorToolset.LevelToolset.AddToSceneFromClass` (`ActorType`, `Name`, `Xform`, optional `Parent`, `bSnapToGround`, `bAllowSingletonDuplicate`) for C++ classes (e.g., `/Script/Engine.PointLight`).
  - Call `EditorToolset.LevelToolset.AddToSceneFromAsset` (`AssetPath`, `Name`, `Xform`, ...) for Blueprint or static mesh assets (e.g., `/Game/Blueprints/BP_Crate`).
- **List, Inspect & Configure Actors:**
  - Call `EditorToolset.LevelToolset.FindActors` (`Filter`) to query actors in the active level.
  - Call `EditorToolset.ObjectToolset.GetProperties` / `EditorToolset.ObjectToolset.SetProperties` (`Instance`, `Values` JSON string) to read or modify actor properties.
- **Transform, Components, Folders & Delete:**
  - Use `EditorToolset.ActorToolset` (`GetActorTransform`, `SetActorTransform`, `SetLabel`, `AddTag`, `GetComponents`, `AddComponent`, `RemoveComponent`, `SetParentComponent`), `EditorToolset.LevelToolset` (`SetActorFolder`, `SetActorHidden`, `RemoveFromScene`), and `EditorToolset.EditorAppToolset.SelectActors`.

## 2. Create a Reusable Blueprint Actor Asset (`BlueprintToolsets` & `EditorToolset`)

1. Call `BlueprintToolsets.BlueprintToolset.Create` with `FolderPath` (e.g., `/Game/Blueprints`), `AssetName` (e.g., `BP_InteractableChest`), and `AssetType` (`Actor`, `Pawn`, `Character`, etc.).
2. Add components via `EditorToolset.ActorToolset.AddComponent` (`Owner`, `ComponentType`, `Name`).
3. Author EventGraph/function logic via `BlueprintToolsets.BlueprintDslToolset.Edit` (or `BlueprintToolsets.BlueprintToolset` node tools).
4. Compile via `BlueprintToolsets.BlueprintToolset.CompileBlueprint` and save via `EditorToolset.AssetToolset.SaveAsset`.

## 3. Create a C++ Actor Subclass (`AgentFrameworkActions.AgentFrameworkCppToolset`)

1. Call `AgentFrameworkActions.AgentFrameworkCppToolset.ScaffoldCppClass` (or `CreateCppClass` for minimal boilerplate):
   - `ClassName`: Unprefixed class name (e.g., `"InteractableChest"`).
   - `ParentClassName`: `"Actor"` (or `"AActor"`, `"Pawn"`, `"Character"`).
   - Optional `ModuleName`, `SubDirectory`, `Properties` (array of `FAgentFrameworkCppPropertySpec`), `Methods` (array of `FAgentFrameworkCppMethodSpec`), and `AdditionalIncludes`.
2. Call `generate_compile_commands` on `cpp-ast-rag` so Clang indexes the new `.h` and `.cpp` files.
3. For live `.cpp` body edits on already-loaded classes, call `LiveCodingToolset.LiveCodingToolset.CompileLiveCoding` (after adding new headers or `UCLASS`/`UPROPERTY`/`UFUNCTION` definitions, call `AgentFrameworkActions.AgentFrameworkCppToolset.RegenerateProjectFiles` and rebuild offline when the Editor is closed).
