#pragma once

#include "pch.h"

class CMainFrame final : public CFrameWnd {
public:
  CMainFrame();

protected:
  afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);

  DECLARE_MESSAGE_MAP()
};
