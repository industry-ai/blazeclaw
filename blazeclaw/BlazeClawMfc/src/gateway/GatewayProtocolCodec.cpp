#include "pch.h"
#include "GatewayProtocolCodec.h"
#include "GatewayJsonUtils.h"

namespace blazeclaw::gateway::protocol {
namespace {

bool FindStringField(
    const std::string& text,
    const std::string& fieldName,
    std::string& outValue) {
  return json::FindStringField(text, fieldName, outValue);
}

bool FindRawField(
    const std::string& text,
    const std::string& fieldName,
    std::string& outValue) {
  return json::FindRawField(text, fieldName, outValue);
}

} // namespace

bool TryDecodeRequestFrame(const std::string& inboundJson, RequestFrame& outFrame, std::string& error) {
  std::string type;
  if (!FindStringField(inboundJson, "type", type)) {
    error = "Missing required field: type";
    return false;
  }

  if (type != "req") {
    error = "Unsupported frame type for request decode: " + type;
    return false;
  }

  if (!FindStringField(inboundJson, "id", outFrame.id)) {
    error = "Missing required field: id";
    return false;
  }

  if (!FindStringField(inboundJson, "method", outFrame.method)) {
    error = "Missing required field: method";
    return false;
  }

  std::string params;
  if (FindRawField(inboundJson, "params", params)) {
    outFrame.paramsJson = params;
  } else {
    outFrame.paramsJson = std::nullopt;
  }

  error.clear();
  return true;
}

std::string EncodeResponseFrame(const ResponseFrame& frame) {
  return SerializeResponseFrame(frame);
}

std::string EncodeEventFrame(const EventFrame& frame) {
  return SerializeEventFrame(frame);
}

} // namespace blazeclaw::gateway::protocol
