#pragma once

#include "webview_routers/WebViewRouterContext.h"

#include "../gateway/GatewayProtocolModels.h"

#include <functional>
#include <optional>
#include <string>

#if defined(__has_include)
# if __has_include(<WebView2.h>)
struct ICoreWebView2;
# endif
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
