// Copyright 2026 AgentFramework. All Rights Reserved.

#include "AgentFrameworkActionUtils.h"
#include "UObject/SavePackage.h"
#include "Misc/ScopeLock.h"
#include "HAL/CriticalSection.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AsyncFunctionResult/AsyncFunctionResultString.h"
#include "AsyncFunctionResult/AsyncFunctionResultUtils.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Templates/Atomic.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"

// ============================================================================
// Internal Telemetry Storage & Thread Safety
// ============================================================================

namespace AgentFrameworkTelemetryInternal
{
	static FCriticalSection TelemetryCS;
	static TMap<FString, FAgentFrameworkToolMetrics> ToolMetricsMap;
	static TArray<FAgentFrameworkErrorRecord> ErrorRingBuffer;
	static constexpr int32 MaxErrorRingBufferCapacity = 256;
}

// ============================================================================
// Parameter Parsing Utilities
// ============================================================================

bool UAgentFrameworkActionUtils::TryGetStringParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, FString& OutValue, TArray<FString>& OutErrors, bool bRequired)
{
	if (!InParams.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("JSON parameters object is invalid (null) when looking for field: %s"), *InFieldName));
		return false;
	}

	if (!InParams->TryGetStringField(InFieldName, OutValue))
	{
		if (bRequired)
		{
			OutErrors.Add(FString::Printf(TEXT("Parameter '%s' is required."), *InFieldName));
			return false;
		}
		return true;
	}

	if (bRequired && OutValue.IsEmpty())
	{
		OutErrors.Add(FString::Printf(TEXT("Parameter '%s' is required and must not be empty."), *InFieldName));
		return false;
	}

	return true;
}

bool UAgentFrameworkActionUtils::TryGetBoolParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, bool& OutValue, TArray<FString>& OutErrors, bool bRequired)
{
	if (!InParams.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("JSON parameters object is invalid (null) when looking for field: %s"), *InFieldName));
		return false;
	}

	if (!InParams->TryGetBoolField(InFieldName, OutValue))
	{
		if (bRequired)
		{
			OutErrors.Add(FString::Printf(TEXT("Parameter '%s' is required."), *InFieldName));
			return false;
		}
	}

	return true;
}

bool UAgentFrameworkActionUtils::TryGetDoubleParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, double& OutValue, TArray<FString>& OutErrors, bool bRequired)
{
	if (!InParams.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("JSON parameters object is invalid (null) when looking for field: %s"), *InFieldName));
		return false;
	}

	if (!InParams->TryGetNumberField(InFieldName, OutValue))
	{
		if (bRequired)
		{
			OutErrors.Add(FString::Printf(TEXT("Parameter '%s' is required and must be a number."), *InFieldName));
			return false;
		}
	}

	return true;
}

bool UAgentFrameworkActionUtils::TryGetFloatParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, float& OutValue, TArray<FString>& OutErrors, bool bRequired)
{
	double TempDouble = static_cast<double>(OutValue);
	if (!TryGetDoubleParam(InParams, InFieldName, TempDouble, OutErrors, bRequired))
	{
		return false;
	}
	OutValue = static_cast<float>(TempDouble);
	return true;
}

bool UAgentFrameworkActionUtils::TryGetIntParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, int32& OutValue, TArray<FString>& OutErrors, bool bRequired)
{
	double TempDouble = static_cast<double>(OutValue);
	if (!TryGetDoubleParam(InParams, InFieldName, TempDouble, OutErrors, bRequired))
	{
		return false;
	}
	OutValue = static_cast<int32>(TempDouble);
	return true;
}

bool UAgentFrameworkActionUtils::TryGetStringArrayParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, TArray<FString>& OutValue, TArray<FString>& OutErrors, bool bRequired)
{
	if (!InParams.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("JSON parameters object is invalid (null) when looking for field: %s"), *InFieldName));
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
	if (!InParams->TryGetArrayField(InFieldName, ArrayField))
	{
		if (bRequired)
		{
			OutErrors.Add(FString::Printf(TEXT("Parameter '%s' is required and must be an array."), *InFieldName));
			return false;
		}
		return true;
	}

	if (bRequired && ArrayField->Num() == 0)
	{
		OutErrors.Add(FString::Printf(TEXT("Parameter '%s' must not be empty."), *InFieldName));
		return false;
	}

	for (int32 Index = 0; Index < ArrayField->Num(); ++Index)
	{
		FString Val;
		if ((*ArrayField)[Index]->TryGetString(Val))
		{
			OutValue.Add(Val);
		}
		else
		{
			OutErrors.Add(FString::Printf(TEXT("Element at index %d in array '%s' is not a string."), Index, *InFieldName));
			return false;
		}
	}

	return true;
}

bool UAgentFrameworkActionUtils::TryGetObjectParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, const TSharedPtr<FJsonObject>*& OutValue, TArray<FString>& OutErrors, bool bRequired)
{
	if (!InParams.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("JSON parameters object is invalid (null) when looking for field: %s"), *InFieldName));
		return false;
	}

	if (!InParams->TryGetObjectField(InFieldName, OutValue))
	{
		if (bRequired)
		{
			OutErrors.Add(FString::Printf(TEXT("Parameter '%s' is required and must be an object."), *InFieldName));
			return false;
		}
	}

	return true;
}

