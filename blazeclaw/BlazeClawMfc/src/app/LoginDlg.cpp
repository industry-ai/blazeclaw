// LoginDlg.cpp : implementation file
//

#include "pch.h"
#include "framework.h"
#include "LoginDlg.h"
#include "afxdialogex.h"

#include <string>
#include <memory>
#include <filesystem>

#include "Client.h"
#include "CNetwork_c.h"
#include "Logger.h"
#include "LogSinks.h"
#include "config_client.h"
#include "TlsAuthClient.h"
#include "BlazeClawMFCApp.h"
#include "QRCodeLoginDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


// CLoginDlg dialog

IMPLEMENT_DYNAMIC(CLoginDlg, CDialogEx)

CLoginDlg::CLoginDlg(CWnd* pParent /*=nullptr*/)
	: CDialogEx(IDD_LOGIN_DIALOG, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

CLoginDlg::~CLoginDlg()
{
}

void CLoginDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CLoginDlg, CDialogEx)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_ERASEBKGND()
	ON_BN_CLICKED(IDC_GET_CODE_BUTTON, &CLoginDlg::OnBnClickedGetCodeButton)
	ON_BN_CLICKED(IDC_LOGIN_BUTTON, &CLoginDlg::OnBnClickedLoginButton)
	ON_BN_CLICKED(IDC_QRCODE_LOGIN_BUTTON, &CLoginDlg::OnBnClickedQrCodeLoginButton)
END_MESSAGE_MAP()


// CLoginDlg message handlers

BOOL CLoginDlg::OnInitDialog()
{
	CDialogEx::OnInitDialog();

	// Set the icon for this dialog.
	// The framework does this automatically
	// when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	// TODO: Add extra initialization here
	
	// 设置默认手机号格式
	CEdit* pPhoneEdit = (CEdit*)GetDlgItem(IDC_PHONE_EDIT);
	if (pPhoneEdit)
	{
		pPhoneEdit->SetCueBanner(_T("请输入手机号"));
		// 设置输入框字体
		CFont* pFont = new CFont();
		pFont->CreatePointFont(120, _T("Microsoft YaHei")); 
		pPhoneEdit->SetFont(pFont);
	}
	
	// 设置验证码输入提示
	CEdit* pCodeEdit = (CEdit*)GetDlgItem(IDC_CODE_EDIT);
	if (pCodeEdit)
	{
		pCodeEdit->SetCueBanner(_T("请输入6位验证码"));
		// 设置输入框字体
		CFont* pFont = new CFont();
		pFont->CreatePointFont(120, _T("Microsoft YaHei"));
		pCodeEdit->SetFont(pFont);
	}
	
	// 设置按钮字体和颜色
	CButton* pGetCodeButton = (CButton*)GetDlgItem(IDC_GET_CODE_BUTTON);
	if (pGetCodeButton)
	{
		CFont* pFont = new CFont();
		pFont->CreatePointFont(110, _T("Microsoft YaHei"));
		pGetCodeButton->SetFont(pFont);
	}
	
	CButton* pLoginButton = (CButton*)GetDlgItem(IDC_LOGIN_BUTTON);
	if (pLoginButton)
	{
		CFont* pFont = new CFont();
		pFont->CreatePointFont(120, _T("Microsoft YaHei"));
		pLoginButton->SetFont(pFont);
	}
	
	// 设置扫码登录按钮字体
	CButton* pQrCodeButton = (CButton*)GetDlgItem(IDC_QRCODE_LOGIN_BUTTON);
	if (pQrCodeButton)
	{
		CFont* pFont = new CFont();
		pFont->CreatePointFont(110, _T("Microsoft YaHei"));
		pQrCodeButton->SetFont(pFont);
	}
	
	// 设置标题字体
	CWnd* pWelcomeText = GetDlgItem(IDC_STATIC);
	if (pWelcomeText)
	{
		CFont* pFont = new CFont();
		pFont->CreatePointFont(160, _T("Microsoft YaHei"));
		pWelcomeText->SetFont(pFont);
	}
	
	// 设置提示文本字体
	CWnd* pHintText = GetDlgItem(IDC_STATIC + 1);
	if (pHintText)
	{
		CFont* pFont = new CFont();
		pFont->CreatePointFont(110, _T("Microsoft YaHei"));
		pHintText->SetFont(pFont);
	}

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CLoginDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	CDialogEx::OnSysCommand(nID, lParam);
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CLoginDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialogEx::OnPaint();
	}
}

