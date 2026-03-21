#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"
#include "GatewayProtocolSchemaValidator.Internal.h"

namespace blazeclaw::gateway::protocol {

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

bool IsJsonObjectShape(const std::string& value) {
    const std::string trimmed = Trim(value);
    return trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}';
}

void SetIssue(SchemaValidationIssue& issue, const std::string& code, const std::string& message) {
    issue.code = code;
    issue.message = message;
}

bool ContainsFieldToken(const std::string& json, const std::string& fieldName, std::size_t& tokenPos) {
    const std::string token = "\"" + fieldName + "\"";
    tokenPos = json.find(token);
    return tokenPos != std::string::npos;
}

bool IsFieldValueType(const std::string& json, const std::string& fieldName, char expectedFirstChar) {
    std::size_t tokenPos = 0;
    if (!ContainsFieldToken(json, fieldName, tokenPos)) {
        return false;
    }

    std::size_t valuePos = json.find(':', tokenPos);
    if (valuePos == std::string::npos) {
        return false;
    }

    ++valuePos;
    while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
        ++valuePos;
    }

    if (valuePos >= json.size()) {
        return false;
    }

    return json[valuePos] == expectedFirstChar;
}

bool IsFieldBoolean(const std::string& json, const std::string& fieldName) {
    std::size_t tokenPos = 0;
    if (!ContainsFieldToken(json, fieldName, tokenPos)) {
        return false;
    }

    std::size_t valuePos = json.find(':', tokenPos);
    if (valuePos == std::string::npos) {
        return false;
    }

    ++valuePos;
    while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
        ++valuePos;
    }

    return json.compare(valuePos, 4, "true") == 0 || json.compare(valuePos, 5, "false") == 0;
}

bool IsFieldNumber(const std::string& json, const std::string& fieldName) {
    std::size_t tokenPos = 0;
    if (!ContainsFieldToken(json, fieldName, tokenPos)) {
        return false;
    }

    std::size_t valuePos = json.find(':', tokenPos);
    if (valuePos == std::string::npos) {
        return false;
    }

    ++valuePos;
    while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
        ++valuePos;
    }

    if (valuePos >= json.size()) {
        return false;
    }

    const char ch = json[valuePos];
    return std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '-';
}

bool IsArrayFieldExplicitlyEmpty(const std::string& json, const std::string& fieldName) {
    std::size_t tokenPos = 0;
    if (!ContainsFieldToken(json, fieldName, tokenPos)) {
        return false;
    }

    std::size_t valuePos = json.find(':', tokenPos);
    if (valuePos == std::string::npos) {
        return false;
    }

    ++valuePos;
    while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
        ++valuePos;
    }

    if (valuePos >= json.size() || json[valuePos] != '[') {
        return false;
    }

    ++valuePos;
    while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
        ++valuePos;
    }

    return valuePos < json.size() && json[valuePos] == ']';
}

bool PayloadContainsAllFieldTokens(const std::string& json, std::initializer_list<const char*> fieldNames) {
    for (const char* fieldName : fieldNames) {
        std::size_t tokenPos = 0;
        if (!ContainsFieldToken(json, fieldName, tokenPos)) {
            return false;
        }
    }

    return true;
}

bool PayloadContainsAllStringValues(const std::string& json, std::initializer_list<const char*> values) {
    for (const char* value : values) {
        const std::string token = "\"" + std::string(value) + "\"";
        if (json.find(token) == std::string::npos) {
            return false;
        }
    }

    return true;
}

bool ValidateResponseEnvelope(const ResponseFrame& response, SchemaValidationIssue& issue) {
    if (response.id.empty()) {
        SetIssue(issue, "schema_invalid_response", "Response `id` is required.");
        return false;
    }

    if (!response.ok) {
        SetIssue(issue, "schema_invalid_response", "Expected success response (`ok=true`) for method fixture validation.");
        return false;
    }

    if (!response.payloadJson.has_value() || !IsJsonObjectShape(response.payloadJson.value())) {
        SetIssue(issue, "schema_invalid_response", "Response `payload` must be a JSON object.");
        return false;
    }

    return true;
}

} // namespace blazeclaw::gateway::protocol
