// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Toolsets/AgentFrameworkBuildToolset.h"
#include "AgentFrameworkActionUtils.h"
#include "AsyncFunctionResult/AsyncFunctionResultString.h"
#include "AsyncFunctionResult/AsyncFunctionResultUtils.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "LightingBuildOptions.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/Paths.h"

namespace AgentFrameworkBuildToolsetInternal
{
	static bool ParseLightingQuality(const FString& InQuality, ELightingBuildQuality& OutQuality, FString& OutCanonicalName)
	{
		const FString Trimmed = InQuality.TrimStartAndEnd();
		if (Trimmed.Equals(TEXT("Preview"), ESearchCase::IgnoreCase))
		{
			OutQuality = Quality_Preview;
			OutCanonicalName = TEXT("Preview");
			return true;
		}
		if (Trimmed.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
		{
			OutQuality = Quality_Medium;
			OutCanonicalName = TEXT("Medium");
			return true;
		}
		if (Trimmed.Equals(TEXT("High"), ESearchCase::IgnoreCase))
		{
			OutQuality = Quality_High;
			OutCanonicalName = TEXT("High");
			return true;
		}
		if (Trimmed.Equals(TEXT("Production"), ESearchCase::IgnoreCase))
		{
			OutQuality = Quality_Production;
			OutCanonicalName = TEXT("Production");
			return true;
		}
		return false;
	}

	static bool NormalizeTargetPlatform(const FString& InPlatform, FString& OutCanonical)
	{
		const FString Trimmed = InPlatform.TrimStartAndEnd();
		static const TMap<FString, FString> ValidPlatforms = {
			{ TEXT("win64"), TEXT("Win64") },
			{ TEXT("linux"), TEXT("Linux") },
			{ TEXT("linuxarm64"), TEXT("LinuxArm64") },
			{ TEXT("mac"), TEXT("Mac") },
			{ TEXT("android"), TEXT("Android") },
			{ TEXT("ios"), TEXT("IOS") }
		};
		if (const FString* Found = ValidPlatforms.Find(Trimmed.ToLower()))
		{
			OutCanonical = *Found;
			return true;
		}
		return false;
	}

	static bool NormalizeBuildConfiguration(const FString& InConfig, FString& OutCanonical)
	{
		const FString Trimmed = InConfig.TrimStartAndEnd();
		static const TMap<FString, FString> ValidConfigs = {
			{ TEXT("debug"), TEXT("Debug") },
			{ TEXT("debuggame"), TEXT("DebugGame") },
			{ TEXT("development"), TEXT("Development") },
			{ TEXT("shipping"), TEXT("Shipping") },
			{ TEXT("test"), TEXT("Test") }
		};
		if (const FString* Found = ValidConfigs.Find(Trimmed.ToLower()))
		{
			OutCanonical = *Found;
			return true;
		}
		return false;
	}

	static bool HasUnsafeCommandCharacters(const FString& PathStr)
	{
		if (PathStr.Contains(TEXT("..")))
		{
			return true;
		}
		for (TCHAR Ch : PathStr)
		{
			if (Ch == TEXT('"') || Ch == TEXT(';') || Ch == TEXT('|') || Ch == TEXT('&')
				|| Ch == TEXT('$') || Ch == TEXT('`') || Ch == TEXT('<') || Ch == TEXT('>')
				|| Ch == TEXT('\r') || Ch == TEXT('\n') || Ch == TEXT('\0'))
			{
				return true;
			}
		}
		return false;
	}
}

FAgentFrameworkBuildLightingResult UAgentFrameworkBuildToolset::BuildLighting(const FString& Quality)
{
	FAgentFrameworkBuildLightingResult Result;

	if (!GEditor)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("BuildLighting"), TEXT("Unreal Editor instance is not available."));
		return Result;
	}

	if (GEditor->IsPlayingSessionInEditor())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("BuildLighting"), TEXT("Cannot build lighting while a Play-In-Editor session is active."));
		return Result;
	}

	if (GEditor->IsLightingBuildCurrentlyRunning())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("BuildLighting"), TEXT("A lighting build is already in progress."));
		return Result;
	}

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	if (!EditorWorld)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("BuildLighting"), TEXT("No active editor world is open."));
		return Result;
	}

	if (EditorWorld->GetWorldSettings() && EditorWorld->GetWorldSettings()->bForceNoPrecomputedLighting)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("BuildLighting"), TEXT("Current level WorldSettings has bForceNoPrecomputedLighting enabled."));
		return Result;
	}

	ELightingBuildQuality ParsedQuality = Quality_Preview;
	FString CanonicalQuality;
	if (!AgentFrameworkBuildToolsetInternal::ParseLightingQuality(Quality, ParsedQuality, CanonicalQuality))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("BuildLighting"),
			FString::Printf(TEXT("Invalid Quality '%s'. Expected one of: Preview, Medium, High, Production."), *Quality));
		return Result;
	}

	FLightingBuildOptions LightingOptions;
	LightingOptions.QualityLevel = ParsedQuality;
	GEditor->BuildLighting(LightingOptions);

	Result.LevelName = EditorWorld->GetMapName();
	Result.Quality = CanonicalQuality;
	Result.bBuildStarted = true;
	Result.Message = FString::Printf(TEXT("Started %s lighting build for level '%s'."), *CanonicalQuality, *Result.LevelName);

	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("BuildLighting"), 0.0, true, {});
	return Result;
}

