#include "pch.h"
#include "MainFrame.h"
#include "framework.h"

#include "BlazeClawMfcApp.h"
#include <atlconv.h>
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

} // namespace
// ApiKey dialog declared in its own files


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
	ON_COMMAND(ID_EXTENSION_DEEPSEEK, &CMainFrame::OnExtensionDeepseek)
	ON_UPDATE_COMMAND_UI(ID_EXTENSION_DEEPSEEK, &CMainFrame::OnUpdateExtensionDeepseek)
	ON_COMMAND(ID_EXTENSION_MODELSET, &CMainFrame::OnExtensionModelSet)
	ON_COMMAND(ID_WINDOW_NEW_WEBVIEW, &CMainFrame::OnWindowNew)
	ON_UPDATE_COMMAND_UI(ID_WINDOW_NEW_WEBVIEW, &CMainFrame::OnUpdateWindowNewWebViewOnly)
	ON_COMMAND(ID_WINDOW_NEW_WEBVIEW_CHAT, &CMainFrame::OnWindowNewWebViewChat)
	ON_COMMAND(ID_WINDOW_NEW_WEBVIEW_MARKDOWN, &CMainFrame::OnWindowNewWebViewMarkdown)
	ON_WM_SETTINGCHANGE()
	ON_MESSAGE(kMsgCreateMdiGroup, &CMainFrame::OnCreateMdiGroup)

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
END_MESSAGE_MAP()

CMainFrame::CMainFrame() noexcept
{
	theApp.m_nAppLook = theApp.GetInt(_T("ApplicationLook"), ID_VIEW_APPLOOK_VS_2008);

	//Create(nullptr, _T("BlazeClaw - OpenClaw C++ Port"), WS_OVERLAPPEDWINDOW, CRect(100, 100, 1280, 800));
}

CMainFrame::~CMainFrame()
{}

LRESULT CMainFrame::OnCreateMdiGroup(WPARAM, LPARAM)
{
	CreateTwoTabbedGroups();
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

	// set the visual manager and style based on persisted value
	OnApplicationLook(theApp.m_nAppLook);

	// Enable enhanced windows management dialog
	EnableWindowsDialog(ID_WINDOW_MANAGER, ID_WINDOW_MANAGER, TRUE);

	// Switch the order of document name and application name on the window title bar. This
	// improves the usability of the taskbar because the document name is visible with the thumbnail.
	ModifyStyle(0, FWS_PREFIXTITLE);

	m_menuBar.CreateMenu();
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

	SetWindowText(_T("BlazeClaw - Service Console"));
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
		// Save enabled models configuration
		const auto& models = dlg.GetEnabledModels();
		std::vector<std::string> enabledIds;
		for (const auto& m : models) {
			if (m.enabled) {
				enabledIds.push_back(m.id);
			}
		}
		// TODO: Save enabled model IDs to config
		(void)enabledIds;
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

void CMainFrame::OpenWebViewPlusChatTab()
{
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
