// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Toolsets/AgentFrameworkGASToolset.h"
#include "AgentFrameworkActionUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/FieldPath.h"
#include "UObject/Package.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace AgentFrameworkGASInternal
{
	static FString NormalizeAssetPackageInput(const FString& InPath)
	{
		const FString Trimmed = InPath.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return Trimmed;
		}
		return Trimmed.StartsWith(TEXT("/")) ? Trimmed : (TEXT("/Game/") + Trimmed);
	}

	static bool IsValidClass(const UClass* Cls)
	{
		if (!Cls || Cls->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			return false;
		}
		const FString Name = Cls->GetName();
		return !Name.StartsWith(TEXT("SKEL_"))
			&& !Name.StartsWith(TEXT("TRASHCLASS_"))
			&& !Name.StartsWith(TEXT("REINST_"))
			&& !Name.StartsWith(TEXT("HOTRELOADED_"));
	}

	static bool ResolveEnumNameFromProperty(
		FProperty* Prop,
		const FString& RequestedName,
		int64& OutValue,
		FString& OutCanonicalName,
		FString& OutError)
	{
		UEnum* Enum = nullptr;
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			Enum = EnumProp->GetEnum();
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			Enum = ByteProp->Enum;
		}

		if (!Enum)
		{
			OutError = FString::Printf(TEXT("Property '%s' has no backing UEnum."), Prop ? *Prop->GetName() : TEXT("<null>"));
			return false;
		}

		const FString Trimmed = RequestedName.TrimStartAndEnd();
		TArray<FString> ValidNames;
		for (int32 Idx = 0; Idx < Enum->NumEnums(); ++Idx)
		{
			if (Enum->ContainsExistingMax() && Idx == Enum->NumEnums() - 1)
			{
				continue;
			}
			const FString EntryName = Enum->GetNameStringByIndex(Idx);
			if (EntryName.Equals(TEXT("Max"), ESearchCase::IgnoreCase)
				|| EntryName.EndsWith(TEXT("_MAX"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			ValidNames.Add(EntryName);
			if (EntryName.Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				OutValue = Enum->GetValueByIndex(Idx);
				OutCanonicalName = EntryName;
				return true;
			}
		}

		OutError = FString::Printf(
			TEXT("Invalid enum value '%s' for '%s'. Valid options: %s."),
			*RequestedName, *Enum->GetName(), *FString::Join(ValidNames, TEXT(", ")));
		return false;
	}

	static bool SetEnumPropertyValue(FProperty* Prop, void* ContainerPtr, int64 Value)
	{
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(EnumProp->ContainerPtrToValuePtr<void>(ContainerPtr), Value);
			return true;
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			ByteProp->SetPropertyValue_InContainer(ContainerPtr, static_cast<uint8>(Value));
			return true;
		}
		return false;
	}

	static UClass* FindAttributeSetSubclass(const FString& ClassToken, UClass* AttributeSetBaseClass, FString& OutError)
	{
		const FString Trimmed = ClassToken.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutError = TEXT("AttributeSet class name cannot be empty.");
			return nullptr;
		}

		const FString WithUPrefix = Trimmed.StartsWith(TEXT("U")) ? Trimmed : (TEXT("U") + Trimmed);
		const FString WithoutUPrefix = (Trimmed.StartsWith(TEXT("U")) && Trimmed.Len() > 1) ? Trimmed.RightChop(1) : Trimmed;

		TArray<UClass*> Matches;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (!IsValidClass(Candidate) || !Candidate->IsChildOf(AttributeSetBaseClass))
			{
				continue;
			}

			const FString ClsName = Candidate->GetName();
			if (ClsName.Equals(Trimmed, ESearchCase::IgnoreCase)
				|| ClsName.Equals(WithoutUPrefix, ESearchCase::IgnoreCase)
				|| ClsName.Equals(WithUPrefix, ESearchCase::IgnoreCase))
			{
				Matches.Add(Candidate);
			}
		}

		if (Matches.Num() == 0)
		{
			OutError = FString::Printf(TEXT("AttributeSet class '%s' not found."), *ClassToken);
			return nullptr;
		}
		if (Matches.Num() > 1)
		{
			OutError = FString::Printf(TEXT("AttributeSet class name '%s' matched multiple classes."), *ClassToken);
			return nullptr;
		}
		return Matches[0];
	}

	static bool BuildTagContainerImportText(const TArray<FString>& CleanTags, FString& OutImportText)
	{
		TArray<FString> Entries;
		Entries.Reserve(CleanTags.Num());
		for (const FString& Tag : CleanTags)
		{
			Entries.Add(FString::Printf(TEXT("(TagName=\"%s\")"), *Tag));
		}
		OutImportText = FString::Printf(TEXT("(GameplayTags=(%s))"), *FString::Join(Entries, TEXT(",")));
		return true;
	}

	static bool PopulateAndValidateTagContainer(
		FStructProperty* AddedContainerProp,
		void* AddedContainerPtr,
		const TArray<FString>& CleanTags,
		FString& OutError)
	{
		FString BulkText;
		BuildTagContainerImportText(CleanTags, BulkText);

		if (!AddedContainerProp->ImportText_Direct(*BulkText, AddedContainerPtr, nullptr, PPF_None))
		{
			OutError = TEXT("Failed to parse GameplayTag container import text.");
			return false;
		}

		FArrayProperty* GameplayTagsArrayProp = CastField<FArrayProperty>(
			AddedContainerProp->Struct->FindPropertyByName(TEXT("GameplayTags")));
		if (!GameplayTagsArrayProp)
		{
			OutError = TEXT("FGameplayTagContainer missing expected 'GameplayTags' property.");
			return false;
		}

		FScriptArrayHelper ArrayHelper(GameplayTagsArrayProp, GameplayTagsArrayProp->ContainerPtrToValuePtr<void>(AddedContainerPtr));
		if (ArrayHelper.Num() == CleanTags.Num())
		{
			return true;
		}

		// Probe each tag individually to identify unregistered GameplayTags accurately.
		TArray<FString> UnresolvedTags;
		for (const FString& SingleTag : CleanTags)
		{
			FStructOnScope TempContainer(AddedContainerProp->Struct);
			FString SingleText;
			BuildTagContainerImportText({ SingleTag }, SingleText);
			AddedContainerProp->ImportText_Direct(*SingleText, TempContainer.GetStructMemory(), nullptr, PPF_None);
			FScriptArrayHelper SingleHelper(GameplayTagsArrayProp, GameplayTagsArrayProp->ContainerPtrToValuePtr<void>(TempContainer.GetStructMemory()));
			if (SingleHelper.Num() == 0)
			{
				UnresolvedTags.Add(SingleTag);
			}
		}

		OutError = FString::Printf(
			TEXT("One or more GrantedTags could not be resolved as registered GameplayTags: %s."),
			*FString::Join(UnresolvedTags, TEXT(", ")));
		return false;
	}
}

