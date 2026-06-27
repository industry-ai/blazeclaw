#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <functional>

#include "WebView2Availability.h"

#ifdef HAVE_WEBVIEW2_HEADER
# include <wrl.h>
# include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#else
struct ICoreWebView2;
struct ICoreWebView2Controller;
#endif

class CAIChatView : public CView
{
protected:
	CAIChatView() noexcept;
	DECLARE_DYNCREATE(CAIChatView)

public:
	~CAIChatView();

public:
#ifdef _DEBUG
	virtual void AssertValid() const override;
	virtual void Dump(CDumpContext& dc) const override;
#endif

protected:
	virtual void OnDraw(CDC* pDC) override;
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs) override;

protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnWebMessageReceived(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	std::wstring GetWebAssetsPath() const;
	bool InitWebView();
	bool CreateWebViewController();

#ifdef HAVE_WEBVIEW2_HEADER
	void SetupWebViewEvents();
	ComPtr<ICoreWebView2Controller> m_webViewController;
	ComPtr<ICoreWebView2> m_webView;
	EventRegistrationToken m_webMessageToken{};
	std::wstring m_webAssetsPath;
#else
	ICoreWebView2Controller* m_webViewController = nullptr;
	ICoreWebView2* m_webView = nullptr;
	void* m_webMessageToken = nullptr;
	std::wstring m_webAssetsPath;
#endif

	std::function<void(const std::wstring&)> m_messageHandler;
};
