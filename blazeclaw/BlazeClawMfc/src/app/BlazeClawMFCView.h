// This MFC Samples source code demonstrates using MFC Microsoft Office Fluent User Interface
// (the "Fluent UI") and is provided only as referential material to supplement the
// Microsoft Foundation Classes Reference and related electronic documentation
// included with the MFC C++ library software.
// License terms to copy, use or distribute the Fluent UI are available separately.
// To learn more about our Fluent UI licensing program, please visit
// https://go.microsoft.com/fwlink/?LinkId=238214.
//
// Copyright (C) Microsoft Corporation
// All rights reserved.

// BlazeClaw.MFCView.h : interface of the CBlazeClawMFCView class
//

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

#if defined(__has_include)
# if __has_include(<WebView2.h>)
# include <WebView2.h>
# define HAVE_WEBVIEW2_HEADER
# endif
#endif

#ifdef HAVE_WEBVIEW2_HEADER
# include <wrl.h>
# include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#else
// Forward declarations to avoid pulling in WebView2 headers
struct ICoreWebView2;
struct ICoreWebView2Controller;
#endif


class CBlazeClawMFCDoc;
class CBlazeClawMFCView : public CView
{
protected: // create from serialization only
	CBlazeClawMFCView() noexcept;
	DECLARE_DYNCREATE(CBlazeClawMFCView)

	// Attributes
public:
	CBlazeClawMFCDoc* GetDocument() const;

	// Operations
public:
	void ShowSkillSelection(
		const std::string& skillKey,
		const std::string& propertiesJson);
	bool OpenSkillConfigDocument(
		const std::string& skillKey,
		const std::string& propertiesJson = std::string());

	// Overrides
public:
	virtual void OnDraw(CDC* pDC);  // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
	virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
	virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
	virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);

	// Implementation
public:
	virtual ~CBlazeClawMFCView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:
	// WebView2 members
#ifdef HAVE_WEBVIEW2_HEADER
	ComPtr<ICoreWebView2Controller> m_webViewController;
	ComPtr<ICoreWebView2> m_webView;
	EventRegistrationToken m_webMessageToken{};
#else
	// keep raw pointers when header not available
	ICoreWebView2Controller* m_webViewController = nullptr;
	ICoreWebView2* m_webView = nullptr;
#endif
	UINT_PTR m_bridgeTimerId = 0;
	bool m_bridgeLastConnected = false;
	bool m_bridgeLifecycleSent = false;
	std::string m_bridgeSessionId = "main";
	std::uint64_t m_bridgeEventSeq = 0;
	std::uint64_t m_bridgeTraceReqCount = 0;
	std::uint64_t m_bridgeTraceResCount = 0;
	std::uint64_t m_bridgeTraceEventCount = 0;
	std::uint64_t m_bridgeTraceLastFlushTickMs = 0;
	std::string m_bridgeLastProvider;
	std::string m_bridgeLastModel;
	std::string m_bridgeLastRuntimeKind;
	std::unordered_set<std::string> m_reportedSkillPathRunIds;

	void InitializeWebViewBridge();
	void HandleWebMessageJson(const std::wstring& webMessageJson);
	bool HandleEmailConfigBridgeMessage(const std::string& messageJson);
	bool HandleSkillConfigBridgeMessage(const std::string& messageJson);
	bool OpenEmailConfigDocument();
	void PersistEmailConfigFromPayload(const std::string& payloadJson);
	void LoadEmailConfigToBridge();
	void LoadSkillConfigToBridge(
		const std::string& skillKey,
		const std::string& correlationId);
	void PersistSkillConfigFromPayload(
		const std::string& skillKey,
		const std::string& correlationId,
		const std::string& payloadJson);
	std::wstring BuildGeneratedSkillConfigPageUrl(
		const std::string& skillKey,
		const std::string& propertiesJson) const;
	std::wstring ResolveInitialNavigationUrl() const;
	void PostBridgeMessageJson(const std::wstring& jsonMessage);
	void PostOpenClawWsFrameJson(const std::string& frameJson);
	void PostOpenClawWsClose(std::uint16_t code, const char* reason);
	void EmitOpenClawChatEvents(const std::string& eventsArrayJson);
	void ReportRunSkillPathsToFindOutput(const std::string& runId);
	void EmitSkillPathLinesFromEvents(const std::string& eventsRaw);
	void EnsureOpenClawBridgeShim();
	void TraceBridgeTraffic(
		const char* kind,
		const std::string& detail = std::string());
	void FlushBridgeTraceIfNeeded();
	void PostBridgeLifecycleEvent(
		const wchar_t* state,
		const wchar_t* reason = nullptr,
		const std::string& provider = std::string(),
		const std::string& model = std::string(),
		const std::string& runtimeKind = std::string());
	void PumpBridgeLifecycle();

	// Generated message map functions
protected:
	afx_msg void OnFilePrintPreview();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	DECLARE_MESSAGE_MAP()
public:
	virtual void OnInitialUpdate();
	static void SetPendingStartupUrl(const std::wstring& url);
	static void ClearPendingStartupState();
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnDestroy();
};

#ifndef _DEBUG  // debug version in BlazeClaw.MFCView.cpp
inline CBlazeClawMFCDoc* CBlazeClawMFCView::GetDocument() const
{
	return reinterpret_cast<CBlazeClawMFCDoc*>(m_pDocument);
}
#endif

