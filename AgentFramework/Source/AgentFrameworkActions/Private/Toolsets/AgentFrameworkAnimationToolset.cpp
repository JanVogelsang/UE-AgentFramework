// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Toolsets/AgentFrameworkAnimationToolset.h"
#include "AgentFrameworkActionUtils.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendProfile.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Factories/AnimBlueprintFactory.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/Factory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace AgentFrameworkAnimationInternal
{
	static UClass* ResolveAnimInstanceSubClass(const FString& ParentClassName, FString& OutError)
	{
		const FString Trimmed = ParentClassName.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || Trimmed.Equals(TEXT("AnimInstance"), ESearchCase::IgnoreCase) || Trimmed.Equals(TEXT("UAnimInstance"), ESearchCase::IgnoreCase))
		{
			return UAnimInstance::StaticClass();
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (!Candidate || !Candidate->IsChildOf<UAnimInstance>()
				|| Candidate->HasAnyClassFlags(CLASS_NewerVersionExists))
			{
				continue;
			}
			const FString Name = Candidate->GetName();
			if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("TRASHCLASS_")) || Name.StartsWith(TEXT("REINST_")))
			{
				continue;
			}
			if (Name.Equals(Trimmed, ESearchCase::IgnoreCase)
				|| Name.Equals(TEXT("U") + Trimmed, ESearchCase::IgnoreCase)
				|| Candidate->GetPathName().Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}

		OutError = FString::Printf(TEXT("AnimInstance parent class '%s' not found."), *ParentClassName);
		return nullptr;
	}

	template<typename SocketArrayType>
	static void AppendSockets(const SocketArrayType& SocketList, const FString& SourceLabel, TArray<FAgentFrameworkSocketInfo>& OutSockets)
	{
		for (const auto& SocketPtr : SocketList)
		{
			const USkeletalMeshSocket* Socket = SocketPtr;
			if (!Socket)
			{
				continue;
			}
			FAgentFrameworkSocketInfo Entry;
			Entry.SocketName = Socket->SocketName.ToString();
			Entry.BoneName = Socket->BoneName.ToString();
			Entry.RelativeLocation = Socket->RelativeLocation;
			Entry.RelativeRotation = Socket->RelativeRotation;
			Entry.RelativeScale = Socket->RelativeScale;
			Entry.Source = SourceLabel;
			OutSockets.Add(MoveTemp(Entry));
		}
	}

	static void PopulateSkeletonData(const USkeleton* Skeleton, FAgentFrameworkSkeletonInfoResult& OutInfo)
	{
		if (!Skeleton)
		{
			return;
		}
		OutInfo.SkeletonPath = Skeleton->GetPathName();

		const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
		OutInfo.BoneCount = RefSkeleton.GetNum();
		for (int32 BoneIdx = 0; BoneIdx < RefSkeleton.GetNum(); ++BoneIdx)
		{
			FAgentFrameworkBoneInfo BoneInfo;
			BoneInfo.Index = BoneIdx;
			BoneInfo.Name = RefSkeleton.GetBoneName(BoneIdx).ToString();
			BoneInfo.ParentIndex = RefSkeleton.GetParentIndex(BoneIdx);
			BoneInfo.ParentName = (BoneInfo.ParentIndex != INDEX_NONE)
				? RefSkeleton.GetBoneName(BoneInfo.ParentIndex).ToString()
				: FString();
			OutInfo.Bones.Add(MoveTemp(BoneInfo));
		}

		AppendSockets(Skeleton->Sockets, TEXT("Skeleton"), OutInfo.Sockets);

		for (const FVirtualBone& VB : Skeleton->GetVirtualBones())
		{
			OutInfo.VirtualBones.Add(FString::Printf(TEXT("%s (%s -> %s)"),
				*VB.VirtualBoneName.ToString(), *VB.SourceBoneName.ToString(), *VB.TargetBoneName.ToString()));
		}

		for (const FAnimSlotGroup& Group : Skeleton->GetSlotGroups())
		{
			OutInfo.SlotGroups.Add(Group.GroupName.ToString());
		}

		for (const TObjectPtr<UBlendProfile>& Profile : Skeleton->BlendProfiles)
		{
			if (Profile)
			{
				OutInfo.BlendProfiles.Add(Profile->GetName());
			}
		}
	}

	static UClass* LoadPluginUClass(const TCHAR* ModuleName, const TCHAR* ClassPath, const TCHAR* PluginFriendlyName, FString& OutError)
	{
		if (ModuleName && *ModuleName)
		{
			FModuleManager::Get().LoadModule(ModuleName);
		}
		UClass* Cls = LoadObject<UClass>(nullptr, ClassPath);
		if (!Cls)
		{
			OutError = FString::Printf(TEXT("%s class '%s' could not be loaded. Ensure the %s plugin is enabled."),
				PluginFriendlyName, ClassPath, PluginFriendlyName);
		}
		return Cls;
	}

	static bool SetParamValue(UFunction* Func, uint8* Buffer, const TCHAR* ParamName, const void* SrcPtr, FString& OutError)
	{
		FProperty* Prop = Func->FindPropertyByName(ParamName);
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Function '%s' missing parameter '%s'."), *Func->GetName(), ParamName);
			return false;
		}
		void* Dest = Prop->ContainerPtrToValuePtr<void>(Buffer);
		Prop->CopyCompleteValue(Dest, SrcPtr);
		return true;
	}

	static bool SetObjectParamValue(UFunction* Func, uint8* Buffer, const TCHAR* ParamName, UObject* Obj, FString& OutError)
	{
		FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(Func->FindPropertyByName(ParamName));
		if (!Prop)
		{
			OutError = FString::Printf(TEXT("Function '%s' missing object parameter '%s'."), *Func->GetName(), ParamName);
			return false;
		}
		Prop->SetObjectPropertyValue_InContainer(Buffer, Obj);
		return true;
	}

	static bool GetObjectReturnValue(UFunction* Func, uint8* Buffer, UObject*& OutObj, FString& OutError)
	{
		FObjectPropertyBase* RetProp = CastField<FObjectPropertyBase>(Func->GetReturnProperty());
		if (!RetProp)
		{
			OutError = FString::Printf(TEXT("Function '%s' has unexpected return property."), *Func->GetName());
			return false;
		}
		OutObj = RetProp->GetObjectPropertyValue_InContainer(Buffer);
		return true;
	}

	static bool InvokeIKRigGetController(UClass* ControllerClass, UObject* Asset, UObject*& OutController, FString& OutError)
	{
		UFunction* GetCtrlFunc = ControllerClass->FindFunctionByName(TEXT("GetController"));
		if (!GetCtrlFunc)
		{
			OutError = FString::Printf(TEXT("Controller class '%s' has no GetController function."), *ControllerClass->GetName());
			return false;
		}
		uint8* Buf = static_cast<uint8*>(FMemory_Alloca_Aligned(GetCtrlFunc->PropertiesSize, GetCtrlFunc->GetMinAlignment()));
		UAgentFrameworkActionUtils::FScopedUFunctionFrame Frame(GetCtrlFunc, Buf);
		FProperty* FirstParam = nullptr;
		for (TFieldIterator<FProperty> It(GetCtrlFunc); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Parm) && !It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				FirstParam = *It;
				break;
			}
		}
		FObjectPropertyBase* FirstObjParam = CastField<FObjectPropertyBase>(FirstParam);
		if (!FirstObjParam)
		{
			OutError = TEXT("GetController first parameter is not an object property.");
			return false;
		}
		FirstObjParam->SetObjectPropertyValue_InContainer(Buf, Asset);
		ControllerClass->GetDefaultObject()->ProcessEvent(GetCtrlFunc, Buf);
		return GetObjectReturnValue(GetCtrlFunc, Buf, OutController, OutError) && OutController != nullptr;
	}
}

