#include "pch.h"
#include "QRCodeLoginDlg.h"
#include "QRCodeLoginService.h"
#include "afxdialogex.h"
#include "Logger.h"
#include "qrcodegen.hpp"

#include <vector>
#include <time.h>
#include <string>
#include <algorithm>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using namespace qrcodegen;

IMPLEMENT_DYNAMIC(CQRCodeLoginDlg, CDialogEx)

CQRCodeLoginDlg::CQRCodeLoginDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_QRCODE_LOGIN, pParent)
    , m_loginStatus(_T("正在获取二维码..."))
    , m_pollingTimer(0)
    , m_isExpired(false)
{
}

CQRCodeLoginDlg::~CQRCodeLoginDlg()
{
    StopPolling();
}

void CQRCodeLoginDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Text(pDX, IDC_STATIC_STATUS, m_loginStatus);
}

BEGIN_MESSAGE_MAP(CQRCodeLoginDlg, CDialogEx)
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_BUTTON_REFRESH, &CQRCodeLoginDlg::OnBnClickedRefresh)
    ON_BN_CLICKED(IDCANCEL, &CQRCodeLoginDlg::OnBnClickedCancel)
    ON_WM_PAINT()
END_MESSAGE_MAP()

BOOL CQRCodeLoginDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    if (RequestQrCode())
    {
        StartPolling();
    }
    else
    {
        UpdateStatus(_T("获取二维码失败，请重试"));
    }

    return TRUE;
}

void CQRCodeLoginDlg::OnCancel()
{
    StopPolling();
    QRCodeLoginService::Instance().Stop();
    CDialogEx::OnCancel();
}

void CQRCodeLoginDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1 && !m_isLoggedIn.load())
    {
        CheckLoginStatus();
    }
    CDialogEx::OnTimer(nIDEvent);
}

void CQRCodeLoginDlg::OnPaint()
{
    CPaintDC dc(this);

    if (m_qrData.empty()) {
        LOG_INFO("[CQRCodeLoginDlg] OnPaint: m_qrData is empty, skipping");
        return;
    }

    LOG_INFO("[CQRCodeLoginDlg] OnPaint: drawing QR code {}x{}", m_qrData.size(), m_qrData.size());

    CWnd* pStatic = GetDlgItem(IDC_STATIC_QRCODE);
    if (!pStatic) return;

    CRect rc;
    pStatic->GetClientRect(&rc);

    CPoint pt(0, 0);
    pStatic->ClientToScreen(&pt);
    ScreenToClient(&pt);

    CDC memDC;
    CBitmap bmp;
    memDC.CreateCompatibleDC(&dc);
    bmp.CreateCompatibleBitmap(&dc, rc.Width(), rc.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&bmp);

    memDC.FillSolidRect(&rc, RGB(255, 255, 255));

    int qrSize = (int)m_qrData.size();
    int padding = 4;
    int availableSize = min(rc.Width(), rc.Height()) - padding * 2;
    int scale = availableSize / qrSize;
    if (scale < 1) scale = 1;
    int qrPixelSize = scale * qrSize;
    int offsetX = (rc.Width() - qrPixelSize) / 2;
    int offsetY = (rc.Height() - qrPixelSize) / 2;

    for (int y = 0; y < qrSize; y++) {
        for (int x = 0; x < qrSize; x++) {
            if (m_qrData[y][x]) {
                CRect cellRect(
                    offsetX + x * scale,
                    offsetY + y * scale,
                    offsetX + (x + 1) * scale,
                    offsetY + (y + 1) * scale
                );
                memDC.FillSolidRect(&cellRect, RGB(0, 0, 0));
            }
        }
    }

    dc.BitBlt(pt.x, pt.y, rc.Width(), rc.Height(), &memDC, 0, 0, SRCCOPY);
    memDC.SelectObject(pOldBmp);
}

void CQRCodeLoginDlg::OnBnClickedRefresh()
{
    StopPolling();
    m_isLoggedIn.store(false);
    m_isExpired = false;
    UpdateStatus(_T("正在获取二维码..."));
    m_qrData.clear();
    Invalidate();
    // 清理之前的绑定状态
    QRCodeLoginService::Instance().Stop();

    if (RequestQrCode())
    {
        StartPolling();
    }
    else
    {
        UpdateStatus(_T("获取二维码失败，请重试"));
    }
}

void CQRCodeLoginDlg::OnBnClickedCancel()
{
    StopPolling();
    QRCodeLoginService::Instance().Stop();
    if (m_loginCallback)
    {
        m_loginCallback(false, L"用户取消登录");
    }
    CDialogEx::OnCancel();
}

