#pragma once

#include "pch.h"

namespace blazeclaw::gateway {

struct ToolCatalogEntry {
  std::string id;
  std::string label;
  std::string category;
  bool enabled = true;
};

struct ToolPreviewResult {
  std::string tool;
  bool allowed = false;
  std::string reason;
};

class GatewayToolRegistry {
public:
  GatewayToolRegistry();

  std::vector<ToolCatalogEntry> List() const;
  ToolPreviewResult Preview(const std::string& requestedTool) const;

private:
  std::unordered_map<std::string, ToolCatalogEntry> m_tools;
};

} // namespace blazeclaw::gateway
