// Copyright 2026 AgentFramework. All Rights Reserved.

#include "PIE/AgentFrameworkPIEActions.h"
#include "AgentFrameworkActionUtils.h"
#include "AgentFrameworkCoreModule.h"
#include "AgentFrameworkSettings.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Widgets/Text/STextBlock.h"
#include "Components/Button.h"
#include "UObject/UnrealType.h"
#include "Sound/SoundBase.h"

#if WITH_EDITOR
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Logging/TokenizedMessage.h"
#include "Modules/ModuleManager.h"
#endif

#define LOCTEXT_NAMESPACE "AgentFrameworkPIEActions"

// ============================================================================
// Lifecycle
// ============================================================================

FAgentFrameworkPIEActions::FAgentFrameworkPIEActions() {}
FAgentFrameworkPIEActions::~FAgentFrameworkPIEActions() {}

// ============================================================================
// IAgentFrameworkActionExecutor Interface
// ============================================================================

FName FAgentFrameworkPIEActions::GetActionName() const { return FName(TEXT("PIE")); }

TArray<FString> FAgentFrameworkPIEActions::GetSupportedToolNames() const
{
	return {
		TEXT("start_pie_session"),
		TEXT("simulate_input"),
		TEXT("stop_pie_session"),
		TEXT("extract_ui_state"),
		TEXT("trigger_ui_element"),
		TEXT("query_world_state"),
		TEXT("invoke_pie_widget_delegate"),
		TEXT("get_active_runtime_widgets"),
		TEXT("execute_console_command"),
		TEXT("call_pie_actor_function")
	};
}

bool FAgentFrameworkPIEActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	return true;
}

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FAgentFrameworkActionResult Result;
	Result.bSuccess = false;

	// Security gate: require Full Access mode
	const UAgentFrameworkDeveloperSettings* Settings = UAgentFrameworkDeveloperSettings::Get();
	if (!IsValid(Settings) || Settings->SecurityMode != EAgentFrameworkSecurityMode::FullAccess)
	{
		Result.Errors.Add(TEXT("PIE automation requires Full Access security mode."));
		return Result;
	}

	FString Action;
	TArray<FString> IgnoredErrors;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("action"), Action, IgnoredErrors, false) || Action.IsEmpty())
	{
		UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("tool_name"), Action, IgnoredErrors, false);
	}

	FAgentFrameworkActionResult ExecutedResult;
	if (Action == TEXT("start_pie_session"))
		ExecutedResult = ExecuteStartPIE(Params, Result);
	else if (Action == TEXT("simulate_input"))
		ExecutedResult = ExecuteSimulateInput(Params, Result);
	else if (Action == TEXT("stop_pie_session"))
		ExecutedResult = ExecuteStopPIE(Params, Result);
	else if (Action == TEXT("extract_ui_state"))
		ExecutedResult = ExecuteExtractUIState(Params, Result);
	else if (Action == TEXT("trigger_ui_element"))
		ExecutedResult = ExecuteTriggerUIElement(Params, Result);
	else if (Action == TEXT("query_world_state"))
		ExecutedResult = ExecuteQueryWorldState(Params, Result);
	else if (Action == TEXT("invoke_pie_widget_delegate"))
		ExecutedResult = ExecuteInvokePIEWidgetDelegate(Params, Result);
	else if (Action == TEXT("get_active_runtime_widgets"))
		ExecutedResult = ExecuteGetActiveRuntimeWidgets(Params, Result);
	else if (Action == TEXT("execute_console_command"))
		ExecutedResult = ExecuteConsoleCommand(Params, Result);
	else if (Action == TEXT("call_pie_actor_function"))
		ExecutedResult = ExecuteCallActorFunction(Params, Result);
	else
	{
		Result.Errors.Add(TEXT("Unknown PIE action. Use start_pie_session, simulate_input, stop_pie_session, extract_ui_state, trigger_ui_element, query_world_state, invoke_pie_widget_delegate, get_active_runtime_widgets, execute_console_command, or call_pie_actor_function."));
		return Result;
	}

#if WITH_EDITOR
	if (ExecutedResult.bSuccess && GEditor)
	{
		USoundBase* SuccessSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileSuccess.CompileSuccess"));
		if (IsValid(SuccessSound))
		{
			GEditor->PlayEditorSound(SuccessSound);
		}
	}
#endif

	return ExecutedResult;
}

// ============================================================================
// start_pie_session
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteStartPIE(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (IsPIERunning())
	{
		Result.Warnings.Add(TEXT("PIE is already running."));
		Result.bSuccess = true;
		Result.ResultMessage = TEXT("PIE session is already active. Use simulate_input to interact, or stop_pie_session to end it.");
		return Result;
	}

	if (!GUnrealEd)
	{
		Result.Errors.Add(TEXT("GUnrealEd is not available."));
		return Result;
	}

	// Configure PIE settings
	FRequestPlaySessionParams PlayParams;

	// Optional: set the start location
	FString StartMode;
	TArray<FString> OptionalErrors;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("start_mode"), StartMode, OptionalErrors, false);

	if (StartMode.Equals(TEXT("current_camera"), ESearchCase::IgnoreCase))
	{
		ULevelEditorPlaySettings* PlaySettings = NewObject<ULevelEditorPlaySettings>();
		if (IsValid(PlaySettings))
		{
			PlaySettings->LastExecutedPlayModeLocation = PlayLocation_CurrentCameraLocation;
			PlayParams.EditorPlaySettings = PlaySettings;
		}
	}

	// Request PIE session
	GUnrealEd->RequestPlaySession(PlayParams);

	UE_LOG(LogAgentFramework, Log, TEXT("PIEActions: Requested PIE session start."));

	Result.bSuccess = true;
	Result.ResultMessage = TEXT("PIE session started. The game is now running in the editor. "
		"Use read_message_log to check for runtime errors. "
		"Use simulate_input to inject key presses. "
		"Use stop_pie_session to end the session.");
	return Result;
}

