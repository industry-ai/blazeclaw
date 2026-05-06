#include "pch.h"
#include "CBridge.h"
#include "CMgrMessage.h"

#include "../gateway/GatewayJsonUtils.h"

#include <algorithm>
#include <thread>
#include <utility>
#include <nlohmann/json.hpp>

namespace {

	std::string ToUtf8Bridge(const std::wstring& value)
	{
		if (value.empty())
		{
			return {};
		}

		const int sizeNeeded = WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (sizeNeeded <= 0)
		{
			std::string fallback;
			fallback.reserve(value.size());
			for (const wchar_t ch : value)
			{
				fallback.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			return fallback;
		}

		std::string output(sizeNeeded, '\0');
		const int written = WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			output.data(),
			sizeNeeded,
			nullptr,
			nullptr);
		if (written <= 0)
		{
			return {};
		}

		return output;
	}

	std::size_t CountTopLevelArrayItemsSafe(const std::string& arrayJson)
	{
		const auto parsed = nlohmann::json::parse(arrayJson, nullptr, false);
		if (parsed.is_discarded() || !parsed.is_array())
		{
			return 0;
		}
		return parsed.size();
	}

} // namespace

void CBridge::Initialize(Dependencies deps, Config cfg)
{
	m_deps = std::move(deps);
	m_cfg = cfg;
	m_initialized = true;
}

void CBridge::ResetLifecycle()
{
	m_lifecycleSent = false;
}

void CBridge::OnTimerTick(const UINT_PTR timerId)
{
	if (!m_initialized)
	{
		return;
	}

	if (m_cfg.lifecycleTimerId == 0 || timerId != m_cfg.lifecycleTimerId)
	{
		return;
	}

	PumpLifecycle();
}

void CBridge::PumpLifecycle()
{
	if (!m_initialized || m_pumpingGuard)
	{
		return;
	}

	m_pumpingGuard = true;
	struct Guard final
	{
		bool* flag = nullptr;
		~Guard()
		{
			if (flag != nullptr)
			{
				*flag = false;
			}
		}
	} guard{ &m_pumpingGuard };

	const bool connected = m_deps.isGatewayRunning && m_deps.isGatewayRunning();
	const std::string provider = connected && m_deps.activeProvider
		? m_deps.activeProvider()
		: std::string();
	const std::string model = connected && m_deps.activeModel
		? m_deps.activeModel()
		: std::string();
	const std::string runtimeKind = provider == "deepseek"
		? "remote"
		: (provider.empty() ? std::string() : "local");

	if (!m_lifecycleSent)
	{
		if (m_deps.appendChatStatusStage)
		{
			m_deps.appendChatStatusStage(
				connected ? L"lifecycle.connected" : L"lifecycle.disconnected");
		}
		if (m_deps.emitLifecycle)
		{
			m_deps.emitLifecycle(
				connected ? L"connected" : L"disconnected",
				connected ? L"service-ready" : L"service-not-running",
				provider,
				model,
				runtimeKind);
		}
		m_lifecycleSent = true;
		m_lastConnected = connected;
		m_lastProvider = provider;
		m_lastModel = model;
		m_lastRuntimeKind = runtimeKind;
	}
	else if (connected != m_lastConnected)
	{
		if (connected)
		{
			if (m_deps.appendChatStatusStage)
			{
				m_deps.appendChatStatusStage(L"lifecycle.reconnected");
			}
			if (m_deps.emitLifecycle)
			{
				m_deps.emitLifecycle(
					L"reconnected",
					L"service-ready",
					provider,
					model,
					runtimeKind);
			}
		}
		else
		{
			if (m_deps.appendChatStatusStage)
			{
				m_deps.appendChatStatusStage(L"lifecycle.service-stopped");
			}
			if (m_deps.emitLifecycle)
			{
				m_deps.emitLifecycle(
					L"disconnected",
					L"service-stopped",
					std::string(),
					std::string(),
					std::string());
			}
			if (m_deps.emitWsClose)
			{
				m_deps.emitWsClose(1001, "gateway disconnected");
			}
		}

		m_lastConnected = connected;
		m_lastProvider = connected ? provider : std::string();
		m_lastModel = connected ? model : std::string();
		m_lastRuntimeKind = connected ? runtimeKind : std::string();
	}

	if (connected &&
		m_lifecycleSent &&
		(provider != m_lastProvider ||
			model != m_lastModel ||
			runtimeKind != m_lastRuntimeKind))
	{
		if (m_deps.appendChatStatusStage)
		{
			m_deps.appendChatStatusStage(L"lifecycle.runtime-updated");
		}
		if (m_deps.emitLifecycle)
		{
			m_deps.emitLifecycle(
				L"connected",
				L"runtime-updated",
				provider,
				model,
				runtimeKind);
		}
		m_lastProvider = provider;
		m_lastModel = model;
		m_lastRuntimeKind = runtimeKind;
	}

	if (!connected)
	{
		if (m_pollHealthState != "disconnected")
		{
			EmitPollHealth(
				L"disconnected",
				L"service-not-running",
				m_pollConsecutiveFailures,
				m_cfg.pollIntervalDisconnectedMs);
		}
		ScheduleNextPoll(m_cfg.pollIntervalDisconnectedMs);
		return;
	}

	if (m_cfg.pushEnabled &&
		m_pushConnected &&
		!m_cfg.pushFallbackPollEnabled &&
		!m_pushRecoveryPollPending)
	{
		// Push-primary mode: do not keep timer-polling when fallback polling is disabled.
		return;
	}

	const std::uint64_t nowMs = GetTickCount64();
	if (m_nextPollTickMs == 0)
	{
		m_nextPollTickMs = nowMs;
	}

	if (m_pollInFlight || nowMs < m_nextPollTickMs)
	{
		return;
	}

	StartEventsPollAsync();
}

