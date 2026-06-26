#pragma once

#include "../config/ConfigModels.h"

#if defined(__has_include)
# if __has_include(<WebView2.h>)
struct ICoreWebView2;
# endif
#endif

namespace blazeclaw::app::webview_startup {

std::wstring BuildRuntimeConfigBootstrapScript(
	const blazeclaw::config::AppConfig& config);
void InjectRuntimeConfigBootstrap(
	ICoreWebView2* webView,
	const blazeclaw::config::AppConfig& config);

} // namespace blazeclaw::app::webview_startup
