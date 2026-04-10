#pragma once

#include "GatewayProtocolSchemaValidator.h"

#include <cstddef>
#include <initializer_list>
#include <string>

namespace blazeclaw::gateway::protocol {

	std::string Trim(const std::string& value);
	bool IsJsonObjectShape(const std::string& value);
	void SetIssue(SchemaValidationIssue& issue, const std::string& code, const std::string& message);
	bool ContainsFieldToken(const std::string& json, const std::string& fieldName, std::size_t& tokenPos);
	bool IsFieldValueType(const std::string& json, const std::string& fieldName, char expectedFirstChar);
	bool IsFieldBoolean(const std::string& json, const std::string& fieldName);
	bool IsFieldNumber(const std::string& json, const std::string& fieldName);
	bool IsArrayFieldExplicitlyEmpty(const std::string& json, const std::string& fieldName);
	bool PayloadContainsAllFieldTokens(const std::string& json, std::initializer_list<const char*> fieldNames);
	bool PayloadContainsAllStringValues(const std::string& json, std::initializer_list<const char*> values);
	bool ValidateResponseEnvelope(const ResponseFrame& response, SchemaValidationIssue& issue);
	bool ValidateTaskDeltaPayloadShape(const std::string& payloadJson, SchemaValidationIssue& issue);

	template <std::size_t N>
	bool PayloadContainsAllFieldTokens(const std::string& json, const char* const (&fieldNames)[N]) {
		for (const char* fieldName : fieldNames) {
			std::size_t tokenPos = 0;
			if (!ContainsFieldToken(json, fieldName, tokenPos)) {
				return false;
			}
		}

		return true;
	}

	template <std::size_t N>
	bool PayloadContainsAllStringValues(const std::string& json, const char* const (&values)[N]) {
		for (const char* value : values) {
			const std::string token = "\"" + std::string(value) + "\"";
			if (json.find(token) == std::string::npos) {
				return false;
			}
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
