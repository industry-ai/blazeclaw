#pragma once

#include "resource.h"
#include "framework.h"
#include <vector>
#include <string>
#include <optional>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace blazeclaw::cron {
	class CronOpsService;
}

class CCronTasksDialog : public CDialogEx {
	DECLARE_DYNAMIC(CCronTasksDialog)

public:
	explicit CCronTasksDialog(CWnd* pParent = nullptr);
	~CCronTasksDialog() override;

	struct TaskItem {
		std::string id;
		std::string name;
		std::string description;
		std::string schedule;
		bool        enabled = true;
		std::string nextRunAt;
		std::string lastRunAt;
		std::string status;
	};

	const std::vector<std::string>& GetDeletedTaskIds() const { return m_deletedIds; }

protected:
	enum { IDD = IDD_CRON_TASKS_DIALOG };

	BOOL OnInitDialog() override;
	void DoDataExchange(CDataExchange* pDX) override;

	DECLARE_MESSAGE_MAP()

private:
	void LoadTasks();
	void PopulateList();
	void RefreshTaskDisplay();
	std::string FormatTimestamp(std::int64_t ms) const;
	std::string ExtractScheduleText(const nlohmann::json& job) const;
	afx_msg void OnSelectAll();
	afx_msg void OnDeselectAll();
	afx_msg void OnDeleteSelected();
	afx_msg void OnItemChanged(NMHDR* pNMHDR, LRESULT* pResult);
	afx_msg LRESULT OnNcHitTest(CPoint point);

	CListCtrl       m_listTasks;
	CStatic         m_staticCount;
	std::vector<TaskItem> m_tasks;
	std::vector<std::string> m_deletedIds;
	std::string     m_scheduleDisplayFormat;
};
