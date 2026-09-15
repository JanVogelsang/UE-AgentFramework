// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HttpServerModule.h"
#include "IHttpRouter.h"
#include "AgentFrameworkActionRouter.h"

class FAgentFrameworkHttpServer
{
public:
	static void Start();
	static void Stop();

	/**
	 * Port the server is currently bound to, or will bind to once Start() has resolved it.
	 * Only meaningful after Start(); before that it holds DefaultPort.
	 */
	static uint32 GetPort() { return Port; }

	/**
	 * Rejects port 0, the well-known range below 1024, and anything past the 16-bit ceiling.
	 * Public so the port-resolution rules can be tested without exercising a real bind.
	 */
	static bool IsValidPort(uint32 InPort);

	/** Port used when nothing overrides it. Every existing tool and script assumes this value. */
	static constexpr uint32 DefaultPort = 18777;

private:
	static bool HandleExecuteToolRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	static bool HandleListToolsRequest(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	static void RegisterAllExecutors(TSharedRef<FAgentFrameworkActionRouter> InRouter);

	/**
	 * Resolves the listen port. Precedence: -AgentFrameworkPort= on the command line, then the
	 * AGENTFRAMEWORK_HTTP_PORT environment variable, then DefaultPort. Anything unusable falls
	 * through to the next source with a warning rather than failing the launch.
	 */
	static uint32 ResolvePort();

	/**
	 * Absolute path of the endpoint file this editor publishes. It lives under the project's
	 * Saved directory, so the directory it sits in is what identifies the project - a worktree
	 * publishes its own, separate from the main checkout's.
	 */
	static FString GetEndpointFilePath();

	/**
	 * Publishes the bound port. -AgentFrameworkPort= tells this server where to listen but told
	 * no client where to look, so the port had to be repeated to the bridge out of band, before
	 * the client process even started. Writing it here lets a client discover the editor serving
	 * its own project instead. Called only after a successful bind.
	 */
	static void WriteEndpointFile();

	/** Removes the published endpoint so a dead editor stops advertising a port nobody serves. */
	static void RemoveEndpointFile();

	static TSharedPtr<FAgentFrameworkActionRouter> ActionRouter;
	static uint32 Port;
};
