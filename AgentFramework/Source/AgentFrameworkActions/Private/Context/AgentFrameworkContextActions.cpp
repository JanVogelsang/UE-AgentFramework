// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Context/AgentFrameworkContextActions.h"
#include "AgentFrameworkActionUtils.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Sound/SoundBase.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectIterator.h"
#include "UObject/SavePackage.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#include "EditorAssetLibrary.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "ObjectTools.h"
#include "Factories/Factory.h"
#include "Factories/DataAssetFactory.h"
#endif

FAgentFrameworkContextActions::FAgentFrameworkContextActions() {}
FAgentFrameworkContextActions::~FAgentFrameworkContextActions() {}

FName FAgentFrameworkContextActions::GetActionName() const { return FName(TEXT("Context")); }

TArray<FString> FAgentFrameworkContextActions::GetSupportedToolNames() const
{
	return {
		TEXT("search_assets"),
		TEXT("list_directory"),
		TEXT("read_file_snippet"),
		TEXT("activate_skill"),
		TEXT("enforce_naming_conventions"),
		TEXT("organize_assets_by_type"),
		TEXT("consolidate_asset_references"),
		TEXT("delete_asset"),
		TEXT("create_asset")
	};
}

bool FAgentFrameworkContextActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAgentFrameworkActionResult Result;
	Result.bSuccess = false;

	FString Action;
	TSharedPtr<FJsonObject> ParamsPtr = Params;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("action"), Action, Result.Errors, false);
	if (Action.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("tool_name"), Action, Result.Errors, false);
	}

	if (Action == TEXT("search_assets"))
	{
		Result = ExecuteSearchAssets(Params, Result);
	}
	else if (Action == TEXT("list_directory"))
	{
		Result = ExecuteListDirectory(Params, Result);
	}
	else if (Action == TEXT("read_file_snippet"))
	{
		Result = ExecuteReadFileSnippet(Params, Result);
	}
	else if (Action == TEXT("activate_skill"))
	{
		Result = ExecuteActivateSkill(Params, Result);
	}
	else if (Action == TEXT("enforce_naming_conventions"))
	{
		Result = ExecuteEnforceNamingConventions(Params, Result);
	}
	else if (Action == TEXT("organize_assets_by_type"))
	{
		Result = ExecuteOrganizeAssetsByType(Params, Result);
	}
	else if (Action == TEXT("consolidate_asset_references"))
	{
		Result = ExecuteConsolidateAssetReferences(Params, Result);
	}
	else if (Action == TEXT("delete_asset"))
	{
		Result = ExecuteDeleteAsset(Params, Result);
	}
	else if (Action == TEXT("create_asset"))
	{
		Result = ExecuteCreateAsset(Params, Result);
	}
	else
	{
		Result.Errors.Add(TEXT("Could not determine context action."));
	}

	if (Result.bSuccess)
	{
#if WITH_EDITOR
		if (GEditor)
		{
			USoundBase* SuccessSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileSuccess.CompileSuccess"));
			if (IsValid(SuccessSound))
			{
				GEditor->PlayEditorSound(SuccessSound);
			}
		}
#endif
	}

	return Result;
}



