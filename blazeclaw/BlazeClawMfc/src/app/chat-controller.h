#pragma once

#include <cstdint>
#include <chrono>
#include <mutex>
#include <memory>
#include <string>

namespace blazeclaw::app::chatcontroller {

	struct NativeControllerBuildMarker {
		std::string name = "chat-controller";
	};

	NativeControllerBuildMarker CreateNativeControllerBuildMarker();

	struct NativeControllerInitializeParams {
		std::string sessionKey = "main";
		std::string contractName = "blazeclaw.chat.controller.bridge";
		std::string contractVersion = "1.0.0";
		std::string schemaName = "chat-controller-bridge-envelope";
		std::string schemaVersion = "1.0.0";
	};

	struct NativeControllerLifecycleSnapshot {
		bool initialized = false;
		std::uint64_t lifecycleGeneration = 0;
		std::chrono::steady_clock::time_point initializedAt = std::chrono::steady_clock::time_point{};
		std::chrono::steady_clock::time_point resetAt = std::chrono::steady_clock::time_point{};
		std::string sessionKey = "main";
		std::string contractName = "blazeclaw.chat.controller.bridge";
		std::string contractVersion = "1.0.0";
		std::string schemaName = "chat-controller-bridge-envelope";
		std::string schemaVersion = "1.0.0";
	};

	class NativeChatControllerLifecycle final {
	public:
		// Time provider interface for testability. Unit tests can inject a provider that
		// returns controlled time points from Now(). Production code uses SteadyTimeProvider.
		struct ITimeProvider {
			virtual ~ITimeProvider() = default;
			virtual std::chrono::steady_clock::time_point Now() const = 0;
		};

		struct SteadyTimeProvider : ITimeProvider {
			std::chrono::steady_clock::time_point Now() const override
			{
				return std::chrono::steady_clock::now();
			}
		};

		// Construct with optional injectable time provider. If null, a SteadyTimeProvider is used.
		explicit NativeChatControllerLifecycle(std::shared_ptr<const ITimeProvider> timeProvider = nullptr);

		void Initialize(const NativeControllerInitializeParams& params);
		NativeControllerLifecycleSnapshot GetSnapshot() const;
		void Reset();
		bool IsInitialized() const;

		// Replace the time provider at runtime. Thread-unsafe; call during setup in tests.
		void SetTimeProvider(std::shared_ptr<const ITimeProvider> timeProvider);

	private:
		static std::string NormalizeSessionKey(const std::string& value);

		mutable std::mutex m_mutex;
		NativeControllerLifecycleSnapshot m_snapshot;

		// Time provider used to obtain Now() time points. Defaults to SteadyTimeProvider.
		std::shared_ptr<const ITimeProvider> m_timeProvider;
	};

} // namespace blazeclaw::app::chatcontroller
