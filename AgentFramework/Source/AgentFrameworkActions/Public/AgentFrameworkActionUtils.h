// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Dom/JsonObject.h"
#include "Misc/DateTime.h"
#include "AgentFrameworkActionUtils.generated.h"

/**
 * Single tool execution record for telemetry tracking.
 */
USTRUCT(BlueprintType)
struct AGENTFRAMEWORKACTIONS_API FAgentFrameworkToolTelemetryRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	FString ToolName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	FDateTime Timestamp = FDateTime::MinValue();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	double DurationMicros = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	bool bSuccess = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	TArray<FString> Errors;
};

/**
 * Aggregated telemetry metrics for a tool over time.
 */
USTRUCT(BlueprintType)
struct AGENTFRAMEWORKACTIONS_API FAgentFrameworkToolMetrics
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	FString ToolName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	int64 TotalExecutions = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	int64 SuccessCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	int64 ErrorCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	double TotalDurationMicros = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	double MinDurationMicros = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	double MaxDurationMicros = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	double AvgDurationMicros = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	FDateTime LastExecutionTime = FDateTime::MinValue();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	bool bLastSuccess = false;
};

/**
 * Record of an error occurrence stored in the error ring buffer memory.
 */
USTRUCT(BlueprintType)
struct AGENTFRAMEWORKACTIONS_API FAgentFrameworkErrorRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	FString ToolName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	FString ErrorMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	FDateTime Timestamp = FDateTime::MinValue();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	int32 Frequency = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Telemetry")
	FString ContextSummary;
};

/**
 * Scoped RAII timing profiler that measures tool execution duration in microseconds
 * and automatically logs execution telemetry and errors upon destruction.
 */
struct AGENTFRAMEWORKACTIONS_API FAgentFrameworkScopedTelemetry
{
public:
	FAgentFrameworkScopedTelemetry(const FString& InToolName, TSharedPtr<FJsonObject> InContext = nullptr);
	FAgentFrameworkScopedTelemetry(const FString& InToolName, const FString& InContextSummary);
	~FAgentFrameworkScopedTelemetry();

	/** Set outcome result before scope exit */
	void SetResult(bool bInSuccess, const TArray<FString>& InErrors = TArray<FString>());

private:
	FString ToolName;
	FString ContextSummary;
	double StartTimeSeconds = 0.0;
	bool bSuccess = false;
	TArray<FString> Errors;
	bool bResultSet = false;
};

/**
 * UAgentFrameworkActionUtils
 *
 * Consolidated utilities for AgentFramework actions, including JSON parsing boilerplate
 * and microsecond tool telemetry & error diagnostics memory.
 */
