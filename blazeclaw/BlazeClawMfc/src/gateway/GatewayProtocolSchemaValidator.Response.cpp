#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"
#include "GatewayProtocolSchemaValidator.Internal.h"
#include "generated/GatewaySchemaCatalog.Generated.h"

#include <functional>
#include <string_view>
#include <unordered_map>

namespace blazeclaw::gateway::protocol {
	namespace {
		constexpr const char* kSessionFieldTokens[] = { "id", "scope", "active" };
		constexpr const char* kAgentFieldTokens[] = { "id", "name", "active" };
		constexpr const char* kModelFieldTokens[] = { "id", "provider", "displayName", "streaming" };
		constexpr const char* kRouteFieldTokens[] = { "channel", "accountId", "agentId", "sessionId" };
		constexpr const char* kAccountFieldTokens[] = { "channel", "accountId", "label", "active", "connected" };
		constexpr const char* kChannelFieldTokens[] = { "id", "label", "connected", "accounts" };
		constexpr const char* kToolFieldTokens[] = { "id", "label", "category", "enabled" };
		constexpr const char* kToolExecutionFieldTokens[] = { "tool", "executed", "status", "output", "argsProvided" };
		constexpr const char* kChannelAdapterFieldTokens[] = { "id", "label", "defaultAccountId" };
		constexpr const char* kFileFieldTokens[] = { "path", "size", "updatedMs", "content" };
		constexpr const char* kFileListFieldTokens[] = { "path", "size", "updatedMs" };
		constexpr const char* kCatalogEventNames[] = {
			"gateway.tick",
			"gateway.health",
			"gateway.shutdown",
			"gateway.channels.update",
			"gateway.channels.accounts.update",
			"gateway.session.reset",
			"gateway.agent.update",
			"gateway.tools.catalog.update",
		};

		bool PayloadContainsGeneratedMethodRules(const std::string& payload) {
			for (const auto& rule : generated::GetSchemaMethodRules()) {
				if (rule.name == nullptr) {
					continue;
				}

				const std::string token = "\"" + std::string(rule.name) + "\"";
				if (payload.find(token) == std::string::npos) {
					return false;
				}
			}

			return true;
		}

		bool PayloadContainsGeneratedPatternCoverage(
			const std::string& payload,
			std::initializer_list<const char*> requiredPatterns) {
			for (const char* requiredPattern : requiredPatterns) {
				if (requiredPattern == nullptr) {
					continue;
				}

				bool matchedPatternRule = false;
				for (const auto& patternRule : generated::GetSchemaMethodPatternRules()) {
					if (patternRule.pattern == nullptr ||
						std::string_view(patternRule.pattern) != requiredPattern) {
						continue;
					}

					matchedPatternRule = true;
					const std::string_view patternView(patternRule.pattern);
					std::string prefix;
					if (patternView.size() >= 2 &&
						patternView.substr(patternView.size() - 2) == ".*") {
						prefix = std::string(patternView.substr(0, patternView.size() - 1));
					}
					else {
						prefix = std::string(patternView);
					}

					const std::string token = "\"" + prefix;
					if (payload.find(token) == std::string::npos) {
						return false;
					}

					break;
				}

				if (!matchedPatternRule) {
					return false;
				}
			}

			return true;
		}

		// Legacy all-catalog hardcoded method array removed in PR5.
		// Generated schema catalog membership checks now serve this role.

		bool ValidateObjectWithTokens(
			const std::string& payload,
			const char* objectField,
			const char* const* tokens,
			std::size_t tokenCount,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, objectField, '{')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			for (std::size_t index = 0; index < tokenCount; ++index) {
				std::size_t tokenPos = 0;
				if (!ContainsFieldToken(payload, tokens[index], tokenPos)) {
					SetIssue(issue, "schema_invalid_response", errorMessage);
					return false;
				}
			}

			return true;
		}

		template <std::size_t N>
		bool ValidateObjectWithTokens(
			const std::string& payload,
			const char* objectField,
			const char* const (&tokens)[N],
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			return ValidateObjectWithTokens(payload, objectField, tokens, N, issue, errorMessage);
		}

