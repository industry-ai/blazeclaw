#include "gateway/PluginRuntimeStateService.h"

#include <catch2/catch_all.hpp>

using namespace blazeclaw::gateway;

namespace {

	const std::vector<ExtensionManifest> kFixtureRegistryA = {
		ExtensionManifest{.id = "alpha" },
		ExtensionManifest{.id = "beta" },
	};

	const std::vector<ExtensionManifest> kFixtureRegistryB = {
		ExtensionManifest{.id = "route" },
	};

} // namespace

TEST_CASE(
	"PluginRuntimeStateService tracks imported plugin ids and clears on reset",
	"[plugin-runtime-state][import-trace]") {
	PluginRuntimeStateService service;
	service.RecordImportedPluginId("alpha");
	service.RecordImportedPluginId("beta");
	service.RecordImportedPluginId("alpha");

	const auto imported = service.ListImportedRuntimePluginIds();
	REQUIRE(imported.size() == 2);
	REQUIRE(imported[0] == "alpha");
	REQUIRE(imported[1] == "beta");

	service.ResetForTest();
	REQUIRE(service.ListImportedRuntimePluginIds().empty());
}

TEST_CASE(
	"PluginRuntimeStateService require APIs install fallback registries",
	"[plugin-runtime-state][require]") {
	PluginRuntimeStateService service;
	REQUIRE(service.GetActiveRegistry() == nullptr);
	REQUIRE(service.GetHttpRouteRegistry() == nullptr);
	REQUIRE(service.GetChannelRegistry() == nullptr);

	const auto* active = service.RequireActiveRegistry(
		&kFixtureRegistryA,
		"cache-a",
		"workspace-a",
		PluginRuntimeSubagentMode::Explicit);
	REQUIRE(active == &kFixtureRegistryA);
	REQUIRE(service.GetActiveRegistry() == &kFixtureRegistryA);
	REQUIRE(service.GetCacheKey() == "cache-a");
	REQUIRE(service.GetWorkspaceDir() == "workspace-a");
	REQUIRE(service.GetRuntimeSubagentMode() == PluginRuntimeSubagentMode::Explicit);

	const auto* route = service.RequireHttpRouteRegistry(
		&kFixtureRegistryB,
		"cache-b",
		"workspace-b",
		PluginRuntimeSubagentMode::GatewayBindable);
	REQUIRE(route == &kFixtureRegistryA);

	const auto* channel = service.RequireChannelRegistry(
		&kFixtureRegistryB,
		"cache-c",
		"workspace-c",
		PluginRuntimeSubagentMode::Default);
	REQUIRE(channel == &kFixtureRegistryA);
}

TEST_CASE(
	"PluginRuntimeStateService reset clears runtime state surfaces",
	"[plugin-runtime-state][reset]") {
	PluginRuntimeStateService service;
	service.SetActiveRegistry(
		&kFixtureRegistryA,
		"cache-main",
		"workspace-main",
		PluginRuntimeSubagentMode::GatewayBindable);
	service.PinHttpRouteRegistry(&kFixtureRegistryB);
	service.PinChannelRegistry(&kFixtureRegistryB);
	service.RecordImportedPluginId("alpha");

	service.ResetForTest();

	const auto snapshot = service.Snapshot();
	REQUIRE(snapshot.activeRegistry == nullptr);
	REQUIRE(snapshot.httpRoute.registry == nullptr);
	REQUIRE(snapshot.channel.registry == nullptr);
	REQUIRE(snapshot.cacheKey.empty());
	REQUIRE(snapshot.workspaceDir.empty());
	REQUIRE(snapshot.runtimeSubagentMode == PluginRuntimeSubagentMode::Default);
	REQUIRE(snapshot.importedPluginIds.empty());
}

TEST_CASE(
	"PluginRuntimeStateService activation API centralizes pin transitions",
	"[plugin-runtime-state][activation]") {
	PluginRuntimeStateService service;
	service.ActivateRuntimeRegistry(
		&kFixtureRegistryA,
		"cache-activate",
		"workspace-activate",
		PluginRuntimeSubagentMode::GatewayBindable,
		true,
		true);

	const auto activated = service.Snapshot();
	REQUIRE(activated.activeRegistry == &kFixtureRegistryA);
	REQUIRE(activated.httpRoute.registry == &kFixtureRegistryA);
	REQUIRE(activated.channel.registry == &kFixtureRegistryA);
	REQUIRE(activated.httpRoute.pinned);
	REQUIRE(activated.channel.pinned);

	service.DeactivateRuntimeRegistry();
	const auto deactivated = service.Snapshot();
	REQUIRE(deactivated.activeRegistry == nullptr);
	REQUIRE(deactivated.httpRoute.registry == nullptr);
	REQUIRE(deactivated.channel.registry == nullptr);
	REQUIRE_FALSE(deactivated.httpRoute.pinned);
	REQUIRE_FALSE(deactivated.channel.pinned);
	REQUIRE(deactivated.cacheKey.empty());
	REQUIRE(deactivated.workspaceDir.empty());
	REQUIRE(
		deactivated.runtimeSubagentMode ==
		PluginRuntimeSubagentMode::Default);
}
