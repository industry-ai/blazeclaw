#include "pch.h"
#include "DashboardBridgeHost.h"

#include "DashboardWnd.h"
#include "BlazeClawMFCApp.h"
#include "BlazeClawMFCViewTextHelpers.h"
#include "MainFrame.h"
#include "WebViewBridgeSupport.h"

#include "../gateway/GatewayJsonUtils.h"

namespace blazeclaw::app::dashboard_bridge {
namespace {

std::optional<std::wstring> GetEnvValue(const wchar_t* name)
{
	if (name == nullptr || *name == L'\0')
	{
		return std::nullopt;
	}

	size_t valueLength = 0;
	wchar_t* rawValue = nullptr;
	if (_wdupenv_s(&rawValue, &valueLength, name) != 0 || rawValue == nullptr)
	{
		return std::nullopt;
	}

	std::wstring value(rawValue);
	free(rawValue);
	if (value.empty())
	{
		return std::nullopt;
	}

	return value;
}

std::wstring TrimCopy(std::wstring value)
{
	auto isSpace = [](wchar_t ch)
	{
		return ::iswspace(ch) != 0;
	};

	while (!value.empty() && isSpace(value.front()))
	{
		value.erase(value.begin());
	}

	while (!value.empty() && isSpace(value.back()))
	{
		value.pop_back();
	}

	return value;
}

bool ParseEnvBool(const std::string& value, const bool fallback)
{
	const std::string lowered = blazeclaw::app::webview_bridge::ToLowerAscii(value);
	if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
	{
		return true;
	}

	if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
	{
		return false;
	}

	return fallback;
}

std::wstring ToWide(const std::string& value)
{
	if (value.empty())
	{
		return std::wstring();
	}

	const int sizeNeeded = MultiByteToWideChar(
		CP_UTF8,
		0,
		value.c_str(),
		static_cast<int>(value.size()),
		nullptr,
		0);
	if (sizeNeeded <= 0)
	{
		return std::wstring();
	}

	std::wstring output(static_cast<std::size_t>(sizeNeeded), L'\0');
	const int written = MultiByteToWideChar(
		CP_UTF8,
		0,
		value.c_str(),
		static_cast<int>(value.size()),
		output.data(),
		sizeNeeded);
	if (written <= 0)
	{
		return std::wstring();
	}

	return output;
}

} // namespace

void CDashboardBridgeHost::Initialize(
	CDashboardWnd* owner,
	HWND targetHwnd,
	std::function<void(const std::wstring& jsonMessage)> postJson)
{
	Shutdown();

	m_owner = owner;
	m_targetHwnd = targetHwnd;
	m_postJson = std::move(postJson);

	m_eventTransport.SetEmitter(
		[this](const std::string& json)
		{
			if (m_postJson)
			{
				m_postJson(ToWide(json));
			}
		});
	m_eventTransport.SetSessionIdProvider(
		[this]()
		{
			return m_bridgeSessionId;
		});

	bool emitLegacyChannels = true;
	if (const auto value = GetEnvValue(L"BLAZECLAW_BRIDGE_LEGACY_CHANNELS"); value.has_value())
	{
		std::wstring normalized = TrimCopy(value.value());
		for (wchar_t& ch : normalized)
		{
			ch = static_cast<wchar_t>(::towlower(ch));
		}

		if (normalized == L"off" || normalized == L"false" || normalized == L"0")
		{
			emitLegacyChannels = false;
		}
	}
	m_eventTransport.SetEmitLegacyChannels(emitLegacyChannels);

	CBridge::Dependencies bridgeDeps{};
	bridgeDeps.isGatewayRunning =
		[]()
		{
			auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
			return app != nullptr && app->Services().IsRunning();
		};
	bridgeDeps.activeProvider =
		[]()
		{
			auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
			return app != nullptr ? app->Services().ActiveChatProvider() : std::string();
		};
	bridgeDeps.activeModel =
		[]()
		{
			auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
			return app != nullptr ? app->Services().ActiveChatModel() : std::string();
		};
	bridgeDeps.sessionIdProvider =
		[this]()
		{
			return m_bridgeSessionId;
		};
	bridgeDeps.getTargetHwnd =
		[this]()
		{
			return m_targetHwnd;
		};
	bridgeDeps.routeGatewayRequest =
		[](const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
			if (app == nullptr)
			{
				return blazeclaw::gateway::protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = blazeclaw::gateway::protocol::ErrorShape{
						.code = "app_unavailable",
						.message = "Application context unavailable.",
						.detailsJson = std::nullopt,
						.retryable = false,
						.retryAfterMs = std::nullopt,
					},
				};
			}

			return app->RouteGatewayRequest(request);
		};
	bridgeDeps.appendChatStatusStage =
		[this](const wchar_t* stage)
		{
			AppendDashboardStatus(stage);
		};
	bridgeDeps.appendChatStatusDetail =
		[this](const wchar_t* stage, const std::string& detail)
		{
			AppendDashboardStatus(stage, detail);
		};
	bridgeDeps.emitLifecycle =
		[this](
			const wchar_t* state,
			const wchar_t* reason,
			const std::string& provider,
			const std::string& model,
			const std::string& runtimeKind)
		{
			PostBridgeLifecycleEvent(state, reason, provider, model, runtimeKind);
		};
	bridgeDeps.emitWsClose =
		[this](const std::uint16_t code, const char* reason)
		{
			PostOpenClawWsClose(code, reason);
		};
	bridgeDeps.emitPollHealth =
		[this](
			const std::string& state,
			const std::string& reason,
			const std::uint32_t failureCount,
			const std::uint32_t nextPollMs,
			const std::uint64_t sinceLastSuccessMs,
			const std::uint64_t pollTotalCount,
			const std::uint64_t pollEmptyCount,
			const std::uint64_t pollTotalEvents,
			const std::uint64_t pollP95EstimateMs)
		{
			std::string payload =
				"{\"sessionId\":" +
				blazeclaw::app::webview_bridge::JsonString(m_bridgeSessionId) +
				",\"state\":" +
				blazeclaw::app::webview_bridge::JsonString(state) +
				",\"failureCount\":" +
				std::to_string(failureCount) +
				",\"nextPollMs\":" +
				std::to_string(nextPollMs);
			if (!reason.empty())
			{
				payload += ",\"reason\":" + blazeclaw::app::webview_bridge::JsonString(reason);
			}
			if (sinceLastSuccessMs > 0)
			{
				payload += ",\"sinceLastSuccessMs\":" + std::to_string(sinceLastSuccessMs);
			}
			payload += ",\"pollTotalCount\":" + std::to_string(pollTotalCount);
			payload += ",\"pollEmptyCount\":" + std::to_string(pollEmptyCount);
			payload += ",\"pollTotalEvents\":" + std::to_string(pollTotalEvents);
			payload += ",\"pollP95EstimateMs\":" + std::to_string(pollP95EstimateMs);
			payload += "}";
			m_eventTransport.EmitTopic(BridgeEventTopic::PollHealth, payload);
		};
	bridgeDeps.emitPushHealth =
		[this](
			const std::string& state,
			const std::string& reason,
			const std::uint32_t reconnectCount,
			const std::uint64_t pushLagMs,
			const std::uint64_t droppedFrames)
		{
			std::string detail =
				"state=" + state +
				" reconnects=" + std::to_string(reconnectCount) +
				" lagMs=" + std::to_string(pushLagMs) +
				" dropped=" + std::to_string(droppedFrames);
			if (!reason.empty())
			{
				detail += " reason=" + reason;
			}
			AppendDashboardStatus(L"events.push.health", detail);
		};
	bridgeDeps.handleEventsBatch =
		[](const std::string& /*eventsRaw*/)
		{
		};

