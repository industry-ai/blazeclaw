// SharedDocWebViewChildFrame.cpp : MDI child frame for WebView view with shared document
//

#include "pch.h"
#include "framework.h"
#include "SharedDocWebViewChildFrame.h"
#include "BlazeClawMFCView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CSharedDocWebViewChildFrame

IMPLEMENT_DYNCREATE(CSharedDocWebViewChildFrame, CMDIChildWndEx)

BEGIN_MESSAGE_MAP(CSharedDocWebViewChildFrame, CMDIChildWndEx)
END_MESSAGE_MAP()

CSharedDocWebViewChildFrame::CSharedDocWebViewChildFrame() noexcept
{
}

CSharedDocWebViewChildFrame::~CSharedDocWebViewChildFrame()
{
}

BOOL CSharedDocWebViewChildFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    // Use default frame window styles
    if (!CMDIChildWndEx::PreCreateWindow(cs))
        return FALSE;

    // Set window title
    cs.lpszName = _T("WebView");
    cs.style &= ~FWS_ADDTOTITLE;  // Don't add document name to title

    return TRUE;
}