bool UAgentFrameworkActionUtils::TryGetArrayParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, const TArray<TSharedPtr<FJsonValue>>*& OutValue, TArray<FString>& OutErrors, bool bRequired)
{
	if (!InParams.IsValid())
	{
		OutErrors.Add(FString::Printf(TEXT("JSON parameters object is invalid (null) when looking for field: %s"), *InFieldName));
		return false;
	}

	if (!InParams->TryGetArrayField(InFieldName, OutValue))
	{
		if (bRequired)
		{
			OutErrors.Add(FString::Printf(TEXT("Parameter '%s' is required and must be an array."), *InFieldName));
			return false;
		}
	}

	return true;
}

// ============================================================================
// Telemetry & Diagnostic Memory Implementation
// ============================================================================

void UAgentFrameworkActionUtils::RecordToolExecution(const FString& ToolName, double DurationMicros, bool bSuccess, const TArray<FString>& Errors, const FString& ContextSummary)
{
	FScopeLock Lock(&AgentFrameworkTelemetryInternal::TelemetryCS);

	FAgentFrameworkToolMetrics& Metrics = AgentFrameworkTelemetryInternal::ToolMetricsMap.FindOrAdd(ToolName);
	if (Metrics.TotalExecutions == 0)
	{
		Metrics.ToolName = ToolName;
		Metrics.TotalExecutions = 1;
		Metrics.SuccessCount = bSuccess ? 1 : 0;
		Metrics.ErrorCount = bSuccess ? 0 : 1;
		Metrics.TotalDurationMicros = DurationMicros;
		Metrics.MinDurationMicros = DurationMicros;
		Metrics.MaxDurationMicros = DurationMicros;
		Metrics.AvgDurationMicros = DurationMicros;
		Metrics.LastExecutionTime = FDateTime::UtcNow();
		Metrics.bLastSuccess = bSuccess;
	}
	else
	{
		Metrics.TotalExecutions++;
		if (bSuccess)
		{
			Metrics.SuccessCount++;
		}
		else
		{
			Metrics.ErrorCount++;
		}
		Metrics.TotalDurationMicros += DurationMicros;
		if (DurationMicros < Metrics.MinDurationMicros)
		{
			Metrics.MinDurationMicros = DurationMicros;
		}
		if (DurationMicros > Metrics.MaxDurationMicros)
		{
			Metrics.MaxDurationMicros = DurationMicros;
		}
		Metrics.AvgDurationMicros = Metrics.TotalDurationMicros / static_cast<double>(Metrics.TotalExecutions);
		Metrics.LastExecutionTime = FDateTime::UtcNow();
		Metrics.bLastSuccess = bSuccess;
	}

	if (!bSuccess || Errors.Num() > 0)
	{
		for (const FString& ErrMsg : Errors)
		{
			if (ErrMsg.IsEmpty())
			{
				continue;
			}

			FAgentFrameworkErrorRecord* ExistingRecord = nullptr;
			for (FAgentFrameworkErrorRecord& Record : AgentFrameworkTelemetryInternal::ErrorRingBuffer)
			{
				if (Record.ToolName == ToolName && Record.ErrorMessage == ErrMsg)
				{
					ExistingRecord = &Record;
					break;
				}
			}

			if (ExistingRecord)
			{
				ExistingRecord->Frequency++;
				ExistingRecord->Timestamp = FDateTime::UtcNow();
				if (!ContextSummary.IsEmpty())
				{
					ExistingRecord->ContextSummary = ContextSummary;
				}
			}
			else
			{
				if (AgentFrameworkTelemetryInternal::ErrorRingBuffer.Num() >= AgentFrameworkTelemetryInternal::MaxErrorRingBufferCapacity)
				{
					AgentFrameworkTelemetryInternal::ErrorRingBuffer.RemoveAt(0);
				}

				FAgentFrameworkErrorRecord NewRecord;
				NewRecord.ToolName = ToolName;
				NewRecord.ErrorMessage = ErrMsg;
				NewRecord.Timestamp = FDateTime::UtcNow();
				NewRecord.Frequency = 1;
				NewRecord.ContextSummary = ContextSummary;

				AgentFrameworkTelemetryInternal::ErrorRingBuffer.Add(NewRecord);
			}
		}
	}
}

void UAgentFrameworkActionUtils::RecordToolExecution(const FString& ToolName, double DurationMicros, bool bSuccess, const TArray<FString>& Errors, TSharedPtr<FJsonObject> Context)
{
	FString ContextSummary;
	if (Context.IsValid())
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ContextSummary);
		FJsonSerializer::Serialize(Context.ToSharedRef(), Writer);
		if (ContextSummary.Len() > 256)
		{
			ContextSummary = ContextSummary.Left(256) + TEXT("...");
		}
	}
	RecordToolExecution(ToolName, DurationMicros, bSuccess, Errors, ContextSummary);
}

TArray<FAgentFrameworkToolMetrics> UAgentFrameworkActionUtils::GetToolTelemetry(const FString& ToolNameFilter)
{
	FScopeLock Lock(&AgentFrameworkTelemetryInternal::TelemetryCS);
	TArray<FAgentFrameworkToolMetrics> Results;

	for (const auto& Pair : AgentFrameworkTelemetryInternal::ToolMetricsMap)
	{
		if (ToolNameFilter.IsEmpty() ||
			Pair.Key.Equals(ToolNameFilter, ESearchCase::IgnoreCase) ||
			Pair.Key.Contains(ToolNameFilter, ESearchCase::IgnoreCase))
		{
			Results.Add(Pair.Value);
		}
	}

	return Results;
}

