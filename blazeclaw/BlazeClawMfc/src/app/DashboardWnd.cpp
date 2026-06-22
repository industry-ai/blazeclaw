#include "pch.h"
#include "framework.h"

#include "DashboardWnd.h"
#include "Resource.h"
#include "MainFrame.h"
#include "BlazeClawMFCApp.h"
#include "BlazeClawMFCViewTextHelpers.h"
#include "ChatUiStartupResolver.h"
#include "WebViewBridgeSupport.h"

#include "FloatPaneTracker.h"

#include <Shlwapi.h>
#include <filesystem>
#include <optional>
#include <afxext.h> // MFC extensions (splitter, docking)
#include <afxcontrolbars.h> // MFC Feature Pack (docking panes, control bars)

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

namespace {

std::optional<std::wstring> GetEnvValue(const wchar_t* name)
{
	if (name == nullptr || *name == L'\0')
	{
		return std::nullopt;
	}

	wchar_t buffer[32768]{};
	const DWORD length = ::GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
	if (length == 0 || length >= std::size(buffer))
	{
		return std::nullopt;
	}

	return std::wstring(buffer, length);
}

std::wstring TrimCopy(const std::wstring& value)
{
	std::size_t start = 0;
	while (start < value.size() && iswspace(value[start]) != 0)
	{
		++start;
	}

	std::size_t end = value.size();
	while (end > start && iswspace(value[end - 1]) != 0)
	{
		--end;
	}

	return value.substr(start, end - start);
}

std::wstring BuildFileUrl(const std::filesystem::path& filePath)
{
	std::wstring genericPath = filePath.lexically_normal().generic_wstring();
	if (!genericPath.empty() && genericPath.front() != L'/')
	{
		genericPath.insert(genericPath.begin(), L'/');
	}

	std::wstring escaped;
	escaped.reserve(genericPath.size() + 8);
	for (const wchar_t ch : genericPath)
	{
		switch (ch)
		{
		case L' ':
			escaped += L"%20";
			break;
		case L'#':
			escaped += L"%23";
			break;
		case L'%':
			escaped += L"%25";
			break;
		case L'?':
			escaped += L"%3F";
			break;
		default:
			escaped += ch;
			break;
		}
	}

	return L"file://" + escaped;
}

bool IsDevModePreferred()
{
#ifdef _DEBUG
	return true;
#else
	return false;
#endif
}

std::wstring ResolveDashboardStartupUrl()
{
	if (const auto envUrl = GetEnvValue(L"BLAZECLAW_DASHBOARD_DEV_URL"); envUrl.has_value())
	{
		const std::wstring value = TrimCopy(envUrl.value());
		if (!value.empty())
		{
			return value;
		}
	}

	if (const auto envUrl = GetEnvValue(L"OPENCLAW_UI_DEV_URL"); envUrl.has_value())
	{
		const std::wstring value = TrimCopy(envUrl.value());
		if (!value.empty())
		{
			const std::wstring suffix = value.find(L'?') == std::wstring::npos
				? L"?host=dashboard"
				: L"&host=dashboard";
			return value + suffix;
		}
	}

	if (const auto envFile = GetEnvValue(L"BLAZECLAW_DASHBOARD_UI_FILE"); envFile.has_value())
	{
		const std::wstring value = TrimCopy(envFile.value());
		if (!value.empty())
		{
			const std::filesystem::path explicitFile(value);
			if (std::filesystem::exists(explicitFile))
			{
				return BuildFileUrl(explicitFile);
			}
		}
	}

	if (const auto envRoot = GetEnvValue(L"BLAZECLAW_DASHBOARD_UI_ROOT"); envRoot.has_value())
	{
		const std::wstring value = TrimCopy(envRoot.value());
		if (!value.empty())
		{
			const std::filesystem::path root(value);
			const auto sourceDashboard = root / L"dashboard.html";
			if (std::filesystem::exists(sourceDashboard))
			{
				return BuildFileUrl(sourceDashboard);
			}
			const auto distDashboard = root / L"dist" / L"dashboard.html";
			if (std::filesystem::exists(distDashboard))
			{
				return BuildFileUrl(distDashboard);
			}
		}
	}

	bool preferSource = IsDevModePreferred();
	if (const auto mode = GetEnvValue(L"BLAZECLAW_CHAT_UI_MODE"); mode.has_value())
	{
		std::wstring normalized = TrimCopy(mode.value());
		for (wchar_t& ch : normalized)
		{
			ch = static_cast<wchar_t>(::towlower(ch));
		}

		if (normalized == L"dev")
		{
			return L"http://127.0.0.1:5173/dashboard.html";
		}
		if (normalized == L"source")
		{
			preferSource = true;
		}
		if (normalized == L"dist")
		{
			preferSource = false;
		}
	}

	wchar_t modulePath[MAX_PATH]{};
	std::filesystem::path moduleDir;
	if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
	{
		moduleDir = std::filesystem::path(modulePath).parent_path();
	}

	const auto roots = blazeclaw::app::chatui::BuildOrderedRoots(
		moduleDir,
		std::filesystem::current_path());
	const auto preference = preferSource
		? blazeclaw::app::chatui::StartupPreference::PreferSource
		: blazeclaw::app::chatui::StartupPreference::PreferDist;
	for (const auto& root : roots)
	{
		if (const auto found = blazeclaw::app::chatui::FindDashboardUiIndex(root, preference);
			found.has_value())
		{
			return BuildFileUrl(found->selectedPath);
		}
	}

	return {};
}

} // namespace

