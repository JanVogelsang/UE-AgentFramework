---
name: niagara-authoring
description: Use this skill when creating, inspecting, or modifying Niagara visual effects (`UNiagaraSystem`, `UNiagaraEmitter`, modules, renderers, user parameters, and level components) in Unreal Engine 5.9 using Epic's native `NiagaraToolsets` plugin (`NiagaraToolsets.NiagaraToolset_Assets`, `NiagaraToolsets.NiagaraToolset_System`, `NiagaraToolsets.NiagaraToolset_Component`, `NiagaraToolsets.NiagaraToolset_Blueprint`, `NiagaraToolsets.NiagaraToolset_Info`).
---

# Niagara VFX Authoring (Unreal Engine 5.9)

In Unreal Engine 5.9, Niagara VFX authoring is handled natively by Epic's `NiagaraToolsets` plugin (pass module-qualified `toolset_name` values to `describe_toolsets`, `describe_tools`, and `call_tool`: `NiagaraToolsets.NiagaraToolset_Assets`, `NiagaraToolsets.NiagaraToolset_System`, `NiagaraToolsets.NiagaraToolset_Component`, `NiagaraToolsets.NiagaraToolset_Blueprint`, and `NiagaraToolsets.NiagaraToolset_Info`).

## 1. Asset Creation & Discovery (`NiagaraToolsets.NiagaraToolset_Assets` & `NiagaraToolsets.NiagaraToolset_System`)

- **Find Templates & Scripts (`NiagaraToolsets.NiagaraToolset_Assets`):**
  - Call `NiagaraToolsets.NiagaraToolset_Assets.GetAssetDiscoveryInfo` and `NiagaraToolsets.NiagaraToolset_Assets.FindNiagaraScripts` (or `EditorToolset.AssetToolset.FindAssets`) to browse template systems/emitters and existing Niagara scripts/modules.
  - Inspect script inputs and metadata with `NiagaraToolsets.NiagaraToolset_Assets.GetNiagaraScriptDigest`.
- **Create System (`NiagaraToolsets.NiagaraToolset_System`):**
  - Call `NiagaraToolsets.NiagaraToolset_System.CreateNiagaraSystem` (`AssetName`, `AssetPath`, optional `TemplateSystem`) to create a new `UNiagaraSystem` in `/Game/VFX/`.

## 2. System, Emitter & Stack Authoring (`NiagaraToolsets.NiagaraToolset_System`)

Use `NiagaraToolsets.NiagaraToolset_System` (inspect full schemas via `describe_toolsets` / `GetSystemSchema` / `GetEmitterSchema` / `GetModuleSchema`) to inspect and manipulate the Niagara stack:
1. **Inspect System & Topology:**
   - `NiagaraToolsets.NiagaraToolset_System.GetSystemSummary` / `GetSystemData` / `GetSystemDependencies` / `GetUserVariables`
   - `NiagaraToolsets.NiagaraToolset_System.GetEmitterSummary` / `GetEmitterTopology` / `GetScriptStackTopology` / `GetModuleTopology` / `GetStackInputTopology`
2. **Manage Emitters in System:**
   - `NiagaraToolsets.NiagaraToolset_System.AddEmitter` / `RemoveEmitter`, plus `SetSystemData` and `SetEmitterData` to configure emitter settings and lifecycle.
3. **Modules, Parameters & Dynamic Inputs:**
   - `NiagaraToolsets.NiagaraToolset_System.AddModule`, `RemoveModule`, `SetModuleEnabled`, `AddSetParametersModule`, `AddSetParameterEntry`, `RemoveSetParameterEntry`.
   - Query available dynamic inputs with `GetAvailableDynamicInputs` / `GetModuleInputValues` / `GetStackInputData` and update values via `SetStackInputData`.
4. **Renderers:**
   - Discover renderer schemas via `GetRendererSchema` and manage renderers with `AddRenderer`, `RemoveRenderer`, `GetRendererData`, and `SetRendererData` (`UNiagaraSpriteRendererProperties`, `UNiagaraRibbonRendererProperties`, `UNiagaraMeshRendererProperties`, `UNiagaraLightRendererProperties`).
5. **User Parameters & Validation:**
   - Add/remove exposed User Parameters with `AddUserVariables` / `RemoveUserVariables`.
   - Validate compilation and stack issues via `GetSystemCompileState`, `GetStackIssues`, and `ApplyStackIssueFix`.

## 3. Level, Component & Blueprint Placement (`NiagaraToolsets.NiagaraToolset_Component`, `NiagaraToolsets.NiagaraToolset_Blueprint`, `EditorToolset.ActorToolset`, `EditorToolset.LevelToolset`)

- Spawn an instance in the level using `EditorToolset.LevelToolset.AddToSceneFromAsset` (with the `UNiagaraSystem` path) or `EditorToolset.LevelToolset.AddToSceneFromClass` (`ANiagaraActor`).
- Attach a `UNiagaraComponent` to a Blueprint or actor with `EditorToolset.ActorToolset.AddComponent` (`ComponentType: "/Script/Niagara.NiagaraComponent"`), or generate a Blueprint wrapper via `NiagaraToolsets.NiagaraToolset_Blueprint.ConstructNiagaraBPWrapperFromSystem` / `ConstructNiagaraBPWrapperFromComponent`.
- Assign systems and override per-instance User Parameters using `NiagaraToolsets.NiagaraToolset_Component.SetSystem`, `NiagaraToolsets.NiagaraToolset_Component.GetUserVariables`, `NiagaraToolsets.NiagaraToolset_Component.GetVariable`, and `NiagaraToolsets.NiagaraToolset_Component.SetVariable`.
