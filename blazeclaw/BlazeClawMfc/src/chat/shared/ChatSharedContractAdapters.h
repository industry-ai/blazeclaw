#pragma once

#include "ChatSharedContracts.h"

#include <chrono>
#include <functional>
#include <mutex>
#include <unordered_set>

namespace blazeclaw::chat::shared {

	class LambdaChatRuntimeLifecycle final : public IChatRuntimeLifecycle {
	public:
		using StartFn = std::function<bool()>;
		using StopFn = std::function<void()>;
		using IsHealthyFn = std::function<bool()>;

		LambdaChatRuntimeLifecycle(
			StartFn start,
			StopFn stop,
			IsHealthyFn isHealthy)
			: m_start(std::move(start))
			, m_stop(std::move(stop))
			, m_isHealthy(std::move(isHealthy)) {
		}

		[[nodiscard]] bool Start() override {
			return static_cast<bool>(m_start) ? m_start() : false;
		}

		void Stop() override {
			if (static_cast<bool>(m_stop)) {
				m_stop();
			}
		}

		[[nodiscard]] bool IsHealthy() const override {
			return static_cast<bool>(m_isHealthy) ? m_isHealthy() : false;
		}

	private:
		StartFn m_start;
		StopFn m_stop;
		IsHealthyFn m_isHealthy;
	};

	class LambdaChatRequestOrchestrator final : public IChatRequestOrchestrator {
	public:
		using RouteFn = std::function<blazeclaw::gateway::protocol::ResponseFrame(
			const blazeclaw::gateway::protocol::RequestFrame& request)>;

		explicit LambdaChatRequestOrchestrator(RouteFn route)
			: m_route(std::move(route)) {
		}

		[[nodiscard]] blazeclaw::gateway::protocol::ResponseFrame Route(
			const blazeclaw::gateway::protocol::RequestFrame& request) const override {
			if (static_cast<bool>(m_route)) {
				return m_route(request);
			}

			return blazeclaw::gateway::protocol::ResponseFrame{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = blazeclaw::gateway::protocol::ErrorShape{
					.code = "route_unavailable",
					.message = "Shared chat orchestrator route is unavailable.",
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};
		}

	private:
		RouteFn m_route;
	};

	class PassthroughChatStreamEventNormalizer final : public IChatStreamEventNormalizer {
	public:
		[[nodiscard]] nlohmann::json Normalize(
			const nlohmann::json& eventPayload) const override {
			return eventPayload;
		}
	};

	struct StreamShapeConformanceResult {
		bool hasType = false;
		bool hasTimestamp = false;
		bool hasOptionalRequestId = false;
		bool hasOptionalRunId = false;
		bool hasOptionalState = false;
	};

	class ConformantChatStreamEventNormalizer final : public IChatStreamEventNormalizer {
	public:
		explicit ConformantChatStreamEventNormalizer(
			std::string fallbackType = "delta")
			: m_fallbackType(std::move(fallbackType)) {
		}

		[[nodiscard]] nlohmann::json Normalize(
			const nlohmann::json& eventPayload) const override {
			nlohmann::json normalized = eventPayload;
			if (!normalized.is_object()) {
				normalized = nlohmann::json::object();
			}

			if (!normalized.contains("type") || !normalized["type"].is_string()) {
				normalized["type"] = m_fallbackType;
			}

			if (!normalized.contains("timestamp") || !normalized["timestamp"].is_number_unsigned()) {
				normalized["timestamp"] = CurrentEpochMilliseconds();
			}

			return normalized;
		}

		[[nodiscard]] static StreamShapeConformanceResult Check(
			const nlohmann::json& payload) {
			StreamShapeConformanceResult result;
			if (!payload.is_object()) {
				return result;
			}

			result.hasType =
				payload.contains("type") &&
				payload["type"].is_string();
			result.hasTimestamp =
				payload.contains("timestamp") &&
				payload["timestamp"].is_number_unsigned();
			result.hasOptionalRequestId =
				payload.contains("requestId") &&
				payload["requestId"].is_string();
			result.hasOptionalRunId =
				payload.contains("runId") &&
				payload["runId"].is_string();
			result.hasOptionalState =
				payload.contains("state") &&
				payload["state"].is_string();
			return result;
		}

	private:
		[[nodiscard]] static std::uint64_t CurrentEpochMilliseconds() {
			using namespace std::chrono;
			return static_cast<std::uint64_t>(duration_cast<milliseconds>(
				system_clock::now().time_since_epoch()).count());
		}

		std::string m_fallbackType;
	};

	class InMemoryChatSessionStateStore final : public IChatSessionStateStore {
	public:
		void BeginRequest(const std::string& requestId) override {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_active.insert(requestId);
			m_cancelled.erase(requestId);
		}

		void CancelRequest(const std::string& requestId) override {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_cancelled.insert(requestId);
		}

		void CompleteRequest(const std::string& requestId) override {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_active.erase(requestId);
			m_cancelled.erase(requestId);
		}

		[[nodiscard]] bool IsRequestActive(const std::string& requestId) const override {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_active.find(requestId) != m_active.end();
		}

		[[nodiscard]] bool IsRequestCancelled(const std::string& requestId) const override {
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_cancelled.find(requestId) != m_cancelled.end();
		}

	private:
		mutable std::mutex m_mutex;
		std::unordered_set<std::string> m_active;
		std::unordered_set<std::string> m_cancelled;
	};

	class NullChatTelemetryHooks final : public IChatTelemetryHooks {
	public:
		void OnCounter(const std::string&, const std::uint64_t) override {
		}

		void OnEvent(const std::string&, const nlohmann::json&) override {
		}
	};

} // namespace blazeclaw::chat::shared
