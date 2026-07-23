#include "pch.h"
#include "framework.h"
#include "BlazeClawAgentChatView.h"
#include "BlazeClawMfcApp.h"
#include "TcpReceiverWnd.h"
#include "MainFrame.h"
#include "CChatRoomBridge.h"
#include "Client.h"
#include "CNetwork_c.h"
#include "agent-chat/AgentChatEvent.h"
#include "../agentchat/AgentChatEventPayload.h"
#include "../chat/shared/ChatSharedContractAdapters.h"
#include "../chat/shared/ChatStateTelemetryConsolidation.h"

#include <Shlwapi.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <sstream>
#include <deque>
#include <mutex>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#ifdef BLAZECLAW_AGENTCHATVIEW_WEBVIEW2

using namespace Microsoft::WRL;

constexpr UINT WM_AGENTCHAT_WEBMESSAGE_RECEIVED = WM_USER + 210;
constexpr LPCWSTR AGENTCHAT_HOST_NAME = L"blazeclaw-agentchat.localhost";
constexpr LPCWSTR AGENTCHAT_INDEX_FILE = L"index.html";

namespace
{
	static_assert(
		std::is_base_of_v<
			blazeclaw::chat::shared::IChatRequestOrchestrator,
			blazeclaw::chat::shared::LambdaChatRequestOrchestrator>,
		"LambdaChatRequestOrchestrator must implement IChatRequestOrchestrator");

	static_assert(
		std::is_base_of_v<
			blazeclaw::chat::shared::IChatStreamEventNormalizer,
			blazeclaw::chat::shared::ConformantChatStreamEventNormalizer>,
		"ConformantChatStreamEventNormalizer must implement IChatStreamEventNormalizer");

	static_assert(
		std::is_base_of_v<
			blazeclaw::chat::shared::IChatSessionStateStore,
			blazeclaw::chat::shared::InMemoryChatSessionStateStore>,
		"InMemoryChatSessionStateStore must implement IChatSessionStateStore");

	static_assert(
		std::is_base_of_v<
			blazeclaw::chat::shared::IChatTelemetryHooks,
			blazeclaw::chat::shared::NullChatTelemetryHooks>,
		"NullChatTelemetryHooks must implement IChatTelemetryHooks");

	blazeclaw::chat::shared::SharedChatDiagnosticsCollector& AgentChatDiagnostics()
	{
		static blazeclaw::chat::shared::SharedChatDiagnosticsCollector collector;
		return collector;
	}

	blazeclaw::chat::shared::StreamParityValidator& AgentChatParityValidator()
	{
		static blazeclaw::chat::shared::StreamParityValidator validator;
		return validator;
	}

	//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	// 2026/06/28, jicheng, add dual mode support for agent chat bridge
	std::string WideToUtf8(const std::wstring& value)
	{
		if (value.empty())
		{
			return {};
		}

		const int required = WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (required <= 0)
		{
			return {};
		}

		std::string utf8(required, '\0');
		WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			utf8.data(),
			required,
			nullptr,
			nullptr);
		return utf8;
	}

	std::wstring Utf8ToWide(const std::string& value)
	{
		if (value.empty())
		{
			return {};
		}

		const int required = MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0);
		if (required <= 0)
		{
			return {};
		}

		std::wstring wide(required, L'\0');
		MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			wide.data(),
			required);
		return wide;
	}

	std::wstring EscapeJsSingleQuotedString(const std::wstring& value)
	{
		std::wstring out;
		out.reserve(value.size());
		for (wchar_t ch : value)
		{
			if (ch == L'\'')
			{
				out.append(L"\\'");
			}
			else if (ch == L'\\')
			{
				out.append(L"\\\\");
			}
			else
			{
				out.push_back(ch);
			}
		}
		return out;
	}
	//-------------------------------------------------------------------

	bool IsProcessStillActive(const PROCESS_INFORMATION& processInfo)
	{
		if (processInfo.hProcess == nullptr)
		{
			return false;
		}

		DWORD exitCode = 0;
		return GetExitCodeProcess(processInfo.hProcess, &exitCode) &&
			exitCode == STILL_ACTIVE;
	}
}

IMPLEMENT_DYNCREATE(CBlazeClawAgentChatView, CView)

BEGIN_MESSAGE_MAP(CBlazeClawAgentChatView, CView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_MESSAGE(WM_AGENTCHAT_WEBMESSAGE_RECEIVED, &CBlazeClawAgentChatView::OnWebMessageReceived)
	ON_MESSAGE(WM_CHATROOM_EMIT_TO_WEB, &CBlazeClawAgentChatView::OnChatroomEmitToWeb)
END_MESSAGE_MAP()

CBlazeClawAgentChatView::CBlazeClawAgentChatView() noexcept
	: m_webAssetsPath()
	, m_hasInjectedAuth(false)
{
}

CBlazeClawAgentChatView::~CBlazeClawAgentChatView()
{
	StopNodeServer();
}

#ifdef _DEBUG
void CBlazeClawAgentChatView::AssertValid() const
{
	CView::AssertValid();
}

void CBlazeClawAgentChatView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}
#endif

BOOL CBlazeClawAgentChatView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CView::PreCreateWindow(cs);
}

int CBlazeClawAgentChatView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CView::OnCreate(lpCreateStruct) == -1)
	{
		return -1;
	}

	//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	// 2026/06/28, jicheng, add dual mode support for agent chat bridge
	//StartNodeServer();

	InitChatRoomBridge();
	StartConfiguredRuntime();
	//-------------------------------------------------------------------

	if (!InitWebView())
	{
		TRACE0("CBlazeClawAgentChatView: Failed to initialize WebView2\n");
	}

	return 0;
}

void CBlazeClawAgentChatView::OnInitialUpdate()
{
	CView::OnInitialUpdate();
}

void CBlazeClawAgentChatView::OnSize(UINT nType, int /*cx*/, int /*cy*/)
{
	CView::OnSize(nType, 0, 0);

	if (m_webViewController != nullptr)
	{
		CRect rc;
		GetClientRect(&rc);
		m_webViewController->put_Bounds(rc);
	}
}

void CBlazeClawAgentChatView::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == 1 && m_webViewController != nullptr)
	{
		KillTimer(1);
		CRect rc;
		GetClientRect(&rc);
		TRACE("CBlazeClawAgentChatView: OnTimer fixing bounds (%d,%d,%d,%d)\n",
			rc.left, rc.top, rc.right, rc.bottom);
		m_webViewController->put_Bounds(rc);
	}

	CView::OnTimer(nIDEvent);
}

void CBlazeClawAgentChatView::OnDraw(CDC* /*pDC*/)
{
}

BOOL CBlazeClawAgentChatView::OnEraseBkgnd(CDC* /*pDC*/)
{
	return FALSE;
}

void CBlazeClawAgentChatView::OnDestroy()
{
	//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	// 2026/06/28, jicheng, add dual mode support for agent chat bridge
	StopNativeRuntime();
	//-------------------------------------------------------------------
	StopNodeServer();

	if (m_webView != nullptr)
	{
		m_webView->remove_WebMessageReceived(m_webMessageToken);
		m_webView = nullptr;
	}
	m_webViewController = nullptr;

	CView::OnDestroy();
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 2026/06/28, jicheng, add dual mode support for agent chat bridge
blazeclaw::config::AgentChatRuntimeMode CBlazeClawAgentChatView::ResolveRuntimeMode() const
{
	auto* app = static_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		return blazeclaw::config::AgentChatRuntimeMode::Auto;
	}

	const std::wstring mode = app->Config().agentChatRuntime.mode;
	if (_wcsicmp(mode.c_str(), L"legacy") == 0)
	{
		return blazeclaw::config::AgentChatRuntimeMode::Legacy;
	}

	if (_wcsicmp(mode.c_str(), L"native") == 0)
	{
		return blazeclaw::config::AgentChatRuntimeMode::Native;
	}

	return blazeclaw::config::AgentChatRuntimeMode::Auto;
}

