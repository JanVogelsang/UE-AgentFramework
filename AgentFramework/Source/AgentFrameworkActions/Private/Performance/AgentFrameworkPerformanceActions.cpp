// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Performance/AgentFrameworkPerformanceActions.h"
#include "AgentFrameworkCoreModule.h"
#include "AgentFrameworkActionUtils.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "Engine/RendererSettings.h"
#include "Scalability.h"
#include "GameFramework/GameUserSettings.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/UnrealType.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"

#if WITH_EDITOR
#include "Sound/SoundBase.h"
#endif

FAgentFrameworkPerformanceActions::FAgentFrameworkPerformanceActions() {}
FAgentFrameworkPerformanceActions::~FAgentFrameworkPerformanceActions() {}
FName FAgentFrameworkPerformanceActions::GetActionName() const { return FName(TEXT("Performance")); }

TArray<FString> FAgentFrameworkPerformanceActions::GetSupportedToolNames() const
{
	return {
		// Original tools
		TEXT("get_memory_stats"),
		TEXT("get_performance_stats"),
		TEXT("run_stat_command"),
		TEXT("analyze_asset_sizes"),
		// New CVar tools
		TEXT("get_cvar"),
		TEXT("set_cvar"),
		TEXT("discover_cvars"),
		TEXT("execute_console_command"),
		// Profiling tools
		TEXT("start_csv_profiler"),
		TEXT("stop_csv_profiler"),
		TEXT("read_profiling_file"),
		// Scalability tools
		TEXT("get_scalability_settings"),
		TEXT("set_scalability_settings"),
		// Renderer settings tools
		TEXT("get_renderer_settings"),
		TEXT("set_renderer_setting"),
		// Lumen & HLOD
		TEXT("adjust_lumen_settings"),
		TEXT("configure_hlod_setup")
	};
}

bool FAgentFrameworkPerformanceActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const { return true; }

