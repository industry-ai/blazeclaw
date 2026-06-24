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

	StartNodeServer();

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
	StopNodeServer();

	if (m_webView != nullptr)
	{
		m_webView->remove_WebMessageReceived(m_webMessageToken);
		m_webView = nullptr;
	}
	m_webViewController = nullptr;

	CView::OnDestroy();
}

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
	webAssetsPath += L"\\agent-chat-vanilla\\html";

	return webAssetsPath;
}

bool CBlazeClawAgentChatView::InitWebView()
{
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
	serverPath += L"\\agent-chat-vanilla\\server";

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

void CBlazeClawAgentChatView::StartNodeServer()
{
	TRACE("CBlazeClawAgentChatView: Starting Node.js server...\n");

	std::wstring serverPath = GetServerPath();
	if (serverPath.empty())
	{
		TRACE("CBlazeClawAgentChatView: Failed to get server path\n");
		return;
	}

	TRACE("CBlazeClawAgentChatView: Server path: %s\n", serverPath.c_str());

	m_hNodeStartedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	std::wstring psCommand = L"Set-Location -Path \"" + serverPath + L"\"; npm run chat-bridge";
	std::wstring cmdLine = L"powershell.exe -NoExit -Command \"" + psCommand + L"\"";

	TRACE("CBlazeClawAgentChatView: Executing: %s\n", cmdLine.c_str());

	ZeroMemory(&m_nodeProcessInfo, sizeof(m_nodeProcessInfo));
	STARTUPINFOW siNpm = { sizeof(siNpm) };
	std::vector<wchar_t> cmdLineBuffer(cmdLine.begin(), cmdLine.end());
	cmdLineBuffer.push_back(L'\0');

	BOOL bSuccess = CreateProcessW(
		NULL, &cmdLineBuffer[0],
		NULL, NULL, FALSE,
		CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE,
		NULL, NULL,
		&siNpm,
		&m_nodeProcessInfo);

	if (!bSuccess)
	{
		DWORD err = GetLastError();
		TRACE("CBlazeClawAgentChatView: Failed to start Node.js server. Error: %d\n", err);
		if (m_hNodeStartedEvent)
		{
			CloseHandle(m_hNodeStartedEvent);
			m_hNodeStartedEvent = nullptr;
		}
		return;
	}

	TRACE("CBlazeClawAgentChatView: Node.js server started. PID: %d\n", m_nodeProcessInfo.dwProcessId);

	bool serverReady = false;
	for (int i = 0; i < 30; i++)
	{
		Sleep(500);

		DWORD exitCode;
		if (GetExitCodeProcess(m_nodeProcessInfo.hProcess, &exitCode) && exitCode == STILL_ACTIVE)
		{
			serverReady = true;
			m_bNodeServerStarted = true;
			TRACE("CBlazeClawAgentChatView: Node.js server started successfully\n");
			break;
		}
		else
		{
			TRACE("CBlazeClawAgentChatView: Node.js process exited with code: %d\n", exitCode);
			break;
		}
	}

	if (!serverReady)
	{
		TRACE("CBlazeClawAgentChatView: Warning: Node.js server may not have started properly\n");
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

	if (m_nodeProcessInfo.hProcess)
	{
		TerminateProcess(m_nodeProcessInfo.hProcess, 0);
		CloseHandle(m_nodeProcessInfo.hProcess);
		m_nodeProcessInfo.hProcess = nullptr;
	}

	if (m_nodeProcessInfo.hThread)
	{
		CloseHandle(m_nodeProcessInfo.hThread);
		m_nodeProcessInfo.hThread = nullptr;
	}

	m_bNodeServerStarted = false;
	TRACE("CBlazeClawAgentChatView: Node.js server stopped\n");
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
