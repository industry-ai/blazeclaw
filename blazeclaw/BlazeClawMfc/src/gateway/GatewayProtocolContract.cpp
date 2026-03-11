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

		const std::array<ResponseFrame, 3> negativeResponses = {
			ResponseFrame{ .id = "neg-1", .ok = true, .payloadJson = "{\"accounts\":[{\"channel\":\"telegram\",\"accountId\":\"telegram.default\",\"label\":\"Telegram Default\",\"active\":true}]}", .error = std::nullopt },
			ResponseFrame{ .id = "neg-2", .ok = true, .payloadJson = "{\"session\":{\"id\":\"thread-1\",\"scope\":\"thread\",\"active\":false},\"deleted\":true}", .error = std::nullopt },
			ResponseFrame{ .id = "neg-3", .ok = true, .payloadJson = "{\"tool\":\"chat.send\",\"executed\":true,\"status\":\"ok\",\"argsProvided\":false}", .error = std::nullopt },
		};

		if (!ValidateNegativeResponseCase("gateway.channels.accounts", negativeResponses[0], "gateway.channels.accounts missing `connected`", error) ||
			!ValidateNegativeResponseCase("gateway.sessions.delete", negativeResponses[1], "gateway.sessions.delete missing `remaining`", error) ||
			!ValidateNegativeResponseCase("gateway.tools.call.execute", negativeResponses[2], "gateway.tools.call.execute missing `output`", error)) {
			return false;
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
