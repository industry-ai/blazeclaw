#pragma once

#include "pch.h"

class CMainFrame final : public CFrameWnd {
public:
  CMainFrame();

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnUiParityActionFormProbe();
  afx_msg void OnUiParityAdminSnapshot();
  afx_msg void OnUiParityAdminPolicyGet();
  afx_msg void OnUiParityAdminConfigAgent();
  afx_msg void OnUiParitySessionList();
  afx_msg void OnUiParitySessionActivate();
  afx_msg void OnUiParityRuntimeStatus();
  afx_msg void OnUiParityDesktopStatus();
  afx_msg void OnUiParityDesktopWebStatus();
  afx_msg void OnUiParitySkillsStatus();
  afx_msg void OnUiParitySkillsList();
  afx_msg void OnUiParitySkillsInfo();
  afx_msg void OnUiParitySkillsCheck();
  afx_msg void OnUiParitySkillsDiagnostics();
  afx_msg void OnUiParitySkillsInstallOptions();
  afx_msg void OnUiParitySkillsScanStatus();
  afx_msg void OnUiParityOperatorDiagnosticsReport();
  afx_msg void OnUiParityOperatorPromotionReadiness();

 private:
  void ShowParityResult(
      const wchar_t* title,
      const std::string& method,
      const std::optional<std::string>& paramsJson = std::nullopt);

  CMenu m_menuBar;
  CMenu m_parityMenu;

  DECLARE_MESSAGE_MAP()
};
