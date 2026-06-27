#include "pch.h"
#include "WebViewStartupConfigBridge.h"
#include "WebView2Availability.h"

#include "../gateway/GatewayJsonUtils.h"

#include <wrl/client.h>
#include <wrl/event.h>

namespace blazeclaw::app::webview_startup {
namespace {

using Microsoft::WRL::Callback;

std::wstring AgentsEnabledLiteral(const blazeclaw::config::AppConfig& config)
{
	if (!config.agents.controlPlaneEnabled.has_value())
	{
		return L"null";
	}

	return config.agents.controlPlaneEnabled.value() ? L"true" : L"false";
}

void LogStage(
	const StartupLogFn& logFn,
	const wchar_t* stage,
	const std::string& detail)
{
	if (logFn)
	{
		logFn(stage, detail);
	}
}

std::string NarrowUtf8(const std::wstring& value)
{
	if (value.empty())
	{
		return {};
	}

	const int sizeNeeded = WideCharToMultiByte(
		CP_UTF8,
		0,
		value.c_str(),
		static_cast<int>(value.size()),
		nullptr,
		0,
		nullptr,
		nullptr);
	if (sizeNeeded <= 0)
	{
		return {};
	}

	std::string output(sizeNeeded, '\0');
	WideCharToMultiByte(
		CP_UTF8,
		0,
		value.c_str(),
		static_cast<int>(value.size()),
		output.data(),
		sizeNeeded,
		nullptr,
		nullptr);
	return output;
}

std::string FormatHresult(const HRESULT hr)
{
	char buffer[32]{};
	sprintf_s(buffer, "0x%08X", static_cast<unsigned>(hr));
	return buffer;
}

struct RuntimeConfigProbe
{
	bool hasRuntimeConfigAgentsEnabled = false;
	bool runtimeConfigAgentsEnabled = false;
	bool runtimeConfigAgentsNull = false;
	bool resolved = false;
	std::string source = "unknown";
	bool modulePresent = false;
	bool resynced = false;
	std::string resyncReason;
};

bool ParseRuntimeConfigProbeJson(const std::string& json, RuntimeConfigProbe& outProbe)
{
	if (json.empty())
	{
		return false;
	}

	std::string rawEnabled;
	if (blazeclaw::gateway::json::FindRawField(
		json,
		"runtimeConfigAgentsEnabled",
		rawEnabled))
	{
		outProbe.hasRuntimeConfigAgentsEnabled = true;
		const std::string trimmed = blazeclaw::gateway::json::Trim(rawEnabled);
		if (trimmed == "true")
		{
			outProbe.runtimeConfigAgentsEnabled = true;
		}
		else if (trimmed == "false")
		{
			outProbe.runtimeConfigAgentsEnabled = false;
		}
		else if (trimmed == "null")
		{
			outProbe.runtimeConfigAgentsNull = true;
		}
	}

	bool resolved = false;
	if (blazeclaw::gateway::json::FindBoolField(json, "resolved", resolved))
	{
		outProbe.resolved = resolved;
	}

	std::string source;
	if (blazeclaw::gateway::json::FindStringField(json, "source", source))
	{
		outProbe.source = source;
	}

	bool modulePresent = false;
	if (blazeclaw::gateway::json::FindBoolField(json, "modulePresent", modulePresent))
	{
		outProbe.modulePresent = modulePresent;
	}

	bool resynced = false;
	if (blazeclaw::gateway::json::FindBoolField(json, "resynced", resynced))
	{
		outProbe.resynced = resynced;
	}

	std::string resyncReason;
	if (blazeclaw::gateway::json::FindStringField(json, "reason", resyncReason))
	{
		outProbe.resyncReason = resyncReason;
	}

	return true;
}

std::string DescribeRuntimeConfigAgents(const RuntimeConfigProbe& probe)
{
	if (!probe.hasRuntimeConfigAgentsEnabled || probe.runtimeConfigAgentsNull)
	{
		return "unset";
	}

	return probe.runtimeConfigAgentsEnabled ? "true" : "false";
}

void LogRuntimeConfigProbe(
	const StartupLogFn& logFn,
	const RuntimeConfigProbe& probe,
	const char* fallbackTag)
{
	LogStage(
		logFn,
		L"startup.webview.runtimeConfig.agents",
		std::string("enabled=") + DescribeRuntimeConfigAgents(probe));

	std::string toggleDetail = std::string("resolved=") +
		(probe.resolved ? "true" : "false") +
		" source=" + probe.source +
		" modulePresent=" + (probe.modulePresent ? "true" : "false");
	if (fallbackTag != nullptr && fallbackTag[0] != '\0')
	{
		toggleDetail += " fallback=";
		toggleDetail += fallbackTag;
	}
	if (probe.resynced)
	{
		toggleDetail += " resynced=true";
	}
	if (!probe.resyncReason.empty())
	{
		toggleDetail += " resyncReason=" + probe.resyncReason;
	}

	LogStage(logFn, L"startup.webview.agentsToggle", toggleDetail);
}

bool RuntimeConfigProbeNeedsFix(
	const RuntimeConfigProbe& probe,
	const blazeclaw::config::AppConfig& config)
{
	if (!config.agents.controlPlaneEnabled.has_value())
	{
		return false;
	}

	const bool expected = config.agents.controlPlaneEnabled.value();
	if (expected)
	{
		return !(probe.hasRuntimeConfigAgentsEnabled &&
			!probe.runtimeConfigAgentsNull &&
			probe.runtimeConfigAgentsEnabled);
	}

	return !(probe.hasRuntimeConfigAgentsEnabled &&
		!probe.runtimeConfigAgentsNull &&
		!probe.runtimeConfigAgentsEnabled);
}

#ifdef HAVE_WEBVIEW2_HEADER
void ExecuteScriptAsync(
	ICoreWebView2* webView,
	const std::wstring& script,
	std::function<void(HRESULT hr, const std::string& resultJson)> onComplete)
{
	if (webView == nullptr)
	{
		if (onComplete)
		{
			onComplete(E_POINTER, std::string());
		}
		return;
	}

	webView->ExecuteScript(
		script.c_str(),
		Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
			[onComplete = std::move(onComplete)](
				HRESULT errorCode,
				LPCWSTR resultObjectAsJson) -> HRESULT
			{
				std::string resultJson;
				if (SUCCEEDED(errorCode) && resultObjectAsJson != nullptr)
				{
					resultJson = NarrowUtf8(resultObjectAsJson);
				}

				if (onComplete)
				{
					onComplete(errorCode, resultJson);
				}

				return S_OK;
			}).Get());
}
#endif

} // namespace

