#pragma once

#include "../gateway/GatewayProtocolModels.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

struct CBridgePollCompletionPayload
{
	bool ok = false;
	std::optional<std::string> payloadJson;
};

class CBridge
{
public:
	struct Config
	{
		UINT_PTR lifecycleTimerId = 0;
		UINT pollCompletedMessageId = 0;
		std::uint32_t pollIntervalActiveMs = 300;
		std::uint32_t pollIntervalIdleMs = 1000;
		std::uint32_t pollIntervalFailureMs = 3000;
		std::uint32_t pollIntervalFailureMaxMs = 15000;
		std::uint32_t pollIntervalDisconnectedMs = 2000;
		std::uint64_t traceFlushIntervalMs = 1000;
		bool pushEnabled = true;
		bool pushFallbackPollEnabled = true;
		bool pushRecoveryPollEnabled = true;
		std::uint32_t pushIngestionMaxBatchEvents = 50;
		std::uint32_t pushUiThrottleMs = 120;
		std::size_t pushDedupeCapacity = 512;
	};

	struct Dependencies
	{
		std::function<bool()> isGatewayRunning;
		std::function<std::string()> activeProvider;
		std::function<std::string()> activeModel;
		std::function<std::string()> sessionIdProvider;
		std::function<HWND()> getTargetHwnd;
		std::function<blazeclaw::gateway::protocol::ResponseFrame(
			const blazeclaw::gateway::protocol::RequestFrame& request)> routeGatewayRequest;
		std::function<void(const wchar_t* stage)> appendChatStatusStage;
		std::function<void(const wchar_t* stage, const std::string& detail)> appendChatStatusDetail;
		std::function<void(
			const wchar_t* state,
			const wchar_t* reason,
			const std::string& provider,
			const std::string& model,
			const std::string& runtimeKind)> emitLifecycle;
		std::function<void(std::uint16_t code, const char* reason)> emitWsClose;
		std::function<void(
			const std::string& state,
			const std::string& reason,
			std::uint32_t failureCount,
			std::uint32_t nextPollMs,
			std::uint64_t sinceLastSuccessMs,
			std::uint64_t pollTotalCount,
			std::uint64_t pollEmptyCount,
			std::uint64_t pollTotalEvents,
			std::uint64_t pollP95EstimateMs)> emitPollHealth;
		std::function<void(
			const std::string& state,
			const std::string& reason,
			std::uint32_t reconnectCount,
			std::uint64_t pushLagMs,
			std::uint64_t droppedFrames)> emitPushHealth;
		std::function<void(const std::string& eventsRaw)> handleEventsBatch;
	};

	void Initialize(Dependencies deps, Config cfg);
	void ResetLifecycle();
	void OnTimerTick(UINT_PTR timerId);
	void PumpLifecycle();
	void StartEventsPollAsync();
	void HandlePollCompleted(
		bool ok,
		const std::optional<std::string>& payloadJson);
	void HandlePushConnected(const std::string& reason = "push-connected");
	void HandlePushDisconnected(const std::string& reason = "push-disconnected");
	void HandlePushChatEventFrame(
		const std::string& eventPayloadObjectJson,
		std::optional<std::uint64_t> frameSeq = std::nullopt);
	void ScheduleNextPoll(std::uint32_t intervalMs);
	const std::string& PollHealthState() const;
	std::uint32_t PollConsecutiveFailures() const;
	std::uint64_t PollLastSuccessTickMs() const;

	void IncrementReqCount();
	void IncrementResCount();
	void IncrementEventCount();
	std::uint64_t NextEventSeq();
	void FlushTraceIfNeeded(
		const std::function<void(
			std::uint64_t req,
			std::uint64_t res,
			std::uint64_t evt,
			std::uint64_t seq)>& onFlush);

private:
	std::uint32_t ComputeFailureBackoffMs(std::uint32_t failureCount) const;
	void EmitPollHealth(
		const wchar_t* state,
		const wchar_t* reason,
		std::uint32_t failureCount,
		std::uint32_t nextPollMs);
	void HandlePollResponse(
		bool ok,
		const std::optional<std::string>& payloadJson);
	void EmitPushHealth(const std::string& state, const std::string& reason);
	void RecordPushFingerprint(const std::string& fingerprint);
	bool IsPushFingerprintDuplicate(const std::string& fingerprint) const;
	void PrunePushDedupeIfNeeded();
	void HandleInboundEventsBatch(
		const std::string& eventsRaw,
		bool fromPush);

	Dependencies m_deps;
	Config m_cfg;

	bool m_initialized = false;
	bool m_pumpingGuard = false;
	bool m_lifecycleSent = false;
	bool m_lastConnected = false;
	std::string m_lastProvider;
	std::string m_lastModel;
	std::string m_lastRuntimeKind;

	bool m_pollInFlight = false;
	std::uint32_t m_pollIntervalMs = 1000;
	std::uint64_t m_nextPollTickMs = 0;
	std::uint32_t m_pollConsecutiveFailures = 0;
	std::uint64_t m_pollLastSuccessTickMs = 0;
	std::string m_pollHealthState = "unknown";
	std::uint64_t m_pollTotalCount = 0;
	std::uint64_t m_pollEmptyCount = 0;
	std::uint64_t m_pollTotalEvents = 0;
	std::uint64_t m_pollBatchCount = 0;
	std::uint64_t m_pollP95EstimateMs = 0;

	bool m_pushConnected = false;
	std::uint64_t m_pushLastEventTickMs = 0;
	std::uint64_t m_pushLastDispatchTickMs = 0;
	std::uint64_t m_pushDroppedFrameCount = 0;
	std::uint32_t m_pushReconnectCount = 0;
	std::optional<std::uint64_t> m_pushLastFrameSeq;
	bool m_pushRecoveryPollPending = false;
	std::deque<std::string> m_pushRecentFingerprints;
	std::unordered_set<std::string> m_pushFingerprintSet;

	std::unordered_set<std::string> m_reportedSkillPathRunIds;

	std::uint64_t m_traceReqCount = 0;
	std::uint64_t m_traceResCount = 0;
	std::uint64_t m_traceEventCount = 0;
	std::uint64_t m_eventSeq = 0;
	std::uint64_t m_traceLastFlushTickMs = 0;
};
