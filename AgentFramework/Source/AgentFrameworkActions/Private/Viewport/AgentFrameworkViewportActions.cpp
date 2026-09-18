// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Viewport/AgentFrameworkViewportActions.h"
#include "AgentFrameworkActionUtils.h"
#include "AgentFrameworkCoreModule.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "Selection.h"
#include "GameFramework/Actor.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "AgentFrameworkViewportActions"

// ============================================================================
// Lifecycle
// ============================================================================

FAgentFrameworkViewportActions::FAgentFrameworkViewportActions() {}
FAgentFrameworkViewportActions::~FAgentFrameworkViewportActions() {}

// ============================================================================
// IAgentFrameworkActionExecutor Interface
// ============================================================================

FName FAgentFrameworkViewportActions::GetActionName() const { return FName(TEXT("Viewport")); }

TArray<FString> FAgentFrameworkViewportActions::GetSupportedToolNames() const
{
	return {
		TEXT("capture_viewport"),
		TEXT("set_viewport_camera"),
		TEXT("set_viewport_view_mode"),
		TEXT("set_viewport_realtime"),
		TEXT("focus_viewport_on_selection")
	};
}

bool FAgentFrameworkViewportActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	FString ToolName;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, OutErrors, false);

	if (ToolName == TEXT("set_viewport_view_mode"))
	{
		FString ViewModeStr;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("view_mode"), ViewModeStr, OutErrors, true))
		{
			return false;
		}
	}
	else if (ToolName == TEXT("set_viewport_realtime"))
	{
		bool bDummy = false;
		if (!UAgentFrameworkActionUtils::TryGetBoolParam(Params, TEXT("realtime"), bDummy, OutErrors, true))
		{
			return false;
		}
	}

	return OutErrors.Num() == 0;
}

FAgentFrameworkActionResult FAgentFrameworkViewportActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAgentFrameworkActionResult Result;
	Result.bSuccess = false;

	if (!IsValid(GEditor))
	{
		Result.Errors.Add(TEXT("Editor not available."));
		return Result;
	}

	FString ToolName;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, Result.Errors, false);

	if (ToolName == TEXT("capture_viewport"))
	{
		return ExecuteCaptureViewport(Params, Result);
	}
	else if (ToolName == TEXT("set_viewport_camera"))
	{
		return ExecuteSetViewportCamera(Params, Result);
	}
	else if (ToolName == TEXT("set_viewport_view_mode"))
	{
		return ExecuteSetViewportViewMode(Params, Result);
	}
	else if (ToolName == TEXT("set_viewport_realtime"))
	{
		return ExecuteSetViewportRealtime(Params, Result);
	}
	else if (ToolName == TEXT("focus_viewport_on_selection"))
	{
		return ExecuteFocusViewportOnSelection(Params, Result);
	}
	else
	{
		// Legacy fallback if _tool_name is not provided or set to action name
		FString ActionStr;
		if (UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("action"), ActionStr, Result.Errors, false) && !ActionStr.IsEmpty())
		{
			if (ActionStr == TEXT("set_viewport_camera")) return ExecuteSetViewportCamera(Params, Result);
			if (ActionStr == TEXT("set_viewport_view_mode")) return ExecuteSetViewportViewMode(Params, Result);
			if (ActionStr == TEXT("set_viewport_realtime")) return ExecuteSetViewportRealtime(Params, Result);
			if (ActionStr == TEXT("focus_viewport_on_selection")) return ExecuteFocusViewportOnSelection(Params, Result);
		}
		// Default to capture_viewport if no tool_name or action specified
		return ExecuteCaptureViewport(Params, Result);
	}
}

