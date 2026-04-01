#include "pch.h"
#include "GatewayJsonUtils.h"

namespace blazeclaw::gateway::json {

namespace {

int ParseHexDigit(const char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }

    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }

    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }

    return -1;
}

}

std::size_t SkipWhitespace(const std::string& text, std::size_t index) {
    while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
        ++index;
    }

    return index;
}

std::string Trim(const std::string& value) {
    std::size_t start = 0;
    std::size_t end = value.size();

    while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
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
            case 'u': {
                if (index + 4 > text.size()) {
                    return false;
                }

                std::uint32_t codepoint = 0;
                for (int i = 0; i < 4; ++i) {
                    const int hex = ParseHexDigit(text[index + i]);
                    if (hex < 0) {
                        return false;
                    }

                    codepoint = (codepoint << 4) | static_cast<std::uint32_t>(hex);
                }

                index += 4;
                outValue.push_back(
                    static_cast<char>(codepoint <= 0x7F ? codepoint : '?'));
                break;
            }
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
    const char opener = text[index];

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
            }
            else if (ch == closer) {
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

bool FindBoolField(const std::string& text, const std::string& fieldName, bool& outValue) {
    std::string raw;
    if (!FindRawField(text, fieldName, raw)) {
        return false;
    }

    raw = Trim(raw);
    if (raw == "true") {
        outValue = true;
        return true;
    }

    if (raw == "false") {
        outValue = false;
        return true;
    }

    return false;
}

bool FindUInt64Field(const std::string& text, const std::string& fieldName, std::uint64_t& outValue) {
    std::string raw;
    if (!FindRawField(text, fieldName, raw)) {
        return false;
    }

    raw = Trim(raw);
    if (raw.empty()) {
        return false;
    }

    try {
        std::size_t consumed = 0;
        const std::uint64_t parsed = std::stoull(raw, &consumed);
        if (consumed != raw.size()) {
            return false;
        }

        outValue = parsed;
        return true;
    }
    catch (...) {
        return false;
    }
}

bool IsJsonObjectShape(const std::string& value) {
    const std::string trimmed = Trim(value);
    return trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}';
}

bool IsFieldValueType(const std::string& text, const std::string& fieldName, char expectedFirstChar) {
    const std::string token = "\"" + fieldName + "\"";
    const std::size_t tokenPos = text.find(token);
    if (tokenPos == std::string::npos) {
        return false;
    }

    std::size_t valuePos = text.find(':', tokenPos);
    if (valuePos == std::string::npos) {
        return false;
    }

    ++valuePos;
    while (valuePos < text.size() && std::isspace(static_cast<unsigned char>(text[valuePos])) != 0) {
        ++valuePos;
    }

    if (valuePos >= text.size()) {
        return false;
    }

    return text[valuePos] == expectedFirstChar;
}

} // namespace blazeclaw::gateway::json