UAsyncFunctionResultString* UAgentFrameworkBuildToolset::PackageProject(
	const FString& TargetPlatform,
	const FString& Configuration,
	const FString& OutputDirectory,
	bool bCook,
	bool bArchive,
	int32 TimeoutSeconds)
{
	if (GEditor && GEditor->IsPlayingSessionInEditor())
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			TEXT("Cannot package project while a Play-In-Editor session is active."));
	}

	if (TimeoutSeconds <= 0)
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("TimeoutSeconds must be > 0 (got %d)."), TimeoutSeconds));
	}

	FString CanonicalPlatform;
	if (!AgentFrameworkBuildToolsetInternal::NormalizeTargetPlatform(TargetPlatform, CanonicalPlatform))
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("Invalid TargetPlatform '%s'. Supported: Win64, Linux, LinuxArm64, Mac, Android, IOS."), *TargetPlatform));
	}

	FString CanonicalConfig;
	if (!AgentFrameworkBuildToolsetInternal::NormalizeBuildConfiguration(Configuration, CanonicalConfig))
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("Invalid Configuration '%s'. Supported: Debug, DebugGame, Development, Shipping, Test."), *Configuration));
	}

	if (AgentFrameworkBuildToolsetInternal::HasUnsafeCommandCharacters(OutputDirectory))
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("Unsafe characters or traversal in OutputDirectory '%s'."), *OutputDirectory));
	}

	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	if (ProjectFile.IsEmpty() || !FPaths::FileExists(ProjectFile))
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("Valid .uproject file not found (resolved '%s')."), *ProjectFile));
	}

	FString ResolvedOutputDir = OutputDirectory.TrimStartAndEnd();
	if (ResolvedOutputDir.IsEmpty())
	{
		ResolvedOutputDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("StagedBuilds"));
	}
	else
	{
		ResolvedOutputDir = FPaths::ConvertRelativePathToFull(ResolvedOutputDir);
	}
	FPaths::NormalizeDirectoryName(ResolvedOutputDir);

	if (bArchive && !IFileManager::Get().MakeDirectory(*ResolvedOutputDir, true))
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("Failed to create output directory '%s'."), *ResolvedOutputDir));
	}

	const FString UatPath = FSerializedUATProcess::GetUATPath();
	if (UatPath.IsEmpty() || !FPaths::FileExists(UatPath))
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("RunUAT script not found at '%s'."), *UatPath));
	}

	const bool bHasCppSource = FPaths::DirectoryExists(FPaths::GameSourceDir());
	const FString BuildFlag = bHasCppSource ? TEXT("-build ") : TEXT("");
	const FString CookFlag = bCook ? TEXT("-cook ") : TEXT("-skipcook ");
	const FString ArchiveFlags = bArchive
		? FString::Printf(TEXT("-archive -archivedirectory=\"%s\" "), *ResolvedOutputDir)
		: TEXT("");

	const FString UatArgs = FString::Printf(
		TEXT("BuildCookRun -project=\"%s\" -noP4 -platform=%s -clientconfig=%s -serverconfig=%s %s%s-stage -pak %s-utf8output -unattended -NoMutex"),
		*ProjectFile,
		*CanonicalPlatform,
		*CanonicalConfig,
		*CanonicalConfig,
		*BuildFlag,
		*CookFlag,
		*ArchiveFlags);

	FString Executable;
	FString Params;
#if PLATFORM_WINDOWS
	Executable = TEXT("cmd.exe");
	Params = FString::Printf(TEXT("/c \"\"%s\" %s\""), *UatPath, *UatArgs);
#elif PLATFORM_MAC || PLATFORM_LINUX
	Executable = TEXT("/bin/bash");
	Params = FString::Printf(TEXT("\"%s\" %s"), *UatPath, *UatArgs);
#endif

	return UAgentFrameworkActionUtils::RunMonitoredProcessAsync(
		TEXT("PackageProject"),
		Executable,
		Params,
		static_cast<float>(TimeoutSeconds),
		[CanonicalPlatform, CanonicalConfig, ResolvedOutputDir, bArchive](int32 ExitCode, const FString& Output, FString& OutValue, FString& OutError)
		{
			if (ExitCode == 0)
			{
				const FString OutputNote = bArchive ? ResolvedOutputDir : TEXT("(not archived)");
				OutValue = FString::Printf(TEXT("Packaged project for %s (%s) to '%s'.\n%s"),
					*CanonicalPlatform, *CanonicalConfig, *OutputNote, *Output);
			}
			else
			{
				OutError = FString::Printf(TEXT("PackageProject failed for %s (%s) with exit code %d.\n%s"),
					*CanonicalPlatform, *CanonicalConfig, ExitCode, *Output);
			}
		});
}
