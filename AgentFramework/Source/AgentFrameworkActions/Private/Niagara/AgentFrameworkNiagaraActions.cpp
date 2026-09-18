// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Niagara/AgentFrameworkNiagaraActions.h"
#include "AgentFrameworkCoreModule.h"
#include "AgentFrameworkActionUtils.h"

// Niagara Runtime & Actor
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraComponent.h"
#include "NiagaraActor.h"
#include "NiagaraWorldManager.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraTypes.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"

// Niagara Editor & Graph (WITH_EDITOR context)
#if WITH_EDITOR
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "EdGraph/EdGraphNode.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraDataInterface.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraDataChannelAsset.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "NiagaraDataInterfaceCurve.h"
#include "NiagaraDataInterfaceColorCurve.h"
#include "NiagaraDataInterfaceVectorCurve.h"
#include "NiagaraDataInterfaceVector2DCurve.h"
#include "NiagaraDataInterfaceVector4Curve.h"
#endif

// Unreal Engine Core / Editor Systems
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Engine/SceneCapture2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Viewport/AgentFrameworkViewportActions.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "Sound/SoundBase.h"
namespace
{
	FString NiagaraFormatJsonObjectToUnrealText(const TSharedPtr<FJsonObject>& Obj);

	FString NiagaraFormatJsonValueToUnrealText(const TSharedPtr<FJsonValue>& Val)
	{
		if (!Val.IsValid() || Val->IsNull()) return TEXT("");
		if (Val->Type == EJson::String) return Val->AsString();
		if (Val->Type == EJson::Number) return FString::Printf(TEXT("%f"), Val->AsNumber());
		if (Val->Type == EJson::Boolean) return Val->AsBool() ? TEXT("True") : TEXT("False");
		if (Val->Type == EJson::Object) return NiagaraFormatJsonObjectToUnrealText(Val->AsObject());
		if (Val->Type == EJson::Array)
		{
			FString OutStr = TEXT("(");
			bool bFirst = true;
			for (const auto& Elem : Val->AsArray())
			{
				if (!bFirst) OutStr += TEXT(",");
				bFirst = false;
				OutStr += NiagaraFormatJsonValueToUnrealText(Elem);
			}
			OutStr += TEXT(")");
			return OutStr;
		}
		return TEXT("");
	}

	FString NiagaraFormatJsonObjectToUnrealText(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid()) return TEXT("()");
		FString OutStr = TEXT("(");
		bool bFirst = true;
		for (const auto& Pair : Obj->Values)
		{
			if (!bFirst) OutStr += TEXT(",");
			bFirst = false;
			OutStr += FString(*Pair.Key) + TEXT("=") + NiagaraFormatJsonValueToUnrealText(Pair.Value);
		}
		OutStr += TEXT(")");
		return OutStr;
	}

	int32 ApplyPropertiesFromJsonObject(UObject* TargetObject, const TSharedPtr<FJsonObject>& PropertiesObj, FAgentFrameworkActionResult& Result)
	{
		if (!IsValid(TargetObject) || !PropertiesObj.IsValid()) return 0;
		UClass* TargetClass = TargetObject->GetClass();
		if (!IsValid(TargetClass)) return 0;

		int32 ModifiedCount = 0;
		for (const auto& Pair : PropertiesObj->Values)
		{
			FString PropName = FString(*Pair.Key);
			FProperty* Prop = TargetClass->FindPropertyByName(FName(*PropName));
			if (!Prop)
			{
				for (TFieldIterator<FProperty> It(TargetClass); It; ++It)
				{
					if (It->GetName().Equals(PropName, ESearchCase::IgnoreCase))
					{
						Prop = *It;
						break;
					}
				}
			}

			if (!Prop)
			{
				Result.Warnings.Add(FString::Printf(TEXT("Property '%s' not found on class '%s'."), *PropName, *TargetClass->GetName()));
				continue;
			}

			FString ValueString = NiagaraFormatJsonValueToUnrealText(Pair.Value);
			TargetObject->PreEditChange(Prop);
			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(TargetObject);
			const TCHAR* ImportResult = Prop->ImportText_Direct(*ValueString, PropAddr, TargetObject, PPF_None);

			FPropertyChangedEvent ChangedEvent(Prop);
			TargetObject->PostEditChangeProperty(ChangedEvent);

			if (ImportResult != nullptr)
			{
				ModifiedCount++;
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Failed to import value '%s' for property '%s' on '%s'."), *ValueString, *PropName, *TargetObject->GetName()));
			}
		}
		return ModifiedCount;
	}

	// Every emitter handle must be represented by a UNiagaraNodeEmitter in both system script graphs; that node
	// is what pulls the emitter's EmitterSpawn / EmitterUpdate graphs (spawn rate, burst, state) into the compiled
	// system scripts. A handle without one compiles clean, shows every module in the stack, and never spawns a
	// particle. Returns how many handles are missing their node in either graph.
	int32 CountEmitterHandlesWithoutSystemNodes(UNiagaraSystem& System)
	{
#if WITH_EDITOR
		auto CountEmitterNodes = [](UNiagaraScript* Script) -> int32
		{
			UNiagaraScriptSource* Source = Script ? Cast<UNiagaraScriptSource>(Script->GetLatestSource()) : nullptr;
			if (!Source || !Source->NodeGraph)
			{
				return 0;
			}
			// UNiagaraNodeEmitter lives in a private NiagaraEditor header, so match it by class name.
			static const FName EmitterNodeClassName(TEXT("NiagaraNodeEmitter"));
			int32 NumEmitterNodes = 0;
			for (const UEdGraphNode* Node : Source->NodeGraph->Nodes)
			{
				if (Node && Node->GetClass()->GetFName() == EmitterNodeClassName)
				{
					++NumEmitterNodes;
				}
			}
			return NumEmitterNodes;
		};
		const int32 NumHandles = System.GetEmitterHandles().Num();
		const int32 MissingInSpawn = NumHandles - CountEmitterNodes(System.GetSystemSpawnScript());
		const int32 MissingInUpdate = NumHandles - CountEmitterNodes(System.GetSystemUpdateScript());
		return FMath::Max(0, FMath::Max(MissingInSpawn, MissingInUpdate));
#else
		return 0;
#endif
	}

	// Rebuild the system scripts' emitter nodes for every handle. FNiagaraStackGraphUtilities::RebuildEmitterNodes
	// is what the engine uses but it is not exported, so this goes through two exported entry points that each call
	// it: FNiagaraEditorUtilities::AddEmitterToSystem (adds a throwaway emitter, rebuilds the nodes of every handle)
	// and UNiagaraExternalEditUtilities::RemoveEmitter (removes it again, rebuilds once more). The system ends with
	// the same handles it started with.
	bool RebuildSystemEmitterNodes(UNiagaraSystem& System, FString& OutError)
	{
#if WITH_EDITOR
		UNiagaraEmitter* ThrowawayTemplate = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst"));
		if (!IsValid(ThrowawayTemplate))
		{
			OutError = TEXT("Could not load the engine template emitter needed to rebuild the system's emitter nodes.");
			return false;
		}

		const int32 NumHandlesBefore = System.GetEmitterHandles().Num();
		const FGuid ThrowawayId = FNiagaraEditorUtilities::AddEmitterToSystem(System, *ThrowawayTemplate, ThrowawayTemplate->GetExposedVersion().VersionGuid);
		const FNiagaraEmitterHandle* ThrowawayHandle = System.GetEmitterHandles().FindByPredicate(
			[&ThrowawayId](const FNiagaraEmitterHandle& Handle) { return Handle.GetId() == ThrowawayId; });
		if (!ThrowawayHandle)
		{
			OutError = TEXT("Adding the throwaway emitter used to rebuild the system's emitter nodes did not produce a handle.");
			return false;
		}
		const FName ThrowawayName = ThrowawayHandle->GetName();
		UNiagaraEmitter* ThrowawayEmitter = ThrowawayHandle->GetInstance().Emitter;

		FNiagaraExternalEditContext Context(&System);
		const FNiagaraExt_StackItemReference ThrowawayRef(&System, ThrowawayName);
		UNiagaraExternalEditUtilities::RemoveEmitter(ThrowawayRef, Context);
		if (!Context.HasErrors() && IsValid(ThrowawayEmitter) && ThrowawayEmitter->GetOuter() == &System)
		{
			// Removing the handle leaves the emitter object outered to the system, and the next save would write it
			// into the package as an orphan. Move it out so it is neither saved nor found by name again.
			ThrowawayEmitter->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			ThrowawayEmitter->MarkAsGarbage();
		}
		if (Context.HasErrors() || System.GetEmitterHandles().Num() != NumHandlesBefore)
		{
			OutError = FString::Printf(TEXT("Removing the throwaway emitter '%s' after rebuilding the system's emitter nodes failed (%d handle(s) before, %d after)."),
				*ThrowawayName.ToString(), NumHandlesBefore, System.GetEmitterHandles().Num());
			for (const FText& Error : Context.Errors)
			{
				OutError += TEXT(" ") + Error.ToString();
			}
			return false;
		}
		return true;
#else
		OutError = TEXT("Rebuilding emitter nodes is only supported in the Editor.");
		return false;
#endif
	}

	void SaveAndDirtyAsset(UNiagaraSystem* System)
	{
		if (!IsValid(System)) return;
		UPackage* Package = System->GetOutermost();
		if (IsValid(Package))
		{
			Package->MarkPackageDirty();
			FString PackageFilename;
			if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
			{
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Standalone;
				if (!UPackage::SavePackage(Package, System, *PackageFilename, SaveArgs))
				{
					UE_LOG(LogAgentFramework, Warning, TEXT("SaveAndDirtyAsset: Failed to save package '%s' to '%s'."), *Package->GetName(), *PackageFilename);
				}
			}
			else
			{
				UE_LOG(LogAgentFramework, Warning, TEXT("SaveAndDirtyAsset: Failed to resolve package filename for '%s'."), *Package->GetName());
			}
		}
	}
}

FAgentFrameworkNiagaraActions::FAgentFrameworkNiagaraActions() {}
FAgentFrameworkNiagaraActions::~FAgentFrameworkNiagaraActions() {}

FName FAgentFrameworkNiagaraActions::GetActionName() const { return FName(TEXT("Niagara")); }

TArray<FString> FAgentFrameworkNiagaraActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_niagara_system"),
		TEXT("create_niagara_data_channel"),
		TEXT("create_niagara_effect_type"),
		TEXT("add_niagara_emitter"),
		TEXT("add_niagara_module"),
		TEXT("set_niagara_module_pin"),
		TEXT("reset_niagara_module_pin"),
		TEXT("compile_niagara_system"),
		TEXT("capture_niagara_system_isolated"),
		TEXT("set_niagara_parameter"),
		TEXT("set_niagara_data_interface"),
		TEXT("add_niagara_renderer"),
		TEXT("edit_niagara_renderer"),
		TEXT("remove_niagara_renderer"),
		TEXT("remove_niagara_emitter"),
		TEXT("remove_niagara_module"),
		TEXT("list_niagara_parameters"),
		TEXT("remove_niagara_parameter")
	};
}

bool FAgentFrameworkNiagaraActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	FString ToolName;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, OutErrors, false);

	if (ToolName == TEXT("create_niagara_system") || ToolName == TEXT("create_niagara_data_channel") || ToolName == TEXT("create_niagara_effect_type"))
	{
		FString AssetPath;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("AssetPath"), AssetPath, OutErrors, true))
		{
			return false;
		}
	}
	else
	{
		// All other tools require system_path (or SystemAsset or asset_path as fallbacks)
		if (!Params->HasField(TEXT("system_path")) && !Params->HasField(TEXT("SystemAsset")) && !Params->HasField(TEXT("asset_path")))
		{
			OutErrors.Add(TEXT("Missing required field: system_path or SystemAsset"));
			return false;
		}

		if (ToolName == TEXT("add_niagara_emitter"))
		{
			FString EmitterTemplate, EmitterName;
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_template"), EmitterTemplate, OutErrors, true) ||
				!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, OutErrors, true))
			{
				return false;
			}
		}
		else if (ToolName == TEXT("add_niagara_module"))
		{
			FString Phase, ModuleType;
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("phase"), Phase, OutErrors, true) ||
				!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("module_type"), ModuleType, OutErrors, true))
			{
				return false;
			}

			const bool bIsSystemPhase = (Phase == TEXT("SystemSpawn") || Phase == TEXT("SystemUpdate"));
			if (!bIsSystemPhase)
			{
				FString EmitterName;
				if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, OutErrors, true))
				{
					return false;
				}
			}
		}
		else if (ToolName == TEXT("set_niagara_module_pin"))
		{
			FString Phase, ModuleType, PinName;
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("phase"), Phase, OutErrors, true) ||
				!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("module_type"), ModuleType, OutErrors, true) ||
				!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("pin_name"), PinName, OutErrors, true))
			{
				return false;
			}

			if (!Params->HasField(TEXT("value")) && !Params->HasField(TEXT("Value")) &&
				!Params->HasField(TEXT("link_parameter")) && !Params->HasField(TEXT("LinkParameter")) &&
				!Params->HasField(TEXT("asset_path")) && !Params->HasField(TEXT("AssetPath")) &&
				!Params->HasField(TEXT("interface_class")) && !Params->HasField(TEXT("data_interface_class")) && !Params->HasField(TEXT("DataInterfaceClass")) &&
				!Params->HasField(TEXT("properties")) && !Params->HasField(TEXT("Properties")) &&
				!Params->HasField(TEXT("curve_keys")) && !Params->HasField(TEXT("CurveKeys")) &&
				!Params->HasField(TEXT("dynamic_input")) && !Params->HasField(TEXT("DynamicInput")))
			{
				OutErrors.Add(TEXT("Either 'value', 'link_parameter', 'asset_path', 'interface_class', 'properties', 'curve_keys', or 'dynamic_input' must be provided for set_niagara_module_pin."));
				return false;
			}

			const bool bIsSystemPhase = (Phase == TEXT("SystemSpawn") || Phase == TEXT("SystemUpdate"));
			if (!bIsSystemPhase)
			{
				FString EmitterName;
				if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, OutErrors, true))
				{
					return false;
				}
			}
		}
		else if (ToolName == TEXT("reset_niagara_module_pin"))
		{
			FString Phase, ModuleType, PinName;
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("phase"), Phase, OutErrors, true) ||
				!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("module_type"), ModuleType, OutErrors, true) ||
				!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("pin_name"), PinName, OutErrors, true))
			{
				return false;
			}

			const bool bIsSystemPhase = (Phase == TEXT("SystemSpawn") || Phase == TEXT("SystemUpdate"));
			if (!bIsSystemPhase)
			{
				FString EmitterName;
				if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, OutErrors, true))
				{
					return false;
				}
			}
		}
		else if (ToolName == TEXT("set_niagara_parameter"))
		{
			if (!Params->HasField(TEXT("parameter_name")) && !Params->HasField(TEXT("ParameterName")))
			{
				OutErrors.Add(TEXT("Missing required field: parameter_name or ParameterName"));
				return false;
			}
		}
		else if (ToolName == TEXT("set_niagara_data_interface"))
		{
			if (!Params->HasField(TEXT("parameter_name")) && !Params->HasField(TEXT("ParameterName")))
			{
				OutErrors.Add(TEXT("Missing required field: parameter_name or ParameterName"));
				return false;
			}
			if (!Params->HasField(TEXT("interface_class")) && !Params->HasField(TEXT("data_interface_class")) && !Params->HasField(TEXT("DataInterfaceClass")))
			{
				OutErrors.Add(TEXT("Missing required field: interface_class or data_interface_class"));
				return false;
			}
		}
		else if (ToolName == TEXT("add_niagara_renderer"))
		{
			FString EmitterName, RendererType;
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, OutErrors, true) ||
				!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("renderer_type"), RendererType, OutErrors, true))
			{
				return false;
			}
		}
		else if (ToolName == TEXT("edit_niagara_renderer") || ToolName == TEXT("remove_niagara_renderer"))
		{
			FString EmitterName;
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, OutErrors, true))
			{
				return false;
			}
			if (!Params->HasField(TEXT("renderer_type")) && !Params->HasField(TEXT("renderer_index")) && !Params->HasField(TEXT("target_index")))
			{
				OutErrors.Add(TEXT("Either 'renderer_type' or 'renderer_index' must be provided."));
				return false;
			}
		}
		else if (ToolName == TEXT("remove_niagara_emitter"))
		{
			FString EmitterName;
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, OutErrors, true))
			{
				return false;
			}
		}
		else if (ToolName == TEXT("remove_niagara_module"))
		{
			FString Phase, ModuleType;
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("phase"), Phase, OutErrors, true) ||
				!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("module_type"), ModuleType, OutErrors, true))
			{
				return false;
			}
			const bool bIsSystemPhase = (Phase == TEXT("SystemSpawn") || Phase == TEXT("SystemUpdate"));
			if (!bIsSystemPhase)
			{
				FString EmitterName;
				if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, OutErrors, true))
				{
					return false;
				}
			}
		}
		else if (ToolName == TEXT("list_niagara_parameters"))
		{
			// system_path or asset_path validated in the outer common block
		}
		else if (ToolName == TEXT("remove_niagara_parameter"))
		{
			if (!Params->HasField(TEXT("parameter_name")) && !Params->HasField(TEXT("ParameterName")))
			{
				OutErrors.Add(TEXT("Missing required field: parameter_name or ParameterName"));
				return false;
			}
		}
	}

	return true;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAgentFrameworkActionResult Result;
	Result.bSuccess = false;

	FString ToolName;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, Result.Errors, false);

	bool bIsReadOnly = (ToolName == TEXT("capture_niagara_system_isolated") || ToolName == TEXT("list_niagara_parameters"));

	TOptional<FScopedTransaction> Transaction;
	if (!bIsReadOnly)
	{
		Transaction.Emplace(FText::FromString(TEXT("AgentFramework Niagara Action")));
	}

	if (ToolName == TEXT("create_niagara_system"))               Result = ExecuteCreateSystem(Params, Result);
	else if (ToolName == TEXT("create_niagara_data_channel"))    Result = ExecuteCreateDataChannel(Params, Result);
	else if (ToolName == TEXT("create_niagara_effect_type"))     Result = ExecuteCreateEffectType(Params, Result);
	else if (ToolName == TEXT("add_niagara_emitter"))             Result = ExecuteAddEmitter(Params, Result);
	else if (ToolName == TEXT("add_niagara_module"))              Result = ExecuteAddModule(Params, Result);
	else if (ToolName == TEXT("set_niagara_module_pin"))          Result = ExecuteSetModulePin(Params, Result);
	else if (ToolName == TEXT("reset_niagara_module_pin"))        Result = ExecuteResetModulePin(Params, Result);
	else if (ToolName == TEXT("compile_niagara_system"))          Result = ExecuteCompileSystem(Params, Result);
	else if (ToolName == TEXT("capture_niagara_system_isolated")) Result = ExecuteCaptureIsolated(Params, Result);
	else if (ToolName == TEXT("set_niagara_parameter"))          Result = ExecuteSetNiagaraParameter(Params, Result);
	else if (ToolName == TEXT("set_niagara_data_interface"))     Result = ExecuteSetDataInterface(Params, Result);
	else if (ToolName == TEXT("add_niagara_renderer"))            Result = ExecuteAddRenderer(Params, Result);
	else if (ToolName == TEXT("edit_niagara_renderer"))           Result = ExecuteEditRenderer(Params, Result);
	else if (ToolName == TEXT("remove_niagara_renderer"))         Result = ExecuteRemoveRenderer(Params, Result);
	else if (ToolName == TEXT("remove_niagara_emitter"))          Result = ExecuteRemoveEmitter(Params, Result);
	else if (ToolName == TEXT("remove_niagara_module"))           Result = ExecuteRemoveModule(Params, Result);
	else if (ToolName == TEXT("list_niagara_parameters"))         Result = ExecuteListNiagaraParameters(Params, Result);
	else if (ToolName == TEXT("remove_niagara_parameter"))       Result = ExecuteRemoveNiagaraParameter(Params, Result);
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("Unknown Niagara tool: '%s'"), *ToolName));
	}

	if (Result.bSuccess)
	{
		PlaySuccessSound();
	}

	if (Transaction.IsSet() && !Result.bSuccess)
	{
		Transaction->Cancel();
	}

	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteCreateSystem(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}
	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/VFX/NS_Explosion."),
			*AssetPath));
		return Result;
	}

	// Load through the explicit object path — a bare package path does not reliably resolve.
	UNiagaraSystem* ExistingSystem = LoadObject<UNiagaraSystem>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName), nullptr, LOAD_NoWarn, nullptr);
	if (IsValid(ExistingSystem))
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Niagara System '%s' already exists."), *AssetName);
		Result.ModifiedAssets.Add(AssetPath);
		return Result;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();
	UNiagaraSystemFactoryNew* Factory = NewObject<UNiagaraSystemFactoryNew>();
	if (!IsValid(Factory))
	{
		Result.Errors.Add(TEXT("Failed to create UNiagaraSystemFactoryNew instance."));
		return Result;
	}

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UNiagaraSystem::StaticClass(), Factory);
	UNiagaraSystem* NewSystem = Cast<UNiagaraSystem>(NewAsset);

	if (!IsValid(NewSystem))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Niagara System at %s"), *AssetPath));
		return Result;
	}

	NewSystem->Modify();
	UPackage* Package = NewSystem->GetOutermost();
	if (IsValid(Package))
	{
		Package->MarkPackageDirty();

		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, NewSystem, *PackageFilename, SaveArgs);
		}
	}

	FAssetRegistryModule::AssetCreated(NewSystem);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created empty Niagara System '%s'"), *AssetName);
	Result.ModifiedAssets.Add(AssetPath);
#else
	Result.Errors.Add(TEXT("Niagara System creation is only supported in the Editor."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteCreateDataChannel(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, false) &&
		!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("AssetPath"), AssetPath, Result.Errors, true))
	{
		return Result;
	}
	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/VFX/NDC_Impacts."),
			*AssetPath));
		return Result;
	}

	UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName), nullptr, LOAD_NoWarn, nullptr);
	if (IsValid(ExistingAsset))
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Niagara Data Channel '%s' already exists."), *AssetName);
		Result.ModifiedAssets.Add(AssetPath);
		return Result;
	}

	UClass* DataChannelClass = LoadObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraDataChannelAsset"));
	if (!IsValid(DataChannelClass))
	{
		DataChannelClass = FindFirstObject<UClass>(TEXT("NiagaraDataChannelAsset"), EFindFirstObjectOptions::NativeFirst);
	}
	if (!IsValid(DataChannelClass))
	{
		DataChannelClass = FindFirstObject<UClass>(TEXT("UNiagaraDataChannelAsset"), EFindFirstObjectOptions::NativeFirst);
	}
	if (!IsValid(DataChannelClass))
	{
		DataChannelClass = FindFirstObject<UClass>(TEXT("NiagaraDataChannel"), EFindFirstObjectOptions::NativeFirst);
	}

	if (!IsValid(DataChannelClass))
	{
		Result.Errors.Add(TEXT("NiagaraDataChannelAsset class not found. Ensure Niagara plugin is enabled."));
		return Result;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UClass* FactoryClass = LoadObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraDataChannelAssetFactoryNew"));
	if (!IsValid(FactoryClass))
	{
		FactoryClass = FindFirstObject<UClass>(TEXT("NiagaraDataChannelAssetFactoryNew"), EFindFirstObjectOptions::NativeFirst);
	}
	if (!IsValid(FactoryClass))
	{
		FactoryClass = FindFirstObject<UClass>(TEXT("UNiagaraDataChannelAssetFactoryNew"), EFindFirstObjectOptions::NativeFirst);
	}

	UFactory* Factory = nullptr;
	if (IsValid(FactoryClass) && FactoryClass->IsChildOf(UFactory::StaticClass()))
	{
		Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
	}
	else
	{
		// Search registered asset factories
		for (UFactory* Candidate : AssetTools.GetNewAssetFactories())
		{
			if (IsValid(Candidate) && Candidate->GetSupportedClass() == DataChannelClass)
			{
				Factory = NewObject<UFactory>(GetTransientPackage(), Candidate->GetClass());
				break;
			}
		}
	}

	UObject* NewAsset = nullptr;
	if (IsValid(Factory))
	{
		NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, DataChannelClass, Factory);
	}
	else
	{
		NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, DataChannelClass, nullptr);
	}

	if (!IsValid(NewAsset))
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (IsValid(Package))
		{
			NewAsset = NewObject<UObject>(Package, DataChannelClass, FName(*AssetName), RF_Public | RF_Standalone);
			if (IsValid(NewAsset))
			{
				FAssetRegistryModule::AssetCreated(NewAsset);
			}
		}
	}

	if (!IsValid(NewAsset))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Niagara Data Channel at %s"), *AssetPath));
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
	Result.ResultMessage = FString::Printf(TEXT("Created Niagara Data Channel '%s'"), *AssetName);
	Result.ModifiedAssets.Add(AssetPath);
