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

 private:
  void ShowParityResult(
      const wchar_t* title,
      const std::string& method,
      const std::optional<std::string>& paramsJson = std::nullopt);

  CMenu m_menuBar;
  CMenu m_parityMenu;

  DECLARE_MESSAGE_MAP()
};
