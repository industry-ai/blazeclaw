#include "pch.h"
#include "framework.h"
#include "BlazeClawAgentChatView.h"
#include "BlazeClawMfcApp.h"

#include <Shlwapi.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <sstream>

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
END_MESSAGE_MAP()

CBlazeClawAgentChatView::CBlazeClawAgentChatView() noexcept
	: m_webAssetsPath()
	, m_hasInjectedAuth(false)
{
}

CBlazeClawAgentChatView::~CBlazeClawAgentChatView()
{
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
		TRACE("CBlazeClawAgentChatView: OnSize fixing bounds (%d,%d,%d,%d)\n",
			rc.left, rc.top, rc.right, rc.bottom);
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
	orchestratorAdapter->SetRouter(
		[app](const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			return app->RouteGatewayRequest(request);
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

void CBlazeClawAgentChatView::StartConfiguredRuntime()
{
	m_runtimeModeResolved = ResolveRuntimeMode();
	m_nodeRuntimeStartedByMode = false;

	switch (m_runtimeModeResolved)
	{
	case blazeclaw::config::AgentChatRuntimeMode::Legacy:
		TRACE("CBlazeClawAgentChatView: AgentChat runtime mode=legacy\n");
		StartNodeServer();
		m_nodeRuntimeStartedByMode = m_bNodeServerStarted;
		break;

	case blazeclaw::config::AgentChatRuntimeMode::Native:
		TRACE("CBlazeClawAgentChatView: AgentChat runtime mode=native\n");
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
			TRACE("CBlazeClawAgentChatView: Auto mode fallback to legacy Node runtime\n");
			StopNativeRuntime();
			StartNodeServer();
			m_nodeRuntimeStartedByMode = m_bNodeServerStarted;
			m_runtimeModeResolved = blazeclaw::config::AgentChatRuntimeMode::Legacy;
		}
		break;
	}
}
//-------------------------------------------------------------------

LRESULT CBlazeClawAgentChatView::OnWebMessageReceived(WPARAM, LPARAM)
{
	std::wstring pendingJson;
	{
		std::lock_guard<std::mutex> lock(m_webBridgeMutex);
		pendingJson = std::move(m_pendingWebMessageJson);
		m_pendingWebMessageJson.clear();
	}

	if (pendingJson.empty())
	{
		return 0;
	}

	const nlohmann::json frame = nlohmann::json::parse(
		WideToUtf8(pendingJson),
		nullptr,
		false);
	if (frame.is_discarded() || !frame.is_object())
	{
		return 0;
	}

	const std::string channel = frame.value("channel", std::string());
	if (channel != "agentchat.bridge.request")
	{
		return 0;
	}

	const std::string requestId = frame.value("requestId", std::string());
	const std::string kind = frame.value("kind", std::string());
	if (requestId.empty() || kind.empty())
	{
		return 0;
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
		TRACE(
			"CBlazeClawAgentChatView: native bridge request aborted requestId=%s\n",
			requestId.c_str());
		emitToWeb(nlohmann::json{
			{ "channel", "agentchat.bridge.response" },
			{ "requestId", requestId },
			{ "ok", true },
			{ "payload", nlohmann::json{ { "aborted", true } } },
		});
		return 0;
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
			return 0;
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
		return 0;
	}

	if (kind != "agent.turn")
	{
		emitToWeb(nlohmann::json{
			{ "channel", "agentchat.bridge.response" },
			{ "requestId", requestId },
			{ "ok", false },
			{ "error", nlohmann::json{ { "message", "unsupported_kind" } } },
		});
		return 0;
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
			return 0;
		}
		m_activeAgentBridgeRequestIds.insert(requestId);
		m_cancelledAgentBridgeRequestIds.erase(requestId);
	}

	auto finalizeRequest = [this, &requestId]()
	{
		std::lock_guard<std::mutex> lock(m_webBridgeMutex);
		m_activeAgentBridgeRequestIds.erase(requestId);
		m_cancelledAgentBridgeRequestIds.erase(requestId);
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
		return 0;
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
		return 0;
	}

	const nlohmann::json payload = frame.contains("payload") && frame["payload"].is_object()
		? frame["payload"]
		: nlohmann::json::object();
	const bool stream = payload.value("stream", true);
	const std::string requestBody = payload.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
	TRACE(
		"CBlazeClawAgentChatView: native bridge request posted requestId=%s stream=%s\n",
		requestId.c_str(),
		stream ? "true" : "false");
	const auto response = m_nativeBridgeHost->HandleRequest("POST", "/api/blazeclaw-agent", requestBody);
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

			const std::string type = eventPayload.value("type", std::string());
			if (type == "delta")
			{
				TRACE(
					"CBlazeClawAgentChatView: native bridge delta requestId=%s\n",
					requestId.c_str());
				emitToWeb(nlohmann::json{
					{ "channel", "agentchat.bridge.stream.delta" },
					{ "requestId", requestId },
					{ "payload", eventPayload },
				});
			}
			else if (type == "final")
			{
				TRACE(
					"CBlazeClawAgentChatView: native bridge final requestId=%s\n",
					requestId.c_str());
				emitToWeb(nlohmann::json{
					{ "channel", "agentchat.bridge.stream.final" },
					{ "requestId", requestId },
					{ "payload", eventPayload },
				});
			}
			else if (type == "error")
			{
				TRACE(
					"CBlazeClawAgentChatView: native bridge error requestId=%s\n",
					requestId.c_str());
				emitToWeb(nlohmann::json{
					{ "channel", "agentchat.bridge.stream.error" },
					{ "requestId", requestId },
					{ "payload", eventPayload },
				});
			}
		}

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
		return 0;
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

	TRACE("CBlazeClawAgentChatView: Started %ls. PID: %lu\n",
		scriptName.c_str(),
		processInfo.dwProcessId);
	return true;
}

