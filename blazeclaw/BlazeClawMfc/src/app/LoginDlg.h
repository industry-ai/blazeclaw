// LoginDlg.h : header file
//
#include <string>
#pragma once


// CLoginDlg dialog
class CLoginDlg : public CDialogEx
{
// Construction
public:
	CLoginDlg(CWnd* pParent = nullptr);	// standard constructor
	~CLoginDlg();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_LOGIN_DIALOG };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support

// Implementation
protected:
	HICON m_hIcon;

	// Generated message map functions
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg void OnBnClickedGetCodeButton();
	afx_msg void OnBnClickedLoginButton();
	afx_msg void OnBnClickedQrCodeLoginButton();
	DECLARE_MESSAGE_MAP()
	DECLARE_DYNAMIC(CLoginDlg)

private:
	std::string m_strPhoneNumber;
	std::string m_strCode;
	bool m_bAgree;

public:
	// 获取登录成功的手机号
	std::string GetPhoneNumber() const { return m_strPhoneNumber; }
};