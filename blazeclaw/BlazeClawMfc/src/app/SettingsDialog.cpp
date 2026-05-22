#include "pch.h"
#include "SettingsDialog.h"
#include "BlazeClawMfcApp.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <optional>
#include <unordered_map>

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

	std::string ToNarrowAscii(const std::wstring& value)
	{
		std::string output;
		output.reserve(value.size());
		for (const wchar_t ch : value) {
			output.push_back(static_cast<char>(
				(ch <= 0x7F) ? ch : '?'));
		}

		return output;
	}

	bool ParseBoolW(const std::wstring& raw, const bool fallback)
	{
		const std::wstring value = TrimW(raw);
		if (value == L"true" || value == L"1" || value == L"yes") {
			return true;
		}

		if (value == L"false" || value == L"0" || value == L"no") {
			return false;
		}

		return fallback;
	}

	struct SpeechConfigState {
		bool enabled = false;
		std::wstring provider = L"onnx";
		std::wstring storageRoot =
			L"BlazeClawMfc/models/chat/qwen3-asr-1.7b-onnx";
		std::wstring modelPath;
	};

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
		if (item.id.rfind("llama/", 0) == 0) {
			return { "local", item.id };
		}

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

	bool IsLlamaModelId(const std::string& modelId)
	{
		return modelId.rfind("llama/", 0) == 0;
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

	void ReadModelConfigState(
		std::unordered_map<std::string, bool>& outEnabledById,
		std::string& outActiveProvider,
		std::string& outActiveModel)
	{
		outEnabledById.clear();
		outActiveProvider.clear();
		outActiveModel.clear();

		std::wifstream input(kConfigPath);
		if (!input.is_open()) {
			return;
		}

		std::wstring line;
		while (std::getline(input, line)) {
			const std::wstring trimmed = TrimW(line);
			if (trimmed.empty() || trimmed.starts_with(L"#")) {
				continue;
			}

			if (trimmed.rfind(L"chat.activeProvider=", 0) == 0) {
				outActiveProvider = ToNarrowAscii(trimmed.substr(20));
				continue;
			}

			if (trimmed.rfind(L"chat.activeModel=", 0) == 0) {
				outActiveModel = ToNarrowAscii(trimmed.substr(17));
				continue;
			}

			constexpr wchar_t kPrefix[] = L"chat.model.enabled.";
			if (trimmed.rfind(kPrefix, 0) == 0) {
				const std::size_t eqPos = trimmed.find(L'=');
				if (eqPos == std::wstring::npos || eqPos <= wcslen(kPrefix)) {
					continue;
				}

				const std::wstring modelIdW =
					trimmed.substr(wcslen(kPrefix), eqPos - wcslen(kPrefix));
				const std::wstring rawValue = trimmed.substr(eqPos + 1);
				outEnabledById.insert_or_assign(
					ToNarrowAscii(modelIdW),
					ParseBoolW(rawValue, false));
			}
		}
	}

	SpeechConfigState ReadSpeechConfigState()
	{
		SpeechConfigState state;

		std::wifstream input(kConfigPath);
		if (!input.is_open()) {
			return state;
		}

		std::wstring line;
		while (std::getline(input, line)) {
			const std::wstring trimmed = TrimW(line);
			if (trimmed.empty() || trimmed.starts_with(L"#")) {
				continue;
			}

			if (trimmed.rfind(L"speech.enabled=", 0) == 0) {
				state.enabled = ParseBoolW(trimmed.substr(15), state.enabled);
				continue;
			}

			if (trimmed.rfind(L"speech.provider=", 0) == 0) {
				state.provider = TrimW(trimmed.substr(16));
				continue;
			}

			if (trimmed.rfind(L"speech.storageRoot=", 0) == 0) {
				state.storageRoot = TrimW(trimmed.substr(19));
				continue;
			}

			if (trimmed.rfind(L"speech.model_path=", 0) == 0) {
				state.modelPath = TrimW(trimmed.substr(18));
				continue;
			}
		}

		if (state.provider.empty()) {
			state.provider = L"onnx";
		}
		if (state.storageRoot.empty()) {
			state.storageRoot =
				L"BlazeClawMfc/models/chat/qwen3-asr-1.7b-onnx";
		}

		return state;
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
	m_listGenerativeModels.SubclassDlgItem(IDC_LIST_MODELS, this);
	m_listFeatureModels.SubclassDlgItem(IDC_LIST_MODELS_EX, this);
	m_staticCount.SubclassDlgItem(IDC_STATIC_MODEL_COUNT, this);
	//m_editSpeechStorageRoot.SubclassDlgItem(IDC_EDIT_SPEECH_STORAGE_ROOT, this);
	//m_editSpeechModelPath.SubclassDlgItem(IDC_EDIT_SPEECH_MODEL_PATH, this);

	// Set up list view for checkboxes
	m_listGenerativeModels.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
	m_listFeatureModels.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);

	// Add column (no header, just the model name)
	m_listGenerativeModels.InsertColumn(0, _T(""), LVCFMT_LEFT, 370);
	m_listFeatureModels.InsertColumn(0, _T(""), LVCFMT_LEFT, 370);
	m_progress.SetRange32(0, 100);
	m_progress.SetPos(0);

	LoadModels();
	LoadFeatureModels();
	UpdateModelCount();

	return TRUE;
}

void CSettingsDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_PROGRESS1, m_progress);
	DDX_Control(pDX, IDC_EDIT_SPEECH_STORAGE_ROOT, m_editSpeechStorageRoot);
	DDX_Control(pDX, IDC_EDIT_SPEECH_MODEL_PATH, m_editSpeechModelPath);
	DDX_Text(pDX, IDC_EDIT_SPEECH_STORAGE_ROOT, m_speechStorageRoot);
	DDX_Text(pDX, IDC_EDIT_SPEECH_MODEL_PATH, m_speechModelPath);
}

void CSettingsDialog::LoadFeatureModels()
{
	m_featureModels.clear();
	m_listFeatureModels.DeleteAllItems();

	m_featureModels.push_back({
		"speech/qwen3-asr-1.7b-onnx",
		"Qwen3 ASR 1.7B (ONNX)",
		"ONNX Runtime",
		false,
		});

	const SpeechConfigState speechConfig = ReadSpeechConfigState();

	m_speechStorageRoot = speechConfig.storageRoot.c_str();
	m_speechModelPath = speechConfig.modelPath.c_str();

	if (!m_featureModels.empty()) {
		m_featureModels[0].enabled = speechConfig.enabled;
	}

	for (size_t i = 0; i < m_featureModels.size(); ++i) {
		const int item = m_listFeatureModels.InsertItem(
			static_cast<int>(i),
			CA2T(m_featureModels[i].name.c_str(), CP_UTF8));
		m_listFeatureModels.SetItemData(item, static_cast<DWORD_PTR>(i));
		m_listFeatureModels.SetCheck(
			item,
			m_featureModels[i].enabled ? TRUE : FALSE);
	}

	UpdateData(FALSE);
}

void CSettingsDialog::LoadModels()
{
	m_models.clear();
	m_listGenerativeModels.DeleteAllItems();
	m_listFeatureModels.DeleteAllItems();

	// Built-in models
	m_models.push_back({
		"default",
		"Default Model (Local ONNX)",
		"Seed",
		"",
		false
		});
	m_models.push_back({
		"reasoner",
		"Reasoner Model (Local ONNX)",
		"Seed",
		"",
		false
		});
	m_models.push_back({
		"deepseek/deepseek-chat",
		"DeepSeek Chat",
		"DeepSeek",
		"",
		false
		});
	m_models.push_back({
		"deepseek/deepseek-reasoner",
		"DeepSeek Reasoner",
		"DeepSeek",
		"",
		false
		});
	m_models.push_back({
		"qwen3-local-onnx",
		"Qwen3 Local ONNX",
		"Local",
		"",
		false
		});
	m_models.push_back({
		"llama/gemma-4-E2B-it",
		"Gemma 4 E2B (IT) — Local (llama.cpp)",
		"Local (llama.cpp)",
		"",
		false
		});

	// Planned / not yet fully implemented model entries (disabled by default)
	m_models.push_back({
		"qwen2.5-1.5b-instruct-onnx",
		"Qwen2.5 1.5B Instruct (ONNX)",
		"Local",
		"",
		false
		});
	m_models.push_back({
		"gpt-4o",
		"GPT-4o",
		"OpenAI",
		"",
		false
		});
	m_models.push_back({
		"gpt-4o-mini",
		"GPT-4o Mini",
		"OpenAI",
		"",
		false
		});
	m_models.push_back({
		"gpt-4-turbo",
		"GPT-4 Turbo",
		"OpenAI",
		"",
		false
		});
	m_models.push_back({
		"gpt-3.5-turbo",
		"GPT-3.5 Turbo",
		"OpenAI",
		"",
		false
		});

	// Load enabled state from config if available
	std::unordered_map<std::string, bool>	enabledById;
	std::string		activeProvider;
	std::string		activeModel;

	ReadModelConfigState(enabledById, activeProvider, activeModel);

	bool	anyChecked	= false;
	for (auto& model : m_models) {
		const auto it	= enabledById.find(model.id);
		if (it == enabledById.end()) {
			continue;
		}

		model.enabled	= it->second;
		if (model.enabled) {
			anyChecked	= true;
		}
	}

	if (!anyChecked) {
		for (auto& model : m_models) {
			if (MatchesActiveSelection(model, activeProvider, activeModel)) {
				model.enabled	= true;
				anyChecked		= true;
				break;
			}
		}
	}

	if (!anyChecked) {
		for (auto& model : m_models) {
			if (model.id == "default") {
				model.enabled	= true;
				break;
			}
		}
	}

	// Populate the list
	for (size_t i = 0; i < m_models.size(); ++i) {
		int item = m_listGenerativeModels.InsertItem((int)i,
			CA2T(m_models[i].name.c_str(), CP_UTF8));
		m_listGenerativeModels.SetItemData(item, (DWORD_PTR)i);
		m_listGenerativeModels.SetCheck(item, m_models[i].enabled ? TRUE : FALSE);
	}
}