/*
 *	start the native agent chat bridge + agent chat native runner
 */
bool CBlazeClawAgentChatView::StartNativeRuntime()
{
	m_nativeRuntimeStarted = false;
	m_nativeBridgeHostStarted = false;
	m_nativeRunnerStarted = false;
	m_nativeModeDegraded = false;
	m_nativeHttpListenerStarted = false;
	m_nativeHttpListenerPort = 0;
	m_nativeBridgeHost.reset();
	m_nativeRunner.reset();

	auto* app = static_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		TRACE("CBlazeClawAgentChatView: Native runtime unavailable because app context is null\n");
		return false;
	}

	const auto& runtime = app->Config().agentChatRuntime;
	blazeclaw::agentchat::AgentChatBridgeConfig bridgeConfig;
	bridgeConfig.enabled = true;
	bridgeConfig.mode = "native";
	bridgeConfig.enableHttpListener = true;
	bridgeConfig.enableHttpPushIngress = runtime.httpPushIngress;
	bridgeConfig.enableGatewayRouting = true;
	bridgeConfig.enablePushTransport = true;
	bridgeConfig.enableUiInProcessAgentPath = runtime.uiInProcessAgentPath;
	bridgeConfig.allowNonLoopbackHttpBind = runtime.allowNonLoopbackHttpBind;
	bridgeConfig.compatibilityOpenClawAliases = runtime.enableOpenClawAliases;
	bridgeConfig.bindAddress = WideToUtf8(runtime.bindAddress);
	if (bridgeConfig.bindAddress.empty())
	{
		bridgeConfig.bindAddress = "127.0.0.1";
	}
	bridgeConfig.port = runtime.port;
	bridgeConfig.pushChatHost = WideToUtf8(runtime.pushChatHost);
	bridgeConfig.pushChatPort = runtime.pushChatPort;
	bridgeConfig.pushTimeoutMs = runtime.pushTimeoutMs;
	if (!runtime.stateRoot.empty())
	{
		bridgeConfig.stateRoot = std::filesystem::path(runtime.stateRoot);
	}
	if (!runtime.legacyStateRoot.empty())
	{
		bridgeConfig.legacyStateRoot = std::filesystem::path(runtime.legacyStateRoot);
	}
	bridgeConfig.legacyStateMigrationEnabled = runtime.legacyStateMigrationEnabled;

	auto bridgeHost = std::make_unique<blazeclaw::agentchat::AgentChatBridgeHost>();
	auto orchestratorAdapter = std::make_shared<blazeclaw::agentchat::CallbackAgentChatOrchestratorAdapter>();
	const blazeclaw::chat::shared::LambdaChatRequestOrchestrator requestOrchestrator(
		[app](const blazeclaw::gateway::protocol::RequestFrame& request)
			-> std::optional<blazeclaw::gateway::protocol::ResponseFrame>
		{
			if (app == nullptr)
			{
				return std::nullopt;
			}
			return std::optional<blazeclaw::gateway::protocol::ResponseFrame>(
				app->RouteGatewayRequest(request));
		},
		blazeclaw::chat::shared::LambdaChatRequestOrchestrator::UnavailableResponse{
			.code = "app_unavailable",
			.message = "Application context unavailable.",
		});
	orchestratorAdapter->SetRouter(
		[requestOrchestrator](const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			return requestOrchestrator.Route(request);
		});
	bridgeHost->SetOrchestratorAdapter(orchestratorAdapter);
	bool httpListenerFallbackApplied = false;
	if (!bridgeHost->Initialize(bridgeConfig))
	{
		TRACE(
			"CBlazeClawAgentChatView: Failed to initialize native bridge host with HTTP listener (%s:%u). Trying in-process-only fallback.\n",
			bridgeConfig.bindAddress.c_str(),
			bridgeConfig.port);

		blazeclaw::agentchat::AgentChatBridgeConfig inProcessOnlyConfig = bridgeConfig;
		inProcessOnlyConfig.enableHttpListener = false;

		auto inProcessOnlyBridgeHost = std::make_unique<blazeclaw::agentchat::AgentChatBridgeHost>();
		inProcessOnlyBridgeHost->SetOrchestratorAdapter(orchestratorAdapter);
		if (!inProcessOnlyBridgeHost->Initialize(inProcessOnlyConfig))
		{
			TRACE(
				"CBlazeClawAgentChatView: Failed to initialize native bridge host in in-process-only mode\n");
			return false;
		}

		bridgeHost = std::move(inProcessOnlyBridgeHost);
		httpListenerFallbackApplied = true;
	}
	m_nativeBridgeHostStarted = true;
	m_nativeHttpListenerStarted = !httpListenerFallbackApplied;
	m_nativeHttpListenerPort = m_nativeHttpListenerStarted ? bridgeConfig.port : 0;
	if (httpListenerFallbackApplied)
	{
		m_nativeModeDegraded = true;
		TRACE(
			"CBlazeClawAgentChatView: Native bridge host started in in-process-only mode (HTTP listener disabled)\n");
	}

	auto nativeRunner = std::make_unique<blazeclaw::agentchat::AgentChatNativeRunner>();
	nativeRunner->SetChatEndpoint(
		WideToUtf8(runtime.runnerChatHost),
		runtime.runnerChatPort);
	nativeRunner->SetOrchestratorAdapter(orchestratorAdapter);
	if (!nativeRunner->Initialize())
	{
		m_nativeBridgeHost = std::move(bridgeHost);
		m_nativeRunner.reset();
		m_nativeRunnerStarted = false;
		m_nativeModeDegraded = true;
		m_nativeRuntimeStarted = true;
		TRACE(
			"CBlazeClawAgentChatView: Native runtime started in degraded mode (bridge on, runner off, httpListenerFallback=%s). bind=%s:%u\n",
			httpListenerFallbackApplied ? "true" : "false",
			bridgeConfig.bindAddress.c_str(),
			bridgeConfig.port);
		return true;
	}

	m_nativeBridgeHost = std::move(bridgeHost);
	m_nativeRunner = std::move(nativeRunner);
	m_nativeRunnerStarted = true;
	m_nativeModeDegraded = httpListenerFallbackApplied;
	m_nativeRuntimeStarted = true;
	TRACE(
		"CBlazeClawAgentChatView: Native runtime started (aliases=%s, bind=%s:%u, httpListenerFallback=%s)\n",
		bridgeConfig.compatibilityOpenClawAliases ? "true" : "false",
		WideToUtf8(runtime.bindAddress).c_str(),
		runtime.port,
		httpListenerFallbackApplied ? "true" : "false");
	return true;
}

void CBlazeClawAgentChatView::StopNativeRuntime()
{
	if (m_nativeRunner)
	{
		m_nativeRunner->Shutdown();
		m_nativeRunner.reset();
	}

	if (m_nativeBridgeHost)
	{
		m_nativeBridgeHost->Shutdown();
		m_nativeBridgeHost.reset();
	}

	m_nativeRuntimeStarted = false;
	m_nativeBridgeHostStarted = false;
	m_nativeRunnerStarted = false;
	m_nativeModeDegraded = false;
	m_nativeHttpListenerStarted = false;
	m_nativeHttpListenerPort = 0;
}

void CBlazeClawAgentChatView::InitChatRoomBridge()
{
	auto transport = std::shared_ptr<blazeclaw::irc::ITransport>(
		&blazeclaw::irc::CIrcChatTransport::Instance(),
		[](blazeclaw::irc::ITransport*) {});

	blazeclaw::irc::ChatRoomBridgeDependencies deps;
	deps.transport = transport;
	deps.get_current_session_id = []() {
		return std::to_string(CClient::Instance().GetSessionId());
	};
	deps.get_current_nickname = []() {
		auto info = CClient::Instance().GetTokenInfo();
		return info.name.empty() ? info.phone : info.name;
	};
	deps.emit_to_web = [this](const std::string& json) {
		this->EmitToChatroomWeb(json);
	};
	deps.send_request = [](uint8_t msg_type, const std::string& payload, std::string& response) -> bool {
		auto& network = CNetwork_c::Instance();
		response = network.SendRequest(msg_type, payload);
		return !response.empty();
	};

	CClient::Instance().SetChatTransport(transport);
	blazeclaw::irc::CChatRoomBridge::Instance().Initialize(std::move(deps));
}