	CBridge::Config bridgeCfg{};
	bridgeCfg.lifecycleTimerId = kDashboardBridgeLifecycleTimerId;
	bridgeCfg.pollCompletedMessageId = kDashboardBridgePollCompletedMessage;
	bridgeCfg.pushEnabled = false;
	bridgeCfg.pushFallbackPollEnabled = ParseEnvBool(
		blazeclaw::app::view_helpers::ToNarrowUtf8(
			GetEnvValue(L"BLAZECLAW_BRIDGE_PUSH_FALLBACK_POLL_ENABLED").value_or(L"false")),
		false);
	bridgeCfg.pushRecoveryPollEnabled = false;
	m_bridge.Initialize(std::move(bridgeDeps), bridgeCfg);

	if (m_owner != nullptr && m_bridgeTimerId == 0)
	{
		m_bridgeTimerId = m_owner->SetTimer(
			kDashboardBridgeLifecycleTimerId,
			kDashboardBridgeLifecycleTimerMs,
			nullptr);
	}

	m_bridge.ResetLifecycle();
	PumpBridgeLifecycle();
}

void CDashboardBridgeHost::Shutdown()
{
	if (m_owner != nullptr && m_bridgeTimerId != 0)
	{
		m_owner->KillTimer(m_bridgeTimerId);
		m_bridgeTimerId = 0;
	}

	m_postJson = nullptr;
	m_owner = nullptr;
	m_targetHwnd = nullptr;
}

