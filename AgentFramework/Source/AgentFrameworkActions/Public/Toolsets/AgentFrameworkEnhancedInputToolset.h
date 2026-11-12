// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "AgentFrameworkEnhancedInputToolset.generated.h"

USTRUCT(BlueprintType)
struct FAgentFrameworkInputModifierSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|EnhancedInput")
	FString Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|EnhancedInput")
	TMap<FString, FString> Properties;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkInputTriggerSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|EnhancedInput")
	FString Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|EnhancedInput")
	TMap<FString, FString> Properties;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkInputActionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	FString PackageName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	FString ValueType;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkInputMappingContextResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	FString PackageName;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkInputMappingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	FString MappingContextPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	FString InputActionPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	FString KeyName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	int32 ModifierCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	int32 TriggerCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|EnhancedInput")
	int32 TotalMappings = 0;
};

UCLASS(BlueprintType)
class AGENTFRAMEWORKACTIONS_API UAgentFrameworkEnhancedInputAuthoringToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|EnhancedInput")
	static FAgentFrameworkInputActionResult CreateInputAction(
		const FString& AssetPath,
		const FString& ValueType = TEXT("Boolean"));

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|EnhancedInput")
	static FAgentFrameworkInputMappingContextResult CreateInputMappingContext(
		const FString& AssetPath);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|EnhancedInput")
	static FAgentFrameworkInputMappingResult AddInputMapping(
		const FString& MappingContextPath,
		const FString& InputActionPath,
		const FString& KeyName,
		const TOptional<TArray<FAgentFrameworkInputModifierSpec>>& Modifiers,
		const TOptional<TArray<FAgentFrameworkInputTriggerSpec>>& Triggers);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|EnhancedInput")
	static FAgentFrameworkInputMappingResult ConfigureInputMappingModifiersTriggers(
		const FString& MappingContextPath,
		const FString& InputActionPath,
		const FString& KeyName,
		const TOptional<TArray<FAgentFrameworkInputModifierSpec>>& Modifiers,
		const TOptional<TArray<FAgentFrameworkInputTriggerSpec>>& Triggers);
};
