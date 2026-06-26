#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include "Client.h"

#if defined(__has_include)
# if __has_include(<WebView2.h>)
#  include <WebView2.h>
#  define BLAZECLAW_AGENTCHATVIEW_WEBVIEW2
# endif
#endif

#ifdef BLAZECLAW_AGENTCHATVIEW_WEBVIEW2
# include <wrl.h>
# include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#else
struct ICoreWebView2;
struct ICoreWebView2Controller;
#endif

class CBlazeClawAgentChatView : public CView
{
protected:
	CBlazeClawAgentChatView() noexcept;
	DECLARE_DYNCREATE(CBlazeClawAgentChatView)

public:
	~CBlazeClawAgentChatView();

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
	afx_msg void OnInitialUpdate();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	afx_msg void OnDestroy();
	afx_msg LRESULT OnWebMessageReceived(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	std::wstring GetWebAssetsPath() const;
	std::wstring GetServerPath() const;
	bool InitWebView();
	bool CreateWebViewController();

#ifdef BLAZECLAW_AGENTCHATVIEW_WEBVIEW2
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

private:
	PROCESS_INFORMATION m_nodeProcessInfo{};
	PROCESS_INFORMATION m_agentBridgeProcessInfo{};
	HANDLE m_hNodeStartedEvent = nullptr;
	bool m_bNodeServerStarted = false;

	bool StartNodeScript(
		const std::wstring& serverPath,
		const std::wstring& scriptName,
		PROCESS_INFORMATION& processInfo);
	void StopNodeProcess(PROCESS_INFORMATION& processInfo);

public:
	void StartNodeServer();
	void StopNodeServer();
	bool WaitForNodeServer(int timeoutMs);
	bool IsNodeServerStarted() const { return m_bNodeServerStarted; }
	void InjectAuthState(const std::string& token, const std::string& sessionId, const std::string& userId, const std::string& phone);

	void _DoInjectAuthState();

private:
	std::wstring m_injectedToken;
	std::wstring m_injectedSessionId;
	std::wstring m_injectedUserId;
	std::wstring m_injectedPhone;
	bool m_hasInjectedAuth;
};
