#include "pch.h"
#include "MainFrame.h"

#include "BlazeClawMfcApp.h"

namespace {
constexpr UINT kIdUiParityActionFormProbe = 0x8101;
constexpr UINT kIdUiParityAdminSnapshot = 0x8102;
constexpr UINT kIdUiParitySessionList = 0x8103;
constexpr UINT kIdUiParityRuntimeStatus = 0x8104;
constexpr UINT kIdUiParityDesktopStatus = 0x8105;

std::wstring ToWide(const std::string& value) {
  std::wstring output;
  output.reserve(value.size());

  for (const char ch : value) {
    output.push_back(static_cast<wchar_t>(
        static_cast<unsigned char>(ch)));
  }

  return output;
}
} // namespace

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
  ON_WM_CREATE()
  ON_COMMAND(kIdUiParityActionFormProbe, &CMainFrame::OnUiParityActionFormProbe)
  ON_COMMAND(kIdUiParityAdminSnapshot, &CMainFrame::OnUiParityAdminSnapshot)
  ON_COMMAND(kIdUiParitySessionList, &CMainFrame::OnUiParitySessionList)
  ON_COMMAND(kIdUiParityRuntimeStatus, &CMainFrame::OnUiParityRuntimeStatus)
  ON_COMMAND(kIdUiParityDesktopStatus, &CMainFrame::OnUiParityDesktopStatus)
END_MESSAGE_MAP()

CMainFrame::CMainFrame() {
  Create(nullptr, _T("BlazeClaw - OpenClaw C++ Port"), WS_OVERLAPPEDWINDOW, CRect(100, 100, 1280, 800));
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CFrameWnd::OnCreate(lpCreateStruct) == -1) {
    return -1;
  }

  m_menuBar.CreateMenu();
  m_parityMenu.CreatePopupMenu();
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityActionFormProbe,
      _T("Action/Form Probe"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityAdminSnapshot,
      _T("Admin Snapshot"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySessionList,
      _T("Session List"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityRuntimeStatus,
      _T("Runtime Status"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityDesktopStatus,
      _T("Desktop Status"));
  m_menuBar.AppendMenu(
      MF_POPUP,
      reinterpret_cast<UINT_PTR>(m_parityMenu.GetSafeHmenu()),
      _T("Parity"));
  SetMenu(&m_menuBar);

  SetWindowText(_T("BlazeClaw - Service Console"));
  return 0;
}

void CMainFrame::ShowParityResult(
    const wchar_t* title,
    const std::string& method,
    const std::optional<std::string>& paramsJson) {
  const auto* app =
      dynamic_cast<CBlazeClawMfcApp*>(AfxGetApp());
  if (app == nullptr) {
    AfxMessageBox(_T("App context unavailable."));
    return;
  }

  const std::string result =
      app->Services().InvokeGatewayMethod(method, paramsJson);
  const std::wstring body =
      L"Method: " + ToWide(method) +
      L"\n\nResult:\n" + ToWide(result);

  AfxMessageBox(
      body.c_str(),
      MB_OK | MB_ICONINFORMATION,
      0);
}

void CMainFrame::OnUiParityActionFormProbe() {
  ShowParityResult(
      L"Action/Form Probe",
      "gateway.tools.call.preview",
      std::optional<std::string>(
          "{\"tool\":\"chat.send\",\"args\":{\"message\":\"ui-probe\"}}"));
}

void CMainFrame::OnUiParityAdminSnapshot() {
  ShowParityResult(
      L"Admin Snapshot",
      "gateway.config.snapshot");
}

void CMainFrame::OnUiParitySessionList() {
  ShowParityResult(
      L"Session List",
      "gateway.session.list",
      std::optional<std::string>("{\"active\":true}"));
}

void CMainFrame::OnUiParityRuntimeStatus() {
  ShowParityResult(
      L"Runtime Status",
      "gateway.runtime.orchestration.status");
}

void CMainFrame::OnUiParityDesktopStatus() {
  ShowParityResult(
      L"Desktop Status",
      "gateway.platform.cli.status");
}
