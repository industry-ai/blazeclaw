#include "pch.h"
#include "GatewayProtocolJson.h"

namespace blazeclaw::gateway::protocol {
namespace {

std::string EscapeJson(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 8);

  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }

  return escaped;
}

void AppendJsonString(std::string& target, const std::string& key, const std::string& value, bool& first) {
  if (!first) {
    target += ",";
  }

  first = false;
  target += "\"" + key + "\":\"" + EscapeJson(value) + "\"";
}

void AppendJsonRaw(std::string& target, const std::string& key, const std::string& value, bool& first) {
  if (!first) {
    target += ",";
  }

  first = false;
  target += "\"" + key + "\":" + value;
}

void AppendJsonBool(std::string& target, const std::string& key, bool value, bool& first) {
  if (!first) {
    target += ",";
  }

  first = false;
  target += "\"" + key + "\":";
  target += value ? "true" : "false";
}

void AppendJsonUInt64(std::string& target, const std::string& key, std::uint64_t value, bool& first) {
  if (!first) {
    target += ",";
  }

  first = false;
  target += "\"" + key + "\":" + std::to_string(value);
}

std::string SerializeErrorShape(const ErrorShape& error) {
  std::string output = "{";
  bool first = true;

  AppendJsonString(output, "code", error.code, first);
  AppendJsonString(output, "message", error.message, first);

  if (error.detailsJson.has_value()) {
    AppendJsonRaw(output, "details", error.detailsJson.value(), first);
  }

  if (error.retryable.has_value()) {
    AppendJsonBool(output, "retryable", error.retryable.value(), first);
  }

  if (error.retryAfterMs.has_value()) {
    AppendJsonUInt64(output, "retryAfterMs", error.retryAfterMs.value(), first);
  }

  output += "}";
  return output;
}

} // namespace

std::string SerializeRequestFrame(const RequestFrame& frame) {
  std::string output = "{";
  bool first = true;

  AppendJsonString(output, "type", "req", first);
  AppendJsonString(output, "id", frame.id, first);
  AppendJsonString(output, "method", frame.method, first);

  if (frame.paramsJson.has_value()) {
    AppendJsonRaw(output, "params", frame.paramsJson.value(), first);
  }

  output += "}";
  return output;
}

std::string SerializeResponseFrame(const ResponseFrame& frame) {
  std::string output = "{";
  bool first = true;

  AppendJsonString(output, "type", "res", first);
  AppendJsonString(output, "id", frame.id, first);
  AppendJsonBool(output, "ok", frame.ok, first);

  if (frame.payloadJson.has_value()) {
    AppendJsonRaw(output, "payload", frame.payloadJson.value(), first);
  }

  if (frame.error.has_value()) {
    AppendJsonRaw(output, "error", SerializeErrorShape(frame.error.value()), first);
  }

  output += "}";
  return output;
}

std::string SerializeEventFrame(const EventFrame& frame) {
  std::string output = "{";
  bool first = true;

  AppendJsonString(output, "type", "event", first);
  AppendJsonString(output, "event", frame.eventName, first);

  if (frame.payloadJson.has_value()) {
    AppendJsonRaw(output, "payload", frame.payloadJson.value(), first);
  }

  if (frame.seq.has_value()) {
    AppendJsonUInt64(output, "seq", frame.seq.value(), first);
  }

  if (frame.stateVersion.has_value()) {
    AppendJsonUInt64(output, "stateVersion", frame.stateVersion.value(), first);
  }

  output += "}";
  return output;
}

} // namespace blazeclaw::gateway::protocol
