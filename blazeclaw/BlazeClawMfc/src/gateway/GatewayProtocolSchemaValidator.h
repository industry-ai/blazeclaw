#pragma once

#include "GatewayProtocolModels.h"

namespace blazeclaw::gateway::protocol {

struct SchemaValidationIssue {
  std::string code;
  std::string message;
};

class GatewayProtocolSchemaValidator {
public:
  [[nodiscard]] static bool ValidateRequest(
      const RequestFrame& request,
      SchemaValidationIssue& issue);
  [[nodiscard]] static bool ValidateResponseForMethod(
      const std::string& method,
      const ResponseFrame& response,
      SchemaValidationIssue& issue);
  [[nodiscard]] static bool ValidateEvent(
      const EventFrame& event,
      SchemaValidationIssue& issue);
};

} // namespace blazeclaw::gateway::protocol
