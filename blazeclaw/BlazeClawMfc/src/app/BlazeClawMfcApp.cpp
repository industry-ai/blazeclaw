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

#include "../core/runtime/LocalModel/TokenizerBridge.h"

#include <filesystem>
#include <Windows.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {
	constexpr wchar_t kConfigPath[] = L"blazeclaw.conf";

	std::wstring ToWide(const std::string& value) {
		std::wstring output;
		output.reserve(value.size());

		for (const char ch : value) {
			output.push_back(static_cast<wchar_t>(
				static_cast<unsigned char>(ch)));
		}

		return output;
	}

	void AppendMainFrameStatusLine(const CString& line) {
		auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
		if (mainFrame == nullptr) {
			return;
		}

		mainFrame->AddChatStatusLine(line);
	}

	void AppendStartupConfigStatus(const blazeclaw::config::AppConfig& config) {
		const auto absoluteConfigPath =
			std::filesystem::absolute(std::filesystem::path(kConfigPath));

		wchar_t modulePath[MAX_PATH]{};
		const DWORD moduleLength = GetModuleFileNameW(
			nullptr,
			modulePath,
			MAX_PATH);
		const std::wstring executablePath = moduleLength > 0
			? std::wstring(modulePath, modulePath + moduleLength)
			: std::wstring(L"unknown");

		const std::wstring workingDirectory =
			std::filesystem::current_path().wstring();

		CString runtimeContextLine;
		runtimeContextLine.Format(
			L"[Chat] startup.runtime.context - exe=%s cwd=%s config=%s",
			executablePath.c_str(),
			workingDirectory.c_str(),
			absoluteConfigPath.c_str());
		AppendMainFrameStatusLine(runtimeContextLine);

		CString configPathLine;
		configPathLine.Format(
			L"[Chat] startup.config.path - %s",
			absoluteConfigPath.c_str());
		AppendMainFrameStatusLine(configPathLine);

		CString modeLine;
		modeLine.Format(
			L"[Chat] startup.config.mode - %s",
			config.chat.mode.c_str());
		AppendMainFrameStatusLine(modeLine);
	}

	void AppendStartupEmbeddingsStatus(
		const blazeclaw::config::AppConfig& config,
		const blazeclaw::core::ServiceManager& services) {
		if (!config.embeddings.enabled) {
			AppendMainFrameStatusLine(
				L"[Embeddings] startup.disabled - embeddings.enabled=false");
			return;
		}

		CString configLine;
		configLine.Format(
			L"[Embeddings] startup.config - provider=%s model=%s tokenizer=%s",
			config.embeddings.provider.c_str(),
			config.embeddings.modelPath.c_str(),
			config.embeddings.tokenizerPath.c_str());
		AppendMainFrameStatusLine(configLine);

		const std::string probeResult = services.InvokeGatewayMethod(
			"gateway.embeddings.generate",
			std::optional<std::string>(
				"{\"text\":\"startup-embedding-probe\"}"));

		if (probeResult.find("\"vector\":") != std::string::npos) {
			AppendMainFrameStatusLine(
				L"[Embeddings] startup.loaded - model probe succeeded");
			return;
		}

		const CString errorLine(
			(L"[Embeddings] startup.error - " + ToWide(probeResult)).c_str());
		AppendMainFrameStatusLine(errorLine);
	}

	void AppendStartupLocalModelStatus(
       const blazeclaw::config::AppConfig& config,
		const blazeclaw::core::ServiceManager& services) {
		if (!config.localModel.enabled) {
			AppendMainFrameStatusLine(
				L"[Chat] startup.localModel.disabled - chat.localModel.enabled=false");
			return;
		}

		const auto runtime = services.LocalModelRuntime();

		CString localModelLine;
		localModelLine.Format(
          L"[Chat] startup.localModel.config - provider=%s stage=%s model=%s tokenizer=%s maxTokens=%u temperature=%.2f",
         ToWide(runtime.provider).c_str(),
          ToWide(runtime.rolloutStage).c_str(),
			ToWide(runtime.modelPath).c_str(),
			ToWide(runtime.tokenizerPath).c_str(),
			runtime.maxTokens,
			runtime.temperature);
		AppendMainFrameStatusLine(localModelLine);

		CString gatingLine;
		gatingLine.Format(
			L"[Chat] startup.localModel.gating - rolloutEligible=%s activationEnabled=%s reason=%s",
			services.LocalModelRolloutEligible() ? L"true" : L"false",
			services.LocalModelActivationEnabled() ? L"true" : L"false",
			ToWide(services.LocalModelActivationReason()).c_str());
		AppendMainFrameStatusLine(gatingLine);

		CString integrityLine;
		integrityLine.Format(
			L"[Chat] startup.localModel.integrity - runtimeDll=%s modelHashVerified=%s tokenizerHashVerified=%s",
			runtime.runtimeDllPresent ? L"true" : L"false",
			runtime.modelHashVerified ? L"true" : L"false",
			runtime.tokenizerHashVerified ? L"true" : L"false");
		AppendMainFrameStatusLine(integrityLine);

		if (!runtime.tokenizerPath.empty()) {
			blazeclaw::core::localmodel::TokenizerBridge tokenizer;
			std::string tokenizerLoadError;
			const std::filesystem::path tokenizerPath(
				ToWide(runtime.tokenizerPath));
			if (!tokenizer.Load(tokenizerPath, tokenizerLoadError)) {
				const CString tokenizerErrorLine(
					(L"[Chat] startup.localModel.tokenizer.roundtrip - load_failed: " +
						ToWide(tokenizerLoadError)).c_str());
				AppendMainFrameStatusLine(tokenizerErrorLine);
			}
			else {
				blazeclaw::core::localmodel::TextGenerationError tokenizationError;
				const std::string probeText =
					"roundtrip probe: hello tokenizer 123";
				const auto ids = tokenizer.EncodeToIds(
					probeText,
					96,
					tokenizationError,
					true);

				if (ids.empty()) {
					const std::wstring reason = tokenizationError.message.empty()
						? L"unknown"
						: ToWide(tokenizationError.message);
					const CString tokenizerErrorLine(
						(L"[Chat] startup.localModel.tokenizer.roundtrip - encode_failed: " +
							reason).c_str());
					AppendMainFrameStatusLine(tokenizerErrorLine);
				}
				else {
					const std::string decoded = tokenizer.DecodeFromIds(ids);
					const bool matched = decoded == probeText;

					CString tokenizerLine;
					tokenizerLine.Format(
						L"[Chat] startup.localModel.tokenizer.roundtrip - ok=%s ids=%zu",
						matched ? L"true" : L"false",
						ids.size());
					AppendMainFrameStatusLine(tokenizerLine);

					if (!matched) {
						const CString mismatchLine(
							(L"[Chat] startup.localModel.tokenizer.roundtrip.mismatch - decoded=" +
								ToWide(decoded)).c_str());
						AppendMainFrameStatusLine(mismatchLine);
					}
				}
			}
		}

		if (runtime.ready) {
         AppendMainFrameStatusLine(
				L"[Chat] startup.localModel.qwenContract - promptTemplate=qwen3-chat markers=<|im_start|>/<|im_end|> decodeStop=<|im_end|>");
          if (services.LocalModelActivationEnabled()) {
				AppendMainFrameStatusLine(
					L"[Chat] startup.localModel.loaded - local ONNX runtime ready and active");
			} else {
				AppendMainFrameStatusLine(
					L"[Chat] startup.localModel.loaded - local ONNX runtime ready but fallback is active");
			}
			return;
		}

		const CString errorLine(
			(L"[Chat] startup.localModel.error - status=" +
				ToWide(runtime.status)).c_str());
		AppendMainFrameStatusLine(errorLine);
	}

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
	AppendStartupConfigStatus(m_config);
  AppendStartupLocalModelStatus(m_config, m_serviceManager);
	AppendStartupEmbeddingsStatus(m_config, m_serviceManager);

	return TRUE;
}

int CBlazeClawMFCApp::ExitInstance() {
	m_serviceManager.Stop();

	AfxOleTerm(FALSE);

	return CWinApp::ExitInstance();
}

BOOL CBlazeClawMFCApp::OnIdle(LONG lCount) {
	BOOL baseHandled = CWinAppEx::OnIdle(lCount);

	std::string pumpError;
	if (!m_serviceManager.PumpGatewayNetworkOnce(pumpError)) {
		if (!pumpError.empty()) {
			TRACE(
				"[Gateway][PumpNetworkOnce] %s\n",
				pumpError.c_str());
		}
	}

	return baseHandled;
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
