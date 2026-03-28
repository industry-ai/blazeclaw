#include "pch.h"
#include "ApiKeyDialog.h"

IMPLEMENT_DYNAMIC(CApiKeyDialog, CDialogEx)

CApiKeyDialog::CApiKeyDialog(CWnd* pParent)
    : CDialogEx(IDD_APIKEY_DIALOG, pParent)
{
}

BOOL CApiKeyDialog::OnInitDialog() {
    CDialogEx::OnInitDialog();
    CString title;
    if (title.LoadString(IDS_DEEPSEEK_DIALOG_TITLE)) {
        SetWindowText(title);
    }

    CString label;
    if (label.LoadString(IDS_DEEPSEEK_DIALOG_LABEL)) {
        SetDlgItemText(IDC_STATIC_APIKEY_LABEL, label);
    }

    SetDlgItemText(IDC_EDIT_APIKEY, m_apiKey);
    return TRUE;
}

void CApiKeyDialog::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_EDIT_APIKEY, m_apiKey);
}

BEGIN_MESSAGE_MAP(CApiKeyDialog, CDialogEx)
END_MESSAGE_MAP()
