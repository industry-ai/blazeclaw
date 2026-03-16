#include "pch.h"
#include "GatewayProtocolContract.h"

#include "GatewayProtocolCodec.h"
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
			std::size_t start = 0;
			std::size_t end = value.size();

			while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}

			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}

			return value.substr(start, end - start);
		}

		std::size_t SkipWhitespace(const std::string& text, std::size_t index) {
			while (index < text.size() && std::isspace(static_cast<unsigned char>(text[index])) != 0) {
				++index;
			}

			return index;
		}

		bool ParseJsonStringAt(const std::string& text, std::size_t& index, std::string& outValue) {
			if (index >= text.size() || text[index] != '"') {
				return false;
			}

			++index;
			outValue.clear();

			while (index < text.size()) {
				const char ch = text[index++];
				if (ch == '"') {
					return true;
				}

				if (ch == '\\') {
					if (index >= text.size()) {
						return false;
					}

					const char escaped = text[index++];
					switch (escaped) {
					case '"':
					case '\\':
					case '/':
						outValue.push_back(escaped);
						break;
					case 'b':
						outValue.push_back('\b');
						break;
					case 'f':
						outValue.push_back('\f');
						break;
					case 'n':
						outValue.push_back('\n');
						break;
					case 'r':
						outValue.push_back('\r');
						break;
					case 't':
						outValue.push_back('\t');
						break;
					default:
						return false;
					}

					continue;
				}

				outValue.push_back(ch);
			}

			return false;
		}

		bool FindStringField(const std::string& text, const std::string& fieldName, std::string& outValue) {
			const std::string token = "\"" + fieldName + "\"";
			const std::size_t keyPos = text.find(token);
			if (keyPos == std::string::npos) {
				return false;
			}

			std::size_t index = keyPos + token.size();
			index = SkipWhitespace(text, index);
			if (index >= text.size() || text[index] != ':') {
				return false;
			}

			++index;
			index = SkipWhitespace(text, index);

			return ParseJsonStringAt(text, index, outValue);
		}

		bool FindRawField(const std::string& text, const std::string& fieldName, std::string& outValue) {
			const std::string token = "\"" + fieldName + "\"";
			const std::size_t keyPos = text.find(token);
			if (keyPos == std::string::npos) {
				return false;
			}

			std::size_t index = keyPos + token.size();
			index = SkipWhitespace(text, index);
			if (index >= text.size() || text[index] != ':') {
				return false;
			}

			++index;
			index = SkipWhitespace(text, index);
			if (index >= text.size()) {
				return false;
			}

			const std::size_t start = index;
			const char opener = text[index];

			if (opener == '{' || opener == '[') {
				const char closer = opener == '{' ? '}' : ']';
				int depth = 0;
				bool inString = false;

				for (; index < text.size(); ++index) {
					const char ch = text[index];
					if (inString) {
						if (ch == '\\') {
							++index;
							continue;
						}

						if (ch == '"') {
							inString = false;
						}

						continue;
					}

					if (ch == '"') {
						inString = true;
						continue;
					}

					if (ch == opener) {
						++depth;
					}
					else if (ch == closer) {
						--depth;
						if (depth == 0) {
							outValue = text.substr(start, (index - start) + 1);
							return true;
						}
					}
				}

				return false;
			}

			if (opener == '"') {
				std::string parsed;
				if (!ParseJsonStringAt(text, index, parsed)) {
					return false;
				}

				outValue = "\"" + parsed + "\"";
				return true;
			}

			while (index < text.size() && text[index] != ',' && text[index] != '}') {
				++index;
			}

			outValue = text.substr(start, index - start);
			return true;
		}

		bool FindBoolField(const std::string& text, const std::string& fieldName, bool& outValue) {
			std::string raw;
			if (!FindRawField(text, fieldName, raw)) {
				return false;
			}

			raw = TrimBoundaryWhitespace(raw);
			if (raw == "true") {
				outValue = true;
				return true;
			}

			if (raw == "false") {
				outValue = false;
				return true;
			}

			return false;
		}

		bool FindUInt64Field(const std::string& text, const std::string& fieldName, std::uint64_t& outValue) {
			std::string raw;
			if (!FindRawField(text, fieldName, raw)) {
				return false;
			}

			raw = TrimBoundaryWhitespace(raw);
			if (raw.empty()) {
				return false;
			}

			try {
				std::size_t consumed = 0;
				const std::uint64_t parsed = std::stoull(raw, &consumed);
				if (consumed != raw.size()) {
					return false;
				}

				outValue = parsed;
				return true;
			}
			catch (...) {
				return false;
			}
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

       const std::array<ResponseFrame, 33> negativeResponses = {
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
			!ValidateNegativeResponseCase("gateway.models.failover.override.catalog", negativeResponses[32], "gateway.models.failover.override.catalog missing `model`", error)) {
			return false;
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
