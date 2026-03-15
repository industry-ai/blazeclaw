#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"

#include <functional>
#include <unordered_set>

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

		using RequestValidator = std::function<bool(const RequestFrame&, SchemaValidationIssue&)>;

		static const std::unordered_map<std::string, RequestValidator> directValidators = {
			{ "gateway.ping", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidatePingParams(r, i); } },
			{ "gateway.logs.tail", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateLogsTailParams(r, i); } },
			{ "gateway.logs.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateLogsCountParams(r, i); } },
			{ "gateway.sessions.compact", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionsCompactParams(r, i); } },
			{ "gateway.sessions.preview", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionsPreviewParams(r, i); } },
			{ "gateway.sessions.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionsCountParams(r, i); } },
			{ "gateway.sessions.patch", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionMutationParams(r, i, r.method); } },
			{ "gateway.sessions.create", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionMutationParams(r, i, r.method); } },
			{ "gateway.sessions.reset", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionMutationParams(r, i, r.method); } },
			{ "gateway.channels.route.resolve", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteResolveParams(r, i); } },
			{ "gateway.channels.route.set", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteSetParams(r, i); } },
			{ "gateway.channels.route.delete", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteDeleteParams(r, i); } },
			{ "gateway.channels.route.exists", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteExistsParams(r, i); } },
			{ "gateway.channels.route.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteGetParams(r, i); } },
			{ "gateway.channels.route.restore", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteRestoreParams(r, i); } },
			{ "gateway.channels.route.reset", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteRestoreParams(r, i); } },
			{ "gateway.channels.route.patch", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutePatchParams(r, i); } },
			{ "gateway.channels.routes.clear", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutesClearParams(r, i); } },
			{ "gateway.channels.routes.restore", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutesRestoreParams(r, i); } },
			{ "gateway.channels.routes.reset", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutesResetParams(r, i); } },
			{ "gateway.channels.routes.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutesCountParams(r, i); } },
			{ "gateway.channels.accounts.activate", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsActivateParams(r, i); } },
			{ "gateway.channels.accounts.deactivate", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsDeactivateParams(r, i); } },
			{ "gateway.channels.accounts.exists", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsExistsParams(r, i); } },
			{ "gateway.channels.accounts.update", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsUpdateParams(r, i); } },
			{ "gateway.channels.accounts.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsGetParams(r, i); } },
			{ "gateway.channels.accounts.create", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsCreateParams(r, i); } },
			{ "gateway.channels.accounts.delete", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsDeleteParams(r, i); } },
			{ "gateway.channels.accounts.clear", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsClearParams(r, i); } },
			{ "gateway.channels.accounts.restore", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsRestoreParams(r, i); } },
			{ "gateway.channels.accounts.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsCountParams(r, i); } },
			{ "gateway.channels.accounts.reset", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.status.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.status.exists", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.status.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.status", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.routes", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.accounts", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.tools.call.preview", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateToolsCallPreviewParams(r, i); } },
			{ "gateway.tools.call.execute", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateToolsCallExecuteParams(r, i); } },
			{ "gateway.agents.create", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsCreateParams(r, i); } },
			{ "gateway.agents.update", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsCreateParams(r, i); } },
			{ "gateway.agents.files.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesListParams(r, i); } },
			{ "gateway.agents.files.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesGetParams(r, i); } },
			{ "gateway.agents.files.set", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesSetParams(r, i); } },
			{ "gateway.agents.files.delete", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesDeleteParams(r, i); } },
			{ "gateway.agents.files.exists", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesExistsParams(r, i); } },
			{ "gateway.channels.logout", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsLogoutParams(r, i); } },
			{ "gateway.config.set", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateConfigSetParams(r, i); } },
			{ "gateway.tools.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalActiveParam(r, i, r.method); } },
			{ "gateway.agents.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalActiveParam(r, i, r.method); } },
			{ "gateway.agents.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalActiveParam(r, i, r.method); } },
			{ "gateway.session.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalSessionListParams(r, i, r.method); } },
		};

		if (const auto it = directValidators.find(request.method); it != directValidators.end()) {
			return it->second(request, issue);
		}

		static const std::unordered_map<std::string, const char*> stringIdFields = {
			{ "gateway.sessions.resolve", "sessionId" },
			{ "gateway.sessions.exists", "sessionId" },
			{ "gateway.sessions.activate", "sessionId" },
			{ "gateway.sessions.delete", "sessionId" },
			{ "gateway.sessions.usage", "sessionId" },
			{ "gateway.agents.get", "agentId" },
			{ "gateway.agents.activate", "agentId" },
			{ "gateway.agents.delete", "agentId" },
			{ "gateway.agents.exists", "agentId" },
			{ "gateway.config.exists", "key" },
			{ "gateway.config.getKey", "key" },
			{ "gateway.config.count", "section" },
			{ "gateway.config.getSection", "section" },
			{ "gateway.events.exists", "event" },
			{ "gateway.events.count", "event" },
			{ "gateway.events.get", "event" },
			{ "gateway.events.search", "term" },
			{ "gateway.events.latestByType", "type" },
			{ "gateway.models.exists", "modelId" },
			{ "gateway.models.get", "modelId" },
			{ "gateway.models.count", "provider" },
			{ "gateway.models.listByProvider", "provider" },
			{ "gateway.tools.exists", "tool" },
			{ "gateway.tools.get", "tool" },
			{ "gateway.tools.list", "category" },
			{ "gateway.transport.endpoint.exists", "endpoint" },
			{ "gateway.transport.endpoint.set", "endpoint" },
		};

		if (const auto it = stringIdFields.find(request.method); it != stringIdFields.end()) {
			return ValidateStringIdParam(request, issue, request.method, it->second);
		}

		static const std::unordered_set<std::string> noParamsMethods = {
			"gateway.protocol.version", "gateway.features.list", "gateway.config.get", "gateway.config.keys",
			"gateway.runtime.orchestration.status", "gateway.runtime.streaming.status", "gateway.models.failover.status",
			"gateway.runtime.orchestration.queue", "gateway.runtime.streaming.sample", "gateway.models.failover.preview",
          "gateway.runtime.orchestration.assign", "gateway.runtime.streaming.window", "gateway.models.failover.metrics",
          "gateway.runtime.orchestration.rebalance", "gateway.runtime.streaming.backpressure",
			"gateway.models.failover.simulate",
          "gateway.runtime.orchestration.drain", "gateway.runtime.streaming.replay",
			"gateway.models.failover.audit",
          "gateway.runtime.orchestration.snapshot", "gateway.runtime.streaming.cursor",
			"gateway.models.failover.policy",
			"gateway.channels.adapters.list", "gateway.tools.executions.list",
			"gateway.config.sections", "gateway.config.schema", "gateway.config.validate", "gateway.config.audit",
			"gateway.config.rollback", "gateway.config.backup", "gateway.config.diff", "gateway.config.snapshot",
			"gateway.config.revision", "gateway.config.history", "gateway.config.profile", "gateway.config.template",
			"gateway.config.bundle", "gateway.config.package", "gateway.config.archive", "gateway.config.manifest",
			"gateway.config.index", "gateway.config.state", "gateway.transport.endpoint.get",
			"gateway.config.session", "gateway.config.tokenKey", "gateway.config.sequenceKey",
			"gateway.config.streamKey", "gateway.config.bundleKey", "gateway.config.packageKey",
			"gateway.config.archiveKey", "gateway.config.manifestKey", "gateway.config.profileKey",
			"gateway.config.templateKey", "gateway.config.revisionKey", "gateway.config.historyKey",
			"gateway.config.snapshotKey", "gateway.config.indexKey", "gateway.config.windowScopeKey",
			"gateway.config.cursorScopeKey", "gateway.config.offsetScopeKey", "gateway.config.anchorScopeKey",
			"gateway.config.pointerScopeKey", "gateway.config.markerScopeKey", "gateway.config.tokenScopeKey",
			"gateway.config.streamScopeKey", "gateway.config.sequenceScopeKey", "gateway.config.bundleScopeKey",
			"gateway.config.packageScopeKey", "gateway.config.archiveScopeKey", "gateway.config.manifestScopeKey",
			"gateway.config.profileScopeKey", "gateway.config.templateScopeKey", "gateway.config.revisionScopeKey",
			"gateway.config.historyScopeKey", "gateway.config.snapshotScopeKey", "gateway.config.indexScopeKey",
			"gateway.config.windowScopeId", "gateway.config.cursorScopeId", "gateway.config.anchorScopeId",
			"gateway.config.offsetScopeId", "gateway.config.pointerScopeId", "gateway.config.tokenScopeId",
			"gateway.config.sequenceScopeId", "gateway.config.streamScopeId", "gateway.config.bundleScopeId",
			"gateway.config.packageScopeId", "gateway.config.archiveScopeId", "gateway.config.manifestScopeId",
			"gateway.config.profileScopeId", "gateway.config.templateScopeId", "gateway.config.revisionScopeId",
			"gateway.config.historyScopeId", "gateway.config.snapshotScopeId", "gateway.config.indexScopeId",
			"gateway.config.markerScopeId",
			"gateway.transport.connections.count", "gateway.transport.endpoints.list", "gateway.transport.policy.get",
			"gateway.transport.policy.set", "gateway.transport.policy.reset", "gateway.transport.policy.status",
			"gateway.transport.policy.validate", "gateway.transport.policy.history", "gateway.transport.policy.metrics",
			"gateway.transport.policy.export", "gateway.transport.policy.import", "gateway.transport.policy.digest",
			"gateway.transport.policy.preview", "gateway.transport.policy.commit", "gateway.transport.policy.apply",
			"gateway.transport.policy.stage", "gateway.transport.policy.reconcile", "gateway.transport.policy.sync",
			"gateway.transport.policy.refresh", "gateway.transport.policy.session", "gateway.transport.policy.scope",
			"gateway.transport.policy.context", "gateway.transport.policy.channel", "gateway.transport.policy.route",
			"gateway.transport.policy.account", "gateway.transport.policy.agent", "gateway.transport.policy.model",
			"gateway.transport.policy.config", "gateway.transport.policy.policy",
			"gateway.transport.policy.tool", "gateway.transport.policy.transport", "gateway.transport.policy.runtime", "gateway.transport.policy.stateKey",
			"gateway.transport.policy.healthKey", "gateway.transport.policy.log", "gateway.transport.policy.metric", "gateway.transport.policy.trace",
			"gateway.transport.policy.audit", "gateway.transport.policy.debug", "gateway.transport.policy.cache", "gateway.transport.policy.queueKey",
			"gateway.transport.policy.windowKey", "gateway.transport.policy.cursorKey", "gateway.transport.policy.anchorKey", "gateway.transport.policy.offsetKey",
			"gateway.transport.policy.markerKey", "gateway.transport.policy.pointerKey", "gateway.transport.policy.tokenKey", "gateway.transport.policy.sequenceKey",
			"gateway.transport.policy.streamKey", "gateway.transport.policy.bundleKey", "gateway.transport.policy.packageKey", "gateway.transport.policy.archiveKey",
			"gateway.transport.policy.manifestKey", "gateway.transport.policy.profileKey", "gateway.transport.policy.templateKey", "gateway.transport.policy.revisionKey",
			"gateway.transport.policy.historyKey", "gateway.transport.policy.snapshotKey", "gateway.transport.policy.indexKey", "gateway.transport.policy.windowScopeKey",
			"gateway.transport.policy.cursorScopeKey", "gateway.transport.policy.offsetScopeKey", "gateway.transport.policy.anchorScopeKey", "gateway.transport.policy.pointerScopeKey",
			"gateway.transport.policy.markerScopeKey", "gateway.transport.policy.tokenScopeKey", "gateway.transport.policy.streamScopeKey", "gateway.transport.policy.sequenceScopeKey",
			"gateway.transport.policy.bundleScopeKey", "gateway.transport.policy.packageScopeKey", "gateway.transport.policy.archiveScopeKey", "gateway.transport.policy.manifestScopeKey",
			"gateway.transport.policy.profileScopeKey", "gateway.transport.policy.templateScopeKey", "gateway.transport.policy.revisionScopeKey", "gateway.transport.policy.historyScopeKey",
			"gateway.transport.policy.snapshotScopeKey", "gateway.transport.policy.indexScopeKey",
			"gateway.transport.policy.windowScopeId", "gateway.transport.policy.cursorScopeId",
			"gateway.transport.policy.anchorScopeId", "gateway.transport.policy.offsetScopeId", "gateway.transport.policy.pointerScopeId",
			"gateway.transport.policy.tokenScopeId", "gateway.transport.policy.sequenceScopeId", "gateway.transport.policy.streamScopeId",
			"gateway.transport.policy.bundleScopeId", "gateway.transport.policy.packageScopeId", "gateway.transport.policy.archiveScopeId",
			"gateway.transport.policy.manifestScopeId", "gateway.transport.policy.profileScopeId", "gateway.transport.policy.templateScopeId",
			"gateway.transport.policy.revisionScopeId", "gateway.transport.policy.historyScopeId", "gateway.transport.policy.snapshotScopeId",
			"gateway.transport.policy.indexScopeId", "gateway.transport.policy.markerScopeId",
			"gateway.logs.levels", "gateway.events.catalog", "gateway.events.list",
			"gateway.events.last", "gateway.events.summary", "gateway.events.types", "gateway.events.channels",
			"gateway.events.timeline", "gateway.events.sample", "gateway.events.window", "gateway.events.recent",
			"gateway.events.batch", "gateway.events.cursor", "gateway.events.anchor", "gateway.events.offset",
			"gateway.events.marker", "gateway.events.sequence", "gateway.events.pointer", "gateway.events.token",
			"gateway.events.stream", "gateway.events.sessionKey", "gateway.events.scopeKey", "gateway.events.contextKey",
			"gateway.events.channelKey", "gateway.events.routeKey", "gateway.events.accountKey",
			"gateway.events.agentKey", "gateway.events.modelKey", "gateway.events.configKey", "gateway.events.policyKey",
			"gateway.events.toolKey", "gateway.events.transportKey", "gateway.events.runtimeKey", "gateway.events.stateKey",
			"gateway.events.healthKey", "gateway.events.logKey", "gateway.events.metricKey", "gateway.events.traceKey",
			"gateway.events.auditKey", "gateway.events.debugKey", "gateway.events.cacheKey", "gateway.events.queueKey",
			"gateway.events.windowKey", "gateway.events.cursorKey", "gateway.events.anchorKey", "gateway.events.offsetKey",
			"gateway.events.markerKey", "gateway.events.pointerKey", "gateway.events.tokenKey", "gateway.events.sequenceKey",
			"gateway.events.streamKey", "gateway.events.bundleKey", "gateway.events.packageKey", "gateway.events.archiveKey",
			"gateway.events.manifestKey", "gateway.events.profileKey", "gateway.events.templateKey", "gateway.events.revisionKey",
			"gateway.events.historyKey", "gateway.events.snapshotKey", "gateway.events.indexKey", "gateway.events.windowScopeKey",
			"gateway.events.cursorScopeKey", "gateway.events.offsetScopeKey", "gateway.events.anchorScopeKey", "gateway.events.pointerScopeKey",
			"gateway.events.markerScopeKey", "gateway.events.tokenScopeKey", "gateway.events.streamScopeKey", "gateway.events.sequenceScopeKey",
			"gateway.events.bundleScopeKey", "gateway.events.packageScopeKey", "gateway.events.archiveScopeKey", "gateway.events.manifestScopeKey",
			"gateway.events.profileScopeKey", "gateway.events.templateScopeKey", "gateway.events.revisionScopeKey", "gateway.events.historyScopeKey", 
			"gateway.events.snapshotScopeKey", "gateway.events.indexScopeKey", "gateway.events.windowScopeId",
			"gateway.events.cursorScopeId", "gateway.events.anchorScopeId", "gateway.events.offsetScopeId",
			"gateway.events.pointerScopeId", "gateway.events.tokenScopeId", "gateway.models.list", "gateway.models.providers",
			"gateway.events.sequenceScopeId", "gateway.events.streamScopeId", "gateway.events.bundleScopeId",
			"gateway.events.packageScopeId", "gateway.events.archiveScopeId", "gateway.events.manifestScopeId",
			"gateway.events.profileScopeId", "gateway.events.templateScopeId", "gateway.events.revisionScopeId",
			"gateway.events.historyScopeId", "gateway.events.snapshotScopeId", "gateway.events.indexScopeId",
			"gateway.events.markerScopeId", "gateway.models.default.get",
			"gateway.models.compatibility", "gateway.models.recommended", "gateway.models.fallback",
			"gateway.models.selection", "gateway.models.routing", "gateway.models.preference", "gateway.models.priority",
			"gateway.models.affinity", "gateway.models.pool", "gateway.models.manifest", "gateway.models.catalog",
			"gateway.models.inventory", "gateway.models.snapshot", "gateway.models.registry", "gateway.models.index",
			"gateway.models.state", "gateway.models.session", "gateway.models.scope", "gateway.models.context", "gateway.models.channel",
			"gateway.models.route", "gateway.models.account", "gateway.models.agent", "gateway.models.model", "gateway.models.config",
			"gateway.models.policy", "gateway.models.tool", "gateway.models.transport", "gateway.models.runtime", "gateway.models.stateKey",
			"gateway.models.healthKey", "gateway.models.log", "gateway.models.metric", "gateway.models.trace", "gateway.models.audit",
			"gateway.models.debug", "gateway.models.cache", "gateway.models.queueKey", "gateway.models.windowKey", "gateway.models.cursorKey", "gateway.models.anchorKey",
			"gateway.models.offsetKey", "gateway.models.markerKey", "gateway.models.pointerKey", "gateway.models.tokenKey",
			"gateway.models.sequenceKey", "gateway.models.streamKey", "gateway.models.bundleKey", "gateway.models.packageKey",
			"gateway.models.archiveKey", "gateway.models.manifestKey", "gateway.models.profileKey", "gateway.models.templateKey",
			"gateway.models.revisionKey", "gateway.models.historyKey", "gateway.models.snapshotKey", "gateway.models.indexKey",
			"gateway.models.windowScopeKey", "gateway.models.cursorScopeKey", "gateway.models.offsetScopeKey",
			"gateway.models.anchorScopeKey", "gateway.models.pointerScopeKey", "gateway.models.markerScopeKey",
			"gateway.models.tokenScopeKey", "gateway.models.streamScopeKey", "gateway.models.sequenceScopeKey",
			"gateway.models.bundleScopeKey", "gateway.models.packageScopeKey", "gateway.models.archiveScopeKey",
			"gateway.models.manifestScopeKey", "gateway.models.profileScopeKey", "gateway.models.templateScopeKey",
			"gateway.models.revisionScopeKey", "gateway.models.historyScopeKey", "gateway.models.snapshotScopeKey",
			"gateway.models.indexScopeKey", "gateway.models.windowScopeId", "gateway.models.cursorScopeId",
			"gateway.models.anchorScopeId", "gateway.models.offsetScopeId", "gateway.models.pointerScopeId",
			"gateway.models.tokenScopeId", "gateway.models.sequenceScopeId", "gateway.models.streamScopeId",
			"gateway.models.bundleScopeId", "gateway.models.packageScopeId", "gateway.models.archiveScopeId",
			"gateway.models.manifestScopeId", "gateway.models.profileScopeId", "gateway.models.templateScopeId",
			"gateway.models.revisionScopeId", "gateway.models.historyScopeId", "gateway.models.snapshotScopeId",
			"gateway.models.indexScopeId", "gateway.models.markerScopeId",
			"gateway.tools.health", "gateway.tools.stats", "gateway.tools.failures",
			"gateway.tools.usage", "gateway.tools.latency", "gateway.tools.errors", "gateway.tools.throughput",
			"gateway.tools.capacity", "gateway.tools.queue", "gateway.tools.scheduler", "gateway.tools.backlog",
			"gateway.tools.window", "gateway.tools.pipeline", "gateway.tools.dispatch", "gateway.tools.router",
			"gateway.tools.selector", "gateway.tools.mapper", "gateway.tools.binding", "gateway.tools.profile",
			"gateway.tools.channel", "gateway.tools.route", "gateway.tools.account",
			"gateway.tools.agent", "gateway.tools.model", "gateway.tools.config", "gateway.tools.policy",
			"gateway.tools.tool", "gateway.tools.transport", "gateway.tools.runtime", "gateway.tools.state",
			"gateway.tools.healthKey", "gateway.tools.log", "gateway.tools.metric", "gateway.tools.trace",
			"gateway.tools.audit", "gateway.tools.debug", "gateway.tools.cache", "gateway.tools.queueKey",
			"gateway.tools.windowKey", "gateway.tools.cursorKey", "gateway.tools.anchorKey",
			"gateway.tools.offsetKey", "gateway.tools.markerKey", "gateway.tools.pointerKey",
			"gateway.tools.tokenKey", "gateway.tools.sequenceKey", "gateway.tools.streamKey",
			"gateway.tools.bundleKey", "gateway.tools.packageKey", "gateway.tools.archiveKey",
			"gateway.tools.manifestKey", "gateway.tools.profileKey", "gateway.tools.templateKey", "gateway.tools.revisionKey",
			"gateway.tools.historyKey", "gateway.tools.snapshotKey", "gateway.tools.indexKey", "gateway.tools.windowScopeKey",
			"gateway.tools.cursorScopeKey", "gateway.tools.offsetScopeKey", "gateway.tools.anchorScopeKey", "gateway.tools.pointerScopeKey",
			"gateway.tools.markerScopeKey", "gateway.tools.tokenScopeKey", "gateway.tools.streamScopeKey", "gateway.tools.sequenceScopeKey",
			"gateway.tools.bundleScopeKey", "gateway.tools.packageScopeKey", "gateway.tools.archiveScopeKey", "gateway.tools.manifestScopeKey",
			"gateway.tools.profileScopeKey", "gateway.tools.templateScopeKey", "gateway.tools.revisionScopeKey", "gateway.tools.historyScopeKey",
			"gateway.tools.snapshotScopeKey", "gateway.tools.indexScopeKey", "gateway.tools.windowScopeId", "gateway.tools.metrics",
			"gateway.tools.cursorScopeId", "gateway.tools.anchorScopeId", "gateway.tools.offsetScopeId", "gateway.tools.pointerScopeId",
			"gateway.tools.tokenScopeId", "gateway.tools.sequenceScopeId", "gateway.tools.streamScopeId",
			"gateway.tools.bundleScopeId", "gateway.tools.packageScopeId", "gateway.tools.archiveScopeId",
			"gateway.tools.manifestScopeId", "gateway.tools.profileScopeId", "gateway.tools.templateScopeId",
			"gateway.tools.revisionScopeId", "gateway.tools.historyScopeId", "gateway.tools.snapshotScopeId",
			"gateway.tools.indexScopeId", "gateway.tools.markerScopeId",
			"gateway.tools.categories", "gateway.tools.catalog",
			"gateway.health", "gateway.health.details", "gateway.transport.status",
		};

		if (noParamsMethods.contains(request.method)) {
			return ValidateNoParamsAllowed(request, issue, request.method);
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
