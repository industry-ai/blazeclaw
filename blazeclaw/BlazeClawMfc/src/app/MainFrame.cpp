#include "pch.h"
#include "MainFrame.h"
#include "framework.h"

#include "BlazeClawMfcApp.h"
#include "CMgrMessage.h"
#include <atlconv.h>
#include "ChatView.h"
#include <optional>
#include <algorithm>
#include <cwctype>
#include "../config/ConfigLoader.h"
#include <fstream>
#include <vector>
#include <string>
#include "CredentialStore.h"
#include "ApiKeyDialog.h"
#include "SettingsDialog.h"
#include "BlazeClawMFCView.h"
#include "BlazeClawMarkdownView.h"
#include "SharedTabsDocTemplate.h"
#include "ChildFrm.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {
	constexpr UINT kIdUiParityActionFormProbe = 0x8101;
	constexpr UINT kIdUiParityAdminSnapshot = 0x8102;
	constexpr UINT kIdUiParityAdminPolicyGet = 0x8103;
	constexpr UINT kIdUiParityAdminConfigAgent = 0x8104;
	constexpr UINT kIdUiParityDeepSeekExtension = 0x810A;
	constexpr UINT kIdUiParitySessionList = 0x8105;
	constexpr UINT kIdUiParitySessionActivate = 0x8106;
	constexpr UINT kIdUiParityRuntimeStatus = 0x8107;
	constexpr UINT kIdUiParityDesktopStatus = 0x8108;
	constexpr UINT kIdUiParityDesktopWebStatus = 0x8109;
	constexpr UINT kIdUiParitySkillsStatus = 0x8110;
	constexpr UINT kIdUiParitySkillsList = 0x8111;
	constexpr UINT kIdUiParitySkillsInfo = 0x8112;
	constexpr UINT kIdUiParitySkillsCheck = 0x8113;
	constexpr UINT kIdUiParitySkillsDiagnostics = 0x8114;
	constexpr UINT kIdUiParitySkillsInstallOptions = 0x8115;
	constexpr UINT kIdUiParitySkillsScanStatus = 0x8116;
	constexpr UINT kIdUiParityOperatorDiagnosticsReport = 0x8117;
	constexpr UINT kIdUiParityOperatorPromotionReadiness = 0x8118;
	constexpr LPCTSTR kDashboardProfileSection = _T("Dashboard");
	constexpr LPCTSTR kDashboardLastPaneIdKey = _T("LastVisiblePaneId");

	static std::wstring TrimMain(const std::wstring& value) {
		const auto first = std::find_if_not(
			value.begin(), value.end(), [](wchar_t ch) { return std::iswspace(ch) != 0; });
		const auto last = std::find_if_not(
			value.rbegin(), value.rend(), [](wchar_t ch) { return std::iswspace(ch) != 0; }).base();
		if (first >= last) {
			return {};
		}
		return std::wstring(first, last);
	}

	std::wstring ToWide(const std::string& value) {
		std::wstring output;
		output.reserve(value.size());

		for (const char ch : value) {
			output.push_back(static_cast<wchar_t>(
				static_cast<unsigned char>(ch)));
		}

		return output;
	}

	CString BuildDeepSeekDiagnosticLine(
		const char* stage,
		const std::string& detail)
	{
		const std::string safeStage =
			(stage == nullptr || std::string(stage).empty())
			? "unknown"
			: std::string(stage);
		const std::string line =
			std::string("[DeepSeek][") +
			safeStage +
			"] " +
			detail;
		return CString(CA2W(line.c_str(), CP_UTF8));
	}

	class ScopedDashboardFloatDockSyncGuard
	{
	public:
		explicit ScopedDashboardFloatDockSyncGuard(std::atomic<int>& counter) noexcept
			: counterRef(counter)
		{
			counterRef.fetch_add(1, std::memory_order_acq_rel);
		}

		~ScopedDashboardFloatDockSyncGuard()
		{
			counterRef.fetch_sub(1, std::memory_order_acq_rel);
		}

		// non-copyable
		ScopedDashboardFloatDockSyncGuard(const ScopedDashboardFloatDockSyncGuard&) = delete;
		ScopedDashboardFloatDockSyncGuard& operator=(const ScopedDashboardFloatDockSyncGuard&) = delete;

	private:
		std::atomic<int>& counterRef;
	};

} // namespace
// ApiKey dialog declared in its own files

CChatView* CMainFrame::GetActiveChatView()
{
	CMDIChildWndEx* activeChild = DYNAMIC_DOWNCAST(CMDIChildWndEx, MDIGetActive());
	if (activeChild == nullptr) {
		return nullptr;
	}
	CView* activeView = activeChild->GetActiveView();
	auto* chatView = DYNAMIC_DOWNCAST(CChatView, activeView);
	return chatView;
}

IMPLEMENT_DYNAMIC(CMainFrame, CMDIFrameWndEx)

BEGIN_MESSAGE_MAP(CMainFrame, CMDIFrameWndEx)
	ON_WM_CREATE()
	ON_COMMAND(ID_WINDOW_MANAGER, &CMainFrame::OnWindowManager)
	ON_COMMAND_RANGE(ID_VIEW_APPLOOK_WIN_2000, ID_VIEW_APPLOOK_WINDOWS_7, &CMainFrame::OnApplicationLook)
	ON_UPDATE_COMMAND_UI_RANGE(ID_VIEW_APPLOOK_WIN_2000, ID_VIEW_APPLOOK_WINDOWS_7, &CMainFrame::OnUpdateApplicationLook)
	ON_COMMAND(ID_VIEW_CAPTION_BAR, &CMainFrame::OnViewCaptionBar)
	ON_UPDATE_COMMAND_UI(ID_VIEW_CAPTION_BAR, &CMainFrame::OnUpdateViewCaptionBar)
	ON_COMMAND(ID_TOOLS_OPTIONS, &CMainFrame::OnOptions)
	ON_COMMAND(ID_VIEW_FILEVIEW, &CMainFrame::OnViewFileView)
	ON_UPDATE_COMMAND_UI(ID_VIEW_FILEVIEW, &CMainFrame::OnUpdateViewFileView)
	ON_COMMAND(ID_VIEW_CLASSVIEW, &CMainFrame::OnViewClassView)
	ON_UPDATE_COMMAND_UI(ID_VIEW_CLASSVIEW, &CMainFrame::OnUpdateViewClassView)
	ON_COMMAND(ID_VIEW_OUTPUTWND, &CMainFrame::OnViewOutputWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_OUTPUTWND, &CMainFrame::OnUpdateViewOutputWindow)
	ON_COMMAND(ID_VIEW_PROPERTIESWND, &CMainFrame::OnViewPropertiesWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_PROPERTIESWND, &CMainFrame::OnUpdateViewPropertiesWindow)
	ON_COMMAND(ID_VIEW_DASHBOARDWND, &CMainFrame::OnViewDashboardWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARDWND, &CMainFrame::OnUpdateViewDashboardWindow)

	ON_COMMAND(ID_VIEW_DASHBOARD_OVERVIEW_WND, &CMainFrame::OnViewDashboardOverviewWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_OVERVIEW_WND, &CMainFrame::OnUpdateViewDashboardOverviewWindow)
	ON_COMMAND(ID_VIEW_DASHBOARD_TOOLS_WND, &CMainFrame::OnViewDashboardToolsWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_TOOLS_WND, &CMainFrame::OnUpdateViewDashboardToolsWindow)
	ON_COMMAND(ID_VIEW_DASHBOARD_FILES_WND, &CMainFrame::OnViewDashboardFilesWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_FILES_WND, &CMainFrame::OnUpdateViewDashboardFilesWindow)
	ON_COMMAND(ID_VIEW_DASHBOARD_SKILLS_WND, &CMainFrame::OnViewDashboardSkillsWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_SKILLS_WND, &CMainFrame::OnUpdateViewDashboardSkillsWindow)
	ON_COMMAND(ID_VIEW_DASHBOARD_CHANNELS_WND, &CMainFrame::OnViewDashboardChannelsWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_CHANNELS_WND, &CMainFrame::OnUpdateViewDashboardChannelsWindow)

	ON_COMMAND(ID_VIEW_DASHBOARD_CRON_WND, &CMainFrame::OnViewDashboardCronWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_CRON_WND, &CMainFrame::OnUpdateViewDashboardCronWindow)

	ON_COMMAND(ID_VIEW_DASHBOARD_DREAMING_WND, &CMainFrame::OnViewDashboardDreamingWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_DREAMING_WND, &CMainFrame::OnUpdateViewDashboardDreamingWindow)
	ON_COMMAND(ID_VIEW_DASHBOARD_NODES_WND, &CMainFrame::OnViewDashboardNodesWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_NODES_WND, &CMainFrame::OnUpdateViewDashboardNodesWindow)
	ON_COMMAND(ID_VIEW_DASHBOARD_INSTANCES_WND, &CMainFrame::OnViewDashboardInstancesWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_INSTANCES_WND, &CMainFrame::OnUpdateViewDashboardInstancesWindow)
	ON_COMMAND(ID_VIEW_DASHBOARD_USAGE_WND, &CMainFrame::OnViewDashboardUsageWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_USAGE_WND, &CMainFrame::OnUpdateViewDashboardUsageWindow)
	ON_COMMAND(ID_VIEW_DASHBOARD_DEVICES_WND, &CMainFrame::OnViewDashboardDevicesWindow)
	ON_UPDATE_COMMAND_UI(ID_VIEW_DASHBOARD_DEVICES_WND, &CMainFrame::OnUpdateViewDashboardDevicesWindow)

	ON_COMMAND(ID_EXTENSION_DEEPSEEK, &CMainFrame::OnExtensionDeepseek)
	ON_UPDATE_COMMAND_UI(ID_EXTENSION_DEEPSEEK, &CMainFrame::OnUpdateExtensionDeepseek)
	ON_COMMAND(ID_EXTENSION_MODELSET, &CMainFrame::OnExtensionModelSet)
	ON_COMMAND(ID_WINDOW_NEW_WEBVIEW, &CMainFrame::OnWindowNew)
	ON_UPDATE_COMMAND_UI(ID_WINDOW_NEW_WEBVIEW, &CMainFrame::OnUpdateWindowNewWebViewOnly)
	ON_COMMAND(ID_WINDOW_NEW_WEBVIEW_CHAT, &CMainFrame::OnWindowNewWebViewChat)
	ON_COMMAND(ID_WINDOW_NEW_WEBVIEW_MARKDOWN, &CMainFrame::OnWindowNewWebViewMarkdown)
	ON_COMMAND(ID_WINDOW_NEWAICHATVIEW, &CMainFrame::OnWindowNewAIChatView)
	ON_WM_SETTINGCHANGE()

	ON_MESSAGE(kMsgCreateMdiGroup, &CMainFrame::OnCreateMdiGroup)
	ON_MESSAGE(kMsgAppendToolStatusLine, &CMainFrame::OnAppendToolStatusLine)

	ON_MESSAGE(kMsgSyncDashboardPaneSize, &CMainFrame::OnSyncDashboardPaneSize)
	ON_MESSAGE(kMsgSyncDashboardPanePosition, &CMainFrame::OnSyncDashboardPanePosition)

	ON_MESSAGE(kMsgSyncDashboardAfterFloat, &CMainFrame::OnSyncDashboardAfterFloat)
	ON_MESSAGE(kMsgSyncDashboardAfterDock, &CMainFrame::OnSyncDashboardAfterDock)
	ON_MESSAGE(WM_USER + 0x200, &CMainFrame::OnHideAllDashboards)

	ON_COMMAND(kIdUiParityActionFormProbe, &CMainFrame::OnUiParityActionFormProbe)
	ON_COMMAND(kIdUiParityAdminSnapshot, &CMainFrame::OnUiParityAdminSnapshot)
	ON_COMMAND(kIdUiParityAdminPolicyGet, &CMainFrame::OnUiParityAdminPolicyGet)
	ON_COMMAND(kIdUiParityAdminConfigAgent, &CMainFrame::OnUiParityAdminConfigAgent)
	ON_COMMAND(kIdUiParityDeepSeekExtension, &CMainFrame::OnUiParityDeepSeekExtension)
	ON_COMMAND(kIdUiParitySessionList, &CMainFrame::OnUiParitySessionList)
	ON_COMMAND(kIdUiParitySessionActivate, &CMainFrame::OnUiParitySessionActivate)
	ON_COMMAND(kIdUiParityRuntimeStatus, &CMainFrame::OnUiParityRuntimeStatus)
	ON_COMMAND(kIdUiParityDesktopStatus, &CMainFrame::OnUiParityDesktopStatus)
	ON_COMMAND(kIdUiParityDesktopWebStatus, &CMainFrame::OnUiParityDesktopWebStatus)
	ON_COMMAND(kIdUiParitySkillsStatus, &CMainFrame::OnUiParitySkillsStatus)
	ON_COMMAND(kIdUiParitySkillsList, &CMainFrame::OnUiParitySkillsList)
	ON_COMMAND(kIdUiParitySkillsInfo, &CMainFrame::OnUiParitySkillsInfo)
	ON_COMMAND(kIdUiParitySkillsCheck, &CMainFrame::OnUiParitySkillsCheck)
	ON_COMMAND(kIdUiParitySkillsDiagnostics, &CMainFrame::OnUiParitySkillsDiagnostics)
	ON_COMMAND(kIdUiParitySkillsInstallOptions, &CMainFrame::OnUiParitySkillsInstallOptions)
	ON_COMMAND(kIdUiParitySkillsScanStatus, &CMainFrame::OnUiParitySkillsScanStatus)
	ON_COMMAND(kIdUiParityOperatorDiagnosticsReport, &CMainFrame::OnUiParityOperatorDiagnosticsReport)
	ON_COMMAND(kIdUiParityOperatorPromotionReadiness, &CMainFrame::OnUiParityOperatorPromotionReadiness)
	ON_COMMAND(ID_EDIT_CHAT, &CMainFrame::OnEditChat)
	ON_COMMAND(ID_EDIT_DASHBOARD, &CMainFrame::OnEditDashboard)
	ON_UPDATE_COMMAND_UI(ID_EDIT_DASHBOARD, &CMainFrame::OnUpdateEditDashboard)
