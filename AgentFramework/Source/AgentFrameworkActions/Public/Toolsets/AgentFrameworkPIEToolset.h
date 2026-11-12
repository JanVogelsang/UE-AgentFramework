// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "AgentFrameworkPIEToolset.generated.h"

USTRUCT(BlueprintType)
struct FAgentFrameworkPIEFunctionResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|PIE")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|PIE")
	FString ActorLabel;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|PIE")
	FString ActorClass;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|PIE")
	FString FunctionName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|PIE")
	FString ReturnValueJson;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|PIE")
	FString OutParametersJson;
};

/**
 * UE 5.9 Native Toolset for invoking UFunctions on live Play-In-Editor (PIE) actors.
 * Complements UE 5.9's native EditorAppToolset (StartPIE, StopPIE, IsPIERunning),
 * LevelToolset (FindActors), InputToolset, and SlateInspectorToolset.
 */
UCLASS(BlueprintType)
class AGENTFRAMEWORKACTIONS_API UAgentFrameworkPIERuntimeToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Invoke a BlueprintCallable or Exec UFunction on a live actor in the active PIE world,
	 * with optional JSON arguments and structured return / out-parameter serialization.
	 */
	UFUNCTION(meta = (AICallable, AIAccessMode = "Execute"), Category = "AgentFramework|PIE")
	static FAgentFrameworkPIEFunctionResult CallPIEActorFunction(
		const FString& ActorIdentifier,
		const FString& FunctionName,
		const TOptional<FString>& ParametersJson);
};
