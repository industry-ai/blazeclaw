#pragma once

#include "pch.h"
#include "Resource.h"

class CApiKeyDialog : public CDialogEx {
public:
    CApiKeyDialog(CWnd* pParent = nullptr);
    CString m_apiKey;

protected:
    virtual BOOL OnInitDialog() override;
    virtual void DoDataExchange(CDataExchange* pDX) override;

    DECLARE_DYNAMIC(CApiKeyDialog)
    DECLARE_MESSAGE_MAP()
};