END_MESSAGE_MAP()

CMainFrame::CMainFrame() noexcept
{
	theApp.m_nAppLook = theApp.GetInt(_T("ApplicationLook"), ID_VIEW_APPLOOK_VS_2008);

	//Create(nullptr, _T("BlazeClaw - OpenClaw C++ Port"), WS_OVERLAPPEDWINDOW, CRect(100, 100, 1280, 800));
}

CMainFrame::~CMainFrame()
{
	CMgrMessage::Instance().Shutdown();
}

LRESULT CMainFrame::OnCreateMdiGroup(WPARAM, LPARAM)
{
	CreateTwoTabbedGroups();
	return 0;
}

LRESULT CMainFrame::OnAppendToolStatusLine(WPARAM, LPARAM lParam)
{
	std::unique_ptr<CString> line(reinterpret_cast<CString*>(lParam));
	if (!line)
	{
		return 0;
	}

	AddToolStatusLine(*line);
	return 0;
}

void CMainFrame::CreateTwoTabbedGroups()
{
	// MDI Tabbed Groups already enabled in OnCreate via EnableMDITabbedGroups.

	// Default startup: single WebView+Markdown shared tab
	OpenWebViewMarkdownTab();

	RecalcLayout(FALSE);
}

void CMainFrame::LogDeepSeekDiagnostic(
	const char* stage,
	const std::string& detail)
{
	AddChatStatusLine(BuildDeepSeekDiagnosticLine(stage, detail));
}

void CMainFrame::AddChatStatusLine(const CString& line)
{
	if (!::IsWindow(m_hWnd))
	{
		return;
	}

	m_wndOutput.AddChatStatusLine(line);
}

void CMainFrame::AddChatStatusBlock(const CString& text)
{
	if (!::IsWindow(m_hWnd))
	{
		return;
	}

	m_wndOutput.AddChatStatusBlock(text);
}

void CMainFrame::AddToolStatusLine(const CString& line)
{
	if (!::IsWindow(m_hWnd))
	{
		return;
	}

	m_wndOutput.AddToolStatusLine(line);
}

void CMainFrame::AddToolStatusBlock(const CString& text)
{
	if (!::IsWindow(m_hWnd))
	{
		return;
	}

	m_wndOutput.AddToolStatusBlock(text);
}

void CMainFrame::RefreshSkillView()
{
	if (!::IsWindow(m_hWnd))
	{
		return;
	}

	m_wndSkillView.RefreshSkills();
}

void CMainFrame::ShowSkillSelectionInActiveView(
	const std::string& skillKey,
	const std::string& propertiesJson)
{
	auto routeToView = [&skillKey, &propertiesJson](CView* view) -> bool
		{
			auto* chatView = DYNAMIC_DOWNCAST(CBlazeClawMFCView, view);
			if (chatView == nullptr)
			{
				return false;
			}

			chatView->ShowSkillSelection(skillKey, propertiesJson);
			return true;
		};

	CMDIChildWndEx* activeChild =
		DYNAMIC_DOWNCAST(CMDIChildWndEx, MDIGetActive());
	if (activeChild == nullptr)
	{
		return;
	}

	CView* activeView = activeChild->GetActiveView();
	if (routeToView(activeView))
	{
		return;
	}

	OpenDefaultTab();
	CMDIChildWndEx* newActiveChild =
		DYNAMIC_DOWNCAST(CMDIChildWndEx, MDIGetActive());
	if (newActiveChild != nullptr)
	{
		routeToView(newActiveChild->GetActiveView());
	}
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CMDIFrameWndEx::OnCreate(lpCreateStruct) == -1)
		return -1;

	m_dashboardPaneSyncReady.store(false, std::memory_order_release);

	CMgrMessage::Instance().Initialize(m_hWnd);
	CMgrMessage::Instance().RegisterMessage({
		.messageId = kMsgAppendToolStatusLine,
		.name = "kMsgAppendToolStatusLine",
		.producer = "CMgrMessage producer helpers",
		.consumer = "CMainFrame::OnAppendToolStatusLine",
		.payloadContract = "LPARAM: CString*",
		.threadContract = "Produced on worker/UI, consumed on UI"
	});

	BOOL bNameValid;

	CMDITabInfo mdiTabParams;
	mdiTabParams.m_style = CMFCTabCtrl::STYLE_3D_ONENOTE; // other styles available...
	mdiTabParams.m_bActiveTabCloseButton = TRUE;      // set to FALSE to place close button at right of tab area
	mdiTabParams.m_bTabIcons = FALSE;    // set to TRUE to enable document icons on MDI taba
	mdiTabParams.m_bAutoColor = TRUE;    // set to FALSE to disable auto-coloring of MDI tabs
	mdiTabParams.m_bDocumentMenu = TRUE; // enable the document menu at the right edge of the tab area
	EnableMDITabbedGroups(TRUE, mdiTabParams);

	m_wndRibbonBar.Create(this);
	m_wndRibbonBar.LoadFromResource(IDR_RIBBON);

	if (!m_wndStatusBar.Create(this))
	{
		TRACE0("Failed to create status bar\n");
		return -1;      // fail to create
	}

	CString strTitlePane1;
	CString strTitlePane2;
	bNameValid = strTitlePane1.LoadString(IDS_STATUS_PANE1);
	ASSERT(bNameValid);
	bNameValid = strTitlePane2.LoadString(IDS_STATUS_PANE2);
	ASSERT(bNameValid);
	m_wndStatusBar.AddElement(new CMFCRibbonStatusBarPane(ID_STATUSBAR_PANE1, strTitlePane1, TRUE), strTitlePane1);
	m_wndStatusBar.AddExtendedElement(new CMFCRibbonStatusBarPane(ID_STATUSBAR_PANE2, strTitlePane2, TRUE), strTitlePane2);

	// enable Visual Studio 2005 style docking window behavior
	CDockingManager::SetDockingMode(DT_SMART);
	// enable Visual Studio 2005 style docking window auto-hide behavior
	EnableAutoHidePanes(CBRS_ALIGN_ANY);

	// Navigation pane will be created at left, so temporary disable docking at the left side:
	EnableDocking(CBRS_ALIGN_TOP | CBRS_ALIGN_BOTTOM | CBRS_ALIGN_RIGHT);

	// Create and setup "Outlook" navigation bar:
	if (!CreateOutlookBar(m_wndNavigationBar, ID_VIEW_NAVIGATION, m_wndTree, m_wndCalendar, 250))
	{
		TRACE0("Failed to create navigation pane\n");
		return -1;      // fail to create
	}

	// Create a caption bar:
	if (!CreateCaptionBar())
	{
		TRACE0("Failed to create caption bar\n");
		return -1;      // fail to create
	}

	// Outlook bar is created and docking on the left side should be allowed.
	EnableDocking(CBRS_ALIGN_LEFT);
	EnableAutoHidePanes(CBRS_ALIGN_RIGHT);

	// Load menu item image (not placed on any standard toolbars):
	CMFCToolBar::AddToolBarForImageCollection(IDR_MENU_IMAGES, theApp.m_bHiColorIcons ? IDB_MENU_IMAGES_24 : 0);

	// create docking windows
	if (!CreateDockingWindows())
	{
		TRACE0("Failed to create docking windows\n");
		return -1;
	}

	m_wndFileView.EnableDocking(CBRS_ALIGN_ANY);
	m_wndSkillView.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndFileView);
	CDockablePane* pTabbedBar = nullptr;
	m_wndSkillView.AttachToTabWnd(&m_wndFileView, DM_SHOW, TRUE, &pTabbedBar);
	m_wndOutput.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndOutput);
	m_wndProperties.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndProperties);
	m_wndDashboard.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard);

	m_wndDashboard_overview.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_overview);
	m_wndDashboard_tools.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_tools);
	m_wndDashboard_files.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_files);
	m_wndDashboard_skills.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_skills);
	m_wndDashboard_channels.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_channels);

	m_wndDashboard_overview.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_tools.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_files.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_skills.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_channels.ShowPane(FALSE, FALSE, FALSE);

	m_wndDashboard_cron.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_cron);
	m_wndDashboard_cron.ShowPane(FALSE, FALSE, FALSE);

	m_wndDashboard_dreaming.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_dreaming);
	m_wndDashboard_nodes.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_nodes);
	m_wndDashboard_instances.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_instances);
	m_wndDashboard_usage.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_usage);
	m_wndDashboard_devices.EnableDocking(CBRS_ALIGN_ANY);
	DockPane(&m_wndDashboard_devices);

	m_wndDashboard_dreaming.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_nodes.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_instances.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_usage.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_devices.ShowPane(FALSE, FALSE, FALSE);

	//RestoreLastDashboardPane();  // Don't restore on startup - hide all dashboards

	CDockingManager* pDockMgr = GetDockingManager();
	// Allow docking on all edges
	pDockMgr->EnableDocking(CBRS_ALIGN_ANY);
	// Enable auto hide for left/right
	pDockMgr->EnableAutoHidePanes(CBRS_ALIGN_LEFT | CBRS_ALIGN_RIGHT);

	//// Create custom dock pane
	//m_wndTreePane.Create(_T("Tree View"), this, CRect(0, 0, 200, 400), TRUE, ID_VIEW_TREE);
	//// Dock to left side
	pDockMgr->DockPane(&m_wndDashboard, CBRS_ALIGN_LEFT);
	pDockMgr->DockPane(&m_wndDashboard_cron, CBRS_ALIGN_LEFT);

	//// Restore saved layout
	//pDockMgr->LoadState(_T("AppLayout"));

	// set the visual manager and style based on persisted value
	OnApplicationLook(theApp.m_nAppLook);

	// Enable enhanced windows management dialog
	EnableWindowsDialog(ID_WINDOW_MANAGER, ID_WINDOW_MANAGER, TRUE);

	// Switch the order of document name and application name on the window title bar. This
	// improves the usability of the taskbar because the document name is visible with the thumbnail.
	ModifyStyle(0, FWS_PREFIXTITLE);

