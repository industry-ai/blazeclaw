#include "pch.h"
#include "CronTasksDialog.h"
#include "framework.h"
#include "BlazeClawMfcApp.h"
#include "afxcmn.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <ctime>

#include <nlohmann/json.hpp>
#include "../cron/CronOpsService.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

IMPLEMENT_DYNAMIC(CCronTasksDialog, CDialogEx)

CCronTasksDialog::CCronTasksDialog(CWnd* pParent)
	: CDialogEx(IDD_CRON_TASKS_DIALOG, pParent)
{
}

CCronTasksDialog::~CCronTasksDialog() = default;

void CCronTasksDialog::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_LIST_CRON_TASKS, m_listTasks);
	DDX_Control(pDX, IDC_STATIC_CRON_COUNT, m_staticCount);
}

BEGIN_MESSAGE_MAP(CCronTasksDialog, CDialogEx)
	ON_BN_CLICKED(IDC_BUTTON_CRON_SELECT_ALL, &CCronTasksDialog::OnSelectAll)
	ON_BN_CLICKED(IDC_BUTTON_CRON_DESELECT_ALL, &CCronTasksDialog::OnDeselectAll)
	ON_BN_CLICKED(IDC_BUTTON_CRON_DELETE, &CCronTasksDialog::OnDeleteSelected)
	ON_NOTIFY(LVN_ITEMCHANGED, IDC_LIST_CRON_TASKS, &CCronTasksDialog::OnItemChanged)
	ON_WM_NCHITTEST()
END_MESSAGE_MAP()

namespace {
std::string TrimWideAscii(const std::wstring& value)
{
	const auto first = std::find_if_not(
		value.begin(), value.end(), [](wchar_t ch) { return ch == L' ' || ch == L'\t'; });
	const auto last = std::find_if_not(
		value.rbegin(), value.rend(), [](wchar_t ch) { return ch == L' ' || ch == L'\t'; }).base();
	if (first >= last) {
		return {};
	}
	std::string output;
	output.reserve(static_cast<size_t>(last - first));
	for (auto it = first; it != last; ++it) {
		output.push_back(static_cast<char>(*it <= 0x7F ? *it : '?'));
	}
	return output;
}

std::string ToUtf8(const std::wstring& wide)
{
	if (wide.empty()) {
		return {};
	}
	const int required = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
	if (required <= 0) {
		return {};
	}
	std::string output(static_cast<size_t>(required), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), output.data(), required, nullptr, nullptr);
	return output;
}
} // namespace

BOOL CCronTasksDialog::OnInitDialog()
{
	CDialogEx::OnInitDialog();
	SetWindowTextW(_T("Cron Tasks"));

	m_listTasks.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
	m_listTasks.InsertColumn(0, _T("Name"), LVCFMT_LEFT, 260);
	m_listTasks.InsertColumn(1, _T("Schedule"), LVCFMT_LEFT, 200);
	m_listTasks.InsertColumn(2, _T("Last Run"), LVCFMT_LEFT, 140);

	LoadTasks();
	PopulateList();
	RefreshTaskDisplay();
	return TRUE;
}

void CCronTasksDialog::LoadTasks()
{
	m_tasks.clear();
	m_deletedIds.clear();

	auto* ops = &blazeclaw::cron::GetCronOpsService();
	const nlohmann::json payload = ops->List({ { "limit", 200 }, { "includeDisabled", true } });
	const auto jobs = payload.value("jobs", nlohmann::json::array());

	m_tasks.reserve(jobs.size());
	for (const auto& job : jobs) {
		TaskItem item{};
		item.id = job.value("id", "");
		item.name = job.value("name", "");
		item.description = job.value("description", "");
		item.enabled = job.value("enabled", true);
		item.status = item.enabled ? "enabled" : "disabled";

		const std::int64_t next = job.value("nextRunAtMs", static_cast<std::int64_t>(0));
		const std::int64_t last = job.value("updatedAtMs", static_cast<std::int64_t>(0));
		item.nextRunAt = FormatTimestamp(next);
		item.lastRunAt = FormatTimestamp(last);
		item.schedule = ExtractScheduleText(job);

		m_tasks.push_back(item);
	}
}

void CCronTasksDialog::PopulateList()
{
	m_listTasks.DeleteAllItems();
	for (size_t i = 0; i < m_tasks.size(); ++i) {
		const TaskItem& item = m_tasks[i];
		const int row = static_cast<int>(i);

		const std::wstring name(CA2W(item.name.c_str(), CP_UTF8));
		const std::wstring schedule(CA2W(item.schedule.c_str(), CP_UTF8));
		const std::wstring last(CA2W(item.lastRunAt.c_str(), CP_UTF8));

		const int itemIndex = m_listTasks.InsertItem(row, name.empty() ? _T("-") : name.c_str());
		m_listTasks.SetItemText(itemIndex, 1, schedule.empty() ? _T("-") : schedule.c_str());
		m_listTasks.SetItemText(itemIndex, 2, last.empty() ? _T("-") : last.c_str());
		m_listTasks.SetItemData(itemIndex, static_cast<DWORD_PTR>(row));
		m_listTasks.SetCheck(itemIndex, TRUE);
	}

	RefreshTaskDisplay();
}