FAgentFrameworkCppClassResult UAgentFrameworkGASAuthoringToolset::CreateAttributeSetClass(
	const FString& ClassName,
	const TArray<FAgentFrameworkAttributeSpec>& Attributes,
	const FString& ModuleName,
	const FString& SubDirectory)
{
	FAgentFrameworkCppClassResult Result;

	FString BaseClassName = ClassName.TrimStartAndEnd();
	if (BaseClassName.StartsWith(TEXT("U")) && BaseClassName.Len() > 1 && FChar::IsUpper(BaseClassName[1]))
	{
		BaseClassName.RightChopInline(1);
	}
	if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(BaseClassName))
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateAttributeSetClass"),
			FString::Printf(TEXT("Invalid C++ AttributeSet class name '%s'."), *ClassName));
		return Result;
	}

	if (Attributes.Num() == 0)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAttributeSetClass"), TEXT("Attributes array must not be empty."));
		return Result;
	}

	struct FValidatedAttribute
	{
		FString Name;
		FString DefaultLiteral;
		bool bReplicated = true;
		FString MaxAttributeName;
	};

	TArray<FValidatedAttribute> ValidatedAttrs;
	TSet<FString> SeenNames;
	bool bAnyReplicated = false;

	for (int32 Index = 0; Index < Attributes.Num(); ++Index)
	{
		const FAgentFrameworkAttributeSpec& Spec = Attributes[Index];
		const FString AttrName = Spec.Name.TrimStartAndEnd();
		if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(AttrName))
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CreateAttributeSetClass"),
				FString::Printf(TEXT("Invalid attribute identifier '%s' at index %d."), *Spec.Name, Index));
			return Result;
		}
		if (SeenNames.Contains(AttrName))
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CreateAttributeSetClass"),
				FString::Printf(TEXT("Duplicate attribute name '%s'."), *AttrName));
			return Result;
		}
		SeenNames.Add(AttrName);

		FString DefaultLit;
		if (!UAgentFrameworkActionUtils::FormatCppFloatLiteral(Spec.DefaultValue, DefaultLit))
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CreateAttributeSetClass"),
				FString::Printf(TEXT("Attribute '%s' has non-finite DefaultValue."), *AttrName));
			return Result;
		}

		const FString MaxAttr = Spec.MaxAttributeName.TrimStartAndEnd();
		if (!MaxAttr.IsEmpty())
		{
			if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(MaxAttr))
			{
				UAgentFrameworkActionUtils::RaiseToolError(
					TEXT("CreateAttributeSetClass"),
					FString::Printf(TEXT("Attribute '%s' has invalid MaxAttributeName '%s'."), *AttrName, *Spec.MaxAttributeName));
				return Result;
			}
			if (MaxAttr.Equals(AttrName, ESearchCase::IgnoreCase))
			{
				UAgentFrameworkActionUtils::RaiseToolError(
					TEXT("CreateAttributeSetClass"),
					FString::Printf(TEXT("Attribute '%s' cannot clamp to itself via MaxAttributeName."), *AttrName));
				return Result;
			}
		}

		if (Spec.bReplicated)
		{
			bAnyReplicated = true;
		}

		FValidatedAttribute Entry;
		Entry.Name = AttrName;
		Entry.DefaultLiteral = DefaultLit;
		Entry.bReplicated = Spec.bReplicated;
		Entry.MaxAttributeName = MaxAttr;
		ValidatedAttrs.Add(MoveTemp(Entry));
	}

	// Ensure every referenced MaxAttributeName exists in this AttributeSet so generated C++ compiles cleanly.
	for (const FValidatedAttribute& Entry : ValidatedAttrs)
	{
		if (!Entry.MaxAttributeName.IsEmpty() && !SeenNames.Contains(Entry.MaxAttributeName))
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CreateAttributeSetClass"),
				FString::Printf(
					TEXT("Attribute '%s' references MaxAttributeName '%s', which is missing from Attributes."),
					*Entry.Name, *Entry.MaxAttributeName));
			return Result;
		}
	}

	FString ResolvedModuleName;
	FString HeaderDir;
	FString SourceDir;
	FString IncludePrefix;
	FString ApiMacro;
	FString Error;
	if (!UAgentFrameworkActionUtils::ResolveWritableModulePaths(
		ModuleName, SubDirectory,
		ResolvedModuleName, HeaderDir, SourceDir, IncludePrefix, ApiMacro, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAttributeSetClass"), Error);
		return Result;
	}

	const FString HeaderPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(HeaderDir, BaseClassName + TEXT(".h")));
	const FString SourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SourceDir, BaseClassName + TEXT(".cpp")));

	if (!UAgentFrameworkActionUtils::IsWritableSourcePath(HeaderPath)
		|| !UAgentFrameworkActionUtils::IsWritableSourcePath(SourcePath))
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateAttributeSetClass"),
			FString::Printf(TEXT("Target paths are outside writable project Source/Plugin directories: '%s', '%s'."), *HeaderPath, *SourcePath));
		return Result;
	}

	if (FPaths::FileExists(HeaderPath) || FPaths::FileExists(SourcePath))
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateAttributeSetClass"),
			FString::Printf(TEXT("AttributeSet files already exist for '%s' (%s / %s)."), *BaseClassName, *HeaderPath, *SourcePath));
		return Result;
	}

	const FString FullClassName = TEXT("U") + BaseClassName;

	FString HeaderContent = TEXT("// Copyright AgentFramework. All Rights Reserved.\n\n#pragma once\n\n");
	HeaderContent += TEXT("#include \"CoreMinimal.h\"\n");
	HeaderContent += TEXT("#include \"AttributeSet.h\"\n");
	HeaderContent += TEXT("#include \"AbilitySystemComponent.h\"\n");
	HeaderContent += FString::Printf(TEXT("#include \"%s.generated.h\"\n\n"), *BaseClassName);
	HeaderContent += TEXT("#ifndef ATTRIBUTE_ACCESSORS\n");
	HeaderContent += TEXT("#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \\\n");
	HeaderContent += TEXT("\tGAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \\\n");
	HeaderContent += TEXT("\tGAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \\\n");
	HeaderContent += TEXT("\tGAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \\\n");
	HeaderContent += TEXT("\tGAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)\n");
	HeaderContent += TEXT("#endif\n\n");
	HeaderContent += TEXT("UCLASS()\n");
	HeaderContent += FString::Printf(TEXT("class %s %s : public UAttributeSet\n{\n\tGENERATED_BODY()\n\npublic:\n\t%s();\n\n"),
		*ApiMacro, *FullClassName, *FullClassName);
	if (bAnyReplicated)
	{
		HeaderContent += TEXT("\tvirtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;\n");
	}
	HeaderContent += TEXT("\tvirtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;\n\n");

	for (const FValidatedAttribute& Attr : ValidatedAttrs)
	{
		if (Attr.bReplicated)
		{
			HeaderContent += FString::Printf(TEXT("\tUPROPERTY(BlueprintReadOnly, Category = \"Attributes\", ReplicatedUsing = OnRep_%s)\n"), *Attr.Name);
		}
		else
		{
			HeaderContent += TEXT("\tUPROPERTY(BlueprintReadOnly, Category = \"Attributes\")\n");
		}
		HeaderContent += FString::Printf(TEXT("\tFGameplayAttributeData %s;\n\tATTRIBUTE_ACCESSORS(%s, %s)\n\n"),
			*Attr.Name, *FullClassName, *Attr.Name);
	}

	if (bAnyReplicated)
	{
		HeaderContent += TEXT("protected:\n");
		for (const FValidatedAttribute& Attr : ValidatedAttrs)
		{
			if (Attr.bReplicated)
			{
				HeaderContent += FString::Printf(TEXT("\tUFUNCTION()\n\tvirtual void OnRep_%s(const FGameplayAttributeData& Old%s);\n\n"), *Attr.Name, *Attr.Name);
			}
		}
	}
	HeaderContent += TEXT("};\n");

	FString SourceContent = FString::Printf(TEXT("// Copyright AgentFramework. All Rights Reserved.\n\n#include \"%s%s.h\"\n"), *IncludePrefix, *BaseClassName);
	SourceContent += TEXT("#include \"GameplayEffectExtension.h\"\n");
	if (bAnyReplicated)
	{
		SourceContent += TEXT("#include \"Net/UnrealNetwork.h\"\n");
	}
	SourceContent += TEXT("\n");
	SourceContent += FString::Printf(TEXT("%s::%s()\n{\n"), *FullClassName, *FullClassName);
	for (const FValidatedAttribute& Attr : ValidatedAttrs)
	{
		SourceContent += FString::Printf(TEXT("\tInit%s(%s);\n"), *Attr.Name, *Attr.DefaultLiteral);
	}
	SourceContent += TEXT("}\n\n");

	if (bAnyReplicated)
	{
		SourceContent += FString::Printf(
			TEXT("void %s::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const\n{\n\tSuper::GetLifetimeReplicatedProps(OutLifetimeProps);\n\n"),
			*FullClassName);
		for (const FValidatedAttribute& Attr : ValidatedAttrs)
		{
			if (Attr.bReplicated)
			{
				SourceContent += FString::Printf(
					TEXT("\tDOREPLIFETIME_CONDITION_NOTIFY(%s, %s, COND_None, REPNOTIFY_Always);\n"),
					*FullClassName, *Attr.Name);
			}
		}
		SourceContent += TEXT("}\n\n");
	}

	SourceContent += FString::Printf(
		TEXT("void %s::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)\n{\n\tSuper::PostGameplayEffectExecute(Data);\n"),
		*FullClassName);
	for (const FValidatedAttribute& Attr : ValidatedAttrs)
	{
		if (!Attr.MaxAttributeName.IsEmpty())
		{
			SourceContent += FString::Printf(
				TEXT("\tif (Data.EvaluatedData.Attribute == Get%sAttribute())\n\t{\n\t\tSet%s(FMath::Clamp(Get%s(), 0.0f, Get%s()));\n\t}\n"),
				*Attr.Name, *Attr.Name, *Attr.Name, *Attr.MaxAttributeName);
		}
	}
	SourceContent += TEXT("}\n");

	for (const FValidatedAttribute& Attr : ValidatedAttrs)
	{
		if (Attr.bReplicated)
		{
			SourceContent += FString::Printf(
				TEXT("\nvoid %s::OnRep_%s(const FGameplayAttributeData& Old%s)\n{\n\tGAMEPLAYATTRIBUTE_REPNOTIFY(%s, %s, Old%s);\n}\n"),
				*FullClassName, *Attr.Name, *Attr.Name, *FullClassName, *Attr.Name, *Attr.Name);
		}
	}

	if (!UAgentFrameworkCppToolset::ValidateCodeSafety(HeaderContent, Error)
		|| !UAgentFrameworkCppToolset::ValidateCodeSafety(SourceContent, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAttributeSetClass"), Error);
		return Result;
	}

	IFileManager::Get().MakeDirectory(*HeaderDir, true);
	IFileManager::Get().MakeDirectory(*SourceDir, true);

	if (!FFileHelper::SaveStringToFile(HeaderContent, *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateAttributeSetClass"),
			FString::Printf(TEXT("Failed to write header file '%s'."), *HeaderPath));
		return Result;
	}

	if (!FFileHelper::SaveStringToFile(SourceContent, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*HeaderPath, false, true);
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateAttributeSetClass"),
			FString::Printf(TEXT("Failed to write source file '%s'."), *SourcePath));
		return Result;
	}

	Result.ClassName = BaseClassName;
	Result.PrefixedClassName = FullClassName;
	Result.ParentClassName = TEXT("UAttributeSet");
	Result.ModuleName = ResolvedModuleName;
	Result.HeaderPath = HeaderPath;
	Result.SourcePath = SourcePath;
	Result.bRequiresRebuild = true;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateAttributeSetClass"), 0.0, true, {});
	return Result;
}

