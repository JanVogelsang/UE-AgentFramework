// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "AgentFrameworkBuildToolset.generated.h"

class UAsyncFunctionResultString;

USTRUCT(BlueprintType)
struct FAgentFrameworkBuildLightingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Build")
	FString LevelName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Build")
	FString Quality;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Build")
	bool bBuildStarted = false;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Build")
	FString Message;
};

UCLASS(BlueprintType)
class AGENTFRAMEWORKACTIONS_API UAgentFrameworkBuildToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (AICallable, AIAccessMode = "Execute"), Category = "AgentFramework|Build")
	static FAgentFrameworkBuildLightingResult BuildLighting(const FString& Quality = TEXT("Preview"));

	UFUNCTION(meta = (AICallable, AIAccessMode = "Execute"), Category = "AgentFramework|Build")
	static UAsyncFunctionResultString* PackageProject(
		const FString& TargetPlatform = TEXT("Win64"),
		const FString& Configuration = TEXT("Development"),
		const FString& OutputDirectory = TEXT(""),
		bool bCook = true,
		bool bArchive = true,
		int32 TimeoutSeconds = 600);
};
