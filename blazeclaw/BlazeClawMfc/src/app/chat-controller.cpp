#include "pch.h"
#include "chat-controller.h"
#include "../gateway/GatewayProtocolModels.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <memory>

#include <nlohmann/json.hpp>

namespace blazeclaw::app::chatcontroller {

	namespace {

		NativeChatControllerLifecycle& LifecycleInstance()
		{
			static NativeChatControllerLifecycle instance;
			return instance;
		}


		std::string TrimCopy(const std::string& value)
		{
			std::size_t start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0)
			{
				++start;
			}

			std::size_t end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0)
			{
				--end;
			}

			return value.substr(start, end - start);
		}

		std::string NormalizeVersionOrDefault(const std::string& value, const char* fallback)
		{
			const std::string normalized = TrimCopy(value);
			if (!normalized.empty())
			{
				return normalized;
			}

			return std::string(fallback != nullptr ? fallback : "");
		}

		nlohmann::json BuildLifecyclePayload(
			const NativeControllerLifecycleSnapshot& snapshot,
			const NativeChatControllerLifecycle& lifecycle,
			const char* operation)
		{
			nlohmann::json payload = {
				{"statePatch", {
					{"controllerLifecycle", {
						{"initialized", snapshot.initialized},
						{"lifecycleGeneration", snapshot.lifecycleGeneration},
						{"initializedAtMs", lifecycle.GetInitializedAtMs()},
						{"resetAtMs", lifecycle.GetResetAtMs()},
						{"sessionKey", snapshot.sessionKey},
						{"contractName", snapshot.contractName},
						{"contractVersion", snapshot.contractVersion},
						{"schemaName", snapshot.schemaName},
						{"schemaVersion", snapshot.schemaVersion},
					}},
				}},
				{"uiOps", nlohmann::json::array()},
				{"diagnostics", {
					{"counters", nlohmann::json::object()},
					{"events", nlohmann::json::array()},
				}},
				{"warnings", nlohmann::json::array()},
			};

			if (operation != nullptr && operation[0] != '\0')
			{
				payload["operation"] = operation;
			}

			return payload;
		}

		NativeControllerInitializeParams ParseInitializeParams(
			const blazeclaw::gateway::protocol::RequestFrame& request)
		{
			NativeControllerInitializeParams params;

			const auto parsed = nlohmann::json::parse(
				request.paramsJson.value_or("{}"),
				nullptr,
				false);
			if (parsed.is_discarded() || !parsed.is_object())
			{
				return params;
			}

			auto applyIfString = [&parsed](const char* key, std::string& target)
				{
					if (key == nullptr)
					{
						return;
					}
					const auto it = parsed.find(key);
					if (it == parsed.end() || !it->is_string())
					{
						return;
					}
					target = it->get<std::string>();
				};

			applyIfString("sessionKey", params.sessionKey);
			applyIfString("contractName", params.contractName);
			applyIfString("contractVersion", params.contractVersion);
			applyIfString("schemaName", params.schemaName);
			applyIfString("schemaVersion", params.schemaVersion);

			return params;
		}

	} // namespace

	NativeControllerBuildMarker CreateNativeControllerBuildMarker()
	{
		return NativeControllerBuildMarker{};
	}

	NativeChatControllerLifecycle::NativeChatControllerLifecycle(std::shared_ptr<const ITimeProvider> timeProvider)
		: m_timeProvider(std::move(timeProvider))
	{
		if (!m_timeProvider)
		{
			m_timeProvider = std::make_shared<SteadyTimeProvider>();
		}
	}

	void NativeChatControllerLifecycle::SetTimeProvider(std::shared_ptr<const ITimeProvider> timeProvider)
	{
		// Not synchronized; intended for test setup before concurrent use.
		if (timeProvider)
		{
			m_timeProvider = std::move(timeProvider);
		}
	}

	void NativeChatControllerLifecycle::Initialize(const NativeControllerInitializeParams& params)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_snapshot.initialized = true;
		m_snapshot.lifecycleGeneration += 1;
		m_snapshot.initializedAt = m_timeProvider->Now();
		m_snapshot.sessionKey = NormalizeSessionKey(params.sessionKey);
		m_snapshot.contractName = NormalizeVersionOrDefault(
			params.contractName,
			"blazeclaw.chat.controller.bridge");
		m_snapshot.contractVersion = NormalizeVersionOrDefault(
			params.contractVersion,
			"1.0.0");
		m_snapshot.schemaName = NormalizeVersionOrDefault(
			params.schemaName,
			"chat-controller-bridge-envelope");
		m_snapshot.schemaVersion = NormalizeVersionOrDefault(
			params.schemaVersion,
			"1.0.0");
	}

	NativeControllerLifecycleSnapshot NativeChatControllerLifecycle::GetSnapshot() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

	void NativeChatControllerLifecycle::Reset()
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_snapshot.initialized = false;
		m_snapshot.lifecycleGeneration += 1;
		m_snapshot.resetAt = m_timeProvider->Now();
		m_snapshot.initializedAt = std::chrono::steady_clock::time_point{};
		m_snapshot.sessionKey = "main";
		m_snapshot.contractName = "blazeclaw.chat.controller.bridge";
		m_snapshot.contractVersion = "1.0.0";
		m_snapshot.schemaName = "chat-controller-bridge-envelope";
		m_snapshot.schemaVersion = "1.0.0";
	}

	bool NativeChatControllerLifecycle::IsInitialized() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot.initialized;
	}

	std::string NativeChatControllerLifecycle::NormalizeSessionKey(const std::string& value)
	{
		const std::string trimmed = TrimCopy(value);
		if (!trimmed.empty())
		{
			return trimmed;
		}

		return "main";
	}

	uint64_t NativeChatControllerLifecycle::TimePointToMs(std::chrono::steady_clock::time_point tp)
	{
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count());
	}

	std::chrono::milliseconds NativeChatControllerLifecycle::TimePointToDuration(std::chrono::steady_clock::time_point tp)
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch());
	}

	uint64_t NativeChatControllerLifecycle::GetInitializedAtMs() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return TimePointToMs(m_snapshot.initializedAt);
	}

	uint64_t NativeChatControllerLifecycle::GetResetAtMs() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return TimePointToMs(m_snapshot.resetAt);
	}

	std::chrono::milliseconds NativeChatControllerLifecycle::GetInitializedAtDuration() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return TimePointToDuration(m_snapshot.initializedAt);
	}

	std::chrono::milliseconds NativeChatControllerLifecycle::GetResetAtDuration() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return TimePointToDuration(m_snapshot.resetAt);
	}

	uint64_t NativeChatControllerLifecycle::GetTimeSinceInitializedMs() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto tp = m_snapshot.initializedAt;
		if (tp == std::chrono::steady_clock::time_point{})
		{
			return 0;
		}
		const auto now = m_timeProvider->Now();
		if (now <= tp)
		{
			return 0;
		}
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now - tp).count());
	}

	uint64_t NativeChatControllerLifecycle::GetTimeSinceResetMs() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto tp = m_snapshot.resetAt;
		if (tp == std::chrono::steady_clock::time_point{})
		{
			return 0;
		}
		const auto now = m_timeProvider->Now();
		if (now <= tp)
		{
			return 0;
		}
		return static_cast<uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(now - tp).count());
	}

	bool IsNativeChatControllerBridgeMethod(const std::string& method)
	{
		return method == "chat.controller.initialize" ||
			method == "chat.controller.getStateSnapshot" ||
			method == "chat.controller.reset";
	}

	blazeclaw::gateway::protocol::ResponseFrame DispatchNativeChatControllerBridgeRequest(
		const blazeclaw::gateway::protocol::RequestFrame& request)
	{
		auto& lifecycle = LifecycleInstance();

		if (request.method == "chat.controller.initialize")
		{
			const NativeControllerInitializeParams params = ParseInitializeParams(request);
			lifecycle.Initialize(params);
			const auto snapshot = lifecycle.GetSnapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(snapshot, lifecycle, "initialize").dump());
		}

		if (request.method == "chat.controller.getStateSnapshot")
		{
			const auto snapshot = lifecycle.GetSnapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(snapshot, lifecycle, "snapshot").dump());
		}

		if (request.method == "chat.controller.reset")
		{
			lifecycle.Reset();
			const auto snapshot = lifecycle.GetSnapshot();
			return blazeclaw::gateway::protocol::OkResponse(
				request,
				BuildLifecyclePayload(snapshot, lifecycle, "reset").dump());
		}

		return blazeclaw::gateway::protocol::ErrorResponse(
			request,
			"method_not_supported",
			"native chat-controller bridge method is not supported");
	}

} // namespace blazeclaw::app::chatcontroller
