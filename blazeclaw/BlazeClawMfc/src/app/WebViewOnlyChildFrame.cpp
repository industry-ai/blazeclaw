// WebViewOnlyChildFrame.cpp : implementation of the CWebViewOnlyChildFrame class
//

#include "pch.h"
#include "framework.h"
#include "BlazeClawMFCApp.h"
#include "WebViewOnlyChildFrame.h"

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

// No OnCreateClient override: CMultiDocTemplate already pairs this frame with CBlazeClawMFCView.
// CMDIChildWndEx::OnCreateClient creates that single view. A CSplitterWnd cannot be 1x1 — MFC
// asserts (CreateStatic requires at least two panes).

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
