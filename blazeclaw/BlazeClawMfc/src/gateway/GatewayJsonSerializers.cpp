#include "pch.h"
#include "GatewayJsonSerializers.h"

#include <algorithm>

namespace blazeclaw::gateway {

std::string EscapeJsonString(const std::string& value) {
	std::string escaped;
	escaped.reserve(value.size() + 8);

	for (const char ch : value) {
		switch (ch) {
		case '"':
			escaped += "\\\"";
			break;
		case '\\':
			escaped += "\\\\";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped.push_back(ch);
			break;
		}
	}

	return escaped;
}

std::string SerializeSession(const SessionEntry& session) {
	return "{\"id\":\"" + EscapeJsonString(session.id) + "\",\"scope\":\"" + EscapeJsonString(session.scope) +
		"\",\"active\":" + std::string(session.active ? "true" : "false") + "}";
}

std::string SerializeAgent(const AgentEntry& agent) {
	return "{\"id\":\"" + EscapeJsonString(agent.id) + "\",\"name\":\"" + EscapeJsonString(agent.name) +
		"\",\"active\":" + std::string(agent.active ? "true" : "false") + "}";
}

std::string SerializeAgentFile(const AgentFileEntry& file) {
	return "{\"path\":\"" + EscapeJsonString(file.path) + "\",\"size\":" + std::to_string(file.size) +
		",\"updatedMs\":" + std::to_string(file.updatedMs) + "}";
}

std::string SerializeAgentFileContent(const AgentFileContentEntry& file) {
	return "{\"path\":\"" + EscapeJsonString(file.path) + "\",\"size\":" + std::to_string(file.size) +
		",\"updatedMs\":" + std::to_string(file.updatedMs) +
		",\"content\":\"" + EscapeJsonString(file.content) + "\"}";
}

std::string SerializeChannelStatus(const ChannelStatusEntry& channel) {
	return "{\"id\":\"" + EscapeJsonString(channel.id) + "\",\"label\":\"" + EscapeJsonString(channel.label) +
		"\",\"connected\":" + std::string(channel.connected ? "true" : "false") +
		",\"accounts\":" + std::to_string(channel.accountCount) + "}";
}

std::string SerializeChannelAccount(const ChannelAccountEntry& account) {
	return "{\"channel\":\"" + EscapeJsonString(account.channel) + "\",\"accountId\":\"" +
		EscapeJsonString(account.accountId) + "\",\"label\":\"" + EscapeJsonString(account.label) +
		"\",\"active\":" + std::string(account.active ? "true" : "false") +
		",\"connected\":" + std::string(account.connected ? "true" : "false") + "}";
}

std::string SerializeChannelRoute(const ChannelRouteEntry& route) {
	return "{\"channel\":\"" + EscapeJsonString(route.channel) + "\",\"accountId\":\"" +
		EscapeJsonString(route.accountId) + "\",\"agentId\":\"" + EscapeJsonString(route.agentId) +
		"\",\"sessionId\":\"" + EscapeJsonString(route.sessionId) + "\"}";
}

std::string SerializeTool(const ToolCatalogEntry& tool) {
	const auto dot = tool.id.find('.');
	const std::string derivedSkillKey =
		dot == std::string::npos
		? tool.id
		: tool.id.substr(0, dot);
	const std::string skillKey =
		tool.skillKey.empty() ? derivedSkillKey : tool.skillKey;
	const std::string installKind =
		tool.installKind.empty()
		? (tool.category == "extension"
			? "runtime-registered"
			: tool.category)
		: tool.installKind;
	const std::string source =
		tool.source.empty()
		? "runtime.tool.registry"
		: tool.source;
	return "{\"id\":\"" + EscapeJsonString(tool.id) + "\",\"label\":\"" + EscapeJsonString(tool.label) +
		"\",\"category\":\"" + EscapeJsonString(tool.category) +
		"\",\"skillKey\":\"" + EscapeJsonString(skillKey) +
		"\",\"installKind\":\"" + EscapeJsonString(installKind) +
		"\",\"source\":\"" + EscapeJsonString(source) + "\",\"enabled\":" +
		std::string(tool.enabled ? "true" : "false") + "}";
}

std::string SerializeTaskDeltaEntry(const TaskDeltaEntry& delta) {
	return "{\"index\":" + std::to_string(delta.index) +
		",\"schemaVersion\":" + std::to_string(delta.schemaVersion) +
		",\"runId\":\"" + EscapeJsonString(delta.runId) +
		"\",\"sessionId\":\"" + EscapeJsonString(delta.sessionId) +
		"\",\"phase\":\"" + EscapeJsonString(delta.phase) +
		"\",\"toolName\":\"" + EscapeJsonString(delta.toolName) +
		"\",\"fallbackBackend\":\"" + EscapeJsonString(delta.fallbackBackend) +
		"\",\"fallbackAction\":\"" + EscapeJsonString(delta.fallbackAction) +
		"\",\"fallbackAttempt\":" + std::to_string(delta.fallbackAttempt) +
		",\"fallbackMaxAttempts\":" + std::to_string(delta.fallbackMaxAttempts) +
		",\"argsJson\":\"" + EscapeJsonString(delta.argsJson) +
		"\",\"resultJson\":\"" + EscapeJsonString(delta.resultJson) +
		"\",\"status\":\"" + EscapeJsonString(delta.status) +
		"\",\"errorCode\":\"" + EscapeJsonString(delta.errorCode) +
		"\",\"errorMessage\":\"" + EscapeJsonString(delta.errorMessage) +
		"\",\"startedAtMs\":" + std::to_string(delta.startedAtMs) +
		",\"completedAtMs\":" + std::to_string(delta.completedAtMs) +
		",\"latencyMs\":" + std::to_string(delta.latencyMs) +
		",\"modelTurnId\":\"" + EscapeJsonString(delta.modelTurnId) +
		"\",\"stepLabel\":\"" + EscapeJsonString(delta.stepLabel) + "\"}";
}

std::string SerializeTaskDeltaState(
	const std::unordered_map<std::string, std::vector<TaskDeltaEntry>>& state) {
	std::vector<std::string> runIds;
	runIds.reserve(state.size());
	for (const auto& [runId, _] : state) {
		runIds.push_back(runId);
	}

	std::sort(runIds.begin(), runIds.end());

	std::string json = "{\"runs\":[";
	bool firstRun = true;
	for (const auto& runId : runIds) {
		const auto it = state.find(runId);
		if (it == state.end()) {
			continue;
		}

		if (!firstRun) {
			json += ",";
		}
		firstRun = false;

		json += "{\"runId\":\"" + EscapeJsonString(runId) + "\",\"taskDeltas\":[";
		for (std::size_t i = 0; i < it->second.size(); ++i) {
			if (i > 0) {
				json += ",";
			}

			json += SerializeTaskDeltaEntry(it->second[i]);
		}
		json += "]}";
	}

	json += "]}";
	return json;
}

std::string SerializeToolExecution(const ToolExecutionEntry& execution) {
	return "{\"tool\":\"" + EscapeJsonString(execution.tool) + "\",\"executed\":" +
		std::string(execution.executed ? "true" : "false") +
		",\"status\":\"" + EscapeJsonString(execution.status) + "\",\"output\":\"" +
		EscapeJsonString(execution.output) + "\",\"argsProvided\":" +
		std::string(execution.argsProvided ? "true" : "false") + "}";
}

std::string SerializeChannelAdapter(const ChannelAdapterDescriptor& adapter) {
	return "{\"id\":\"" + EscapeJsonString(adapter.id) + "\",\"label\":\"" +
		EscapeJsonString(adapter.label) + "\",\"defaultAccountId\":\"" +
		EscapeJsonString(adapter.defaultAccountId) + "\"}";
}

std::string SerializeStringArray(const std::vector<std::string>& values) {
	std::string json = "[";
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i > 0) {
			json += ",";
		}

		json += "\"" + EscapeJsonString(values[i]) + "\"";
	}

	json += "]";
	return json;
}

} // namespace blazeclaw::gateway