FAgentFrameworkAnimAssetResult UAgentFrameworkAnimationAuthoringToolset::CreateAnimBlueprint(
	const FString& AssetPath,
	const FString& SkeletonPath,
	const FString& ParentClassName)
{
	FAgentFrameworkAnimAssetResult Result;
	FString PackageName;
	FString AssetName;
	FString Error;
	if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(AssetPath, PackageName, AssetName, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimBlueprint"), Error);
		return Result;
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(SkeletonPath));
	if (!Skeleton)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimBlueprint"), FString::Printf(TEXT("USkeleton not found at '%s'."), *SkeletonPath));
		return Result;
	}

	UClass* ParentClass = AgentFrameworkAnimationInternal::ResolveAnimInstanceSubClass(ParentClassName, Error);
	if (!ParentClass)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimBlueprint"), Error);
		return Result;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimBlueprint"), FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return Result;
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = ParentClass;

	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
		UAnimBlueprint::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn));
	if (!AnimBP)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, nullptr);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimBlueprint"), TEXT("Failed to create AnimBlueprint."));
		return Result;
	}

	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	AnimBP->PostEditChange();

	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Package, AnimBP, true, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimBlueprint"), Error);
		return Result;
	}

	Result.AssetPath = AnimBP->GetPathName();
	Result.SkeletonPath = Skeleton->GetPathName();
	Result.ParentClassName = ParentClass->GetName();
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateAnimBlueprint"), 0.0, true, {});
	return Result;
}

FAgentFrameworkAnimMontageResult UAgentFrameworkAnimationAuthoringToolset::CreateAnimMontage(
	const FString& AssetPath,
	const FString& SkeletonPath,
	const TOptional<TArray<FString>>& SequencePaths)
{
	FAgentFrameworkAnimMontageResult Result;
	FString PackageName;
	FString AssetName;
	FString Error;
	if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(AssetPath, PackageName, AssetName, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimMontage"), Error);
		return Result;
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(SkeletonPath));
	if (!Skeleton)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimMontage"), FString::Printf(TEXT("USkeleton not found at '%s'."), *SkeletonPath));
		return Result;
	}

	TArray<UAnimSequence*> LoadedSequences;
	if (SequencePaths.IsSet())
	{
		for (const FString& SeqPath : *SequencePaths)
		{
			UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(SeqPath));
			if (!Seq)
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimMontage"), FString::Printf(TEXT("UAnimSequence not found at '%s'."), *SeqPath));
				return Result;
			}
			if (Seq->GetSkeleton() != Skeleton)
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimMontage"),
					FString::Printf(TEXT("Sequence '%s' skeleton does not match '%s'."), *Seq->GetPathName(), *Skeleton->GetPathName()));
				return Result;
			}
			LoadedSequences.Add(Seq);
		}
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimMontage"), FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return Result;
	}

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = Skeleton;
	UAnimMontage* Montage = Cast<UAnimMontage>(Factory->FactoryCreateNew(
		UAnimMontage::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn));
	if (!Montage)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, nullptr);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimMontage"), TEXT("Failed to create UAnimMontage."));
		return Result;
	}

	Montage->Modify();
	if (Montage->SlotAnimTracks.Num() == 0)
	{
		FSlotAnimationTrack NewTrack;
		NewTrack.SlotName = FAnimSlotGroup::DefaultSlotName;
		Montage->SlotAnimTracks.Add(NewTrack);
	}

	float CursorTime = 0.0f;
	for (UAnimSequence* Seq : LoadedSequences)
	{
		FAnimSegment NewSegment;
		NewSegment.SetAnimReference(Seq, true);
		NewSegment.StartPos = CursorTime;
		CursorTime += Seq->GetPlayLength();
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Add(NewSegment);
	}

	Montage->UpdateLinkableElements();
	Montage->CalculateSequenceLength();
	Montage->PostEditChange();

	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Package, Montage, true, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateAnimMontage"), Error);
		return Result;
	}

	Result.AssetPath = Montage->GetPathName();
	Result.SkeletonPath = Skeleton->GetPathName();
	Result.SequenceLength = Montage->GetPlayLength();
	Result.SlotName = Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0].SlotName.ToString() : FString();
	Result.SectionCount = Montage->CompositeSections.Num();
	Result.SegmentCount = Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() : 0;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateAnimMontage"), 0.0, true, {});
	return Result;
}

