// Copyright 2026 AgentFramework. All Rights Reserved.

#include "AgentFrameworkActionsModule.h"
#include "AgentFrameworkCoreModule.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Toolsets/AgentFrameworkCppToolset.h"
#include "Toolsets/AgentFrameworkBuildToolset.h"
#include "Toolsets/AgentFrameworkEnhancedInputToolset.h"
#include "Toolsets/AgentFrameworkAnimationToolset.h"
#include "Toolsets/AgentFrameworkGASToolset.h"
#include "Toolsets/AgentFrameworkPIEToolset.h"

#define LOCTEXT_NAMESPACE "FAgentFrameworkActionsModule"

void FAgentFrameworkActionsModule::StartupModule()
{
	if (UToolsetRegistry::IsAvailable())
	{
		UToolsetRegistry::RegisterToolsetClass(UAgentFrameworkCppToolset::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UAgentFrameworkBuildToolset::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UAgentFrameworkEnhancedInputAuthoringToolset::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UAgentFrameworkAnimationAuthoringToolset::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UAgentFrameworkGASAuthoringToolset::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UAgentFrameworkPIERuntimeToolset::StaticClass());

		UE_LOG(LogAgentFramework, Log, TEXT("AgentFrameworkActions: Registered 6 UE 5.9 UToolsetDefinition classes with UToolsetRegistry."));
	}
	else
	{
		UE_LOG(LogAgentFramework, Warning, TEXT("AgentFrameworkActions: UToolsetRegistry is not available during StartupModule."));
	}
}

void FAgentFrameworkActionsModule::ShutdownModule()
{
	if (UToolsetRegistry::IsAvailable())
	{
		UToolsetRegistry::UnregisterToolsetClass(UAgentFrameworkPIERuntimeToolset::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UAgentFrameworkGASAuthoringToolset::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UAgentFrameworkAnimationAuthoringToolset::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UAgentFrameworkEnhancedInputAuthoringToolset::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UAgentFrameworkBuildToolset::StaticClass());
		UToolsetRegistry::UnregisterToolsetClass(UAgentFrameworkCppToolset::StaticClass());
	}
}



FAgentFrameworkActionsModule& FAgentFrameworkActionsModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAgentFrameworkActionsModule>("AgentFrameworkActions");
}

bool FAgentFrameworkActionsModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded("AgentFrameworkActions");
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAgentFrameworkActionsModule, AgentFrameworkActions)