void CDashboardBridgeHost::HandleWebMessageJson(const std::wstring& webMessageJson)
{
	const std::string message =
		blazeclaw::app::view_helpers::ToNarrowUtf8(webMessageJson);
	const auto ctx = BuildRouterContext();

	blazeclaw::app::webview_bridge::DashboardBridgeDispatchHooks hooks;
	hooks.onLifecycleSubscribe =
		[this]()
		{
			m_bridge.ResetLifecycle();
			PumpBridgeLifecycle();
		};
	hooks.appendStatus =
		[this](const wchar_t* stage, const std::string& detail)
		{
			AppendDashboardStatus(stage, detail);
		};

	blazeclaw::app::webview_bridge::RouteDashboardWebMessage(
		ctx,
		m_bridge,
		hooks,
		message);
}

void CDashboardBridgeHost::OnTimer(UINT_PTR timerId)
{
	if (timerId == kDashboardBridgeLifecycleTimerId)
	{
		m_bridge.OnTimerTick(timerId);
	}
}

void CDashboardBridgeHost::OnBridgePollCompleted(WPARAM wParam, LPARAM lParam)
{
	const auto* payload = reinterpret_cast<const CBridgePollCompletionPayload*>(lParam);
	if (payload == nullptr)
	{
		m_bridge.HandlePollCompleted(false, std::nullopt);
		return;
	}

	m_bridge.HandlePollCompleted(payload->ok, payload->payloadJson);
	delete payload;
	UNREFERENCED_PARAMETER(wParam);
}

blazeclaw::webview_routers::WebViewRouterContext CDashboardBridgeHost::BuildRouterContext()
{
	blazeclaw::webview_routers::WebViewRouterContext ctx;
	ctx.bridge = &m_bridge;
	ctx.eventTransport = &m_eventTransport;
	ctx.app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	ctx.viewHwnd = m_targetHwnd;
	ctx.bridgeSessionId = m_bridgeSessionId;

	ctx.appendChatProcedureStatusLine =
		[this](const wchar_t* stage)
		{
			AppendDashboardStatus(stage);
		};
	ctx.appendChatProcedureStatusLineWithDetail =
		[this](const wchar_t* stage, const std::string& detail)
		{
			AppendDashboardStatus(stage, detail);
		};
	ctx.appendFindSkillPathStatus =
		[this](const wchar_t* stage, const std::string& detail)
		{
			AppendDashboardStatus(stage, detail);
		};
	ctx.traceBridgeTraffic =
		[this](const std::string& kind, const std::string& detail)
		{
			AppendDashboardStatus(L"bridge.trace", kind + (detail.empty() ? "" : " " + detail));
		};
	ctx.traceSpeechBridgeOrder = [](const std::string&, const std::string&) {};
	ctx.flushBridgeTraceIfNeeded = []() {};

	ctx.postBridgeMessageJson =
		[this](const std::wstring& json)
		{
			if (m_postJson)
			{
				m_postJson(json);
			}
		};
	ctx.postOpenClawWsFrameJson =
		[this](const std::string& frameJson)
		{
			PostOpenClawWsFrameJson(frameJson);
		};

	ctx.buildOpenClawWsResponseFrameJson =
		[](const blazeclaw::gateway::protocol::ResponseFrame& response,
			const std::string& correlationId) -> std::string
		{
			return blazeclaw::app::webview_bridge::BuildOpenClawWsResponseFrameJson(
				response,
				correlationId);
		};
	ctx.buildOpenClawHelloPayloadJson =
		[]() -> std::string
		{
			return blazeclaw::app::webview_bridge::BuildOpenClawHelloPayloadJson();
		};
	ctx.buildBridgeRpcResultJson =
		[](const blazeclaw::gateway::protocol::ResponseFrame& response,
			const std::string& correlationId) -> std::string
		{
			return blazeclaw::app::webview_bridge::BuildBridgeRpcResultJson(
				response,
				correlationId);
		};

	ctx.buildToolStartDetail =
		[](const std::optional<std::string>& paramsJson) -> std::string
		{
			return blazeclaw::app::webview_bridge::BuildToolStartDetail(paramsJson);
		};
	ctx.buildToolResultDetail =
		[](const blazeclaw::gateway::protocol::ResponseFrame& response) -> std::string
		{
			return blazeclaw::app::webview_bridge::BuildToolResultDetail(response);
		};

	ctx.buildSpeechLifecyclePayloadJson =
		[](const std::string&,
			const std::string&,
			const std::string&,
			const std::string&,
			const std::string&,
			const std::string&,
			std::uint64_t,
			bool,
			const std::string&,
			const std::string&,
			const std::string&) -> std::string
		{
			return {};
		};
	ctx.emitSpeechLifecycleEvent = [](const std::string&) {};
	ctx.extractTerminalRunIds = [](const std::string&) -> std::vector<std::string> { return {}; };
	ctx.reportRunSkillPathsToToolOutput = [](const std::string&) {};
	ctx.isToolExecuteMethod =
		[](const std::string& method) -> bool
		{
			return blazeclaw::app::webview_bridge::IsToolExecuteMethod(method);
		};

	return ctx;
}

