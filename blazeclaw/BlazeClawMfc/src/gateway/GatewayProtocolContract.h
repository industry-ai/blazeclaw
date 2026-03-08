#pragma once

#include "GatewayProtocolJson.h"

namespace blazeclaw::gateway::protocol {

class GatewayProtocolContract {
public:
  [[nodiscard]] static bool ValidateFixtureParity(const std::string& fixtureRoot, std::string& error);
};

} // namespace blazeclaw::gateway::protocol
