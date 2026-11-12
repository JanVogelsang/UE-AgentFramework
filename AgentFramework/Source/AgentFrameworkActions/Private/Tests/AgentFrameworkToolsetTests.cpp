// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AgentFrameworkActionUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <limits>
#include "ToolsetRegistry/ToolsetJsonSchema.h"
#include "ToolsetRegistry/ToolsetRegistry.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "Toolsets/AgentFrameworkAnimationToolset.h"
#include "Toolsets/AgentFrameworkBuildToolset.h"
#include "Toolsets/AgentFrameworkCppToolset.h"
#include "Toolsets/AgentFrameworkEnhancedInputToolset.h"
#include "Toolsets/AgentFrameworkGASToolset.h"
#include "Toolsets/AgentFrameworkPIEToolset.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAgentFrameworkUE59ToolsetRegistrationTest,
	"AgentFramework.Toolsets.UE59RegistrationAndSchemas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAgentFrameworkUE59ToolsetRegistrationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("UToolsetRegistry::IsAvailable() should be true"), UToolsetRegistry::IsAvailable());

	const TArray<UClass*> ExpectedClasses = {
		UAgentFrameworkCppToolset::StaticClass(),
		UAgentFrameworkBuildToolset::StaticClass(),
		UAgentFrameworkEnhancedInputAuthoringToolset::StaticClass(),
		UAgentFrameworkAnimationAuthoringToolset::StaticClass(),
		UAgentFrameworkGASAuthoringToolset::StaticClass(),
		UAgentFrameworkPIERuntimeToolset::StaticClass()
	};

	for (UClass* Cls : ExpectedClasses)
	{
		TestTrue(
			FString::Printf(TEXT("%s should be registered"), *Cls->GetName()),
			UToolsetRegistry::IsToolsetClassRegistered(Cls));

		const FString SchemaStr = UToolsetRegistry::GetToolsetJsonSchema(Cls);
		TestFalse(
			FString::Printf(TEXT("GetToolsetJsonSchema(%s) should be non-empty"), *Cls->GetName()),
			SchemaStr.IsEmpty());

		TSharedPtr<FJsonObject> ParsedSchema;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SchemaStr);
		TestTrue(
			FString::Printf(TEXT("Schema for %s should parse as JSON object"), *Cls->GetName()),
			FJsonSerializer::Deserialize(Reader, ParsedSchema) && ParsedSchema.IsValid());
	}

	UE::ToolsetRegistry::FToolsetRegistry* Registry = UE::ToolsetRegistry::FToolsetRegistry::Get();
	TestNotNull(TEXT("FToolsetRegistry::Get() should be valid"), Registry);
	if (!Registry)
	{
		return false;
	}

	const TArray<UE::ToolsetRegistry::FToolsetJsonSchema> AllSchemas = Registry->GetToolsetJsonSchemas();
	TMap<FString, TArray<FString>> ToolsByToolset;
	TSet<FString> AllAgentFrameworkTools;
	for (const UE::ToolsetRegistry::FToolsetJsonSchema& Schema : AllSchemas)
	{
		if (Schema.IsEmpty())
		{
			continue;
		}
		const FString ToolsetName = Schema.GetToolsetName();
		if (ToolsetName.StartsWith(TEXT("AgentFrameworkActions.")))
		{
			const TArray<FString> ToolNames = Schema.ListToolNames();
			ToolsByToolset.Add(ToolsetName, ToolNames);
			for (const FString& ToolName : ToolNames)
			{
				AllAgentFrameworkTools.Add(ToolName);
			}
		}
	}

	TestFalse(
		TEXT("Removed MassAndSmartObjects toolset must not be registered"),
		ToolsByToolset.Contains(TEXT("AgentFrameworkActions.AgentFrameworkMassAndSmartObjectsToolset")));
	TestFalse(
		TEXT("Removed MassAndSmartObjects short name must not be registered"),
		UToolsetRegistry::IsToolsetRegistered(TEXT("AgentFrameworkMassAndSmartObjectsToolset")));

	const TArray<FString> ExpectedToolNames = {
		TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.CreateCppClass"),
		TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.ScaffoldCppClass"),
		TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.RegenerateProjectFiles"),
		TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.GetCppReflectionInfo"),
		TEXT("AgentFrameworkActions.AgentFrameworkBuildToolset.BuildLighting"),
		TEXT("AgentFrameworkActions.AgentFrameworkBuildToolset.PackageProject"),
		TEXT("AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.CreateInputAction"),
		TEXT("AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.CreateInputMappingContext"),
		TEXT("AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.AddInputMapping"),
		TEXT("AgentFrameworkActions.AgentFrameworkEnhancedInputAuthoringToolset.ConfigureInputMappingModifiersTriggers"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.CreateAnimBlueprint"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.CreateAnimMontage"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.ConfigureAnimMontage"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.CreateBlendSpace"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.GetSkeletonAnimationInfo"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.CreateIKRig"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.CreateIKRetargeter"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.ConfigureMotionMatchingDatabase"),
		TEXT("AgentFrameworkActions.AgentFrameworkGASAuthoringToolset.CreateAttributeSetClass"),
		TEXT("AgentFrameworkActions.AgentFrameworkGASAuthoringToolset.CreateGameplayEffectBlueprint"),
		TEXT("AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction")
	};

	for (const FString& ExpectedTool : ExpectedToolNames)
	{
		TestTrue(
			FString::Printf(TEXT("Expected tool '%s' should appear in FToolsetRegistry schemas"), *ExpectedTool),
			AllAgentFrameworkTools.Contains(ExpectedTool));
	}

	const TArray<FString> RetiredToolNames = {
		TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.ModifyCppFile"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.ImportAnimationFbx"),
		TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.AssignAnimBlueprint"),
		TEXT("AgentFrameworkActions.AgentFrameworkGASAuthoringToolset.SetupAbilitySystemComponent"),
		TEXT("AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.QueryPIEWorldActors"),
		TEXT("AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.InvokePIEWidgetDelegate")
	};

	for (const FString& RetiredTool : RetiredToolNames)
	{
		TestFalse(
			FString::Printf(TEXT("Retired tool '%s' must not appear in schemas"), *RetiredTool),
			AllAgentFrameworkTools.Contains(RetiredTool));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAgentFrameworkPathSafetyAndUtilsTest,
	"AgentFramework.Toolsets.PathSafetyAndUtilities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAgentFrameworkPathSafetyAndUtilsTest::RunTest(const FString& Parameters)
{
	// Positive source path checks
	const FString ValidGameHeader = FPaths::Combine(FPaths::GameSourceDir(), TEXT("TestGame/Public/TestActor.h"));
	const FString ValidPluginCpp = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("TestPlugin/Source/TestPlugin/Private/TestPlugin.cpp"));
	TestTrue(TEXT("Game Source header path should be writable"), UAgentFrameworkCppToolset::IsWritableSourcePath(ValidGameHeader));
	TestTrue(TEXT("Project Plugin Source cpp path should be writable"), UAgentFrameworkCppToolset::IsWritableSourcePath(ValidPluginCpp));

	// Negative source path checks
	TestFalse(TEXT("Path traversal should be rejected"),
		UAgentFrameworkCppToolset::IsWritableSourcePath(FPaths::Combine(FPaths::GameSourceDir(), TEXT("../Config/DefaultEngine.ini"))));
	TestFalse(TEXT("Build.cs modification should be rejected"),
		UAgentFrameworkCppToolset::IsWritableSourcePath(FPaths::Combine(FPaths::GameSourceDir(), TEXT("MyGame/MyGame.Build.cs"))));
	TestFalse(TEXT("uplugin modification should be rejected"),
		UAgentFrameworkCppToolset::IsWritableSourcePath(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("MyPlugin/MyPlugin.uplugin"))));
	TestFalse(TEXT("Non-C++ extension should be rejected"),
		UAgentFrameworkCppToolset::IsWritableSourcePath(FPaths::Combine(FPaths::GameSourceDir(), TEXT("MyGame/Notes.txt"))));
	TestFalse(TEXT("SourceBackup prefix bypass must be rejected"),
		UAgentFrameworkCppToolset::IsWritableSourcePath(FPaths::Combine(FPaths::ProjectDir(), TEXT("SourceBackup/Evil.h"))));
	TestFalse(TEXT("Content/Source path bypass must be rejected"),
		UAgentFrameworkCppToolset::IsWritableSourcePath(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Content/Source/Evil.h"))));
	TestFalse(TEXT("Plugin Content/Source path bypass must be rejected"),
		UAgentFrameworkCppToolset::IsWritableSourcePath(FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("MyPlugin/Content/Source/Evil.h"))));

	// C++ code safety checks
	FString RejectReason;
	TestTrue(TEXT("Benign C++ snippet should pass"),
		UAgentFrameworkCppToolset::ValidateCodeSafety(TEXT("Health = FMath::Clamp(Health + Delta, 0.0f, MaxHealth);"), RejectReason));
	TestFalse(TEXT("system() should be rejected"),
		UAgentFrameworkCppToolset::ValidateCodeSafety(TEXT("void Foo() { system(\"rm -rf /\"); }"), RejectReason));
	TestFalse(TEXT("RejectReason should be populated"), RejectReason.IsEmpty());
	TestFalse(TEXT("FPlatformProcess::CreateProc should be rejected"),
		UAgentFrameworkCppToolset::ValidateCodeSafety(TEXT("FPlatformProcess::CreateProc(TEXT(\"cmd\"), nullptr, true, false, false, nullptr, 0, nullptr, nullptr);")));
	TestFalse(TEXT("#import directive should be rejected"),
		UAgentFrameworkCppToolset::ValidateCodeSafety(TEXT("#import \"evil.dll\"")));

	// C++ identifiers and finite float formatting
	TestTrue(TEXT("Valid identifier accepted"), UAgentFrameworkActionUtils::IsValidCppIdentifier(TEXT("MyHealth_01")));
	TestFalse(TEXT("Leading digit rejected"), UAgentFrameworkActionUtils::IsValidCppIdentifier(TEXT("1Health")));
	TestFalse(TEXT("C++ keyword 'class' rejected"), UAgentFrameworkActionUtils::IsValidCppIdentifier(TEXT("class")));
	TestFalse(TEXT("Semicolon injection rejected"), UAgentFrameworkActionUtils::IsValidCppIdentifier(TEXT("Foo;int")));

	FString Literal;
	TestTrue(TEXT("Finite float formats cleanly"), UAgentFrameworkActionUtils::FormatCppFloatLiteral(100.0, Literal));
	TestEqual(TEXT("Formatted float literal has f suffix"), Literal, TEXT("100.0f"));
	TestFalse(TEXT("NaN float literal rejected"), UAgentFrameworkActionUtils::FormatCppFloatLiteral(std::numeric_limits<double>::quiet_NaN(), Literal));

	// Asset path splitting including dots in parent directories & trailing slashes
	FString PkgName, PkgPath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(TEXT("/Game/Characters.v2/BP_Hero.BP_Hero"), PkgName, PkgPath, AssetName);
	TestEqual(TEXT("SplitAssetPath PackageName"), PkgName, TEXT("/Game/Characters.v2/BP_Hero"));
	TestEqual(TEXT("SplitAssetPath PackagePath"), PkgPath, TEXT("/Game/Characters.v2"));
	TestEqual(TEXT("SplitAssetPath AssetName"), AssetName, TEXT("BP_Hero"));

	UAgentFrameworkActionUtils::SplitAssetPath(TEXT("/Game/Characters.v2/BP_Hero/"), PkgName, PkgPath, AssetName);
	TestEqual(TEXT("Trailing slash trimmed PackageName"), PkgName, TEXT("/Game/Characters.v2/BP_Hero"));
	TestEqual(TEXT("Trailing slash trimmed AssetName"), AssetName, TEXT("BP_Hero"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAgentFrameworkToolExecutionAndErrorsTest,
	"AgentFramework.Toolsets.ExecutionAndErrorPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAgentFrameworkToolExecutionAndErrorsTest::RunTest(const FString& Parameters)
{
	UE::ToolsetRegistry::FToolsetRegistry* Registry = UE::ToolsetRegistry::FToolsetRegistry::Get();
	if (!TestNotNull(TEXT("FToolsetRegistry::Get() must be valid"), Registry))
	{
		return false;
	}

	auto RunSync = [&](const FString& ToolName, const FString& JsonArgs) -> TValueOrError<FString, FString>
	{
		TFuture<TValueOrError<FString, FString>> Future = Registry->ExecuteTool(ToolName, JsonArgs);
		TestTrue(FString::Printf(TEXT("Future for %s should be ready synchronously"), *ToolName), Future.IsReady());
		return Future.Get();
	};

	// Positive test: GetCppReflectionInfo on Actor
	{
		const TValueOrError<FString, FString> Res = RunSync(
			TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.GetCppReflectionInfo"),
			TEXT("{\"TypeName\":\"Actor\",\"bIncludeInherited\":false}"));
		TestFalse(TEXT("GetCppReflectionInfo(Actor) should succeed"), Res.HasError());
		if (!Res.HasError())
		{
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res.GetValue());
			TestTrue(TEXT("Reflection output parses as JSON"), FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid());
			const TSharedPtr<FJsonObject>* RetObj = nullptr;
			TestTrue(TEXT("Output wrapped in returnValue object"), Root && Root->TryGetObjectField(TEXT("returnValue"), RetObj));
			if (RetObj && (*RetObj).IsValid())
			{
				FString Kind;
				(*RetObj)->TryGetStringField(TEXT("TypeKind"), Kind);
				TestEqual(TEXT("Actor TypeKind should be Class"), Kind, TEXT("Class"));
			}
		}
	}

	// Positive test: GetCppReflectionInfo on enum should omit synthetic MAX entry
	{
		const TValueOrError<FString, FString> Res = RunSync(
			TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.GetCppReflectionInfo"),
			TEXT("{\"TypeName\":\"EAttachmentRule\",\"bIncludeInherited\":false}"));
		TestFalse(TEXT("GetCppReflectionInfo(EAttachmentRule) should succeed"), Res.HasError());
		if (!Res.HasError())
		{
			TestFalse(TEXT("Enum output must not leak EAttachmentRule_MAX"), Res.GetValue().Contains(TEXT("_MAX")));
		}
	}

	// Error path tests (every one should reject cleanly with an error instead of returning fake success JSON)
	const TArray<TPair<FString, FString>> ExpectedErrorCalls = {
		{
			TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.GetCppReflectionInfo"),
			TEXT("{\"TypeName\":\"NonExistent_AgentFramework_Type_XYZ\"}")
		},
		{
			TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.CreateCppClass"),
			TEXT("{\"ClassName\":\"Bad;Class\",\"ParentClassName\":\"Actor\"}")
		},
		{
			TEXT("AgentFrameworkActions.AgentFrameworkCppToolset.RegenerateProjectFiles"),
			TEXT("{\"TimeoutSeconds\":0}")
		},
		{
			TEXT("AgentFrameworkActions.AgentFrameworkBuildToolset.BuildLighting"),
			TEXT("{\"Quality\":\"UltraMegaInvalid\"}")
		},
		{
			TEXT("AgentFrameworkActions.AgentFrameworkBuildToolset.PackageProject"),
			TEXT("{\"TargetPlatform\":\"Bad;Platform\"}")
		},
		{
			TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.GetSkeletonAnimationInfo"),
			TEXT("{\"AssetPath\":\"/Game/__NonExistent_Test_Path__/MissingSkeleton\"}")
		},
		{
			TEXT("AgentFrameworkActions.AgentFrameworkAnimationAuthoringToolset.ConfigureAnimMontage"),
			TEXT("{\"MontagePath\":\"/Game/__NonExistent_Test_Path__/MissingMontage\"}")
		},
		{
			TEXT("AgentFrameworkActions.AgentFrameworkGASAuthoringToolset.CreateGameplayEffectBlueprint"),
			TEXT("{\"AssetPath\":\"/Game/__NonExistent_Test_Path__/GE_Invalid\",\"DurationPolicy\":\"InvalidPolicyXYZ\"}")
		},
		{
			TEXT("AgentFrameworkActions.AgentFrameworkPIERuntimeToolset.CallPIEActorFunction"),
			TEXT("{\"ActorIdentifier\":\"PlayerPawn\",\"FunctionName\":\"K2_GetActorLocation\"}")
		}
	};

	AddExpectedMessage(TEXT(""), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, -1, false);
	for (const TPair<FString, FString>& Case : ExpectedErrorCalls)
	{
		const TValueOrError<FString, FString> Res = RunSync(Case.Key, Case.Value);
		TestTrue(
			FString::Printf(TEXT("Expected error from %s with args %s"), *Case.Key, *Case.Value),
			Res.HasError());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