FAgentFrameworkAnimMontageResult UAgentFrameworkAnimationAuthoringToolset::ConfigureAnimMontage(
	const FString& MontagePath,
	const TOptional<FString>& SlotName,
	const TOptional<FString>& AddSectionName,
	TOptional<float> SectionStartTime,
	const TOptional<FString>& LinkNextSectionFrom,
	const TOptional<FString>& LinkNextSectionTo,
	TOptional<float> BlendInTime,
	TOptional<float> BlendOutTime)
{
	FAgentFrameworkAnimMontageResult Result;

	if (MontagePath.TrimStartAndEnd().IsEmpty())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"), TEXT("MontagePath cannot be empty."));
		return Result;
	}

	const bool bHasSlot = SlotName.IsSet() && !SlotName->TrimStartAndEnd().IsEmpty();
	const bool bHasAddSection = AddSectionName.IsSet() && !AddSectionName->TrimStartAndEnd().IsEmpty();
	const bool bHasFrom = LinkNextSectionFrom.IsSet() && !LinkNextSectionFrom->TrimStartAndEnd().IsEmpty();
	const bool bHasTo = LinkNextSectionTo.IsSet() && !LinkNextSectionTo->TrimStartAndEnd().IsEmpty();
	const bool bHasLink = bHasFrom || bHasTo;

	if (!bHasSlot && !bHasAddSection && !bHasLink && !BlendInTime.IsSet() && !BlendOutTime.IsSet())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"),
			TEXT("At least one configuration change (SlotName, AddSectionName, LinkNextSectionFrom/To, BlendInTime, or BlendOutTime) must be provided."));
		return Result;
	}

	if (bHasLink && (!bHasFrom || !bHasTo))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"),
			TEXT("Both LinkNextSectionFrom and LinkNextSectionTo must be provided together."));
		return Result;
	}

	if (SectionStartTime.IsSet() && *SectionStartTime < 0.0f)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"), TEXT("SectionStartTime cannot be negative."));
		return Result;
	}
	if (BlendInTime.IsSet() && *BlendInTime < 0.0f)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"), TEXT("BlendInTime cannot be negative."));
		return Result;
	}
	if (BlendOutTime.IsSet() && *BlendOutTime < 0.0f)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"), TEXT("BlendOutTime cannot be negative."));
		return Result;
	}

	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(MontagePath));
	if (!Montage)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"), FString::Printf(TEXT("UAnimMontage not found at '%s'."), *MontagePath));
		return Result;
	}

	if (bHasSlot && Montage->SlotAnimTracks.Num() == 0)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"), TEXT("Montage has no SlotAnimTracks to rename."));
		return Result;
	}

	FName AddedSectionFName = NAME_None;
	if (bHasAddSection)
	{
		AddedSectionFName = FName(*AddSectionName->TrimStartAndEnd());
		if (Montage->GetSectionIndex(AddedSectionFName) != INDEX_NONE)
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"),
				FString::Printf(TEXT("Section '%s' already exists in montage '%s'."), *AddedSectionFName.ToString(), *Montage->GetPathName()));
			return Result;
		}
	}

	if (bHasLink)
	{
		const FName FromName(*LinkNextSectionFrom->TrimStartAndEnd());
		const FName ToName(*LinkNextSectionTo->TrimStartAndEnd());
		const bool bFromExists = (Montage->GetSectionIndex(FromName) != INDEX_NONE)
			|| (bHasAddSection && FromName == AddedSectionFName);
		const bool bToExists = (Montage->GetSectionIndex(ToName) != INDEX_NONE)
			|| (bHasAddSection && ToName == AddedSectionFName);
		if (!bFromExists || !bToExists)
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"),
				FString::Printf(TEXT("Cannot link sections '%s' -> '%s': one or both sections were not found."),
					*FromName.ToString(), *ToName.ToString()));
			return Result;
		}
	}

	Montage->Modify();

	if (bHasSlot)
	{
		Montage->SlotAnimTracks[0].SlotName = FName(*SlotName->TrimStartAndEnd());
	}

	if (bHasAddSection)
	{
		const float StartTime = SectionStartTime.Get(0.0f);
		const int32 AddedIdx = Montage->AddAnimCompositeSection(AddedSectionFName, StartTime);
		if (AddedIdx == INDEX_NONE)
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"),
				FString::Printf(TEXT("Failed to add section '%s' to montage."), *AddedSectionFName.ToString()));
			return Result;
		}
	}

	if (bHasLink)
	{
		const FName FromName(*LinkNextSectionFrom->TrimStartAndEnd());
		const FName ToName(*LinkNextSectionTo->TrimStartAndEnd());
		const int32 FromIdx = Montage->GetSectionIndex(FromName);
		const int32 ToIdx = Montage->GetSectionIndex(ToName);
		if (FromIdx == INDEX_NONE || ToIdx == INDEX_NONE)
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"),
				FString::Printf(TEXT("Cannot link sections '%s' -> '%s': one or both sections were not found."),
					*FromName.ToString(), *ToName.ToString()));
			return Result;
		}
		Montage->CompositeSections[FromIdx].NextSectionName = ToName;
	}

	if (BlendInTime.IsSet())
	{
		Montage->BlendIn.SetBlendTime(*BlendInTime);
	}
	if (BlendOutTime.IsSet())
	{
		Montage->BlendOut.SetBlendTime(*BlendOutTime);
	}

	Montage->UpdateLinkableElements();
	Montage->CalculateSequenceLength();
	Montage->PostEditChange();

	FString Error;
	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Montage->GetPackage(), Montage, false, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureAnimMontage"), Error);
		return Result;
	}

	Result.AssetPath = Montage->GetPathName();
	Result.SkeletonPath = Montage->GetSkeleton() ? Montage->GetSkeleton()->GetPathName() : FString();
	Result.SequenceLength = Montage->GetPlayLength();
	Result.SlotName = Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0].SlotName.ToString() : FString();
	Result.SectionCount = Montage->CompositeSections.Num();
	Result.SegmentCount = Montage->SlotAnimTracks.Num() > 0 ? Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() : 0;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("ConfigureAnimMontage"), 0.0, true, {});
	return Result;
}

