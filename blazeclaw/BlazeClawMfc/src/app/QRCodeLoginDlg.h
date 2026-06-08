#pragma once

#include "resource.h"
#include "framework.h"
#include <functional>
#include <string>
#include <vector>

// 前向声明
class QRCodeLoginService;
struct BindResult;
enum class QRCodeStatus;

class CQRCodeLoginDlg : public CDialogEx
{
    DECLARE_DYNAMIC(CQRCodeLoginDlg)

public:
    CQRCodeLoginDlg(CWnd* pParent = nullptr);
    virtual ~CQRCodeLoginDlg();

    enum { IDD = IDD_QRCODE_LOGIN };

    using LoginCallback = std::function<void(bool success, const std::wstring& message)>;

    void SetLoginCallback(LoginCallback callback) { m_loginCallback = callback; }

protected:
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;
    virtual void OnCancel() override;
    afx_msg void OnPaint();

    DECLARE_MESSAGE_MAP()

private:
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedRefresh();
    afx_msg void OnBnClickedCancel();

    bool RequestQrCode();
    bool CheckLoginStatus();
    void StopPolling();
    void StartPolling();
    void UpdateStatus(const std::wstring& status);
    void GenerateQrImage(const std::string& qrContent);

    CString m_loginStatus;
    LoginCallback m_loginCallback;
    UINT_PTR m_pollingTimer = 0;
    std::atomic<bool> m_isLoggedIn{false};
    std::atomic<bool> m_stopPolling{false};
    bool m_isExpired{false};
    std::string m_bindToken;
    std::vector<std::vector<bool>> m_qrData;

    static const int POLLING_INTERVAL_MS = 3000;
};
