#pragma once

//#include "pch.h"
#include "FileView.h"
#include "SkillView.h"
#include "OutputWnd.h"
#include "PropertiesWnd.h"
#include "DashboardWnd.h"
#include "CalendarBar.h"
#include "Resource.h"
#include <vector>
#include <atomic>

class COutlookBar : public CMFCOutlookBar
{
	virtual BOOL AllowShowOnPaneMenu() const { return TRUE; }
	virtual void GetPaneName(CString& strName) const { BOOL bNameValid = strName.LoadString(IDS_OUTLOOKBAR); ASSERT(bNameValid); if (!bNameValid) strName.Empty(); }
};

// Forward declarations
class CChatView;

constexpr UINT kMsgCreateMdiGroup = WM_USER + 0x100;  // custom message for deferred tab split
constexpr UINT kMsgAppendToolStatusLine = WM_USER + 0x101;  // append line to Output.Tool from non-UI threads
constexpr UINT kMsgSyncDashboardPaneSize = WM_USER + 0x102;
constexpr UINT kMsgSyncDashboardPanePosition = WM_USER + 0x103;
constexpr UINT kMsgSyncDashboardAfterFloat = WM_USER + 0x104;
constexpr UINT kMsgSyncDashboardAfterDock = WM_USER + 0x105;

class CMainFrame final : public CMDIFrameWndEx
{
	DECLARE_DYNAMIC(CMainFrame)
public:
	// Return the active ChatView if present, otherwise nullptr
	CChatView* GetActiveChatView();
	CMainFrame() noexcept;

	// Attributes
public:
	bool IsDashboardFloatDockSyncInProgress() const;

	// Operations
public:
	void AddChatStatusLine(const CString& line);
	void AddChatStatusBlock(const CString& text);
	void AddToolStatusLine(const CString& line);
	void AddToolStatusBlock(const CString& text);
	void RefreshSkillView();

	// Overrides
public:
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
	afx_msg LRESULT OnCreateMdiGroup(WPARAM, LPARAM);
	afx_msg LRESULT OnAppendToolStatusLine(WPARAM, LPARAM);

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
	CSkillView        m_wndSkillView;
	COutputWnd        m_wndOutput;
	CPropertiesWnd    m_wndProperties;

	CDashboardWnd     m_wndDashboard;

	CDashboardWnd     m_wndDashboard_overview;
	CDashboardWnd     m_wndDashboard_tools;
	CDashboardWnd     m_wndDashboard_files;
	CDashboardWnd     m_wndDashboard_skills;
	CDashboardWnd     m_wndDashboard_channels;
	CDashboardWnd     m_wndDashboard_cron;
	CDashboardWnd     m_wndDashboard_dreaming;
	CDashboardWnd     m_wndDashboard_nodes;
	CDashboardWnd     m_wndDashboard_instances;
	CDashboardWnd     m_wndDashboard_usage;
	CDashboardWnd     m_wndDashboard_devices;

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
	afx_msg void OnViewDashboardWindow();
	afx_msg void OnUpdateViewDashboardWindow(CCmdUI* pCmdUI);

	afx_msg void OnViewDashboardOverviewWindow();
	afx_msg void OnUpdateViewDashboardOverviewWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewDashboardToolsWindow();
	afx_msg void OnUpdateViewDashboardToolsWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewDashboardFilesWindow();
	afx_msg void OnUpdateViewDashboardFilesWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewDashboardSkillsWindow();
	afx_msg void OnUpdateViewDashboardSkillsWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewDashboardChannelsWindow();
	afx_msg void OnUpdateViewDashboardChannelsWindow(CCmdUI* pCmdUI);

	afx_msg void OnViewDashboardCronWindow();
	afx_msg void OnUpdateViewDashboardCronWindow(CCmdUI* pCmdUI);

