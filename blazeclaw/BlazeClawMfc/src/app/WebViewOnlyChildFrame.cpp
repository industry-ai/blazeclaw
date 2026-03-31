// WebViewOnlyChildFrame.cpp : implementation of the CWebViewOnlyChildFrame class
//

#include "pch.h"
#include "framework.h"
#include "BlazeClawMFCApp.h"
#include "WebViewOnlyChildFrame.h"
#include "BlazeClawMFCView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CWebViewOnlyChildFrame

IMPLEMENT_DYNCREATE(CWebViewOnlyChildFrame, CMDIChildWndEx)

BEGIN_MESSAGE_MAP(CWebViewOnlyChildFrame, CMDIChildWndEx)
END_MESSAGE_MAP()

// CWebViewOnlyChildFrame construction/destruction

CWebViewOnlyChildFrame::CWebViewOnlyChildFrame() noexcept
{
}

CWebViewOnlyChildFrame::~CWebViewOnlyChildFrame()
{
}

BOOL CWebViewOnlyChildFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	if (!CMDIChildWndEx::PreCreateWindow(cs))
		return FALSE;

	return TRUE;
}

BOOL CWebViewOnlyChildFrame::OnCreateClient(LPCREATESTRUCT* /*lpcs*/, CCreateContext* pContext)
{
	// Create a single view pane that fills the entire client area - pure WebView mode
	if (!m_wndSplitter.CreateStatic(this, 1, 1))
	{
		TRACE0("Failed to create static splitter for WebView-only mode\n");
		return FALSE;
	}

	// Single pane: WebView (full window)
	if (!m_wndSplitter.CreateView(0, 0, RUNTIME_CLASS(CBlazeClawMFCView), CSize(100, 100), pContext))
	{
		TRACE0("Failed to create WebView-only pane\n");
		return FALSE;
	}

	m_wndSplitter.SetColumnInfo(0, 800, 100);
	m_wndSplitter.RecalcLayout();

	return TRUE;
}

// CWebViewOnlyChildFrame diagnostics

#ifdef _DEBUG
void CWebViewOnlyChildFrame::AssertValid() const
{
	CMDIChildWndEx::AssertValid();
}

void CWebViewOnlyChildFrame::Dump(CDumpContext& dc) const
{
	CMDIChildWndEx::Dump(dc);
}
#endif //_DEBUG