#else
	Result.Errors.Add(TEXT("Niagara Data Channel creation is only supported in the Editor."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteCreateEffectType(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, false) &&
		!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("AssetPath"), AssetPath, Result.Errors, true))
	{
		return Result;
	}
	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/VFX/NE_ExplosionScalability."),
			*AssetPath));
		return Result;
	}

	UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName), nullptr, LOAD_NoWarn, nullptr);
	if (IsValid(ExistingAsset))
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Niagara Effect Type '%s' already exists."), *AssetName);
		Result.ModifiedAssets.Add(AssetPath);
		return Result;
	}

	UClass* EffectTypeClass = LoadObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraEffectType"));
	if (!IsValid(EffectTypeClass))
	{
		EffectTypeClass = FindFirstObject<UClass>(TEXT("NiagaraEffectType"), EFindFirstObjectOptions::NativeFirst);
	}
	if (!IsValid(EffectTypeClass))
	{
		EffectTypeClass = FindFirstObject<UClass>(TEXT("UNiagaraEffectType"), EFindFirstObjectOptions::NativeFirst);
	}

	if (!IsValid(EffectTypeClass))
	{
		Result.Errors.Add(TEXT("NiagaraEffectType class not found. Ensure Niagara plugin is enabled."));
		return Result;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UClass* FactoryClass = LoadObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraEffectTypeFactoryNew"));
	if (!IsValid(FactoryClass))
	{
		FactoryClass = FindFirstObject<UClass>(TEXT("NiagaraEffectTypeFactoryNew"), EFindFirstObjectOptions::NativeFirst);
	}
	if (!IsValid(FactoryClass))
	{
		FactoryClass = FindFirstObject<UClass>(TEXT("UNiagaraEffectTypeFactoryNew"), EFindFirstObjectOptions::NativeFirst);
	}

	UFactory* Factory = nullptr;
	if (IsValid(FactoryClass) && FactoryClass->IsChildOf(UFactory::StaticClass()))
	{
		Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
	}
	else
	{
		// Search registered asset factories
		for (UFactory* Candidate : AssetTools.GetNewAssetFactories())
		{
			if (IsValid(Candidate) && Candidate->GetSupportedClass() == EffectTypeClass)
			{
				Factory = NewObject<UFactory>(GetTransientPackage(), Candidate->GetClass());
				break;
			}
		}
	}

	UObject* NewAsset = nullptr;
	if (IsValid(Factory))
	{
		NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, EffectTypeClass, Factory);
	}
	else
	{
		NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, EffectTypeClass, nullptr);
	}

	if (!IsValid(NewAsset))
	{
		UPackage* Package = CreatePackage(*PackageName);
		if (IsValid(Package))
		{
			NewAsset = NewObject<UObject>(Package, EffectTypeClass, FName(*AssetName), RF_Public | RF_Standalone);
			if (IsValid(NewAsset))
			{
				FAssetRegistryModule::AssetCreated(NewAsset);
			}
		}
	}

	if (!IsValid(NewAsset))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Niagara Effect Type at %s"), *AssetPath));
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
	Result.ResultMessage = FString::Printf(TEXT("Created Niagara Effect Type '%s'"), *AssetName);
	Result.ModifiedAssets.Add(AssetPath);
