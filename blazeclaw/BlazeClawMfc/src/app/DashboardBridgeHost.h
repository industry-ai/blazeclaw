#pragma once

#include "CBridge.h"
#include "EventTransport.h"
#include "webview_routers/WebViewRouterContext.h"

#include <functional>
#include <string>

class CDashboardWnd;

namespace blazeclaw::app::dashboard_bridge {

constexpr UINT kDashboardBridgePollCompletedMessage = WM_APP + 0x2A5;
constexpr UINT_PTR kDashboardBridgeLifecycleTimerId = 0x4A23;
constexpr UINT kDashboardBridgeLifecycleTimerMs = 1000;

class CDashboardBridgeHost
{
public:
	CDashboardBridgeHost() = default;

	void Initialize(
		CDashboardWnd* owner,
		HWND targetHwnd,
		std::function<void(const std::wstring& jsonMessage)> postJson);
	void Shutdown();

	void HandleWebMessageJson(const std::wstring& webMessageJson);
	void OnTimer(UINT_PTR timerId);
	void OnBridgePollCompleted(WPARAM wParam, LPARAM lParam);

	CBridge& Bridge() { return m_bridge; }
	const std::string& SessionId() const { return m_bridgeSessionId; }

private:
	blazeclaw::webview_routers::WebViewRouterContext BuildRouterContext();
	void PumpBridgeLifecycle();
	void PostBridgeLifecycleEvent(
		const wchar_t* state,
		const wchar_t* reason = nullptr,
		const std::string& provider = std::string(),
		const std::string& model = std::string(),
		const std::string& runtimeKind = std::string());
	void PostOpenClawWsClose(std::uint16_t code, const char* reason);
	void PostOpenClawWsFrameJson(const std::string& frameJson);
	void AppendDashboardStatus(const wchar_t* stage, const std::string& detail = std::string());

	CDashboardWnd* m_owner = nullptr;
	HWND m_targetHwnd = nullptr;
	std::function<void(const std::wstring& jsonMessage)> m_postJson;
	CBridge m_bridge;
	CEventTransport m_eventTransport;
	std::string m_bridgeSessionId = "dashboard";
	UINT_PTR m_bridgeTimerId = 0;
};

} // namespace blazeclaw::app::dashboard_bridge
