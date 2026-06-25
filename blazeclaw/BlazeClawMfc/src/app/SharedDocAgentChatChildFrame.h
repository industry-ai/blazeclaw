// SharedDocAgentChatChildFrame.h : MDI child frame for Agent Chat view with shared document
//

#pragma once

#include <afxext.h>

class CSharedDocAgentChatChildFrame : public CMDIChildWndEx
{
    DECLARE_DYNCREATE(CSharedDocAgentChatChildFrame)

public:
    CSharedDocAgentChatChildFrame() noexcept;
    virtual ~CSharedDocAgentChatChildFrame();

    virtual BOOL PreCreateWindow(CREATESTRUCT& cs) override;

protected:
    DECLARE_MESSAGE_MAP()
};