// ============================================================================
// search_assets: Search the asset registry by class, name, or path
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteSearchAssets(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString Query, ClassFilter, PathFilter;
	TSharedPtr<FJsonObject> ParamsPtr = Params;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("query"), Query, Result.Errors, false);
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("class_filter"), ClassFilter, Result.Errors, false);
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("path_filter"), PathFilter, Result.Errors, false);

	int32 MaxResults = 50;
	UAgentFrameworkActionUtils::TryGetIntParam(ParamsPtr, TEXT("max_results"), MaxResults, Result.Errors, false);
	MaxResults = FMath::Clamp(MaxResults, 1, 200);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> AllAssets;
	AssetRegistry.GetAllAssets(AllAssets, true);

	FString Output = TEXT("=== Asset Search Results ===\n");
	int32 MatchCount = 0;

	for (const FAssetData& Asset : AllAssets)
	{
		if (MatchCount >= MaxResults) break;

		FString AssetPath = Asset.GetObjectPathString();
		FString AssetName = Asset.AssetName.ToString();
		FString AssetClass = Asset.AssetClassPath.GetAssetName().ToString();

		// Only project assets
		if (!AssetPath.StartsWith(TEXT("/Game/"))) continue;

		// Apply filters
		if (!ClassFilter.IsEmpty() && !AssetClass.Contains(ClassFilter, ESearchCase::IgnoreCase))
			continue;
		if (!PathFilter.IsEmpty() && !AssetPath.Contains(PathFilter, ESearchCase::IgnoreCase))
			continue;
		if (!Query.IsEmpty() && !AssetName.Contains(Query, ESearchCase::IgnoreCase) && !AssetPath.Contains(Query, ESearchCase::IgnoreCase))
			continue;

		Output += FString::Printf(TEXT("  %s [%s] â€” %s\n"), *AssetName, *AssetClass, *AssetPath);
		MatchCount++;
	}

	if (MatchCount == 0)
	{
		Output += TEXT("  (no matching assets found)\n");
	}
	else
	{
		Output += FString::Printf(TEXT("\nFound %d matches"), MatchCount);
		if (MatchCount >= MaxResults)
		{
			Output += FString::Printf(TEXT(" (capped at %d, use filters to narrow)"), MaxResults);
		}
		Output += TEXT("\n");
	}

	Result.bSuccess = true;
	Result.ResultMessage = Output;
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteListDirectory(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString RelativePath;
	TSharedPtr<FJsonObject> ParamsPtr = Params;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("directory"), RelativePath, Result.Errors, false);
	if (RelativePath.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("relative_path"), RelativePath, Result.Errors, false);
	}

	FString AbsolutePath = FPaths::ProjectDir() / RelativePath;
	FPaths::NormalizeDirectoryName(AbsolutePath);

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*AbsolutePath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Directory does not exist: %s"), *AbsolutePath));
		return Result;
	}

	TArray<FString> Files;
	TArray<FString> Directories;
	FileManager.FindFiles(Files, *(AbsolutePath / TEXT("*")), true, false);
	FileManager.FindFiles(Directories, *(AbsolutePath / TEXT("*")), false, true);

	FString Output = FString::Printf(TEXT("Contents of directory: %s\n\nDirectories:\n"), *RelativePath);
	for (const FString& Dir : Directories)
	{
		Output += FString::Printf(TEXT("  [DIR]  %s\n"), *Dir);
	}
	Output += TEXT("\nFiles:\n");
	for (const FString& File : Files)
	{
		Output += FString::Printf(TEXT("         %s\n"), *File);
	}

	Result.bSuccess = true;
	Result.ResultMessage = Output;
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteReadFileSnippet(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString RelativePath;
	TSharedPtr<FJsonObject> ParamsPtr = Params;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("file_path"), RelativePath, Result.Errors, false);
	if (RelativePath.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("relative_path"), RelativePath, Result.Errors, false);
	}

	if (RelativePath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Parameter 'file_path' or 'relative_path' is required."));
		return Result;
	}

	int32 StartLine = 1;
	int32 EndLine = 100;
	UAgentFrameworkActionUtils::TryGetIntParam(ParamsPtr, TEXT("start_line"), StartLine, Result.Errors, false);
	UAgentFrameworkActionUtils::TryGetIntParam(ParamsPtr, TEXT("end_line"), EndLine, Result.Errors, false);

	FString AbsolutePath = FPaths::ProjectDir() / RelativePath;
	FPaths::NormalizeFilename(AbsolutePath);

	FString FileContent;
	if (!FFileHelper::LoadFileToString(FileContent, *AbsolutePath))
	{
		Result.Errors.Add(FString::Printf(TEXT("Could not read file: %s"), *AbsolutePath));
		return Result;
	}

	TArray<FString> Lines;
	FileContent.ParseIntoArrayLines(Lines, false);

	if (StartLine < 1) StartLine = 1;
	if (EndLine > Lines.Num()) EndLine = Lines.Num();

	FString Output = FString::Printf(TEXT("File: %s (lines %d to %d of %d)\n\n"), *RelativePath, StartLine, EndLine, Lines.Num());
	for (int32 i = StartLine - 1; i < EndLine; ++i)
	{
		Output += FString::Printf(TEXT("%d: %s\n"), i + 1, *Lines[i]);
	}

	Result.bSuccess = true;
	Result.ResultMessage = Output;
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteActivateSkill(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	// 1. Get skill names to add
	TArray<FString> SkillsToAdd;
	TSharedPtr<FJsonObject> ParamsPtr = Params;

	FString SingleSkill;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("skill_name"), SingleSkill, Result.Errors, false);
	if (!SingleSkill.IsEmpty())
	{
		SkillsToAdd.Add(SingleSkill);
	}

	TArray<FString> TempSkillsArray;
	UAgentFrameworkActionUtils::TryGetStringArrayParam(ParamsPtr, TEXT("skills"), TempSkillsArray, Result.Errors, false);
	for (const FString& SkillStr : TempSkillsArray)
	{
		if (!SkillStr.IsEmpty())
		{
			SkillsToAdd.AddUnique(SkillStr);
		}
	}

	if (SkillsToAdd.Num() == 0)
	{
		Result.Errors.Add(TEXT("No skill name or skills array specified."));
		return Result;
	}

	// 2. Read existing skills to merge
	FString ActiveSkillsDir = FPaths::Combine(FPaths::ProjectDir(), TEXT(".agents"));
	FString ActiveSkillsPath = FPaths::Combine(ActiveSkillsDir, TEXT("active_skills.json"));

	TArray<FString> CurrentSkills;
	if (FPaths::FileExists(ActiveSkillsPath))
	{
		FString SkillsContent;
		if (FFileHelper::LoadFileToString(SkillsContent, *ActiveSkillsPath))
		{
			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SkillsContent);
			TArray<TSharedPtr<FJsonValue>> ArrayVal;
			if (FJsonSerializer::Deserialize(Reader, ArrayVal))
			{
				for (const auto& Val : ArrayVal)
				{
					if (Val.IsValid() && Val->Type == EJson::String)
					{
						CurrentSkills.AddUnique(Val->AsString());
					}
				}
			}
		}
	}

	// 3. Merge new skills
	for (const FString& NewSkill : SkillsToAdd)
	{
		CurrentSkills.AddUnique(NewSkill);
	}

	// 4. Ensure .agents directory exists
	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.DirectoryExists(*ActiveSkillsDir))
	{
		FileManager.MakeDirectory(*ActiveSkillsDir, true);
	}

	// 5. Write back to active_skills.json as a JSON array of strings
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	for (const FString& Skill : CurrentSkills)
	{
		JsonArray.Add(MakeShared<FJsonValueString>(Skill));
	}

	FString OutJsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
	FJsonSerializer::Serialize(JsonArray, Writer);

	if (FFileHelper::SaveStringToFile(OutJsonString, *ActiveSkillsPath))
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Successfully activated skills. Current active skills: %s"), *OutJsonString);
	}
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to write to file: %s"), *ActiveSkillsPath));
	}

	return Result;
}