void CBlazeClawAgentChatView::StartConfiguredRuntime()
{
	m_runtimeModeResolved = ResolveRuntimeMode();
	m_nodeRuntimeStartedByMode = false;

	switch (m_runtimeModeResolved)
	{
	case blazeclaw::config::AgentChatRuntimeMode::Legacy:
		TRACE("CBlazeClawAgentChatView: AgentChat runtime mode=legacy\n");
		//StartNodeServer();
		m_nodeRuntimeStartedByMode = m_bNodeServerStarted;
		break;

	case blazeclaw::config::AgentChatRuntimeMode::Native:
		TRACE("CBlazeClawAgentChatView: AgentChat runtime mode=native\n");
		//StartNodeServer();
		if (!StartNativeRuntime())
		{
			TRACE("CBlazeClawAgentChatView: Native mode startup failed\n");
		}
		else if (m_nativeModeDegraded)
		{
			TRACE("CBlazeClawAgentChatView: Native mode running degraded (bridge on, runner off)\n");
		}
		break;

	case blazeclaw::config::AgentChatRuntimeMode::Auto:
	default:
		TRACE("CBlazeClawAgentChatView: AgentChat runtime mode=auto\n");
		if (!StartNativeRuntime())
		{
			TRACE("CBlazeClawAgentChatView: Auto mode: native runtime unavailable; staying in-process only (no Node.js fallback)\n");
			StopNativeRuntime();
			// Intentionally NOT falling back to StartNodeServer(): the chatroom
			// is fully driven by the C++ IRC transport + Bridge. Legacy Node
			// bridge is disabled per project decision.
			m_nodeRuntimeStartedByMode = false;
		}
		break;
	}

	const std::string mode =
		m_runtimeModeResolved == blazeclaw::config::AgentChatRuntimeMode::Legacy
		? "legacy"
		: (m_runtimeModeResolved == blazeclaw::config::AgentChatRuntimeMode::Native
			? "native"
			: "auto");
	if (!blazeclaw::chat::shared::RollbackSafetyEvaluator::IsSafeModeTransition(
		mode,
		m_nativeRuntimeStarted,
		m_nodeRuntimeStartedByMode))
	{
		AgentChatDiagnostics().RecordParityViolation();
		const nlohmann::json snapshotJson =
			blazeclaw::chat::shared::SharedChatDiagnosticsCollector::BuildSnapshotJson(
				"agent-chat-runtime",
				AgentChatDiagnostics().Snapshot());
		TRACE(
			"CBlazeClawAgentChatView: rollback safety validation failed mode=%s payload=%s\n",
			mode.c_str(),
			snapshotJson.dump().c_str());
	}
}
//-------------------------------------------------------------------

