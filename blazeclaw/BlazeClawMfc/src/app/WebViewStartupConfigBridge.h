#pragma once

#include "WebView2Availability.h"
#include "../config/ConfigModels.h"

#include <functional>
#include <string>

#ifndef HAVE_WEBVIEW2_HEADER
struct ICoreWebView2;
#endif

namespace blazeclaw::app::webview_startup {

using StartupLogFn = std::function<void(const wchar_t* stage, const std::string& detail)>;

std::wstring BuildRuntimeConfigBootstrapScript(
	const blazeclaw::config::AppConfig& config);
std::wstring BuildRuntimeConfigProbeScript();
std::wstring BuildRuntimeConfigResyncScript();

// True when this translation unit compiled WebView2 bridge injection (not stubs).
bool IsWebViewStartupBridgeCompiled();

bool InjectRuntimeConfigBootstrap(
	ICoreWebView2* webView,
	const blazeclaw::config::AppConfig& config,
	const StartupLogFn& logFn = StartupLogFn());

void ApplyRuntimeConfigBootstrapScript(
	ICoreWebView2* webView,
	const blazeclaw::config::AppConfig& config,
	std::function<void(HRESULT hr)> onComplete);

void EnsureRuntimeConfigAfterNavigation(
	ICoreWebView2* webView,
	const blazeclaw::config::AppConfig& config,
	const StartupLogFn& logFn = StartupLogFn());

} // namespace blazeclaw::app::webview_startup
