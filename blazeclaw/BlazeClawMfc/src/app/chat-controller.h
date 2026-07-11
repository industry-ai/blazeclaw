#pragma once

#include <cstdint>
#include <chrono>
#include <mutex>
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
	std::chrono::steady_clock::time_point initializedAtMs = std::chrono::steady_clock::time_point{};
	std::chrono::steady_clock::time_point resetAtMs = std::chrono::steady_clock::time_point{};
		std::string sessionKey = "main";
		std::string contractName = "blazeclaw.chat.controller.bridge";
		std::string contractVersion = "1.0.0";
		std::string schemaName = "chat-controller-bridge-envelope";
		std::string schemaVersion = "1.0.0";
	};

	class NativeChatControllerLifecycle final {
	public:
		void Initialize(const NativeControllerInitializeParams& params);
		NativeControllerLifecycleSnapshot GetSnapshot() const;
		void Reset();
		bool IsInitialized() const;

	private:
		static std::string NormalizeSessionKey(const std::string& value);

		mutable std::mutex m_mutex;
		NativeControllerLifecycleSnapshot m_snapshot;
	};

} // namespace blazeclaw::app::chatcontroller