// ============================================================================
// simulate_input
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteSimulateInput(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.Errors.Add(TEXT("PIE is not running. Call start_pie_session first."));
		return Result;
	}

	FString KeyName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("key"), KeyName, Result.Errors, true))
	{
		return Result;
	}

	FString ActionType = TEXT("press");
	TArray<FString> OptionalErrors;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("action_type"), ActionType, OptionalErrors, false);
	ActionType = ActionType.ToLower();

	float DurationSeconds = 0.0f;
	UAgentFrameworkActionUtils::TryGetFloatParam(Params, TEXT("duration"), DurationSeconds, OptionalErrors, false);
	DurationSeconds = FMath::Clamp(DurationSeconds, 0.0f, 10.0f);

	// Resolve the FKey
	FKey Key(*KeyName);
	if (!Key.IsValid())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("Invalid key name: '%s'. Use UE key names like 'W', 'A', 'S', 'D', 'SpaceBar', 'LeftShift', "
				 "'LeftMouseButton', 'Gamepad_FaceButton_Bottom', 'Gamepad_LeftStick_Up', etc."),
			*KeyName));
		return Result;
	}

	FString Report;

	if (ActionType == TEXT("press") || ActionType == TEXT("tap"))
	{
		// Simulate press + release
		FSlateApplication& Slate = FSlateApplication::Get();
		FKeyEvent KeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);

		Slate.ProcessKeyDownEvent(KeyEvent);

		if (DurationSeconds > 0.0f)
		{
			// For duration-based press, we'd need async. For now, just hold for one frame.
			// The AI should use multiple simulate_input calls for sustained input.
			Report = FString::Printf(TEXT("Pressed '%s' (note: sustained press for %.1fs requires multiple calls or PIE tick integration). "
				"Key was pressed and released in a single frame."), *KeyName, DurationSeconds);
		}
		else
		{
			Report = FString::Printf(TEXT("Tapped '%s' (press + release in one frame)."), *KeyName);
		}

		Slate.ProcessKeyUpEvent(KeyEvent);
	}
	else if (ActionType == TEXT("down") || ActionType == TEXT("hold"))
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		FKeyEvent KeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
		Slate.ProcessKeyDownEvent(KeyEvent);
		Report = FString::Printf(TEXT("Pressed '%s' down (held). Use action_type='up' to release."), *KeyName);
	}
	else if (ActionType == TEXT("up") || ActionType == TEXT("release"))
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		FKeyEvent KeyEvent(Key, FModifierKeysState(), 0, false, 0, 0);
		Slate.ProcessKeyUpEvent(KeyEvent);
		Report = FString::Printf(TEXT("Released '%s'."), *KeyName);
	}
	else
	{
		Result.Errors.Add(FString::Printf(TEXT("Unknown action_type '%s'. Use 'press', 'down', or 'up'."), *ActionType));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = Report;
	return Result;
}

// ============================================================================
// stop_pie_session
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteStopPIE(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.bSuccess = true;
		Result.ResultMessage = TEXT("PIE is not running. Nothing to stop.");
		return Result;
	}

	if (GUnrealEd)
	{
		GUnrealEd->RequestEndPlayMap();
		UE_LOG(LogAgentFramework, Log, TEXT("PIEActions: Requested PIE session stop."));
	}

	FString PiePopupErrors;
#if WITH_EDITOR
	if (FModuleManager::Get().IsModuleLoaded("MessageLog"))
	{
		FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
		if (MessageLogModule.IsRegisteredLogListing("PIE"))
		{
			TSharedPtr<IMessageLogListing> PieListing = MessageLogModule.GetLogListing("PIE");
			if (PieListing.IsValid())
			{
				const TArray<TSharedRef<FTokenizedMessage>>& Messages = PieListing->GetFilteredMessages();
				for (const TSharedRef<FTokenizedMessage>& Msg : Messages)
				{
					if (Msg->GetSeverity() == EMessageSeverity::Error)
					{
						FString MsgText = Msg->ToText().ToString();
						PiePopupErrors += FString::Printf(TEXT("  - [PIE MessageLog] %s\n"), *MsgText);
					}
				}
			}
		}
	}
#endif

	Result.bSuccess = true;
	if (!PiePopupErrors.IsEmpty())
	{
		Result.ResultMessage = FString::Printf(TEXT("PIE session stopped.\n\n--- Editor PIE Message Log Errors ---\n%s"), *PiePopupErrors);
	}
	else
	{
		Result.ResultMessage = TEXT("PIE session stopped (no PIE Message Log errors).");
	}
	return Result;
}