TArray<FAgentFrameworkErrorRecord> UAgentFrameworkActionUtils::GetRecentErrors(int32 MaxCount, const FString& ToolNameFilter)
{
	FScopeLock Lock(&AgentFrameworkTelemetryInternal::TelemetryCS);
	TArray<FAgentFrameworkErrorRecord> Results;

	for (int32 Index = AgentFrameworkTelemetryInternal::ErrorRingBuffer.Num() - 1; Index >= 0; --Index)
	{
		const FAgentFrameworkErrorRecord& Record = AgentFrameworkTelemetryInternal::ErrorRingBuffer[Index];
		if (ToolNameFilter.IsEmpty() ||
			Record.ToolName.Equals(ToolNameFilter, ESearchCase::IgnoreCase) ||
			Record.ToolName.Contains(ToolNameFilter, ESearchCase::IgnoreCase))
		{
			Results.Add(Record);
			if (Results.Num() >= MaxCount)
			{
				break;
			}
		}
	}

	return Results;
}

FString UAgentFrameworkActionUtils::GetTelemetryMetricsJson()
{
	FScopeLock Lock(&AgentFrameworkTelemetryInternal::TelemetryCS);

	TSharedRef<FJsonObject> RootObject = MakeShared<FJsonObject>();

	int64 TotalExecutions = 0;
	int64 TotalSuccesses = 0;
	int64 TotalErrors = 0;

	TArray<TSharedPtr<FJsonValue>> MetricsArray;
	for (const auto& Pair : AgentFrameworkTelemetryInternal::ToolMetricsMap)
	{
		const FAgentFrameworkToolMetrics& M = Pair.Value;
		TotalExecutions += M.TotalExecutions;
		TotalSuccesses += M.SuccessCount;
		TotalErrors += M.ErrorCount;

		TSharedRef<FJsonObject> MetricObj = MakeShared<FJsonObject>();
		MetricObj->SetStringField(TEXT("tool_name"), M.ToolName);
		MetricObj->SetNumberField(TEXT("total_executions"), M.TotalExecutions);
		MetricObj->SetNumberField(TEXT("success_count"), M.SuccessCount);
		MetricObj->SetNumberField(TEXT("error_count"), M.ErrorCount);
		MetricObj->SetNumberField(TEXT("total_duration_micros"), M.TotalDurationMicros);
		MetricObj->SetNumberField(TEXT("min_duration_micros"), M.MinDurationMicros);
		MetricObj->SetNumberField(TEXT("max_duration_micros"), M.MaxDurationMicros);
		MetricObj->SetNumberField(TEXT("avg_duration_micros"), M.AvgDurationMicros);
		MetricObj->SetStringField(TEXT("last_execution_time"), M.LastExecutionTime.ToIso8601());
		MetricObj->SetBoolField(TEXT("last_success"), M.bLastSuccess);

		MetricsArray.Add(MakeShared<FJsonValueObject>(MetricObj));
	}

	RootObject->SetNumberField(TEXT("total_tools_tracked"), AgentFrameworkTelemetryInternal::ToolMetricsMap.Num());
	RootObject->SetNumberField(TEXT("total_executions"), TotalExecutions);
	RootObject->SetNumberField(TEXT("total_successes"), TotalSuccesses);
	RootObject->SetNumberField(TEXT("total_errors"), TotalErrors);
	RootObject->SetArrayField(TEXT("metrics"), MetricsArray);

	TArray<TSharedPtr<FJsonValue>> ErrorsArray;
	for (int32 Index = AgentFrameworkTelemetryInternal::ErrorRingBuffer.Num() - 1; Index >= 0; --Index)
	{
		const FAgentFrameworkErrorRecord& E = AgentFrameworkTelemetryInternal::ErrorRingBuffer[Index];
		TSharedRef<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetStringField(TEXT("tool_name"), E.ToolName);
		ErrorObj->SetStringField(TEXT("error_message"), E.ErrorMessage);
		ErrorObj->SetStringField(TEXT("timestamp"), E.Timestamp.ToIso8601());
		ErrorObj->SetNumberField(TEXT("frequency"), E.Frequency);
		ErrorObj->SetStringField(TEXT("context"), E.ContextSummary);

		ErrorsArray.Add(MakeShared<FJsonValueObject>(ErrorObj));
	}
	RootObject->SetArrayField(TEXT("recent_errors"), ErrorsArray);

	FString OutputJson;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputJson);
	FJsonSerializer::Serialize(RootObject, Writer);

	return OutputJson;
}

void UAgentFrameworkActionUtils::ClearTelemetryData()
{
	FScopeLock Lock(&AgentFrameworkTelemetryInternal::TelemetryCS);
	AgentFrameworkTelemetryInternal::ToolMetricsMap.Empty();
	AgentFrameworkTelemetryInternal::ErrorRingBuffer.Empty();
}