void CSettingsDialog::UpdateModelCount()
{
	int total	= (int)m_models.size();
	int enabled	= 0;
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
		m_listGenerativeModels.SetCheck(i, select);
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
	for (int i = 0; i < m_listGenerativeModels.GetItemCount(); ++i) {
		size_t idx = (size_t)m_listGenerativeModels.GetItemData(i);
		if (idx < m_models.size()) {
			m_models[idx].enabled = (m_listGenerativeModels.GetCheck(i) != FALSE);
		}
	}

	UpdateData(TRUE);
	for (int i = 0; i < m_listFeatureModels.GetItemCount(); ++i) {
		const size_t idx =
			static_cast<size_t>(m_listFeatureModels.GetItemData(i));
		if (idx < m_featureModels.size()) {
			m_featureModels[idx].enabled =
				(m_listFeatureModels.GetCheck(i) != FALSE);
		}
	}

	const bool speechEnabled = std::any_of(
		m_featureModels.begin(),
		m_featureModels.end(),
		[](const FeatureModelItem& item) {
			return item.enabled;
		});

	std::wstring speechStorageRoot =
		TrimW(static_cast<LPCWSTR>(m_speechStorageRoot));
	if (speechStorageRoot.empty()) {
		speechStorageRoot =
			L"BlazeClawMfc/models/chat/qwen3-asr-1.7b-onnx";
	}
	const std::wstring speechModelPath =
		TrimW(static_cast<LPCWSTR>(m_speechModelPath));
	updateProgress(40);

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	std::optional<size_t> targetIndex;

	if (app) {
		const std::string activeProvider = app->Services().ActiveChatProvider();
		const std::string activeModel = app->Services().ActiveChatModel();

		const int selectedItem =
			m_listGenerativeModels.GetNextItem(-1, LVNI_SELECTED);
		if (selectedItem >= 0) {
			const size_t selectedIndex =
				(size_t)m_listGenerativeModels.GetItemData(selectedItem);
			if (selectedIndex < m_models.size() &&
				m_models[selectedIndex].enabled) {
				targetIndex = selectedIndex;
			}
		}

		for (size_t i = 0; i < m_models.size(); ++i) {
			if (targetIndex.has_value()) {
				break;
			}

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
	}

	std::vector<std::wstring> lines;
	{
		updateProgress(50);
		std::wifstream input(kConfigPath);
		std::wstring line;
		while (std::getline(input, line)) {
			lines.push_back(line);
		}
	}

	for (const auto& item : m_models) {
		UpsertConfigEntry(
			lines,
			L"chat.model.enabled." + ToWideAscii(item.id),
			item.enabled ? L"true" : L"false");
	}

	UpsertConfigEntry(
		lines,
		L"speech.enabled",
		speechEnabled ? L"true" : L"false");
	UpsertConfigEntry(
		lines,
		L"speech.provider",
		L"onnx");
	UpsertConfigEntry(
		lines,
		L"speech.storageRoot",
		speechStorageRoot);
	UpsertConfigEntry(
		lines,
		L"speech.model_path",
		speechModelPath);

	if (app && targetIndex.has_value() && *targetIndex < m_models.size()) {
		const auto [provider, model] =
			ResolveActiveProviderModel(m_models[*targetIndex]);
		if (!provider.empty() && !model.empty()) {
			updateProgress(75);
			app->Services().SetActiveChatProvider(provider, model);

			UpsertConfigEntry(
				lines,
				L"chat.activeProvider",
				ToWideAscii(provider));
			UpsertConfigEntry(
				lines,
				L"chat.activeModel",
				ToWideAscii(model));

			if (provider == "local") {
				if (IsLlamaModelId(model)) {
					UpsertConfigEntry(
						lines,
						L"chat.localModel.provider",
						L"llama.cpp");
					UpsertConfigEntry(
						lines,
						L"chat.localModel.storageRoot",
						L"blazeclaw/BlazeClawMfc/models/google/gemma-4-E2B-it");
					UpsertConfigEntry(
						lines,
						L"chat.localModel.modelPath",
						L"gemma-4-E2B-it.gguf");
				}
				else {
					UpsertConfigEntry(
						lines,
						L"chat.localModel.provider",
						L"onnx");
				}
			}
		}
	}

	updateProgress(85);
	{
		std::wofstream output(kConfigPath, std::ios::trunc);
		if (output.is_open()) {
			updateProgress(95);
			for (const auto& line : lines) {
				output << line << L"\n";
			}
		}
	}

	updateProgress(100);

	CDialogEx::OnOK();
}
