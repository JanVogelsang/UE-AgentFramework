// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "Toolsets/AgentFrameworkCppToolset.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "AgentFrameworkGASToolset.generated.h"

USTRUCT(BlueprintType)
struct FAgentFrameworkAttributeSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|GAS")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|GAS")
	float DefaultValue = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|GAS")
	bool bReplicated = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|GAS")
	FString MaxAttributeName;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkGameplayModifierSpec
{
	GENERATED_BODY()

	/** Attribute reference formatted as "<AttributeSetClassName>.<PropertyName>" (e.g. "MyAttributeSet.Health"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|GAS")
	FString AttributeClassAndName;

	/** Modifier operation: AddBase (Additive), MultiplyAdditive (Multiplicitive), DivideAdditive (Division), MultiplyCompound, AddFinal, or Override. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|GAS")
	FString ModifierOp = TEXT("AddBase");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|GAS")
	float Magnitude = 0.0f;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkGameplayEffectResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|GAS")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|GAS")
	FString DurationPolicy;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|GAS")
	float DurationSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|GAS")
	float PeriodSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|GAS")
	int32 ModifierCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|GAS")
	TArray<FString> GrantedTags;
};

/**
 * UE 5.9 Native Toolset for Gameplay Ability System (GAS) C++ AttributeSet code generation
 * and GameplayEffect Blueprint authoring.
 * Complements UE 5.9's native GAS Toolsets (GameplayAbilityToolset, AbilitySystemInspectorToolset,
 * AttributeSetToolset, GameplayCueToolset), GameplayTagsToolset, and ActorToolset::AddComponent.
 */
UCLASS(BlueprintType)
class AGENTFRAMEWORKACTIONS_API UAgentFrameworkGASAuthoringToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Generate a C++ UAttributeSet subclass (.h and .cpp) with ATTRIBUTE_ACCESSORS macros,
	 * conditional network replication, and PostGameplayEffectExecute clamping.
	 */
	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|GAS")
	static FAgentFrameworkCppClassResult CreateAttributeSetClass(
		const FString& ClassName,
		const TArray<FAgentFrameworkAttributeSpec>& Attributes,
		const FString& ModuleName = TEXT(""),
		const FString& SubDirectory = TEXT(""));

	/**
	 * Create a UGameplayEffect Blueprint asset and configure DurationPolicy (Instant, HasDuration, Infinite),
	 * DurationSeconds, PeriodSeconds, attribute Modifiers, and GrantedTags via TargetTagsGameplayEffectComponent.
	 */
	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|GAS")
	static FAgentFrameworkGameplayEffectResult CreateGameplayEffectBlueprint(
		const FString& AssetPath,
		const TOptional<FString>& DurationPolicy,
		TOptional<float> DurationSeconds,
		TOptional<float> PeriodSeconds,
		const TOptional<TArray<FAgentFrameworkGameplayModifierSpec>>& Modifiers,
		const TOptional<TArray<FString>>& GrantedTags);
};