bool UAgentFrameworkActionUtils::IsValidCppIdentifier(const FString& Name)
{
	if (Name.IsEmpty() || Name.Len() > 128)
	{
		return false;
	}

	const TCHAR First = Name[0];
	if (!FChar::IsAlpha(First) && First != TEXT('_'))
	{
		return false;
	}

	for (int32 Index = 1; Index < Name.Len(); ++Index)
	{
		const TCHAR Ch = Name[Index];
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_'))
		{
			return false;
		}
	}

	static const TSet<FString> DisallowedIdentifiers = {
		TEXT("alignas"), TEXT("alignof"), TEXT("and"), TEXT("asm"), TEXT("auto"),
		TEXT("bool"), TEXT("break"), TEXT("case"), TEXT("catch"), TEXT("char"),
		TEXT("class"), TEXT("const"), TEXT("consteval"), TEXT("constexpr"), TEXT("constinit"),
		TEXT("continue"), TEXT("co_await"), TEXT("co_return"), TEXT("co_yield"),
		TEXT("decltype"), TEXT("default"), TEXT("delete"), TEXT("do"), TEXT("double"),
		TEXT("dynamic_cast"), TEXT("else"), TEXT("enum"), TEXT("explicit"), TEXT("export"),
		TEXT("extern"), TEXT("false"), TEXT("final"), TEXT("float"), TEXT("for"),
		TEXT("friend"), TEXT("goto"), TEXT("if"), TEXT("inline"), TEXT("int"),
		TEXT("long"), TEXT("mutable"), TEXT("namespace"), TEXT("new"), TEXT("noexcept"),
		TEXT("nullptr"), TEXT("operator"), TEXT("override"), TEXT("private"), TEXT("protected"),
		TEXT("public"), TEXT("register"), TEXT("reinterpret_cast"), TEXT("return"), TEXT("short"),
		TEXT("signed"), TEXT("sizeof"), TEXT("static"), TEXT("static_assert"), TEXT("static_cast"),
		TEXT("struct"), TEXT("switch"), TEXT("template"), TEXT("this"), TEXT("thread_local"),
		TEXT("throw"), TEXT("true"), TEXT("try"), TEXT("typedef"), TEXT("typeid"),
		TEXT("typename"), TEXT("union"), TEXT("unsigned"), TEXT("using"), TEXT("virtual"),
		TEXT("void"), TEXT("volatile"), TEXT("wchar_t"), TEXT("while")
	};

	return !DisallowedIdentifiers.Contains(Name);
}

bool UAgentFrameworkActionUtils::FormatCppFloatLiteral(double Value, FString& OutLiteral)
{
	if (!FMath::IsFinite(Value))
	{
		return false;
	}

	FString Formatted = FString::Printf(TEXT("%.9g"), Value);
	if (!Formatted.Contains(TEXT(".")) && !Formatted.Contains(TEXT("e"), ESearchCase::IgnoreCase))
	{
		Formatted += TEXT(".0");
	}
	OutLiteral = Formatted + TEXT("f");
	return true;
}

bool UAgentFrameworkActionUtils::IsWritableSourcePath(const FString& FilePath)
{
	if (FilePath.IsEmpty() || FilePath.Contains(TEXT("..")))
	{
		return false;
	}

	for (TCHAR Ch : FilePath)
	{
		if (Ch == TEXT('\r') || Ch == TEXT('\n') || Ch == TEXT('\0'))
		{
			return false;
		}
	}

	if (FilePath.EndsWith(TEXT(".Build.cs"), ESearchCase::IgnoreCase)
		|| FilePath.EndsWith(TEXT(".Target.cs"), ESearchCase::IgnoreCase)
		|| FilePath.EndsWith(TEXT(".uplugin"), ESearchCase::IgnoreCase)
		|| FilePath.EndsWith(TEXT(".uproject"), ESearchCase::IgnoreCase))
	{
		return false;
	}

	const FString Ext = FPaths::GetExtension(FilePath);
	if (!Ext.IsEmpty())
	{
		const bool bAllowedExt = Ext.Equals(TEXT("h"), ESearchCase::IgnoreCase)
			|| Ext.Equals(TEXT("hpp"), ESearchCase::IgnoreCase)
			|| Ext.Equals(TEXT("inl"), ESearchCase::IgnoreCase)
			|| Ext.Equals(TEXT("cpp"), ESearchCase::IgnoreCase)
			|| Ext.Equals(TEXT("c"), ESearchCase::IgnoreCase);
		if (!bAllowedExt)
		{
			return false;
		}
	}

	FString NormalizedFile = FPaths::ConvertRelativePathToFull(FilePath);
	FPaths::CollapseRelativeDirectories(NormalizedFile);
	FPaths::NormalizeFilename(NormalizedFile);

	FString ProjectSourcePrefix = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir());
	FPaths::CollapseRelativeDirectories(ProjectSourcePrefix);
	FPaths::NormalizeDirectoryName(ProjectSourcePrefix);
	ProjectSourcePrefix += TEXT("/");

	if (NormalizedFile.StartsWith(ProjectSourcePrefix, ESearchCase::IgnoreCase))
	{
		const FString RelFromSource = NormalizedFile.RightChop(ProjectSourcePrefix.Len());
		return !RelFromSource.IsEmpty() && !RelFromSource.Contains(TEXT("/.."));
	}

	FString ProjectPluginsPrefix = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
	FPaths::CollapseRelativeDirectories(ProjectPluginsPrefix);
	FPaths::NormalizeDirectoryName(ProjectPluginsPrefix);
	ProjectPluginsPrefix += TEXT("/");

	if (NormalizedFile.StartsWith(ProjectPluginsPrefix, ESearchCase::IgnoreCase))
	{
		const FString RelFromPlugins = NormalizedFile.RightChop(ProjectPluginsPrefix.Len());
		TArray<FString> Segments;
		RelFromPlugins.ParseIntoArray(Segments, TEXT("/"), true);

		static const TSet<FString> NonSourceTopDirs = {
			TEXT("Content"), TEXT("Saved"), TEXT("Intermediate"), TEXT("Binaries"), TEXT("Config"), TEXT("Resources")
		};
		if (Segments.Num() >= 2 && !NonSourceTopDirs.Contains(Segments[0]))
		{
			if (Segments[1].Equals(TEXT("Source"), ESearchCase::IgnoreCase))
			{
				return true;
			}
			if (Segments.Num() >= 3 && !NonSourceTopDirs.Contains(Segments[1]) && Segments[2].Equals(TEXT("Source"), ESearchCase::IgnoreCase))
			{
				const FString CandidatePluginRoot = FPaths::Combine(ProjectPluginsPrefix, Segments[0], Segments[1]);
				TArray<FString> FoundUplugins;
				IFileManager::Get().FindFiles(FoundUplugins, *(CandidatePluginRoot / TEXT("*.uplugin")), true, false);
				return FoundUplugins.Num() > 0;
			}
		}
	}

	return false;
}