TSharedPtr<SLevelViewport> FAgentFrameworkViewportActions::GetActiveLevelViewport(TArray<FString>& OutErrors) const
{
	if (!IsValid(GEditor))
	{
		OutErrors.Add(TEXT("Editor not available."));
		return nullptr;
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (!LevelEditor.IsValid())
	{
		OutErrors.Add(TEXT("No Level Editor is active."));
		return nullptr;
	}

	TSharedPtr<SLevelViewport> ActiveViewport = LevelEditor->GetActiveViewportInterface();
	if (!ActiveViewport.IsValid())
	{
		OutErrors.Add(TEXT("No active viewport found in the Level Editor."));
		return nullptr;
	}

	return ActiveViewport;
}

// ============================================================================
// capture_viewport
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkViewportActions::ExecuteCaptureViewport(
	const TSharedRef<FJsonObject>& Params,
	FAgentFrameworkActionResult& Result)
{
	int32 MaxDimension = 512;
	UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("max_dimension"), MaxDimension, Result.Errors, false);
	MaxDimension = FMath::Clamp(MaxDimension, 256, 1024);

	int32 ViewportIndex = 0;
	UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("viewport_index"), ViewportIndex, Result.Errors, false);

	FViewport* Viewport = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		UWorld* PlayWorld = GEditor->GetCurrentPlayWorld();
		if (!PlayWorld)
		{
			PlayWorld = GEditor->PlayWorld;
		}
		if (PlayWorld && PlayWorld->GetGameViewport() && PlayWorld->GetGameViewport()->Viewport)
		{
			Viewport = PlayWorld->GetGameViewport()->Viewport;
		}
	}

	if (!Viewport)
	{
		TSharedPtr<SLevelViewport> ActiveViewport = GetActiveLevelViewport(Result.Errors);
		if (!ActiveViewport.IsValid())
		{
			return Result;
		}

		Viewport = ActiveViewport->GetActiveViewport();
		if (!Viewport)
		{
			Result.Errors.Add(TEXT("Could not get FViewport from the active Level Viewport."));
			return Result;
		}
	}

	int32 Width = Viewport->GetSizeXY().X;
	int32 Height = Viewport->GetSizeXY().Y;

	if (Width <= 0 || Height <= 0)
	{
		Result.Errors.Add(TEXT("Viewport has zero dimensions. It may be minimized or not yet rendered."));
		return Result;
	}

	TArray<FColor> Pixels;
	if (!Viewport->ReadPixels(Pixels))
	{
		Result.Errors.Add(TEXT("Failed to read pixels from the viewport. The viewport may not have rendered yet."));
		return Result;
	}

	if (Pixels.Num() != Width * Height)
	{
		Result.Errors.Add(FString::Printf(TEXT("Pixel count mismatch: expected %d, got %d"), Width * Height, Pixels.Num()));
		return Result;
	}

	UE_LOG(LogAgentFramework, Log, TEXT("ViewportActions: Captured viewport %dx%d (%d pixels)"), Width, Height, Pixels.Num());

	int32 JpegQuality = 75;
	UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("quality"), JpegQuality, Result.Errors, false);
	JpegQuality = FMath::Clamp(JpegQuality, 30, 95);

	FString FilePath = SavePixelsToDisk(Pixels, Width, Height, MaxDimension, JpegQuality);
	if (FilePath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Failed to save viewport capture to JPEG."));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("[IMAGE:%s]\n\n"
			 "Viewport captured successfully (%dx%d, resized to max %dpx, JPEG quality %d). "
			 "The image shows the current editor viewport and has been saved to the path above."),
		*FilePath, Width, Height, MaxDimension, JpegQuality);

	return Result;
}