void CDashboardBridgeHost::PumpBridgeLifecycle()
{
	m_bridge.PumpLifecycle();
}

void CDashboardBridgeHost::PostBridgeLifecycleEvent(
	const wchar_t* state,
	const wchar_t* reason,
	const std::string& provider,
	const std::string& model,
	const std::string& runtimeKind)
{
	std::string payload =
		"{\"channel\":\"blazeclaw.gateway.lifecycle\",\"sessionId\":" +
		blazeclaw::app::webview_bridge::JsonString(m_bridgeSessionId) +
		",\"state\":" +
		blazeclaw::app::webview_bridge::JsonString(
			state != nullptr
				? blazeclaw::app::view_helpers::ToNarrowUtf8(state)
				: std::string("unknown"));

	if (reason != nullptr && *reason != L'\0')
	{
		payload += ",\"reason\":" +
			blazeclaw::app::webview_bridge::JsonString(
				blazeclaw::app::view_helpers::ToNarrowUtf8(reason));
	}

	if (!provider.empty())
	{
		payload += ",\"provider\":" + blazeclaw::app::webview_bridge::JsonString(provider);
	}

	if (!model.empty())
	{
		payload += ",\"model\":" + blazeclaw::app::webview_bridge::JsonString(model);
	}

	if (!runtimeKind.empty())
	{
		payload += ",\"runtimeKind\":" + blazeclaw::app::webview_bridge::JsonString(runtimeKind);
	}

	payload += "}";
	m_eventTransport.EmitTopic(BridgeEventTopic::Lifecycle, payload);
}

void CDashboardBridgeHost::PostOpenClawWsClose(const std::uint16_t code, const char* reason)
{
	const std::string closeJson =
		"{\"code\":" +
		std::to_string(code) +
		",\"reason\":" +
		blazeclaw::app::webview_bridge::JsonString(reason != nullptr ? reason : "closed") +
		"}";
	m_eventTransport.EmitTopic(BridgeEventTopic::WsClose, closeJson);
}

void CDashboardBridgeHost::PostOpenClawWsFrameJson(const std::string& frameJson)
{
	if (frameJson.empty())
	{
		return;
	}

	m_eventTransport.EmitTopic(
		BridgeEventTopic::WsFrame,
		std::string("{\"frame\":") + frameJson + "}");
}

void CDashboardBridgeHost::AppendDashboardStatus(
	const wchar_t* stage,
	const std::string& detail)
{
	auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
	if (mainFrame == nullptr || stage == nullptr)
	{
		return;
	}

	// Throttle frequent poll-related status lines to avoid UI/log spam.
	// Poll failures are reported each poll cycle with an increasing failure count;
	// show at most one events.poll* line per 10 seconds per stage to keep the
	// dashboard status readable while still surfacing degradations.
	const std::wstring stageW(stage);
	constexpr std::uint64_t kThrottleMs = 10000; // 10s
	static std::unordered_map<std::wstring, std::uint64_t> s_lastLogMs;
	const std::uint64_t now = GetTickCount64();
	bool shouldThrottle = false;
	if (stageW.rfind(L"events.poll", 0) == 0 || stageW.rfind(L"events.push", 0) == 0)
	{
		const auto it = s_lastLogMs.find(stageW);
		if (it != s_lastLogMs.end())
		{
			if ((now - it->second) < kThrottleMs)
			{
				shouldThrottle = true;
			}
		}
	}

	if (!shouldThrottle)
	{
		CString line;
		if (detail.empty())
		{
			line.Format(L"[Dashboard] %s", stage);
		}
		else
		{
			CStringW detailW(CA2W(detail.c_str(), CP_UTF8));
			line.Format(L"[Dashboard] %s - %s", stage, detailW.GetString());
		}

		mainFrame->AddToolStatusLine(line);

		if (stageW.rfind(L"events.poll", 0) == 0 || stageW.rfind(L"events.push", 0) == 0)
		{
			s_lastLogMs[stageW] = now;
		}
	}
}

} // namespace blazeclaw::app::dashboard_bridge