bool UAgentFrameworkActionUtils::ResolveWritableModulePaths(
	const FString& RequestedModuleName,
	const FString& SubDirectory,
	FString& OutModuleName,
	FString& OutHeaderDir,
	FString& OutSourceDir,
	FString& OutIncludePrefix,
	FString& OutApiMacro,
	FString& OutError)
{
	TArray<FString> SubSegments;
	if (!SubDirectory.IsEmpty())
	{
		FString NormalizedSub = SubDirectory;
		NormalizedSub.ReplaceInline(TEXT("\\"), TEXT("/"));
		if (NormalizedSub.Contains(TEXT("..")) || NormalizedSub.StartsWith(TEXT("/")) || NormalizedSub.Contains(TEXT(":")))
		{
			OutError = FString::Printf(TEXT("Invalid SubDirectory '%s': relative paths or traversal ('..') are not allowed."), *SubDirectory);
			return false;
		}
		NormalizedSub.ParseIntoArray(SubSegments, TEXT("/"), true);
		for (const FString& Seg : SubSegments)
		{
			if (Seg == TEXT(".") || Seg == TEXT(".."))
			{
				OutError = FString::Printf(TEXT("Invalid SubDirectory segment '%s'."), *Seg);
				return false;
			}
			for (TCHAR Ch : Seg)
			{
				if (!FChar::IsAlnum(Ch) && Ch != TEXT('_') && Ch != TEXT('-'))
				{
					OutError = FString::Printf(TEXT("SubDirectory segment '%s' contains unsupported character '%s'."), *Seg, *FString::Chr(Ch));
					return false;
				}
			}
		}
	}

	FString CandidateModuleName = RequestedModuleName.TrimStartAndEnd();
	if (CandidateModuleName.IsEmpty())
	{
		CandidateModuleName = FApp::GetProjectName();
	}
	if (!IsValidCppIdentifier(CandidateModuleName))
	{
		OutError = FString::Printf(TEXT("Invalid C++ module name '%s'."), *CandidateModuleName);
		return false;
	}

	FString ModuleRootDir;
	const FString GameSourceDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir());
	const FString DirectGameModule = FPaths::Combine(GameSourceDir, CandidateModuleName);
	if (FPaths::DirectoryExists(DirectGameModule) && FPaths::FileExists(FPaths::Combine(DirectGameModule, CandidateModuleName + TEXT(".Build.cs"))))
	{
		ModuleRootDir = DirectGameModule;
	}
	else if (RequestedModuleName.IsEmpty() && FPaths::DirectoryExists(GameSourceDir))
	{
		TArray<FString> BuildFiles;
		IFileManager::Get().FindFilesRecursive(BuildFiles, *GameSourceDir, TEXT("*.Build.cs"), true, false);
		if (BuildFiles.Num() == 1)
		{
			ModuleRootDir = FPaths::GetPath(BuildFiles[0]);
			CandidateModuleName = FPaths::GetCleanFilename(ModuleRootDir);
		}
	}

	if (ModuleRootDir.IsEmpty())
	{
		const FString PluginsDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir());
		if (FPaths::DirectoryExists(PluginsDir))
		{
			TArray<FString> BuildFiles;
			const FString TargetBuildFile = CandidateModuleName + TEXT(".Build.cs");
			IFileManager::Get().FindFilesRecursive(BuildFiles, *PluginsDir, *TargetBuildFile, true, false);
			for (const FString& FoundBuildCs : BuildFiles)
			{
				const FString CandidateDir = FPaths::GetPath(FoundBuildCs);
				if (IsWritableSourcePath(FoundBuildCs.Replace(TEXT(".Build.cs"), TEXT(".h"))))
				{
					ModuleRootDir = CandidateDir;
					break;
				}
			}
		}
	}

	if (ModuleRootDir.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Could not locate writable source module '%s' with '%s.Build.cs' in project Source or Plugins."), *CandidateModuleName, *CandidateModuleName);
		return false;
	}

	OutModuleName = CandidateModuleName;
	OutApiMacro = OutModuleName.ToUpper() + TEXT("_API");

	const FString PublicDir = FPaths::Combine(ModuleRootDir, TEXT("Public"));
	const FString PrivateDir = FPaths::Combine(ModuleRootDir, TEXT("Private"));
	const bool bHasSplitPublicPrivate = FPaths::DirectoryExists(PublicDir) && FPaths::DirectoryExists(PrivateDir);

	const FString RelSubPath = FString::Join(SubSegments, TEXT("/"));
	if (bHasSplitPublicPrivate)
	{
		OutHeaderDir = RelSubPath.IsEmpty() ? PublicDir : FPaths::Combine(PublicDir, RelSubPath);
		OutSourceDir = RelSubPath.IsEmpty() ? PrivateDir : FPaths::Combine(PrivateDir, RelSubPath);
		OutIncludePrefix = RelSubPath.IsEmpty() ? TEXT("") : (RelSubPath + TEXT("/"));
	}
	else
	{
		const FString CombinedDir = RelSubPath.IsEmpty() ? ModuleRootDir : FPaths::Combine(ModuleRootDir, RelSubPath);
		OutHeaderDir = CombinedDir;
		OutSourceDir = CombinedDir;
		OutIncludePrefix = RelSubPath.IsEmpty() ? TEXT("") : (RelSubPath + TEXT("/"));
	}

	return true;
}

