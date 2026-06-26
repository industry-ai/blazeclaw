#include "pch.h"
#include "WebViewStartupConfigBridge.h"

#if defined(__has_include)
# if __has_include(<WebView2.h>)
#  include <WebView2.h>
# endif
#endif

namespace blazeclaw::app::webview_startup {
namespace {

std::wstring AgentsEnabledLiteral(const blazeclaw::config::AppConfig& config)
{
	if (!config.agents.controlPlaneEnabled.has_value())
	{
		return L"null";
	}

	return config.agents.controlPlaneEnabled.value() ? L"true" : L"false";
}

} // namespace

std::wstring BuildRuntimeConfigBootstrapScript(
	const blazeclaw::config::AppConfig& config)
{
	const std::wstring enabledLiteral = AgentsEnabledLiteral(config);
	return
		L"(function(){"
		L"window.__BLAZECLAW_RUNTIME_CONFIG__=window.__BLAZECLAW_RUNTIME_CONFIG__||{};"
		L"window.__BLAZECLAW_RUNTIME_CONFIG__.agents={enabled:" +
		enabledLiteral +
		L"};"
		L"})();";
}

void InjectRuntimeConfigBootstrap(
	ICoreWebView2* webView,
	const blazeclaw::config::AppConfig& config)
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (webView == nullptr)
	{
		return;
	}

	const std::wstring script = BuildRuntimeConfigBootstrapScript(config);
	webView->AddScriptToExecuteOnDocumentCreated(script.c_str(), nullptr);
#else
	UNREFERENCED_PARAMETER(webView);
	UNREFERENCED_PARAMETER(config);
#endif
}

} // namespace blazeclaw::app::webview_startup
