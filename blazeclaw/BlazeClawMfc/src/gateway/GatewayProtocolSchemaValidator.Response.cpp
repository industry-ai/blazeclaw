#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"
#include "GatewayProtocolSchemaValidator.Internal.h"

#include <functional>
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
		constexpr const char* kFeatureRequiredCatalogAll[] = {
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
			"gateway.config.package",
			"gateway.config.archive",
			"gateway.config.manifest",
			"gateway.config.index",
			"gateway.config.state",
			"gateway.config.session",
			"gateway.config.scope",
			"gateway.config.context",
			"gateway.config.channel",
			"gateway.config.route",
			"gateway.config.account",
			"gateway.config.agent",
			"gateway.config.model",
			"gateway.config.config",
			"gateway.config.policy",
			"gateway.config.tool",
			"gateway.config.transport",
			"gateway.config.runtime",
			"gateway.config.stateKey",
			"gateway.config.healthKey",
			"gateway.config.log",
			"gateway.config.metric",
			"gateway.config.trace",
			"gateway.config.auditKey",
			"gateway.config.debug",
			"gateway.config.cache",
			"gateway.config.queueKey",
			"gateway.config.windowKey",
			"gateway.config.cursorKey",
			"gateway.config.anchorKey",
			"gateway.config.offsetKey",
			"gateway.config.markerKey",
			"gateway.config.pointerKey",
			"gateway.config.tokenKey",
			"gateway.config.sequenceKey",
			"gateway.config.streamKey",
			"gateway.config.bundleKey",
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
			"gateway.transport.policy.apply",
			"gateway.transport.policy.stage",
			"gateway.transport.policy.reconcile",
			"gateway.transport.policy.sync",
			"gateway.transport.policy.refresh",
			"gateway.transport.policy.session",
			"gateway.transport.policy.scope",
			"gateway.transport.policy.context",
			"gateway.transport.policy.channel",
			"gateway.transport.policy.route",
			"gateway.transport.policy.account",
			"gateway.transport.policy.agent",
			"gateway.transport.policy.model",
			"gateway.transport.policy.config",
			"gateway.transport.policy.policy",
			"gateway.transport.policy.tool",
			"gateway.transport.policy.transport",
			"gateway.transport.policy.runtime",
			"gateway.transport.policy.stateKey",
			"gateway.transport.policy.healthKey",
			"gateway.transport.policy.log",
			"gateway.transport.policy.metric",
			"gateway.transport.policy.trace",
			"gateway.transport.policy.audit",
			"gateway.transport.policy.debug",
			"gateway.transport.policy.cache",
			"gateway.transport.policy.queueKey",
			"gateway.transport.policy.windowKey",
			"gateway.transport.policy.cursorKey",
			"gateway.transport.policy.anchorKey",
			"gateway.transport.policy.offsetKey",
			"gateway.transport.policy.markerKey",
			"gateway.transport.policy.pointerKey",
			"gateway.transport.policy.tokenKey",
			"gateway.transport.policy.sequenceKey",
			"gateway.transport.policy.streamKey",
			"gateway.transport.policy.bundleKey",
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
			"gateway.events.marker",
			"gateway.events.sequence",
			"gateway.events.pointer",
			"gateway.events.token",
			"gateway.events.stream",
			"gateway.events.sessionKey",
            "gateway.events.scopeKey",
            "gateway.events.contextKey",
            "gateway.events.channelKey",
            "gateway.events.routeKey",
            "gateway.events.accountKey",
            "gateway.events.agentKey",
            "gateway.events.modelKey",
            "gateway.events.configKey",
            "gateway.events.policyKey",
            "gateway.events.toolKey",
            "gateway.events.transportKey",
            "gateway.events.runtimeKey",
            "gateway.events.stateKey",
            "gateway.events.healthKey",
            "gateway.events.logKey",
            "gateway.events.metricKey",
            "gateway.events.traceKey",
            "gateway.events.auditKey",
            "gateway.events.debugKey",
            "gateway.events.cacheKey",
            "gateway.events.queueKey",
            "gateway.events.windowKey",
            "gateway.events.cursorKey",
            "gateway.events.anchorKey",
            "gateway.events.offsetKey",
            "gateway.events.markerKey",
            "gateway.events.pointerKey",
            "gateway.events.tokenKey",
            "gateway.events.sequenceKey",
            "gateway.events.streamKey",
            "gateway.events.bundleKey",
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
			"gateway.tools.window",
			"gateway.tools.pipeline",
			"gateway.tools.dispatch",
			"gateway.tools.router",
			"gateway.tools.selector",
			"gateway.tools.mapper",
            "gateway.tools.binding",
            "gateway.tools.profile",
            "gateway.tools.channel",
            "gateway.tools.route",
            "gateway.tools.account",
            "gateway.tools.agent",
            "gateway.tools.model",
            "gateway.tools.config",
            "gateway.tools.policy",
            "gateway.tools.tool",
            "gateway.tools.transport",
            "gateway.tools.runtime",
            "gateway.tools.state",
            "gateway.tools.healthKey",
            "gateway.tools.log",
            "gateway.tools.metric",
            "gateway.tools.trace",
            "gateway.tools.audit",
            "gateway.tools.debug",
            "gateway.tools.cache",
            "gateway.tools.queueKey",
            "gateway.tools.windowKey",
            "gateway.tools.cursorKey",
            "gateway.tools.anchorKey",
            "gateway.tools.offsetKey",
            "gateway.tools.markerKey",
            "gateway.tools.pointerKey",
            "gateway.tools.tokenKey",
            "gateway.tools.sequenceKey",
            "gateway.tools.streamKey",
            "gateway.tools.bundleKey",
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
			"gateway.models.inventory",
			"gateway.models.snapshot",
			"gateway.models.registry",
			"gateway.models.index",
			"gateway.models.state",
			"gateway.models.session",
            "gateway.models.scope",
            "gateway.models.context",
            "gateway.models.channel",
            "gateway.models.route",
            "gateway.models.account",
            "gateway.models.agent",
            "gateway.models.model",
            "gateway.models.config",
            "gateway.models.policy",
            "gateway.models.tool",
            "gateway.models.transport",
            "gateway.models.runtime",
            "gateway.models.stateKey",
            "gateway.models.healthKey",
            "gateway.models.log",
            "gateway.models.metric",
            "gateway.models.trace",
            "gateway.models.audit",
            "gateway.models.debug",
            "gateway.models.cache",
            "gateway.models.queueKey",
            "gateway.models.windowKey",
            "gateway.models.cursorKey",
            "gateway.models.anchorKey",
            "gateway.models.offsetKey",
            "gateway.models.markerKey",
            "gateway.models.pointerKey",
            "gateway.models.tokenKey",
            "gateway.models.sequenceKey",
            "gateway.models.streamKey",
            "gateway.models.bundleKey",
			"gateway.config.getKey",
			"gateway.transport.endpoint.exists",
			"gateway.tick",
			"gateway.health",
			"gateway.shutdown",
		};
		constexpr const char* kFeatureRequiredConfigCluster[] = {
			"gateway.config.validate",
			"gateway.config.audit",
			"gateway.config.snapshot",
			"gateway.config.schema",
		};
		constexpr const char* kFeatureRequiredTransportCluster[] = {
			"gateway.transport.status",
			"gateway.transport.policy.get",
			"gateway.transport.policy.history",
			"gateway.transport.policy.validate",
		};
		constexpr const char* kFeatureRequiredEventCluster[] = {
			"gateway.events.catalog",
			"gateway.events.list",
			"gateway.events.types",
			"gateway.events.summary",
		};
		constexpr const char* kFeatureRequiredModelToolCluster[] = {
			"gateway.models.get",
			"gateway.models.listByProvider",
			"gateway.tools.get",
			"gateway.tools.list",
		};

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
			{ "gateway.channels.accounts.get", [&]() {
				return ValidateObjectWithTokens(
					payload,
					"account",
					kAccountFieldTokens,
					issue,
					"`gateway.channels.accounts.get` requires account fields `channel`, `accountId`, `label`, `active`, and `connected`.");
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
			{ "gateway.events.token", [&]() {
				return ValidateTwoStringFields(payload, "token", "event", issue, "`gateway.events.token` requires `token` string and `event` string.");
			} },
			{ "gateway.events.pointer", [&]() {
				return ValidateTwoStringFields(payload, "pointer", "event", issue, "`gateway.events.pointer` requires `pointer` string and `event` string.");
			} },
			{ "gateway.events.marker", [&]() {
				return ValidateTwoStringFields(payload, "marker", "event", issue, "`gateway.events.marker` requires `marker` string and `event` string.");
			} },
			{ "gateway.events.anchor", [&]() {
				return ValidateTwoStringFields(payload, "anchor", "event", issue, "`gateway.events.anchor` requires `anchor` string and `event` string.");
			} },
			{ "gateway.events.cursor", [&]() {
				return ValidateTwoStringFields(payload, "cursor", "event", issue, "`gateway.events.cursor` requires `cursor` string and `event` string.");
			} },
			{ "gateway.transport.policy.refresh", [&]() {
				return ValidateBooleanNumberBoolean(payload, "refreshed", "version", "applied", issue, "`gateway.transport.policy.refresh` requires `refreshed` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.session", [&]() {
				return ValidateBooleanNumberBoolean(payload, "sessionApplied", "version", "applied", issue, "`gateway.transport.policy.session` requires `sessionApplied` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.scope", [&]() {
				return ValidateBooleanNumberBoolean(payload, "scoped", "version", "applied", issue, "`gateway.transport.policy.scope` requires `scoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.context", [&]() {
				return ValidateBooleanNumberBoolean(payload, "contextual", "version", "applied", issue, "`gateway.transport.policy.context` requires `contextual` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.channel", [&]() {
				return ValidateBooleanNumberBoolean(payload, "channelScoped", "version", "applied", issue, "`gateway.transport.policy.channel` requires `channelScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.route", [&]() {
				return ValidateBooleanNumberBoolean(payload, "routed", "version", "applied", issue, "`gateway.transport.policy.route` requires `routed` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.account", [&]() {
				return ValidateBooleanNumberBoolean(payload, "accountScoped", "version", "applied", issue, "`gateway.transport.policy.account` requires `accountScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.agent", [&]() {
				return ValidateBooleanNumberBoolean(payload, "agentScoped", "version", "applied", issue, "`gateway.transport.policy.agent` requires `agentScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.model", [&]() {
				return ValidateBooleanNumberBoolean(payload, "modelScoped", "version", "applied", issue, "`gateway.transport.policy.model` requires `modelScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.config", [&]() {
				return ValidateBooleanNumberBoolean(payload, "configScoped", "version", "applied", issue, "`gateway.transport.policy.config` requires `configScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.policy", [&]() {
				return ValidateBooleanNumberBoolean(payload, "policyScoped", "version", "applied", issue, "`gateway.transport.policy.policy` requires `policyScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.tool", [&]() {
				return ValidateBooleanNumberBoolean(payload, "toolScoped", "version", "applied", issue, "`gateway.transport.policy.tool` requires `toolScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.transport", [&]() {
				return ValidateBooleanNumberBoolean(payload, "transportScoped", "version", "applied", issue, "`gateway.transport.policy.transport` requires `transportScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.runtime", [&]() {
				return ValidateBooleanNumberBoolean(payload, "runtimeScoped", "version", "applied", issue, "`gateway.transport.policy.runtime` requires `runtimeScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.stateKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "stateScoped", "version", "applied", issue, "`gateway.transport.policy.stateKey` requires `stateScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.healthKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "healthScoped", "version", "applied", issue, "`gateway.transport.policy.healthKey` requires `healthScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.log", [&]() {
				return ValidateBooleanNumberBoolean(payload, "logScoped", "version", "applied", issue, "`gateway.transport.policy.log` requires `logScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.metric", [&]() {
				return ValidateBooleanNumberBoolean(payload, "metricScoped", "version", "applied", issue, "`gateway.transport.policy.metric` requires `metricScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.trace", [&]() {
				return ValidateBooleanNumberBoolean(payload, "traceScoped", "version", "applied", issue, "`gateway.transport.policy.trace` requires `traceScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.audit", [&]() {
				return ValidateBooleanNumberBoolean(payload, "auditScoped", "version", "applied", issue, "`gateway.transport.policy.audit` requires `auditScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.debug", [&]() {
				return ValidateBooleanNumberBoolean(payload, "debugScoped", "version", "applied", issue, "`gateway.transport.policy.debug` requires `debugScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.cache", [&]() {
				return ValidateBooleanNumberBoolean(payload, "cacheScoped", "version", "applied", issue, "`gateway.transport.policy.cache` requires `cacheScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.queueKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "queueScoped", "version", "applied", issue, "`gateway.transport.policy.queueKey` requires `queueScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.windowKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "windowScoped", "version", "applied", issue, "`gateway.transport.policy.windowKey` requires `windowScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.cursorKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "cursorScoped", "version", "applied", issue, "`gateway.transport.policy.cursorKey` requires `cursorScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.anchorKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "anchorScoped", "version", "applied", issue, "`gateway.transport.policy.anchorKey` requires `anchorScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.offsetKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "offsetScoped", "version", "applied", issue, "`gateway.transport.policy.offsetKey` requires `offsetScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.markerKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "markerScoped", "version", "applied", issue, "`gateway.transport.policy.markerKey` requires `markerScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.pointerKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "pointerScoped", "version", "applied", issue, "`gateway.transport.policy.pointerKey` requires `pointerScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.tokenKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "tokenScoped", "version", "applied", issue, "`gateway.transport.policy.tokenKey` requires `tokenScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.sequenceKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "sequenceScoped", "version", "applied", issue, "`gateway.transport.policy.sequenceKey` requires `sequenceScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.streamKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "streamScoped", "version", "applied", issue, "`gateway.transport.policy.streamKey` requires `streamScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.bundleKey", [&]() {
				return ValidateBooleanNumberBoolean(payload, "bundleScoped", "version", "applied", issue, "`gateway.transport.policy.bundleKey` requires `bundleScoped` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.sync", [&]() {
				return ValidateBooleanNumberBoolean(payload, "synced", "version", "applied", issue, "`gateway.transport.policy.sync` requires `synced` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.reconcile", [&]() {
				return ValidateBooleanNumberBoolean(payload, "reconciled", "version", "applied", issue, "`gateway.transport.policy.reconcile` requires `reconciled` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.stage", [&]() {
				return ValidateBooleanNumberBoolean(payload, "staged", "version", "applied", issue, "`gateway.transport.policy.stage` requires `staged` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.commit", [&]() {
				return ValidateBooleanNumberBoolean(payload, "committed", "version", "applied", issue, "`gateway.transport.policy.commit` requires `committed` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.transport.policy.import", [&]() {
				return ValidateBooleanNumberBoolean(payload, "imported", "version", "applied", issue, "`gateway.transport.policy.import` requires `imported` boolean, `version` number, and `applied` boolean.");
			} },
			{ "gateway.models.state", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.state` requires `models` array and `count` number.");
			} },
			{ "gateway.models.session", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.session` requires `models` array and `count` number.");
			} },
			{ "gateway.models.scope", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.scope` requires `models` array and `count` number.");
			} },
			{ "gateway.models.context", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.context` requires `models` array and `count` number.");
			} },
			{ "gateway.models.channel", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.channel` requires `models` array and `count` number.");
			} },
			{ "gateway.models.route", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.route` requires `models` array and `count` number.");
			} },
			{ "gateway.models.account", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.account` requires `models` array and `count` number.");
			} },
			{ "gateway.models.agent", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.agent` requires `models` array and `count` number.");
			} },
			{ "gateway.models.model", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.model` requires `models` array and `count` number.");
			} },
			{ "gateway.models.config", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.config` requires `models` array and `count` number.");
			} },
			{ "gateway.models.policy", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.policy` requires `models` array and `count` number.");
			} },
			{ "gateway.models.tool", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.tool` requires `models` array and `count` number.");
			} },
			{ "gateway.models.transport", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.transport` requires `models` array and `count` number.");
			} },
			{ "gateway.models.runtime", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.runtime` requires `models` array and `count` number.");
			} },
			{ "gateway.models.stateKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.stateKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.healthKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.healthKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.log", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.log` requires `models` array and `count` number.");
			} },
			{ "gateway.models.metric", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.metric` requires `models` array and `count` number.");
			} },
			{ "gateway.models.trace", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.trace` requires `models` array and `count` number.");
			} },
			{ "gateway.models.audit", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.audit` requires `models` array and `count` number.");
			} },
			{ "gateway.models.debug", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.debug` requires `models` array and `count` number.");
			} },
			{ "gateway.models.cache", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.cache` requires `models` array and `count` number.");
			} },
			{ "gateway.models.queueKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.queueKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.windowKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.windowKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.cursorKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.cursorKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.anchorKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.anchorKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.offsetKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.offsetKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.markerKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.markerKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.pointerKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.pointerKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.tokenKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.tokenKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.sequenceKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.sequenceKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.streamKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.streamKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.bundleKey", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.bundleKey` requires `models` array and `count` number.");
			} },
			{ "gateway.models.index", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.index` requires `models` array and `count` number.");
			} },
			{ "gateway.models.registry", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.registry` requires `models` array and `count` number.");
			} },
			{ "gateway.models.snapshot", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.snapshot` requires `models` array and `count` number.");
			} },
			{ "gateway.models.inventory", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.inventory` requires `models` array and `count` number.");
			} },
			{ "gateway.models.catalog", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.catalog` requires `models` array and `count` number.");
			} },
			{ "gateway.events.recent", [&]() {
				return ValidateArrayAndCount(payload, "events", issue, "`gateway.events.recent` requires `events` array and `count` number.");
			} },
			{ "gateway.events.window", [&]() {
				return ValidateArrayAndCount(payload, "events", issue, "`gateway.events.window` requires `events` array and `count` number.");
			} },
			{ "gateway.events.sample", [&]() {
				return ValidateArrayAndCount(payload, "events", issue, "`gateway.events.sample` requires `events` array and `count` number.");
			} },
			{ "gateway.events.timeline", [&]() {
				return ValidateArrayAndCount(payload, "events", issue, "`gateway.events.timeline` requires `events` array and `count` number.");
			} },
			{ "gateway.config.state", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.state` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.session", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.session` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.scope", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.scope` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.context", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.context` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.channel", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.channel` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.route", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.route` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.account", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.account` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.agent", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.agent` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.model", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.model` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.config", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.config` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.policy", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.policy` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.tool", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.tool` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.transport", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.transport` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.runtime", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.runtime` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.stateKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.stateKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.healthKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.healthKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.log", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.log` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.metric", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.metric` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.trace", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.trace` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.auditKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.auditKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.debug", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.debug` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.cache", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.cache` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.queueKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.queueKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.windowKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.windowKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.cursorKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.cursorKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.anchorKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.anchorKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.offsetKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.offsetKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.markerKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.markerKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.pointerKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.pointerKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.tokenKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.tokenKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.sequenceKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.sequenceKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.streamKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.streamKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.bundleKey", [&]() {
				return ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.bundleKey` requires `sections` array and `count` number.");
			} },
			{ "gateway.config.index", [&]() {
				return ValidateArrayAndCount(payload, "keys", issue, "`gateway.config.index` requires `keys` array and `count` number.");
			} },
			{ "gateway.tools.selector", [&]() {
				return ValidateNumericFields(payload, { "selected", "fallback", "tools" }, issue, "`gateway.tools.selector` requires numeric fields `selected`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.mapper", [&]() {
				return ValidateNumericFields(payload, { "mapped", "fallback", "tools" }, issue, "`gateway.tools.mapper` requires numeric fields `mapped`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.binding", [&]() {
				return ValidateNumericFields(payload, { "bound", "fallback", "tools" }, issue, "`gateway.tools.binding` requires numeric fields `bound`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.profile", [&]() {
				return ValidateNumericFields(payload, { "profiled", "fallback", "tools" }, issue, "`gateway.tools.profile` requires numeric fields `profiled`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.channel", [&]() {
				return ValidateNumericFields(payload, { "channelled", "fallback", "tools" }, issue, "`gateway.tools.channel` requires numeric fields `channelled`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.route", [&]() {
				return ValidateNumericFields(payload, { "routed", "fallback", "tools" }, issue, "`gateway.tools.route` requires numeric fields `routed`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.account", [&]() {
				return ValidateNumericFields(payload, { "accounted", "fallback", "tools" }, issue, "`gateway.tools.account` requires numeric fields `accounted`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.agent", [&]() {
				return ValidateNumericFields(payload, { "agented", "fallback", "tools" }, issue, "`gateway.tools.agent` requires numeric fields `agented`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.model", [&]() {
				return ValidateNumericFields(payload, { "modelled", "fallback", "tools" }, issue, "`gateway.tools.model` requires numeric fields `modelled`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.config", [&]() {
				return ValidateNumericFields(payload, { "configured", "fallback", "tools" }, issue, "`gateway.tools.config` requires numeric fields `configured`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.policy", [&]() {
				return ValidateNumericFields(payload, { "policied", "fallback", "tools" }, issue, "`gateway.tools.policy` requires numeric fields `policied`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.tool", [&]() {
				return ValidateNumericFields(payload, { "tooled", "fallback", "tools" }, issue, "`gateway.tools.tool` requires numeric fields `tooled`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.transport", [&]() {
				return ValidateNumericFields(payload, { "transported", "fallback", "tools" }, issue, "`gateway.tools.transport` requires numeric fields `transported`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.runtime", [&]() {
				return ValidateNumericFields(payload, { "runtimed", "fallback", "tools" }, issue, "`gateway.tools.runtime` requires numeric fields `runtimed`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.state", [&]() {
				return ValidateNumericFields(payload, { "stated", "fallback", "tools" }, issue, "`gateway.tools.state` requires numeric fields `stated`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.healthKey", [&]() {
				return ValidateNumericFields(payload, { "healthChecked", "fallback", "tools" }, issue, "`gateway.tools.healthKey` requires numeric fields `healthChecked`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.log", [&]() {
				return ValidateNumericFields(payload, { "logged", "fallback", "tools" }, issue, "`gateway.tools.log` requires numeric fields `logged`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.metric", [&]() {
				return ValidateNumericFields(payload, { "metered", "fallback", "tools" }, issue, "`gateway.tools.metric` requires numeric fields `metered`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.trace", [&]() {
				return ValidateNumericFields(payload, { "traced", "fallback", "tools" }, issue, "`gateway.tools.trace` requires numeric fields `traced`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.audit", [&]() {
				return ValidateNumericFields(payload, { "audited", "fallback", "tools" }, issue, "`gateway.tools.audit` requires numeric fields `audited`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.debug", [&]() {
				return ValidateNumericFields(payload, { "debugged", "fallback", "tools" }, issue, "`gateway.tools.debug` requires numeric fields `debugged`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.cache", [&]() {
				return ValidateNumericFields(payload, { "cached", "fallback", "tools" }, issue, "`gateway.tools.cache` requires numeric fields `cached`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.queueKey", [&]() {
				return ValidateNumericFields(payload, { "queuedKey", "fallback", "tools" }, issue, "`gateway.tools.queueKey` requires numeric fields `queuedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.windowKey", [&]() {
				return ValidateNumericFields(payload, { "windowedKey", "fallback", "tools" }, issue, "`gateway.tools.windowKey` requires numeric fields `windowedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.cursorKey", [&]() {
				return ValidateNumericFields(payload, { "cursoredKey", "fallback", "tools" }, issue, "`gateway.tools.cursorKey` requires numeric fields `cursoredKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.anchorKey", [&]() {
				return ValidateNumericFields(payload, { "anchoredKey", "fallback", "tools" }, issue, "`gateway.tools.anchorKey` requires numeric fields `anchoredKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.offsetKey", [&]() {
				return ValidateNumericFields(payload, { "offsettedKey", "fallback", "tools" }, issue, "`gateway.tools.offsetKey` requires numeric fields `offsettedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.markerKey", [&]() {
				return ValidateNumericFields(payload, { "markeredKey", "fallback", "tools" }, issue, "`gateway.tools.markerKey` requires numeric fields `markeredKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.pointerKey", [&]() {
				return ValidateNumericFields(payload, { "pointedKey", "fallback", "tools" }, issue, "`gateway.tools.pointerKey` requires numeric fields `pointedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.tokenKey", [&]() {
				return ValidateNumericFields(payload, { "tokenedKey", "fallback", "tools" }, issue, "`gateway.tools.tokenKey` requires numeric fields `tokenedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.sequenceKey", [&]() {
				return ValidateNumericFields(payload, { "sequencedKey", "fallback", "tools" }, issue, "`gateway.tools.sequenceKey` requires numeric fields `sequencedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.streamKey", [&]() {
				return ValidateNumericFields(payload, { "streamedKey", "fallback", "tools" }, issue, "`gateway.tools.streamKey` requires numeric fields `streamedKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.bundleKey", [&]() {
				return ValidateNumericFields(payload, { "bundledKey", "fallback", "tools" }, issue, "`gateway.tools.bundleKey` requires numeric fields `bundledKey`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.router", [&]() {
				return ValidateNumericFields(payload, { "routed", "fallback", "tools" }, issue, "`gateway.tools.router` requires numeric fields `routed`, `fallback`, and `tools`.");
			} },
			{ "gateway.tools.dispatch", [&]() {
				return ValidateNumericFields(payload, { "queued", "dispatched", "tools" }, issue, "`gateway.tools.dispatch` requires numeric fields `queued`, `dispatched`, and `tools`.");
			} },
			{ "gateway.tools.window", [&]() {
				return ValidateNumericFields(payload, { "windowSec", "calls", "tools" }, issue, "`gateway.tools.window` requires numeric fields `windowSec`, `calls`, and `tools`.");
			} },
			{ "gateway.tools.backlog", [&]() {
				return ValidateNumericFields(payload, { "pending", "capacity", "tools" }, issue, "`gateway.tools.backlog` requires numeric fields `pending`, `capacity`, and `tools`.");
			} },
			{ "gateway.tools.scheduler", [&]() {
				return ValidateNumericFields(payload, { "ticks", "queued", "tools" }, issue, "`gateway.tools.scheduler` requires numeric fields `ticks`, `queued`, and `tools`.");
			} },
			{ "gateway.tools.queue", [&]() {
				return ValidateNumericFields(payload, { "queued", "running", "tools" }, issue, "`gateway.tools.queue` requires numeric fields `queued`, `running`, and `tools`.");
			} },
			{ "gateway.tools.capacity", [&]() {
				return ValidateNumericFields(payload, { "total", "used", "free" }, issue, "`gateway.tools.capacity` requires numeric fields `total`, `used`, and `free`.");
			} },
			{ "gateway.tools.errors", [&]() {
				return ValidateNumericFields(payload, { "errors", "tools", "rate" }, issue, "`gateway.tools.errors` requires numeric fields `errors`, `tools`, and `rate`.");
			} },
			{ "gateway.tools.usage", [&]() {
				return ValidateNumericFields(payload, { "calls", "tools", "avgMs" }, issue, "`gateway.tools.usage` requires numeric fields `calls`, `tools`, and `avgMs`.");
			} },
			{ "gateway.tools.failures", [&]() {
				return ValidateNumericFields(payload, { "failed", "total", "rate" }, issue, "`gateway.tools.failures` requires numeric fields `failed`, `total`, and `rate`.");
			} },
			{ "gateway.tools.stats", [&]() {
				return ValidateNumericFields(payload, { "enabled", "disabled", "total" }, issue, "`gateway.tools.stats` requires numeric fields `enabled`, `disabled`, and `total`.");
			} },
			{ "gateway.config.manifest", [&]() {
				return ValidateStringArrayAndCount(payload, "manifest", "sections", issue, "`gateway.config.manifest` requires `manifest` string, `sections` array, and `count` number.");
			} },
			{ "gateway.config.archive", [&]() {
				return ValidateStringArrayAndCount(payload, "archive", "sections", issue, "`gateway.config.archive` requires `archive` string, `sections` array, and `count` number.");
			} },
			{ "gateway.config.package", [&]() {
				return ValidateStringArrayAndCount(payload, "package", "sections", issue, "`gateway.config.package` requires `package` string, `sections` array, and `count` number.");
			} },
			{ "gateway.config.bundle", [&]() {
				return ValidateStringArrayAndCount(payload, "name", "sections", issue, "`gateway.config.bundle` requires `name` string, `sections` array, and `count` number.");
			} },
			{ "gateway.config.template", [&]() {
				return ValidateStringArrayAndCount(payload, "template", "keys", issue, "`gateway.config.template` requires `template` string, `keys` array, and `count` number.");
			} },
			{ "gateway.channels.accounts.clear", [&]() {
				return ValidateNumericFields(payload, { "cleared", "remaining" }, issue, "`gateway.channels.accounts.clear` requires numeric fields `cleared` and `remaining`.");
			} },
			{ "gateway.channels.routes.clear", [&]() {
				return ValidateNumericFields(payload, { "cleared", "remaining" }, issue, "`gateway.channels.routes.clear` requires numeric fields `cleared` and `remaining`.");
			} },
			{ "gateway.channels.accounts.restore", [&]() {
				return ValidateNumericFields(payload, { "restored", "total" }, issue, "`gateway.channels.accounts.restore` requires numeric fields `restored` and `total`.");
			} },
			{ "gateway.channels.routes.restore", [&]() {
				return ValidateNumericFields(payload, { "restored", "total" }, issue, "`gateway.channels.routes.restore` requires numeric fields `restored` and `total`.");
			} },
			{ "gateway.channels.accounts.reset", [&]() {
				return ValidateNumericFields(payload, { "cleared", "restored", "total" }, issue, "`gateway.channels.accounts.reset` requires numeric fields `cleared`, `restored`, and `total`.");
			} },
			{ "gateway.channels.routes.reset", [&]() {
				return ValidateNumericFields(payload, { "cleared", "restored", "total" }, issue, "`gateway.channels.routes.reset` requires numeric fields `cleared`, `restored`, and `total`.");
			} },
			{ "gateway.channels.accounts.count", [&]() {
				return ValidateNumberAndString(payload, "count", "channel", issue, "`gateway.channels.accounts.count` requires `channel` string and `count` number.");
			} },
			{ "gateway.channels.routes.count", [&]() {
				return ValidateNumberAndString(payload, "count", "channel", issue, "`gateway.channels.routes.count` requires `channel` string and `count` number.");
			} },
			{ "gateway.channels.status.count", [&]() {
				return ValidateNumberAndString(payload, "count", "channel", issue, "`gateway.channels.status.count` requires `channel` string and `count` number.");
			} },
			{ "gateway.channels.status.exists", [&]() {
				if (!IsFieldValueType(payload, "channel", '"') || !IsFieldBoolean(payload, "exists")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.status.exists` requires `channel` string and `exists` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.channels.accounts.exists", [&]() {
				if (!IsFieldValueType(payload, "channel", '"') || !IsFieldValueType(payload, "accountId", '"') || !IsFieldBoolean(payload, "exists")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts.exists` requires `channel` string, `accountId` string, and `exists` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.channels.route.exists", [&]() {
				if (!IsFieldValueType(payload, "channel", '"') || !IsFieldValueType(payload, "accountId", '"') || !IsFieldBoolean(payload, "exists")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.exists` requires `channel` string, `accountId` string, and `exists` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.get", [&]() {
				return ValidateObjectWithTokens(payload, "model", kModelFieldTokens, issue, "`gateway.models.get` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
			} },
			{ "gateway.models.default.get", [&]() {
				return ValidateObjectWithTokens(payload, "model", kModelFieldTokens, issue, "`gateway.models.default.get` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
			} },
			{ "gateway.models.recommended", [&]() {
				if (!IsFieldValueType(payload, "reason", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.recommended` requires `model` object and `reason` string.");
					return false;
				}
				return ValidateObjectWithTokens(payload, "model", kModelFieldTokens, issue, "`gateway.models.recommended` requires model fields `id`, `provider`, `displayName`, and `streaming`.");
			} },
			{ "gateway.events.sequence", [&]() {
				return ValidateNumberAndString(payload, "sequence", "event", issue, "`gateway.events.sequence` requires `sequence` number and `event` string.");
			} },
			{ "gateway.events.offset", [&]() {
				return ValidateNumberAndString(payload, "offset", "event", issue, "`gateway.events.offset` requires `offset` number and `event` string.");
			} },
			{ "gateway.events.latestByType", [&]() {
				return ValidateTwoStringFields(payload, "type", "event", issue, "`gateway.events.latestByType` requires `type` string and `event` string.");
			} },
			{ "gateway.events.last", [&]() {
				if (!IsFieldValueType(payload, "event", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.last` requires `event` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.get", [&]() {
				if (!IsFieldValueType(payload, "event", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.get` requires `event` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.catalog", [&]() {
				return ValidateArrayWithOptionalEntryTokens(payload, "tools", kToolFieldTokens, issue, "`gateway.tools.catalog` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
			} },
			{ "gateway.tools.list", [&]() {
				return ValidateArrayWithOptionalEntryTokens(payload, "tools", kToolFieldTokens, issue, "`gateway.tools.list` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
			} },
			{ "gateway.tools.get", [&]() {
				return ValidateObjectWithTokens(payload, "tool", kToolFieldTokens, issue, "`gateway.tools.get` requires tool fields `id`, `label`, `category`, and `enabled`.");
			} },
			{ "gateway.agents.files.list", [&]() {
				if (!IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.files.list` requires `files` array and `count` number fields.");
					return false;
				}
				return ValidateArrayWithOptionalEntryTokens(payload, "files", kFileListFieldTokens, issue, "`gateway.agents.files.list` requires file entries with `path`, `size`, and `updatedMs` fields.");
			} },
			{ "gateway.agents.files.get", [&]() {
				return ValidateObjectWithTokens(payload, "file", kFileFieldTokens, issue, "`gateway.agents.files.get` requires `file` fields `path`, `size`, `updatedMs`, and `content`.");
			} },
			{ "gateway.agents.files.set", [&]() {
				return ValidateObjectWithBooleanAndTokens(payload, "file", "saved", kFileFieldTokens, issue, "`gateway.agents.files.set` requires `file` object and `saved` boolean.");
			} },
			{ "gateway.agents.files.delete", [&]() {
				return ValidateObjectWithBooleanAndTokens(payload, "file", "deleted", kFileFieldTokens, issue, "`gateway.agents.files.delete` requires `file` object and `deleted` boolean.");
			} },
			{ "gateway.transport.endpoint.get", [&]() {
				if (!IsFieldValueType(payload, "endpoint", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.endpoint.get` requires `endpoint` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.endpoint.exists", [&]() {
				return ValidateStringAndBoolean(payload, "endpoint", "exists", issue, "`gateway.transport.endpoint.exists` requires `endpoint` string and `exists` boolean.");
			} },
			{ "gateway.transport.endpoint.set", [&]() {
				return ValidateStringAndBoolean(payload, "endpoint", "updated", issue, "`gateway.transport.endpoint.set` requires `endpoint` string and `updated` boolean.");
			} },
			{ "gateway.transport.endpoints.list", [&]() {
				return ValidateArrayAndCount(payload, "endpoints", issue, "`gateway.transport.endpoints.list` requires `endpoints` array and `count` number.");
			} },
			{ "gateway.config.validate", [&]() {
				return ValidateBooleanArrayAndCount(payload, "valid", "errors", issue, "`gateway.config.validate` requires `valid` boolean, `errors` array, and `count` number.");
			} },
			{ "gateway.transport.policy.validate", [&]() {
				return ValidateBooleanArrayAndCount(payload, "valid", "errors", issue, "`gateway.transport.policy.validate` requires `valid` boolean, `errors` array, and `count` number.");
			} },
			{ "gateway.config.history", [&]() {
				return ValidateArrayAndCount(payload, "revisions", issue, "`gateway.config.history` requires `revisions` array and `count` number.");
			} },
			{ "gateway.config.diff", [&]() {
				return ValidateArrayAndCount(payload, "changed", issue, "`gateway.config.diff` requires `changed` array and `count` number.");
			} },
			{ "gateway.transport.policy.apply", [&]() {
				if (!IsFieldBoolean(payload, "applied") || !IsFieldNumber(payload, "version") || !IsFieldValueType(payload, "reason", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.apply` requires `applied` boolean, `version` number, and `reason` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.preview", [&]() {
				if (!IsFieldValueType(payload, "path", '"') || !IsFieldBoolean(payload, "applied") || !IsFieldValueType(payload, "notes", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.preview` requires `path` string, `applied` boolean, and `notes` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.digest", [&]() {
				if (!IsFieldValueType(payload, "digest", '"') || !IsFieldNumber(payload, "version")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.digest` requires `digest` string and `version` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.export", [&]() {
				if (!IsFieldValueType(payload, "path", '"') || !IsFieldNumber(payload, "version") || !IsFieldBoolean(payload, "exported")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.export` requires `path` string, `version` number, and `exported` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.set", [&]() {
				return ValidateBooleanAndString(payload, "applied", "reason", issue, "`gateway.transport.policy.set` requires `applied` boolean and `reason` string.");
			} },
			{ "gateway.transport.policy.reset", [&]() {
				if (!IsFieldBoolean(payload, "reset") || !IsFieldBoolean(payload, "applied")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.reset` requires `reset` boolean and `applied` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.status", [&]() {
				if (!IsFieldBoolean(payload, "mutable") || !IsFieldValueType(payload, "lastApplied", '"') || !IsFieldNumber(payload, "policyVersion")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.status` requires `mutable` boolean, `lastApplied` string, and `policyVersion` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.pipeline", [&]() {
				return ValidateNumericFields(payload, { "queued", "running", "failed", "tools" }, issue, "`gateway.tools.pipeline` requires numeric fields `queued`, `running`, `failed`, and `tools`.");
			} },
			{ "gateway.tools.throughput", [&]() {
				return ValidateNumericFields(payload, { "calls", "windowSec", "perMinute", "tools" }, issue, "`gateway.tools.throughput` requires numeric fields `calls`, `windowSec`, `perMinute`, and `tools`.");
			} },
			{ "gateway.tools.metrics", [&]() {
				return ValidateNumericFields(payload, { "invocations", "enabled", "disabled", "total" }, issue, "`gateway.tools.metrics` requires numeric fields `invocations`, `enabled`, `disabled`, and `total`.");
			} },
			{ "gateway.tools.health", [&]() {
				if (!IsFieldBoolean(payload, "healthy") || !IsFieldNumber(payload, "enabled") || !IsFieldNumber(payload, "total")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.tools.health` requires `healthy` boolean plus numeric `enabled` and `total` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.latency", [&]() {
				return ValidateNumericFields(payload, { "minMs", "maxMs", "avgMs", "samples" }, issue, "`gateway.tools.latency` requires numeric fields `minMs`, `maxMs`, `avgMs`, and `samples`.");
			} },
			{ "gateway.models.manifest", [&]() {
				if (!IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "manifestVersion")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.manifest` requires `models` array and `manifestVersion` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.pool", [&]() {
				return ValidateArrayAndCount(payload, "models", issue, "`gateway.models.pool` requires `models` array and `count` number.");
			} },
			{ "gateway.models.affinity", [&]() {
				if (!IsFieldValueType(payload, "models", '[') || !IsFieldValueType(payload, "affinity", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.affinity` requires `models` array and `affinity` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.preference", [&]() {
				if (!IsFieldValueType(payload, "model", '"') || !IsFieldValueType(payload, "provider", '"') || !IsFieldValueType(payload, "source", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.preference` requires `model` string, `provider` string, and `source` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.routing", [&]() {
				if (!IsFieldValueType(payload, "primary", '"') || !IsFieldValueType(payload, "fallback", '"') || !IsFieldValueType(payload, "strategy", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.routing` requires `primary` string, `fallback` string, and `strategy` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.selection", [&]() {
				return ValidateTwoStringFields(payload, "selected", "strategy", issue, "`gateway.models.selection` requires `selected` string and `strategy` string.");
			} },
			{ "gateway.models.fallback", [&]() {
				if (!IsFieldValueType(payload, "preferred", '"') || !IsFieldValueType(payload, "fallback", '"') || !IsFieldBoolean(payload, "configured")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.fallback` requires `preferred` string, `fallback` string, and `configured` boolean.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.profile", [&]() {
				return ValidateTwoStringFields(payload, "name", "source", issue, "`gateway.config.profile` requires `name` string and `source` string.");
			} },
			{ "gateway.config.revision", [&]() {
				return ValidateNumberAndString(payload, "revision", "source", issue, "`gateway.config.revision` requires `revision` number and `source` string.");
			} },
			{ "gateway.config.backup", [&]() {
				if (!IsFieldBoolean(payload, "saved") || !IsFieldNumber(payload, "version") || !IsFieldValueType(payload, "path", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.backup` requires `saved` boolean, `version` number, and `path` string.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.rollback", [&]() {
				return ValidateBooleanAndNumber(payload, "rolledBack", "version", issue, "`gateway.config.rollback` requires `rolledBack` boolean and `version` number.");
			} },
			{ "gateway.events.summary", [&]() {
				return ValidateNumericFields(payload, { "total", "lifecycle", "updates" }, issue, "`gateway.events.summary` requires numeric fields `total`, `lifecycle`, and `updates`.");
			} },
			{ "gateway.events.channels", [&]() {
				return ValidateNumericFields(payload, { "channelEvents", "accountEvents", "count" }, issue, "`gateway.events.channels` requires numeric fields `channelEvents`, `accountEvents`, and `count`.");
			} },
			{ "gateway.models.priority", [&]() {
				if (!ValidateArrayAndCount(payload, "models", issue, "`gateway.models.priority` requires `models` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "default", "reasoner" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.priority` is missing required model ids.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.providers", [&]() {
				if (!ValidateArrayAndCount(payload, "providers", issue, "`gateway.models.providers` requires `providers` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "seed" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.providers` is missing required provider members.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.compatibility", [&]() {
				if (!IsFieldValueType(payload, "default", '"') || !IsFieldValueType(payload, "reasoner", '"') || !IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.compatibility` requires `default` string, `reasoner` string, and `count` number.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.schema", [&]() {
				if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.schema` requires `gateway` and `agent` object fields.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, { "bind", "port", "model", "streaming" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.schema` requires `bind`, `port`, `model`, and `streaming` schema members.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.snapshot", [&]() {
				if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.snapshot` requires `gateway` and `agent` object fields.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, { "bind", "port", "model", "streaming" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.snapshot` requires `bind`, `port`, `model`, and `streaming` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.logs.levels", [&]() {
				if (!ValidateArrayAndCount(payload, "levels", issue, "`gateway.logs.levels` requires `levels` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "info", "debug" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.logs.levels` is missing required level members.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.categories", [&]() {
				if (!ValidateArrayAndCount(payload, "categories", issue, "`gateway.tools.categories` requires `categories` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "messaging", "knowledge" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.tools.categories` is missing required category members.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.types", [&]() {
				if (!ValidateArrayAndCount(payload, "types", issue, "`gateway.events.types` requires `types` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "lifecycle", "update" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.types` is missing required type members.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.list", [&]() {
				if (!ValidateArrayAndCount(payload, "events", issue, "`gateway.events.list` requires `events` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, kCatalogEventNames)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.list` is missing required event members.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.catalog", [&]() {
				if (!IsFieldValueType(payload, "events", '[')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.catalog` requires array field `events`.");
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, kCatalogEventNames)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.catalog` is missing required event members.");
					return false;
				}
				return true;
			} },
			{ "gateway.events.batch", [&]() {
				if (!ValidateArrayAndCount(payload, "batches", issue, "`gateway.events.batch` requires `batches` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "lifecycle", "updates" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.events.batch` is missing required batch names.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.count", [&]() {
				return ValidateNumberAndString(payload, "count", "section", issue, "`gateway.config.count` requires `section` string and `count` number.");
			} },
			{ "gateway.models.count", [&]() {
				return ValidateNumberAndString(payload, "count", "provider", issue, "`gateway.models.count` requires `provider` string and `count` number.");
			} },
			{ "gateway.config.getKey", [&]() {
				return ValidateTwoStringFields(payload, "key", "value", issue, "`gateway.config.getKey` requires `key` string and `value` string.");
			} },
			{ "gateway.events.search", [&]() {
				return ValidateStringArrayAndCount(payload, "term", "events", issue, "`gateway.events.search` requires `term` string, `events` array, and `count` number.");
			} },
			{ "gateway.config.audit", [&]() {
				if (!IsFieldBoolean(payload, "enabled") || !IsFieldValueType(payload, "source", '"') || !IsFieldNumber(payload, "lastUpdatedMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.audit` requires `enabled` boolean, `source` string, and `lastUpdatedMs` number.");
					return false;
				}
				return true;
			} },
		  { "gateway.agents.create", [&]() {
				if (!IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "created")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.create` requires `agent` object and `created` boolean.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kAgentFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.create` requires `agent` fields `id`, `name`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.delete", [&]() {
				if (!IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "deleted") || !IsFieldNumber(payload, "remaining")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.delete` requires `agent` object, `deleted` boolean, and `remaining` number.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kAgentFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.delete` requires `agent` fields `id`, `name`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.update", [&]() {
				if (!IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "updated")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.update` requires `agent` object and `updated` boolean.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kAgentFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.update` requires `agent` fields `id`, `name`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.list", [&]() {
				if (!IsFieldValueType(payload, "agents", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "activeAgentId", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.list` requires `agents` array, `count` number, and `activeAgentId` string fields.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "agents")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, kAgentFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.agents.list` requires agent entries with `id`, `name`, and `active` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.agents.files.exists", [&]() {
				return ValidateStringAndBoolean(payload, "path", "exists", issue, "`gateway.agents.files.exists` requires `path` string and `exists` boolean fields.");
			} },
			{ "gateway.sessions.delete", [&]() {
				if (!IsFieldValueType(payload, "session", '{') || !IsFieldBoolean(payload, "deleted") || !IsFieldNumber(payload, "remaining")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.delete` requires `session` object, `deleted` boolean, and `remaining` number fields.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kSessionFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.delete` requires `session` fields `id`, `scope`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.compact", [&]() {
				if (!IsFieldNumber(payload, "compacted") || !IsFieldNumber(payload, "remaining") || !IsFieldBoolean(payload, "dryRun")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.compact` requires numeric `compacted`/`remaining` and boolean `dryRun` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.patch", [&]() {
				if (!IsFieldValueType(payload, "session", '{') || !IsFieldBoolean(payload, "patched")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.patch` requires `session` object and `patched` boolean.");
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, kSessionFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.patch` requires `session` fields `id`, `scope`, and `active`.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.preview", [&]() {
				const bool hasRoot = IsFieldValueType(payload, "session", '{') && IsFieldValueType(payload, "title", '"') && IsFieldBoolean(payload, "hasMessages") && IsFieldNumber(payload, "unread");
				if (!hasRoot || !PayloadContainsAllFieldTokens(payload, kSessionFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.preview` requires `session` with `id/scope/active`, `title`, `hasMessages`, and `unread` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.sessions.usage", [&]() {
				const bool hasRoot = IsFieldValueType(payload, "sessionId", '"') && IsFieldNumber(payload, "messages") && IsFieldValueType(payload, "tokens", '{') && IsFieldNumber(payload, "lastActiveMs");
				const bool hasTokens = IsFieldNumber(payload, "input") && IsFieldNumber(payload, "output") && IsFieldNumber(payload, "total");
				if (!(hasRoot && hasTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.sessions.usage` requires `sessionId`, `messages`, `tokens.{input,output,total}`, and `lastActiveMs` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.session.list", [&]() {
				if (!IsFieldValueType(payload, "sessions", '[') || !IsFieldNumber(payload, "count") || !IsFieldValueType(payload, "activeSessionId", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.session.list` requires `sessions` array, `count` number, and `activeSessionId` string fields.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "sessions")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, kSessionFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.session.list` requires session entries with `id`, `scope`, and `active` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.get", [&]() {
				if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.get` requires object fields `gateway` and `agent`.");
					return false;
				}
				if (!IsFieldValueType(payload, "bind", '"') || !IsFieldNumber(payload, "port") || !IsFieldValueType(payload, "model", '"') || !IsFieldBoolean(payload, "streaming")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.get` requires `gateway.bind` string, `gateway.port` number, `agent.model` string, and `agent.streaming` boolean fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.set", [&]() {
				if (!IsFieldValueType(payload, "gateway", '{') || !IsFieldValueType(payload, "agent", '{') || !IsFieldBoolean(payload, "updated")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.set` requires `gateway`, `agent`, and `updated` fields.");
					return false;
				}
				if (!IsFieldValueType(payload, "bind", '"') || !IsFieldNumber(payload, "port") || !IsFieldValueType(payload, "model", '"') || !IsFieldBoolean(payload, "streaming")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.set` requires `gateway.bind`, `gateway.port`, `agent.model`, and `agent.streaming` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.getSection", [&]() {
				if (!IsFieldValueType(payload, "section", '"') || !IsFieldValueType(payload, "config", '{')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.getSection` requires `section` string and `config` object.");
					return false;
				}
				return true;
			} },
			{ "gateway.config.sections", [&]() {
				if (!ValidateArrayAndCount(payload, "sections", issue, "`gateway.config.sections` requires `sections` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllStringValues(payload, { "gateway", "agent" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.config.sections` is missing required section members.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.connections.count", [&]() {
				if (!IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.connections.count` requires numeric field `count`.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.get", [&]() {
				if (!IsFieldBoolean(payload, "exclusiveAddrUse") || !IsFieldBoolean(payload, "keepAlive") || !IsFieldBoolean(payload, "noDelay") || !IsFieldNumber(payload, "idleTimeoutMs") || !IsFieldNumber(payload, "handshakeTimeoutMs")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.get` requires policy booleans and timeout number fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.policy.history", [&]() {
				if (!ValidateArrayAndCount(payload, "entries", issue, "`gateway.transport.policy.history` requires `entries` array and `count` number.")) {
					return false;
				}
				if (!PayloadContainsAllFieldTokens(payload, { "version", "applied", "reason" })) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.policy.history` requires entry fields `version`, `applied`, and `reason`.");
					return false;
				}
				return true;
			} },
			{ "gateway.transport.status", [&]() {
				const bool hasCoreFields = IsFieldBoolean(payload, "running") && IsFieldValueType(payload, "endpoint", '"') && IsFieldNumber(payload, "connections");
				const bool hasTimeoutFields = IsFieldValueType(payload, "timeouts", '{') && IsFieldNumber(payload, "handshake") && IsFieldNumber(payload, "idle");
				const bool hasCloseFields = IsFieldValueType(payload, "closes", '{') && IsFieldNumber(payload, "invalidUtf8") && IsFieldNumber(payload, "messageTooBig") && IsFieldNumber(payload, "extensionRejected");
				if (!(hasCoreFields && hasTimeoutFields && hasCloseFields)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.transport.status` requires `running`, `endpoint`, `connections`, `timeouts.{handshake,idle}`, and `closes.{invalidUtf8,messageTooBig,extensionRejected}`.");
					return false;
				}
				return true;
			} },
			{ "gateway.tools.call.preview", [&]() {
				return IsFieldValueType(payload, "tool", '"') && IsFieldBoolean(payload, "allowed") && IsFieldValueType(payload, "reason", '"') && IsFieldBoolean(payload, "argsProvided") && IsFieldValueType(payload, "policy", '"')
					? true
					: (SetIssue(issue, "schema_invalid_response", "`gateway.tools.call.preview` requires `tool`, `allowed`, `reason`, `argsProvided`, and `policy` fields."), false);
			} },
			{ "gateway.tools.call.execute", [&]() {
				return IsFieldValueType(payload, "tool", '"') && IsFieldBoolean(payload, "executed") && IsFieldValueType(payload, "status", '"') && IsFieldValueType(payload, "output", '"') && IsFieldBoolean(payload, "argsProvided")
					? true
					: (SetIssue(issue, "schema_invalid_response", "`gateway.tools.call.execute` requires `tool`, `executed`, `status`, `output`, and `argsProvided` fields."), false);
			} },
			{ "gateway.features.list", [&]() {
				if (!IsFieldValueType(payload, "methods", '[') || !IsFieldValueType(payload, "events", '[')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.features.list` requires array fields `methods` and `events`.");
					return false;
				}

				if (!PayloadContainsAllStringValues(payload, kFeatureRequiredCatalogAll) ||
					!PayloadContainsAllStringValues(payload, kFeatureRequiredConfigCluster) ||
					!PayloadContainsAllStringValues(payload, kFeatureRequiredTransportCluster) ||
					!PayloadContainsAllStringValues(payload, kFeatureRequiredEventCluster) ||
					!PayloadContainsAllStringValues(payload, kFeatureRequiredModelToolCluster)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.features.list` catalog is missing required method/event members.");
					return false;
				}

				return true;
			} },
			{ "gateway.ping", [&]() {
				if (!IsFieldBoolean(payload, "pong")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.ping` response requires boolean field `pong`.");
					return false;
				}
				return true;
			} },
			{ "gateway.protocol.version", [&]() {
				if (!IsFieldNumber(payload, "minProtocol") || !IsFieldNumber(payload, "maxProtocol")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.protocol.version` requires numeric protocol fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.channels.logout", [&]() {
				if (!IsFieldBoolean(payload, "loggedOut") || !IsFieldNumber(payload, "affected")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.channels.logout` requires `loggedOut` boolean and `affected` number fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.logs.tail", [&]() {
				if (!IsFieldValueType(payload, "entries", '[')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.logs.tail` requires array field `entries`.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "entries")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, { "ts", "level", "source", "message" }) || !IsFieldNumber(payload, "ts") || !IsFieldValueType(payload, "level", '"') || !IsFieldValueType(payload, "source", '"') || !IsFieldValueType(payload, "message", '"')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.logs.tail` requires log entries with `ts`, `level`, `source`, and `message` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.list", [&]() {
				if (!IsFieldValueType(payload, "models", '[')) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.list` requires array field `models`.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "models")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, kModelFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.list` requires model entries with `id`, `provider`, `displayName`, and `streaming` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.models.listByProvider", [&]() {
				if (!IsFieldValueType(payload, "provider", '"') || !IsFieldValueType(payload, "models", '[') || !IsFieldNumber(payload, "count")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.listByProvider` requires `provider` string, `models` array, and `count` number.");
					return false;
				}
				if (IsArrayFieldExplicitlyEmpty(payload, "models")) {
					return true;
				}
				if (!PayloadContainsAllFieldTokens(payload, kModelFieldTokens)) {
					SetIssue(issue, "schema_invalid_response", "`gateway.models.listByProvider` requires model entries with `id`, `provider`, `displayName`, and `streaming` fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.health", [&]() {
				return IsFieldValueType(payload, "status", '"') && IsFieldBoolean(payload, "running")
					? true
					: (SetIssue(issue, "schema_invalid_response", "`gateway.health` requires `status` string and `running` boolean."), false);
			} },
			{ "gateway.health.details", [&]() {
				if (!IsFieldValueType(payload, "status", '"') || !IsFieldBoolean(payload, "running") || !IsFieldValueType(payload, "transport", '{') || !IsFieldValueType(payload, "endpoint", '"') || !IsFieldNumber(payload, "connections")) {
					SetIssue(issue, "schema_invalid_response", "`gateway.health.details` requires `status`, `running`, and `transport.{endpoint,connections}` fields.");
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