// The system calls this function to obtain the cursor to display while the user drags
//  the minimized window.

HCURSOR CLoginDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

BOOL CLoginDlg::OnEraseBkgnd(CDC* pDC)
{
	// TODO: Add your message handler code here and/or call default

	return CDialogEx::OnEraseBkgnd(pDC);
}

void CLoginDlg::OnBnClickedGetCodeButton()
{
	// 获取手机号
	CEdit* pPhoneEdit = (CEdit*)GetDlgItem(IDC_PHONE_EDIT);
	if (pPhoneEdit)
	{
		CString strPhone;
		pPhoneEdit->GetWindowText(strPhone);
		m_strPhoneNumber = CW2A(strPhone.GetString());
	}

	if (m_strPhoneNumber.empty())
	{
		AfxMessageBox(_T("请输入手机号"));
		return;
	}

	try
	{
		// 初始化日志系统
		Logger::Instance().SetLevel(LogLevel::Info);
		Logger::Instance().AddSink(std::make_shared<ConsoleSink>());
		Logger::Instance().AddSink(std::make_shared<FileSink>("sms_login_test.log"));

		LOG_INFO("[SMS Login Test] Starting SMS login test");

		// 获取网络实例
		CNetwork_c& network = CNetwork_c::Instance();

		// 创建TlsAuthClient实例
		std::unique_ptr<TlsAuthClient> tls_auth_client = std::make_unique<TlsAuthClient>(network);

		// 从配置文件加载服务器地址
		// Get executable directory
		wchar_t buffer[MAX_PATH];
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		std::filesystem::path exePath(buffer);
		std::filesystem::path exeDir = exePath.parent_path();
		std::filesystem::path configPath = exeDir / L"client.conf";
		
		ConfigClient::instance().loadFromPath(configPath.string());
		std::string tls_ip = ConfigClient::instance().getTlsHost();
		int tls_port = ConfigClient::instance().getTlsPort();

		if (tls_ip.empty() || tls_port <= 0)
		{
			LOG_ERROR("[SMS Login Test] Server address not configured in client.conf");
			AfxMessageBox(_T("服务器地址未配置"));
			return;
		}

		LOG_INFO("[SMS Login Test] Using TLS server: {}:{}", tls_ip, tls_port);

		// 发送短信验证码
		LOG_INFO("[SMS Login Test] Sending SMS code to: {}", m_strPhoneNumber);
		bool sendSuccess = tls_auth_client->SendSmsCode(m_strPhoneNumber);
		if (!sendSuccess)
		{
			LOG_ERROR("[SMS Login Test] Failed to send SMS code");
			AfxMessageBox(_T("发送验证码失败"));
			return;
		}
		LOG_INFO("[SMS Login Test] SMS code sent successfully");
		AfxMessageBox(_T("验证码发送成功"));
	}
	catch (const std::exception& ex)
	{
		LOG_ERROR("[SMS Login Test] Exception: {}", ex.what());
		AfxMessageBox(_T("发送验证码失败，请检查网络连接"));
	}
}

