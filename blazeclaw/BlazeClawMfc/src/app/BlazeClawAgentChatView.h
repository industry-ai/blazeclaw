#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>
#include <deque>
#include "Client.h"

#include "../config/ConfigModels.h"
#include "../agentchat/AgentChatBridgeHost.h"
#include "../agentchat/AgentChatNativeRunner.h"

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

	//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	// 2026/06/28, jicheng, add dual mode support for agent chat bridge
	//std::unique_ptr<blazeclaw::agentchat::AgentChatBridgeHost>		m_nativeBridgeHost;
	//std::unique_ptr<blazeclaw::agentchat::AgentChatNativeRunner>	m_nativeRunner;
	//blazeclaw::config::AgentChatRuntimeMode							m_runtimeModeResolved;
	//-------------------------------------------------------------------

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
	afx_msg LRESULT OnChatroomEmitToWeb(WPARAM wParam, LPARAM lParam);
	DECLARE_MESSAGE_MAP()

private:
	std::wstring GetWebAssetsPath() const;
	std::wstring GetServerPath() const;
	bool InitWebView();
	bool CreateWebViewController();
	void InjectRuntimeBridgeConfig();
	//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	// 2026/06/28, jicheng, add dual mode support for agent chat bridge
	blazeclaw::config::AgentChatRuntimeMode ResolveRuntimeMode() const;
	bool	StartNativeRuntime();
	void	StopNativeRuntime();
	void	StartConfiguredRuntime();
	// Initialize ChatRoom Bridge unconditionally for push event routing.
	// Must succeed regardless of native runtime mode.
	void	InitChatRoomBridge();
	// Forward a chatroom bridge message (UTF-8 JSON) to the embedded WebView2.
	// Safe to call before the WebView2 controller is created - the call is
	// silently dropped in that case so C++ push events are not lost forever
	// (the IRC transport keeps them queued until a later pull/snapshot).
	void	EmitToChatroomWeb(const std::string& json);
	//-------------------------------------------------------------------

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
	HANDLE m_hNodeJsJobObject = nullptr;
	bool m_bNodeServerStarted = false;

	bool StartNodeScript(
		const std::wstring& serverPath,
		const std::wstring& scriptName,
		PROCESS_INFORMATION& processInfo);
	void StopNodeProcess(PROCESS_INFORMATION& processInfo);

	//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	// 2026/06/28, jicheng, add dual mode support for agent chat bridge
	std::unique_ptr<blazeclaw::agentchat::AgentChatBridgeHost>		m_nativeBridgeHost;
	std::unique_ptr<blazeclaw::agentchat::AgentChatNativeRunner>	m_nativeRunner;
	blazeclaw::config::AgentChatRuntimeMode	m_runtimeModeResolved	=
		blazeclaw::config::AgentChatRuntimeMode::Auto;
	bool m_nativeRuntimeStarted		= false;
	bool m_nativeBridgeHostStarted	= false;
	bool m_nativeRunnerStarted		= false;
	bool m_nativeModeDegraded		= false;
	bool m_nativeHttpListenerStarted = false;
	std::uint16_t m_nativeHttpListenerPort = 0;
	bool m_nodeRuntimeStartedByMode	= false;
	//-------------------------------------------------------------------------------------------------

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
	std::mutex m_webBridgeMutex;
	// 改为队列 + 队列长度上限，避免恶意/异常前端把内存撑爆。
	std::deque<std::wstring> m_pendingWebMessageJson;
	static constexpr size_t kMaxPendingWebMessages = 64;
	std::unordered_set<std::string> m_activeAgentBridgeRequestIds;
	std::unordered_set<std::string> m_cancelledAgentBridgeRequestIds;

	// 跨线程消息队列：TCP 线程把消息入队，主 UI 线程派发到 WebView
	static constexpr UINT WM_CHATROOM_EMIT_TO_WEB = WM_USER + 200;
	std::deque<std::string> m_chatroomEmitQueue;
	std::mutex m_chatroomEmitQueueMutex;
	void FlushChatroomEmitQueue();
};
