#include "pch.h"
#include "GatewayManagedConfigReloader.h"

#include <chrono>
#include <fstream>

namespace blazeclaw::core {

	namespace {

		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		std::wstring ToWide(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			std::wstring output;
			output.reserve(value.size());
			for (const char ch : value) {
				output.push_back(static_cast<wchar_t>(
					static_cast<unsigned char>(ch)));
			}

			return output;
		}

	} // namespace

	void GatewayManagedConfigReloader::Start(
		const blazeclaw::config::AppConfig& initialConfig,
		Options options,
		Callbacks callbacks) {
		UNREFERENCED_PARAMETER(initialConfig);

		m_options = std::move(options);
		m_callbacks = std::move(callbacks);
		m_running = true;
		m_applyCount = 0;
		m_rejectCount = 0;
		m_lastPollEpochMs = 0;
		m_pendingInternalWriteHash =
			m_options.initialInternalWriteHash;
		m_lastContentHash = ComputeFileContentHash(m_options.configPath);
		EmitTrace("GatewayManagedConfigReloader.Start.ready");
	}

	void GatewayManagedConfigReloader::Stop() {
		if (!m_running) {
			return;
		}

		EmitTrace("GatewayManagedConfigReloader.Stop.begin");
		m_running = false;
		m_callbacks = {};
		m_lastContentHash.reset();
		m_pendingInternalWriteHash.reset();
		m_lastPollEpochMs = 0;
		EmitTrace("GatewayManagedConfigReloader.Stop.done");
	}

	void GatewayManagedConfigReloader::RegisterInternalWriteHash(
		std::uint64_t hash) {
		m_pendingInternalWriteHash = hash;
	}

	void GatewayManagedConfigReloader::Pump() {
		if (!m_running || !ShouldPollNow()) {
			return;
		}

		if (m_options.pollInternalWriteHash) {
			if (const auto pendingHash = m_options.pollInternalWriteHash();
				pendingHash.has_value()) {
				m_pendingInternalWriteHash = pendingHash;
				EmitTrace("GatewayManagedConfigReloader.Pump.internal_write_event");
			}
		}

		const auto currentHash = ComputeFileContentHash(m_options.configPath);
		if (!currentHash.has_value()) {
			return;
		}

		if (m_lastContentHash.has_value() &&
			m_lastContentHash.value() == currentHash.value()) {
			return;
		}

		if (m_pendingInternalWriteHash.has_value() &&
			m_pendingInternalWriteHash.value() == currentHash.value()) {
			m_lastContentHash = currentHash;
			m_pendingInternalWriteHash.reset();
			EmitTrace("GatewayManagedConfigReloader.Pump.skipped_internal_write");
			return;
		}

		blazeclaw::config::AppConfig nextConfig;
		if (!m_loader.LoadFromFile(m_options.configPath.wstring(), nextConfig)) {
			++m_rejectCount;
			EmitWarning(
				L"managed config reload skipped because config parse failed.");
			m_lastContentHash = currentHash;
			return;
		}

		std::wstring warningMessage;
		const bool applied =
			m_callbacks.applyConfigDiff &&
			m_callbacks.applyConfigDiff(nextConfig, warningMessage);
		if (!applied) {
			++m_rejectCount;
			if (warningMessage.empty()) {
				warningMessage =
					L"managed config reload diff was rejected by runtime policy.";
			}
			EmitWarning(warningMessage);
			m_lastContentHash = currentHash;
			return;
		}

		++m_applyCount;
		m_lastContentHash = currentHash;
		EmitTrace("GatewayManagedConfigReloader.Pump.applied");
	}

	bool GatewayManagedConfigReloader::IsRunning() const noexcept {
		return m_running;
	}

	std::uint64_t GatewayManagedConfigReloader::ApplyCount() const noexcept {
		return m_applyCount;
	}

	std::uint64_t GatewayManagedConfigReloader::RejectCount() const noexcept {
		return m_rejectCount;
	}

	std::optional<std::uint64_t>
		GatewayManagedConfigReloader::ComputeFileContentHash(
			const std::filesystem::path& path) {
		std::ifstream input(path, std::ios::in | std::ios::binary);
		if (!input.is_open()) {
			return std::nullopt;
		}

		std::string content(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		return std::hash<std::string>{}(content);
	}

	bool GatewayManagedConfigReloader::ShouldPollNow() const {
		const std::uint64_t now = CurrentEpochMs();
		if (m_lastPollEpochMs == 0 ||
			now - m_lastPollEpochMs >= m_options.pollIntervalMs) {
			m_lastPollEpochMs = now;
			return true;
		}

		return false;
	}

	void GatewayManagedConfigReloader::EmitWarning(
		const std::wstring& message) const {
		if (m_callbacks.onWarning) {
			m_callbacks.onWarning(message);
		}
	}

	void GatewayManagedConfigReloader::EmitTrace(const char* stage) const {
		if (m_callbacks.appendTrace) {
			m_callbacks.appendTrace(stage);
		}
	}

} // namespace blazeclaw::core