// ============================================================================
// Helpers
// ============================================================================

bool FAgentFrameworkPIEActions::IsPIERunning()
{
	if (!GEditor) return false;
	return GEditor->IsPlaySessionInProgress();
}

static FString GetWidgetText(UWidget* Widget)
{
	if (!IsValid(Widget)) return TEXT("");
	
	FProperty* TextProp = Widget->GetClass()->FindPropertyByName(TEXT("Text"));
	if (TextProp)
	{
		if (FTextProperty* TextProperty = CastField<FTextProperty>(TextProp))
		{
			FText TextValue = TextProperty->GetPropertyValue_InContainer(Widget);
			return TextValue.ToString();
		}
		else if (FStrProperty* StrProperty = CastField<FStrProperty>(TextProp))
		{
			return StrProperty->GetPropertyValue_InContainer(Widget);
		}
		else if (FNameProperty* NameProperty = CastField<FNameProperty>(TextProp))
		{
			return NameProperty->GetPropertyValue_InContainer(Widget).ToString();
		}
	}
	
	return TEXT("");
}

static FString FindTextInWidget(UWidget* Widget)
{
	if (!IsValid(Widget)) return TEXT("");
	FString FoundText = GetWidgetText(Widget);
	if (!FoundText.IsEmpty()) return FoundText;

	if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
	{
		if (IsValid(Panel))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				UWidget* Child = Panel->GetChildAt(i);
				if (IsValid(Child))
				{
					FoundText = FindTextInWidget(Child);
					if (!FoundText.IsEmpty()) return FoundText;
				}
			}
		}
	}
	return TEXT("");
}

static void FindSlateWidgetsRecursive(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SWidget>>& OutWidgets)
{
	OutWidgets.Add(Widget);
	FChildren* Children = Widget->GetChildren();
	if (Children)
	{
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			FindSlateWidgetsRecursive(Children->GetChildAt(i), OutWidgets);
		}
	}
}

static bool IsSlateInteractable(const TSharedRef<SWidget>& Widget)
{
	FString TypeName = Widget->GetTypeAsString();
	return TypeName.Contains(TEXT("Button")) || TypeName.Contains(TEXT("CheckBox")) || TypeName.Contains(TEXT("Hyperlink"));
}

