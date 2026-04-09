#include "pch.h"
#include "CBridge.h"

#include "../gateway/GatewayJsonUtils.h"

#include <thread>

void CBridge::Initialize(Dependencies deps, Config cfg)
{
	m_deps = std::move(deps);
	m_cfg = cfg;
	m_initialized = true;
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

			if (!::PostMessage(
				hwnd,
				completedMessage,
				reinterpret_cast<WPARAM>(payload),
				0))
			{
				delete payload;
			}
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
		m_deps.emitPollHealth(
			state != nullptr
			? std::string(std::wstring(state).begin(), std::wstring(state).end())
			: std::string("unknown"),
			reason != nullptr
			? std::string(std::wstring(reason).begin(), std::wstring(reason).end())
			: std::string(),
			failureCount,
			nextPollMs,
			sinceLastSuccessMs);
	}

	std::wstring stateW = state != nullptr ? state : L"unknown";
	m_pollHealthState = std::string(stateW.begin(), stateW.end());
}

void CBridge::HandlePollResponse(
	const bool ok,
	const std::optional<std::string>& payloadJson)
{
	if (!ok || !payloadJson.has_value())
	{
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

	if (blazeclaw::gateway::json::Trim(eventsRaw) == "[]")
	{
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

	if (m_deps.handleEventsBatch)
	{
		m_deps.handleEventsBatch(eventsRaw);
	}

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
