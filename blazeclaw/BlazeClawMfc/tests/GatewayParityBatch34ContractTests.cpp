#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	std::filesystem::path GatewayChatPipelineSourcePath()
	{
		return std::filesystem::path("BlazeClawMfc") /
			"src" /
			"gateway" /
			"GatewayHost.Handlers.Runtime.ChatPipeline.cpp";
	}

	std::filesystem::path GatewayRuntimeHelpersPath()
	{
		return std::filesystem::path("BlazeClawMfc") /
			"src" /
			"gateway" /
			"GatewayHost.Handlers.RuntimeHelpers.inl";
	}

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream in(path.string());
		REQUIRE(in.is_open());
		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

} // namespace

TEST_CASE(
	"Batch 3 transcript contract: user and assistant transcript append APIs are present",
	"[gateway][parity][batch34][parity][batch3][transcript]")
{
	const auto headerPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"ChatTranscriptStore.h";
	const std::string header = ReadTextFile(headerPath);

	REQUIRE(header.find("std::string role") != std::string::npos);
	REQUIRE(header.find("AppendUserMessage") != std::string::npos);
	REQUIRE(header.find("AppendAssistantMessage") != std::string::npos);

	const std::string chatPipelineSource = ReadTextFile(GatewayChatPipelineSourcePath());
	REQUIRE(chatPipelineSource.find("AppendUserMessage(") != std::string::npos);
	REQUIRE(chatPipelineSource.find("AppendAssistantMessage(") != std::string::npos);
	REQUIRE(chatPipelineSource.find("runId + \":user\"") != std::string::npos);
	REQUIRE(chatPipelineSource.find("runId + \":assistant\"") != std::string::npos);
}

TEST_CASE(
	"Batch 3 event fanout contract: recipient registry exposes active runs for late join replay",
	"[gateway][parity][batch34][parity][batch3][fanout]")
{
	const auto registryHeaderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"TransportRecipientRegistry.h";
	const std::string registryHeader = ReadTextFile(registryHeaderPath);
	REQUIRE(registryHeader.find("ActiveRunsForSession") != std::string::npos);

	const std::string chatPipelineSource = ReadTextFile(GatewayChatPipelineSourcePath());
	REQUIRE(chatPipelineSource.find("late_join_replay") != std::string::npos);
	REQUIRE(chatPipelineSource.find("ActiveRunsForSession") != std::string::npos);
	REQUIRE(chatPipelineSource.find("delta_replayed") != std::string::npos);
}

TEST_CASE(
	"Batch 3 error model contract: runtime retryability helpers are wired",
	"[gateway][parity][batch34][parity][batch3][error-model]")
{
	const auto guardHeaderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"RuntimeTranscriptGuard.h";
	const std::string guardHeader = ReadTextFile(guardHeaderPath);
	REQUIRE(guardHeader.find("IsRetryableErrorCode") != std::string::npos);
	REQUIRE(guardHeader.find("SuggestedRetryAfterMs") != std::string::npos);

	const std::string runtimeHelpers = ReadTextFile(GatewayRuntimeHelpersPath());
	REQUIRE(runtimeHelpers.find("BuildRuntimeErrorShape") != std::string::npos);
	REQUIRE(runtimeHelpers.find("RuntimeTranscriptGuard::IsRetryableErrorCode") !=
		std::string::npos);
	REQUIRE(runtimeHelpers.find("RuntimeTranscriptGuard::SuggestedRetryAfterMs") !=
		std::string::npos);

	const std::string chatPipelineSource = ReadTextFile(GatewayChatPipelineSourcePath());
	REQUIRE(chatPipelineSource.find("BuildRuntimeErrorShape") != std::string::npos);
}

TEST_CASE(
	"Batch 4 diagnostics and rollout contract: payload summary and stage rollout cohorts are present",
	"[gateway][parity][batch34][parity][batch4][diagnostics][rollout]")
{
	const auto diagnosticsHeaderPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"BranchDecisionDiagnostics.h";
	const std::string diagnosticsHeader = ReadTextFile(diagnosticsHeaderPath);
	REQUIRE(diagnosticsHeader.find("EmitWithPayloadSummary") != std::string::npos);

	const auto diagnosticsImplPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"BranchDecisionDiagnostics.cpp";
	const std::string diagnosticsImpl = ReadTextFile(diagnosticsImplPath);
	REQUIRE(diagnosticsImpl.find("payloadSummary") != std::string::npos);

	const std::string chatPipelineSource = ReadTextFile(GatewayChatPipelineSourcePath());
	REQUIRE(chatPipelineSource.find("runtime_message_built") != std::string::npos);

	const auto routerImplPath = std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHostRouter.cpp";
	const std::string routerImpl = ReadTextFile(routerImplPath);
	REQUIRE(routerImpl.find("legacy_rollout_cohort_off") != std::string::npos);
	REQUIRE(routerImpl.find("stage_pipeline_canary_bucket") != std::string::npos);
	REQUIRE(routerImpl.find("stage_pipeline_full_rollout") != std::string::npos);
}
