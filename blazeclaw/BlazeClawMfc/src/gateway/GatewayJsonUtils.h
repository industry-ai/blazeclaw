#pragma once

#include "../app/pch.h"

namespace blazeclaw::gateway::json {

enum class JsonValueKind {
    String,
    Number,
    Boolean,
    Object,
    Array,
    Null,
    Unknown,
};

struct FieldValueView {
    std::string value;
    JsonValueKind kind = JsonValueKind::Unknown;
};

std::size_t SkipWhitespace(const std::string& text, std::size_t index);
std::string Trim(const std::string& value);
bool ParseJsonStringAt(const std::string& text, std::size_t& index, std::string& outValue);
bool FindStringField(const std::string& text, const std::string& fieldName, std::string& outValue);
bool FindRawField(const std::string& text, const std::string& fieldName, std::string& outValue);
bool FindBoolField(const std::string& text, const std::string& fieldName, bool& outValue);
bool FindUInt64Field(const std::string& text, const std::string& fieldName, std::uint64_t& outValue);
bool IsJsonObjectShape(const std::string& value);
bool IsFieldValueType(const std::string& text, const std::string& fieldName, char expectedFirstChar);

} // namespace blazeclaw::gateway::json