LRESULT CBlazeClawAgentChatView::OnWebMessageReceived(WPARAM, LPARAM)
{
	// 一次性处理队列里所有累积的 web message。
	// 之前单槽位 + std::move 的实现会把同时间窗到达的多条消息相互覆盖，
	// 表现为 "empty pending payload" + 部分请求永远等不到响应。
	while (true)
	{
		std::wstring pendingJson;
		{
			std::lock_guard<std::mutex> lock(m_webBridgeMutex);
			if (m_pendingWebMessageJson.empty())
			{
				return 0;
			}
			pendingJson = std::move(m_pendingWebMessageJson.front());
			m_pendingWebMessageJson.pop_front();
		}

		if (pendingJson.empty())
		{
			TRACE("CBlazeClawAgentChatView: native bridge web message dropped (empty pending payload)\n");
			continue;
		}

	const nlohmann::json frame = nlohmann::json::parse(
		WideToUtf8(pendingJson),
		nullptr,
		false);
	if (frame.is_discarded() || !frame.is_object())
	{
		TRACE(
			"CBlazeClawAgentChatView: native bridge web message dropped (invalid json, size=%u)\n",
			static_cast<unsigned int>(pendingJson.size()));
		continue;
	}

	const std::string channel = frame.value("channel", std::string());

	if (channel == "chatroom.bridge.request")
	{
		const std::string requestId = frame.value("requestId", std::string());
		const std::string kind = frame.value("kind", std::string());

		TRACE(
			"CBlazeClawAgentChatView: chatroom bridge request received requestId=%s kind=%s\n",
			requestId.c_str(),
			kind.c_str());

		auto chatroomEmitToWeb = [this](const std::string& json, const char* evt)
		{
			if (m_webView == nullptr)
			{
				TRACE("CBlazeClawAgentChatView: chatroomEmitToWeb skipped (m_webView null)\n");
				return;
			}
			const std::wstring script =
				L"(function(){try{const __msg=" +
				Utf8ToWide(json) +
				L";window.dispatchEvent(new CustomEvent('" + Utf8ToWide(std::string(evt)) +
				L"',{detail:__msg}));}catch(e){}})();";
			HRESULT execHr = m_webView->ExecuteScript(script.c_str(), nullptr);
			if (FAILED(execHr)) {
				TRACE("CBlazeClawAgentChatView: chatroomEmitToWeb ExecuteScript failed hr=0x%08x\n",
					static_cast<unsigned int>(execHr));
			}
		};

		nlohmann::json payload = frame.value("payload", nlohmann::json::object());
		auto& bridge = blazeclaw::irc::CChatRoomBridge::Instance();

		nlohmann::json responseJson;
		responseJson["channel"] = "chatroom.bridge.response";
		responseJson["requestId"] = requestId;

		// Non-blocking: HandleWebMessageAsync posts response to web when ready.
		// This avoids blocking the UI thread while waiting for server response.
		blazeclaw::irc::BridgeRequest req;
		req.request_id = requestId;
		req.kind = kind;
		req.payload_json = payload.is_object() ? payload.dump() : "{}";
		req.session_id = frame.value("sessionId", std::string());
		req.timestamp_ms = static_cast<uint64_t>(
			std::chrono::steady_clock::now().time_since_epoch().count());

		// Async: returns immediately, response arrives later via emit_to_web
		bridge.HandleWebMessageAsync(req);

		// Don't emit a response here — HandleWebMessageAsync will do it via deps_.emit_to_web
		continue;
	}

	if (channel != "agentchat.bridge.request")
	{
		TRACE(
			"CBlazeClawAgentChatView: native bridge web message ignored channel=%s\n",
			channel.c_str());
		continue;
	}

	const std::string requestId = frame.value("requestId", std::string());
	const std::string kind = frame.value("kind", std::string());
	if (requestId.empty() || kind.empty())
	{
		TRACE(
			"CBlazeClawAgentChatView: native bridge web message dropped (missing requestId/kind) channel=%s\n",
			channel.c_str());
		continue;
	}
	TRACE(
		"CBlazeClawAgentChatView: native bridge request received requestId=%s kind=%s\n",
		requestId.c_str(),
		kind.c_str());

	auto emitToWeb = [this](const nlohmann::json& message)
	{
		if (m_webView == nullptr)
		{
			return;
		}
		const std::string jsonUtf8 = message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
		const std::wstring script =
			L"(function(){try{const __msg=" +
			Utf8ToWide(jsonUtf8) +
			L";window.dispatchEvent(new CustomEvent('agentchat.bridge.message',{detail:__msg}));}catch(e){}})();";
		m_webView->ExecuteScript(script.c_str(), nullptr);
	};

	if (kind == "agent.abort")
	{
		{
			std::lock_guard<std::mutex> lock(m_webBridgeMutex);
			m_cancelledAgentBridgeRequestIds.insert(requestId);
		}
		AgentChatDiagnostics().CancelRequest(requestId);
		TRACE(
			"CBlazeClawAgentChatView: native bridge request aborted requestId=%s\n",
			requestId.c_str());
		emitToWeb(nlohmann::json{
			{ "channel", "agentchat.bridge.response" },
			{ "requestId", requestId },
			{ "ok", true },
			{ "payload", nlohmann::json{ { "aborted", true } } },
		});
		continue;
	}

	if (kind == "agent.health")
	{
		if (!m_nativeBridgeHost)
		{
			TRACE(
				"CBlazeClawAgentChatView: native bridge health unavailable requestId=%s\n",
				requestId.c_str());
			emitToWeb(nlohmann::json{
				{ "channel", "agentchat.bridge.response" },
				{ "requestId", requestId },
				{ "ok", false },
				{ "error", nlohmann::json{ { "message", "native_bridge_unavailable" } } },
			});
			continue;
		}

		const auto healthResponse = m_nativeBridgeHost->HandleRequest("GET", "/health", "{}");
		TRACE(
			"CBlazeClawAgentChatView: native bridge health response requestId=%s status=%d\n",
			requestId.c_str(),
			healthResponse.statusCode);
		nlohmann::json payload = nlohmann::json::object();
		if (!healthResponse.body.empty())
		{
			const auto parsed = nlohmann::json::parse(healthResponse.body, nullptr, false);
			if (!parsed.is_discarded())
			{
				payload = parsed;
			}
		}

		emitToWeb(nlohmann::json{
			{ "channel", "agentchat.bridge.response" },
			{ "requestId", requestId },
			{ "ok", healthResponse.statusCode >= 200 && healthResponse.statusCode < 300 },
			{ "payload", payload },
		});
		continue;
	}

	if (kind != "agent.turn")
	{
		emitToWeb(nlohmann::json{
			{ "channel", "agentchat.bridge.response" },
			{ "requestId", requestId },
			{ "ok", false },
			{ "error", nlohmann::json{ { "message", "unsupported_kind" } } },
		});
		continue;
	}

	{
		std::lock_guard<std::mutex> lock(m_webBridgeMutex);
		if (m_activeAgentBridgeRequestIds.find(requestId) != m_activeAgentBridgeRequestIds.end())
		{
			emitToWeb(nlohmann::json{
				{ "channel", "agentchat.bridge.response" },
				{ "requestId", requestId },
				{ "ok", false },
				{ "error", nlohmann::json{ { "message", "request_already_in_progress" } } },
			});
			continue;
		}
		m_activeAgentBridgeRequestIds.insert(requestId);
		m_cancelledAgentBridgeRequestIds.erase(requestId);
		AgentChatDiagnostics().BeginRequest(requestId);
	}

	auto finalizeRequest = [this, &requestId]()
	{
		std::lock_guard<std::mutex> lock(m_webBridgeMutex);
		m_activeAgentBridgeRequestIds.erase(requestId);
		m_cancelledAgentBridgeRequestIds.erase(requestId);
		AgentChatDiagnostics().CompleteRequest(requestId);
	};

	auto isCancelled = [this, &requestId]()
	{
		std::lock_guard<std::mutex> lock(m_webBridgeMutex);
		return m_cancelledAgentBridgeRequestIds.find(requestId) != m_cancelledAgentBridgeRequestIds.end();
	};

	if (!m_nativeBridgeHost)
	{
		TRACE(
			"CBlazeClawAgentChatView: native bridge unavailable for requestId=%s\n",
			requestId.c_str());
		emitToWeb(nlohmann::json{
			{ "channel", "agentchat.bridge.response" },
			{ "requestId", requestId },
			{ "ok", false },
			{ "error", nlohmann::json{ { "message", "native_bridge_unavailable" } } },
		});
		finalizeRequest();
		continue;
	}

	if (isCancelled())
	{
		TRACE(
			"CBlazeClawAgentChatView: native bridge request cancelled before dispatch requestId=%s\n",
			requestId.c_str());
		emitToWeb(nlohmann::json{
			{ "channel", "agentchat.bridge.stream.error" },
			{ "requestId", requestId },
			{ "payload", nlohmann::json{ { "type", "error" }, { "message", "request_aborted" } } },
		});
		finalizeRequest();
		continue;
	}

	const nlohmann::json rawPayload = frame.contains("payload") && frame["payload"].is_object()
		? frame["payload"]
		: nlohmann::json::object();

	nlohmann::json payload = rawPayload;
	payload["message"] = rawPayload.value("text", std::string());
	payload["userId"] = frame.value("userId", std::string());
	payload["userName"] = frame.value("userName", std::string());
	payload["phone"] = frame.value("phone", std::string());
	payload["conversationId"] = rawPayload.value("conversationId", std::string());
	payload["channel"] = rawPayload.value("channel", std::string());
	payload["groupId"] = rawPayload.value("groupId", std::string());
	payload["sessionKey"] = rawPayload.value("sessionKey", std::string());

	const bool stream = rawPayload.value("stream", true);
	const std::string requestBody = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	TRACE(
		"CBlazeClawAgentChatView: native bridge request posted requestId=%s stream=%s body=%s\n",
		requestId.c_str(),
		stream ? "true" : "false",
		requestBody.c_str());
	const auto response = m_nativeBridgeHost->HandleInProcessAgentTurn(requestBody);
	const bool isSseResponse = response.contentType.find("text/event-stream") != std::string::npos;

	if (isSseResponse)
	{
		std::stringstream ss(response.body);
		std::string line;
		while (std::getline(ss, line))
		{
			if (isCancelled())
			{
				emitToWeb(nlohmann::json{
					{ "channel", "agentchat.bridge.stream.error" },
					{ "requestId", requestId },
					{ "payload", nlohmann::json{ { "type", "error" }, { "message", "request_aborted" } } },
				});
				break;
			}

			if (line.rfind("data:", 0) != 0)
			{
				continue;
			}
			std::string jsonText = line.substr(5);
			if (!jsonText.empty() && jsonText[0] == ' ')
			{
				jsonText.erase(jsonText.begin());
			}
			const auto eventPayload = nlohmann::json::parse(jsonText, nullptr, false);
			if (eventPayload.is_discarded() || !eventPayload.is_object())
			{
				continue;
			}

			auto mappedEvent = blazeclaw::agentchat::AgentChatEventPayload::FromWireObject(eventPayload);
			mappedEvent.requestId = requestId;
			blazeclaw::agentchat::AgentChatEvent appEvent;
			appEvent.inner = mappedEvent.core;
			appEvent.requestId = mappedEvent.requestId;
			appEvent.runId = mappedEvent.runId;
			appEvent.state = mappedEvent.state;
			if (mappedEvent.extra.has_value()) {
				appEvent.extra = mappedEvent.extra.value();
			}
			const blazeclaw::chat::shared::ConformantChatStreamEventNormalizer normalizer("delta");
			const nlohmann::json normalizedEventPayload =
				normalizer.Normalize(appEvent.ToWireObject());
			const auto conformance =
				blazeclaw::chat::shared::ConformantChatStreamEventNormalizer::Check(normalizedEventPayload);
			if (!conformance.hasType || !conformance.hasTimestamp)
			{
				AgentChatDiagnostics().RecordConformanceFailure();
				TRACE(
					"CBlazeClawAgentChatView: non-conformant stream payload requestId=%s\n",
					requestId.c_str());
				continue;
			}

			// Streamed responses are produced by the orchestrator and normalized into frontend events
			const std::string type = normalizedEventPayload.value("type", std::string());
			AgentChatDiagnostics().RecordStreamType(type);
			const std::string timestampText = std::to_string(
				normalizedEventPayload.value("timestamp", static_cast<std::uint64_t>(0)));
			const std::string idempotencyKey = requestId + "|" + type + "|" + timestampText;
			if (!AgentChatDiagnostics().ObserveIdempotencyKey(idempotencyKey))
			{
				TRACE(
					"CBlazeClawAgentChatView: duplicate stream idempotency key requestId=%s\n",
					requestId.c_str());
			}
			if (!AgentChatParityValidator().Observe(requestId, type))
			{
				AgentChatDiagnostics().RecordParityViolation();
				TRACE(
					"CBlazeClawAgentChatView: stream parity violation requestId=%s type=%s\n",
					requestId.c_str(),
					type.c_str());
			}
			// the normalization/emit sites that convert orchestrator SSE payloads into 
			// frontend events (`emitToWeb` calls)
			if (type == "delta")	// partial/streamed updates
			{
				TRACE(
					"CBlazeClawAgentChatView: native bridge delta requestId=%s\n",
					requestId.c_str());
				emitToWeb(nlohmann::json{
					{ "channel", "agentchat.bridge.stream.delta" },
					{ "requestId", requestId },
					{ "payload", normalizedEventPayload },
				});
			}
			else if (type == "final")	// finalized response
			{
				TRACE(
					"CBlazeClawAgentChatView: native bridge final requestId=%s\n",
					requestId.c_str());
				emitToWeb(nlohmann::json{
					{ "channel", "agentchat.bridge.stream.final" },
					{ "requestId", requestId },
					{ "payload", normalizedEventPayload },
				});
			}
			else if (type == "error")	// errors
			{
				TRACE(
					"CBlazeClawAgentChatView: native bridge error requestId=%s\n",
					requestId.c_str());
				emitToWeb(nlohmann::json{
					{ "channel", "agentchat.bridge.stream.error" },
					{ "requestId", requestId },
					{ "payload", normalizedEventPayload },
				});
			}
		}

		const auto snapshot = AgentChatDiagnostics().Snapshot();
		const nlohmann::json snapshotJson =
			blazeclaw::chat::shared::SharedChatDiagnosticsCollector::BuildSnapshotJson(
				"agent-chat",
				snapshot);
		TRACE(
			"CBlazeClawAgentChatView: %s\n",
			snapshotJson.dump().c_str());

		emitToWeb(nlohmann::json{
			{ "channel", "agentchat.bridge.response" },
			{ "requestId", requestId },
			{ "ok", response.statusCode >= 200 && response.statusCode < 300 },
			{ "payload", nlohmann::json{ { "statusCode", response.statusCode } } },
		});
		TRACE(
			"CBlazeClawAgentChatView: native bridge response sent requestId=%s status=%d\n",
			requestId.c_str(),
			response.statusCode);
		finalizeRequest();
		continue;
	}

	nlohmann::json responsePayload = nlohmann::json::object();
	nlohmann::json responseError = nlohmann::json::object();
	if (!response.body.empty())
	{
		const auto parsedResponse = nlohmann::json::parse(response.body, nullptr, false);
		if (!parsedResponse.is_discarded())
		{
			if (parsedResponse.is_object())
			{
				responsePayload = parsedResponse;
			}
			else
			{
				responsePayload["raw"] = parsedResponse;
			}
		}
	}

	if (response.statusCode < 200 || response.statusCode >= 300)
	{
		std::string errorMessage;
		std::string errorCode;

		if (responsePayload.is_object())
		{
			errorMessage = responsePayload.value("message", std::string());
			if (errorMessage.empty() && responsePayload.contains("error") && responsePayload["error"].is_object())
			{
				const auto& nestedError = responsePayload["error"];
				errorMessage = nestedError.value("message", std::string());
				errorCode = nestedError.value("code", std::string());
			}
			if (errorMessage.empty() && responsePayload.contains("error") && responsePayload["error"].is_string())
			{
				errorMessage = responsePayload["error"].get<std::string>();
				if (errorCode.empty())
				{
					errorCode = errorMessage;
				}
			}
			if (errorCode.empty())
			{
				errorCode = responsePayload.value("code", std::string());
			}
		}

		if (errorMessage.empty())
		{
			errorMessage = std::string("native_bridge_http_") + std::to_string(response.statusCode);
		}

		responseError["message"] = errorMessage;
		if (!errorCode.empty())
		{
			responseError["code"] = errorCode;
		}

		TRACE(
			"CBlazeClawAgentChatView: native bridge non-sse error requestId=%s status=%d message=%s\n",
			requestId.c_str(),
			response.statusCode,
			errorMessage.c_str());
	}

	responsePayload["statusCode"] = response.statusCode;

	nlohmann::json bridgeResponse = nlohmann::json{
		{ "channel", "agentchat.bridge.response" },
		{ "requestId", requestId },
		{ "ok", response.statusCode >= 200 && response.statusCode < 300 },
		{ "payload", responsePayload },
	};
	if (!responseError.empty())
	{
		bridgeResponse["error"] = responseError;
	}

	emitToWeb(bridgeResponse);
	finalizeRequest();
	continue;
	}
	// while(true) 永远循环取下一条消息；这里的 return 不可达，作为占位
	// 防止编译器对 "non-void function does not return a value" 报错
	return 0;
}

