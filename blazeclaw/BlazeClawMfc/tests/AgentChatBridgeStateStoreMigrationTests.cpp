#include "agentchat/AgentChatBridgeStateStore.h"

#include <catch2/catch_all.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace {
	std::filesystem::path MakeTempPath(const std::string& name) {
		const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
		return std::filesystem::temp_directory_path() /
			("blazeclaw-agentchat-" + name + "-" + std::to_string(now));
	}
}

TEST_CASE("State store remembers push idempotency key", "[agentchat][state][idempotency]") {
	const auto root = MakeTempPath("idempotency");
	std::error_code ec;
	std::filesystem::remove_all(root, ec);

	blazeclaw::agentchat::AgentChatBridgeStateStore store(root);
	store.EnsureInitialized();

	const std::uint64_t nowMs = 1760000000000ULL;
	REQUIRE(store.RememberPushIdempotencyKey("k-1", nowMs, 60'000ULL, 100));
	REQUIRE(store.HasRecentPushIdempotencyKey("k-1", nowMs + 10ULL, 60'000ULL));

	std::filesystem::remove_all(root, ec);
}

TEST_CASE("State migration copies legacy idempotency cache once with marker", "[agentchat][state][migration]") {
	const auto root = MakeTempPath("state-root");
	const auto legacyRoot = MakeTempPath("legacy-root");
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
	std::filesystem::remove_all(legacyRoot, ec);

	std::filesystem::create_directories(legacyRoot / "cache", ec);
	{
		nlohmann::json legacyEntries = {
			{ "entries", nlohmann::json::array({
				nlohmann::json{
					{ "key", "legacy-k" },
					{ "seenAtMs", 1760000000000ULL },
				},
			}) },
		};
		std::ofstream out(legacyRoot / "cache" / "push.idempotency.json", std::ios::binary | std::ios::trunc);
		REQUIRE(out.is_open());
		out << legacyEntries.dump(2);
	}

	blazeclaw::agentchat::AgentChatBridgeStateStore store(root, legacyRoot, true);
	store.EnsureInitialized();
	REQUIRE(store.MigrateLegacyOpenClawStateIfNeeded());

	const auto migratedPath = root / "cache" / "push.idempotency.json";
	REQUIRE(std::filesystem::exists(migratedPath));
	const auto markerPath = root / "migration" / "openclaw-state.migrated.json";
	REQUIRE(std::filesystem::exists(markerPath));

	const std::uint64_t nowMs = 1760000001000ULL;
	REQUIRE(store.HasRecentPushIdempotencyKey("legacy-k", nowMs, 60'000ULL));

	// second migration is idempotent due marker
	REQUIRE_FALSE(store.MigrateLegacyOpenClawStateIfNeeded());

	std::filesystem::remove_all(root, ec);
	std::filesystem::remove_all(legacyRoot, ec);
}