FAgentFrameworkBlendSpaceResult UAgentFrameworkAnimationAuthoringToolset::CreateBlendSpace(
	const FString& AssetPath,
	const FString& SkeletonPath,
	const TOptional<FString>& HorizontalAxisName,
	TOptional<float> HorizontalMin,
	TOptional<float> HorizontalMax,
	const TOptional<FString>& VerticalAxisName,
	TOptional<float> VerticalMin,
	TOptional<float> VerticalMax,
	const TOptional<TArray<FAgentFrameworkBlendSpaceSampleSpec>>& Samples)
{
	FAgentFrameworkBlendSpaceResult Result;
	FString PackageName;
	FString AssetName;
	FString Error;
	if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(AssetPath, PackageName, AssetName, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"), Error);
		return Result;
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(SkeletonPath));
	if (!Skeleton)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"), FString::Printf(TEXT("USkeleton not found at '%s'."), *SkeletonPath));
		return Result;
	}

	const FString HorizName = (HorizontalAxisName.IsSet() && !HorizontalAxisName->TrimStartAndEnd().IsEmpty())
		? HorizontalAxisName->TrimStartAndEnd()
		: TEXT("Direction");
	const float HMin = HorizontalMin.Get(-180.0f);
	const float HMax = HorizontalMax.Get(180.0f);
	if (HMin >= HMax)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"),
			FString::Printf(TEXT("HorizontalMin (%.3f) must be strictly less than HorizontalMax (%.3f)."), HMin, HMax));
		return Result;
	}

	const FString VertName = VerticalAxisName.IsSet() ? VerticalAxisName->TrimStartAndEnd() : FString();
	const bool bIs1D = VertName.IsEmpty();
	const float VMin = VerticalMin.Get(0.0f);
	const float VMax = VerticalMax.Get(600.0f);
	if (!bIs1D && VMin >= VMax)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"),
			FString::Printf(TEXT("VerticalMin (%.3f) must be strictly less than VerticalMax (%.3f)."), VMin, VMax));
		return Result;
	}

	struct FLoadedSample
	{
		UAnimSequence* Sequence = nullptr;
		float X = 0.0f;
		float Y = 0.0f;
	};
	TArray<FLoadedSample> ValidSamples;
	if (Samples.IsSet())
	{
		for (const FAgentFrameworkBlendSpaceSampleSpec& SampleSpec : *Samples)
		{
			UAnimSequence* Seq = LoadObject<UAnimSequence>(nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(SampleSpec.AnimationPath));
			if (!Seq)
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"),
					FString::Printf(TEXT("Sample UAnimSequence not found at '%s'."), *SampleSpec.AnimationPath));
				return Result;
			}
			if (Seq->GetSkeleton() != Skeleton)
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"),
					FString::Printf(TEXT("Sample sequence '%s' skeleton does not match '%s'."), *Seq->GetPathName(), *Skeleton->GetPathName()));
				return Result;
			}
			ValidSamples.Add({ Seq, SampleSpec.HorizontalValue, bIs1D ? 0.0f : SampleSpec.VerticalValue });
		}
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"), FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return Result;
	}

	UBlendSpace* BlendSpace = nullptr;
	if (bIs1D)
	{
		UBlendSpaceFactory1D* Factory1D = NewObject<UBlendSpaceFactory1D>();
		Factory1D->TargetSkeleton = Skeleton;
		BlendSpace = Cast<UBlendSpace>(Factory1D->FactoryCreateNew(
			UBlendSpace1D::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn));
	}
	else
	{
		UBlendSpaceFactoryNew* Factory2D = NewObject<UBlendSpaceFactoryNew>();
		Factory2D->TargetSkeleton = Skeleton;
		BlendSpace = Cast<UBlendSpace>(Factory2D->FactoryCreateNew(
			UBlendSpace::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn));
	}

	if (!BlendSpace)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, nullptr);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"), TEXT("Failed to allocate UBlendSpace asset."));
		return Result;
	}

	BlendSpace->Modify();

	FStructProperty* BlendParamsProp = CastField<FStructProperty>(UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters")));
	if (!BlendParamsProp || BlendParamsProp->ArrayDim < 2)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, BlendSpace);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"), TEXT("Failed to reflect BlendParameters on UBlendSpace."));
		return Result;
	}

	FBlendParameter* Axis0 = BlendParamsProp->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace, 0);
	Axis0->DisplayName = HorizName;
	Axis0->Min = HMin;
	Axis0->Max = HMax;

	if (!bIs1D)
	{
		FBlendParameter* Axis1 = BlendParamsProp->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace, 1);
		Axis1->DisplayName = VertName;
		Axis1->Min = VMin;
		Axis1->Max = VMax;
	}

	for (const FLoadedSample& S : ValidSamples)
	{
		const int32 SampleIdx = BlendSpace->AddSample(S.Sequence, FVector(S.X, S.Y, 0.0f));
		if (SampleIdx == INDEX_NONE)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, BlendSpace);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"),
				FString::Printf(TEXT("AddSample failed for sequence '%s' at (%.2f, %.2f)."),
					*S.Sequence->GetPathName(), S.X, S.Y));
			return Result;
		}
	}

	BlendSpace->ValidateSampleData();
	BlendSpace->PostEditChange();

	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Package, BlendSpace, true, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateBlendSpace"), Error);
		return Result;
	}

	Result.AssetPath = BlendSpace->GetPathName();
	Result.SkeletonPath = Skeleton->GetPathName();
	Result.bIs1D = bIs1D;
	Result.HorizontalAxisName = HorizName;
	Result.VerticalAxisName = VertName;
	Result.SampleCount = ValidSamples.Num();
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateBlendSpace"), 0.0, true, {});
	return Result;
}

