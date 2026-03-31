#include "pch.h"
#include "NewTabDialog.h"

IMPLEMENT_DYNAMIC(CNewTabDialog, CDialogEx)

CNewTabDialog::CNewTabDialog(CWnd* pParent)
    : CDialogEx(IDD_NEWTAB_DIALOG, pParent)
{
}

BOOL CNewTabDialog::OnInitDialog() {
    CDialogEx::OnInitDialog();
    SetWindowText(_T("New Tab"));
    return TRUE;
}

void CNewTabDialog::DoDataExchange(CDataExchange* pDX) {
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CNewTabDialog, CDialogEx)
    ON_BN_CLICKED(ID_NEW_TAB_WEBVIEW_ONLY, &CNewTabDialog::OnWebViewOnly)
    ON_BN_CLICKED(ID_NEW_TAB_WEBVIEW_CHAT, &CNewTabDialog::OnWebViewChat)
END_MESSAGE_MAP()

void CNewTabDialog::OnWebViewOnly() {
    m_selectedTab = NewTabType::WebViewOnly;
    EndDialog(IDOK);
}

void CNewTabDialog::OnWebViewChat() {
    m_selectedTab = NewTabType::WebViewPlusChat;
    EndDialog(IDOK);
}
