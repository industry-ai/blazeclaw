#pragma once

//#include "pch.h"
#include "FileView.h"
#include "ClassView.h"
#include "OutputWnd.h"
#include "PropertiesWnd.h"
#include "CalendarBar.h"
#include "Resource.h"

class COutlookBar : public CMFCOutlookBar
{
	virtual BOOL AllowShowOnPaneMenu() const { return TRUE; }
	virtual void GetPaneName(CString& strName) const { BOOL bNameValid = strName.LoadString(IDS_OUTLOOKBAR); ASSERT(bNameValid); if (!bNameValid) strName.Empty(); }
};

constexpr UINT kMsgCreateMdiGroup = WM_USER + 0x100;  // custom message for deferred tab split

class CMainFrame final : public CMDIFrameWndEx
{
	DECLARE_DYNAMIC(CMainFrame)
public:
	CMainFrame() noexcept;

	// Attributes
public:

	// Operations
public:
	void AddChatStatusLine(const CString& line);
	void AddChatStatusBlock(const CString& text);

	// Overrides
public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
	afx_msg LRESULT OnCreateMdiGroup(WPARAM, LPARAM);

	// Implementation
public:
	virtual ~CMainFrame();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif


protected:  // control bar embedded members
	CMFCRibbonBar     m_wndRibbonBar;
	CMFCRibbonApplicationButton m_MainButton;
	CMFCToolBarImages m_PanelImages;
	CMFCRibbonStatusBar  m_wndStatusBar;
	CFileView         m_wndFileView;
	CClassView        m_wndClassView;
	COutputWnd        m_wndOutput;
	CPropertiesWnd    m_wndProperties;
	COutlookBar       m_wndNavigationBar;
	CMFCShellTreeCtrl m_wndTree;
	CCalendarBar      m_wndCalendar;
	CMFCCaptionBar    m_wndCaptionBar;

	// Generated message map functions
protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnWindowManager();
	afx_msg void OnApplicationLook(UINT id);
	afx_msg void OnUpdateApplicationLook(CCmdUI* pCmdUI);
	afx_msg void OnViewFileView();
	afx_msg void OnUpdateViewFileView(CCmdUI* pCmdUI);
	afx_msg void OnViewClassView();
	afx_msg void OnUpdateViewClassView(CCmdUI* pCmdUI);
	afx_msg void OnViewOutputWindow();
	afx_msg void OnUpdateViewOutputWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewPropertiesWindow();
	afx_msg void OnUpdateViewPropertiesWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewCaptionBar();
	afx_msg void OnUpdateViewCaptionBar(CCmdUI* pCmdUI);
	afx_msg void OnOptions();
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg BOOL OnIdle(WPARAM wParam, LPARAM lParam);

	// New tab creation commands
	afx_msg void OnWindowNew();
	afx_msg void OnWindowNewWebViewChat();
	afx_msg void OnWindowNewWebViewMarkdown();
	afx_msg void OnUpdateWindowNewWebViewOnly(CCmdUI* pCmdUI);

public:
	// Called by app to open a tab without showing dialog
	void OpenDefaultTab() { OpenWebViewPlusChatTab(); }
	// File / Ribbon "New" — MDI routes ID_FILE_NEW to CWinApp while a child is active, so the app calls this too.
	void OpenNewTabWithChoiceDialog();

private:
	void OpenWebViewPlusChatTab();
	void OpenWebViewMarkdownTab();

	// extension commands
	afx_msg void OnExtensionDeepseek();
	afx_msg void OnUpdateExtensionDeepseek(CCmdUI* pCmdUI);
	afx_msg void OnExtensionModelSet();

	afx_msg void OnUiParityActionFormProbe();
	afx_msg void OnUiParityAdminSnapshot();
	afx_msg void OnUiParityAdminPolicyGet();
	afx_msg void OnUiParityAdminConfigAgent();
	afx_msg void OnUiParityDeepSeekExtension();
	afx_msg void OnUiParitySessionList();
	afx_msg void OnUiParitySessionActivate();
	afx_msg void OnUiParityRuntimeStatus();
	afx_msg void OnUiParityDesktopStatus();
	afx_msg void OnUiParityDesktopWebStatus();
	afx_msg void OnUiParitySkillsStatus();
	afx_msg void OnUiParitySkillsList();
	afx_msg void OnUiParitySkillsInfo();
	afx_msg void OnUiParitySkillsCheck();
	afx_msg void OnUiParitySkillsDiagnostics();
	afx_msg void OnUiParitySkillsInstallOptions();
	afx_msg void OnUiParitySkillsScanStatus();
	afx_msg void OnUiParityOperatorDiagnosticsReport();
	afx_msg void OnUiParityOperatorPromotionReadiness();

	DECLARE_MESSAGE_MAP()

	BOOL CreateDockingWindows();
	void SetDockingWindowIcons(BOOL bHiColorIcons);
	BOOL CreateOutlookBar(CMFCOutlookBar& bar, UINT uiID, CMFCShellTreeCtrl& tree, CCalendarBar& calendar, int nInitialWidth);
	BOOL CreateCaptionBar();

	int FindFocusedOutlookWnd(CMFCOutlookBarTabCtrl** ppOutlookWnd);

	CMFCOutlookBarTabCtrl* FindOutlookParent(CWnd* pWnd);
	CMFCOutlookBarTabCtrl* m_pCurrOutlookWnd;
	CMFCOutlookBarPane* m_pCurrOutlookPage;

private:
	void LogDeepSeekDiagnostic(
		const char* stage,
		const std::string& detail);

	void ShowParityResult(
		const wchar_t* title,
		const std::string& method,
		const std::optional<std::string>& paramsJson = std::nullopt);

	CMenu m_menuBar;
	CMenu m_parityMenu;
public:
	void CreateTwoTabbedGroups();
};