FAgentFrameworkSkeletonInfoResult UAgentFrameworkAnimationAuthoringToolset::GetSkeletonAnimationInfo(
	const FString& AssetPath)
{
	FAgentFrameworkSkeletonInfoResult Result;
	if (AssetPath.TrimStartAndEnd().IsEmpty())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("GetSkeletonAnimationInfo"), TEXT("AssetPath cannot be empty."));
		return Result;
	}

	const FString NormalizedPath = UAgentFrameworkActionUtils::NormalizeAssetObjectPath(AssetPath);
	UObject* Loaded = LoadObject<UObject>(nullptr, *NormalizedPath);
	if (!Loaded)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("GetSkeletonAnimationInfo"), FString::Printf(TEXT("Asset not found at '%s'."), *AssetPath));
		return Result;
	}

	Result.AssetPath = Loaded->GetPathName();

	if (const USkeleton* Skeleton = Cast<USkeleton>(Loaded))
	{
		Result.AssetType = TEXT("Skeleton");
		AgentFrameworkAnimationInternal::PopulateSkeletonData(Skeleton, Result);
	}
	else if (const USkeletalMesh* Mesh = Cast<USkeletalMesh>(Loaded))
	{
		Result.AssetType = TEXT("SkeletalMesh");
		Result.LODCount = Mesh->GetLODNum();
		for (const TObjectPtr<UMorphTarget>& Morph : Mesh->GetMorphTargets())
		{
			if (Morph)
			{
				Result.MorphTargetNames.Add(Morph->GetName());
			}
		}
		if (const USkeleton* MeshSkeleton = Mesh->GetSkeleton())
		{
			AgentFrameworkAnimationInternal::PopulateSkeletonData(MeshSkeleton, Result);
		}
		AgentFrameworkAnimationInternal::AppendSockets(Mesh->GetMeshOnlySocketList(), TEXT("SkeletalMesh"), Result.Sockets);
	}
	else if (const UAnimSequence* Sequence = Cast<UAnimSequence>(Loaded))
	{
		Result.AssetType = TEXT("AnimSequence");
		Result.SequenceLength = Sequence->GetPlayLength();
		Result.NumberOfSampledKeys = Sequence->GetNumberOfSampledKeys();
		const FFrameRate Rate = Sequence->GetSamplingFrameRate();
		Result.FrameRate = Rate.Denominator > 0 ? static_cast<float>(Rate.AsDecimal()) : 0.0f;
		Result.bEnableRootMotion = Sequence->HasRootMotion();
		if (const USkeleton* SeqSkeleton = Sequence->GetSkeleton())
		{
			AgentFrameworkAnimationInternal::PopulateSkeletonData(SeqSkeleton, Result);
		}
	}
	else
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("GetSkeletonAnimationInfo"),
			FString::Printf(TEXT("Asset '%s' (%s) is not a USkeleton, USkeletalMesh, or UAnimSequence."),
				*AssetPath, *Loaded->GetClass()->GetName()));
		return Result;
	}

	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("GetSkeletonAnimationInfo"), 0.0, true, {});
	return Result;
}