BEGIN_MESSAGE_MAP(CDashboardWnd, CDockablePane)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_ERASEBKGND()
	ON_WM_TIMER()
	ON_MESSAGE(blazeclaw::app::dashboard_bridge::kDashboardBridgePollCompletedMessage, &CDashboardWnd::OnDashboardBridgePollCompleted)
	ON_WM_WINDOWPOSCHANGED()
END_MESSAGE_MAP()

CDashboardWnd::CDashboardWnd() noexcept
{
}

CDashboardWnd::~CDashboardWnd()
{
	m_bridgeHost.Shutdown();
}

int CDashboardWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CDockablePane::OnCreate(lpCreateStruct) == -1)
	{
		return -1;
	}

	if (!InitWebView())
	{
		TRACE0("Failed to initialize dashboard WebView2\n");
	}

	return 0;
}

void CDashboardWnd::OnSize(UINT nType, int cx, int cy)
{
	CDockablePane::OnSize(nType, cx, cy);
	if (IsVisible())
	{
		OnPaneVisibilityChanged(TRUE);
	}
	else
	{
		OnPaneVisibilityChanged(FALSE);
	}
}

void CDashboardWnd::OnDestroy()
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webView != nullptr)
	{
		m_webView->remove_WebMessageReceived(m_webMessageToken);
		m_webView = nullptr;
	}
	m_webViewController = nullptr;
#endif

	m_bridgeHost.Shutdown();
	m_webViewReady = false;

	CDockablePane::OnDestroy();
}

BOOL CDashboardWnd::OnEraseBkgnd(CDC* /*pDC*/)
{
	return TRUE;
}

bool CDashboardWnd::InitWebView()
{
	return CreateWebViewController();
}

void CDashboardWnd::ResizeWebViewBounds()
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webViewController == nullptr)
	{
		return;
	}

	CRect rcClient;
	GetClientRect(&rcClient);
	if (rcClient.Width() <= 0 || rcClient.Height() <= 0)
	{
		return;
	}

	m_webViewController->put_Bounds(rcClient);
#endif
}

std::wstring CDashboardWnd::ResolveDashboardNavigationUrl() const
{
	const std::wstring startupUrl = ResolveDashboardStartupUrl();
	if (startupUrl.empty() || startupUrl == L"about:blank")
	{
		return startupUrl;
	}

	const std::wstring separator = (startupUrl.find(L'?') == std::wstring::npos)
		? L"?"
		: L"&";
	return startupUrl + separator + L"_wv_refresh=" + std::to_wstring(::GetTickCount64());
}

void CDashboardWnd::ShowDashboardStartupError(
	const wchar_t* title,
	const wchar_t* details)
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webView == nullptr)
	{
		return;
	}

	std::wstring html =
		L"<html><body style='font-family:Segoe UI;padding:20px;'>"
		L"<h2>BlazeClaw Dashboard Startup Error</h2>"
		L"<p><strong>";
	html += (title != nullptr ? title : L"Unable to load dashboard UI");
	html += L"</strong></p><p>";
	html += (details != nullptr ? details : L"");
	html += L"</p></body></html>";

	m_webView->NavigateToString(html.c_str());
#else
	UNREFERENCED_PARAMETER(title);
	UNREFERENCED_PARAMETER(details);
#endif
}

