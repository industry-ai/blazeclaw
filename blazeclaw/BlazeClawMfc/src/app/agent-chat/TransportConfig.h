#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "../NetworkTimeouts.h"

namespace blazeclaw::irc {

// Where consumer callbacks (push + connection-state) are invoked.
enum class CallbackDispatchMode {
	// Dedicated PushDispatcher worker thread (default production path).
	DispatcherWorker = 0,
	// Invoke synchronously on the posting/calling thread (unit tests).
	CallerThread = 1,
	// Post via an injected executor (e.g. MFC UI/main thread PostMessage).
	ExternalExecutor = 2,
};

using CallbackExecutor = std::function<void(std::function<void()>)>;

// Injected transport behavior knobs. Defaults match NetworkTimeouts.h.
struct TransportConfig {
	std::chrono::milliseconds initialReconnectBackoff{
		blazeclaw::net::kInitialReconnectBackoff};
	std::chrono::milliseconds maxReconnectBackoff{
		blazeclaw::net::kMaxReconnectBackoff};
	std::chrono::milliseconds heartbeatInterval{
		std::chrono::milliseconds{blazeclaw::net::kHeartbeatIntervalMs}};
	std::chrono::milliseconds heartbeatStep{
		std::chrono::milliseconds{blazeclaw::net::kHeartbeatStepMs}};
	CallbackDispatchMode callbackDispatchMode{
		CallbackDispatchMode::DispatcherWorker};

	static CallbackDispatchMode ParseDispatchMode(const std::wstring& raw) {
		if (raw == L"caller" || raw == L"caller_thread" || raw == L"sync") {
			return CallbackDispatchMode::CallerThread;
		}
		if (raw == L"external" || raw == L"ui" || raw == L"executor") {
			return CallbackDispatchMode::ExternalExecutor;
		}
		return CallbackDispatchMode::DispatcherWorker;
	}

	static const char* DispatchModeToString(CallbackDispatchMode mode) {
		switch (mode) {
		case CallbackDispatchMode::CallerThread:
			return "caller";
		case CallbackDispatchMode::ExternalExecutor:
			return "external";
		case CallbackDispatchMode::DispatcherWorker:
		default:
			return "dispatcher";
		}
	}
};

} // namespace blazeclaw::irc
