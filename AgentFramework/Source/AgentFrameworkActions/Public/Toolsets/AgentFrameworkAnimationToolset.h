// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "AgentFrameworkAnimationToolset.generated.h"

USTRUCT(BlueprintType)
struct FAgentFrameworkBlendSpaceSampleSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Animation")
	FString AnimationPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Animation")
	float HorizontalValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Animation")
	float VerticalValue = 0.0f;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkIKChainSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Animation")
	FString ChainName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Animation")
	FString StartBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Animation")
	FString EndBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Animation")
	FString GoalName;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkAnimAssetResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SkeletonPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString ParentClassName;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkAnimMontageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SkeletonPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	float SequenceLength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SlotName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 SectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 SegmentCount = 0;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkBlendSpaceResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SkeletonPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	bool bIs1D = true;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString HorizontalAxisName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString VerticalAxisName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 SampleCount = 0;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkBoneInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 Index = -1;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString ParentName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 ParentIndex = -1;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkSocketInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SocketName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString BoneName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FVector RelativeScale = FVector::OneVector;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString Source;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkSkeletonInfoResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString AssetType;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SkeletonPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 BoneCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	TArray<FAgentFrameworkBoneInfo> Bones;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	TArray<FAgentFrameworkSocketInfo> Sockets;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	TArray<FString> VirtualBones;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	TArray<FString> SlotGroups;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	TArray<FString> BlendProfiles;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 LODCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	TArray<FString> MorphTargetNames;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	float SequenceLength = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 NumberOfSampledKeys = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	float FrameRate = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	bool bEnableRootMotion = false;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkIKRigResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SkeletalMeshPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	TArray<FString> CreatedChains;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkIKRetargeterResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SourceIKRigPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString TargetIKRigPath;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkMotionMatchingResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString DatabasePath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	FString SchemaPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 TotalAnimationAssets = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	int32 AddedAnimationAssets = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Animation")
	bool bCreated = false;
};

UCLASS(BlueprintType)
class AGENTFRAMEWORKACTIONS_API UAgentFrameworkAnimationAuthoringToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Animation")
	static FAgentFrameworkAnimAssetResult CreateAnimBlueprint(
		const FString& AssetPath,
		const FString& SkeletonPath,
		const FString& ParentClassName = TEXT("AnimInstance"));

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Animation")
	static FAgentFrameworkAnimMontageResult CreateAnimMontage(
		const FString& AssetPath,
		const FString& SkeletonPath,
		const TOptional<TArray<FString>>& SequencePaths);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Animation")
	static FAgentFrameworkAnimMontageResult ConfigureAnimMontage(
		const FString& MontagePath,
		const TOptional<FString>& SlotName,
		const TOptional<FString>& AddSectionName,
		TOptional<float> SectionStartTime,
		const TOptional<FString>& LinkNextSectionFrom,
		const TOptional<FString>& LinkNextSectionTo,
		TOptional<float> BlendInTime,
		TOptional<float> BlendOutTime);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Animation")
	static FAgentFrameworkBlendSpaceResult CreateBlendSpace(
		const FString& AssetPath,
		const FString& SkeletonPath,
		const TOptional<FString>& HorizontalAxisName,
		TOptional<float> HorizontalMin,
		TOptional<float> HorizontalMax,
		const TOptional<FString>& VerticalAxisName,
		TOptional<float> VerticalMin,
		TOptional<float> VerticalMax,
		const TOptional<TArray<FAgentFrameworkBlendSpaceSampleSpec>>& Samples);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Read"), Category = "AgentFramework|Animation")
	static FAgentFrameworkSkeletonInfoResult GetSkeletonAnimationInfo(
		const FString& AssetPath);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Animation")
	static FAgentFrameworkIKRigResult CreateIKRig(
		const FString& AssetPath,
		const FString& SkeletalMeshPath,
		const TOptional<TArray<FAgentFrameworkIKChainSpec>>& Chains);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Animation")
	static FAgentFrameworkIKRetargeterResult CreateIKRetargeter(
		const FString& AssetPath,
		const FString& SourceIKRigPath,
		const FString& TargetIKRigPath);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Animation")
	static FAgentFrameworkMotionMatchingResult ConfigureMotionMatchingDatabase(
		const FString& AssetPath,
		const TOptional<FString>& SchemaPath,
		const TOptional<TArray<FString>>& AnimationAssetPaths,
		TOptional<bool> bCreateIfMissing);
};
