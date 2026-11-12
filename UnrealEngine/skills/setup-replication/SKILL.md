---
name: setup-replication
description: Configure an actor class for network replication in multiplayer UE games.
---
# Skill: Setup Actor Replication
## Description
Configure an actor class for network replication in multiplayer UE games.
## Arguments
- {{arg}}: Actor class to configure replication for
## Steps

### Option A: UE 5.9 Native Toolset Routes (`EditorToolset.ObjectToolset` & `BlueprintToolsets.BlueprintToolset`)

Use UE 5.9's MCP meta-tools (`describe_tools` / `call_tool`) with module-qualified toolset names `BlueprintToolsets.BlueprintToolset`, `EditorToolset.ObjectToolset`, and `EditorToolset.AssetToolset`:

1. **Configure Actor Network Replication (`BlueprintToolsets.BlueprintToolset.GetDefaultObject` + `EditorToolset.ObjectToolset.SetProperties`)**:
   First load the Blueprint (`EditorToolset.AssetToolset.LoadAsset`) and fetch its Class Default Object (`BlueprintToolsets.BlueprintToolset.GetDefaultObject`), then call `EditorToolset.ObjectToolset.SetProperties` with `Instance` set to the CDO and `Values` formatted as a JSON string:
   ```json
   {
     "toolset_name": "EditorToolset.ObjectToolset",
     "tool_name": "SetProperties",
     "arguments": {
       "Instance": "/Game/Blueprints/BP_NetworkPawn.Default__BP_NetworkPawn_C",
       "Values": "{\"bReplicates\": true, \"bReplicateMovement\": true, \"NetDormancy\": \"DORM_Never\", \"NetUpdateFrequency\": 100.0, \"NetPriority\": 1.0}"
     }
   }
   ```
   *(If unsure of the exact CDO reference or field names, inspect first with `EditorToolset.ObjectToolset.ListProperties` or `EditorToolset.ObjectToolset.GetPropertySchema`.)*

2. **Configure Variable Replication & RepNotify (`BlueprintToolsets.BlueprintToolset.SetVariableReplication`)**:
   Call `BlueprintToolsets.BlueprintToolset.SetVariableReplication` with `Blueprint`, `VariableName`, and `Replication` (`"None"`, `"Replicated"`, or `"RepNotify"`):

   - **Standard Variable Replication (`"Replicated"`)**:
     ```json
     {
       "toolset_name": "BlueprintToolsets.BlueprintToolset",
       "tool_name": "SetVariableReplication",
       "arguments": {
         "Blueprint": "/Game/Blueprints/BP_NetworkPawn",
         "VariableName": "Health",
         "Replication": "Replicated"
       }
     }
     ```

   - **Replication with RepNotify Callback (`"RepNotify"`)**:
     Setting `Replication` to `"RepNotify"` automatically generates the `OnRep_Shield` callback function on the Blueprint if it does not already exist:
     ```json
     {
       "toolset_name": "BlueprintToolsets.BlueprintToolset",
       "tool_name": "SetVariableReplication",
       "arguments": {
         "Blueprint": "/Game/Blueprints/BP_NetworkPawn",
         "VariableName": "Shield",
         "Replication": "RepNotify"
       }
     }
     ```

   - **Verify, Compile, and Save**:
     Query current replication mode with `BlueprintToolsets.BlueprintToolset.GetVariableReplication`, compile via `BlueprintToolsets.BlueprintToolset.CompileBlueprint`, and save via `EditorToolset.AssetToolset.SaveAsset`. *(Note: lifetime replication conditions such as `COND_OwnerOnly` are configured in C++ via `DOREPLIFETIME_CONDITION` as shown below, or via Blueprint property details if exposed on the variable.)*

### Option B: C++ Code Patterns (Base Class Implementation)

1. **Constructor**: Enable replication
   ```cpp
   bReplicates = true;
   SetReplicateMovement(true);
   ```

2. **Add GetLifetimeReplicatedProps**:
   ```cpp
   void {{arg}}::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
   {
       Super::GetLifetimeReplicatedProps(OutLifetimeProps);
       DOREPLIFETIME({{arg}}, Health);
       DOREPLIFETIME_CONDITION({{arg}}, bIsVisible, COND_OwnerOnly);
   }
   ```

3. **Mark properties for replication**:
   
   **Standard Replication (No callback)**:
   ```cpp
   UPROPERTY(Replicated)
   float Health;
   ```
   
   **Replication with RepNotify Callback (Client side effect)**:
   ```cpp
   UPROPERTY(ReplicatedUsing=OnRep_Shield)
   float Shield;
   
   UFUNCTION()
   void OnRep_Shield(float OldShield);
   ```

4. **Server RPCs** for player actions:
   ```cpp
   UFUNCTION(Server, Reliable)
   void ServerDoAction();
   ```

5. **Required include**: `#include "Net/UnrealNetwork.h"`

## Python API Tip (UE 5.9)
- To set replication on a Blueprint CDO via Unreal Python: `cdo = bp.get_editor_property('generated_class').get_default_object()`, call `cdo.set_replicates(True)`, and compile with `unreal.BlueprintEditorLibrary.compile_blueprint(bp)`.
