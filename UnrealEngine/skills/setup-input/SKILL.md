---
name: setup-input
description: Use this skill to create and configure Unreal Engine 5.9 Enhanced Input assets (`UInputAction`, `UInputMappingContext`), bind keys, and configure `UInputModifier` and `UInputTrigger` chains via `AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset`.
---

# Setup Enhanced Input (Unreal Engine 5.9)

Use `AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset` (via `call_tool` on `/mcp`) for authoring Enhanced Input assets in the Content Browser, and `InputToolset.InputToolset` (`ApplyInput`, `IsInputApplied`) for injecting runtime inputs during PIE testing.

## Workflow

### Step 1: Create Input Actions (`UInputAction`)
Call `AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.CreateInputAction` for each semantic gameplay input:
- `AssetPath`: Full package path (e.g., `/Game/Input/Actions/IA_Move`, `/Game/Input/Actions/IA_Look`, `/Game/Input/Actions/IA_Jump`).
- `ValueType`:
  - `"Boolean"` (also accepts `"Bool"`) — Buttons such as Jump, Fire, Interact, Crouch.
  - `"Axis1D"` (also accepts `"Float"`, `"1D"`) — Single-axis inputs such as Throttle, Zoom, Scroll.
  - `"Axis2D"` (also accepts `"Vector2D"`, `"2D"`) — 2D directional inputs such as Move (`WASD` / Left Stick) or Look (Mouse Delta / Right Stick).
  - `"Axis3D"` (also accepts `"Vector"`, `"3D"`) — 3D spatial inputs.
- Returns `FAgentFrameworkInputActionResult` (`AssetPath`, `PackageName`, `ValueType`).

### Step 2: Create an Input Mapping Context (`UInputMappingContext`)
Call `AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.CreateInputMappingContext`:
- `AssetPath`: Full package path (e.g., `/Game/Input/IMC_Default`).
- Returns `FAgentFrameworkInputMappingContextResult` (`AssetPath`, `PackageName`).

### Step 3: Bind Keys to Input Actions (`AddInputMapping`)
Call `AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.AddInputMapping`:
- `MappingContextPath`: Path to the `UInputMappingContext` (e.g., `/Game/Input/IMC_Default`).
- `InputActionPath`: Path to the `UInputAction` (e.g., `/Game/Input/Actions/IA_Move`).
- `KeyName`: Unreal Engine `FKey` name, such as:
  - Keyboard: `W`, `A`, `S`, `D`, `SpaceBar`, `LeftShift`, `LeftControl`, `E`, `F`, `Tab`, `Escape`
  - Mouse: `Mouse2D`, `MouseX`, `MouseY`, `LeftMouseButton`, `RightMouseButton`, `MouseWheelAxis`
  - Gamepad: `Gamepad_Left2D`, `Gamepad_Right2D`, `Gamepad_FaceButton_Bottom`, `Gamepad_RightTriggerAxis`
- Optional `Modifiers`: Array of `FAgentFrameworkInputModifierSpec` objects `{"Type": "...", "Properties": {"Key": "Val"}}`.
- Optional `Triggers`: Array of `FAgentFrameworkInputTriggerSpec` objects `{"Type": "...", "Properties": {"Key": "Val"}}`.
- Returns `FAgentFrameworkInputMappingResult` (`MappingContextPath`, `InputActionPath`, `KeyName`, `ModifierCount`, `TriggerCount`, `TotalMappings`).

### Step 4: Configure Modifiers & Triggers (`ConfigureInputMappingModifiersTriggers`)
Call `AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.ConfigureInputMappingModifiersTriggers` to update or replace the `UInputModifier` / `UInputTrigger` arrays on an existing `(MappingContextPath, InputActionPath, KeyName)` mapping (passing an explicit empty array `[]` clears modifiers or triggers):
- **Standard 2D Movement (`IA_Move` as `Axis2D`):**
  - `D` (Right, +X): omit `Modifiers` or pass `[]`
  - `A` (Left, -X): `Modifiers = [{"Type": "Negate"}]`
  - `W` (Forward, +Y): `Modifiers = [{"Type": "SwizzleAxis", "Properties": {"Order": "YXZ"}}]`
  - `S` (Backward, -Y): `Modifiers = [{"Type": "SwizzleAxis", "Properties": {"Order": "YXZ"}}, {"Type": "Negate"}]`
- **How `Type` Resolves:** Concrete `UInputModifier` and `UInputTrigger` subclasses resolve by class name or short name without the `InputModifier` / `InputTrigger` prefix:
  - Common Modifiers: `"Negate"`, `"SwizzleAxis"`, `"DeadZone"`, `"Scalar"`, `"ResponseCurveExponential"`, `"FOVScaling"`, `"Smooth"`.
  - Common Triggers: `"Down"`, `"Pressed"`, `"Released"`, `"Hold"`, `"HoldAndRelease"`, `"Tap"`, `"Pulse"`, `"ChordAction"`.
  - Use `Properties` (`TMap<FString, FString>`) to set reflected `UPROPERTY` fields using Unreal `ImportText` strings (e.g., `{"HoldTimeThreshold": "0.5"}` or `{"LowerThreshold": "0.2"}`).

### Step 5: Bind in Blueprint or C++ & Verify in PIE
1. Assign the `UInputMappingContext` and `UInputAction` references on the Character or PlayerController Blueprint using `EditorToolset.ObjectToolset.SetProperties` or `BlueprintToolsets.BlueprintDslToolset.Edit`.
2. Start PIE (`EditorToolset.EditorAppToolset.StartPIE`) and verify input response using `InputToolset.InputToolset.ApplyInput` and `AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction`.
