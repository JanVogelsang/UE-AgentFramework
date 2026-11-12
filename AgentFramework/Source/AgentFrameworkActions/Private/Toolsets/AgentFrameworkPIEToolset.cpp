// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Toolsets/AgentFrameworkPIEToolset.h"
#include "AgentFrameworkActionUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace AgentFrameworkPIEInternal
{
	static UWorld* GetActivePIEWorld()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		if (FWorldContext* PIEContext = GEditor->GetPIEWorldContext())
		{
			if (UWorld* W = PIEContext->World())
			{
				return W;
			}
		}
		if (UWorld* CurrentPlayWorld = GEditor->GetCurrentPlayWorld())
		{
			return CurrentPlayWorld;
		}
		return GEditor->PlayWorld;
	}

	static AActor* FindSinglePIEActor(UWorld* World, const FString& Identifier, FString& OutError)
	{
		const FString Trimmed = Identifier.TrimStartAndEnd();
		TArray<AActor*> ExactCaseMatches;
		TArray<AActor*> ExactCaseInsensitiveMatches;
		TArray<AActor*> SubstringMatches;

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}

			const FString Name = Actor->GetName();
			const FString Label = Actor->GetActorLabel();

			if (Name.Equals(Trimmed, ESearchCase::CaseSensitive) || Label.Equals(Trimmed, ESearchCase::CaseSensitive))
			{
				ExactCaseMatches.Add(Actor);
			}
			else if (Name.Equals(Trimmed, ESearchCase::IgnoreCase) || Label.Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				ExactCaseInsensitiveMatches.Add(Actor);
			}
			else if (Name.Contains(Trimmed, ESearchCase::IgnoreCase) || Label.Contains(Trimmed, ESearchCase::IgnoreCase))
			{
				SubstringMatches.Add(Actor);
			}
		}

		auto ResolveTier = [&](const TArray<AActor*>& Candidates, const TCHAR* TierLabel) -> AActor*
		{
			if (Candidates.Num() == 1)
			{
				return Candidates[0];
			}
			if (Candidates.Num() > 1)
			{
				TArray<FString> Descriptions;
				for (int32 i = 0; i < FMath::Min(Candidates.Num(), 8); ++i)
				{
					Descriptions.Add(FString::Printf(TEXT("%s (%s)"), *Candidates[i]->GetName(), *Candidates[i]->GetActorLabel()));
				}
				OutError = FString::Printf(
					TEXT("Actor identifier '%s' is ambiguous (%s, %d matches): %s."),
					*Trimmed, TierLabel, Candidates.Num(), *FString::Join(Descriptions, TEXT(", ")));
			}
			return nullptr;
		};

		if (ExactCaseMatches.Num() > 0)
		{
			return ResolveTier(ExactCaseMatches, TEXT("exact"));
		}
		if (ExactCaseInsensitiveMatches.Num() > 0)
		{
			return ResolveTier(ExactCaseInsensitiveMatches, TEXT("case-insensitive"));
		}
		if (SubstringMatches.Num() > 0)
		{
			return ResolveTier(SubstringMatches, TEXT("substring"));
		}

		OutError = FString::Printf(TEXT("Actor '%s' not found in the active PIE world."), *Trimmed);
		return nullptr;
	}

	static FString SerializeJsonValue(const TSharedPtr<FJsonValue>& Val)
	{
		if (!Val.IsValid())
		{
			return TEXT("null");
		}
		if (Val->Type == EJson::Object)
		{
			FString Out;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Val->AsObject().ToSharedRef(), Writer);
			return Out;
		}
		if (Val->Type == EJson::Array)
		{
			FString Out;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Val->AsArray(), Writer);
			return Out;
		}
		if (Val->Type == EJson::String)
		{
			return FString::Printf(TEXT("\"%s\""), *Val->AsString().ReplaceCharWithEscapedChar());
		}
		if (Val->Type == EJson::Boolean)
		{
			return Val->AsBool() ? TEXT("true") : TEXT("false");
		}
		if (Val->Type == EJson::Number)
		{
			return LexToString(Val->AsNumber());
		}
		return TEXT("null");
	}
}