bool UAgentFrameworkActionUtils::EnsureAssetDoesNotExist(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FString& OutError)
{
	FString PackagePath;
	SplitAssetPath(AssetPath, OutPackageName, PackagePath, OutAssetName);
	if (OutPackageName.IsEmpty() || OutAssetName.IsEmpty() || !OutPackageName.StartsWith(TEXT("/")))
	{
		OutError = FString::Printf(TEXT("Invalid Unreal asset path: '%s'."), *AssetPath);
		return false;
	}

	FText ValidationReason;
	if (!FPackageName::IsValidLongPackageName(OutPackageName, /*bIncludeReadOnlyRoots=*/false, &ValidationReason))
	{
		OutError = FString::Printf(TEXT("Invalid package path '%s': %s"), *OutPackageName, *ValidationReason.ToString());
		return false;
	}

	const FString ObjectPath = OutPackageName + TEXT(".") + OutAssetName;
	if (FindObject<UObject>(nullptr, *ObjectPath) != nullptr || FPackageName::DoesPackageExist(OutPackageName))
	{
		OutError = FString::Printf(TEXT("Asset already exists at '%s'."), *OutPackageName);
		return false;
	}

	return true;
}

void UAgentFrameworkActionUtils::CleanupFailedNewAsset(UPackage* Package, UObject* Asset)
{
	auto TrashObject = [](UObject* Obj)
	{
		if (!Obj || Obj == GetTransientPackage())
		{
			return;
		}
		if (Obj->IsRooted())
		{
			Obj->RemoveFromRoot();
		}
		Obj->ClearFlags(RF_Public | RF_Standalone | RF_Transactional);
		Obj->MarkAsGarbage();
	};

	const ERenameFlags PkgRenameFlags =
		REN_AllowPackageLinkerMismatch | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional | REN_SkipGeneratedClasses;

	bool bRenamedPackage = false;
	if (Package && Package != GetTransientPackage())
	{
		Package->SetDirtyFlag(false);
		const FName FailedPkgName = MakeUniqueObjectName(
			nullptr,
			UPackage::StaticClass(),
			*FString::Printf(TEXT("%s_FAILED"), *Package->GetName()));
		bRenamedPackage = Package->Rename(*FailedPkgName.ToString(), nullptr, PkgRenameFlags);

		TArray<UObject*> InnerObjects;
		GetObjectsWithPackage(Package, InnerObjects, EGetObjectsFlags::IncludeNestedObjects);
		for (UObject* InnerObj : InnerObjects)
		{
			if (!bRenamedPackage && InnerObj && InnerObj->GetOuter() == Package)
			{
				InnerObj->Rename(nullptr, GetTransientPackage(), PkgRenameFlags);
			}
			TrashObject(InnerObj);
		}
		TrashObject(Package);
	}

	if (Asset && Asset != GetTransientPackage())
	{
		if ((!Package || !bRenamedPackage) && Asset->GetOuter() != GetTransientPackage())
		{
			Asset->Rename(nullptr, GetTransientPackage(), PkgRenameFlags);
		}
		TrashObject(Asset);
	}
}

bool UAgentFrameworkActionUtils::SaveAssetChecked(UPackage* Package, UObject* Asset, bool bIsNewAsset, FString& OutError)
{
	if (!Package || !Asset)
	{
		OutError = TEXT("Cannot save null package or asset.");
		return false;
	}

	Asset->MarkPackageDirty();
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	if (!SaveAssetPackage(Package, Asset, PackageFileName))
	{
		OutError = FString::Printf(TEXT("Failed to save asset package '%s' to '%s'."), *Package->GetName(), *PackageFileName);
		if (bIsNewAsset)
		{
			CleanupFailedNewAsset(Package, Asset);
		}
		return false;
	}

	if (bIsNewAsset)
	{
		FAssetRegistryModule::AssetCreated(Asset);
	}

	return true;
}

void UAgentFrameworkActionUtils::RaiseToolError(const FString& ToolName, const FString& ErrorMessage)
{
	RecordToolExecution(ToolName, 0.0, false, { ErrorMessage });
	UKismetSystemLibrary::RaiseScriptError(ErrorMessage);
}

