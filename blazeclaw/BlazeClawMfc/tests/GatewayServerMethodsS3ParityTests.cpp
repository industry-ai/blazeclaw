#include <catch2/catch_all.hpp>

#include "gateway/GatewayHost.h"
#include "gateway/GatewayProtocolModels.h"

using blazeclaw::gateway::GatewayHost;
using blazeclaw::gateway::protocol::RequestFrame;

namespace {

	blazeclaw::gateway::protocol::ResponseFrame Route(
		GatewayHost& host,
		const std::string& id,
		const std::string& method,
		std::optional<std::string> paramsJson = std::nullopt)
	{
		return host.RouteRequest(
			RequestFrame{
				.id = id,
				.method = method,
				.paramsJson = std::move(paramsJson),
			});
	}

	void RequireMethodImplemented(
		const blazeclaw::gateway::protocol::ResponseFrame& response)
	{
		if (response.error.has_value()) {
			REQUIRE(response.error->code != "method_not_implemented");
		}
	}

} // namespace

TEST_CASE("S3 parity: chat/config/sessions/skills/tools families resolve through runtime dispatch surface", "[gateway][parity][s3][server-methods]") {
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());
	REQUIRE(host.RuntimeContext().IsBound());
	REQUIRE(host.RuntimeContext().dispatcher != nullptr);

	const auto chatSend = Route(
		host,
		"s3-chat-send",
		"chat.send",
		std::string("{\"message\":\"hello from s3 parity\"}"));
	RequireMethodImplemented(chatSend);

	const auto configGet = Route(host, "s3-config-get", "config.get");
	RequireMethodImplemented(configGet);

	const auto sessionsSend = Route(
		host,
		"s3-sessions-send",
		"sessions.send",
		std::string("{\"sessionId\":\"main\",\"message\":\"ping\"}"));
	RequireMethodImplemented(sessionsSend);

	const auto skillsSearch = Route(host, "s3-skills-search", "skills.search");
	RequireMethodImplemented(skillsSearch);

	const auto toolsCatalog = Route(host, "s3-tools-catalog", "tools.catalog");
	RequireMethodImplemented(toolsCatalog);
}

TEST_CASE("S3 parity: runtime callback seams remain wired for chat/config/skills/embeddings", "[gateway][parity][s3][server-methods]") {
	GatewayHost host;
	REQUIRE(host.StartLocalDispatchOnly());

	int chatRuntimeCalls = 0;
	int configGetCalls = 0;
	int configLookupCalls = 0;
	int skillsUpdateCalls = 0;
	int embeddingsGenerateCalls = 0;
	int embeddingsBatchCalls = 0;

	host.SetChatRuntimeCallback(
		[&chatRuntimeCalls](const GatewayHost::ChatRuntimeRequest& request) {
			++chatRuntimeCalls;
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = "s3-callback";
			result.modelId = "s3-model";
			result.taskDeltas = {
				{
					.index = 0,
					.runId = request.runId.empty() ? "s3-run" : request.runId,
					.sessionId = request.sessionKey.empty() ? "main" : request.sessionKey,
					.phase = "final",
					.resultJson = "{\"status\":\"ok\"}",
					.status = "completed",
					.stepLabel = "s3-callback",
				},
			};
			return result;
		});

	host.SetConfigSchemaGetCallback(
		[&configGetCalls]() {
			++configGetCalls;
			return blazeclaw::gateway::ConfigSchemaGatewayState{
				.schemaJson = "{\"gateway\":{\"bind\":\"string\"}}",
				.uiHintsJson = "{}",
				.version = "s3",
				.generatedAt = "2026-04-23T00:00:00Z",
			};
		});

	host.SetConfigSchemaLookupCallback(
		[&configLookupCalls](const std::string& path) -> std::optional<blazeclaw::gateway::ConfigSchemaGatewayLookupResult> {
			++configLookupCalls;
			return blazeclaw::gateway::ConfigSchemaGatewayLookupResult{
				.path = path,
				.schemaJson = "{\"type\":\"string\"}",
				.hint = std::nullopt,
				.hintPath = {},
				.children = {},
			};
		});

	host.SetSkillsUpdateCallback(
		[&skillsUpdateCalls](const blazeclaw::gateway::protocol::RequestFrame& request) {
			++skillsUpdateCalls;
			return blazeclaw::gateway::protocol::OkResponse(request, "{\"status\":\"updated\",\"source\":\"s3-callback\"}");
		});

	host.SetEmbeddingsGenerateCallback(
		[&embeddingsGenerateCalls](const GatewayHost::EmbeddingsGenerateRequest&) {
			++embeddingsGenerateCalls;
			GatewayHost::EmbeddingsGenerateResult result;
			result.ok = true;
			result.vector = { 1.0f, 2.0f, 3.0f };
			result.dimension = 3;
			result.provider = "s3";
			result.modelId = "embed-s3";
			result.status = "ok";
			return result;
		});

	host.SetEmbeddingsBatchCallback(
		[&embeddingsBatchCalls](const GatewayHost::EmbeddingsBatchRequest&) {
			++embeddingsBatchCalls;
			GatewayHost::EmbeddingsBatchResult result;
			result.ok = true;
			result.vectors = { { 1.0f, 2.0f }, { 3.0f, 4.0f } };
			result.dimension = 2;
			result.provider = "s3";
			result.modelId = "embed-s3-batch";
			result.status = "ok";
			return result;
		});

	const auto chatSend = Route(
		host,
		"s3-callback-chat-send",
		"chat.send",
		std::string("{\"message\":\"exercise runtime callback\"}"));
	RequireMethodImplemented(chatSend);
	REQUIRE(chatRuntimeCalls > 0);

	const auto configSchema = Route(host, "s3-callback-config-schema", "config.schema");
	RequireMethodImplemented(configSchema);
	REQUIRE(configGetCalls > 0);

	const auto configLookup = Route(
		host,
		"s3-callback-config-lookup",
		"config.schema.lookup",
		std::string("{\"path\":\"gateway.bind\"}"));
	RequireMethodImplemented(configLookup);
	REQUIRE(configLookupCalls > 0);

	const auto skillsUpdate = Route(
		host,
		"s3-callback-skills-update",
		"skills.update",
		std::string("{\"name\":\"email.schedule\",\"source\":\"clawhub\"}"));
	RequireMethodImplemented(skillsUpdate);
	REQUIRE(skillsUpdateCalls > 0);

	const auto embeddingsGenerate = Route(
		host,
		"s3-callback-embeddings-generate",
		"gateway.embeddings.generate",
		std::string("{\"text\":\"hello\"}"));
	RequireMethodImplemented(embeddingsGenerate);
	REQUIRE(embeddingsGenerateCalls > 0);

	const auto embeddingsBatch = Route(
		host,
		"s3-callback-embeddings-batch",
		"gateway.embeddings.batchGenerate",
		std::string("{\"texts\":[\"a\",\"b\"]}"));
	RequireMethodImplemented(embeddingsBatch);
	REQUIRE(embeddingsBatchCalls > 0);
}
