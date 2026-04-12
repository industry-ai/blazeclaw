#pragma once

#include "../../config/ConfigLoader.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace blazeclaw::core {

	class GatewayManagedConfigReloader {
	public:
		struct Options {
			std::filesystem::path configPath = L"blazeclaw.conf";
			std::uint64_t pollIntervalMs = 1000;
		};

		struct Callbacks {
			std::function<void(const char*)> appendTrace;
			std::function<void(const std::wstring&)> onWarning;
			std::function<bool(
				const blazeclaw::config::AppConfig&,
				std::wstring&)> applyConfigDiff;
		};

		void Start(
			const blazeclaw::config::AppConfig& initialConfig,
			Options options,
			Callbacks callbacks);
		void Stop();
		void Pump();
		[[nodiscard]] bool IsRunning() const noexcept;
		[[nodiscard]] std::uint64_t ApplyCount() const noexcept;
		[[nodiscard]] std::uint64_t RejectCount() const noexcept;

	private:
		[[nodiscard]] static std::optional<std::uint64_t> ComputeFileContentHash(
			const std::filesystem::path& path);
		[[nodiscard]] bool ShouldPollNow() const;
		void EmitWarning(const std::wstring& message) const;
		void EmitTrace(const char* stage) const;

		blazeclaw::config::ConfigLoader m_loader;
		Options m_options;
		Callbacks m_callbacks;
		bool m_running = false;
		std::optional<std::uint64_t> m_lastContentHash;
		mutable std::uint64_t m_lastPollEpochMs = 0;
		std::uint64_t m_applyCount = 0;
		std::uint64_t m_rejectCount = 0;
	};

} // namespace blazeclaw::core
