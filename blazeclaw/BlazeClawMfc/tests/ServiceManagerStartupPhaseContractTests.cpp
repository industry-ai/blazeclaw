#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	std::string ReadServiceManagerSource()
	{
		const auto sourcePath = std::filesystem::path("BlazeClawMfc") /
			"src" /
			"core" /
			"ServiceManager.cpp";
		std::ifstream in(sourcePath.string());
		REQUIRE(in.is_open());

		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

} // namespace

TEST_CASE(
	"ServiceManager startup contract: Start delegates phases in order",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();

	const auto startPos = source.find(
		"bool ServiceManager::Start(const blazeclaw::config::AppConfig& config)");
	REQUIRE(startPos != std::string::npos);

	const auto configurePos = source.find("ConfigurePolicies(config);", startPos);
	const auto initializePos = source.find("InitializeModules();", startPos);
	const auto wirePos = source.find("WireGatewayCallbacks();", startPos);
	const auto finalizePos = source.find("return FinalizeStartup(config);", startPos);

	REQUIRE(configurePos != std::string::npos);
	REQUIRE(initializePos != std::string::npos);
	REQUIRE(wirePos != std::string::npos);
	REQUIRE(finalizePos != std::string::npos);

	REQUIRE(configurePos < initializePos);
	REQUIRE(initializePos < wirePos);
	REQUIRE(wirePos < finalizePos);
}

TEST_CASE(
	"ServiceManager startup contract: phase methods exist",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();

	REQUIRE(source.find("void ServiceManager::ConfigurePolicies(") != std::string::npos);
	REQUIRE(source.find("void ServiceManager::InitializeModules()") != std::string::npos);
	REQUIRE(source.find("void ServiceManager::WireGatewayCallbacks()") != std::string::npos);
	REQUIRE(source.find("bool ServiceManager::FinalizeStartup(") != std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: FinalizeStartup delegates bootstrap coordinator and failure cleanup",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto finalizePos = source.find("bool ServiceManager::FinalizeStartup(");
	REQUIRE(finalizePos != std::string::npos);

	const auto finalizeBody = source.substr(finalizePos);

	REQUIRE(
		finalizeBody.find("m_gatewayRuntimeBootstrapCoordinator.ExecuteStartup") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("m_gatewayRuntimeBootstrapCoordinator.HandleStartupFailure") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("gateway startup failed; running in degraded local mode.") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.failed") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.degraded") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.failed") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.afterStart") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: Stop executes owned runtime cleanup",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto stopPos = source.find("void ServiceManager::Stop()");
	REQUIRE(stopPos != std::string::npos);

	const auto stopBody = source.substr(stopPos);
	REQUIRE(
		stopBody.find("m_state.gatewayLifecycle.cleanupPath = \"normal_stop\";") !=
		std::string::npos);
	REQUIRE(
		stopBody.find("ExecuteGatewayOwnedRuntimeCleanup();") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: managed config auth generation enforcement exists",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto applyDiffPos = source.find("bool ServiceManager::ApplyManagedRuntimeConfigDiff(");
	REQUIRE(applyDiffPos != std::string::npos);

	const auto applyDiffBody = source.substr(applyDiffPos);
	REQUIRE(
		applyDiffBody.find("HasAuthSensitiveConfigChanges(") !=
		std::string::npos);
	REQUIRE(
		applyDiffBody.find("gateway.authSessionGeneration") !=
		std::string::npos);
	REQUIRE(
		applyDiffBody.find("authSessionGenerationRejectCount") !=
		std::string::npos);
	REQUIRE(
		applyDiffBody.find("gateway auth/session config change requires") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: startup failure cleanup path is dedicated",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto finalizePos = source.find("bool ServiceManager::FinalizeStartup(");
	REQUIRE(finalizePos != std::string::npos);

	const auto finalizeBody = source.substr(finalizePos);
	REQUIRE(
		finalizeBody.find("ExecuteGatewayStartupFailureCleanup(config, startupResult);") !=
		std::string::npos);

	REQUIRE(
		source.find("void ServiceManager::ExecuteGatewayStartupFailureCleanup(") !=
		std::string::npos);
	REQUIRE(
		source.find("m_state.gatewayLifecycle.cleanupPath = \"startup_failure\";") !=
		std::string::npos);
}

TEST_CASE(
	"ServiceManager startup contract: startup mode matrix labels are fully defined",
	"[servicemanager][startup][contract]")
{
	const auto coordinatorPath =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"bootstrap" /
		"GatewayRuntimeBootstrapCoordinator.cpp";
	std::ifstream in(coordinatorPath.string());
	REQUIRE(in.is_open());

	const std::string coordinatorSource(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(coordinatorSource.find("raw == L\"disabled\"") != std::string::npos);
	REQUIRE(coordinatorSource.find("raw == L\"local_only\"") != std::string::npos);
	REQUIRE(coordinatorSource.find("raw == L\"full\" || raw == L\"transport\"") != std::string::npos);
	REQUIRE(coordinatorSource.find("return \"local_runtime_dispatch\";") != std::string::npos);
}
