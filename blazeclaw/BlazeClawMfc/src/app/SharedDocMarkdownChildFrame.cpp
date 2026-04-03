// SharedDocMarkdownChildFrame.cpp : MDI child frame for Markdown view with shared document
//

#include "pch.h"
#include "framework.h"
#include "SharedDocMarkdownChildFrame.h"
#include "BlazeClawMarkdownView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CSharedDocMarkdownChildFrame

IMPLEMENT_DYNCREATE(CSharedDocMarkdownChildFrame, CMDIChildWndEx)

BEGIN_MESSAGE_MAP(CSharedDocMarkdownChildFrame, CMDIChildWndEx)
END_MESSAGE_MAP()

CSharedDocMarkdownChildFrame::CSharedDocMarkdownChildFrame() noexcept
{
}

CSharedDocMarkdownChildFrame::~CSharedDocMarkdownChildFrame()
{
}

BOOL CSharedDocMarkdownChildFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    // Use default frame window styles
    if (!CMDIChildWndEx::PreCreateWindow(cs))
        return FALSE;

    // Set window title
    cs.lpszName = _T("Markdown");
    cs.style &= ~FWS_ADDTOTITLE;  // Don't add document name to title

    return TRUE;
}
