#include "app/WebViewStartupConfigBridge.h"
#include "config/ConfigModels.h"

#include <catch2/catch_all.hpp>
#include <string>

TEST_CASE("BuildRuntimeConfigBootstrapScript emits enabled=true", "[webview][agents][bootstrap]") {
	blazeclaw::config::AppConfig config;
	config.agents.controlPlaneEnabled = true;

	const std::wstring script =
		blazeclaw::app::webview_startup::BuildRuntimeConfigBootstrapScript(config);
	const std::string narrow(script.begin(), script.end());

	REQUIRE(narrow.find("enabled:true") != std::string::npos);
	REQUIRE(narrow.find("__BLAZECLAW_RUNTIME_CONFIG__") != std::string::npos);
}

TEST_CASE("BuildRuntimeConfigBootstrapScript emits enabled=false", "[webview][agents][bootstrap]") {
	blazeclaw::config::AppConfig config;
	config.agents.controlPlaneEnabled = false;

	const std::wstring script =
		blazeclaw::app::webview_startup::BuildRuntimeConfigBootstrapScript(config);
	const std::string narrow(script.begin(), script.end());

	REQUIRE(narrow.find("enabled:false") != std::string::npos);
}

TEST_CASE("BuildRuntimeConfigBootstrapScript emits enabled=null when unset", "[webview][agents][bootstrap]") {
	blazeclaw::config::AppConfig config;

	const std::wstring script =
		blazeclaw::app::webview_startup::BuildRuntimeConfigBootstrapScript(config);
	const std::string narrow(script.begin(), script.end());

	REQUIRE(narrow.find("enabled:null") != std::string::npos);
}

TEST_CASE("BuildRuntimeConfigProbeScript reads runtime config shape", "[webview][agents][bootstrap]") {
	const std::wstring script =
		blazeclaw::app::webview_startup::BuildRuntimeConfigProbeScript();
	const std::string narrow(script.begin(), script.end());

	REQUIRE(narrow.find("__BLAZECLAW_RUNTIME_CONFIG__") != std::string::npos);
	REQUIRE(narrow.find("BlazeClawAgentsToggle") != std::string::npos);
	REQUIRE(narrow.find("runtimeConfigAgentsEnabled") != std::string::npos);
}

#if defined(__has_include)
# if __has_include(<WebView2.h>)
TEST_CASE("WebView startup bridge compiles with WebView2 injection enabled", "[webview][agents][bootstrap]") {
	REQUIRE(blazeclaw::app::webview_startup::IsWebViewStartupBridgeCompiled());
}
# endif
#endif
