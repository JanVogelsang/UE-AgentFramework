// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AgentFrameworkInterfaces.h"

/**
 * FAgentFrameworkDataAssetActions
 *
 * Provides tools for creating, updating, and inspecting UDataAsset / UPrimaryDataAsset assets:
 *   - create_data_asset: Create a Data Asset of a specified class
 *   - set_data_asset_properties: Write properties on a Data Asset using reflection
 *   - set_uobject_properties: Write properties on any UObject asset using reflection
 *   - add_instanced_subobject: Instantiate and attach inline UObject subobjects
 *   - get_data_asset_info: Inspect a Data Asset's current properties and values
 */
class AGENTFRAMEWORKACTIONS_API FAgentFrameworkDataAssetActions : public IAgentFrameworkActionExecutor
{
public:
	FAgentFrameworkDataAssetActions();
	virtual ~FAgentFrameworkDataAssetActions();

	virtual FName GetActionName() const override;
	virtual FAgentFrameworkActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) override;
	virtual TArray<FString> GetSupportedToolNames() const override;
	virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const override;

private:
	FAgentFrameworkActionResult ExecuteCreateDataAsset(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
	FAgentFrameworkActionResult ExecuteSetDataAssetProperties(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
	FAgentFrameworkActionResult ExecuteGetDataAssetInfo(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
	FAgentFrameworkActionResult ExecuteAddInstancedSubobject(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);
	FAgentFrameworkActionResult ExecuteSaveAsset(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

public:
	static int32 SetObjectPropertiesFromJsonObject(UObject* TargetObject, const TSharedPtr<FJsonObject>& PropertiesObj, FAgentFrameworkActionResult& Result);

private:
	void PlaySuccessSound();
};

