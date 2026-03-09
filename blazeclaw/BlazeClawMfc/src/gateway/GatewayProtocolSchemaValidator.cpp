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

	bool ValidateChannelsRouteResolveParams(const RequestFrame& request, SchemaValidationIssue& issue) {
		if (!request.paramsJson.has_value()) {
			return true;
		}

		const std::string params = Trim(request.paramsJson.value());
		if (!IsJsonObjectShape(params)) {
			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `gateway.channels.route.resolve` expects `params` to be a JSON object when provided.");
			return false;
		}

		if (params.find("\"channel\"") != std::string::npos && !IsFieldValueType(params, "channel", '"')) {
			SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.resolve` requires `params.channel` to be a string.");
			return false;
		}

		if (params.find("\"accountId\"") != std::string::npos && !IsFieldValueType(params, "accountId", '"')) {
			SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.resolve` requires `params.accountId` to be a string.");
			return false;
		}

		return true;
	}

	bool ValidateOptionalChannelParam(
		const RequestFrame& request,
		SchemaValidationIssue& issue,
		const std::string& methodName) {
		if (!request.paramsJson.has_value()) {
			return true;
		}

		const std::string params = Trim(request.paramsJson.value());
		if (!IsJsonObjectShape(params)) {
			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `" + methodName + "` expects `params` to be a JSON object when provided.");
			return false;
		}

		if (params.find("\"channel\"") != std::string::npos && !IsFieldValueType(params, "channel", '"')) {
			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `" + methodName + "` requires `params.channel` to be a string.");
			return false;
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

	bool ValidateToolsCallPreviewParams(const RequestFrame& request, SchemaValidationIssue& issue) {
		if (!request.paramsJson.has_value()) {
			return true;
		}

		const std::string params = Trim(request.paramsJson.value());
		if (!IsJsonObjectShape(params)) {
			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `gateway.tools.call.preview` expects `params` to be a JSON object when provided.");
			return false;
		}

		if (params.find("\"tool\"") != std::string::npos && !IsFieldValueType(params, "tool", '"')) {
			SetIssue(issue, "schema_invalid_params", "Method `gateway.tools.call.preview` requires `params.tool` to be a string.");
			return false;
		}

		if (params.find("\"args\"") != std::string::npos && !IsFieldValueType(params, "args", '{')) {
			SetIssue(issue, "schema_invalid_params", "Method `gateway.tools.call.preview` requires `params.args` to be an object.");
			return false;
		}

		return true;
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
		if (!request.paramsJson.has_value()) {
			return true;
		}

		const std::string params = Trim(request.paramsJson.value());
		if (!IsJsonObjectShape(params)) {
			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `" + methodName + "` expects `params` to be a JSON object when provided.");
			return false;
		}

		if (params.find("\"" + fieldName + "\"") != std::string::npos &&
			!IsFieldValueType(params, fieldName, '"')) {
			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `" + methodName + "` requires `params." + fieldName + "` to be a string.");
			return false;
		}

		return true;
	}

	bool ValidateLogsTailParams(const RequestFrame& request, SchemaValidationIssue& issue) {
		if (!request.paramsJson.has_value()) {
			return true;
		}

		const std::string params = Trim(request.paramsJson.value());
		if (!IsJsonObjectShape(params)) {
			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `gateway.logs.tail` expects `params` to be a JSON object when provided.");
			return false;
		}

		if (params.find("\"limit\"") != std::string::npos && !IsFieldNumber(params, "limit")) {
			SetIssue(issue, "schema_invalid_params", "Method `gateway.logs.tail` requires `params.limit` to be numeric.");
			return false;
		}

		return true;
	}

	bool ValidatePingParams(const RequestFrame& request, SchemaValidationIssue& issue) {
		if (!request.paramsJson.has_value()) {
			return true;
		}

		const std::string params = Trim(request.paramsJson.value());
		if (!IsJsonObjectShape(params)) {
			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `gateway.ping` expects `params` to be a JSON object when provided.");
			return false;
		}

		if (params.find("\"echo\"") != std::string::npos && !IsFieldValueType(params, "echo", '"')) {
			SetIssue(issue, "schema_invalid_params", "Method `gateway.ping` requires `params.echo` to be a string.");
			return false;
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

	if (request.method == "gateway.channels.route.resolve") {
		return ValidateChannelsRouteResolveParams(request, issue);
	}

	if (request.method == "gateway.tools.call.preview") {
		return ValidateToolsCallPreviewParams(request, issue);
	}

	if (request.method == "gateway.sessions.resolve") {
		return ValidateStringIdParam(request, issue, request.method, "sessionId");
	}

	if (request.method == "gateway.sessions.create" || request.method == "gateway.sessions.reset") {
		return ValidateStringIdParam(request, issue, request.method, "sessionId");
	}

	if (request.method == "gateway.agents.get" || request.method == "gateway.agents.activate") {
		return ValidateStringIdParam(request, issue, request.method, "agentId");
	}

	if (request.method == "gateway.protocol.version" ||
		request.method == "gateway.features.list" ||
		request.method == "gateway.config.get" ||
		request.method == "gateway.agents.list" ||
		request.method == "gateway.tools.catalog" ||
		request.method == "gateway.health" ||
		request.method == "gateway.transport.status" ||
		request.method == "gateway.session.list" ||
		request.method == "gateway.events.catalog") {
		return ValidateNoParamsAllowed(request, issue, request.method);
	}

	if (request.method == "gateway.channels.status" ||
		request.method == "gateway.channels.routes" ||
		request.method == "gateway.channels.accounts") {
		return ValidateOptionalChannelParam(request, issue, request.method);
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

	if (method == "gateway.protocol.version") {
		if (!IsFieldNumber(payload, "minProtocol") || !IsFieldNumber(payload, "maxProtocol")) {
			SetIssue(issue, "schema_invalid_response", "`gateway.protocol.version` requires numeric protocol fields.");
			return false;
		}
		return true;
	}

	if (method == "gateway.config.get") {
		return IsFieldValueType(payload, "gateway", '{') && IsFieldValueType(payload, "agent", '{')
			? true
			: (SetIssue(issue, "schema_invalid_response", "`gateway.config.get` requires object fields `gateway` and `agent`."), false);
	}

	if (method == "gateway.channels.status") {
       if (!IsFieldValueType(payload, "channels", '[')) {
			SetIssue(issue, "schema_invalid_response", "`gateway.channels.status` requires array field `channels`.");
			return false;
		}

		if (IsArrayFieldExplicitlyEmpty(payload, "channels")) {
			return true;
		}

		if (!PayloadContainsAllFieldTokens(payload, {"id", "label", "connected", "accounts"})) {
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

		if (!PayloadContainsAllFieldTokens(payload, {"channel", "accountId", "agentId", "sessionId"})) {
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

		if (!PayloadContainsAllFieldTokens(payload, {"channel", "accountId", "label", "active", "connected"})) {
			SetIssue(issue, "schema_invalid_response", "`gateway.channels.accounts` requires account entries with `channel`, `accountId`, `label`, `active`, and `connected` fields.");
			return false;
		}

		return true;
	}

	if (method == "gateway.channels.route.resolve") {
      if (!IsFieldValueType(payload, "route", '{')) {
			SetIssue(issue, "schema_invalid_response", "`gateway.channels.route.resolve` requires object field `route`.");
			return false;
		}

		if (!PayloadContainsAllFieldTokens(payload, {"channel", "accountId", "agentId", "sessionId"})) {
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

		if (!PayloadContainsAllFieldTokens(payload, {"ts", "level", "source", "message"}) ||
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

		if (!PayloadContainsAllFieldTokens(payload, {"id", "scope", "active"})) {
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

		if (!PayloadContainsAllFieldTokens(payload, {"id", "name", "active"})) {
			SetIssue(issue, "schema_invalid_response", "`" + method + "` requires agent fields `id`, `name`, and `active`.");
			return false;
		}

		return true;
	}

	if (method == "gateway.agents.list") {
     if (!IsFieldValueType(payload, "agents", '[')) {
			SetIssue(issue, "schema_invalid_response", "`gateway.agents.list` requires array field `agents`.");
			return false;
		}

		if (IsArrayFieldExplicitlyEmpty(payload, "agents")) {
			return true;
		}

		if (!PayloadContainsAllFieldTokens(payload, {"id", "name", "active"})) {
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

		if (!PayloadContainsAllFieldTokens(payload, {"id", "label", "category", "enabled"})) {
			SetIssue(issue, "schema_invalid_response", "`gateway.tools.catalog` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
			return false;
		}

		return true;
	}

	if (method == "gateway.tools.call.preview") {
		return IsFieldValueType(payload, "tool", '"') && IsFieldBoolean(payload, "allowed") &&
			IsFieldValueType(payload, "reason", '"')
			? true
			: (SetIssue(issue, "schema_invalid_response", "`gateway.tools.call.preview` requires `tool`, `allowed`, and `reason` fields."), false);
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
       if (!IsFieldValueType(payload, "sessions", '[')) {
			SetIssue(issue, "schema_invalid_response", "`gateway.session.list` requires array field `sessions`.");
			return false;
		}

		if (IsArrayFieldExplicitlyEmpty(payload, "sessions")) {
			return true;
		}

		if (!PayloadContainsAllFieldTokens(payload, {"id", "scope", "active"})) {
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
		return IsFieldNumber(payload, "ts")
			? true
			: (SetIssue(issue, "schema_invalid_event", "`gateway.tick` requires numeric field `ts`."), false);
	}

	if (event.eventName == "gateway.health") {
		return IsFieldValueType(payload, "status", '"') && IsFieldBoolean(payload, "running")
			? true
			: (SetIssue(issue, "schema_invalid_event", "`gateway.health` requires `status` string and `running` boolean."), false);
	}

	if (event.eventName == "gateway.shutdown") {
		return IsFieldValueType(payload, "reason", '"')
			? true
			: (SetIssue(issue, "schema_invalid_event", "`gateway.shutdown` requires string field `reason`."), false);
	}

	if (event.eventName == "gateway.channels.update") {
       if (!IsFieldValueType(payload, "channels", '[')) {
			SetIssue(issue, "schema_invalid_event", "`gateway.channels.update` requires array field `channels`.");
			return false;
		}

		if (IsArrayFieldExplicitlyEmpty(payload, "channels")) {
			return true;
		}

		if (!PayloadContainsAllFieldTokens(payload, {"id", "label", "connected", "accounts"})) {
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

		if (!PayloadContainsAllFieldTokens(payload, {"channel", "accountId", "label", "active", "connected"})) {
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
				!PayloadContainsAllFieldTokens(payload, {"id", "scope", "active"})) {
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
				!PayloadContainsAllFieldTokens(payload, {"id", "name", "active"})) {
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

		if (!PayloadContainsAllFieldTokens(payload, {"id", "label", "category", "enabled"})) {
			SetIssue(issue, "schema_invalid_event", "`gateway.tools.catalog.update` requires tool entries with `id`, `label`, `category`, and `enabled` fields.");
			return false;
		}

		return true;
	}

	return true;
}

} // namespace blazeclaw::gateway::protocol
