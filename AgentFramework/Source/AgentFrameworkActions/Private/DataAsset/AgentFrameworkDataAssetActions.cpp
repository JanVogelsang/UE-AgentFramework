// Copyright 2026 AgentFramework. All Rights Reserved.

#include "DataAsset/AgentFrameworkDataAssetActions.h"
#include "AgentFrameworkActionUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/DataAssetFactory.h"
#include "Engine/DataAsset.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Sound/SoundBase.h"
#endif

#define LOCTEXT_NAMESPACE "AgentFrameworkDataAssetActions"

namespace
{
	FString FormatJsonObjectToUnrealText(const TSharedPtr<FJsonObject>& Obj);

	FString FormatJsonValueToUnrealText(const TSharedPtr<FJsonValue>& Val)
	{
		if (!Val.IsValid() || Val->IsNull()) return TEXT("");
		if (Val->Type == EJson::String) return Val->AsString();
		if (Val->Type == EJson::Number) return FString::Printf(TEXT("%f"), Val->AsNumber());
		if (Val->Type == EJson::Boolean) return Val->AsBool() ? TEXT("True") : TEXT("False");
		if (Val->Type == EJson::Object) return FormatJsonObjectToUnrealText(Val->AsObject());
		if (Val->Type == EJson::Array)
		{
			FString OutStr = TEXT("(");
			bool bFirst = true;
			for (const auto& Elem : Val->AsArray())
			{
				if (!bFirst) OutStr += TEXT(",");
				bFirst = false;
				OutStr += FormatJsonValueToUnrealText(Elem);
			}
			OutStr += TEXT(")");
			return OutStr;
		}
		return TEXT("");
	}

	FString FormatJsonObjectToUnrealText(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid()) return TEXT("()");
		FString OutStr = TEXT("(");
		bool bFirst = true;
		for (const auto& Pair : Obj->Values)
		{
			if (!bFirst) OutStr += TEXT(",");
			bFirst = false;
			OutStr += FString(Pair.Key) + TEXT("=") + FormatJsonValueToUnrealText(Pair.Value);
		}
		OutStr += TEXT(")");
		return OutStr;
	}
}

FAgentFrameworkDataAssetActions::FAgentFrameworkDataAssetActions() {}
FAgentFrameworkDataAssetActions::~FAgentFrameworkDataAssetActions() {}

FName FAgentFrameworkDataAssetActions::GetActionName() const { return FName(TEXT("DataAsset")); }

TArray<FString> FAgentFrameworkDataAssetActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_data_asset"),
		TEXT("set_data_asset_properties"),
		TEXT("set_uobject_properties"),
		TEXT("add_instanced_subobject"),
		TEXT("get_data_asset_info")
	};
}

bool FAgentFrameworkDataAssetActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAgentFrameworkActionResult FAgentFrameworkDataAssetActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAgentFrameworkActionResult Result;
	Result.bSuccess = false;

	FString Action;
	TArray<FString> TempErrors;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("action"), Action, TempErrors, false) || Action.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("tool_name"), Action, TempErrors, false);
	}

	if (Action == TEXT("create_data_asset"))
	{
		Result = ExecuteCreateDataAsset(Params, Result);
	}
	else if (Action == TEXT("set_data_asset_properties") || Action == TEXT("set_uobject_properties"))
	{
		Result = ExecuteSetDataAssetProperties(Params, Result);
	}
	else if (Action == TEXT("add_instanced_subobject"))
	{
		Result = ExecuteAddInstancedSubobject(Params, Result);
	}
	else if (Action == TEXT("get_data_asset_info"))
	{
		Result = ExecuteGetDataAssetInfo(Params, Result);
	}
	else
	{
		Result.Errors.Add(TEXT("Could not determine DataAsset action."));
	}

	if (Result.bSuccess)
	{
		PlaySuccessSound();
	}

	return Result;
}

int32 FAgentFrameworkDataAssetActions::SetObjectPropertiesFromJsonObject(
	UObject* TargetObject, const TSharedPtr<FJsonObject>& PropertiesObj, FAgentFrameworkActionResult& Result)
{
	if (!IsValid(TargetObject) || !PropertiesObj.IsValid())
	{
		return 0;
	}

	UClass* TargetClass = TargetObject->GetClass();
	if (!IsValid(TargetClass))
	{
		return 0;
	}

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

		FString ValueString = FormatJsonValueToUnrealText(Pair.Value);

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
			Result.Warnings.Add(FString::Printf(TEXT("Failed to import text '%s' for property '%s' on '%s'."), *ValueString, *PropName, *TargetObject->GetName()));
		}
	}

	return ModifiedCount;
}

