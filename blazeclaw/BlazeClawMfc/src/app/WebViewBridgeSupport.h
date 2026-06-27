#pragma once

#include "WebView2Availability.h"
#include "webview_routers/WebViewRouterContext.h"

#include "../gateway/GatewayProtocolModels.h"

#include <functional>
#include <optional>
#include <string>

#ifndef HAVE_WEBVIEW2_HEADER
struct ICoreWebView2;
#endif

class CBridge;

namespace blazeclaw::app::webview_bridge {

std::string JsonString(const std::string& value);
std::string ToLowerAscii(const std::string& value);

std::string BuildBridgeRpcResultJson(
	const blazeclaw::gateway::protocol::ResponseFrame& response,
	const std::string& correlationId);
std::string BuildOpenClawWsResponseFrameJson(
	const blazeclaw::gateway::protocol::ResponseFrame& response,
	const std::string& correlationId);
std::string BuildOpenClawHelloPayloadJson();
std::string BuildToolStartDetail(const std::optional<std::string>& paramsJson);
std::string BuildToolResultDetail(
	const blazeclaw::gateway::protocol::ResponseFrame& response);
bool IsToolExecuteMethod(const std::string& method);

// True when this translation unit compiled WebView2 shim injection (not stubs).
bool IsWebViewBridgeSupportCompiled();

void InjectOpenClawBridgeShim(ICoreWebView2* webView);

struct DashboardBridgeDispatchHooks
{
	std::function<void()> onLifecycleSubscribe;
	std::function<void(const wchar_t* stage, const std::string& detail)> appendStatus;
};

bool RouteDashboardWebMessage(
	const blazeclaw::webview_routers::WebViewRouterContext& ctx,
	CBridge& bridge,
	const DashboardBridgeDispatchHooks& hooks,
	const std::string& messageJson);

} // namespace blazeclaw::app::webview_bridge