// ============================================================================
// enforce_naming_conventions: Enforce UE standard asset naming conventions
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteEnforceNamingConventions(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	TSharedPtr<FJsonObject> ParamsPtr = Params;

	// 1. Folder Path extraction with dual aliases
	FString FolderPath;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("folder_path"), FolderPath, Result.Errors, false);
	if (FolderPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("directory_path"), FolderPath, Result.Errors, false);
	if (FolderPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("target_folder"), FolderPath, Result.Errors, false);
	if (FolderPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("FolderPath"), FolderPath, Result.Errors, false);

	if (FolderPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Parameter 'folder_path' (or 'directory_path' / 'target_folder' / 'FolderPath') is required."));
		return Result;
	}

	// Normalize folder path to starting with /Game/
	if (!FolderPath.StartsWith(TEXT("/Game")))
	{
		if (FolderPath.StartsWith(TEXT("Content/"))) FolderPath = TEXT("/Game/") + FolderPath.RightChop(8);
		else if (FolderPath.StartsWith(TEXT("Content"))) FolderPath = TEXT("/Game");
		else if (!FolderPath.StartsWith(TEXT("/"))) FolderPath = TEXT("/Game/") + FolderPath;
	}
	FPaths::NormalizeDirectoryName(FolderPath);

	// 2. Dry run flag extraction with dual aliases
	bool bDryRun = false;
	UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("dry_run"), bDryRun, Result.Errors, false);
	if (!ParamsPtr->HasField(TEXT("dry_run")))
	{
		UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("dry_run_mode"), bDryRun, Result.Errors, false);
		if (!ParamsPtr->HasField(TEXT("dry_run_mode")))
		{
			UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("DryRun"), bDryRun, Result.Errors, false);
		}
	}

	// 3. Recursive flag extraction with dual aliases
	bool bRecursive = true;
	if (ParamsPtr->HasField(TEXT("recursive")))
	{
		UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("recursive"), bRecursive, Result.Errors, false);
	}
	else if (ParamsPtr->HasField(TEXT("Recursive")))
	{
		UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("Recursive"), bRecursive, Result.Errors, false);
	}

	// 4. Custom rules extraction with dual aliases
	TMap<FString, FString> CustomRulesMap;
	const TSharedPtr<FJsonObject>* CustomRulesObj = nullptr;
	if (ParamsPtr->HasTypedField<EJson::Object>(TEXT("custom_rules")))
	{
		CustomRulesObj = &ParamsPtr->GetObjectField(TEXT("custom_rules"));
	}
	else if (ParamsPtr->HasTypedField<EJson::Object>(TEXT("CustomRules")))
	{
		CustomRulesObj = &ParamsPtr->GetObjectField(TEXT("CustomRules"));
	}

	if (CustomRulesObj && CustomRulesObj->IsValid())
	{
		for (const auto& Pair : (*CustomRulesObj)->Values)
		{
			if (Pair.Value.IsValid() && Pair.Value->Type == EJson::String)
			{
				CustomRulesMap.Add(FString(Pair.Key), Pair.Value->AsString());
			}
		}
	}

	// Helper lambda for class prefix lookup
	auto GetPrefixForClass = [&CustomRulesMap](const FString& ClassName) -> FString
	{
		if (const FString* CustomPrefix = CustomRulesMap.Find(ClassName))
		{
			return *CustomPrefix;
		}

		static const TMap<FString, FString> PrefixMap = {
			{ TEXT("Blueprint"), TEXT("BP_") },
			{ TEXT("BlueprintGeneratedClass"), TEXT("BP_") },
			{ TEXT("WidgetBlueprint"), TEXT("WBP_") },
			{ TEXT("WidgetBlueprintGeneratedClass"), TEXT("WBP_") },
			{ TEXT("AnimBlueprint"), TEXT("ABP_") },
			{ TEXT("AnimBlueprintGeneratedClass"), TEXT("ABP_") },
			{ TEXT("Material"), TEXT("M_") },
			{ TEXT("MaterialInstanceConstant"), TEXT("MI_") },
			{ TEXT("MaterialInstanceDynamic"), TEXT("MI_") },
			{ TEXT("MaterialInstance"), TEXT("MI_") },
			{ TEXT("MaterialFunction"), TEXT("MF_") },
			{ TEXT("MaterialParameterCollection"), TEXT("MPC_") },
			{ TEXT("Texture2D"), TEXT("T_") },
			{ TEXT("TextureCube"), TEXT("T_") },
			{ TEXT("VolumeTexture"), TEXT("T_") },
			{ TEXT("Texture"), TEXT("T_") },
			{ TEXT("RenderTarget2D"), TEXT("RT_") },
			{ TEXT("StaticMesh"), TEXT("SM_") },
			{ TEXT("SkeletalMesh"), TEXT("SKM_") },
			{ TEXT("PhysicsAsset"), TEXT("PHYS_") },
			{ TEXT("NiagaraSystem"), TEXT("NS_") },
			{ TEXT("NiagaraEmitter"), TEXT("NE_") },
			{ TEXT("ParticleSystem"), TEXT("PS_") },
			{ TEXT("InputAction"), TEXT("IA_") },
			{ TEXT("InputMappingContext"), TEXT("IMC_") },
			{ TEXT("SoundWave"), TEXT("SW_") },
			{ TEXT("SoundCue"), TEXT("SC_") },
			{ TEXT("SoundAttenuation"), TEXT("SA_") },
			{ TEXT("SoundConcurrency"), TEXT("SCN_") },
			{ TEXT("MetaSoundSource"), TEXT("MS_") },
			{ TEXT("DataAsset"), TEXT("DA_") },
			{ TEXT("PrimaryDataAsset"), TEXT("DA_") },
			{ TEXT("DataTable"), TEXT("DT_") },
			{ TEXT("CurveTable"), TEXT("CT_") },
			{ TEXT("StringTable"), TEXT("ST_") },
			{ TEXT("LevelSequence"), TEXT("LS_") },
			{ TEXT("AnimSequence"), TEXT("A_") },
			{ TEXT("AnimMontage"), TEXT("AM_") },
			{ TEXT("BlendSpace"), TEXT("BS_") },
			{ TEXT("BlendSpace1D"), TEXT("BS_") },
			{ TEXT("Skeleton"), TEXT("SK_") },
			{ TEXT("IKRigDefinition"), TEXT("IKR_") },
			{ TEXT("IKRetargeter"), TEXT("IKRT_") },
			{ TEXT("BehaviorTree"), TEXT("BT_") },
			{ TEXT("BlackboardData"), TEXT("BB_") },
			{ TEXT("PCGGraph"), TEXT("PCG_") },
			{ TEXT("PCGGraphInterface"), TEXT("PCG_") },
			{ TEXT("World"), TEXT("L_") },
			{ TEXT("Level"), TEXT("L_") },
			{ TEXT("SubsurfaceProfile"), TEXT("SP_") },
			{ TEXT("PhysicalMaterial"), TEXT("PM_") }
		};

		if (const FString* Found = PrefixMap.Find(ClassName))
		{
			return *Found;
		}
		return FString();
	};

	// Helper lambda to strip legacy / incorrect prefixes
	auto StripLegacyPrefix = [](const FString& InAssetName, const FString& ExpectedPrefix) -> FString
	{
		FString Name = InAssetName;

		if (Name.StartsWith(ExpectedPrefix, ESearchCase::IgnoreCase))
		{
			Name = Name.RightChop(ExpectedPrefix.Len());
		}
		else
		{
			static const TArray<FString> LegacyPrefixes = {
				TEXT("bp_"), TEXT("Bp_"), TEXT("BP_"),
				TEXT("wbp_"), TEXT("Wbp_"), TEXT("WBP_"),
				TEXT("abp_"), TEXT("Abp_"), TEXT("ABP_"),
				TEXT("m_"), TEXT("M_"), TEXT("Mat_"), TEXT("mat_"), TEXT("MAT_"), TEXT("Material_"),
				TEXT("mi_"), TEXT("MI_"), TEXT("Mi_"),
				TEXT("t_"), TEXT("T_"), TEXT("Tex_"), TEXT("tex_"), TEXT("TEX_"), TEXT("Texture_"),
				TEXT("sm_"), TEXT("SM_"), TEXT("Sm_"), TEXT("Mesh_"), TEXT("SMesh_"), TEXT("StaticMesh_"),
				TEXT("skm_"), TEXT("SKM_"), TEXT("Skm_"), TEXT("SKMesh_"), TEXT("SkeletalMesh_"),
				TEXT("ns_"), TEXT("NS_"), TEXT("Ns_"),
				TEXT("ne_"), TEXT("NE_"), TEXT("Ne_"),
				TEXT("ia_"), TEXT("IA_"), TEXT("Ia_"),
				TEXT("imc_"), TEXT("IMC_"), TEXT("Imc_"),
				TEXT("sw_"), TEXT("SW_"), TEXT("Sw_"),
				TEXT("sc_"), TEXT("SC_"), TEXT("Sc_"),
				TEXT("da_"), TEXT("DA_"), TEXT("Da_"),
				TEXT("dt_"), TEXT("DT_"), TEXT("Dt_"),
				TEXT("ls_"), TEXT("LS_"), TEXT("Ls_")
			};

			for (const FString& Legacy : LegacyPrefixes)
			{
				if (Name.StartsWith(Legacy, ESearchCase::IgnoreCase))
				{
					Name = Name.RightChop(Legacy.Len());
					break;
				}
			}
		}

		while (Name.StartsWith(TEXT("_")))
		{
			Name = Name.RightChop(1);
		}

		if (Name.IsEmpty())
		{
			Name = InAssetName;
		}

		return Name;
	};

	// 5. Query Asset Registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPath(FName(*FolderPath), Assets, bRecursive, false);

	TArray<FAssetRenameData> RenameDataArray;
	int32 TotalScanned = Assets.Num();
	int32 CompliantCount = 0;
	int32 RenamedCount = 0;
	TArray<TSharedPtr<FJsonValue>> RenamedDetailsJson;

	for (const FAssetData& Asset : Assets)
	{
		FString AssetName = Asset.AssetName.ToString();
		FString PackagePath = Asset.PackagePath.ToString();
		FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();

		FString ExpectedPrefix = GetPrefixForClass(ClassName);
		if (ExpectedPrefix.IsEmpty())
		{
			// Class is not mapped; skip from enforcement
			continue;
		}

		// Exact case check for compliance
		if (AssetName.StartsWith(ExpectedPrefix, ESearchCase::CaseSensitive))
		{
			CompliantCount++;
			continue;
		}

		FString CleanedName = StripLegacyPrefix(AssetName, ExpectedPrefix);
		FString NewAssetName = ExpectedPrefix + CleanedName;

		if (NewAssetName == AssetName)
		{
			CompliantCount++;
			continue;
		}

		RenamedCount++;

		FString OldObjectPath = Asset.GetObjectPathString();
		FString NewPackageName = PackagePath / NewAssetName;
		FString NewObjectPath = NewPackageName + TEXT(".") + NewAssetName;

		TSharedPtr<FJsonObject> DetailObj = MakeShared<FJsonObject>();
		DetailObj->SetStringField(TEXT("asset_class"), ClassName);
		DetailObj->SetStringField(TEXT("old_name"), AssetName);
		DetailObj->SetStringField(TEXT("new_name"), NewAssetName);
		DetailObj->SetStringField(TEXT("old_path"), OldObjectPath);
		DetailObj->SetStringField(TEXT("new_path"), NewObjectPath);
		RenamedDetailsJson.Add(MakeShared<FJsonValueObject>(DetailObj));

		if (!bDryRun)
		{
			RenameDataArray.Add(FAssetRenameData(Asset.GetAsset(), PackagePath, NewAssetName));
			Result.ModifiedAssets.Add(NewPackageName);
		}
	}

	if (!bDryRun && RenameDataArray.Num() > 0)
	{
#if WITH_EDITOR
		FScopedTransaction Transaction(NSLOCTEXT("AgentFramework", "EnforceNamingConventions", "Enforce Asset Naming Conventions"));
#endif
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.RenameAssets(RenameDataArray);
	}

	FString SummaryMsg = FString::Printf(TEXT("%s naming conventions in %s. Scanned: %d, Compliant: %d, Renamed: %d (DryRun: %s)."),
		bDryRun ? TEXT("Evaluated") : TEXT("Enforced"),
		*FolderPath, TotalScanned, CompliantCount, RenamedCount, bDryRun ? TEXT("true") : TEXT("false"));

	TSharedPtr<FJsonObject> OutputObj = MakeShared<FJsonObject>();
	OutputObj->SetBoolField(TEXT("bSuccess"), true);
	OutputObj->SetStringField(TEXT("folder_path"), FolderPath);
	OutputObj->SetNumberField(TEXT("total_scanned_count"), TotalScanned);
	OutputObj->SetNumberField(TEXT("compliant_count"), CompliantCount);
	OutputObj->SetNumberField(TEXT("renamed_assets_count"), RenamedCount);
	OutputObj->SetBoolField(TEXT("dry_run"), bDryRun);
	OutputObj->SetArrayField(TEXT("renamed_details"), RenamedDetailsJson);
	OutputObj->SetStringField(TEXT("ResultMessage"), SummaryMsg);

	FString ResponseJsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseJsonString);
	FJsonSerializer::Serialize(OutputObj.ToSharedRef(), Writer);

	Result.bSuccess = true;
	Result.ResultMessage = ResponseJsonString;
	return Result;
}


