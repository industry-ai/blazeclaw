#pragma once

#ifndef __AFXWIN_H__
#error "include 'pch.h' before including this file for PCH"
#endif

#include "resource.h"       // main symbols

#include "../config/ConfigLoader.h"
#include "../core/ServiceManager.h"

class CMultiDocTemplate;
class CBlazeClawMFCView;
class CBlazeClawMarkdownView;
class CSharedTabsDocTemplate;

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
	// TRUE after InitInstance completes. OnFileNew is a no-op until then so startup
	// only creates the two WebView-only groups from CreateTwoTabbedGroups.
	BOOL  m_bStartupComplete = FALSE;

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

	CMultiDocTemplate* GetChatDocTemplate() const noexcept { return m_pChatDocTemplate; }
	CSharedTabsDocTemplate* GetWebViewMarkdownSharedDocTemplate() const noexcept { return m_pWebViewMarkdownSharedDocTemplate; }
	CRuntimeClass* GetWebViewMarkdownLeftViewClass() const;
	CRuntimeClass* GetWebViewMarkdownRightViewClass() const;

private:
	blazeclaw::config::ConfigLoader m_configLoader;
	blazeclaw::config::AppConfig m_config;
	blazeclaw::core::ServiceManager m_serviceManager;

	CMultiDocTemplate* m_pChatDocTemplate = nullptr;
	CSharedTabsDocTemplate* m_pWebViewMarkdownSharedDocTemplate = nullptr;
};

extern CBlazeClawMFCApp theApp;
