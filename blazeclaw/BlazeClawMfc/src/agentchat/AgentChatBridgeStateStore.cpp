#include "pch.h"
#include "AgentChatBridgeStateStore.h"

#include "../gateway/GatewayPersistencePaths.h"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

namespace blazeclaw::agentchat {

	AgentChatBridgeStateStore::AgentChatBridgeStateStore(
		std::filesystem::path stateRoot)
		: m_stateRoot(std::move(stateRoot)) {
		if (m_stateRoot.empty()) {
			m_stateRoot = ResolveDefaultStateRoot();
		}
		m_pushIdempotencyPath = m_stateRoot / "cache" / "push.idempotency.json";
	}

	std::filesystem::path AgentChatBridgeStateStore::ResolveDefaultStateRoot() {
		return blazeclaw::gateway::ResolveGatewayStateDirectory() /
			"agent-chat" /
			"bridge";
	}

	void AgentChatBridgeStateStore::EnsureInitialized() const {
		std::error_code ec;
		std::filesystem::create_directories(m_stateRoot / "identity", ec);
		std::filesystem::create_directories(m_stateRoot / "cache", ec);
		std::filesystem::create_directories(m_stateRoot / "sessions", ec);
	}

	const std::filesystem::path& AgentChatBridgeStateStore::PushIdempotencyStorePath() const noexcept {
		return m_pushIdempotencyPath;
	}

	std::string AgentChatBridgeStateStore::NormalizeIdempotencyKey(
		const std::string& value) {
		const auto begin = std::find_if_not(
			value.begin(),
			value.end(),
			[](unsigned char ch) {
				return std::isspace(ch) != 0;
			});
		const auto end = std::find_if_not(
			value.rbegin(),
			value.rend(),
			[](unsigned char ch) {
				return std::isspace(ch) != 0;
			}).base();
		if (begin >= end) {
			return {};
		}
		return std::string(begin, end);
	}

	void AgentChatBridgeStateStore::EnsurePushIdempotencyLoadedLocked() {
		if (m_pushIdempotencyLoaded) {
			return;
		}
		m_pushIdempotencyLoaded = true;
		m_pushIdempotencyByKey.clear();

		std::error_code existsError;
		if (!std::filesystem::exists(m_pushIdempotencyPath, existsError)) {
			return;
		}

		std::ifstream input(m_pushIdempotencyPath, std::ios::binary);
		if (!input.is_open()) {
			return;
		}

		std::string raw(
			(std::istreambuf_iterator<char>(input)),
			std::istreambuf_iterator<char>());
		const auto parsed = nlohmann::json::parse(raw, nullptr, false);
		if (parsed.is_discarded() || !parsed.is_object()) {
			return;
		}

		const auto entriesIt = parsed.find("entries");
		if (entriesIt == parsed.end() || !entriesIt->is_array()) {
			return;
		}

		for (const auto& entry : *entriesIt) {
			if (!entry.is_object()) {
				continue;
			}
			const std::string key = NormalizeIdempotencyKey(entry.value("key", std::string()));
			if (key.empty()) {
				continue;
			}
			const std::uint64_t seenAtMs = entry.value("seenAtMs", static_cast<std::uint64_t>(0));
			if (seenAtMs == 0) {
				continue;
			}
			m_pushIdempotencyByKey.insert_or_assign(key, seenAtMs);
		}
	}

	void AgentChatBridgeStateStore::PrunePushIdempotencyLocked(
		const std::uint64_t nowMs,
		const std::uint64_t ttlMs,
		const std::size_t maxEntries) {
		for (auto it = m_pushIdempotencyByKey.begin(); it != m_pushIdempotencyByKey.end();) {
			const std::uint64_t seenAtMs = it->second;
			if (seenAtMs > nowMs || nowMs - seenAtMs > ttlMs) {
				it = m_pushIdempotencyByKey.erase(it);
				continue;
			}
			++it;
		}

		if (m_pushIdempotencyByKey.size() <= maxEntries) {
			return;
		}

		std::vector<PushIdempotencyEntry> ordered;
		ordered.reserve(m_pushIdempotencyByKey.size());
		for (const auto& [key, seenAtMs] : m_pushIdempotencyByKey) {
			ordered.push_back(PushIdempotencyEntry{ .key = key, .seenAtMs = seenAtMs });
		}
		std::sort(
			ordered.begin(),
			ordered.end(),
			[](const PushIdempotencyEntry& left, const PushIdempotencyEntry& right) {
				return left.seenAtMs < right.seenAtMs;
			});

		const std::size_t removeCount = ordered.size() - maxEntries;
		for (std::size_t index = 0; index < removeCount; ++index) {
			m_pushIdempotencyByKey.erase(ordered[index].key);
		}
	}

	void AgentChatBridgeStateStore::SavePushIdempotencyLocked() const {
		nlohmann::json entries = nlohmann::json::array();
		for (const auto& [key, seenAtMs] : m_pushIdempotencyByKey) {
			entries.push_back(nlohmann::json{
				{ "key", key },
				{ "seenAtMs", seenAtMs },
			});
		}

		nlohmann::json root = {
			{ "entries", entries },
		};

		const std::filesystem::path tempPath = m_pushIdempotencyPath.string() + ".tmp";
		{
			std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				return;
			}
			output << root.dump(2);
		}

		std::error_code renameError;
		std::filesystem::rename(tempPath, m_pushIdempotencyPath, renameError);
		if (renameError) {
			std::error_code removeError;
			std::filesystem::remove(m_pushIdempotencyPath, removeError);
			renameError.clear();
			std::filesystem::rename(tempPath, m_pushIdempotencyPath, renameError);
			if (renameError) {
				std::error_code cleanupError;
				std::filesystem::remove(tempPath, cleanupError);
			}
		}
	}

	bool AgentChatBridgeStateStore::RememberPushIdempotencyKey(
		const std::string& idempotencyKey,
		const std::uint64_t nowMs,
		const std::uint64_t ttlMs,
		const std::size_t maxEntries) {
		const std::string normalized = NormalizeIdempotencyKey(idempotencyKey);
		if (normalized.empty()) {
			return true;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		EnsurePushIdempotencyLoadedLocked();
		PrunePushIdempotencyLocked(nowMs, ttlMs, maxEntries);

		const auto it = m_pushIdempotencyByKey.find(normalized);
		if (it != m_pushIdempotencyByKey.end() &&
			nowMs >= it->second &&
			nowMs - it->second <= ttlMs) {
			return false;
		}

		m_pushIdempotencyByKey.insert_or_assign(normalized, nowMs);
		SavePushIdempotencyLocked();
		return true;
	}

	bool AgentChatBridgeStateStore::HasRecentPushIdempotencyKey(
		const std::string& idempotencyKey,
		const std::uint64_t nowMs,
		const std::uint64_t ttlMs) {
		const std::string normalized = NormalizeIdempotencyKey(idempotencyKey);
		if (normalized.empty()) {
			return false;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		EnsurePushIdempotencyLoadedLocked();
		const auto it = m_pushIdempotencyByKey.find(normalized);
		if (it == m_pushIdempotencyByKey.end()) {
			return false;
		}
		if (it->second > nowMs) {
			return false;
		}
		return nowMs - it->second <= ttlMs;
	}

	bool AgentChatBridgeStateStore::MigrateLegacyOpenClawStateIfNeeded() const {
		return false;
	}

	const std::filesystem::path& AgentChatBridgeStateStore::StateRoot() const noexcept {
		return m_stateRoot;
	}

} // namespace blazeclaw::agentchat
