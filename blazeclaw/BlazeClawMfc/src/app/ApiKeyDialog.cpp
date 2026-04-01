#include "pch.h"
#include "ApiKeyDialog.h"
#include "BlazeClawMfcApp.h"
#include "../config/ConfigLoader.h"

#include <vector>
#include <fstream>

IMPLEMENT_DYNAMIC(CApiKeyDialog, CDialogEx)

CApiKeyDialog::CApiKeyDialog(CWnd* pParent)
	: CDialogEx(IDD_APIKEY_DIALOG, pParent)
{
}

CApiKeyDialog::~CApiKeyDialog() = default;

BEGIN_MESSAGE_MAP(CApiKeyDialog, CDialogEx)
	ON_CBN_SELCHANGE(IDC_COMBO_PROVIDER, &CApiKeyDialog::OnProviderChanged)
	ON_BN_CLICKED(IDC_BUTTON_TEST_KEY, &CApiKeyDialog::OnTestKey)
	ON_BN_CLICKED(IDC_BUTTON_DELETE_KEY, &CApiKeyDialog::OnDeleteKey)
END_MESSAGE_MAP()

// =============================================================================
// Credential management helpers
// =============================================================================

/*static*/ std::wstring CApiKeyDialog::CredTargetName(Provider provider)
{
	switch (provider) {
	case Provider::DeepSeek: return _T("blazeclaw.deepseek");
	case Provider::OpenAI:   return _T("blazeclaw.openai");
	}
	return _T("blazeclaw.unknown");
}

std::wstring DpapiFilePath(CApiKeyDialog::Provider provider)
{
	wchar_t appdataBuf[MAX_PATH];
	if (GetEnvironmentVariableW(_T("APPDATA"), appdataBuf, (DWORD)std::size(appdataBuf)) == 0)
		return {};
	return std::wstring(appdataBuf) + _T("\\BlazeClaw\\") +
		CApiKeyDialog::CredTargetName(provider) + _T(".key");
}

/*static*/ bool CApiKeyDialog::MigrateLegacyConfig(Provider provider)
{
	if (provider != Provider::DeepSeek)
		return false;

	blazeclaw::config::AppConfig cfg;
	blazeclaw::config::ConfigLoader loader;
	if (!loader.LoadFromFile(_T("blazeclaw.conf"), cfg) || cfg.deepseekApiKey.empty())
		return false;

	const std::string key(CT2A(cfg.deepseekApiKey.c_str(), CP_UTF8));

	blazeclaw::app::CredentialStore::SaveCredential(CredTargetName(provider), key);

	wchar_t appdataBuf[MAX_PATH];
	if (GetEnvironmentVariableW(_T("APPDATA"), appdataBuf, (DWORD)std::size(appdataBuf)) > 0) {
		std::wstring dir = std::wstring(appdataBuf) + _T("\\BlazeClaw");
		::CreateDirectoryW(dir.c_str(), nullptr);
		blazeclaw::app::CredentialStore::SaveCredentialDPAPI(DpapiFilePath(provider), key);
	}

	// Strip plaintext from config
	const std::wstring configPath = _T("blazeclaw.conf");
	std::vector<std::wstring> lines;
	std::wifstream infile(configPath);
	bool updated = false;
	if (infile.is_open()) {
		std::wstring line;
		while (std::getline(infile, line)) {
			size_t start = line.find_first_not_of(_T(" \t\r\n"));
			if (start != std::wstring::npos &&
				line.compare(start, 16, _T("deepseek.apiKey=")) == 0) {
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
			for (const auto& l : lines)
				outfile << l << _T("\n");
			outfile.close();
		}
	}
	return true;
}

/*static*/ bool CApiKeyDialog::LoadKey(Provider provider, std::string& outUtf8, Source& outSource)
{
	const std::wstring target = CredTargetName(provider);

	// 1. Credential Manager (primary)
	if (auto cred = blazeclaw::app::CredentialStore::LoadCredential(target); cred.has_value()) {
		outUtf8 = *cred;
		outSource = Source::CredentialManager;
		return true;
	}

	// 2. DPAPI per-user file (legacy fallback)
	std::wstring dpPath = DpapiFilePath(provider);
	if (!dpPath.empty()) {
		if (auto dp = blazeclaw::app::CredentialStore::LoadCredentialDPAPI(dpPath); dp.has_value()) {
			blazeclaw::app::CredentialStore::SaveCredential(target, *dp);
			outUtf8 = *dp;
			outSource = Source::DPAPI;
			return true;
		}
	}

	// 3. Legacy plaintext config
	if (MigrateLegacyConfig(provider)) {
		if (auto cred = blazeclaw::app::CredentialStore::LoadCredential(target); cred.has_value()) {
			outUtf8 = *cred;
			outSource = Source::ConfigFile;
			return true;
		}
	}
	return false;
}

/*static*/ bool CApiKeyDialog::SaveKey(Provider provider, std::string_view utf8Key)
{
	const std::wstring target = CredTargetName(provider);
	if (!blazeclaw::app::CredentialStore::SaveCredential(target, std::string(utf8Key)))
		return false;

	MigrateLegacyConfig(provider);
	std::wstring dpPath = DpapiFilePath(provider);
	if (!dpPath.empty())
		::DeleteFileW(dpPath.c_str());
	return true;
}

/*static*/ bool CApiKeyDialog::DeleteKey(Provider provider)
{
	const std::wstring target = CredTargetName(provider);
	bool ok = blazeclaw::app::CredentialStore::DeleteCredential(target);
	std::wstring dpPath = DpapiFilePath(provider);
	if (!dpPath.empty())
		::DeleteFileW(dpPath.c_str());
	return ok;
}

// =============================================================================
// Dialog methods
// =============================================================================

BOOL CApiKeyDialog::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	SetWindowTextW(_T("API Key Configuration"));

	// Subclass the controls
	m_comboProvider.SubclassDlgItem(IDC_COMBO_PROVIDER, this);
	m_editKey.SubclassDlgItem(IDC_EDIT_APIKEY, this);
	m_btnTest.SubclassDlgItem(IDC_BUTTON_TEST_KEY, this);
	m_btnDelete.SubclassDlgItem(IDC_BUTTON_DELETE_KEY, this);
	m_staticStatus.SubclassDlgItem(IDC_STATIC_APIKEY_STATUS, this);

	FillProviderCombo();
	LoadExistingKey();
	return TRUE;
}

void CApiKeyDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	// Controls are already subclassed via SubclassDlgItem in OnInitDialog
}

void CApiKeyDialog::FillProviderCombo()
{
	m_comboProvider.ResetContent();
	m_comboProvider.AddString(_T("DeepSeek"));
	m_comboProvider.AddString(_T("OpenAI"));
	m_comboProvider.SetCurSel(0);
}

CApiKeyDialog::Provider CApiKeyDialog::CurrentProvider() const
{
	return (m_comboProvider.GetCurSel() == 1) ? Provider::OpenAI : Provider::DeepSeek;
}

void CApiKeyDialog::LoadExistingKey()
{
	auto    provider = CurrentProvider();
	std::string utf8;
	Source  source = Source::None;
	bool hasKey = LoadKey(provider, utf8, source);

	m_lastSource = source;
	m_savedKey = hasKey ? utf8 : std::string{};

	UpdateStatusText(m_lastSource, hasKey);

	m_btnDelete.EnableWindow(hasKey ? TRUE : FALSE);

	if (hasKey) {
		m_apiKey = CA2T(utf8.c_str(), CP_UTF8);
		m_editKey.SetWindowText(m_apiKey);
	}
	else {
		m_apiKey.Empty();
		m_editKey.SetWindowText(_T(""));
	}
}

void CApiKeyDialog::UpdateStatusText(Source source, bool hasKey)
{
	const wchar_t* text = nullptr;
	if (hasKey) {
		switch (source) {
		case Source::CredentialManager:
			text = _T("Key stored securely in Windows Credential Manager");
			break;
		case Source::DPAPI:
			text = _T("Key loaded from local encrypted store");
			break;
		case Source::ConfigFile:
			text = _T("Key loaded from legacy config");
			break;
		default:
			text = _T("Key configured");
		}
	}
	else {
		text = _T("No key configured");
	}
	m_staticStatus.SetWindowTextW(text);
}


void CApiKeyDialog::OnProviderChanged()
{
	LoadExistingKey();
}

void CApiKeyDialog::OnTestKey()
{
	CString keyW;
	m_editKey.GetWindowText(keyW);
	if (keyW.IsEmpty()) {
		m_staticStatus.SetWindowTextW(_T("Please enter an API key first"));
		return;
	}

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr) {
		m_staticStatus.SetWindowTextW(_T("App context unavailable"));
		return;
	}

	std::string keyUtf8(CT2A(keyW, CP_UTF8));
	auto       provider = CurrentProvider();
	std::string model = (provider == Provider::DeepSeek) ? "deepseek-chat" : "gpt-4o";
	std::string params = "{\"model\":\"" + model + "\",\"deepseekApiKey\":\"" + keyUtf8 + "\"}";

	(void)app->Services().InvokeGatewayMethod("gateway.config.set", params);
	m_staticStatus.SetWindowTextW(_T("Test request sent - check gateway status pane"));
}

void CApiKeyDialog::OnDeleteKey()
{
	auto provider = CurrentProvider();
	DeleteKey(provider);

	m_staticStatus.SetWindowTextW(_T("Key cleared"));
	m_editKey.SetWindowTextW(_T(""));
	m_lastSource = Source::None;
	m_savedKey.clear();
}

void CApiKeyDialog::OnOK()
{
	CString keyW;
	m_editKey.GetWindowText(keyW);
	std::string keyUtf8(CT2A(keyW, CP_UTF8));

	if (keyUtf8.empty()) {
		AfxMessageBox(_T("Please enter an API key."), MB_OK | MB_ICONEXCLAMATION);
		return;
	}

	auto provider = CurrentProvider();
	if (SaveKey(provider, keyUtf8)) {
		m_staticStatus.SetWindowTextW(_T("Key saved to Windows Credential Manager"));
		m_savedKey = keyUtf8;
		m_lastSource = Source::CredentialManager;
		m_apiKey = keyW;
		CDialogEx::OnOK();
	}
	else {
		AfxMessageBox(_T("Failed to save the key to Windows Credential Manager."),
			MB_OK | MB_ICONEXCLAMATION);
	}
}
