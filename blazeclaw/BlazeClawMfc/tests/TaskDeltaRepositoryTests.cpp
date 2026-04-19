#include "gateway/TaskDeltaRepository.h"

#include <catch2/catch_all.hpp>

using blazeclaw::gateway::TaskDeltaEntry;
using blazeclaw::gateway::TaskDeltaRepository;

namespace {

	TaskDeltaEntry MakeOneDelta(const std::string& runId, std::uint64_t completedAtMs) {
		TaskDeltaEntry e;
		e.runId = runId;
		e.completedAtMs = completedAtMs;
		e.startedAtMs = completedAtMs > 0 ? completedAtMs - 1 : 0;
		e.phase = "final";
		e.status = "completed";
		return e;
	}

} // namespace

TEST_CASE("TaskDeltaRepository EnforceRetentionLimit evicts lowest activity", "[gateway][taskdelta]") {
	TaskDeltaRepository::Store store;
	TaskDeltaRepository repo(store);

	(void)repo.Upsert("run-old", { MakeOneDelta("run-old", 10) }, 10);
	(void)repo.Upsert("run-mid", { MakeOneDelta("run-mid", 50) }, 50);
	(void)repo.Upsert("run-new", { MakeOneDelta("run-new", 90) }, 90);

	repo.EnforceRetentionLimit(2);

	REQUIRE(store.size() == 2);
	REQUIRE(store.find("run-old") == store.end());
	REQUIRE(store.find("run-mid") != store.end());
	REQUIRE(store.find("run-new") != store.end());
}

TEST_CASE("TaskDeltaRepository EnforceRetentionLimit tie-breaks by runId", "[gateway][taskdelta]") {
	TaskDeltaRepository::Store store;
	TaskDeltaRepository repo(store);

	(void)repo.Upsert("zebra", { MakeOneDelta("zebra", 1) }, 100);
	(void)repo.Upsert("apple", { MakeOneDelta("apple", 1) }, 100);

	repo.EnforceRetentionLimit(1);

	REQUIRE(store.size() == 1);
	REQUIRE(store.find("apple") != store.end());
	REQUIRE(store.find("zebra") == store.end());
}

TEST_CASE("TaskDeltaRepository Get refreshes recency", "[gateway][taskdelta]") {
	TaskDeltaRepository::Store store;
	TaskDeltaRepository repo(store);

	(void)repo.Upsert("stale", { MakeOneDelta("stale", 1) }, 1);
	(void)repo.Upsert("fresh", { MakeOneDelta("fresh", 2) }, 2);

	(void)repo.Get("stale", true);

	repo.EnforceRetentionLimit(1);

	REQUIRE(store.size() == 1);
	REQUIRE(store.find("stale") != store.end());
	REQUIRE(store.find("fresh") == store.end());
}