// ============================================================================
// organize_assets_by_type: Reorganize mixed assets into type-specific subfolders
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteOrganizeAssetsByType(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	TSharedPtr<FJsonObject> ParamsPtr = Params;

	// 1. Target folder path extraction with dual aliases
	FString FolderPath;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("folder_path"), FolderPath, Result.Errors, false);
	if (FolderPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("directory_path"), FolderPath, Result.Errors, false);
	if (FolderPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("source_path"), FolderPath, Result.Errors, false);
	if (FolderPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("FolderPath"), FolderPath, Result.Errors, false);

	if (FolderPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Parameter 'folder_path' (or 'directory_path' / 'source_path' / 'FolderPath') is required."));
		return Result;
	}

	// Normalize path
	if (!FolderPath.StartsWith(TEXT("/Game")))
	{
		if (FolderPath.StartsWith(TEXT("Content/"))) FolderPath = TEXT("/Game/") + FolderPath.RightChop(8);
		else if (FolderPath.StartsWith(TEXT("Content"))) FolderPath = TEXT("/Game");
		else if (!FolderPath.StartsWith(TEXT("/"))) FolderPath = TEXT("/Game/") + FolderPath;
	}
	FPaths::NormalizeDirectoryName(FolderPath);

	// 2. Dry run flag extraction with dual aliases
	bool bDryRun = false;
	UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("dry_run"), bDryRun, Result.Errors, false);
	if (!ParamsPtr->HasField(TEXT("dry_run")))
	{
		UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("dry_run_mode"), bDryRun, Result.Errors, false);
		if (!ParamsPtr->HasField(TEXT("dry_run_mode")))
		{
			UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("DryRun"), bDryRun, Result.Errors, false);
		}
	}

	// 3. Create subfolders flag extraction with dual aliases
	bool bCreateSubfolders = true;
	if (ParamsPtr->HasField(TEXT("create_subfolders")))
	{
		UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("create_subfolders"), bCreateSubfolders, Result.Errors, false);
	}
	else if (ParamsPtr->HasField(TEXT("CreateSubfolders")))
	{
		UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("CreateSubfolders"), bCreateSubfolders, Result.Errors, false);
	}

	// 4. Recursive flag extraction with dual aliases
	bool bRecursive = true;
	if (ParamsPtr->HasField(TEXT("recursive")))
	{
		UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("recursive"), bRecursive, Result.Errors, false);
	}
	else if (ParamsPtr->HasField(TEXT("Recursive")))
	{
		UAgentFrameworkActionUtils::TryGetBoolParam(ParamsPtr, TEXT("Recursive"), bRecursive, Result.Errors, false);
	}

	// Helper lambda for subfolder mapping
	auto GetSubfolderForClass = [](const FString& ClassName) -> FString
	{
		static const TMap<FString, FString> CategoryMap = {
			// Blueprints
			{ TEXT("Blueprint"), TEXT("Blueprints") },
			{ TEXT("BlueprintGeneratedClass"), TEXT("Blueprints") },

			// Materials
			{ TEXT("Material"), TEXT("Materials") },
			{ TEXT("MaterialInstanceConstant"), TEXT("Materials") },
			{ TEXT("MaterialInstanceDynamic"), TEXT("Materials") },
			{ TEXT("MaterialInstance"), TEXT("Materials") },
			{ TEXT("MaterialFunction"), TEXT("Materials") },
			{ TEXT("MaterialParameterCollection"), TEXT("Materials") },
			{ TEXT("SubsurfaceProfile"), TEXT("Materials") },
			{ TEXT("PhysicalMaterial"), TEXT("Materials") },

			// Textures
			{ TEXT("Texture2D"), TEXT("Textures") },
			{ TEXT("TextureCube"), TEXT("Textures") },
			{ TEXT("VolumeTexture"), TEXT("Textures") },
			{ TEXT("RenderTarget2D"), TEXT("Textures") },
			{ TEXT("Texture"), TEXT("Textures") },

			// UI
			{ TEXT("WidgetBlueprint"), TEXT("UI") },
			{ TEXT("WidgetBlueprintGeneratedClass"), TEXT("UI") },
			{ TEXT("SlateWidgetStyleAsset"), TEXT("UI") },
			{ TEXT("Font"), TEXT("UI") },
			{ TEXT("FontFace"), TEXT("UI") },

			// Effects
			{ TEXT("NiagaraSystem"), TEXT("Effects") },
			{ TEXT("NiagaraEmitter"), TEXT("Effects") },
			{ TEXT("ParticleSystem"), TEXT("Effects") },

			// Input
			{ TEXT("InputAction"), TEXT("Input") },
			{ TEXT("InputMappingContext"), TEXT("Input") },

			// Audio
			{ TEXT("SoundWave"), TEXT("Audio") },
			{ TEXT("SoundCue"), TEXT("Audio") },
			{ TEXT("SoundAttenuation"), TEXT("Audio") },
			{ TEXT("SoundConcurrency"), TEXT("Audio") },
			{ TEXT("MetaSoundSource"), TEXT("Audio") },

			// Meshes
			{ TEXT("StaticMesh"), TEXT("Meshes") },
			{ TEXT("SkeletalMesh"), TEXT("Meshes") },
			{ TEXT("PhysicsAsset"), TEXT("Meshes") },

			// Animation
			{ TEXT("AnimSequence"), TEXT("Animation") },
			{ TEXT("AnimMontage"), TEXT("Animation") },
			{ TEXT("AnimBlueprint"), TEXT("Animation") },
			{ TEXT("AnimBlueprintGeneratedClass"), TEXT("Animation") },
			{ TEXT("BlendSpace"), TEXT("Animation") },
			{ TEXT("BlendSpace1D"), TEXT("Animation") },
			{ TEXT("Skeleton"), TEXT("Animation") },
			{ TEXT("IKRigDefinition"), TEXT("Animation") },
			{ TEXT("IKRetargeter"), TEXT("Animation") },

			// Data
			{ TEXT("DataAsset"), TEXT("Data") },
			{ TEXT("PrimaryDataAsset"), TEXT("Data") },
			{ TEXT("DataTable"), TEXT("Data") },
			{ TEXT("CurveTable"), TEXT("Data") },
			{ TEXT("StringTable"), TEXT("Data") },

			// Sequencer
			{ TEXT("LevelSequence"), TEXT("Sequencer") },

			// PCG
			{ TEXT("PCGGraph"), TEXT("PCG") },
			{ TEXT("PCGGraphInterface"), TEXT("PCG") },

			// Maps
			{ TEXT("World"), TEXT("Maps") },
			{ TEXT("Level"), TEXT("Maps") }
		};

		if (const FString* Found = CategoryMap.Find(ClassName))
		{
			return *Found;
		}
		return FString();
	};

	// 5. Query Asset Registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPath(FName(*FolderPath), Assets, bRecursive, false);

	TArray<FAssetRenameData> RenameDataArray;
	int32 TotalScanned = Assets.Num();
	int32 MovedCount = 0;
	TMap<FString, int32> CategoryCounts;
	TArray<TSharedPtr<FJsonValue>> MovedDetailsJson;

	for (const FAssetData& Asset : Assets)
	{
		FString AssetName = Asset.AssetName.ToString();
		FString CurrentPackagePath = Asset.PackagePath.ToString();
		FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();

		FString CategorySubfolder = GetSubfolderForClass(ClassName);
		if (CategorySubfolder.IsEmpty())
		{
			// Skip unmapped asset class
			continue;
		}

		FString TargetPackagePath = FolderPath / CategorySubfolder;
		FPaths::NormalizeDirectoryName(TargetPackagePath);
		FPaths::NormalizeDirectoryName(CurrentPackagePath);

		// Check if asset is already inside the target subfolder (or sub-directory of target subfolder)
		if (CurrentPackagePath.Equals(TargetPackagePath, ESearchCase::IgnoreCase) ||
			CurrentPackagePath.StartsWith(TargetPackagePath + TEXT("/"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		MovedCount++;
		CategoryCounts.FindOrAdd(CategorySubfolder)++;

		FString OldObjectPath = Asset.GetObjectPathString();
		FString NewPackageName = TargetPackagePath / AssetName;
		FString NewObjectPath = NewPackageName + TEXT(".") + AssetName;

		TSharedPtr<FJsonObject> DetailObj = MakeShared<FJsonObject>();
		DetailObj->SetStringField(TEXT("asset_name"), AssetName);
		DetailObj->SetStringField(TEXT("asset_class"), ClassName);
		DetailObj->SetStringField(TEXT("category"), CategorySubfolder);
		DetailObj->SetStringField(TEXT("old_path"), OldObjectPath);
		DetailObj->SetStringField(TEXT("new_path"), NewObjectPath);
		MovedDetailsJson.Add(MakeShared<FJsonValueObject>(DetailObj));

		if (!bDryRun)
		{
			RenameDataArray.Add(FAssetRenameData(Asset.GetAsset(), TargetPackagePath, AssetName));
			Result.ModifiedAssets.Add(NewPackageName);
		}
	}

	if (!bDryRun && RenameDataArray.Num() > 0)
	{
#if WITH_EDITOR
		FScopedTransaction Transaction(NSLOCTEXT("AgentFramework", "OrganizeAssetsByType", "Organize Assets By Type"));
#endif
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.RenameAssets(RenameDataArray);
	}

	TSharedPtr<FJsonObject> CategoryCountsObj = MakeShared<FJsonObject>();
	for (const auto& Pair : CategoryCounts)
	{
		CategoryCountsObj->SetNumberField(FString(Pair.Key), Pair.Value);
	}

	FString SummaryMsg = FString::Printf(TEXT("%s %d assets into type subfolders under %s (DryRun: %s)."),
		bDryRun ? TEXT("Evaluated moving") : TEXT("Organized"),
		MovedCount, *FolderPath, bDryRun ? TEXT("true") : TEXT("false"));

	TSharedPtr<FJsonObject> OutputObj = MakeShared<FJsonObject>();
	OutputObj->SetBoolField(TEXT("bSuccess"), true);
	OutputObj->SetStringField(TEXT("folder_path"), FolderPath);
	OutputObj->SetNumberField(TEXT("total_scanned_count"), TotalScanned);
	OutputObj->SetNumberField(TEXT("moved_assets_count"), MovedCount);
	OutputObj->SetBoolField(TEXT("dry_run"), bDryRun);
	OutputObj->SetObjectField(TEXT("category_counts"), CategoryCountsObj);
	OutputObj->SetArrayField(TEXT("moved_details"), MovedDetailsJson);
	OutputObj->SetStringField(TEXT("ResultMessage"), SummaryMsg);

	FString ResponseJsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseJsonString);
	FJsonSerializer::Serialize(OutputObj.ToSharedRef(), Writer);

	Result.bSuccess = true;
	Result.ResultMessage = ResponseJsonString;
	return Result;
}

// ============================================================================
// consolidate_asset_references: Consolidate references from source to target asset
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteConsolidateAssetReferences(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	TSharedPtr<FJsonObject> ParamsPtr = Params;

	FString SourcePath;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("source_asset_path"), SourcePath, Result.Errors, false);
	if (SourcePath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("SourceAssetPath"), SourcePath, Result.Errors, false);
	if (SourcePath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("source_asset"), SourcePath, Result.Errors, false);
	if (SourcePath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("SourceAsset"), SourcePath, Result.Errors, false);

	FString TargetPath;
	UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("target_asset_path"), TargetPath, Result.Errors, false);
	if (TargetPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("TargetAssetPath"), TargetPath, Result.Errors, false);
	if (TargetPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("target_asset"), TargetPath, Result.Errors, false);
	if (TargetPath.IsEmpty()) UAgentFrameworkActionUtils::TryGetStringParam(ParamsPtr, TEXT("TargetAsset"), TargetPath, Result.Errors, false);

	if (SourcePath.IsEmpty() || TargetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Parameters 'source_asset_path' and 'target_asset_path' are required."));
		return Result;
	}

	if (SourcePath.Equals(TargetPath, ESearchCase::IgnoreCase))
	{
		Result.Errors.Add(TEXT("Source and target asset paths cannot be identical."));
		return Result;
	}

	UObject* SourceAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *SourcePath);
	if (!SourceAsset && !SourcePath.Contains(TEXT(".")))
	{
		FString AssetName = FPaths::GetBaseFilename(SourcePath);
		FString FullPath = SourcePath + TEXT(".") + AssetName;
		SourceAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *FullPath);
	}

	UObject* TargetAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *TargetPath);
	if (!TargetAsset && !TargetPath.Contains(TEXT(".")))
	{
		FString AssetName = FPaths::GetBaseFilename(TargetPath);
		FString FullPath = TargetPath + TEXT(".") + AssetName;
		TargetAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *FullPath);
	}

	if (!SourceAsset)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to load source asset at path: %s"), *SourcePath));
		return Result;
	}

	if (!TargetAsset)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to load target asset at path: %s"), *TargetPath));
		return Result;
	}