	afx_msg void OnViewDashboardDreamingWindow();
	afx_msg void OnUpdateViewDashboardDreamingWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewDashboardNodesWindow();
	afx_msg void OnUpdateViewDashboardNodesWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewDashboardInstancesWindow();
	afx_msg void OnUpdateViewDashboardInstancesWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewDashboardUsageWindow();
	afx_msg void OnUpdateViewDashboardUsageWindow(CCmdUI* pCmdUI);
	afx_msg void OnViewDashboardDevicesWindow();
	afx_msg void OnUpdateViewDashboardDevicesWindow(CCmdUI* pCmdUI);

	afx_msg void OnViewCaptionBar();
	afx_msg void OnUpdateViewCaptionBar(CCmdUI* pCmdUI);
	afx_msg void OnOptions();
	afx_msg void OnSettingChange(UINT uFlags, LPCTSTR lpszSection);
	afx_msg BOOL OnIdle(WPARAM wParam, LPARAM lParam);

	// New tab creation commands
	afx_msg void OnWindowNew();
	afx_msg void OnWindowNewWebViewChat();
	afx_msg void OnWindowNewWebViewMarkdown();
	afx_msg void OnWindowNewAIChatView();
	afx_msg void OnUpdateWindowNewWebViewOnly(CCmdUI* pCmdUI);

	afx_msg LRESULT OnSyncDashboardPaneSize(WPARAM, LPARAM);
	afx_msg LRESULT OnSyncDashboardPanePosition(WPARAM, LPARAM);
	afx_msg LRESULT OnSyncDashboardAfterFloat(WPARAM, LPARAM);
	afx_msg LRESULT OnSyncDashboardAfterDock(WPARAM, LPARAM);

	void	SyncDashboardPaneSize(HWND sourceHwnd, int cx, int cy);
	void	SyncDashboardPanePosition(HWND sourceHwnd, int x, int y);
	void	SyncDashboardAfterFloat(HWND sourceHwnd);
	void	SyncDashboardAfterDock(HWND sourceHwnd);

	volatile bool	m_isSyncingDashboardPaneSize = false;
	volatile bool	m_isSyncingDashboardPanePosition = false;
	volatile bool	m_isDashboardFloat = false;
	volatile bool   m_isSwitchFloatDock = false;

	//volatile bool	m_isSyncingDashboardFloatDock = false;
	// Re-entrancy counter for float/dock sync (scoped RAII guard will increment/decrement)
	std::atomic<int>	m_dashboardFloatDockSyncCount{ 0 };

public:
	// Called by app to open a tab without showing dialog
	void OpenDefaultTab() { OpenWebViewPlusChatTab(); }
	void ShowSkillSelectionInActiveView(
		const std::string& skillKey,
		const std::string& propertiesJson);
	// Open a new WebView+Chat tab with ChatView hidden (for SkillView)
	// Returns true if tab was created, false if existing tab was activated
	bool OpenSkillViewTab(
		const std::string& skillKey,
		const std::string& propertiesJson);
	// File / Ribbon "New" — MDI routes ID_FILE_NEW to CWinApp while a child is active, so the app calls this too.
	void OpenNewTabWithChoiceDialog();

private:
	// Helper: find existing CChildFrame with the given skill key (case-insensitive)
	// Returns the CWnd* of the frame (cast to CChildFrame in implementation).
	// Defined in MainFrame.cpp where CChildFrame is fully defined.
	CWnd* FindChildFrameWithSkill(const std::string& skillKey) const;

	std::vector<CDashboardWnd*> CollectDashboardPanes();
	void SyncAllDashboardsFloatState(HWND sourceHwnd, bool shouldFloat);

private:
	void OpenWebViewPlusChatTab();
	void OpenWebViewMarkdownTab();
	void OpenAIChatViewTab();

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
	afx_msg void OnEditChat();
	afx_msg void OnEditDashboard();
	afx_msg void OnUpdateEditDashboard(CCmdUI* pCmdUI);
};
