// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Toolsets/AgentFrameworkEnhancedInputToolset.h"
#include "AgentFrameworkActionUtils.h"
#include "EnhancedActionKeyMapping.h"
#include "EnhancedInputLibrary.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace AgentFrameworkEnhancedInputInternal
{
	static bool ParseInputActionValueType(const FString& InType, EInputActionValueType& OutValueType, FString& OutCanonicalName)
	{
		const FString Trimmed = InType.TrimStartAndEnd();
		if (Trimmed.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
		{
			OutValueType = EInputActionValueType::Boolean;
			OutCanonicalName = TEXT("Boolean");
			return true;
		}
		if (Trimmed.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("Float"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("1D"), ESearchCase::IgnoreCase))
		{
			OutValueType = EInputActionValueType::Axis1D;
			OutCanonicalName = TEXT("Axis1D");
			return true;
		}
		if (Trimmed.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("2D"), ESearchCase::IgnoreCase))
		{
			OutValueType = EInputActionValueType::Axis2D;
			OutCanonicalName = TEXT("Axis2D");
			return true;
		}
		if (Trimmed.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("Vector"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("3D"), ESearchCase::IgnoreCase))
		{
			OutValueType = EInputActionValueType::Axis3D;
			OutCanonicalName = TEXT("Axis3D");
			return true;
		}
		return false;
	}

	static UClass* ResolveInputSubClass(const FString& TypeName, UClass* BaseClass, const FString& PrefixAlias)
	{
		const FString Trimmed = TypeName.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return nullptr;
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (!Candidate || !Candidate->IsChildOf(BaseClass)
				|| Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				continue;
			}
			const FString Name = Candidate->GetName();
			if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("TRASHCLASS_")) || Name.StartsWith(TEXT("REINST_")))
			{
				continue;
			}
			if (Name.Equals(Trimmed, ESearchCase::IgnoreCase)
				|| Name.Equals(PrefixAlias + Trimmed, ESearchCase::IgnoreCase)
				|| Candidate->GetPathName().Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	static bool ApplyPropertiesToObject(UObject* Target, const TMap<FString, FString>& Props, FString& OutError)
	{
		if (!Target)
		{
			OutError = TEXT("Null object when applying properties.");
			return false;
		}

		for (const TPair<FString, FString>& Pair : Props)
		{
			FProperty* Prop = Target->GetClass()->FindPropertyByName(*Pair.Key.TrimStartAndEnd());
			if (!Prop)
			{
				OutError = FString::Printf(TEXT("Unknown property '%s' on '%s'."), *Pair.Key, *Target->GetClass()->GetName());
				return false;
			}

			const TCHAR* EndPtr = Prop->ImportText_InContainer(*Pair.Value, Target, Target, PPF_None);
			if (!EndPtr)
			{
				OutError = FString::Printf(TEXT("Failed to parse value '%s' for property '%s' on '%s'."), *Pair.Value, *Pair.Key, *Target->GetClass()->GetName());
				return false;
			}
			while (*EndPtr != TEXT('\0') && FChar::IsWhitespace(*EndPtr))
			{
				++EndPtr;
			}
			if (*EndPtr != TEXT('\0'))
			{
				OutError = FString::Printf(TEXT("Trailing unparsed text '%s' for property '%s' on '%s'."), EndPtr, *Pair.Key, *Target->GetClass()->GetName());
				return false;
			}
		}
		return true;
	}

	static bool BuildValidatedModifiers(
		const TArray<FAgentFrameworkInputModifierSpec>& Specs,
		TArray<TObjectPtr<UInputModifier>>& OutModifiers,
		FString& OutError)
	{
		OutModifiers.Reset();
		for (const FAgentFrameworkInputModifierSpec& Spec : Specs)
		{
			UClass* ModClass = ResolveInputSubClass(Spec.Type, UInputModifier::StaticClass(), TEXT("InputModifier"));
			if (!ModClass)
			{
				OutError = FString::Printf(TEXT("Unknown UInputModifier class '%s'."), *Spec.Type);
				return false;
			}
			UInputModifier* ModObj = NewObject<UInputModifier>(GetTransientPackage(), ModClass);
			if (!ModObj || !ApplyPropertiesToObject(ModObj, Spec.Properties, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(TEXT("Failed to instantiate modifier '%s'."), *Spec.Type);
				}
				return false;
			}
			OutModifiers.Add(ModObj);
		}
		return true;
	}

	static bool BuildValidatedTriggers(
		const TArray<FAgentFrameworkInputTriggerSpec>& Specs,
		TArray<TObjectPtr<UInputTrigger>>& OutTriggers,
		FString& OutError)
	{
		OutTriggers.Reset();
		for (const FAgentFrameworkInputTriggerSpec& Spec : Specs)
		{
			UClass* TriggerClass = ResolveInputSubClass(Spec.Type, UInputTrigger::StaticClass(), TEXT("InputTrigger"));
			if (!TriggerClass)
			{
				OutError = FString::Printf(TEXT("Unknown UInputTrigger class '%s'."), *Spec.Type);
				return false;
			}
			UInputTrigger* TriggerObj = NewObject<UInputTrigger>(GetTransientPackage(), TriggerClass);
			if (!TriggerObj || !ApplyPropertiesToObject(TriggerObj, Spec.Properties, OutError))
			{
				if (OutError.IsEmpty())
				{
					OutError = FString::Printf(TEXT("Failed to instantiate trigger '%s'."), *Spec.Type);
				}
				return false;
			}
			OutTriggers.Add(TriggerObj);
		}
		return true;
	}

	static void ReparentObjectsToContext(UInputMappingContext* Context, TArray<TObjectPtr<UInputModifier>>& Modifiers, TArray<TObjectPtr<UInputTrigger>>& Triggers)
	{
		for (TObjectPtr<UInputModifier>& Mod : Modifiers)
		{
			if (Mod)
			{
				Mod->Rename(nullptr, Context, REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}
		for (TObjectPtr<UInputTrigger>& Trigger : Triggers)
		{
			if (Trigger)
			{
				Trigger->Rename(nullptr, Context, REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}
	}

	static bool LoadMappingContextActionAndKey(
		const FString& MappingContextPath,
		const FString& InputActionPath,
		const FString& KeyName,
		UInputMappingContext*& OutContext,
		UInputAction*& OutAction,
		FKey& OutKey,
		FString& OutError)
	{
		const FString ContextObjPath = UAgentFrameworkActionUtils::NormalizeAssetObjectPath(MappingContextPath);
		OutContext = LoadObject<UInputMappingContext>(nullptr, *ContextObjPath);
		if (!OutContext)
		{
			OutError = FString::Printf(TEXT("InputMappingContext not found at '%s'."), *MappingContextPath);
			return false;
		}

		const FString ActionObjPath = UAgentFrameworkActionUtils::NormalizeAssetObjectPath(InputActionPath);
		OutAction = LoadObject<UInputAction>(nullptr, *ActionObjPath);
		if (!OutAction)
		{
			OutError = FString::Printf(TEXT("InputAction not found at '%s'."), *InputActionPath);
			return false;
		}

		OutKey = FKey(*KeyName.TrimStartAndEnd());
		if (!OutKey.IsValid())
		{
			OutError = FString::Printf(TEXT("Invalid FKey name '%s'."), *KeyName);
			return false;
		}

		return true;
	}
}

FAgentFrameworkInputActionResult UAgentFrameworkEnhancedInputAuthoringToolset::CreateInputAction(
	const FString& AssetPath,
	const FString& ValueType)
{
	FAgentFrameworkInputActionResult Result;
	FString PackageName;
	FString AssetName;
	FString Error;
	if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(AssetPath, PackageName, AssetName, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputAction"), Error);
		return Result;
	}

	EInputActionValueType ParsedValueType = EInputActionValueType::Boolean;
	FString CanonicalTypeName;
	if (!AgentFrameworkEnhancedInputInternal::ParseInputActionValueType(ValueType, ParsedValueType, CanonicalTypeName))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputAction"),
			FString::Printf(TEXT("Invalid ValueType '%s'. Supported: Boolean, Axis1D, Axis2D, Axis3D."), *ValueType));
		return Result;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputAction"), FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return Result;
	}

	UInputAction* Action = NewObject<UInputAction>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!Action)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, nullptr);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputAction"), TEXT("Failed to allocate UInputAction."));
		return Result;
	}

	Action->ValueType = ParsedValueType;
	Action->PostEditChange();

	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Package, Action, true, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputAction"), Error);
		return Result;
	}

	Result.AssetPath = Action->GetPathName();
	Result.PackageName = PackageName;
	Result.ValueType = CanonicalTypeName;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateInputAction"), 0.0, true, {});
	return Result;
}

