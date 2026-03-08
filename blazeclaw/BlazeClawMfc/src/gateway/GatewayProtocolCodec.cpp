#include "pch.h"
#include "GatewayProtocolCodec.h"

namespace blazeclaw::gateway::protocol {
namespace {

std::size_t SkipWhitespace(const std::string& text, std::size_t index) {
  while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
    ++index;
  }

  return index;
}

bool ParseJsonStringAt(const std::string& text, std::size_t& index, std::string& outValue) {
  if (index >= text.size() || text[index] != '"') {
    return false;
  }

  ++index;
  outValue.clear();

  while (index < text.size()) {
    const char ch = text[index++];
    if (ch == '"') {
      return true;
    }

    if (ch == '\\') {
      if (index >= text.size()) {
        return false;
      }

      const char escaped = text[index++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          outValue.push_back(escaped);
          break;
        case 'b':
          outValue.push_back('\b');
          break;
        case 'f':
          outValue.push_back('\f');
          break;
        case 'n':
          outValue.push_back('\n');
          break;
        case 'r':
          outValue.push_back('\r');
          break;
        case 't':
          outValue.push_back('\t');
          break;
        default:
          return false;
      }

      continue;
    }

    outValue.push_back(ch);
  }

  return false;
}

bool FindStringField(const std::string& text, const std::string& fieldName, std::string& outValue) {
  const std::string token = "\"" + fieldName + "\"";
  const std::size_t keyPos = text.find(token);
  if (keyPos == std::string::npos) {
    return false;
  }

  std::size_t index = keyPos + token.size();
  index = SkipWhitespace(text, index);
  if (index >= text.size() || text[index] != ':') {
    return false;
  }

  ++index;
  index = SkipWhitespace(text, index);

  return ParseJsonStringAt(text, index, outValue);
}

bool FindRawField(const std::string& text, const std::string& fieldName, std::string& outValue) {
  const std::string token = "\"" + fieldName + "\"";
  const std::size_t keyPos = text.find(token);
  if (keyPos == std::string::npos) {
    return false;
  }

  std::size_t index = keyPos + token.size();
  index = SkipWhitespace(text, index);
  if (index >= text.size() || text[index] != ':') {
    return false;
  }

  ++index;
  index = SkipWhitespace(text, index);
  if (index >= text.size()) {
    return false;
  }

  const std::size_t start = index;
  char opener = text[index];

  if (opener == '{' || opener == '[') {
    const char closer = opener == '{' ? '}' : ']';
    int depth = 0;
    bool inString = false;

    for (; index < text.size(); ++index) {
      const char ch = text[index];
      if (inString) {
        if (ch == '\\') {
          ++index;
          continue;
        }

        if (ch == '"') {
          inString = false;
        }

        continue;
      }

      if (ch == '"') {
        inString = true;
        continue;
      }

      if (ch == opener) {
        ++depth;
      } else if (ch == closer) {
        --depth;
        if (depth == 0) {
          outValue = text.substr(start, (index - start) + 1);
          return true;
        }
      }
    }

    return false;
  }

  if (opener == '"') {
    std::string parsed;
    if (!ParseJsonStringAt(text, index, parsed)) {
      return false;
    }

    outValue = "\"" + parsed + "\"";
    return true;
  }

  while (index < text.size() && text[index] != ',' && text[index] != '}') {
    ++index;
  }

  outValue = text.substr(start, index - start);
  return true;
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