FAgentFrameworkIKRigResult UAgentFrameworkAnimationAuthoringToolset::CreateIKRig(
	const FString& AssetPath,
	const FString& SkeletalMeshPath,
	const TOptional<TArray<FAgentFrameworkIKChainSpec>>& Chains)
{
	FAgentFrameworkIKRigResult Result;
	FString PackageName;
	FString AssetName;
	FString Error;
	if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(AssetPath, PackageName, AssetName, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), Error);
		return Result;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(SkeletalMeshPath));
	if (!Mesh)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), FString::Printf(TEXT("USkeletalMesh not found at '%s'."), *SkeletalMeshPath));
		return Result;
	}

	UClass* IKRigDefClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("IKRig"), TEXT("/Script/IKRig.IKRigDefinition"), TEXT("IKRig"), Error);
	if (!IKRigDefClass)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), Error);
		return Result;
	}
	UClass* IKRigFactoryClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("IKRigEditor"), TEXT("/Script/IKRigEditor.IKRigDefinitionFactory"), TEXT("IKRigEditor"), Error);
	UClass* ControllerClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("IKRigEditor"), TEXT("/Script/IKRigEditor.IKRigController"), TEXT("IKRigEditor"), Error);
	if (!IKRigFactoryClass || !ControllerClass)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), Error);
		return Result;
	}

	if (Chains.IsSet())
	{
		for (const FAgentFrameworkIKChainSpec& Chain : *Chains)
		{
			if (Chain.ChainName.TrimStartAndEnd().IsEmpty())
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), TEXT("IK chain spec cannot have empty ChainName."));
				return Result;
			}
		}
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return Result;
	}

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), IKRigFactoryClass);
	UObject* IKRigAsset = Factory ? Factory->FactoryCreateNew(IKRigDefClass, Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn) : nullptr;
	if (!IKRigAsset)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, nullptr);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), TEXT("Failed to allocate UIKRigDefinition asset."));
		return Result;
	}

	UObject* Controller = nullptr;
	if (!AgentFrameworkAnimationInternal::InvokeIKRigGetController(ControllerClass, IKRigAsset, Controller, Error))
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, IKRigAsset);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), Error);
		return Result;
	}

	if (UFunction* SetMeshFunc = ControllerClass->FindFunctionByName(TEXT("SetSkeletalMesh")))
	{
		uint8* Buf = static_cast<uint8*>(FMemory_Alloca_Aligned(SetMeshFunc->PropertiesSize, SetMeshFunc->GetMinAlignment()));
		UAgentFrameworkActionUtils::FScopedUFunctionFrame Frame(SetMeshFunc, Buf);
		if (!AgentFrameworkAnimationInternal::SetObjectParamValue(SetMeshFunc, Buf, TEXT("SkeletalMesh"), Mesh, Error))
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, IKRigAsset);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), Error);
			return Result;
		}
		Controller->ProcessEvent(SetMeshFunc, Buf);
		if (FBoolProperty* RetBool = CastField<FBoolProperty>(SetMeshFunc->GetReturnProperty()))
		{
			if (!RetBool->GetPropertyValue_InContainer(Buf))
			{
				UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, IKRigAsset);
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"),
					FString::Printf(TEXT("IKRig SetSkeletalMesh rejected mesh '%s'."), *Mesh->GetPathName()));
				return Result;
			}
		}
	}

	if (Chains.IsSet())
	{
		UFunction* AddChainFunc = ControllerClass->FindFunctionByName(TEXT("AddRetargetChain"));
		if (!AddChainFunc)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, IKRigAsset);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), TEXT("UIKRigController::AddRetargetChain function not found."));
			return Result;
		}

		for (const FAgentFrameworkIKChainSpec& Chain : *Chains)
		{
			uint8* Buf = static_cast<uint8*>(FMemory_Alloca_Aligned(AddChainFunc->PropertiesSize, AddChainFunc->GetMinAlignment()));
			UAgentFrameworkActionUtils::FScopedUFunctionFrame Frame(AddChainFunc, Buf);
			const FName ChainName(*Chain.ChainName.TrimStartAndEnd());
			const FName StartBone(*Chain.StartBone.TrimStartAndEnd());
			const FName EndBone(*Chain.EndBone.TrimStartAndEnd());
			const FName GoalName(*Chain.GoalName.TrimStartAndEnd());
			if (!AgentFrameworkAnimationInternal::SetParamValue(AddChainFunc, Buf, TEXT("ChainName"), &ChainName, Error)
				|| !AgentFrameworkAnimationInternal::SetParamValue(AddChainFunc, Buf, TEXT("StartBoneName"), &StartBone, Error)
				|| !AgentFrameworkAnimationInternal::SetParamValue(AddChainFunc, Buf, TEXT("EndBoneName"), &EndBone, Error)
				|| !AgentFrameworkAnimationInternal::SetParamValue(AddChainFunc, Buf, TEXT("GoalName"), &GoalName, Error))
			{
				UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, IKRigAsset);
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), Error);
				return Result;
			}
			Controller->ProcessEvent(AddChainFunc, Buf);
			if (FNameProperty* RetName = CastField<FNameProperty>(AddChainFunc->GetReturnProperty()))
			{
				const FName CreatedName = RetName->GetPropertyValue_InContainer(Buf);
				if (CreatedName == NAME_None)
				{
					UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, IKRigAsset);
					UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"),
						FString::Printf(TEXT("AddRetargetChain failed for chain '%s'."), *ChainName.ToString()));
					return Result;
				}
				Result.CreatedChains.Add(CreatedName.ToString());
			}
		}
	}

	IKRigAsset->PostEditChange();
	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Package, IKRigAsset, true, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRig"), Error);
		return Result;
	}

	Result.AssetPath = IKRigAsset->GetPathName();
	Result.SkeletalMeshPath = Mesh->GetPathName();
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateIKRig"), 0.0, true, {});
	return Result;
}