FAgentFrameworkInputMappingContextResult UAgentFrameworkEnhancedInputAuthoringToolset::CreateInputMappingContext(
	const FString& AssetPath)
{
	FAgentFrameworkInputMappingContextResult Result;
	FString PackageName;
	FString AssetName;
	FString Error;
	if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(AssetPath, PackageName, AssetName, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputMappingContext"), Error);
		return Result;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputMappingContext"), FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return Result;
	}

	UInputMappingContext* MappingContext = NewObject<UInputMappingContext>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!MappingContext)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, nullptr);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputMappingContext"), TEXT("Failed to allocate UInputMappingContext."));
		return Result;
	}

	MappingContext->PostEditChange();
	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Package, MappingContext, true, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateInputMappingContext"), Error);
		return Result;
	}

	Result.AssetPath = MappingContext->GetPathName();
	Result.PackageName = PackageName;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateInputMappingContext"), 0.0, true, {});
	return Result;
}

FAgentFrameworkInputMappingResult UAgentFrameworkEnhancedInputAuthoringToolset::AddInputMapping(
	const FString& MappingContextPath,
	const FString& InputActionPath,
	const FString& KeyName,
	const TOptional<TArray<FAgentFrameworkInputModifierSpec>>& Modifiers,
	const TOptional<TArray<FAgentFrameworkInputTriggerSpec>>& Triggers)
{
	FAgentFrameworkInputMappingResult Result;
	UInputMappingContext* MappingContext = nullptr;
	UInputAction* InputAction = nullptr;
	FKey Key;
	FString Error;

	if (!AgentFrameworkEnhancedInputInternal::LoadMappingContextActionAndKey(
		MappingContextPath, InputActionPath, KeyName, MappingContext, InputAction, Key, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("AddInputMapping"), Error);
		return Result;
	}

	for (const FEnhancedActionKeyMapping& Existing : MappingContext->GetMappings())
	{
		if (Existing.Action == InputAction && Existing.Key == Key)
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("AddInputMapping"),
				FString::Printf(TEXT("Mapping for action '%s' and key '%s' already exists; use ConfigureInputMappingModifiersTriggers instead."),
					*InputAction->GetName(), *Key.ToString()));
			return Result;
		}
	}

	TArray<TObjectPtr<UInputModifier>> BuiltModifiers;
	if (Modifiers.IsSet() && !AgentFrameworkEnhancedInputInternal::BuildValidatedModifiers(*Modifiers, BuiltModifiers, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("AddInputMapping"), Error);
		return Result;
	}

	TArray<TObjectPtr<UInputTrigger>> BuiltTriggers;
	if (Triggers.IsSet() && !AgentFrameworkEnhancedInputInternal::BuildValidatedTriggers(*Triggers, BuiltTriggers, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("AddInputMapping"), Error);
		return Result;
	}

	MappingContext->Modify();
	AgentFrameworkEnhancedInputInternal::ReparentObjectsToContext(MappingContext, BuiltModifiers, BuiltTriggers);

	FEnhancedActionKeyMapping& NewMapping = MappingContext->MapKey(InputAction, Key);
	NewMapping.Modifiers = MoveTemp(BuiltModifiers);
	NewMapping.Triggers = MoveTemp(BuiltTriggers);

	MappingContext->PostEditChange();
	UEnhancedInputLibrary::RequestRebuildControlMappingsUsingContext(MappingContext, true);

	if (!UAgentFrameworkActionUtils::SaveAssetChecked(MappingContext->GetPackage(), MappingContext, false, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("AddInputMapping"), Error);
		return Result;
	}

	Result.MappingContextPath = MappingContext->GetPathName();
	Result.InputActionPath = InputAction->GetPathName();
	Result.KeyName = Key.ToString();
	Result.ModifierCount = NewMapping.Modifiers.Num();
	Result.TriggerCount = NewMapping.Triggers.Num();
	Result.TotalMappings = MappingContext->GetMappings().Num();
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("AddInputMapping"), 0.0, true, {});
	return Result;
}