// ---------------------------------------------------------------------------
// Helper: get the editor world safely
// ---------------------------------------------------------------------------
static UWorld* GetEditorWorld()
{
	if (GEditor && IsValid(GEditor))
	{
		return GEditor->GetEditorWorldContext().World();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Helper: get Saved/Profiling directory
// ---------------------------------------------------------------------------
static FString GetProfilingDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Profiling"));
}

// ---------------------------------------------------------------------------
// ExecuteAction — dispatcher
// ---------------------------------------------------------------------------
FAgentFrameworkActionResult FAgentFrameworkPerformanceActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAgentFrameworkActionResult Result;
	Result.bSuccess = false;

	FString ToolName;
	TArray<FString> IgnoreErrors;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, IgnoreErrors, false) || ToolName.IsEmpty())
	{
		// Legacy compat: read 'tool_name' (without underscore) or 'action' field
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("tool_name"), ToolName, IgnoreErrors, false) || ToolName.IsEmpty())
		{
			UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("action"), ToolName, IgnoreErrors, false);
		}
	}

	// -----------------------------------------------------------------------
	// LEGACY: map old action values -> new tool names
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("memory_stats"))   ToolName = TEXT("get_memory_stats");
	if (ToolName == TEXT("stat_command"))   ToolName = TEXT("run_stat_command");
	if (ToolName == TEXT("fps"))            ToolName = TEXT("get_performance_stats");

	// -----------------------------------------------------------------------
	// 1. get_performance_stats — FPS, frame time
	// -----------------------------------------------------------------------
	if (ToolName == TEXT("get_performance_stats"))
	{
		float DeltaTime = FApp::GetDeltaTime();
		float FPS = (DeltaTime > 0.0f) ? (1.0f / DeltaTime) : 0.0f;

		FString Report;
		Report += FString::Printf(TEXT("FPS: %.1f\n"), FPS);
		Report += FString::Printf(TEXT("Frame Time: %.2f ms\n"), DeltaTime * 1000.0f);

		if (GEngine && IsValid(GEngine))
		{
			Report += FString::Printf(TEXT("Fixed Tick Rate: %.1f\n"), GEngine->GetMaxFPS());
		}

		Result.bSuccess = true;
		Result.ResultMessage = Report;
	}

	// -----------------------------------------------------------------------
	// 2. get_memory_stats — platform memory
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("get_memory_stats"))
	{
		FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
		FString Report;
		Report += FString::Printf(TEXT("Physical Memory Used:      %.2f MB\n"), MemStats.UsedPhysical / (1024.0 * 1024.0));
		Report += FString::Printf(TEXT("Physical Memory Available: %.2f MB\n"), MemStats.AvailablePhysical / (1024.0 * 1024.0));
		Report += FString::Printf(TEXT("Virtual Memory Used:       %.2f MB\n"), MemStats.UsedVirtual / (1024.0 * 1024.0));
		Report += FString::Printf(TEXT("Peak Physical Used:        %.2f MB\n"), MemStats.PeakUsedPhysical / (1024.0 * 1024.0));

		Result.bSuccess = true;
		Result.ResultMessage = Report;
	}

	// -----------------------------------------------------------------------
	// 3. run_stat_command — toggle a stat overlay (stat fps, stat unit, etc.)
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("run_stat_command"))
	{
		FString Command;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("command"), Command, Result.Errors, true))
		{
			return Result;
		}

		UWorld* World = GetEditorWorld();
		if (GEngine && IsValid(GEngine) && IsValid(World))
		{
			GEngine->Exec(World, *FString::Printf(TEXT("stat %s"), *Command));
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("Executed: stat %s"), *Command);
		}
		else
		{
			Result.Errors.Add(TEXT("GEngine or editor world not available."));
		}
	}

	// -----------------------------------------------------------------------
	// 4. analyze_asset_sizes — iterate asset registry, report largest assets
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("analyze_asset_sizes"))
	{
		FString FilterPath;
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("path_filter"), FilterPath, Result.Errors, false);

		int32 TopN = 20;
		UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("top_n"), TopN, Result.Errors, false);

		FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AR = ARModule.Get();

		TArray<FAssetData> AllAssets;
		AR.GetAllAssets(AllAssets, true);

		// Filter by path if provided
		if (!FilterPath.IsEmpty())
		{
			AllAssets = AllAssets.FilterByPredicate([&FilterPath](const FAssetData& A)
			{
				return A.PackagePath.ToString().StartsWith(FilterPath);
			});
		}

		// Gather disk sizes
		TArray<TPair<int64, FString>> SizeList;
		for (const FAssetData& A : AllAssets)
		{
			FString PackageFilePath;
			if (FPackageName::TryConvertLongPackageNameToFilename(A.PackageName.ToString(), PackageFilePath, TEXT(".uasset")))
			{
				int64 FileSize = IFileManager::Get().FileSize(*PackageFilePath);
				if (FileSize > 0)
				{
					SizeList.Add(TPair<int64, FString>(FileSize, A.GetObjectPathString()));
				}
			}
		}

		// Sort descending
		SizeList.Sort([](const TPair<int64, FString>& A, const TPair<int64, FString>& B)
		{
			return A.Key > B.Key;
		});

		FString Report = FString::Printf(TEXT("Top %d assets by disk size:\n"), TopN);
		int32 Shown = 0;
		for (const TPair<int64, FString>& Entry : SizeList)
		{
			if (Shown >= TopN) break;
			Report += FString::Printf(TEXT("  %.2f MB  %s\n"), Entry.Key / (1024.0 * 1024.0), *Entry.Value);
			Shown++;
		}
		Report += FString::Printf(TEXT("\nTotal assets scanned: %d\n"), SizeList.Num());

		Result.bSuccess = true;
		Result.ResultMessage = Report;
	}

	// -----------------------------------------------------------------------
	// 5. get_cvar — read a single console variable
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("get_cvar"))
	{
		FString CVarName;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("name"), CVarName, Result.Errors, true))
		{
			return Result;
		}

		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
		if (!CVar)
		{
			Result.Errors.Add(FString::Printf(TEXT("CVar not found: %s"), *CVarName));
			return Result;
		}

		FString Report;
		Report += FString::Printf(TEXT("Name:  %s\n"), *CVarName);
		Report += FString::Printf(TEXT("Value: %s\n"), *CVar->GetString());
		Report += FString::Printf(TEXT("Help:  %s\n"), CVar->GetHelp() ? CVar->GetHelp() : TEXT("(no description)"));

		Result.bSuccess = true;
		Result.ResultMessage = Report;
	}

	// -----------------------------------------------------------------------
	// 6. set_cvar — set a console variable (transient, not saved to .ini)
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("set_cvar"))
	{
		FString CVarName, CVarValue;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("name"), CVarName, Result.Errors, true) ||
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("value"), CVarValue, Result.Errors, true))
		{
			return Result;
		}

		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*CVarName);
		if (!CVar)
		{
			Result.Errors.Add(FString::Printf(TEXT("CVar not found: %s"), *CVarName));
			return Result;
		}

		FString OldValue = CVar->GetString();
		CVar->Set(*CVarValue, ECVF_SetByConsole);

		UE_LOG(LogAgentFramework, Log, TEXT("PerformanceActions: CVar %s: %s -> %s"), *CVarName, *OldValue, *CVarValue);

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(
			TEXT("Set %s = %s  (was: %s)\nNOTE: This is transient — use set_renderer_setting or write_config_value to make it persistent."),
			*CVarName, *CVarValue, *OldValue);
	}

	// -----------------------------------------------------------------------
	// 7. discover_cvars — enumerate CVars by prefix
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("discover_cvars"))
	{
		FString Prefix = TEXT("r.");
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("prefix"), Prefix, Result.Errors, false);

		int32 MaxResults = 50;
		UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("max_results"), MaxResults, Result.Errors, false);

		TArray<TPair<FString, FString>> Found;

		IConsoleManager::Get().ForEachConsoleObjectThatStartsWith(
			FConsoleObjectVisitor::CreateLambda([&Found, MaxResults](const TCHAR* Name, IConsoleObject* Obj)
			{
				if (Found.Num() >= MaxResults) return;

				IConsoleVariable* Var = Obj->AsVariable();
				if (Var)
				{
					FString Val = Var->GetString();
					Found.Add(TPair<FString, FString>(FString(Name), Val));
				}
			}),
			*Prefix
		);

		FString Report = FString::Printf(TEXT("CVars matching prefix '%s' (%d found, showing up to %d):\n\n"),
			*Prefix, Found.Num(), MaxResults);

		for (const TPair<FString, FString>& Entry : Found)
		{
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Entry.Key);
			const TCHAR* HelpText = Var ? Var->GetHelp() : nullptr;
			Report += FString::Printf(TEXT("  %s = %s\n"), *Entry.Key, *Entry.Value);
			if (HelpText && FCString::Strlen(HelpText) > 0)
			{
				Report += FString::Printf(TEXT("    -> %s\n"), HelpText);
			}
		}

		Result.bSuccess = true;
		Result.ResultMessage = Report;
	}

	// -----------------------------------------------------------------------
	// 8. execute_console_command — run any GEngine->Exec command
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("execute_console_command"))
	{
		FString Command;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("command"), Command, Result.Errors, true))
		{
			return Result;
		}

		UWorld* World = nullptr;
		int32 WorldContextIndex = 0;
		Params->TryGetNumberField(TEXT("world_context_index"), WorldContextIndex);

		if (GEditor && GEditor->PlayWorld)
		{
			FWorldContext* PIEContext = GEditor->GetPIEWorldContext(WorldContextIndex);
			if (PIEContext && PIEContext->World())
			{
				World = PIEContext->World();
			}
			else
			{
				World = GEditor->GetCurrentPlayWorld();
			}
		}

		if (!World)
		{
			World = GetEditorWorld();
		}

		if (!GEngine || !IsValid(GEngine))
		{
			Result.Errors.Add(TEXT("GEngine not available."));
			return Result;
		}

		bool bExecuted = false;
		if (World)
		{
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				PC->ConsoleCommand(Command, true);
				bExecuted = true;
			}
			else
			{
				bExecuted = GEngine->Exec(World, *Command);
			}
		}
		else
		{
			bExecuted = GEngine->Exec(nullptr, *Command);
		}

		UE_LOG(LogAgentFramework, Log, TEXT("PerformanceActions: Executed console command: %s (result=%d, world=%s)"),
			*Command, (int)bExecuted, World ? *World->GetName() : TEXT("None"));

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Executed: %s"), *Command);
	}

	// -----------------------------------------------------------------------
	// 9. start_csv_profiler
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("start_csv_profiler"))
	{
		UWorld* World = GetEditorWorld();
		if (GEngine && IsValid(GEngine))
		{
			GEngine->Exec(World, TEXT("csvprofile start"));
			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(
				TEXT("CSV profiler started. Output will be written to: %s\nUse stop_csv_profiler when done, then read_profiling_file to analyze."),
				*GetProfilingDir());
		}
		else
		{
			Result.Errors.Add(TEXT("GEngine not available."));
		}
	}

	// -----------------------------------------------------------------------
	// 10. stop_csv_profiler — stop and list generated files
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("stop_csv_profiler"))
	{
		UWorld* World = GetEditorWorld();
		if (GEngine && IsValid(GEngine))
		{
			GEngine->Exec(World, TEXT("csvprofile stop"));

			// List CSV files in profiling dir
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(GetProfilingDir() / TEXT("*.csv")), true, false);
			Files.Sort();

			FString Report = TEXT("CSV profiler stopped.\n\nAvailable profiling files:\n");
			for (const FString& F : Files)
			{
				Report += FString::Printf(TEXT("  %s\n"), *F);
			}
			if (Files.Num() == 0)
			{
				Report += TEXT("  (none found — profiling may not have produced output yet)\n");
			}

			Result.bSuccess = true;
			Result.ResultMessage = Report;
		}
		else
		{
			Result.Errors.Add(TEXT("GEngine not available."));
		}
	}

	// -----------------------------------------------------------------------
	// 11. read_profiling_file — read a CSV/log from Saved/Profiling
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("read_profiling_file"))
	{
		FString FileName;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("file_name"), FileName, IgnoreErrors, false) || FileName.IsEmpty())
		{
			// List available files so the AI knows what to pick
			TArray<FString> CSVFiles, LogFiles;
			IFileManager::Get().FindFiles(CSVFiles, *(GetProfilingDir() / TEXT("*.csv")), true, false);
			IFileManager::Get().FindFiles(LogFiles, *(GetProfilingDir() / TEXT("*.log")), true, false);
			CSVFiles.Sort(); LogFiles.Sort();

			FString Report = FString::Printf(TEXT("Profiling directory: %s\n\nCSV files:\n"), *GetProfilingDir());
			for (const FString& F : CSVFiles) Report += FString::Printf(TEXT("  %s\n"), *F);
			Report += TEXT("\nLog files:\n");
			for (const FString& F : LogFiles) Report += FString::Printf(TEXT("  %s\n"), *F);
			if (CSVFiles.Num() == 0 && LogFiles.Num() == 0) Report += TEXT("  (none)\n");

			Result.bSuccess = true;
			Result.ResultMessage = Report;
		}
		else
		{
			// Absolute path or relative to profiling dir
			FString FullPath = FPaths::IsRelative(FileName) ? (GetProfilingDir() / FileName) : FileName;

			FString FileContent;
			if (!FFileHelper::LoadFileToString(FileContent, *FullPath))
			{
				Result.Errors.Add(FString::Printf(TEXT("Could not read file: %s"), *FullPath));
				return Result;
			}

			// Optionally truncate very large files to first N lines
			int32 MaxLines = 200;
			UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("max_lines"), MaxLines, Result.Errors, false);

			TArray<FString> Lines;
			FileContent.ParseIntoArrayLines(Lines, false);
			FString Truncated;
			int32 Count = FMath::Min(MaxLines, Lines.Num());
			for (int32 i = 0; i < Count; i++)
			{
				Truncated += Lines[i] + TEXT("\n");
			}
			if (Lines.Num() > MaxLines)
			{
				Truncated += FString::Printf(TEXT("... (%d lines total, showing first %d)\n"), Lines.Num(), MaxLines);
			}

			Result.bSuccess = true;
			Result.ResultMessage = FString::Printf(TEXT("File: %s\n\n%s"), *FullPath, *Truncated);
		}
	}

	// -----------------------------------------------------------------------
	// 12. get_scalability_settings — read quality levels
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("get_scalability_settings"))
	{
		Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();

		FString Report = TEXT("Current Scalability / Quality Settings:\n\n");
		Report += FString::Printf(TEXT("  ResolutionQuality:        %.0f%%\n"), Levels.ResolutionQuality);
		Report += FString::Printf(TEXT("  ViewDistanceQuality:      %d  (0=Low, 1=Med, 2=High, 3=Epic)\n"), Levels.ViewDistanceQuality);
		Report += FString::Printf(TEXT("  AntiAliasingQuality:      %d\n"), Levels.AntiAliasingQuality);
		Report += FString::Printf(TEXT("  ShadowQuality:            %d\n"), Levels.ShadowQuality);
		Report += FString::Printf(TEXT("  GlobalIlluminationQuality:%d\n"), Levels.GlobalIlluminationQuality);
		Report += FString::Printf(TEXT("  ReflectionQuality:        %d\n"), Levels.ReflectionQuality);
		Report += FString::Printf(TEXT("  PostProcessQuality:       %d\n"), Levels.PostProcessQuality);
		Report += FString::Printf(TEXT("  TextureQuality:           %d\n"), Levels.TextureQuality);
		Report += FString::Printf(TEXT("  EffectsQuality:           %d\n"), Levels.EffectsQuality);
		Report += FString::Printf(TEXT("  FoliageQuality:           %d\n"), Levels.FoliageQuality);
		Report += FString::Printf(TEXT("  ShadingQuality:           %d\n"), Levels.ShadingQuality);

		Result.bSuccess = true;
		Result.ResultMessage = Report;
	}

	// -----------------------------------------------------------------------
	// 13. set_scalability_settings — apply quality level overrides
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("set_scalability_settings"))
	{
		Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();

		float ResQuality = Levels.ResolutionQuality;
		if (UAgentFrameworkActionUtils::TryGetFloatParam(Params, TEXT("resolution_quality"), ResQuality, Result.Errors, false))
		{
			Levels.ResolutionQuality = ResQuality;
		}

		int32 ViewDistQuality = Levels.ViewDistanceQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("view_distance_quality"), ViewDistQuality, Result.Errors, false))
		{
			Levels.ViewDistanceQuality = ViewDistQuality;
		}

		int32 AAQuality = Levels.AntiAliasingQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("anti_aliasing_quality"), AAQuality, Result.Errors, false))
		{
			Levels.AntiAliasingQuality = AAQuality;
		}

		int32 ShadowQuality = Levels.ShadowQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("shadow_quality"), ShadowQuality, Result.Errors, false))
		{
			Levels.ShadowQuality = ShadowQuality;
		}

		int32 GIQuality = Levels.GlobalIlluminationQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("global_illumination_quality"), GIQuality, Result.Errors, false))
		{
			Levels.GlobalIlluminationQuality = GIQuality;
		}

		int32 ReflectionQuality = Levels.ReflectionQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("reflection_quality"), ReflectionQuality, Result.Errors, false))
		{
			Levels.ReflectionQuality = ReflectionQuality;
		}

		int32 PPQuality = Levels.PostProcessQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("post_process_quality"), PPQuality, Result.Errors, false))
		{
			Levels.PostProcessQuality = PPQuality;
		}

		int32 TexQuality = Levels.TextureQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("texture_quality"), TexQuality, Result.Errors, false))
		{
			Levels.TextureQuality = TexQuality;
		}

		int32 FxQuality = Levels.EffectsQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("effects_quality"), FxQuality, Result.Errors, false))
		{
			Levels.EffectsQuality = FxQuality;
		}

		int32 FoliageQuality = Levels.FoliageQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("foliage_quality"), FoliageQuality, Result.Errors, false))
		{
			Levels.FoliageQuality = FoliageQuality;
		}

		int32 ShadingQuality = Levels.ShadingQuality;
		if (UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("shading_quality"), ShadingQuality, Result.Errors, false))
		{
			Levels.ShadingQuality = ShadingQuality;
		}

		Scalability::SetQualityLevels(Levels);

		// Optionally save to GameUserSettings
		bool bSave = false;
		UAgentFrameworkActionUtils::TryGetBoolParam(Params, TEXT("save_to_ini"), bSave, Result.Errors, false);
		if (bSave)
		{
			UGameUserSettings* UserSettings = (GEngine && IsValid(GEngine)) ? GEngine->GetGameUserSettings() : nullptr;
			if (IsValid(UserSettings))
			{
				UserSettings->SetVisualEffectQuality(Levels.EffectsQuality);
				UserSettings->SetShadowQuality(Levels.ShadowQuality);
				UserSettings->SetAntiAliasingQuality(Levels.AntiAliasingQuality);
				UserSettings->SetTextureQuality(Levels.TextureQuality);
				UserSettings->SetPostProcessingQuality(Levels.PostProcessQuality);
				UserSettings->SetFoliageQuality(Levels.FoliageQuality);
				UserSettings->SetShadingQuality(Levels.ShadingQuality);
				UserSettings->ApplySettings(false);
				UserSettings->SaveSettings();
			}
		}

		UE_LOG(LogAgentFramework, Log, TEXT("PerformanceActions: Scalability levels updated (save=%d)"), (int)bSave);

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(
			TEXT("Scalability settings applied%s.\nUse get_scalability_settings to verify."),
			bSave ? TEXT(" and saved to GameUserSettings.ini") : TEXT(" (transient — set save_to_ini=true to persist)"));
	}

	// -----------------------------------------------------------------------
	// 14. get_renderer_settings — read URendererSettings properties
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("get_renderer_settings"))
	{
		const URendererSettings* RendSettings = GetDefault<URendererSettings>();
		if (!IsValid(RendSettings))
		{
			Result.Errors.Add(TEXT("Could not load URendererSettings."));
			return Result;
		}

		// Optional: filter by property name prefix
		FString Filter;
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("filter"), Filter, Result.Errors, false);

		UClass* Class = URendererSettings::StaticClass();
		FString Report = TEXT("URendererSettings (DefaultEngine.ini / [/Script/Engine.RendererSettings]):\n\n");

		for (TFieldIterator<FProperty> PropIt(Class); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			if (!(Prop->PropertyFlags & CPF_Config)) continue;

			FString PropName = Prop->GetName();
			if (!Filter.IsEmpty() && !PropName.Contains(Filter)) continue;

			FString ValueStr;
			const void* PropAddr = Prop->ContainerPtrToValuePtr<void>(RendSettings);
			Prop->ExportTextItem_Direct(ValueStr, PropAddr, nullptr, nullptr, PPF_None);

			Report += FString::Printf(TEXT("  %-50s = %s\n"), *PropName, *ValueStr);
		}

		Result.bSuccess = true;
		Result.ResultMessage = Report;
	}

	// -----------------------------------------------------------------------
	// 15. set_renderer_setting — set a URendererSettings property via reflection
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("set_renderer_setting"))
	{
		FString PropName, Value;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("property"), PropName, Result.Errors, true) ||
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("value"), Value, Result.Errors, true))
		{
			return Result;
		}

		URendererSettings* RendSettings = GetMutableDefault<URendererSettings>();
		if (!IsValid(RendSettings))
		{
			Result.Errors.Add(TEXT("Could not get mutable URendererSettings."));
			return Result;
		}

		UClass* Class = URendererSettings::StaticClass();
		FProperty* Prop = Class->FindPropertyByName(FName(*PropName));
		if (!Prop)
		{
			// Try case-insensitive search
			for (TFieldIterator<FProperty> PropIt(Class); PropIt; ++PropIt)
			{
				if ((*PropIt)->GetName().Equals(PropName, ESearchCase::IgnoreCase))
				{
					Prop = *PropIt;
					break;
				}
			}
		}

		if (!Prop)
		{
			Result.Errors.Add(FString::Printf(
				TEXT("Property '%s' not found on URendererSettings. Use get_renderer_settings to list all available properties."),
				*PropName));
			return Result;
		}

		// Apply via reflection
		RendSettings->Modify();
		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(RendSettings);
		Prop->ImportText_Direct(*Value, PropAddr, RendSettings, PPF_None);

		// Notify the editor UI and save to .ini
		FPropertyChangedEvent ChangedEvent(Prop);
		RendSettings->PostEditChangeProperty(ChangedEvent);
		RendSettings->SaveConfig();

		UE_LOG(LogAgentFramework, Log, TEXT("PerformanceActions: URendererSettings.%s = %s (saved to DefaultEngine.ini)"),
			*PropName, *Value);

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(
			TEXT("URendererSettings.%s = %s\nSaved to DefaultEngine.ini. Editor may require restart for some settings to take effect."),
			*PropName, *Value);
		Result.ModifiedPaths.Add(TEXT("Config/DefaultEngine.ini: [/Script/Engine.RendererSettings]"));
	}

	// -----------------------------------------------------------------------
	// 16. adjust_lumen_settings
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("adjust_lumen_settings"))
	{
		double GIQuality = 1.0;
		double ReflectionQuality = 1.0;
		UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("gi_quality"), GIQuality, Result.Errors, false);
		UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("reflection_quality"), ReflectionQuality, Result.Errors, false);

		UWorld* World = GetEditorWorld();
		if (!IsValid(World))
		{
			Result.Errors.Add(TEXT("No active editor world found."));
			return Result;
		}

		APostProcessVolume* PPVolume = nullptr;
		for (TActorIterator<APostProcessVolume> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				PPVolume = *It;
				break;
			}
		}

		if (!IsValid(PPVolume))
		{
			PPVolume = World->SpawnActor<APostProcessVolume>();
			if (IsValid(PPVolume))
			{
				PPVolume->SetActorLabel(TEXT("LumenPPVolume"));
				PPVolume->bUnbound = true;
			}
			else
			{
				Result.Errors.Add(TEXT("Failed to spawn APostProcessVolume."));
				return Result;
			}
		}

		PPVolume->Modify();
		FPostProcessSettings& Settings = PPVolume->Settings;
		Settings.bOverride_DynamicGlobalIlluminationMethod = true;
		Settings.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
		Settings.bOverride_ReflectionMethod = true;
		Settings.ReflectionMethod = EReflectionMethod::Lumen;
		Settings.bOverride_LumenSceneLightingQuality = true;
		Settings.LumenSceneLightingQuality = static_cast<float>(GIQuality);
		Settings.bOverride_LumenSceneDetail = true;
		Settings.LumenSceneDetail = static_cast<float>(ReflectionQuality);

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Adjusted Lumen settings on PostProcessVolume '%s'."), *PPVolume->GetName());
	}

	// -----------------------------------------------------------------------
	// 17. configure_hlod_setup
	// -----------------------------------------------------------------------
	else if (ToolName == TEXT("configure_hlod_setup"))
	{
		int32 NumHLODLevels = 3;
		UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("num_hlod_levels"), NumHLODLevels, Result.Errors, false);

		UWorld* World = GetEditorWorld();
		if (!IsValid(World))
		{
			Result.Errors.Add(TEXT("No active editor world found."));
			return Result;
		}

		AWorldSettings* WorldSettings = World->GetWorldSettings();
		if (!IsValid(WorldSettings))
		{
			Result.Errors.Add(TEXT("Failed to retrieve WorldSettings."));
			return Result;
		}

		WorldSettings->Modify();
		WorldSettings->bGenerateSingleClusterForLevel = true;
		WorldSettings->NumHLODLevels = NumHLODLevels;

		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Configured HLOD setup: bGenerateSingleClusterForLevel=true, NumHLODLevels=%d."), NumHLODLevels);
	}

	// -----------------------------------------------------------------------
	// Unknown tool
	// -----------------------------------------------------------------------
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("Unknown performance tool: '%s'"), *ToolName));
	}

	if (Result.bSuccess)
	{
		PlaySuccessSound();
	}

	return Result;
}

void FAgentFrameworkPerformanceActions::PlaySuccessSound()
{
#if WITH_EDITOR
	if (GEditor && IsValid(GEditor))
	{
		USoundBase* SuccessSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileSuccess.CompileSuccess"));
		if (IsValid(SuccessSound))
		{
			GEditor->PlayEditorSound(SuccessSound);
		}
	}
#endif
}