#else
	Result.Errors.Add(TEXT("Niagara Effect Type creation is only supported in the Editor."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteAddEmitter(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}
	FString EmitterTemplate, EmitterName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_template"), EmitterTemplate, Result.Errors, true) ||
		!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, Result.Errors, true))
	{
		return Result;
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	if (IsGarbageCollecting())
	{
		Result.Errors.Add(TEXT("Cannot modify Niagara System while Garbage Collection is in progress."));
		return Result;
	}

	UNiagaraEmitter* SourceEmitter = nullptr;
	FGuid VersionGuid;

	// Case 1: Arbitrary path starting with /
	if (EmitterTemplate.StartsWith(TEXT("/")))
	{
		// Check if it's a UNiagaraEmitter
		SourceEmitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterTemplate);
		if (!IsValid(SourceEmitter))
		{
			FString LeafName = FPackageName::GetShortName(EmitterTemplate);
			FString NormalizedPath = FString::Printf(TEXT("%s.%s"), *EmitterTemplate, *LeafName);
			SourceEmitter = LoadObject<UNiagaraEmitter>(nullptr, *NormalizedPath);
		}

		if (!IsValid(SourceEmitter))
		{
			// Check if it's a UNiagaraSystem from which we can extract an emitter
			UNiagaraSystem* SrcSystem = LoadObject<UNiagaraSystem>(nullptr, *EmitterTemplate);
			if (IsValid(SrcSystem))
			{
				FString SourceEmitterName;
				Params->TryGetStringField(TEXT("source_emitter_name"), SourceEmitterName);
				if (SourceEmitterName.IsEmpty())
				{
					Params->TryGetStringField(TEXT("SourceEmitterName"), SourceEmitterName);
				}

				const TArray<FNiagaraEmitterHandle>& SrcHandles = SrcSystem->GetEmitterHandles();
				const FNiagaraEmitterHandle* MatchedHandle = nullptr;

				if (!SourceEmitterName.IsEmpty())
				{
					for (const FNiagaraEmitterHandle& Handle : SrcHandles)
					{
						if (Handle.GetName().ToString() == SourceEmitterName)
						{
							MatchedHandle = &Handle;
							break;
						}
					}
					if (!MatchedHandle)
					{
						Result.Errors.Add(FString::Printf(TEXT("Source emitter '%s' not found in source Niagara System '%s'"), *SourceEmitterName, *EmitterTemplate));
						return Result;
					}
				}
				else if (SrcHandles.Num() > 0)
				{
					MatchedHandle = &SrcHandles[0];
				}
				else
				{
					Result.Errors.Add(FString::Printf(TEXT("Source Niagara System '%s' has no emitters to copy"), *EmitterTemplate));
					return Result;
				}

				if (MatchedHandle)
				{
					SourceEmitter = MatchedHandle->GetInstance().Emitter;
					VersionGuid = MatchedHandle->GetInstance().Version;
				}
			}
		}
	}
	else
	{
		// Standard engine template names
		FString TemplatePath;
		if (EmitterTemplate.Equals(TEXT("SpriteBurst"), ESearchCase::IgnoreCase))        TemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst");
		else if (EmitterTemplate.Equals(TEXT("RibbonTrail"), ESearchCase::IgnoreCase))   TemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Emitters/LocationBasedRibbon.LocationBasedRibbon");
		else if (EmitterTemplate.Equals(TEXT("MeshDebris"), ESearchCase::IgnoreCase))    TemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Emitters/UpwardMeshBurst.UpwardMeshBurst");
		else if (EmitterTemplate.Equals(TEXT("GPUSimulation"), ESearchCase::IgnoreCase)) TemplatePath = TEXT("/Niagara/DefaultAssets/Templates/Emitters/DirectionalBurst.DirectionalBurst");

		if (!TemplatePath.IsEmpty())
		{
			SourceEmitter = LoadObject<UNiagaraEmitter>(nullptr, *TemplatePath);
		}
		else
		{
			Result.Errors.Add(FString::Printf(TEXT("Unrecognized emitter template '%s'. Expected 'SpriteBurst', 'RibbonTrail', 'MeshDebris', 'GPUSimulation', or a valid /Game/... or /Niagara/... asset path."), *EmitterTemplate));
			return Result;
		}
	}

	if (!IsValid(SourceEmitter))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to load source emitter or template at '%s'"), *EmitterTemplate));
		return Result;
	}

	System->Modify();

	if (!VersionGuid.IsValid())
	{
		VersionGuid = SourceEmitter->GetExposedVersion().VersionGuid;
		if (!VersionGuid.IsValid())
		{
			FVersionedNiagaraEmitterData* EmitterData = SourceEmitter->GetLatestEmitterData();
			if (EmitterData)
			{
				VersionGuid = EmitterData->Version.VersionGuid;
			}
		}
	}

	const FNiagaraEmitterHandle& AddedHandle = System->AddEmitterHandle(*SourceEmitter, FName(*EmitterName), VersionGuid);
	if (!AddedHandle.GetId().IsValid())
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to add emitter handle '%s' to system"), *EmitterName));
		return Result;
	}

	// The engine's own add path (FNiagaraEditorUtilities::AddEmitterToSystem) rebuilds the system scripts' emitter
	// nodes right after adding the handle. Without this the new emitter's EmitterSpawn / EmitterUpdate graphs are
	// never compiled into the system: NS_MuzzleFlash was built this way and never spawned a particle while eleven
	// structural tests and a clean compile said it was fine (PLAN_3.9, 2026-09-14).
	FString RebuildError;
	if (!RebuildSystemEmitterNodes(*System, RebuildError))
	{
		Result.Errors.Add(RebuildError);
		return Result;
	}

	UNiagaraSystemEditorData* SystemEditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
	if (SystemEditorData)
	{
		SystemEditorData->SynchronizeOverviewGraphWithSystem(*System);
	}

	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully added emitter '%s' (from '%s') to system '%s'"), *EmitterName, *EmitterTemplate, *SystemPath);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Emitter configuration is only supported in the Editor."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteAddModule(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}
	FString EmitterName, Phase, ModuleType;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("phase"), Phase, Result.Errors, true) ||
		!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("module_type"), ModuleType, Result.Errors, true))
	{
		return Result;
	}

	const bool bIsSystemPhase = (Phase == TEXT("SystemSpawn") || Phase == TEXT("SystemUpdate"));
	if (!bIsSystemPhase)
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, Result.Errors, true))
		{
			return Result;
		}
	}
	else
	{
		Params->TryGetStringField(TEXT("emitter_name"), EmitterName);
	}

	// Forward renderer requests if someone passed LightRenderer or other renderer type
	if (ModuleType.Equals(TEXT("LightRenderer"), ESearchCase::IgnoreCase) ||
		ModuleType.Equals(TEXT("SpriteRenderer"), ESearchCase::IgnoreCase) ||
		ModuleType.Equals(TEXT("RibbonRenderer"), ESearchCase::IgnoreCase) ||
		ModuleType.Equals(TEXT("MeshRenderer"), ESearchCase::IgnoreCase))
	{
		TSharedRef<FJsonObject> RendererParams = MakeShared<FJsonObject>();
		for (const auto& Pair : Params->Values)
		{
			RendererParams->SetField(Pair.Key, Pair.Value);
		}
		FString RendererTypeStr = ModuleType;
		if (RendererTypeStr.EndsWith(TEXT("Renderer")))
		{
			RendererTypeStr = RendererTypeStr.LeftChop(8);
		}
		RendererParams->SetStringField(TEXT("renderer_type"), RendererTypeStr);
		return ExecuteAddRenderer(RendererParams, Result);
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	UNiagaraGraph* Graph = nullptr;
	UNiagaraNodeOutput* OutputNode = nullptr;
	FString FindError;
	if (!ResolvePhaseContext(System, EmitterName, Phase, Graph, OutputNode, FindError))
	{
		Result.Errors.Add(FindError);
		return Result;
	}

	// Resolve standard module script path
	FString ModulePath;
	if (ModuleType.StartsWith(TEXT("/")))
	{
		ModulePath = ModuleType;
	}
	else if (ModuleType.Equals(TEXT("AddVelocity"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity");
	else if (ModuleType.Equals(TEXT("GravityForce"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce");
	else if (ModuleType.Equals(TEXT("Drag"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Update/Forces/Drag.Drag");
	else if (ModuleType.Equals(TEXT("Collision"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Collision/Collision.Collision");
	else if (ModuleType.Equals(TEXT("AccelerationForce"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Update/Forces/AccelerationForce.AccelerationForce");
	else if (ModuleType.Equals(TEXT("SpawnBurstInstantaneous"), ESearchCase::IgnoreCase) || ModuleType.Equals(TEXT("SpawnBurst_Instantaneous"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous");
	else if (ModuleType.Equals(TEXT("SpawnRate"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate");
	else if (ModuleType.Equals(TEXT("SpawnPerFrame"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Emitter/SpawnPerFrame.SpawnPerFrame");
	else if (ModuleType.Equals(TEXT("ScaleSpriteSize"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize");
	else if (ModuleType.Equals(TEXT("ScaleColor"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Update/Color/ScaleColor.ScaleColor");
	else if (ModuleType.Equals(TEXT("SolveForcesAndVelocity"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity");
	else if (ModuleType.Equals(TEXT("UpdateAge"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Update/Lifetime/UpdateAge.UpdateAge");
	else if (ModuleType.Equals(TEXT("PointLocation"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Spawn/Location/PointLocation.PointLocation");
	else if (ModuleType.Equals(TEXT("SphereLocation"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Spawn/Location/SphereLocation.SphereLocation");
	else if (ModuleType.Equals(TEXT("BoxLocation"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Spawn/Location/BoxLocation.BoxLocation");
	else if (ModuleType.Equals(TEXT("SpriteFacingAndAlignment"), ESearchCase::IgnoreCase))
		ModulePath = TEXT("/Niagara/Modules/Update/Renderers/Sprite/SpriteFacingAndAlignment.SpriteFacingAndAlignment");
	else
	{
		// Default search in standard module locations
		ModulePath = FString::Printf(TEXT("/Niagara/Modules/Emitter/%s.%s"), *ModuleType, *ModuleType);
	}

	UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, *ModulePath);
	if (!IsValid(ModuleScript) && !ModuleType.StartsWith(TEXT("/")))
	{
		// Fallbacks in Update/Forces, Spawn/Velocity, and Collision
		ModuleScript = LoadObject<UNiagaraScript>(nullptr, *FString::Printf(TEXT("/Niagara/Modules/Update/Forces/%s.%s"), *ModuleType, *ModuleType));
		if (!IsValid(ModuleScript))
		{
			ModuleScript = LoadObject<UNiagaraScript>(nullptr, *FString::Printf(TEXT("/Niagara/Modules/Spawn/Velocity/%s.%s"), *ModuleType, *ModuleType));
		}
		if (!IsValid(ModuleScript))
		{
			ModuleScript = LoadObject<UNiagaraScript>(nullptr, *FString::Printf(TEXT("/Niagara/Modules/Collision/%s.%s"), *ModuleType, *ModuleType));
		}
	}
	else if (!IsValid(ModuleScript) && ModuleType.StartsWith(TEXT("/")))
	{
		// Try appending leaf name if missing
		FString LeafName = FPackageName::GetShortName(ModuleType);
		if (!ModuleType.Contains(TEXT(".")))
		{
			FString DotPath = FString::Printf(TEXT("%s.%s"), *ModuleType, *LeafName);
			ModuleScript = LoadObject<UNiagaraScript>(nullptr, *DotPath);
		}
	}

	if (!IsValid(ModuleScript))
	{
		// Query Asset Registry for any UNiagaraScript asset matching ModuleType (or leaf name)
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> ScriptAssets;
		AssetRegistryModule.Get().GetAssetsByClass(UNiagaraScript::StaticClass()->GetClassPathName(), ScriptAssets, true);

		FString SearchName = ModuleType.StartsWith(TEXT("/")) ? FPackageName::GetShortName(ModuleType) : ModuleType;
		for (const FAssetData& AssetData : ScriptAssets)
		{
			if (AssetData.AssetName.ToString().Equals(SearchName, ESearchCase::IgnoreCase))
			{
				ModuleScript = Cast<UNiagaraScript>(AssetData.GetAsset());
				if (IsValid(ModuleScript))
				{
					ModulePath = AssetData.GetObjectPathString();
					break;
				}
			}
		}
	}

	if (!IsValid(ModuleScript))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara Script Module not found for '%s' (resolved path: %s)"), *ModuleType, *ModulePath));
		return Result;
	}

	int32 TargetIndex = INDEX_NONE;
	if (Params->HasField(TEXT("target_index")))
	{
		TargetIndex = (int32)Params->GetNumberField(TEXT("target_index"));
	}
	else if (Params->HasField(TEXT("TargetIndex")))
	{
		TargetIndex = (int32)Params->GetNumberField(TEXT("TargetIndex"));
	}

	FString SuggestedName;
	Params->TryGetStringField(TEXT("suggested_name"), SuggestedName);

	System->Modify();
	Graph->Modify();

	FString ModuleName = SuggestedName.IsEmpty() ? ModuleScript->GetName() : SuggestedName;
	FGuid VersionGuid = ModuleScript->IsVersioningEnabled() ? ModuleScript->GetExposedVersion().VersionGuid : FGuid();

	UNiagaraNodeFunctionCall* NewNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
		ModuleScript,
		*OutputNode,
		TargetIndex,
		ModuleName,
		VersionGuid);
	if (!IsValid(NewNode))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to wire module '%s' into stack graph for phase '%s'"), *ModuleType, *Phase));
		return Result;
	}

	Graph->NotifyGraphChanged();
	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully added module '%s' to phase '%s' on %s"), *ModuleScript->GetName(), *Phase, bIsSystemPhase ? TEXT("System") : *EmitterName);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Graph editing is only supported in the Editor."));
#endif
	return Result;
}

#if WITH_EDITOR
static void CleanOverridePinConnectedNodes(UEdGraphPin* Pin)
{
	if (!Pin || Pin->LinkedTo.Num() == 0) return;
	for (int32 i = Pin->LinkedTo.Num() - 1; i >= 0; --i)
	{
		UEdGraphPin* LinkedPin = Pin->LinkedTo[i];
		if (!LinkedPin) continue;
		UEdGraphNode* ConnectedNode = LinkedPin->GetOwningNode();
		if (!ConnectedNode) continue;

		FString ClassName = ConnectedNode->GetClass()->GetName();
		if (ClassName.Contains(TEXT("ParameterMapGet")) || ConnectedNode->IsA<UNiagaraNodeInput>() || ConnectedNode->IsA<UNiagaraNodeFunctionCall>())
		{
			bool bOnlyUsedByThisPin = true;
			for (UEdGraphPin* NodePin : ConnectedNode->Pins)
			{
				if (NodePin && NodePin->Direction == EGPD_Output)
				{
					for (UEdGraphPin* Consumer : NodePin->LinkedTo)
					{
						if (Consumer && Consumer != Pin)
						{
							bOnlyUsedByThisPin = false;
							break;
						}
					}
				}
				if (!bOnlyUsedByThisPin) break;
			}

			if (bOnlyUsedByThisPin)
			{
				ConnectedNode->BreakAllNodeLinks();
				if (UEdGraph* Graph = ConnectedNode->GetGraph())
				{
					Graph->RemoveNode(ConnectedNode);
				}
			}
		}
	}
	Pin->BreakAllPinLinks(true);
}

static UEdGraphPin* FindStackOverridePin(UNiagaraNodeFunctionCall* FunctionCallNode, const FNiagaraParameterHandle& AliasedHandle)
{
	if (!FunctionCallNode) return nullptr;
	if (UNiagaraGraph* CalledGraph = FunctionCallNode->GetCalledGraph())
	{
		for (const FNiagaraVariable& InputVar : CalledGraph->FindStaticSwitchInputs())
		{
			if (InputVar.GetName() == AliasedHandle.GetName())
			{
				for (UEdGraphPin* SwitchPin : FunctionCallNode->Pins)
				{
					if (SwitchPin && SwitchPin->Direction == EGPD_Input && SwitchPin->PinName == InputVar.GetName())
					{
						return SwitchPin;
					}
				}
			}
		}
	}
	const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(FunctionCallNode->GetSchema());
	for (UEdGraphPin* Pin : FunctionCallNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Schema->PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			if (Pin->LinkedTo.Num() == 1 && Pin->LinkedTo[0])
			{
				UEdGraphNode* OverrideNode = Pin->LinkedTo[0]->GetOwningNode();
				for (UEdGraphPin* OverridePin : OverrideNode->Pins)
				{
					if (OverridePin && OverridePin->Direction == EGPD_Input)
					{
						FName HandleName = AliasedHandle.GetParameterHandleString();
						FString HandleStr = HandleName.ToString();
						FString BareName = AliasedHandle.GetName().ToString();
						FString OverridePinName = OverridePin->PinName.ToString();
						if (OverridePin->PinName == HandleName ||
							OverridePinName.Equals(HandleStr, ESearchCase::IgnoreCase) ||
							OverridePinName.EndsWith(FString::Printf(TEXT(".%s"), *BareName), ESearchCase::IgnoreCase) ||
							OverridePinName.Equals(BareName, ESearchCase::IgnoreCase))
						{
							return OverridePin;
						}
					}
				}
			}
			break;
		}
	}
	return nullptr;
}

static int32 PruneOrphanedInputNodes(UEdGraph* Graph)
{
	if (!Graph) return 0;
	int32 PrunedCount = 0;
	for (int32 i = Graph->Nodes.Num() - 1; i >= 0; --i)
	{
		UEdGraphNode* Node = Graph->Nodes[i];
		if (!IsValid(Node) || !Node->IsA<UNiagaraNodeInput>()) continue;

		bool bHasActiveOutputs = false;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
			{
				bHasActiveOutputs = true;
				break;
			}
		}

		if (!bHasActiveOutputs)
		{
			Node->BreakAllNodeLinks();
			Graph->RemoveNode(Node);
			PrunedCount++;
		}
	}
	return PrunedCount;
}

static UClass* ResolveDataInterfaceClass(const FString& InClassName)
{
	if (InClassName.IsEmpty()) return nullptr;
	FString CleanClassName = InClassName;
	if (CleanClassName.StartsWith(TEXT("U")))
	{
		CleanClassName = CleanClassName.RightChop(1);
	}

	UClass* DIClass = LoadObject<UClass>(nullptr, *InClassName);
	if (!IsValid(DIClass))
	{
		DIClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Niagara.%s"), *CleanClassName));
	}
	if (!IsValid(DIClass))
	{
		DIClass = FindFirstObject<UClass>(*CleanClassName, EFindFirstObjectOptions::NativeFirst);
	}
	if (!IsValid(DIClass))
	{
		DIClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *CleanClassName), EFindFirstObjectOptions::NativeFirst);
	}
	if (IsValid(DIClass) && DIClass->IsChildOf(UNiagaraDataInterface::StaticClass()))
	{
		return DIClass;
	}
	return nullptr;
}

static TSharedPtr<FJsonValue> SerializeCurveKeysToJson(UNiagaraDataInterfaceCurveBase* CurveDI)
{
	if (!IsValid(CurveDI)) return nullptr;

	TArray<UNiagaraDataInterfaceCurveBase::FCurveData> CurveData;
	CurveDI->GetCurveData(CurveData);

	if (CurveData.Num() == 1)
	{
		// Single-channel curve (NiagaraDataInterfaceCurve): flat array of {time, value}
		TArray<TSharedPtr<FJsonValue>> KeysArray;
		const FRichCurve* RC = CurveData[0].Curve;
		if (RC)
		{
			for (const FRichCurveKey& Key : RC->GetConstRefOfKeys())
			{
				TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
				KeyObj->SetNumberField(TEXT("time"), Key.Time);
				KeyObj->SetNumberField(TEXT("value"), Key.Value);
				KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
			}
		}
		return MakeShared<FJsonValueArray>(KeysArray);
	}

	// Multi-channel curve: object with channel_name -> [{time, value}, ...]
	TSharedPtr<FJsonObject> ChannelsObj = MakeShared<FJsonObject>();
	for (const auto& CD : CurveData)
	{
		TArray<TSharedPtr<FJsonValue>> KeysArray;
		if (CD.Curve)
		{
			for (const FRichCurveKey& Key : CD.Curve->GetConstRefOfKeys())
			{
				TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
				KeyObj->SetNumberField(TEXT("time"), Key.Time);
				KeyObj->SetNumberField(TEXT("value"), Key.Value);
				KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
			}
		}
		ChannelsObj->SetArrayField(CD.Name.ToString(), KeysArray);
	}
	return MakeShared<FJsonValueObject>(ChannelsObj);
}

static bool ApplyCurveKeysFromJson(
	UNiagaraDataInterfaceCurveBase* CurveDI,
	const TSharedPtr<FJsonValue>& CurveKeysValue,
	TArray<FString>& OutWarnings)
{
	if (!IsValid(CurveDI) || !CurveKeysValue.IsValid()) return false;

	TArray<UNiagaraDataInterfaceCurveBase::FCurveData> CurveData;
	CurveDI->GetCurveData(CurveData);

	if (CurveData.Num() == 0)
	{
		OutWarnings.Add(TEXT("Curve DI has no curve channels."));
		return false;
	}

	auto ParseKeysArray = [](const TArray<TSharedPtr<FJsonValue>>& InArray, TArray<FRichCurveKey>& OutKeys) -> bool
	{
		OutKeys.Reset();
		for (const auto& Elem : InArray)
		{
			const TSharedPtr<FJsonObject>* KeyObjPtr = nullptr;
			if (Elem.IsValid() && Elem->TryGetObject(KeyObjPtr) && KeyObjPtr && (*KeyObjPtr).IsValid())
			{
				double T = 0, V = 0;
				bool bHasTime = (*KeyObjPtr)->TryGetNumberField(TEXT("time"), T) || (*KeyObjPtr)->TryGetNumberField(TEXT("Time"), T);
				bool bHasVal = (*KeyObjPtr)->TryGetNumberField(TEXT("value"), V) || (*KeyObjPtr)->TryGetNumberField(TEXT("Value"), V);
				if (bHasTime && bHasVal)
				{
					FRichCurveKey NewKey((float)T, (float)V);
					OutKeys.Add(NewKey);
				}
			}
		}
		return OutKeys.Num() > 0;
	};

	CurveDI->Modify();

	int32 ChannelsUpdated = 0;

	if (CurveKeysValue->Type == EJson::Array)
	{
		TArray<FRichCurveKey> ParsedKeys;
		if (ParseKeysArray(CurveKeysValue->AsArray(), ParsedKeys))
		{
			// Apply parsed keys to all channels in CurveData (1 channel for float curve, or uniform across all channels for vector/color curve)
			for (auto& CD : CurveData)
			{
				FRichCurve* RC = CD.Curve;
				if (!RC) continue;
				RC->Reset();
				for (const FRichCurveKey& K : ParsedKeys)
				{
					RC->AddKey(K.Time, K.Value);
				}
				ChannelsUpdated++;
			}
		}
		else
		{
			OutWarnings.Add(TEXT("No valid keyframes with numerical time and value fields could be parsed from curve_keys array."));
			return false;
		}
	}
	else if (CurveKeysValue->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>& ChannelsObj = CurveKeysValue->AsObject();
		for (auto& CD : CurveData)
		{
			TArray<FString> CandidateNames = { CD.Name.ToString() };
			if (CD.Name == TEXT("Red"))   CandidateNames.Add(TEXT("R"));
			if (CD.Name == TEXT("Green")) CandidateNames.Add(TEXT("G"));
			if (CD.Name == TEXT("Blue"))  CandidateNames.Add(TEXT("B"));
			if (CD.Name == TEXT("Alpha")) CandidateNames.Add(TEXT("A"));
			if (CD.Name == TEXT("X"))     CandidateNames.Add(TEXT("x"));
			if (CD.Name == TEXT("Y"))     CandidateNames.Add(TEXT("y"));
			if (CD.Name == TEXT("Z"))     CandidateNames.Add(TEXT("z"));
			if (CD.Name == TEXT("W"))     CandidateNames.Add(TEXT("w"));

			const TArray<TSharedPtr<FJsonValue>>* ChannelKeys = nullptr;
			for (const FString& NameCandidate : CandidateNames)
			{
				if (ChannelsObj->TryGetArrayField(NameCandidate, ChannelKeys) && ChannelKeys)
				{
					break;
				}
			}

			if (ChannelKeys)
			{
				TArray<FRichCurveKey> ParsedKeys;
				if (ParseKeysArray(*ChannelKeys, ParsedKeys))
				{
					FRichCurve* RC = CD.Curve;
					if (!RC) continue;
					RC->Reset();
					for (const FRichCurveKey& K : ParsedKeys)
					{
						RC->AddKey(K.Time, K.Value);
					}
					ChannelsUpdated++;
				}
			}
		}

		if (ChannelsUpdated == 0)
		{
			OutWarnings.Add(TEXT("No recognized channel names (e.g. Red/R, Green/G, Blue/B, Alpha/A, X, Y, Z) with valid keyframes were found in curve_keys object."));
			return false;
		}
	}
	else
	{
		OutWarnings.Add(TEXT("curve_keys must be an array (single-channel or uniform multi-channel) or object (channel-specific)."));
		return false;
	}

#if WITH_EDITORONLY_DATA
	CurveDI->CurveAsset = nullptr;
#endif

	CurveDI->UpdateTimeRanges();
#if WITH_EDITORONLY_DATA
	CurveDI->UpdateLUT();
#endif
	return true;
}

static bool AssignAssetToDataInterface(UNiagaraDataInterface* DataInterface, const FString& BoundAssetPath, const FString& TargetPropName, TArray<FString>& OutWarnings)
{
	if (!IsValid(DataInterface) || BoundAssetPath.IsEmpty()) return false;

	UObject* BoundAsset = LoadObject<UObject>(nullptr, *BoundAssetPath);
	if (!IsValid(BoundAsset))
	{
		OutWarnings.Add(FString::Printf(TEXT("Asset '%s' could not be loaded to bind to data interface '%s'."), *BoundAssetPath, *DataInterface->GetName()));
		return false;
	}

	UClass* DIClass = DataInterface->GetClass();

	if (!TargetPropName.IsEmpty())
	{
		FProperty* Prop = DIClass->FindPropertyByName(FName(*TargetPropName));
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			if (BoundAsset->IsA(ObjProp->PropertyClass))
			{
				ObjProp->SetObjectPropertyValue_InContainer(DataInterface, BoundAsset);
				return true;
			}
			else
			{
				OutWarnings.Add(FString::Printf(TEXT("Asset '%s' (class '%s') is incompatible with specified property '%s' (expects '%s')."),
					*BoundAssetPath, *BoundAsset->GetClass()->GetName(), *TargetPropName, *ObjProp->PropertyClass->GetName()));
			}
		}
		else
		{
			OutWarnings.Add(FString::Printf(TEXT("Property '%s' not found on data interface class '%s'."), *TargetPropName, *DIClass->GetName()));
		}
		return false;
	}

	// Heuristic matching for common asset properties
	static const TArray<FName> CommonAssetPropNames = {
		FName(TEXT("DataChannelAsset")),
		FName(TEXT("Channel")),
		FName(TEXT("Mesh")),
		FName(TEXT("StaticMesh")),
		FName(TEXT("DefaultMesh")),
		FName(TEXT("Texture")),
		FName(TEXT("Source"))
	};

	for (const FName& CandidateName : CommonAssetPropNames)
	{
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(DIClass->FindPropertyByName(CandidateName)))
		{
			if (BoundAsset->IsA(ObjProp->PropertyClass))
			{
				ObjProp->SetObjectPropertyValue_InContainer(DataInterface, BoundAsset);
				return true;
			}
		}
	}

	// Fallback to first compatible non-transient Object property
	for (TFieldIterator<FObjectProperty> PropIt(DIClass); PropIt; ++PropIt)
	{
		FObjectProperty* ObjProp = *PropIt;
		if (ObjProp && !ObjProp->HasAnyPropertyFlags(CPF_Transient) && BoundAsset->IsA(ObjProp->PropertyClass))
		{
			ObjProp->SetObjectPropertyValue_InContainer(DataInterface, BoundAsset);
			return true;
		}
	}

	OutWarnings.Add(FString::Printf(TEXT("No compatible property found on data interface '%s' for asset '%s' (class '%s')."),
		*DIClass->GetName(), *BoundAssetPath, *BoundAsset->GetClass()->GetName()));
	return false;
}
#endif

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteSetModulePin(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}
	FString EmitterName, Phase, ModuleType, PinName, Value;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("phase"), Phase, Result.Errors, true) ||
		!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("module_type"), ModuleType, Result.Errors, true) ||
		!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("pin_name"), PinName, Result.Errors, true))
	{
		return Result;
	}

	FString LinkParam;
	Params->TryGetStringField(TEXT("link_parameter"), LinkParam);
	if (LinkParam.IsEmpty()) Params->TryGetStringField(TEXT("LinkParameter"), LinkParam);

	const TSharedPtr<FJsonValue>* ValueField = Params->Values.Find(TEXT("value"));
	if (!ValueField || !(*ValueField).IsValid()) ValueField = Params->Values.Find(TEXT("Value"));
	if (ValueField && (*ValueField).IsValid())
	{
		Value = NiagaraFormatJsonValueToUnrealText(*ValueField);
	}

	FString InterfaceClassName;
	Params->TryGetStringField(TEXT("interface_class"), InterfaceClassName);
	if (InterfaceClassName.IsEmpty()) Params->TryGetStringField(TEXT("data_interface_class"), InterfaceClassName);
	if (InterfaceClassName.IsEmpty()) Params->TryGetStringField(TEXT("DataInterfaceClass"), InterfaceClassName);

	FString AssetPath;
	Params->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty()) Params->TryGetStringField(TEXT("AssetPath"), AssetPath);

	FString AssetPropName;
	Params->TryGetStringField(TEXT("asset_property_name"), AssetPropName);
	if (AssetPropName.IsEmpty()) Params->TryGetStringField(TEXT("AssetPropertyName"), AssetPropName);

	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (Params->HasTypedField<EJson::Object>(TEXT("properties")))
	{
		PropertiesObj = &Params->GetObjectField(TEXT("properties"));
	}
	else if (Params->HasTypedField<EJson::Object>(TEXT("Properties")))
	{
		PropertiesObj = &Params->GetObjectField(TEXT("Properties"));
	}

	TSharedPtr<FJsonValue> CurveKeysValue;
	if (Params->HasField(TEXT("curve_keys")))
	{
		CurveKeysValue = Params->TryGetField(TEXT("curve_keys"));
	}
	else if (Params->HasField(TEXT("CurveKeys")))
	{
		CurveKeysValue = Params->TryGetField(TEXT("CurveKeys"));
	}

	FString DynamicInput;
	Params->TryGetStringField(TEXT("dynamic_input"), DynamicInput);
	if (DynamicInput.IsEmpty()) Params->TryGetStringField(TEXT("DynamicInput"), DynamicInput);

	const bool bHasValue = !Value.IsEmpty() || !LinkParam.IsEmpty() || !AssetPath.IsEmpty() || !InterfaceClassName.IsEmpty() || (PropertiesObj && PropertiesObj->IsValid()) || (CurveKeysValue.IsValid() && !CurveKeysValue->IsNull()) || !DynamicInput.IsEmpty();
	if (!bHasValue)
	{
		Result.Errors.Add(TEXT("Either 'value', 'link_parameter', 'asset_path', 'interface_class', 'properties', 'curve_keys', or 'dynamic_input' must be provided for set_niagara_module_pin."));
		return Result;
	}

	if (Value.IsEmpty() && !AssetPath.IsEmpty())
	{
		Value = AssetPath;
	}

	const bool bIsSystemPhase = (Phase == TEXT("SystemSpawn") || Phase == TEXT("SystemUpdate"));
	if (!bIsSystemPhase)
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, Result.Errors, true))
		{
			return Result;
		}
	}
	else
	{
		Params->TryGetStringField(TEXT("emitter_name"), EmitterName);
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	UNiagaraGraph* Graph = nullptr;
	UNiagaraNodeOutput* OutputNode = nullptr;
	FString FindError;
	if (!ResolvePhaseContext(System, EmitterName, Phase, Graph, OutputNode, FindError))
	{
		Result.Errors.Add(FindError);
		return Result;
	}

	// Search function call nodes in graph
	TArray<UNiagaraNodeFunctionCall*> StackNodes;
	Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(StackNodes);

	UNiagaraNodeFunctionCall* TargetNode = nullptr;
	FString ModuleLeafName = FPackageName::GetShortName(ModuleType);

	int32 TargetModuleIndex = INDEX_NONE;
	Params->TryGetNumberField(TEXT("module_index"), TargetModuleIndex);

	FString NodeGuidStr;
	Params->TryGetStringField(TEXT("node_guid"), NodeGuidStr);

	int32 MatchCount = 0;
	for (UNiagaraNodeFunctionCall* Node : StackNodes)
	{
		if (!IsValid(Node)) continue;

		if (!NodeGuidStr.IsEmpty() && Node->NodeGuid.ToString().Equals(NodeGuidStr, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}

		bool bMatch = false;
		if (Node->GetFunctionName().Equals(ModuleType, ESearchCase::IgnoreCase) ||
			Node->GetFunctionName().Equals(ModuleLeafName, ESearchCase::IgnoreCase))
		{
			bMatch = true;
		}
		else if (Node->FunctionScript && (
			Node->FunctionScript->GetName().Equals(ModuleType, ESearchCase::IgnoreCase) ||
			Node->FunctionScript->GetName().Equals(ModuleLeafName, ESearchCase::IgnoreCase) ||
			Node->FunctionScript->GetPathName().Contains(ModuleType)))
		{
			bMatch = true;
		}

		if (bMatch)
		{
			if (TargetModuleIndex == INDEX_NONE || MatchCount == TargetModuleIndex)
			{
				TargetNode = Node;
				break;
			}
			MatchCount++;
		}
	}

	if (!TargetNode)
	{
		Result.Errors.Add(FString::Printf(TEXT("Module '%s' not found in phase '%s'"), *ModuleType, *Phase));
		return Result;
	}

	// Normalize pin name: strip "Module." or "<FunctionName>." or script name prefix
	FString BarePinName = PinName;
	if (BarePinName.StartsWith(TEXT("Module.")))
	{
		BarePinName = BarePinName.RightChop(7);
	}
	else if (BarePinName.StartsWith(TargetNode->GetFunctionName() + TEXT(".")))
	{
		BarePinName = BarePinName.RightChop(TargetNode->GetFunctionName().Len() + 1);
	}
	else if (TargetNode->FunctionScript && BarePinName.StartsWith(TargetNode->FunctionScript->GetName() + TEXT(".")))
	{
		BarePinName = BarePinName.RightChop(TargetNode->FunctionScript->GetName().Len() + 1);
	}

	UEdGraphPin* TargetPin = nullptr;

	// 1. Check if it's a static switch on the function call node
	UNiagaraGraph* CalledGraph = TargetNode->GetCalledGraph();
	if (!CalledGraph && TargetNode->FunctionScript)
	{
		if (UNiagaraScriptSourceBase* Src = TargetNode->FunctionScript->GetLatestSource())
		{
			if (UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(Src))
			{
				CalledGraph = ScriptSource->NodeGraph;
			}
		}
	}

	if (CalledGraph)
	{
		for (const FNiagaraVariable& InputVar : CalledGraph->FindStaticSwitchInputs())
		{
			FString SwitchName = InputVar.GetName().ToString();
			if (SwitchName.Equals(BarePinName, ESearchCase::IgnoreCase) || SwitchName.Equals(PinName, ESearchCase::IgnoreCase))
			{
				for (UEdGraphPin* Pin : TargetNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Input && (Pin->PinName == InputVar.GetName() || Pin->PinName.ToString().Equals(SwitchName, ESearchCase::IgnoreCase)))
					{
						TargetPin = Pin;
						break;
					}
				}
				if (TargetPin) break;
			}
		}
	}

	// 2. Check if it's a direct pin on the function call node
	if (!TargetPin)
	{
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && (
				Pin->PinName.ToString().Equals(BarePinName, ESearchCase::IgnoreCase) ||
				Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) ||
				Pin->PinName.ToString().EndsWith(FString::Printf(TEXT(".%s"), *BarePinName))))
			{
				TargetPin = Pin;
				break;
			}
		}
	}

	// 3. Locate or create stack override pin on the preceding parameter map set node
	if (!TargetPin)
	{
		FNiagaraParameterHandle ModuleParamHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*BarePinName));
		FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(ModuleParamHandle, TargetNode);

		// 3-pre: First check if an override pin already exists on the parameter map set node (e.g. duplicated emitter)
		UEdGraphPin* ExistingOverridePin = FindStackOverridePin(TargetNode, AliasedHandle);
		if (ExistingOverridePin)
		{
			TargetPin = ExistingOverridePin;
		}

		if (!TargetPin)
		{
			FNiagaraTypeDefinition InputType;
			FGuid InputGuid;
			bool bFoundInput = false;

			// 3a. Search CalledGraph GetAllMetaData (without restrictive Module. namespace filter)
			if (CalledGraph)
			{
				for (const auto& Pair : CalledGraph->GetAllMetaData())
				{
					const FNiagaraVariable& InputVar = Pair.Key;
					FString InputVarName = InputVar.GetName().ToString();
					FNiagaraParameterHandle Handle(InputVar.GetName());
					FString BaseName = Handle.GetName().ToString();

					if (BaseName.Equals(BarePinName, ESearchCase::IgnoreCase) ||
						InputVarName.Equals(BarePinName, ESearchCase::IgnoreCase) ||
						InputVarName.Equals(PinName, ESearchCase::IgnoreCase) ||
						InputVarName.EndsWith(FString::Printf(TEXT(".%s"), *BarePinName), ESearchCase::IgnoreCase))
					{
						InputType = InputVar.GetType();
						if (Pair.Value)
						{
							InputGuid = Pair.Value->Metadata.GetVariableGuid();
						}
						bFoundInput = true;
						break;
					}

					if (Pair.Value)
					{
						for (const FName& AltName : Pair.Value->Metadata.AlternateAliases)
						{
							if (AltName.ToString().Equals(BarePinName, ESearchCase::IgnoreCase) ||
								AltName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
							{
								InputType = InputVar.GetType();
								InputGuid = Pair.Value->Metadata.GetVariableGuid();
								bFoundInput = true;
								break;
							}
						}
						if (bFoundInput) break;
					}
				}
			}

			// 3b. Search CalledGraph Nodes directly for UNiagaraNodeInput (DLL-safe iteration)
			if (!bFoundInput && CalledGraph)
			{
				for (UEdGraphNode* Node : CalledGraph->Nodes)
				{
					if (UNiagaraNodeInput* NodeInput = Cast<UNiagaraNodeInput>(Node))
					{
						if (NodeInput->Usage == ENiagaraInputNodeUsage::Parameter)
						{
							const FNiagaraVariable& InVar = NodeInput->Input;
							FString InVarName = InVar.GetName().ToString();
							FNiagaraParameterHandle InHandle(InVar.GetName());
							FString InBaseName = InHandle.GetName().ToString();

							if (InBaseName.Equals(BarePinName, ESearchCase::IgnoreCase) ||
								InVarName.Equals(BarePinName, ESearchCase::IgnoreCase) ||
								InVarName.Equals(PinName, ESearchCase::IgnoreCase) ||
								InVarName.EndsWith(FString::Printf(TEXT(".%s"), *BarePinName), ESearchCase::IgnoreCase))
							{
								InputType = InVar.GetType();
								TOptional<FNiagaraVariableMetaData> Meta = CalledGraph->GetMetaData(InVar);
								if (Meta.IsSet() && Meta->GetVariableGuid().IsValid())
								{
									InputGuid = Meta->GetVariableGuid();
								}
								bFoundInput = true;
								break;
							}
						}
					}
				}
			}

			// 3c. Search GetStackFunctionInputs (AllInputs)
			if (!bFoundInput)
			{
				FCompileConstantResolver ConstantResolver;
				if (!bIsSystemPhase)
				{
					FNiagaraEmitterHandle* HandlePtr = nullptr;
					for (FNiagaraEmitterHandle& H : System->GetEmitterHandles())
					{
						if (H.GetName().ToString() == EmitterName) { HandlePtr = &H; break; }
					}
					if (HandlePtr && HandlePtr->GetInstance().Emitter)
					{
						ConstantResolver = FCompileConstantResolver(HandlePtr->GetInstance(), OutputNode ? OutputNode->GetUsage() : ENiagaraScriptUsage::ParticleUpdateScript);
					}
				}
				else
				{
					ConstantResolver = FCompileConstantResolver(System, OutputNode ? OutputNode->GetUsage() : ENiagaraScriptUsage::SystemUpdateScript);
				}

				TArray<FNiagaraVariable> StackInputs;
				FNiagaraStackGraphUtilities::GetStackFunctionInputs(*TargetNode, StackInputs, ConstantResolver, FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::AllInputs);
				for (const FNiagaraVariable& Var : StackInputs)
				{
					FNiagaraParameterHandle Handle(Var.GetName());
					if (Handle.GetName().ToString().Equals(BarePinName, ESearchCase::IgnoreCase) ||
						Var.GetName().ToString().Equals(BarePinName, ESearchCase::IgnoreCase) ||
						Var.GetName().ToString().Equals(PinName, ESearchCase::IgnoreCase) ||
						Var.GetName().ToString().EndsWith(FString::Printf(TEXT(".%s"), *BarePinName), ESearchCase::IgnoreCase))
					{
						InputType = Var.GetType();
						if (CalledGraph)
						{
							TOptional<FNiagaraVariableMetaData> Meta = CalledGraph->GetMetaData(Var);
							if (Meta.IsSet() && Meta->GetVariableGuid().IsValid())
							{
								InputGuid = Meta->GetVariableGuid();
							}
						}
						bFoundInput = true;
						break;
					}
				}
			}

			// 3d. Search static switches
			if (!bFoundInput && CalledGraph)
			{
				for (const FNiagaraVariable& SwitchVar : CalledGraph->FindStaticSwitchInputs())
				{
					if (SwitchVar.GetName().ToString().Equals(BarePinName, ESearchCase::IgnoreCase) ||
						SwitchVar.GetName().ToString().Equals(PinName, ESearchCase::IgnoreCase))
					{
						InputType = SwitchVar.GetType();
						bFoundInput = true;
						break;
					}
				}
			}

			// 3e. Fallback from explicit interface_class parameter if provided
			if (!bFoundInput && !InterfaceClassName.IsEmpty())
			{
				UClass* DIClass = ResolveDataInterfaceClass(InterfaceClassName);
				if (IsValid(DIClass) && DIClass->IsChildOf(UNiagaraDataInterface::StaticClass()))
				{
					InputType = FNiagaraTypeDefinition(DIClass);
					bFoundInput = true;
				}
			}

			// 3f. Fallback from asset_path type deduction
			if (!bFoundInput && !AssetPath.IsEmpty())
			{
				UObject* LoadedAsset = LoadObject<UObject>(nullptr, *AssetPath);
				if (LoadedAsset)
				{
					UClass* DIClass = nullptr;
					if (LoadedAsset->IsA(UNiagaraDataChannelAsset::StaticClass()))
					{
						FString FuncName = TargetNode->GetFunctionName();
						if (FuncName.Contains(TEXT("Write"), ESearchCase::IgnoreCase))
						{
							DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceDataChannelWrite"));
						}
						else
						{
							DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceDataChannelRead"));
						}
					}
					else if (LoadedAsset->IsA(UStaticMesh::StaticClass()))
					{
						DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceStaticMesh"));
					}
					else if (LoadedAsset->IsA(UTexture::StaticClass()))
					{
						DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceTexture"));
					}

					if (DIClass)
					{
						InputType = FNiagaraTypeDefinition(DIClass);
						bFoundInput = true;
					}
				}
			}

			// 3g. Fallback from link_parameter type if available
			if (!bFoundInput && !LinkParam.IsEmpty())
			{
				FNiagaraUserRedirectionParameterStore& UserStore = System->GetExposedParameters();
				TArray<FNiagaraVariable> UserVars;
				UserStore.GetUserParameters(UserVars);
				for (const FNiagaraVariable& UVar : UserVars)
				{
					if (UVar.GetName().ToString().Equals(LinkParam, ESearchCase::IgnoreCase) ||
						FString::Printf(TEXT("User.%s"), *UVar.GetName().ToString()).Equals(LinkParam, ESearchCase::IgnoreCase))
					{
						InputType = UVar.GetType();
						bFoundInput = true;
						break;
					}
				}
			}

			// 3h. Fallback from curve_keys if provided
			if (!bFoundInput && CurveKeysValue.IsValid() && !CurveKeysValue->IsNull())
			{
				UClass* DIClass = nullptr;
				if (!InterfaceClassName.IsEmpty())
				{
					DIClass = ResolveDataInterfaceClass(InterfaceClassName);
				}
				else if (CurveKeysValue->Type == EJson::Array)
				{
					DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceCurve"));
				}
				else if (CurveKeysValue->Type == EJson::Object)
				{
					DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceColorCurve"));
				}

				if (DIClass)
				{
					InputType = FNiagaraTypeDefinition(DIClass);
					bFoundInput = true;
				}
			}

			if (bFoundInput && InputType.IsValid())
			{
				TargetPin = &FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
					*TargetNode,
					AliasedHandle,
					InputType,
					InputGuid,
					FGuid());
			}
		}
	}

	if (!TargetPin)
	{
		Result.Errors.Add(FString::Printf(TEXT("Pin '%s' not found on module '%s' in phase '%s'"), *PinName, *ModuleType, *Phase));
		return Result;
	}

	System->Modify();
	TargetPin->GetOwningNode()->Modify();

	if (TargetPin->GetOwningNode() == TargetNode)
	{
		// Direct Pin / Static Switch on TargetNode
		if (!LinkParam.IsEmpty())
		{
			Result.Errors.Add(FString::Printf(TEXT("Pin '%s' on '%s' is a static switch or direct node pin and cannot be dynamically linked to parameter '%s'."), *PinName, *ModuleType, *LinkParam));
			return Result;
		}

		FString FormattedValue = Value;
		if (UEnum* Enum = Cast<UEnum>(TargetPin->PinType.PinSubCategoryObject.Get()))
		{
			FString ResolvedEnumValue;
			bool bFoundEnum = false;
			TArray<FString> ValidNames;
			TArray<FString> ValidDisplayNames;

			for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
			{
				FString EnumName = Enum->GetNameStringByIndex(i);
				FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
				ValidNames.Add(EnumName);
				ValidDisplayNames.Add(DisplayName);

				if (Value.Equals(EnumName, ESearchCase::IgnoreCase) ||
					Value.Equals(DisplayName, ESearchCase::IgnoreCase))
				{
					ResolvedEnumValue = EnumName;
					bFoundEnum = true;
					break;
				}

				if (EnumName.Contains(TEXT("::")))
				{
					FString ShortName = EnumName.RightChop(EnumName.Find(TEXT("::"), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 2);
					if (Value.Equals(ShortName, ESearchCase::IgnoreCase))
					{
						ResolvedEnumValue = EnumName;
						bFoundEnum = true;
						break;
					}
				}
			}

			if (!bFoundEnum && Value.IsNumeric())
			{
				int32 IntVal = FCString::Atoi(*Value);
				if (IntVal >= 0 && IntVal < Enum->NumEnums() - 1)
				{
					ResolvedEnumValue = Enum->GetNameStringByIndex(IntVal);
					bFoundEnum = true;
				}
			}

			if (bFoundEnum)
			{
				FormattedValue = ResolvedEnumValue;
			}
			else
			{
				Result.Errors.Add(FString::Printf(TEXT("Invalid value '%s' for static switch enum pin '%s'. Valid options: [%s] (or display names: [%s])."),
					*Value, *PinName, *FString::Join(ValidNames, TEXT(", ")), *FString::Join(ValidDisplayNames, TEXT(", "))));
				return Result;
			}
		}

		if (FormattedValue.Contains(TEXT(",")) && !FormattedValue.StartsWith(TEXT("(")) && !FormattedValue.EndsWith(TEXT(")")))
		{
			FormattedValue = FString::Printf(TEXT("(%s)"), *FormattedValue);
		}
		TargetPin->DefaultValue = FormattedValue;
		TargetNode->MarkNodeRequiresSynchronization(TEXT("Static switch value modified"), true);
	}
	else
	{
		// Stack override pin on parameter map set node
		const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
		FNiagaraTypeDefinition InputType = NiagaraSchema ? NiagaraSchema->PinToTypeDefinition(TargetPin) : FNiagaraTypeDefinition::GetFloatDef();

		if (!DynamicInput.IsEmpty())
		{
			CleanOverridePinConnectedNodes(TargetPin);

			FString DynamicInputPath = DynamicInput;
			if (!DynamicInputPath.StartsWith(TEXT("/")))
			{
				FString PotentialPath = FString::Printf(TEXT("/Niagara/Modules/Expressions/%s.%s"), *DynamicInput, *DynamicInput);
				if (LoadObject<UNiagaraScript>(nullptr, *PotentialPath))
				{
					DynamicInputPath = PotentialPath;
				}
				else
				{
					PotentialPath = FString::Printf(TEXT("/Niagara/Modules/Math/%s.%s"), *DynamicInput, *DynamicInput);
					if (LoadObject<UNiagaraScript>(nullptr, *PotentialPath))
					{
						DynamicInputPath = PotentialPath;
					}
					else
					{
						PotentialPath = FString::Printf(TEXT("/Niagara/Modules/%s.%s"), *DynamicInput, *DynamicInput);
						if (LoadObject<UNiagaraScript>(nullptr, *PotentialPath))
						{
							DynamicInputPath = PotentialPath;
						}
					}
				}
			}

			UNiagaraScript* DynamicInputScript = LoadObject<UNiagaraScript>(nullptr, *DynamicInputPath);
			if (!IsValid(DynamicInputScript))
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				TArray<FAssetData> ScriptAssets;
				AssetRegistryModule.Get().GetAssetsByClass(UNiagaraScript::StaticClass()->GetClassPathName(), ScriptAssets, true);
				for (const FAssetData& AssetData : ScriptAssets)
				{
					if (AssetData.AssetName.ToString().Equals(DynamicInput, ESearchCase::IgnoreCase))
					{
						DynamicInputScript = Cast<UNiagaraScript>(AssetData.GetAsset());
						break;
					}
				}
			}

			if (!IsValid(DynamicInputScript))
			{
				Result.Errors.Add(FString::Printf(TEXT("Could not find Niagara dynamic input script for '%s'"), *DynamicInput));
				return Result;
			}

			UNiagaraNodeFunctionCall* OutDynamicNode = nullptr;
			FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(
				*TargetPin,
				DynamicInputScript,
				OutDynamicNode);

			if (!IsValid(OutDynamicNode))
			{
				Result.Errors.Add(FString::Printf(TEXT("Failed to attach dynamic input '%s' to pin '%s'"), *DynamicInput, *PinName));
				return Result;
			}

			if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(TargetPin->GetOwningNode()))
			{
				OwningNode->MarkNodeRequiresSynchronization(TEXT("OverridePin DynamicInput Changed"), true);
			}
			TargetNode->MarkNodeRequiresSynchronization(TEXT("OverridePin DynamicInput Changed"), true);
		}
		else if (!LinkParam.IsEmpty())
		{
			FString ResolvedLinkParam = LinkParam;
			if (!ResolvedLinkParam.Contains(TEXT(".")))
			{
				FString PotentialUserParam = FString::Printf(TEXT("User.%s"), *ResolvedLinkParam);
				TArray<FNiagaraVariable> ExistingUserVars;
				System->GetExposedParameters().GetUserParameters(ExistingUserVars);
				for (const FNiagaraVariable& UV : ExistingUserVars)
				{
					if (UV.GetName().ToString().Equals(PotentialUserParam, ESearchCase::IgnoreCase) ||
						UV.GetName().ToString().Equals(ResolvedLinkParam, ESearchCase::IgnoreCase))
					{
						ResolvedLinkParam = PotentialUserParam;
						break;
					}
				}
			}

			CleanOverridePinConnectedNodes(TargetPin);

			FNiagaraVariableBase LinkedVar(InputType, FName(*ResolvedLinkParam));
			TSet<FNiagaraVariableBase> KnownParameters;
			TArray<FNiagaraVariable> UserParams;
			System->GetExposedParameters().GetUserParameters(UserParams);
			for (FNiagaraVariable& UVar : UserParams)
			{
				FNiagaraUserRedirectionParameterStore::MakeUserVariable(UVar);
				KnownParameters.Add(UVar);
			}
			FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(*TargetPin, LinkedVar, KnownParameters);
		}
		else if (InputType.IsDataInterface() || !InterfaceClassName.IsEmpty() || !AssetPath.IsEmpty() || (CurveKeysValue.IsValid() && !CurveKeysValue->IsNull()))
		{
			UClass* DIClass = nullptr;
			if (!InterfaceClassName.IsEmpty())
			{
				DIClass = ResolveDataInterfaceClass(InterfaceClassName);
			}
			else if (InputType.IsDataInterface())
			{
				DIClass = InputType.GetClass();
			}
			else if (!AssetPath.IsEmpty())
			{
				UObject* LoadedAsset = LoadObject<UObject>(nullptr, *AssetPath);
				if (LoadedAsset && LoadedAsset->IsA(UNiagaraDataChannelAsset::StaticClass()))
				{
					FString FuncName = TargetNode->GetFunctionName();
					if (FuncName.Contains(TEXT("Write"), ESearchCase::IgnoreCase))
					{
						DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceDataChannelWrite"));
					}
					else
					{
						DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceDataChannelRead"));
					}
				}
				else if (LoadedAsset && LoadedAsset->IsA(UStaticMesh::StaticClass()))
				{
					DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceStaticMesh"));
				}
				else if (LoadedAsset && LoadedAsset->IsA(UTexture::StaticClass()))
				{
					DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceTexture"));
				}
			}
			else if (CurveKeysValue.IsValid() && !CurveKeysValue->IsNull())
			{
				if (InputType.IsValid() && !InputType.IsDataInterface() && InterfaceClassName.IsEmpty())
				{
					Result.Errors.Add(FString::Printf(TEXT("Pin '%s' on module '%s' has type '%s' which is not a Data Interface. curve_keys can only be applied to curve Data Interface pins (e.g. 'Uniform Curve Sprite Scale' or 'Linear Color Curve')."),
						*PinName, *ModuleType, *InputType.GetName()));
					return Result;
				}

				if (CurveKeysValue->Type == EJson::Array)
				{
					DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceCurve"));
				}
				else if (CurveKeysValue->Type == EJson::Object)
				{
					DIClass = ResolveDataInterfaceClass(TEXT("NiagaraDataInterfaceColorCurve"));
				}
			}

			if (!IsValid(DIClass) || !DIClass->IsChildOf(UNiagaraDataInterface::StaticClass()))
			{
				Result.Errors.Add(FString::Printf(TEXT("Could not resolve valid UNiagaraDataInterface subclass for pin '%s' (type: '%s', requested class: '%s')"),
					*PinName, *InputType.GetName(), *InterfaceClassName));
				return Result;
			}

			CleanOverridePinConnectedNodes(TargetPin);

			UNiagaraDataInterface* OutDataObject = nullptr;
			FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
				*TargetPin,
				DIClass,
				TargetPin->PinName.ToString(),
				OutDataObject);

			if (!IsValid(OutDataObject))
			{
				Result.Errors.Add(FString::Printf(TEXT("Failed to instantiate data interface of class '%s' for pin '%s'"), *DIClass->GetName(), *PinName));
				return Result;
			}

			FString BoundAssetPath = AssetPath;
			if (BoundAssetPath.IsEmpty() && !Value.IsEmpty())
			{
				if (Value.StartsWith(TEXT("/")) || Value.Contains(TEXT("'")))
				{
					BoundAssetPath = Value;
				}
			}

			if (!BoundAssetPath.IsEmpty())
			{
				AssignAssetToDataInterface(OutDataObject, BoundAssetPath, AssetPropName, Result.Warnings);
			}

			if (PropertiesObj && PropertiesObj->IsValid())
			{
				ApplyPropertiesFromJsonObject(OutDataObject, PropertiesObj->ToSharedRef(), Result);
			}

			if (CurveKeysValue.IsValid() && !CurveKeysValue->IsNull())
			{
				if (UNiagaraDataInterfaceCurveBase* CurveDI = Cast<UNiagaraDataInterfaceCurveBase>(OutDataObject))
				{
					ApplyCurveKeysFromJson(CurveDI, CurveKeysValue, Result.Warnings);
				}
				else
				{
					Result.Warnings.Add(FString::Printf(TEXT("curve_keys was provided but DI '%s' is not a curve data interface."), *OutDataObject->GetClass()->GetName()));
				}
			}

			OutDataObject->PostEditChange();

			if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(TargetPin->GetOwningNode()))
			{
				OwningNode->MarkNodeRequiresSynchronization(TEXT("OverridePin DI Value Changed"), true);
			}
			TargetNode->MarkNodeRequiresSynchronization(TEXT("OverridePin DI Value Changed"), true);
		}
		else if (InputType.IsUObject())
		{
			CleanOverridePinConnectedNodes(TargetPin);

			FString ObjectAssetPath = AssetPath.IsEmpty() ? Value : AssetPath;
			UObject* ObjectAsset = LoadObject<UObject>(nullptr, *ObjectAssetPath);
			if (!IsValid(ObjectAsset))
			{
				Result.Errors.Add(FString::Printf(TEXT("Object asset not found at path '%s' for pin '%s'"), *ObjectAssetPath, *PinName));
				return Result;
			}

			FNiagaraStackGraphUtilities::SetObjectAssetValueForFunctionInput(
				*TargetPin,
				InputType.GetClass(),
				TargetPin->PinName.ToString(),
				ObjectAsset);

			if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(TargetPin->GetOwningNode()))
			{
				OwningNode->MarkNodeRequiresSynchronization(TEXT("OverridePin ObjectAsset Changed"), true);
			}
			TargetNode->MarkNodeRequiresSynchronization(TEXT("OverridePin ObjectAsset Changed"), true);
		}
		else
		{
			CleanOverridePinConnectedNodes(TargetPin);

			FString FormattedValue = Value;
			if (FormattedValue.Contains(TEXT(",")) && !FormattedValue.StartsWith(TEXT("(")) && !FormattedValue.EndsWith(TEXT(")")))
			{
				FormattedValue = FString::Printf(TEXT("(%s)"), *FormattedValue);
			}
			TargetPin->DefaultValue = FormattedValue;
			if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(TargetPin->GetOwningNode()))
			{
				OwningNode->MarkNodeRequiresSynchronization(TEXT("OverridePin Default Value Changed"), true);
			}
		}
	}

	// Check for inert pin warnings (e.g. Color Mode is Unset or Lifetime Mode mismatch)
	for (UEdGraphPin* NodePin : TargetNode->Pins)
	{
		if (NodePin && NodePin->Direction == EGPD_Input && NodePin->PinType.PinSubCategoryObject.IsValid())
		{
			FString SwitchPinName = NodePin->PinName.ToString();
			if (SwitchPinName.Contains(TEXT("Mode")) || SwitchPinName.Contains(TEXT("Switch")))
			{
				FString SwitchVal = NodePin->DefaultValue;
				if (PinName.Contains(TEXT("Color")) && SwitchPinName.Contains(TEXT("Color")) && SwitchVal.Contains(TEXT("Unset"), ESearchCase::IgnoreCase))
				{
					Result.Warnings.Add(FString::Printf(TEXT("Advisory: Pin '%s' was set, but static switch '%s' on module '%s' is currently '%s'. The value will not take effect until '%s' is changed."),
						*PinName, *SwitchPinName, *ModuleType, *SwitchVal, *SwitchPinName));
				}
				else if (PinName.Contains(TEXT("Lifetime")) && SwitchPinName.Contains(TEXT("Lifetime")) && SwitchVal.Contains(TEXT("Random"), ESearchCase::IgnoreCase) && !PinName.Contains(TEXT("Min")) && !PinName.Contains(TEXT("Max")))
				{
					Result.Warnings.Add(FString::Printf(TEXT("Advisory: Pin '%s' was set, but static switch '%s' on module '%s' is currently '%s'. Pin may be gated off."),
						*PinName, *SwitchPinName, *ModuleType, *SwitchVal));
				}
			}
		}
	}

	PruneOrphanedInputNodes(Graph);
	TargetPin->GetOwningNode()->GetGraph()->NotifyGraphChanged();
	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully configured pin '%s' on module '%s' in phase '%s'"), *PinName, *ModuleType, *Phase);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Graph editing is only supported in the Editor."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteResetModulePin(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	FString EmitterName, Phase, ModuleType, PinName;
	Params->TryGetStringField(TEXT("emitter_name"), EmitterName);
	if (EmitterName.IsEmpty()) Params->TryGetStringField(TEXT("EmitterName"), EmitterName);
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("phase"), Phase, Result.Errors, true);
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("module_type"), ModuleType, Result.Errors, true);
	if (ModuleType.IsEmpty()) Params->TryGetStringField(TEXT("module_name"), ModuleType);
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("pin_name"), PinName, Result.Errors, true);

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	UNiagaraGraph* Graph = nullptr;
	UNiagaraNodeOutput* OutputNode = nullptr;
	FString ContextError;
	if (!ResolvePhaseContext(System, EmitterName, Phase, Graph, OutputNode, ContextError))
	{
		Result.Errors.Add(ContextError);
		return Result;
	}

	UNiagaraNodeFunctionCall* TargetNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			if (FuncNode->FunctionScript &&
				(FuncNode->FunctionScript->GetName().Equals(ModuleType, ESearchCase::IgnoreCase) ||
				 FuncNode->GetFunctionName().Equals(ModuleType, ESearchCase::IgnoreCase)))
			{
				TargetNode = FuncNode;
				break;
			}
		}
	}

	if (!IsValid(TargetNode))
	{
		Result.Errors.Add(FString::Printf(TEXT("Module '%s' not found in phase '%s' graph"), *ModuleType, *Phase));
		return Result;
	}

	FNiagaraParameterHandle InputHandle(*PinName);
	FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, TargetNode);

	UEdGraphPin* OverridePin = FindStackOverridePin(TargetNode, AliasedHandle);
	if (!OverridePin)
	{
		const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(TargetNode->GetSchema());
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Schema->PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
			{
				if (Pin->LinkedTo.Num() == 1 && Pin->LinkedTo[0])
				{
					UEdGraphNode* OverrideNode = Pin->LinkedTo[0]->GetOwningNode();
					for (UEdGraphPin* CandidatePin : OverrideNode->Pins)
					{
						if (CandidatePin && CandidatePin->Direction == EGPD_Input)
						{
							FString CandName = CandidatePin->PinName.ToString();
							if (CandName.Equals(AliasedHandle.GetParameterHandleString().ToString(), ESearchCase::IgnoreCase) ||
								CandName.EndsWith(FString::Printf(TEXT(".%s"), *PinName), ESearchCase::IgnoreCase))
							{
								OverridePin = CandidatePin;
								break;
							}
						}
					}
				}
				break;
			}
		}
	}

	System->Modify();
	Graph->Modify();

	bool bPinReset = false;
	if (OverridePin)
	{
		if (OverridePin->GetOwningNode() == TargetNode)
		{
			OverridePin->DefaultValue = OverridePin->AutogeneratedDefaultValue;
			TargetNode->MarkNodeRequiresSynchronization(TEXT("Reset static switch"), true);
			bPinReset = true;
		}
		else
		{
			CleanOverridePinConnectedNodes(OverridePin);
			UEdGraphNode* OwningNode = OverridePin->GetOwningNode();
			OwningNode->Modify();
			OwningNode->RemovePin(OverridePin);
			if (UNiagaraNode* NiagaraOwningNode = Cast<UNiagaraNode>(OwningNode))
			{
				NiagaraOwningNode->MarkNodeRequiresSynchronization(TEXT("Reset override pin"), true);
			}
			TargetNode->MarkNodeRequiresSynchronization(TEXT("Reset override pin"), true);
			bPinReset = true;
		}
	}

	bool bCleanOrphans = true;
	if (Params->HasField(TEXT("clean_orphaned_nodes")))
	{
		bCleanOrphans = Params->GetBoolField(TEXT("clean_orphaned_nodes"));
	}
	int32 Pruned = 0;
	if (bCleanOrphans)
	{
		Pruned = PruneOrphanedInputNodes(Graph);
	}

	Graph->NotifyGraphChanged();
	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}

	if (bPinReset)
	{
		Result.ResultMessage = FString::Printf(TEXT("Successfully reset pin '%s' on module '%s' in phase '%s' to default (pruned %d orphaned nodes)."),
			*PinName, *ModuleType, *Phase, Pruned);
	}
	else
	{
		Result.ResultMessage = FString::Printf(TEXT("Pin '%s' on module '%s' in phase '%s' had no active override and was already at default (pruned %d orphaned nodes)."),
			*PinName, *ModuleType, *Phase, Pruned);
	}
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Graph editing is only supported in the Editor."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteCompileSystem(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	// Systems assembled by add_niagara_emitter before it rebuilt emitter nodes have handles whose emitter scripts
	// are not part of the compiled system. Rebuild the nodes here so a compile repairs them, and say so.
	const int32 HandlesWithoutNodes = CountEmitterHandlesWithoutSystemNodes(*System);
	if (HandlesWithoutNodes > 0)
	{
		FString RebuildError;
		if (!RebuildSystemEmitterNodes(*System, RebuildError))
		{
			Result.Errors.Add(RebuildError);
			return Result;
		}
	}

	System->RequestCompile(true);
	Result.bSuccess = WaitAndReportCompile(System, Result);

	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
		Result.ModifiedAssets.Add(SystemPath);
	}

	if (HandlesWithoutNodes > 0)
	{
		Result.ResultMessage += FString::Printf(
			TEXT(" Rebuilt the system graph's emitter nodes: %d emitter handle(s) had none, so their emitter scripts were not compiled into the system until now."),
			HandlesWithoutNodes);
	}
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteCaptureIsolated(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}
	double DurationSeconds = 2.0;
	UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("duration_seconds"), DurationSeconds, Result.Errors, false);
	int32 MaxDimension = 512;
	UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("max_dimension"), MaxDimension, Result.Errors, false);

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	UWorld* World = nullptr;
	if (GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}
	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("Active Editor World Context not found."));
		return Result;
	}

	// 1. Spawn Transient Niagara Actor and assign System
	//
	// Spawned far from the origin, deliberately outside any placed level content. See the PrimitiveRenderMode
	// comment further down: any per-primitive Hidden/ShowOnly filter list broke the capture in this project's
	// editor world (see that comment for the full elimination sequence), so isolation here is done by geometry
	// instead of by filtering - nothing else in the level is anywhere near this location.
	const FVector IsolationOrigin(500000.0, 500000.0, 50000.0);

	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags = RF_Transient;
	ANiagaraActor* NiagaraActor = World->SpawnActor<ANiagaraActor>(ANiagaraActor::StaticClass(), IsolationOrigin, FRotator::ZeroRotator, SpawnParams);
	if (!IsValid(NiagaraActor) || !IsValid(NiagaraActor->GetNiagaraComponent()))
	{
		Result.Errors.Add(TEXT("Failed to spawn transient Niagara Actor."));
		return Result;
	}

	UNiagaraComponent* Component = NiagaraActor->GetNiagaraComponent();
	Component->SetAsset(System);

	// Defensive: stop scalability culling swapping in a CullProxy, whose bounds report at the origin in place of
	// the real simulated instance.
	Component->SetAllowScalability(false);

	// Force the script compile to finish BEFORE activating. ActivateInternal gates on Asset->IsReadyToRun(); while
	// compilation is outstanding it silently sets bAwaitingActivationDueToNotReady and returns WITHOUT creating a
	// SystemInstanceController. With no instance, AdvanceSimulation() - guarded by SystemInstanceController.IsValid()
	// - is a permanent no-op, so nothing simulates or renders and CalcBounds() falls back to its 1x1x1 box. That was
	// the "black image, 2x2x2 bounds" defect: activation was attempted before this wait, and the engine's deferred
	// retry never arrived because this function never ticks the world.
	System->WaitForCompilationComplete(true, false);

	Component->Activate(true);

	// Verify activation actually produced a live instance instead of assuming it did. The wait above already forced
	// the compile synchronously, so a handful of retries covers any residual initialization latency.
	{
		constexpr int32 MaxActivationRetries = 5;
		for (int32 RetryIndex = 0; RetryIndex < MaxActivationRetries && !Component->GetSystemInstanceController().IsValid(); ++RetryIndex)
		{
			Component->Activate(true);
		}
	}

	if (!Component->GetSystemInstanceController().IsValid())
	{
		if (IsValid(NiagaraActor))
		{
			World->DestroyActor(NiagaraActor);
		}
		Result.Errors.Add(FString::Printf(TEXT("Niagara system '%s' failed to activate: no system instance controller was created (the system may not be ready to run). Aborting rather than returning a black image."), *SystemPath));
		return Result;
	}

	// 2. Spawn Transient Scene Capture Actor and configure
	ASceneCapture2D* CaptureActor = World->SpawnActor<ASceneCapture2D>(ASceneCapture2D::StaticClass(), IsolationOrigin + FVector(0, -300, 100), FRotator(0, 90, 0), SpawnParams);
	USceneCaptureComponent2D* CaptureComponent = IsValid(CaptureActor) ? CaptureActor->GetCaptureComponent2D() : nullptr;
	if (!IsValid(CaptureComponent))
	{
		if (IsValid(NiagaraActor))
		{
			World->DestroyActor(NiagaraActor);
		}
		Result.Errors.Add(TEXT("Failed to spawn transient Scene Capture 2D Actor."));
		return Result;
	}

	// Create Transient Render Target
	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(World);
	if (!IsValid(RenderTarget))
	{
		if (IsValid(NiagaraActor)) World->DestroyActor(NiagaraActor);
		if (IsValid(CaptureActor)) World->DestroyActor(CaptureActor);
		Result.Errors.Add(TEXT("Failed to create transient RenderTarget."));
		return Result;
	}
	RenderTarget->InitAutoFormat(MaxDimension / 2, MaxDimension / 2); // each slice is half dimensions
	RenderTarget->ClearColor = FLinearColor(0.12f, 0.12f, 0.12f, 1.0f); // neutral dark gray

	CaptureComponent->TextureTarget = RenderTarget;

	// PRM_UseShowOnlyList + ShowOnlyComponents.Add(Component) was the actual cause of the black image, and it had
	// nothing to do with Niagara. Confirmed empirically (see BUGREPORT_capture_niagara_system_isolated.md follow-up,
	// 2026-09-13): a transient UPrimitiveComponent spawned in this same call - Niagara or a plain
	// UStaticMeshComponent, doesn't matter - never appears in a PRM_UseShowOnlyList capture of this editor world,
	// even after looping CaptureScene() ten times, and even after waiting a full real editor tick between spawning
	// it and capturing it in a separate tool call. A component already resident in the level before this call (e.g.
	// a placed StaticMeshActor) captures correctly through the identical ShowOnlyComponents path.
	//
	// The natural fix - keep PRM_RenderScenePrimitives and hide every pre-existing actor via HiddenActors instead
	// of trying to show-only the new one - was tried and is WORSE: hiding even a single harmless pre-existing actor
	// (in this project, L_MainMenu's "SkySphere" static mesh) blacked out the ENTIRE capture, including the
	// Niagara component that was never on the hidden list. So HiddenComponents/HiddenActors is not a safe
	// substitute for ShowOnlyComponents here either - something in this engine/project's primitive-filtering
	// resolution (Hidden or ShowOnly, doesn't matter which) is unreliable in this editor world, and the only
	// combination proven to render correctly, repeatedly, is PRM_RenderScenePrimitives with BOTH lists empty.
	//
	// So this capture is isolated by geometry, not by a filter list: spawned at IsolationOrigin, far outside any
	// placed level content (see above), with the scene rendered unfiltered. Atmosphere and fog are turned off so
	// the distant sky doesn't dominate the frame - RenderTarget->ClearColor (set above) is NOT what shows through
	// once they're gone, despite the name: this capture path clears to transparent black regardless of that
	// property, so the backdrop is black, not neutral gray. Good enough to see the particles; a truly clean/neutral
	// backdrop would need a dedicated preview scene (see FNiagaraBakerRenderer) rather than the shared editor world.
	CaptureComponent->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	CaptureComponent->ShowFlags.SetAtmosphere(false);
	CaptureComponent->ShowFlags.SetFog(false);

	// Drive the capture manually, exactly once per keyframe. Left at its default, bCaptureEveryFrame has the render
	// thread capturing continuously, and the engine warns "Scene capture with bCaptureEveryFrame enabled was told to
	// update - major inefficiency" on every explicit CaptureScene() call - the automatic capture races the manual one
	// and can sample before the advanced simulation has been pushed to the proxy. FNiagaraBakerRenderer disables both
	// of these for the same reason, and sets visibility explicitly because CaptureScene() early-outs when not visible.
	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->bAlwaysPersistRenderingState = true;
	CaptureComponent->SetVisibility(true);

	// Frame the camera from MEASURED bounds. This previously read Component->Bounds immediately after Activate(),
	// i.e. before any simulation had run, so it always saw the unpopulated fallback box. Run a throwaway pass over
	// the full capture window to populate the simulation, measure the real particle extent, and let the existing
	// ResetSystem() below rewind to t=0 before the keyframes are captured.
	if (DurationSeconds > 0.0)
	{
		Component->AdvanceSimulation(FMath::Max(1, FMath::RoundToInt(DurationSeconds * 60.0)), 1.0f / 60.0f);
	}

	// Niagara writes particle bounds in PostSystemTick_GameThread (reached via FinalizeTick_GameThread) and
	// pushes render dynamic data to the scene proxy through the world's end-of-frame updates. AdvanceSimulation
	// does neither on its own, so without this flush the simulation advances but nothing reaches the renderer or
	// the bounds - captures come back black with fallback bounds even when activation succeeded. This mirrors
	// what FNiagaraBakerRenderer does after each of its own advances.
	World->SendAllEndOfFrameUpdates();
	if (FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World))
	{
		WorldManager->FlushComputeAndDeferredQueues(false);
	}

	FBoxSphereBounds Bounds = Component->CalcBounds(Component->GetComponentTransform());

	// Floor the box to a minimum extent, mirroring the +/-100 unit padding the Niagara system viewport applies, so a
	// small effect is not framed absurdly tight and a degenerate bound cannot place the camera inside the particles.
	FVector FlooredExtent = Bounds.BoxExtent;
	FlooredExtent.X = FMath::Max(FlooredExtent.X, 100.0f);
	FlooredExtent.Y = FMath::Max(FlooredExtent.Y, 100.0f);
	FlooredExtent.Z = FMath::Max(FlooredExtent.Z, 100.0f);
	Bounds = FBoxSphereBounds(Bounds.Origin, FlooredExtent, FlooredExtent.Size());

	float MaxBoundsSize = FMath::Max3(Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z);

	// Engine thumbnail framing math (ThumbnailHelpers.cpp): distance = (SphereRadius * 1.15) / tan(HalfFOV), with a
	// hard minimum. The camera is placed once and held for all four quadrants, so the timestamps read as one
	// evolution rather than four independently framed images.
	const float HalfFOVRadians = FMath::DegreesToRadians(CaptureComponent->FOVAngle) * 0.5f;
	const float MinCameraDistance = 48.0f;
	const float TargetDistance = FMath::Max((Bounds.SphereRadius * 1.15f) / FMath::Tan(HalfFOVRadians), MinCameraDistance);

	FVector CamPos = Bounds.Origin - FVector(0.0f, TargetDistance, 0.0f); // look from Front Y axis
	CaptureActor->SetActorLocation(CamPos);
	CaptureActor->SetActorRotation(FRotator(0, 90, 0)); // Rotated to look down Y axis

	// 3. Render 4 Chronological Keyframes (Top-Left, Top-Right, Bottom-Left, Bottom-Right)
	TArray<FColor> StitchedPixels;
	StitchedPixels.AddZeroed(MaxDimension * MaxDimension); // Grid buffer

	int32 SliceWidth = MaxDimension / 2;
	int32 SliceHeight = MaxDimension / 2;

	TArray<double> Times = { DurationSeconds * 0.1, DurationSeconds * 0.3, DurationSeconds * 0.6, DurationSeconds * 0.9 };

	Component->ResetSystem();
	double LastTime = 0.0f;

	for (int32 i = 0; i < 4; ++i)
	{
		double TargetTime = Times[i];
		double Step = TargetTime - LastTime;
		if (Step > 0.0)
		{
			Component->AdvanceSimulation(FMath::RoundToInt(Step * 60.0f), 1.0f / 60.0f);
		}
		LastTime = TargetTime;

		// Niagara writes particle bounds in PostSystemTick_GameThread (reached via FinalizeTick_GameThread) and
		// pushes render dynamic data to the scene proxy through the world's end-of-frame updates. AdvanceSimulation
		// does neither on its own, so without this flush the simulation advances but nothing reaches the renderer or
		// the bounds - captures come back black with fallback bounds even when activation succeeded. This mirrors
		// what FNiagaraBakerRenderer does after each of its own advances.
		World->SendAllEndOfFrameUpdates();
		if (FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World))
		{
			WorldManager->FlushComputeAndDeferredQueues(false);
		}

		// Force Scene Capture
		CaptureComponent->CaptureScene();

		// Read pixels from Render Target
		FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
		if (RTResource)
		{
			TArray<FColor> OutColor;
			RTResource->ReadPixels(OutColor);

			if (OutColor.Num() == SliceWidth * SliceHeight)
			{
				// Copy quadrant pixels to stitched buffer
				int32 QuadX = (i % 2) * SliceWidth;
				int32 QuadY = (i / 2) * SliceHeight;

				for (int32 y = 0; y < SliceHeight; ++y)
				{
					for (int32 x = 0; x < SliceWidth; ++x)
					{
						int32 DestX = QuadX + x;
						int32 DestY = QuadY + y;
						int32 DestIndex = DestY * MaxDimension + DestX;
						int32 SrcIndex = y * SliceWidth + x;

						StitchedPixels[DestIndex] = OutColor[SrcIndex];
					}
				}
			}
		}
	}

	// 4. Add scale bar visual reference (draw 1m baseline overlay in Bottom-Left quadrant)
	// 1 meter = 100 Unreal Units. Draw horizontal line.
	// Bounds are measured up front now, before the camera is placed, so there is deliberately no recompute here:
	// the framing and the bar derive from the same measurement and agree by construction. The camera sits at
	// TargetDistance with CaptureComponent->FOVAngle, so the visible half-width at the subject plane is
	// TargetDistance * tan(HalfFOV) - derive the bar from that, not from the bounds. A scale bar that overstates a
	// metre is worse than no scale bar at all, since judging physical size is its entire purpose.
	const float FrameHalfWidth = TargetDistance * FMath::Tan(HalfFOVRadians);
	float PixelsPerUnit = (float)SliceWidth / (FrameHalfWidth * 2.0f);
	int32 LineWidthPixels = FMath::Clamp(FMath::RoundToInt(100.0f * PixelsPerUnit), 10, SliceWidth - 20);

	int32 StartLineX = 20;
	int32 EndLineX = StartLineX + LineWidthPixels;
	int32 LineY = MaxDimension - 20; // 20 pixels from bottom margin

	for (int32 x = StartLineX; x <= EndLineX; ++x)
	{
		int32 DestIndex = LineY * MaxDimension + x;
		StitchedPixels[DestIndex] = FColor::White;
	}

	// Draw ticks at ends of scale line
	for (int32 tickY = LineY - 5; tickY <= LineY + 5; ++tickY)
	{
		int32 StartTickIndex = tickY * MaxDimension + StartLineX;
		int32 EndTickIndex = tickY * MaxDimension + EndLineX;
		
		StitchedPixels[StartTickIndex] = FColor::White;
		StitchedPixels[EndTickIndex]   = FColor::White;
	}

	// 5. Encode to JPEG and Save to Disk
	FString FilePath = FAgentFrameworkViewportActions::SavePixelsToDisk(StitchedPixels, MaxDimension, MaxDimension, MaxDimension, 90);

	// 6. Cleanup transient Actors and RenderTarget
	if (IsValid(NiagaraActor)) World->DestroyActor(NiagaraActor);
	if (IsValid(CaptureActor)) World->DestroyActor(CaptureActor);
	if (IsValid(RenderTarget)) RenderTarget->MarkAsGarbage();

	// 7. Populate metadata response
	TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
	if (!FilePath.IsEmpty())
	{
		ResponseObj->SetStringField(TEXT("image_path"), FilePath);
	}
	
	TArray<TSharedPtr<FJsonValue>> TimesArray;
	for (double T : Times) TimesArray.Add(MakeShared<FJsonValueNumber>(T));
	ResponseObj->SetArrayField(TEXT("quadrant_times"), TimesArray);

	TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
	BoundsObj->SetNumberField(TEXT("x"), Bounds.BoxExtent.X * 2.0);
	BoundsObj->SetNumberField(TEXT("y"), Bounds.BoxExtent.Y * 2.0);
	BoundsObj->SetNumberField(TEXT("z"), Bounds.BoxExtent.Z * 2.0);
	ResponseObj->SetObjectField(TEXT("bounds_cm"), BoundsObj);

	FString ResponseString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

	Result.bSuccess = true;
	Result.ResultMessage = ResponseString;
	return Result;
}

bool FAgentFrameworkNiagaraActions::ResolvePhaseContext(
	UNiagaraSystem* System,
	const FString& EmitterName,
	const FString& PhaseStr,
	UNiagaraGraph*& OutGraph,
	UNiagaraNodeOutput*& OutOutputNode,
	FString& OutError) const
{
#if WITH_EDITOR
	OutGraph = nullptr;
	OutOutputNode = nullptr;

	if (!IsValid(System))
	{
		OutError = TEXT("Niagara System pointer is invalid.");
		return false;
	}

	if (PhaseStr == TEXT("SystemSpawn") || PhaseStr == TEXT("SystemUpdate"))
	{
		const bool bIsSpawn = (PhaseStr == TEXT("SystemSpawn"));
		UNiagaraScript* TargetScript = bIsSpawn ? System->GetSystemSpawnScript() : System->GetSystemUpdateScript();
		if (!IsValid(TargetScript))
		{
			OutError = FString::Printf(TEXT("System script for phase %s not found."), *PhaseStr);
			return false;
		}

		UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(TargetScript->GetSource(TargetScript->GetExposedVersion().VersionGuid));
		if (!IsValid(ScriptSource) || !IsValid(ScriptSource->NodeGraph))
		{
			OutError = FString::Printf(TEXT("Niagara graph source missing for system phase %s."), *PhaseStr);
			return false;
		}

		OutGraph = ScriptSource->NodeGraph;
		const ENiagaraScriptUsage Usage = bIsSpawn ? ENiagaraScriptUsage::SystemSpawnScript : ENiagaraScriptUsage::SystemUpdateScript;
		OutOutputNode = OutGraph->FindEquivalentOutputNode(Usage, FGuid());
		if (!IsValid(OutOutputNode))
		{
			for (UEdGraphNode* Node : OutGraph->Nodes)
			{
				if (UNiagaraNodeOutput* NodeOut = Cast<UNiagaraNodeOutput>(Node))
				{
					if (NodeOut->GetUsage() == Usage)
					{
						OutOutputNode = NodeOut;
						break;
					}
				}
			}
		}
		if (!IsValid(OutOutputNode))
		{
			OutError = FString::Printf(TEXT("Output node for system phase %s not found in graph."), *PhaseStr);
			return false;
		}
		return true;
	}

	// Emitter-level phase
	if (EmitterName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Emitter name is required for emitter phase '%s'."), *PhaseStr);
		return false;
	}

	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString() == EmitterName)
		{
			TargetHandle = &Handle;
			break;
		}
	}

	if (!TargetHandle)
	{
		OutError = FString::Printf(TEXT("Emitter handle '%s' not found inside Niagara System."), *EmitterName);
		return false;
	}

	UNiagaraEmitter* Emitter = TargetHandle->GetInstance().Emitter;
	if (!IsValid(Emitter))
	{
		OutError = FString::Printf(TEXT("Underlying UNiagaraEmitter is null or invalid for emitter handle '%s'."), *EmitterName);
		return false;
	}

	FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
	if (!EmitterData)
	{
		OutError = FString::Printf(TEXT("Unable to retrieve EmitterData for emitter '%s'."), *EmitterName);
		return false;
	}

	UNiagaraScript* TargetScript = nullptr;
	ENiagaraScriptUsage Usage = ENiagaraScriptUsage::Function;

	if (PhaseStr == TEXT("EmitterSpawn"))
	{
#if WITH_EDITORONLY_DATA
		TargetScript = EmitterData->EmitterSpawnScriptProps.Script;
		Usage = ENiagaraScriptUsage::EmitterSpawnScript;
#endif
	}
	else if (PhaseStr == TEXT("EmitterUpdate"))
	{
#if WITH_EDITORONLY_DATA
		TargetScript = EmitterData->EmitterUpdateScriptProps.Script;
		Usage = ENiagaraScriptUsage::EmitterUpdateScript;
#endif
	}
	else if (PhaseStr == TEXT("ParticleSpawn"))
	{
		TargetScript = EmitterData->SpawnScriptProps.Script;
		Usage = ENiagaraScriptUsage::ParticleSpawnScript;
	}
	else if (PhaseStr == TEXT("ParticleUpdate"))
	{
		TargetScript = EmitterData->UpdateScriptProps.Script;
		Usage = ENiagaraScriptUsage::ParticleUpdateScript;
	}
	else if (PhaseStr == TEXT("ParticleEvent") || PhaseStr.StartsWith(TEXT("Event")))
	{
		if (EmitterData->GetEventHandlers().Num() > 0)
		{
			TargetScript = EmitterData->GetEventHandlers()[0].Script;
			Usage = ENiagaraScriptUsage::ParticleEventScript;
		}
		else
		{
			OutError = FString::Printf(TEXT("No event handler scripts configured on emitter '%s' to add module to."), *EmitterName);
			return false;
		}
	}
	else
	{
		OutError = FString::Printf(TEXT("Unrecognized phase '%s'. Valid phases: EmitterSpawn, EmitterUpdate, ParticleSpawn, ParticleUpdate, ParticleEvent, SystemSpawn, SystemUpdate."), *PhaseStr);
		return false;
	}

	if (!IsValid(TargetScript))
	{
		OutError = FString::Printf(TEXT("Niagara script for phase %s not found on emitter %s."), *PhaseStr, *EmitterName);
		return false;
	}

	UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(TargetScript->GetSource(TargetScript->GetExposedVersion().VersionGuid));
	if (!IsValid(ScriptSource) || !IsValid(ScriptSource->NodeGraph))
	{
		OutError = FString::Printf(TEXT("Niagara graph source missing for phase %s script on emitter %s."), *PhaseStr, *EmitterName);
		return false;
	}

	OutGraph = ScriptSource->NodeGraph;
	OutOutputNode = OutGraph->FindEquivalentOutputNode(Usage, FGuid());
	if (!IsValid(OutOutputNode))
	{
		for (UEdGraphNode* Node : OutGraph->Nodes)
		{
			if (UNiagaraNodeOutput* NodeOut = Cast<UNiagaraNodeOutput>(Node))
			{
				if (NodeOut->GetUsage() == Usage)
				{
					OutOutputNode = NodeOut;
					break;
				}
			}
		}
	}
	if (!IsValid(OutOutputNode))
	{
		OutError = FString::Printf(TEXT("Output node for phase %s on emitter %s not found in graph."), *PhaseStr, *EmitterName);
		return false;
	}
	return true;