FAgentFrameworkPIEFunctionResult UAgentFrameworkPIERuntimeToolset::CallPIEActorFunction(
	const FString& ActorIdentifier,
	const FString& FunctionName,
	const TOptional<FString>& ParametersJson)
{
	FAgentFrameworkPIEFunctionResult Result;

	const FString TrimmedActor = ActorIdentifier.TrimStartAndEnd();
	const FString TrimmedFunc = FunctionName.TrimStartAndEnd();
	if (TrimmedActor.IsEmpty() || TrimmedFunc.IsEmpty())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CallPIEActorFunction"), TEXT("ActorIdentifier and FunctionName are required."));
		return Result;
	}

	UWorld* World = AgentFrameworkPIEInternal::GetActivePIEWorld();
	if (!World)
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CallPIEActorFunction"),
			TEXT("PIE is not currently running. Call EditorAppToolset.StartPIE first."));
		return Result;
	}

	FString Error;
	AActor* TargetActor = AgentFrameworkPIEInternal::FindSinglePIEActor(World, TrimmedActor, Error);
	if (!TargetActor)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CallPIEActorFunction"), Error);
		return Result;
	}

	UFunction* Func = TargetActor->FindFunction(*TrimmedFunc);
	if (!Func)
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CallPIEActorFunction"),
			FString::Printf(TEXT("Function '%s' not found on actor '%s' (class %s)."),
				*TrimmedFunc, *TargetActor->GetName(), *TargetActor->GetClass()->GetName()));
		return Result;
	}

	if (!Func->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_Exec)
		|| Func->HasAnyFunctionFlags(FUNC_EditorOnly)
		|| Func->GetName().StartsWith(TEXT("ExecuteUbergraph")))
	{
		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CallPIEActorFunction"),
			FString::Printf(TEXT("Function '%s' on '%s' is not a BlueprintCallable or Exec runtime function."),
				*TrimmedFunc, *TargetActor->GetName()));
		return Result;
	}

	TSharedPtr<FJsonObject> InputParams = MakeShared<FJsonObject>();
	const FString RawJson = ParametersJson.Get(TEXT("")).TrimStartAndEnd();
	if (!RawJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(RawJson);
		if (!FJsonSerializer::Deserialize(Reader, InputParams) || !InputParams.IsValid())
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CallPIEActorFunction"),
				TEXT("ParametersJson must be a valid JSON object."));
			return Result;
		}
	}

	// Verify all provided JSON keys match known non-return parameters of Func.
	TMap<FString, FProperty*> ParamByNameLower;
	for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty* Param = *It;
		if (!Param->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			ParamByNameLower.Add(Param->GetName().ToLower(), Param);
		}
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : InputParams->Values)
	{
		if (!ParamByNameLower.Contains(Pair.Key.ToLower()))
		{
			UAgentFrameworkActionUtils::RaiseToolError(
				TEXT("CallPIEActorFunction"),
				FString::Printf(TEXT("Unknown parameter '%s' for function '%s'."), *Pair.Key, *TrimmedFunc));
			return Result;
		}
	}

	uint8* FrameBuffer = nullptr;
	if (Func->PropertiesSize > 0)
	{
		FrameBuffer = static_cast<uint8*>(FMemory_Alloca_Aligned(Func->PropertiesSize, Func->GetMinAlignment()));
	}
	UAgentFrameworkActionUtils::FScopedUFunctionFrame Frame(Func, FrameBuffer);

	// Populate inputs and defaults
	for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty* Param = *It;
		if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			continue;
		}

		// Find JSON field (exact first, then case-insensitive fallback)
		TSharedPtr<FJsonValue> JsonVal = InputParams->TryGetField(Param->GetName());
		if (!JsonVal.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : InputParams->Values)
			{
				if (Pair.Key.Equals(Param->GetName(), ESearchCase::IgnoreCase))
				{
					JsonVal = Pair.Value;
					break;
				}
			}
		}

		void* DestPtr = Param->ContainerPtrToValuePtr<void>(FrameBuffer);
		if (JsonVal.IsValid())
		{
			FText FailReason;
			if (!FJsonObjectConverter::JsonValueToUProperty(JsonVal, Param, DestPtr, 0, 0, true, &FailReason))
			{
				UAgentFrameworkActionUtils::RaiseToolError(
					TEXT("CallPIEActorFunction"),
					FString::Printf(TEXT("Failed to convert JSON value for parameter '%s': %s"),
						*Param->GetName(), *FailReason.ToString()));
				return Result;
			}
			continue;
		}

		const bool bIsPureOutParam = Param->HasAnyPropertyFlags(CPF_OutParm) && !Param->HasAnyPropertyFlags(CPF_ConstParm);
		if (bIsPureOutParam)
		{
			continue;
		}

#if WITH_METADATA
		const FName DefaultMetaKey(*(TEXT("CPP_Default_") + Param->GetName()));
		if (Func->HasMetaData(DefaultMetaKey))
		{
			const FString& DefaultText = Func->GetMetaData(DefaultMetaKey);
			if (!DefaultText.IsEmpty() && !Param->ImportText_Direct(*DefaultText, DestPtr, TargetActor, PPF_None))
			{
				UAgentFrameworkActionUtils::RaiseToolError(
					TEXT("CallPIEActorFunction"),
					FString::Printf(TEXT("Failed to import default value '%s' for optional parameter '%s'."),
						*DefaultText, *Param->GetName()));
				return Result;
			}
			continue;
		}
#endif

		UAgentFrameworkActionUtils::RaiseToolError(
			TEXT("CallPIEActorFunction"),
			FString::Printf(TEXT("Missing required parameter '%s' for function '%s'."), *Param->GetName(), *TrimmedFunc));
		return Result;
	}

	TargetActor->ProcessEvent(Func, FrameBuffer);

	if (FProperty* RetProp = Func->GetReturnProperty())
	{
		const TSharedPtr<FJsonValue> RetJsonVal = FJsonObjectConverter::UPropertyToJsonValue(
			RetProp, RetProp->ContainerPtrToValuePtr<void>(FrameBuffer));
		Result.ReturnValueJson = AgentFrameworkPIEInternal::SerializeJsonValue(RetJsonVal);
	}

	TSharedRef<FJsonObject> OutParmsObj = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Func); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		FProperty* Param = *It;
		if (!Param->HasAnyPropertyFlags(CPF_ReturnParm)
			&& Param->HasAnyPropertyFlags(CPF_OutParm)
			&& !Param->HasAnyPropertyFlags(CPF_ConstParm))
		{
			const TSharedPtr<FJsonValue> OutVal = FJsonObjectConverter::UPropertyToJsonValue(
				Param, Param->ContainerPtrToValuePtr<void>(FrameBuffer));
			OutParmsObj->SetField(Param->GetName(), OutVal.IsValid() ? OutVal : MakeShared<FJsonValueNull>());
		}
	}

	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result.OutParametersJson);
		FJsonSerializer::Serialize(OutParmsObj, Writer);
	}

	Result.ActorName = TargetActor->GetName();
	Result.ActorLabel = TargetActor->GetActorLabel();
	Result.ActorClass = TargetActor->GetClass()->GetName();
	Result.FunctionName = Func->GetName();
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CallPIEActorFunction"), 0.0, true, {});
	return Result;
}