/*	m_menuBar.CreateMenu();
	m_parityMenu.CreatePopupMenu();
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityActionFormProbe,
		_T("Action/Form Probe"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityAdminSnapshot,
		_T("Admin Snapshot"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityAdminPolicyGet,
		_T("Admin Policy Get"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityAdminConfigAgent,
		_T("Admin Config Agent"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityDeepSeekExtension,
		_T("DeepSeek Extension"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySessionList,
		_T("Session List"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySessionActivate,
		_T("Session Activate"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityRuntimeStatus,
		_T("Runtime Status"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityDesktopStatus,
		_T("Desktop Status"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityDesktopWebStatus,
		_T("Desktop Web Status"));
	m_parityMenu.AppendMenu(MF_SEPARATOR);
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySkillsStatus,
		_T("Skills Status"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySkillsList,
		_T("Skills List"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySkillsInfo,
		_T("Skills Info"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySkillsCheck,
		_T("Skills Check"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySkillsDiagnostics,
		_T("Skills Diagnostics"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySkillsInstallOptions,
		_T("Skills Install Options"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParitySkillsScanStatus,
		_T("Skills Scan Status"));
	m_parityMenu.AppendMenu(MF_SEPARATOR);
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityOperatorDiagnosticsReport,
		_T("Operator Diagnostics Report"));
	m_parityMenu.AppendMenu(
		MF_STRING,
		kIdUiParityOperatorPromotionReadiness,
		_T("Operator Promotion Readiness"));
	m_menuBar.AppendMenu(
		MF_POPUP,
		reinterpret_cast<UINT_PTR>(m_parityMenu.GetSafeHmenu()),
		_T("Parity"));
	SetMenu(&m_menuBar);
*/

/*	if (!m_menuBar.LoadMenu(IDR_MAINFRAME))
	{
		TRACE0("Failed to load menu resource IDR_MAINFRAME\n");
	}
	else
	{
		// Install as the window menu so normal menu routing & accelerators work
		SetMenu(&m_menuBar);

		//// Optional: expose the same menu from the Ribbon application button
		//// (attach HMENU to the app button and set it on the ribbon)
		//if (m_MainButton.GetSafeHwnd() == nullptr)
		//{
		//	// ensure the ribbon has an application button slot
		//	m_wndRibbonBar.SetApplicationButton(&m_MainButton, IDR_MAINFRAME);
		//}
		
		// Expose the same menu from the Ribbon application button.
		// CMFCRibbonApplicationButton is not a CWnd, so do not call GetSafeHwnd() on it.
		m_wndRibbonBar.SetApplicationButton(&m_MainButton, IDR_MAINFRAME);

		// If SetApplicationButton above is not present in your MFC version, you can
		// still attach the HMENU directly to the CMFCRibbonApplicationButton:
		m_MainButton.SetMenu(m_menuBar.GetSafeHmenu());
	}
*/
	SetWindowText(_T("BlazeClaw - Service Console"));
	m_dashboardPaneSyncReady.store(true, std::memory_order_release);
	return 0;
}

void CMainFrame::ShowParityResult(
	const wchar_t* title,
	const std::string& method,
	const std::optional<std::string>& paramsJson)
{
	//const auto* app =
	//	dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	//if (app == nullptr) {
	//	AfxMessageBox(_T("App context unavailable."));
	//	return;
	//}

	//const std::string result =
	//	app->Services().InvokeGatewayMethod(method, paramsJson);
	//const std::wstring body =
	//	L"Method: " + ToWide(method) +
	//	L"\n\nResult:\n" + ToWide(result);

	//AfxMessageBox(
	//	body.c_str(),
	//	MB_OK | MB_ICONINFORMATION,
	//	0);
	const auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr) {
		AfxMessageBox(_T("App context unavailable."));
		return;
	}

	const std::string result = app->Services().InvokeGatewayMethod(method, paramsJson);

	// Mask deepseekApiKey in returned JSON for UI safety
	std::string maskedResult = result;
	const std::string keyField = "\"deepseekApiKey\":\"";
	std::size_t pos = 0;
	while ((pos = maskedResult.find(keyField, pos)) != std::string::npos) {
		const std::size_t start = pos + keyField.size();
		const std::size_t end = maskedResult.find('\"', start);
		if (end == std::string::npos) break;
		const std::size_t len = end - start;
		std::string masked(len, '*');
		if (len > 6) {
			// keep last 4 chars visible
			masked.replace(masked.size() - 4, 4, maskedResult.substr(end - 4, 4));
		}
		maskedResult.replace(start, len, masked);
		pos = end + 1;
	}

	const std::wstring body = L"Method: " + ToWide(method) + L"\n\nResult:\n" + ToWide(maskedResult);

	AfxMessageBox(
		body.c_str(),
		MB_OK | MB_ICONINFORMATION,
		0);
}

void CMainFrame::OnUiParityActionFormProbe() {
	ShowParityResult(
		L"Action/Form Probe",
		"gateway.tools.call.preview",
		std::optional<std::string>(
			"{\"tool\":\"chat.send\",\"args\":{\"message\":\"ui-probe\"}}"));
}

void CMainFrame::OnUiParityAdminSnapshot() {
	ShowParityResult(
		L"Admin Snapshot",
		"gateway.config.snapshot");
}

void CMainFrame::OnUiParityAdminPolicyGet() {
	ShowParityResult(
		L"Admin Policy Get",
		"gateway.transport.policy.get");
}

void CMainFrame::OnUiParityAdminConfigAgent() {
	ShowParityResult(
		L"Admin Config Agent",
		"gateway.config.getSection",
		std::optional<std::string>("{\"section\":\"agent\"}"));
}

void CMainFrame::OnUiParityDeepSeekExtension() {
	//ShowParityResult(
	//	L"DeepSeek Extension",
	//	"gateway.models.listByProvider",
	//	std::optional<std::string>("{\"provider\":\"deepseek\"}"));
}

void CMainFrame::OnUiParitySessionList() {
	ShowParityResult(
		L"Session List",
		"gateway.session.list",
		std::optional<std::string>("{\"active\":true}"));
}

void CMainFrame::OnUiParitySessionActivate() {
	ShowParityResult(
		L"Session Activate",
		"gateway.sessions.activate",
		std::optional<std::string>("{\"sessionId\":\"main\"}"));
}

void CMainFrame::OnUiParityRuntimeStatus() {
	ShowParityResult(
		L"Runtime Status",
		"gateway.runtime.orchestration.status");
}

void CMainFrame::OnUiParityDesktopStatus() {
	ShowParityResult(
		L"Desktop Status",
		"gateway.platform.cli.status");
}

void CMainFrame::OnUiParityDesktopWebStatus() {
	ShowParityResult(
		L"Desktop Web Status",
		"gateway.platform.web.status");
}

void CMainFrame::OnUiParitySkillsStatus() {
	ShowParityResult(
		L"Skills Status",
		"gateway.skills.status");
}

void CMainFrame::OnUiParitySkillsList() {
	ShowParityResult(
		L"Skills List",
		"gateway.skills.list",
		std::optional<std::string>("{\"includeInvalid\":true}"));
}

void CMainFrame::OnUiParitySkillsInfo() {
	ShowParityResult(
		L"Skills Info",
		"gateway.skills.info",
		std::optional<std::string>("{\"skill\":\"install-node\"}"));
}

void CMainFrame::OnUiParitySkillsCheck() {
	ShowParityResult(
		L"Skills Check",
		"gateway.skills.check");
}

void CMainFrame::OnUiParitySkillsDiagnostics() {
	ShowParityResult(
		L"Skills Diagnostics",
		"gateway.skills.diagnostics");
}

void CMainFrame::OnUiParitySkillsInstallOptions() {
	ShowParityResult(
		L"Skills Install Options",
		"gateway.skills.install.options");
}

void CMainFrame::OnUiParitySkillsScanStatus() {
	ShowParityResult(
		L"Skills Scan Status",
		"gateway.skills.scan.status");
}

void CMainFrame::OnUiParityOperatorDiagnosticsReport()
{
	//const auto* app =
	//	dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	//if (app == nullptr) {
	//	AfxMessageBox(_T("App context unavailable."));
	//	return;
	//}

	//const std::string report =
	//	app->Services().BuildOperatorDiagnosticsReport();
	//const std::wstring message =
	//	L"Operator Diagnostics Report\n\n" + ToWide(report);

	//AfxMessageBox(
	//	message.c_str(),
	//	MB_OK | MB_ICONINFORMATION,
	//	0);
}

void CMainFrame::OnUiParityOperatorPromotionReadiness()
{
	//const auto* app =
	//	dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	//if (app == nullptr) {
	//	AfxMessageBox(_T("App context unavailable."));
	//	return;
	//}

	//const auto& registry = app->Services().Registry();
	//std::size_t implemented = 0;
	//std::size_t planned = 0;
	//std::size_t inProgress = 0;

	//for (const auto& feature : registry.Features()) {
	//	if (feature.state == blazeclaw::core::FeatureState::Implemented) {
	//		++implemented;
	//		continue;
	//	}

	//	if (feature.state == blazeclaw::core::FeatureState::InProgress) {
	//		++inProgress;
	//		continue;
	//	}

	//	++planned;
	//}

	//const bool promotionReady =
	//	app->Services().IsRunning() &&
	//	inProgress == 0 &&
	//	planned == 0;

	//std::wstring message = L"Promotion Readiness\n\n";
	//message += L"Runtime Running: ";
	//message += app->Services().IsRunning() ? L"yes" : L"no";
	//message += L"\nImplemented Features: ";
	//message += std::to_wstring(implemented);
	//message += L"\nIn-Progress Features: ";
	//message += std::to_wstring(inProgress);
	//message += L"\nPlanned Features: ";
	//message += std::to_wstring(planned);
	//message += L"\n\nPromotion Ready: ";
	//message += promotionReady ? L"yes" : L"no";

	//AfxMessageBox(
	//	message.c_str(),
	//	MB_OK | MB_ICONINFORMATION,
	//	0);
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs)
{
	if (!CMDIFrameWndEx::PreCreateWindow(cs))
		return FALSE;
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	cs.style = WS_OVERLAPPED | WS_CAPTION | FWS_ADDTOTITLE
		| WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_MAXIMIZE | WS_SYSMENU;

	return TRUE;
}

