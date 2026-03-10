#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"

namespace blazeclaw::gateway::protocol {
	namespace {

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

		enum class JsonFieldKind {
			String,
			Number,
			Boolean,
			Object,
			Array,
			Null,
		};

		using ParsedObjectFieldKinds = std::unordered_map<std::string, JsonFieldKind>;

		std::size_t SkipWhitespace(const std::string& json, std::size_t position) {
			while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0) {
				++position;
			}

			return position;
		}

		bool TryConsumeJsonString(const std::string& json, std::size_t& position, std::string& value) {
			if (position >= json.size() || json[position] != '"') {
				return false;
			}

			++position;
			value.clear();
			while (position < json.size()) {
				const char ch = json[position++];
				if (ch == '\\') {
					if (position >= json.size()) {
						return false;
					}

					value.push_back(json[position++]);
					continue;
				}

				if (ch == '"') {
					return true;
				}

				value.push_back(ch);
			}

			return false;
		}

		bool TryConsumeBalancedComposite(
			const std::string& json,
			std::size_t& position,
			char openChar,
			char closeChar) {
			if (position >= json.size() || json[position] != openChar) {
				return false;
			}

			int depth = 0;
			bool inString = false;
			bool escaped = false;
			while (position < json.size()) {
				const char ch = json[position++];
				if (inString) {
					if (escaped) {
						escaped = false;
					}
					else if (ch == '\\') {
						escaped = true;
					}
					else if (ch == '"') {
						inString = false;
					}

					continue;
				}

				if (ch == '"') {
					inString = true;
					continue;
				}

				if (ch == openChar) {
					++depth;
				}
				else if (ch == closeChar) {
					--depth;
					if (depth == 0) {
						return true;
					}
				}
			}

			return false;
		}

		//bool TryParseTopLevelObjectFieldKinds(const std::string& json, ParsedObjectFieldKinds& kinds);

		//bool TryParseRequestParamsObject(
		//	const RequestFrame& request,
		//	SchemaValidationIssue& issue,
		//	const std::string& methodName,
		//	ParsedObjectFieldKinds& fieldKinds) {
		//	if (!request.paramsJson.has_value()) {
		//		fieldKinds.clear();
		//		return true;
		//	}

		//	if (!TryParseTopLevelObjectFieldKinds(request.paramsJson.value(), fieldKinds)) {
		//		SetIssue(
		//			issue,
		//			"schema_invalid_params",
		//			"Method `" + methodName + "` expects `params` to be a JSON object when provided.");
		//		return false;
		//	}

		//	return true;
		//}

		//bool RequireFieldKindIfPresent(
		//	const ParsedObjectFieldKinds& fieldKinds,
		//	const std::string& fieldName,
		//	JsonFieldKind kind,
		//	SchemaValidationIssue& issue,
		//	const std::string& methodName,
		//	const std::string& typeLabel) {
		//	const auto it = fieldKinds.find(fieldName);
		//	if (it == fieldKinds.end()) {
		//		return true;
		//	}

		//	if (it->second == kind) {
		//		return true;
		//	}

		//	SetIssue(
		//		issue,
		//		"schema_invalid_params",
		//		"Method `" + methodName + "` requires `params." + fieldName + "` to be " + typeLabel + ".");
		//	return false;
		//}
		//	}

		//	return false;
		//}

		bool TryConsumePrimitive(const std::string& json, std::size_t& position, const std::string& token) {
			if (json.compare(position, token.size(), token) != 0) {
				return false;
			}

			position += token.size();
			return true;
		}

		bool TryConsumeJsonValue(const std::string& json, std::size_t& position, JsonFieldKind& kind) {
			position = SkipWhitespace(json, position);
			if (position >= json.size()) {
				return false;
			}

			const char ch = json[position];
			if (ch == '"') {
				std::string ignored;
				if (!TryConsumeJsonString(json, position, ignored)) {
					return false;
				}

				kind = JsonFieldKind::String;
				return true;
			}

			if (ch == '{') {
				if (!TryConsumeBalancedComposite(json, position, '{', '}')) {
					return false;
				}

				kind = JsonFieldKind::Object;
				return true;
			}

			if (ch == '[') {
				if (!TryConsumeBalancedComposite(json, position, '[', ']')) {
					return false;
				}

				kind = JsonFieldKind::Array;
				return true;
			}

			if (TryConsumePrimitive(json, position, "true") || TryConsumePrimitive(json, position, "false")) {
				kind = JsonFieldKind::Boolean;
				return true;
			}

			if (TryConsumePrimitive(json, position, "null")) {
				kind = JsonFieldKind::Null;
				return true;
			}

			if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
				++position;
				while (position < json.size()) {
					const char numberCh = json[position];
					const bool isNumberChar =
						std::isdigit(static_cast<unsigned char>(numberCh)) != 0 ||
						numberCh == '.' || numberCh == 'e' || numberCh == 'E' || numberCh == '+' || numberCh == '-';
					if (!isNumberChar) {
						break;
					}

					++position;
				}

				kind = JsonFieldKind::Number;
				return true;
			}