FAgentFrameworkInputMappingResult UAgentFrameworkEnhancedInputAuthoringToolset::ConfigureInputMappingModifiersTriggers(
	const FString& MappingContextPath,
	const FString& InputActionPath,
	const FString& KeyName,
	const TOptional<TArray<FAgentFrameworkInputModifierSpec>>& Modifiers,
	const TOptional<TArray<FAgentFrameworkInputTriggerSpec>>& Triggers)
{
	FAgentFrameworkInputMappingResult Result;
	if (!Modifiers.IsSet() && !Triggers.IsSet())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureInputMappingModifiersTriggers"),
			TEXT("At least one of Modifiers or Triggers must be provided (pass an empty array to clear)."));
		return Result;
	}

	UInputMappingContext* MappingContext = nullptr;
	UInputAction* InputAction = nullptr;
	FKey Key;
	FString Error;
	if (!AgentFrameworkEnhancedInputInternal::LoadMappingContextActionAndKey(
		MappingContextPath, InputActionPath, KeyName, MappingContext, InputAction, Key, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureInputMappingModifiersTriggers"), Error);
		return Result;
	}

	int32 FoundIdx = INDEX_NONE;
	const TArray<FEnhancedActionKeyMapping>& Mappings = MappingContext->GetMappings();
	for (int32 Idx = 0; Idx < Mappings.Num(); ++Idx)
	{
		if (Mappings[Idx].Action == InputAction && Mappings[Idx].Key == Key)
		{
			FoundIdx = Idx;
			break;
		}
	}

	if (FoundIdx == INDEX_NONE)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureInputMappingModifiersTriggers"),
			FString::Printf(TEXT("No mapping found for action '%s' and key '%s' in '%s'."),
				*InputAction->GetName(), *Key.ToString(), *MappingContext->GetPathName()));
		return Result;
	}

	TArray<TObjectPtr<UInputModifier>> BuiltModifiers;
	if (Modifiers.IsSet() && !AgentFrameworkEnhancedInputInternal::BuildValidatedModifiers(*Modifiers, BuiltModifiers, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureInputMappingModifiersTriggers"), Error);
		return Result;
	}

	TArray<TObjectPtr<UInputTrigger>> BuiltTriggers;
	if (Triggers.IsSet() && !AgentFrameworkEnhancedInputInternal::BuildValidatedTriggers(*Triggers, BuiltTriggers, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureInputMappingModifiersTriggers"), Error);
		return Result;
	}

	MappingContext->Modify();
	AgentFrameworkEnhancedInputInternal::ReparentObjectsToContext(MappingContext, BuiltModifiers, BuiltTriggers);

	FEnhancedActionKeyMapping& MutableMapping = MappingContext->GetMapping(FoundIdx);
	if (Modifiers.IsSet())
	{
		MutableMapping.Modifiers = MoveTemp(BuiltModifiers);
	}
	if (Triggers.IsSet())
	{
		MutableMapping.Triggers = MoveTemp(BuiltTriggers);
	}

	MappingContext->PostEditChange();
	UEnhancedInputLibrary::RequestRebuildControlMappingsUsingContext(MappingContext, true);

	if (!UAgentFrameworkActionUtils::SaveAssetChecked(MappingContext->GetPackage(), MappingContext, false, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureInputMappingModifiersTriggers"), Error);
		return Result;
	}

	Result.MappingContextPath = MappingContext->GetPathName();
	Result.InputActionPath = InputAction->GetPathName();
	Result.KeyName = Key.ToString();
	Result.ModifierCount = MutableMapping.Modifiers.Num();
	Result.TriggerCount = MutableMapping.Triggers.Num();
	Result.TotalMappings = MappingContext->GetMappings().Num();
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("ConfigureInputMappingModifiersTriggers"), 0.0, true, {});
	return Result;
}
