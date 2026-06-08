#include "pch.h"
#include "framework.h"
#include "AIChatView.h"
#include "BlazeClawMFCApp.h"

#include <Shlwapi.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#ifdef HAVE_WEBVIEW2_HEADER
using namespace Microsoft::WRL;

constexpr UINT WM_WEBVIEW2_WEBMESSAGE_RECEIVED = WM_USER + 100;
constexpr LPCWSTR WEBVIEW_HOST_NAME = L"app.localhost";
constexpr LPCWSTR WEBVIEW_INDEX_FILE = L"index.html";

IMPLEMENT_DYNCREATE(CAIChatView, CView)

BEGIN_MESSAGE_MAP(CAIChatView, CView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_MESSAGE(WM_WEBVIEW2_WEBMESSAGE_RECEIVED, &CAIChatView::OnWebMessageReceived)
END_MESSAGE_MAP()

CAIChatView::CAIChatView() noexcept
	: m_webAssetsPath()
{}

CAIChatView::~CAIChatView()
{
}

#ifdef _DEBUG
void CAIChatView::AssertValid() const
{
	CView::AssertValid();
}

void CAIChatView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}
#endif

BOOL CAIChatView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CView::PreCreateWindow(cs);
}

int CAIChatView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CView::OnCreate(lpCreateStruct) == -1)
	{
		return -1;
	}

	if (!InitWebView())
	{
		TRACE0("Failed to initialize WebView2\n");
	}

	return 0;
}

void CAIChatView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);

	if (m_webViewController != nullptr && cx > 0 && cy > 0)
	{
		CRect rc(0, 0, cx, cy);
		m_webViewController->put_Bounds(rc);
	}
}

void CAIChatView::OnDraw(CDC* /*pDC*/)
{
}

void CAIChatView::OnDestroy()
{
	if (m_webView != nullptr)
	{
		m_webView->remove_WebMessageReceived(m_webMessageToken);
		m_webView = nullptr;
	}
	m_webViewController = nullptr;

	CView::OnDestroy();
}

LRESULT CAIChatView::OnWebMessageReceived(WPARAM, LPARAM)
{
	return 0;
}

std::wstring CAIChatView::GetWebAssetsPath() const
{
	TCHAR exePath[MAX_PATH] = {};
	if (!GetModuleFileName(nullptr, exePath, MAX_PATH))
	{
		return std::wstring();
	}

	PathRemoveFileSpec(exePath);

	std::wstring webAssetsPath(exePath);
	webAssetsPath += L"\\dist";

	return webAssetsPath;
}

bool CAIChatView::InitWebView()
{
	CRect rcClient;
	GetClientRect(&rcClient);

	return CreateWebViewController();
}

bool CAIChatView::CreateWebViewController()
{
	auto hwnd = GetSafeHwnd();
	if (hwnd == nullptr)
	{
		TRACE("CreateWebViewController: HWND is null\n");
		return false;
	}

	CRect rcClient;
	GetClientRect(&rcClient);

	m_webAssetsPath = GetWebAssetsPath();
	TRACE("Web assets folder: %ls\n", m_webAssetsPath.c_str());

	if (GetFileAttributes(m_webAssetsPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		TRACE("Warning: web assets folder not found at %ls\n", m_webAssetsPath.c_str());
	}

	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
		nullptr, nullptr, nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[this, rcClient](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
			{
				if (FAILED(result))
				{
					TRACE("Failed to create WebView2 environment: 0x%08X\n", result);
					return result;
				}

				if (environment == nullptr)
				{
					TRACE("WebView2 environment is null\n");
					return E_POINTER;
				}

				return environment->CreateCoreWebView2Controller(
					GetSafeHwnd(),
					Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[this, rcClient](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
						{
							if (FAILED(result))
							{
								TRACE("Failed to create WebView2 controller: 0x%08X\n", result);
								return result;
							}

							if (controller == nullptr)
							{
								TRACE("WebView2 controller is null\n");
								return E_POINTER;
							}

							m_webViewController = controller;
							HRESULT hr = controller->get_CoreWebView2(&m_webView);
							if (FAILED(hr))
							{
								TRACE("Failed to get CoreWebView2: 0x%08X\n", hr);
								return hr;
							}

							controller->put_Bounds(rcClient);

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
									WEBVIEW_HOST_NAME,
									m_webAssetsPath.c_str(),
									COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);

								TRACE("SetVirtualHostNameToFolderMapping succeeded\n");

								std::wstring mappedUrl = L"https://";
								mappedUrl += WEBVIEW_HOST_NAME;
								mappedUrl += L"/";
								mappedUrl += WEBVIEW_INDEX_FILE;
								TRACE("Navigating to: %ls\n", mappedUrl.c_str());
								m_webView->Navigate(mappedUrl.c_str());
							}
							else
							{
								TRACE("ICoreWebView2_3 not available, falling back to file://\n");

								std::wstring filePath = m_webAssetsPath + L"\\" + WEBVIEW_INDEX_FILE;
								if (PathFileExists(filePath.c_str()))
								{
									std::wstring url = L"file:///";
									url += filePath;
									TRACE("Fallback navigating to: %ls\n", url.c_str());
									m_webView->Navigate(url.c_str());
								}
								else
								{
									TRACE("HTML file not found: %ls\n", filePath.c_str());
								}
							}

							SetupWebViewEvents();

							return S_OK;
						}).Get());
			}).Get());

	if (FAILED(hr))
	{
		TRACE("CreateCoreWebView2EnvironmentWithOptions failed: 0x%08X\n", hr);
		return false;
	}

	return true;
}

void CAIChatView::SetupWebViewEvents()
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
					PostMessage(WM_WEBVIEW2_WEBMESSAGE_RECEIVED);
					CoTaskMemFree(rawMessage);
				}

				return S_OK;
			}).Get(),
		&m_webMessageToken);
}

#else

IMPLEMENT_DYNCREATE(CAIChatView, CView)

BEGIN_MESSAGE_MAP(CAIChatView, CView)
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

CAIChatView::CAIChatView() noexcept
{
}

CAIChatView::~CAIChatView()
{
}

#ifdef _DEBUG
void CAIChatView::AssertValid() const
{
	CView::AssertValid();
}

void CAIChatView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}
#endif

BOOL CAIChatView::PreCreateWindow(CREATESTRUCT& cs)
{
	return CView::PreCreateWindow(cs);
}

int CAIChatView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CView::OnCreate(lpCreateStruct) == -1)
	{
		return -1;
	}

	TRACE0("WebView2 is not available. Please install WebView2 runtime.\n");
	return 0;
}

void CAIChatView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);
}

void CAIChatView::OnDraw(CDC* pDC)
{
	CRect rc;
	GetClientRect(&rc);
	pDC->DrawText(_T("WebView2 is not available.\nPlease install WebView2 runtime."), rc, DT_CENTER | DT_VCENTER);
}

void CAIChatView::OnDestroy()
{
	CView::OnDestroy();
}

LRESULT CAIChatView::OnWebMessageReceived(WPARAM, LPARAM)
{
	return 0;
}

std::wstring CAIChatView::GetWebAssetsPath() const
{
	return std::wstring();
}

bool CAIChatView::InitWebView()
{
	return false;
}

#endif