// ============================================================================
// set_viewport_camera
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkViewportActions::ExecuteSetViewportCamera(
	const TSharedRef<FJsonObject>& Params,
	FAgentFrameworkActionResult& Result)
{
	TSharedPtr<SLevelViewport> ActiveViewport = GetActiveLevelViewport(Result.Errors);
	if (!ActiveViewport.IsValid())
	{
		return Result;
	}

	FLevelEditorViewportClient& ViewportClient = ActiveViewport->GetLevelViewportClient();

	double LocationX = ViewportClient.GetViewLocation().X;
	double LocationY = ViewportClient.GetViewLocation().Y;
	double LocationZ = ViewportClient.GetViewLocation().Z;
	bool bHasLocation = false;

	const TArray<TSharedPtr<FJsonValue>>* LocationArray = nullptr;
	if (UAgentFrameworkActionUtils::TryGetArrayParam(Params, TEXT("location"), LocationArray, Result.Errors, false) && LocationArray && LocationArray->Num() >= 3)
	{
		LocationX = (*LocationArray)[0]->AsNumber();
		LocationY = (*LocationArray)[1]->AsNumber();
		LocationZ = (*LocationArray)[2]->AsNumber();
		bHasLocation = true;
	}
	else
	{
		bool bLocXSet = UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("location_x"), LocationX, Result.Errors, false);
		bool bLocYSet = UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("location_y"), LocationY, Result.Errors, false);
		bool bLocZSet = UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("location_z"), LocationZ, Result.Errors, false);
		bHasLocation = bLocXSet || bLocYSet || bLocZSet;
	}

	if (bHasLocation)
	{
		ViewportClient.SetViewLocation(FVector(LocationX, LocationY, LocationZ));
	}

	double Pitch = ViewportClient.GetViewRotation().Pitch;
	double Yaw = ViewportClient.GetViewRotation().Yaw;
	double Roll = ViewportClient.GetViewRotation().Roll;
	bool bHasRotation = false;

	const TArray<TSharedPtr<FJsonValue>>* RotationArray = nullptr;
	if (UAgentFrameworkActionUtils::TryGetArrayParam(Params, TEXT("rotation"), RotationArray, Result.Errors, false) && RotationArray && RotationArray->Num() >= 3)
	{
		Pitch = (*RotationArray)[0]->AsNumber();
		Yaw = (*RotationArray)[1]->AsNumber();
		Roll = (*RotationArray)[2]->AsNumber();
		bHasRotation = true;
	}
	else
	{
		bool bPitchSet = UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("pitch"), Pitch, Result.Errors, false);
		bool bYawSet = UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("yaw"), Yaw, Result.Errors, false);
		bool bRollSet = UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("roll"), Roll, Result.Errors, false);
		bHasRotation = bPitchSet || bYawSet || bRollSet;
	}

	if (bHasRotation)
	{
		ViewportClient.SetViewRotation(FRotator(Pitch, Yaw, Roll));
	}

	double SpeedScalar = static_cast<double>(ViewportClient.GetCameraSpeed());
	if (UAgentFrameworkActionUtils::TryGetDoubleParam(Params, TEXT("speed"), SpeedScalar, Result.Errors, false) && SpeedScalar > 0.0)
	{
		FEditorViewportCameraSpeedSettings SpeedSettings = ViewportClient.GetCameraSpeedSettings();
		SpeedSettings.SetCurrentSpeed(FMath::Clamp(static_cast<float>(SpeedScalar), 0.01f, 100.0f));
		ViewportClient.SetCameraSpeedSettings(SpeedSettings);
	}

	ViewportClient.Invalidate();

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("Viewport camera updated: Location=(%.2f, %.2f, %.2f), Rotation=(Pitch=%.2f, Yaw=%.2f, Roll=%.2f), Speed=%.2f"),
		ViewportClient.GetViewLocation().X, ViewportClient.GetViewLocation().Y, ViewportClient.GetViewLocation().Z,
		ViewportClient.GetViewRotation().Pitch, ViewportClient.GetViewRotation().Yaw, ViewportClient.GetViewRotation().Roll,
		ViewportClient.GetCameraSpeed());

	return Result;
}

