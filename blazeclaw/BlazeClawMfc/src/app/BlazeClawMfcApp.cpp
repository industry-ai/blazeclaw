#include "pch.h"
#include "framework.h"
#include "afxwinappex.h"
#include "afxdialogex.h"
#include "BlazeClawMFCApp.h"

#include "MainFrame.h"

#include "ChildFrm.h"
#include "BlazeClawMFCDoc.h"
#include "BlazeClawMFCView.h"
#include "ChatView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {
	constexpr wchar_t kConfigPath[] = L"blazeclaw.conf";

	CRuntimeClass* ResolveChatRuntimeViewClass(
		const blazeclaw::config::AppConfig& config) {
		if (config.chat.mode == L"native") {
			return RUNTIME_CLASS(CChatView);
		}

		return RUNTIME_CLASS(CBlazeClawMFCView);
	}
}


// CBlazeClawMFCApp

BEGIN_MESSAGE_MAP(CBlazeClawMFCApp, CWinAppEx)
	ON_COMMAND(ID_APP_ABOUT, &CBlazeClawMFCApp::OnAppAbout)
	// Standard file based document commands
	ON_COMMAND(ID_FILE_NEW, &CWinAppEx::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, &CWinAppEx::OnFileOpen)
	// Standard print setup command
	ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinAppEx::OnFilePrintSetup)
END_MESSAGE_MAP()

// CBlazeClawMFCApp construction

CBlazeClawMFCApp::CBlazeClawMFCApp() noexcept
{
	m_bHiColorIcons = TRUE;


	m_nAppLook = 0;
	// support Restart Manager
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_ALL_ASPECTS;
#ifdef _MANAGED
	// If the application is built using Common Language Runtime support (/clr):
	//     1) This additional setting is needed for Restart Manager support to work properly.
	//     2) In your project, you must add a reference to System.Windows.Forms in order to build.
	System::Windows::Forms::Application::SetUnhandledExceptionMode(System::Windows::Forms::UnhandledExceptionMode::ThrowException);
#endif

	// TODO: replace application ID string below with unique ID string; recommended
	// format for string is CompanyName.ProductName.SubProduct.VersionInformation
	SetAppID(_T("BlazeClawMFC.AppID.NoVersion"));

	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

// The one and only CBlazeClawMFCApp object
CBlazeClawMFCApp theApp;


// CBlazeClawMFCApp initialization
BOOL CBlazeClawMFCApp::InitInstance() {
	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinAppEx::InitInstance();

	if (!AfxSocketInit())
	{
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
		return FALSE;
	}

	// Initialize OLE libraries
	if (!AfxOleInit())
	{
		AfxMessageBox(IDP_OLE_INIT_FAILED);
		return FALSE;
	}

	AfxEnableControlContainer();

	EnableTaskbarInteraction();

	// AfxInitRichEdit2() is required to use RichEdit control
	// AfxInitRichEdit2();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	// of your final executable, you should remove from the following
	// the specific initialization routines you do not need
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(_T("BlazeClaw @ Wuhan, China"));
	LoadStdProfileSettings(16);  // Load standard INI file options (including MRU)


	InitContextMenuManager();
	InitShellManager();

	InitKeyboardManager();

	InitTooltipManager();
	CMFCToolTipInfo ttParams;
	ttParams.m_bVislManagerTheme = TRUE;
	theApp.GetTooltipManager()->SetTooltipParams(AFX_TOOLTIP_TYPE_ALL,
		RUNTIME_CLASS(CMFCToolTipCtrl), &ttParams);

	m_configLoader.LoadFromFile(kConfigPath, m_config);
	m_serviceManager.Start(m_config);

	// Register the application's document templates.  Document templates
	//  serve as the connection between documents, frame windows and views
	CMultiDocTemplate* pDocTemplate;
  CRuntimeClass* viewRuntimeClass = ResolveChatRuntimeViewClass(m_config);
	pDocTemplate = new CMultiDocTemplate(IDR_BlazeClawMFCTYPE,
		RUNTIME_CLASS(CBlazeClawMFCDoc),
		RUNTIME_CLASS(CChildFrame), // custom MDI child frame
      viewRuntimeClass);
	if (!pDocTemplate)
		return FALSE;
	AddDocTemplate(pDocTemplate);

	//auto* frame = new CMainFrame();
	//m_pMainWnd = frame;

	// create main MDI Frame window
	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame || !pMainFrame->LoadFrame(IDR_MAINFRAME))
	{
		delete pMainFrame;
		return FALSE;
	}
	m_pMainWnd = pMainFrame;

	// call DragAcceptFiles only if there's a suffix
	//  In an MDI app, this should occur immediately after setting m_pMainWnd
	// Enable drag/drop open
	m_pMainWnd->DragAcceptFiles();

	// Parse command line for standard shell commands, DDE, file open
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);

	// Enable DDE Execute open
	EnableShellOpen();
	RegisterShellFileTypes(TRUE);

	//frame->ShowWindow(SW_SHOW);
	//frame->UpdateWindow();

	// Dispatch commands specified on the command line.  Will return FALSE if
	// app was launched with /RegServer, /Register, /Unregserver or /Unregister.
	if (!ProcessShellCommand(cmdInfo))
		return FALSE;
	// The main window has been initialized, so show and update it
	pMainFrame->ShowWindow(SW_SHOWMAXIMIZED);
	pMainFrame->UpdateWindow();

	return TRUE;
}

int CBlazeClawMFCApp::ExitInstance() {
	m_serviceManager.Stop();

	AfxOleTerm(FALSE);

	return CWinApp::ExitInstance();
}

blazeclaw::core::ServiceManager& CBlazeClawMFCApp::Services() noexcept {
	return m_serviceManager;
}

const blazeclaw::core::ServiceManager& CBlazeClawMFCApp::Services() const noexcept {
	return m_serviceManager;
}

// CBlazeClawMFCApp message handlers


// CAboutDlg dialog used for App About

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg() noexcept;

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() noexcept : CDialogEx(IDD_ABOUTBOX)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

// App command to run the dialog
void CBlazeClawMFCApp::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}

// CBlazeClawMFCApp customization load/save methods

void CBlazeClawMFCApp::PreLoadState()
{
	BOOL bNameValid;
	CString strName;
	bNameValid = strName.LoadString(IDS_EDIT_MENU);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EDIT);
	bNameValid = strName.LoadString(IDS_EXPLORER);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EXPLORER);
}

void CBlazeClawMFCApp::LoadCustomState()
{
}

void CBlazeClawMFCApp::SaveCustomState()
{
}