FAgentFrameworkActionResult FAgentFrameworkDataAssetActions::ExecuteCreateDataAsset(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	TArray<FString> TempErrors;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, TempErrors, false) || AssetPath.IsEmpty())
	{
		FString PackagePath, AssetName;
		if (UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("package_path"), PackagePath, TempErrors, false) &&
			UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_name"), AssetName, TempErrors, false) &&
			!PackagePath.IsEmpty() && !AssetName.IsEmpty())
		{
			AssetPath = PackagePath.EndsWith(TEXT("/")) ? (PackagePath + AssetName) : (PackagePath + TEXT("/") + AssetName);
		}
	}

	if (AssetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: asset_path (or package_path and asset_name)."));
		return Result;
	}

	FString ClassName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("class_name"), ClassName, TempErrors, false) || ClassName.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("data_asset_class"), ClassName, TempErrors, false);
	}

	if (ClassName.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: class_name (or data_asset_class)."));
		return Result;
	}

	// Try finding the class
	UClass* TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	if (!IsValid(TargetClass))
	{
		if (ClassName.StartsWith(TEXT("U")) || ClassName.StartsWith(TEXT("A")))
		{
			TargetClass = FindFirstObject<UClass>(*ClassName.RightChop(1), EFindFirstObjectOptions::NativeFirst);
		}
	}
	if (!IsValid(TargetClass))
	{
		TargetClass = StaticLoadClass(UDataAsset::StaticClass(), nullptr, *ClassName);
	}

	if (!IsValid(TargetClass))
	{
		Result.Errors.Add(FString::Printf(TEXT("DataAsset class '%s' not found. If this is a Blueprint class, ensure the path ends with '_C'."), *ClassName));
		return Result;
	}

	if (!TargetClass->IsChildOf(UDataAsset::StaticClass()))
	{
		Result.Errors.Add(FString::Printf(TEXT("Class '%s' does not derive from UDataAsset."), *ClassName));
		return Result;
	}

	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName = FPackageName::GetLongPackageAssetName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
	if (!IsValid(Factory))
	{
		Result.Errors.Add(TEXT("Failed to create UDataAssetFactory."));
		return Result;
	}
	Factory->DataAssetClass = TargetClass;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, TargetClass, Factory);
	if (!IsValid(NewAsset))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Data Asset at '%s'."), *AssetPath));
		return Result;
	}

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(TEXT("Successfully created Data Asset '%s' of class '%s'."), *AssetPath, *TargetClass->GetName());
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkDataAssetActions::ExecuteSetDataAssetProperties(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	UObject* TargetObject = LoadObject<UObject>(nullptr, *AssetPath);
	if (!IsValid(TargetObject))
	{
		Result.Errors.Add(FString::Printf(TEXT("Target object / Data Asset not found at '%s'. Create it first with create_data_asset."), *AssetPath));
		return Result;
	}

	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (!UAgentFrameworkActionUtils::TryGetObjectParam(Params, TEXT("properties"), PropertiesObj, Result.Errors, true))
	{
		return Result;
	}

	TargetObject->Modify();
	int32 ModifiedCount = SetObjectPropertiesFromJsonObject(TargetObject, *PropertiesObj, Result);

	TargetObject->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(AssetPath);
	Result.ResultMessage = FString::Printf(TEXT("Successfully updated %d properties on Data Asset '%s'."), ModifiedCount, *AssetPath);
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkDataAssetActions::ExecuteAddInstancedSubobject(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString ParentAssetPath;
	TArray<FString> TempErrors;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("parent_asset_path"), ParentAssetPath, TempErrors, false) || ParentAssetPath.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), ParentAssetPath, TempErrors, false);
	}

	if (ParentAssetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: parent_asset_path (or asset_path)."));
		return Result;
	}

	FString PropertyName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("array_property_name"), PropertyName, TempErrors, false) || PropertyName.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("property_name"), PropertyName, TempErrors, false);
	}

	if (PropertyName.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: array_property_name (or property_name)."));
		return Result;
	}

	FString SubobjectClassName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("subobject_class"), SubobjectClassName, TempErrors, false) || SubobjectClassName.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("class_name"), SubobjectClassName, TempErrors, false);
	}

	if (SubobjectClassName.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: subobject_class (or class_name)."));
		return Result;
	}

	UObject* ParentObject = LoadObject<UObject>(nullptr, *ParentAssetPath);
	if (!IsValid(ParentObject))
	{
		Result.Errors.Add(FString::Printf(TEXT("Parent asset / UObject not found at '%s'."), *ParentAssetPath));
		return Result;
	}

	UClass* SubobjectClass = FindFirstObject<UClass>(*SubobjectClassName, EFindFirstObjectOptions::NativeFirst);
	if (!IsValid(SubobjectClass))
	{
		if (SubobjectClassName.StartsWith(TEXT("U")) || SubobjectClassName.StartsWith(TEXT("A")))
		{
			SubobjectClass = FindFirstObject<UClass>(*SubobjectClassName.RightChop(1), EFindFirstObjectOptions::NativeFirst);
		}
	}
	if (!IsValid(SubobjectClass))
	{
		SubobjectClass = StaticLoadClass(UObject::StaticClass(), nullptr, *SubobjectClassName);
	}

	if (!IsValid(SubobjectClass))
	{
		Result.Errors.Add(FString::Printf(TEXT("Subobject class '%s' not found."), *SubobjectClassName));
		return Result;
	}

	UClass* ParentClass = ParentObject->GetClass();
	FProperty* TargetProp = ParentClass->FindPropertyByName(FName(*PropertyName));
	if (!TargetProp)
	{
		for (TFieldIterator<FProperty> It(ParentClass); It; ++It)
		{
			if (It->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				TargetProp = *It;
				break;
			}
		}
	}

	if (!TargetProp)
	{
		Result.Errors.Add(FString::Printf(TEXT("Property '%s' not found on parent class '%s'."), *PropertyName, *ParentClass->GetName()));
		return Result;
	}

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(TargetProp);
	FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(TargetProp);

	if (ArrayProp)
	{
		FObjectPropertyBase* InnerObjProp = CastField<FObjectPropertyBase>(ArrayProp->Inner);
		if (!InnerObjProp)
		{
			Result.Errors.Add(FString::Printf(TEXT("Array property '%s' inner type is not a UObject pointer."), *PropertyName));
			return Result;
		}
	}
	else if (!ObjProp)
	{
		Result.Errors.Add(FString::Printf(TEXT("Property '%s' is neither a TArray of UObject pointers nor a UObject pointer property."), *PropertyName));
		return Result;
	}

	ParentObject->Modify();

	UObject* NewSubobject = NewObject<UObject>(ParentObject, SubobjectClass, NAME_None, RF_Public | RF_Transactional);
	if (!IsValid(NewSubobject))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to instantiate subobject of class '%s'."), *SubobjectClass->GetName()));
		return Result;
	}

	NewSubobject->Modify();

	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropertiesObj) && PropertiesObj && (*PropertiesObj).IsValid())
	{
		SetObjectPropertiesFromJsonObject(NewSubobject, *PropertiesObj, Result);
	}

	if (ArrayProp)
	{
		FObjectPropertyBase* InnerObjProp = CastField<FObjectPropertyBase>(ArrayProp->Inner);
		void* ArrayAddr = ArrayProp->ContainerPtrToValuePtr<void>(ParentObject);
		FScriptArrayHelper ArrayHelper(ArrayProp, ArrayAddr);
		int32 NewIndex = ArrayHelper.AddValue();
		InnerObjProp->SetObjectPropertyValue(ArrayHelper.GetRawPtr(NewIndex), NewSubobject);
	}
	else if (ObjProp)
	{
		void* ObjAddr = ObjProp->ContainerPtrToValuePtr<void>(ParentObject);
		ObjProp->SetObjectPropertyValue(ObjAddr, NewSubobject);
	}

	FPropertyChangedEvent ChangedEvent(TargetProp);
	ParentObject->PostEditChangeProperty(ChangedEvent);
	ParentObject->MarkPackageDirty();

	Result.bSuccess = true;
	Result.ModifiedAssets.Add(ParentAssetPath);
	Result.ResultMessage = FString::Printf(TEXT("Successfully added subobject of class '%s' to property '%s' on '%s'."),
		*SubobjectClass->GetName(), *TargetProp->GetName(), *ParentAssetPath);
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkDataAssetActions::ExecuteGetDataAssetInfo(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	UDataAsset* DataAsset = LoadObject<UDataAsset>(nullptr, *AssetPath);
	if (!IsValid(DataAsset))
	{
		Result.Errors.Add(FString::Printf(TEXT("Data Asset not found at '%s'."), *AssetPath));
		return Result;
	}

	UClass* DataAssetClass = DataAsset->GetClass();
	if (!IsValid(DataAssetClass))
	{
		Result.Errors.Add(TEXT("Data Asset has an invalid class."));
		return Result;
	}

	TSharedPtr<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
	ResponseObj->SetStringField(TEXT("asset_path"), AssetPath);
	ResponseObj->SetStringField(TEXT("class_name"), DataAssetClass->GetName());

	TSharedPtr<FJsonObject> PropertiesObj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(DataAssetClass); It; ++It)
	{
		FProperty* Prop = *It;
		if (Prop)
		{
			// Skip properties belonging to base UObject, UDataAsset, or UPrimaryDataAsset
			UClass* OwnerClass = Prop->GetOwnerClass();
			if (IsValid(OwnerClass) && (OwnerClass == UObject::StaticClass() || OwnerClass == UDataAsset::StaticClass() || OwnerClass == UPrimaryDataAsset::StaticClass()))
			{
				continue;
			}

			FString ValueStr;
			void* PropAddr = Prop->ContainerPtrToValuePtr<void>(DataAsset);
			Prop->ExportTextItem_Direct(ValueStr, PropAddr, nullptr, nullptr, PPF_None);
			PropertiesObj->SetStringField(Prop->GetName(), ValueStr);
		}
	}
	ResponseObj->SetObjectField(TEXT("properties"), PropertiesObj);

	FString ResponseString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ResponseObj.ToSharedRef(), Writer);

	Result.bSuccess = true;
	Result.ResultMessage = ResponseString;
	return Result;
}

void FAgentFrameworkDataAssetActions::PlaySuccessSound()
{
#if WITH_EDITOR
	if (IsValid(GEditor))
	{
		USoundBase* SuccessSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileSuccess.CompileSuccess"));
		if (IsValid(SuccessSound))
		{
			GEditor->PlayEditorSound(SuccessSound);
		}
	}
#endif
}

#undef LOCTEXT_NAMESPACE