void CDashboardWnd::NavigateDashboardOrShowError()
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webView == nullptr)
	{
		return;
	}

	const std::wstring startupUrl = ResolveDashboardNavigationUrl();
	if (startupUrl.empty())
	{
		ShowDashboardStartupError(
			L"No startup URL resolved",
			L"Set BLAZECLAW_DASHBOARD_DEV_URL or provide blazeclaw/BlazeClawMfc/web/chat/dashboard.html.");
		return;
	}

	m_webView->Navigate(startupUrl.c_str());
#endif
}

void CDashboardWnd::PostBridgeMessageJson(const std::wstring& jsonMessage)
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webView == nullptr || jsonMessage.empty())
	{
		return;
	}

	m_webView->PostWebMessageAsJson(jsonMessage.c_str());
#else
	UNREFERENCED_PARAMETER(jsonMessage);
#endif
}

void CDashboardWnd::HandleWebMessageJson(const std::wstring& webMessageJson)
{
	m_bridgeHost.HandleWebMessageJson(webMessageJson);
}

void CDashboardWnd::InitializeDashboardBridge()
{
	if (!m_webViewReady)
	{
		return;
	}

	m_bridgeHost.Initialize(
		this,
		GetSafeHwnd(),
		[this](const std::wstring& responseJson)
		{
			PostBridgeMessageJson(responseJson);
		});
}

void CDashboardWnd::OnTimer(UINT_PTR nIDEvent)
{
	m_bridgeHost.OnTimer(nIDEvent);
	CDockablePane::OnTimer(nIDEvent);
}

LRESULT CDashboardWnd::OnDashboardBridgePollCompleted(WPARAM wParam, LPARAM lParam)
{
	m_bridgeHost.OnBridgePollCompleted(wParam, lParam);
	return 0;
}

void CDashboardWnd::OnPaneVisibilityChanged(BOOL visible)
{
	if (visible)
	{
		ResizeWebViewBounds();
	}
#ifdef HAVE_WEBVIEW2_HEADER
	else if (m_webViewController != nullptr)
	{
		m_webViewController->put_IsVisible(FALSE);
		return;
	}
#endif

#ifdef HAVE_WEBVIEW2_HEADER
	if (visible && m_webViewController != nullptr)
	{
		m_webViewController->put_IsVisible(TRUE);
	}
#endif
}

bool CDashboardWnd::CreateWebViewController()
{
#ifdef HAVE_WEBVIEW2_HEADER
	auto hwnd = GetSafeHwnd();
	if (hwnd == nullptr)
	{
		return false;
	}

	CRect rcClient;
	GetClientRect(&rcClient);

	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
		nullptr,
		nullptr,
		nullptr,
		Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[this, rcClient](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
			{
				if (FAILED(result) || environment == nullptr)
				{
					TRACE("Dashboard WebView2 environment failed: 0x%08X\n", result);
					return FAILED(result) ? result : E_POINTER;
				}

				return environment->CreateCoreWebView2Controller(
					GetSafeHwnd(),
					Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[this, rcClient](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
						{
							if (FAILED(result) || controller == nullptr)
							{
								TRACE("Dashboard WebView2 controller failed: 0x%08X\n", result);
								return FAILED(result) ? result : E_POINTER;
							}

							m_webViewController = controller;
							HRESULT getViewHr = controller->get_CoreWebView2(&m_webView);
							if (FAILED(getViewHr) || m_webView == nullptr)
							{
								TRACE("Dashboard WebView2 get_CoreWebView2 failed: 0x%08X\n", getViewHr);
								return FAILED(getViewHr) ? getViewHr : E_POINTER;
							}

							controller->put_Bounds(rcClient);
							controller->put_IsVisible(TRUE);

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

							blazeclaw::app::webview_bridge::InjectOpenClawBridgeShim(m_webView.Get());
							SetupWebViewEvents();
							m_webViewReady = true;
							InitializeDashboardBridge();

							m_webView->add_NavigationCompleted(
								Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
									[this](
										ICoreWebView2* /*sender*/,
										ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
									{
										BOOL isSuccess = FALSE;
										if (args != nullptr)
										{
											args->get_IsSuccess(&isSuccess);
										}

										if (!isSuccess)
										{
											ShowDashboardStartupError(
												L"Navigation failed",
												L"Verify dashboard web assets or dev server availability.");
										}

										return S_OK;
									}).Get(),
								nullptr);

							NavigateDashboardOrShowError();
							return S_OK;
						}).Get());
			}).Get());

	if (FAILED(hr))
	{
		TRACE("CreateCoreWebView2EnvironmentWithOptions failed for dashboard: 0x%08X\n", hr);
		return false;
	}

	return true;
#else
	TRACE0("WebView2 headers not available for dashboard pane.\n");
	return false;
