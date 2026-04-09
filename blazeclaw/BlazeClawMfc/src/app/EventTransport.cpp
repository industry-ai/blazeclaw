#include "pch.h"
#include "EventTransport.h"

#include <nlohmann/json.hpp>

#include <utility>

void CEventTransport::SetEmitter(EmitJsonCallback callback)
{
	m_emitJson = std::move(callback);
}

void CEventTransport::SetSessionIdProvider(SessionIdProvider provider)
{
	m_sessionIdProvider = std::move(provider);
}

void CEventTransport::EmitTopic(
	const BridgeEventTopic topic,
	const std::string& payloadObjectJson)
{
	if (!m_emitJson)
	{
		return;
	}

	const auto compatChannels = ResolveCompatibilityChannels(topic);
	const std::string canonicalJson = BuildCanonicalEnvelope(
		topic,
		payloadObjectJson,
		compatChannels);
	if (!canonicalJson.empty())
	{
		m_emitJson(canonicalJson);
	}

	EmitCompatibilityChannels(compatChannels, payloadObjectJson);
}

std::string CEventTransport::TopicToString(const BridgeEventTopic topic)
{
	switch (topic)
	{
	case BridgeEventTopic::WsFrame:
		return "ws.frame";
	case BridgeEventTopic::WsClose:
		return "ws.close";
	case BridgeEventTopic::Lifecycle:
		return "lifecycle";
	case BridgeEventTopic::ChatEvents:
		return "chat.events";
	case BridgeEventTopic::PollHealth:
		return "poll.health";
	case BridgeEventTopic::ToolsLifecycle:
		return "tools.lifecycle";
	case BridgeEventTopic::RpcResult:
		return "rpc.result";
	default:
		return "unknown";
	}
}

std::vector<std::string> CEventTransport::ResolveCompatibilityChannels(
	const BridgeEventTopic topic) const
{
	switch (topic)
	{
	case BridgeEventTopic::WsFrame:
		return { "openclaw.ws.frame" };
	case BridgeEventTopic::WsClose:
		return { "openclaw.ws.close" };
	case BridgeEventTopic::Lifecycle:
		return { "blazeclaw.gateway.lifecycle" };
	case BridgeEventTopic::ChatEvents:
		return { "blazeclaw.gateway.chat.events" };
	case BridgeEventTopic::PollHealth:
		return { "blazeclaw.gateway.poll.health" };
	case BridgeEventTopic::ToolsLifecycle:
		return { "blazeclaw.gateway.tools.lifecycle" };
	case BridgeEventTopic::RpcResult:
		return { "blazeclaw.gateway.rpc.result" };
	default:
		return {};
	}
}

std::string CEventTransport::BuildCanonicalEnvelope(
	const BridgeEventTopic topic,
	const std::string& payloadObjectJson,
	const std::vector<std::string>& compatChannels)
{
	nlohmann::json payload;
	if (!payloadObjectJson.empty())
	{
		payload = nlohmann::json::parse(payloadObjectJson, nullptr, false);
	}

	if (payload.is_discarded() || !payload.is_object())
	{
		payload = nlohmann::json::object();
	}

	nlohmann::json envelope = {
		{ "channel", "blazeclaw.transport.event.v1" },
		{ "topic", TopicToString(topic) },
		{ "sessionId", m_sessionIdProvider ? m_sessionIdProvider() : std::string() },
		{ "payload", payload },
		{ "meta", {
			{ "seq", ++m_transportSeq },
			{ "source", "CBlazeClawMFCView" },
			{ "tsMs", static_cast<std::uint64_t>(GetTickCount64()) },
			{ "compat", compatChannels },
		} }
	};

	return envelope.dump();
}

void CEventTransport::EmitCompatibilityChannels(
	const std::vector<std::string>& channels,
	const std::string& payloadObjectJson)
{
	if (!m_emitJson || channels.empty())
	{
		return;
	}

	nlohmann::json payload;
	if (!payloadObjectJson.empty())
	{
		payload = nlohmann::json::parse(payloadObjectJson, nullptr, false);
	}

	if (payload.is_discarded() || !payload.is_object())
	{
		return;
	}

	for (const auto& channel : channels)
	{
		nlohmann::json out = payload;
		out["channel"] = channel;
		m_emitJson(out.dump());
	}
}
