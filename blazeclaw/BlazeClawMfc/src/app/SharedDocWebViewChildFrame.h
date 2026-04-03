// SharedDocWebViewChildFrame.h : MDI child frame for WebView view with shared document
//

#pragma once

#include <afxext.h>

class CSharedDocWebViewChildFrame : public CMDIChildWndEx
{
    DECLARE_DYNCREATE(CSharedDocWebViewChildFrame)

public:
    CSharedDocWebViewChildFrame() noexcept;
    virtual ~CSharedDocWebViewChildFrame();

    virtual BOOL PreCreateWindow(CREATESTRUCT& cs) override;

protected:
    DECLARE_MESSAGE_MAP()
};
