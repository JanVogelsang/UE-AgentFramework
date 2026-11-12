---
name: create-interface
description: Create a C++ UINTERFACE/IInterface or Blueprint Interface in Unreal Engine 5.9 using `AgentFrameworkActions.AgentFrameworkCppToolset` or `BlueprintToolsets.BlueprintToolset`.
---
# Skill: Create Blueprint & C++ Interface (Unreal Engine 5.9)
## Description
Create a Blueprint-accessible C++ interface (`UINTERFACE` + `IInterface`) with `AgentFrameworkActions.AgentFrameworkCppToolset`, or a pure Blueprint Interface asset via `BlueprintToolsets.BlueprintToolset`.
## Arguments
- `{{arg}}`: Interface name (without `U`/`I` prefix, e.g., `Interactable`)
## Steps

### Option A: C++ Backing Interface (`AgentFrameworkActions.AgentFrameworkCppToolset`)
1. **Scaffold class files** via `call_tool` on `AgentFrameworkActions.AgentFrameworkCppToolset.CreateCppClass`:
   - `ClassName`: `"{{arg}}"`
   - `ParentClassName`: `"Interface"`
   - Optional `ModuleName` / `SubDirectory`
2. **Declare interface methods** in `{{arg}}.h`:
   ```cpp
   UINTERFACE(MinimalAPI, Blueprintable)
   class U{{arg}} : public UInterface
   {
       GENERATED_BODY()
   };

   class I{{arg}}
   {
       GENERATED_BODY()
   public:
       UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interaction")
       void Interact(AActor* Interactor);
   };
   ```
3. **Refresh AST & Compile:**
   - Call `generate_compile_commands` on `cpp-ast-rag` so Clang indexes the new header/source files.
   - Rebuild the Editor target via UBT when adding new `UINTERFACE` headers (or call `LiveCodingToolset.LiveCodingToolset.CompileLiveCoding` after editing existing `.cpp` bodies).

4. **Implement on a C++ actor**:
   ```cpp
   class AMyActor : public AActor, public I{{arg}}
   {
       virtual void Interact_Implementation(AActor* Interactor) override;
   };
   ```

5. **Call via interface** (handles both C++ and Blueprint-implemented interfaces safely):
   ```cpp
   if (TargetActor && TargetActor->GetClass()->ImplementsInterface(U{{arg}}::StaticClass()))
   {
       I{{arg}}::Execute_Interact(TargetActor, this);
   }
   ```

### Option B: Implement or Author in Blueprint (`BlueprintToolsets.BlueprintToolset`)
- Call `BlueprintToolsets.BlueprintToolset.AddInterface` (`Blueprint`, `InterfaceClass`) to implement `U{{arg}}` on any `UBlueprint` asset, then implement the event graph via `BlueprintToolsets.BlueprintDslToolset.Edit` and compile with `BlueprintToolsets.BlueprintToolset.CompileBlueprint`.
