#include "pch.h"
#include "SettingsDialog.h"
#include "BlazeClawMfcApp.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <optional>

namespace {
	constexpr wchar_t kConfigPath[] = L"blazeclaw.conf";

	std::wstring TrimW(const std::wstring& value)
	{
		const auto first = std::find_if_not(
			value.begin(),
			value.end(),
			[](const wchar_t ch) { return std::iswspace(ch) != 0; });
		const auto last = std::find_if_not(
			value.rbegin(),
			value.rend(),
			[](const wchar_t ch) { return std::iswspace(ch) != 0; }).base();
		if (first >= last) {
			return {};
		}

		return std::wstring(first, last);
	}

	std::wstring ToWideAscii(const std::string& value)
	{
		std::wstring output;
		output.reserve(value.size());
		for (const char ch : value) {
			output.push_back(static_cast<wchar_t>(
				static_cast<unsigned char>(ch)));
		}
		return output;
	}

	void UpsertConfigEntry(
		std::vector<std::wstring>& lines,
		const std::wstring& key,
		const std::wstring& value)
	{
		const std::wstring prefix = key + L"=";
		for (std::wstring& line : lines) {
			if (TrimW(line).rfind(prefix, 0) == 0) {
				line = prefix + value;
				return;
			}
		}

		lines.push_back(prefix + value);
	}

	std::pair<std::string, std::string> ResolveActiveProviderModel(
		const CSettingsDialog::ModelItem& item)
	{
		if (item.id.rfind("deepseek/", 0) == 0) {
			const std::string model = item.id.substr(std::string("deepseek/").size());
			return { "deepseek", model.empty() ? "deepseek-chat" : model };
		}

		if (item.id == "reasoner") {
			return { "local", "reasoner" };
		}

		if (item.id == "default" || item.id == "qwen3-local-onnx") {
			return { "local", "default" };
		}

		return { "", "" };
	}

	bool MatchesActiveSelection(
		const CSettingsDialog::ModelItem& item,
		const std::string& activeProvider,
		const std::string& activeModel)
	{
		const auto [provider, model] = ResolveActiveProviderModel(item);
		if (provider.empty() || model.empty()) {
			return false;
		}

		return provider == activeProvider && model == activeModel;
	}
}

IMPLEMENT_DYNAMIC(CSettingsDialog, CDialogEx)

CSettingsDialog::CSettingsDialog(CWnd* pParent)
	: CDialogEx(IDD_SETTINGS_DIALOG, pParent)
{}

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
	m_progress.SetRange32(0, 100);
	m_progress.SetPos(0);

	LoadModels();
	UpdateModelCount();

	return TRUE;
}

void CSettingsDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PROGRESS1, m_progress);
}

void CSettingsDialog::LoadModels()
{
	m_models.clear();
	m_listModels.DeleteAllItems();

	// Built-in models
	m_models.push_back({
		 "default",
		 "Default Model (Local ONNX)",
		 "Seed",
		 true
		});
	m_models.push_back({
		"reasoner",
		"Reasoner Model (Local ONNX)",
		"Seed",
		true
		});
	m_models.push_back({
		"deepseek/deepseek-chat",
		"DeepSeek Chat",
		"DeepSeek",
		true
		});
	m_models.push_back({
		"deepseek/deepseek-reasoner",
		"DeepSeek Reasoner",
		"DeepSeek",
		true
		});
	m_models.push_back({
		"qwen3-local-onnx",
		"Qwen3 Local ONNX",
		"Local",
		true
		});

	// Planned / not yet fully implemented model entries (disabled by default)
	m_models.push_back({
		"qwen2.5-1.5b-instruct-onnx",
		"Qwen2.5 1.5B Instruct (ONNX)",
		"Local",
		false
		});
	m_models.push_back({
		"gpt-4o",
		"GPT-4o",
		"OpenAI",
		false
		});
	m_models.push_back({
		"gpt-4o-mini",
		"GPT-4o Mini",
		"OpenAI",
		false
		});
	m_models.push_back({
		"gpt-4-turbo",
		"GPT-4 Turbo",
		"OpenAI",
		false
		});
	m_models.push_back({
		"gpt-3.5-turbo",
		"GPT-3.5 Turbo",
		"OpenAI",
		false
		});

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
	for (int i = 0; i < static_cast<int>(m_models.size()); ++i) {
		m_listModels.SetCheck(i, select);
		m_models[static_cast<size_t>(i)].enabled = (select != FALSE);
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
	auto updateProgress = [this](int value) {
		m_progress.SetPos((std::max)(0, (std::min)(value, 100)));
		m_progress.RedrawWindow();
		m_progress.UpdateWindow();
		};

	updateProgress(5);

	const std::vector<bool> previousEnabled = [this]() {
		std::vector<bool> states;
		states.reserve(m_models.size());
		for (const auto& model : m_models) {
			states.push_back(model.enabled);
		}
		return states;
		}();
	updateProgress(20);

	// Save enabled state back to config
	for (int i = 0; i < m_listModels.GetItemCount(); ++i) {
		size_t idx = (size_t)m_listModels.GetItemData(i);
		if (idx < m_models.size()) {
			m_models[idx].enabled = (m_listModels.GetCheck(i) != FALSE);
		}
	}
	updateProgress(40);

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app) {
		const std::string activeProvider = app->Services().ActiveChatProvider();
		const std::string activeModel = app->Services().ActiveChatModel();

		std::optional<size_t> targetIndex;
		for (size_t i = 0; i < m_models.size(); ++i) {
			if (!m_models[i].enabled) {
				continue;
			}
			if (i < previousEnabled.size() && !previousEnabled[i]) {
				targetIndex = i;
				break;
			}
		}

		if (!targetIndex.has_value()) {
			for (size_t i = 0; i < m_models.size(); ++i) {
				if (!m_models[i].enabled) {
					continue;
				}
				if (MatchesActiveSelection(m_models[i], activeProvider, activeModel)) {
					targetIndex = i;
					break;
				}
			}
		}
		updateProgress(60);

		if (!targetIndex.has_value()) {
			for (size_t i = 0; i < m_models.size(); ++i) {
				if (m_models[i].enabled) {
					targetIndex = i;
					break;
				}
			}
		}

		if (!targetIndex.has_value()) {
			for (size_t i = 0; i < m_models.size(); ++i) {
				if (m_models[i].id == "default") {
					m_models[i].enabled = true;
					targetIndex = i;
					break;
				}
			}
		}

		if (targetIndex.has_value() && *targetIndex < m_models.size()) {
			const auto [provider, model] =
				ResolveActiveProviderModel(m_models[*targetIndex]);
			if (!provider.empty() && !model.empty()) {
				updateProgress(75);
				app->Services().SetActiveChatProvider(provider, model);

				std::vector<std::wstring> lines;
				{
					updateProgress(85);
					std::wifstream input(kConfigPath);
					std::wstring line;
					while (std::getline(input, line)) {
						lines.push_back(line);
					}
				}

				UpsertConfigEntry(
					lines,
					L"chat.activeProvider",
					ToWideAscii(provider));
				UpsertConfigEntry(
					lines,
					L"chat.activeModel",
					ToWideAscii(model));

				std::wofstream output(kConfigPath, std::ios::trunc);
				if (output.is_open()) {
					updateProgress(95);
					for (const auto& line : lines) {
						output << line << L"\n";
					}
				}
			}
		}
	}

	updateProgress(100);

	CDialogEx::OnOK();
}
