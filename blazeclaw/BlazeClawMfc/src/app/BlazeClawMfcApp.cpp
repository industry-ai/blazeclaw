#include "pch.h"
#include "BlazeClawMfcApp.h"

#include "MainFrame.h"

namespace {
constexpr wchar_t kConfigPath[] = L"blazeclaw.conf";
}

CBlazeClawMfcApp theApp;

BOOL CBlazeClawMfcApp::InitInstance() {
  CWinApp::InitInstance();

  m_configLoader.LoadFromFile(kConfigPath, m_config);
  m_serviceManager.Start(m_config);

  auto* frame = new CMainFrame();
  m_pMainWnd = frame;

  frame->ShowWindow(SW_SHOW);
  frame->UpdateWindow();

  return TRUE;
}

int CBlazeClawMfcApp::ExitInstance() {
  m_serviceManager.Stop();
  return CWinApp::ExitInstance();
}
