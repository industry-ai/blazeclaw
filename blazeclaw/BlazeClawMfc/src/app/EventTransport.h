#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

enum class BridgeEventTopic
{
	WsFrame,
	WsClose,
	Lifecycle,
	ChatEvents,
	PollHealth,
	ToolsLifecycle,
	RpcResult,
};

class CEventTransport
{
public:
	using EmitJsonCallback = std::function<void(const std::string& json)>;
	using SessionIdProvider = std::function<std::string()>;

	void SetEmitter(EmitJsonCallback callback);
	void SetSessionIdProvider(SessionIdProvider provider);

	void EmitTopic(
		BridgeEventTopic topic,
		const std::string& payloadObjectJson);

private:
	static std::string TopicToString(BridgeEventTopic topic);

	std::vector<std::string> ResolveCompatibilityChannels(
		BridgeEventTopic topic) const;

	std::string BuildCanonicalEnvelope(
		BridgeEventTopic topic,
		const std::string& payloadObjectJson,
		const std::vector<std::string>& compatChannels);

	void EmitCompatibilityChannels(
		const std::vector<std::string>& channels,
		const std::string& payloadObjectJson);

	EmitJsonCallback m_emitJson;
	SessionIdProvider m_sessionIdProvider;
	std::uint64_t m_transportSeq = 0;
};