#endif
}

void CDashboardWnd::SetupWebViewEvents()
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webView == nullptr)
	{
		return;
	}

	m_webView->add_WebMessageReceived(
		Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
			[this](
				ICoreWebView2* /*sender*/,
				ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
			{
				if (args == nullptr)
				{
					return S_OK;
				}

				LPWSTR rawJson = nullptr;
				if (FAILED(args->get_WebMessageAsJson(&rawJson)) || rawJson == nullptr)
				{
					return S_OK;
				}

				std::wstring jsonMessage(rawJson);
				CoTaskMemFree(rawJson);
				HandleWebMessageJson(jsonMessage);
				return S_OK;
			}).Get(),
		&m_webMessageToken);
#endif
}

void CDashboardWnd::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CDockablePane::OnWindowPosChanged(lpwndpos);

	if ((lpwndpos->flags & SWP_SHOWWINDOW) != 0)
	{
		OnPaneVisibilityChanged(TRUE);
	}
	else if ((lpwndpos->flags & SWP_HIDEWINDOW) != 0)
	{
		OnPaneVisibilityChanged(FALSE);
	}

	if (IsFloating())
	{
		// Sometimes works, but not during drag
	}

	if (!(lpwndpos->flags & SWP_NOMOVE) && (GetFocus() == this))
	{
		// Position changed
		int x = lpwndpos->x;
		int y = lpwndpos->y;

		if (x >= 0 && y >= 0) {
			CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
			if (pMain != nullptr && ::IsWindow(pMain->GetSafeHwnd())) {
				pMain->PostMessage(
					kMsgSyncDashboardPanePosition,
					reinterpret_cast<WPARAM>(GetSafeHwnd()),
					MAKELPARAM(x, y)
				);
			}
		}
	}

	if (!(lpwndpos->flags & SWP_NOSIZE) && (GetFocus() == this))
	{
		// Size changed
		int cx = lpwndpos->cx;
		int cy = lpwndpos->cy;

		if (cx > 0 && cy > 0) {
			CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
			if (pMain != nullptr && ::IsWindow(pMain->GetSafeHwnd())) {
				pMain->PostMessage(
					kMsgSyncDashboardPaneSize,
					reinterpret_cast<WPARAM>(GetSafeHwnd()),
					MAKELPARAM(cx, cy)
				);
			}
		}
	}
}

void CDashboardWnd::OnAfterFloat()
{
	CDockablePane::OnAfterFloat();
	// OnPaneVisibilityChanged(IsWindowVisible());

	const HWND paneHwnd = GetSafeHwnd();
	if (paneHwnd == nullptr || !::IsWindow(paneHwnd))
	{
		return;
	}

	if (m_bNewlyCreated)
	{
		m_bNewlyCreated = false;
		return; // Skip synchronization on the initial float after creation, as it's not a user-initiated action.
	}

	CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	if (pMain == nullptr ||
		!::IsWindow(pMain->GetSafeHwnd()) ||
		!pMain->IsDashboardPaneSyncReady())
	{
		return;
	}

	if (pMain->IsDashboardFloatDockSyncInProgress())
	{
		return;
	}

	// At this point, the pane has already CMultiPaneFrameWnd is already subclassed by MFC
	// to implement floating behavior, but if we intercept it earlier in FloatPane to replace 
	// the subclass with our CFloatPaneTracker which forwards messages to the original procedure,
	// `CWnd::UnsubclassWindow()` detaches the HWND from the `CWnd` object (`Detach()`).
	// After that, the original `CMultiPaneFrameWnd` object (pMulti) is still used by MFC internals, 
	// but its `m_hWnd` is no longer valid. Later (during idle/UI updates), MFC calls `CWnd::SendMessage`, 
	// which asserts at afxwin2.inl:32: ASSERT(::IsWindow(m_hWnd)); This leads to an assertion failure 
	// because the window handle has been detached from the original `CMultiPaneFrameWnd` object, and 
	// the `CFloatPaneTracker` subclass is not properly forwarding messages to the original window procedure 
	// as intended.
	// 
	// So the following code snippet will not use `UnsubclassWindow()` and instead it directly subclasses 
	// the `pMiniFrame->GetWindow(GW_CHILD)`
	//auto pMiniFrame = GetParentMiniFrame();
	//if (pMiniFrame == nullptr)
	//	return;
	auto pMultiFrame	= GetParentMiniFrame();	// This is the CMultiPaneFrameWnd created by MFC when floating, which hosts the actual mini frame (CPaneFrameWnd) as its child.

	if (pMultiFrame == nullptr)	return;

	if (!pMultiFrame->IsKindOf(RUNTIME_CLASS(CMultiPaneFrameWnd)))
	{
		return;
	}

	// GetWindow returns a CWnd*. Safely downcast to CPaneFrameWnd* to avoid 
	// implicit base->derived conversion error.
	CWnd* pChild = pMultiFrame->GetWindow(GW_CHILD);

	if (pChild == nullptr)
	{
		return;
	}

	if (!pChild->IsKindOf(RUNTIME_CLASS(CDashboardWnd)))
	{
		return;
	}

	if (pChild != this)	return;	// pChild is the first pane that floated, other panes will be synchronized into pMultiFrame

	pMain->PostMessage(
		kMsgSyncDashboardAfterFloat,
		reinterpret_cast<WPARAM>(paneHwnd),
		MAKELPARAM(-1, -1));

	return;

	////CMFCMultiPaneSplitterWnd
	//auto pMultiSplitter = DYNAMIC_DOWNCAST(CPaneFrameWnd, pChild);

	//if (pMultiSplitter && 
	//	!pMultiSplitter->IsKindOf(RUNTIME_CLASS(CFloatPaneTracker)) && 
	//	CWnd::FromHandlePermanent(pMultiSplitter->m_hWnd) == nullptr)
	//{
	//	CFloatPaneTracker* pTracker = new CFloatPaneTracker;
	//	pTracker->SubclassWindow(pMultiSplitter->m_hWnd);
	//}

	// Pane is now floating — refresh visibility/controls
	OnPaneVisibilityChanged(IsWindowVisible());

	if (pChild != this) {
		// this one is original source floating pane, send message to other panes to float together
		//OnPaneFloat();	// Post a message to trigger synchronization of floating state across panes

		return;
	}

}

