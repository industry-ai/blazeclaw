#pragma once

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace blazeclaw::agentchat {

class AgentChatBridgeStateStore {
public:
	explicit AgentChatBridgeStateStore(
		std::filesystem::path stateRoot,
		std::filesystem::path legacyStateRoot = {},
		bool legacyStateMigrationEnabled = true);

	void EnsureInitialized() const;
	bool MigrateLegacyOpenClawStateIfNeeded() const;
	const std::filesystem::path& StateRoot() const noexcept;
	const std::filesystem::path& PushIdempotencyStorePath() const noexcept;

	bool RememberPushIdempotencyKey(
		const std::string& idempotencyKey,
		std::uint64_t nowMs,
		std::uint64_t ttlMs,
		std::size_t maxEntries);
	bool HasRecentPushIdempotencyKey(
		const std::string& idempotencyKey,
		std::uint64_t nowMs,
		std::uint64_t ttlMs);

	static std::filesystem::path ResolveDefaultStateRoot();

private:
	struct PushIdempotencyEntry {
		std::string key;
		std::uint64_t seenAtMs = 0;
	};

	void EnsurePushIdempotencyLoadedLocked();
	void PrunePushIdempotencyLocked(
		std::uint64_t nowMs,
		std::uint64_t ttlMs,
		std::size_t maxEntries);
	void SavePushIdempotencyLocked() const;
	static std::string NormalizeIdempotencyKey(
		const std::string& value);

	mutable std::mutex m_mutex;
	std::filesystem::path m_stateRoot;
	std::filesystem::path m_legacyStateRoot;
	bool m_legacyStateMigrationEnabled = true;
	std::filesystem::path m_pushIdempotencyPath;
	bool m_pushIdempotencyLoaded = false;
	std::unordered_map<std::string, std::uint64_t> m_pushIdempotencyByKey;
};

} // namespace blazeclaw::agentchat