BOOL CMainFrame::CreateDockingWindows()
{
	BOOL bNameValid;

	// Create class view
	CString strClassView;
	bNameValid = strClassView.LoadString(IDS_CLASS_VIEW);
	ASSERT(bNameValid);
	if (!m_wndSkillView.Create(strClassView, this, CRect(0, 0, 200, 200), TRUE, ID_VIEW_CLASSVIEW, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_LEFT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Class View window\n");
		return FALSE; // failed to create
	}

	// Create file view
	CString strFileView;
	bNameValid = strFileView.LoadString(IDS_FILE_VIEW);
	ASSERT(bNameValid);
	if (!m_wndFileView.Create(strFileView, this, CRect(0, 0, 200, 200), TRUE, ID_VIEW_FILEVIEW, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_LEFT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create File View window\n");
		return FALSE; // failed to create
	}

	// Create output window
	CString strOutputWnd;
	bNameValid = strOutputWnd.LoadString(IDS_OUTPUT_WND);
	ASSERT(bNameValid);
	if (!m_wndOutput.Create(strOutputWnd, this, CRect(0, 0, 100, 100), TRUE, ID_VIEW_OUTPUTWND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_BOTTOM | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Output window\n");
		return FALSE; // failed to create
	}

	// Create properties window
	CString strPropertiesWnd;
	bNameValid = strPropertiesWnd.LoadString(IDS_PROPERTIES_WND);
	ASSERT(bNameValid);
	if (!m_wndProperties.Create(strPropertiesWnd, this, CRect(0, 0, 200, 200), TRUE, ID_VIEW_PROPERTIESWND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Properties window\n");
		return FALSE; // failed to create
	}

	CString strDashboardWnd;
	bNameValid = strDashboardWnd.LoadString(IDS_DASHBOARD_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard.Create(strDashboardWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARDWND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard window\n");
		return FALSE; // failed to create
	}

	CString strDashboardOverviewWnd;
	bNameValid = strDashboardOverviewWnd.LoadString(IDS_DASHBOARD_OVERVIEW_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_overview.Create(strDashboardOverviewWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_OVERVIEW_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Overview window\n");
		return FALSE; // failed to create
	}

	CString strDashboardToolsWnd;
	bNameValid = strDashboardToolsWnd.LoadString(IDS_DASHBOARD_TOOLS_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_tools.Create(strDashboardToolsWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_TOOLS_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Tools window\n");
		return FALSE; // failed to create
	}

	CString strDashboardFilesWnd;
	bNameValid = strDashboardFilesWnd.LoadString(IDS_DASHBOARD_FILES_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_files.Create(strDashboardFilesWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_FILES_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Files window\n");
		return FALSE; // failed to create
	}

	CString strDashboardSkillsWnd;
	bNameValid = strDashboardSkillsWnd.LoadString(IDS_DASHBOARD_SKILLS_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_skills.Create(strDashboardSkillsWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_SKILLS_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Skills window\n");
		return FALSE; // failed to create
	}

	CString strDashboardChannelsWnd;
	bNameValid = strDashboardChannelsWnd.LoadString(IDS_DASHBOARD_CHANNELS_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_channels.Create(strDashboardChannelsWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_CHANNELS_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Channels window\n");
		return FALSE; // failed to create
	}

	CString strDashboardCronWnd;
	bNameValid = strDashboardCronWnd.LoadString(IDS_DASHBOARD_CRON_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_cron.Create(strDashboardCronWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_CRON_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Cron Dashboard window\n");
		return FALSE; // failed to create
	}

	CString strDashboardDreamingWnd;
	bNameValid = strDashboardDreamingWnd.LoadString(IDS_DASHBOARD_DREAMING_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_dreaming.Create(strDashboardDreamingWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_DREAMING_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Dreaming window\n");
		return FALSE; // failed to create
	}

	CString strDashboardNodesWnd;
	bNameValid = strDashboardNodesWnd.LoadString(IDS_DASHBOARD_NODES_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_nodes.Create(strDashboardNodesWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_NODES_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Nodes window\n");
		return FALSE; // failed to create
	}

	CString strDashboardInstancesWnd;
	bNameValid = strDashboardInstancesWnd.LoadString(IDS_DASHBOARD_INSTANCES_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_instances.Create(strDashboardInstancesWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_INSTANCES_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Instances window\n");
		return FALSE; // failed to create
	}

	CString strDashboardUsageWnd;
	bNameValid = strDashboardUsageWnd.LoadString(IDS_DASHBOARD_USAGE_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_usage.Create(strDashboardUsageWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_USAGE_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Usage window\n");
		return FALSE; // failed to create
	}

	CString strDashboardDevicesWnd;
	bNameValid = strDashboardDevicesWnd.LoadString(IDS_DASHBOARD_DEVICES_WND);
	ASSERT(bNameValid);
	if (!m_wndDashboard_devices.Create(strDashboardDevicesWnd, this, CRect(0, 0, 512, 1024), TRUE, ID_VIEW_DASHBOARD_DEVICES_WND, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | CBRS_RIGHT | CBRS_FLOAT_MULTI))
	{
		TRACE0("Failed to create Dashboard Devices window\n");
		return FALSE; // failed to create
	}

	SetDockingWindowIcons(theApp.m_bHiColorIcons);
	return TRUE;
}

void CMainFrame::SetDockingWindowIcons(BOOL bHiColorIcons)
{
	HICON hFileViewIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_FILE_VIEW_HC : IDI_FILE_VIEW), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndFileView.SetIcon(hFileViewIcon, FALSE);

	HICON hClassViewIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_CLASS_VIEW_HC : IDI_CLASS_VIEW), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndSkillView.SetIcon(hClassViewIcon, FALSE);

	HICON hOutputBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_OUTPUT_WND_HC : IDI_OUTPUT_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndOutput.SetIcon(hOutputBarIcon, FALSE);

	HICON hPropertiesBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_PROPERTIES_WND_HC : IDI_PROPERTIES_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndProperties.SetIcon(hPropertiesBarIcon, FALSE);

	HICON hDashboardBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_WND_HC : IDI_DASHBOARD_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard.SetIcon(hDashboardBarIcon, FALSE);

	HICON hDashboardOverviewBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_OVERVIEW_WND_HC : IDI_DASHBOARD_OVERVIEW_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_overview.SetIcon(hDashboardOverviewBarIcon, FALSE);

	HICON hDashboardToolsBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_TOOLS_WND_HC : IDI_DASHBOARD_TOOLS_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_tools.SetIcon(hDashboardToolsBarIcon, FALSE);

	HICON hDashboardFilesBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_FILES_WND_HC : IDI_DASHBOARD_FILES_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_files.SetIcon(hDashboardFilesBarIcon, FALSE);

	HICON hDashboardSkillsBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_SKILLS_WND_HC : IDI_DASHBOARD_SKILLS_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_skills.SetIcon(hDashboardSkillsBarIcon, FALSE);

	HICON hDashboardChannelsBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_CHANNELS_WND_HC : IDI_DASHBOARD_CHANNELS_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_channels.SetIcon(hDashboardChannelsBarIcon, FALSE);

	HICON hDashboardCronBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_CRON_WND_HC : IDI_DASHBOARD_CRON_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_cron.SetIcon(hDashboardCronBarIcon, FALSE);

	HICON hDashboardDreamingBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_DREAMING_WND_HC : IDI_DASHBOARD_DREAMING_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_dreaming.SetIcon(hDashboardDreamingBarIcon, FALSE);

	HICON hDashboardNodesBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_NODES_WND_HC : IDI_DASHBOARD_NODES_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_nodes.SetIcon(hDashboardNodesBarIcon, FALSE);

	HICON hDashboardInstancesBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_INSTANCES_WND_HC : IDI_DASHBOARD_INSTANCES_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_instances.SetIcon(hDashboardInstancesBarIcon, FALSE);

	HICON hDashboardUsageBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_USAGE_WND_HC : IDI_DASHBOARD_USAGE_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_usage.SetIcon(hDashboardUsageBarIcon, FALSE);

	HICON hDashboardDevicesBarIcon = (HICON) ::LoadImage(::AfxGetResourceHandle(), MAKEINTRESOURCE(bHiColorIcons ? IDI_DASHBOARD_DEVICES_WND_HC : IDI_DASHBOARD_DEVICES_WND), IMAGE_ICON, ::GetSystemMetrics(SM_CXSMICON), ::GetSystemMetrics(SM_CYSMICON), 0);
	m_wndDashboard_devices.SetIcon(hDashboardDevicesBarIcon, FALSE);

	UpdateMDITabbedBarsIcons();
}

BOOL CMainFrame::CreateOutlookBar(CMFCOutlookBar& bar, UINT uiID, CMFCShellTreeCtrl& tree, CCalendarBar& calendar, int nInitialWidth)
{
	bar.SetMode2003();

	BOOL bNameValid;
	CString strTemp;
	bNameValid = strTemp.LoadString(IDS_SHORTCUTS);
	ASSERT(bNameValid);
	if (!bar.Create(strTemp, this, CRect(0, 0, nInitialWidth, 32000), uiID, WS_CHILD | WS_VISIBLE | CBRS_LEFT))
	{
		return FALSE; // fail to create
	}

	CMFCOutlookBarTabCtrl* pOutlookBar = (CMFCOutlookBarTabCtrl*)bar.GetUnderlyingWindow();

	if (pOutlookBar == nullptr)
	{
		ASSERT(FALSE);
		return FALSE;
	}

	pOutlookBar->EnableInPlaceEdit(TRUE);

	static UINT uiPageID = 1;

	// can float, can autohide, can resize, CAN NOT CLOSE
	DWORD dwStyle = AFX_CBRS_FLOAT | AFX_CBRS_AUTOHIDE | AFX_CBRS_RESIZE;

	CRect rectDummy(0, 0, 0, 0);
	const DWORD dwTreeStyle = WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS;

	tree.Create(dwTreeStyle, rectDummy, &bar, 1200);
	bNameValid = strTemp.LoadString(IDS_FOLDERS);
	ASSERT(bNameValid);
	pOutlookBar->AddControl(&tree, strTemp, 2, TRUE, dwStyle);

	calendar.Create(rectDummy, &bar, 1201);
	bNameValid = strTemp.LoadString(IDS_CALENDAR);
	ASSERT(bNameValid);
	pOutlookBar->AddControl(&calendar, strTemp, 3, TRUE, dwStyle);

	bar.SetPaneStyle(bar.GetPaneStyle() | CBRS_TOOLTIPS | CBRS_FLYBY | CBRS_SIZE_DYNAMIC);

	pOutlookBar->SetImageList(theApp.m_bHiColorIcons ? IDB_PAGES_HC : IDB_PAGES, 24);
	pOutlookBar->SetToolbarImageList(theApp.m_bHiColorIcons ? IDB_PAGES_SMALL_HC : IDB_PAGES_SMALL, 16);
	pOutlookBar->RecalcLayout();

	BOOL bAnimation = theApp.GetInt(_T("OutlookAnimation"), TRUE);
	CMFCOutlookBarTabCtrl::EnableAnimation(bAnimation);

	bar.SetButtonsFont(&afxGlobalData.fontBold);

	return TRUE;
}

BOOL CMainFrame::CreateCaptionBar()
{
	if (!m_wndCaptionBar.Create(WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, this, ID_VIEW_CAPTION_BAR, -1, TRUE))
	{
		TRACE0("Failed to create caption bar\n");
		return FALSE;
	}

	BOOL bNameValid;

	CString strTemp, strTemp2;
	bNameValid = strTemp.LoadString(IDS_CAPTION_BUTTON);
	ASSERT(bNameValid);
	m_wndCaptionBar.SetButton(strTemp, ID_TOOLS_OPTIONS, CMFCCaptionBar::ALIGN_LEFT, FALSE);
	bNameValid = strTemp.LoadString(IDS_CAPTION_BUTTON_TIP);
	ASSERT(bNameValid);
	m_wndCaptionBar.SetButtonToolTip(strTemp);

	bNameValid = strTemp.LoadString(IDS_CAPTION_TEXT);
	ASSERT(bNameValid);
	m_wndCaptionBar.SetText(strTemp, CMFCCaptionBar::ALIGN_LEFT);

	m_wndCaptionBar.SetBitmap(IDB_INFO, RGB(255, 255, 255), FALSE, CMFCCaptionBar::ALIGN_LEFT);
	bNameValid = strTemp.LoadString(IDS_CAPTION_IMAGE_TIP);
	ASSERT(bNameValid);
	bNameValid = strTemp2.LoadString(IDS_CAPTION_IMAGE_TEXT);
	ASSERT(bNameValid);
	m_wndCaptionBar.SetImageToolTip(strTemp, strTemp2);

	return TRUE;
}

// CMainFrame diagnostics

#ifdef _DEBUG
void CMainFrame::AssertValid() const
{
	CMDIFrameWndEx::AssertValid();
}

void CMainFrame::Dump(CDumpContext& dc) const
{
	CMDIFrameWndEx::Dump(dc);
}
#endif //_DEBUG


// CMainFrame message handlers

void CMainFrame::OnWindowManager()
{
	ShowWindowsDialog();
}

void CMainFrame::OnApplicationLook(UINT id)
{
	CWaitCursor wait;

	theApp.m_nAppLook = id;

	switch (theApp.m_nAppLook)
	{
	case ID_VIEW_APPLOOK_WIN_2000:
		CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManager));
		m_wndRibbonBar.SetWindows7Look(FALSE);
		break;

	case ID_VIEW_APPLOOK_OFF_XP:
		CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerOfficeXP));
		m_wndRibbonBar.SetWindows7Look(FALSE);
		break;

	case ID_VIEW_APPLOOK_WIN_XP:
		CMFCVisualManagerWindows::m_b3DTabsXPTheme = TRUE;
		CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows));
		m_wndRibbonBar.SetWindows7Look(FALSE);
		break;

	case ID_VIEW_APPLOOK_OFF_2003:
		CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerOffice2003));
		CDockingManager::SetDockingMode(DT_SMART);
		m_wndRibbonBar.SetWindows7Look(FALSE);
		break;

	case ID_VIEW_APPLOOK_VS_2005:
		CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerVS2005));
		CDockingManager::SetDockingMode(DT_SMART);
		m_wndRibbonBar.SetWindows7Look(FALSE);
		break;

	case ID_VIEW_APPLOOK_VS_2008:
		CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerVS2008));
		CDockingManager::SetDockingMode(DT_SMART);
		m_wndRibbonBar.SetWindows7Look(FALSE);
		break;

	case ID_VIEW_APPLOOK_WINDOWS_7:
		CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerWindows7));
		CDockingManager::SetDockingMode(DT_SMART);
		m_wndRibbonBar.SetWindows7Look(TRUE);
		break;

	default:
		switch (theApp.m_nAppLook)
		{
		case ID_VIEW_APPLOOK_OFF_2007_BLUE:
			CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_LunaBlue);
			break;

		case ID_VIEW_APPLOOK_OFF_2007_BLACK:
			CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_ObsidianBlack);
			break;

		case ID_VIEW_APPLOOK_OFF_2007_SILVER:
			CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_Silver);
			break;

		case ID_VIEW_APPLOOK_OFF_2007_AQUA:
			CMFCVisualManagerOffice2007::SetStyle(CMFCVisualManagerOffice2007::Office2007_Aqua);
			break;
		}

		CMFCVisualManager::SetDefaultManager(RUNTIME_CLASS(CMFCVisualManagerOffice2007));
		CDockingManager::SetDockingMode(DT_SMART);
		m_wndRibbonBar.SetWindows7Look(FALSE);
	}

	m_wndOutput.UpdateFonts();
	RedrawWindow(nullptr, nullptr, RDW_ALLCHILDREN | RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME | RDW_ERASE);

	theApp.WriteInt(_T("ApplicationLook"), theApp.m_nAppLook);
}

