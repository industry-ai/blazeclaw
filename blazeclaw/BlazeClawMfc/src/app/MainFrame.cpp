#include "pch.h"
#include "MainFrame.h"

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
END_MESSAGE_MAP()

CMainFrame::CMainFrame() {
  Create(nullptr, _T("BlazeClaw - OpenClaw C++ Port"), WS_OVERLAPPEDWINDOW, CRect(100, 100, 1280, 800));
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CFrameWnd::OnCreate(lpCreateStruct) == -1) {
    return -1;
  }

  SetWindowText(_T("BlazeClaw - Service Console"));
  return 0;
}
