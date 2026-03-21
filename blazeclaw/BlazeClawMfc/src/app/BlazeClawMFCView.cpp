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

// BlazeClaw.MFCView.cpp : implementation of the CBlazeClawMFCView class
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "BlazeClawMFCApp.h"
#endif

#include "BlazeClawMFCDoc.h"
#include "BlazeClawMFCView.h"
#include "../gateway/GatewayJsonUtils.h"
#include "../gateway/GatewayProtocolModels.h"

#include <cwctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {

constexpr UINT_PTR kBridgeLifecycleTimerId = 0x4A21;
constexpr UINT kBridgeLifecycleTimerMs = 1000;

std::string ToNarrow(const std::wstring& value)
{
	std::string output;
	output.reserve(value.size());
	for (const wchar_t ch : value)
	{
		output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
	}
	return output;
}

std::wstring ToWide(const std::string& value)
{
	std::wstring output;
	output.reserve(value.size());
	for (const char ch : value)
	{
		output.push_back(
			static_cast<wchar_t>(
				static_cast<unsigned char>(ch)));
	}
	return output;
}

std::string EscapeJson(const std::string& value)
{
	std::string escaped;
	escaped.reserve(value.size() + 8);
	for (const char ch : value)
	{
		switch (ch)
		{
		case '"':
			escaped += "\\\"";
			break;
		case '\\':
			escaped += "\\\\";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped.push_back(ch);
			break;
		}
	}
	return escaped;
}

std::string JsonString(const std::string& value)
{
	return std::string("\"") + EscapeJson(value) + "\"";
}

std::string BuildBridgeRpcResultJson(
	const blazeclaw::gateway::protocol::ResponseFrame& response,
	const std::string& correlationId)
{
	std::string json =
		"{\"channel\":\"blazeclaw.gateway.rpc.result\",\"id\":" +
		JsonString(correlationId) +
		",\"ok\":" +
		(response.ok ? "true" : "false");

	if (response.ok)
	{
		if (response.payloadJson.has_value())
		{
			json += ",\"payload\":" + response.payloadJson.value();
		}
		else
		{
			json += ",\"payload\":null";
		}
	}
	else
	{
		json += ",\"error\":";
		if (response.error.has_value())
		{
			const auto& error = response.error.value();
			json += "{";
			json += "\"code\":" + JsonString(error.code);
			json += ",\"message\":" + JsonString(error.message);
			if (error.detailsJson.has_value())
			{
				json += ",\"details\":" + error.detailsJson.value();
			}
			json += "}";
		}
		else
		{
			json +=
				"{\"code\":\"error_unknown\",\"message\":\"Gateway request failed.\"}";
		}
	}

	json += "}";
	return json;
}

std::optional<std::wstring> GetEnvValue(const wchar_t* name)
{
	if (name == nullptr || *name == L'\0')
	{
		return std::nullopt;
	}

	size_t valueLength = 0;
	wchar_t* rawValue = nullptr;
	if (_wdupenv_s(&rawValue, &valueLength, name) != 0 || rawValue == nullptr)
	{
		return std::nullopt;
	}

	std::wstring value(rawValue);
	free(rawValue);
	if (value.empty())
	{
		return std::nullopt;
	}

	return value;
}

std::wstring TrimCopy(std::wstring value)
{
	auto isSpace = [](wchar_t ch)
	{
      return ::iswspace(ch) != 0;
	};

	while (!value.empty() && isSpace(value.front()))
	{
		value.erase(value.begin());
	}

	while (!value.empty() && isSpace(value.back()))
	{
		value.pop_back();
	}

	return value;
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
		default:
			escaped.push_back(ch);
			break;
		}
	}

	return L"file://" + escaped;
}

std::optional<std::filesystem::path> FindOpenClawUiIndex(const std::filesystem::path& start)
{
	std::filesystem::path cursor = start;
	while (!cursor.empty())
	{
		const auto direct = cursor / L"openclaw" / L"ui" / L"index.html";
		if (std::filesystem::exists(direct))
		{
			return direct;
		}

		const auto dist = cursor / L"openclaw" / L"ui" / L"dist" / L"index.html";
		if (std::filesystem::exists(dist))
		{
			return dist;
		}

		if (!cursor.has_parent_path())
		{
			break;
		}

		auto parent = cursor.parent_path();
		if (parent == cursor)
		{
			break;
		}

		cursor = parent;
	}

	return std::nullopt;
}