void CMainFrame::OnUpdateApplicationLook(CCmdUI* pCmdUI)
{
	pCmdUI->SetRadio(theApp.m_nAppLook == pCmdUI->m_nID);
}

void CMainFrame::OnViewCaptionBar()
{
	m_wndCaptionBar.ShowWindow(m_wndCaptionBar.IsVisible() ? SW_HIDE : SW_SHOW);
	RecalcLayout(FALSE);
}

void CMainFrame::OnUpdateViewCaptionBar(CCmdUI* pCmdUI)
{
	pCmdUI->SetCheck(m_wndCaptionBar.IsVisible());
}

void CMainFrame::OnOptions()
{
	CMFCRibbonCustomizeDialog* pOptionsDlg = new CMFCRibbonCustomizeDialog(this, &m_wndRibbonBar);
	ASSERT(pOptionsDlg != nullptr);

	pOptionsDlg->DoModal();
	delete pOptionsDlg;
}

void CMainFrame::OnViewFileView()
{
	// Show or activate the pane, depending on current state.  The
	// pane can only be closed via the [x] button on the pane frame.
	m_wndFileView.ShowPane(TRUE, FALSE, TRUE);
	m_wndFileView.SetFocus();
}

void CMainFrame::OnUpdateViewFileView(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

void CMainFrame::OnViewClassView()
{
	// Show or activate the pane, depending on current state.  The
	// pane can only be closed via the [x] button on the pane frame.
	m_wndSkillView.ShowPane(TRUE, FALSE, TRUE);
	m_wndSkillView.SetFocus();
}

void CMainFrame::OnUpdateViewClassView(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

void CMainFrame::OnViewOutputWindow()
{
	// Show or activate the pane, depending on current state.  The
	// pane can only be closed via the [x] button on the pane frame.
	m_wndOutput.ShowPane(TRUE, FALSE, TRUE);
	m_wndOutput.SetFocus();
}

void CMainFrame::OnUpdateViewOutputWindow(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

void CMainFrame::OnViewPropertiesWindow()
{
	// Show or activate the pane, depending on current state.  The
	// pane can only be closed via the [x] button on the pane frame.
	m_wndProperties.ShowPane(TRUE, FALSE, TRUE);
	m_wndProperties.SetFocus();
}

void CMainFrame::OnUpdateViewPropertiesWindow(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

void CMainFrame::OnViewDashboardWindow()
{
	ActivateDashboardPane(m_wndDashboard);
}

void CMainFrame::OnUpdateViewDashboardWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool dashboardVisible =
		m_wndDashboard.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(dashboardVisible);
}

void CMainFrame::OnViewDashboardOverviewWindow()
{
	ActivateDashboardPane(m_wndDashboard_overview);
}

void CMainFrame::OnUpdateViewDashboardOverviewWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_overview.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_overview.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardToolsWindow()
{
	ActivateDashboardPane(m_wndDashboard_tools);
}

void CMainFrame::OnUpdateViewDashboardToolsWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_tools.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_tools.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardFilesWindow()
{
	ActivateDashboardPane(m_wndDashboard_files);
}

void CMainFrame::OnUpdateViewDashboardFilesWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_files.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_files.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardSkillsWindow()
{
	ActivateDashboardPane(m_wndDashboard_skills);
}

void CMainFrame::OnUpdateViewDashboardSkillsWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_skills.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_skills.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardChannelsWindow()
{
	ActivateDashboardPane(m_wndDashboard_channels);
}

void CMainFrame::OnUpdateViewDashboardChannelsWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_channels.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_channels.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardCronWindow()
{
	ActivateDashboardPane(m_wndDashboard_cron);
}

void CMainFrame::OnUpdateViewDashboardCronWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_cron.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_cron.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardDreamingWindow()
{
	ActivateDashboardPane(m_wndDashboard_dreaming);
}

void CMainFrame::OnUpdateViewDashboardDreamingWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_dreaming.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_dreaming.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardNodesWindow()
{
	ActivateDashboardPane(m_wndDashboard_nodes);
}

void CMainFrame::OnUpdateViewDashboardNodesWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_nodes.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_nodes.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardInstancesWindow()
{
	ActivateDashboardPane(m_wndDashboard_instances);
}

void CMainFrame::OnUpdateViewDashboardInstancesWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_instances.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_instances.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardUsageWindow()
{
	ActivateDashboardPane(m_wndDashboard_usage);
}

void CMainFrame::OnUpdateViewDashboardUsageWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_usage.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_usage.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}

void CMainFrame::OnViewDashboardDevicesWindow()
{
	ActivateDashboardPane(m_wndDashboard_devices);
}

void CMainFrame::OnUpdateViewDashboardDevicesWindow(CCmdUI* pCmdUI)
{
	if (pCmdUI == nullptr)
	{
		return;
	}

	pCmdUI->Enable(TRUE);

	const bool isVisible =
		m_wndDashboard_devices.GetSafeHwnd() != nullptr &&
		::IsWindowVisible(m_wndDashboard_devices.GetSafeHwnd()) != FALSE;
	pCmdUI->SetCheck(isVisible);
}
void CMainFrame::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	CMDIFrameWndEx::OnSettingChange(uFlags, lpszSection);
	m_wndOutput.UpdateFonts();
}

void CMainFrame::OnExtensionDeepseek()
{
	LogDeepSeekDiagnostic(
		"configure",
		"DeepSeek extension action started.");

	// Show modal API key input dialog (prefill from saved config if available)
	CApiKeyDialog dlg(this);
	// Load existing key from app config if present
	// Load existing key from Windows Credential Manager if available
	bool loadedFromAny = false;
	std::string credentialSource = "missing";
	if (const auto stored = blazeclaw::app::CredentialStore::LoadCredential(L"blazeclaw.deepseek"); stored.has_value()) {
		// stored contains UTF-8 bytes
		const std::string s = *stored;
		int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
		if (needed > 0) {
			std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1);
			::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), buf.data(), needed);
			dlg.m_apiKey = buf.data();
		}
		loadedFromAny = true;
		credentialSource = "CredentialManager";
		LogDeepSeekDiagnostic(
			"configure",
			"Loaded existing credential from Credential Manager.");
	}

	if (!loadedFromAny) {
		// Try DPAPI per-user file fallback
		wchar_t appdataBuf[MAX_PATH];
		if (GetEnvironmentVariableW(L"APPDATA", appdataBuf, (DWORD)std::size(appdataBuf)) > 0) {
			std::wstring dpPath =
				std::wstring(appdataBuf) +
				L"\\BlazeClaw\\deepseek.key";
			if (const auto dp = blazeclaw::app::CredentialStore::LoadCredentialDPAPI(dpPath); dp.has_value()) {
				const std::string s = *dp;
				int needed = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
				if (needed > 0) {
					std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1);
					::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), buf.data(), needed);
					dlg.m_apiKey = buf.data();
					// attempt to migrate back to Credential Manager
					blazeclaw::app::CredentialStore::SaveCredential(L"blazeclaw.deepseek", s);
					credentialSource = "DPAPI";
					LogDeepSeekDiagnostic(
						"configure",
						"Loaded existing credential from DPAPI fallback.");
				}
			}
			else {
				// Fallback: if config contains a plaintext key (older installs), load that and then migrate it
				blazeclaw::config::AppConfig tempCfg;
				blazeclaw::config::ConfigLoader loader;
				if (loader.LoadFromFile(L"blazeclaw.conf", tempCfg) && !tempCfg.deepseekApiKey.empty()) {
					dlg.m_apiKey = tempCfg.deepseekApiKey.c_str();
					// migrate to credential store and DPAPI
					std::wstring wk = tempCfg.deepseekApiKey;
					int needed = ::WideCharToMultiByte(
						CP_UTF8,
						0,
						wk.c_str(),
						-1,
						nullptr,
						0,
						nullptr,
						nullptr);
					if (needed > 0) {
						std::vector<char> buf(static_cast<size_t>(needed));
						::WideCharToMultiByte(CP_UTF8, 0, wk.c_str(), -1, buf.data(), needed, nullptr, nullptr);
						const std::string utf8(buf.data());
						blazeclaw::app::CredentialStore::SaveCredential(L"blazeclaw.deepseek", utf8);
						// ensure dpapi directory exists and save
						std::wstring dir = std::wstring(appdataBuf) + L"\\BlazeClaw";
						CreateDirectoryW(dir.c_str(), nullptr);
						std::wstring dpPath2 = dir + L"\\deepseek.key";
						blazeclaw::app::CredentialStore::SaveCredentialDPAPI(dpPath2, utf8);
						credentialSource = "ConfigMigration";
						LogDeepSeekDiagnostic(
							"configure",
							"Migrated legacy config credential into secure store.");
					}
				}
			}
		}
	}

	if (!loadedFromAny) {
		LogDeepSeekDiagnostic(
			"configure",
			"No existing credential found; waiting for user input.");
	}

	if (dlg.DoModal() != IDOK) {
		LogDeepSeekDiagnostic(
			"configure",
			"Configuration dialog cancelled by user.");
		OnUiParityDeepSeekExtension();
		return;
	}

	const CString apiKeyCs = dlg.m_apiKey;
	if (apiKeyCs.IsEmpty()) {
		LogDeepSeekDiagnostic(
			"configure",
			"Configuration rejected: empty API key input.");
		OnUiParityDeepSeekExtension();
		return;
	}

	// Convert wide string (CString) to UTF-8
	const std::wstring apiKeyW(apiKeyCs);
	int needed = ::WideCharToMultiByte(CP_UTF8, 0, apiKeyW.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string apiKey;
	if (needed > 0) {
		std::vector<char> buf(static_cast<size_t>(needed));
		::WideCharToMultiByte(CP_UTF8, 0, apiKeyW.c_str(), -1, buf.data(), needed, nullptr, nullptr);
		apiKey.assign(buf.data());
	}

	const std::string params = std::string("{\"model\":\"deepseek-chat\",\"deepseekApiKey\":\"") +
		apiKey + "\"}";

	// Save the API key into Windows Credential Manager instead of plain config file
	const std::wstring credTarget = L"blazeclaw.deepseek";
	blazeclaw::app::CredentialStore::SaveCredential(credTarget, apiKey);
	LogDeepSeekDiagnostic(
		"configure",
		"Credential saved to secure store.");

	// Remove any plaintext deepseek.apiKey lines from config file entirely
	const std::wstring configPath = L"blazeclaw.conf";
	std::vector<std::wstring> lines;
	std::wifstream infile(configPath);
	bool updated = false;
	if (infile.is_open()) {
		std::wstring line;
		while (std::getline(infile, line)) {
			const std::wstring t = TrimMain(line);
			if (t.rfind(L"deepseek.apiKey=", 0) == 0) {
				// drop the line entirely
				updated = true;
				continue;
			}
			lines.push_back(line);
		}
		infile.close();
	}

	if (updated) {
		std::wofstream outfile(configPath, std::ios::trunc);
		if (outfile.is_open()) {
			for (const auto& l : lines) {
				outfile << l << L"\n";
			}
			outfile.close();
		}
	}

	// Mask the API key for display
	std::string masked(apiKey.size(), '*');
	if (apiKey.size() > 6) {
		// keep last 4 characters visible
		masked.replace(masked.size() - 4, 4, apiKey.substr(apiKey.size() - 4));
	}

	const std::string maskedParams = std::string("{\"model\":\"deepseek-chat\",\"deepseekApiKey\":\"") +
		masked + "\"}";

	ShowParityResult(L"Configure DeepSeek", "gateway.config.set", std::optional<std::string>(params));
	LogDeepSeekDiagnostic(
		"configure",
		std::string("Provider activation request sent. source=") + credentialSource);

	// Activate provider/model for chat pipeline at app runtime level
	if (auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp()); app != nullptr) {
		app->Services().SetActiveChatProvider("deepseek", "deepseek-chat");
		LogDeepSeekDiagnostic(
			"configure",
			"Active provider set to deepseek-chat.");
	}

	// Show masked response and an action to remove/rotate the stored key
	const std::string display = std::string("Requested: gateway.config.set\nParams: ") + maskedParams +
		"\n\nThe API key is stored securely in Windows Credential Manager under 'blazeclaw.deepseek'.";
	// Offer rotate (Yes), remove (No), or close (Cancel)
	const int msgRes = AfxMessageBox(CA2W(display.c_str()), MB_YESNOCANCEL | MB_ICONINFORMATION);
	if (msgRes == IDYES) {
		// Rotate: reopen dialog to enter new key
		CApiKeyDialog dlg2(this);
		if (dlg2.DoModal() == IDOK && !dlg2.m_apiKey.IsEmpty()) {
			const std::wstring newKeyW(dlg2.m_apiKey);
			int needed2 = ::WideCharToMultiByte(CP_UTF8, 0, newKeyW.c_str(), -1, nullptr, 0, nullptr, nullptr);
			if (needed2 > 0) {
				std::vector<char> buf2(static_cast<size_t>(needed2));
				::WideCharToMultiByte(CP_UTF8, 0, newKeyW.c_str(), -1, buf2.data(), needed2, nullptr, nullptr);
				blazeclaw::app::CredentialStore::SaveCredential(L"blazeclaw.deepseek", std::string(buf2.data()));
				LogDeepSeekDiagnostic(
					"configure",
					"Credential rotated successfully.");
				AfxMessageBox(_T("DeepSeek API key rotated and stored."), MB_OK | MB_ICONINFORMATION);
			}
		}
	}
	else if (msgRes == IDNO) {
		// Remove the stored credential
		if (blazeclaw::app::CredentialStore::DeleteCredential(L"blazeclaw.deepseek")) {
			LogDeepSeekDiagnostic(
				"configure",
				"Credential removed from secure store.");
			AfxMessageBox(_T("DeepSeek API key removed."), MB_OK | MB_ICONINFORMATION);
		}
		else {
			LogDeepSeekDiagnostic(
				"configure",
				"Credential remove requested but no entry found.");
			AfxMessageBox(_T("No stored DeepSeek API key found."), MB_OK | MB_ICONINFORMATION);
		}
	}
}

