#pragma once

#include "TaskDeltaRepository.h"
#include "GatewayEventFanoutService.h"
#include "TransportRecipientRegistry.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <optional>
#include <mutex>
#include <unordered_map>
#include <deque>

namespace blazeclaw::cron {
	struct CronScheduleNotificationEvent;
	struct CronRealtimeEvent;
}

namespace blazeclaw::gateway {

	class GatewayHost;
	class GatewayWebSocketTransport;

	namespace test_hooks {
		class GatewayHostCronProductionAdapter;
		void WireCronProductionIntegrationForTest(GatewayHost& host);
	}

	/**
	 * Adapter for cron production integration and runtime execution.
	 * 
	 * Encapsulates cron-specific orchestration logic, runtime adapters,
	 * task ledger hooks, notification dispatchers, and broadcast mechanisms.
	 * 
	 * GatewayHost remains the composition root and lifecycle owner;
	 * this adapter receives injected callbacks for host state access.
	 * 
	 * This adapter uses type-erased callbacks to avoid circular dependency with GatewayHost.h.
	 */
	class GatewayHostCronProductionAdapter {
	public:
		/**
		 * Context struct providing host state accessors without granting
		 * composition-root mutation. All fields are injected by GatewayHost.
		 * Uses opaque void* pointers to avoid circular header dependency.
		 */
		struct AdapterContext {
			void* chatRuntimeCallbackPtr = nullptr;
			std::function<void(const std::string&, std::string&)> transportBroadcast;
			std::function<void(const std::string&, const std::string&)> emitTelemetry;
			const void* chatRunsByIdPtr = nullptr;
			const TransportRecipientRegistry* transportRecipientRegistry = nullptr;
			TaskDeltaRepository* taskDeltaRepository = nullptr;
			GatewayEventFanoutService* eventFanoutService = nullptr;
			std::uint64_t* chatPushEventSeq = nullptr;
			std::uint64_t* cronPushEventSeq = nullptr;
			bool transportRunning = false;
		};

		GatewayHostCronProductionAdapter() = default;

		/**
		 * Wire cron production integration with runtime adapters, task ledger hooks,
		 * schedule notification hooks, and realtime event hooks.
		 * 
		 * Idempotent: will not rewire if already wired.
		 * Emits telemetry: "gateway.cron.production_integration.wired".
		 */
		void WireIntegration(const AdapterContext& context);

		/**
		 * Execute cron job in main chat session runtime.
		 * Returns cron result JSON or nullopt if not handled.
		 */
		[[nodiscard]] std::optional<nlohmann::json> ExecuteMainSessionRuntime(
			const nlohmann::json& job,
			std::int64_t nowMs,
			const AdapterContext& context);

		/**
		 * Execute cron job in isolated chat session runtime.
		 * Returns cron result JSON or nullopt if not handled.
		 */
		[[nodiscard]] std::optional<nlohmann::json> ExecuteIsolatedSessionRuntime(
			const nlohmann::json& job,
			std::int64_t nowMs,
			const AdapterContext& context);

		/**
		 * Handle task ledger create running event.
		 */
		void HandleTaskLedgerCreateRunning(
			const nlohmann::json& payload,
			const AdapterContext& context);

		/**
		 * Handle task ledger complete event.
		 */
		void HandleTaskLedgerComplete(
			const nlohmann::json& payload,
			const AdapterContext& context);

		/**
		 * Handle task ledger fail event.
		 */
		void HandleTaskLedgerFail(
			const nlohmann::json& payload,
			const AdapterContext& context);

		/**
		 * Dispatch cron schedule auto-disable notification.
		 */
		void DispatchScheduleAutoDisableNotification(
			const cron::CronScheduleNotificationEvent& event,
			const AdapterContext& context);

		/**
		 * Dispatch cron failure alert notification.
		 */
		void DispatchFailureAlertNotification(
			const nlohmann::json& payload,
			const AdapterContext& context);

		/**
		 * Dispatch cron announce delivery notification.
		 */
		void DispatchAnnounceDeliveryNotification(
			const nlohmann::json& payload,
			const AdapterContext& context);

		/**
		 * Broadcast cron realtime event.
		 */
		void BroadcastRealtimeEvent(
			const cron::CronRealtimeEvent& event,
			const AdapterContext& context);

		/**
		 * Check if cron chat session is busy (has active non-cron runs).
		 */
		[[nodiscard]] bool IsChatSessionBusy(
			const std::string& sessionKey,
			const AdapterContext& context) const;

		/**
		 * Returns true if integration has been wired.
		 */
		[[nodiscard]] bool IsWired() const noexcept { return m_wired; }

	private:
		friend void test_hooks::WireCronProductionIntegrationForTest(GatewayHost& host);

		void UpsertTaskLedgerEntry(
			const nlohmann::json& payload,
			bool terminal,
			const AdapterContext& context);

		mutable std::mutex m_mutex;
		bool m_wired = false;
	};

} // namespace blazeclaw::gateway
