#pragma once

#ifndef __AFXWIN_H__
#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"       // main symbols

#include "../config/ConfigLoader.h"
#include "../core/ServiceManager.h"

class CMultiDocTemplate;

class CBlazeClawMFCApp final : public CWinAppEx {
public:
	CBlazeClawMFCApp() noexcept;


// Overrides
public:
	virtual BOOL InitInstance();
	virtual int ExitInstance();
	virtual BOOL OnIdle(LONG lCount);

// Implementation
	UINT  m_nAppLook;
	BOOL  m_bHiColorIcons;

	virtual void PreLoadState();
	virtual void LoadCustomState();
	virtual void SaveCustomState();

	afx_msg void OnAppAbout();
	// Override to prevent CWinAppEx::OnFileNew (pops up multi-template dialog).
	// Tab creation is handled by CMainFrame::OnWindowNew instead.
	afx_msg void OnFileNew();
	DECLARE_MESSAGE_MAP()

public:

	blazeclaw::core::ServiceManager& Services() noexcept;
	const blazeclaw::core::ServiceManager& Services() const noexcept;

	CMultiDocTemplate* GetWebViewOnlyDocTemplate() const noexcept { return m_pWebViewOnlyDocTemplate; }
	CMultiDocTemplate* GetChatDocTemplate() const noexcept { return m_pChatDocTemplate; }

private:
	blazeclaw::config::ConfigLoader m_configLoader;
	blazeclaw::config::AppConfig m_config;
	blazeclaw::core::ServiceManager m_serviceManager;

	CMultiDocTemplate* m_pWebViewOnlyDocTemplate = nullptr;
	CMultiDocTemplate* m_pChatDocTemplate = nullptr;
};

extern CBlazeClawMFCApp theApp;
