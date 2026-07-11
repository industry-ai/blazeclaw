#include "pch.h"
#include "chat-controller.h"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace blazeclaw::app::chatcontroller {

	namespace {


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

	} // namespace

	NativeControllerBuildMarker CreateNativeControllerBuildMarker()
	{
		return NativeControllerBuildMarker{};
	}

	void NativeChatControllerLifecycle::Initialize(const NativeControllerInitializeParams& params)
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		m_snapshot.initialized = true;
		m_snapshot.lifecycleGeneration += 1;
		m_snapshot.initializedAtMs = std::chrono::steady_clock::now();
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
		m_snapshot.resetAtMs = std::chrono::steady_clock::now();
		m_snapshot.initializedAtMs = std::chrono::steady_clock::time_point{};
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

} // namespace blazeclaw::app::chatcontroller