#if WITH_EDITOR
	TArray<UObject*> AssetsToConsolidate;
	AssetsToConsolidate.Add(SourceAsset);

	bool bConsolidated = UEditorAssetLibrary::ConsolidateAssets(TargetAsset, AssetsToConsolidate);
	if (bConsolidated)
	{
		Result.bSuccess = true;
		Result.ModifiedAssets.Add(TargetPath);
		FString SummaryMsg = FString::Printf(TEXT("Successfully consolidated references from '%s' to '%s'."), *SourcePath, *TargetPath);

		TSharedPtr<FJsonObject> OutputObj = MakeShared<FJsonObject>();
		OutputObj->SetBoolField(TEXT("bSuccess"), true);
		OutputObj->SetStringField(TEXT("source_asset_path"), SourcePath);
		OutputObj->SetStringField(TEXT("target_asset_path"), TargetPath);
		OutputObj->SetStringField(TEXT("ResultMessage"), SummaryMsg);

		FString ResponseJsonString;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseJsonString);
		FJsonSerializer::Serialize(OutputObj.ToSharedRef(), Writer);

		Result.ResultMessage = ResponseJsonString;
	}
	else
	{
		Result.bSuccess = false;
		Result.Errors.Add(FString::Printf(TEXT("Failed to consolidate asset references from '%s' to '%s'."), *SourcePath, *TargetPath));
	}
