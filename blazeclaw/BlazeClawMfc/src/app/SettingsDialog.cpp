#include "pch.h"
#include "SettingsDialog.h"
#include "BlazeClawMfcApp.h"

IMPLEMENT_DYNAMIC(CSettingsDialog, CDialogEx)

CSettingsDialog::CSettingsDialog(CWnd* pParent)
	: CDialogEx(IDD_SETTINGS_DIALOG, pParent)
{
}

CSettingsDialog::~CSettingsDialog() = default;

BEGIN_MESSAGE_MAP(CSettingsDialog, CDialogEx)
	ON_BN_CLICKED(IDC_BUTTON_SELECT_ALL, &CSettingsDialog::OnSelectAll)
	ON_BN_CLICKED(IDC_BUTTON_DESELECT_ALL, &CSettingsDialog::OnDeselectAll)
END_MESSAGE_MAP()

BOOL CSettingsDialog::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	SetWindowTextW(_T("Settings"));

	// Subclass the controls
	m_listModels.SubclassDlgItem(IDC_LIST_MODELS, this);
	m_staticCount.SubclassDlgItem(IDC_STATIC_MODEL_COUNT, this);

	// Set up list view for checkboxes
	m_listModels.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

	// Add column (no header, just the model name)
	m_listModels.InsertColumn(0, _T(""), LVCFMT_LEFT, 370);

	LoadModels();
	UpdateModelCount();

	return TRUE;
}

void CSettingsDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

void CSettingsDialog::LoadModels()
{
	m_models.clear();
	m_listModels.DeleteAllItems();

	// Built-in models
	m_models.push_back({"deepseek-chat", "DeepSeek Chat", "DeepSeek", true});
	m_models.push_back({"deepseek-reasoner", "DeepSeek Reasoner", "DeepSeek", true});
	m_models.push_back({"gpt-4o", "GPT-4o", "OpenAI", true});
	m_models.push_back({"gpt-4o-mini", "GPT-4o Mini", "OpenAI", true});
	m_models.push_back({"gpt-4-turbo", "GPT-4 Turbo", "OpenAI", false});
	m_models.push_back({"gpt-3.5-turbo", "GPT-3.5 Turbo", "OpenAI", false});

	// Load enabled state from config if available
	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app) {
		// TODO: Load enabled models from config
	}

	// Populate the list
	for (size_t i = 0; i < m_models.size(); ++i) {
		int item = m_listModels.InsertItem((int)i,
			CA2T(m_models[i].name.c_str(), CP_UTF8));
		m_listModels.SetItemData(item, (DWORD_PTR)i);
		m_listModels.SetCheck(item, m_models[i].enabled ? TRUE : FALSE);
	}
}

void CSettingsDialog::UpdateModelCount()
{
	int total = (int)m_models.size();
	int enabled = 0;
	for (const auto& m : m_models) {
		if (m.enabled) ++enabled;
	}

	CString text;
	text.Format(_T("%d of %d enabled"), enabled, total);
	m_staticCount.SetWindowTextW(text);
}

void CSettingsDialog::SelectAll(BOOL select)
{
	for (size_t i = 0; i < (int)m_models.size(); ++i) {
		m_listModels.SetCheck(i, select);
		m_models[i].enabled = (select != FALSE);
	}
	UpdateModelCount();
}

void CSettingsDialog::OnSelectAll()
{
	SelectAll(TRUE);
}

void CSettingsDialog::OnDeselectAll()
{
	SelectAll(FALSE);
}

void CSettingsDialog::OnOK()
{
	// Save enabled state back to config
	for (int i = 0; i < m_listModels.GetItemCount(); ++i) {
		size_t idx = (size_t)m_listModels.GetItemData(i);
		if (idx < m_models.size()) {
			m_models[idx].enabled = (m_listModels.GetCheck(i) != FALSE);
		}
	}

	// TODO: Save to config file

	CDialogEx::OnOK();
}