std::wstring CBlazeClawAgentChatView::GetWebAssetsPath() const
{
	TCHAR exePath[MAX_PATH] = {};
	if (!GetModuleFileName(nullptr, exePath, MAX_PATH))
	{
		return std::wstring();
	}

	PathRemoveFileSpec(exePath);

	std::wstring webAssetsPath(exePath);
	webAssetsPath += L"\\web\\agent-chat-vanilla\\html";

	return webAssetsPath;
}

bool CBlazeClawAgentChatView::InitWebView()
{
	if (CClient::Instance().IsLoggedIn()) {
		
				const auto info = CClient::Instance().GetTokenInfo();
				InjectAuthState(
					info.token,
					std::to_string(CClient::Instance().GetSessionId()),
					info.sub,
					info.phone);
		
	}
	return CreateWebViewController();
}

bool CBlazeClawAgentChatView::CreateWebViewController()
{
	auto hwnd = GetSafeHwnd();
	if (hwnd == nullptr)
	{
		TRACE("CBlazeClawAgentChatView: CreateWebViewController HWND is null\n");
		return false;
	}

	m_webAssetsPath = GetWebAssetsPath();
	TRACE("CBlazeClawAgentChatView: Web assets folder: %ls\n", m_webAssetsPath.c_str());

	if (GetFileAttributes(m_webAssetsPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		TRACE("CBlazeClawAgentChatView: Warning - web assets folder not found at %ls\n", m_webAssetsPath.c_str());
	}

	// Initialize COM for WebView2
	HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(hrCom) && hrCom != RPC_E_CHANGED_MODE)
	{
		TRACE("CBlazeClawAgentChatView: Failed to initialize COM: 0x%08x\n", hrCom);
	}

	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
		nullptr, nullptr, nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[this](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
			{
				if (FAILED(result))
				{
					TRACE("CBlazeClawAgentChatView: Failed to create WebView2 environment: 0x%08X\n", result);
					return result;
				}

				if (environment == nullptr)
				{
					TRACE("CBlazeClawAgentChatView: WebView2 environment is null\n");
					return E_POINTER;
				}

				return environment->CreateCoreWebView2Controller(
					GetSafeHwnd(),
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[this](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
						{
							if (FAILED(result))
							{
								TRACE("CBlazeClawAgentChatView: Failed to create WebView2 controller: 0x%08X\n", result);
								return result;
							}

							if (controller == nullptr)
							{
								TRACE("CBlazeClawAgentChatView: WebView2 controller is null\n");
								return E_POINTER;
							}

							m_webViewController = controller;
							HRESULT hr = controller->get_CoreWebView2(&m_webView);
							if (FAILED(hr))
							{
								TRACE("CBlazeClawAgentChatView: Failed to get CoreWebView2: 0x%08X\n", hr);
								return hr;
							}

							// Re-fetch bounds NOW (not captured from OnCreate, which had 0x0)
							CRect rcNow;
							GetClientRect(&rcNow);
							TRACE("CBlazeClawAgentChatView: put_Bounds with rect (%d,%d,%d,%d)\n",
								rcNow.left, rcNow.top, rcNow.right, rcNow.bottom);

							controller->put_Bounds(rcNow);
							controller->put_IsVisible(TRUE);

							// Re-apply bounds after delay to handle window sizing timing
							SetTimer(1, 150, nullptr);

							// Then configure settings
							ComPtr<ICoreWebView2Settings> settings;
							if (SUCCEEDED(m_webView->get_Settings(&settings)) && settings)
							{
								settings->put_IsStatusBarEnabled(FALSE);
								settings->put_AreDevToolsEnabled(TRUE);
								settings->put_AreDefaultContextMenusEnabled(TRUE);
								settings->put_IsScriptEnabled(TRUE);
								settings->put_AreDefaultScriptDialogsEnabled(TRUE);
								settings->put_IsWebMessageEnabled(TRUE);
							}

							ComPtr<ICoreWebView2_3> webView3;
							if (SUCCEEDED(m_webView.As(&webView3)) && webView3)
							{
								webView3->SetVirtualHostNameToFolderMapping(
									AGENTCHAT_HOST_NAME,
									m_webAssetsPath.c_str(),
									COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
								TRACE("CBlazeClawAgentChatView: SetVirtualHostNameToFolderMapping succeeded\n");

								std::wstring mappedUrl = L"http://";
								mappedUrl += AGENTCHAT_HOST_NAME;
								mappedUrl += L"/";
								mappedUrl += AGENTCHAT_INDEX_FILE;
								TRACE("CBlazeClawAgentChatView: Navigating to: %ls\n", mappedUrl.c_str());
								m_webView->Navigate(mappedUrl.c_str());
							}
							else
							{
								TRACE("CBlazeClawAgentChatView: ICoreWebView2_3 not available\n");
							}

							SetupWebViewEvents();

							return S_OK;
						}).Get());
			}).Get());

	if (FAILED(hr))
	{
		TRACE("CBlazeClawAgentChatView: CreateCoreWebView2EnvironmentWithOptions failed: 0x%08X\n", hr);
		return false;
	}

	return true;
}

