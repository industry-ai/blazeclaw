// SharedDocAgentChatChildFrame.cpp : MDI child frame for Agent Chat view with shared document
//

#include "pch.h"
#include "framework.h"
#include "SharedDocAgentChatChildFrame.h"
#include "BlazeClawAgentChatView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CSharedDocAgentChatChildFrame

IMPLEMENT_DYNCREATE(CSharedDocAgentChatChildFrame, CMDIChildWndEx)

BEGIN_MESSAGE_MAP(CSharedDocAgentChatChildFrame, CMDIChildWndEx)
END_MESSAGE_MAP()

CSharedDocAgentChatChildFrame::CSharedDocAgentChatChildFrame() noexcept
{
}

CSharedDocAgentChatChildFrame::~CSharedDocAgentChatChildFrame()
{
}

BOOL CSharedDocAgentChatChildFrame::PreCreateWindow(CREATESTRUCT& cs)
{
    if (!CMDIChildWndEx::PreCreateWindow(cs))
        return FALSE;

    cs.lpszName = _T("Agent Chat");
    cs.style &= ~FWS_ADDTOTITLE;

    return TRUE;
}