std::wstring ResolveChatStartupUrl()
{
	if (const auto envUrl = GetEnvValue(L"OPENCLAW_UI_DEV_URL"); envUrl.has_value())
	{
		const std::wstring value = TrimCopy(envUrl.value());
		if (!value.empty())
		{
			return value;
		}
	}

	if (const auto envUrl = GetEnvValue(L"BLAZECLAW_CHAT_DEV_URL"); envUrl.has_value())
	{
		const std::wstring value = TrimCopy(envUrl.value());
		if (!value.empty())
		{
			return value;
		}
	}

	const auto mode = GetEnvValue(L"BLAZECLAW_CHAT_UI_MODE");
	if (mode.has_value())
	{
		std::wstring normalized = TrimCopy(mode.value());
		for (wchar_t& ch : normalized)
		{
           ch = static_cast<wchar_t>(::towlower(ch));
		}

		if (normalized == L"dev")
		{
			return L"http://127.0.0.1:5173";
		}
	}

	std::vector<std::filesystem::path> roots;
	roots.push_back(std::filesystem::current_path());

	wchar_t modulePath[MAX_PATH]{};
	if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
	{
		roots.push_back(std::filesystem::path(modulePath).parent_path());
	}

	for (const auto& root : roots)
	{
		if (const auto found = FindOpenClawUiIndex(root); found.has_value())
		{
			return BuildFileUrl(found.value());
		}
	}

	return {};
}

void ShowChatStartupError(
	ICoreWebView2* webView,
	const wchar_t* title,
	const wchar_t* details)
{
	if (webView == nullptr)
	{
		return;
	}

	std::wstring html =
		L"<html><body style='font-family:Segoe UI;padding:20px;'>"
		L"<h2>BlazeClaw Chat Startup Error</h2>"
		L"<p><strong>";
	html += (title != nullptr ? title : L"Unable to load chat UI");
	html += L"</strong></p><p>";
	html += (details != nullptr ? details : L"");
	html += L"</p></body></html>";

	webView->NavigateToString(html.c_str());
}

} // namespace


// CBlazeClawMFCView

IMPLEMENT_DYNCREATE(CBlazeClawMFCView, CView)

BEGIN_MESSAGE_MAP(CBlazeClawMFCView, CView)
	// Standard printing commands
	ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CBlazeClawMFCView::OnFilePrintPreview)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_WM_SIZE()
	ON_WM_DESTROY()
   ON_WM_TIMER()
END_MESSAGE_MAP()

// CBlazeClawMFCView construction/destruction

CBlazeClawMFCView::CBlazeClawMFCView() noexcept
{
	EnableActiveAccessibility();
}

CBlazeClawMFCView::~CBlazeClawMFCView()
{
}

BOOL CBlazeClawMFCView::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	return CView::PreCreateWindow(cs);
}

// CBlazeClawMFCView drawing

void CBlazeClawMFCView::OnDraw(CDC* pDC)
{
	//CBlazeClawMFCDoc* pDoc = GetDocument();
	//ASSERT_VALID(pDoc);
	//if (!pDoc)
	//	return;

	// If WebView2 is available and initialized it will paint itself. Otherwise draw a placeholder.
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webView)
	{
		// nothing: WebView2 paints itself
		return;
	}
#endif
	if (!pDC)
		return;

	CRect rc;
	GetClientRect(&rc);
	COLORREF bgcolor = GetSysColor(COLOR_WINDOW);
	pDC->FillSolidRect(&rc, bgcolor);
	CString msg = L"WebView2 content not available. Ensure WebView2 runtime is installed.";
	pDC->SetTextColor(RGB(0, 0, 0));
	pDC->SetBkMode(TRANSPARENT);
	pDC->DrawText(msg, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}


// CBlazeClawMFCView printing


void CBlazeClawMFCView::OnFilePrintPreview()
{
#ifndef SHARED_HANDLERS
	AFXPrintPreview(this);
#endif
}

BOOL CBlazeClawMFCView::OnPreparePrinting(CPrintInfo* pInfo)
{
	// default preparation
	return DoPreparePrinting(pInfo);
}