// ============================================================================
// set_viewport_view_mode
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkViewportActions::ExecuteSetViewportViewMode(
	const TSharedRef<FJsonObject>& Params,
	FAgentFrameworkActionResult& Result)
{
	TSharedPtr<SLevelViewport> ActiveViewport = GetActiveLevelViewport(Result.Errors);
	if (!ActiveViewport.IsValid())
	{
		return Result;
	}

	FLevelEditorViewportClient& ViewportClient = ActiveViewport->GetLevelViewportClient();

	FString ViewModeStr;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("view_mode"), ViewModeStr, Result.Errors, true))
	{
		return Result;
	}

	EViewModeIndex TargetViewMode = VMI_Lit;
	FString CleanMode = ViewModeStr.ToLower().TrimStartAndEnd();

	if (CleanMode == TEXT("lit"))                               TargetViewMode = VMI_Lit;
	else if (CleanMode == TEXT("unlit"))                       TargetViewMode = VMI_Unlit;
	else if (CleanMode == TEXT("wireframe"))                   TargetViewMode = VMI_Wireframe;
	else if (CleanMode == TEXT("detaillighting") || CleanMode == TEXT("detail_lighting")) TargetViewMode = VMI_Lit_DetailLighting;
	else if (CleanMode == TEXT("lightingonly") || CleanMode == TEXT("lighting_only"))     TargetViewMode = VMI_LightingOnly;
	else if (CleanMode == TEXT("shadercomplexity") || CleanMode == TEXT("shader_complexity")) TargetViewMode = VMI_ShaderComplexity;
	else if (CleanMode == TEXT("collision"))                   TargetViewMode = VMI_CollisionPawn;
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("Unrecognized view mode '%s'. Supported: lit, unlit, wireframe, detail_lighting, lighting_only, shader_complexity, collision."), *ViewModeStr));
		return Result;
	}

	ViewportClient.SetViewMode(TargetViewMode);
	ViewportClient.Invalidate();

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Viewport view mode set to '%s'."), *CleanMode);
	return Result;
}

// ============================================================================
// set_viewport_realtime
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkViewportActions::ExecuteSetViewportRealtime(
	const TSharedRef<FJsonObject>& Params,
	FAgentFrameworkActionResult& Result)
{
	TSharedPtr<SLevelViewport> ActiveViewport = GetActiveLevelViewport(Result.Errors);
	if (!ActiveViewport.IsValid())
	{
		return Result;
	}

	FLevelEditorViewportClient& ViewportClient = ActiveViewport->GetLevelViewportClient();

	bool bRealtime = false;
	if (!UAgentFrameworkActionUtils::TryGetBoolParam(Params, TEXT("realtime"), bRealtime, Result.Errors, true))
	{
		return Result;
	}

	ViewportClient.SetRealtime(bRealtime);
	ViewportClient.Invalidate();

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Viewport realtime rendering set to %s."), bRealtime ? TEXT("ON") : TEXT("OFF"));
	return Result;
}

// ============================================================================
// focus_viewport_on_selection
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkViewportActions::ExecuteFocusViewportOnSelection(
	const TSharedRef<FJsonObject>& Params,
	FAgentFrameworkActionResult& Result)
{
	if (!IsValid(GEditor))
	{
		Result.Errors.Add(TEXT("Editor not available."));
		return Result;
	}

	TSharedPtr<SLevelViewport> ActiveViewport = GetActiveLevelViewport(Result.Errors);
	if (!ActiveViewport.IsValid())
	{
		return Result;
	}

	FLevelEditorViewportClient& ViewportClient = ActiveViewport->GetLevelViewportClient();

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (!IsValid(SelectedActors) || SelectedActors->Num() == 0)
	{
		Result.Errors.Add(TEXT("No actors currently selected to focus on."));
		return Result;
	}

	FBox BoundingBox(ForceInit);
	int32 ValidActorCount = 0;

	for (FSelectionIterator It(*SelectedActors); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (IsValid(Actor))
		{
			BoundingBox += Actor->GetComponentsBoundingBox(true);
			ValidActorCount++;
		}
	}

	if (ValidActorCount == 0 || !BoundingBox.IsValid)
	{
		Result.Errors.Add(TEXT("Selected actors yielded an invalid bounding box."));
		return Result;
	}

	bool bInstant = false;
	UAgentFrameworkActionUtils::TryGetBoolParam(Params, TEXT("instant"), bInstant, Result.Errors, false);

	ViewportClient.FocusViewportOnBox(BoundingBox, bInstant);
	ViewportClient.Invalidate();

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Focused viewport on %d selected actor(s)."), ValidActorCount);
	return Result;
}