void CBlazeClawAgentChatView::StopNodeProcess(PROCESS_INFORMATION& processInfo)
{
	if (processInfo.hProcess)
	{
		TerminateProcess(processInfo.hProcess, 0);
		CloseHandle(processInfo.hProcess);
		processInfo.hProcess = nullptr;
	}

	if (processInfo.hThread)
	{
		CloseHandle(processInfo.hThread);
		processInfo.hThread = nullptr;
	}
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
	TRACE("CBlazeClawAgentChatView: Node.js AgentChat bridge startup will be retired in native cutover mode\n");

	std::wstring serverPath = GetServerPath();
	if (serverPath.empty())
	{
		TRACE("CBlazeClawAgentChatView: Failed to get server path\n");
		return;
	}

	TRACE("CBlazeClawAgentChatView: Server path: %ls\n", serverPath.c_str());

	m_hNodeStartedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// launches two Node.js processes 
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
			// marks the view as “server started” for downstream logic
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
				LPWSTR rawMessageJson = nullptr;
				if (SUCCEEDED(args->get_WebMessageAsJson(&rawMessageJson)) && rawMessageJson != nullptr)
				{
					std::wstring message(rawMessageJson);
					if (m_messageHandler)
					{
						m_messageHandler(message);
					}
					{
						std::lock_guard<std::mutex> lock(m_webBridgeMutex);
						m_pendingWebMessageJson = message;
					}
					PostMessage(WM_AGENTCHAT_WEBMESSAGE_RECEIVED);
					CoTaskMemFree(rawMessageJson);
				}

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

			return S_OK;
		}).Get(), nullptr);
}

#else

IMPLEMENT_DYNCREATE(CBlazeClawAgentChatView, CView)

BEGIN_MESSAGE_MAP(CBlazeClawAgentChatView, CView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CBlazeClawAgentChatView::CBlazeClawAgentChatView() noexcept
{
}

CBlazeClawAgentChatView::~CBlazeClawAgentChatView()
{
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

	TRACE0("CBlazeClawAgentChatView: WebView2 is not available. Please install WebView2 runtime.\n");
	return 0;
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// 2026/06/28, jicheng, add dual mode support for agent chat bridge
blazeclaw::config::AgentChatRuntimeMode CBlazeClawAgentChatView::ResolveRuntimeMode() const
{
	return blazeclaw::config::AgentChatRuntimeMode::Auto;
}

bool CBlazeClawAgentChatView::StartNativeRuntime()
{
	return false;
}

void CBlazeClawAgentChatView::StopNativeRuntime()
{
}

void CBlazeClawAgentChatView::StartConfiguredRuntime()
{
}
//-------------------------------------------------------------------

void CBlazeClawAgentChatView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);
}

void CBlazeClawAgentChatView::OnDraw(CDC* pDC)
{
	CRect rc;
	GetClientRect(&rc);
	pDC->DrawText(_T("WebView2 is not available.\nPlease install WebView2 runtime."), rc, DT_CENTER | DT_VCENTER);
}

void CBlazeClawAgentChatView::OnDestroy()
{
	CView::OnDestroy();
}

LRESULT CBlazeClawAgentChatView::OnWebMessageReceived(WPARAM, LPARAM)
{
	return 0;
}

std::wstring CBlazeClawAgentChatView::GetWebAssetsPath() const
{
	return std::wstring();
}

bool CBlazeClawAgentChatView::InitWebView()
{
	return false;
}

std::wstring CBlazeClawAgentChatView::GetServerPath() const
{
	return std::wstring();
}

bool CBlazeClawAgentChatView::WaitForNodeServer(int /*timeoutMs*/)
{
	return false;
}

void CBlazeClawAgentChatView::StartNodeServer()
{
}

void CBlazeClawAgentChatView::StopNodeServer()
{
}

#endif


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
