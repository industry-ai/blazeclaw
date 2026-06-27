#include "pch.h"
#include "WebViewBridgeSupport.h"
#include "WebView2Availability.h"

#include "CMgrMessage.h"
#include "CBridge.h"
#include "webview_routers/WebViewGatewayRpcRouter.h"
#include "webview_routers/WebViewLifecyclePushRouter.h"
#include "webview_routers/WebViewOpenClawShimRouter.h"
#include "../gateway/GatewayJsonUtils.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>

namespace blazeclaw::app::webview_bridge {
namespace {

std::string EscapeJson(const std::string& value)
{
	std::string escaped;
	escaped.reserve(value.size() + 8);
	for (const char ch : value)
	{
		switch (ch)
		{
		case '"':
			escaped += "\\\"";
			break;
		case '\\':
			escaped += "\\\\";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped.push_back(ch);
			break;
		}
	}
	return escaped;
}

struct ToolLifecycleStartInfo
{
	std::string tool = "unknown";
	std::string action;
};

struct ToolLifecycleResultInfo
{
	std::string tool = "unknown";
	std::string status = "ok";
	bool executed = false;
	std::string phase = "result";
	std::string code;
	std::string toolPayloadErrorMessage;
};

ToolLifecycleStartInfo ParseToolStartInfo(const std::optional<std::string>& paramsJson)
{
	ToolLifecycleStartInfo info;
	if (!paramsJson.has_value())
	{
		return info;
	}

	blazeclaw::gateway::json::FindStringField(paramsJson.value(), "tool", info.tool);
	if (info.tool.empty())
	{
		info.tool = "unknown";
	}

	std::string argsRaw;
	if (blazeclaw::gateway::json::FindRawField(paramsJson.value(), "args", argsRaw))
	{
		blazeclaw::gateway::json::FindStringField(argsRaw, "action", info.action);
	}

	return info;
}

ToolLifecycleResultInfo ParseToolResultInfo(
	const blazeclaw::gateway::protocol::ResponseFrame& response)
{
	ToolLifecycleResultInfo info;
	if (!response.ok)
	{
		info.phase = "error";
		info.status = "error";
		if (response.error.has_value())
		{
			info.code = response.error->code;
		}
		return info;
	}

	if (!response.payloadJson.has_value())
	{
		return info;
	}

	const std::string& payload = response.payloadJson.value();
	blazeclaw::gateway::json::FindStringField(payload, "tool", info.tool);
	blazeclaw::gateway::json::FindStringField(payload, "status", info.status);
	blazeclaw::gateway::json::FindBoolField(payload, "executed", info.executed);
	blazeclaw::gateway::json::FindStringField(payload, "errorMessage", info.toolPayloadErrorMessage);

	std::string toolPayloadErrorCode;
	if (!info.executed &&
		blazeclaw::gateway::json::FindStringField(payload, "errorCode", toolPayloadErrorCode) &&
		!toolPayloadErrorCode.empty())
	{
		info.code = toolPayloadErrorCode;
	}

	if (info.tool.empty())
	{
		info.tool = "unknown";
	}
	if (info.status.empty())
	{
		info.status = "ok";
	}

	if (info.status == "needs_approval")
	{
		info.phase = "approval-needed";
	}
	else if (info.status == "cancelled")
	{
		info.phase = "approval-resolved";
	}
	else if (info.status == "ok")
	{
		info.phase = "result";
	}
	else
	{
		info.phase = "error";
	}

	return info;
}

} // namespace

std::string JsonString(const std::string& value)
{
	return std::string("\"") + EscapeJson(value) + "\"";
}

std::string ToLowerAscii(const std::string& value)
{
	std::string lowered = value;
	std::transform(
		lowered.begin(),
		lowered.end(),
		lowered.begin(),
		[](const unsigned char ch)
		{
			return static_cast<char>(std::tolower(ch));
		});
	return lowered;
}

std::string BuildBridgeRpcResultJson(
	const blazeclaw::gateway::protocol::ResponseFrame& response,
	const std::string& correlationId)
{
	std::string json =
		"{\"id\":" +
		JsonString(correlationId) +
		",\"ok\":" +
		(response.ok ? "true" : "false");

	if (response.ok)
	{
		if (response.payloadJson.has_value())
		{
			json += ",\"payload\":" + response.payloadJson.value();
		}
		else
		{
			json += ",\"payload\":null";
		}
	}
	else
	{
		json += ",\"error\":";
		if (response.error.has_value())
		{
			const auto& error = response.error.value();
			json += "{";
			json += "\"code\":" + JsonString(error.code);
			json += ",\"message\":" + JsonString(error.message);
			if (error.detailsJson.has_value())
			{
				json += ",\"details\":" + error.detailsJson.value();
			}
			json += "}";
		}
		else
		{
			json += "{\"code\":\"error_unknown\",\"message\":\"Gateway request failed.\"}";
		}
	}

	json += "}";
	return json;
}

std::string BuildOpenClawWsResponseFrameJson(
	const blazeclaw::gateway::protocol::ResponseFrame& response,
	const std::string& correlationId)
{
	std::string frame =
		"{\"type\":\"res\",\"id\":" +
		JsonString(correlationId) +
		",\"ok\":" +
		(response.ok ? "true" : "false");

	if (response.ok)
	{
		if (response.payloadJson.has_value())
		{
			frame += ",\"payload\":" + response.payloadJson.value();
		}
	}
	else
	{
		frame += ",\"error\":";
		if (response.error.has_value())
		{
			const auto& error = response.error.value();
			frame += "{";
			frame += "\"code\":" + JsonString(error.code);
			frame += ",\"message\":" + JsonString(error.message);
			if (error.detailsJson.has_value())
			{
				frame += ",\"details\":" + error.detailsJson.value();
			}
			frame += "}";
		}
		else
		{
			frame += "{\"code\":\"UNAVAILABLE\",\"message\":\"request failed\"}";
		}
	}

	frame += "}";
	return frame;
}

std::string BuildOpenClawHelloPayloadJson()
{
	return
		"{"
		"\"type\":\"hello-ok\","
		"\"protocol\":3,"
		"\"server\":{\"version\":\"blazeclaw-mfc\",\"connId\":\"webview2-dashboard-bridge\"},"
		"\"features\":{"
		"\"methods\":[\"chat.history\",\"chat.send\",\"chat.abort\",\"chat.events.poll\"],"
		"\"events\":[\"chat\"]"
		"},"
		"\"snapshot\":{},"
		"\"policy\":{\"tickIntervalMs\":1000}"
		"}";
}

bool IsToolExecuteMethod(const std::string& method)
{
	return method == "gateway.tools.call.execute";
}

std::string BuildToolStartDetail(const std::optional<std::string>& paramsJson)
{
	const auto info = ParseToolStartInfo(paramsJson);
	std::string detail = "tool=" + info.tool;
	if (!info.action.empty())
	{
		detail += " action=" + info.action;
	}
	return detail;
}

std::string BuildToolResultDetail(
	const blazeclaw::gateway::protocol::ResponseFrame& response)
{
	const auto info = ParseToolResultInfo(response);
	if (!response.ok && info.phase == "error" && !info.code.empty())
	{
		return "status=error code=" + info.code;
	}

	std::string detail =
		"tool=" + info.tool +
		" status=" + info.status +
		" executed=" + (info.executed ? "true" : "false");

	if (!info.phase.empty())
	{
		detail += " phase=" + info.phase;
	}

	return detail;
}

bool IsWebViewBridgeSupportCompiled()
{
#ifdef HAVE_WEBVIEW2_HEADER
	return true;
#else
	return false;
#endif
}

void InjectOpenClawBridgeShim(ICoreWebView2* webView)
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (webView == nullptr)
	{
		return;
	}