FAgentFrameworkGameplayEffectResult UAgentFrameworkGASAuthoringToolset::CreateGameplayEffectBlueprint(
	const FString& AssetPath,
	const TOptional<FString>& DurationPolicy,
	TOptional<float> DurationSeconds,
	TOptional<float> PeriodSeconds,
	const TOptional<TArray<FAgentFrameworkGameplayModifierSpec>>& Modifiers,
	const TOptional<TArray<FString>>& GrantedTags)
{
	FAgentFrameworkGameplayEffectResult Result;

	const FString NormalizedInputPath = AgentFrameworkGASInternal::NormalizeAssetPackageInput(AssetPath);
	FString PackageName;
	FString AssetName;
	FString Error;
	if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(NormalizedInputPath, PackageName, AssetName, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), Error);
		return Result;
	}

	FModuleManager::Get().LoadModule(TEXT("GameplayAbilities"));
	UClass* GEClass = LoadObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.GameplayEffect"));
	UClass* AttrSetBaseClass = LoadObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.AttributeSet"));
	UClass* TargetTagsCompClass = LoadObject<UClass>(nullptr, TEXT("/Script/GameplayAbilities.TargetTagsGameplayEffectComponent"));
	if (!GEClass || !AttrSetBaseClass || !TargetTagsCompClass)
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateGameplayEffectBlueprint"),
			TEXT("GameplayAbilities classes not found. Ensure the GameplayAbilities plugin is enabled in the project."));
		return Result;
	}

	// Validate DurationPolicy enum via reflection
	FProperty* DurationPolicyProp = GEClass->FindPropertyByName(TEXT("DurationPolicy"));
	const FString RequestedPolicy = DurationPolicy.Get(TEXT("Instant")).TrimStartAndEnd();
	int64 DurationPolicyValue = 0;
	FString CanonicalDurationPolicy;
	if (!DurationPolicyProp
		|| !AgentFrameworkGASInternal::ResolveEnumNameFromProperty(DurationPolicyProp, RequestedPolicy, DurationPolicyValue, CanonicalDurationPolicy, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), Error);
		return Result;
	}

	const bool bIsInstant = CanonicalDurationPolicy.Equals(TEXT("Instant"), ESearchCase::IgnoreCase);
	const bool bIsHasDuration = CanonicalDurationPolicy.Equals(TEXT("HasDuration"), ESearchCase::IgnoreCase);

	const float ResolvedDurationSeconds = DurationSeconds.Get(0.0f);
	const float ResolvedPeriodSeconds = PeriodSeconds.Get(0.0f);

	if (!FMath::IsFinite(ResolvedDurationSeconds) || ResolvedDurationSeconds < 0.0f)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("DurationSeconds must be finite and >= 0."));
		return Result;
	}
	if (!FMath::IsFinite(ResolvedPeriodSeconds) || ResolvedPeriodSeconds < 0.0f)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("PeriodSeconds must be finite and >= 0."));
		return Result;
	}
	if (bIsHasDuration && ResolvedDurationSeconds <= 0.0f)
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateGameplayEffectBlueprint"),
			TEXT("DurationPolicy 'HasDuration' requires DurationSeconds > 0."));
		return Result;
	}
	if (!bIsHasDuration && DurationSeconds.IsSet() && ResolvedDurationSeconds > 0.0f)
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateGameplayEffectBlueprint"),
			FString::Printf(TEXT("DurationSeconds > 0 is only valid with DurationPolicy 'HasDuration' (got '%s')."), *CanonicalDurationPolicy));
		return Result;
	}
	if (bIsInstant && ResolvedPeriodSeconds > 0.0f)
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateGameplayEffectBlueprint"),
			TEXT("PeriodSeconds > 0 is incompatible with DurationPolicy 'Instant'; use 'HasDuration' or 'Infinite'."));
		return Result;
	}

	// Pre-validate Modifiers BEFORE creating any package/asset
	FArrayProperty* ModifiersArrayProp = CastField<FArrayProperty>(GEClass->FindPropertyByName(TEXT("Modifiers")));
	FStructProperty* ModifierElemStructProp = ModifiersArrayProp ? CastField<FStructProperty>(ModifiersArrayProp->Inner) : nullptr;
	UScriptStruct* ModifierInfoStruct = ModifierElemStructProp ? ModifierElemStructProp->Struct : nullptr;

	FStructProperty* AttrFieldStructProp = ModifierInfoStruct ? CastField<FStructProperty>(ModifierInfoStruct->FindPropertyByName(TEXT("Attribute"))) : nullptr;
	FProperty* ModOpProp = ModifierInfoStruct ? ModifierInfoStruct->FindPropertyByName(TEXT("ModifierOp")) : nullptr;
	FStructProperty* ModMagStructProp = ModifierInfoStruct ? CastField<FStructProperty>(ModifierInfoStruct->FindPropertyByName(TEXT("ModifierMagnitude"))) : nullptr;

	struct FValidatedModifier
	{
		UClass* AttributeSetClass = nullptr;
		FProperty* AttributeProp = nullptr;
		int64 ModifierOpValue = 0;
		float Magnitude = 0.0f;
	};
	TArray<FValidatedModifier> ValidatedModifiers;

	if (Modifiers.IsSet() && Modifiers->Num() > 0)
	{
		if (!ModifiersArrayProp || !ModifierInfoStruct || !AttrFieldStructProp || !ModOpProp || !ModMagStructProp)
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CreateGameplayEffectBlueprint"),
				TEXT("UGameplayEffect reflection layout does not match expected Modifiers properties."));
			return Result;
		}

		for (int32 Index = 0; Index < Modifiers->Num(); ++Index)
		{
			const FAgentFrameworkGameplayModifierSpec& Spec = (*Modifiers)[Index];
			if (!FMath::IsFinite(Spec.Magnitude))
			{
				UAgentFrameworkActionUtils::RaiseToolError(
					TEXT("CreateGameplayEffectBlueprint"),
					FString::Printf(TEXT("Modifier[%d] has non-finite Magnitude."), Index));
				return Result;
			}

			const FString AttrRef = Spec.AttributeClassAndName.TrimStartAndEnd();
			FString AttrClassToken;
			FString AttrPropToken;
			if (!AttrRef.Split(TEXT("."), &AttrClassToken, &AttrPropToken)
				|| AttrClassToken.TrimStartAndEnd().IsEmpty()
				|| AttrPropToken.TrimStartAndEnd().IsEmpty())
			{
				UAgentFrameworkActionUtils::RaiseToolError(
					TEXT("CreateGameplayEffectBlueprint"),
					FString::Printf(TEXT("Modifier[%d] AttributeClassAndName '%s' must be formatted as '<AttributeSetClass>.<PropertyName>'."),
						Index, *Spec.AttributeClassAndName));
				return Result;
			}

			UClass* AttrSetClass = AgentFrameworkGASInternal::FindAttributeSetSubclass(AttrClassToken, AttrSetBaseClass, Error);
			if (!AttrSetClass)
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), Error);
				return Result;
			}

			FProperty* TargetProp = AttrSetClass->FindPropertyByName(*AttrPropToken.TrimStartAndEnd());
			const bool bIsAttrDataStruct = [&]()
			{
				if (FStructProperty* SP = CastField<FStructProperty>(TargetProp))
				{
					return SP->Struct && SP->Struct->GetName() == TEXT("GameplayAttributeData");
				}
				return CastField<FNumericProperty>(TargetProp) != nullptr;
			}();

			if (!TargetProp || !bIsAttrDataStruct)
			{
				UAgentFrameworkActionUtils::RaiseToolError(
					TEXT("CreateGameplayEffectBlueprint"),
					FString::Printf(TEXT("Attribute property '%s' not found on AttributeSet class '%s'."),
						*AttrPropToken.TrimStartAndEnd(), *AttrSetClass->GetName()));
				return Result;
			}

			const FString OpStr = Spec.ModifierOp.TrimStartAndEnd().IsEmpty() ? TEXT("AddBase") : Spec.ModifierOp.TrimStartAndEnd();
			int64 OpVal = 0;
			FString CanonicalOp;
			if (!AgentFrameworkGASInternal::ResolveEnumNameFromProperty(ModOpProp, OpStr, OpVal, CanonicalOp, Error))
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), Error);
				return Result;
			}

			FValidatedModifier Validated;
			Validated.AttributeSetClass = AttrSetClass;
			Validated.AttributeProp = TargetProp;
			Validated.ModifierOpValue = OpVal;
			Validated.Magnitude = Spec.Magnitude;
			ValidatedModifiers.Add(Validated);
		}
	}

	// Pre-validate GrantedTags BEFORE creating any package/asset
	TArray<FString> CleanTags;
	FStructProperty* InheritableGrantedProp = CastField<FStructProperty>(
		TargetTagsCompClass->FindPropertyByName(TEXT("InheritableGrantedTagsContainer")));
	FStructProperty* AddedTagsProp = InheritableGrantedProp
		? CastField<FStructProperty>(InheritableGrantedProp->Struct->FindPropertyByName(TEXT("Added")))
		: nullptr;

	if (GrantedTags.IsSet() && GrantedTags->Num() > 0)
	{
		if (bIsInstant)
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CreateGameplayEffectBlueprint"),
				TEXT("GrantedTags cannot be added to an 'Instant' GameplayEffect; use 'HasDuration' or 'Infinite'."));
			return Result;
		}
		if (!InheritableGrantedProp || !AddedTagsProp)
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CreateGameplayEffectBlueprint"),
				TEXT("UTargetTagsGameplayEffectComponent missing InheritableGrantedTagsContainer.Added property."));
			return Result;
		}

		TSet<FString> SeenTags;
		for (const FString& RawTag : *GrantedTags)
		{
			const FString Tag = RawTag.TrimStartAndEnd();
			if (Tag.IsEmpty())
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("GrantedTags cannot contain empty entries."));
				return Result;
			}
			for (TCHAR Ch : Tag)
			{
				if (Ch == TEXT('"') || Ch == TEXT('(') || Ch == TEXT(')') || Ch == TEXT('\r') || Ch == TEXT('\n') || Ch == TEXT('\0'))
				{
					UAgentFrameworkActionUtils::RaiseToolError(
						TEXT("CreateGameplayEffectBlueprint"),
						FString::Printf(TEXT("GrantedTag '%s' contains unsupported character."), *Tag));
					return Result;
				}
			}
			if (!SeenTags.Contains(Tag))
			{
				SeenTags.Add(Tag);
				CleanTags.Add(Tag);
			}
		}

		FStructOnScope ValidationScope(AddedTagsProp->Struct);
		if (!AgentFrameworkGASInternal::PopulateAndValidateTagContainer(AddedTagsProp, ValidationScope.GetStructMemory(), CleanTags, Error))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), Error);
			return Result;
		}
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateGameplayEffectBlueprint"),
			FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return Result;
	}

	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		GEClass,
		Package,
		*AssetName,
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass(),
		FName(TEXT("AgentFrameworkGASToolset")));

	if (!NewBP || !NewBP->GeneratedClass)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, NewBP);
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CreateGameplayEffectBlueprint"),
			FString::Printf(TEXT("Failed to create GameplayEffect Blueprint '%s'."), *AssetName));
		return Result;
	}

	UObject* CDO = NewBP->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, NewBP);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("Created GameplayEffect Blueprint has null CDO."));
		return Result;
	}

	CDO->Modify();

	if (!AgentFrameworkGASInternal::SetEnumPropertyValue(DurationPolicyProp, CDO, DurationPolicyValue))
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, NewBP);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("Failed to assign DurationPolicy on CDO."));
		return Result;
	}

	// Configure DurationMagnitude when HasDuration
	if (bIsHasDuration)
	{
		FStructProperty* DurationMagProp = CastField<FStructProperty>(GEClass->FindPropertyByName(TEXT("DurationMagnitude")));
		if (!DurationMagProp)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, NewBP);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("DurationMagnitude property not found."));
			return Result;
		}
		void* MagPtr = DurationMagProp->ContainerPtrToValuePtr<void>(CDO);
		if (FProperty* CalcTypeProp = DurationMagProp->Struct->FindPropertyByName(TEXT("MagnitudeCalculationType")))
		{
			int64 ScalableFloatEnumVal = 0;
			FString IgnoredCanonical;
			if (AgentFrameworkGASInternal::ResolveEnumNameFromProperty(CalcTypeProp, TEXT("ScalableFloat"), ScalableFloatEnumVal, IgnoredCanonical, Error))
			{
				AgentFrameworkGASInternal::SetEnumPropertyValue(CalcTypeProp, MagPtr, ScalableFloatEnumVal);
			}
		}
		if (FStructProperty* ScalableProp = CastField<FStructProperty>(DurationMagProp->Struct->FindPropertyByName(TEXT("ScalableFloatMagnitude"))))
		{
			void* ScalablePtr = ScalableProp->ContainerPtrToValuePtr<void>(MagPtr);
			if (FFloatProperty* ValueProp = CastField<FFloatProperty>(ScalableProp->Struct->FindPropertyByName(TEXT("Value"))))
			{
				ValueProp->SetPropertyValue_InContainer(ScalablePtr, ResolvedDurationSeconds);
			}
		}
	}

	// Configure Period
	if (ResolvedPeriodSeconds > 0.0f)
	{
		if (FStructProperty* PeriodProp = CastField<FStructProperty>(GEClass->FindPropertyByName(TEXT("Period"))))
		{
			void* PeriodPtr = PeriodProp->ContainerPtrToValuePtr<void>(CDO);
			if (FFloatProperty* ValueProp = CastField<FFloatProperty>(PeriodProp->Struct->FindPropertyByName(TEXT("Value"))))
			{
				ValueProp->SetPropertyValue_InContainer(PeriodPtr, ResolvedPeriodSeconds);
			}
		}
	}

	// Configure Modifiers
	if (ValidatedModifiers.Num() > 0)
	{
		FScriptArrayHelper ModHelper(ModifiersArrayProp, ModifiersArrayProp->ContainerPtrToValuePtr<void>(CDO));
		ModHelper.EmptyAndAddValues(ValidatedModifiers.Num());

		FFieldPathProperty* FieldPathProp = CastField<FFieldPathProperty>(AttrFieldStructProp->Struct->FindPropertyByName(TEXT("Attribute")));
		FStrProperty* AttrNameProp = CastField<FStrProperty>(AttrFieldStructProp->Struct->FindPropertyByName(TEXT("AttributeName")));
		FObjectPropertyBase* AttrOwnerProp = CastField<FObjectPropertyBase>(AttrFieldStructProp->Struct->FindPropertyByName(TEXT("AttributeOwner")));

		FProperty* ModCalcTypeProp = ModMagStructProp->Struct->FindPropertyByName(TEXT("MagnitudeCalculationType"));
		FStructProperty* ModScalableProp = CastField<FStructProperty>(ModMagStructProp->Struct->FindPropertyByName(TEXT("ScalableFloatMagnitude")));
		FFloatProperty* ModScalableValProp = ModScalableProp ? CastField<FFloatProperty>(ModScalableProp->Struct->FindPropertyByName(TEXT("Value"))) : nullptr;

		if (!FieldPathProp || !ModScalableValProp)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, NewBP);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("Missing Attribute or ScalableFloatMagnitude properties on FGameplayModifierInfo."));
			return Result;
		}

		for (int32 Index = 0; Index < ValidatedModifiers.Num(); ++Index)
		{
			const FValidatedModifier& ModSpec = ValidatedModifiers[Index];
			uint8* ModPtr = ModHelper.GetRawPtr(Index);

			void* AttrPtr = AttrFieldStructProp->ContainerPtrToValuePtr<void>(ModPtr);
			FieldPathProp->SetPropertyValue_InContainer(AttrPtr, FFieldPath(ModSpec.AttributeProp));
			if (AttrNameProp)
			{
				AttrNameProp->SetPropertyValue_InContainer(AttrPtr, ModSpec.AttributeProp->GetName());
			}
			if (AttrOwnerProp)
			{
				AttrOwnerProp->SetObjectPropertyValue_InContainer(AttrPtr, ModSpec.AttributeSetClass);
			}

			AgentFrameworkGASInternal::SetEnumPropertyValue(ModOpProp, ModPtr, ModSpec.ModifierOpValue);

			void* MagPtr = ModMagStructProp->ContainerPtrToValuePtr<void>(ModPtr);
			if (ModCalcTypeProp)
			{
				int64 ScalableFloatEnumVal = 0;
				FString IgnoredCanonical;
				if (AgentFrameworkGASInternal::ResolveEnumNameFromProperty(ModCalcTypeProp, TEXT("ScalableFloat"), ScalableFloatEnumVal, IgnoredCanonical, Error))
				{
					AgentFrameworkGASInternal::SetEnumPropertyValue(ModCalcTypeProp, MagPtr, ScalableFloatEnumVal);
				}
			}
			void* ScalablePtr = ModScalableProp->ContainerPtrToValuePtr<void>(MagPtr);
			ModScalableValProp->SetPropertyValue_InContainer(ScalablePtr, ModSpec.Magnitude);
		}
	}

	// Configure GrantedTags via UTargetTagsGameplayEffectComponent
	if (CleanTags.Num() > 0)
	{
		FArrayProperty* GEComponentsProp = CastField<FArrayProperty>(GEClass->FindPropertyByName(TEXT("GEComponents")));
		FObjectPropertyBase* InnerCompProp = GEComponentsProp ? CastField<FObjectPropertyBase>(GEComponentsProp->Inner) : nullptr;
		if (!GEComponentsProp || !InnerCompProp)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, NewBP);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("UGameplayEffect missing GEComponents array property."));
			return Result;
		}

		UObject* TargetTagsComp = NewObject<UObject>(
			CDO, TargetTagsCompClass, NAME_None,
			CDO->GetMaskedFlags(RF_PropagateToSubObjects) | RF_Transactional);
		if (!TargetTagsComp)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, NewBP);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), TEXT("Failed to allocate UTargetTagsGameplayEffectComponent."));
			return Result;
		}

		void* InheritablePtr = InheritableGrantedProp->ContainerPtrToValuePtr<void>(TargetTagsComp);
		void* AddedPtr = AddedTagsProp->ContainerPtrToValuePtr<void>(InheritablePtr);
		if (!AgentFrameworkGASInternal::PopulateAndValidateTagContainer(AddedTagsProp, AddedPtr, CleanTags, Error))
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, NewBP);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), Error);
			return Result;
		}

		FScriptArrayHelper CompHelper(GEComponentsProp, GEComponentsProp->ContainerPtrToValuePtr<void>(CDO));
		const int32 NewIdx = CompHelper.AddValue();
		InnerCompProp->SetObjectPropertyValue(CompHelper.GetRawPtr(NewIdx), TargetTagsComp);

#if WITH_EDITOR
		FPropertyChangedEvent ChangeEvent(InheritableGrantedProp);
		TargetTagsComp->PostEditChangeProperty(ChangeEvent);
#endif
	}

	CDO->PostEditChange();
	FBlueprintEditorUtils::MarkBlueprintAsModified(NewBP);
	FKismetEditorUtilities::CompileBlueprint(NewBP);

	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Package, NewBP, true, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateGameplayEffectBlueprint"), Error);
		return Result;
	}

	Result.AssetPath = NewBP->GetPathName();
	Result.DurationPolicy = CanonicalDurationPolicy;
	Result.DurationSeconds = bIsHasDuration ? ResolvedDurationSeconds : 0.0f;
	Result.PeriodSeconds = ResolvedPeriodSeconds;
	Result.ModifierCount = ValidatedModifiers.Num();
	Result.GrantedTags = CleanTags;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateGameplayEffectBlueprint"), 0.0, true, {});
	return Result;
}