void CMainFrame::OnUpdateExtensionDeepseek(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

void CMainFrame::OnExtensionModelSet()
{
	CSettingsDialog dlg(this);
	if (dlg.DoModal() == IDOK) {
		// `chat.model.enabled.*` and active provider/model lines are written by
		// `CSettingsDialog::OnOK` to `blazeclaw.conf` (same as Settings from the dialog).
	}
}

void CMainFrame::OpenNewTabWithChoiceDialog()
{
	OpenWebViewPlusChatTab();
}

void CMainFrame::OnWindowNew()
{
	OpenNewTabWithChoiceDialog();
}

void CMainFrame::OnWindowNewWebViewChat()
{
	OpenWebViewPlusChatTab();
}

void CMainFrame::OnWindowNewWebViewMarkdown()
{
	OpenWebViewMarkdownTab();
}

void CMainFrame::OnWindowNewAIChatView()
{
	OpenAIChatViewTab();
}

void CMainFrame::OpenWebViewPlusChatTab()
{
	// Clear any pending URL from previous SkillView interactions
	CBlazeClawMFCView::ClearPendingStartupState();

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	auto* tpl = app ? app->GetChatDocTemplate() : nullptr;
	TRACE(_T("[CMainFrame::OpenWebViewPlusChatTab] app=%p tpl=%p\n"), app, tpl);
	if (!tpl)
		return;

	CDocument* pDoc = tpl->OpenDocumentFile(nullptr);
	TRACE(_T("[CMainFrame::OpenWebViewPlusChatTab] pDoc=%p\n"), pDoc);
	if (!pDoc)
		return;

	AddChatStatusLine(_T("[Tab] New WebView+Chat tab created."));
}

CWnd* CMainFrame::FindChildFrameWithSkill(const std::string& skillKey) const
{
	// Normalize the search key
	std::string normalizedSearch;
	normalizedSearch.reserve(skillKey.size());
	for (const char ch : skillKey)
	{
		if (ch == '-')
		{
			normalizedSearch.push_back('_');
		}
		else
		{
			normalizedSearch.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
		}
	}

	// Enumerate all MDI child windows
	for (HWND hWndChild = ::GetWindow(::AfxGetMainWnd()->m_hWnd, GW_CHILD);
		hWndChild != nullptr;
		hWndChild = ::GetWindow(hWndChild, GW_HWNDNEXT))
	{
		// Check if it's an MDI client
		TCHAR className[64];
		::GetClassName(hWndChild, className, _countof(className));
		if (_tcscmp(className, _T("MDIClient")) != 0)
			continue;

		// Enumerate MDI children under this client
		for (HWND hWndMdiChild = ::GetWindow(hWndChild, GW_CHILD);
			hWndMdiChild != nullptr;
			hWndMdiChild = ::GetWindow(hWndMdiChild, GW_HWNDNEXT))
		{
			// Try to cast to CChildFrame
			CWnd* pWnd = CWnd::FromHandlePermanent(hWndMdiChild);
			if (pWnd == nullptr)
				continue;

			auto* childFrame = DYNAMIC_DOWNCAST(CChildFrame, pWnd);
			if (childFrame == nullptr)
				continue;

			// Normalize the frame's skill key
			const std::string& frameSkillKey = childFrame->GetCurrentSkillKey();
			if (frameSkillKey.empty())
				continue;

			std::string normalizedFrame;
			normalizedFrame.reserve(frameSkillKey.size());
			for (const char ch : frameSkillKey)
			{
				if (ch == '-')
				{
					normalizedFrame.push_back('_');
				}
				else
				{
					normalizedFrame.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
				}
			}

			if (normalizedFrame == normalizedSearch)
			{
				TRACE(_T("[CMainFrame::FindChildFrameWithSkill] found match: %s\n"), frameSkillKey.c_str());
				return childFrame;
			}
		}
	}

	TRACE(_T("[CMainFrame::FindChildFrameWithSkill] no match found for: %s\n"), skillKey.c_str());
	return nullptr;
}

bool CMainFrame::OpenSkillViewTab(
	const std::string& skillKey,
	const std::string& propertiesJson)
{
	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	auto* tpl = app ? app->GetChatDocTemplate() : nullptr;
	TRACE(_T("[CMainFrame::OpenSkillViewTab] skillKey=%s app=%p tpl=%p\n"),
		CA2W(skillKey.c_str()).m_psz, app, tpl);
	if (!tpl)
		return false;

	// Check if this skill already has an open tab
	CWnd* pExistingWnd = FindChildFrameWithSkill(skillKey);
	if (pExistingWnd != nullptr)
	{
		TRACE(_T("[CMainFrame::OpenSkillViewTab] found existing tab for skill, activating\n"));
		// Cast to CChildFrame for activation
		auto* pExistingFrame = DYNAMIC_DOWNCAST(CChildFrame, pExistingWnd);
		if (pExistingFrame != nullptr)
		{
			CMDIChildWndEx* pMdiChild = DYNAMIC_DOWNCAST(CMDIChildWndEx, pExistingFrame);
			if (pMdiChild != nullptr)
			{
				pMdiChild->ActivateFrame(SW_SHOW);
				pMdiChild->BringWindowToTop();
				MDIActivate(pMdiChild);
			}

			// Update the skill content in the existing tab
			CView* activeView = pExistingFrame->GetActiveView();
			auto* webView = DYNAMIC_DOWNCAST(CBlazeClawMFCView, activeView);
			if (webView != nullptr)
			{
				webView->ShowSkillSelection(skillKey, propertiesJson);
			}
		}

		AddChatStatusLine(_T("[Tab] Switched to existing SkillView tab."));
		return false;  // Did NOT create a new tab
	}

	// Get the currently active child before creating new tab
	CMDIChildWndEx* pPrevActive = DYNAMIC_DOWNCAST(CMDIChildWndEx, MDIGetActive());

	// Create the WebView+Chat tab
	CDocument* pDoc = tpl->OpenDocumentFile(nullptr);
	TRACE(_T("[CMainFrame::OpenSkillViewTab] pDoc=%p\n"), pDoc);
	if (!pDoc)
		return false;

	// Get the newly active MDI child frame (should be the one we just created)
	CMDIChildWndEx* activeChild = DYNAMIC_DOWNCAST(CMDIChildWndEx, MDIGetActive());
	if (activeChild != nullptr && activeChild != pPrevActive)
	{
		// Cast to CChildFrame and hide the chat view
		auto* childFrame = DYNAMIC_DOWNCAST(CChildFrame, activeChild);
		if (childFrame != nullptr)
		{
			childFrame->HideChatView();
			childFrame->SetCurrentSkillKey(skillKey);
		}

		// Show skill selection in the active WebView
		auto* activeView = activeChild->GetActiveView();
		auto* webView = DYNAMIC_DOWNCAST(CBlazeClawMFCView, activeView);
		if (webView != nullptr)
		{
			webView->ShowSkillSelection(skillKey, propertiesJson);
		}
	}

	AddChatStatusLine(_T("[Tab] SkillView tab created (Chat hidden)."));
	return true;  // Created a new tab
}

void CMainFrame::OpenWebViewMarkdownTab()
{
	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	auto* tpl = app ? app->GetWebViewMarkdownSharedDocTemplate() : nullptr;
	TRACE(_T("[CMainFrame::OpenWebViewMarkdownTab] app=%p tpl=%p\n"), app, tpl);
	if (!app || !tpl)
		return;

	// CreateSharedTabs creates two MDI tabs (WebView + Markdown) sharing the same document
	CDocument* pDoc = tpl->CreateSharedTabs(this, TRUE);
	TRACE(_T("[CMainFrame::OpenWebViewMarkdownTab] pDoc=%p\n"), pDoc);

	AddChatStatusLine(_T("[Tab] New WebView+Markdown shared tabs created."));
}

void CMainFrame::OnUpdateWindowNewWebViewOnly(CCmdUI* pCmdUI)
{
	pCmdUI->Enable(TRUE);
}

void CMainFrame::OpenAIChatViewTab()
{
	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	auto* tpl = app ? app->GetAIChatViewTemplate() : nullptr;
	TRACE(_T("[CMainFrame::OpenAIChatViewTab] app=%p tpl=%p\n"), app, tpl);
	if (!tpl)
		return;
	CDocument* pDoc = tpl->OpenDocumentFile(nullptr);
	TRACE(_T("[CMainFrame::OpenAIChatViewTab] pDoc=%p\n"), pDoc);
	if (!pDoc)
		return;
	AddChatStatusLine(_T("[Tab] New AI Chat View tab created."));
}

void CMainFrame::OnEditChat()
{
	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	auto* tpl = app ? app->GetAIChatViewTemplate() : nullptr;
	TRACE(_T("[CMainFrame::OpenAIChatViewTab] app=%p tpl=%p\n"), app, tpl);
	if (!tpl)
		return;
	CDocument* pDoc = tpl->OpenDocumentFile(nullptr);
	TRACE(_T("[CMainFrame::OpenAIChatViewTab] pDoc=%p\n"), pDoc);
	if (!pDoc)
		return;
	AddChatStatusLine(_T("[Tab] New AI Chat View tab created."));
}

LRESULT CMainFrame::OnSyncDashboardPanePosition(WPARAM wParam, LPARAM lParam)
{
	if (m_isSyncingDashboardPanePosition) {
		// Prevent recursive updates
		return 0;
	}

	const HWND sourceHwnd = reinterpret_cast<HWND>(wParam);
	const int x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
	const int y = static_cast<int>(static_cast<short>(HIWORD(lParam)));

	SyncDashboardPanePosition(sourceHwnd, x, y);

	// This handler is called by dashboard panes when they are shown/hidden or when their visibility changes.
	// We can use this to trigger a layout recalculation to ensure the panes are positioned correctly.
	
	// Avoid forced RecalcLayout here to prevent feedback loops.
	//RecalcLayout(FALSE);

	return 0;
}

void CMainFrame::SyncDashboardPanePosition(HWND sourceHwnd, int x, int y)
{
	//CRect rc;

	if (m_isSyncingDashboardPanePosition) {
		// Prevent recursive updates
		return;
	}

	m_isSyncingDashboardPanePosition = true;

	//if ( m_wndDashboard.GetSafeHwnd() != sourceHwnd) {
	//	m_wndDashboard.GetWindowRect(&rc);

	//	m_wndDashboard.SetWindowPos(nullptr, x, y, rc.Width(), rc.Height(), SWP_NOZORDER);
	//	m_wndDashboard.Invalidate();
	//	m_wndDashboard.UpdateWindow();
	//}

	//if (m_wndDashboard_cron.GetSafeHwnd() != sourceHwnd) {
	//	m_wndDashboard_cron.GetWindowRect(&rc);

	//	m_wndDashboard_cron.SetWindowPos(nullptr, x, y, rc.Width(), rc.Height(), SWP_NOZORDER);
	//	m_wndDashboard_cron.Invalidate();
	//	m_wndDashboard_cron.UpdateWindow();
	//}

	const UINT kFlags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
	auto applyPosition = [&](CDashboardWnd& pane)
		{
			const HWND targetHwnd = pane.GetSafeHwnd();
			if (targetHwnd == nullptr || targetHwnd == sourceHwnd || !::IsWindow(targetHwnd))
			{
				return;
			}

			CRect rc{};
			pane.GetWindowRect(&rc);
			if (rc.left == x && rc.top == y)
			{
				// No-op guard: do not trigger redundant move notifications.
				return;
			}

			pane.SetWindowPos(nullptr, x, y, 0, 0, kFlags);
		};

	applyPosition(m_wndDashboard);
	applyPosition(m_wndDashboard_cron);

	m_isSyncingDashboardPanePosition = false;
}

LRESULT CMainFrame::OnSyncDashboardPaneSize(WPARAM wParam, LPARAM lParam)
{
	if (m_isSyncingDashboardPaneSize) {
		// Prevent recursive updates
		return 0;
	}

	const HWND sourceHwnd = reinterpret_cast<HWND>(wParam);
	const int cx = static_cast<int>(static_cast<short>(LOWORD(lParam)));
	const int cy = static_cast<int>(static_cast<short>(HIWORD(lParam)));

	SyncDashboardPaneSize(sourceHwnd, cx, cy);

	// This handler is called by dashboard panes when they are resized.
	// We can use this to trigger a layout recalculation to ensure the panes are sized correctly.
	
	// Do not force RecalcLayout here; it can trigger repeated size churn.
	//RecalcLayout(TRUE);

	//m_isSyncingDashboardPaneSize = false;

	return 0;
}

void CMainFrame::SyncDashboardPaneSize(HWND sourceHwnd, int cx, int cy)
{
	//CRect rc;

	if (m_isSyncingDashboardPaneSize) {
		// Prevent recursive updates
		return;
	}

	if (cx <= 0 || cy <= 0) {
		// Invalid size, ignore
		return;
	}

	m_isSyncingDashboardPaneSize = true;

	//if ( m_wndDashboard.GetSafeHwnd() != sourceHwnd) {
	//	m_wndDashboard.GetWindowRect(&rc);

	//	m_wndDashboard.SetWindowPos(nullptr, rc.left, rc.top, cx, cy, SWP_NOZORDER);
	//	m_wndDashboard.Invalidate();
	//	m_wndDashboard.UpdateWindow();
	//}

	//if (m_wndDashboard_cron.GetSafeHwnd() != sourceHwnd) {
	//	m_wndDashboard_cron.GetWindowRect(&rc);

	//	m_wndDashboard_cron.SetWindowPos(nullptr, rc.left, rc.top, cx, cy, SWP_NOZORDER);
	//	m_wndDashboard_cron.Invalidate();
	//	m_wndDashboard_cron.UpdateWindow();
	//}

	const UINT kFlags = SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE;
	auto applySize = [&](CDashboardWnd& pane)
		{
			const HWND targetHwnd = pane.GetSafeHwnd();
			if (targetHwnd == nullptr || targetHwnd == sourceHwnd || !::IsWindow(targetHwnd))
			{
				return;
			}

			CRect rc{};
			pane.GetWindowRect(&rc);
			if (rc.Width() == cx && rc.Height() == cy)
			{
				// Critical: skip no-op resize to prevent feedback loops.
				return;
			}

			pane.SetWindowPos(nullptr, 0, 0, cx, cy, kFlags);
		};

	applySize(m_wndDashboard);
	applySize(m_wndDashboard_cron);

	m_isSyncingDashboardPaneSize = false;
}

LRESULT CMainFrame::OnSyncDashboardAfterFloat(WPARAM wParam, LPARAM)
{
	//if (m_isSyncingDashboardFloatDock)
	if (IsDashboardFloatDockSyncInProgress())
	{
		return 0;
	}

	const HWND sourceHwnd = reinterpret_cast<HWND>(wParam);

	m_isDashboardFloat = true;
	//SyncDashboardAfterFloat(sourceHwnd);

	const auto panes = CollectDashboardPanes();

	CDashboardWnd* pSourcePane = nullptr;

	if (sourceHwnd != nullptr)
	{
		CWnd* pWnd	= CWnd::FromHandlePermanent(sourceHwnd);
		pSourcePane	= dynamic_cast<CDashboardWnd*>(pWnd);
	}
	else
	{
		for (CDashboardWnd* pane : panes)
		{
			if (pane == nullptr)
			{
				continue;
			}

			const HWND paneHwnd = pane->GetSafeHwnd();
			if (paneHwnd == nullptr || !::IsWindow(paneHwnd))
			{
				continue;
			}

			if (::IsWindowVisible(paneHwnd) != FALSE)
			{
				pSourcePane = pane;
				break;
			}
		}
	}

	if (pSourcePane == nullptr || !::IsWindow(pSourcePane->GetSafeHwnd()))
	{
		return 0;
	}

	ScopedDashboardFloatDockSyncGuard guard(m_dashboardFloatDockSyncCount);

	CMultiPaneFrameWnd* pFrame =
		dynamic_cast<CMultiPaneFrameWnd*>(pSourcePane->GetParentMiniFrame());

	if (pFrame == nullptr)	return	0;

	for (CDashboardWnd* pane : panes)
	{
		if (pane == nullptr || pane == pSourcePane)
		{
			continue;
		}

		if (!::IsWindow(pane->GetSafeHwnd()))
		{
			continue;
		}

		// Already in the same floating frame
		if (pane->GetParentMiniFrame() == pFrame)
		{
			continue;
		}

		CPaneDivider*	pDivider	= pane->GetDefaultPaneDivider();
		CPaneContainer*	pContainer	= nullptr;

		if (pDivider != nullptr)
		{
			BOOL bIsLeft;
			pContainer = pDivider->FindPaneContainer(pane, bIsLeft);

			//pDivider->RemovePane(pane);

			if (pContainer != nullptr)
			{
				pContainer->RemovePane(pane);
			}
		}

		// Important: detach from current docking container/context first.
		pane->UndockPane();

		// Now add to the source floating frame.
		pFrame->AddPane(pane);
		pane->ShowPane(TRUE, FALSE, TRUE);
	}

	pFrame->OnPaneRecalcLayout();
	pFrame->AdjustLayout();
	pFrame->RedrawWindow(
		nullptr,
		nullptr,
		RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

	return 0;
}

LRESULT CMainFrame::OnSyncDashboardAfterDock(WPARAM wParam, LPARAM)
{
	//if (m_isSyncingDashboardFloatDock)
	if (IsDashboardFloatDockSyncInProgress())
	{
		return 0;
	}

	const HWND sourceHwnd = reinterpret_cast<HWND>(wParam);

	m_isDashboardFloat = false;
	SyncDashboardAfterDock(sourceHwnd);

	return 0;
}

//void CMainFrame::SyncDashboardAfterFloat(HWND sourceHwnd)
//{
//	// This handler is called by dashboard panes after they are floated.
//	// We can use this to trigger any necessary adjustments after a pane has been floated.
//	// For example, we might want to ensure that the floated pane is fully visible on the screen,
//	// or we might want to adjust the layout of the remaining docked panes.
//	// In this example, we'll just trigger a layout recalculation to ensure everything is positioned correctly.
//	if (m_wndDashboard.GetSafeHwnd() != sourceHwnd) {
//		m_wndDashboard.FloatPane(CRect(100, 100, 500, 500));
//	}
//
//	if (m_wndDashboard_cron.GetSafeHwnd() != sourceHwnd) {
//		m_wndDashboard_cron.FloatPane(CRect(150, 150, 550, 550));
//	}
//
//	RecalcLayout(FALSE);
//}
//
//void CMainFrame::SyncDashboardAfterDock(HWND sourceHwnd)
//{
//	// This handler is called by dashboard panes after they are docked.
//	// Similar to the float handler, we can use this to trigger adjustments after a pane has been docked.
//	// For example, we might want to ensure that the newly docked pane is integrated smoothly into the existing layout,
//	// or we might want to adjust the sizes of adjacent panes to accommodate the new docked pane.
//	// In this example, we'll just trigger a layout recalculation to ensure everything is positioned correctly.
//	if (m_wndDashboard.GetSafeHwnd() != sourceHwnd) {
//		DockPane(&m_wndDashboard);
//	}
//
//	if (m_wndDashboard_cron.GetSafeHwnd() != sourceHwnd) {
//		DockPane(&m_wndDashboard_cron);
//	}
//
//	RecalcLayout(FALSE);
//}

bool CMainFrame::IsDashboardFloatDockSyncInProgress() const
{
//	return m_isSyncingDashboardFloatDock;

	return m_dashboardFloatDockSyncCount.load(std::memory_order_acquire) > 0;
}

bool CMainFrame::IsDashboardPaneSyncReady() const
{
	return m_dashboardPaneSyncReady.load(std::memory_order_acquire);
}

void CMainFrame::SyncDashboardAfterFloat(HWND sourceHwnd)
{
	SyncAllDashboardsFloatState(sourceHwnd, true);
}

void CMainFrame::SyncDashboardAfterDock(HWND sourceHwnd)
{
	SyncAllDashboardsFloatState(sourceHwnd, false);
}

void CMainFrame::OnEditDashboard()
{
	m_isDashboardFloat	= !m_isDashboardFloat;
	m_isSwitchFloatDock	= true;

	if (m_isDashboardFloat) {
		SyncDashboardAfterFloat(nullptr);
	}
	else {
		SyncDashboardAfterDock(nullptr);
	}

	m_isSwitchFloatDock = false;
}

void CMainFrame::ActivateDashboardPane(CDashboardWnd& targetPane)
{
	CDashboardWnd* effectiveTarget = &targetPane;
	HWND targetHwnd = effectiveTarget->GetSafeHwnd();
	if (targetHwnd == nullptr || !::IsWindow(targetHwnd))
	{
		for (CDashboardWnd* pane : CollectDashboardPanes())
		{
			if (pane == nullptr)
			{
				continue;
			}

			const HWND candidateHwnd = pane->GetSafeHwnd();
			if (candidateHwnd == nullptr || !::IsWindow(candidateHwnd))
			{
				continue;
			}

			effectiveTarget = pane;
			targetHwnd = candidateHwnd;
			break;
		}

		if (targetHwnd == nullptr || !::IsWindow(targetHwnd))
		{
			TRACE0("Dashboard pane is not properly initialized.\n");
			return;
		}

		TRACE0("Dashboard pane fallback activated due to invalid target pane.\n");
	}

	CDashboardWnd& target = *effectiveTarget;

	CDashboardWnd* sourcePane = nullptr;
	CRect sourceRect{};
	bool sourceIsFloating = false;

	const auto panes = CollectDashboardPanes();
	for (CDashboardWnd* pane : panes)
	{
		if (pane == nullptr || pane == &target)
		{
			continue;
		}

		const HWND paneHwnd = pane->GetSafeHwnd();
		if (paneHwnd == nullptr || !::IsWindow(paneHwnd))
		{
			continue;
		}

		if (::IsWindowVisible(paneHwnd) != FALSE)
		{
			if (sourcePane == nullptr)
			{
				sourcePane = pane;
				sourceIsFloating = pane->IsFloating();
				pane->GetWindowRect(&sourceRect);
			}

			pane->ShowPane(FALSE, FALSE, TRUE);
			pane->OnPaneVisibilityChanged(FALSE);
		}
	}

	if (sourcePane != nullptr && !sourceRect.IsRectEmpty())
	{
		if (sourceIsFloating)
		{
			if (target.IsFloating())
			{
				target.SetWindowPos(
					nullptr,
					sourceRect.left,
					sourceRect.top,
					sourceRect.Width(),
					sourceRect.Height(),
					SWP_NOZORDER | SWP_NOACTIVATE);
			}
			else
			{
				target.FloatToRect(sourceRect);
			}
		}
		else
		{
			if (target.IsFloating())
			{
				DockPane(&target);
			}

			target.SetWindowPos(
				nullptr,
				sourceRect.left,
				sourceRect.top,
				sourceRect.Width(),
				sourceRect.Height(),
				SWP_NOZORDER | SWP_NOACTIVATE);
		}
	}

	target.ShowPane(TRUE, FALSE, TRUE);
	target.SetFocus();
	target.OnPaneVisibilityChanged(TRUE);
	RecalcLayout(FALSE);
}

void CMainFrame::OnUpdateEditDashboard(CCmdUI* pCmdUI)
{
	if (m_isDashboardFloat)
		pCmdUI->SetText(_T("Dock"));
	else
		pCmdUI->SetText(_T("Float"));
}

void CMainFrame::RestoreLastDashboardPane()
{
	CWinApp* app = AfxGetApp();
	const UINT commandId =
		app != nullptr
		? static_cast<UINT>(app->GetProfileInt(
			kDashboardProfileSection,
			kDashboardLastPaneIdKey,
			ID_VIEW_DASHBOARDWND))
		: ID_VIEW_DASHBOARDWND;

	CDashboardWnd* pane = GetDashboardPaneByCommandId(commandId);
	if (pane == nullptr)
	{
		pane = &m_wndDashboard;
	}

	ActivateDashboardPane(*pane);
}

void CMainFrame::RememberLastDashboardPane(const CDashboardWnd& pane)
{
	CWinApp* app = AfxGetApp();
	if (app == nullptr)
	{
		return;
	}

	app->WriteProfileInt(
		kDashboardProfileSection,
		kDashboardLastPaneIdKey,
		static_cast<int>(GetDashboardPaneCommandId(pane)));
}

CDashboardWnd* CMainFrame::GetDashboardPaneByCommandId(UINT commandId)
{
	switch (commandId)
	{
	case ID_VIEW_DASHBOARDWND:
		return &m_wndDashboard;
	case ID_VIEW_DASHBOARD_OVERVIEW_WND:
		return &m_wndDashboard_overview;
	case ID_VIEW_DASHBOARD_TOOLS_WND:
		return &m_wndDashboard_tools;
	case ID_VIEW_DASHBOARD_FILES_WND:
		return &m_wndDashboard_files;
	case ID_VIEW_DASHBOARD_SKILLS_WND:
		return &m_wndDashboard_skills;
	case ID_VIEW_DASHBOARD_CHANNELS_WND:
		return &m_wndDashboard_channels;
	case ID_VIEW_DASHBOARD_CRON_WND:
		return &m_wndDashboard_cron;
	case ID_VIEW_DASHBOARD_DREAMING_WND:
		return &m_wndDashboard_dreaming;
	case ID_VIEW_DASHBOARD_NODES_WND:
		return &m_wndDashboard_nodes;
	case ID_VIEW_DASHBOARD_INSTANCES_WND:
		return &m_wndDashboard_instances;
	case ID_VIEW_DASHBOARD_USAGE_WND:
		return &m_wndDashboard_usage;
	case ID_VIEW_DASHBOARD_DEVICES_WND:
		return &m_wndDashboard_devices;
	default:
		return nullptr;
	}
}

UINT CMainFrame::GetDashboardPaneCommandId(const CDashboardWnd& pane) const
{
	if (&pane == &m_wndDashboard)
	{
		return ID_VIEW_DASHBOARDWND;
	}
	if (&pane == &m_wndDashboard_overview)
	{
		return ID_VIEW_DASHBOARD_OVERVIEW_WND;
	}
	if (&pane == &m_wndDashboard_tools)
	{
		return ID_VIEW_DASHBOARD_TOOLS_WND;
	}
	if (&pane == &m_wndDashboard_files)
	{
		return ID_VIEW_DASHBOARD_FILES_WND;
	}
	if (&pane == &m_wndDashboard_skills)
	{
		return ID_VIEW_DASHBOARD_SKILLS_WND;
	}
	if (&pane == &m_wndDashboard_channels)
	{
		return ID_VIEW_DASHBOARD_CHANNELS_WND;
	}
	if (&pane == &m_wndDashboard_cron)
	{
		return ID_VIEW_DASHBOARD_CRON_WND;
	}
	if (&pane == &m_wndDashboard_dreaming)
	{
		return ID_VIEW_DASHBOARD_DREAMING_WND;
	}
	if (&pane == &m_wndDashboard_nodes)
	{
		return ID_VIEW_DASHBOARD_NODES_WND;
	}
	if (&pane == &m_wndDashboard_instances)
	{
		return ID_VIEW_DASHBOARD_INSTANCES_WND;
	}
	if (&pane == &m_wndDashboard_usage)
	{
		return ID_VIEW_DASHBOARD_USAGE_WND;
	}
	if (&pane == &m_wndDashboard_devices)
	{
		return ID_VIEW_DASHBOARD_DEVICES_WND;
	}

	return ID_VIEW_DASHBOARDWND;
}

std::vector<CDashboardWnd*> CMainFrame::CollectDashboardPanes()
{
	std::vector<CDashboardWnd*> panes;
	panes.reserve(12);

	panes.push_back(&m_wndDashboard);
	panes.push_back(&m_wndDashboard_overview);
	panes.push_back(&m_wndDashboard_tools);
	panes.push_back(&m_wndDashboard_files);
	panes.push_back(&m_wndDashboard_skills);
	panes.push_back(&m_wndDashboard_channels);
	panes.push_back(&m_wndDashboard_cron);
	panes.push_back(&m_wndDashboard_dreaming);
	panes.push_back(&m_wndDashboard_nodes);
	panes.push_back(&m_wndDashboard_instances);
	panes.push_back(&m_wndDashboard_usage);
	panes.push_back(&m_wndDashboard_devices);

	return panes;
}

void CMainFrame::SyncAllDashboardsFloatState(HWND sourceHwnd, bool shouldFloat)
{
	//if (m_isSyncingDashboardFloatDock)
	// If another sync is in progress, bail out
	if (IsDashboardFloatDockSyncInProgress())
	{
		return;
	}

	const auto panes = CollectDashboardPanes();

	auto isValidPaneWindow = [](const CDashboardWnd* pane) -> bool
	{
		if (pane == nullptr)
		{
			return false;
		}

		const HWND paneHwnd = pane->GetSafeHwnd();
		return paneHwnd != nullptr && ::IsWindow(paneHwnd);
	};

	CDashboardWnd* pSourcePane = nullptr;
	if (sourceHwnd == nullptr)
	{	// If no source provided, determine the source based on which pane is currently active/visible
		for (CDashboardWnd* pane : panes)
		{
			if (!isValidPaneWindow(pane))
			{
				continue;
			}

			if (::IsWindowVisible(pane->GetSafeHwnd()) != FALSE)
			{
				pSourcePane = pane;
				break;
			}
		}
	}
	else {
		pSourcePane = CWnd::FromHandlePermanent(sourceHwnd) != nullptr ? dynamic_cast<CDashboardWnd*>(CWnd::FromHandlePermanent(sourceHwnd)) : nullptr;
		if (!isValidPaneWindow(pSourcePane))
		{
			pSourcePane = nullptr;
		}
	}

	//m_isSyncingDashboardFloatDock = true;
	// Scoped guard increments counter and will decrement on leave (including exceptions)
	ScopedDashboardFloatDockSyncGuard guard(m_dashboardFloatDockSyncCount);

	CMultiPaneFrameWnd* pFrame	= nullptr;
	CRect sourceFloatRect(100, 100, 500, 500);
	//if (sourceHwnd != nullptr && ::IsWindow(sourceHwnd))
	//{
	//	::GetWindowRect(sourceHwnd, &sourceFloatRect);
	//}
	if (pSourcePane != nullptr && ::IsWindow(pSourcePane->GetSafeHwnd()))
	{
		pSourcePane->GetWindowRect(&sourceFloatRect);
		pFrame = dynamic_cast<CMultiPaneFrameWnd*>(pSourcePane->GetParentMiniFrame());
	}

	//CDashboardWnd* pSourcePane = CWnd::FromHandlePermanent(sourceHwnd) != nullptr ? dynamic_cast<CDashboardWnd*>(CWnd::FromHandlePermanent(sourceHwnd)) : nullptr;

	for (CDashboardWnd* pane : panes)
	{
		if (!isValidPaneWindow(pane))
		{
			continue;
		}

		//const HWND targetHwnd = pane->GetSafeHwnd();
		//if (targetHwnd == nullptr || !::IsWindow(targetHwnd))
		//{
		//	continue;
		//}

		if (pane == pSourcePane)
		{
			continue;
		}

		if (shouldFloat)
		{
			if (!pane->IsFloating())
			{
				if (pFrame != nullptr)
				{
					pFrame->AddPane(pane);
				}
				else
				{
					//pane->FloatPane(sourceFloatRect);
					pane->FloatToRect(sourceFloatRect);
				}
			}
		}
		else
		{
			if (pane->IsFloating())
			{
				DockPane(pane);
			}
		}
	}

	RecalcLayout(FALSE);
	//m_isSyncingDashboardFloatDock = false;
}

LRESULT CMainFrame::OnHideAllDashboards(WPARAM, LPARAM)
{
	m_wndDashboard.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_overview.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_tools.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_files.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_skills.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_channels.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_cron.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_dreaming.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_nodes.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_instances.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_usage.ShowPane(FALSE, FALSE, FALSE);
	m_wndDashboard_devices.ShowPane(FALSE, FALSE, FALSE);
	return 0;
}
