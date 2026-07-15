#include "pch.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <catch2/catch_all.hpp>

#include "gateway/GatewayHost.h"
#include "gateway/GatewayProtocolSchemaValidator.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using blazeclaw::gateway::GatewayHost;
using blazeclaw::gateway::protocol::GatewayProtocolSchemaValidator;
using blazeclaw::gateway::protocol::RequestFrame;
using blazeclaw::gateway::protocol::ResponseFrame;
using blazeclaw::gateway::protocol::SchemaValidationIssue;

namespace {

std::string PollAllEvents(
	GatewayHost& host,
	const std::string& sessionKey,
	int attempts = 8,
	int limit = 64)
{
	std::string combined;
	for (int index = 0; index < attempts; ++index) {
		const auto poll = host.RouteRequest(
			RequestFrame{
				.id = std::string("multi-active-contract-poll-") + std::to_string(index),
				.method = "chat.events.poll",
				.paramsJson = std::string("{\"sessionKey\":\"") +
					sessionKey +
					"\",\"limit\":" + std::to_string(limit) + "}",
			});
		REQUIRE(poll.ok);
		REQUIRE(poll.payloadJson.has_value());
		combined += poll.payloadJson.value();
	}
	return combined;
}

bool ContainsAny(
	const std::string& source,
	const std::vector<std::string>& candidates)
{
	return std::any_of(
		candidates.begin(),
		candidates.end(),
		[&source](const std::string& token) {
			return source.find(token) != std::string::npos;
		});
}

} // namespace

TEST_CASE(
	"Multi-active contract validates chat.send request and response schemas",
	"[gateway][multi-active][contract][schema]")
{
	SchemaValidationIssue issue;

	const RequestFrame sendRequest{
		.id = "multi-active-send-schema",
		.method = "chat.send",
		.paramsJson = std::string(
			"{"
			"\"sessionKey\":\"main\","
			"\"message\":\"schema coverage\","
			"\"idempotencyKey\":\"multi-active-schema-idem\","
			"\"responseMode\":\"multi_active\","
			"\"responders\":[\"local:gemma\",\"remote:deepseek\"]"
			"}"),
	};
	REQUIRE(GatewayProtocolSchemaValidator::ValidateRequest(sendRequest, issue));

	const ResponseFrame sendResponse{
		.id = "multi-active-send-response",
		.ok = true,
		.payloadJson = std::string(
			"{"
			"\"runId\":\"run-42\","
			"\"promptRunId\":\"run-42.prompt\","
			"\"responseMode\":\"multi_active\","
			"\"responderRunIds\":[\"run-42.r0\",\"run-42.r1\"],"
			"\"responders\":["
			"{"
			"\"responderId\":\"local:gemma\","
			"\"responderLabel\":\"Gemma (Local)\","
			"\"responderOrder\":0,"
			"\"responderRunId\":\"run-42.r0\","
			"\"runtimeKind\":\"local\","
			"\"provider\":\"local\","
			"\"model\":\"gemma\""
			"},"
			"{"
			"\"responderId\":\"remote:deepseek\","
			"\"responderLabel\":\"DeepSeek (Remote)\","
			"\"responderOrder\":1,"
			"\"responderRunId\":\"run-42.r1\","
			"\"runtimeKind\":\"remote\","
			"\"provider\":\"deepseek\","
			"\"model\":\"deepseek-chat\""
			"}"
			"]"
			"}"),
		.error = std::nullopt,
	};
	REQUIRE(
		GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"chat.send",
			sendResponse,
			issue));
}

