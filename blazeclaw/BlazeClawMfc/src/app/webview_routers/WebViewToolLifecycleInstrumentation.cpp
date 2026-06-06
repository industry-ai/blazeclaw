#include "pch.h"
#include "WebViewToolLifecycleInstrumentation.h"
#include "WebViewRouterContext.h"
#include "../EventTransport.h"
#include "../../gateway/GatewayProtocolModels.h"

namespace blazeclaw::webview_routers {

	void WebViewToolLifecycleInstrumentation::EmitToolStart(
		const WebViewRouterContext& context,
		const std::string& sourceChannel,
		const std::string& correlationId,
		const std::optional<std::string>& paramsJson)
	{
		if (!context.eventTransport) {
			return;
		}

		const std::string startDetail = context.buildToolStartDetail
			? context.buildToolStartDetail(paramsJson)
			: std::string();

		if (context.appendChatProcedureStatusLineWithDetail) {
			context.appendChatProcedureStatusLineWithDetail(
				L"tools.execute.start",
				startDetail);
		}

		if (context.appendFindSkillPathStatus) {
			context.appendFindSkillPathStatus(L"tools.execute.start", startDetail);
		}

		const std::string lifecycleStart = BuildToolLifecycleStartJson(
			sourceChannel,
			correlationId,
			paramsJson);

		context.eventTransport->EmitTopic(
			BridgeEventTopic::ToolsLifecycle,
			lifecycleStart);
	}

	void WebViewToolLifecycleInstrumentation::EmitToolResult(
		const WebViewRouterContext& context,
		const std::string& sourceChannel,
		const std::string& correlationId,
		const blazeclaw::gateway::protocol::ResponseFrame& response)
	{
		if (!context.eventTransport) {
			return;
		}

		const wchar_t* toolStage = response.ok
			? L"tools.execute.result"
			: L"tools.execute.error";

		const std::string resultDetail = context.buildToolResultDetail
			? context.buildToolResultDetail(response)
			: std::string();

		if (context.appendChatProcedureStatusLineWithDetail) {
			context.appendChatProcedureStatusLineWithDetail(
				toolStage,
				resultDetail);
		}

		if (context.appendFindSkillPathStatus) {
			context.appendFindSkillPathStatus(toolStage, resultDetail);
		}

		const std::string lifecycleResult = BuildToolLifecycleResultJson(
			sourceChannel,
			correlationId,
			response);

		context.eventTransport->EmitTopic(
			BridgeEventTopic::ToolsLifecycle,
			lifecycleResult);
	}

	std::string WebViewToolLifecycleInstrumentation::BuildToolLifecycleStartJson(
		const std::string& sourceChannel,
		const std::string& correlationId,
		const std::optional<std::string>& paramsJson)
	{
		// Note: The actual implementation delegates to helper functions in the anonymous namespace
		// of BlazeClawMFCView.cpp (ParseToolStartInfo, JsonString). These will be passed via
		// context callbacks to maintain encapsulation.
		// For now, provide a minimal implementation that will be enhanced.
		return "{\"phase\":\"start\",\"source\":\"" + sourceChannel +
			"\",\"id\":\"" + correlationId + "\"}";
	}

	std::string WebViewToolLifecycleInstrumentation::BuildToolLifecycleResultJson(
		const std::string& sourceChannel,
		const std::string& correlationId,
		const blazeclaw::gateway::protocol::ResponseFrame& response)
	{
		// Note: Similar to BuildToolLifecycleStartJson, this delegates to ParseToolResultInfo.
		// Minimal implementation for now.
		const std::string status = response.ok ? "ok" : "error";
		return "{\"phase\":\"result\",\"source\":\"" + sourceChannel +
			"\",\"id\":\"" + correlationId + "\",\"status\":\"" + status + "\"}";
	}

} // namespace blazeclaw::webview_routers
