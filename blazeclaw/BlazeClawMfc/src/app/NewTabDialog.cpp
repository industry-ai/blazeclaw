#include "pch.h"
#include "NewTabDialog.h"

IMPLEMENT_DYNAMIC(CNewTabDialog, CDialogEx)

CNewTabDialog::CNewTabDialog(CWnd* pParent)
    : CDialogEx(IDD_NEWTAB_DIALOG, pParent)
{
    TRACE(_T("[CNewTabDialog::CNewTabDialog] this=0x%p parent=0x%p\n"), this, pParent);
}

BOOL CNewTabDialog::OnInitDialog() {
    TRACE(_T("[CNewTabDialog::OnInitDialog]\n"));
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
    TRACE(_T("[CNewTabDialog::OnWebViewOnly] setting WebViewOnly\n"));
    m_selectedTab = NewTabType::WebViewOnly;
    EndDialog(IDOK);
}

void CNewTabDialog::OnWebViewChat() {
    TRACE(_T("[CNewTabDialog::OnWebViewChat] setting WebViewPlusChat\n"));
    m_selectedTab = NewTabType::WebViewPlusChat;
    EndDialog(IDOK);
}