void CCronTasksDialog::RefreshTaskDisplay()
{
	const int total = m_listTasks.GetItemCount();
	int checked = 0;
	for (int i = 0; i < total; ++i) {
		if (m_listTasks.GetCheck(i) != FALSE) {
			++checked;
		}
	}

	CString text;
	text.Format(_T("Cron tasks: %d total, %d selected"), total, checked);
	m_staticCount.SetWindowTextW(text);
}

std::string CCronTasksDialog::FormatTimestamp(std::int64_t ms) const
{
	if (ms <= 0) {
		return {};
	}

	using namespace std::chrono;
	const auto time = system_clock::time_point(milliseconds(ms));
	std::time_t timeT = system_clock::to_time_t(time);
	std::tm local{};
	if (!localtime_s(&local, &timeT)) {
		std::array<char, 64> buffer{};
		std::strftime(buffer.data(), buffer.size(), "%Y-%m-%d %H:%M:%S", &local);
		return std::string(buffer.data());
	}
	return {};
}

std::string CCronTasksDialog::ExtractScheduleText(const nlohmann::json& job) const
{
	const auto schedule = job.value("schedule", nlohmann::json::object());
	if (!schedule.is_object()) {
		return {};
	}

	std::ostringstream out;
	const bool hasCron = schedule.contains("cron") && schedule["cron"].is_string();
	const bool hasIntervalMs = schedule.contains("intervalMs") && schedule["intervalMs"].is_number_integer();
	const bool hasAtMs = schedule.contains("atMs") && schedule["atMs"].is_number_integer();

	if (hasCron) {
		out << schedule.value("cron", "");
	}

	if (hasIntervalMs) {
		if (!out.str().empty()) {
			out << " | ";
		}
		out << "interval " << schedule.value("intervalMs", 0) << "ms";
	}

	if (hasAtMs) {
		if (!out.str().empty()) {
			out << " | ";
		}
		out << "at " << FormatTimestamp(schedule.value("atMs", static_cast<std::int64_t>(0)));
	}

	return out.str();
}

LRESULT CCronTasksDialog::OnNcHitTest(CPoint point)
{
	LRESULT hit = CDialogEx::OnNcHitTest(point);
	if (hit == HTCLIENT) {
		ScreenToClient(&point);
		CWnd* pChild = ChildWindowFromPoint(point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED);
		if (pChild == nullptr || (pChild->GetStyle() & WS_TABSTOP) == 0) {
			return HTCAPTION;
		}
	}
	return hit;
}

void CCronTasksDialog::OnSelectAll()
{
	for (int i = 0; i < m_listTasks.GetItemCount(); ++i) {
		m_listTasks.SetCheck(i, TRUE);
	}
	RefreshTaskDisplay();
}

void CCronTasksDialog::OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult)
{
	const auto* pNMLV = reinterpret_cast<NMLISTVIEW*>(pNMHDR);
	if ((pNMLV->uChanged & LVIF_STATE) &&
	    ((pNMLV->uOldState & LVIS_STATEIMAGEMASK) != (pNMLV->uNewState & LVIS_STATEIMAGEMASK))) {
		RefreshTaskDisplay();
	}
	*pResult = 0;
}

void CCronTasksDialog::OnDeleteSelected()
{
	std::vector<size_t> selectedRows;
	selectedRows.reserve(static_cast<size_t>((std::max)(0, m_listTasks.GetItemCount())));

	const int total = m_listTasks.GetItemCount();

	for (int i = 0; i < total; ++i) {
		if (m_listTasks.GetCheck(i) != FALSE) {
			selectedRows.push_back(static_cast<size_t>(m_listTasks.GetItemData(i)));
		}
	}

	if (selectedRows.empty()) {
		AfxMessageBox(_T("Please select at least one task to delete."), MB_ICONINFORMATION);
		return;
	}

	const bool hasDisabledSelected = std::any_of(selectedRows.begin(), selectedRows.end(), [this](size_t row) {
		return row < m_tasks.size() && !m_tasks[row].enabled;
	});

	CString prompt;
	if (selectedRows.size() == 1) {
		prompt.Format(_T("Delete the selected cron task?"));
	}
	else {
		prompt.Format(_T("Delete %d selected cron tasks?"), static_cast<int>(selectedRows.size()));
	}
	prompt.Append(_T("\n\nThis will remove the tasks from cron permanently."));
	if (hasDisabledSelected) {
		prompt.Append(_T("\n\nWarning: selected tasks include disabled tasks."));
	}

	if (AfxMessageBox(prompt, MB_ICONQUESTION | MB_YESNO) != IDYES) {
		return;
	}

	std::vector<std::string> failed;
	for (size_t row : selectedRows) {
		if (row >= m_tasks.size()) {
			continue;
		}
		const std::string& taskId = m_tasks[row].id;
		try {
			blazeclaw::cron::GetCronOpsService().Remove({ { "id", taskId } });
			m_deletedIds.push_back(taskId);
		}
		catch (const std::exception& ex) {
			UNREFERENCED_PARAMETER(ex);
			failed.push_back(taskId);
		}
	}

	if (!failed.empty()) {
		CString error;
		error.Format(_T("Failed to delete %d task(s)."), static_cast<int>(failed.empty() ? 0 : failed.size()));
		AfxMessageBox(error, MB_ICONWARNING);
	}

	LoadTasks();
	PopulateList();
}

void CCronTasksDialog::OnDeselectAll()
{
	for (int i = 0; i < m_listTasks.GetItemCount(); ++i) {
		m_listTasks.SetCheck(i, FALSE);
	}
	RefreshTaskDisplay();
}