UAsyncFunctionResultString* UAgentFrameworkActionUtils::RunMonitoredProcessAsync(
	const FString& ToolName,
	const FString& Executable,
	const FString& Params,
	float TimeoutSeconds,
	TFunction<void(int32 ExitCode, const FString& Output, FString& OutValue, FString& OutError)> OnComplete)
{
	if (Executable.IsEmpty())
	{
		const FString Error = FString::Printf(TEXT("%s: Executable path is empty."), *ToolName);
		RecordToolExecution(ToolName, 0.0, false, { Error });
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(Error);
	}

	if (TimeoutSeconds <= 0.0f)
	{
		const FString Error = FString::Printf(TEXT("%s: TimeoutSeconds must be positive (got %.2f)."), *ToolName, TimeoutSeconds);
		RecordToolExecution(ToolName, 0.0, false, { Error });
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(Error);
	}

	struct FAgentAsyncProcessState
	{
		TStrongObjectPtr<UAsyncFunctionResultString> ResultStrong;
		TSharedPtr<FMonitoredProcess> Process;
		TFunction<void(int32, const FString&, FString&, FString&)> OnComplete;
		FString ToolName;
		FCriticalSection OutputCS;
		FString OutputBuffer;
		double StartTimeSeconds = 0.0;
		double TimeoutSeconds = 300.0;
		TAtomic<bool> bProcessCompleted{ false };
		TAtomic<bool> bProcessCanceled{ false };
		TAtomic<int32> ExitCode{ -1 };
	};

	TSharedRef<FAgentAsyncProcessState> State = MakeShared<FAgentAsyncProcessState>();
	UAsyncFunctionResultString* AsyncResult = NewObject<UAsyncFunctionResultString>();
	State->ResultStrong = TStrongObjectPtr<UAsyncFunctionResultString>(AsyncResult);
	State->Process = MakeShared<FMonitoredProcess>(Executable, Params, /*InHidden=*/true, /*InCreatePipes=*/true);
	State->OnComplete = MoveTemp(OnComplete);
	State->ToolName = ToolName;
	State->StartTimeSeconds = FPlatformTime::Seconds();
	State->TimeoutSeconds = static_cast<double>(TimeoutSeconds);

	static constexpr int32 MaxRetainedOutputChars = 60000;
	static constexpr int32 OutputTruncateThresholdChars = 65536;

	TWeakPtr<FAgentAsyncProcessState> WeakState = State;
	State->Process->OnOutput().BindLambda([WeakState](const FString& Line)
	{
		if (TSharedPtr<FAgentAsyncProcessState> Pinned = WeakState.Pin())
		{
			FScopeLock Lock(&Pinned->OutputCS);
			if (!Pinned->OutputBuffer.IsEmpty())
			{
				Pinned->OutputBuffer += TEXT("\n");
			}
			Pinned->OutputBuffer += Line;
			if (Pinned->OutputBuffer.Len() > OutputTruncateThresholdChars)
			{
				Pinned->OutputBuffer = TEXT("...[truncated older output]...\n") + Pinned->OutputBuffer.Right(MaxRetainedOutputChars);
			}
		}
	});
	State->Process->OnCompleted().BindLambda([WeakState](int32 Code)
	{
		if (TSharedPtr<FAgentAsyncProcessState> Pinned = WeakState.Pin())
		{
			Pinned->ExitCode.Store(Code);
			Pinned->bProcessCompleted.Store(true);
		}
	});
	State->Process->OnCanceled().BindLambda([WeakState]()
	{
		if (TSharedPtr<FAgentAsyncProcessState> Pinned = WeakState.Pin())
		{
			Pinned->bProcessCanceled.Store(true);
		}
	});

	if (!State->Process->Launch())
	{
		const FString Error = FString::Printf(TEXT("%s: Failed to launch process '%s %s'."), *ToolName, *Executable, *Params);
		RecordToolExecution(ToolName, 0.0, false, { Error });
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(Error);
	}

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime) -> bool
	{
		if (IsEngineExitRequested() || GExitPurge)
		{
			State->Process->Cancel(true);
			State->ResultStrong.Reset();
			return false;
		}

		if (!State->ResultStrong.IsValid())
		{
			State->Process->Cancel(true);
			return false;
		}

		if (State->ResultStrong->IsCanceled())
		{
			State->Process->Cancel(true);
			State->ResultStrong.Reset();
			return false;
		}

		const bool bStillRunning = State->Process->Update();
		if (!bStillRunning || State->bProcessCompleted.Load() || State->bProcessCanceled.Load())
		{
			if (bStillRunning)
			{
				// Wait one more tick so FMonitoredProcess finishes draining pipes and marks !bIsRunning.
				return true;
			}

			FString CapturedOutput;
			{
				FScopeLock Lock(&State->OutputCS);
				CapturedOutput = State->OutputBuffer;
			}

			const double ElapsedMicros = (FPlatformTime::Seconds() - State->StartTimeSeconds) * 1000000.0;
			if (State->bProcessCanceled.Load())
			{
				const FString CancelErr = FString::Printf(TEXT("%s was canceled."), *State->ToolName);
				RecordToolExecution(State->ToolName, ElapsedMicros, false, { CancelErr });
				State->ResultStrong->SetError(CancelErr);
			}
			else
			{
				const int32 FinalExitCode = State->Process->GetReturnCode();
				FString OutValue;
				FString OutError;
				if (State->OnComplete)
				{
					State->OnComplete(FinalExitCode, CapturedOutput, OutValue, OutError);
				}
				else if (FinalExitCode == 0)
				{
					OutValue = CapturedOutput;
				}
				else
				{
					OutError = FString::Printf(TEXT("%s exited with code %d.\n%s"), *State->ToolName, FinalExitCode, *CapturedOutput);
				}

				if (!OutError.IsEmpty())
				{
					RecordToolExecution(State->ToolName, ElapsedMicros, false, { OutError });
					State->ResultStrong->SetError(OutError);
				}
				else
				{
					RecordToolExecution(State->ToolName, ElapsedMicros, true, {});
					State->ResultStrong->SetValue(OutValue);
				}
			}

			State->ResultStrong.Reset();
			return false;
		}

		if ((FPlatformTime::Seconds() - State->StartTimeSeconds) >= State->TimeoutSeconds)
		{
			State->Process->Cancel(true);
			const double ElapsedMicros = (FPlatformTime::Seconds() - State->StartTimeSeconds) * 1000000.0;
			const FString TimeoutErr = FString::Printf(TEXT("%s timed out after %.0f seconds."), *State->ToolName, State->TimeoutSeconds);
			RecordToolExecution(State->ToolName, ElapsedMicros, false, { TimeoutErr });
			State->ResultStrong->SetError(TimeoutErr);
			State->ResultStrong.Reset();
			return false;
		}

		return true;
	}), 0.1f);

	return AsyncResult;
}

