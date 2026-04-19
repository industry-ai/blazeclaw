#pragma once

#include "GatewayAgentRegistry.h"
#include "GatewayChannelRegistry.h"
#include "GatewaySessionRegistry.h"
#include "GatewayToolRegistry.h"
#include "TaskDeltaRepository.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::gateway {

/// Minimal JSON string escaping for concatenated protocol payloads (quotes, backslash, common control chars).
[[nodiscard]] std::string EscapeJsonString(const std::string& value);

[[nodiscard]] std::string SerializeSession(const SessionEntry& session);
[[nodiscard]] std::string SerializeAgent(const AgentEntry& agent);
[[nodiscard]] std::string SerializeAgentFile(const AgentFileEntry& file);
[[nodiscard]] std::string SerializeAgentFileContent(const AgentFileContentEntry& file);
[[nodiscard]] std::string SerializeChannelStatus(const ChannelStatusEntry& channel);
[[nodiscard]] std::string SerializeChannelAccount(const ChannelAccountEntry& account);
[[nodiscard]] std::string SerializeChannelRoute(const ChannelRouteEntry& route);
[[nodiscard]] std::string SerializeTool(const ToolCatalogEntry& tool);
[[nodiscard]] std::string SerializeTaskDeltaEntry(const TaskDeltaEntry& delta);
[[nodiscard]] std::string SerializeTaskDeltaState(
	const std::unordered_map<std::string, std::vector<TaskDeltaEntry>>& state);
[[nodiscard]] std::string SerializeToolExecution(const ToolExecutionEntry& execution);
[[nodiscard]] std::string SerializeChannelAdapter(const ChannelAdapterDescriptor& adapter);
[[nodiscard]] std::string SerializeStringArray(const std::vector<std::string>& values);

} // namespace blazeclaw::gateway