void CBridge::StartEventsPollAsync()
{
	if (!m_initialized || m_pollInFlight)
	{
		return;
	}

	if (!m_deps.isGatewayRunning || !m_deps.isGatewayRunning())
	{
		ScheduleNextPoll(m_cfg.pollIntervalDisconnectedMs);
		return;
	}

	if (!m_deps.getTargetHwnd)
	{
		ScheduleNextPoll(m_cfg.pollIntervalIdleMs);
		return;
	}

	const HWND hwnd = m_deps.getTargetHwnd();
	if (hwnd == nullptr)
	{
		ScheduleNextPoll(m_cfg.pollIntervalIdleMs);
		return;
	}

	m_pollInFlight = true;
	const std::string sessionId = m_deps.sessionIdProvider
		? m_deps.sessionIdProvider()
		: std::string("main");

	auto route = m_deps.routeGatewayRequest;
	const UINT completedMessage = m_cfg.pollCompletedMessageId;
	std::thread(
		[hwnd, sessionId, route, completedMessage]()
		{
			if (!route)
			{
				return;
			}

			const blazeclaw::gateway::protocol::RequestFrame pollRequest{
				.id = "bridge-chat-events",
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"") +
					sessionId +
					"\",\"limit\":20}",
			};

			const auto pollResponse = route(pollRequest);
			auto* payload = new CBridgePollCompletionPayload{
				.ok = pollResponse.ok,
				.payloadJson = pollResponse.payloadJson,
			};

			CMgrMessage::Instance().PostOwnedPayloadToHwnd(
				hwnd,
				completedMessage,
				payload,
				true,
				[](void* raw)
				{
					delete static_cast<CBridgePollCompletionPayload*>(raw);
				});
		})
		.detach();
}

void CBridge::HandlePollCompleted(
	const bool ok,
	const std::optional<std::string>& payloadJson)
{
	m_pollInFlight = false;
	HandlePollResponse(ok, payloadJson);
}

void CBridge::ScheduleNextPoll(const std::uint32_t intervalMs)
{
	m_pollIntervalMs = intervalMs;
	m_nextPollTickMs = GetTickCount64() + intervalMs;
}

const std::string& CBridge::PollHealthState() const
{
	return m_pollHealthState;
}

std::uint32_t CBridge::PollConsecutiveFailures() const
{
	return m_pollConsecutiveFailures;
}

std::uint64_t CBridge::PollLastSuccessTickMs() const
{
	return m_pollLastSuccessTickMs;
}

void CBridge::IncrementReqCount()
{
	++m_traceReqCount;
}

void CBridge::IncrementResCount()
{
	++m_traceResCount;
}

void CBridge::IncrementEventCount()
{
	++m_traceEventCount;
}

std::uint64_t CBridge::NextEventSeq()
{
	return ++m_eventSeq;
}

void CBridge::FlushTraceIfNeeded(
	const std::function<void(
		std::uint64_t req,
		std::uint64_t res,
		std::uint64_t evt,
		std::uint64_t seq)>& onFlush)
{
	const std::uint64_t nowMs = GetTickCount64();
	if (m_traceLastFlushTickMs != 0 &&
		(nowMs - m_traceLastFlushTickMs) < m_cfg.traceFlushIntervalMs)
	{
		return;
	}

	m_traceLastFlushTickMs = nowMs;
	if (onFlush)
	{
		onFlush(m_traceReqCount, m_traceResCount, m_traceEventCount, m_eventSeq);
	}
}