// ============================================================================
// FAgentFrameworkScopedTelemetry Profiler
// ============================================================================

FAgentFrameworkScopedTelemetry::FAgentFrameworkScopedTelemetry(const FString& InToolName, TSharedPtr<FJsonObject> InContext)
	: ToolName(InToolName)
	, StartTimeSeconds(FPlatformTime::Seconds())
	, bSuccess(false)
	, bResultSet(false)
{
	if (InContext.IsValid())
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ContextSummary);
		FJsonSerializer::Serialize(InContext.ToSharedRef(), Writer);
		if (ContextSummary.Len() > 256)
		{
			ContextSummary = ContextSummary.Left(256) + TEXT("...");
		}
	}
}

FAgentFrameworkScopedTelemetry::FAgentFrameworkScopedTelemetry(const FString& InToolName, const FString& InContextSummary)
	: ToolName(InToolName)
	, ContextSummary(InContextSummary)
	, StartTimeSeconds(FPlatformTime::Seconds())
	, bSuccess(false)
	, bResultSet(false)
{
}

FAgentFrameworkScopedTelemetry::~FAgentFrameworkScopedTelemetry()
{
	double DurationMicros = (FPlatformTime::Seconds() - StartTimeSeconds) * 1000000.0;
	if (DurationMicros < 0.0)
	{
		DurationMicros = 0.0;
	}
	UAgentFrameworkActionUtils::RecordToolExecution(ToolName, DurationMicros, bSuccess, Errors, ContextSummary);
}

void FAgentFrameworkScopedTelemetry::SetResult(bool bInSuccess, const TArray<FString>& InErrors)
{
	bSuccess = bInSuccess;
	Errors = InErrors;
	bResultSet = true;
}

bool UAgentFrameworkActionUtils::SaveAssetPackage(UPackage* Package, UObject* Asset, const FString& PackageFileName)
{
	if (!Package || !Asset || PackageFileName.IsEmpty())
	{
		return false;
	}

	Package->FullyLoad();

	FSavePackageArgs SaveArgs;
	return UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
}

FString UAgentFrameworkActionUtils::NormalizeAssetObjectPath(const FString& InPath)
{
	if (InPath.IsEmpty())
	{
		return InPath;
	}

	int32 LastDotIndex = INDEX_NONE;
	int32 LastSlashIndex = INDEX_NONE;
	InPath.FindLastChar(TEXT('.'), LastDotIndex);
	InPath.FindLastChar(TEXT('/'), LastSlashIndex);

	if (LastDotIndex != INDEX_NONE && LastDotIndex > LastSlashIndex)
	{
		return InPath;
	}

	FString AssetName = FPackageName::GetLongPackageAssetName(InPath);
	if (!AssetName.IsEmpty())
	{
		return FString::Printf(TEXT("%s.%s"), *InPath, *AssetName);
	}

	return InPath;
}

void UAgentFrameworkActionUtils::SplitAssetPath(const FString& InPath, FString& OutPackageName, FString& OutPackagePath, FString& OutAssetName)
{
	OutPackageName = InPath;
	OutPackagePath.Reset();
	OutAssetName.Reset();

	if (InPath.IsEmpty())
	{
		return;
	}

	// A trailing slash would otherwise yield an empty asset name and break suffix detection.
	while (OutPackageName.Len() > 1 && OutPackageName.EndsWith(TEXT("/")))
	{
		OutPackageName = OutPackageName.LeftChop(1);
	}

	// Strip a trailing ".ObjectName" suffix, but only when the dot comes after the last
	// slash — a dot earlier in the path is part of a folder name, not an object suffix.
	int32 LastSlashIndex = INDEX_NONE;
	int32 LastDotIndex = INDEX_NONE;
	OutPackageName.FindLastChar(TEXT('/'), LastSlashIndex);
	OutPackageName.FindLastChar(TEXT('.'), LastDotIndex);
	if (LastDotIndex != INDEX_NONE && LastDotIndex > LastSlashIndex)
	{
		OutPackageName = OutPackageName.Left(LastDotIndex);
	}

	OutPackagePath = FPackageName::GetLongPackagePath(OutPackageName);
	OutAssetName = FPackageName::GetLongPackageAssetName(OutPackageName);
}