	const wchar_t* shimScript = LR"JS(
(function() {
  if (window.__blazeclawBridgeInjected) return;
  window.__blazeclawBridgeInjected = true;
  if (!window.chrome || !window.chrome.webview) return;
  window.chrome.webview.postMessage({ channel: 'openclaw.ws.shim.ready', phase: 'boot', href: String(window.location && window.location.href ? window.location.href : '') });
  const listeners = { open: [], message: [], close: [], error: [] };
  let activeSocket = null;
  let syntheticUrl = 'ws://127.0.0.1:18789';
  let connectTimer = null;
  function emit(type, evt) { const arr = listeners[type] || []; for (const fn of arr) { try { fn(evt); } catch (_) {} } }
  function scheduleOpen() {
    if (connectTimer) clearTimeout(connectTimer);
    connectTimer = setTimeout(() => {
      if (!activeSocket) return;
      activeSocket.readyState = WebViewGatewaySocket.OPEN;
      window.chrome.webview.postMessage({ channel: 'openclaw.ws.shim.ready', url: syntheticUrl, readyState: activeSocket.readyState });
      emit('open', { type: 'open' });
      window.chrome.webview.postMessage({ channel: 'openclaw.ws.req', frame: { type: 'req', id: 'bridge-connect-challenge', method: 'connect.challenge', params: {} } });
    }, 0);
  }
  class WebViewGatewaySocket {
    constructor(url) {
      activeSocket = this;
      syntheticUrl = typeof url === 'string' && url.length ? url : syntheticUrl;
      this.url = syntheticUrl;
      this.readyState = WebViewGatewaySocket.CONNECTING;
      this.binaryType = 'arraybuffer';
      scheduleOpen();
    }
    addEventListener(type, handler) { if (!listeners[type]) return; listeners[type].push(handler); }
    removeEventListener(type, handler) { if (!listeners[type]) return; const index = listeners[type].indexOf(handler); if (index >= 0) listeners[type].splice(index, 1); }
    send(raw) {
      if (this.readyState !== WebViewGatewaySocket.OPEN) return;
      let frame = null;
      try { frame = JSON.parse(String(raw || '')); } catch (_) {}
      if (!frame || frame.type !== 'req') return;
      window.chrome.webview.postMessage({ channel: 'openclaw.ws.req', frame });
    }
    close(code, reason) {
      this.readyState = WebViewGatewaySocket.CLOSED;
      if (activeSocket === this) { activeSocket = null; }
      emit('close', { type: 'close', code: typeof code === 'number' ? code : 1000, reason: typeof reason === 'string' ? reason : 'closed' });
    }
  }
  WebViewGatewaySocket.CONNECTING = 0;
  WebViewGatewaySocket.OPEN = 1;
  WebViewGatewaySocket.CLOSING = 2;
  WebViewGatewaySocket.CLOSED = 3;
  window.chrome.webview.addEventListener('message', (event) => {
    const msg = event && event.data;
    if (!msg || typeof msg !== 'object') return;
    if (msg.channel === 'openclaw.ws.frame' && msg.frame) { emit('message', { data: JSON.stringify(msg.frame) }); return; }
    if (msg.channel === 'openclaw.ws.close') {
      if (activeSocket) { activeSocket.readyState = WebViewGatewaySocket.CLOSED; activeSocket = null; }
      emit('close', { type: 'close', code: typeof msg.code === 'number' ? msg.code : 1006, reason: typeof msg.reason === 'string' ? msg.reason : 'closed' });
    }
  });
  window.WebSocket = WebViewGatewaySocket;
  window.__OPENCLAW_CONTROL_UI_BASE_PATH__ = '/';
})();
)JS";

	webView->AddScriptToExecuteOnDocumentCreated(shimScript, nullptr);
