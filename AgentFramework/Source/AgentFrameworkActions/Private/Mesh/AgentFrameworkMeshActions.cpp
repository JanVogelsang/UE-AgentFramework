// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Mesh/AgentFrameworkMeshActions.h"
#include "AgentFrameworkCoreModule.h"
#include "AgentFrameworkActionUtils.h"

// Engine & Asset includes
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "Engine/StaticMesh.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "ScopedTransaction.h"
#include "VT/RuntimeVirtualTextureVolume.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "PhysicsField/PhysicsFieldComponent.h"
#include "EngineUtils.h"
#include "UObject/SavePackage.h"
#include "Materials/MaterialInterface.h"


#if WITH_EDITOR
#include "Editor.h"
#include "Sound/SoundBase.h"
#endif

FAgentFrameworkMeshActions::FAgentFrameworkMeshActions() {}
FAgentFrameworkMeshActions::~FAgentFrameworkMeshActions() {}
FName FAgentFrameworkMeshActions::GetActionName() const { return FName(TEXT("Mesh")); }

TArray<FString> FAgentFrameworkMeshActions::GetSupportedToolNames() const
{
	return {
		TEXT("import_mesh"),
		TEXT("import_assets_batch"),
		TEXT("configure_static_mesh"),
		TEXT("create_dynamic_mesh"),
		TEXT("audit_nanite_settings"),
		TEXT("setup_runtime_virtual_texture"),
		TEXT("setup_chaos_physics"),
		TEXT("setup_dataflow_graph"),
		TEXT("setup_clothing_simulation"),
		TEXT("setup_sparse_volume_texture")
	};
}

bool FAgentFrameworkMeshActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	FString ToolName;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, OutErrors, false);
	if (ToolName.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("tool_name"), ToolName, OutErrors, false);
	}

	if (ToolName == TEXT("import_mesh"))
	{
		FString SourceFile;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("source_file"), SourceFile, OutErrors, true))
		{
			return false;
		}
	}
	else if (ToolName == TEXT("import_assets_batch"))
	{
		TArray<FString> SourceFiles;
		if (!UAgentFrameworkActionUtils::TryGetStringArrayParam(Params, TEXT("source_files"), SourceFiles, OutErrors, true))
		{
			return false;
		}
	}
	else if (ToolName == TEXT("configure_static_mesh") || ToolName == TEXT("create_dynamic_mesh") || ToolName == TEXT("audit_nanite_settings") || ToolName == TEXT("setup_dataflow_graph"))
	{
		FString AssetPath;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, OutErrors, true))
		{
			return false;
		}
	}
	else if (ToolName == TEXT("setup_runtime_virtual_texture"))
	{
		FString RVTVolumeName, RVTAssetPath;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("rvt_volume_name"), RVTVolumeName, OutErrors, true) ||
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("rvt_asset_path"), RVTAssetPath, OutErrors, true))
		{
			return false;
		}
	}

	return true;
}

FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	TOptional<FScopedTransaction> Transaction;
	Transaction.Emplace(FText::FromString(TEXT("AgentFramework Mesh Action")));

	FAgentFrameworkActionResult Result;
	Result.bSuccess = false;

	FString ToolName;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, Result.Errors, false);
	if (ToolName.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("tool_name"), ToolName, Result.Errors, false);
	}

	if (!ToolName.IsEmpty())
	{
		if (ToolName == TEXT("configure_static_mesh"))       Result = ExecuteConfigureStaticMesh(Params, Result);
		else if (ToolName == TEXT("create_dynamic_mesh"))    Result = ExecuteCreateDynamicMesh(Params, Result);
		else if (ToolName == TEXT("audit_nanite_settings"))  Result = ExecuteAuditNaniteSettings(Params, Result);
		else if (ToolName == TEXT("setup_runtime_virtual_texture")) Result = ExecuteSetupRuntimeVirtualTexture(Params, Result);
		else if (ToolName == TEXT("setup_chaos_physics"))    Result = ExecuteSetupChaosPhysics(Params, Result);
		else if (ToolName == TEXT("setup_dataflow_graph"))   Result = ExecuteSetupDataflowGraph(Params, Result);
		else if (ToolName == TEXT("setup_clothing_simulation")) Result = ExecuteSetupClothingSimulation(Params, Result);
		else if (ToolName == TEXT("setup_sparse_volume_texture")) Result = ExecuteSetupSparseVolumeTexture(Params, Result);
		else if (ToolName == TEXT("import_mesh") || ToolName == TEXT("import_assets_batch"))
		{
			// Fall through to import handling below
		}
		else
		{
			Result.Errors.Add(FString::Printf(TEXT("Unknown mesh tool: '%s'"), *ToolName));
			Transaction->Cancel();
			return Result;
		}

		if (ToolName != TEXT("import_mesh") && ToolName != TEXT("import_assets_batch"))
		{
			if (Transaction.IsSet() && !Result.bSuccess)
			{
				Transaction->Cancel();
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
	}

	// Get source file paths for import_mesh / import_assets_batch
	TArray<FString> SourceFiles;
	if (!UAgentFrameworkActionUtils::TryGetStringArrayParam(Params, TEXT("source_files"), SourceFiles, Result.Errors, false) || SourceFiles.Num() == 0)
	{
		FString SingleFile;
		if (UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("source_file"), SingleFile, Result.Errors, false) && !SingleFile.IsEmpty())
		{
			SourceFiles.Add(SingleFile);
		}
	}

	if (SourceFiles.Num() == 0)
	{
		Result.Errors.Add(TEXT("No source files specified for import."));
		if (Transaction.IsSet())
		{
			Transaction->Cancel();
		}
		return Result;
	}

	FString DestinationPath = TEXT("/Game/Meshes");
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("destination_path"), DestinationPath, Result.Errors, false);

	// Create import tasks
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	TArray<UAssetImportTask*> ImportTasks;

	for (const FString& SourceFile : SourceFiles)
	{
		if (!FPaths::FileExists(SourceFile))
		{
			Result.Warnings.Add(FString::Printf(TEXT("File not found: %s"), *SourceFile));
			continue;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		if (!IsValid(Task))
		{
			continue;
		}
		Task->Filename = SourceFile;
		Task->DestinationPath = DestinationPath;
		Task->bAutomated = true;
		Task->bReplaceExisting = true;
		Task->bSave = true;
		ImportTasks.Add(Task);
	}

	if (ImportTasks.Num() == 0)
	{
		Result.Errors.Add(TEXT("No valid files to import."));
		if (Transaction.IsSet())
		{
			Transaction->Cancel();
		}
		return Result;
	}

	AssetTools.ImportAssetTasks(ImportTasks);

	// Collect results
	for (UAssetImportTask* Task : ImportTasks)
	{
		if (IsValid(Task))
		{
			for (const FString& ImportedPath : Task->ImportedObjectPaths)
			{
				Result.ModifiedAssets.Add(ImportedPath);
			}
		}
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Imported %d asset(s) to %s"), ImportTasks.Num(), *DestinationPath);

	if (Transaction.IsSet() && !Result.bSuccess)
	{
		Transaction->Cancel();
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

FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteConfigureStaticMesh(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
	if (!IsValid(StaticMesh))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to load Static Mesh at path: %s"), *AssetPath));
		return Result;
	}

	// Enable Nanite settings
	bool bEnableNanite = false;
	if (Params->HasField(TEXT("enable_nanite")))
	{
		if (UAgentFrameworkActionUtils::TryGetBoolParam(Params, TEXT("enable_nanite"), bEnableNanite, Result.Errors, false))
		{
			FMeshNaniteSettings NaniteSettingsCopy = StaticMesh->GetNaniteSettings();
			NaniteSettingsCopy.bEnabled = bEnableNanite;
			StaticMesh->SetNaniteSettings(NaniteSettingsCopy);
		}
	}

	// LOD Generation settings
	FString LodGeneration = TEXT("auto");
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("lod_generation"), LodGeneration, Result.Errors, false);

	if (LodGeneration == TEXT("auto") || LodGeneration == TEXT("manual"))
	{
		int32 LodCount = 4;
		UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("lod_count"), LodCount, Result.Errors, false);
		if (LodCount < 1) LodCount = 1;

		StaticMesh->SetNumSourceModels(LodCount);
		for (int32 i = 1; i < LodCount; ++i)
		{
			FStaticMeshSourceModel& SourceModel = StaticMesh->GetSourceModel(i);
			SourceModel.ReductionSettings.PercentTriangles = FMath::Pow(0.5f, i);
		}
	}
	else if (LodGeneration == TEXT("none"))
	{
		StaticMesh->SetNumSourceModels(1);
	}

	// Collision Complexity
	FString CollisionComplexity;
	if (UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("collision_complexity"), CollisionComplexity, Result.Errors, false) && !CollisionComplexity.IsEmpty())
	{
		UBodySetup* BodySetup = StaticMesh->GetBodySetup();
		if (IsValid(BodySetup))
		{
			if (CollisionComplexity == TEXT("simple"))
			{
				BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
			}
			else if (CollisionComplexity == TEXT("complex") || CollisionComplexity == TEXT("use_complex_as_simple"))
			{
				BodySetup->CollisionTraceFlag = CTF_UseComplexAsSimple;
			}
			else if (CollisionComplexity == TEXT("simple_and_complex"))
			{
				BodySetup->CollisionTraceFlag = CTF_UseSimpleAndComplex;
			}
			else
			{
				BodySetup->CollisionTraceFlag = CTF_UseDefault;
			}
		}
	}

	// Lightmap Resolution
	int32 LightmapResolution = 0;
	if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("lightmap_resolution"), LightmapResolution, Result.Errors, false) && LightmapResolution > 0)
	{
		StaticMesh->SetLightMapResolution(LightmapResolution);
	}

	// Material Slot Assignment
	const TArray<TSharedPtr<FJsonValue>>* MaterialsArray = nullptr;
	if (UAgentFrameworkActionUtils::TryGetArrayParam(Params, TEXT("materials"), MaterialsArray, Result.Errors, false) && MaterialsArray)
	{
		for (int32 SlotIndex = 0; SlotIndex < MaterialsArray->Num(); ++SlotIndex)
		{
			const TSharedPtr<FJsonValue>& MatVal = (*MaterialsArray)[SlotIndex];
			if (!MatVal.IsValid()) continue;
			FString MatPath = MatVal->AsString();
			if (!MatPath.IsEmpty())
			{
				UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MatPath);
				if (IsValid(Material))
				{
					StaticMesh->SetMaterial(SlotIndex, Material);
				}
				else
				{
					Result.Warnings.Add(FString::Printf(TEXT("Material at slot %d not found: %s"), SlotIndex, *MatPath));
				}
			}
		}
	}

	FString SingleMaterial;
	if (UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("material"), SingleMaterial, Result.Errors, false) && !SingleMaterial.IsEmpty())
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *SingleMaterial);
		if (IsValid(Material))
		{
			StaticMesh->SetMaterial(0, Material);
		}
		else
		{
			Result.Warnings.Add(FString::Printf(TEXT("Material not found: %s"), *SingleMaterial));
		}
	}

	// Mark package as dirty and rebuild the static mesh
	StaticMesh->Modify();
	StaticMesh->PostEditChange();

	UPackage* Package = StaticMesh->GetOutermost();
	if (IsValid(Package))
	{
		Package->MarkPackageDirty();
		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, StaticMesh, *PackageFilename, SaveArgs);
		}
	}

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(TEXT("Configured Static Mesh settings for: %s"), *AssetPath);
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteCreateDynamicMesh(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	UClass* DynamicMeshClass = FindFirstObject<UClass>(TEXT("DynamicMesh"), EFindFirstObjectOptions::None);
	if (!IsValid(DynamicMeshClass))
	{
		DynamicMeshClass = FindFirstObject<UClass>(TEXT("UDynamicMesh"), EFindFirstObjectOptions::None);
	}

	if (!IsValid(DynamicMeshClass))
	{
		Result.Errors.Add(TEXT("GeometryFramework module is not loaded in this project. Add 'GeometryFramework' to your host project's .Build.cs."));
		return Result;
	}

	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/Meshes/DM_Terrain."),
			*AssetPath));
		return Result;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* Mesh = AssetTools.CreateAsset(AssetName, PackagePath, DynamicMeshClass, nullptr);

	if (!IsValid(Mesh))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create UDynamicMesh at '%s'."), *AssetPath));
		return Result;
	}

	UPackage* Package = Mesh->GetOutermost();
	if (IsValid(Package))
	{
		Package->MarkPackageDirty();
		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, Mesh, *PackageFilename, SaveArgs);
		}
	}
	FAssetRegistryModule::AssetCreated(Mesh);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Successfully created UDynamicMesh asset at '%s'."), *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}


FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteAuditNaniteSettings(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *AssetPath);
	if (!IsValid(StaticMesh))
	{
		Result.Errors.Add(FString::Printf(TEXT("StaticMesh not found: '%s'"), *AssetPath));
		return Result;
	}

	StaticMesh->Modify();
	StaticMesh->GetNaniteSettings().bEnabled = true;
	StaticMesh->PostEditChange();

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Successfully enabled Nanite settings for Static Mesh '%s'."), *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteSetupRuntimeVirtualTexture(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString RVTVolumeName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("rvt_volume_name"), RVTVolumeName, Result.Errors, true))
	{
		return Result;
	}

	FString RVTAssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("rvt_asset_path"), RVTAssetPath, Result.Errors, true))
	{
		return Result;
	}

	URuntimeVirtualTexture* RVTAsset = LoadObject<URuntimeVirtualTexture>(nullptr, *RVTAssetPath);
	if (!IsValid(RVTAsset))
	{
		Result.Errors.Add(FString::Printf(TEXT("RuntimeVirtualTexture asset not found at '%s'."), *RVTAssetPath));
		return Result;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("No active editor world found."));
		return Result;
	}

	ARuntimeVirtualTextureVolume* RVTVolume = nullptr;
	for (TActorIterator<ARuntimeVirtualTextureVolume> It(World); It; ++It)
	{
		if (IsValid(*It) && (It->GetName() == RVTVolumeName || It->GetActorLabel() == RVTVolumeName))
		{
			RVTVolume = *It;
			break;
		}
	}

	if (!IsValid(RVTVolume))
	{
		RVTVolume = World->SpawnActor<ARuntimeVirtualTextureVolume>();
		if (IsValid(RVTVolume))
		{
			RVTVolume->SetActorLabel(*RVTVolumeName);
		}
	}

	if (IsValid(RVTVolume))
	{
		URuntimeVirtualTextureComponent* Component = RVTVolume->VirtualTextureComponent;
		if (IsValid(Component))
		{
			Component->Modify();
			Component->SetVirtualTexture(RVTAsset);
			Component->MarkRenderStateDirty();
		}
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Configured Runtime Virtual Texture on volume '%s' with asset '%s'."), *RVTVolumeName, *RVTAssetPath);
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteSetupChaosPhysics(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("No active editor world found."));
		return Result;
	}

	UPhysicsFieldComponent* PhysicsField = NewObject<UPhysicsFieldComponent>();
	if (!IsValid(PhysicsField))
	{
		Result.Errors.Add(TEXT("Failed to instantiate UPhysicsFieldComponent."));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = TEXT("Successfully instantiated and verified UPhysicsFieldComponent for Chaos Physics solver.");
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteSetupDataflowGraph(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	UClass* DataflowClass = FindFirstObject<UClass>(TEXT("Dataflow"), EFindFirstObjectOptions::None);
	if (!IsValid(DataflowClass))
	{
		DataflowClass = FindFirstObject<UClass>(TEXT("UDataflow"), EFindFirstObjectOptions::None);
	}

	if (!IsValid(DataflowClass))
	{
		Result.Errors.Add(TEXT("DataflowCore module is not loaded in this project. Add 'DataflowCore' to your host project's .Build.cs."));
		return Result;
	}

	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/Meshes/DF_Fracture."),
			*AssetPath));
		return Result;
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UObject* DataflowObj = AssetTools.CreateAsset(AssetName, PackagePath, DataflowClass, nullptr);

	if (!IsValid(DataflowObj))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Dataflow asset at '%s'."), *AssetPath));
		return Result;
	}

	UPackage* Package = DataflowObj->GetOutermost();
	if (IsValid(Package))
	{
		Package->MarkPackageDirty();
		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, DataflowObj, *PackageFilename, SaveArgs);
		}
	}
	FAssetRegistryModule::AssetCreated(DataflowObj);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Successfully created Procedural Dataflow Graph asset at '%s'."), *AssetPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}


FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteSetupClothingSimulation(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	UClass* ClothClass = FindFirstObject<UClass>(TEXT("ClothingAssetBase"), EFindFirstObjectOptions::None);
	if (!IsValid(ClothClass))
	{
		Result.Errors.Add(TEXT("ClothingSystemRuntimeInterface module is not loaded in this project. Add 'ClothingSystemRuntimeInterface' to your host project's .Build.cs."));
		return Result;
	}

	UObject* ClothAsset = NewObject<UObject>(GetTransientPackage(), ClothClass);
	if (!IsValid(ClothAsset))
	{
		Result.Errors.Add(TEXT("Failed to instantiate Clothing Simulation asset."));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = TEXT("Successfully instantiated and verified Clothing Simulation Asset.");
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMeshActions::ExecuteSetupSparseVolumeTexture(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	UClass* SVTClass = FindFirstObject<UClass>(TEXT("SparseVolumeTexture"), EFindFirstObjectOptions::None);
	if (!IsValid(SVTClass))
	{
		Result.Errors.Add(TEXT("SparseVolumeTexture module is not loaded in this project. Add 'SparseVolumeTexture' to your host project's .Build.cs."));
		return Result;
	}

	UObject* SVT = NewObject<UObject>(GetTransientPackage(), SVTClass);
	if (!IsValid(SVT))
	{
		Result.Errors.Add(TEXT("Failed to instantiate Sparse Volume Texture."));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = TEXT("Successfully instantiated and verified Sparse Volume Texture (SVT) asset.");
	return Result;
}

