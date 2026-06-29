#include "pch.h"
#include "framework.h"
#include "BlazeClawAgentChatView.h"
#include "BlazeClawMfcApp.h"

#include <Shlwapi.h>

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
	bridgeConfig.enableGatewayRouting = true;
	bridgeConfig.enablePushTransport = true;
	bridgeConfig.compatibilityOpenClawAliases = true;
	bridgeConfig.bindAddress = WideToUtf8(runtime.bindAddress);
	if (bridgeConfig.bindAddress.empty())
	{
		bridgeConfig.bindAddress = "127.0.0.1";
	}
	bridgeConfig.port = runtime.port;

	auto bridgeHost = std::make_unique<blazeclaw::agentchat::AgentChatBridgeHost>();
	bridgeHost->SetGatewayRequestRouter(
		[app](const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			return app->RouteGatewayRequest(request);
		});
	if (!bridgeHost->Initialize(bridgeConfig))
	{
		TRACE(
			"CBlazeClawAgentChatView: Failed to initialize native bridge host (%s:%u)\n",
			bridgeConfig.bindAddress.c_str(),
			bridgeConfig.port);
		return false;
	}

	auto nativeRunner = std::make_unique<blazeclaw::agentchat::AgentChatNativeRunner>();
	nativeRunner->SetGatewayRequestRouter(
		[app](const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			return app->RouteGatewayRequest(request);
		});
	if (!nativeRunner->Initialize())
	{
		bridgeHost->Shutdown();
		TRACE("CBlazeClawAgentChatView: Failed to initialize native runner\n");
		return false;
	}

	m_nativeBridgeHost = std::move(bridgeHost);
	m_nativeRunner = std::move(nativeRunner);
	m_nativeRuntimeStarted = true;
	TRACE(
		"CBlazeClawAgentChatView: Native runtime started (aliases=%s, bind=%s:%u)\n",
		bridgeConfig.compatibilityOpenClawAliases ? "true" : "false",
		WideToUtf8(runtime.bindAddress).c_str(),
		runtime.port);
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
				LPWSTR rawMessage = nullptr;
				if (SUCCEEDED(args->TryGetWebMessageAsString(&rawMessage)) && rawMessage != nullptr)
				{
					std::wstring message(rawMessage);
					if (m_messageHandler)
					{
						m_messageHandler(message);
					}
					PostMessage(WM_AGENTCHAT_WEBMESSAGE_RECEIVED);
					CoTaskMemFree(rawMessage);
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
