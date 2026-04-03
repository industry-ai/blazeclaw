// SharedDocMarkdownChildFrame.h : MDI child frame for Markdown view with shared document
//

#pragma once

#include <afxext.h>

class CSharedDocMarkdownChildFrame : public CMDIChildWndEx
{
    DECLARE_DYNCREATE(CSharedDocMarkdownChildFrame)

public:
    CSharedDocMarkdownChildFrame() noexcept;
    virtual ~CSharedDocMarkdownChildFrame();

    virtual BOOL PreCreateWindow(CREATESTRUCT& cs) override;

protected:
    DECLARE_MESSAGE_MAP()
};
