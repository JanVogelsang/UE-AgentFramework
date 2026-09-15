// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "SWebBrowser.h"
#include "AIAssistantBridge.generated.h"

DECLARE_DELEGATE_TwoParams(FAIAssistantResponseDelegate, const FString&, bool);

DECLARE_MULTICAST_DELEGATE_TwoParams(FAIAssistantQueryCompletedSignature, const FString&, bool);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAIAssistantQueryCompletedDynamicSignature, const FString&, Response, bool, bSuccess);

UCLASS(BlueprintType)
class AGENTFRAMEWORKACTIONS_API UAIAssistantBridge : public UObject
{
	GENERATED_BODY()

public:
	UAIAssistantBridge();

	/** Binds this object to the active AIAssistant web browser */
	bool InitializeBridge();

	/** Sends a prompt to the assistant and registers a callback for the response */
	bool SendQuery(const FString& InPrompt, FAIAssistantResponseDelegate InCallback);

	/** UFUNCTION that will be invoked from JS context */
	UFUNCTION(BlueprintCallable, Category = "AgentFramework")
	void OnResponseReceived(const FString& Response, bool bSuccess);

	void SetActiveQueryPrompt(const FString& InPrompt) { ActiveQueryPrompt = InPrompt; }
	FString GetActiveQueryPrompt() const { return ActiveQueryPrompt; }

	/** Native C++ delegate for query completion */
	FAIAssistantQueryCompletedSignature OnQueryCompleted;

	/** Blueprint delegate for query completion */
	UPROPERTY(BlueprintAssignable, Category = "AgentFramework|AIAssistant")
	FAIAssistantQueryCompletedDynamicSignature OnQueryCompletedDynamic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|AIAssistant")
	TObjectPtr<class USoundBase> QueryCompletedSound;

private:
	TSharedPtr<SWebBrowser> FindWebBrowserWidget();
	TSharedPtr<SWidget> FindWidgetOfClass(TSharedPtr<SWidget> StartWidget, const FName& ClassName);

	FAIAssistantResponseDelegate CurrentCallback;
	TWeakPtr<SWebBrowser> BoundWebBrowser;
	FString ActiveQueryPrompt;
};

