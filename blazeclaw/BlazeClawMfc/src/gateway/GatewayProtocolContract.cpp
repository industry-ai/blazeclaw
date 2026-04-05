#include "pch.h"
#include "GatewayProtocolContract.h"

#include "GatewayProtocolCodec.h"
#include "GatewayJsonUtils.h"
#include "GatewayProtocolSchemaValidator.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace blazeclaw::gateway::protocol {
	namespace {

		std::string ReadFileText(const std::filesystem::path& path) {
			std::ifstream input(path, std::ios::in | std::ios::binary);
			if (!input.is_open()) {
				return {};
			}

			std::ostringstream buffer;
			buffer << input.rdbuf();
			return buffer.str();
		}

		std::string TrimBoundaryWhitespace(const std::string& value) {
			return json::Trim(value);
		}

		bool FindStringField(
			const std::string& text,
			const std::string& fieldName,
			std::string& outValue) {
			return json::FindStringField(text, fieldName, outValue);
		}

		bool FindRawField(
			const std::string& text,
			const std::string& fieldName,
			std::string& outValue) {
			return json::FindRawField(text, fieldName, outValue);
		}

		bool FindBoolField(
			const std::string& text,
			const std::string& fieldName,
			bool& outValue) {
			return json::FindBoolField(text, fieldName, outValue);
		}

		bool FindUInt64Field(
			const std::string& text,
			const std::string& fieldName,
			std::uint64_t& outValue) {
			return json::FindUInt64Field(text, fieldName, outValue);
		}

		bool TryDecodeResponseFrame(
			const std::string& inboundJson,
			ResponseFrame& outFrame,
			std::string& error) {
			std::string type;
			if (!FindStringField(inboundJson, "type", type)) {
				error = "Missing required field: type";
				return false;
			}

			if (type != "res") {
				error = "Unsupported frame type for response decode: " + type;
				return false;
			}

			if (!FindStringField(inboundJson, "id", outFrame.id)) {
				error = "Missing required field: id";
				return false;
			}

			if (!FindBoolField(inboundJson, "ok", outFrame.ok)) {
				error = "Missing or invalid required field: ok";
				return false;
			}

			std::string payload;
			if (FindRawField(inboundJson, "payload", payload)) {
				outFrame.payloadJson = payload;
			}
			else {
				outFrame.payloadJson = std::nullopt;
			}

			error.clear();
			return true;
		}

		bool TryDecodeEventFrame(
			const std::string& inboundJson,
			EventFrame& outFrame,
			std::string& error) {
			std::string type;
			if (!FindStringField(inboundJson, "type", type)) {
				error = "Missing required field: type";
				return false;
			}

			if (type != "event") {
				error = "Unsupported frame type for event decode: " + type;
				return false;
			}

			if (!FindStringField(inboundJson, "event", outFrame.eventName)) {
				error = "Missing required field: event";
				return false;
			}

			std::string payload;
			if (FindRawField(inboundJson, "payload", payload)) {
				outFrame.payloadJson = payload;
			}
			else {
				outFrame.payloadJson = std::nullopt;
			}

			std::uint64_t seq = 0;
			if (FindUInt64Field(inboundJson, "seq", seq)) {
				outFrame.seq = seq;
			}
			else {
				outFrame.seq = std::nullopt;
			}

			std::uint64_t stateVersion = 0;
			if (FindUInt64Field(inboundJson, "stateVersion", stateVersion)) {
				outFrame.stateVersion = stateVersion;
			}
			else {
				outFrame.stateVersion = std::nullopt;
			}

			error.clear();
			return true;
		}

		bool CompareFixture(const std::filesystem::path& path, const std::string& actual, std::string& error) {
			const std::string expected = TrimBoundaryWhitespace(ReadFileText(path));
			if (expected.empty()) {
				error = "Missing or empty fixture: " + path.string();
				return false;
			}

			const std::string normalizedActual = TrimBoundaryWhitespace(actual);
			if (expected != normalizedActual) {
				error = "Fixture mismatch: " + path.string();
				return false;
			}

			return true;
		}

		bool StartsWith(const std::string& value, const std::string& prefix) {
			return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
		}

		std::string ReplaceUnderscoresWithDots(std::string value) {
			for (char& ch : value) {
				if (ch == '_') {
					ch = '.';
				}
			}
			return value;
		}

		std::string InferMethodFromResponseFixtureName(const std::string& fileName) {
			if (fileName == "response_pong.json") {
				return "gateway.ping";
			}

			if (!StartsWith(fileName, "response_") || fileName.size() <= 14 || fileName.substr(fileName.size() - 5) != ".json") {
				return {};
			}

			const std::string stem = fileName.substr(9, fileName.size() - 14);
			if (stem.empty()) {
				return {};
			}

			return "gateway." + ReplaceUnderscoresWithDots(stem);
		}

		std::string InferEventNameFromEventFixtureName(const std::string& fileName) {
			if (!StartsWith(fileName, "event_") || fileName.size() <= 11 || fileName.substr(fileName.size() - 5) != ".json") {
				return {};
			}

			const std::string stem = fileName.substr(6, fileName.size() - 11);
			if (stem.empty()) {
				return {};
			}

			return "gateway." + ReplaceUnderscoresWithDots(stem);
		}

		bool ValidateDecodedResponseCase(
			const std::filesystem::path& fixturePath,
			const std::string& method,
			const ResponseFrame& response,
			std::string& error) {
			SchemaValidationIssue issue;
			if (!GatewayProtocolSchemaValidator::ValidateResponseForMethod(method, response, issue)) {
				error = "Response schema validation failed for " + method + " (" + fixturePath.filename().string() + "): " + issue.message;
				return false;
			}

			return true;
		}

		bool ValidateDecodedEventCase(
			const std::filesystem::path& fixturePath,
			const EventFrame& event,
			std::string& error) {
			SchemaValidationIssue issue;
			if (!GatewayProtocolSchemaValidator::ValidateEvent(event, issue)) {
				error = "Event schema validation failed for " + event.eventName + " (" + fixturePath.filename().string() + "): " + issue.message;
				return false;
			}

			return true;
		}

		bool ValidateNegativeResponseCase(
			const std::string& method,
			const ResponseFrame& response,
			const std::string& label,
			std::string& error) {
			SchemaValidationIssue issue;
			if (GatewayProtocolSchemaValidator::ValidateResponseForMethod(method, response, issue)) {
				error = "Schema response negative case unexpectedly passed for " + label + ".";
				return false;
			}

			return true;
		}

	} // namespace

	bool GatewayProtocolContract::ValidateFixtureParity(const std::string& fixtureRoot, std::string& error) {
		const std::filesystem::path root(fixtureRoot);

		const std::string requestFixture = TrimBoundaryWhitespace(ReadFileText(root / "request_ping.json"));
		RequestFrame decodedRequest;
		std::string decodeError;
		if (!TryDecodeRequestFrame(requestFixture, decodedRequest, decodeError)) {
			error = "Request frame decode failed: " + decodeError;
			return false;
		}

		if (decodedRequest.id != "req-1" || decodedRequest.method != "gateway.ping") {
			error = "Decoded request frame does not match canonical request fixture.";
			return false;
		}

		if (!decodedRequest.paramsJson.has_value() || decodedRequest.paramsJson.value() != "{\"echo\":\"hello\"}") {
			error = "Decoded request params do not match canonical request fixture.";
			return false;
		}

		if (!CompareFixture(root / "request_ping.json", SerializeRequestFrame(decodedRequest), error)) {
			return false;
		}

		const std::string invalidProtocolParamsRequestFixture =
			TrimBoundaryWhitespace(ReadFileText(root / "request_invalid_protocol_params.json"));
		RequestFrame invalidProtocolParamsRequest;
		std::string invalidDecodeError;
		if (!TryDecodeRequestFrame(
			invalidProtocolParamsRequestFixture,
			invalidProtocolParamsRequest,
			invalidDecodeError)) {
			error = "Invalid protocol params request decode failed: " + invalidDecodeError;
			return false;
		}

		SchemaValidationIssue validationIssue;
		if (GatewayProtocolSchemaValidator::ValidateRequest(invalidProtocolParamsRequest, validationIssue)) {
			error = "Invalid protocol params request unexpectedly passed schema validation.";
			return false;
		}

		const ResponseFrame invalidProtocolParamsResponse{
			.id = invalidProtocolParamsRequest.id,
			.ok = false,
			.payloadJson = std::nullopt,
			.error = ErrorShape{
				.code = validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code,
				.message = validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message,
				.detailsJson = "{\"method\":\"" + invalidProtocolParamsRequest.method + "\"}",
				.retryable = false,
				.retryAfterMs = std::nullopt,
			},
		};

		if (!CompareFixture(
			root / "response_invalid_protocol_params.json",
			SerializeResponseFrame(invalidProtocolParamsResponse),
			error)) {
			return false;
		}

		const ResponseFrame runtimeTaskDeltasGetResponse{
			.id = "runtime-taskdeltas-get-1",
			.ok = true,
			.payloadJson =
				"{\"runId\":\"chat-run-orch-1\",\"taskDeltas\":[{\"index\":0,\"runId\":\"chat-run-orch-1\",\"sessionId\":\"main\",\"phase\":\"plan\",\"toolName\":\"\",\"argsJson\":\"\",\"resultJson\":\"[]\",\"status\":\"planned\",\"errorCode\":\"\",\"startedAtMs\":1,\"completedAtMs\":1,\"latencyMs\":0,\"modelTurnId\":\"\",\"stepLabel\":\"execution_plan\"},{\"index\":1,\"runId\":\"chat-run-orch-1\",\"sessionId\":\"main\",\"phase\":\"final\",\"toolName\":\"\",\"argsJson\":\"\",\"resultJson\":\"done\",\"status\":\"completed\",\"errorCode\":\"\",\"startedAtMs\":2,\"completedAtMs\":2,\"latencyMs\":0,\"modelTurnId\":\"\",\"stepLabel\":\"run_terminal\"}],\"count\":2}",
			.error = std::nullopt,
		};
		if (!ValidateDecodedResponseCase(
			root / "response_gateway_runtime_taskDeltas_get.json",
			"gateway.runtime.taskDeltas.get",
			runtimeTaskDeltasGetResponse,
			error)) {
			return false;
		}

		const ResponseFrame runtimeTaskDeltasClearResponse{
			.id = "runtime-taskdeltas-clear-1",
			.ok = true,
			.payloadJson =
				"{\"runId\":\"chat-run-orch-1\",\"cleared\":1,\"remaining\":0}",
			.error = std::nullopt,
		};
		if (!ValidateDecodedResponseCase(
			root / "response_gateway_runtime_taskDeltas_clear.json",
			"gateway.runtime.taskDeltas.clear",
			runtimeTaskDeltasClearResponse,
			error)) {
			return false;
		}

		const ResponseFrame chatAbortResponse{
			.id = "chat-abort-1",
			.ok = true,
			.payloadJson =
				"{\"aborted\":true,\"runId\":\"chat-run-orch-1\",\"sessionKey\":\"main\"}",
			.error = std::nullopt,
		};
		if (!ValidateDecodedResponseCase(
			root / "response_chat_abort_orchestration.json",
			"chat.abort",
			chatAbortResponse,
			error)) {
			return false;
		}

		const ResponseFrame runtimeOrchestrationStatusResponse{
			.id = "runtime-orch-status-1",
			.ok = true,
			.payloadJson =
				"{\"state\":\"idle\",\"activeSession\":\"main\",\"activeAgent\":\"default\",\"queueDepth\":0,\"running\":0,\"capacity\":8,\"dynamicLoopMetrics\":{\"success\":1,\"failure\":0,\"timeout\":0,\"cancelled\":0,\"fallback\":0}}",
			.error = std::nullopt,
		};
		if (!ValidateDecodedResponseCase(
			root / "response_gateway_runtime_orchestration_status.json",
			"gateway.runtime.orchestration.status",
			runtimeOrchestrationStatusResponse,
			error)) {
			return false;
		}

		const ResponseFrame runtimeHealthDependenciesResponse{
			.id = "runtime-health-dependencies-1",
			.ok = true,
			.payloadJson =
				"{\"probes\":[{\"key\":\"backend:himalaya\",\"state\":\"ready\",\"reasonCode\":\"ok\",\"reasonMessage\":\"himalaya cli available\",\"checkedAtEpochMs\":1735689600000,\"expiresAtEpochMs\":1735689660000},{\"key\":\"runtime:node\",\"state\":\"ready\",\"reasonCode\":\"ok\",\"reasonMessage\":\"node runtime available\",\"checkedAtEpochMs\":1735689600000,\"expiresAtEpochMs\":1735689660000}],\"count\":2,\"generatedAtEpochMs\":1735689600000,\"ttlMs\":60000}",
			.error = std::nullopt,
		};
		if (!ValidateDecodedResponseCase(
			root / "response_gateway_runtime_health_dependencies.json",
			"gateway.runtime.health.dependencies",
			runtimeHealthDependenciesResponse,
			error)) {
			return false;
		}

		const ResponseFrame runtimeHealthCapabilitiesResponse{
			.id = "runtime-health-capabilities-1",
			.ok = true,
			.payloadJson =
				"{\"capabilities\":[{\"name\":\"email.send\",\"state\":\"ready\"}],\"count\":1,\"generatedAtEpochMs\":1735689600000,\"ttlMs\":60000}",
			.error = std::nullopt,
		};
		if (!ValidateDecodedResponseCase(
			root / "response_gateway_runtime_health_capabilities.json",
			"gateway.runtime.health.capabilities",
			runtimeHealthCapabilitiesResponse,
			error)) {
			return false;
		}

		std::vector<std::filesystem::path> responseFixtures;
		std::vector<std::filesystem::path> eventFixtures;
		for (const auto& entry : std::filesystem::directory_iterator(root)) {
			if (!entry.is_regular_file()) {
				continue;
			}

			const std::string name = entry.path().filename().string();
			if (StartsWith(name, "response_") && name != "response_invalid_protocol_params.json") {
				responseFixtures.push_back(entry.path());
				continue;
			}

			if (StartsWith(name, "event_")) {
				eventFixtures.push_back(entry.path());
			}
		}

		std::sort(responseFixtures.begin(), responseFixtures.end());
		std::sort(eventFixtures.begin(), eventFixtures.end());

		for (const auto& fixturePath : responseFixtures) {
			const std::string fixtureText = TrimBoundaryWhitespace(ReadFileText(fixturePath));
			ResponseFrame decodedResponse;
			std::string responseDecodeError;
			if (!TryDecodeResponseFrame(fixtureText, decodedResponse, responseDecodeError)) {
				error = "Response frame decode failed for " + fixturePath.filename().string() + ": " + responseDecodeError;
				return false;
			}

			const std::string method = InferMethodFromResponseFixtureName(fixturePath.filename().string());
			if (method.empty()) {
				error = "Could not infer method from fixture name: " + fixturePath.filename().string();
				return false;
			}

			if (!ValidateDecodedResponseCase(fixturePath, method, decodedResponse, error)) {
				return false;
			}

			if (!CompareFixture(fixturePath, SerializeResponseFrame(decodedResponse), error)) {
				return false;
			}
		}

		for (const auto& fixturePath : eventFixtures) {
			const std::string fixtureText = TrimBoundaryWhitespace(ReadFileText(fixturePath));
			EventFrame decodedEvent;
			std::string eventDecodeError;
			if (!TryDecodeEventFrame(fixtureText, decodedEvent, eventDecodeError)) {
				error = "Event frame decode failed for " + fixturePath.filename().string() + ": " + eventDecodeError;
				return false;
			}

			const std::string expectedEventName = InferEventNameFromEventFixtureName(fixturePath.filename().string());
			if (expectedEventName.empty() || decodedEvent.eventName != expectedEventName) {
				error = "Decoded event name mismatch for fixture: " + fixturePath.filename().string();
				return false;
			}

			if (!ValidateDecodedEventCase(fixturePath, decodedEvent, error)) {
				return false;
			}

			if (!CompareFixture(fixturePath, SerializeEventFrame(decodedEvent), error)) {
				return false;
			}
		}

		const ResponseFrame chatSendOrchestrationResponse{
			.id = "chat-send-orch-1",
			.ok = true,
			.payloadJson =
				"{\"runId\":\"chat-run-orch-1\",\"backendErrorCode\":null,\"queued\":true,\"deduped\":false}",
			.error = std::nullopt,
		};
		if (!ValidateDecodedResponseCase(
			root / "response_chat_send_orchestration.json",
			"chat.send",
			chatSendOrchestrationResponse,
			error)) {
			return false;
		}

		const ResponseFrame chatEventsOrchestrationPollResponse{
			.id = "chat-poll-orch-1",
			.ok = true,
			.payloadJson =
				"{\"sessionKey\":\"main\",\"events\":[{\"runId\":\"chat-run-orch-1\",\"sessionKey\":\"main\",\"state\":\"delta\",\"timestamp\":1,\"message\":{\"role\":\"assistant\",\"text\":\"tools.execute.start tool=weather.lookup\"}},{\"runId\":\"chat-run-orch-1\",\"sessionKey\":\"main\",\"state\":\"final\",\"timestamp\":2,\"message\":{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"Email scheduling is pending approval.\"}],\"timestamp\":2}}],\"count\":2}",
			.error = std::nullopt,
		};
		if (!ValidateDecodedResponseCase(
			root / "response_chat_events_poll_orchestration.json",
			"chat.events.poll",
			chatEventsOrchestrationPollResponse,
			error)) {
			return false;
		}

		const std::array<ResponseFrame, 362> negativeResponses = {
			  ResponseFrame{.id = "neg-1", .ok = true, .payloadJson = "{\"accounts\":[{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true}]}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-2", .ok = true, .payloadJson = "{\"session\":{\"id\":\"thread-1\",\"scope\":\"thread\",\"active\":false},\"deleted\":true}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-3", .ok = true, .payloadJson = "{\"tool\":\"chat.send\",\"executed\":true,\"status\":\"ok\",\"argsProvided\":false}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-4", .ok = true, .payloadJson = "{\"count\":2,\"succeeded\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-5", .ok = true, .payloadJson = "{\"found\":true,\"count\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-6", .ok = true, .payloadJson = "{\"cleared\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-7", .ok = true, .payloadJson = "{\"queueLoad\":0,\"agentLoad\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-8", .ok = true, .payloadJson = "{\"bufferedFrames\":0,\"highWatermark\":16}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-9", .ok = true, .payloadJson = "{\"active\":false,\"model\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-10", .ok = true, .payloadJson = "{\"saturation\":0,\"capacity\":8}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-11", .ok = true, .payloadJson = "{\"limitPerSec\":120,\"currentPerSec\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-12", .ok = true, .payloadJson = "{\"cleared\":true,\"active\":false}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-13", .ok = true, .payloadJson = "{\"pressure\":0,\"threshold\":80}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-14", .ok = true, .payloadJson = "{\"paceMs\":50,\"burst\":1}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-15", .ok = true, .payloadJson = "{\"active\":false,\"model\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-16", .ok = true, .payloadJson = "{\"headroom\":8,\"used\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-17", .ok = true, .payloadJson = "{\"jitterMs\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-18", .ok = true, .payloadJson = "{\"entries\":0,\"lastModel\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-19", .ok = true, .payloadJson = "{\"balanced\":true,\"skew\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-20", .ok = true, .payloadJson = "{\"driftMs\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-21", .ok = true, .payloadJson = "{\"active\":false,\"switches\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-22", .ok = true, .payloadJson = "{\"efficiency\":100,\"waste\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-23", .ok = true, .payloadJson = "{\"variance\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-24", .ok = true, .payloadJson = "{\"active\":false,\"windowSec\":60}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-25", .ok = true, .payloadJson = "{\"utilization\":0,\"capacity\":8}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-26", .ok = true, .payloadJson = "{\"deviation\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-27", .ok = true, .payloadJson = "{\"active\":false,\"digest\":\"sha256:override-v1\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-28", .ok = true, .payloadJson = "{\"capacity\":8,\"used\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-29", .ok = true, .payloadJson = "{\"aligned\":true,\"offsetMs\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-30", .ok = true, .payloadJson = "{\"entries\":0,\"active\":false}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-31", .ok = true, .payloadJson = "{\"occupancy\":0,\"slots\":8}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-32", .ok = true, .payloadJson = "{\"skewMs\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-33", .ok = true, .payloadJson = "{\"active\":false,\"count\":1}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-34", .ok = true, .payloadJson = "{\"elasticity\":100,\"headroom\":8}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-35", .ok = true, .payloadJson = "{\"dispersion\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-36", .ok = true, .payloadJson = "{\"active\":false,\"entries\":1}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-37", .ok = true, .payloadJson = "{\"cohesion\":100,\"groups\":1}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-38", .ok = true, .payloadJson = "{\"curvature\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-39", .ok = true, .payloadJson = "{\"active\":false,\"rows\":1}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-40", .ok = true, .payloadJson = "{\"resilience\":100,\"faults\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-41", .ok = true, .payloadJson = "{\"smoothness\":100,\"jitterMs\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-42", .ok = true, .payloadJson = "{\"active\":false,\"revision\":1}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-43", .ok = true, .payloadJson = "{\"ready\":true,\"queueDepth\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-44", .ok = true, .payloadJson = "{\"harmonics\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-45", .ok = true, .payloadJson = "{\"active\":false,\"pointer\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-46", .ok = true, .payloadJson = "{\"contention\":0,\"waiters\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-47", .ok = true, .payloadJson = "{\"phase\":\"steady\",\"step\":1}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-48", .ok = true, .payloadJson = "{\"active\":false,\"state\":\"none\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-49", .ok = true, .payloadJson = "{\"fairness\":100,\"skew\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-50", .ok = true, .payloadJson = "{\"tempo\":1,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-51", .ok = true, .payloadJson = "{\"active\":false,\"profile\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-52", .ok = true, .payloadJson = "{\"equilibrium\":100,\"delta\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-53", .ok = true, .payloadJson = "{\"steady\":true,\"variance\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-54", .ok = true, .payloadJson = "{\"temporal\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-55", .ok = true, .payloadJson = "{\"consistent\":true,\"deviation\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-56", .ok = true, .payloadJson = "{\"active\":false,\"entries\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-57", .ok = true, .payloadJson = "{\"parity\":100,\"gap\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-58", .ok = true, .payloadJson = "{\"stabilityIndex\":100,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-59", .ok = true, .payloadJson = "{\"spectral\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-60", .ok = true, .payloadJson = "{\"floor\":0,\"ceiling\":100}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-61", .ok = true, .payloadJson = "{\"active\":false,\"checkpoint\":\"cp-override-1\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-62", .ok = true, .payloadJson = "{\"convergence\":100,\"drift\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-63", .ok = true, .payloadJson = "{\"hysteresis\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-64", .ok = true, .payloadJson = "{\"resonance\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-65", .ok = true, .payloadJson = "{\"vectors\":2,\"magnitude\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-66", .ok = true, .payloadJson = "{\"active\":false,\"baseline\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-67", .ok = true, .payloadJson = "{\"balanceIndex\":100,\"skew\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-68", .ok = true, .payloadJson = "{\"locked\":true,\"phase\":\"steady\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-69", .ok = true, .payloadJson = "{\"waveform\":\"flat\",\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-70", .ok = true, .payloadJson = "{\"horizonMs\":1000,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-71", .ok = true, .payloadJson = "{\"active\":false,\"manifest\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-72", .ok = true, .payloadJson = "{\"symmetry\":100,\"offset\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-73", .ok = true, .payloadJson = "{\"gradient\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-74", .ok = true, .payloadJson = "{\"clock\":1,\"lag\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-75", .ok = true, .payloadJson = "{\"trend\":\"flat\",\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-76", .ok = true, .payloadJson = "{\"active\":false,\"entries\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-77", .ok = true, .payloadJson = "{\"harmonicity\":100,\"detune\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-78", .ok = true, .payloadJson = "{\"inertia\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-79", .ok = true, .payloadJson = "{\"coordinated\":true,\"lag\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-80", .ok = true, .payloadJson = "{\"minMs\":0,\"maxMs\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-81", .ok = true, .payloadJson = "{\"active\":false,\"index\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-82", .ok = true, .payloadJson = "{\"cadenceIndex\":100,\"jitter\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-83", .ok = true, .payloadJson = "{\"damping\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-84", .ok = true, .payloadJson = "{\"phaseNoise\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-85", .ok = true, .payloadJson = "{\"beatHz\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-86", .ok = true, .payloadJson = "{\"active\":false,\"digestIndex\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-87", .ok = true, .payloadJson = "{\"locked\":true,\"phase\":\"steady\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-88", .ok = true, .payloadJson = "{\"flux\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-89", .ok = true, .payloadJson = "{\"modulation\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-90", .ok = true, .payloadJson = "{\"pulseHz\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-91", .ok = true, .payloadJson = "{\"active\":false,\"cursor\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-92", .ok = true, .payloadJson = "{\"vectors\":2,\"magnitude\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-93", .ok = true, .payloadJson = "{\"phase\":\"steady\",\"amplitude\":1}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-94", .ok = true, .payloadJson = "{\"cohesive\":true,\"delta\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-95", .ok = true, .payloadJson = "{\"waveIndex\":1,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-96", .ok = true, .payloadJson = "{\"active\":false,\"vector\":\"default\"}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-97", .ok = true, .payloadJson = "{\"vectorDrift\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-98", .ok = true, .payloadJson = "{\"phase\":\"steady\",\"bias\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-99", .ok = true, .payloadJson = "{\"syncBand\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-100", .ok = true, .payloadJson = "{\"waveDrift\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-101", .ok = true, .payloadJson = "{\"active\":false,\"vectorDrift\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-102", .ok = true, .payloadJson = "{\"vectorPhase\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-103", .ok = true, .payloadJson = "{\"biasDrift\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-104", .ok = true, .payloadJson = "{\"syncDrift\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-105", .ok = true, .payloadJson = "{\"bandStability\":100,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-106", .ok = true, .payloadJson = "{\"active\":false,\"phaseBias\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-107", .ok = true, .payloadJson = "{\"phaseVector\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-108", .ok = true, .payloadJson = "{\"biasEnvelope\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-109", .ok = true, .payloadJson = "{\"syncEnvelope\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-110", .ok = true, .payloadJson = "{\"bandDrift\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-111", .ok = true, .payloadJson = "{\"active\":false,\"biasEnvelope\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-112", .ok = true, .payloadJson = "{\"phaseLattice\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-113", .ok = true, .payloadJson = "{\"envelopeDrift\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-114", .ok = true, .payloadJson = "{\"syncMatrix\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-115", .ok = true, .payloadJson = "{\"bandVector\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-116", .ok = true, .payloadJson = "{\"active\":false,\"envelopeDrift\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-117", .ok = true, .payloadJson = "{\"phaseContour\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-118", .ok = true, .payloadJson = "{\"driftVector\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-119", .ok = true, .payloadJson = "{\"syncContour\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-120", .ok = true, .payloadJson = "{\"bandMatrix\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-121", .ok = true, .payloadJson = "{\"active\":false,\"driftVector\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-122", .ok = true, .payloadJson = "{\"phaseRibbon\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-123", .ok = true, .payloadJson = "{\"vectorEnvelope\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-124", .ok = true, .payloadJson = "{\"syncRibbon\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-125", .ok = true, .payloadJson = "{\"bandContour\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-126", .ok = true, .payloadJson = "{\"active\":false,\"vectorEnvelope\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-127", .ok = true, .payloadJson = "{\"phaseSpiral\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-128", .ok = true, .payloadJson = "{\"vectorRibbon\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-129", .ok = true, .payloadJson = "{\"syncSpiral\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-130", .ok = true, .payloadJson = "{\"bandHelix\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-131", .ok = true, .payloadJson = "{\"active\":false,\"vectorRibbon\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-132", .ok = true, .payloadJson = "{\"phaseMesh\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-133", .ok = true, .payloadJson = "{\"vectorArc\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-134", .ok = true, .payloadJson = "{\"syncMesh\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-135", .ok = true, .payloadJson = "{\"bandLattice\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-136", .ok = true, .payloadJson = "{\"active\":false,\"vectorArc\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-137", .ok = true, .payloadJson = "{\"phaseFabric\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-138", .ok = true, .payloadJson = "{\"vectorMesh\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-139", .ok = true, .payloadJson = "{\"syncFabric\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-140", .ok = true, .payloadJson = "{\"bandArc\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-141", .ok = true, .payloadJson = "{\"active\":false,\"vectorMesh\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-142", .ok = true, .payloadJson = "{\"phaseNet\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-143", .ok = true, .payloadJson = "{\"vectorNode\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-144", .ok = true, .payloadJson = "{\"syncNet\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-145", .ok = true, .payloadJson = "{\"bandNode\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-146", .ok = true, .payloadJson = "{\"active\":false,\"vectorNode\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-147", .ok = true, .payloadJson = "{\"phaseCore\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-148", .ok = true, .payloadJson = "{\"vectorCore\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-149", .ok = true, .payloadJson = "{\"syncCore\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-150", .ok = true, .payloadJson = "{\"bandCore\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-151", .ok = true, .payloadJson = "{\"active\":false,\"vectorCore\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-152", .ok = true, .payloadJson = "{\"phaseFrame\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-153", .ok = true, .payloadJson = "{\"vectorFrame\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-154", .ok = true, .payloadJson = "{\"syncFrame\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-155", .ok = true, .payloadJson = "{\"bandFrame\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-156", .ok = true, .payloadJson = "{\"active\":false,\"vectorFrame\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-157", .ok = true, .payloadJson = "{\"phaseSpan\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-158", .ok = true, .payloadJson = "{\"vectorSpan\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-159", .ok = true, .payloadJson = "{\"syncSpan\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-160", .ok = true, .payloadJson = "{\"bandSpan\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-161", .ok = true, .payloadJson = "{\"active\":false,\"vectorSpan\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-162", .ok = true, .payloadJson = "{\"phaseGrid\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-163", .ok = true, .payloadJson = "{\"vectorGrid\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-164", .ok = true, .payloadJson = "{\"syncGrid\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-165", .ok = true, .payloadJson = "{\"bandGrid\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-166", .ok = true, .payloadJson = "{\"active\":false,\"vectorGrid\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-167", .ok = true, .payloadJson = "{\"phaseLane\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-168", .ok = true, .payloadJson = "{\"vectorLane\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-169", .ok = true, .payloadJson = "{\"syncLane\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-170", .ok = true, .payloadJson = "{\"bandLane\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-171", .ok = true, .payloadJson = "{\"active\":false,\"vectorLane\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-172", .ok = true, .payloadJson = "{\"phaseTrack\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-173", .ok = true, .payloadJson = "{\"vectorTrack\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-174", .ok = true, .payloadJson = "{\"syncTrack\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-175", .ok = true, .payloadJson = "{\"bandTrack\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-176", .ok = true, .payloadJson = "{\"active\":false,\"vectorTrack\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-177", .ok = true, .payloadJson = "{\"phaseRail\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-178", .ok = true, .payloadJson = "{\"vectorRail\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-179", .ok = true, .payloadJson = "{\"syncRail\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-180", .ok = true, .payloadJson = "{\"bandRail\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-181", .ok = true, .payloadJson = "{\"active\":false,\"vectorRail\":0}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-182", .ok = true, .payloadJson = "{\"phaseSpline\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-183", .ok = true, .payloadJson = "{\"vectorSpline\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-184", .ok = true, .payloadJson = "{\"syncSpline\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-185", .ok = true, .payloadJson = "{\"bandSpline\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-186", .ok = true, .payloadJson = "{\"active\":false,\"vectorSpline\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-187", .ok = true, .payloadJson = "{\"phaseChain\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-188", .ok = true, .payloadJson = "{\"vectorChain\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-189", .ok = true, .payloadJson = "{\"syncChain\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-190", .ok = true, .payloadJson = "{\"bandChain\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-191", .ok = true, .payloadJson = "{\"active\":false,\"vectorChain\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-192", .ok = true, .payloadJson = "{\"phaseThread\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-193", .ok = true, .payloadJson = "{\"vectorThread\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-194", .ok = true, .payloadJson = "{\"syncThread\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-195", .ok = true, .payloadJson = "{\"bandThread\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-196", .ok = true, .payloadJson = "{\"active\":false,\"vectorThread\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-197", .ok = true, .payloadJson = "{\"phaseLink\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-198", .ok = true, .payloadJson = "{\"vectorLink\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-199", .ok = true, .payloadJson = "{\"syncLink\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-200", .ok = true, .payloadJson = "{\"bandLink\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-201", .ok = true, .payloadJson = "{\"active\":false,\"vectorLink\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-202", .ok = true, .payloadJson = "{\"phaseNode\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-203", .ok = true, .payloadJson = "{\"vectorNode2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-204", .ok = true, .payloadJson = "{\"syncNode2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-205", .ok = true, .payloadJson = "{\"bandNode2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-206", .ok = true, .payloadJson = "{\"active\":false,\"vectorNode2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-207", .ok = true, .payloadJson = "{\"phaseBridge\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-208", .ok = true, .payloadJson = "{\"vectorBridge\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-209", .ok = true, .payloadJson = "{\"syncBridge\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-210", .ok = true, .payloadJson = "{\"bandBridge\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-211", .ok = true, .payloadJson = "{\"active\":false,\"vectorBridge\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-212", .ok = true, .payloadJson = "{\"phasePortal\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-213", .ok = true, .payloadJson = "{\"vectorPortal\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-214", .ok = true, .payloadJson = "{\"syncPortal\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-215", .ok = true, .payloadJson = "{\"bandPortal\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-216", .ok = true, .payloadJson = "{\"active\":false,\"vectorPortal\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-217", .ok = true, .payloadJson = "{\"phaseRelay2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-218", .ok = true, .payloadJson = "{\"vectorRelay2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-219", .ok = true, .payloadJson = "{\"syncRelay2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-220", .ok = true, .payloadJson = "{\"bandRelay2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-221", .ok = true, .payloadJson = "{\"active\":false,\"vectorRelay2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-222", .ok = true, .payloadJson = "{\"phaseGate2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-223", .ok = true, .payloadJson = "{\"vectorGate2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-224", .ok = true, .payloadJson = "{\"syncGate2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-225", .ok = true, .payloadJson = "{\"bandGate2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-226", .ok = true, .payloadJson = "{\"active\":false,\"vectorGate2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-227", .ok = true, .payloadJson = "{\"phaseHub2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-228", .ok = true, .payloadJson = "{\"vectorHub2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-229", .ok = true, .payloadJson = "{\"syncHub2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-230", .ok = true, .payloadJson = "{\"bandHub2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-231", .ok = true, .payloadJson = "{\"active\":false,\"vectorHub2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-232", .ok = true, .payloadJson = "{\"phaseNode3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-233", .ok = true, .payloadJson = "{\"vectorNode3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-234", .ok = true, .payloadJson = "{\"syncNode3\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-235", .ok = true, .payloadJson = "{\"bandNode3\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-236", .ok = true, .payloadJson = "{\"active\":false,\"vectorNode3\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-237", .ok = true, .payloadJson = "{\"phaseLink2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-238", .ok = true, .payloadJson = "{\"vectorLink2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-239", .ok = true, .payloadJson = "{\"syncLink2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-240", .ok = true, .payloadJson = "{\"bandLink2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-241", .ok = true, .payloadJson = "{\"active\":false,\"vectorLink2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-242", .ok = true, .payloadJson = "{\"phaseMesh2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-243", .ok = true, .payloadJson = "{\"vectorMesh2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-244", .ok = true, .payloadJson = "{\"syncMesh2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-245", .ok = true, .payloadJson = "{\"bandMesh2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-246", .ok = true, .payloadJson = "{\"active\":false,\"vectorMesh2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-247", .ok = true, .payloadJson = "{\"phaseArc2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-248", .ok = true, .payloadJson = "{\"vectorArc2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-249", .ok = true, .payloadJson = "{\"syncArc2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-250", .ok = true, .payloadJson = "{\"bandArc2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-251", .ok = true, .payloadJson = "{\"active\":false,\"vectorArc2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-252", .ok = true, .payloadJson = "{\"phaseBand2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-253", .ok = true, .payloadJson = "{\"vectorBand2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-254", .ok = true, .payloadJson = "{\"syncBand2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-255", .ok = true, .payloadJson = "{\"bandBand2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-256", .ok = true, .payloadJson = "{\"active\":false,\"vectorBand2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-257", .ok = true, .payloadJson = "{\"phaseGrid2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-258", .ok = true, .payloadJson = "{\"vectorGrid2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-259", .ok = true, .payloadJson = "{\"syncGrid2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-260", .ok = true, .payloadJson = "{\"bandGrid2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-261", .ok = true, .payloadJson = "{\"active\":false,\"vectorGrid2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-262", .ok = true, .payloadJson = "{\"phaseLane2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-263", .ok = true, .payloadJson = "{\"vectorLane2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-264", .ok = true, .payloadJson = "{\"syncLane2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-265", .ok = true, .payloadJson = "{\"bandLane2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-266", .ok = true, .payloadJson = "{\"active\":false,\"vectorLane2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-267", .ok = true, .payloadJson = "{\"phaseTrack2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-268", .ok = true, .payloadJson = "{\"vectorTrack2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-269", .ok = true, .payloadJson = "{\"syncTrack2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-270", .ok = true, .payloadJson = "{\"bandTrack2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-271", .ok = true, .payloadJson = "{\"active\":false,\"vectorTrack2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-272", .ok = true, .payloadJson = "{\"phaseRail2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-273", .ok = true, .payloadJson = "{\"vectorRail2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-274", .ok = true, .payloadJson = "{\"syncRail2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-275", .ok = true, .payloadJson = "{\"bandRail2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-276", .ok = true, .payloadJson = "{\"active\":false,\"vectorRail2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-277", .ok = true, .payloadJson = "{\"phaseSpline2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-278", .ok = true, .payloadJson = "{\"vectorSpline2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-279", .ok = true, .payloadJson = "{\"syncSpline2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-280", .ok = true, .payloadJson = "{\"bandSpline2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-281", .ok = true, .payloadJson = "{\"active\":false,\"vectorSpline2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-282", .ok = true, .payloadJson = "{\"phaseChain2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-283", .ok = true, .payloadJson = "{\"vectorChain2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-284", .ok = true, .payloadJson = "{\"syncChain2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-285", .ok = true, .payloadJson = "{\"bandChain2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-286", .ok = true, .payloadJson = "{\"active\":false,\"vectorChain2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-287", .ok = true, .payloadJson = "{\"phaseThread2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-288", .ok = true, .payloadJson = "{\"vectorThread2\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-289", .ok = true, .payloadJson = "{\"syncThread2\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-290", .ok = true, .payloadJson = "{\"bandThread2\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-291", .ok = true, .payloadJson = "{\"active\":false,\"vectorThread2\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-292", .ok = true, .payloadJson = "{\"phaseLink3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-293", .ok = true, .payloadJson = "{\"vectorLink3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-294", .ok = true, .payloadJson = "{\"syncLink3\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-295", .ok = true, .payloadJson = "{\"bandLink3\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-296", .ok = true, .payloadJson = "{\"active\":false,\"vectorLink3\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-297", .ok = true, .payloadJson = "{\"phaseNode4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-298", .ok = true, .payloadJson = "{\"vectorNode4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-299", .ok = true, .payloadJson = "{\"syncNode4\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-300", .ok = true, .payloadJson = "{\"bandNode4\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-301", .ok = true, .payloadJson = "{\"active\":false,\"vectorNode4\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-302", .ok = true, .payloadJson = "{\"phaseMesh3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-303", .ok = true, .payloadJson = "{\"vectorMesh3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-304", .ok = true, .payloadJson = "{\"syncMesh3\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-305", .ok = true, .payloadJson = "{\"bandMesh3\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-306", .ok = true, .payloadJson = "{\"active\":false,\"vectorMesh3\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-307", .ok = true, .payloadJson = "{\"phaseBridge3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-308", .ok = true, .payloadJson = "{\"vectorBridge3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-309", .ok = true, .payloadJson = "{\"syncBridge3\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-310", .ok = true, .payloadJson = "{\"bandBridge3\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-311", .ok = true, .payloadJson = "{\"active\":false,\"vectorBridge3\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-312", .ok = true, .payloadJson = "{\"phasePortal3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-313", .ok = true, .payloadJson = "{\"vectorPortal3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-314", .ok = true, .payloadJson = "{\"syncPortal3\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-315", .ok = true, .payloadJson = "{\"bandPortal3\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-316", .ok = true, .payloadJson = "{\"active\":false,\"vectorPortal3\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-317", .ok = true, .payloadJson = "{\"phaseRelay3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-318", .ok = true, .payloadJson = "{\"vectorRelay3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-319", .ok = true, .payloadJson = "{\"syncRelay3\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-320", .ok = true, .payloadJson = "{\"bandRelay3\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-321", .ok = true, .payloadJson = "{\"active\":false,\"vectorRelay3\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-322", .ok = true, .payloadJson = "{\"phaseGate3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-323", .ok = true, .payloadJson = "{\"vectorGate3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-324", .ok = true, .payloadJson = "{\"syncGate3\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-325", .ok = true, .payloadJson = "{\"bandGate3\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-326", .ok = true, .payloadJson = "{\"active\":false,\"vectorGate3\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-327", .ok = true, .payloadJson = "{\"phaseHub3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-328", .ok = true, .payloadJson = "{\"vectorHub3\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-329", .ok = true, .payloadJson = "{\"syncHub3\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-330", .ok = true, .payloadJson = "{\"bandHub3\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-331", .ok = true, .payloadJson = "{\"active\":false,\"vectorHub3\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-332", .ok = true, .payloadJson = "{\"phaseNode5\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-333", .ok = true, .payloadJson = "{\"vectorNode5\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-334", .ok = true, .payloadJson = "{\"syncNode5\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-335", .ok = true, .payloadJson = "{\"bandNode5\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-336", .ok = true, .payloadJson = "{\"active\":false,\"vectorNode5\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-337", .ok = true, .payloadJson = "{\"phaseLink4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-338", .ok = true, .payloadJson = "{\"vectorLink4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-339", .ok = true, .payloadJson = "{\"syncLink4\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-340", .ok = true, .payloadJson = "{\"bandLink4\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-341", .ok = true, .payloadJson = "{\"active\":false,\"vectorLink4\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-342", .ok = true, .payloadJson = "{\"phaseBridge4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-343", .ok = true, .payloadJson = "{\"vectorBridge4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-344", .ok = true, .payloadJson = "{\"syncBridge4\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-345", .ok = true, .payloadJson = "{\"bandBridge4\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-346", .ok = true, .payloadJson = "{\"active\":false,\"vectorBridge4\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-347", .ok = true, .payloadJson = "{\"phasePortal4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-348", .ok = true, .payloadJson = "{\"vectorPortal4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-349", .ok = true, .payloadJson = "{\"syncPortal4\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-350", .ok = true, .payloadJson = "{\"bandPortal4\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-351", .ok = true, .payloadJson = "{\"active\":false,\"vectorPortal4\":0}", .error = std::nullopt },
			ResponseFrame{.id = "neg-352", .ok = true, .payloadJson = "{\"phaseGate4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-353", .ok = true, .payloadJson = "{\"vectorGate4\":0,\"windowMs\":1000}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-354", .ok = true, .payloadJson = "{\"syncGate4\":1,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-355", .ok = true, .payloadJson = "{\"bandGate4\":0,\"samples\":2}", .error = std::nullopt },
			  ResponseFrame{.id = "neg-356", .ok = true, .payloadJson = "{\"active\":false,\"vectorGate4\":0}", .error = std::nullopt },
		};

		if (!ValidateNegativeResponseCase("gateway.channels.accounts", negativeResponses[0], "gateway.channels.accounts missing `connected`", error) ||
			!ValidateNegativeResponseCase("gateway.sessions.delete", negativeResponses[1], "gateway.sessions.delete missing `remaining`", error) ||
			!ValidateNegativeResponseCase("gateway.tools.call.execute", negativeResponses[2], "gateway.tools.call.execute missing `output`", error) ||
			!ValidateNegativeResponseCase("gateway.tools.executions.count", negativeResponses[3], "gateway.tools.executions.count missing `failed`", error) ||
			!ValidateNegativeResponseCase("gateway.tools.executions.latest", negativeResponses[4], "gateway.tools.executions.latest missing `execution`", error) ||
			!ValidateNegativeResponseCase("gateway.tools.executions.clear", negativeResponses[5], "gateway.tools.executions.clear missing `remaining`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.load", negativeResponses[6], "gateway.runtime.orchestration.load missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.buffer", negativeResponses[7], "gateway.runtime.streaming.buffer missing `bufferedBytes`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override", negativeResponses[8], "gateway.models.failover.override missing `reason`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.saturation", negativeResponses[9], "gateway.runtime.orchestration.saturation missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.throttle", negativeResponses[10], "gateway.runtime.streaming.throttle missing `throttled`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.clear", negativeResponses[11], "gateway.models.failover.override.clear missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.pressure", negativeResponses[12], "gateway.runtime.orchestration.pressure missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.pacing", negativeResponses[13], "gateway.runtime.streaming.pacing missing `adaptive`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.status", negativeResponses[14], "gateway.models.failover.override.status missing `source`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.headroom", negativeResponses[15], "gateway.runtime.orchestration.headroom missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.jitter", negativeResponses[16], "gateway.runtime.streaming.jitter missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.history", negativeResponses[17], "gateway.models.failover.override.history missing `active`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.balance", negativeResponses[18], "gateway.runtime.orchestration.balance missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.drift", negativeResponses[19], "gateway.runtime.streaming.drift missing `corrected`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.metrics", negativeResponses[20], "gateway.models.failover.override.metrics missing `lastModel`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.efficiency", negativeResponses[21], "gateway.runtime.orchestration.efficiency missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.variance", negativeResponses[22], "gateway.runtime.streaming.variance missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.window", negativeResponses[23], "gateway.models.failover.override.window missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.utilization", negativeResponses[24], "gateway.runtime.orchestration.utilization missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.deviation", negativeResponses[25], "gateway.runtime.streaming.deviation missing `withinBudget`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.digest", negativeResponses[26], "gateway.models.failover.override.digest missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.capacity", negativeResponses[27], "gateway.runtime.orchestration.capacity missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.alignment", negativeResponses[28], "gateway.runtime.streaming.alignment missing `windowMs`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.timeline", negativeResponses[29], "gateway.models.failover.override.timeline missing `lastModel`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.occupancy", negativeResponses[30], "gateway.runtime.orchestration.occupancy missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.skew", negativeResponses[31], "gateway.runtime.streaming.skew missing `bounded`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.catalog", negativeResponses[32], "gateway.models.failover.override.catalog missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.elasticity", negativeResponses[33], "gateway.runtime.orchestration.elasticity missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.dispersion", negativeResponses[34], "gateway.runtime.streaming.dispersion missing `bounded`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.registry", negativeResponses[35], "gateway.models.failover.override.registry missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.cohesion", negativeResponses[36], "gateway.runtime.orchestration.cohesion missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.curvature", negativeResponses[37], "gateway.runtime.streaming.curvature missing `bounded`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.matrix", negativeResponses[38], "gateway.models.failover.override.matrix missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.resilience", negativeResponses[39], "gateway.runtime.orchestration.resilience missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.smoothness", negativeResponses[40], "gateway.runtime.streaming.smoothness missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.snapshot", negativeResponses[41], "gateway.models.failover.override.snapshot missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.readiness", negativeResponses[42], "gateway.runtime.orchestration.readiness missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.harmonics", negativeResponses[43], "gateway.runtime.streaming.harmonics missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.pointer", negativeResponses[44], "gateway.models.failover.override.pointer missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.contention", negativeResponses[45], "gateway.runtime.orchestration.contention missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.phase", negativeResponses[46], "gateway.runtime.streaming.phase missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.state", negativeResponses[47], "gateway.models.failover.override.state missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.fairness", negativeResponses[48], "gateway.runtime.orchestration.fairness missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.tempo", negativeResponses[49], "gateway.runtime.streaming.tempo missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.profile", negativeResponses[50], "gateway.models.failover.override.profile missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.equilibrium", negativeResponses[51], "gateway.runtime.orchestration.equilibrium missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.steadiness", negativeResponses[52], "gateway.runtime.orchestration.steadiness missing `windowMs`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.temporal", negativeResponses[53], "gateway.runtime.streaming.temporal missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.consistency", negativeResponses[54], "gateway.runtime.streaming.consistency missing `samples`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.audit", negativeResponses[55], "gateway.models.failover.override.audit missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.parity", negativeResponses[56], "gateway.runtime.orchestration.parity missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.stabilityIndex", negativeResponses[57], "gateway.runtime.orchestration.stabilityIndex missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.spectral", negativeResponses[58], "gateway.runtime.streaming.spectral missing `bounded`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.envelope", negativeResponses[59], "gateway.runtime.streaming.envelope missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.checkpoint", negativeResponses[60], "gateway.models.failover.override.checkpoint missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.convergence", negativeResponses[61], "gateway.runtime.orchestration.convergence missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.hysteresis", negativeResponses[62], "gateway.runtime.orchestration.hysteresis missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.resonance", negativeResponses[63], "gateway.runtime.streaming.resonance missing `bounded`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.vectorField", negativeResponses[64], "gateway.runtime.streaming.vectorField missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.baseline", negativeResponses[65], "gateway.models.failover.override.baseline missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.balanceIndex", negativeResponses[66], "gateway.runtime.orchestration.balanceIndex missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseLock", negativeResponses[67], "gateway.runtime.orchestration.phaseLock missing `drift`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.waveform", negativeResponses[68], "gateway.runtime.streaming.waveform missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.horizon", negativeResponses[69], "gateway.runtime.streaming.horizon missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.manifest", negativeResponses[70], "gateway.models.failover.override.manifest missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.symmetry", negativeResponses[71], "gateway.runtime.orchestration.symmetry missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.gradient", negativeResponses[72], "gateway.runtime.orchestration.gradient missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.vectorClock", negativeResponses[73], "gateway.runtime.streaming.vectorClock missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.trend", negativeResponses[74], "gateway.runtime.streaming.trend missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.ledger", negativeResponses[75], "gateway.models.failover.override.ledger missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.harmonicity", negativeResponses[76], "gateway.runtime.orchestration.harmonicity missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.inertia", negativeResponses[77], "gateway.runtime.orchestration.inertia missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.coordination", negativeResponses[78], "gateway.runtime.streaming.coordination missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.latencyBand", negativeResponses[79], "gateway.runtime.streaming.latencyBand missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.snapshotIndex", negativeResponses[80], "gateway.models.failover.override.snapshotIndex missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.cadenceIndex", negativeResponses[81], "gateway.runtime.orchestration.cadenceIndex missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.damping", negativeResponses[82], "gateway.runtime.orchestration.damping missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.phaseNoise", negativeResponses[83], "gateway.runtime.streaming.phaseNoise missing `bounded`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.beat", negativeResponses[84], "gateway.runtime.streaming.beat missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.digestIndex", negativeResponses[85], "gateway.models.failover.override.digestIndex missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.waveLock", negativeResponses[86], "gateway.runtime.orchestration.waveLock missing `slip`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.flux", negativeResponses[87], "gateway.runtime.orchestration.flux missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.modulation", negativeResponses[88], "gateway.runtime.streaming.modulation missing `bounded`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.pulseTrain", negativeResponses[89], "gateway.runtime.streaming.pulseTrain missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.cursor", negativeResponses[90], "gateway.models.failover.override.cursor missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorField", negativeResponses[91], "gateway.runtime.orchestration.vectorField missing `state`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseEnvelope", negativeResponses[92], "gateway.runtime.orchestration.phaseEnvelope missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.cohesion", negativeResponses[93], "gateway.runtime.streaming.cohesion missing `samples`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.waveIndex", negativeResponses[94], "gateway.runtime.streaming.waveIndex missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vector", negativeResponses[95], "gateway.models.failover.override.vector missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorDrift", negativeResponses[96], "gateway.runtime.orchestration.vectorDrift missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseBias", negativeResponses[97], "gateway.runtime.orchestration.phaseBias missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncBand", negativeResponses[98], "gateway.runtime.streaming.syncBand missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.waveDrift", negativeResponses[99], "gateway.runtime.streaming.waveDrift missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorDrift", negativeResponses[100], "gateway.models.failover.override.vectorDrift missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorPhase", negativeResponses[101], "gateway.runtime.orchestration.vectorPhase missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.biasDrift", negativeResponses[102], "gateway.runtime.orchestration.biasDrift missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncDrift", negativeResponses[103], "gateway.runtime.streaming.syncDrift missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandStability", negativeResponses[104], "gateway.runtime.streaming.bandStability missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.phaseBias", negativeResponses[105], "gateway.models.failover.override.phaseBias missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseVector", negativeResponses[106], "gateway.runtime.orchestration.phaseVector missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.biasEnvelope", negativeResponses[107], "gateway.runtime.orchestration.biasEnvelope missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncEnvelope", negativeResponses[108], "gateway.runtime.streaming.syncEnvelope missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandDrift", negativeResponses[109], "gateway.runtime.streaming.bandDrift missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.biasEnvelope", negativeResponses[110], "gateway.models.failover.override.biasEnvelope missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseLattice", negativeResponses[111], "gateway.runtime.orchestration.phaseLattice missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.envelopeDrift", negativeResponses[112], "gateway.runtime.orchestration.envelopeDrift missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncMatrix", negativeResponses[113], "gateway.runtime.streaming.syncMatrix missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandVector", negativeResponses[114], "gateway.runtime.streaming.bandVector missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.envelopeDrift", negativeResponses[115], "gateway.models.failover.override.envelopeDrift missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseContour", negativeResponses[116], "gateway.runtime.orchestration.phaseContour missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.driftVector", negativeResponses[117], "gateway.runtime.orchestration.driftVector missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncContour", negativeResponses[118], "gateway.runtime.streaming.syncContour missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandMatrix", negativeResponses[119], "gateway.runtime.streaming.bandMatrix missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.driftVector", negativeResponses[120], "gateway.models.failover.override.driftVector missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseRibbon", negativeResponses[121], "gateway.runtime.orchestration.phaseRibbon missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorEnvelope", negativeResponses[122], "gateway.runtime.orchestration.vectorEnvelope missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncRibbon", negativeResponses[123], "gateway.runtime.streaming.syncRibbon missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandContour", negativeResponses[124], "gateway.runtime.streaming.bandContour missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorEnvelope", negativeResponses[125], "gateway.models.failover.override.vectorEnvelope missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseSpiral", negativeResponses[126], "gateway.runtime.orchestration.phaseSpiral missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorRibbon", negativeResponses[127], "gateway.runtime.orchestration.vectorRibbon missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncSpiral", negativeResponses[128], "gateway.runtime.streaming.syncSpiral missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandHelix", negativeResponses[129], "gateway.runtime.streaming.bandHelix missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorRibbon", negativeResponses[130], "gateway.models.failover.override.vectorRibbon missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseMesh", negativeResponses[131], "gateway.runtime.orchestration.phaseMesh missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorArc", negativeResponses[132], "gateway.runtime.orchestration.vectorArc missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncMesh", negativeResponses[133], "gateway.runtime.streaming.syncMesh missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandLattice", negativeResponses[134], "gateway.runtime.streaming.bandLattice missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorArc", negativeResponses[135], "gateway.models.failover.override.vectorArc missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseFabric", negativeResponses[136], "gateway.runtime.orchestration.phaseFabric missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorMesh", negativeResponses[137], "gateway.runtime.orchestration.vectorMesh missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncFabric", negativeResponses[138], "gateway.runtime.streaming.syncFabric missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandArc", negativeResponses[139], "gateway.runtime.streaming.bandArc missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorMesh", negativeResponses[140], "gateway.models.failover.override.vectorMesh missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseNet", negativeResponses[141], "gateway.runtime.orchestration.phaseNet missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorNode", negativeResponses[142], "gateway.runtime.orchestration.vectorNode missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncNet", negativeResponses[143], "gateway.runtime.streaming.syncNet missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandNode", negativeResponses[144], "gateway.runtime.streaming.bandNode missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorNode", negativeResponses[145], "gateway.models.failover.override.vectorNode missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseCore", negativeResponses[146], "gateway.runtime.orchestration.phaseCore missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorCore", negativeResponses[147], "gateway.runtime.orchestration.vectorCore missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncCore", negativeResponses[148], "gateway.runtime.streaming.syncCore missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandCore", negativeResponses[149], "gateway.runtime.streaming.bandCore missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorCore", negativeResponses[150], "gateway.models.failover.override.vectorCore missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseFrame", negativeResponses[151], "gateway.runtime.orchestration.phaseFrame missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorFrame", negativeResponses[152], "gateway.runtime.orchestration.vectorFrame missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncFrame", negativeResponses[153], "gateway.runtime.streaming.syncFrame missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandFrame", negativeResponses[154], "gateway.runtime.streaming.bandFrame missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorFrame", negativeResponses[155], "gateway.models.failover.override.vectorFrame missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseSpan", negativeResponses[156], "gateway.runtime.orchestration.phaseSpan missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorSpan", negativeResponses[157], "gateway.runtime.orchestration.vectorSpan missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncSpan", negativeResponses[158], "gateway.runtime.streaming.syncSpan missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandSpan", negativeResponses[159], "gateway.runtime.streaming.bandSpan missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorSpan", negativeResponses[160], "gateway.models.failover.override.vectorSpan missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseGrid", negativeResponses[161], "gateway.runtime.orchestration.phaseGrid missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorGrid", negativeResponses[162], "gateway.runtime.orchestration.vectorGrid missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncGrid", negativeResponses[163], "gateway.runtime.streaming.syncGrid missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandGrid", negativeResponses[164], "gateway.runtime.streaming.bandGrid missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorGrid", negativeResponses[165], "gateway.models.failover.override.vectorGrid missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseLane", negativeResponses[166], "gateway.runtime.orchestration.phaseLane missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorLane", negativeResponses[167], "gateway.runtime.orchestration.vectorLane missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncLane", negativeResponses[168], "gateway.runtime.streaming.syncLane missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandLane", negativeResponses[169], "gateway.runtime.streaming.bandLane missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorLane", negativeResponses[170], "gateway.models.failover.override.vectorLane missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseTrack", negativeResponses[171], "gateway.runtime.orchestration.phaseTrack missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorTrack", negativeResponses[172], "gateway.runtime.orchestration.vectorTrack missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncTrack", negativeResponses[173], "gateway.runtime.streaming.syncTrack missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandTrack", negativeResponses[174], "gateway.runtime.streaming.bandTrack missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorTrack", negativeResponses[175], "gateway.models.failover.override.vectorTrack missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseRail", negativeResponses[176], "gateway.runtime.orchestration.phaseRail missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorRail", negativeResponses[177], "gateway.runtime.orchestration.vectorRail missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncRail", negativeResponses[178], "gateway.runtime.streaming.syncRail missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandRail", negativeResponses[179], "gateway.runtime.streaming.bandRail missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorRail", negativeResponses[180], "gateway.models.failover.override.vectorRail missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseSpline", negativeResponses[181], "gateway.runtime.orchestration.phaseSpline missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorSpline", negativeResponses[182], "gateway.runtime.orchestration.vectorSpline missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncSpline", negativeResponses[183], "gateway.runtime.streaming.syncSpline missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandSpline", negativeResponses[184], "gateway.runtime.streaming.bandSpline missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorSpline", negativeResponses[185], "gateway.models.failover.override.vectorSpline missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseChain", negativeResponses[186], "gateway.runtime.orchestration.phaseChain missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorChain", negativeResponses[187], "gateway.runtime.orchestration.vectorChain missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncChain", negativeResponses[188], "gateway.runtime.streaming.syncChain missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandChain", negativeResponses[189], "gateway.runtime.streaming.bandChain missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorChain", negativeResponses[190], "gateway.models.failover.override.vectorChain missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseThread", negativeResponses[191], "gateway.runtime.orchestration.phaseThread missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorThread", negativeResponses[192], "gateway.runtime.orchestration.vectorThread missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncThread", negativeResponses[193], "gateway.runtime.streaming.syncThread missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandThread", negativeResponses[194], "gateway.runtime.streaming.bandThread missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorThread", negativeResponses[195], "gateway.models.failover.override.vectorThread missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseLink", negativeResponses[196], "gateway.runtime.orchestration.phaseLink missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorLink", negativeResponses[197], "gateway.runtime.orchestration.vectorLink missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncLink", negativeResponses[198], "gateway.runtime.streaming.syncLink missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandLink", negativeResponses[199], "gateway.runtime.streaming.bandLink missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorLink", negativeResponses[200], "gateway.models.failover.override.vectorLink missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseNode", negativeResponses[201], "gateway.runtime.orchestration.phaseNode missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorNode2", negativeResponses[202], "gateway.runtime.orchestration.vectorNode2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncNode2", negativeResponses[203], "gateway.runtime.streaming.syncNode2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandNode2", negativeResponses[204], "gateway.runtime.streaming.bandNode2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorNode2", negativeResponses[205], "gateway.models.failover.override.vectorNode2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseBridge", negativeResponses[206], "gateway.runtime.orchestration.phaseBridge missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorBridge", negativeResponses[207], "gateway.runtime.orchestration.vectorBridge missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncBridge", negativeResponses[208], "gateway.runtime.streaming.syncBridge missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandBridge", negativeResponses[209], "gateway.runtime.streaming.bandBridge missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorBridge", negativeResponses[210], "gateway.models.failover.override.vectorBridge missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phasePortal", negativeResponses[211], "gateway.runtime.orchestration.phasePortal missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorPortal", negativeResponses[212], "gateway.runtime.orchestration.vectorPortal missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncPortal", negativeResponses[213], "gateway.runtime.streaming.syncPortal missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandPortal", negativeResponses[214], "gateway.runtime.streaming.bandPortal missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorPortal", negativeResponses[215], "gateway.models.failover.override.vectorPortal missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseRelay2", negativeResponses[216], "gateway.runtime.orchestration.phaseRelay2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorRelay2", negativeResponses[217], "gateway.runtime.orchestration.vectorRelay2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncRelay2", negativeResponses[218], "gateway.runtime.streaming.syncRelay2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandRelay2", negativeResponses[219], "gateway.runtime.streaming.bandRelay2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorRelay2", negativeResponses[220], "gateway.models.failover.override.vectorRelay2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseGate2", negativeResponses[221], "gateway.runtime.orchestration.phaseGate2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorGate2", negativeResponses[222], "gateway.runtime.orchestration.vectorGate2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncGate2", negativeResponses[223], "gateway.runtime.streaming.syncGate2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandGate2", negativeResponses[224], "gateway.runtime.streaming.bandGate2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorGate2", negativeResponses[225], "gateway.models.failover.override.vectorGate2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseHub2", negativeResponses[226], "gateway.runtime.orchestration.phaseHub2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorHub2", negativeResponses[227], "gateway.runtime.orchestration.vectorHub2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncHub2", negativeResponses[228], "gateway.runtime.streaming.syncHub2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandHub2", negativeResponses[229], "gateway.runtime.streaming.bandHub2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorHub2", negativeResponses[230], "gateway.models.failover.override.vectorHub2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseNode3", negativeResponses[231], "gateway.runtime.orchestration.phaseNode3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorNode3", negativeResponses[232], "gateway.runtime.orchestration.vectorNode3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncNode3", negativeResponses[233], "gateway.runtime.streaming.syncNode3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandNode3", negativeResponses[234], "gateway.runtime.streaming.bandNode3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorNode3", negativeResponses[235], "gateway.models.failover.override.vectorNode3 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseLink2", negativeResponses[236], "gateway.runtime.orchestration.phaseLink2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorLink2", negativeResponses[237], "gateway.runtime.orchestration.vectorLink2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncLink2", negativeResponses[238], "gateway.runtime.streaming.syncLink2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandLink2", negativeResponses[239], "gateway.runtime.streaming.bandLink2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorLink2", negativeResponses[240], "gateway.models.failover.override.vectorLink2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseMesh2", negativeResponses[241], "gateway.runtime.orchestration.phaseMesh2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorMesh2", negativeResponses[242], "gateway.runtime.orchestration.vectorMesh2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncMesh2", negativeResponses[243], "gateway.runtime.streaming.syncMesh2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandMesh2", negativeResponses[244], "gateway.runtime.streaming.bandMesh2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorMesh2", negativeResponses[245], "gateway.models.failover.override.vectorMesh2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseArc2", negativeResponses[246], "gateway.runtime.orchestration.phaseArc2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorArc2", negativeResponses[247], "gateway.runtime.orchestration.vectorArc2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncArc2", negativeResponses[248], "gateway.runtime.streaming.syncArc2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandArc2", negativeResponses[249], "gateway.runtime.streaming.bandArc2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorArc2", negativeResponses[250], "gateway.models.failover.override.vectorArc2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseBand2", negativeResponses[251], "gateway.runtime.orchestration.phaseBand2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorBand2", negativeResponses[252], "gateway.runtime.orchestration.vectorBand2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncBand2", negativeResponses[253], "gateway.runtime.streaming.syncBand2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandBand2", negativeResponses[254], "gateway.runtime.streaming.bandBand2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorBand2", negativeResponses[255], "gateway.models.failover.override.vectorBand2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseGrid2", negativeResponses[256], "gateway.runtime.orchestration.phaseGrid2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorGrid2", negativeResponses[257], "gateway.runtime.orchestration.vectorGrid2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncGrid2", negativeResponses[258], "gateway.runtime.streaming.syncGrid2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandGrid2", negativeResponses[259], "gateway.runtime.streaming.bandGrid2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorGrid2", negativeResponses[260], "gateway.models.failover.override.vectorGrid2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseLane2", negativeResponses[261], "gateway.runtime.orchestration.phaseLane2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorLane2", negativeResponses[262], "gateway.runtime.orchestration.vectorLane2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncLane2", negativeResponses[263], "gateway.runtime.streaming.syncLane2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandLane2", negativeResponses[264], "gateway.runtime.streaming.bandLane2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorLane2", negativeResponses[265], "gateway.models.failover.override.vectorLane2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseTrack2", negativeResponses[266], "gateway.runtime.orchestration.phaseTrack2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorTrack2", negativeResponses[267], "gateway.runtime.orchestration.vectorTrack2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncTrack2", negativeResponses[268], "gateway.runtime.streaming.syncTrack2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandTrack2", negativeResponses[269], "gateway.runtime.streaming.bandTrack2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorTrack2", negativeResponses[270], "gateway.models.failover.override.vectorTrack2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseRail2", negativeResponses[271], "gateway.runtime.orchestration.phaseRail2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorRail2", negativeResponses[272], "gateway.runtime.orchestration.vectorRail2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncRail2", negativeResponses[273], "gateway.runtime.streaming.syncRail2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandRail2", negativeResponses[274], "gateway.runtime.streaming.bandRail2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorRail2", negativeResponses[275], "gateway.models.failover.override.vectorRail2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseSpline2", negativeResponses[276], "gateway.runtime.orchestration.phaseSpline2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorSpline2", negativeResponses[277], "gateway.runtime.orchestration.vectorSpline2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncSpline2", negativeResponses[278], "gateway.runtime.streaming.syncSpline2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandSpline2", negativeResponses[279], "gateway.runtime.streaming.bandSpline2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorSpline2", negativeResponses[280], "gateway.models.failover.override.vectorSpline2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseChain2", negativeResponses[281], "gateway.runtime.orchestration.phaseChain2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorChain2", negativeResponses[282], "gateway.runtime.orchestration.vectorChain2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncChain2", negativeResponses[283], "gateway.runtime.streaming.syncChain2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandChain2", negativeResponses[284], "gateway.runtime.streaming.bandChain2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorChain2", negativeResponses[285], "gateway.models.failover.override.vectorChain2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseThread2", negativeResponses[286], "gateway.runtime.orchestration.phaseThread2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorThread2", negativeResponses[287], "gateway.runtime.orchestration.vectorThread2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncThread2", negativeResponses[288], "gateway.runtime.streaming.syncThread2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandThread2", negativeResponses[289], "gateway.runtime.streaming.bandThread2 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorThread2", negativeResponses[290], "gateway.models.failover.override.vectorThread2 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseLink3", negativeResponses[291], "gateway.runtime.orchestration.phaseLink3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorLink3", negativeResponses[292], "gateway.runtime.orchestration.vectorLink3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncLink3", negativeResponses[293], "gateway.runtime.streaming.syncLink3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandLink3", negativeResponses[294], "gateway.runtime.streaming.bandLink3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorLink3", negativeResponses[295], "gateway.models.failover.override.vectorLink3 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseNode4", negativeResponses[296], "gateway.runtime.orchestration.phaseNode4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorNode4", negativeResponses[297], "gateway.runtime.orchestration.vectorNode4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncNode4", negativeResponses[298], "gateway.runtime.streaming.syncNode4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandNode4", negativeResponses[299], "gateway.runtime.streaming.bandNode4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorNode4", negativeResponses[300], "gateway.models.failover.override.vectorNode4 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseMesh3", negativeResponses[301], "gateway.runtime.orchestration.phaseMesh3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorMesh3", negativeResponses[302], "gateway.runtime.orchestration.vectorMesh3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncMesh3", negativeResponses[303], "gateway.runtime.streaming.syncMesh3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandMesh3", negativeResponses[304], "gateway.runtime.streaming.bandMesh3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorMesh3", negativeResponses[305], "gateway.models.failover.override.vectorMesh3 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseBridge3", negativeResponses[306], "gateway.runtime.orchestration.phaseBridge3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorBridge3", negativeResponses[307], "gateway.runtime.orchestration.vectorBridge3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncBridge3", negativeResponses[308], "gateway.runtime.streaming.syncBridge3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandBridge3", negativeResponses[309], "gateway.runtime.streaming.bandBridge3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorBridge3", negativeResponses[310], "gateway.models.failover.override.vectorBridge3 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phasePortal3", negativeResponses[311], "gateway.runtime.orchestration.phasePortal3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorPortal3", negativeResponses[312], "gateway.runtime.orchestration.vectorPortal3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncPortal3", negativeResponses[313], "gateway.runtime.streaming.syncPortal3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandPortal3", negativeResponses[314], "gateway.runtime.streaming.bandPortal3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorPortal3", negativeResponses[315], "gateway.models.failover.override.vectorPortal3 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseRelay3", negativeResponses[316], "gateway.runtime.orchestration.phaseRelay3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorRelay3", negativeResponses[317], "gateway.runtime.orchestration.vectorRelay3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncRelay3", negativeResponses[318], "gateway.runtime.streaming.syncRelay3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandRelay3", negativeResponses[319], "gateway.runtime.streaming.bandRelay3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorRelay3", negativeResponses[320], "gateway.models.failover.override.vectorRelay3 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseGate3", negativeResponses[321], "gateway.runtime.orchestration.phaseGate3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorGate3", negativeResponses[322], "gateway.runtime.orchestration.vectorGate3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncGate3", negativeResponses[323], "gateway.runtime.streaming.syncGate3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandGate3", negativeResponses[324], "gateway.runtime.streaming.bandGate3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorGate3", negativeResponses[325], "gateway.models.failover.override.vectorGate3 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseHub3", negativeResponses[326], "gateway.runtime.orchestration.phaseHub3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorHub3", negativeResponses[327], "gateway.runtime.orchestration.vectorHub3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncHub3", negativeResponses[328], "gateway.runtime.streaming.syncHub3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandHub3", negativeResponses[329], "gateway.runtime.streaming.bandHub3 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorHub3", negativeResponses[330], "gateway.models.failover.override.vectorHub3 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseNode5", negativeResponses[331], "gateway.runtime.orchestration.phaseNode5 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorNode5", negativeResponses[332], "gateway.runtime.orchestration.vectorNode5 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncNode5", negativeResponses[333], "gateway.runtime.streaming.syncNode5 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandNode5", negativeResponses[334], "gateway.runtime.streaming.bandNode5 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorNode5", negativeResponses[335], "gateway.models.failover.override.vectorNode5 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseLink4", negativeResponses[336], "gateway.runtime.orchestration.phaseLink4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorLink4", negativeResponses[337], "gateway.runtime.orchestration.vectorLink4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncLink4", negativeResponses[338], "gateway.runtime.streaming.syncLink4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandLink4", negativeResponses[339], "gateway.runtime.streaming.bandLink4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorLink4", negativeResponses[340], "gateway.models.failover.override.vectorLink4 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseBridge4", negativeResponses[341], "gateway.runtime.orchestration.phaseBridge4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorBridge4", negativeResponses[342], "gateway.runtime.orchestration.vectorBridge4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncBridge4", negativeResponses[343], "gateway.runtime.streaming.syncBridge4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandBridge4", negativeResponses[344], "gateway.runtime.streaming.bandBridge4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorBridge4", negativeResponses[345], "gateway.models.failover.override.vectorBridge4 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phasePortal4", negativeResponses[346], "gateway.runtime.orchestration.phasePortal4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorPortal4", negativeResponses[347], "gateway.runtime.orchestration.vectorPortal4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncPortal4", negativeResponses[348], "gateway.runtime.streaming.syncPortal4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandPortal4", negativeResponses[349], "gateway.runtime.streaming.bandPortal4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorPortal4", negativeResponses[350], "gateway.models.failover.override.vectorPortal4 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.phaseGate4", negativeResponses[351], "gateway.runtime.orchestration.phaseGate4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.vectorGate4", negativeResponses[352], "gateway.runtime.orchestration.vectorGate4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.syncGate4", negativeResponses[353], "gateway.runtime.streaming.syncGate4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.streaming.bandGate4", negativeResponses[354], "gateway.runtime.streaming.bandGate4 missing `stable`", error) ||
			!ValidateNegativeResponseCase("gateway.models.failover.override.vectorGate4", negativeResponses[355], "gateway.models.failover.override.vectorGate4 missing `model`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.taskDeltas.get", negativeResponses[356], "gateway.runtime.taskDeltas.get missing `runId`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.taskDeltas.clear", negativeResponses[357], "gateway.runtime.taskDeltas.clear missing `cleared`", error) ||
			!ValidateNegativeResponseCase("chat.send", negativeResponses[358], "chat.send missing `backendErrorCode`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.orchestration.status", negativeResponses[359], "gateway.runtime.orchestration.status.dynamicLoopMetrics missing `fallback`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.health.dependencies", negativeResponses[360], "gateway.runtime.health.dependencies probe entry missing `key`", error) ||
			!ValidateNegativeResponseCase("gateway.runtime.health.capabilities", negativeResponses[361], "gateway.runtime.health.capabilities entry missing `state`", error)) {
			return false;
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
