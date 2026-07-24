#include "pch.h"
#include <catch2/catch_all.hpp>
#include "../src/core/ServiceManagerSnapshotHelpers.h"

using namespace blazeclaw::core::servicemanager_snapshot;

TEST_CASE("[servicemanager][snapshot] CollectGatewayStatusSnapshot returns basic shape", "[servicemanager][snapshot]") {
	blazeclaw::config::AppConfig cfg;
	auto s = CollectGatewayStatusSnapshot(cfg);
	REQUIRE(s.supported == true);
	REQUIRE(!s.status.empty());
}

TEST_CASE("[servicemanager][snapshot] CollectRuntimeHealthSnapshot default", "[servicemanager][snapshot]") {
	auto r = CollectRuntimeHealthSnapshot();
	REQUIRE(r.allSystemsGo == true);
	REQUIRE(r.summary == "all_systems_nominal");
}
