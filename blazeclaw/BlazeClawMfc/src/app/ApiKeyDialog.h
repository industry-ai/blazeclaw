#pragma once

#include "pch.h"
#include "Resource.h"
#include "CredentialStore.h"

#include <Windows.h>
#include <wincred.h>
#include <string>

class CApiKeyDialog : public CDialogEx {
	DECLARE_DYNAMIC(CApiKeyDialog)

public:
	explicit CApiKeyDialog(CWnd* pParent = nullptr);
	~CApiKeyDialog() override;

	CString      m_apiKey;

	// Credential source
	enum class Source { None, CredentialManager, DPAPI, ConfigFile };

	// Provider
	enum class Provider { DeepSeek, OpenAI };

	// UTF-8 conversion
	static std::string WstrToUtf8(const std::wstring& w);
	static std::wstring Utf8ToWstr(const std::string& s);

	// Credential helpers (static utility methods)
	static std::wstring CredTargetName(Provider provider);
	static bool LoadKey(Provider provider, std::string& outUtf8, Source& outSource);
	static bool SaveKey(Provider provider, std::string_view utf8Key);
	static bool DeleteKey(Provider provider);
	static bool MigrateLegacyConfig(Provider provider);

protected:
	enum { IDD = IDD_APIKEY_DIALOG };

	BOOL OnInitDialog() override;
	void DoDataExchange(CDataExchange* pDX) override;
	void OnOK() override;

	DECLARE_MESSAGE_MAP()

	// UI helpers
	void LoadExistingKey();
	void UpdateStatusText(Source source, bool hasKey);
	void FillProviderCombo();
	Provider CurrentProvider() const;

	// Actions
	afx_msg void OnProviderChanged();
	afx_msg void OnTestKey();
	afx_msg void OnDeleteKey();

	// Controls
	CComboBox   m_comboProvider;
	CButton     m_btnTest;
	CButton     m_btnDelete;
	CEdit       m_editKey;
	CStatic     m_staticStatus;

	// Runtime state
	std::string   m_savedKey;
	Source        m_lastSource  = Source::None;

};

// DPAPI file path helper (defined in .cpp)
std::wstring DpapiFilePath(CApiKeyDialog::Provider provider);
