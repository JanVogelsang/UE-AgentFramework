// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AgentFrameworkInterfaces.h"

/**
 * UE-Specific Context Executor.
 * 
 * Provides on-demand tools for AgentFramework (Gemini) to explore the project.
 * 
 * - search_assets: Search the asset registry by class, name, or path
 * 
 * These tools are always available regardless of security mode (read-only).
 */
class AGENTFRAMEWORKACTIONS_API FAgentFrameworkContextActions : public IAgentFrameworkActionExecutor
{
public:
    FAgentFrameworkContextActions();
    virtual ~FAgentFrameworkContextActions();

    virtual FName GetActionName() const override;
    virtual FAgentFrameworkActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) override;
    virtual TArray<FString> GetSupportedToolNames() const override;
    virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const override;

private:
    FAgentFrameworkActionResult ExecuteSearchAssets(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
    FAgentFrameworkActionResult ExecuteListDirectory(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
    FAgentFrameworkActionResult ExecuteReadFileSnippet(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
    FAgentFrameworkActionResult ExecuteActivateSkill(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
    FAgentFrameworkActionResult ExecuteEnforceNamingConventions(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
    FAgentFrameworkActionResult ExecuteOrganizeAssetsByType(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
    FAgentFrameworkActionResult ExecuteConsolidateAssetReferences(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
    FAgentFrameworkActionResult ExecuteDeleteAsset(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
    FAgentFrameworkActionResult ExecuteCreateAsset(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
};
