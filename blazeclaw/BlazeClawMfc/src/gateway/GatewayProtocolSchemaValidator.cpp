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

		bool ValidateChannelsRouteSetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.set", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.set",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.set",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"agentId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.set",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"sessionId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.set",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId" || field == "agentId" || field == "sessionId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.set` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteDeleteParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.delete", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.delete",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.delete",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.delete` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteExistsParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.exists", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.exists",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.exists",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.exists` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteGetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.get", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.get",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.get",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.get` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteRestoreParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.restore", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.restore",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.restore",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.restore` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutesClearParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.routes.clear", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.routes.clear",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.routes.clear` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutesRestoreParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.routes.restore", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.routes.restore",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.routes.restore` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutesResetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.routes.reset", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.routes.reset", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.routes.reset` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutesCountParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.routes.count", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.routes.count", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.routes.count` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutePatchParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.patch", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.route.patch", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "accountId", JsonFieldKind::String, issue, "gateway.channels.route.patch", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "agentId", JsonFieldKind::String, issue, "gateway.channels.route.patch", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "sessionId", JsonFieldKind::String, issue, "gateway.channels.route.patch", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId" || field == "agentId" || field == "sessionId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.patch` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsActivateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.activate", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.activate",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.activate",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.activate` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsDeactivateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.deactivate", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.deactivate",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.deactivate",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.deactivate` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsClearParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.clear", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.clear",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.clear` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsRestoreParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.restore", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.accounts.restore", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.restore` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsCountParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.count", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.accounts.count", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.count` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsExistsParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.exists", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.exists",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.exists",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.exists` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsUpdateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.update", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.update",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.update",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"label",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.update",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"active",
					JsonFieldKind::Boolean,
					issue,
					"gateway.channels.accounts.update",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"connected",
					JsonFieldKind::Boolean,
					issue,
					"gateway.channels.accounts.update",
					"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId" || field == "label" || field == "active" || field == "connected") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.update` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsGetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.get", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.get",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.get",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.get` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsCreateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.create", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.create",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.create",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"label",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.create",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"active",
					JsonFieldKind::Boolean,
					issue,
					"gateway.channels.accounts.create",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"connected",
					JsonFieldKind::Boolean,
					issue,
					"gateway.channels.accounts.create",
					"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId" || field == "label" || field == "active" || field == "connected") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.create` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsDeleteParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.delete", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.delete",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.delete",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.delete` does not allow `params." + field + "`.");
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

		bool ValidateSessionsCountParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.sessions.count", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "scope", JsonFieldKind::String, issue, "gateway.sessions.count", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "active", JsonFieldKind::Boolean, issue, "gateway.sessions.count", "boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "scope" || field == "active") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.sessions.count` does not allow `params." + field + "`.");
				return false;
			}

			return true;
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

		bool ValidateLogsCountParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.logs.count", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "level", JsonFieldKind::String, issue, "gateway.logs.count", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "level") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.logs.count` does not allow `params." + field + "`.");
				return false;
			}

			return true;
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

		bool ValidateAgentsFilesListParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.list", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.list",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.list` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsFilesGetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.get", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.get",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"path",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.get",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "path") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.get` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsFilesSetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.set", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.set",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"path",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.set",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"content",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.set",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "path" || field == "content") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.set` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsFilesDeleteParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.delete", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.delete",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"path",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.delete",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "path") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.delete` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsFilesExistsParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.exists", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.exists",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"path",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.exists",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "path") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.exists` does not allow `params." + field + "`.");
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

		if (request.method == "gateway.channels.route.set") {
			return ValidateChannelsRouteSetParams(request, issue);
		}

		if (request.method == "gateway.channels.route.delete") {
			return ValidateChannelsRouteDeleteParams(request, issue);
		}

		if (request.method == "gateway.channels.route.exists") {
			return ValidateChannelsRouteExistsParams(request, issue);
		}

		if (request.method == "gateway.channels.route.get") {
			return ValidateChannelsRouteGetParams(request, issue);
		}

		if (request.method == "gateway.channels.route.restore") {
			return ValidateChannelsRouteRestoreParams(request, issue);
		}

		if (request.method == "gateway.channels.route.reset") {
			return ValidateChannelsRouteRestoreParams(request, issue);
		}

		if (request.method == "gateway.channels.route.patch") {
			return ValidateChannelsRoutePatchParams(request, issue);
		}

		if (request.method == "gateway.channels.routes.clear") {
			return ValidateChannelsRoutesClearParams(request, issue);
		}

		if (request.method == "gateway.channels.routes.restore") {
			return ValidateChannelsRoutesRestoreParams(request, issue);
		}

		if (request.method == "gateway.channels.routes.reset") {
			return ValidateChannelsRoutesResetParams(request, issue);
		}

		if (request.method == "gateway.channels.routes.count") {
			return ValidateChannelsRoutesCountParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.activate") {
			return ValidateChannelsAccountsActivateParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.deactivate") {
			return ValidateChannelsAccountsDeactivateParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.exists") {
			return ValidateChannelsAccountsExistsParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.update") {
			return ValidateChannelsAccountsUpdateParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.get") {
			return ValidateChannelsAccountsGetParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.create") {
			return ValidateChannelsAccountsCreateParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.delete") {
			return ValidateChannelsAccountsDeleteParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.clear") {
			return ValidateChannelsAccountsClearParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.restore") {
			return ValidateChannelsAccountsRestoreParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.count") {
			return ValidateChannelsAccountsCountParams(request, issue);
		}

		if (request.method == "gateway.channels.accounts.reset") {
			return ValidateOptionalChannelParam(request, issue, request.method);
		}

		if (request.method == "gateway.channels.status.get" ||
			request.method == "gateway.channels.status.exists" ||
			request.method == "gateway.channels.status.count") {
			return ValidateOptionalChannelParam(request, issue, request.method);
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

		if (request.method == "gateway.sessions.exists" || request.method == "gateway.sessions.activate") {
			return ValidateStringIdParam(request, issue, request.method, "sessionId");
		}

		if (request.method == "gateway.sessions.count") {
			return ValidateSessionsCountParams(request, issue);
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

		if (request.method == "gateway.agents.files.list") {
			return ValidateAgentsFilesListParams(request, issue);
		}

		if (request.method == "gateway.agents.files.get") {
			return ValidateAgentsFilesGetParams(request, issue);
		}

		if (request.method == "gateway.agents.files.set") {
			return ValidateAgentsFilesSetParams(request, issue);
		}

		if (request.method == "gateway.agents.files.delete") {
			return ValidateAgentsFilesDeleteParams(request, issue);
		}

		if (request.method == "gateway.agents.files.exists") {
			return ValidateAgentsFilesExistsParams(request, issue);
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
			request.method == "gateway.config.keys" ||
			request.method == "gateway.config.sections" ||
			request.method == "gateway.config.schema" ||
			request.method == "gateway.config.validate" ||
			request.method == "gateway.config.audit" ||
			request.method == "gateway.config.rollback" ||
			request.method == "gateway.config.backup" ||
			request.method == "gateway.config.diff" ||
			request.method == "gateway.config.snapshot" ||
			request.method == "gateway.config.revision" ||
			request.method == "gateway.config.history" ||
           request.method == "gateway.config.profile" ||
           request.method == "gateway.config.template" ||
           request.method == "gateway.config.bundle" ||
			request.method == "gateway.transport.endpoint.get" ||
			request.method == "gateway.transport.connections.count" ||
			request.method == "gateway.transport.endpoints.list" ||
			request.method == "gateway.transport.policy.get" ||
			request.method == "gateway.transport.policy.set" ||
			request.method == "gateway.transport.policy.reset" ||
			request.method == "gateway.transport.policy.status" ||
			request.method == "gateway.transport.policy.validate" ||
			request.method == "gateway.transport.policy.history" ||
			request.method == "gateway.transport.policy.metrics" ||
			request.method == "gateway.transport.policy.export" ||
			request.method == "gateway.transport.policy.import" ||
          request.method == "gateway.transport.policy.digest" ||
          request.method == "gateway.transport.policy.preview" ||
          request.method == "gateway.transport.policy.commit" ||
			request.method == "gateway.logs.levels" ||
			request.method == "gateway.events.catalog" ||
			request.method == "gateway.events.list" ||
			request.method == "gateway.events.last" ||
			request.method == "gateway.events.summary" ||
			request.method == "gateway.events.types" ||
			request.method == "gateway.events.channels" ||
			request.method == "gateway.events.timeline" ||
			request.method == "gateway.events.sample" ||
			request.method == "gateway.events.window" ||
			request.method == "gateway.events.recent" ||
			request.method == "gateway.events.batch" ||
          request.method == "gateway.events.cursor" ||
          request.method == "gateway.events.anchor" ||
          request.method == "gateway.events.offset" ||
			request.method == "gateway.models.list" ||
			request.method == "gateway.models.providers" ||
			request.method == "gateway.models.default.get" ||
			request.method == "gateway.models.compatibility" ||
			request.method == "gateway.models.recommended" ||
			request.method == "gateway.models.fallback" ||
			request.method == "gateway.models.selection" ||
			request.method == "gateway.models.routing" ||
			request.method == "gateway.models.preference" ||
			request.method == "gateway.models.priority" ||
			request.method == "gateway.models.affinity" ||
         request.method == "gateway.models.pool" ||
         request.method == "gateway.models.manifest" ||
         request.method == "gateway.models.catalog" ||
			request.method == "gateway.tools.health" ||
			request.method == "gateway.tools.stats" ||
			request.method == "gateway.tools.failures" ||
			request.method == "gateway.tools.usage" ||
			request.method == "gateway.tools.latency" ||
			request.method == "gateway.tools.errors" ||
			request.method == "gateway.tools.throughput" ||
			request.method == "gateway.tools.capacity" ||
            request.method == "gateway.tools.queue" ||
            request.method == "gateway.tools.scheduler" ||
            request.method == "gateway.tools.backlog" ||
			request.method == "gateway.tools.metrics" ||
			request.method == "gateway.tools.categories" ||
			request.method == "gateway.tools.catalog" ||
			request.method == "gateway.health" ||
			request.method == "gateway.health.details" ||
			request.method == "gateway.transport.status") {
			return ValidateNoParamsAllowed(request, issue, request.method);
		}

		if (request.method == "gateway.config.exists") {
			return ValidateStringIdParam(request, issue, request.method, "key");
		}

		if (request.method == "gateway.events.exists" || request.method == "gateway.events.count") {
			return ValidateStringIdParam(request, issue, request.method, "event");
		}

		if (request.method == "gateway.events.get") {
			return ValidateStringIdParam(request, issue, request.method, "event");
		}

		if (request.method == "gateway.events.search") {
			return ValidateStringIdParam(request, issue, request.method, "term");
		}

		if (request.method == "gateway.events.latestByType") {
			return ValidateStringIdParam(request, issue, request.method, "type");
		}

		if (request.method == "gateway.config.count") {
			return ValidateStringIdParam(request, issue, request.method, "section");
		}

		if (request.method == "gateway.models.count") {
			return ValidateStringIdParam(request, issue, request.method, "provider");
		}

		if (request.method == "gateway.tools.exists") {
			return ValidateStringIdParam(request, issue, request.method, "tool");
		}

		if (request.method == "gateway.tools.get") {
			return ValidateStringIdParam(request, issue, request.method, "tool");
		}

		if (request.method == "gateway.tools.list") {
			return ValidateStringIdParam(request, issue, request.method, "category");
		}

		if (request.method == "gateway.tools.count") {
			return ValidateOptionalActiveParam(request, issue, request.method);
		}

		if (request.method == "gateway.models.exists") {
			return ValidateStringIdParam(request, issue, request.method, "modelId");
		}

		if (request.method == "gateway.models.get") {
			return ValidateStringIdParam(request, issue, request.method, "modelId");
		}

		if (request.method == "gateway.models.listByProvider") {
			return ValidateStringIdParam(request, issue, request.method, "provider");
		}

		if (request.method == "gateway.config.getKey") {
			return ValidateStringIdParam(request, issue, request.method, "key");
		}

		if (request.method == "gateway.config.getSection") {
			return ValidateStringIdParam(request, issue, request.method, "section");
		}

		if (request.method == "gateway.transport.endpoint.exists") {
			return ValidateStringIdParam(request, issue, request.method, "endpoint");
		}

		if (request.method == "gateway.transport.endpoint.set") {
			return ValidateStringIdParam(request, issue, request.method, "endpoint");
		}

		if (request.method == "gateway.channels.status" ||
			request.method == "gateway.channels.routes" ||
			request.method == "gateway.channels.accounts") {
			return ValidateOptionalChannelParam(request, issue, request.method);
		}

		if (request.method == "gateway.channels.logout") {
			return ValidateChannelsLogoutParams(request, issue);
		}

		if (request.method == "gateway.logs.count") {
			return ValidateLogsCountParams(request, issue);
		}

		if (request.method == "gateway.agents.list") {
			return ValidateOptionalActiveParam(request, issue, request.method);
		}

		if (request.method == "gateway.agents.exists") {
			return ValidateStringIdParam(request, issue, request.method, "agentId");
		}

		if (request.method == "gateway.agents.count") {
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

		if (method == "gateway.agents.files.list") {
			if (!IsFieldValueType(payload, "files", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.list` requires `files` array and `count` number fields.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "files")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "path", "size", "updatedMs" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.list` requires file entries with `path`, `size`, and `updatedMs` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.files.get") {
			if (!IsFieldValueType(payload, "file", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.get` requires object field `file`.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "path", "size", "updatedMs", "content" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.get` requires `file` fields `path`, `size`, `updatedMs`, and `content`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.files.set") {
			if (!IsFieldValueType(payload, "file", '{') || !IsFieldBoolean(payload, "saved")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.set` requires `file` object and `saved` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "path", "size", "updatedMs", "content" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.set` requires `file` fields `path`, `size`, `updatedMs`, and `content`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.files.delete") {
			if (!IsFieldValueType(payload, "file", '{') || !IsFieldBoolean(payload, "deleted")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.delete` requires `file` object and `deleted` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "path", "size", "updatedMs", "content" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.delete` requires `file` fields `path`, `size`, `updatedMs`, and `content`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.agents.files.exists") {
			if (!IsFieldValueType(payload, "path", '"') || !IsFieldBoolean(payload, "exists")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.exists` requires `path` string and `exists` boolean fields.");
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

		if (method == "gateway.channels.accounts.activate") {
			if (!IsFieldValueType(payload, "account", '{') || !IsFieldBoolean(payload, "activated")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.activate` requires `account` object and `activated` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "label", "active", "connected" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.activate` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.deactivate") {
			if (!IsFieldValueType(payload, "account", '{') || !IsFieldBoolean(payload, "deactivated")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.deactivate` requires `account` object and `deactivated` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "label", "active", "connected" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.deactivate` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.update") {
			if (!IsFieldValueType(payload, "account", '{') || !IsFieldBoolean(payload, "updated")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.update` requires `account` object and `updated` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "label", "active", "connected" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.update` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.exists") {
			if (!IsFieldValueType(payload, "channel", '"') ||
				!IsFieldValueType(payload, "accountId", '"') ||
				!IsFieldBoolean(payload, "exists")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.exists` requires `channel` string, `accountId` string, and `exists` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.get") {
			if (!IsFieldValueType(payload, "account", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.get` requires `account` object.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "label", "active", "connected" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.get` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.create") {
			if (!IsFieldValueType(payload, "account", '{') || !IsFieldBoolean(payload, "created")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.create` requires `account` object and `created` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "label", "active", "connected" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.create` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.delete") {
			if (!IsFieldValueType(payload, "account", '{') || !IsFieldBoolean(payload, "deleted")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.delete` requires `account` object and `deleted` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "label", "active", "connected" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.delete` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.clear") {
			if (!IsFieldNumber(payload, "cleared") || !IsFieldNumber(payload, "remaining")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.clear` requires numeric fields `cleared` and `remaining`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.restore") {
			if (!IsFieldNumber(payload, "restored") || !IsFieldNumber(payload, "total")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.restore` requires numeric fields `restored` and `total`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.count") {
			if (!IsFieldValueType(payload, "channel", '"') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.count` requires `channel` string and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.accounts.reset") {
			if (!IsFieldNumber(payload, "cleared") || !IsFieldNumber(payload, "restored") || !IsFieldNumber(payload, "total")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.reset` requires numeric fields `cleared`, `restored`, and `total`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.status.get") {
			if (!IsFieldValueType(payload, "channel", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.status.get` requires object field `channel`.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "label", "connected", "accounts" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.status.get` requires channel fields `id`, `label`, `connected`, and `accounts`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.status.exists") {
			if (!IsFieldValueType(payload, "channel", '"') || !IsFieldBoolean(payload, "exists")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.status.exists` requires `channel` string and `exists` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.status.count") {
			if (!IsFieldValueType(payload, "channel", '"') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.status.count` requires `channel` string and `count` number.");
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

		if (method == "gateway.channels.route.reset") {
			if (!IsFieldValueType(payload, "route", '{') || !IsFieldBoolean(payload, "deleted") || !IsFieldBoolean(payload, "restored")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.reset` requires `route` object, `deleted` boolean, and `restored` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "agentId", "sessionId" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.reset` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.route.patch") {
			if (!IsFieldValueType(payload, "route", '{') || !IsFieldBoolean(payload, "updated")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.patch` requires `route` object and `updated` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "agentId", "sessionId" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.patch` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
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

		if (method == "gateway.channels.route.set") {
			if (!IsFieldValueType(payload, "route", '{') || !IsFieldBoolean(payload, "saved")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.set` requires `route` object and `saved` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "agentId", "sessionId" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.set` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.route.delete") {
			if (!IsFieldValueType(payload, "route", '{') || !IsFieldBoolean(payload, "deleted")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.delete` requires `route` object and `deleted` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "agentId", "sessionId" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.delete` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.route.exists") {
			if (!IsFieldValueType(payload, "channel", '"') ||
				!IsFieldValueType(payload, "accountId", '"') ||
				!IsFieldBoolean(payload, "exists")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.exists` requires `channel` string, `accountId` string, and `exists` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.route.get") {
			if (!IsFieldValueType(payload, "route", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.get` requires object field `route`.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "agentId", "sessionId" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.get` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.route.restore") {
			if (!IsFieldValueType(payload, "route", '{') || !IsFieldBoolean(payload, "restored")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.restore` requires `route` object and `restored` boolean.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "channel", "accountId", "agentId", "sessionId" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.restore` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.routes.clear") {
			if (!IsFieldNumber(payload, "cleared") || !IsFieldNumber(payload, "remaining")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.routes.clear` requires numeric fields `cleared` and `remaining`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.routes.restore") {
			if (!IsFieldNumber(payload, "restored") || !IsFieldNumber(payload, "total")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.routes.restore` requires numeric fields `restored` and `total`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.routes.reset") {
			if (!IsFieldNumber(payload, "cleared") || !IsFieldNumber(payload, "restored") || !IsFieldNumber(payload, "total")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.routes.reset` requires numeric fields `cleared`, `restored`, and `total`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.channels.routes.count") {
			if (!IsFieldValueType(payload, "channel", '"') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.channels.routes.count` requires `channel` string and `count` number.");
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
				"gateway.config.exists",
				"gateway.config.keys",
				"gateway.config.count",
				"gateway.config.sections",
				"gateway.config.getSection",
				"gateway.config.schema",
				"gateway.config.validate",
				"gateway.config.audit",
				"gateway.config.rollback",
				"gateway.config.backup",
				"gateway.config.diff",
				"gateway.config.snapshot",
				"gateway.config.revision",
				"gateway.config.history",
				"gateway.config.profile",
				"gateway.config.template",
              "gateway.config.bundle",
				"gateway.transport.connections.count",
				"gateway.transport.endpoint.get",
				"gateway.transport.endpoint.set",
				"gateway.transport.endpoints.list",
				"gateway.transport.policy.get",
				"gateway.transport.policy.set",
				"gateway.transport.policy.reset",
				"gateway.transport.policy.status",
				"gateway.transport.policy.validate",
				"gateway.transport.policy.history",
				"gateway.transport.policy.metrics",
				"gateway.transport.policy.export",
				"gateway.transport.policy.import",
				"gateway.transport.policy.digest",
				"gateway.transport.policy.preview",
              "gateway.transport.policy.commit",
				"gateway.config.sections",
				"gateway.transport.endpoint.set",
				"gateway.health.details",
				"gateway.logs.count",
				"gateway.logs.levels",
				"gateway.events.get",
				"gateway.events.last",
				"gateway.events.search",
				"gateway.events.summary",
				"gateway.events.types",
				"gateway.events.channels",
				"gateway.events.timeline",
				"gateway.events.latestByType",
				"gateway.events.sample",
				"gateway.events.window",
				"gateway.events.recent",
				"gateway.events.batch",
				"gateway.events.cursor",
				"gateway.events.anchor",
              "gateway.events.offset",
				"gateway.events.last",
				"gateway.agents.create",
				"gateway.sessions.delete",
				"gateway.sessions.compact",
				"gateway.sessions.patch",
				"gateway.sessions.preview",
				"gateway.sessions.usage",
				"gateway.sessions.exists",
				"gateway.sessions.count",
				"gateway.sessions.activate",
				"gateway.events.exists",
				"gateway.events.count",
				"gateway.events.list",
				"gateway.events.get",
				"gateway.channels.accounts",
				"gateway.channels.accounts.activate",
				"gateway.channels.accounts.deactivate",
				"gateway.channels.accounts.exists",
				"gateway.channels.accounts.update",
				"gateway.channels.accounts.get",
				"gateway.channels.accounts.create",
				"gateway.channels.accounts.delete",
				"gateway.channels.accounts.clear",
				"gateway.channels.accounts.restore",
				"gateway.channels.accounts.count",
				"gateway.channels.accounts.reset",
				"gateway.channels.status.get",
				"gateway.channels.status.exists",
				"gateway.channels.status.count",
				"gateway.channels.route.exists",
				"gateway.channels.route.get",
				"gateway.channels.route.restore",
				"gateway.channels.route.reset",
				"gateway.channels.route.patch",
				"gateway.channels.routes.clear",
				"gateway.channels.routes.restore",
				"gateway.channels.routes.reset",
				"gateway.channels.routes.count",
				"gateway.tools.call.preview",
				"gateway.tools.exists",
				"gateway.tools.count",
				"gateway.tools.get",
				"gateway.tools.list",
				"gateway.tools.categories",
				"gateway.tools.stats",
				"gateway.tools.health",
				"gateway.tools.metrics",
				"gateway.tools.failures",
				"gateway.tools.usage",
				"gateway.tools.latency",
				"gateway.tools.errors",
				"gateway.tools.throughput",
				"gateway.tools.capacity",
                "gateway.tools.queue",
                "gateway.tools.scheduler",
                "gateway.tools.backlog",
				"gateway.models.exists",
				"gateway.models.count",
				"gateway.models.get",
				"gateway.models.listByProvider",
				"gateway.models.providers",
				"gateway.models.default.get",
				"gateway.models.compatibility",
				"gateway.models.recommended",
				"gateway.models.fallback",
				"gateway.models.selection",
				"gateway.models.routing",
				"gateway.models.preference",
				"gateway.models.priority",
				"gateway.models.affinity",
                "gateway.models.pool",
                "gateway.models.manifest",
                "gateway.models.catalog",
				"gateway.config.getKey",
				"gateway.transport.endpoint.exists",
				"gateway.tick",
				"gateway.health",
				"gateway.shutdown",
				})) {
				SetIssue(issue, "schema_invalid_response", "`gateway.features.list` catalog is missing required method/event members.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.offset") {
			if (!IsFieldNumber(payload, "offset") || !IsFieldValueType(payload, "event", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.offset` requires `offset` number and `event` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.anchor") {
			if (!IsFieldValueType(payload, "anchor", '"') || !IsFieldValueType(payload, "event", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.anchor` requires `anchor` string and `event` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.catalog") {
			if (!IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.catalog` requires `models` array and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.cursor") {
			if (!IsFieldValueType(payload, "cursor", '"') || !IsFieldValueType(payload, "event", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.cursor` requires `cursor` string and `event` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.backlog") {
			if (!IsFieldNumber(payload, "pending") || !IsFieldNumber(payload, "capacity") || !IsFieldNumber(payload, "tools")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.backlog` requires numeric fields `pending`, `capacity`, and `tools`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.manifest") {
			if (!IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "manifestVersion")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.manifest` requires `models` array and `manifestVersion` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.commit") {
			if (!IsFieldBoolean(payload, "committed") || !IsFieldNumber(payload, "version") || !IsFieldBoolean(payload, "applied")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.commit` requires `committed` boolean, `version` number, and `applied` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.batch") {
			if (!IsFieldValueType(payload, "batches", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.batch` requires `batches` array and `count` number.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, { "lifecycle", "updates" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.batch` is missing required batch names.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.bundle") {
			if (!IsFieldValueType(payload, "name", '"') || !IsFieldValueType(payload, "sections", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.bundle` requires `name` string, `sections` array, and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.scheduler") {
			if (!IsFieldNumber(payload, "ticks") || !IsFieldNumber(payload, "queued") || !IsFieldNumber(payload, "tools")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.scheduler` requires numeric fields `ticks`, `queued`, and `tools`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.pool") {
			if (!IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.pool` requires `models` array and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.preview") {
			if (!IsFieldValueType(payload, "path", '"') || !IsFieldBoolean(payload, "applied") || !IsFieldValueType(payload, "notes", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.preview` requires `path` string, `applied` boolean, and `notes` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.recent") {
			if (!IsFieldValueType(payload, "events", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.recent` requires `events` array and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.template") {
			if (!IsFieldValueType(payload, "template", '"') || !IsFieldValueType(payload, "keys", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.template` requires `template` string, `keys` array, and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.queue") {
			if (!IsFieldNumber(payload, "queued") || !IsFieldNumber(payload, "running") || !IsFieldNumber(payload, "tools")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.queue` requires numeric fields `queued`, `running`, and `tools`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.affinity") {
			if (!IsFieldValueType(payload, "models", '[') || !IsFieldValueType(payload, "affinity", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.affinity` requires `models` array and `affinity` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.digest") {
			if (!IsFieldValueType(payload, "digest", '"') || !IsFieldNumber(payload, "version")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.digest` requires `digest` string and `version` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.window") {
			if (!IsFieldValueType(payload, "events", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.window` requires `events` array and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.profile") {
			if (!IsFieldValueType(payload, "name", '"') || !IsFieldValueType(payload, "source", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.profile` requires `name` string and `source` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.capacity") {
			if (!IsFieldNumber(payload, "total") || !IsFieldNumber(payload, "used") || !IsFieldNumber(payload, "free")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.capacity` requires numeric fields `total`, `used`, and `free`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.priority") {
			if (!IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.priority` requires `models` array and `count` number.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, { "default", "reasoner" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.priority` is missing required model ids.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.import") {
			if (!IsFieldBoolean(payload, "imported") || !IsFieldNumber(payload, "version") || !IsFieldBoolean(payload, "applied")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.import` requires `imported` boolean, `version` number, and `applied` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.sample") {
			if (!IsFieldValueType(payload, "events", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.sample` requires `events` array and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.history") {
			if (!IsFieldValueType(payload, "revisions", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.history` requires `revisions` array and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.throughput") {
			if (!IsFieldNumber(payload, "calls") || !IsFieldNumber(payload, "windowSec") || !IsFieldNumber(payload, "perMinute") || !IsFieldNumber(payload, "tools")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.throughput` requires numeric fields `calls`, `windowSec`, `perMinute`, and `tools`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.preference") {
			if (!IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "provider", '"') || !IsFieldValueType(payload, "source", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.preference` requires `model` string, `provider` string, and `source` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.export") {
			if (!IsFieldValueType(payload, "path", '"') || !IsFieldNumber(payload, "version") || !IsFieldBoolean(payload, "exported")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.export` requires `path` string, `version` number, and `exported` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.latestByType") {
			if (!IsFieldValueType(payload, "type", '"') || !IsFieldValueType(payload, "event", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.latestByType` requires `type` string and `event` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.revision") {
			if (!IsFieldNumber(payload, "revision") || !IsFieldValueType(payload, "source", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.revision` requires `revision` number and `source` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.errors") {
			if (!IsFieldNumber(payload, "errors") || !IsFieldNumber(payload, "tools") || !IsFieldNumber(payload, "rate")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.errors` requires numeric fields `errors`, `tools`, and `rate`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.routing") {
			if (!IsFieldValueType(payload, "primary", '"') || !IsFieldValueType(payload, "fallback", '"') || !IsFieldValueType(payload, "strategy", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.routing` requires `primary` string, `fallback` string, and `strategy` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.metrics") {
			if (!IsFieldNumber(payload, "validations") || !IsFieldNumber(payload, "resets") || !IsFieldNumber(payload, "sets")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.metrics` requires numeric fields `validations`, `resets`, and `sets`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.timeline") {
			if (!IsFieldValueType(payload, "events", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.timeline` requires `events` array and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.snapshot") {
			if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.snapshot` requires `gateway` and `agent` object fields.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "bind", "port", "model", "streaming" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.snapshot` requires `bind`, `port`, `model`, and `streaming` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.latency") {
			if (!IsFieldNumber(payload, "minMs") || !IsFieldNumber(payload, "maxMs") || !IsFieldNumber(payload, "avgMs") || !IsFieldNumber(payload, "samples")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.latency` requires numeric fields `minMs`, `maxMs`, `avgMs`, and `samples`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.selection") {
			if (!IsFieldValueType(payload, "selected", '"') || !IsFieldValueType(payload, "strategy", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.selection` requires `selected` string and `strategy` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.history") {
			if (!IsFieldValueType(payload, "entries", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.history` requires `entries` array and `count` number.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "version", "applied", "reason" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.history` requires entry fields `version`, `applied`, and `reason`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.channels") {
			if (!IsFieldNumber(payload, "channelEvents") || !IsFieldNumber(payload, "accountEvents") || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.channels` requires numeric fields `channelEvents`, `accountEvents`, and `count`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.diff") {
			if (!IsFieldValueType(payload, "changed", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.diff` requires `changed` array and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.usage") {
			if (!IsFieldNumber(payload, "calls") || !IsFieldNumber(payload, "tools") || !IsFieldNumber(payload, "avgMs")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.usage` requires numeric fields `calls`, `tools`, and `avgMs`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.fallback") {
			if (!IsFieldValueType(payload, "preferred", '"') || !IsFieldValueType(payload, "fallback", '"') || !IsFieldBoolean(payload, "configured")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.fallback` requires `preferred` string, `fallback` string, and `configured` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.validate") {
			if (!IsFieldBoolean(payload, "valid") || !IsFieldValueType(payload, "errors", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.validate` requires `valid` boolean, `errors` array, and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.types") {
			if (!IsFieldValueType(payload, "types", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.types` requires `types` array and `count` number.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, { "lifecycle", "update" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.types` is missing required type members.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.backup") {
			if (!IsFieldBoolean(payload, "saved") || !IsFieldNumber(payload, "version") || !IsFieldValueType(payload, "path", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.backup` requires `saved` boolean, `version` number, and `path` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.failures") {
			if (!IsFieldNumber(payload, "failed") || !IsFieldNumber(payload, "total") || !IsFieldNumber(payload, "rate")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.failures` requires numeric fields `failed`, `total`, and `rate`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.recommended") {
			if (!IsFieldValueType(payload, "model", '{') || !IsFieldValueType(payload, "reason", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.recommended` requires `model` object and `reason` string.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "provider", "displayName", "streaming" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.recommended` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.status") {
			if (!IsFieldBoolean(payload, "mutable") || !IsFieldValueType(payload, "lastApplied", '"') || !IsFieldNumber(payload, "policyVersion")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.status` requires `mutable` boolean, `lastApplied` string, and `policyVersion` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.summary") {
			if (!IsFieldNumber(payload, "total") || !IsFieldNumber(payload, "lifecycle") || !IsFieldNumber(payload, "updates")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.summary` requires numeric fields `total`, `lifecycle`, and `updates`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.rollback") {
			if (!IsFieldBoolean(payload, "rolledBack") || !IsFieldNumber(payload, "version")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.rollback` requires `rolledBack` boolean and `version` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.metrics") {
			if (!IsFieldNumber(payload, "invocations") || !IsFieldNumber(payload, "enabled") || !IsFieldNumber(payload, "disabled") || !IsFieldNumber(payload, "total")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.metrics` requires numeric fields `invocations`, `enabled`, `disabled`, and `total`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.compatibility") {
			if (!IsFieldValueType(payload, "default", '"') || !IsFieldValueType(payload, "reasoner", '"') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.compatibility` requires `default` string, `reasoner` string, and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.reset") {
			if (!IsFieldBoolean(payload, "reset") || !IsFieldBoolean(payload, "applied")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.reset` requires `reset` boolean and `applied` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.search") {
			if (!IsFieldValueType(payload, "term", '"') || !IsFieldValueType(payload, "events", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.search` requires `term` string, `events` array, and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.audit") {
			if (!IsFieldBoolean(payload, "enabled") || !IsFieldValueType(payload, "source", '"') || !IsFieldNumber(payload, "lastUpdatedMs")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.audit` requires `enabled` boolean, `source` string, and `lastUpdatedMs` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.health") {
			if (!IsFieldBoolean(payload, "healthy") || !IsFieldNumber(payload, "enabled") || !IsFieldNumber(payload, "total")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.health` requires `healthy` boolean plus numeric `enabled` and `total` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.default.get") {
			if (!IsFieldValueType(payload, "model", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.default.get` requires `model` object.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "provider", "displayName", "streaming" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.default.get` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.set") {
			if (!IsFieldBoolean(payload, "applied") || !IsFieldValueType(payload, "reason", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.set` requires `applied` boolean and `reason` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.last") {
			if (!IsFieldValueType(payload, "event", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.last` requires `event` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.validate") {
			if (!IsFieldBoolean(payload, "valid") || !IsFieldValueType(payload, "errors", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.validate` requires `valid` boolean, `errors` array, and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.stats") {
			if (!IsFieldNumber(payload, "enabled") || !IsFieldNumber(payload, "disabled") || !IsFieldNumber(payload, "total")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.stats` requires numeric fields `enabled`, `disabled`, and `total`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.providers") {
			if (!IsFieldValueType(payload, "providers", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.providers` requires `providers` array and `count` number.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, { "seed" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.providers` is missing required provider members.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.policy.get") {
			if (!IsFieldBoolean(payload, "exclusiveAddrUse") || !IsFieldBoolean(payload, "keepAlive") || !IsFieldBoolean(payload, "noDelay") || !IsFieldNumber(payload, "idleTimeoutMs") || !IsFieldNumber(payload, "handshakeTimeoutMs")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.get` requires policy booleans and timeout number fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.list") {
			if (!IsFieldValueType(payload, "events", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.list` requires `events` array and `count` number.");
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
				SetIssue(issue, "schema_invalid_response", "`gateway.events.list` is missing required event members.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.schema") {
			if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.schema` requires `gateway` and `agent` object fields.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "bind", "port", "model", "streaming" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.schema` requires `bind`, `port`, `model`, and `streaming` schema members.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.categories") {
			if (!IsFieldValueType(payload, "categories", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.categories` requires `categories` array and `count` number.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, { "messaging", "knowledge" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.categories` is missing required category members.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.listByProvider") {
			if (!IsFieldValueType(payload, "provider", '"') || !IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.listByProvider` requires `provider` string, `models` array, and `count` number.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "models")) {
				return true;
			}

			if (method == "gateway.transport.endpoints.list") {
				if (!IsFieldValueType(payload, "endpoints", '[') || !IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.endpoints.list` requires `endpoints` array and `count` number.");
					return false;
				}

				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "provider", "displayName", "streaming" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.listByProvider` requires model entries with `id`, `provider`, `displayName`, and `streaming` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.getSection") {
			if (!IsFieldValueType(payload, "section", '"') || !IsFieldValueType(payload, "config", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.getSection` requires `section` string and `config` object.");
				return false;
			}

			return true;
		}

		if (method == "gateway.events.get") {
			if (!IsFieldValueType(payload, "event", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.events.get` requires `event` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.list") {
			if (!IsFieldValueType(payload, "tools", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.list` requires `tools` array and `count` number.");
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, "tools")) {
				return true;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "label", "category", "enabled" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.list` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.health") {
			return IsFieldValueType(payload, "status", '"') && IsFieldBoolean(payload, "running")
				? true
				: (SetIssue(issue, "schema_invalid_response", "`gateway.health` requires `status` string and `running` boolean."), false);
		}

		if (method == "gateway.health.details") {
			if (!IsFieldValueType(payload, "status", '"') ||
				!IsFieldBoolean(payload, "running") ||
				!IsFieldValueType(payload, "transport", '{') ||
				!IsFieldValueType(payload, "endpoint", '"') ||
				!IsFieldNumber(payload, "connections")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.health.details` requires `status`, `running`, and `transport.{endpoint,connections}` fields.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.endpoint.set") {
			if (!IsFieldValueType(payload, "endpoint", '"') || !IsFieldBoolean(payload, "updated")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.endpoint.set` requires `endpoint` string and `updated` boolean.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.sections") {
			if (!IsFieldValueType(payload, "sections", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.sections` requires `sections` array and `count` number.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, { "gateway", "agent" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.sections` is missing required section members.");
				return false;
			}

			return true;
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

		if (method == "gateway.transport.connections.count") {
			if (!IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.connections.count` requires numeric field `count`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.count") {
			if (!IsFieldValueType(payload, "section", '"') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.count` requires `section` string and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.config.getKey") {
			if (!IsFieldValueType(payload, "key", '"') || !IsFieldValueType(payload, "value", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.config.getKey` requires `key` string and `value` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.count") {
			if (!IsFieldValueType(payload, "provider", '"') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.count` requires `provider` string and `count` number.");
				return false;
			}

			return true;
		}

		if (method == "gateway.models.get") {
			if (!IsFieldValueType(payload, "model", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.get` requires `model` object.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "provider", "displayName", "streaming" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.models.get` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.tools.get") {
			if (!IsFieldValueType(payload, "tool", '{')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.get` requires `tool` object.");
				return false;
			}

			if (!PayloadContainsAllFieldTokens(payload, { "id", "label", "category", "enabled" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.tools.get` requires tool fields `id`, `label`, `category`, and `enabled`.");
				return false;
			}

			return true;
		}

		if (method == "gateway.logs.levels") {
			if (!IsFieldValueType(payload, "levels", '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.logs.levels` requires `levels` array and `count` number.");
				return false;
			}

			if (!PayloadContainsAllStringValues(payload, { "info", "debug" })) {
				SetIssue(issue, "schema_invalid_response", "`gateway.logs.levels` is missing required level members.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.endpoint.get") {
			if (!IsFieldValueType(payload, "endpoint", '"')) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.endpoint.get` requires `endpoint` string.");
				return false;
			}

			return true;
		}

		if (method == "gateway.transport.endpoint.exists") {
			if (!IsFieldValueType(payload, "endpoint", '"') || !IsFieldBoolean(payload, "exists")) {
				SetIssue(issue, "schema_invalid_response", "`gateway.transport.endpoint.exists` requires `endpoint` string and `exists` boolean.");
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