bool IsWebViewStartupBridgeCompiled()
{
#ifdef HAVE_WEBVIEW2_HEADER
	return true;
#else
	return false;
#endif
}

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

std::wstring BuildRuntimeConfigProbeScript()
{
	return
		L"(function(){"
		L"function readConfigEnabled(){"
		L"var runtime=window.__BLAZECLAW_RUNTIME_CONFIG__;"
		L"if(!runtime||!runtime.agents||typeof runtime.agents!=='object'){return null;}"
		L"var value=runtime.agents.enabled;"
		L"return value===true||value===false?value:null;"
		L"}"
		L"var configEnabled=readConfigEnabled();"
		L"var trace=window.BlazeClawAgentsToggle&&typeof window.BlazeClawAgentsToggle.resolveAgentsEnabled==='function'"
		L"?window.BlazeClawAgentsToggle.resolveAgentsEnabled()"
		L":{resolved:false,source:'missing-module',config:configEnabled};"
		L"return {"
		L"runtimeConfigAgentsEnabled:configEnabled,"
		L"resolved:trace.resolved===true,"
		L"source:String(trace.source||'unknown'),"
		L"modulePresent:!!window.BlazeClawAgentsToggle"
		L"};"
		L"})();";
}

std::wstring BuildRuntimeConfigResyncScript()
{
	return
		L"(function(){"
		L"if(typeof window.__BLAZECLAW_RESYNC_AGENTS_CONTROL_PLANE__==='function'){"
		L"return window.__BLAZECLAW_RESYNC_AGENTS_CONTROL_PLANE__();"
		L"}"
		L"var trace=window.BlazeClawAgentsToggle&&typeof window.BlazeClawAgentsToggle.resolveAgentsEnabled==='function'"
		L"?window.BlazeClawAgentsToggle.resolveAgentsEnabled()"
		L":{resolved:false,source:'missing-module'};"
		L"return {resynced:false,reason:'hook-missing',resolved:trace.resolved===true,source:String(trace.source||'unknown')};"
		L"})();";
}