static FString GetSlateWidgetText(const TSharedRef<SWidget>& Widget)
{
	FString TypeName = Widget->GetTypeAsString();
	if (TypeName == TEXT("STextBlock"))
	{
		const STextBlock* TextBlock = static_cast<const STextBlock*>(&Widget.Get());
		return TextBlock->GetText().ToString();
	}
	
	FChildren* Children = Widget->GetChildren();
	if (Children)
	{
		for (int32 i = 0; i < Children->Num(); ++i)
		{
			FString ChildText = GetSlateWidgetText(Children->GetChildAt(i));
			if (!ChildText.IsEmpty()) return ChildText;
		}
	}
	return TEXT("");
}

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteExtractUIState(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.Errors.Add(TEXT("PIE is not running."));
		return Result;
	}

	UWorld* World = nullptr;
	if (GEditor)
	{
		FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext();
		if (PIEWorldContext)
		{
			World = PIEWorldContext->World();
		}
	}
	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("Could not find active PIE world."));
		return Result;
	}

	TSharedRef<FJsonObject> UMGStateJson = MakeShared<FJsonObject>();
	
	// Clear previous cache for raw Slate widgets
	CachedSlateWidgets.Empty();
	int32 SlateIndex = 0;

	// 1. Extract UMG widgets
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* UserWidget = *It;
		if (IsValid(UserWidget) && UserWidget->GetWorld() == World && UserWidget->IsInViewport() && IsValid(UserWidget->WidgetTree))
		{
			FString WidgetName = UserWidget->GetName();
			
			UserWidget->WidgetTree->ForEachWidget([&](UWidget* ChildWidget) {
				if (IsValid(ChildWidget) && ChildWidget->IsVisible())
				{
					FString TypeName = ChildWidget->GetClass()->GetName();
					
					bool bIsInteractable = TypeName.Contains(TEXT("Button")) || 
										   TypeName.Contains(TEXT("CheckBox")) || 
										   TypeName.Contains(TEXT("EditableText")) ||
										   TypeName.Contains(TEXT("Slider"));
										   
					if (bIsInteractable)
					{
						FString Path = FString::Printf(TEXT("%s.%s"), *WidgetName, *ChildWidget->GetName());
						
						TSharedRef<FJsonObject> ElementJson = MakeShared<FJsonObject>();
						ElementJson->SetStringField(TEXT("type"), TypeName);
						
						FString TextContent = FindTextInWidget(ChildWidget);
						if (!TextContent.IsEmpty())
						{
							ElementJson->SetStringField(TEXT("text"), TextContent);
						}
						
						TSharedPtr<SWidget> SlateWidget = ChildWidget->GetCachedWidget();
						if (SlateWidget.IsValid())
						{
							FGeometry Geometry = SlateWidget->GetTickSpaceGeometry();
							FVector2D Center = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5f;
							ElementJson->SetNumberField(TEXT("center_x"), Center.X);
							ElementJson->SetNumberField(TEXT("center_y"), Center.Y);
						}
						
						UMGStateJson->SetObjectField(Path, ElementJson);
					}
				}
			});
		}
	}
	
	TSharedRef<FJsonObject> SlateStateJson = MakeShared<FJsonObject>();
	
	// 2. Extract raw Slate widgets
	TArray<TSharedRef<SWindow>> Windows;
	FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
	
	TArray<TSharedRef<SWidget>> AllSlateWidgets;
	for (const TSharedRef<SWindow>& Window : Windows)
	{
		FindSlateWidgetsRecursive(Window, AllSlateWidgets);
	}
	
	for (const TSharedRef<SWidget>& Widget : AllSlateWidgets)
	{
		FString TypeName = Widget->GetTypeAsString();
		if (IsSlateInteractable(Widget))
		{
			if (TypeName.Equals(TEXT("SObjectWidget")) || TypeName.Contains(TEXT("UMG")))
			{
				continue;
			}
			
			FString SlateId = FString::Printf(TEXT("slate_%s_%d"), *TypeName.ToLower(), SlateIndex++);
			CachedSlateWidgets.Add(SlateId, Widget);
			
			TSharedRef<FJsonObject> ElementJson = MakeShared<FJsonObject>();
			ElementJson->SetStringField(TEXT("type"), TypeName);
			
			FString TextContent = GetSlateWidgetText(Widget);
			if (!TextContent.IsEmpty())
			{
				ElementJson->SetStringField(TEXT("text"), TextContent);
			}
			
			FGeometry Geometry = Widget->GetTickSpaceGeometry();
			FVector2D Center = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5f;
			ElementJson->SetNumberField(TEXT("center_x"), Center.X);
			ElementJson->SetNumberField(TEXT("center_y"), Center.Y);
			
			SlateStateJson->SetObjectField(SlateId, ElementJson);
		}
	}
	
	TSharedRef<FJsonObject> ReturnJson = MakeShared<FJsonObject>();
	ReturnJson->SetObjectField(TEXT("umg"), UMGStateJson);
	ReturnJson->SetObjectField(TEXT("slate"), SlateStateJson);
	
	FString ResponseString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ReturnJson, Writer);

	Result.bSuccess = true;
	Result.ResultMessage = ResponseString;
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteTriggerUIElement(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.Errors.Add(TEXT("PIE is not running."));
		return Result;
	}

	UWorld* World = nullptr;
	if (GEditor)
	{
		FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext();
		if (PIEWorldContext)
		{
			World = PIEWorldContext->World();
		}
	}
	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("Could not find active PIE world."));
		return Result;
	}

	FString WidgetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("widget_path"), WidgetPath, Result.Errors, true))
	{
		return Result;
	}

	TSharedPtr<SWidget> TargetSlateWidget;

	if (WidgetPath.StartsWith(TEXT("slate_")))
	{
		TWeakPtr<SWidget>* FoundPtr = CachedSlateWidgets.Find(WidgetPath);
		if (FoundPtr && FoundPtr->IsValid())
		{
			TargetSlateWidget = FoundPtr->Pin();
		}
		else
		{
			Result.Errors.Add(FString::Printf(TEXT("Slate widget ID '%s' is no longer valid or was not cached. Call extract_ui_state first."), *WidgetPath));
			return Result;
		}
	}
	else
	{
		FString WidgetName;
		FString ChildWidgetName;
		if (!WidgetPath.Split(TEXT("."), &WidgetName, &ChildWidgetName))
		{
			Result.Errors.Add(TEXT("Invalid widget_path format. Expected UserWidgetInstanceName.ChildWidgetName"));
			return Result;
		}

		UUserWidget* TargetUserWidget = nullptr;
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* WidgetCandidate = *It;
			if (IsValid(WidgetCandidate) && WidgetCandidate->GetWorld() == World && WidgetCandidate->IsInViewport() && WidgetCandidate->GetName() == WidgetName)
			{
				TargetUserWidget = WidgetCandidate;
				break;
			}
		}

		if (!IsValid(TargetUserWidget))
		{
			Result.Errors.Add(FString::Printf(TEXT("UserWidget '%s' not found or not in viewport."), *WidgetName));
			return Result;
		}

		if (!IsValid(TargetUserWidget->WidgetTree))
		{
			Result.Errors.Add(FString::Printf(TEXT("WidgetTree for UserWidget '%s' is invalid."), *WidgetName));
			return Result;
		}

		UWidget* TargetWidget = TargetUserWidget->WidgetTree->FindWidget(*ChildWidgetName);
		if (!IsValid(TargetWidget))
		{
			Result.Errors.Add(FString::Printf(TEXT("Child widget '%s' not found in '%s'."), *ChildWidgetName, *WidgetName));
			return Result;
		}

		TargetSlateWidget = TargetWidget->GetCachedWidget();
		if (!TargetSlateWidget.IsValid())
		{
			Result.Errors.Add(FString::Printf(TEXT("UMG widget '%s' does not have a valid cached Slate representation."), *WidgetPath));
			return Result;
		}
	}

	FSlateApplication& SlateApp = FSlateApplication::Get();
	FGeometry Geometry = TargetSlateWidget->GetTickSpaceGeometry();
	FVector2D Center = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5f;

	// Use the window that actually owns the target widget, not whichever window Slate currently
	// considers "active". In an automated session the OS foreground (and so Slate's active window)
	// is often the calling process, not the PIE viewport - GetActiveTopLevelWindow() then silently
	// dispatches the synthesized click into the wrong window's widget path, so the tool keeps
	// reporting success while nothing downstream of the target widget ever runs.
	TSharedPtr<SWindow> Window = SlateApp.FindWidgetWindow(TargetSlateWidget.ToSharedRef());
	TSharedPtr<FGenericWindow> GenWindow = Window.IsValid() ? Window->GetNativeWindow() : nullptr;

	TSet<FKey> PressedButtons;
	PressedButtons.Add(EKeys::LeftMouseButton);

	FPointerEvent MouseDownEvent(
		SlateApp.CursorPointerIndex,
		Center,
		Center,
		PressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState()
	);
	SlateApp.ProcessMouseButtonDownEvent(GenWindow, MouseDownEvent);

	TSet<FKey> EmptyPressedButtons;

	FPointerEvent MouseUpEvent(
		SlateApp.CursorPointerIndex,
		Center,
		Center,
		EmptyPressedButtons,
		EKeys::LeftMouseButton,
		0.0f,
		FModifierKeysState()
	);
	SlateApp.ProcessMouseButtonUpEvent(MouseUpEvent);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Successfully programmatically clicked widget '%s' at screen coordinates (%f, %f)."), *WidgetPath, Center.X, Center.Y);
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteQueryWorldState(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.Errors.Add(TEXT("PIE is not running."));
		return Result;
	}

	UWorld* World = nullptr;
	if (GEditor)
	{
		FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext();
		if (PIEWorldContext)
		{
			World = PIEWorldContext->World();
		}
	}
	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("Could not find active PIE world."));
		return Result;
	}

	TArray<AActor*> MatchingActors;
	
	TArray<UClass*> ClassFilters;
	TArray<FString> ClassNames;
	TArray<FString> OptionalErrors;
	if (UAgentFrameworkActionUtils::TryGetStringArrayParam(Params, TEXT("classes"), ClassNames, OptionalErrors, false))
	{
		for (const FString& ClassName : ClassNames)
		{
			UClass* TargetClass = UClass::TryFindTypeSlow<UClass>(*ClassName);
			if (!IsValid(TargetClass))
			{
				TargetClass = FindObject<UClass>(nullptr, *ClassName);
			}
			if (IsValid(TargetClass))
			{
				ClassFilters.Add(TargetClass);
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Could not find class type: '%s'"), *ClassName));
			}
		}
	}
	
	TArray<FString> TagFilters;
	UAgentFrameworkActionUtils::TryGetStringArrayParam(Params, TEXT("tags"), TagFilters, OptionalErrors, false);
	
	APlayerController* PlayerController = World->GetFirstPlayerController();
	APawn* PlayerPawn = IsValid(PlayerController) ? PlayerController->GetPawn() : nullptr;
	FVector PlayerLoc = IsValid(PlayerPawn) ? PlayerPawn->GetActorLocation() : FVector::ZeroVector;

	float Radius = 0.0f;
	UAgentFrameworkActionUtils::TryGetFloatParam(Params, TEXT("radius"), Radius, OptionalErrors, false);

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor)) continue;

		if (ClassFilters.Num() > 0)
		{
			bool bMatchesClass = false;
			for (UClass* ClassFilter : ClassFilters)
			{
				if (IsValid(ClassFilter) && Actor->IsA(ClassFilter))
				{
					bMatchesClass = true;
					break;
				}
			}
			if (!bMatchesClass) continue;
		}

		if (TagFilters.Num() > 0)
		{
			bool bMatchesTag = false;
			for (const FString& TagFilter : TagFilters)
			{
				if (Actor->ActorHasTag(*TagFilter))
				{
					bMatchesTag = true;
					break;
				}
			}
			if (!bMatchesTag) continue;
		}

		if (Radius > 0.0f && IsValid(PlayerPawn))
		{
			float Distance = FVector::Dist(PlayerLoc, Actor->GetActorLocation());
			if (Distance > Radius) continue;
		}

		MatchingActors.Add(Actor);
	}
	
	TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
	
	if (IsValid(PlayerPawn))
	{
		TSharedRef<FJsonObject> PlayerJson = MakeShared<FJsonObject>();
		PlayerJson->SetStringField(TEXT("class"), PlayerPawn->GetClass()->GetName());
		PlayerJson->SetStringField(TEXT("name"), PlayerPawn->GetName());
		
		TSharedRef<FJsonObject> LocJson = MakeShared<FJsonObject>();
		LocJson->SetNumberField(TEXT("x"), PlayerLoc.X);
		LocJson->SetNumberField(TEXT("y"), PlayerLoc.Y);
		LocJson->SetNumberField(TEXT("z"), PlayerLoc.Z);
		PlayerJson->SetObjectField(TEXT("location"), LocJson);
		
		ResponseObj->SetObjectField(TEXT("player"), PlayerJson);
	}

	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	for (AActor* Actor : MatchingActors)
	{
		if (!IsValid(Actor) || Actor == PlayerPawn) continue;
		
		TSharedRef<FJsonObject> ActorJson = MakeShared<FJsonObject>();
		ActorJson->SetStringField(TEXT("name"), Actor->GetName());
		ActorJson->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		
		FVector Loc = Actor->GetActorLocation();
		TSharedRef<FJsonObject> LocJson = MakeShared<FJsonObject>();
		LocJson->SetNumberField(TEXT("x"), Loc.X);
		LocJson->SetNumberField(TEXT("y"), Loc.Y);
		LocJson->SetNumberField(TEXT("z"), Loc.Z);
		ActorJson->SetObjectField(TEXT("location"), LocJson);
		
		TArray<TSharedPtr<FJsonValue>> ActorTags;
		for (const FName& Tag : Actor->Tags)
		{
			ActorTags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		ActorJson->SetArrayField(TEXT("tags"), ActorTags);
		
		ActorsArray.Add(MakeShared<FJsonValueObject>(ActorJson));
	}
	ResponseObj->SetArrayField(TEXT("actors"), ActorsArray);

	FString ResponseString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ResponseObj, Writer);

	Result.bSuccess = true;
	Result.ResultMessage = ResponseString;
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteInvokePIEWidgetDelegate(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.Errors.Add(TEXT("PIE session is not running. Call start_pie_session first."));
		return Result;
	}

	UWorld* World = nullptr;
	if (GEditor && GEditor->GetPIEWorldContext())
	{
		World = GEditor->GetPIEWorldContext()->World();
	}

	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("Could not obtain active PIE world."));
		return Result;
	}

	FString TargetWidgetClassOrName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("widget_class_or_name"), TargetWidgetClassOrName, Result.Errors, true))
	{
		return Result;
	}

	FString WidgetPropertyName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("widget_property_name"), WidgetPropertyName, Result.Errors, true))
	{
		return Result;
	}

	FString DelegateName = TEXT("OnClicked");
	TArray<FString> OptionalErrors;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("delegate_name"), DelegateName, OptionalErrors, false);
	if (DelegateName.IsEmpty())
	{
		DelegateName = TEXT("OnClicked");
	}

	UUserWidget* TargetUserWidget = nullptr;

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (IsValid(Widget) && Widget->GetWorld() == World)
		{
			FString InstanceName = Widget->GetName();
			FString ClassName = Widget->GetClass()->GetName();

			if (InstanceName.Equals(TargetWidgetClassOrName, ESearchCase::IgnoreCase) ||
				ClassName.Equals(TargetWidgetClassOrName, ESearchCase::IgnoreCase) ||
				InstanceName.Contains(TargetWidgetClassOrName) ||
				ClassName.Contains(TargetWidgetClassOrName))
			{
				TargetUserWidget = Widget;
				break;
			}
		}
	}

	if (!IsValid(TargetUserWidget))
	{
		Result.Errors.Add(FString::Printf(TEXT("Could not find active UUserWidget instance matching '%s' in PIE world."), *TargetWidgetClassOrName));
		return Result;
	}

	UObject* TargetObject = nullptr;

	if (IsValid(TargetUserWidget->WidgetTree))
	{
		UWidget* FoundChildWidget = TargetUserWidget->WidgetTree->FindWidget(*WidgetPropertyName);
		if (IsValid(FoundChildWidget))
		{
			TargetObject = FoundChildWidget;
		}
	}

	if (!IsValid(TargetObject))
	{
		FProperty* Prop = TargetUserWidget->GetClass()->FindPropertyByName(*WidgetPropertyName);
		if (Prop)
		{
			if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				TargetObject = ObjProp->GetObjectPropertyValue_InContainer(TargetUserWidget);
			}
		}
	}

	if (!IsValid(TargetObject))
	{
		TargetObject = TargetUserWidget;
	}

	bool bDelegateInvoked = false;

	if (UButton* Button = Cast<UButton>(TargetObject))
	{
		if (DelegateName.Equals(TEXT("OnClicked"), ESearchCase::IgnoreCase) ||
			DelegateName.Equals(TEXT("on_clicked"), ESearchCase::IgnoreCase))
		{
			Button->OnClicked.Broadcast();
			bDelegateInvoked = true;
		}
	}

	if (!bDelegateInvoked)
	{
		FProperty* DelegateProp = TargetObject->GetClass()->FindPropertyByName(*DelegateName);
		if (!DelegateProp)
		{
			for (TFieldIterator<FProperty> PropIt(TargetObject->GetClass()); PropIt; ++PropIt)
			{
				if (PropIt->GetName().Equals(DelegateName, ESearchCase::IgnoreCase))
				{
					DelegateProp = *PropIt;
					break;
				}
			}
		}

		if (DelegateProp)
		{
			if (FMulticastDelegateProperty* MulticastProp = CastField<FMulticastDelegateProperty>(DelegateProp))
			{
				const FMulticastScriptDelegate* ConstScriptDelegate = MulticastProp->GetMulticastDelegate(TargetObject);
				FMulticastScriptDelegate* ScriptDelegate = const_cast<FMulticastScriptDelegate*>(ConstScriptDelegate);
				if (ScriptDelegate)
				{
					ScriptDelegate->ProcessDelegate<UObject>(nullptr);
					bDelegateInvoked = true;
				}
			}
			else if (FDelegateProperty* SingleProp = CastField<FDelegateProperty>(DelegateProp))
			{
				FScriptDelegate* ScriptDelegate = SingleProp->GetPropertyValuePtr_InContainer(TargetObject);
				if (ScriptDelegate)
				{
					ScriptDelegate->ProcessDelegate<UObject>(nullptr);
					bDelegateInvoked = true;
				}
			}
		}
	}

	if (!bDelegateInvoked)
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to invoke delegate '%s' on object '%s' (Widget: '%s'). Property not found or delegate call failed."),
			*DelegateName, *TargetObject->GetName(), *TargetUserWidget->GetName()));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Successfully invoked delegate '%s' on target '%s' within UUserWidget '%s'."),
		*DelegateName, *TargetObject->GetName(), *TargetUserWidget->GetName());
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteGetActiveRuntimeWidgets(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.Errors.Add(TEXT("PIE session is not running. Call start_pie_session first."));
		return Result;
	}

	UWorld* World = nullptr;
	if (GEditor && GEditor->GetPIEWorldContext())
	{
		World = GEditor->GetPIEWorldContext()->World();
	}

	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("Could not obtain active PIE world."));
		return Result;
	}

	FString ClassFilter;
	TArray<FString> OptionalErrors;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("widget_class_name"), ClassFilter, OptionalErrors, false);

	bool bIncludeHidden = true;
	UAgentFrameworkActionUtils::TryGetBoolParam(Params, TEXT("include_hidden"), bIncludeHidden, OptionalErrors, false);

	TArray<TSharedPtr<FJsonValue>> WidgetJsonArray;

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* UserWidget = *It;
		if (IsValid(UserWidget) && UserWidget->GetWorld() == World)
		{
			FString ClassName = UserWidget->GetClass()->GetName();
			FString InstanceName = UserWidget->GetName();

			if (!ClassFilter.IsEmpty())
			{
				if (!ClassName.Contains(ClassFilter, ESearchCase::IgnoreCase) &&
					!InstanceName.Contains(ClassFilter, ESearchCase::IgnoreCase))
				{
					continue;
				}
			}

			bool bIsVisible = UserWidget->IsVisible();
			if (!bIncludeHidden && !bIsVisible)
			{
				continue;
			}

			TSharedRef<FJsonObject> WidgetObj = MakeShared<FJsonObject>();
			WidgetObj->SetStringField(TEXT("name"), InstanceName);
			WidgetObj->SetStringField(TEXT("class_name"), ClassName);
			WidgetObj->SetStringField(TEXT("class_path"), UserWidget->GetClass()->GetPathName());
			WidgetObj->SetBoolField(TEXT("is_in_viewport"), UserWidget->IsInViewport());
			WidgetObj->SetBoolField(TEXT("is_visible"), bIsVisible);

			FString VisibilityStr;
			switch (UserWidget->GetVisibility())
			{
			case ESlateVisibility::Visible: VisibilityStr = TEXT("Visible"); break;
			case ESlateVisibility::Collapsed: VisibilityStr = TEXT("Collapsed"); break;
			case ESlateVisibility::Hidden: VisibilityStr = TEXT("Hidden"); break;
			case ESlateVisibility::HitTestInvisible: VisibilityStr = TEXT("HitTestInvisible"); break;
			case ESlateVisibility::SelfHitTestInvisible: VisibilityStr = TEXT("SelfHitTestInvisible"); break;
			default: VisibilityStr = TEXT("Unknown"); break;
			}
			WidgetObj->SetStringField(TEXT("visibility"), VisibilityStr);

			TArray<TSharedPtr<FJsonValue>> ParentHierarchyArray;
			UWidget* CurrentParent = UserWidget->GetParent();
			while (IsValid(CurrentParent))
			{
				ParentHierarchyArray.Add(MakeShared<FJsonValueString>(
					FString::Printf(TEXT("%s (%s)"), *CurrentParent->GetName(), *CurrentParent->GetClass()->GetName())
				));
				CurrentParent = CurrentParent->GetParent();
			}
			if (UserWidget->IsInViewport())
			{
				ParentHierarchyArray.Add(MakeShared<FJsonValueString>(TEXT("Viewport")));
			}
			WidgetObj->SetArrayField(TEXT("parent_hierarchy"), ParentHierarchyArray);

			int32 ChildCount = 0;
			if (IsValid(UserWidget->WidgetTree))
			{
				UserWidget->WidgetTree->ForEachWidget([&ChildCount](UWidget* Child) {
					if (IsValid(Child)) { ChildCount++; }
				});
			}
			WidgetObj->SetNumberField(TEXT("child_count"), ChildCount);

			WidgetJsonArray.Add(MakeShared<FJsonValueObject>(WidgetObj));
		}
	}

	TSharedRef<FJsonObject> ResponseObj = MakeShared<FJsonObject>();
	ResponseObj->SetArrayField(TEXT("widgets"), WidgetJsonArray);
	ResponseObj->SetNumberField(TEXT("total_count"), WidgetJsonArray.Num());

	FString ResponseString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseString);
	FJsonSerializer::Serialize(ResponseObj, Writer);

	Result.bSuccess = true;
	Result.ResultMessage = ResponseString;
	return Result;
}

