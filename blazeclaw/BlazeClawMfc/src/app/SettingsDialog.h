#pragma once

#include "pch.h"
#include "Resource.h"
#include <vector>
#include <string>

class CSettingsDialog : public CDialogEx {
	DECLARE_DYNAMIC(CSettingsDialog)

public:
	explicit CSettingsDialog(CWnd* pParent = nullptr);
	~CSettingsDialog() override;

	// Model selection state
	struct ModelItem {
		std::string id;
		std::string name;
		std::string provider;
		bool        enabled = false;
	};

	const std::vector<ModelItem>& GetEnabledModels() const { return m_models; }

protected:
	enum { IDD = IDD_SETTINGS_DIALOG };

	BOOL OnInitDialog() override;
	void DoDataExchange(CDataExchange* pDX) override;
	void OnOK() override;

	DECLARE_MESSAGE_MAP()

private:
	void LoadModels();
	void UpdateModelCount();
	void SelectAll(BOOL select);

	CListCtrl       m_listModels;
	CStatic         m_staticCount;
	std::vector<ModelItem> m_models;

	afx_msg void OnSelectAll();
	afx_msg void OnDeselectAll();
};