FAgentFrameworkIKRetargeterResult UAgentFrameworkAnimationAuthoringToolset::CreateIKRetargeter(
	const FString& AssetPath,
	const FString& SourceIKRigPath,
	const FString& TargetIKRigPath)
{
	FAgentFrameworkIKRetargeterResult Result;
	FString PackageName;
	FString AssetName;
	FString Error;
	if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(AssetPath, PackageName, AssetName, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"), Error);
		return Result;
	}

	UClass* IKRigDefClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("IKRig"), TEXT("/Script/IKRig.IKRigDefinition"), TEXT("IKRig"), Error);
	UClass* RetargeterClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("IKRig"), TEXT("/Script/IKRig.IKRetargeter"), TEXT("IKRig"), Error);
	UClass* FactoryClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("IKRigEditor"), TEXT("/Script/IKRigEditor.IKRetargetFactory"), TEXT("IKRigEditor"), Error);
	UClass* ControllerClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("IKRigEditor"), TEXT("/Script/IKRigEditor.IKRetargeterController"), TEXT("IKRigEditor"), Error);
	if (!IKRigDefClass || !RetargeterClass || !FactoryClass || !ControllerClass)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"), Error);
		return Result;
	}

	if (SourceIKRigPath.TrimStartAndEnd().IsEmpty() || TargetIKRigPath.TrimStartAndEnd().IsEmpty())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"),
			TEXT("SourceIKRigPath and TargetIKRigPath cannot be empty."));
		return Result;
	}

	UObject* SourceRig = StaticLoadObject(IKRigDefClass, nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(SourceIKRigPath));
	UObject* TargetRig = StaticLoadObject(IKRigDefClass, nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(TargetIKRigPath));
	if (!SourceRig || !TargetRig)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"),
			FString::Printf(TEXT("Failed to load Source (%s) or Target (%s) UIKRigDefinition."), *SourceIKRigPath, *TargetIKRigPath));
		return Result;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"), FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
		return Result;
	}

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
	UObject* Retargeter = Factory ? Factory->FactoryCreateNew(RetargeterClass, Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn) : nullptr;
	if (!Retargeter)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, nullptr);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"), TEXT("Failed to allocate UIKRetargeter asset."));
		return Result;
	}

	UObject* Controller = nullptr;
	if (!AgentFrameworkAnimationInternal::InvokeIKRigGetController(ControllerClass, Retargeter, Controller, Error))
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, Retargeter);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"), Error);
		return Result;
	}

	UFunction* SetRigFunc = ControllerClass->FindFunctionByName(TEXT("SetIKRig"));
	if (!SetRigFunc)
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, Retargeter);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"), TEXT("UIKRetargeterController::SetIKRig function not found."));
		return Result;
	}

	auto AssignRigSide = [&](const TCHAR* EnumText, UObject* RigObj) -> bool
	{
		uint8* Buf = static_cast<uint8*>(FMemory_Alloca_Aligned(SetRigFunc->PropertiesSize, SetRigFunc->GetMinAlignment()));
		UAgentFrameworkActionUtils::FScopedUFunctionFrame Frame(SetRigFunc, Buf);
		FProperty* SideProp = SetRigFunc->FindPropertyByName(TEXT("SourceOrTarget"));
		if (!SideProp || !SideProp->ImportText_InContainer(EnumText, Buf, nullptr, PPF_None))
		{
			Error = FString::Printf(TEXT("Failed to set SourceOrTarget='%s' on SetIKRig."), EnumText);
			return false;
		}
		if (!AgentFrameworkAnimationInternal::SetObjectParamValue(SetRigFunc, Buf, TEXT("IKRig"), RigObj, Error))
		{
			return false;
		}
		Controller->ProcessEvent(SetRigFunc, Buf);
		return true;
	};

	if (!AssignRigSide(TEXT("Source"), SourceRig) || !AssignRigSide(TEXT("Target"), TargetRig))
	{
		UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, Retargeter);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"), Error);
		return Result;
	}

	Retargeter->PostEditChange();
	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Package, Retargeter, true, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateIKRetargeter"), Error);
		return Result;
	}

	Result.AssetPath = Retargeter->GetPathName();
	Result.SourceIKRigPath = SourceRig->GetPathName();
	Result.TargetIKRigPath = TargetRig->GetPathName();
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateIKRetargeter"), 0.0, true, {});
	return Result;
}