#else
	UNREFERENCED_PARAMETER(webView);
#endif
}

bool RouteDashboardWebMessage(
	const blazeclaw::webview_routers::WebViewRouterContext& ctx,
	CBridge& bridge,
	const DashboardBridgeDispatchHooks& hooks,
	const std::string& message)
{
	std::string channel;
	if (!blazeclaw::gateway::json::FindStringField(message, "channel", channel))
	{
		CMgrMessage::Instance().DispatchWebChannelMessage("", message);
		return true;
	}

	std::unordered_map<std::string, std::function<bool()>> localHandlers;
	localHandlers.emplace(
		"blazeclaw.gateway.lifecycle.subscribe",
		[&]() -> bool
		{
			if (hooks.onLifecycleSubscribe)
			{
				hooks.onLifecycleSubscribe();
			}
			return true;
		});
	localHandlers.emplace(
		"openclaw.ws.shim.ready",
		[&]() -> bool
		{
			if (hooks.appendStatus)
			{
				hooks.appendStatus(L"runtime.shim.ready", message);
			}
			return true;
		});

	CMgrMessage::Instance().ClearWebChannelHandlers();
	for (auto& entry : localHandlers)
	{
		CMgrMessage::Instance().RegisterWebChannelHandler(
			entry.first,
			[fn = std::move(entry.second)](const std::string&) mutable -> bool
			{
				return fn();
			});
	}
	if (CMgrMessage::Instance().DispatchWebChannelMessage(channel, message))
	{
		return true;
	}

	if (blazeclaw::webview_routers::WebViewLifecyclePushRouter::RouteMessage(ctx, channel, message))
	{
		return true;
	}

	if (blazeclaw::webview_routers::WebViewOpenClawShimRouter::RouteMessage(ctx, channel, message))
	{
		return true;
	}

	if (channel == "blazeclaw.gateway.rpc")
	{
		return blazeclaw::webview_routers::WebViewGatewayRpcRouter::RouteMessage(ctx, channel, message);
	}

	UNREFERENCED_PARAMETER(bridge);
	return false;
}

} // namespace blazeclaw::app::webview_bridge
