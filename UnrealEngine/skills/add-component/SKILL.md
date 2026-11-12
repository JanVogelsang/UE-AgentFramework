---
name: add-component
description: Use this skill to add, reparent, configure, or remove Components (`UActorComponent`, `USceneComponent`) on a Blueprint asset or Level Actor in Unreal Engine 5.9 using `EditorToolset.ActorToolset`, `BlueprintToolsets.BlueprintToolset`, `EditorToolset.ObjectToolset`, and `EditorToolset.AssetToolset`.
---

# Add & Configure Components (Unreal Engine 5.9)

Use Epic's native `EditorToolset.ActorToolset`, `BlueprintToolsets.BlueprintToolset`, `EditorToolset.ObjectToolset`, and `EditorToolset.AssetToolset` (invoked via `call_tool` on `/mcp` using module-qualified `toolset_name` values) to manage component hierarchies on Blueprint assets and level actors.

## Workflow

1. **Inspect Existing Component Hierarchy:**
   - For a level `AActor` instance, call `EditorToolset.ActorToolset.GetComponents` (plus `EditorToolset.ActorToolset.GetRootComponent` and `EditorToolset.ActorToolset.GetParentComponent`) to inspect existing components and parent-child relationships.
   - For a Blueprint asset, inspect its structure with `BlueprintToolsets.BlueprintDslToolset.Read` or load its generated CDO via `BlueprintToolsets.BlueprintToolset.GetDefaultObject`.
2. **Add or Reparent the Component:**
   - Call `EditorToolset.ActorToolset.AddComponent` with:
     - `Owner`: Target actor Blueprint, actor instance, or parent `USceneComponent`
     - `ComponentType`: Component class (e.g., `StaticMeshComponent`, `SphereComponent`, `CapsuleComponent`, `AudioComponent`, `NiagaraComponent`, `WidgetComponent`, or a custom C++/Blueprint component class)
     - `Name`: Component name
   - Reparent scene components using `EditorToolset.ActorToolset.SetParentComponent` (`Component`, `Parent`), or delete with `EditorToolset.ActorToolset.RemoveComponent` (`Component`).
3. **Configure Component Properties & Transform:**
   - Call `EditorToolset.ObjectToolset.SetProperties` (`Instance`, `Values` JSON string) on the component template or instance to configure properties (`StaticMesh`, `OverrideMaterials[0]`, `BodyInstance.CollisionProfileName`, gameplay properties, etc.). Inspect property names first with `EditorToolset.ObjectToolset.ListProperties` or `EditorToolset.ObjectToolset.GetPropertySchema`.
   - Use `EditorToolset.ActorToolset.SetComponentTransform` (`Component`, `Translation`, `Rotation`, `Scale`, `bWorldSpace`) for relative/world transform adjustments.
4. **Compile & Save:**
   - When modifying a Blueprint asset, call `BlueprintToolsets.BlueprintToolset.CompileBlueprint` (`Blueprint`) and `EditorToolset.AssetToolset.SaveAsset` (`AssetPath`) to persist changes.