// ============================================================================
// execute_console_command
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteConsoleCommand(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.Errors.Add(TEXT("PIE is not running."));
		return Result;
	}

	FString Command;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("command"), Command, Result.Errors, true))
	{
		return Result;
	}

	int32 WorldContextIndex = 0;
	Params->TryGetNumberField(TEXT("world_context_index"), WorldContextIndex);

	UWorld* World = nullptr;
	if (GEditor)
	{
		FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext(WorldContextIndex);
		if (PIEWorldContext)
		{
			World = PIEWorldContext->World();
		}
		if (!World)
		{
			World = GEditor->GetCurrentPlayWorld();
		}
		if (!World)
		{
			World = GEditor->PlayWorld;
		}
	}

	if (!World)
	{
		Result.Errors.Add(TEXT("Could not resolve active PIE world context."));
		return Result;
	}

	bool bExecuted = false;
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		PC->ConsoleCommand(Command, true);
		bExecuted = true;
	}
	else if (GEngine)
	{
		GEngine->Exec(World, *Command);
		bExecuted = true;
	}

	if (bExecuted)
	{
		Result.bSuccess = true;
		Result.ResultMessage = FString::Printf(TEXT("Executed console command '%s' in PIE world '%s'."), *Command, *World->GetName());
	}
	else
	{
		Result.Errors.Add(TEXT("Failed to execute console command: no PlayerController or GEngine available in PIE world."));
	}

	return Result;
}

