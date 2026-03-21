#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"
#include "GatewayProtocolSchemaValidator.Internal.h"

namespace blazeclaw::gateway::protocol {

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

		using EventValidator = std::function<bool()>;
		const std::unordered_map<std::string, EventValidator> validators = {
			{ "gateway.tick", [&]() {
				if (!IsFieldNumber(payload, "ts") || !IsFieldBoolean(payload, "running") || !IsFieldNumber(payload, "connections")) {
					SetIssue(issue, "schema_invalid_event", "`gateway.tick` requires `ts` number, `running` boolean, and `connections` number fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.health", [&]() {
				if (!IsFieldValueType(payload, "status", '"') || !IsFieldBoolean(payload, "running") || !IsFieldValueType(payload, "endpoint", '"') || !IsFieldNumber(payload, "connections")) {
					SetIssue(issue, "schema_invalid_event", "`gateway.health` requires `status` string, `running` boolean, `endpoint` string, and `connections` number fields.");
					return false;
				}
				if (!IsFieldValueType(payload, "timeouts", '{') || !IsFieldNumber(payload, "handshake") || !IsFieldNumber(payload, "idle") || !IsFieldValueType(payload, "closes", '{') || !IsFieldNumber(payload, "invalidUtf8") || !IsFieldNumber(payload, "messageTooBig") || !IsFieldNumber(payload, "extensionRejected")) {
					SetIssue(issue, "schema_invalid_event", "`gateway.health` requires diagnostics objects `timeouts` and `closes` with numeric counters.");
					return false;
				}
				return true;
			} },
			{ "gateway.shutdown", [&]() {
				if (!IsFieldValueType(payload, "reason", '"') || !IsFieldBoolean(payload, "graceful") || !IsFieldNumber(payload, "seq")) {
					SetIssue(issue, "schema_invalid_event", "`gateway.shutdown` requires `reason` string, `graceful` boolean, and `seq` number fields.");
					return false;
				}
				return true;
			} },
			{ "gateway.channels.update", [&]() {
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
			} },
			{ "gateway.channels.accounts.update", [&]() {
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
			} },
			{ "gateway.session.reset", [&]() {
				if (!IsFieldValueType(payload, "sessionId", '"')) {
					SetIssue(issue, "schema_invalid_event", "`gateway.session.reset` requires string field `sessionId`.");
					return false;
				}
				if (payload.find("\"session\"") != std::string::npos) {
					if (!IsFieldValueType(payload, "session", '{') || !PayloadContainsAllFieldTokens(payload, { "id", "scope", "active" })) {
						SetIssue(issue, "schema_invalid_event", "`gateway.session.reset` optional `session` object requires `id`, `scope`, and `active` fields.");
						return false;
					}
				}
				return true;
			} },
			{ "gateway.agent.update", [&]() {
				if (!IsFieldValueType(payload, "agentId", '"')) {
					SetIssue(issue, "schema_invalid_event", "`gateway.agent.update` requires string field `agentId`.");
					return false;
				}
				if (payload.find("\"agent\"") != std::string::npos) {
					if (!IsFieldValueType(payload, "agent", '{') || !PayloadContainsAllFieldTokens(payload, { "id", "name", "active" })) {
						SetIssue(issue, "schema_invalid_event", "`gateway.agent.update` optional `agent` object requires `id`, `name`, and `active` fields.");
						return false;
					}
				}
				return true;
			} },
			{ "gateway.tools.catalog.update", [&]() {
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
			} },
		};

		if (const auto it = validators.find(event.eventName); it != validators.end()) {
			return it->second();
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