void CLoginDlg::OnBnClickedLoginButton()
{
	// 获取手机号
	CEdit* pPhoneEdit = (CEdit*)GetDlgItem(IDC_PHONE_EDIT);
	if (pPhoneEdit)
	{
		CString strPhone;
		pPhoneEdit->GetWindowText(strPhone);
		m_strPhoneNumber = CW2A(strPhone.GetString());
	}

	// 获取验证码
	CEdit* pCodeEdit = (CEdit*)GetDlgItem(IDC_CODE_EDIT);
	if (pCodeEdit)
	{
		CString strCode;
		pCodeEdit->GetWindowText(strCode);
		m_strCode = CW2A(strCode.GetString());
	}

	// 检查协议是否勾选
	//CButton* pAgreeCheck = (CButton*)GetDlgItem(IDC_AGREE_CHECKBOX);
	//if (pAgreeCheck)
	//{
	//	m_bAgree = (pAgreeCheck->GetCheck() == BST_CHECKED);
	//}

	if (m_strPhoneNumber.empty())
	{
		AfxMessageBox(_T("请输入手机号"));
		return;
	}

	if (m_strCode.empty())
	{
		AfxMessageBox(_T("请输入验证码"));
		return;
	}

	//if (!m_bAgree)
	//{
	//	AfxMessageBox(_T("请阅读并同意用户协议和隐私条款"));
	//	return;
	//}

	try
	{
		// 获取网络实例
		CNetwork_c& network = CNetwork_c::Instance();

		// 创建TlsAuthClient实例
		std::unique_ptr<TlsAuthClient> tls_auth_client = std::make_unique<TlsAuthClient>(network);

		// 从配置文件加载服务器地址
		// Get executable directory
		wchar_t buffer[MAX_PATH];
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		std::filesystem::path exePath(buffer);
		std::filesystem::path exeDir = exePath.parent_path();
		std::filesystem::path configPath = exeDir / L"client.conf";
		
		ConfigClient::instance().loadFromPath(configPath.string());
		std::string tls_ip = ConfigClient::instance().getTlsHost();
		int tls_port = ConfigClient::instance().getTlsPort();

		if (tls_ip.empty() || tls_port <= 0)
		{
			LOG_ERROR("[SMS Login Test] Server address not configured in client.conf");
			AfxMessageBox(_T("服务器地址未配置"));
			return;
		}

		// 短信验证码登录
		LOG_INFO("[SMS Login Test] Logging in with SMS code");
		bool loginSuccess = tls_auth_client->LoginWithSms(m_strPhoneNumber, m_strCode);
		if (!loginSuccess)
		{
			LOG_ERROR("[SMS Login Test] SMS login failed");
			AfxMessageBox(_T("登录失败，请检查验证码是否正确"));
			return;
		}

		// 获取会话信息
		std::string accessToken = tls_auth_client->GetAccessToken();
		bool isLoggedIn = tls_auth_client->IsLoggedIn();
		LOG_INFO("[SMS Login Test] ==================================================================");
		LOG_INFO("[SMS Login Test] Access token: {}", accessToken);
		LOG_INFO("[SMS Login Test] Is logged in: {}", isLoggedIn ? "true" : "false");

		// 获取会话ID
		CClient& client = CClient::Instance();
		uint64_t sessionId = client.GetSessionId();
		LOG_INFO("[SMS Login Test] Session ID: {}", sessionId);
		LOG_INFO("[SMS Login Test] ==================================================================");
		LOG_INFO("");

		// 保存会话ID到文件
		try
		{
			// Get executable directory
			wchar_t buffer[MAX_PATH];
			GetModuleFileNameW(NULL, buffer, MAX_PATH);
			std::filesystem::path exePath(buffer);
			std::filesystem::path exeDir = exePath.parent_path();
			std::filesystem::path sessionFilePath = exeDir / L"session_id.txt";
			
			std::ofstream sessionFile(sessionFilePath.wstring());
			if (sessionFile.is_open())
			{
				sessionFile << sessionId;
				sessionFile.close();
				LOG_INFO("[SMS Login Test] Session ID saved to {}", sessionFilePath.string());
			}
		}
		catch (const std::exception& ex)
		{
			LOG_ERROR("[SMS Login Test] Exception saving session ID: {}", ex.what());
		}

		// 登录成功，关闭登录对话框
		AfxMessageBox(_T("登录成功"));
		CDialogEx::OnOK();
	}
	catch (const std::exception& ex)
	{
		LOG_ERROR("[SMS Login Test] Exception: {}", ex.what());
		AfxMessageBox(_T("登录失败，请检查网络连接"));
	}
}

void CLoginDlg::OnBnClickedQrCodeLoginButton()
{
	// 隐藏当前登录对话框
	ShowWindow(SW_HIDE);
	
	// 弹出二维码登录对话框
	CQRCodeLoginDlg qrDlg(this);
	qrDlg.SetLoginCallback([this](bool success, const std::wstring& message) {
		if (success)
		{
			// 二维码登录成功，关闭登录对话框并返回成功
			PostMessage(WM_COMMAND, IDOK);
		}
		else
		{
			// 二维码登录失败或取消，重新显示登录对话框
			ShowWindow(SW_SHOW);
		}
	});
	
	INT_PTR result = qrDlg.DoModal();
	
	if (result == IDOK)
	{
		// 二维码登录成功
		CDialogEx::OnOK();
	}
	else
	{
		// 取消或失败，重新显示登录对话框
		ShowWindow(SW_SHOW);
	}
}