std::uint32_t CBridge::ComputeFailureBackoffMs(const std::uint32_t failureCount) const
{
	if (failureCount <= 1)
	{
		return m_cfg.pollIntervalFailureMs;
	}

	std::uint32_t backoffMs = m_cfg.pollIntervalFailureMs;
	for (std::uint32_t i = 1; i < failureCount; ++i)
	{
		if (backoffMs >= m_cfg.pollIntervalFailureMaxMs)
		{
			return m_cfg.pollIntervalFailureMaxMs;
		}

		const std::uint32_t doubled = backoffMs * 2;
		if (doubled < backoffMs || doubled > m_cfg.pollIntervalFailureMaxMs)
		{
			return m_cfg.pollIntervalFailureMaxMs;
		}

		backoffMs = doubled;
	}

	return backoffMs;
}

void CBridge::EmitPollHealth(
	const wchar_t* state,
	const wchar_t* reason,
	const std::uint32_t failureCount,
	const std::uint32_t nextPollMs)
{
	std::uint64_t sinceLastSuccessMs = 0;
	if (m_pollLastSuccessTickMs != 0)
	{
		sinceLastSuccessMs = GetTickCount64() - m_pollLastSuccessTickMs;
	}

	if (m_deps.emitPollHealth)
	{
		const std::wstring stateW = state != nullptr ? state : L"unknown";
		const std::wstring reasonW = reason != nullptr ? reason : L"";
		m_deps.emitPollHealth(
			ToUtf8Bridge(stateW),
			ToUtf8Bridge(reasonW),
			failureCount,
			nextPollMs,
			sinceLastSuccessMs,
			m_pollTotalCount,
			m_pollEmptyCount,
			m_pollTotalEvents,
			m_pollP95EstimateMs);
	}

	std::wstring stateW = state != nullptr ? state : L"unknown";
	m_pollHealthState = ToUtf8Bridge(stateW);
}

void CBridge::HandlePollResponse(
	const bool ok,
	const std::optional<std::string>& payloadJson)
{
	if (!ok || !payloadJson.has_value())
	{
		++m_pollTotalCount;
		++m_pollConsecutiveFailures;
		const std::uint32_t backoffMs =
			ComputeFailureBackoffMs(m_pollConsecutiveFailures);
		if (m_deps.appendChatStatusDetail)
		{
			m_deps.appendChatStatusDetail(
				L"events.poll.failed",
				"failures=" + std::to_string(m_pollConsecutiveFailures) +
				" nextMs=" + std::to_string(backoffMs));
		}
		EmitPollHealth(
			L"degraded",
			L"poll-failed",
			m_pollConsecutiveFailures,
			backoffMs);
		ScheduleNextPoll(backoffMs);
		return;
	}

	std::string eventsRaw;
	if (!blazeclaw::gateway::json::FindRawField(
		payloadJson.value(),
		"events",
		eventsRaw))
	{
		++m_pollTotalCount;
		++m_pollConsecutiveFailures;
		const std::uint32_t backoffMs =
			ComputeFailureBackoffMs(m_pollConsecutiveFailures);
		if (m_deps.appendChatStatusDetail)
		{
			m_deps.appendChatStatusDetail(
				L"events.poll.invalid",
				"missing events field; failures=" +
				std::to_string(m_pollConsecutiveFailures));
		}
		EmitPollHealth(
			L"degraded",
			L"payload-missing-events",
			m_pollConsecutiveFailures,
			backoffMs);
		ScheduleNextPoll(backoffMs);
		return;
	}

	const bool wasUnhealthy =
		m_pollConsecutiveFailures > 0 ||
		m_pollHealthState == "degraded" ||
		m_pollHealthState == "disconnected";

	m_pollConsecutiveFailures = 0;
	m_pollLastSuccessTickMs = GetTickCount64();
	++m_pollTotalCount;
	if (m_pushRecoveryPollPending)
	{
		m_pushRecoveryPollPending = false;
	}

	if (blazeclaw::gateway::json::Trim(eventsRaw) == "[]")
	{
		++m_pollEmptyCount;
		if (wasUnhealthy)
		{
			EmitPollHealth(
				L"healthy",
				L"poll-recovered",
				0,
				m_cfg.pollIntervalIdleMs);
		}
		ScheduleNextPoll(m_cfg.pollIntervalIdleMs);
		return;
	}
	HandleInboundEventsBatch(eventsRaw, false);

	if (wasUnhealthy)
	{
		EmitPollHealth(
			L"healthy",
			L"poll-recovered",
			0,
			m_cfg.pollIntervalActiveMs);
	}

	ScheduleNextPoll(m_cfg.pollIntervalActiveMs);
}

void CBridge::HandlePushConnected(const std::string& reason)
{
	const bool wasConnected = m_pushConnected;
	m_pushConnected = true;
	if (!wasConnected)
	{
		++m_pushReconnectCount;
		if (m_cfg.pushRecoveryPollEnabled)
		{
			m_pushRecoveryPollPending = true;
			ScheduleNextPoll(m_cfg.pollIntervalActiveMs);
		}
	}
	EmitPushHealth("connected", reason);
}

