#pragma once

#include <array>

namespace blazeclaw::gateway::generated {

struct SchemaMethodRule {
    const char* name;
    const char* requestPolicyType;
    const char* responseShape;
    const char* stringIdField;
};

struct SchemaMethodPatternRule {
    const char* pattern;
    const char* requestPolicyType;
    const char* responseShape;
};

inline constexpr int kGatewaySchemaCatalogVersion = 1;

const std::array<SchemaMethodRule, 47>& GetSchemaMethodRules() noexcept;
const std::array<SchemaMethodPatternRule, 13>& GetSchemaMethodPatternRules() noexcept;

} // namespace blazeclaw::gateway::generated