void CBlazeClawMFCView::PostBridgeMessageJson(const std::wstring& jsonMessage)
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (!m_webView || jsonMessage.empty())
	{
		return;
	}

	m_webView->PostWebMessageAsJson(jsonMessage.c_str());
#else
	UNREFERENCED_PARAMETER(jsonMessage);
#endif
}

void CBlazeClawMFCView::PostBridgeLifecycleEvent(
	const wchar_t* state,
	const wchar_t* reason)
{
	std::wstringstream stream;
	stream
		<< L"{\"channel\":\"blazeclaw.gateway.lifecycle\",\"sessionId\":\""
		<< ToWide(m_bridgeSessionId)
		<< L"\",\"state\":\""
		<< (state != nullptr ? state : L"unknown")
		<< L"\"";

	if (reason != nullptr && *reason != L'\0')
	{
		stream << L",\"reason\":\"" << reason << L"\"";
	}

	stream << L"}";
	PostBridgeMessageJson(stream.str());
}

void CBlazeClawMFCView::PumpBridgeLifecycle()
{
	const auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	const bool connected = app != nullptr && app->Services().IsRunning();

	if (!m_bridgeLifecycleSent)
	{
		PostBridgeLifecycleEvent(
			connected ? L"connected" : L"disconnected",
			connected ? L"service-ready" : L"service-not-running");
		m_bridgeLifecycleSent = true;
		m_bridgeLastConnected = connected;
		return;
	}

	if (connected == m_bridgeLastConnected)
	{
		return;
	}

	if (connected)
	{
		PostBridgeLifecycleEvent(L"reconnected", L"service-ready");
	}
	else
	{
		PostBridgeLifecycleEvent(L"disconnected", L"service-stopped");
	}

	m_bridgeLastConnected = connected;
}

void CBlazeClawMFCView::HandleWebMessageJson(const std::wstring& webMessageJson)
{
#ifdef HAVE_WEBVIEW2_HEADER
	const std::string message = ToNarrow(webMessageJson);
	std::string channel;
	if (!blazeclaw::gateway::json::FindStringField(message, "channel", channel))
	{
		return;
	}

	if (channel == "blazeclaw.gateway.lifecycle.subscribe")
	{
		m_bridgeLifecycleSent = false;
		PumpBridgeLifecycle();
		return;
	}

	if (channel != "blazeclaw.gateway.rpc")
	{
		return;
	}

	std::string correlationId;
	if (!blazeclaw::gateway::json::FindStringField(message, "id", correlationId))
	{
		correlationId = "rpc-unknown";
	}

	std::string method;
	blazeclaw::gateway::json::FindStringField(message, "method", method);

	std::string paramsJsonRaw;
	std::optional<std::string> paramsJson;
	if (blazeclaw::gateway::json::FindRawField(message, "params", paramsJsonRaw))
	{
		paramsJson = blazeclaw::gateway::json::Trim(paramsJsonRaw);
	}

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		const std::string errorJson =
			"{\"channel\":\"blazeclaw.gateway.rpc.result\",\"id\":" +
			JsonString(correlationId) +
			",\"ok\":false,\"error\":{\"code\":\"app_unavailable\",\"message\":\"Application context unavailable.\"}}";
		PostBridgeMessageJson(ToWide(errorJson));
		return;
	}

	const blazeclaw::gateway::protocol::RequestFrame request{
		.id = correlationId,
		.method = method,
		.paramsJson = paramsJson,
	};
	const auto response = app->Services().RouteGatewayRequest(request);
	const std::string responseJson = BuildBridgeRpcResultJson(response, correlationId);
	PostBridgeMessageJson(ToWide(responseJson));
#else
	UNREFERENCED_PARAMETER(webMessageJson);
#endif
}