void CDashboardWnd::FloatToRect(const CRect & rect)
{
	// Protected FloatPane is available to this derived class.
	FloatPane(rect);
}

void CDashboardWnd::OnAfterDock(CBasePane* pBar, LPCRECT lpRect, AFX_DOCK_METHOD dockMethod)
{
	// Call base with the proper signature
	CDockablePane::OnAfterDock(pBar, lpRect, dockMethod);

	const HWND paneHwnd = GetSafeHwnd();
	if (paneHwnd == nullptr || !::IsWindow(paneHwnd))
	{
		return;
	}

	if (m_bNewlyCreated)
	{
		m_bNewlyCreated = false;
		return; // Skip synchronization on the initial dock after creation, as it's not a user-initiated action.
	}

	// Pane is now docked — refresh visibility/controls
	OnPaneVisibilityChanged(IsWindowVisible());

	CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, AfxGetMainWnd());
	if (pMain == nullptr ||
		!::IsWindow(pMain->GetSafeHwnd()) ||
		!pMain->IsDashboardPaneSyncReady())
	{
		return;
	}

	if (pMain->IsDashboardFloatDockSyncInProgress())
	{
		return;
	}

	if (lpRect != nullptr)
	{
		m_rcStored = *lpRect;
	}
	else
	{
		GetWindowRect(&m_rcStored);
	}

	pMain->PostMessage(
		kMsgSyncDashboardAfterDock,
		reinterpret_cast<WPARAM>(paneHwnd),
		reinterpret_cast<LPARAM>(&m_rcStored));
		//MAKELPARAM(-1, -1));
}

BOOL CDashboardWnd::FloatPane(
	CRect rectFloat,
	AFX_DOCK_METHOD dockMethod,
	bool bShow)
{
	BOOL bResult = __super::FloatPane(rectFloat, dockMethod, bShow);
	/*
	__super is a Microsoft Visual C++ extension (only works with MSVC compiler, not GCC/Clang standard C++).
It automatically refers to the immediate direct base class of the current class.
Equivalent to writing the full base class name manually, but cleaner when you change inheritance later.
	*/

	//CMultiPaneFrameWnd* pMulti =
	//	DYNAMIC_DOWNCAST(CMultiPaneFrameWnd, GetParentMiniFrame());

	//if (pMulti &&
	//	!pMulti->IsKindOf(RUNTIME_CLASS(CFloatPaneTracker)))
	//{
	//	// Replace MFC's runtime class dynamically
	//	pMulti->UnsubclassWindow();

	//	CFloatPaneTracker* pTracker = new CFloatPaneTracker;
	//	pTracker->SubclassWindow(pMulti->m_hWnd);
	//}

	return bResult;
}