FAgentFrameworkMotionMatchingResult UAgentFrameworkAnimationAuthoringToolset::ConfigureMotionMatchingDatabase(
	const FString& AssetPath,
	const TOptional<FString>& SchemaPath,
	const TOptional<TArray<FString>>& AnimationAssetPaths,
	TOptional<bool> bCreateIfMissing)
{
	FAgentFrameworkMotionMatchingResult Result;
	if (AssetPath.TrimStartAndEnd().IsEmpty())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"), TEXT("AssetPath cannot be empty."));
		return Result;
	}

	FString Error;
	UClass* DbClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("PoseSearch"), TEXT("/Script/PoseSearch.PoseSearchDatabase"), TEXT("PoseSearch"), Error);
	UClass* SchemaClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("PoseSearch"), TEXT("/Script/PoseSearch.PoseSearchSchema"), TEXT("PoseSearch"), Error);
	UClass* FactoryClass = AgentFrameworkAnimationInternal::LoadPluginUClass(TEXT("PoseSearchEditor"), TEXT("/Script/PoseSearchEditor.PoseSearchDatabaseFactory"), TEXT("PoseSearchEditor"), Error);
	if (!DbClass || !SchemaClass || !FactoryClass)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"), Error);
		return Result;
	}

	UObject* LoadedSchema = nullptr;
	if (SchemaPath.IsSet() && !SchemaPath->TrimStartAndEnd().IsEmpty())
	{
		LoadedSchema = StaticLoadObject(SchemaClass, nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(*SchemaPath));
		if (!LoadedSchema)
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"),
				FString::Printf(TEXT("UPoseSearchSchema not found at '%s'."), **SchemaPath));
			return Result;
		}
	}

	TArray<UObject*> LoadedAnims;
	if (AnimationAssetPaths.IsSet())
	{
		for (const FString& AnimPath : *AnimationAssetPaths)
		{
			if (AnimPath.TrimStartAndEnd().IsEmpty())
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"),
					TEXT("AnimationAssetPaths contains an empty path."));
				return Result;
			}
			UObject* AnimObj = LoadObject<UObject>(nullptr, *UAgentFrameworkActionUtils::NormalizeAssetObjectPath(AnimPath));
			if (!AnimObj)
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"),
					FString::Printf(TEXT("Animation asset not found at '%s'."), *AnimPath));
				return Result;
			}
			LoadedAnims.Add(AnimObj);
		}
	}

	const FString NormalizedObjPath = UAgentFrameworkActionUtils::NormalizeAssetObjectPath(AssetPath);
	FString CandidatePkgName;
	FString CandidatePkgDir;
	FString CandidateAssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, CandidatePkgName, CandidatePkgDir, CandidateAssetName);

	UObject* Database = FindObject<UObject>(nullptr, *NormalizedObjPath);
	if (!Database && !CandidatePkgName.IsEmpty() && FPackageName::IsValidLongPackageName(CandidatePkgName) && FPackageName::DoesPackageExist(CandidatePkgName))
	{
		Database = StaticLoadObject(DbClass, nullptr, *NormalizedObjPath);
	}
	bool bCreatedNew = false;
	UPackage* CreatedPackage = nullptr;

	if (!Database)
	{
		if (!bCreateIfMissing.Get(true))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"),
				FString::Printf(TEXT("UPoseSearchDatabase not found at '%s' and bCreateIfMissing is false."), *AssetPath));
			return Result;
		}

		FString PackageName;
		FString AssetName;
		if (!UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(AssetPath, PackageName, AssetName, Error))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"), Error);
			return Result;
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"),
				FString::Printf(TEXT("Failed to create package '%s'."), *PackageName));
			return Result;
		}

		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		Database = Factory ? Factory->FactoryCreateNew(DbClass, Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn) : nullptr;
		if (!Database)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(Package, nullptr);
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"), TEXT("Failed to allocate UPoseSearchDatabase."));
			return Result;
		}
		CreatedPackage = Package;
		bCreatedNew = true;
	}

	Database->Modify();

	FObjectPropertyBase* SchemaProp = CastField<FObjectPropertyBase>(DbClass->FindPropertyByName(TEXT("Schema")));
	if (LoadedSchema && SchemaProp)
	{
		SchemaProp->SetObjectPropertyValue_InContainer(Database, LoadedSchema);
	}

	FArrayProperty* DbAssetsProp = CastField<FArrayProperty>(DbClass->FindPropertyByName(TEXT("DatabaseAnimationAssets")));
	if (!DbAssetsProp)
	{
		if (CreatedPackage)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(CreatedPackage, Database);
		}
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"), TEXT("Failed to reflect DatabaseAnimationAssets on UPoseSearchDatabase."));
		return Result;
	}
	FStructProperty* ElemStructProp = CastField<FStructProperty>(DbAssetsProp->Inner);
	FObjectPropertyBase* AnimAssetFieldProp = ElemStructProp ? CastField<FObjectPropertyBase>(ElemStructProp->Struct->FindPropertyByName(TEXT("AnimAsset"))) : nullptr;
	if (!ElemStructProp || !AnimAssetFieldProp)
	{
		if (CreatedPackage)
		{
			UAgentFrameworkActionUtils::CleanupFailedNewAsset(CreatedPackage, Database);
		}
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"), TEXT("Failed to reflect FPoseSearchDatabaseAnimationAsset::AnimAsset."));
		return Result;
	}

	FScriptArrayHelper ArrayHelper(DbAssetsProp, DbAssetsProp->ContainerPtrToValuePtr<void>(Database));
	TSet<UObject*> ExistingAnims;
	for (int32 Idx = 0; Idx < ArrayHelper.Num(); ++Idx)
	{
		if (UObject* Existing = AnimAssetFieldProp->GetObjectPropertyValue_InContainer(ArrayHelper.GetRawPtr(Idx)))
		{
			ExistingAnims.Add(Existing);
		}
	}

	int32 AddedCount = 0;
	for (UObject* AnimObj : LoadedAnims)
	{
		if (!ExistingAnims.Contains(AnimObj))
		{
			const int32 NewIdx = ArrayHelper.AddValue();
			void* ElemPtr = ArrayHelper.GetRawPtr(NewIdx);
			AnimAssetFieldProp->SetObjectPropertyValue_InContainer(ElemPtr, AnimObj);
			ExistingAnims.Add(AnimObj);
			++AddedCount;
		}
	}

	Database->PostEditChange();
	if (!UAgentFrameworkActionUtils::SaveAssetChecked(Database->GetPackage(), Database, bCreatedNew, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ConfigureMotionMatchingDatabase"), Error);
		return Result;
	}

	UObject* FinalSchema = SchemaProp ? SchemaProp->GetObjectPropertyValue_InContainer(Database) : nullptr;
	Result.DatabasePath = Database->GetPathName();
	Result.SchemaPath = FinalSchema ? FinalSchema->GetPathName() : FString();
	Result.TotalAnimationAssets = ArrayHelper.Num();
	Result.AddedAnimationAssets = AddedCount;
	Result.bCreated = bCreatedNew;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("ConfigureMotionMatchingDatabase"), 0.0, true, {});
	return Result;
}