bool CQRCodeLoginDlg::RequestQrCode()
{
    printf("[CQRCodeLoginDlg] RequestQrCode() called\n");
    fflush(stdout);

    try
    {
        LOG_INFO("[CQRCodeLoginDlg] RequestQrCode() called");

        // 获取设备信息（与 TV 端一致）
        std::string deviceType = "WINDOWS";

        // 获取设备名称
        wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1];
        DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
        std::string deviceName = "Windows PC";
        if (GetComputerNameW(computerName, &size)) {
            deviceName = CW2A(computerName).m_psz;
        }

        // 生成设备指纹（使用机器ID或随机UUID）
        std::string deviceFingerprint;
        {
            // 使用时间戳作为简单的设备指纹（生产环境应使用更稳定的设备ID）
            deviceFingerprint = "win_" + std::to_string(time(nullptr));
        }

        printf("[CQRCodeLoginDlg] Creating bind: type=%s, name=%s\n", deviceType.c_str(), deviceName.c_str());
        fflush(stdout);

        LOG_INFO("[CQRCodeLoginDlg] Creating bind: type={}, name={}, fingerprint={}",
                 deviceType, deviceName, deviceFingerprint);

        // 调用 QRCodeLoginService 创建绑定会话
        printf("[CQRCodeLoginDlg] Calling QRCodeLoginService::Instance().CreateBind()\n");
        fflush(stdout);

        bool success = QRCodeLoginService::Instance().CreateBind(
            deviceType,
            deviceName,
            deviceFingerprint
        );

        printf("[CQRCodeLoginDlg] CreateBind returned: %s\n", success ? "true" : "false");
        fflush(stdout);

        if (!success) {
            LOG_ERROR("[CQRCodeLoginDlg] CreateBind failed");
            return false;
        }

        // 获取二维码内容
        std::string qrPayload = QRCodeLoginService::Instance().GetQrPayload();
        m_bindToken = QRCodeLoginService::Instance().GetBindToken();

        LOG_INFO("[CQRCodeLoginDlg] Bind created successfully");
        LOG_INFO("[CQRCodeLoginDlg] bind_token: {}", m_bindToken);
        LOG_INFO("[CQRCodeLoginDlg] qr_payload: {}", qrPayload);

        // 确认 qrPayload 不为空
        if (qrPayload.empty()) {
            LOG_ERROR("[CQRCodeLoginDlg] qrPayload is empty!");
            return false;
        }

        // 生成二维码图像
        GenerateQrImage(qrPayload);

        UpdateStatus(_T("请使用手机App扫描二维码"));
        Invalidate();
        UpdateWindow(); // 强制立即重绘

        return true;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("[CQRCodeLoginDlg] Exception: {}", e.what());
        return false;
    }
    catch (...)
    {
        LOG_ERROR("[CQRCodeLoginDlg] Unknown exception");
        return false;
    }
}

bool CQRCodeLoginDlg::CheckLoginStatus()
{
    if (m_bindToken.empty()) {
        return false;
    }

    try
    {
        if (QRCodeLoginService::Instance().IsLocallyExpired()) {
            LOG_INFO("[CQRCodeLoginDlg] QR code expired (local check)");
            m_isExpired = true;
            StopPolling();
            UpdateStatus(_T("二维码已过期，请点击刷新"));
            // 不调用 loginCallback，让用户留在二维码登录界面
            return false;
        }

        BindResult result = QRCodeLoginService::Instance().GetBindStatus(m_bindToken);

        switch (result.status) {
        case QRCodeStatus::Pending:
            // 等待扫描
            break;

        case QRCodeStatus::Scanned:
            UpdateStatus(_T("已扫描，请在手机确认登录"));
            break;

        case QRCodeStatus::Bound:
            LOG_INFO("[CQRCodeLoginDlg] Login successful!");
            UpdateData(FALSE);
            m_isLoggedIn.store(true);
            StopPolling();
            UpdateStatus(_T("登录成功！"));

            if (m_loginCallback)
            {
                m_loginCallback(true, L"登录成功");
            }

            ::Sleep(1000);
            EndDialog(IDOK);
            break;
        case QRCodeStatus::Expired:
            LOG_INFO("[CQRCodeLoginDlg] QR code expired (from server)");
            m_isExpired = true;
            
            StopPolling();
            UpdateStatus(_T("二维码已过期，请点击刷新"));
            // 不调用 loginCallback，让用户留在二维码登录界面
            return false;

        default:
            break;
        }

        return false;
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("[CQRCodeLoginDlg] CheckLoginStatus exception: {}", e.what());
        return false;
    }
}

void CQRCodeLoginDlg::StopPolling()
{
    m_stopPolling.store(true);
    if (m_pollingTimer != 0)
    {
        KillTimer(m_pollingTimer);
        m_pollingTimer = 0;
    }
}

void CQRCodeLoginDlg::StartPolling()
{
    m_stopPolling.store(false);
    m_pollingTimer = SetTimer(1, POLLING_INTERVAL_MS, nullptr);
}

void CQRCodeLoginDlg::UpdateStatus(const std::wstring& status)
{
    m_loginStatus = status.c_str();
    UpdateData(FALSE);
}

void CQRCodeLoginDlg::GenerateQrImage(const std::string& qrContent)
{
    try
    {
        LOG_INFO("[CQRCodeLoginDlg] Generating QR code for: {}", qrContent);

        // 使用 qrcodegen 生成二维码
        QrCode qr = QrCode::encodeText(qrContent.c_str(), QrCode::Ecc::MEDIUM);
        int size = qr.getSize();

        m_qrData.resize(size, std::vector<bool>(size, false));
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                m_qrData[y][x] = qr.getModule(x, y);
            }
        }

        LOG_INFO("[CQRCodeLoginDlg] QR code generated: {}x{}", size, size);

        // 确认 m_qrData 已填充
        LOG_INFO("[CQRCodeLoginDlg] m_qrData size: {}", m_qrData.size());
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("[CQRCodeLoginDlg] GenerateQrImage exception: {}", e.what());
    }
}