// ============================================================================
// Image Saving Helper
// ============================================================================

FString FAgentFrameworkViewportActions::SavePixelsToDisk(
	const TArray<FColor>& Pixels,
	int32 Width,
	int32 Height,
	int32 MaxDimension,
	int32 JpegQuality)
{
	if (Pixels.Num() == 0 || Width <= 0 || Height <= 0) return FString();

	int32 OutWidth = Width;
	int32 OutHeight = Height;
	int32 LongestEdge = FMath::Max(Width, Height);

	TArray<FColor> ResizedPixels;
	const TArray<FColor>* PixelsToEncode = &Pixels;

	if (LongestEdge > MaxDimension)
	{
		float Scale = static_cast<float>(MaxDimension) / static_cast<float>(LongestEdge);
		OutWidth = FMath::Max(1, FMath::RoundToInt(Width * Scale));
		OutHeight = FMath::Max(1, FMath::RoundToInt(Height * Scale));

		ResizedPixels.SetNumUninitialized(OutWidth * OutHeight);
		for (int32 Y = 0; Y < OutHeight; ++Y)
		{
			for (int32 X = 0; X < OutWidth; ++X)
			{
				float SrcX = static_cast<float>(X) * Width / OutWidth;
				float SrcY = static_cast<float>(Y) * Height / OutHeight;
				int32 SX = FMath::Clamp(FMath::FloorToInt(SrcX), 0, Width - 1);
				int32 SY = FMath::Clamp(FMath::FloorToInt(SrcY), 0, Height - 1);
				ResizedPixels[Y * OutWidth + X] = Pixels[SY * Width + SX];
			}
		}
		PixelsToEncode = &ResizedPixels;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);

	if (!ImageWrapper.IsValid())
	{
		UE_LOG(LogAgentFramework, Error, TEXT("ViewportActions: Failed to create JPEG ImageWrapper"));
		return FString();
	}

	TArray<uint8> RawData;
	RawData.SetNumUninitialized(OutWidth * OutHeight * 4);
	for (int32 i = 0; i < PixelsToEncode->Num(); ++i)
	{
		const FColor& C = (*PixelsToEncode)[i];
		RawData[i * 4 + 0] = C.B;
		RawData[i * 4 + 1] = C.G;
		RawData[i * 4 + 2] = C.R;
		RawData[i * 4 + 3] = C.A;
	}

	if (!ImageWrapper->SetRaw(RawData.GetData(), RawData.Num(), OutWidth, OutHeight, ERGBFormat::BGRA, 8))
	{
		UE_LOG(LogAgentFramework, Error, TEXT("ViewportActions: Failed to set raw pixel data on ImageWrapper"));
		return FString();
	}

	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(JpegQuality);
	if (CompressedData.Num() == 0)
	{
		UE_LOG(LogAgentFramework, Error, TEXT("ViewportActions: JPEG compression returned empty data"));
		return FString();
	}

	UE_LOG(LogAgentFramework, Log, TEXT("ViewportActions: JPEG encoded %dx%d Q%d -> %lld bytes"),
		OutWidth, OutHeight, JpegQuality, CompressedData.Num());

	FString SaveDirectory = FPaths::Combine(FPaths::ProjectDir(), TEXT(".agentframework"), TEXT("scratch"));
	IFileManager::Get().MakeDirectory(*SaveDirectory, true);
	FString FilePath = FPaths::Combine(SaveDirectory, TEXT("viewport_capture.jpg"));
	
	if (FFileHelper::SaveArrayToFile(CompressedData, *FilePath))
	{
		return FPaths::ConvertRelativePathToFull(FilePath);
	}
	
	UE_LOG(LogAgentFramework, Error, TEXT("ViewportActions: Failed to save JPEG to disk at %s"), *FilePath);
	return FString();
}

#undef LOCTEXT_NAMESPACE
