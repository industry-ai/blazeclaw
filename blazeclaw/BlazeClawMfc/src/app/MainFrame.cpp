#include "pch.h"
#include "MainFrame.h"

#include "BlazeClawMfcApp.h"

namespace {
constexpr UINT kIdUiParityActionFormProbe = 0x8101;
constexpr UINT kIdUiParityAdminSnapshot = 0x8102;
constexpr UINT kIdUiParityAdminPolicyGet = 0x8103;
constexpr UINT kIdUiParityAdminConfigAgent = 0x8104;
constexpr UINT kIdUiParitySessionList = 0x8105;
constexpr UINT kIdUiParitySessionActivate = 0x8106;
constexpr UINT kIdUiParityRuntimeStatus = 0x8107;
constexpr UINT kIdUiParityDesktopStatus = 0x8108;
constexpr UINT kIdUiParityDesktopWebStatus = 0x8109;
constexpr UINT kIdUiParitySkillsStatus = 0x8110;
constexpr UINT kIdUiParitySkillsList = 0x8111;
constexpr UINT kIdUiParitySkillsInfo = 0x8112;
constexpr UINT kIdUiParitySkillsCheck = 0x8113;
constexpr UINT kIdUiParitySkillsDiagnostics = 0x8114;
constexpr UINT kIdUiParitySkillsInstallOptions = 0x8115;
constexpr UINT kIdUiParitySkillsScanStatus = 0x8116;
constexpr UINT kIdUiParityOperatorDiagnosticsReport = 0x8117;
constexpr UINT kIdUiParityOperatorPromotionReadiness = 0x8118;

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
  ON_COMMAND(kIdUiParityAdminPolicyGet, &CMainFrame::OnUiParityAdminPolicyGet)
  ON_COMMAND(kIdUiParityAdminConfigAgent, &CMainFrame::OnUiParityAdminConfigAgent)
  ON_COMMAND(kIdUiParitySessionList, &CMainFrame::OnUiParitySessionList)
  ON_COMMAND(kIdUiParitySessionActivate, &CMainFrame::OnUiParitySessionActivate)
  ON_COMMAND(kIdUiParityRuntimeStatus, &CMainFrame::OnUiParityRuntimeStatus)
  ON_COMMAND(kIdUiParityDesktopStatus, &CMainFrame::OnUiParityDesktopStatus)
  ON_COMMAND(kIdUiParityDesktopWebStatus, &CMainFrame::OnUiParityDesktopWebStatus)
  ON_COMMAND(kIdUiParitySkillsStatus, &CMainFrame::OnUiParitySkillsStatus)
  ON_COMMAND(kIdUiParitySkillsList, &CMainFrame::OnUiParitySkillsList)
  ON_COMMAND(kIdUiParitySkillsInfo, &CMainFrame::OnUiParitySkillsInfo)
  ON_COMMAND(kIdUiParitySkillsCheck, &CMainFrame::OnUiParitySkillsCheck)
  ON_COMMAND(kIdUiParitySkillsDiagnostics, &CMainFrame::OnUiParitySkillsDiagnostics)
  ON_COMMAND(kIdUiParitySkillsInstallOptions, &CMainFrame::OnUiParitySkillsInstallOptions)
  ON_COMMAND(kIdUiParitySkillsScanStatus, &CMainFrame::OnUiParitySkillsScanStatus)
  ON_COMMAND(kIdUiParityOperatorDiagnosticsReport, &CMainFrame::OnUiParityOperatorDiagnosticsReport)
  ON_COMMAND(kIdUiParityOperatorPromotionReadiness, &CMainFrame::OnUiParityOperatorPromotionReadiness)
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
      kIdUiParityAdminPolicyGet,
      _T("Admin Policy Get"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityAdminConfigAgent,
      _T("Admin Config Agent"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySessionList,
      _T("Session List"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySessionActivate,
      _T("Session Activate"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityRuntimeStatus,
      _T("Runtime Status"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityDesktopStatus,
      _T("Desktop Status"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityDesktopWebStatus,
      _T("Desktop Web Status"));
  m_parityMenu.AppendMenu(MF_SEPARATOR);
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySkillsStatus,
      _T("Skills Status"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySkillsList,
      _T("Skills List"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySkillsInfo,
      _T("Skills Info"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySkillsCheck,
      _T("Skills Check"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySkillsDiagnostics,
      _T("Skills Diagnostics"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySkillsInstallOptions,
      _T("Skills Install Options"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParitySkillsScanStatus,
      _T("Skills Scan Status"));
  m_parityMenu.AppendMenu(MF_SEPARATOR);
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityOperatorDiagnosticsReport,
      _T("Operator Diagnostics Report"));
  m_parityMenu.AppendMenu(
      MF_STRING,
      kIdUiParityOperatorPromotionReadiness,
      _T("Operator Promotion Readiness"));
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

void CMainFrame::OnUiParityAdminPolicyGet() {
  ShowParityResult(
      L"Admin Policy Get",
      "gateway.transport.policy.get");
}

void CMainFrame::OnUiParityAdminConfigAgent() {
  ShowParityResult(
      L"Admin Config Agent",
      "gateway.config.getSection",
      std::optional<std::string>("{\"section\":\"agent\"}"));
}

void CMainFrame::OnUiParitySessionList() {
  ShowParityResult(
      L"Session List",
      "gateway.session.list",
      std::optional<std::string>("{\"active\":true}"));
}

void CMainFrame::OnUiParitySessionActivate() {
  ShowParityResult(
      L"Session Activate",
      "gateway.sessions.activate",
      std::optional<std::string>("{\"sessionId\":\"main\"}"));
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

void CMainFrame::OnUiParityDesktopWebStatus() {
  ShowParityResult(
      L"Desktop Web Status",
      "gateway.platform.web.status");
}

void CMainFrame::OnUiParitySkillsStatus() {
  ShowParityResult(
      L"Skills Status",
      "gateway.skills.status");
}

void CMainFrame::OnUiParitySkillsList() {
  ShowParityResult(
      L"Skills List",
      "gateway.skills.list",
      std::optional<std::string>("{\"includeInvalid\":true}"));
}

void CMainFrame::OnUiParitySkillsInfo() {
  ShowParityResult(
      L"Skills Info",
      "gateway.skills.info",
      std::optional<std::string>("{\"skill\":\"install-node\"}"));
}

void CMainFrame::OnUiParitySkillsCheck() {
  ShowParityResult(
      L"Skills Check",
      "gateway.skills.check");
}

void CMainFrame::OnUiParitySkillsDiagnostics() {
  ShowParityResult(
      L"Skills Diagnostics",
      "gateway.skills.diagnostics");
}

void CMainFrame::OnUiParitySkillsInstallOptions() {
  ShowParityResult(
      L"Skills Install Options",
      "gateway.skills.install.options");
}

void CMainFrame::OnUiParitySkillsScanStatus() {
  ShowParityResult(
      L"Skills Scan Status",
      "gateway.skills.scan.status");
}

void CMainFrame::OnUiParityOperatorDiagnosticsReport() {
  const auto* app =
      dynamic_cast<CBlazeClawMfcApp*>(AfxGetApp());
  if (app == nullptr) {
    AfxMessageBox(_T("App context unavailable."));
    return;
  }

  const std::string report =
      app->Services().BuildOperatorDiagnosticsReport();
  const std::wstring message =
      L"Operator Diagnostics Report\n\n" + ToWide(report);

  AfxMessageBox(
      message.c_str(),
      MB_OK | MB_ICONINFORMATION,
      0);
}

void CMainFrame::OnUiParityOperatorPromotionReadiness() {
  const auto* app =
      dynamic_cast<CBlazeClawMfcApp*>(AfxGetApp());
  if (app == nullptr) {
    AfxMessageBox(_T("App context unavailable."));
    return;
  }

  const auto& registry = app->Services().Registry();
  std::size_t implemented = 0;
  std::size_t planned = 0;
  std::size_t inProgress = 0;

  for (const auto& feature : registry.Features()) {
    if (feature.state == blazeclaw::core::FeatureState::Implemented) {
      ++implemented;
      continue;
    }

    if (feature.state == blazeclaw::core::FeatureState::InProgress) {
      ++inProgress;
      continue;
    }

    ++planned;
  }

  const bool promotionReady =
      app->Services().IsRunning() &&
      inProgress == 0 &&
      planned == 0;

  std::wstring message = L"Promotion Readiness\n\n";
  message += L"Runtime Running: ";
  message += app->Services().IsRunning() ? L"yes" : L"no";
  message += L"\nImplemented Features: ";
  message += std::to_wstring(implemented);
  message += L"\nIn-Progress Features: ";
  message += std::to_wstring(inProgress);
  message += L"\nPlanned Features: ";
  message += std::to_wstring(planned);
  message += L"\n\nPromotion Ready: ";
  message += promotionReady ? L"yes" : L"no";

  AfxMessageBox(
      message.c_str(),
      MB_OK | MB_ICONINFORMATION,
      0);
}
