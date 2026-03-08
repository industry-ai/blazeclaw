#pragma once

#include "../config/ConfigLoader.h"
#include "../core/ServiceManager.h"

class CBlazeClawMfcApp final : public CWinApp {
public:
  BOOL InitInstance() override;
  int ExitInstance() override;

private:
  blazeclaw::config::ConfigLoader m_configLoader;
  blazeclaw::config::AppConfig m_config;
  blazeclaw::core::ServiceManager m_serviceManager;
};