#else
	OutError = TEXT("Graph retrieval is only supported in Editor builds.");
	return false;
#endif
}

bool FAgentFrameworkNiagaraActions::WaitAndReportCompile(UNiagaraSystem* System, FAgentFrameworkActionResult& Result) const
{
	if (!IsValid(System))
	{
		Result.Errors.Add(TEXT("Niagara System pointer is invalid for compilation check."));
		return false;
	}

	// Request compilation and wait synchronously for worker tasks to complete
	System->RequestCompile(true);
	System->WaitForCompilationComplete(true, false);

	// Extract compilation logs and performance metrics across System and Emitter scripts
	TArray<UNiagaraScript*> ActiveScripts;
	if (UNiagaraScript* SysSpawn = System->GetSystemSpawnScript()) ActiveScripts.Add(SysSpawn);
	if (UNiagaraScript* SysUpdate = System->GetSystemUpdateScript()) ActiveScripts.Add(SysUpdate);

	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
		if (!IsValid(Emitter)) continue;

		FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
		if (!EmitterData) continue;

		if (EmitterData->SpawnScriptProps.Script) ActiveScripts.Add(EmitterData->SpawnScriptProps.Script);
		if (EmitterData->UpdateScriptProps.Script) ActiveScripts.Add(EmitterData->UpdateScriptProps.Script);
#if WITH_EDITORONLY_DATA
		if (EmitterData->EmitterSpawnScriptProps.Script) ActiveScripts.Add(EmitterData->EmitterSpawnScriptProps.Script);
		if (EmitterData->EmitterUpdateScriptProps.Script) ActiveScripts.Add(EmitterData->EmitterUpdateScriptProps.Script);
#endif
		for (const FNiagaraEventScriptProperties& EventProp : EmitterData->GetEventHandlers())
		{
			if (EventProp.Script) ActiveScripts.Add(EventProp.Script);
		}
	}

	for (UNiagaraScript* Script : ActiveScripts)
	{
		if (!IsValid(Script)) continue;
		
#if WITH_EDITORONLY_DATA
		const FNiagaraVMExecutableData& VMData = Script->GetVMExecutableData();
		for (const FNiagaraCompileEvent& CompileEvent : VMData.LastCompileEvents)
		{
			FString Msg = FString::Printf(TEXT("[%s] %s: %s"),
				*Script->GetName(),
				CompileEvent.Severity == FNiagaraCompileEventSeverity::Error ? TEXT("ERROR") : TEXT("WARNING"),
				*CompileEvent.Message);

			if (CompileEvent.Severity == FNiagaraCompileEventSeverity::Error)
			{
				Result.Errors.Add(Msg);
			}
			else
			{
				Result.Warnings.Add(Msg);
			}
		}
#endif
	}

	// Check for standard performance optimization bottlenecks
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
		if (!IsValid(Emitter)) continue;

		FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
		if (EmitterData && EmitterData->SimTarget == ENiagaraSimTarget::CPUSim)
		{
			Result.Warnings.Add(FString::Printf(TEXT("Performance Warning: Emitter '%s' uses CPU Simulation. For AAA particle counts, consider changing SimTarget to GPU Simulation."), *Handle.GetName().ToString()));
		}
	}

	bool bCompileSuccess = (Result.Errors.Num() == 0);

	if (bCompileSuccess && Result.ResultMessage.IsEmpty())
	{
		Result.ResultMessage = FString::Printf(TEXT("Successfully compiled Niagara System '%s' (%d scripts checked, 0 errors%s)."),
			*System->GetPathName(), ActiveScripts.Num(), Result.Warnings.Num() > 0 ? *FString::Printf(TEXT(", %d warning(s)"), Result.Warnings.Num()) : TEXT(""));
	}

	if (!bCompileSuccess)
	{
		Result.Errors.Add(TEXT("Niagara System compiled with compilation errors. Review errors array."));
	}

	return bCompileSuccess;
}

