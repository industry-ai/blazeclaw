#pragma once

#include "pch.h"
#include "Resource.h"

enum class NewTabType {
    WebViewOnly,
    WebViewPlusChat
};

class CNewTabDialog : public CDialogEx {
public:
    CNewTabDialog(CWnd* pParent = nullptr);
    NewTabType GetSelectedTabType() const { return m_selectedTab; }

protected:
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;
    afx_msg void OnWebViewOnly();
    afx_msg void OnWebViewChat();

    DECLARE_DYNAMIC(CNewTabDialog)
    DECLARE_MESSAGE_MAP()

private:
    NewTabType m_selectedTab = NewTabType::WebViewOnly;
};