std::wstring CBlazeClawAgentChatView::GetServerPath() const
{
	wchar_t exePath[MAX_PATH] = { 0 };
	if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
	{
		return L"";
	}

	PathRemoveFileSpecW(exePath);

	std::wstring serverPath(exePath);
	serverPath += L"\\web\\agent-chat-vanilla\\server";

	if (GetFileAttributesW(serverPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		TRACE("CBlazeClawAgentChatView: Server directory not found: %s\n", serverPath.c_str());
		return L"";
	}

	return serverPath;
}

bool CBlazeClawAgentChatView::WaitForNodeServer(int timeoutMs)
{
	if (!m_hNodeStartedEvent)
		return false;

	DWORD result = WaitForSingleObject(m_hNodeStartedEvent, timeoutMs);
	return (result == WAIT_OBJECT_0);
}

bool CBlazeClawAgentChatView::StartNodeScript(
	const std::wstring& serverPath,
	const std::wstring& scriptName,
	PROCESS_INFORMATION& processInfo)
{
	std::wstring psCommand =
		L"Set-Location -Path \"" + serverPath +
		L"\"; npm run " + scriptName;
	std::wstring cmdLine =
		L"powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \"" +
		psCommand +
		L"\"";

	// chat-bridge 进程传入 CTcpReceiverWnd 的 HWND 环境变量
	// 必须用 Set-Item Env: 而不是 $env:=，这样才能被子进程继承
	if (scriptName == L"chat-bridge")
	{
		CMainFrame* pMain = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
		if (pMain && pMain->GetTcpReceiverWnd())
		{
			HWND hTarget = pMain->GetTcpReceiverWnd()->GetSafeHwnd();
			if (hTarget)
			{
				WCHAR hwndHex[32];
				swprintf_s(hwndHex, L"0x%p", hTarget);
				TRACE("CBlazeClawAgentChatView: CHAT_BRIDGE_HWND = %ls\n", hwndHex);

				// 组合完整命令
				std::wstring fullCommand =
					L"Set-Item -Path Env:CHAT_BRIDGE_HWND -Value '" + std::wstring(hwndHex) +
					L"'; Set-Location -Path \"" + serverPath +
					L"\"; npm run " + scriptName;

				// 使用 -EncodedCommand：将命令转为 UTF-16LE 后 Base64 编码
				std::vector<BYTE> utf16Bytes;
				for (wchar_t ch : fullCommand)
				{
					utf16Bytes.push_back(static_cast<BYTE>(ch & 0xFF));
					utf16Bytes.push_back(static_cast<BYTE>((ch >> 8) & 0xFF));
				}

				// Base64 编码
				static const char base64Table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
				std::string base64;
				for (size_t i = 0; i < utf16Bytes.size(); i += 3)
				{
					int n = (utf16Bytes[i] << 16);
					if (i + 1 < utf16Bytes.size()) n += utf16Bytes[i + 1] << 8;
					if (i + 2 < utf16Bytes.size()) n += utf16Bytes[i + 2];
					base64 += base64Table[(n >> 18) & 0x3F];
					base64 += base64Table[(n >> 12) & 0x3F];
					base64 += (i + 1 < utf16Bytes.size()) ? base64Table[(n >> 6) & 0x3F] : '=';
					base64 += (i + 2 < utf16Bytes.size()) ? base64Table[n & 0x3F] : '=';
				}

				cmdLine = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + std::wstring(base64.begin(), base64.end());
			}
		}
	}

	TRACE("CBlazeClawAgentChatView: Executing %ls: %ls\n",
		scriptName.c_str(),
		cmdLine.c_str());

	ZeroMemory(&processInfo, sizeof(processInfo));
	STARTUPINFOW startupInfo = { sizeof(startupInfo) };
	std::vector<wchar_t> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
	cmdLineBuffer.push_back(L'\0');

	const BOOL success = CreateProcessW(
		NULL,
		&cmdLineBuffer[0],
		NULL,
		NULL,
		FALSE,
		CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE,
		NULL,
		NULL,
		&startupInfo,
		&processInfo);

	if (!success)
	{
		TRACE("CBlazeClawAgentChatView: Failed to start %ls. Error: %lu\n",
			scriptName.c_str(),
			GetLastError());
		return false;
	}

	// 将新进程加入 Job Object，关闭 Job 句柄时 Windows 关闭整棵进程树
	if (m_hNodeJsJobObject == nullptr)
	{
		m_hNodeJsJobObject = CreateJobObjectW(nullptr, nullptr);
		if (m_hNodeJsJobObject)
		{
			JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
			jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
			SetInformationJobObject(m_hNodeJsJobObject,
				JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
		}
	}
	if (m_hNodeJsJobObject)
		AssignProcessToJobObject(m_hNodeJsJobObject, processInfo.hProcess);

	TRACE("CBlazeClawAgentChatView: Started %ls. PID: %lu\n",
		scriptName.c_str(),
		processInfo.dwProcessId);
	return true;
}

void CBlazeClawAgentChatView::StopNodeProcess(PROCESS_INFORMATION& processInfo)
{
	if (processInfo.hProcess)
	{
		CloseHandle(processInfo.hProcess);
		processInfo.hProcess = nullptr;
	}
	if (processInfo.hThread)
	{
		CloseHandle(processInfo.hThread);
		processInfo.hThread = nullptr;
	}
	ZeroMemory(&processInfo, sizeof(processInfo));
}

/*
 * The legacy runtime bootstrap for AgentChat is based on Node.js and requires 
 * starting two separate scripts:
 * 1. chat-bridge: This script handles the WebSocket communication and serves 
 *    as a bridge between the web UI and the backend services.
 * 2. blazeclaw-agent-bridge: This script manages the agent chat logic and 
 *    interfaces with the backend services. 
 */
void CBlazeClawAgentChatView::StartNodeServer()
{
	TRACE("CBlazeClawAgentChatView: Starting AgentChat Node.js services...\n");

	std::wstring serverPath = GetServerPath();
	if (serverPath.empty())
	{
		TRACE("CBlazeClawAgentChatView: Failed to get server path\n");
		return;
	}

	TRACE("CBlazeClawAgentChatView: Server path: %ls\n", serverPath.c_str());

	m_hNodeStartedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	const bool chatBridgeStarted = StartNodeScript(
		serverPath,
		L"chat-bridge",
		m_nodeProcessInfo);
	const bool agentBridgeStarted = StartNodeScript(
		serverPath,
		L"blazeclaw-agent-bridge",
		m_agentBridgeProcessInfo);

	if (!chatBridgeStarted || !agentBridgeStarted)
	{
		TRACE("CBlazeClawAgentChatView: Failed to start one or more Node.js services\n");
		StopNodeProcess(m_nodeProcessInfo);
		StopNodeProcess(m_agentBridgeProcessInfo);
		if (m_hNodeStartedEvent)
		{
			CloseHandle(m_hNodeStartedEvent);
			m_hNodeStartedEvent = nullptr;
		}
		return;
	}

	bool serverReady = false;
	for (int i = 0; i < 30; i++)
	{
		Sleep(500);

		if (IsProcessStillActive(m_nodeProcessInfo) &&
			IsProcessStillActive(m_agentBridgeProcessInfo))
		{
			serverReady = true;
			m_bNodeServerStarted = true;
			if (m_hNodeStartedEvent)
			{
				SetEvent(m_hNodeStartedEvent);
			}
			TRACE("CBlazeClawAgentChatView: AgentChat Node.js services started successfully\n");
			break;
		}
		else
		{
			TRACE("CBlazeClawAgentChatView: One or more Node.js services exited during startup\n");
			break;
		}
	}

	if (!serverReady)
	{
		TRACE("CBlazeClawAgentChatView: Warning: Node.js services may not have started properly\n");
		m_bNodeServerStarted = true;
	}
}

void CBlazeClawAgentChatView::StopNodeServer()
{
	TRACE("CBlazeClawAgentChatView: Stopping Node.js server...\n");

	// 关闭 Job Object，Windows 自动杀整棵进程树
	if (m_hNodeJsJobObject)
	{
		CloseHandle(m_hNodeJsJobObject);
		m_hNodeJsJobObject = nullptr;
	}

	if (m_hNodeStartedEvent)
	{
		CloseHandle(m_hNodeStartedEvent);
		m_hNodeStartedEvent = nullptr;
	}

	StopNodeProcess(m_agentBridgeProcessInfo);
	StopNodeProcess(m_nodeProcessInfo);

	m_bNodeServerStarted = false;
	TRACE("CBlazeClawAgentChatView: Node.js services stopped\n");
}


void CBlazeClawAgentChatView::SetupWebViewEvents()
{
	if (m_webView == nullptr)
	{
		return;
	}

	m_webView->add_WebMessageReceived(
		Callback<ICoreWebView2WebMessageReceivedEventHandler>(
			[this](ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
			{
				std::wstring message;
				bool hasMessage = false;

				LPWSTR rawMessage = nullptr;
				if (SUCCEEDED(args->TryGetWebMessageAsString(&rawMessage)) && rawMessage != nullptr)
				{
					message.assign(rawMessage);
					hasMessage = true;
					CoTaskMemFree(rawMessage);
					rawMessage = nullptr;
				}
				else if (SUCCEEDED(args->get_WebMessageAsJson(&rawMessage)) && rawMessage != nullptr)
				{
					message.assign(rawMessage);
					hasMessage = true;
					CoTaskMemFree(rawMessage);
					rawMessage = nullptr;
				}

				if (!hasMessage)
				{
					TRACE("CBlazeClawAgentChatView: native bridge web message dropped (no payload)\n");
					return S_OK;
				}


				{
					std::lock_guard<std::mutex> lock(m_webBridgeMutex);
					if (m_pendingWebMessageJson.size() >= kMaxPendingWebMessages) {
						// 满了就丢最早的，避免内存膨胀；前端会在下一次同步时拿到不一致状态由上层兜底
						m_pendingWebMessageJson.pop_front();
						TRACE(
							"CBlazeClawAgentChatView: pending web message queue full (%zu), dropping oldest\n",
							m_pendingWebMessageJson.size());
					}
					m_pendingWebMessageJson.push_back(std::move(message));
				}

				if (m_messageHandler)
				{
					// 回调一般用于做上层分发（例如预解析）；此处仅同步通知，原消息仍在队列里等 WM_AGENTCHAT_WEBMESSAGE_RECEIVED 处理
					std::wstring snapshot;
					{
						std::lock_guard<std::mutex> lock(m_webBridgeMutex);
						snapshot = m_pendingWebMessageJson.back();
					}
					m_messageHandler(snapshot);
				}

				PostMessage(WM_AGENTCHAT_WEBMESSAGE_RECEIVED);

				return S_OK;
			}).Get(),
		&m_webMessageToken);

	// Navigation events
	m_webView->add_NavigationStarting(
		Callback<ICoreWebView2NavigationStartingEventHandler>(
			[](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
				PWSTR uri = nullptr;
				args->get_Uri(&uri);
				if (uri)
				{
					TRACE("CBlazeClawAgentChatView: Navigation starting: %s\n", uri);
					CoTaskMemFree(uri);
				}
				return S_OK;
			}).Get(), nullptr);

	m_webView->add_NavigationCompleted(
		Callback<ICoreWebView2NavigationCompletedEventHandler>(
			[this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
				BOOL isSuccess = FALSE;
				args->get_IsSuccess(&isSuccess);
			if (!isSuccess)
			{
				COREWEBVIEW2_WEB_ERROR_STATUS status;
				args->get_WebErrorStatus(&status);
				TRACE("CBlazeClawAgentChatView: Navigation failed: %d\n", status);
			}
			else
			{
				TRACE("CBlazeClawAgentChatView: Navigation completed\n");
			}

			if (m_webViewController != nullptr)
			{
				m_webViewController->put_IsVisible(TRUE);
				m_webViewController->NotifyParentWindowPositionChanged();
			}

			if (CWnd* parent = GetParent())
			{
				parent->Invalidate();
				parent->UpdateWindow();
			}

			if (isSuccess)
			{
				InjectRuntimeBridgeConfig();
				_DoInjectAuthState();
			}

			// 关键修复：WebView 加载完成后，必须处理之前排队的 push 消息
			// 在 WebView 加载期间到达的 push 消息会被加入队列但不会发送
			// NavigationCompleted 之后需要手动调用 FlushChatroomEmitQueue()
			TRACE("CBlazeClawAgentChatView: Navigation completed, flushing chatroom emit queue\n");
			FlushChatroomEmitQueue();

			return S_OK;
		}).Get(), nullptr);
}

#endif	// BLAZECLAW_AGENTCHATVIEW_WEBVIEW2

void CBlazeClawAgentChatView::InjectRuntimeBridgeConfig()
{
	if (m_webView == nullptr)
	{
		return;
	}

	std::wstring runtimeMode = L"auto";
	switch (m_runtimeModeResolved)
	{
	case blazeclaw::config::AgentChatRuntimeMode::Legacy:
		runtimeMode = L"legacy";
		break;
	case blazeclaw::config::AgentChatRuntimeMode::Native:
		runtimeMode = L"native";
		break;
	case blazeclaw::config::AgentChatRuntimeMode::Auto:
	default:
		runtimeMode = L"auto";
		break;
	}

	const bool nativeBridgeEnabled = m_nativeRuntimeStarted;
	const bool nativeBridgeHostStarted = m_nativeBridgeHostStarted;
	const bool nativeRunnerStarted = m_nativeRunnerStarted;
	const bool nativeModeDegraded = m_nativeModeDegraded;
	const bool nativeHttpListenerStarted = m_nativeHttpListenerStarted;
	const std::uint16_t nativeHttpListenerPort = m_nativeHttpListenerPort;
	bool uiInProcessAgentPath = true;
	auto* app = static_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app != nullptr)
	{
		uiInProcessAgentPath = app->Config().agentChatRuntime.uiInProcessAgentPath;
	}
	const bool nativeUiBridgeEnabled = nativeBridgeEnabled && uiInProcessAgentPath;
	const std::wstring transportMode = nativeUiBridgeEnabled ? L"native-webview" : L"http";
	const std::wstring effectiveMode = nativeHttpListenerStarted
		? L"native-inprocess+http"
		: L"native-inprocess-only";
	const std::wstring reachabilityHint = nativeUiBridgeEnabled
		? L"native-inprocess"
		: (nativeBridgeHostStarted ? L"http-bridge" : L"bridge-unavailable");

	const std::wstring script =
		L"(function(){"
		L"try{"
		L"window.__APP_CONFIG__=window.__APP_CONFIG__||{};"
		L"window.__APP_CONFIG__.agentRuntimeMode='" + EscapeJsSingleQuotedString(runtimeMode) + L"';"
		L"window.__APP_CONFIG__.enableNativeAgentBridge=" + std::wstring(nativeUiBridgeEnabled ? L"true" : L"false") + L";"
		L"window.__APP_CONFIG__.nativeBridgeHostStarted=" + std::wstring(nativeBridgeHostStarted ? L"true" : L"false") + L";"
		L"window.__APP_CONFIG__.nativeRunnerStarted=" + std::wstring(nativeRunnerStarted ? L"true" : L"false") + L";"
		L"window.__APP_CONFIG__.nativeModeDegraded=" + std::wstring(nativeModeDegraded ? L"true" : L"false") + L";"
		L"window.__APP_CONFIG__.nativeHttpListenerStarted=" + std::wstring(nativeHttpListenerStarted ? L"true" : L"false") + L";"
		L"window.__APP_CONFIG__.nativeHttpListenerPort=" + std::to_wstring(nativeHttpListenerPort) + L";"
		L"window.__APP_CONFIG__.nativeBridgeEffectiveMode='" + EscapeJsSingleQuotedString(effectiveMode) + L"';"
		L"window.__APP_CONFIG__.agentBridgeReachabilityHint='" + EscapeJsSingleQuotedString(reachabilityHint) + L"';"
		L"window.__APP_CONFIG__.agentBridgeTransport='" + EscapeJsSingleQuotedString(transportMode) + L"';"
		L"window.__APP_CONFIG__.enableHttpFallbackOnNativeBridgeError=true;"
		L"}catch(e){}"
		L"}())";

	m_webView->ExecuteScript(script.c_str(), nullptr);
}

void CBlazeClawAgentChatView::InjectAuthState(const std::string& token, const std::string& sessionId, const std::string& userId, const std::string& phone)
{
	m_injectedToken = std::wstring(token.begin(), token.end());
	m_injectedSessionId = std::wstring(sessionId.begin(), sessionId.end());
	m_injectedUserId = std::wstring(userId.begin(), userId.end());
	m_injectedPhone = std::wstring(phone.begin(), phone.end());
	m_hasInjectedAuth = true;
	_DoInjectAuthState();
}

void CBlazeClawAgentChatView::_DoInjectAuthState()
{
	if (!m_hasInjectedAuth || m_webView == nullptr)
	{
		return;
	}

	m_hasInjectedAuth = false;

	auto escapeForJs = [](const std::wstring& value) -> std::wstring {
		std::wstring out;
		out.reserve(value.size());
		for (wchar_t ch : value) {
			if (ch == L'\'')
				out.append(L"\\'");
			else if (ch == L'\\')
				out.append(L"\\\\");
			else
				out.push_back(ch);
		}
		return out;
		};

	const std::wstring script = L"(function(){"
		L"try{"
		L"window.__INJECTED_AUTH__=window.__INJECTED_AUTH__||{};"
		L"window.__INJECTED_AUTH__.token='" + escapeForJs(m_injectedToken) + L"';"
		L"window.__INJECTED_AUTH__.sessionId='" + escapeForJs(m_injectedSessionId) + L"';"
		L"window.__INJECTED_AUTH__.userId='" + escapeForJs(m_injectedUserId) + L"';"
		L"window.__INJECTED_AUTH__.phone='" + escapeForJs(m_injectedPhone) + L"';"
		L"window.dispatchEvent(new Event('__auth_injected__'));"
		L"}catch(e){}}())";

	m_webView->ExecuteScript(script.c_str(), nullptr);
}

void CBlazeClawAgentChatView::EmitToChatroomWeb(const std::string& json)
{
	// WebView2 ExecuteScript must run on the UI thread, but TCP callbacks
	// arrive on a worker thread.  Queue the JSON and post a message to
	// ourselves so that FlushChatroomEmitQueue() runs on the UI thread.
	{
		std::lock_guard<std::mutex> lock(m_chatroomEmitQueueMutex);
		m_chatroomEmitQueue.push_back(json);
	}
	::PostMessage(m_hWnd, WM_CHATROOM_EMIT_TO_WEB, 0, 0);
}

LRESULT CBlazeClawAgentChatView::OnChatroomEmitToWeb(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	FlushChatroomEmitQueue();
	return 0;
}

void CBlazeClawAgentChatView::FlushChatroomEmitQueue()
{
	if (m_webView == nullptr) {
		TRACE(_T("FlushChatroomEmitQueue: m_webView is null, skipping\n"));
		return;
	}

	std::deque<std::string> queue;
	{
		std::lock_guard<std::mutex> lock(m_chatroomEmitQueueMutex);
		queue = std::move(m_chatroomEmitQueue);
	}
	if (queue.empty()) {
		TRACE(_T("FlushChatroomEmitQueue: queue empty, skipping\n"));
		return;
	}

	TRACE(_T("FlushChatroomEmitQueue: processing %zu items\n"), queue.size());

	// 批量合并为一次 ExecuteScript：避免高并发 IRC push 时 UI 线程被多次 EvaluateScript 阻塞
	// channel 决定发送到哪个事件：
	//   - chatroom.bridge.response → request 响应
	//   - chatroom.bridge.push → IRC push 事件
	nlohmann::json arrResponse = nlohmann::json::array();
	nlohmann::json arrPush = nlohmann::json::array();
	for (const auto& json : queue)
	{
		auto parsed = nlohmann::json::parse(json, nullptr, false);
		if (!parsed.is_discarded()) {
			std::string ch = parsed.value("channel", "");
			if (ch == "chatroom.bridge.response") {
				arrResponse.push_back(std::move(parsed));
			} else {
				arrPush.push_back(std::move(parsed));
			}
		}
	}

	const std::wstring script =
		L"(function(){try{"
		L"const __resp=" + Utf8ToWide(arrResponse.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) +
		L";for(const __msg of __resp){window.dispatchEvent(new CustomEvent('chatroom.bridge.response',{detail:__msg}));}"
		L"const __push=" + Utf8ToWide(arrPush.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)) +
		L";for(const __msg of __push){window.dispatchEvent(new CustomEvent('chatroom.bridge.push',{detail:__msg}));}"
		L"}catch(e){}})();";
	TRACE(_T("FlushChatroomEmitQueue: executing script, responses=%zu pushes=%zu\n"), arrResponse.size(), arrPush.size());
	// 调试：打印实际发送的 JSON
	if (!arrResponse.empty()) {
		TRACE(_T("FlushChatroomEmitQueue: response JSON: %hs\n"), arrResponse.dump().c_str());
	}
	m_webView->ExecuteScript(script.c_str(), nullptr);
	TRACE(_T("FlushChatroomEmitQueue: done\n"));
}
