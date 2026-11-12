// Copyright 2026 AgentFramework. All Rights Reserved.

using UnrealBuildTool;

public class AgentFrameworkActions : ModuleRules
{
	public AgentFrameworkActions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"AgentFrameworkCore",
			"ToolsetCore",
			"ToolsetRegistry",
			"AsyncFunctionResult",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"UnrealEd",
			"Kismet",
			"AssetRegistry",
			"AssetTools",
			"Json",
			"JsonUtilities",
			"EnhancedInput",
		});
	}
}
