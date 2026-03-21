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

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


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

							// Navigate to default URL
							if (m_webView)
							{
								m_webView->Navigate(L"https://www.baidu.com");
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
	if (m_webViewController)
	{
		m_webViewController->Close();
		m_webViewController.Reset();
	}
	m_webView.Reset();
#endif

	CView::OnDestroy();
}
