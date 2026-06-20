#pragma once

#include "DashboardBridgeHost.h"

#include <functional>
#include <string>

#if defined(__has_include)
# if __has_include(<WebView2.h>)
#  include <WebView2.h>
#  define HAVE_WEBVIEW2_HEADER
# endif
#endif

#ifdef HAVE_WEBVIEW2_HEADER
# include <wrl.h>
# include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#else
struct ICoreWebView2;
struct ICoreWebView2Controller;
#endif

class CDashboardToolBar : public CMFCToolBar
{
public:
	virtual void OnUpdateCmdUI(CFrameWnd* /*pTarget*/, BOOL bDisableIfNoHndler)
	{
		CMFCToolBar::OnUpdateCmdUI((CFrameWnd*) GetOwner(), bDisableIfNoHndler);
	}

	virtual BOOL AllowShowOnList() const { return FALSE; }
};

class CDashboardWnd : public CDockablePane
{
public:
	void FloatToRect(const CRect& rect);

	CDashboardWnd() noexcept;
	virtual ~CDashboardWnd();

	void OnPaneVisibilityChanged(BOOL visible);

	RECT	m_rcStored;

protected:
	virtual void	OnAfterFloat();
	virtual void	OnAfterDock(CBasePane* pBar, LPCRECT lpRect, AFX_DOCK_METHOD dockMethod);

	virtual BOOL FloatPane(
		CRect rectFloat,
		AFX_DOCK_METHOD dockMethod = DM_UNKNOWN,
		bool bShow = true
	) override;
	//virtual BOOL DockPane(
	//	CDockablePane* pDockBar,
	//	CRect rect = CRect(0, 0, 0, 0),
	//	AFX_DOCK_METHOD dockMethod = DM_UNKNOWN
	//) override;

	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg LRESULT OnDashboardBridgePollCompleted(WPARAM wParam, LPARAM lParam);
	afx_msg void OnWindowPosChanged(WINDOWPOS* lpwndpos);
	DECLARE_MESSAGE_MAP()

private:
#ifdef HAVE_WEBVIEW2_HEADER
	ComPtr<ICoreWebView2Controller> m_webViewController;
	ComPtr<ICoreWebView2> m_webView;
	EventRegistrationToken m_webMessageToken{};
#else
	ICoreWebView2Controller* m_webViewController = nullptr;
	ICoreWebView2* m_webView = nullptr;
#endif

	blazeclaw::app::dashboard_bridge::CDashboardBridgeHost m_bridgeHost;
	bool m_webViewReady = false;

	bool	m_bNewlyCreated = true;

	bool InitWebView();
	bool CreateWebViewController();
	void SetupWebViewEvents();
	void InitializeDashboardBridge();
	void ResizeWebViewBounds();
	std::wstring ResolveDashboardNavigationUrl() const;
	void NavigateDashboardOrShowError();
	void ShowDashboardStartupError(const wchar_t* title, const wchar_t* details);
	void PostBridgeMessageJson(const std::wstring& jsonMessage);
	void HandleWebMessageJson(const std::wstring& webMessageJson);
};