// ============================================================================
// call_pie_actor_function
// ============================================================================

FAgentFrameworkActionResult FAgentFrameworkPIEActions::ExecuteCallActorFunction(
	const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	if (!IsPIERunning())
	{
		Result.Errors.Add(TEXT("PIE is not running."));
		return Result;
	}

	FString ActorIdentifier;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("actor_name"), ActorIdentifier, Result.Errors, false) || ActorIdentifier.IsEmpty())
	{
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("actor_label"), ActorIdentifier, Result.Errors, false) || ActorIdentifier.IsEmpty())
		{
			Result.Errors.Add(TEXT("actor_name or actor_label is required."));
			return Result;
		}
	}

	FString FunctionName;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("function_name"), FunctionName, Result.Errors, true))
	{
		return Result;
	}

	int32 WorldContextIndex = 0;
	Params->TryGetNumberField(TEXT("world_context_index"), WorldContextIndex);

	UWorld* World = nullptr;
	if (GEditor)
	{
		FWorldContext* PIEWorldContext = GEditor->GetPIEWorldContext(WorldContextIndex);
		if (PIEWorldContext)
		{
			World = PIEWorldContext->World();
		}
		if (!World)
		{
			World = GEditor->GetCurrentPlayWorld();
		}
		if (!World)
		{
			World = GEditor->PlayWorld;
		}
	}

	if (!World)
	{
		Result.Errors.Add(TEXT("Could not resolve active PIE world context."));
		return Result;
	}

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor))
		{
			if (Actor->GetName().Equals(ActorIdentifier, ESearchCase::IgnoreCase) ||
#if WITH_EDITOR
				Actor->GetActorLabel().Equals(ActorIdentifier, ESearchCase::IgnoreCase) ||
#endif
				Actor->GetPathName().Contains(ActorIdentifier))
			{
				TargetActor = Actor;
				break;
			}
		}
	}

	if (!TargetActor)
	{
		Result.Errors.Add(FString::Printf(TEXT("Actor '%s' not found in active PIE world."), *ActorIdentifier));
		return Result;
	}

	UFunction* Func = TargetActor->FindFunction(FName(*FunctionName));
	if (!Func)
	{
		Result.Errors.Add(FString::Printf(TEXT("Function '%s' not found on actor '%s' (%s)."),
			*FunctionName, *TargetActor->GetName(), *TargetActor->GetClass()->GetName()));
		return Result;
	}

	// Safe parameter allocation to prevent access violations if function expects parameters
	if (Func->ParmsSize > 0)
	{
		uint8* Parms = (uint8*)FMemory_Alloca(Func->ParmsSize);
		FMemory::Memzero(Parms, Func->ParmsSize);

		for (TFieldIterator<FProperty> It(Func); It; ++It)
		{
			FProperty* Param = *It;
			if (Param && Param->HasAnyPropertyFlags(CPF_Parm))
			{
				Param->InitializeValue_InContainer(Parms);
			}
		}

		TargetActor->ProcessEvent(Func, Parms);

		for (TFieldIterator<FProperty> It(Func); It; ++It)
		{
			FProperty* Param = *It;
			if (Param && Param->HasAnyPropertyFlags(CPF_Parm))
			{
				Param->DestroyValue_InContainer(Parms);
			}
		}
	}
	else
	{
		TargetActor->ProcessEvent(Func, nullptr);
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Successfully invoked function '%s' on actor '%s'."),
		*FunctionName, *TargetActor->GetName());
	return Result;
}

#undef LOCTEXT_NAMESPACE