UCLASS()
class AGENTFRAMEWORKACTIONS_API UAgentFrameworkActionUtils : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Helper to get a string field from a JSON object.
	 * If the field is required and missing or empty, adds an error message to OutErrors and returns false.
	 */
	static bool TryGetStringParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, FString& OutValue, TArray<FString>& OutErrors, bool bRequired = true);

	/**
	 * Helper to get a boolean field from a JSON object.
	 * If required and missing, adds an error message to OutErrors and returns false.
	 */
	static bool TryGetBoolParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, bool& OutValue, TArray<FString>& OutErrors, bool bRequired = false);

	/**
	 * Helper to get a double field from a JSON object.
	 * If required and missing, adds an error message to OutErrors and returns false.
	 */
	static bool TryGetDoubleParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, double& OutValue, TArray<FString>& OutErrors, bool bRequired = false);

	/**
	 * Helper to get a float field from a JSON object.
	 * If required and missing, adds an error message to OutErrors and returns false.
	 */
	static bool TryGetFloatParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, float& OutValue, TArray<FString>& OutErrors, bool bRequired = false);

	/**
	 * Helper to get an integer field from a JSON object.
	 * If required and missing, adds an error message to OutErrors and returns false.
	 */
	static bool TryGetIntParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, int32& OutValue, TArray<FString>& OutErrors, bool bRequired = false);

	/**
	 * Helper to get an array of strings from a JSON object.
	 * If required and missing/empty, adds an error message to OutErrors and returns false.
	 */
	static bool TryGetStringArrayParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, TArray<FString>& OutValue, TArray<FString>& OutErrors, bool bRequired = true);

	/**
	 * Helper to get a nested JSON object from a JSON object.
	 * If required and missing, adds an error message to OutErrors and returns false.
	 */
	static bool TryGetObjectParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, const TSharedPtr<FJsonObject>*& OutValue, TArray<FString>& OutErrors, bool bRequired = false);

	/**
	 * Helper to get a generic array from a JSON object.
	 * If required and missing, adds an error message to OutErrors and returns false.
	 */
	static bool TryGetArrayParam(const TSharedPtr<FJsonObject>& InParams, const FString& InFieldName, const TArray<TSharedPtr<FJsonValue>>*& OutValue, TArray<FString>& OutErrors, bool bRequired = false);

	// =========================================================================
	// Telemetry & Diagnostic Memory Methods
	// =========================================================================

	/**
	 * Record a tool execution into telemetry metrics and error ring buffer memory.
	 * Thread-safe.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentFramework|Telemetry")
	static void RecordToolExecution(const FString& ToolName, double DurationMicros, bool bSuccess, const TArray<FString>& Errors, const FString& ContextSummary = TEXT(""));

	/** Overload taking TSharedPtr<FJsonObject> for context parameter summary */
	static void RecordToolExecution(const FString& ToolName, double DurationMicros, bool bSuccess, const TArray<FString>& Errors, TSharedPtr<FJsonObject> Context);

	/**
	 * Retrieve aggregated tool metrics, optionally filtered by tool name (case-insensitive substring or match).
	 * Thread-safe.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentFramework|Telemetry")
	static TArray<FAgentFrameworkToolMetrics> GetToolTelemetry(const FString& ToolNameFilter = TEXT(""));

	/**
	 * Retrieve recent error records from the ring buffer, up to MaxCount, optionally filtered by ToolNameFilter.
	 * Thread-safe.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentFramework|Telemetry")
	static TArray<FAgentFrameworkErrorRecord> GetRecentErrors(int32 MaxCount = 50, const FString& ToolNameFilter = TEXT(""));

	/**
	 * Export telemetry metrics and recent error records formatted as a JSON string.
	 * Thread-safe.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentFramework|Telemetry")
	static FString GetTelemetryMetricsJson();

	/**
	 * Clear all telemetry records and error memory buffer.
	 * Thread-safe.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentFramework|Telemetry")
	static void ClearTelemetryData();

	/**
	 * Fully loads a package if needed and saves it safely using UPackage::SavePackage, preventing engine assertion crashes.
	 */
	static bool SaveAssetPackage(UPackage* Package, UObject* Asset, const FString& PackageFileName);

	/**
	 * Normalizes a package path (e.g. "/Game/UI/WBP_HealthBar") to a full object path (e.g. "/Game/UI/WBP_HealthBar.WBP_HealthBar").
	 * If the path already contains an object suffix or is empty, it is returned as-is.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentFramework|Utils")
	static FString NormalizeAssetObjectPath(const FString& InPath);

	/**
	 * Splits either a package path ("/Game/UI/WBP_HealthBar") or a full object path
	 * ("/Game/UI/WBP_HealthBar.WBP_HealthBar") into its clean components.
	 *
	 * Agents send both formats interchangeably. FPackageName::GetShortName() does NOT strip
	 * an object suffix, so feeding its output into CreatePackage() produces a malformed
	 * package named "WBP_HealthBar.WBP_HealthBar" and a "short package name" warning.
	 * Always split through here before creating an asset.
	 *
	 * @param OutPackageName Full package name with no object suffix ("/Game/UI/WBP_HealthBar").
	 * @param OutPackagePath Containing directory ("/Game/UI").
	 * @param OutAssetName   Asset name only ("WBP_HealthBar").
	 */
	static void SplitAssetPath(const FString& InPath, FString& OutPackageName, FString& OutPackagePath, FString& OutAssetName);

	static bool IsValidCppIdentifier(const FString& Name);
	static bool FormatCppFloatLiteral(double Value, FString& OutLiteral);
	static bool IsWritableSourcePath(const FString& FilePath);
	static bool ResolveWritableModulePaths(
		const FString& RequestedModuleName,
		const FString& SubDirectory,
		FString& OutModuleName,
		FString& OutHeaderDir,
		FString& OutSourceDir,
		FString& OutIncludePrefix,
		FString& OutApiMacro,
		FString& OutError);
	static bool EnsureAssetDoesNotExist(const FString& AssetPath, FString& OutPackageName, FString& OutAssetName, FString& OutError);
	static bool SaveAssetChecked(UPackage* Package, UObject* Asset, bool bIsNewAsset, FString& OutError);
	static void CleanupFailedNewAsset(UPackage* Package, UObject* Asset);
	static void RaiseToolError(const FString& ToolName, const FString& ErrorMessage);
	static class UAsyncFunctionResultString* RunMonitoredProcessAsync(
		const FString& ToolName,
		const FString& Executable,
		const FString& Params,
		float TimeoutSeconds,
		TFunction<void(int32 ExitCode, const FString& Output, FString& OutValue, FString& OutError)> OnComplete);

	struct FScopedUFunctionFrame
	{
		UFunction* Function = nullptr;
		uint8* Buffer = nullptr;

		FScopedUFunctionFrame(UFunction* InFunction, uint8* InAllocatedBuffer)
			: Function(InFunction)
			, Buffer(InAllocatedBuffer)
		{
			if (Function && Buffer && Function->PropertiesSize > 0)
			{
				FMemory::Memzero(Buffer, Function->PropertiesSize);
				for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					It->InitializeValue_InContainer(Buffer);
				}
			}
		}

		~FScopedUFunctionFrame()
		{
			if (Function && Buffer)
			{
				for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					It->DestroyValue_InContainer(Buffer);
				}
			}
		}

		FScopedUFunctionFrame(const FScopedUFunctionFrame&) = delete;
		FScopedUFunctionFrame& operator=(const FScopedUFunctionFrame&) = delete;
	};
};