#else
	Result.bSuccess = false;
	Result.Errors.Add(TEXT("ConsolidateAssets is only available in editor builds."));
#endif

	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteDeleteAsset(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	UEditorAssetSubsystem* EditorAssetSubsystem = GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	if (EditorAssetSubsystem && EditorAssetSubsystem->DoesAssetExist(AssetPath))
	{
		if (EditorAssetSubsystem->DeleteAsset(AssetPath))
		{
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Successfully deleted asset '%s'."), *AssetPath);
			return Result;
		}
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (IsValid(Asset))
	{
		TArray<UObject*> ObjectsToDelete = { Asset };
		int32 DeletedCount = ObjectTools::DeleteObjects(ObjectsToDelete, /*bShowConfirmation=*/false);
		if (DeletedCount > 0)
		{
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Successfully deleted asset '%s' via ObjectTools."), *AssetPath);
			return Result;
		}
	}

	// Try deleting package directly from disk if file exists
	FString PackageFilename;
	FString PackagePath = FPackageName::ObjectPathToPackageName(AssetPath);
	if (FPackageName::TryConvertLongPackageNameToFilename(PackagePath, PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		if (FPaths::FileExists(PackageFilename))
		{
			if (IFileManager::Get().Delete(*PackageFilename))
			{
				FAssetRegistryModule::AssetDeleted(Asset);
				Result.bSuccess = true;
				Result.ResultMessage = FString::Printf(TEXT("Deleted package file '%s' from disk."), *PackageFilename);
				return Result;
			}
		}
	}

	Result.bSuccess = false;
	Result.Errors.Add(FString::Printf(TEXT("Failed to delete asset at '%s' or asset does not exist."), *AssetPath));
#else
	Result.bSuccess = false;
	Result.Errors.Add(TEXT("delete_asset is only available in editor builds."));
#endif
	return Result;
}

#if WITH_EDITOR
static UClass* ResolveAssetClass(const FString& InClassName)
{
	if (InClassName.IsEmpty())
	{
		return nullptr;
	}

	FString ClassName = InClassName.TrimStartAndEnd();

	// 1. Try direct StaticLoadClass or LoadObject (full paths or /Script/... paths or Blueprint classes)
	UClass* FoundClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassName);
	if (IsValid(FoundClass))
	{
		return FoundClass;
	}

	FoundClass = LoadObject<UClass>(nullptr, *ClassName, nullptr, LOAD_NoWarn, nullptr);
	if (IsValid(FoundClass))
	{
		return FoundClass;
	}

	// 2. Strip 'U' or 'A' prefix if present for FName search
	FString StrippedName = ClassName;
	if ((ClassName.StartsWith(TEXT("U")) || ClassName.StartsWith(TEXT("A"))) && ClassName.Len() > 1 && FChar::IsUpper(ClassName[1]))
	{
		StrippedName = ClassName.RightChop(1);
	}

	// 3. Try FindFirstObject with both original and stripped names
	FoundClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	if (IsValid(FoundClass))
	{
		return FoundClass;
	}

	FoundClass = FindFirstObject<UClass>(*StrippedName, EFindFirstObjectOptions::NativeFirst);
	if (IsValid(FoundClass))
	{
		return FoundClass;
	}

	// 4. Try common UE module package paths
	static const TArray<FString> CommonPackagePrefixes = {
		TEXT("/Script/Engine."),
		TEXT("/Script/CoreUObject."),
		TEXT("/Script/Niagara."),
		TEXT("/Script/EnhancedInput."),
		TEXT("/Script/PhysicsCore."),
		TEXT("/Script/AudioPlatformConfiguration."),
		TEXT("/Script/MetasoundEngine."),
		TEXT("/Script/PCG."),
		TEXT("/Script/StateTreeModule."),
		TEXT("/Script/GameplayAbilities."),
		TEXT("/Script/GameplayTags."),
		TEXT("/Script/AIModule."),
		TEXT("/Script/UMG."),
		TEXT("/Script/CinematicCamera."),
		TEXT("/Script/LevelSequence."),
		TEXT("/Script/MovieScene.")
	};

	for (const FString& Prefix : CommonPackagePrefixes)
	{
		FoundClass = LoadObject<UClass>(nullptr, *(Prefix + StrippedName), nullptr, LOAD_NoWarn, nullptr);
		if (IsValid(FoundClass))
		{
			return FoundClass;
		}
		FoundClass = LoadObject<UClass>(nullptr, *(Prefix + ClassName), nullptr, LOAD_NoWarn, nullptr);
		if (IsValid(FoundClass))
		{
			return FoundClass;
		}
	}

	// 5. Search over loaded UClasses
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Candidate = *It;
		if (IsValid(Candidate))
		{
			if (Candidate->GetName().Equals(StrippedName, ESearchCase::IgnoreCase) ||
				Candidate->GetName().Equals(ClassName, ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}
	}

	return nullptr;
}

static UFactory* ResolveFactoryForClass(UClass* TargetClass, const FString& InFactoryClassName)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	// 1. If explicit factory class name provided
	if (!InFactoryClassName.IsEmpty())
	{
		UClass* FactoryClass = ResolveAssetClass(InFactoryClassName);
		if (IsValid(FactoryClass) && FactoryClass->IsChildOf(UFactory::StaticClass()))
		{
			UFactory* ExplicitFactory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
			if (IsValid(ExplicitFactory))
			{
				return ExplicitFactory;
			}
		}
	}

	// 2. Query AssetTools registered new asset factories
	const TArray<UFactory*>& RegisteredFactories = AssetTools.GetNewAssetFactories();
	UFactory* BestFactory = nullptr;

	for (UFactory* CandidateFactory : RegisteredFactories)
	{
		if (!IsValid(CandidateFactory))
		{
			continue;
		}

		UClass* SupportedClass = CandidateFactory->GetSupportedClass();
		if (IsValid(SupportedClass))
		{
			if (SupportedClass == TargetClass)
			{
				BestFactory = NewObject<UFactory>(GetTransientPackage(), CandidateFactory->GetClass());
				break;
			}
			else if (TargetClass->IsChildOf(SupportedClass) && !BestFactory)
			{
				BestFactory = NewObject<UFactory>(GetTransientPackage(), CandidateFactory->GetClass());
			}
		}
	}

	// 3. If no registered factory found, try convention-based lookup: <ClassName>FactoryNew or <ClassName>Factory
	if (!BestFactory && IsValid(TargetClass))
	{
		FString BaseName = TargetClass->GetName();
		TArray<FString> PotentialNames = {
			BaseName + TEXT("FactoryNew"),
			BaseName + TEXT("Factory"),
			TEXT("U") + BaseName + TEXT("FactoryNew"),
			TEXT("U") + BaseName + TEXT("Factory")
		};

		for (const FString& FactoryName : PotentialNames)
		{
			UClass* FactoryClass = ResolveAssetClass(FactoryName);
			if (IsValid(FactoryClass) && FactoryClass->IsChildOf(UFactory::StaticClass()))
			{
				BestFactory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
				break;
			}
		}
	}

	// 4. Configure factory properties for known base types if needed
	if (IsValid(BestFactory) && IsValid(TargetClass))
	{
		if (UDataAssetFactory* DataAssetFactory = Cast<UDataAssetFactory>(BestFactory))
		{
			DataAssetFactory->DataAssetClass = TargetClass;
		}
		else
		{
			FObjectProperty* DataAssetClassProp = CastField<FObjectProperty>(BestFactory->GetClass()->FindPropertyByName(TEXT("DataAssetClass")));
			if (DataAssetClassProp)
			{
				DataAssetClassProp->SetPropertyValue_InContainer(BestFactory, TargetClass);
			}
		}
	}

	return BestFactory;
}
#endif

FAgentFrameworkActionResult FAgentFrameworkContextActions::ExecuteCreateAsset(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, false))
	{
		FString PackagePath, AssetName;
		if (UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("package_path"), PackagePath, Result.Errors, false) &&
			UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_name"), AssetName, Result.Errors, false))
		{
			AssetPath = PackagePath.EndsWith(TEXT("/")) ? (PackagePath + AssetName) : (PackagePath + TEXT("/") + AssetName);
		}
	}

	if (AssetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: asset_path (or package_path and asset_name)."));
		return Result;
	}

	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/Data/DA_Item."),
			*AssetPath));
		return Result;
	}

	// Check if asset already exists
	UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName));
	if (IsValid(ExistingAsset))
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Asset '%s' already exists of class '%s'."), *AssetName, *ExistingAsset->GetClass()->GetName());
		Result.ModifiedAssets.Add(AssetPath);
		return Result;
	}

	FString ClassName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_class"), ClassName, Result.Errors, false))
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("class_name"), ClassName, Result.Errors, false))
		{
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("AssetClass"), ClassName, Result.Errors, false))
			{
				UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("ClassName"), ClassName, Result.Errors, false);
			}
		}
	}

	if (ClassName.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: asset_class (or class_name)."));
		return Result;
	}

	UClass* TargetClass = ResolveAssetClass(ClassName);
	if (!IsValid(TargetClass))
	{
		Result.Errors.Add(FString::Printf(
			TEXT("Class '%s' could not be found. Ensure the class name is correct or module/plugin is loaded."),
			*ClassName));
		return Result;
	}

	FString FactoryClassName;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("factory_class"), FactoryClassName, Result.Errors, false);

	UFactory* Factory = ResolveFactoryForClass(TargetClass, FactoryClassName);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UObject* NewAsset = nullptr;
	if (IsValid(Factory))
	{
		NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, TargetClass, Factory);
	}
	else
	{
		NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, TargetClass, nullptr);
	}

	if (!IsValid(NewAsset))
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (IsValid(Package))
		{
			NewAsset = NewObject<UObject>(Package, TargetClass, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
			if (IsValid(NewAsset))
			{
				FAssetRegistryModule::AssetCreated(NewAsset);
			}
		}
	}

	if (!IsValid(NewAsset))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create asset '%s' of class '%s'."), *AssetPath, *TargetClass->GetName()));
		return Result;
	}

	NewAsset->Modify();
	UPackage* Package = NewAsset->GetOutermost();
	if (IsValid(Package))
	{
		Package->MarkPackageDirty();

		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, NewAsset, *PackageFilename, SaveArgs);
		}
	}

	FAssetRegistryModule::AssetCreated(NewAsset);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Successfully created asset '%s' of class '%s'."), *AssetPath, *TargetClass->GetName());
	Result.ModifiedAssets.Add(AssetPath);
#else
	Result.Errors.Add(TEXT("Asset creation is only supported in the Editor."));
#endif
	return Result;
}