			return false;
		}

		bool TryParseTopLevelObjectFieldKinds(const std::string& json, ParsedObjectFieldKinds& kinds) {
			kinds.clear();
			const std::string trimmed = Trim(json);
			if (!IsJsonObjectShape(trimmed)) {
				return false;
			}

			std::size_t position = 1;
			while (true) {
				position = SkipWhitespace(trimmed, position);
				if (position >= trimmed.size()) {
					return false;
				}

				if (trimmed[position] == '}') {
					return true;
				}

				std::string key;
				if (!TryConsumeJsonString(trimmed, position, key)) {
					return false;
				}

				position = SkipWhitespace(trimmed, position);
				if (position >= trimmed.size() || trimmed[position] != ':') {
					return false;
				}

				++position;
				JsonFieldKind kind{};
				if (!TryConsumeJsonValue(trimmed, position, kind)) {
					return false;
				}

				kinds.insert_or_assign(key, kind);
				position = SkipWhitespace(trimmed, position);
				if (position >= trimmed.size()) {
					return false;
				}

				if (trimmed[position] == ',') {
					++position;
					continue;
				}

				if (trimmed[position] == '}') {
					return true;
				}

				return false;
			}
		}

		bool IsFieldBoolean(const std::string& json, const std::string& fieldName);
		bool IsFieldNumber(const std::string& json, const std::string& fieldName);

		bool TryParseRequestParamsObject(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName,
			ParsedObjectFieldKinds& fieldKinds) {
			if (!request.paramsJson.has_value()) {
				fieldKinds.clear();
				return true;
			}

			if (!TryParseTopLevelObjectFieldKinds(request.paramsJson.value(), fieldKinds)) {
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` expects `params` to be a JSON object when provided.");
				return false;
			}

			return true;
		}

		bool RequireFieldKindIfPresent(
			const ParsedObjectFieldKinds& fieldKinds,
			const std::string& fieldName,
			JsonFieldKind kind,
			SchemaValidationIssue& issue,
			const std::string& methodName,
			const std::string& typeLabel) {
			const auto it = fieldKinds.find(fieldName);
			if (it == fieldKinds.end()) {
				return true;
			}

			if (it->second == kind) {
				return true;
			}

			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `" + methodName + "` requires `params." + fieldName + "` to be " + typeLabel + ".");
			return false;
		}

		bool ContainsFieldName(std::initializer_list<const char*> allowedFieldNames, const std::string& fieldName) {
			for (const char* allowed : allowedFieldNames) {
				if (fieldName == allowed) {
					return true;
				}
			}

			return false;
		}

		bool TryFindUnexpectedObjectField(
			const std::string& json,
			std::initializer_list<const char*> allowedFieldNames,
			std::string& unexpectedField) {
			for (std::size_t i = 0; i < json.size(); ++i) {
				if (json[i] != '"') {
					continue;
				}

				std::size_t end = i + 1;
				while (end < json.size()) {
					if (json[end] == '\\') {
						end += 2;
						continue;
					}

					if (json[end] == '"') {
						break;
					}

					++end;
				}

				if (end >= json.size()) {
					break;
				}

				std::size_t colonPos = end + 1;
				while (colonPos < json.size() && std::isspace(static_cast<unsigned char>(json[colonPos])) != 0) {
					++colonPos;
				}

				if (colonPos < json.size() && json[colonPos] == ':') {
					const std::string fieldName = json.substr(i + 1, end - i - 1);
					if (!ContainsFieldName(allowedFieldNames, fieldName)) {
						unexpectedField = fieldName;
						return true;
					}
				}

				i = end;
			}

			return false;
		}

		bool ValidateOptionalSessionListParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			if (!request.paramsJson.has_value()) {
				return true;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseTopLevelObjectFieldKinds(request.paramsJson.value(), fieldKinds)) {
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` expects `params` to be a JSON object when provided.");
				return false;
			}

			auto requireFieldKind = [&](const std::string& field, JsonFieldKind kind, const char* typeLabel) {
				const auto it = fieldKinds.find(field);
				if (it == fieldKinds.end()) {
					return true;
				}

				if (it->second == kind) {
					return true;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` requires `params." + field + "` to be " + typeLabel + ".");
				return false;
				};

			if (!requireFieldKind("active", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("scope", JsonFieldKind::String, "a string") ||
				!requireFieldKind("limit", JsonFieldKind::Number, "numeric") ||
				!requireFieldKind("activeMinutes", JsonFieldKind::Number, "numeric") ||
				!requireFieldKind("includeGlobal", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("includeUnknown", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("includeDerivedTitles", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("includeLastMessage", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("label", JsonFieldKind::String, "a string") ||
				!requireFieldKind("spawnedBy", JsonFieldKind::String, "a string") ||
				!requireFieldKind("agentId", JsonFieldKind::String, "a string") ||
				!requireFieldKind("search", JsonFieldKind::String, "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName(
					{ "active",
					 "scope",
					 "limit",
					 "activeMinutes",
					 "includeGlobal",
					 "includeUnknown",
					 "includeDerivedTitles",
					 "includeLastMessage",
					 "label",
					 "spawnedBy",
					 "agentId",
					 "search" },
					field)) {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteResolveParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				return true;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseTopLevelObjectFieldKinds(request.paramsJson.value(), fieldKinds)) {
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.channels.route.resolve` expects `params` to be a JSON object when provided.");
				return false;
			}

			auto requireFieldKind = [&](const char* field, JsonFieldKind kind, const char* typeLabel) {
				const auto it = fieldKinds.find(field);
				if (it == fieldKinds.end()) {
					return true;
				}

				if (it->second == kind) {
					return true;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					std::string("Method `gateway.channels.route.resolve` requires `params.") + field + "` to be " + typeLabel + ".");
				return false;
				};

			if (!requireFieldKind("channel", JsonFieldKind::String, "a string") ||
				!requireFieldKind("accountId", JsonFieldKind::String, "a string") ||
				!requireFieldKind("sessionId", JsonFieldKind::String, "a string") ||
				!requireFieldKind("agentId", JsonFieldKind::String, "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "channel", "accountId", "sessionId", "agentId" }, field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.resolve` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateOptionalChannelParam(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				methodName,
				"a string");
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

		bool ValidateToolsCallPreviewParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.tools.call.preview", fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"tool",
				JsonFieldKind::String,
				issue,
				"gateway.tools.call.preview",
				"a string") &&
				RequireFieldKindIfPresent(
					fieldKinds,
					"args",
					JsonFieldKind::Object,
					issue,
					"gateway.tools.call.preview",
					"an object");
		}

		bool ValidateToolsCallExecuteParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.tools.call.execute", fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"tool",
				JsonFieldKind::String,
				issue,
				"gateway.tools.call.execute",
				"a string") &&
				RequireFieldKindIfPresent(
					fieldKinds,
					"args",
					JsonFieldKind::Object,
					issue,
					"gateway.tools.call.execute",
					"an object");
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

		bool ValidateOptionalActiveParam(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"active",
				JsonFieldKind::Boolean,
				issue,
				methodName,
				"boolean");
		}

		bool ValidateSessionMutationParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"sessionId",
				JsonFieldKind::String,
				issue,
				methodName,
				"a string") &&
				RequireFieldKindIfPresent(
					fieldKinds,
					"scope",
					JsonFieldKind::String,
					issue,
					methodName,
					"a string") &&
				RequireFieldKindIfPresent(
					fieldKinds,
					"active",
					JsonFieldKind::Boolean,
					issue,
					methodName,
					"boolean");
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

		bool ValidateNoParamsAllowed(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			if (!request.paramsJson.has_value()) {
				return true;
			}

			SetIssue(issue, "schema_invalid_params", "Method `" + methodName + "` does not accept `params`.");
			return false;
		}

		bool ValidateStringIdParam(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName,
			const std::string& fieldName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				fieldName,
				JsonFieldKind::String,
				issue,
				methodName,
				"a string");
		}

		bool ValidateLogsTailParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.logs.tail", fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"limit",
				JsonFieldKind::Number,
				issue,
				"gateway.logs.tail",
				"numeric");
		}

		bool ValidateSessionsCompactParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.sessions.compact", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"dryRun",
				JsonFieldKind::Boolean,
				issue,
				"gateway.sessions.compact",
				"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "dryRun") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.sessions.compact` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateSessionsPreviewParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.sessions.preview", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"sessionId",
				JsonFieldKind::String,
				issue,
				"gateway.sessions.preview",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "sessionId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.sessions.preview` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsCreateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.create", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.create",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"name",
					JsonFieldKind::String,
					issue,
					"gateway.agents.create",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"active",
					JsonFieldKind::Boolean,
					issue,
					"gateway.agents.create",
					"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "name" || field == "active") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.create` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsLogoutParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.logout", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.logout",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.logout",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.channels.logout` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateConfigSetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.config.set", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"bind",
				JsonFieldKind::String,
				issue,
				"gateway.config.set",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"port",
					JsonFieldKind::Number,
					issue,
					"gateway.config.set",
					"numeric") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"model",
					JsonFieldKind::String,
					issue,
					"gateway.config.set",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"streaming",
					JsonFieldKind::Boolean,
					issue,
					"gateway.config.set",
					"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "bind" || field == "port" || field == "model" || field == "streaming") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.config.set` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidatePingParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.ping", fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"echo",
				JsonFieldKind::String,
				issue,
				"gateway.ping",
				"a string");
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

	} // namespace

	bool GatewayProtocolSchemaValidator::ValidateRequest(const RequestFrame& request, SchemaValidationIssue& issue) {
		issue = {};

		if (request.id.empty()) {
			SetIssue(issue, "schema_missing_field", "Request `id` is required.");
			return false;
		}

		if (request.method.empty()) {
			SetIssue(issue, "schema_missing_field", "Request `method` is required.");
			return false;
		}

		if (request.method == "gateway.ping") {
			return ValidatePingParams(request, issue);
		}

		if (request.method == "gateway.logs.tail") {
			return ValidateLogsTailParams(request, issue);
		}

		if (request.method == "gateway.sessions.compact") {
			return ValidateSessionsCompactParams(request, issue);
		}

		if (request.method == "gateway.sessions.preview") {
			return ValidateSessionsPreviewParams(request, issue);
		}

		if (request.method == "gateway.channels.route.resolve") {
			return ValidateChannelsRouteResolveParams(request, issue);
		}

		if (request.method == "gateway.tools.call.preview") {
			return ValidateToolsCallPreviewParams(request, issue);
		}

		if (request.method == "gateway.tools.call.execute") {
			return ValidateToolsCallExecuteParams(request, issue);
		}

		if (request.method == "gateway.sessions.resolve") {
			return ValidateStringIdParam(request, issue, request.method, "sessionId");
		}

		if (request.method == "gateway.sessions.delete") {
			return ValidateStringIdParam(request, issue, request.method, "sessionId");
		}

		if (request.method == "gateway.sessions.usage") {
			return ValidateStringIdParam(request, issue, request.method, "sessionId");
		}

		if (request.method == "gateway.sessions.patch") {
			return ValidateSessionMutationParams(request, issue, request.method);
		}

		if (request.method == "gateway.sessions.create" || request.method == "gateway.sessions.reset") {
			return ValidateSessionMutationParams(request, issue, request.method);
		}

		if (request.method == "gateway.agents.get" || request.method == "gateway.agents.activate" || request.method == "gateway.agents.delete") {
			return ValidateStringIdParam(request, issue, request.method, "agentId");
		}

		if (request.method == "gateway.agents.create") {
			return ValidateAgentsCreateParams(request, issue);
		}

		if (request.method == "gateway.agents.update") {
			return ValidateAgentsCreateParams(request, issue);
		}

		if (request.method == "gateway.config.set") {
			return ValidateConfigSetParams(request, issue);
		}

		if (request.method == "gateway.protocol.version" ||
			request.method == "gateway.features.list" ||
			request.method == "gateway.config.get" ||
			request.method == "gateway.models.list" ||
			request.method == "gateway.tools.catalog" ||
			request.method == "gateway.health" ||
			request.method == "gateway.transport.status" ||
			request.method == "gateway.events.catalog") {
			return ValidateNoParamsAllowed(request, issue, request.method);
		}

		if (request.method == "gateway.channels.status" ||
			request.method == "gateway.channels.routes" ||
			request.method == "gateway.channels.accounts") {
			return ValidateOptionalChannelParam(request, issue, request.method);
		}

		if (request.method == "gateway.channels.logout") {
			return ValidateChannelsLogoutParams(request, issue);
		}

		if (request.method == "gateway.agents.list") {
			return ValidateOptionalActiveParam(request, issue, request.method);
		}

		if (request.method == "gateway.session.list") {
			return ValidateOptionalSessionListParams(request, issue, request.method);
		}

		return true;
	}

	bool GatewayProtocolSchemaValidator::ValidateResponseForMethod(
		const std::string& method,
		const ResponseFrame& response,
		SchemaValidationIssue& issue) {
		issue = {};

		if (!ValidateResponseEnvelope(response, issue)) {
			return false;
		}

		const std::string payload = response.payloadJson.value_or("{}");

		if (method == "gateway.ping") {
			if (!IsFieldBoolean(payload, "pong")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.ping` response requires boolean field `pong`.");
				return false;
			}
			return true;
		}

		if (method == "gateway.sessions.delete") {
			if (!IsFieldValueType(payload, "session", '{') ||
				!IsFieldBoolean(payload, "deleted") ||
				!IsFieldNumber(payload, "remaining")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.sessions.delete` requires `session` object, `deleted` boolean, and `remaining` number fields.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "scope", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.sessions.delete` requires `session` fields `id`, `scope`, and `active`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.sessions.usage") {
			const bool hasRoot = IsFieldValueType(payload, "sessionId", '"') &&
				IsFieldNumber(payload, "messages") &&
				IsFieldValueType(payload, "tokens", '{') &&
				IsFieldNumber(payload, "lastActiveMs");
			const bool hasTokens = IsFieldNumber(payload, "input") &&
				IsFieldNumber(payload, "output") &&
				IsFieldNumber(payload, "total");

			if (!(hasRoot && hasTokens)) {
				SetIssue(issue, "schema_invalid_response", "`gateway.sessions.usage` requires `sessionId`, `messages`, `tokens.{input,output,total}`, and `lastActiveMs` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.sessions.compact") {
			if (!IsFieldNumber(payload, "compacted") ||
				!IsFieldNumber(payload, "remaining") ||
				!IsFieldBoolean(payload, "dryRun")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.sessions.compact` requires numeric `compacted`/`remaining` and boolean `dryRun` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.sessions.preview") {
			const bool hasRoot = IsFieldValueType(payload, "session", '{') &&
				IsFieldValueType(payload, "title", '"') &&
				IsFieldBoolean(payload, "hasMessages") &&
				IsFieldNumber(payload, "unread");

			if (!hasRoot || !PayloadContainsAllFieldTokens(payload, { "id", "scope", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.sessions.preview` requires `session` with `id/scope/active`, `title`, `hasMessages`, and `unread` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.sessions.patch") {
			if (!IsFieldValueType(payload, "session", '{') || !IsFieldBoolean(payload, "patched")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.sessions.patch` requires `session` object and `patched` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "scope", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.sessions.patch` requires `session` fields `id`, `scope`, and `active`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.create") {
			if (!IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "created")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.create` requires `agent` object and `created` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "name", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.create` requires `agent` fields `id`, `name`, and `active`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.delete") {
			if (!IsFieldValueType(payload, "agent", '{') ||
				!IsFieldBoolean(payload, "deleted") ||
				!IsFieldNumber(payload, "remaining")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.delete` requires `agent` object, `deleted` boolean, and `remaining` number.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "name", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.delete` requires `agent` fields `id`, `name`, and `active`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.update") {
			if (!IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "updated")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.update` requires `agent` object and `updated` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "name", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.update` requires `agent` fields `id`, `name`, and `active`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.protocol.version") {
			if (!IsFieldNumber(payload, "minProtocol") || !IsFieldNumber(payload, "maxProtocol")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.protocol.version` requires numeric protocol fields.");
				return false;
			}
			return true;
		}

		if (method == "gateway.config.get") {
			if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.get` requires object fields `gateway` and `agent`.");
				return false;
			}

			if (!IsFieldValueType(payload, "bind", '"') ||
				!IsFieldNumber(payload, "port") ||
				!IsFieldValueType(payload, "model", '"') ||
				!IsFieldBoolean(payload, "streaming")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.get` requires `gateway.bind` string, `gateway.port` number, `agent.model` string, and `agent.streaming` boolean fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.set") {
			if (!IsFieldValueType(payload, "gateway", '{') ||
				!IsFieldValueType(payload, "agent", '{') ||
				!IsFieldBoolean(payload, "updated")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.set` requires `gateway`, `agent`, and `updated` fields.");
				return false;
			}

			if (!IsFieldValueType(payload, "bind", '"') ||
				!IsFieldNumber(payload, "port") ||
				!IsFieldValueType(payload, "model", '"') ||
				!IsFieldBoolean(payload, "streaming")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.set` requires `gateway.bind`, `gateway.port`, `agent.model`, and `agent.streaming` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.list") {
			if (!IsFieldValueType(payload, "models", '[')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.list` requires array field `models`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "models")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "provider", "displayName", "streaming" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.list` requires model entries with `id`, `provider`, `displayName`, and `streaming` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.status") {
			if (!IsFieldValueType(payload, "channels", '[')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.status` requires array field `channels`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "channels")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "label", "connected", "accounts" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.status` requires channel entries with `id`, `label`, `connected`, and `accounts` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.routes") {
			if (!IsFieldValueType(payload, "routes", '[')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.routes` requires array field `routes`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "routes")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "agentId", "sessionId" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.routes` requires route entries with `channel`, `accountId`, `agentId`, and `sessionId` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts") {
			if (!IsFieldValueType(payload, "accounts", '[')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts` requires array field `accounts`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "accounts")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "label", "active", "connected" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts` requires account entries with `channel`, `accountId`, `label`, `active`, and `connected` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.logout") {
			if (!IsFieldBoolean(payload, "loggedOut") || !IsFieldNumber(payload, "affected")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.logout` requires `loggedOut` boolean and `affected` number fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.route.resolve") {
			if (!IsFieldValueType(payload, "route", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.resolve` requires object field `route`.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "agentId", "sessionId" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.resolve` requires route object fields `channel`, `accountId`, `agentId`, and `sessionId`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.logs.tail") {
			if (!IsFieldValueType(payload, "entries", '[')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.logs.tail` requires array field `entries`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "entries")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "ts", "level", "source", "message" }) ||
				!IsFieldNumber(payload, "ts") ||
				!IsFieldValueType(payload, "level", '"') ||
				!IsFieldValueType(payload, "source", '"') ||
				!IsFieldValueType(payload, "message", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.logs.tail` requires log entries with `ts`, `level`, `source`, and `message` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.sessions.resolve" || method == "gateway.sessions.create" || method == "gateway.sessions.reset") {
			if (!IsFieldValueType(payload, "session", '{')) {
				SetIssue(issue, "schema_invalid_response", "`" + method + "` requires object field `session`.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "scope", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`" + method + "` requires session fields `id`, `scope`, and `active`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.get" || method == "gateway.agents.activate") {
			if (!IsFieldValueType(payload, "agent", '{')) {
				SetIssue(issue, "schema_invalid_response", "`" + method + "` requires object field `agent`.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "name", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`" + method + "` requires agent fields `id`, `name`, and `active`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.list") {
			if (!IsFieldValueType(payload, "agents", '[') ||
				!IsFieldNumber(payload, "count") ||
				!IsFieldValueType(payload, "activeAgentId", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.list` requires `agents` array, `count` number, and `activeAgentId` string fields.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "agents")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "name", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.list` requires agent entries with `id`, `name`, and `active` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.catalog") {
			if (!IsFieldValueType(payload, "tools", '[')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.catalog` requires array field `tools`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "tools")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "label", "category", "enabled" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.catalog` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.call.preview") {
			return IsFieldValueType(payload, "tool", '"') && IsFieldBoolean(payload, "allowed") &&
				IsFieldValueType(payload, "reason", '"') &&
				IsFieldBoolean(payload, "argsProvided") &&
				IsFieldValueType(payload, "policy", '"')
				? true
				: (SetIssue(issue, "schema_invalid_response", "`gateway.tools.call.preview` requires `tool`, `allowed`, `reason`, `argsProvided`, and `policy` fields."), false);
		}

		if (method == "gateway.tools.call.execute") {
			return IsFieldValueType(payload, "tool", '"') &&
				IsFieldBoolean(payload, "executed") &&
				IsFieldValueType(payload, "status", '"') &&
				IsFieldValueType(payload, "output", '"') &&
				IsFieldBoolean(payload, "argsProvided")
				? true
				: (SetIssue(issue, "schema_invalid_response", "`gateway.tools.call.execute` requires `tool`, `executed`, `status`, `output`, and `argsProvided` fields."), false);
		}

		if (method == "gateway.features.list") {
			if (!IsFieldValueType(payload, "methods", '[') || !IsFieldValueType(payload, "events", '[')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.features.list` requires array fields `methods` and `events`.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, {
				"gateway.ping",
				"gateway.transport.status",
				"gateway.events.catalog",
		  "gateway.agents.create",
				"gateway.sessions.delete",
			   "gateway.sessions.compact",
		  "gateway.sessions.patch",
		   "gateway.sessions.preview",
				"gateway.sessions.usage",
				"gateway.channels.accounts",
				"gateway.tools.call.preview",
				"gateway.tick",
				"gateway.health",
				"gateway.shutdown",
				})) {
				SetIssue(issue, "schema_invalid_response", "`gateway.features.list` catalog is missing required method/event members.");
				return false;
			}

			return true;
		}

		if (method == "gateway.health") {
			return IsFieldValueType(payload, "status", '"') && IsFieldBoolean(payload, "running")
				? true
				: (SetIssue(issue, "schema_invalid_response", "`gateway.health` requires `status` string and `running` boolean."), false);
		}

		if (method == "gateway.transport.status") {
			const bool hasCoreFields = IsFieldBoolean(payload, "running") &&
				IsFieldValueType(payload, "endpoint", '"') &&
				IsFieldNumber(payload, "connections");
			const bool hasTimeoutFields = IsFieldValueType(payload, "timeouts", '{') &&
				IsFieldNumber(payload, "handshake") &&
				IsFieldNumber(payload, "idle");
			const bool hasCloseFields = IsFieldValueType(payload, "closes", '{') &&
				IsFieldNumber(payload, "invalidUtf8") &&
				IsFieldNumber(payload, "messageTooBig") &&
				IsFieldNumber(payload, "extensionRejected");

			if (!(hasCoreFields && hasTimeoutFields && hasCloseFields)) {
				SetIssue(
					issue,
					"schema_invalid_response",
					"`gateway.transport.status` requires `running`, `endpoint`, `connections`, `timeouts.{handshake,idle}`, and `closes.{invalidUtf8,messageTooBig,extensionRejected}`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.session.list") {
			if (!IsFieldValueType(payload, "sessions", '[') ||
				!IsFieldNumber(payload, "count") ||
				!IsFieldValueType(payload, "activeSessionId", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.session.list` requires `sessions` array, `count` number, and `activeSessionId` string fields.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "sessions")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "scope", "active" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.session.list` requires session entries with `id`, `scope`, and `active` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.catalog") {
			if (!IsFieldValueType(payload, "events", '[')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.catalog` requires array field `events`.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, {
				"gateway.tick",
				"gateway.health",
				"gateway.shutdown",
				"gateway.channels.update",
				"gateway.channels.accounts.update",
				"gateway.session.reset",
				"gateway.agent.update",
				"gateway.tools.catalog.update",
				})) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.catalog` is missing required event members.");
				return false;
			}

			return true;
		}

		return true;
	}

	bool GatewayProtocolSchemaValidator::ValidateEvent(const EventFrame& event, SchemaValidationIssue& issue) {
		issue = {};

		if (event.eventName.empty()) {
			SetIssue(issue, "schema_invalid_event", "Event name is required.");
			return false;
		}

		if (!event.payloadJson.has_value() || !IsJsonObjectShape(event.payloadJson.value())) {
			SetIssue(issue, "schema_invalid_event", "Event payload must be a JSON object.");
			return false;
		}

		const std::string payload = event.payloadJson.value();

		if (event.eventName == "gateway.tick") {
			if (!IsFieldNumber(payload, "ts") ||
				!IsFieldBoolean(payload, "running") ||
				!IsFieldNumber(payload, "connections")) {
				SetIssue(issue, "schema_invalid_event", "`gateway.tick` requires `ts` number, `running` boolean, and `connections` number fields.");
				return false;
			}

			return true;
		}

		if (event.eventName == "gateway.health") {
			if (!IsFieldValueType(payload, "status", '"') ||
				!IsFieldBoolean(payload, "running") ||
				!IsFieldValueType(payload, "endpoint", '"') ||
				!IsFieldNumber(payload, "connections")) {
				SetIssue(issue, "schema_invalid_event", "`gateway.health` requires `status` string, `running` boolean, `endpoint` string, and `connections` number fields.");
				return false;
			}

			if (!IsFieldValueType(payload, "timeouts", '{') ||
				!IsFieldNumber(payload, "handshake") ||
				!IsFieldNumber(payload, "idle") ||
				!IsFieldValueType(payload, "closes", '{') ||
				!IsFieldNumber(payload, "invalidUtf8") ||
				!IsFieldNumber(payload, "messageTooBig") ||
				!IsFieldNumber(payload, "extensionRejected")) {
				SetIssue(issue, "schema_invalid_event", "`gateway.health` requires diagnostics objects `timeouts` and `closes` with numeric counters.");
				return false;
			}

			return true;
		}

		if (event.eventName == "gateway.shutdown") {
			if (!IsFieldValueType(payload, "reason", '"') ||
				!IsFieldBoolean(payload, "graceful") ||
				!IsFieldNumber(payload, "seq")) {
				SetIssue(issue, "schema_invalid_event", "`gateway.shutdown` requires `reason` string, `graceful` boolean, and `seq` number fields.");
				return false;
			}

			return true;
		}

		if (event.eventName == "gateway.channels.update") {
			if (!IsFieldValueType(payload, "channels", '[')) {
				SetIssue(issue, "schema_invalid_event", "`gateway.channels.update` requires array field `channels`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "channels")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "label", "connected", "accounts" })) {
				SetIssue(issue, "schema_invalid_event", "`gateway.channels.update` requires channel entries with `id`, `label`, `connected`, and `accounts` fields.");
				return false;
			}

			return true;
		}

		if (event.eventName == "gateway.channels.accounts.update") {
			if (!IsFieldValueType(payload, "accounts", '[')) {
				SetIssue(issue, "schema_invalid_event", "`gateway.channels.accounts.update` requires array field `accounts`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "accounts")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "label", "active", "connected" })) {
				SetIssue(issue, "schema_invalid_event", "`gateway.channels.accounts.update` requires account entries with `channel`, `accountId`, `label`, `active`, and `connected` fields.");
				return false;
			}

			return true;
		}

		if (event.eventName == "gateway.session.reset") {
			if (!IsFieldValueType(payload, "sessionId", '"')) {
				SetIssue(issue, "schema_invalid_event", "`gateway.session.reset` requires string field `sessionId`.");
				return false;
			}

			if (payload.find("\"session\"") != std::string::npos) {
				if (!IsFieldValueType(payload, "session", '{') ||
					!PayloadContainsAllFieldTokens(payload, { "id", "scope", "active" })) {
					SetIssue(issue, "schema_invalid_event", "`gateway.session.reset` optional `session` object requires `id`, `scope`, and `active` fields.");
					return false;
				}
			}

			return true;
		}

		if (event.eventName == "gateway.agent.update") {
			if (!IsFieldValueType(payload, "agentId", '"')) {
				SetIssue(issue, "schema_invalid_event", "`gateway.agent.update` requires string field `agentId`.");
				return false;
			}

			if (payload.find("\"agent\"") != std::string::npos) {
				if (!IsFieldValueType(payload, "agent", '{') ||
					!PayloadContainsAllFieldTokens(payload, { "id", "name", "active" })) {
					SetIssue(issue, "schema_invalid_event", "`gateway.agent.update` optional `agent` object requires `id`, `name`, and `active` fields.");
					return false;
				}
			}

			return true;
		}

		if (event.eventName == "gateway.tools.catalog.update") {
			if (!IsFieldValueType(payload, "tools", '[')) {
				SetIssue(issue, "schema_invalid_event", "`gateway.tools.catalog.update` requires array field `tools`.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "tools")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "label", "category", "enabled" })) {
				SetIssue(issue, "schema_invalid_event", "`gateway.tools.catalog.update` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
				return false;
			}

			return true;
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
