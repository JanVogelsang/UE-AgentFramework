// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AgentFrameworkInterfaces.h"

/**
 * FAgentFrameworkPIEActions
 *
 * Play-In-Editor automation tools:
 *   - start_pie_session: Launch PIE for runtime testing
 *   - simulate_input: Inject key presses during PIE
 *   - stop_pie_session: End the current PIE session
 *
 * Combined with read_message_log (from DiagnosticsActions), the AI can:
 *   1. Build a Blueprint
 *   2. Start PIE
 *   3. Simulate player input (move forward, press interact)
 *   4. Read the Output Log for Accessed None errors
 *   5. Stop PIE and fix the bugs
 *
 * SAFETY:
 *   - PIE is inherently risky (editor crash potential), so rated High risk
 *   - Requires Full Access security mode
 *   - Input simulation is limited to keyboard/gamepad (no mouse movement)
 *   - PIE sessions auto-stop after a configurable timeout
 */
class AGENTFRAMEWORKACTIONS_API FAgentFrameworkPIEActions : public IAgentFrameworkActionExecutor
{
public:
	FAgentFrameworkPIEActions();
	virtual ~FAgentFrameworkPIEActions();

	virtual FName GetActionName() const override;
	virtual FAgentFrameworkActionResult ExecuteAction(const TSharedRef<FJsonObject>& Params) override;
	virtual TArray<FString> GetSupportedToolNames() const override;
	virtual bool ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const override;

private:
	/** Launch Play-In-Editor. */
	FAgentFrameworkActionResult ExecuteStartPIE(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Inject keyboard input during a running PIE session. */
	FAgentFrameworkActionResult ExecuteSimulateInput(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Stop the current PIE session. */
	FAgentFrameworkActionResult ExecuteStopPIE(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Extract visible UI widgets. */
	FAgentFrameworkActionResult ExecuteExtractUIState(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Trigger an interaction on a specific UI element. */
	FAgentFrameworkActionResult ExecuteTriggerUIElement(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Query current world state actors. */
	FAgentFrameworkActionResult ExecuteQueryWorldState(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Broadcast a script delegate on a runtime PIE UUserWidget or child widget. */
	FAgentFrameworkActionResult ExecuteInvokePIEWidgetDelegate(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Query all active runtime UUserWidget instances in PIE. */
	FAgentFrameworkActionResult ExecuteGetActiveRuntimeWidgets(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Execute a console command targeting active PIE world. */
	FAgentFrameworkActionResult ExecuteConsoleCommand(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Call a UFUNCTION on an active PIE actor. */
	FAgentFrameworkActionResult ExecuteCallActorFunction(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result);

	/** Whether PIE is currently running. */
	static bool IsPIERunning();

	/** Cache for raw Slate widgets to trigger them programmatically. */
	TMap<FString, TWeakPtr<SWidget>> CachedSlateWidgets;
};