void CBlazeClawMFCView::InitializeWebViewBridge()
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (!m_webView)
	{
		return;
	}

	m_webView->add_WebMessageReceived(
		Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
			[this](
				ICoreWebView2* sender,
				ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
			{
				UNREFERENCED_PARAMETER(sender);
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

	if (m_bridgeTimerId == 0)
	{
		m_bridgeTimerId = SetTimer(
			kBridgeLifecycleTimerId,
			kBridgeLifecycleTimerMs,
			nullptr);
	}

	m_bridgeLifecycleSent = false;
	PumpBridgeLifecycle();
#endif
}

void CBlazeClawMFCView::OnInitialUpdate()
{
	CView::OnInitialUpdate();

#ifdef HAVE_WEBVIEW2_HEADER
	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
		Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[&](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
			{
				if (FAILED(result) || !env)
					return result;

				env->CreateCoreWebView2Controller(this->GetSafeHwnd(),
					Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[&, env](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
						{
							if (FAILED(result) || !controller)
								return result;

							m_webViewController = controller;
							m_webViewController->get_CoreWebView2(&m_webView);

							// Resize to fit
							CRect rc;
							GetClientRect(&rc);
							m_webViewController->put_Bounds(rc);

							// Make controller visible
							m_webViewController->put_IsVisible(TRUE);
							InitializeWebViewBridge();

							// Navigate to default URL
							if (m_webView)
							{
                              m_webView->add_NavigationCompleted(
									Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
										[this](
											ICoreWebView2* sender,
											ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
										{
											BOOL isSuccess = FALSE;
											if (args != nullptr)
											{
												args->get_IsSuccess(&isSuccess);
											}

											if (!isSuccess)
											{
												ShowChatStartupError(
													sender,
													L"Navigation failed",
													L"Verify OpenClaw UI assets or dev server availability.");
											}

											return S_OK;
										}).Get(),
									nullptr);

								const std::wstring startupUrl = ResolveChatStartupUrl();
								if (startupUrl.empty())
								{
									ShowChatStartupError(
										m_webView.Get(),
										L"No startup URL resolved",
										L"Set OPENCLAW_UI_DEV_URL or ensure openclaw/ui/index.html exists.");
								}
								else
								{
									m_webView->Navigate(startupUrl.c_str());
								}
							}

							return S_OK;
						}).Get());

				return S_OK;
			}).Get());

	if (FAILED(hr))
	{
		TRACE(_T("Failed to initialize WebView2 environment."));
		AfxMessageBox(L"Failed to initialize WebView2 environment. Please install the WebView2 runtime.");
	}
#else
	TRACE(_T("WebView2 headers not found at compile time."));
	AfxMessageBox(L"WebView2 headers not found at compile time.");
#endif
}

void CBlazeClawMFCView::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: add extra initialization before printing
}

void CBlazeClawMFCView::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: add cleanup after printing
}

void CBlazeClawMFCView::OnRButtonUp(UINT /* nFlags */, CPoint point)
{
	ClientToScreen(&point);
	OnContextMenu(this, point);
}

void CBlazeClawMFCView::OnContextMenu(CWnd* /* pWnd */, CPoint point)
{
#ifndef SHARED_HANDLERS
	theApp.GetContextMenuManager()->ShowPopupMenu(IDR_POPUP_EDIT, point.x, point.y, this, TRUE);
#endif
}


// CBlazeClawMFCView diagnostics

#ifdef _DEBUG
void CBlazeClawMFCView::AssertValid() const
{
	CView::AssertValid();
}

void CBlazeClawMFCView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CBlazeClawMFCDoc* CBlazeClawMFCView::GetDocument() const // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CBlazeClawMFCDoc)));
	return (CBlazeClawMFCDoc*)m_pDocument;
}
#endif //_DEBUG


// CBlazeClawMFCView message handlers

void CBlazeClawMFCView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);

#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webViewController)
	{
		CRect rc;
		GetClientRect(&rc);
		m_webViewController->put_Bounds(rc);
	}
#endif
}

void CBlazeClawMFCView::OnDestroy()
{
#ifdef HAVE_WEBVIEW2_HEADER
    if (m_bridgeTimerId != 0)
	{
		KillTimer(m_bridgeTimerId);
		m_bridgeTimerId = 0;
	}

	if (m_webView)
	{
		m_webView->remove_WebMessageReceived(m_webMessageToken);
	}

	if (m_webViewController)
	{
		m_webViewController->Close();
		m_webViewController.Reset();
	}
	m_webView.Reset();
#endif

	CView::OnDestroy();
}

void CBlazeClawMFCView::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == m_bridgeTimerId && nIDEvent != 0)
	{
		PumpBridgeLifecycle();
	}

	CView::OnTimer(nIDEvent);
}