TEST_CASE(
	"Multi-active runtime emits ordered responder envelope and fan-out poll states",
	"[gateway][multi-active][contract][ordering][fanout]")
{
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = std::string("multi-active reply for ") + request.runId;
			result.assistantDeltas = {
				std::string("delta:") + request.runId,
				std::string("final:") + request.runId,
			};
			result.taskDeltas = {
				GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = 0,
					.runId = request.runId,
					.sessionId = request.sessionKey,
					.phase = "final",
					.status = "completed",
					.stepLabel = "run_terminal",
				},
			};
			return result;
		});

	const auto send = host.RouteRequest(
		RequestFrame{
			.id = "multi-active-contract-send-ordering",
			.method = "chat.send",
			.paramsJson = std::string(
				"{"
				"\"sessionKey\":\"main\","
				"\"message\":\"ordering check\","
				"\"idempotencyKey\":\"multi-active-ordering-idem\","
				"\"responseMode\":\"multi_active\","
				"\"responders\":[\"local:gemma\",\"remote:deepseek\"]"
				"}"),
		});
	REQUIRE(send.ok);
	REQUIRE(send.payloadJson.has_value());

	const std::string sendPayload = send.payloadJson.value();
	const auto localPos = sendPayload.find("\"responderId\":\"local");
	const auto remotePos = sendPayload.find("\"responderId\":\"remote");
	REQUIRE(localPos != std::string::npos);
	REQUIRE(remotePos != std::string::npos);
	REQUIRE(localPos < remotePos);

	const std::string polled = PollAllEvents(host, "main");
	REQUIRE(polled.find("\"promptRunId\"") != std::string::npos);
	REQUIRE(polled.find("\"responderRunId\"") != std::string::npos);
	REQUIRE(polled.find("\"responderOrder\":0") != std::string::npos);
	REQUIRE(polled.find("\"responderOrder\":1") != std::string::npos);
	REQUIRE(ContainsAny(polled, {"\"state\":\"delta\"", "\"state\":\"final\"", "\"state\":\"completed\""}));

	host.Stop();
}

TEST_CASE(
	"Multi-active abort path emits terminal abort-compatible envelope",
	"[gateway][multi-active][contract][abort]")
{
	GatewayHost host;
	blazeclaw::config::GatewayConfig gatewayConfig;
	REQUIRE(host.StartLocalOnly(gatewayConfig));

	host.SetChatRuntimeCallback(
		[](const GatewayHost::ChatRuntimeRequest& request) {
			GatewayHost::ChatRuntimeResult result;
			result.ok = true;
			result.assistantText = std::string("abortable-") + request.runId;
			result.assistantDeltas = {"stream-before-abort"};
			return result;
		});

	const auto send = host.RouteRequest(
		RequestFrame{
			.id = "multi-active-contract-send-abort",
			.method = "chat.send",
			.paramsJson = std::string(
				"{"
				"\"sessionKey\":\"main\","
				"\"message\":\"abort check\","
				"\"idempotencyKey\":\"multi-active-abort-idem\","
				"\"responseMode\":\"multi_active\","
				"\"responders\":[\"local:gemma\",\"remote:deepseek\"]"
				"}"),
		});
	REQUIRE(send.ok);
	REQUIRE(send.payloadJson.has_value());

	std::string promptRunId;
	const std::string marker = "\"promptRunId\":\"";
	const auto markerPos = send.payloadJson->find(marker);
	if (markerPos != std::string::npos) {
		const auto start = markerPos + marker.size();
		const auto end = send.payloadJson->find('"', start);
		if (end != std::string::npos) {
			promptRunId = send.payloadJson->substr(start, end - start);
		}
	}
	REQUIRE_FALSE(promptRunId.empty());

	const auto abort = host.RouteRequest(
		RequestFrame{
			.id = "multi-active-contract-abort",
			.method = "chat.abort",
			.paramsJson = std::string("{\"sessionKey\":\"main\",\"runId\":\"") +
				promptRunId +
				"\"}",
		});
	REQUIRE(abort.ok);

	const std::string polled = PollAllEvents(host, "main");
	REQUIRE(polled.find("\"promptRunId\":\"" + promptRunId + "\"") != std::string::npos);
	REQUIRE(ContainsAny(polled, {"\"state\":\"aborted\"", "\"state\":\"error\"", "\"state\":\"completed\""}));

	SchemaValidationIssue issue;
	const ResponseFrame pollFrame{
		.id = "multi-active-abort-poll-validate",
		.ok = true,
		.payloadJson = std::string(
			"{"
			"\"sessionKey\":\"main\","
			"\"events\":["
			"{"
			"\"runId\":\"run-x\","
			"\"promptRunId\":\"run-x.prompt\","
			"\"responderRunId\":\"run-x.r0\","
			"\"responderId\":\"local:gemma\","
			"\"responderLabel\":\"Gemma (Local)\","
			"\"responderOrder\":0,"
			"\"sessionKey\":\"main\","
			"\"state\":\"aborted\","
			"\"timestamp\":1"
			"}"
			"],"
			"\"count\":1"
			"}"),
		.error = std::nullopt,
	};
	REQUIRE(
		GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			"chat.events.poll",
			pollFrame,
			issue));

	host.Stop();
}