		template <std::size_t N>
		bool ValidateObjectWithBooleanAndTokens(
			const std::string& payload,
			const char* objectField,
			const char* booleanField,
			const char* const (&tokens)[N],
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, objectField, '{') || !IsFieldBoolean(payload, booleanField)) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return PayloadContainsAllFieldTokens(payload, tokens)
				? true
				: (SetIssue(issue, "schema_invalid_response", errorMessage), false);
		}

		template <std::size_t N>
		bool ValidateArrayWithOptionalEntryTokens(
			const std::string& payload,
			const char* arrayField,
			const char* const (&tokens)[N],
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, arrayField, '[')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			if (IsArrayFieldExplicitlyEmpty(payload, arrayField)) {
				return true;
			}

			for (const char* token : tokens) {
				std::size_t tokenPos = 0;
				if (!ContainsFieldToken(payload, token, tokenPos)) {
					SetIssue(issue, "schema_invalid_response", errorMessage);
					return false;
				}
			}

			return true;
		}

		bool ValidateTwoStringFields(
			const std::string& payload,
			const char* first,
			const char* second,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, first, '"') || !IsFieldValueType(payload, second, '"')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateBooleanNumberBoolean(
			const std::string& payload,
			const char* boolField1,
			const char* numberField,
			const char* boolField2,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldBoolean(payload, boolField1) || !IsFieldNumber(payload, numberField) || !IsFieldBoolean(payload, boolField2)) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateArrayAndCount(
			const std::string& payload,
			const char* arrayField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, arrayField, '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateNumericFields(
			const std::string& payload,
			std::initializer_list<const char*> fieldNames,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			for (const char* fieldName : fieldNames) {
				if (!IsFieldNumber(payload, fieldName)) {
					SetIssue(issue, "schema_invalid_response", errorMessage);
					return false;
				}
			}

			return true;
		}

		bool ValidateStringArrayAndCount(
			const std::string& payload,
			const char* stringField,
			const char* arrayField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, stringField, '"') || !IsFieldValueType(payload, arrayField, '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateNumberAndString(
			const std::string& payload,
			const char* numberField,
			const char* stringField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldNumber(payload, numberField) || !IsFieldValueType(payload, stringField, '"')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateStringAndBoolean(
			const std::string& payload,
			const char* stringField,
			const char* booleanField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldValueType(payload, stringField, '"') || !IsFieldBoolean(payload, booleanField)) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateBooleanArrayAndCount(
			const std::string& payload,
			const char* booleanField,
			const char* arrayField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldBoolean(payload, booleanField) || !IsFieldValueType(payload, arrayField, '[') || !IsFieldNumber(payload, "count")) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateBooleanAndString(
			const std::string& payload,
			const char* booleanField,
			const char* stringField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldBoolean(payload, booleanField) || !IsFieldValueType(payload, stringField, '"')) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}

		bool ValidateBooleanAndNumber(
			const std::string& payload,
			const char* booleanField,
			const char* numberField,
			SchemaValidationIssue& issue,
			const std::string& errorMessage) {
			if (!IsFieldBoolean(payload, booleanField) || !IsFieldNumber(payload, numberField)) {
				SetIssue(issue, "schema_invalid_response", errorMessage);
				return false;
			}

			return true;
		}
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

		const std::unordered_map<std::string, std::function<bool()>> groupedValidators = {
			{ "gateway.channels.status", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"channels",
					kChannelFieldTokens,
					issue,
					"`gateway.channels.status` requires channel entries with `id`, `label`, `connected`, and `accounts` fields.");
			} },
			{ "gateway.channels.routes", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"routes",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.routes` requires route entries with `channel`, `accountId`, `agentId`, and `sessionId` fields.");
			} },
			{ "gateway.channels.accounts", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"accounts",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts` requires account entries with `channel`, `accountId`, `label`, `active`, and `connected` fields.");
			} },
			{ "gateway.channels.adapters.list", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"adapters",
					kChannelAdapterFieldTokens,
					issue,
					"`gateway.channels.adapters.list` requires adapter entries with `id`, `label`, and `defaultAccountId` fields.");
			} },
			{ "gateway.channels.accounts.get", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"account",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.get` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
			} },
            { "gateway.tools.executions.list", [&]() {
				return ValidateArrayWithOptionalEntryTokens(
					payload,
					"executions",
					kToolExecutionFieldTokens,
					issue,
					"`gateway.tools.executions.list` requires execution entries with `tool`, `executed`, `status`, `output`, and `argsProvided` fields.");
			} },
            { "gateway.tools.executions.count", [&]() {
				return ValidateNumericFields(
					payload,
					{ "count", "succeeded", "failed" },
					issue,
					"`gateway.tools.executions.count` requires numeric fields `count`, `succeeded`, and `failed`.");
			} },
			{ "gateway.tools.executions.latest", [&]() {
				const bool hasFoundAndCount =
					IsFieldBoolean(payload, "found") &&
					IsFieldNumber(payload, "count");
				if (!hasFoundAndCount) {
					SetIssue(
						issue,
						"schema_invalid_response",
						"`gateway.tools.executions.latest` requires `found` boolean and `count` number fields.");
					return false;
				}

				return ValidateObjectWithTokens(
					payload,
					"execution",
					kToolExecutionFieldTokens,
					issue,
					"`gateway.tools.executions.latest` requires `execution` fields `tool`, `executed`, `status`, `output`, and `argsProvided`.");
			} },
			{ "gateway.tools.executions.clear", [&]() {
				return ValidateNumericFields(
					payload,
					{ "cleared", "remaining" },
					issue,
					"`gateway.tools.executions.clear` requires numeric fields `cleared` and `remaining`.");
			} },
			{ "gateway.channels.status.get", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"channel",
					kChannelFieldTokens,
					issue,
					"`gateway.channels.status.get` requires channel fields `id`, `label`, `connected`, and `accounts`.");
			} },
			{ "gateway.channels.accounts.activate", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"activated",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.activate` requires `account` object and `activated` boolean.");
			} },
			{ "gateway.channels.accounts.deactivate", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"deactivated",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.deactivate` requires `account` object and `deactivated` boolean.");
			} },
			{ "gateway.channels.accounts.update", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"updated",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.update` requires `account` object and `updated` boolean.");
			} },
			{ "gateway.channels.accounts.create", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"created",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.create` requires `account` object and `created` boolean.");
			} },
			{ "gateway.channels.accounts.delete", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"account",
					"deleted",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.delete` requires `account` object and `deleted` boolean.");
			} },
			{ "gateway.channels.route.resolve", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"route",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.resolve` requires route object fields `channel`, `accountId`, `agentId`, and `sessionId`.");
			} },
			{ "gateway.channels.route.get", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"route",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.get` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
			} },
			{ "gateway.channels.route.set", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"route",
					"saved",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.set` requires `route` object and `saved` boolean.");
			} },
			{ "gateway.channels.route.delete", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"route",
					"deleted",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.delete` requires `route` object and `deleted` boolean.");
			} },
			{ "gateway.channels.route.restore", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"route",
					"restored",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.restore` requires `route` object and `restored` boolean.");
			} },
			{ "gateway.channels.route.patch", [&]() {
				return ValidateObjectWithBooleanAndTokens(
					payload,
					"route",
					"updated",
					kRouteFieldTokens,
					issue,
					"`gateway.channels.route.patch` requires `route` object and `updated` boolean.");
			} },
			{ "gateway.channels.route.reset", [&]() {
				if (!IsFieldValueType(payload, "route", '{') || !IsFieldBoolean(payload, "deleted") || !IsFieldBoolean(payload, "restored")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.reset` requires `route` object, `deleted` boolean, and `restored` boolean.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kRouteFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.reset` requires route fields `channel`, `accountId`, `agentId`, and `sessionId`.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.resolve", [&]() {
				return ValidateObjectWithTokens(payload, "session", kSessionFieldTokens, issue, "`gateway.sessions.resolve` requires session fields `id`, `scope`, and `active`.");
			} },
			{ "gateway.sessions.create", [&]() {
				return ValidateObjectWithTokens(payload, "session", kSessionFieldTokens, issue, "`gateway.sessions.create` requires session fields `id`, `scope`, and `active`.");
			} },
			{ "gateway.sessions.reset", [&]() {
				return ValidateObjectWithTokens(payload, "session", kSessionFieldTokens, issue, "`gateway.sessions.reset` requires session fields `id`, `scope`, and `active`.");
			} },
			{ "gateway.agents.get", [&]() {
				return ValidateObjectWithTokens(payload, "agent", kAgentFieldTokens, issue, "`gateway.agents.get` requires agent fields `id`, `name`, and `active`.");
			} },
			{ "gateway.agents.activate", [&]() {
				return ValidateObjectWithTokens(payload, "agent", kAgentFieldTokens, issue, "`gateway.agents.activate` requires agent fields `id`, `name`, and `active`.");
			} },
			{ "gateway.events.stream", [&]() {
				return ValidateTwoStringFields(payload, "stream", "event", issue, "`gateway.events.stream` requires `stream` string and `event` string.");
			} },
			{ "gateway.events.sessionKey", [&]() {
				return ValidateTwoStringFields(payload, "sessionKey", "event", issue, "`gateway.events.sessionKey` requires `sessionKey` string and `event` string.");
			} },
			{ "gateway.events.scopeKey", [&]() {
				return ValidateTwoStringFields(payload, "scopeKey", "event", issue, "`gateway.events.scopeKey` requires `scopeKey` string and `event` string.");
			} },
			{ "gateway.events.contextKey", [&]() {
				return ValidateTwoStringFields(payload, "contextKey", "event", issue, "`gateway.events.contextKey` requires `contextKey` string and `event` string.");
			} },
			{ "gateway.events.channelKey", [&]() {
				return ValidateTwoStringFields(payload, "channelKey", "event", issue, "`gateway.events.channelKey` requires `channelKey` string and `event` string.");
			} },
			{ "gateway.events.routeKey", [&]() {
				return ValidateTwoStringFields(payload, "routeKey", "event", issue, "`gateway.events.routeKey` requires `routeKey` string and `event` string.");
			} },
			{ "gateway.events.accountKey", [&]() {
				return ValidateTwoStringFields(payload, "accountKey", "event", issue, "`gateway.events.accountKey` requires `accountKey` string and `event` string.");
			} },
			{ "gateway.events.agentKey", [&]() {
				return ValidateTwoStringFields(payload, "agentKey", "event", issue, "`gateway.events.agentKey` requires `agentKey` string and `event` string.");
			} },
			{ "gateway.events.modelKey", [&]() {
				return ValidateTwoStringFields(payload, "modelKey", "event", issue, "`gateway.events.modelKey` requires `modelKey` string and `event` string.");
			} },
			{ "gateway.events.configKey", [&]() {
				return ValidateTwoStringFields(payload, "configKey", "event", issue, "`gateway.events.configKey` requires `configKey` string and `event` string.");
			} },
			{ "gateway.events.policyKey", [&]() {
				return ValidateTwoStringFields(payload, "policyKey", "event", issue, "`gateway.events.policyKey` requires `policyKey` string and `event` string.");
			} },
			{ "gateway.events.toolKey", [&]() {
				return ValidateTwoStringFields(payload, "toolKey", "event", issue, "`gateway.events.toolKey` requires `toolKey` string and `event` string.");
			} },
			{ "gateway.events.transportKey", [&]() {
				return ValidateTwoStringFields(payload, "transportKey", "event", issue, "`gateway.events.transportKey` requires `transportKey` string and `event` string.");
			} },
			{ "gateway.events.runtimeKey", [&]() {
				return ValidateTwoStringFields(payload, "runtimeKey", "event", issue, "`gateway.events.runtimeKey` requires `runtimeKey` string and `event` string.");
			} },
			{ "gateway.events.stateKey", [&]() {
				return ValidateTwoStringFields(payload, "stateKey", "event", issue, "`gateway.events.stateKey` requires `stateKey` string and `event` string.");
			} },
			{ "gateway.events.healthKey", [&]() {
				return ValidateTwoStringFields(payload, "healthKey", "event", issue, "`gateway.events.healthKey` requires `healthKey` string and `event` string.");
			} },
			{ "gateway.events.logKey", [&]() {
				return ValidateTwoStringFields(payload, "logKey", "event", issue, "`gateway.events.logKey` requires `logKey` string and `event` string.");
			} },
			{ "gateway.events.metricKey", [&]() {
				return ValidateTwoStringFields(payload, "metricKey", "event", issue, "`gateway.events.metricKey` requires `metricKey` string and `event` string.");
			} },
			{ "gateway.events.traceKey", [&]() {
				return ValidateTwoStringFields(payload, "traceKey", "event", issue, "`gateway.events.traceKey` requires `traceKey` string and `event` string.");
			} },
			{ "gateway.events.auditKey", [&]() {
				return ValidateTwoStringFields(payload, "auditKey", "event", issue, "`gateway.events.auditKey` requires `auditKey` string and `event` string.");
			} },
			{ "gateway.events.debugKey", [&]() {
				return ValidateTwoStringFields(payload, "debugKey", "event", issue, "`gateway.events.debugKey` requires `debugKey` string and `event` string.");
			} },
			{ "gateway.events.cacheKey", [&]() {
				return ValidateTwoStringFields(payload, "cacheKey", "event", issue, "`gateway.events.cacheKey` requires `cacheKey` string and `event` string.");
			} },
			{ "gateway.events.queueKey", [&]() {
				return ValidateTwoStringFields(payload, "queueKey", "event", issue, "`gateway.events.queueKey` requires `queueKey` string and `event` string.");
			} },
			{ "gateway.events.windowKey", [&]() {
				return ValidateTwoStringFields(payload, "windowKey", "event", issue, "`gateway.events.windowKey` requires `windowKey` string and `event` string.");
			} },
			{ "gateway.events.cursorKey", [&]() {
				return ValidateTwoStringFields(payload, "cursorKey", "event", issue, "`gateway.events.cursorKey` requires `cursorKey` string and `event` string.");
			} },
			{ "gateway.events.anchorKey", [&]() {
				return ValidateTwoStringFields(payload, "anchorKey", "event", issue, "`gateway.events.anchorKey` requires `anchorKey` string and `event` string.");
			} },
			{ "gateway.events.offsetKey", [&]() {
				return ValidateTwoStringFields(payload, "offsetKey", "event", issue, "`gateway.events.offsetKey` requires `offsetKey` string and `event` string.");
			} },
			{ "gateway.events.markerKey", [&]() {
				return ValidateTwoStringFields(payload, "markerKey", "event", issue, "`gateway.events.markerKey` requires `markerKey` string and `event` string.");
			} },
			{ "gateway.events.pointerKey", [&]() {
				return ValidateTwoStringFields(payload, "pointerKey", "event", issue, "`gateway.events.pointerKey` requires `pointerKey` string and `event` string.");
			} },
			{ "gateway.events.tokenKey", [&]() {
				return ValidateTwoStringFields(payload, "tokenKey", "event", issue, "`gateway.events.tokenKey` requires `tokenKey` string and `event` string.");
			} },
			{ "gateway.events.sequenceKey", [&]() {
				return ValidateTwoStringFields(payload, "sequenceKey", "event", issue, "`gateway.events.sequenceKey` requires `sequenceKey` string and `event` string.");
			} },
			{ "gateway.events.streamKey", [&]() {
				return ValidateTwoStringFields(payload, "streamKey", "event", issue, "`gateway.events.streamKey` requires `streamKey` string and `event` string.");
			} },
			{ "gateway.events.bundleKey", [&]() {
				return ValidateTwoStringFields(payload, "bundleKey", "event", issue, "`gateway.events.bundleKey` requires `bundleKey` string and `event` string.");
			} },
			{ "gateway.events.packageKey", [&]() {
				return ValidateTwoStringFields(payload, "packageKey", "event", issue, "`gateway.events.packageKey` requires `packageKey` string and `event` string.");
			} },
			{ "gateway.events.archiveKey", [&]() {
				return ValidateTwoStringFields(payload, "archiveKey", "event", issue, "`gateway.events.archiveKey` requires `archiveKey` string and `event` string.");
			} },
			{ "gateway.events.manifestKey", [&]() {
				return ValidateTwoStringFields(payload, "manifestKey", "event", issue, "`gateway.events.manifestKey` requires `manifestKey` string and `event` string.");
			} },
			{ "gateway.events.profileKey", [&]() {
				return ValidateTwoStringFields(payload, "profileKey", "event", issue, "`gateway.events.profileKey` requires `profileKey` string and `event` string.");
			} },
			{ "gateway.events.templateKey", [&]() {
				return ValidateTwoStringFields(payload, "templateKey", "event", issue, "`gateway.events.templateKey` requires `templateKey` string and `event` string.");
			} },
			{ "gateway.events.revisionKey", [&]() {
				return ValidateTwoStringFields(payload, "revisionKey", "event", issue, "`gateway.events.revisionKey` requires `revisionKey` string and `event` string.");
			} },
			{ "gateway.events.historyKey", [&]() {
				return ValidateTwoStringFields(payload, "historyKey", "event", issue, "`gateway.events.historyKey` requires `historyKey` string and `event` string.");
			} },
			{ "gateway.events.snapshotKey", [&]() {
				return ValidateTwoStringFields(payload, "snapshotKey", "event", issue, "`gateway.events.snapshotKey` requires `snapshotKey` string and `event` string.");
			} },
			{ "gateway.events.indexKey", [&]() {
				return ValidateTwoStringFields(payload, "indexKey", "event", issue, "`gateway.events.indexKey` requires `indexKey` string and `event` string.");
			} },
			{ "gateway.events.windowScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeKey", "event", issue, "`gateway.events.windowScopeKey` requires `windowScopeKey` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeKey", "event", issue, "`gateway.events.cursorScopeKey` requires `cursorScopeKey` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeKey", "event", issue, "`gateway.events.anchorScopeKey` requires `anchorScopeKey` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeKey", "event", issue, "`gateway.events.offsetScopeKey` requires `offsetScopeKey` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeKey", "event", issue, "`gateway.events.pointerScopeKey` requires `pointerScopeKey` string and `event` string.");
			} },
			{ "gateway.events.markerScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "markerScopeKey", "event", issue, "`gateway.events.markerScopeKey` requires `markerScopeKey` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeKey", "event", issue, "`gateway.events.tokenScopeKey` requires `tokenScopeKey` string and `event` string.");
			} },
			{ "gateway.events.streamScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeKey", "event", issue, "`gateway.events.streamScopeKey` requires `streamScopeKey` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeKey", "event", issue, "`gateway.events.sequenceScopeKey` requires `sequenceScopeKey` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeKey", "event", issue, "`gateway.events.bundleScopeKey` requires `bundleScopeKey` string and `event` string.");
			} },
			{ "gateway.events.packageScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeKey", "event", issue, "`gateway.events.packageScopeKey` requires `packageScopeKey` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeKey", "event", issue, "`gateway.events.archiveScopeKey` requires `archiveScopeKey` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeKey", "event", issue, "`gateway.events.manifestScopeKey` requires `manifestScopeKey` string and `event` string.");
			} },
			{ "gateway.events.profileScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeKey", "event", issue, "`gateway.events.profileScopeKey` requires `profileScopeKey` string and `event` string.");
			} },
			{ "gateway.events.templateScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeKey", "event", issue, "`gateway.events.templateScopeKey` requires `templateScopeKey` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeKey", "event", issue, "`gateway.events.revisionScopeKey` requires `revisionScopeKey` string and `event` string.");
			} },
			{ "gateway.events.historyScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeKey", "event", issue, "`gateway.events.historyScopeKey` requires `historyScopeKey` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeKey", "event", issue, "`gateway.events.snapshotScopeKey` requires `snapshotScopeKey` string and `event` string.");
			} },
			{ "gateway.events.indexScopeKey", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeKey", "event", issue, "`gateway.events.indexScopeKey` requires `indexScopeKey` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.events.indexScopeId", [&]() {
				return ValidateTwoStringFields(payload, "indexScopeId", "event", issue, "`gateway.events.indexScopeId` requires `indexScopeId` string and `event` string.");
			} },
			{ "gateway.events.windowScopeId", [&]() {
				return ValidateTwoStringFields(payload, "windowScopeId", "event", issue, "`gateway.events.windowScopeId` requires `windowScopeId` string and `event` string.");
			} },
			{ "gateway.events.cursorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "cursorScopeId", "event", issue, "`gateway.events.cursorScopeId` requires `cursorScopeId` string and `event` string.");
			} },
			{ "gateway.events.anchorScopeId", [&]() {
				return ValidateTwoStringFields(payload, "anchorScopeId", "event", issue, "`gateway.events.anchorScopeId` requires `anchorScopeId` string and `event` string.");
			} },
			{ "gateway.events.offsetScopeId", [&]() {
				return ValidateTwoStringFields(payload, "offsetScopeId", "event", issue, "`gateway.events.offsetScopeId` requires `offsetScopeId` string and `event` string.");
			} },
			{ "gateway.events.pointerScopeId", [&]() {
				return ValidateTwoStringFields(payload, "pointerScopeId", "event", issue, "`gateway.events.pointerScopeId` requires `pointerScopeId` string and `event` string.");
			} },
			{ "gateway.events.tokenScopeId", [&]() {
				return ValidateTwoStringFields(payload, "tokenScopeId", "event", issue, "`gateway.events.tokenScopeId` requires `tokenScopeId` string and `event` string.");
			} },
			{ "gateway.events.sequenceScopeId", [&]() {
				return ValidateTwoStringFields(payload, "sequenceScopeId", "event", issue, "`gateway.events.sequenceScopeId` requires `sequenceScopeId` string and `event` string.");
			} },
			{ "gateway.events.streamScopeId", [&]() {
				return ValidateTwoStringFields(payload, "streamScopeId", "event", issue, "`gateway.events.streamScopeId` requires `streamScopeId` string and `event` string.");
			} },
			{ "gateway.events.bundleScopeId", [&]() {
				return ValidateTwoStringFields(payload, "bundleScopeId", "event", issue, "`gateway.events.bundleScopeId` requires `bundleScopeId` string and `event` string.");
			} },
			{ "gateway.events.packageScopeId", [&]() {
				return ValidateTwoStringFields(payload, "packageScopeId", "event", issue, "`gateway.events.packageScopeId` requires `packageScopeId` string and `event` string.");
			} },
			{ "gateway.events.archiveScopeId", [&]() {
				return ValidateTwoStringFields(payload, "archiveScopeId", "event", issue, "`gateway.events.archiveScopeId` requires `archiveScopeId` string and `event` string.");
			} },
			{ "gateway.events.manifestScopeId", [&]() {
				return ValidateTwoStringFields(payload, "manifestScopeId", "event", issue, "`gateway.events.manifestScopeId` requires `manifestScopeId` string and `event` string.");
			} },
			{ "gateway.events.profileScopeId", [&]() {
				return ValidateTwoStringFields(payload, "profileScopeId", "event", issue, "`gateway.events.profileScopeId` requires `profileScopeId` string and `event` string.");
			} },
			{ "gateway.events.templateScopeId", [&]() {
				return ValidateTwoStringFields(payload, "templateScopeId", "event", issue, "`gateway.events.templateScopeId` requires `templateScopeId` string and `event` string.");
			} },
			{ "gateway.events.revisionScopeId", [&]() {
				return ValidateTwoStringFields(payload, "revisionScopeId", "event", issue, "`gateway.events.revisionScopeId` requires `revisionScopeId` string and `event` string.");
			} },
			{ "gateway.events.historyScopeId", [&]() {
				return ValidateTwoStringFields(payload, "historyScopeId", "event", issue, "`gateway.events.historyScopeId` requires `historyScopeId` string and `event` string.");
			} },
			{ "gateway.events.snapshotScopeId", [&]() {
				return ValidateTwoStringFields(payload, "snapshotScopeId", "event", issue, "`gateway.events.snapshotScopeId` requires `snapshotScopeId` string and `event` string.");
			} },
			{ "gateway.models.failover.status", [&]() {
				if (!IsFieldValueType(payload, "primary", '"') || !IsFieldValueType(payload, "fallbacks", '[') || !IsFieldNumber(payload, "maxRetries") || !IsFieldValueType(payload, "strategy", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.status` requires `primary`, `fallbacks`, `maxRetries`, and `strategy` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.preview", [&]() {
				if (!IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "attempts", '[') || !IsFieldValueType(payload, "selected", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.preview` requires `model`, `attempts`, and `selected` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.metrics", [&]() {
				if (!IsFieldNumber(payload, "attempts") || !IsFieldNumber(payload, "fallbackHits") || !IsFieldNumber(payload, "successRate")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.metrics` requires `attempts`, `fallbackHits`, and `successRate` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.simulate", [&]() {
				if (!IsFieldValueType(payload, "requested", '"') || !IsFieldValueType(payload, "resolved", '"') || !IsFieldBoolean(payload, "usedFallback")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.simulate` requires `requested`, `resolved`, and `usedFallback` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.audit", [&]() {
				if (!IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "lastModel", '"') || !IsFieldValueType(payload, "lastOutcome", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.audit` requires `entries`, `lastModel`, and `lastOutcome` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.policy", [&]() {
				if (!IsFieldValueType(payload, "policy", '"') || !IsFieldNumber(payload, "maxRetries") || !IsFieldBoolean(payload, "stickyPrimary")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.policy` requires `policy`, `maxRetries`, and `stickyPrimary` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.history", [&]() {
				if (!IsFieldValueType(payload, "events", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "last", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.history` requires `events`, `count`, and `last` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.recent", [&]() {
				if (!IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "active", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.recent` requires `models`, `count`, and `active` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.window", [&]() {
				if (!IsFieldNumber(payload, "windowSec") || !IsFieldNumber(payload, "attempts") || !IsFieldNumber(payload, "fallbacks")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.window` requires `windowSec`, `attempts`, and `fallbacks` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.digest", [&]() {
				if (!IsFieldValueType(payload, "digest", '"') || !IsFieldNumber(payload, "entries") || !IsFieldBoolean(payload, "fresh")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.digest` requires `digest`, `entries`, and `fresh` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.ledger", [&]() {
				if (!IsFieldNumber(payload, "entries") || !IsFieldNumber(payload, "primaryHits") || !IsFieldNumber(payload, "fallbackHits")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.ledger` requires `entries`, `primaryHits`, and `fallbackHits` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.profile", [&]() {
				if (!IsFieldValueType(payload, "profile", '"') || !IsFieldValueType(payload, "weights", '[') || !IsFieldNumber(payload, "version")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.profile` requires `profile`, `weights`, and `version` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.baseline", [&]() {
				if (!IsFieldValueType(payload, "primary", '"') || !IsFieldValueType(payload, "secondary", '"') || !IsFieldNumber(payload, "confidence")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.baseline` requires `primary`, `secondary`, and `confidence` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.forecast", [&]() {
				if (!IsFieldNumber(payload, "windowSec") || !IsFieldNumber(payload, "projectedFallbacks") || !IsFieldValueType(payload, "risk", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.forecast` requires `windowSec`, `projectedFallbacks`, and `risk` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.threshold", [&]() {
				if (!IsFieldNumber(payload, "minSuccessRate") || !IsFieldNumber(payload, "maxFallbacks") || !IsFieldBoolean(payload, "active")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.threshold` requires `minSuccessRate`, `maxFallbacks`, and `active` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.guardrail", [&]() {
				if (!IsFieldValueType(payload, "rule", '"') || !IsFieldNumber(payload, "limit") || !IsFieldBoolean(payload, "enforced")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.guardrail` requires `rule`, `limit`, and `enforced` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.envelope", [&]() {
				if (!IsFieldNumber(payload, "windowSec") || !IsFieldNumber(payload, "floor") || !IsFieldNumber(payload, "ceiling")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.envelope` requires `windowSec`, `floor`, and `ceiling` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.margin", [&]() {
				if (!IsFieldNumber(payload, "headroom") || !IsFieldNumber(payload, "buffer") || !IsFieldBoolean(payload, "safe")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.margin` requires `headroom`, `buffer`, and `safe` fields.");
					return false;
				}
				return true;
			} },
          { "gateway.models.failover.reserve", [&]() {
				if (!IsFieldNumber(payload, "reserve") || !IsFieldBoolean(payload, "available") || !IsFieldNumber(payload, "priority")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.reserve` requires `reserve`, `available`, and `priority` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "reason", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override` requires `active`, `model`, and `reason` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.clear", [&]() {
				if (!IsFieldBoolean(payload, "cleared") || !IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.clear` requires `cleared`, `active`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.status", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "source", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.status` requires `active`, `model`, and `source` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.history", [&]() {
				if (!IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "lastModel", '"') || !IsFieldBoolean(payload, "active")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.history` requires `entries`, `lastModel`, and `active` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.metrics", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "switches") || !IsFieldValueType(payload, "lastModel", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.metrics` requires `active`, `switches`, and `lastModel` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.window", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "windowSec") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.window` requires `active`, `windowSec`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.digest", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "digest", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.digest` requires `active`, `digest`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.timeline", [&]() {
				if (!IsFieldNumber(payload, "entries") || !IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "lastModel", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.timeline` requires `entries`, `active`, and `lastModel` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.catalog", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.catalog` requires `active`, `count`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.registry", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.registry` requires `active`, `entries`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.matrix", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "rows") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.matrix` requires `active`, `rows`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.snapshot", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "revision") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.snapshot` requires `active`, `revision`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.pointer", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "pointer", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.pointer` requires `active`, `pointer`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.state", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "state", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.state` requires `active`, `state`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.profile", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "profile", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.profile` requires `active`, `profile`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.audit", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.audit` requires `active`, `entries`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.checkpoint", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "checkpoint", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.checkpoint` requires `active`, `checkpoint`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.baseline", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "baseline", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.baseline` requires `active`, `baseline`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.manifest", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "manifest", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.manifest` requires `active`, `manifest`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.ledger", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "entries") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.ledger` requires `active`, `entries`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.snapshotIndex", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "index") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.snapshotIndex` requires `active`, `index`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.digestIndex", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "digestIndex") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.digestIndex` requires `active`, `digestIndex`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.cursor", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "cursor", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.cursor` requires `active`, `cursor`, and `model` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.override.vector", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldValueType(payload, "vector", '"') || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vector` requires `active`, `vector`, and `model` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.override.vectorDrift", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorDrift") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorDrift` requires `active`, `vectorDrift`, and `model` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.failover.override.phaseBias", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "phaseBias") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.phaseBias` requires `active`, `phaseBias`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.biasEnvelope", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "biasEnvelope") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.biasEnvelope` requires `active`, `biasEnvelope`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.driftEnvelope", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "driftEnvelope") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.driftEnvelope` requires `active`, `driftEnvelope`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.envelopeDrift", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "envelopeDrift") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.envelopeDrift` requires `active`, `envelopeDrift`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.driftVector", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "driftVector") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.driftVector` requires `active`, `driftVector`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorEnvelope", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorEnvelope") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorEnvelope` requires `active`, `vectorEnvelope`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorContour", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorContour") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorContour` requires `active`, `vectorContour`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorRibbon", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorRibbon") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorRibbon` requires `active`, `vectorRibbon`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorSpiral", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorSpiral") || !IsFieldValueType(payload, "model", '"')) {
                    SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorSpiral` requires `active`, `vectorSpiral`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorArc", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorArc") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorArc` requires `active`, `vectorArc`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorMesh", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorMesh") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorMesh` requires `active`, `vectorMesh`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorNode", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorNode") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorNode` requires `active`, `vectorNode`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorCore", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorCore") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorCore` requires `active`, `vectorCore`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorFrame", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorFrame") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorFrame` requires `active`, `vectorFrame`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorSpan", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorSpan") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorSpan` requires `active`, `vectorSpan`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorGrid", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorGrid") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorGrid` requires `active`, `vectorGrid`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorLane", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorLane") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorLane` requires `active`, `vectorLane`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorTrack", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorTrack") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorTrack` requires `active`, `vectorTrack`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorRail", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorRail") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorRail` requires `active`, `vectorRail`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorSpline", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorSpline") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorSpline` requires `active`, `vectorSpline`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorChain", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorChain") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorChain` requires `active`, `vectorChain`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorThread", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorThread") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorThread` requires `active`, `vectorThread`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorLink", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorLink") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorLink` requires `active`, `vectorLink`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorNode2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorNode2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorNode2` requires `active`, `vectorNode2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorBridge", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorBridge") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorBridge` requires `active`, `vectorBridge`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorAnchor2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorAnchor2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorAnchor2` requires `active`, `vectorAnchor2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorPortal", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorPortal") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorPortal` requires `active`, `vectorPortal`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorRelay2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorRelay2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorRelay2` requires `active`, `vectorRelay2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorGate2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorGate2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorGate2` requires `active`, `vectorGate2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorHub2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorHub2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorHub2` requires `active`, `vectorHub2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorNode3", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorNode3") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorNode3` requires `active`, `vectorNode3`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorLink2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorLink2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorLink2` requires `active`, `vectorLink2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorMesh2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorMesh2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorMesh2` requires `active`, `vectorMesh2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorArc2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorArc2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorArc2` requires `active`, `vectorArc2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorBand2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorBand2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorBand2` requires `active`, `vectorBand2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorGrid2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorGrid2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorGrid2` requires `active`, `vectorGrid2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorLane2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorLane2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorLane2` requires `active`, `vectorLane2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorTrack2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorTrack2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorTrack2` requires `active`, `vectorTrack2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorRail2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorRail2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorRail2` requires `active`, `vectorRail2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorSpline2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorSpline2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorSpline2` requires `active`, `vectorSpline2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorChain2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorChain2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorChain2` requires `active`, `vectorChain2`, and `model` fields.");
					return false;
				}
				return true;
            } },
			{ "gateway.models.failover.override.vectorThread2", [&]() {
				if (!IsFieldBoolean(payload, "active") || !IsFieldNumber(payload, "vectorThread2") || !IsFieldValueType(payload, "model", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.failover.override.vectorThread2` requires `active`, `vectorThread2`, and `model` fields.");
					return false;
				}
				return true;
            } },
		};

		if (const auto groupedIt = groupedValidators.find(method); groupedIt != groupedValidators.end()) {
			return groupedIt->second();
		}



		return true;
	}

} // namespace blazeclaw::gateway::protocol