bool InjectRuntimeConfigBootstrap(
	ICoreWebView2* webView,
	const blazeclaw::config::AppConfig& config,
	const StartupLogFn& logFn)
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (webView == nullptr)
	{
		LogStage(logFn, L"startup.webview.runtimeConfig.inject", "skipped=null-webview");
		return false;
	}

	const std::wstring script = BuildRuntimeConfigBootstrapScript(config);
	const HRESULT hr = webView->AddScriptToExecuteOnDocumentCreated(
		script.c_str(),
		Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
			[logFn](HRESULT errorCode, LPCWSTR /*uniqueId*/) -> HRESULT
			{
				if (FAILED(errorCode))
				{
					LogStage(
						logFn,
						L"startup.webview.runtimeConfig.inject",
						std::string("documentCreated=failed hr=") + FormatHresult(errorCode));
				}
				else
				{
					LogStage(
						logFn,
						L"startup.webview.runtimeConfig.inject",
						"documentCreated=registered");
				}
				return S_OK;
			}).Get());

	if (FAILED(hr))
	{
		LogStage(
			logFn,
			L"startup.webview.runtimeConfig.inject",
			std::string("documentCreated=registerFailed hr=") + FormatHresult(hr));
		return false;
	}

	return true;
#else
	UNREFERENCED_PARAMETER(webView);
	UNREFERENCED_PARAMETER(config);
	UNREFERENCED_PARAMETER(logFn);
	return false;
#endif
}

void ApplyRuntimeConfigBootstrapScript(
	ICoreWebView2* webView,
	const blazeclaw::config::AppConfig& config,
	std::function<void(HRESULT hr)> onComplete)
{
#ifdef HAVE_WEBVIEW2_HEADER
	const std::wstring script = BuildRuntimeConfigBootstrapScript(config);
	ExecuteScriptAsync(
		webView,
		script,
		[onComplete = std::move(onComplete)](HRESULT hr, const std::string&) mutable
		{
			if (onComplete)
			{
				onComplete(hr);
			}
		});
#else
	UNREFERENCED_PARAMETER(webView);
	UNREFERENCED_PARAMETER(config);
	if (onComplete)
	{
		onComplete(E_NOTIMPL);
	}
#endif
}

void EnsureRuntimeConfigAfterNavigation(
	ICoreWebView2* webView,
	const blazeclaw::config::AppConfig& config,
	const StartupLogFn& logFn)
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (webView == nullptr)
	{
		LogStage(logFn, L"startup.webview.runtimeConfig.verify", "skipped=null-webview");
		return;
	}

	const auto runProbe = std::make_shared<std::function<void(bool)>>();
	*runProbe = [webView, config, logFn, runProbe](const bool appliedFallback)
	{
		ExecuteScriptAsync(
			webView,
			BuildRuntimeConfigProbeScript(),
			[webView, config, logFn, runProbe, appliedFallback](
				HRESULT probeHr,
				const std::string& probeJson)
			{
				if (FAILED(probeHr))
				{
					LogStage(
						logFn,
						L"startup.webview.runtimeConfig.verify",
						std::string("probe=failed hr=") + FormatHresult(probeHr));
					return;
				}

				RuntimeConfigProbe probe;
				ParseRuntimeConfigProbeJson(probeJson, probe);

				if (!appliedFallback && RuntimeConfigProbeNeedsFix(probe, config))
				{
					LogStage(
						logFn,
						L"startup.webview.runtimeConfig.verify",
						std::string("probe=mismatch enabled=") +
							DescribeRuntimeConfigAgents(probe) +
							" applyingExecuteScriptFallback=true");

					ApplyRuntimeConfigBootstrapScript(
						webView,
						config,
						[webView, config, logFn, runProbe](HRESULT bootstrapHr)
						{
							if (FAILED(bootstrapHr))
							{
								LogStage(
									logFn,
									L"startup.webview.runtimeConfig.verify",
									std::string("fallback=failed hr=") +
										FormatHresult(bootstrapHr));
								return;
							}

							ExecuteScriptAsync(
								webView,
								BuildRuntimeConfigResyncScript(),
								[webView, config, logFn, runProbe](
									HRESULT /*resyncHr*/,
									const std::string& /*resyncJson*/)
								{
									(*runProbe)(true);
								});
						});
					return;
				}

				LogRuntimeConfigProbe(
					logFn,
					probe,
					appliedFallback ? "executeScript" : "");
			});
	};

	(*runProbe)(false);
#else
	UNREFERENCED_PARAMETER(webView);
	UNREFERENCED_PARAMETER(config);
	UNREFERENCED_PARAMETER(logFn);
#endif
}

} // namespace blazeclaw::app::webview_startup