void FAgentFrameworkNiagaraActions::PlaySuccessSound()
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

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteSetNiagaraParameter(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	// 1. Extract system path (supporting system_path, SystemAsset, asset_path)
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!Params->TryGetStringField(TEXT("SystemAsset"), SystemPath) || SystemPath.IsEmpty())
		{
			if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
			{
				return Result;
			}
		}
	}

	// 2. Extract ParameterScope (default "User")
	FString Scope = TEXT("User");
	if (Params->HasTypedField<EJson::String>(TEXT("parameter_scope")))
	{
		Scope = Params->GetStringField(TEXT("parameter_scope"));
	}
	else if (Params->HasTypedField<EJson::String>(TEXT("ParameterScope")))
	{
		Scope = Params->GetStringField(TEXT("ParameterScope"));
	}

	// 3. Extract ParameterName
	FString ParamName;
	if (Params->HasTypedField<EJson::String>(TEXT("parameter_name")))
	{
		ParamName = Params->GetStringField(TEXT("parameter_name"));
	}
	else if (Params->HasTypedField<EJson::String>(TEXT("ParameterName")))
	{
		ParamName = Params->GetStringField(TEXT("ParameterName"));
	}

	if (ParamName.IsEmpty())
	{
		Result.Errors.Add(TEXT("Parameter name is missing or empty."));
		return Result;
	}

	// 4. Extract DataType (default "Float")
	FString DataType = TEXT("Float");
	if (Params->HasTypedField<EJson::String>(TEXT("data_type")))
	{
		DataType = Params->GetStringField(TEXT("data_type"));
	}
	else if (Params->HasTypedField<EJson::String>(TEXT("DataType")))
	{
		DataType = Params->GetStringField(TEXT("DataType"));
	}

	// 5. Load UNiagaraSystem asset
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	// 6. Enforce User scope (Exposed Parameter Store requires User namespace)
	const bool bNonUserData = (!Scope.IsEmpty() && !Scope.Equals(TEXT("User"), ESearchCase::IgnoreCase)) ||
	                          (ParamName.Contains(TEXT(".")) && !ParamName.StartsWith(TEXT("User.")));
	if (bNonUserData)
	{
		FString BareName = FNiagaraParameterHandle(FName(*ParamName)).GetName().ToString();
		Result.Errors.Add(FString::Printf(
			TEXT("set_niagara_parameter only supports 'User.' parameter scope for exposed system parameters (received '%s'). ")
			TEXT("To configure an emitter or system module input, use set_niagara_module_pin directly, ")
			TEXT("or expose a User parameter (e.g. 'User.%s') and link it via set_niagara_module_pin with link_parameter='User.%s'."),
			*ParamName, *BareName, *BareName));
		return Result;
	}

	FString CleanParamName = ParamName;
	if (CleanParamName.StartsWith(TEXT("User.")))
	{
		CleanParamName = CleanParamName.RightChop(5);
	}

	FString FullParamName = FString::Printf(TEXT("User.%s"), *CleanParamName);

	// 7. Get Exposed Parameter Store
	FNiagaraUserRedirectionParameterStore& UserStore = System->GetExposedParameters();

	System->Modify();

	// 8. Handle Parameter Type
	if (DataType.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
	{
		float FloatVal = 0.0f;
		const TSharedPtr<FJsonValue>* ValueField = nullptr;
		if (Params->Values.Contains(TEXT("value"))) ValueField = &Params->Values[TEXT("value")];
		else if (Params->Values.Contains(TEXT("Value"))) ValueField = &Params->Values[TEXT("Value")];

		if (ValueField && ValueField->IsValid())
		{
			if ((*ValueField)->Type == EJson::Number) FloatVal = (float)(*ValueField)->AsNumber();
			else if ((*ValueField)->Type == EJson::String) FloatVal = FCString::Atof(*(*ValueField)->AsString());
			else if ((*ValueField)->Type == EJson::Boolean) FloatVal = (*ValueField)->AsBool() ? 1.0f : 0.0f;
		}

		FNiagaraTypeDefinition TypeDef = FNiagaraTypeDefinition::GetFloatDef();
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetParameterData((const uint8*)&FloatVal, Var);
	}
	else if (DataType.Equals(TEXT("Vector2"), ESearchCase::IgnoreCase))
	{
		FVector2f VecVal(0.0f, 0.0f);
		const TSharedPtr<FJsonValue>* ValueField = nullptr;
		if (Params->Values.Contains(TEXT("value"))) ValueField = &Params->Values[TEXT("value")];
		else if (Params->Values.Contains(TEXT("Value"))) ValueField = &Params->Values[TEXT("Value")];

		if (ValueField && ValueField->IsValid())
		{
			if ((*ValueField)->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Obj = (*ValueField)->AsObject();
				double X = 0.0, Y = 0.0;
				if (Obj->HasField(TEXT("x"))) X = Obj->GetNumberField(TEXT("x"));
				else if (Obj->HasField(TEXT("X"))) X = Obj->GetNumberField(TEXT("X"));
				if (Obj->HasField(TEXT("y"))) Y = Obj->GetNumberField(TEXT("y"));
				else if (Obj->HasField(TEXT("Y"))) Y = Obj->GetNumberField(TEXT("Y"));
				VecVal = FVector2f((float)X, (float)Y);
			}
			else if ((*ValueField)->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Arr = (*ValueField)->AsArray();
				float X = (Arr.Num() > 0) ? (float)Arr[0]->AsNumber() : 0.0f;
				float Y = (Arr.Num() > 1) ? (float)Arr[1]->AsNumber() : 0.0f;
				VecVal = FVector2f(X, Y);
			}
			else if ((*ValueField)->Type == EJson::String)
			{
				FString Str = (*ValueField)->AsString();
				TArray<FString> Parts;
				Str.ParseIntoArray(Parts, TEXT(","), true);
				if (Parts.Num() >= 2)
				{
					VecVal.X = FCString::Atof(*Parts[0]);
					VecVal.Y = FCString::Atof(*Parts[1]);
				}
				else
				{
					float Scalar = FCString::Atof(*Str);
					VecVal = FVector2f(Scalar, Scalar);
				}
			}
			else if ((*ValueField)->Type == EJson::Number)
			{
				float Scalar = (float)(*ValueField)->AsNumber();
				VecVal = FVector2f(Scalar, Scalar);
			}
		}

		FNiagaraTypeDefinition TypeDef = FNiagaraTypeDefinition::GetVec2Def();
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetParameterData((const uint8*)&VecVal, Var);
	}
	else if (DataType.Equals(TEXT("Vector3"), ESearchCase::IgnoreCase))
	{
		FVector3f VecVal(0.0f, 0.0f, 0.0f);
		const TSharedPtr<FJsonValue>* ValueField = nullptr;
		if (Params->Values.Contains(TEXT("value"))) ValueField = &Params->Values[TEXT("value")];
		else if (Params->Values.Contains(TEXT("Value"))) ValueField = &Params->Values[TEXT("Value")];

		if (ValueField && ValueField->IsValid())
		{
			if ((*ValueField)->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Obj = (*ValueField)->AsObject();
				double X = 0.0, Y = 0.0, Z = 0.0;
				if (Obj->HasField(TEXT("x"))) X = Obj->GetNumberField(TEXT("x"));
				else if (Obj->HasField(TEXT("X"))) X = Obj->GetNumberField(TEXT("X"));
				if (Obj->HasField(TEXT("y"))) Y = Obj->GetNumberField(TEXT("y"));
				else if (Obj->HasField(TEXT("Y"))) Y = Obj->GetNumberField(TEXT("Y"));
				if (Obj->HasField(TEXT("z"))) Z = Obj->GetNumberField(TEXT("z"));
				else if (Obj->HasField(TEXT("Z"))) Z = Obj->GetNumberField(TEXT("Z"));
				VecVal = FVector3f((float)X, (float)Y, (float)Z);
			}
			else if ((*ValueField)->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Arr = (*ValueField)->AsArray();
				float X = (Arr.Num() > 0) ? (float)Arr[0]->AsNumber() : 0.0f;
				float Y = (Arr.Num() > 1) ? (float)Arr[1]->AsNumber() : 0.0f;
				float Z = (Arr.Num() > 2) ? (float)Arr[2]->AsNumber() : 0.0f;
				VecVal = FVector3f(X, Y, Z);
			}
			else if ((*ValueField)->Type == EJson::String)
			{
				FString Str = (*ValueField)->AsString();
				TArray<FString> Parts;
				Str.ParseIntoArray(Parts, TEXT(","), true);
				if (Parts.Num() >= 3)
				{
					VecVal.X = FCString::Atof(*Parts[0]);
					VecVal.Y = FCString::Atof(*Parts[1]);
					VecVal.Z = FCString::Atof(*Parts[2]);
				}
				else
				{
					float Scalar = FCString::Atof(*Str);
					VecVal = FVector3f(Scalar, Scalar, Scalar);
				}
			}
			else if ((*ValueField)->Type == EJson::Number)
			{
				float Scalar = (float)(*ValueField)->AsNumber();
				VecVal = FVector3f(Scalar, Scalar, Scalar);
			}
		}

		FNiagaraTypeDefinition TypeDef = FNiagaraTypeDefinition::GetVec3Def();
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetParameterData((const uint8*)&VecVal, Var);
	}
	else if (DataType.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase))
	{
		FLinearColor ColorVal(0.0f, 0.0f, 0.0f, 1.0f);
		const TSharedPtr<FJsonValue>* ValueField = nullptr;
		if (Params->Values.Contains(TEXT("value"))) ValueField = &Params->Values[TEXT("value")];
		else if (Params->Values.Contains(TEXT("Value"))) ValueField = &Params->Values[TEXT("Value")];

		if (ValueField && ValueField->IsValid())
		{
			if ((*ValueField)->Type == EJson::Object)
			{
				TSharedPtr<FJsonObject> Obj = (*ValueField)->AsObject();
				double R = 0.0, G = 0.0, B = 0.0, A = 1.0;
				if (Obj->HasField(TEXT("r"))) R = Obj->GetNumberField(TEXT("r"));
				else if (Obj->HasField(TEXT("R"))) R = Obj->GetNumberField(TEXT("R"));
				if (Obj->HasField(TEXT("g"))) G = Obj->GetNumberField(TEXT("g"));
				else if (Obj->HasField(TEXT("G"))) G = Obj->GetNumberField(TEXT("G"));
				if (Obj->HasField(TEXT("b"))) B = Obj->GetNumberField(TEXT("b"));
				else if (Obj->HasField(TEXT("B"))) B = Obj->GetNumberField(TEXT("B"));
				if (Obj->HasField(TEXT("a"))) A = Obj->GetNumberField(TEXT("a"));
				else if (Obj->HasField(TEXT("A"))) A = Obj->GetNumberField(TEXT("A"));
				ColorVal = FLinearColor((float)R, (float)G, (float)B, (float)A);
			}
			else if ((*ValueField)->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Arr = (*ValueField)->AsArray();
				float R = (Arr.Num() > 0) ? (float)Arr[0]->AsNumber() : 0.0f;
				float G = (Arr.Num() > 1) ? (float)Arr[1]->AsNumber() : 0.0f;
				float B = (Arr.Num() > 2) ? (float)Arr[2]->AsNumber() : 0.0f;
				float A = (Arr.Num() > 3) ? (float)Arr[3]->AsNumber() : 1.0f;
				ColorVal = FLinearColor(R, G, B, A);
			}
			else if ((*ValueField)->Type == EJson::String)
			{
				FString Str = (*ValueField)->AsString();
				if (!ColorVal.InitFromString(Str))
				{
					TArray<FString> Parts;
					Str.ParseIntoArray(Parts, TEXT(","), true);
					if (Parts.Num() >= 3)
					{
						ColorVal.R = FCString::Atof(*Parts[0]);
						ColorVal.G = FCString::Atof(*Parts[1]);
						ColorVal.B = FCString::Atof(*Parts[2]);
						if (Parts.Num() >= 4) ColorVal.A = FCString::Atof(*Parts[3]);
					}
				}
			}
		}

		FNiagaraTypeDefinition TypeDef = FNiagaraTypeDefinition::GetColorDef();
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetParameterData((const uint8*)&ColorVal, Var);
	}
	else if (DataType.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
	{
		bool bBoolVal = false;
		const TSharedPtr<FJsonValue>* ValueField = nullptr;
		if (Params->Values.Contains(TEXT("value"))) ValueField = &Params->Values[TEXT("value")];
		else if (Params->Values.Contains(TEXT("Value"))) ValueField = &Params->Values[TEXT("Value")];

		if (ValueField && ValueField->IsValid())
		{
			if ((*ValueField)->Type == EJson::Boolean) bBoolVal = (*ValueField)->AsBool();
			else if ((*ValueField)->Type == EJson::String) bBoolVal = (*ValueField)->AsString().ToBool() || (*ValueField)->AsString() == TEXT("1");
			else if ((*ValueField)->Type == EJson::Number) bBoolVal = ((*ValueField)->AsNumber() != 0);
		}

		FNiagaraBool NiagaraBoolVal(bBoolVal);
		FNiagaraTypeDefinition TypeDef = FNiagaraTypeDefinition::GetBoolDef();
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetParameterData((const uint8*)&NiagaraBoolVal, Var);
	}
	else if (DataType.Equals(TEXT("Int32"), ESearchCase::IgnoreCase) || DataType.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
	{
		int32 IntVal = 0;
		const TSharedPtr<FJsonValue>* ValueField = nullptr;
		if (Params->Values.Contains(TEXT("value"))) ValueField = &Params->Values[TEXT("value")];
		else if (Params->Values.Contains(TEXT("Value"))) ValueField = &Params->Values[TEXT("Value")];

		if (ValueField && ValueField->IsValid())
		{
			if ((*ValueField)->Type == EJson::Number) IntVal = (int32)(*ValueField)->AsNumber();
			else if ((*ValueField)->Type == EJson::String) IntVal = FCString::Atoi(*(*ValueField)->AsString());
			else if ((*ValueField)->Type == EJson::Boolean) IntVal = (*ValueField)->AsBool() ? 1 : 0;
		}

		FNiagaraTypeDefinition TypeDef = FNiagaraTypeDefinition::GetIntDef();
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetParameterData((const uint8*)&IntVal, Var);
	}
	else if (DataType.Equals(TEXT("CurveFloat"), ESearchCase::IgnoreCase))
	{
		UCurveFloat* CurveFloatObj = NewObject<UCurveFloat>(System, NAME_None, RF_Transactional);
		if (!IsValid(CurveFloatObj))
		{
			Result.Errors.Add(TEXT("Failed to create transient UCurveFloat object."));
			return Result;
		}

		FRichCurve& RichCurve = CurveFloatObj->FloatCurve;
		RichCurve.Reset();

		const TArray<TSharedPtr<FJsonValue>>* CurveKeysArray = nullptr;
		if (Params->HasTypedField<EJson::Array>(TEXT("curve_keys")))
		{
			CurveKeysArray = &Params->GetArrayField(TEXT("curve_keys"));
		}
		else if (Params->HasTypedField<EJson::Array>(TEXT("CurveKeys")))
		{
			CurveKeysArray = &Params->GetArrayField(TEXT("CurveKeys"));
		}

		if (CurveKeysArray)
		{
			for (const TSharedPtr<FJsonValue>& KeyVal : *CurveKeysArray)
			{
				if (!KeyVal.IsValid() || KeyVal->Type != EJson::Object) continue;
				TSharedPtr<FJsonObject> KeyObj = KeyVal->AsObject();

				float KeyTime = 0.0f;
				if (KeyObj->HasField(TEXT("time"))) KeyTime = (float)KeyObj->GetNumberField(TEXT("time"));
				else if (KeyObj->HasField(TEXT("Time"))) KeyTime = (float)KeyObj->GetNumberField(TEXT("Time"));

				float KeyValue = 0.0f;
				if (KeyObj->HasField(TEXT("value"))) KeyValue = (float)KeyObj->GetNumberField(TEXT("value"));
				else if (KeyObj->HasField(TEXT("Value"))) KeyValue = (float)KeyObj->GetNumberField(TEXT("Value"));

				RichCurve.AddKey(KeyTime, KeyValue);
			}
		}

		FNiagaraTypeDefinition TypeDef(UCurveFloat::StaticClass());
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetUObject(CurveFloatObj, Var);
	}
	else if (DataType.Equals(TEXT("CurveLinearColor"), ESearchCase::IgnoreCase))
	{
		UCurveLinearColor* CurveColorObj = NewObject<UCurveLinearColor>(System, NAME_None, RF_Transactional);
		if (!IsValid(CurveColorObj))
		{
			Result.Errors.Add(TEXT("Failed to create transient UCurveLinearColor object."));
			return Result;
		}

		CurveColorObj->FloatCurves[0].Reset();
		CurveColorObj->FloatCurves[1].Reset();
		CurveColorObj->FloatCurves[2].Reset();
		CurveColorObj->FloatCurves[3].Reset();

		const TArray<TSharedPtr<FJsonValue>>* CurveKeysArray = nullptr;
		if (Params->HasTypedField<EJson::Array>(TEXT("curve_keys")))
		{
			CurveKeysArray = &Params->GetArrayField(TEXT("curve_keys"));
		}
		else if (Params->HasTypedField<EJson::Array>(TEXT("CurveKeys")))
		{
			CurveKeysArray = &Params->GetArrayField(TEXT("CurveKeys"));
		}

		if (CurveKeysArray)
		{
			for (const TSharedPtr<FJsonValue>& KeyVal : *CurveKeysArray)
			{
				if (!KeyVal.IsValid() || KeyVal->Type != EJson::Object) continue;
				TSharedPtr<FJsonObject> KeyObj = KeyVal->AsObject();

				float KeyTime = 0.0f;
				if (KeyObj->HasField(TEXT("time"))) KeyTime = (float)KeyObj->GetNumberField(TEXT("time"));
				else if (KeyObj->HasField(TEXT("Time"))) KeyTime = (float)KeyObj->GetNumberField(TEXT("Time"));

				FLinearColor KeyColor(0.0f, 0.0f, 0.0f, 1.0f);
				if (KeyObj->HasField(TEXT("r")) || KeyObj->HasField(TEXT("R")))
				{
					if (KeyObj->HasField(TEXT("r"))) KeyColor.R = (float)KeyObj->GetNumberField(TEXT("r"));
					else if (KeyObj->HasField(TEXT("R"))) KeyColor.R = (float)KeyObj->GetNumberField(TEXT("R"));
					if (KeyObj->HasField(TEXT("g"))) KeyColor.G = (float)KeyObj->GetNumberField(TEXT("g"));
					else if (KeyObj->HasField(TEXT("G"))) KeyColor.G = (float)KeyObj->GetNumberField(TEXT("G"));
					if (KeyObj->HasField(TEXT("b"))) KeyColor.B = (float)KeyObj->GetNumberField(TEXT("b"));
					else if (KeyObj->HasField(TEXT("B"))) KeyColor.B = (float)KeyObj->GetNumberField(TEXT("B"));
					if (KeyObj->HasField(TEXT("a"))) KeyColor.A = (float)KeyObj->GetNumberField(TEXT("a"));
					else if (KeyObj->HasField(TEXT("A"))) KeyColor.A = (float)KeyObj->GetNumberField(TEXT("A"));
				}
				else if (KeyObj->HasField(TEXT("value")) || KeyObj->HasField(TEXT("Value")))
				{
					const TSharedPtr<FJsonValue>* ValF = KeyObj->HasField(TEXT("value")) ? &KeyObj->Values[TEXT("value")] : &KeyObj->Values[TEXT("Value")];
					if (ValF && ValF->IsValid())
					{
						if ((*ValF)->Type == EJson::Number)
						{
							float Scalar = (float)(*ValF)->AsNumber();
							KeyColor = FLinearColor(Scalar, Scalar, Scalar, 1.0f);
						}
						else if ((*ValF)->Type == EJson::Array)
						{
							TArray<TSharedPtr<FJsonValue>> Arr = (*ValF)->AsArray();
							KeyColor.R = (Arr.Num() > 0) ? (float)Arr[0]->AsNumber() : 0.0f;
							KeyColor.G = (Arr.Num() > 1) ? (float)Arr[1]->AsNumber() : 0.0f;
							KeyColor.B = (Arr.Num() > 2) ? (float)Arr[2]->AsNumber() : 0.0f;
							KeyColor.A = (Arr.Num() > 3) ? (float)Arr[3]->AsNumber() : 1.0f;
						}
					}
				}

				CurveColorObj->FloatCurves[0].AddKey(KeyTime, KeyColor.R);
				CurveColorObj->FloatCurves[1].AddKey(KeyTime, KeyColor.G);
				CurveColorObj->FloatCurves[2].AddKey(KeyTime, KeyColor.B);
				CurveColorObj->FloatCurves[3].AddKey(KeyTime, KeyColor.A);
			}
		}

		FNiagaraTypeDefinition TypeDef(UCurveLinearColor::StaticClass());
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetUObject(CurveColorObj, Var);
	}
	else if (DataType.Equals(TEXT("DataChannel"), ESearchCase::IgnoreCase) || DataType.Equals(TEXT("DataInterface"), ESearchCase::IgnoreCase))
	{
		TSharedRef<FJsonObject> DIParams = MakeShared<FJsonObject>();
		for (const auto& Pair : Params->Values)
		{
			DIParams->SetField(Pair.Key, Pair.Value);
		}
		if (!DIParams->HasField(TEXT("interface_class")))
		{
			DIParams->SetStringField(TEXT("interface_class"), TEXT("NiagaraDataInterfaceDataChannelRead"));
		}
		if (!DIParams->HasField(TEXT("asset_path")) && Params->HasField(TEXT("value")))
		{
			DIParams->SetField(TEXT("asset_path"), Params->GetField<EJson::None>(TEXT("value")));
		}
		return ExecuteSetDataInterface(DIParams, Result);
	}
	else if (DataType.Equals(TEXT("Object"), ESearchCase::IgnoreCase) || DataType.Equals(TEXT("UObject"), ESearchCase::IgnoreCase) || DataType.Equals(TEXT("DataChannelAsset"), ESearchCase::IgnoreCase))
	{
		FString ObjectPath;
		if (Params->HasTypedField<EJson::String>(TEXT("asset_path")))
		{
			ObjectPath = Params->GetStringField(TEXT("asset_path"));
		}
		else if (Params->HasTypedField<EJson::String>(TEXT("value")))
		{
			ObjectPath = Params->GetStringField(TEXT("value"));
		}
		else if (Params->HasTypedField<EJson::String>(TEXT("Value")))
		{
			ObjectPath = Params->GetStringField(TEXT("Value"));
		}

		if (ObjectPath.IsEmpty())
		{
			Result.Errors.Add(TEXT("Object parameter requires 'asset_path' or 'value' containing the object path."));
			return Result;
		}

		UObject* LoadedObj = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!IsValid(LoadedObj))
		{
			Result.Errors.Add(FString::Printf(TEXT("Failed to load object at path '%s'"), *ObjectPath));
			return Result;
		}

		UClass* TargetClass = LoadedObj->GetClass();
		if (DataType.Equals(TEXT("DataChannelAsset"), ESearchCase::IgnoreCase))
		{
			TargetClass = UNiagaraDataChannelAsset::StaticClass();
			if (!LoadedObj->IsA(TargetClass))
			{
				Result.Errors.Add(FString::Printf(TEXT("Loaded object at '%s' is of type '%s', expected UNiagaraDataChannelAsset."), *ObjectPath, *LoadedObj->GetClass()->GetName()));
				return Result;
			}
		}

		FNiagaraTypeDefinition TypeDef(TargetClass);
		FNiagaraVariable Var(TypeDef, FName(*FullParamName));
		if (UserStore.IndexOf(Var) == INDEX_NONE)
		{
			UserStore.AddParameter(Var, true);
		}
		UserStore.SetUObject(LoadedObj, Var);
	}
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("Unsupported Niagara parameter data type: '%s'"), *DataType));
		return Result;
	}

	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully set Niagara parameter '%s' (%s) on system '%s'"), *FullParamName, *DataType, *SystemPath);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Parameter configuration is only supported in Editor builds."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteSetDataInterface(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	FString ParamName, InterfaceClassName;
	if (!Params->TryGetStringField(TEXT("parameter_name"), ParamName) && !Params->TryGetStringField(TEXT("ParameterName"), ParamName))
	{
		Result.Errors.Add(TEXT("Missing required field: parameter_name"));
		return Result;
	}

	if (!Params->TryGetStringField(TEXT("interface_class"), InterfaceClassName) &&
		!Params->TryGetStringField(TEXT("data_interface_class"), InterfaceClassName) &&
		!Params->TryGetStringField(TEXT("DataInterfaceClass"), InterfaceClassName))
	{
		Result.Errors.Add(TEXT("Missing required field: interface_class or data_interface_class"));
		return Result;
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	FString Scope;
	Params->TryGetStringField(TEXT("parameter_scope"), Scope);
	if (Scope.IsEmpty()) Params->TryGetStringField(TEXT("ParameterScope"), Scope);

	const bool bNonUserData = (!Scope.IsEmpty() && !Scope.Equals(TEXT("User"), ESearchCase::IgnoreCase)) ||
	                          (ParamName.Contains(TEXT(".")) && !ParamName.StartsWith(TEXT("User.")));
	if (bNonUserData)
	{
		FString BareName = FNiagaraParameterHandle(FName(*ParamName)).GetName().ToString();
		Result.Errors.Add(FString::Printf(
			TEXT("set_niagara_data_interface only supports 'User.' parameter scope (received '%s'). In Niagara, exposed data interfaces must be User parameters. ")
			TEXT("To bind a data interface to an emitter module, expose it as a User parameter (e.g. 'User.%s') via set_niagara_data_interface, ")
			TEXT("and then link it to the module input pin via set_niagara_module_pin with link_parameter='User.%s'."),
			*ParamName, *BareName, *BareName));
		return Result;
	}

	FString FullParamName;
	if (ParamName.StartsWith(TEXT("User.")))
	{
		FullParamName = ParamName;
	}
	else
	{
		FullParamName = FString::Printf(TEXT("User.%s"), *ParamName);
	}

	FString CleanClassName = InterfaceClassName;
	if (CleanClassName.StartsWith(TEXT("U")))
	{
		CleanClassName = CleanClassName.RightChop(1);
	}

	UClass* DIClass = LoadObject<UClass>(nullptr, *InterfaceClassName);
	if (!IsValid(DIClass))
	{
		DIClass = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Niagara.%s"), *CleanClassName));
	}
	if (!IsValid(DIClass))
	{
		DIClass = FindFirstObject<UClass>(*CleanClassName, EFindFirstObjectOptions::NativeFirst);
	}
	if (!IsValid(DIClass))
	{
		DIClass = FindFirstObject<UClass>(*FString::Printf(TEXT("U%s"), *CleanClassName), EFindFirstObjectOptions::NativeFirst);
	}

	if (!IsValid(DIClass) || !DIClass->IsChildOf(UNiagaraDataInterface::StaticClass()))
	{
		Result.Errors.Add(FString::Printf(TEXT("Data Interface class '%s' not found or is not a UNiagaraDataInterface subclass."), *InterfaceClassName));
		return Result;
	}

	System->Modify();
	FNiagaraUserRedirectionParameterStore& UserStore = System->GetExposedParameters();
	FNiagaraTypeDefinition TypeDef(DIClass);
	FNiagaraVariable Var(TypeDef, FName(*FullParamName));

	if (UserStore.IndexOf(Var) == INDEX_NONE)
	{
		UserStore.AddParameter(Var, true, true);
	}

	UNiagaraDataInterface* DataInterface = UserStore.GetDataInterface(Var);
	if (!IsValid(DataInterface) || DataInterface->GetClass() != DIClass)
	{
		const EObjectFlags DIOldFlags = UNiagaraDataInterface::BuildObjectFlagsForOwner(System, RF_Transactional);
		DataInterface = NewObject<UNiagaraDataInterface>(System, DIClass, NAME_None, DIOldFlags);
		UserStore.SetDataInterface(DataInterface, Var);
	}

	if (!IsValid(DataInterface))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to instantiate or retrieve data interface '%s'."), *DIClass->GetName()));
		return Result;
	}

	DataInterface->Modify();

	// Bind asset if specified
	FString BoundAssetPath;
	if (Params->TryGetStringField(TEXT("asset_path"), BoundAssetPath) && !BoundAssetPath.IsEmpty() && BoundAssetPath != SystemPath)
	{
		FString TargetPropName;
		Params->TryGetStringField(TEXT("asset_property_name"), TargetPropName);
		if (TargetPropName.IsEmpty())
		{
			Params->TryGetStringField(TEXT("AssetPropertyName"), TargetPropName);
		}

		AssignAssetToDataInterface(DataInterface, BoundAssetPath, TargetPropName, Result.Warnings);
	}

	// Apply optional properties using universal reflection helper
	const TSharedPtr<FJsonObject>* PropertiesObjPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropertiesObjPtr) && PropertiesObjPtr && (*PropertiesObjPtr).IsValid())
	{
		ApplyPropertiesFromJsonObject(DataInterface, *PropertiesObjPtr, Result);
	}

	DataInterface->PostEditChange();

	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully configured Niagara data interface parameter '%s' (%s) on system '%s'"),
		*FullParamName, *DIClass->GetName(), *SystemPath);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Data Interface configuration is only supported in Editor builds."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteAddRenderer(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	FString EmitterName, RendererType;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, Result.Errors, true) ||
		!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("renderer_type"), RendererType, Result.Errors, true))
	{
		return Result;
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString() == EmitterName)
		{
			TargetHandle = &Handle;
			break;
		}
	}

	if (!TargetHandle)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter '%s' not found in system '%s'."), *EmitterName, *SystemPath));
		return Result;
	}

	UNiagaraEmitter* Emitter = TargetHandle->GetInstance().Emitter;
	if (!IsValid(Emitter))
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter instance is invalid for emitter '%s'."), *EmitterName));
		return Result;
	}

	FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
	if (!EmitterData)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter data is invalid for emitter '%s'."), *EmitterName));
		return Result;
	}

	UClass* RendererClass = nullptr;
	if (RendererType.StartsWith(TEXT("Light"), ESearchCase::IgnoreCase))
	{
		RendererClass = UNiagaraLightRendererProperties::StaticClass();
	}
	else if (RendererType.StartsWith(TEXT("Sprite"), ESearchCase::IgnoreCase))
	{
		RendererClass = UNiagaraSpriteRendererProperties::StaticClass();
	}
	else if (RendererType.StartsWith(TEXT("Ribbon"), ESearchCase::IgnoreCase))
	{
		RendererClass = UNiagaraRibbonRendererProperties::StaticClass();
	}
	else if (RendererType.StartsWith(TEXT("Mesh"), ESearchCase::IgnoreCase))
	{
		RendererClass = UNiagaraMeshRendererProperties::StaticClass();
	}
	else
	{
		RendererClass = LoadObject<UClass>(nullptr, *RendererType);
		if (!IsValid(RendererClass))
		{
			RendererClass = FindFirstObject<UClass>(*RendererType, EFindFirstObjectOptions::NativeFirst);
		}
	}

	if (!IsValid(RendererClass) || !RendererClass->IsChildOf(UNiagaraRendererProperties::StaticClass()))
	{
		Result.Errors.Add(FString::Printf(TEXT("Invalid or unrecognized Niagara renderer type '%s'."), *RendererType));
		return Result;
	}

	Emitter->Modify();
	System->Modify();

	UNiagaraRendererProperties* NewRenderer = NewObject<UNiagaraRendererProperties>(Emitter, RendererClass, NAME_None, RF_Transactional);
	if (!IsValid(NewRenderer))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to instantiate renderer of class '%s'."), *RendererClass->GetName()));
		return Result;
	}

	Emitter->AddRenderer(NewRenderer, EmitterData->Version.VersionGuid);

	int32 TargetIndex = INDEX_NONE;
	if (Params->TryGetNumberField(TEXT("target_index"), TargetIndex) || Params->TryGetNumberField(TEXT("renderer_index"), TargetIndex))
	{
		if (TargetIndex >= 0)
		{
			Emitter->MoveRenderer(NewRenderer, TargetIndex, EmitterData->Version.VersionGuid);
		}
	}

	// Apply optional properties using universal reflection helper
	const TSharedPtr<FJsonObject>* PropertiesObjPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropertiesObjPtr) && PropertiesObjPtr && (*PropertiesObjPtr).IsValid())
	{
		ApplyPropertiesFromJsonObject(NewRenderer, *PropertiesObjPtr, Result);
	}

	NewRenderer->PostEditChange();
	Emitter->MarkPackageDirty();

	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully added renderer '%s' to emitter '%s' in system '%s'"),
		*RendererClass->GetName(), *EmitterName, *SystemPath);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Renderer configuration is only supported in Editor builds."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteEditRenderer(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	FString EmitterName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, Result.Errors, true))
	{
		return Result;
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString() == EmitterName)
		{
			TargetHandle = &Handle;
			break;
		}
	}

	if (!TargetHandle)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter '%s' not found in system '%s'."), *EmitterName, *SystemPath));
		return Result;
	}

	UNiagaraEmitter* Emitter = TargetHandle->GetInstance().Emitter;
	if (!IsValid(Emitter))
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter instance is invalid for emitter '%s'."), *EmitterName));
		return Result;
	}

	FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
	if (!EmitterData)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter data is invalid for emitter '%s'."), *EmitterName));
		return Result;
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	if (Renderers.Num() == 0)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter '%s' has no renderers to edit."), *EmitterName));
		return Result;
	}

	UNiagaraRendererProperties* TargetRenderer = nullptr;
	int32 TargetIndex = INDEX_NONE;
	if (Params->TryGetNumberField(TEXT("renderer_index"), TargetIndex) || Params->TryGetNumberField(TEXT("target_index"), TargetIndex))
	{
		if (Renderers.IsValidIndex(TargetIndex))
		{
			TargetRenderer = Renderers[TargetIndex];
		}
		else
		{
			Result.Errors.Add(FString::Printf(TEXT("Invalid renderer_index %d. Emitter has %d renderer(s)."), TargetIndex, Renderers.Num()));
			return Result;
		}
	}
	else
	{
		FString RendererType;
		Params->TryGetStringField(TEXT("renderer_type"), RendererType);
		for (UNiagaraRendererProperties* Renderer : Renderers)
		{
			if (Renderer && (
				Renderer->GetClass()->GetName().Contains(RendererType, ESearchCase::IgnoreCase) ||
				Renderer->GetName().Contains(RendererType, ESearchCase::IgnoreCase)))
			{
				TargetRenderer = Renderer;
				break;
			}
		}
	}

	if (!IsValid(TargetRenderer))
	{
		Result.Errors.Add(FString::Printf(TEXT("Could not find matching renderer on emitter '%s'."), *EmitterName));
		return Result;
	}

	Emitter->Modify();
	System->Modify();

	if (Params->HasField(TEXT("enabled")))
	{
		bool bEnabled = Params->GetBoolField(TEXT("enabled"));
		TargetRenderer->SetIsEnabled(bEnabled);
	}
	else if (Params->HasField(TEXT("bIsEnabled")))
	{
		bool bEnabled = Params->GetBoolField(TEXT("bIsEnabled"));
		TargetRenderer->SetIsEnabled(bEnabled);
	}

	const TSharedPtr<FJsonObject>* PropertiesObjPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropertiesObjPtr) && PropertiesObjPtr && (*PropertiesObjPtr).IsValid())
	{
		ApplyPropertiesFromJsonObject(TargetRenderer, *PropertiesObjPtr, Result);
	}

	TargetRenderer->PostEditChange();
	Emitter->MarkPackageDirty();

	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully edited renderer '%s' on emitter '%s' in system '%s'."),
		*TargetRenderer->GetClass()->GetName(), *EmitterName, *SystemPath);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Renderer configuration is only supported in Editor builds."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteRemoveRenderer(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	FString EmitterName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, Result.Errors, true))
	{
		return Result;
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString() == EmitterName)
		{
			TargetHandle = &Handle;
			break;
		}
	}

	if (!TargetHandle)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter '%s' not found in system '%s'."), *EmitterName, *SystemPath));
		return Result;
	}

	UNiagaraEmitter* Emitter = TargetHandle->GetInstance().Emitter;
	if (!IsValid(Emitter))
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter instance is invalid for emitter '%s'."), *EmitterName));
		return Result;
	}

	FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
	if (!EmitterData)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter data is invalid for emitter '%s'."), *EmitterName));
		return Result;
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	if (Renderers.Num() == 0)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter '%s' has no renderers to remove."), *EmitterName));
		return Result;
	}

	UNiagaraRendererProperties* TargetRenderer = nullptr;
	int32 TargetIndex = INDEX_NONE;
	if (Params->TryGetNumberField(TEXT("renderer_index"), TargetIndex) || Params->TryGetNumberField(TEXT("target_index"), TargetIndex))
	{
		if (Renderers.IsValidIndex(TargetIndex))
		{
			TargetRenderer = Renderers[TargetIndex];
		}
		else
		{
			Result.Errors.Add(FString::Printf(TEXT("Invalid renderer_index %d. Emitter has %d renderer(s)."), TargetIndex, Renderers.Num()));
			return Result;
		}
	}
	else
	{
		FString RendererType;
		Params->TryGetStringField(TEXT("renderer_type"), RendererType);
		for (UNiagaraRendererProperties* Renderer : Renderers)
		{
			if (Renderer && (
				Renderer->GetClass()->GetName().Contains(RendererType, ESearchCase::IgnoreCase) ||
				Renderer->GetName().Contains(RendererType, ESearchCase::IgnoreCase)))
			{
				TargetRenderer = Renderer;
				break;
			}
		}
	}

	if (!IsValid(TargetRenderer))
	{
		Result.Errors.Add(FString::Printf(TEXT("Could not find matching renderer on emitter '%s' to remove."), *EmitterName));
		return Result;
	}

	FString RendererClassName = TargetRenderer->GetClass()->GetName();

	Emitter->Modify();
	System->Modify();

	Emitter->RemoveRenderer(TargetRenderer, EmitterData->Version.VersionGuid);
	TargetRenderer->MarkAsGarbage();

	Emitter->MarkPackageDirty();

	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully removed renderer '%s' from emitter '%s' in system '%s'."),
		*RendererClassName, *EmitterName, *SystemPath);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Renderer configuration is only supported in Editor builds."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteRemoveEmitter(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	FString EmitterName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, Result.Errors, true))
	{
		return Result;
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	FNiagaraEmitterHandle* TargetHandle = nullptr;
	for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString() == EmitterName)
		{
			TargetHandle = &Handle;
			break;
		}
	}

	if (!TargetHandle)
	{
		Result.Errors.Add(FString::Printf(TEXT("Emitter '%s' not found in system '%s'."), *EmitterName, *SystemPath));
		return Result;
	}

	UNiagaraEmitter* TargetEmitter = TargetHandle->GetInstance().Emitter;

	System->Modify();

	FNiagaraExternalEditContext Context(System);
	const FNiagaraExt_StackItemReference EmitterRef(System, FName(*EmitterName));
	UNiagaraExternalEditUtilities::RemoveEmitter(EmitterRef, Context);

	if (IsValid(TargetEmitter) && TargetEmitter->GetOuter() == System)
	{
		TargetEmitter->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		TargetEmitter->MarkAsGarbage();
	}

	UNiagaraSystemEditorData* SystemEditorData = Cast<UNiagaraSystemEditorData>(System->GetEditorData());
	if (SystemEditorData)
	{
		SystemEditorData->SynchronizeOverviewGraphWithSystem(*System);
	}

	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully removed emitter '%s' from system '%s'."), *EmitterName, *SystemPath);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Emitter removal is only supported in Editor builds."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteRemoveModule(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	FString EmitterName, Phase, ModuleType;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("phase"), Phase, Result.Errors, true) ||
		!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("module_type"), ModuleType, Result.Errors, true))
	{
		return Result;
	}

	const bool bIsSystemPhase = (Phase == TEXT("SystemSpawn") || Phase == TEXT("SystemUpdate"));
	if (!bIsSystemPhase)
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("emitter_name"), EmitterName, Result.Errors, true))
		{
			return Result;
		}
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	UNiagaraGraph* Graph = nullptr;
	UNiagaraNodeOutput* OutputNode = nullptr;
	FString FindError;
	if (!ResolvePhaseContext(System, EmitterName, Phase, Graph, OutputNode, FindError))
	{
		Result.Errors.Add(FindError);
		return Result;
	}

	TArray<UNiagaraNodeFunctionCall*> StackNodes;
	Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(StackNodes);

	UNiagaraNodeFunctionCall* TargetNode = nullptr;
	FString ModuleLeafName = FPackageName::GetShortName(ModuleType);

	int32 TargetModuleIndex = INDEX_NONE;
	Params->TryGetNumberField(TEXT("module_index"), TargetModuleIndex);

	FString NodeGuidStr;
	Params->TryGetStringField(TEXT("node_guid"), NodeGuidStr);

	int32 MatchCount = 0;
	for (UNiagaraNodeFunctionCall* Node : StackNodes)
	{
		if (!IsValid(Node)) continue;

		if (!NodeGuidStr.IsEmpty() && Node->NodeGuid.ToString().Equals(NodeGuidStr, ESearchCase::IgnoreCase))
		{
			TargetNode = Node;
			break;
		}

		bool bMatch = false;
		if (Node->GetFunctionName().Equals(ModuleType, ESearchCase::IgnoreCase) ||
			Node->GetFunctionName().Equals(ModuleLeafName, ESearchCase::IgnoreCase))
		{
			bMatch = true;
		}
		else if (Node->FunctionScript && (
			Node->FunctionScript->GetName().Equals(ModuleType, ESearchCase::IgnoreCase) ||
			Node->FunctionScript->GetName().Equals(ModuleLeafName, ESearchCase::IgnoreCase) ||
			Node->FunctionScript->GetPathName().Contains(ModuleType)))
		{
			bMatch = true;
		}

		if (bMatch)
		{
			if (TargetModuleIndex == INDEX_NONE || TargetModuleIndex == MatchCount)
			{
				TargetNode = Node;
				break;
			}
			MatchCount++;
		}
	}

	if (!TargetNode)
	{
		Result.Errors.Add(FString::Printf(TEXT("Module '%s' not found in phase '%s' on %s."),
			*ModuleType, *Phase, bIsSystemPhase ? TEXT("System") : *EmitterName));
		return Result;
	}

	System->Modify();
	Graph->Modify();

	const UEdGraphSchema_Niagara* Schema = Cast<UEdGraphSchema_Niagara>(TargetNode->GetSchema());
	UEdGraphPin* ModuleInputPin = nullptr;
	UEdGraphPin* ModuleOutputPin = nullptr;

	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (!Pin) continue;
		if (Schema && Schema->PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			if (Pin->Direction == EGPD_Input)
			{
				ModuleInputPin = Pin;
			}
			else if (Pin->Direction == EGPD_Output)
			{
				ModuleOutputPin = Pin;
			}
		}
	}

	TArray<UEdGraphPin*> PrevLinkedPins;
	if (ModuleInputPin)
	{
		PrevLinkedPins = ModuleInputPin->LinkedTo;
	}

	TArray<UEdGraphPin*> NextLinkedPins;
	if (ModuleOutputPin)
	{
		NextLinkedPins = ModuleOutputPin->LinkedTo;
	}

	// Break parameter map links on the node to remove
	if (ModuleInputPin)
	{
		ModuleInputPin->BreakAllPinLinks();
	}
	if (ModuleOutputPin)
	{
		ModuleOutputPin->BreakAllPinLinks();
	}

	// Reconnect previous parameter map output directly to downstream input(s)
	for (UEdGraphPin* PrevPin : PrevLinkedPins)
	{
		if (PrevPin)
		{
			for (UEdGraphPin* NextPin : NextLinkedPins)
			{
				if (NextPin)
				{
					PrevPin->MakeLinkTo(NextPin);
				}
			}
		}
	}

	// Collect nodes directly feeding inputs into TargetNode (e.g. constant inputs, dynamic inputs)
	TSet<UEdGraphNode*> FeedingNodes;
	for (UEdGraphPin* Pin : TargetNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && Pin != ModuleInputPin)
		{
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					FeedingNodes.Add(LinkedPin->GetOwningNode());
				}
			}
		}
	}

	TargetNode->BreakAllNodeLinks();
	Graph->RemoveNode(TargetNode);

	for (UEdGraphNode* FeedingNode : FeedingNodes)
	{
		if (IsValid(FeedingNode))
		{
			bool bHasOtherOutputs = false;
			for (UEdGraphPin* Pin : FeedingNode->Pins)
			{
				if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
				{
					bHasOtherOutputs = true;
					break;
				}
			}
			if (!bHasOtherOutputs)
			{
				FeedingNode->BreakAllNodeLinks();
				Graph->RemoveNode(FeedingNode);
			}
		}
	}

	PruneOrphanedInputNodes(Graph);
	Graph->NotifyGraphChanged();
	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully removed module '%s' from phase '%s' on %s."),
		*ModuleType, *Phase, bIsSystemPhase ? TEXT("System") : *EmitterName);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Module removal is only supported in Editor builds."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteListNiagaraParameters(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	FString TargetScope = TEXT("all");
	Params->TryGetStringField(TEXT("scope"), TargetScope);
	if (TargetScope.IsEmpty()) Params->TryGetStringField(TEXT("parameter_scope"), TargetScope);
	if (TargetScope.IsEmpty()) TargetScope = TEXT("all");

	FString FilterEmitterName;
	Params->TryGetStringField(TEXT("emitter_name"), FilterEmitterName);
	if (FilterEmitterName.IsEmpty()) Params->TryGetStringField(TEXT("EmitterName"), FilterEmitterName);

	bool bIncludeModuleInputs = true;
	if (Params->HasField(TEXT("include_module_inputs")))
	{
		bIncludeModuleInputs = Params->GetBoolField(TEXT("include_module_inputs"));
	}

	bool bIncludeOrphanedNodes = true;
	if (Params->HasField(TEXT("include_orphaned_nodes")))
	{
		bIncludeOrphanedNodes = Params->GetBoolField(TEXT("include_orphaned_nodes"));
	}

	TArray<TSharedPtr<FJsonValue>> ParamsJsonArray;

	// 1. User Scope Parameters
	if (TargetScope == TEXT("all") || TargetScope.Equals(TEXT("User"), ESearchCase::IgnoreCase))
	{
		FNiagaraUserRedirectionParameterStore& UserStore = System->GetExposedParameters();
		TArray<FNiagaraVariable> AllVars;
		UserStore.GetParameters(AllVars);

		for (const FNiagaraVariable& Var : AllVars)
		{
			TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
			FString VarName = Var.GetName().ToString();
			FString VarScope = TEXT("User");
			if (VarName.StartsWith(TEXT("Emitter."))) VarScope = TEXT("Emitter");
			else if (VarName.StartsWith(TEXT("System."))) VarScope = TEXT("System");
			else if (VarName.StartsWith(TEXT("Engine."))) VarScope = TEXT("Engine");

			ParamObj->SetStringField(TEXT("name"), VarName);
			ParamObj->SetStringField(TEXT("scope"), VarScope);
			ParamObj->SetStringField(TEXT("type"), Var.GetType().GetName());

			const bool bIsDI = Var.GetType().IsDataInterface();
			const bool bIsUObject = Var.GetType().IsUObject();
			ParamObj->SetBoolField(TEXT("is_data_interface"), bIsDI);
			ParamObj->SetBoolField(TEXT("is_uobject"), bIsUObject);

			if (bIsDI)
			{
				UNiagaraDataInterface* DI = UserStore.GetDataInterface(Var);
				if (DI)
				{
					ParamObj->SetStringField(TEXT("data_interface_class"), DI->GetClass()->GetName());

					FString BoundAssetPath;
					FString BoundPropName;
					static const TArray<FName> CommonAssetProps = {
						FName(TEXT("DataChannelAsset")), FName(TEXT("Channel")),
						FName(TEXT("Mesh")), FName(TEXT("StaticMesh")), FName(TEXT("DefaultMesh")),
						FName(TEXT("Texture")), FName(TEXT("Source"))
					};

					for (const FName& PropName : CommonAssetProps)
					{
						if (FObjectProperty* ObjProp = CastField<FObjectProperty>(DI->GetClass()->FindPropertyByName(PropName)))
						{
							UObject* Val = ObjProp->GetObjectPropertyValue_InContainer(DI);
							if (Val)
							{
								BoundAssetPath = Val->GetPathName();
								BoundPropName = PropName.ToString();
								break;
							}
						}
					}

					if (BoundAssetPath.IsEmpty())
					{
						for (TFieldIterator<FObjectProperty> PropIt(DI->GetClass()); PropIt; ++PropIt)
						{
							FObjectProperty* ObjProp = *PropIt;
							if (ObjProp && !ObjProp->HasAnyPropertyFlags(CPF_Transient))
							{
								UObject* Val = ObjProp->GetObjectPropertyValue_InContainer(DI);
								if (Val)
								{
									BoundAssetPath = Val->GetPathName();
									BoundPropName = ObjProp->GetName();
									break;
								}
							}
						}
					}

					ParamObj->SetStringField(TEXT("bound_asset"), BoundAssetPath);
					if (!BoundPropName.IsEmpty())
					{
						ParamObj->SetStringField(TEXT("bound_property"), BoundPropName);
					}

					if (UNiagaraDataInterfaceCurveBase* CurveDI = Cast<UNiagaraDataInterfaceCurveBase>(DI))
					{
						TSharedPtr<FJsonValue> CurveKeysJson = SerializeCurveKeysToJson(CurveDI);
						if (CurveKeysJson.IsValid())
						{
							ParamObj->SetField(TEXT("curve_keys"), CurveKeysJson);
						}
					}
				}
			}
			else if (bIsUObject)
			{
				UObject* Obj = UserStore.GetUObject(Var);
				ParamObj->SetStringField(TEXT("bound_asset"), Obj ? Obj->GetPathName() : TEXT(""));
				if (UCurveFloat* CF = Cast<UCurveFloat>(Obj))
				{
					TArray<TSharedPtr<FJsonValue>> KeysArray;
					for (const FRichCurveKey& Key : CF->FloatCurve.GetConstRefOfKeys())
					{
						TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
						KeyObj->SetNumberField(TEXT("time"), Key.Time);
						KeyObj->SetNumberField(TEXT("value"), Key.Value);
						KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
					}
					ParamObj->SetArrayField(TEXT("curve_keys"), KeysArray);
				}
				else if (UCurveLinearColor* CC = Cast<UCurveLinearColor>(Obj))
				{
					TSharedPtr<FJsonObject> ChannelsObj = MakeShared<FJsonObject>();
					static const TCHAR* ChannelNames[4] = { TEXT("Red"), TEXT("Green"), TEXT("Blue"), TEXT("Alpha") };
					for (int32 i = 0; i < 4; ++i)
					{
						TArray<TSharedPtr<FJsonValue>> KeysArray;
						for (const FRichCurveKey& Key : CC->FloatCurves[i].GetConstRefOfKeys())
						{
							TSharedPtr<FJsonObject> KeyObj = MakeShared<FJsonObject>();
							KeyObj->SetNumberField(TEXT("time"), Key.Time);
							KeyObj->SetNumberField(TEXT("value"), Key.Value);
							KeysArray.Add(MakeShared<FJsonValueObject>(KeyObj));
						}
						ChannelsObj->SetArrayField(ChannelNames[i], KeysArray);
					}
					ParamObj->SetObjectField(TEXT("curve_keys"), ChannelsObj);
				}
			}
			else
			{
				FString ValStr;
				if (Var.GetType() == FNiagaraTypeDefinition::GetFloatDef())
				{
					ValStr = FString::SanitizeFloat(UserStore.GetParameterValue<float>(Var));
				}
				else if (Var.GetType() == FNiagaraTypeDefinition::GetIntDef())
				{
					ValStr = FString::FromInt(UserStore.GetParameterValue<int32>(Var));
				}
				else if (Var.GetType() == FNiagaraTypeDefinition::GetBoolDef())
				{
					ValStr = UserStore.GetParameterValue<FNiagaraBool>(Var).GetValue() ? TEXT("true") : TEXT("false");
				}
				else if (Var.GetType() == FNiagaraTypeDefinition::GetPositionDef())
				{
					ValStr = UserStore.GetParameterValue<FVector>(Var).ToString();
				}
				else if (Var.GetType() == FNiagaraTypeDefinition::GetVec3Def())
				{
					ValStr = UserStore.GetParameterValue<FVector3f>(Var).ToString();
				}
				else if (Var.GetType() == FNiagaraTypeDefinition::GetColorDef())
				{
					ValStr = UserStore.GetParameterValue<FLinearColor>(Var).ToString();
				}
				else if (Var.GetType() == FNiagaraTypeDefinition::GetVec2Def())
				{
					ValStr = UserStore.GetParameterValue<FVector2f>(Var).ToString();
				}
				else if (Var.GetType() == FNiagaraTypeDefinition::GetVec4Def())
				{
					ValStr = UserStore.GetParameterValue<FVector4f>(Var).ToString();
				}
				ParamObj->SetStringField(TEXT("value"), ValStr);
			}

			ParamsJsonArray.Add(MakeShared<FJsonValueObject>(ParamObj));
		}
	}

	// 2. System Scope Variables (from System NodeGraph metadata)
	if (TargetScope == TEXT("all") || TargetScope.Equals(TEXT("System"), ESearchCase::IgnoreCase))
	{
		UNiagaraScript* SysScript = System->GetSystemSpawnScript();
		if (SysScript)
		{
			UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(SysScript->GetSource(SysScript->GetExposedVersion().VersionGuid));
			if (Src && Src->NodeGraph)
			{
				for (const auto& Pair : Src->NodeGraph->GetAllMetaData())
				{
					const FNiagaraVariable& Var = Pair.Key;
					FString VarName = Var.GetName().ToString();
					if (VarName.StartsWith(TEXT("System.")) || VarName.StartsWith(TEXT("Engine.")))
					{
						TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
						ParamObj->SetStringField(TEXT("name"), VarName);
						ParamObj->SetStringField(TEXT("scope"), TEXT("System"));
						ParamObj->SetStringField(TEXT("type"), Var.GetType().GetName());
						ParamObj->SetBoolField(TEXT("is_data_interface"), Var.GetType().IsDataInterface());
						ParamObj->SetBoolField(TEXT("is_uobject"), Var.GetType().IsUObject());
						ParamsJsonArray.Add(MakeShared<FJsonValueObject>(ParamObj));
					}
				}
			}

			TArray<FNiagaraVariable> SysRIPVars;
			SysScript->RapidIterationParameters.GetParameters(SysRIPVars);
			for (const FNiagaraVariable& RIPVar : SysRIPVars)
			{
				TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
				ParamObj->SetStringField(TEXT("name"), RIPVar.GetName().ToString());
				ParamObj->SetStringField(TEXT("scope"), TEXT("RapidIteration"));
				ParamObj->SetStringField(TEXT("type"), RIPVar.GetType().GetName());
				ParamObj->SetBoolField(TEXT("is_data_interface"), RIPVar.GetType().IsDataInterface());
				ParamObj->SetBoolField(TEXT("is_uobject"), RIPVar.GetType().IsUObject());
				ParamsJsonArray.Add(MakeShared<FJsonValueObject>(ParamObj));
			}
		}
	}

	// 3. Emitter Scope Variables (from Emitter NodeGraph metadata)
	if (TargetScope == TEXT("all") || TargetScope.Equals(TEXT("Emitter"), ESearchCase::IgnoreCase))
	{
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FString CurrentEmitterName = Handle.GetName().ToString();
			if (!FilterEmitterName.IsEmpty() && !FilterEmitterName.Equals(CurrentEmitterName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
			if (!IsValid(Emitter)) continue;
			FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
			if (!EmitterData || !EmitterData->SpawnScriptProps.Script) continue;

			UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(EmitterData->SpawnScriptProps.Script->GetSource(EmitterData->SpawnScriptProps.Script->GetExposedVersion().VersionGuid));
			if (Src && Src->NodeGraph)
			{
				for (const auto& Pair : Src->NodeGraph->GetAllMetaData())
				{
					const FNiagaraVariable& Var = Pair.Key;
					FString VarName = Var.GetName().ToString();
					if (VarName.StartsWith(TEXT("Emitter.")) || VarName.StartsWith(TEXT("Particles.")))
					{
						TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
						ParamObj->SetStringField(TEXT("name"), VarName);
						ParamObj->SetStringField(TEXT("scope"), TEXT("Emitter"));
						ParamObj->SetStringField(TEXT("emitter_name"), CurrentEmitterName);
						ParamObj->SetStringField(TEXT("type"), Var.GetType().GetName());
						ParamObj->SetBoolField(TEXT("is_data_interface"), Var.GetType().IsDataInterface());
						ParamObj->SetBoolField(TEXT("is_uobject"), Var.GetType().IsUObject());
						ParamsJsonArray.Add(MakeShared<FJsonValueObject>(ParamObj));
					}
				}
			}

			TArray<FNiagaraVariable> EmitterRIPVars;
			EmitterData->SpawnScriptProps.Script->RapidIterationParameters.GetParameters(EmitterRIPVars);
			for (const FNiagaraVariable& RIPVar : EmitterRIPVars)
			{
				TSharedRef<FJsonObject> ParamObj = MakeShared<FJsonObject>();
				ParamObj->SetStringField(TEXT("name"), RIPVar.GetName().ToString());
				ParamObj->SetStringField(TEXT("scope"), TEXT("RapidIteration"));
				ParamObj->SetStringField(TEXT("emitter_name"), CurrentEmitterName);
				ParamObj->SetStringField(TEXT("type"), RIPVar.GetType().GetName());
				ParamObj->SetBoolField(TEXT("is_data_interface"), RIPVar.GetType().IsDataInterface());
				ParamObj->SetBoolField(TEXT("is_uobject"), RIPVar.GetType().IsUObject());
				ParamsJsonArray.Add(MakeShared<FJsonValueObject>(ParamObj));
			}
		}
	}

	// 4. ModuleInput Scope (active module input overrides in the stack)
	if ((TargetScope == TEXT("all") || TargetScope.Equals(TEXT("ModuleInput"), ESearchCase::IgnoreCase)) && bIncludeModuleInputs)
	{
		auto ScanGraphModuleInputs = [&](UNiagaraGraph* Graph, const FString& EmitterScopeName)
		{
			if (!Graph) return;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(Node);
				if (!FuncNode) continue;
				FString ModName = FuncNode->GetFunctionName();

				const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(FuncNode->GetSchema());
				for (UEdGraphPin* Pin : FuncNode->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Input && Schema->PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
					{
						if (Pin->LinkedTo.Num() == 1 && Pin->LinkedTo[0])
						{
							UEdGraphNode* OverrideNode = Pin->LinkedTo[0]->GetOwningNode();
							for (UEdGraphPin* OverridePin : OverrideNode->Pins)
							{
								if (OverridePin && OverridePin->Direction == EGPD_Input &&
									OverridePin->PinType.PinSubCategoryObject != FNiagaraTypeDefinition::GetParameterMapStruct())
								{
									FString PinFullName = OverridePin->PinName.ToString();
									int32 ModIdx = PinFullName.Find(ModName);
									if (ModIdx != INDEX_NONE && (ModIdx == 0 || PinFullName[ModIdx - 1] == TCHAR('.')))
									{
										TSharedRef<FJsonObject> OverrideObj = MakeShared<FJsonObject>();
										OverrideObj->SetStringField(TEXT("name"), PinFullName);
										OverrideObj->SetStringField(TEXT("scope"), TEXT("ModuleInput"));
										OverrideObj->SetStringField(TEXT("emitter_name"), EmitterScopeName);
										OverrideObj->SetStringField(TEXT("module_name"), ModName);
										OverrideObj->SetStringField(TEXT("pin_name"), PinFullName.RightChop(ModIdx + ModName.Len() + 1));

										FNiagaraTypeDefinition PinType = Schema->PinToTypeDefinition(OverridePin);
										OverrideObj->SetStringField(TEXT("type"), PinType.GetName());
										OverrideObj->SetBoolField(TEXT("is_data_interface"), PinType.IsDataInterface());
										OverrideObj->SetBoolField(TEXT("is_uobject"), PinType.IsUObject());

										if (OverridePin->LinkedTo.Num() > 0 && OverridePin->LinkedTo[0])
										{
											UEdGraphNode* UpstreamNode = OverridePin->LinkedTo[0]->GetOwningNode();
											if (UpstreamNode->IsA<UNiagaraNodeInput>())
											{
												if (FObjectProperty* DIProp = CastField<FObjectProperty>(UpstreamNode->GetClass()->FindPropertyByName(TEXT("DataInterface"))))
												{
													if (UNiagaraDataInterface* DI = Cast<UNiagaraDataInterface>(DIProp->GetObjectPropertyValue_InContainer(UpstreamNode)))
													{
														OverrideObj->SetStringField(TEXT("data_interface_class"), DI->GetClass()->GetName());
														FString BoundPath;
														for (TFieldIterator<FObjectProperty> PropIt(DI->GetClass()); PropIt; ++PropIt)
														{
															if (*PropIt && !PropIt->HasAnyPropertyFlags(CPF_Transient))
															{
																if (UObject* Val = PropIt->GetObjectPropertyValue_InContainer(DI))
																{
																	BoundPath = Val->GetPathName();
																	OverrideObj->SetStringField(TEXT("bound_property"), PropIt->GetName());
																	break;
																}
															}
														}
														OverrideObj->SetStringField(TEXT("bound_asset"), BoundPath);

														// Serialize curve keys if this is a curve DI
														if (UNiagaraDataInterfaceCurveBase* CurveDI = Cast<UNiagaraDataInterfaceCurveBase>(DI))
														{
															TSharedPtr<FJsonValue> CurveKeysJson = SerializeCurveKeysToJson(CurveDI);
															if (CurveKeysJson.IsValid())
															{
																OverrideObj->SetField(TEXT("curve_keys"), CurveKeysJson);
															}
														}
													}
												}
											}
											else
											{
												OverrideObj->SetStringField(TEXT("linked_parameter"), OverridePin->LinkedTo[0]->PinName.ToString());
											}
										}
										else
										{
											OverrideObj->SetStringField(TEXT("value"), OverridePin->DefaultValue);
										}

										ParamsJsonArray.Add(MakeShared<FJsonValueObject>(OverrideObj));
									}
								}
							}
						}
						break;
					}
				}

				// Also inspect static switch pins on FuncNode itself
				for (UEdGraphPin* NodePin : FuncNode->Pins)
				{
					if (NodePin && NodePin->Direction == EGPD_Input && NodePin->PinType.PinSubCategoryObject.IsValid())
					{
						UEnum* EnumObj = Cast<UEnum>(NodePin->PinType.PinSubCategoryObject.Get());
						if (EnumObj || NodePin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryType || NodePin->PinName.ToString().Contains(TEXT("Mode")))
						{
							TSharedRef<FJsonObject> SwitchObj = MakeShared<FJsonObject>();
							SwitchObj->SetStringField(TEXT("name"), NodePin->PinName.ToString());
							SwitchObj->SetStringField(TEXT("scope"), TEXT("StaticSwitch"));
							SwitchObj->SetStringField(TEXT("emitter_name"), EmitterScopeName);
							SwitchObj->SetStringField(TEXT("module_name"), ModName);
							SwitchObj->SetStringField(TEXT("pin_name"), NodePin->PinName.ToString());
							SwitchObj->SetStringField(TEXT("value"), NodePin->DefaultValue);
							SwitchObj->SetBoolField(TEXT("is_static_switch"), true);
							if (EnumObj)
							{
								SwitchObj->SetStringField(TEXT("type"), EnumObj->GetName());
								int32 EnumIdx = EnumObj->GetIndexByNameString(NodePin->DefaultValue);
								if (EnumIdx != INDEX_NONE)
								{
									SwitchObj->SetStringField(TEXT("display_value"), EnumObj->GetDisplayNameTextByIndex(EnumIdx).ToString());
								}
							}
							else
							{
								SwitchObj->SetStringField(TEXT("type"), TEXT("StaticSwitch"));
							}
							ParamsJsonArray.Add(MakeShared<FJsonValueObject>(SwitchObj));
						}
					}
				}
			}
		};

		if (UNiagaraScript* SysScript = System->GetSystemSpawnScript())
		{
			UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(SysScript->GetSource(SysScript->GetExposedVersion().VersionGuid));
			if (Src && Src->NodeGraph) ScanGraphModuleInputs(Src->NodeGraph, TEXT("System"));
		}

		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FString CurrentEmitterName = Handle.GetName().ToString();
			if (!FilterEmitterName.IsEmpty() && !FilterEmitterName.Equals(CurrentEmitterName, ESearchCase::IgnoreCase)) continue;
			UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
			if (!IsValid(Emitter)) continue;
			FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
			if (!EmitterData || !EmitterData->SpawnScriptProps.Script) continue;
			UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(EmitterData->SpawnScriptProps.Script->GetSource(EmitterData->SpawnScriptProps.Script->GetExposedVersion().VersionGuid));
			if (Src && Src->NodeGraph) ScanGraphModuleInputs(Src->NodeGraph, CurrentEmitterName);
		}
	}

	// 5. GraphInput Scope (inspect UNiagaraNodeInput and identify orphaned nodes)
	if ((TargetScope == TEXT("all") || TargetScope.Equals(TEXT("GraphInput"), ESearchCase::IgnoreCase)) && bIncludeOrphanedNodes)
	{
		auto ScanGraphInputNodes = [&](UNiagaraGraph* Graph, const FString& EmitterScopeName)
		{
			if (!Graph) return;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!IsValid(Node) || !Node->IsA<UNiagaraNodeInput>()) continue;

				bool bHasActiveLinks = false;
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->LinkedTo.Num() > 0)
					{
						bHasActiveLinks = true;
						break;
					}
				}

				FString NodeInputName = Node->GetName();
				FString NodeInputType;
				if (FStructProperty* InputProp = CastField<FStructProperty>(Node->GetClass()->FindPropertyByName(TEXT("Input"))))
				{
					if (const FNiagaraVariable* VarPtr = InputProp->ContainerPtrToValuePtr<FNiagaraVariable>(Node))
					{
						NodeInputName = VarPtr->GetName().ToString();
						NodeInputType = VarPtr->GetType().GetName();
					}
				}

				TSharedRef<FJsonObject> NodeObj = MakeShared<FJsonObject>();
				NodeObj->SetStringField(TEXT("name"), NodeInputName);
				NodeObj->SetStringField(TEXT("scope"), TEXT("GraphInput"));
				NodeObj->SetStringField(TEXT("emitter_name"), EmitterScopeName);
				NodeObj->SetStringField(TEXT("type"), NodeInputType);
				NodeObj->SetBoolField(TEXT("is_connected"), bHasActiveLinks);
				NodeObj->SetBoolField(TEXT("is_orphaned"), !bHasActiveLinks);

				if (FObjectProperty* DIProp = CastField<FObjectProperty>(Node->GetClass()->FindPropertyByName(TEXT("DataInterface"))))
				{
					if (UNiagaraDataInterface* DI = Cast<UNiagaraDataInterface>(DIProp->GetObjectPropertyValue_InContainer(Node)))
					{
						NodeObj->SetBoolField(TEXT("is_data_interface"), true);
						NodeObj->SetStringField(TEXT("data_interface_class"), DI->GetClass()->GetName());
						FString BoundPath;
						for (TFieldIterator<FObjectProperty> PropIt(DI->GetClass()); PropIt; ++PropIt)
						{
							if (*PropIt && !PropIt->HasAnyPropertyFlags(CPF_Transient))
							{
								if (UObject* Val = PropIt->GetObjectPropertyValue_InContainer(DI))
								{
									BoundPath = Val->GetPathName();
									NodeObj->SetStringField(TEXT("bound_property"), PropIt->GetName());
									break;
								}
							}
						}
						NodeObj->SetStringField(TEXT("bound_asset"), BoundPath);

						if (UNiagaraDataInterfaceCurveBase* CurveDI = Cast<UNiagaraDataInterfaceCurveBase>(DI))
						{
							TSharedPtr<FJsonValue> CurveKeysJson = SerializeCurveKeysToJson(CurveDI);
							if (CurveKeysJson.IsValid())
							{
								NodeObj->SetField(TEXT("curve_keys"), CurveKeysJson);
							}
						}
					}
				}

				if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Node->GetClass()->FindPropertyByName(TEXT("ObjectAsset"))))
				{
					if (UObject* Obj = ObjProp->GetObjectPropertyValue_InContainer(Node))
					{
						NodeObj->SetBoolField(TEXT("is_uobject"), true);
						NodeObj->SetStringField(TEXT("bound_asset"), Obj->GetPathName());
					}
				}

				ParamsJsonArray.Add(MakeShared<FJsonValueObject>(NodeObj));
			}
		};

		if (UNiagaraScript* SysScript = System->GetSystemSpawnScript())
		{
			UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(SysScript->GetSource(SysScript->GetExposedVersion().VersionGuid));
			if (Src && Src->NodeGraph) ScanGraphInputNodes(Src->NodeGraph, TEXT("System"));
		}

		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FString CurrentEmitterName = Handle.GetName().ToString();
			if (!FilterEmitterName.IsEmpty() && !FilterEmitterName.Equals(CurrentEmitterName, ESearchCase::IgnoreCase)) continue;
			UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
			if (!IsValid(Emitter)) continue;
			FVersionedNiagaraEmitterData* EmitterData = Emitter->GetLatestEmitterData();
			if (!EmitterData || !EmitterData->SpawnScriptProps.Script) continue;
			UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(EmitterData->SpawnScriptProps.Script->GetSource(EmitterData->SpawnScriptProps.Script->GetExposedVersion().VersionGuid));
			if (Src && Src->NodeGraph) ScanGraphInputNodes(Src->NodeGraph, CurrentEmitterName);
		}
	}

	TSharedRef<FJsonObject> RootObj = MakeShared<FJsonObject>();
	RootObj->SetArrayField(TEXT("parameters"), ParamsJsonArray);
	RootObj->SetNumberField(TEXT("count"), ParamsJsonArray.Num());

	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(RootObj, Writer);

	Result.bSuccess = true;
	Result.ResultMessage = OutputStr;
