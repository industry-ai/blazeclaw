#include "pch.h"
#include "GatewayProtocolCodec.h"
#include "GatewayJsonUtils.h"

namespace blazeclaw::gateway::protocol {
namespace {

bool FindTopLevelRawField(
    const std::string& text,
    const std::string& fieldName,
    std::string& outValue) {
  std::size_t index = json::SkipWhitespace(text, 0);
  if (index >= text.size() || text[index] != '{') {
    return false;
  }

  ++index;
  while (index < text.size()) {
    index = json::SkipWhitespace(text, index);
    if (index >= text.size()) {
      return false;
    }

    if (text[index] == '}') {
      return false;
    }

    std::string key;
    if (!json::ParseJsonStringAt(text, index, key)) {
      return false;
    }

    index = json::SkipWhitespace(text, index);
    if (index >= text.size() || text[index] != ':') {
      return false;
    }

    ++index;
    index = json::SkipWhitespace(text, index);
    if (index >= text.size()) {
      return false;
    }

    const std::size_t valueStart = index;
    bool inString = false;
    bool escaped = false;
    int objectDepth = 0;
    int arrayDepth = 0;

    while (index < text.size()) {
      const char ch = text[index];
      if (inString) {
        if (escaped) {
          escaped = false;
        } else if (ch == '\\') {
          escaped = true;
        } else if (ch == '"') {
          inString = false;
        }

        ++index;
        continue;
      }

      if (ch == '"') {
        inString = true;
        ++index;
        continue;
      }

      if (ch == '{') {
        ++objectDepth;
        ++index;
        continue;
      }

      if (ch == '}') {
        if (objectDepth > 0) {
          --objectDepth;
          ++index;
          continue;
        }

        break;
      }

      if (ch == '[') {
        ++arrayDepth;
        ++index;
        continue;
      }

      if (ch == ']') {
        if (arrayDepth > 0) {
          --arrayDepth;
          ++index;
          continue;
        }

        return false;
      }

      if (ch == ',' && objectDepth == 0 && arrayDepth == 0) {
        break;
      }

      ++index;
    }

    const std::size_t valueEnd = index;
    if (key == fieldName) {
      outValue = json::Trim(text.substr(valueStart, valueEnd - valueStart));
      return true;
    }

    index = json::SkipWhitespace(text, index);
    if (index < text.size() && text[index] == ',') {
      ++index;
      continue;
    }

    if (index < text.size() && text[index] == '}') {
      return false;
    }
  }

  return false;
}

bool FindTopLevelStringField(
    const std::string& text,
    const std::string& fieldName,
    std::string& outValue) {
  std::string raw;
  if (!FindTopLevelRawField(text, fieldName, raw)) {
    return false;
  }

  std::size_t index = 0;
  index = json::SkipWhitespace(raw, index);
  return json::ParseJsonStringAt(raw, index, outValue);
}

} // namespace

bool TryDecodeRequestFrame(const std::string& inboundJson, RequestFrame& outFrame, std::string& error) {
  std::string type;
  if (!FindTopLevelStringField(inboundJson, "type", type)) {
    error = "Missing required field: type";
    return false;
  }

  if (type != "req") {
    error = "Unsupported frame type for request decode: " + type;
    return false;
  }

  if (!FindTopLevelStringField(inboundJson, "id", outFrame.id)) {
    error = "Missing required field: id";
    return false;
  }

  if (!FindTopLevelStringField(inboundJson, "method", outFrame.method)) {
    error = "Missing required field: method";
    return false;
  }

  std::string params;
  if (FindTopLevelRawField(inboundJson, "params", params)) {
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