void CBridge::HandlePushDisconnected(const std::string& reason)
{
	m_pushConnected = false;
	EmitPushHealth("disconnected", reason);
	if (m_cfg.pushEnabled && m_cfg.pushFallbackPollEnabled)
	{
		ScheduleNextPoll(m_cfg.pollIntervalActiveMs);
	}
}

void CBridge::HandlePushChatEventFrame(
	const std::string& eventPayloadObjectJson,
	const std::optional<std::uint64_t> frameSeq)
{
	if (!m_cfg.pushEnabled || eventPayloadObjectJson.empty())
	{
		return;
	}

	if (frameSeq.has_value())
	{
		if (m_pushLastFrameSeq.has_value() &&
			frameSeq.value() <= m_pushLastFrameSeq.value())
		{
			++m_pushDroppedFrameCount;
			EmitPushHealth("degraded", "duplicate-seq");
			return;
		}
		m_pushLastFrameSeq = frameSeq.value();
	}

	const std::string fingerprint =
		std::to_string(frameSeq.value_or(0)) +
		":" +
		blazeclaw::gateway::json::Trim(eventPayloadObjectJson);
	if (IsPushFingerprintDuplicate(fingerprint))
	{
		++m_pushDroppedFrameCount;
		EmitPushHealth("degraded", "duplicate-event");
		return;
	}
	RecordPushFingerprint(fingerprint);

	m_pushLastEventTickMs = GetTickCount64();
	const std::string wrappedEvents = "[" + eventPayloadObjectJson + "]";
	HandleInboundEventsBatch(wrappedEvents, true);
	EmitPushHealth("healthy", "push-event");
}

void CBridge::EmitPushHealth(const std::string& state, const std::string& reason)
{
	if (!m_deps.emitPushHealth)
	{
		return;
	}
	const std::uint64_t nowMs = GetTickCount64();
	const std::uint64_t lagMs = m_pushLastEventTickMs > 0
		? (nowMs - m_pushLastEventTickMs)
		: 0;
	m_deps.emitPushHealth(
		state,
		reason,
		m_pushReconnectCount,
		lagMs,
		m_pushDroppedFrameCount);
}

void CBridge::RecordPushFingerprint(const std::string& fingerprint)
{
	if (fingerprint.empty())
	{
		return;
	}
	m_pushRecentFingerprints.push_back(fingerprint);
	m_pushFingerprintSet.insert(fingerprint);
	PrunePushDedupeIfNeeded();
}

bool CBridge::IsPushFingerprintDuplicate(const std::string& fingerprint) const
{
	return !fingerprint.empty() &&
		m_pushFingerprintSet.find(fingerprint) != m_pushFingerprintSet.end();
}

void CBridge::PrunePushDedupeIfNeeded()
{
	const std::size_t cap = (std::max)(static_cast<std::size_t>(32), m_cfg.pushDedupeCapacity);
	while (m_pushRecentFingerprints.size() > cap)
	{
		const std::string oldest = std::move(m_pushRecentFingerprints.front());
		m_pushRecentFingerprints.pop_front();
		m_pushFingerprintSet.erase(oldest);
	}
}

void CBridge::HandleInboundEventsBatch(
	const std::string& eventsRaw,
	const bool fromPush)
{
	if (eventsRaw.empty())
	{
		return;
	}

	const std::size_t itemCount = CountTopLevelArrayItemsSafe(eventsRaw);
	if (itemCount == 0)
	{
		return;
	}

	if (fromPush && itemCount > m_cfg.pushIngestionMaxBatchEvents)
	{
		++m_pushDroppedFrameCount;
		EmitPushHealth("degraded", "push-batch-clamped");
		return;
	}

	if (m_deps.handleEventsBatch)
	{
		const std::uint64_t nowMs = GetTickCount64();
		if (fromPush &&
			m_pushLastDispatchTickMs > 0 &&
			(nowMs - m_pushLastDispatchTickMs) < m_cfg.pushUiThrottleMs)
		{
			++m_pushDroppedFrameCount;
			EmitPushHealth("degraded", "push-ui-throttled");
			return;
		}

		m_deps.handleEventsBatch(eventsRaw);
		m_pushLastDispatchTickMs = nowMs;
	}

	m_pollTotalEvents += itemCount;
	++m_pollBatchCount;
	const std::uint64_t avgBatch = m_pollBatchCount > 0
		? (m_pollTotalEvents / m_pollBatchCount)
		: 0;
	m_pollP95EstimateMs = avgBatch * 15;

	if (fromPush && m_pushRecoveryPollPending)
	{
		m_pushRecoveryPollPending = false;
	}
}