#else
	Result.Errors.Add(TEXT("Parameter inspection is only supported in Editor builds."));
#endif
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkNiagaraActions::ExecuteRemoveNiagaraParameter(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
#if WITH_EDITOR
	FString SystemPath;
	if (!Params->TryGetStringField(TEXT("system_path"), SystemPath) || SystemPath.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), SystemPath, Result.Errors, true))
		{
			return Result;
		}
	}

	FString ParamName;
	if (!Params->TryGetStringField(TEXT("parameter_name"), ParamName) && !Params->TryGetStringField(TEXT("ParameterName"), ParamName))
	{
		Result.Errors.Add(TEXT("Missing required field: parameter_name"));
		return Result;
	}

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!IsValid(System))
	{
		Result.Errors.Add(FString::Printf(TEXT("Niagara System not found at %s"), *SystemPath));
		return Result;
	}

	FNiagaraUserRedirectionParameterStore& UserStore = System->GetExposedParameters();
	TArray<FNiagaraVariable> AllVars;
	UserStore.GetParameters(AllVars);

	FNiagaraVariable FoundVar;
	bool bFound = false;

	for (const FNiagaraVariable& Var : AllVars)
	{
		FString VarName = Var.GetName().ToString();
		if (VarName.Equals(ParamName, ESearchCase::IgnoreCase))
		{
			FoundVar = Var;
			bFound = true;
			break;
		}

		if (ParamName.StartsWith(TEXT("User.")) && VarName.Equals(ParamName.RightChop(5), ESearchCase::IgnoreCase))
		{
			FoundVar = Var;
			bFound = true;
			break;
		}

		if (!ParamName.StartsWith(TEXT("User.")) && VarName.Equals(FString::Printf(TEXT("User.%s"), *ParamName), ESearchCase::IgnoreCase))
		{
			FoundVar = Var;
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		Result.Errors.Add(FString::Printf(TEXT("Parameter '%s' not found on Niagara system '%s'."), *ParamName, *SystemPath));
		return Result;
	}

	System->Modify();
	UserStore.RemoveParameter(FoundVar);

	Result.bSuccess = WaitAndReportCompile(System, Result);
	if (Result.bSuccess)
	{
		SaveAndDirtyAsset(System);
	}
	Result.ResultMessage = FString::Printf(TEXT("Successfully removed parameter '%s' from Niagara system '%s'"), *FoundVar.GetName().ToString(), *SystemPath);
	Result.ModifiedAssets.Add(SystemPath);
#else
	Result.Errors.Add(TEXT("Parameter removal is only supported in Editor builds."));
#endif
	return Result;
}



