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
	"ServiceManager startup contract: FinalizeStartup preserves failure-path warnings and trace labels",
	"[servicemanager][startup][contract]")
{
	const std::string source = ReadServiceManagerSource();
	const auto finalizePos = source.find("bool ServiceManager::FinalizeStartup(");
	REQUIRE(finalizePos != std::string::npos);

	const auto finalizeBody = source.substr(finalizePos);

	REQUIRE(
		finalizeBody.find("gateway local dispatch initialization threw an exception") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("gateway local dispatch initialization failed; methods may be unavailable.") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("gateway local runtime initialization failed; running with limited gateway methods.") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("gateway startup threw an exception; running in degraded local mode.") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("gateway startup failed; running in degraded local mode.") !=
		std::string::npos);

	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.localRuntimeDispatch.exception") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.localRuntimeDispatch.failed") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.localInit.attempted") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.exception") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.failed") !=
		std::string::npos);
	REQUIRE(
		finalizeBody.find("ServiceManager.Start.gateway.afterStart") !=
		std::string::npos);
}
