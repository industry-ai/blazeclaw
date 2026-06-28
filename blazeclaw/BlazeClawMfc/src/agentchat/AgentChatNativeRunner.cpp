#include "pch.h"
#include "AgentChatNativeRunner.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <regex>
#include <nlohmann/json.hpp>

#pragma comment(lib, "Ws2_32.lib")

namespace blazeclaw::agentchat {
	namespace {
		using blazeclaw::gateway::protocol::RequestFrame;
		using blazeclaw::gateway::protocol::ResponseFrame;

		constexpr std::size_t kHbpcHeaderSize = 64;
		constexpr std::size_t kHbpcMaxPayloadSize = 64 * 1024;
		constexpr std::uint8_t kHbpcProtoVersion = 1;
		constexpr std::uint8_t kHbpcIrcMessageReq = 221;
		constexpr std::uint8_t kHbpcIrcMessageResp = 222;
		constexpr std::uint8_t kHbpcSessionMessage = 9;
		constexpr int kReconnectBaseMs = 1000;
		constexpr int kReconnectMaxMs = 30000;
		constexpr int kSocketTimeoutMs = 10000;
		constexpr int kGatewayPollIntervalMs = 160;
		constexpr int kGatewayPollTimeoutMs = 600000;

		std::string TrimCopy(const std::string& value) {
			const auto begin = std::find_if_not(
				value.begin(),
				value.end(),
				[](unsigned char ch) { return std::isspace(ch) != 0; });
			const auto end = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](unsigned char ch) { return std::isspace(ch) != 0; }).base();
			if (begin >= end) {
				return {};
			}
			return std::string(begin, end);
		}

		std::string ToUpperCopy(const std::string& value) {
			std::string upper = value;
			std::transform(
				upper.begin(),
				upper.end(),
				upper.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::toupper(ch));
				});
			return upper;
		}

		std::string ToLowerCopy(const std::string& value) {
			std::string lower = value;
			std::transform(
				lower.begin(),
				lower.end(),
				lower.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lower;
		}

		std::string GetEnv(const char* name) {
			if (name == nullptr || *name == '\0') {
				return {};
			}
			char* value = nullptr;
			size_t length = 0;
			if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
				return {};
			}
			std::string result(value);
			free(value);
			return TrimCopy(result);
		}

		std::uint16_t ParsePort(const std::string& value, std::uint16_t fallback) {
			if (value.empty()) {
				return fallback;
			}
			try {
				const int parsed = std::stoi(value);
				if (parsed <= 0 || parsed > 65535) {
					return fallback;
				}
				return static_cast<std::uint16_t>(parsed);
			}
			catch (...) {
				return fallback;
			}
		}

		std::uint32_t CurrentEpochMilliseconds() {
			using namespace std::chrono;
			return static_cast<std::uint32_t>(duration_cast<milliseconds>(
				system_clock::now().time_since_epoch()).count());
		}

		std::string JsonStringValue(const nlohmann::json& source, const char* key) {
			if (!source.is_object() || key == nullptr || *key == '\0') {
				return {};
			}
			const auto it = source.find(key);
			if (it == source.end()) {
				return {};
			}
			if (it->is_string()) {
				return TrimCopy(it->get<std::string>());
			}
			if (it->is_number_integer()) {
				return std::to_string(it->get<long long>());
			}
			if (it->is_number_unsigned()) {
				return std::to_string(it->get<unsigned long long>());
			}
			return {};
		}

		bool IsPersonalWorkspaceChannel(const std::string& channel) {
			if (channel.empty()) {
				return false;
			}
			const std::string lowered = ToLowerCopy(channel);
			return lowered.find("personal") != std::string::npos ||
				lowered == "#personal-workspace";
		}

		std::string ResolveRunnerChatHost() {
			const std::array<const char*, 4> names = {
				"BLAZECLAW_RUNNER_CHAT_HOST",
				"OPENCLAW_RUNNER_CHAT_HOST",
				"CHAT_TCP_HOST",
				"VITE_CHAT_TCP_HOST",
			};
			for (const char* name : names) {
				const std::string value = GetEnv(name);
				if (!value.empty()) {
					return value;
				}
			}
			return "101.132.254.212";
		}

		std::uint16_t ResolveRunnerChatPort() {
			const std::array<const char*, 4> names = {
				"BLAZECLAW_RUNNER_CHAT_PORT",
				"OPENCLAW_RUNNER_CHAT_PORT",
				"CHAT_TCP_PORT",
				"VITE_CHAT_TCP_PORT",
			};
			for (const char* name : names) {
				const std::string value = GetEnv(name);
				if (!value.empty()) {
					return ParsePort(value, 8765);
				}
			}
			return 8765;
		}

		std::string ResolveRunnerIdentity() {
			const std::array<const char*, 2> names = {
				"BLAZECLAW_RUNNER_AI_IDENTITY",
				"OPENCLAW_RUNNER_AI_IDENTITY",
			};
			for (const char* name : names) {
				const std::string value = GetEnv(name);
				if (!value.empty()) {
					return value;
				}
			}
			return "炎图AI助手";
		}

		std::vector<std::string> ResolveRunnerRooms() {
			const std::array<const char*, 2> names = {
				"BLAZECLAW_RUNNER_ROOMS",
				"OPENCLAW_RUNNER_ROOMS",
			};
			for (const char* name : names) {
				const std::string value = GetEnv(name);
				if (value.empty()) {
					continue;
				}
				std::vector<std::string> rooms;
				std::stringstream ss(value);
				std::string room;
				while (std::getline(ss, room, ',')) {
					room = TrimCopy(room);
					if (!room.empty()) {
						rooms.push_back(room);
					}
				}
				if (!rooms.empty()) {
					return rooms;
				}
			}
			return { "#roadshow-room", "#personal-workspace" };
		}

		std::string NormalizeMentionText(std::string text, const std::string& identity) {
			if (text.empty()) {
				return {};
			}
			text = std::regex_replace(text, std::regex("\\xE2\\x80\\x8B"), "");
			std::vector<std::string> mentions = {
				"@" + identity,
				"＠" + identity,
			};
			for (const std::string& mention : mentions) {
				if (mention.empty()) {
					continue;
				}
				const std::size_t pos = text.find(mention);
				if (pos != std::string::npos) {
					text.erase(pos, mention.size());
				}
			}
			text = std::regex_replace(text, std::regex("\\s+"), " ");
			return TrimCopy(text);
		}

		bool IsMentioned(const std::string& text, const std::string& identity) {
			if (text.empty() || identity.empty()) {
				return false;
			}
			if (text.find("@" + identity) != std::string::npos) {
				return true;
			}
			if (text.find("＠" + identity) != std::string::npos) {
				return true;
			}
			std::string normalizedIdentity = identity;
			normalizedIdentity.erase(
				std::remove(normalizedIdentity.begin(), normalizedIdentity.end(), '\xE2'),
				normalizedIdentity.end());
			return !normalizedIdentity.empty() && text.find("@" + normalizedIdentity) != std::string::npos;
		}

		bool ReadExactly(SOCKET socketHandle, char* buffer, const int expectedBytes) {
			int receivedTotal = 0;
			while (receivedTotal < expectedBytes) {
				const int received = recv(socketHandle, buffer + receivedTotal, expectedBytes - receivedTotal, 0);
				if (received <= 0) {
					return false;
				}
				receivedTotal += received;
			}
			return true;
		}

		bool WriteAll(SOCKET socketHandle, const char* buffer, const int totalBytes) {
			int sentTotal = 0;
			while (sentTotal < totalBytes) {
				const int sent = send(socketHandle, buffer + sentTotal, totalBytes - sentTotal, 0);
				if (sent <= 0) {
					return false;
				}
				sentTotal += sent;
			}
			return true;
		}

		std::vector<char> BuildHbpcHeader(
			const std::uint8_t type,
			const std::uint32_t payloadSize,
			const std::uint64_t sessionId,
			const std::uint32_t sequence) {
			std::vector<char> header(kHbpcHeaderSize, 0);
			header[0] = 'H';
			header[1] = 'B';
			header[2] = 'P';
			header[3] = 'C';
			header[4] = static_cast<char>(kHbpcProtoVersion);
			header[5] = static_cast<char>(type);
			header[8] = static_cast<char>((sequence >> 24) & 0xFF);
			header[9] = static_cast<char>((sequence >> 16) & 0xFF);
			header[10] = static_cast<char>((sequence >> 8) & 0xFF);
			header[11] = static_cast<char>(sequence & 0xFF);
			header[12] = static_cast<char>((payloadSize >> 24) & 0xFF);
			header[13] = static_cast<char>((payloadSize >> 16) & 0xFF);
			header[14] = static_cast<char>((payloadSize >> 8) & 0xFF);
			header[15] = static_cast<char>(payloadSize & 0xFF);
			header[16] = static_cast<char>((sessionId >> 56) & 0xFF);
			header[17] = static_cast<char>((sessionId >> 48) & 0xFF);
			header[18] = static_cast<char>((sessionId >> 40) & 0xFF);
			header[19] = static_cast<char>((sessionId >> 32) & 0xFF);
			header[20] = static_cast<char>((sessionId >> 24) & 0xFF);
			header[21] = static_cast<char>((sessionId >> 16) & 0xFF);
			header[22] = static_cast<char>((sessionId >> 8) & 0xFF);
			header[23] = static_cast<char>(sessionId & 0xFF);
			const std::uint64_t nowMs = CurrentEpochMilliseconds();
			header[24] = static_cast<char>((nowMs >> 56) & 0xFF);
			header[25] = static_cast<char>((nowMs >> 48) & 0xFF);
			header[26] = static_cast<char>((nowMs >> 40) & 0xFF);
			header[27] = static_cast<char>((nowMs >> 32) & 0xFF);
			header[28] = static_cast<char>((nowMs >> 24) & 0xFF);
			header[29] = static_cast<char>((nowMs >> 16) & 0xFF);
			header[30] = static_cast<char>((nowMs >> 8) & 0xFF);
			header[31] = static_cast<char>(nowMs & 0xFF);
			return header;
		}
	}

	AgentChatNativeRunner::AgentChatNativeRunner() = default;

	AgentChatNativeRunner::~AgentChatNativeRunner() {
		Shutdown();
	}

	void AgentChatNativeRunner::SetGatewayRequestRouter(GatewayRouter router) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_gatewayRouter = std::move(router);
	}

	bool AgentChatNativeRunner::Initialize() {
		std::lock_guard<std::mutex> lock(m_mutex);
		if (m_running) {
			return true;
		}
		m_running = true;
		m_worker = std::thread([this]() { WorkerMain(); });
		return true;
	}

	void AgentChatNativeRunner::Shutdown() {
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_running) {
				return;
			}
			m_running = false;
		}
		if (m_worker.joinable()) {
			m_worker.join();
		}
	}

	bool AgentChatNativeRunner::IsRunning() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_running;
	}

	void AgentChatNativeRunner::WorkerMain() {
		int reconnectAttempt = 0;
		while (true) {
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_running) {
					break;
				}
			}

			if (ConnectAndRunOnce()) {
				reconnectAttempt = 0;
			}
			else {
				reconnectAttempt += 1;
			}

			const int delayMs = (std::min)(
				kReconnectBaseMs * static_cast<int>(std::pow(2.0, (std::min)(reconnectAttempt, 6))),
				kReconnectMaxMs);
			std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
		}
	}

	bool AgentChatNativeRunner::ConnectAndRunOnce() {
		WSADATA wsaData{};
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			return false;
		}

		SOCKET socketHandle = INVALID_SOCKET;
		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		const std::string host = ResolveRunnerChatHost();
		const std::string portText = std::to_string(ResolveRunnerChatPort());
		addrinfo* addressInfo = nullptr;
		if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &addressInfo) != 0 || addressInfo == nullptr) {
			WSACleanup();
			return false;
		}

		for (addrinfo* current = addressInfo; current != nullptr; current = current->ai_next) {
			socketHandle = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
			if (socketHandle == INVALID_SOCKET) {
				continue;
			}
			const DWORD timeout = static_cast<DWORD>(kSocketTimeoutMs);
			setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			if (connect(socketHandle, current->ai_addr, static_cast<int>(current->ai_addrlen)) == 0) {
				break;
			}
			closesocket(socketHandle);
			socketHandle = INVALID_SOCKET;
		}
		freeaddrinfo(addressInfo);

		if (socketHandle == INVALID_SOCKET) {
			WSACleanup();
			return false;
		}

		std::uint64_t sessionId = 0;
		const std::string identity = ResolveRunnerIdentity();
		const std::vector<std::string> rooms = ResolveRunnerRooms();
		std::uint32_t sequence = 0;

		{
			nlohmann::json loginPayload = {
				{ "cmd", "LOGIN" },
				{ "identity", "ai_" + identity },
				{ "type", "ai_runner" },
			};
			const std::string payloadRaw = loginPayload.dump();
			const auto header = BuildHbpcHeader(
				kHbpcSessionMessage,
				static_cast<std::uint32_t>(payloadRaw.size()),
				0,
				++sequence);
			std::vector<char> packet;
			packet.reserve(header.size() + payloadRaw.size());
			packet.insert(packet.end(), header.begin(), header.end());
			packet.insert(packet.end(), payloadRaw.begin(), payloadRaw.end());
			if (!WriteAll(socketHandle, packet.data(), static_cast<int>(packet.size()))) {
				closesocket(socketHandle);
				WSACleanup();
				return false;
			}
		}

		for (const std::string& room : rooms) {
			nlohmann::json joinPayload = {
				{ "cmd", "JOIN" },
				{ "channel", room },
			};
			const std::string payloadRaw = joinPayload.dump();
			const auto header = BuildHbpcHeader(
				kHbpcIrcMessageReq,
				static_cast<std::uint32_t>(payloadRaw.size()),
				sessionId,
				++sequence);
			std::vector<char> packet;
			packet.reserve(header.size() + payloadRaw.size());
			packet.insert(packet.end(), header.begin(), header.end());
			packet.insert(packet.end(), payloadRaw.begin(), payloadRaw.end());
			if (!WriteAll(socketHandle, packet.data(), static_cast<int>(packet.size()))) {
				closesocket(socketHandle);
				WSACleanup();
				return false;
			}
		}

		while (true) {
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if (!m_running) {
					break;
				}
			}

			std::array<char, kHbpcHeaderSize> header{};
			if (!ReadExactly(socketHandle, header.data(), static_cast<int>(header.size()))) {
				break;
			}
			if (header[0] != 'H' || header[1] != 'B' || header[2] != 'P' || header[3] != 'C') {
				break;
			}
			if (static_cast<std::uint8_t>(header[4]) != kHbpcProtoVersion) {
				continue;
			}

			const std::uint8_t type = static_cast<std::uint8_t>(header[5]);
			const std::uint32_t payloadLen =
				(static_cast<std::uint32_t>(static_cast<unsigned char>(header[12])) << 24) |
				(static_cast<std::uint32_t>(static_cast<unsigned char>(header[13])) << 16) |
				(static_cast<std::uint32_t>(static_cast<unsigned char>(header[14])) << 8) |
				static_cast<std::uint32_t>(static_cast<unsigned char>(header[15]));
			if (payloadLen > kHbpcMaxPayloadSize) {
				break;
			}

			std::vector<char> payload(payloadLen, 0);
			if (payloadLen > 0 && !ReadExactly(socketHandle, payload.data(), static_cast<int>(payload.size()))) {
				break;
			}

			if (type != kHbpcIrcMessageResp) {
				continue;
			}

			nlohmann::json payloadJson = nlohmann::json::parse(
				std::string(payload.begin(), payload.end()),
				nullptr,
				false);
			if (payloadJson.is_discarded() || !payloadJson.is_object()) {
				continue;
			}

			const std::string event = ToUpperCopy(JsonStringValue(payloadJson, "event"));
			if (event != "PRIVMSG") {
				continue;
			}

			IncomingPrivmsg msg;
			msg.channel = JsonStringValue(payloadJson, "channel");
			msg.message = JsonStringValue(payloadJson, "message");
			msg.from = JsonStringValue(payloadJson, "from");
			msg.messageId = JsonStringValue(payloadJson, "messageId");
			if (msg.messageId.empty()) {
				msg.messageId = JsonStringValue(payloadJson, "message_id");
			}
			if (msg.messageId.empty()) {
				msg.messageId = JsonStringValue(payloadJson, "id");
			}
			if (msg.messageId.empty()) {
				msg.messageId = "msg-" + std::to_string(CurrentEpochMilliseconds());
			}
			msg.userId = JsonStringValue(payloadJson, "userId");
			if (msg.userId.empty()) {
				msg.userId = JsonStringValue(payloadJson, "user_id");
			}
			msg.userPhone = JsonStringValue(payloadJson, "userPhone");
			if (msg.userPhone.empty()) {
				msg.userPhone = JsonStringValue(payloadJson, "user_phone");
			}

			std::string error;
			if (!ProcessChatMessage(
				reinterpret_cast<void*>(socketHandle),
				sessionId,
				sequence,
				msg,
				error)) {
				if (!error.empty()) {
					OutputDebugStringA(("[agentchat-native-runner] process error: " + error + "\n").c_str());
				}
			}
		}

		shutdown(socketHandle, SD_BOTH);
		closesocket(socketHandle);
		WSACleanup();
		return true;
	}

	bool AgentChatNativeRunner::ProcessChatMessage(
		void* socketHandle,
		std::uint64_t sessionId,
		std::uint32_t& sequence,
		const IncomingPrivmsg& message,
		std::string& errorOut) const {
		errorOut.clear();
		if (!ShouldRespond(message)) {
			return true;
		}

		if (!SendTypingIndicator(socketHandle, sessionId, message.channel, true)) {
			OutputDebugStringA("[agentchat-native-runner] typing indicator send failed\n");
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_processingMessageIds.size() > 500) {
				const_cast<AgentChatNativeRunner*>(this)->m_processingMessageIds.clear();
			}
			if (m_processingMessageIds.find(message.messageId) != m_processingMessageIds.end()) {
				return true;
			}
			const_cast<AgentChatNativeRunner*>(this)->m_processingMessageIds.insert(message.messageId);
		}

		std::string responseText;
		const std::string userText = ExtractUserText(message.message);
		if (!RouteChatSendAndAwaitFinal(message, userText, responseText, errorOut)) {
			(void)SendTypingIndicator(socketHandle, sessionId, message.channel, false);
			const std::string fallbackText = "抱歉，助手暂时无法处理您的请求，请稍后再试。";
			std::string sendErr;
			(void)SendReply(
				socketHandle,
				sessionId,
				sequence,
				message.channel,
				fallbackText,
				message.messageId,
				sendErr);
			return false;
		}

		const std::string replyText = BuildReplyText(message, responseText);
		(void)SendTypingIndicator(socketHandle, sessionId, message.channel, false);
		std::string sendErr;
		if (!SendReply(
			socketHandle,
			sessionId,
			sequence,
			message.channel,
			replyText,
			message.messageId,
			sendErr)) {
			errorOut = sendErr.empty() ? std::string("reply_send_failed") : sendErr;
			return false;
		}
		return true;
	}

	bool AgentChatNativeRunner::ShouldRespond(const IncomingPrivmsg& message) const {
		const std::string identity = ResolveRunnerIdentity();
		if (message.from == identity || message.from == "ai_" + identity) {
			return false;
		}
		if (message.message.rfind("c:agentchat.collaboration", 0) == 0) {
			return false;
		}
		if (message.message.rfind("[::AGENT_TYPING::]", 0) == 0) {
			return false;
		}
		if (TrimCopy(message.message).empty()) {
			return false;
		}
		if (IsPersonalWorkspaceChannel(message.channel)) {
			return true;
		}
		return IsMentioned(message.message, identity);
	}

	std::string AgentChatNativeRunner::ExtractUserText(const std::string& raw) const {
		return NormalizeMentionText(raw, ResolveRunnerIdentity());
	}

	std::string AgentChatNativeRunner::BuildReplyText(
		const IncomingPrivmsg& message,
		const std::string& aiText) const {
		if (IsPersonalWorkspaceChannel(message.channel)) {
			return aiText;
		}
		const std::string mentionTarget = !message.userPhone.empty()
			? message.userPhone
			: (!message.userId.empty() ? message.userId : message.from);
		if (mentionTarget.empty()) {
			return aiText;
		}
		if (aiText.rfind("@", 0) == 0) {
			return aiText;
		}
		return "@" + mentionTarget + " " + aiText;
	}

	bool AgentChatNativeRunner::RouteChatSendAndAwaitFinal(
		const IncomingPrivmsg& message,
		const std::string& userText,
		std::string& responseText,
		std::string& errorOut) const {
		GatewayRouter router;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			router = m_gatewayRouter;
		}
		if (!router) {
			errorOut = "gateway_router_unavailable";
			return false;
		}

		const std::string idempotencyKey = "native-runner-" + std::to_string(CurrentEpochMilliseconds());
		nlohmann::json sendParams = {
			{ "sessionKey", "openclaw-runner:main" },
			{ "message", userText },
			{ "deliver", false },
			{ "idempotencyKey", idempotencyKey },
		};

		const RequestFrame sendRequest{
			.id = "agentchat-native-runner-chat-send",
			.method = "chat.send",
			.paramsJson = sendParams.dump(),
		};
		const ResponseFrame sendResponse = router(sendRequest);
		if (!sendResponse.ok) {
			errorOut = sendResponse.error.has_value()
				? sendResponse.error->message
				: std::string("chat_send_failed");
			return false;
		}

		std::string runId;
		if (sendResponse.payloadJson.has_value()) {
			auto sendPayload = nlohmann::json::parse(sendResponse.payloadJson.value(), nullptr, false);
			if (!sendPayload.is_discarded() && sendPayload.is_object()) {
				runId = JsonStringValue(sendPayload, "runId");
			}
		}
		if (runId.empty()) {
			errorOut = "chat_send_missing_run_id";
			return false;
		}

		const auto start = std::chrono::steady_clock::now();
		std::string finalText;
		while (true) {
			const auto now = std::chrono::steady_clock::now();
			const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
			if (elapsedMs > kGatewayPollTimeoutMs) {
				errorOut = "chat_poll_timeout";
				return false;
			}

			nlohmann::json pollParams = {
				{ "sessionKey", "openclaw-runner:main" },
				{ "max", 32 },
			};
			const RequestFrame pollRequest{
				.id = "agentchat-native-runner-chat-poll",
				.method = "chat.events.poll",
				.paramsJson = pollParams.dump(),
			};
			const ResponseFrame pollResponse = router(pollRequest);
			if (!pollResponse.ok || !pollResponse.payloadJson.has_value()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(kGatewayPollIntervalMs));
				continue;
			}

			auto pollPayload = nlohmann::json::parse(pollResponse.payloadJson.value(), nullptr, false);
			if (pollPayload.is_discarded() || !pollPayload.is_object()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(kGatewayPollIntervalMs));
				continue;
			}
			const auto eventsIt = pollPayload.find("events");
			if (eventsIt == pollPayload.end() || !eventsIt->is_array()) {
				std::this_thread::sleep_for(std::chrono::milliseconds(kGatewayPollIntervalMs));
				continue;
			}

			for (const auto& event : *eventsIt) {
				if (!event.is_object()) {
					continue;
				}
				if (JsonStringValue(event, "runId") != runId) {
					continue;
				}

				const std::string state = JsonStringValue(event, "state");
				const auto messageIt = event.find("message");
				if (messageIt != event.end() && messageIt->is_object()) {
					std::string text = JsonStringValue(*messageIt, "text");
					if (text.empty()) {
						const auto contentIt = messageIt->find("content");
						if (contentIt != messageIt->end() && contentIt->is_string()) {
							text = contentIt->get<std::string>();
						}
					}
					if (!text.empty()) {
						finalText = text;
					}
				}

				if (state == "error") {
					errorOut = JsonStringValue(event, "errorMessage");
					if (errorOut.empty()) {
						errorOut = "chat_runtime_error";
					}
					return false;
				}
				if (state == "final" || state == "aborted") {
					responseText = finalText.empty() ? "已处理完成，请查看当前结果。" : finalText;
					return true;
				}
			}

			std::this_thread::sleep_for(std::chrono::milliseconds(kGatewayPollIntervalMs));
		}
	}

	bool AgentChatNativeRunner::SendTypingIndicator(
		void* socketHandle,
		std::uint64_t sessionId,
		const std::string& channel,
		bool typing) const {
		if (socketHandle == nullptr || channel.empty()) {
			return false;
		}
		const SOCKET rawSocket = reinterpret_cast<SOCKET>(socketHandle);
		if (rawSocket == INVALID_SOCKET) {
			return false;
		}

		static std::atomic<std::uint32_t> seq{ 100000 };
		nlohmann::json payload = {
			{ "cmd", "AGENT_BROADCAST" },
			{ "channel", channel },
			{ "typing", typing },
			{ "agentRequestId", "openclaw-runner" },
		};
		const std::string payloadRaw = payload.dump();
		const auto header = BuildHbpcHeader(
			kHbpcIrcMessageReq,
			static_cast<std::uint32_t>(payloadRaw.size()),
			sessionId,
			++seq);
		std::vector<char> packet;
		packet.reserve(header.size() + payloadRaw.size());
		packet.insert(packet.end(), header.begin(), header.end());
		packet.insert(packet.end(), payloadRaw.begin(), payloadRaw.end());
		return WriteAll(rawSocket, packet.data(), static_cast<int>(packet.size()));
	}

	bool AgentChatNativeRunner::SendReply(
		void* socketHandle,
		std::uint64_t sessionId,
		std::uint32_t& sequence,
		const std::string& channel,
		const std::string& text,
		const std::string& replyToMessageId,
		std::string& errorOut) const {
		errorOut.clear();
		if (socketHandle == nullptr) {
			errorOut = "runner_socket_null";
			return false;
		}
		const SOCKET rawSocket = reinterpret_cast<SOCKET>(socketHandle);
		if (rawSocket == INVALID_SOCKET) {
			errorOut = "runner_socket_invalid";
			return false;
		}

		nlohmann::json payload = {
			{ "cmd", "PRIVMSG" },
			{ "channel", channel },
			{ "message", text },
			{ "replyTo", replyToMessageId },
		};
		const std::string payloadRaw = payload.dump();
		if (payloadRaw.size() > kHbpcMaxPayloadSize) {
			errorOut = "reply_payload_too_large";
			return false;
		}
		const auto header = BuildHbpcHeader(
			kHbpcIrcMessageReq,
			static_cast<std::uint32_t>(payloadRaw.size()),
			sessionId,
			++sequence);
		std::vector<char> packet;
		packet.reserve(header.size() + payloadRaw.size());
		packet.insert(packet.end(), header.begin(), header.end());
		packet.insert(packet.end(), payloadRaw.begin(), payloadRaw.end());
		if (!WriteAll(rawSocket, packet.data(), static_cast<int>(packet.size()))) {
			errorOut = "reply_send_failed";
			return false;
		}
		return true;
	}

	namespace test_hooks {
		bool RunnerShouldRespond(
			const AgentChatNativeRunner& runner,
			const std::string& channel,
			const std::string& message,
			const std::string& from,
			const std::string& messageId,
			const std::string& userId,
			const std::string& userPhone) {
			AgentChatNativeRunner::IncomingPrivmsg msg;
			msg.channel = channel;
			msg.message = message;
			msg.from = from;
			msg.messageId = messageId;
			msg.userId = userId;
			msg.userPhone = userPhone;
			return runner.ShouldRespond(msg);
		}

		std::string RunnerExtractUserText(
			const AgentChatNativeRunner& runner,
			const std::string& raw) {
			return runner.ExtractUserText(raw);
		}

		std::string RunnerBuildReplyText(
			const AgentChatNativeRunner& runner,
			const std::string& channel,
			const std::string& from,
			const std::string& userId,
			const std::string& userPhone,
			const std::string& aiText) {
			AgentChatNativeRunner::IncomingPrivmsg msg;
			msg.channel = channel;
			msg.from = from;
			msg.userId = userId;
			msg.userPhone = userPhone;
			return runner.BuildReplyText(msg, aiText);
		}

		bool RunnerRouteChatSendAndAwaitFinal(
			const AgentChatNativeRunner& runner,
			const std::string& channel,
			const std::string& from,
			const std::string& userId,
			const std::string& userPhone,
			const std::string& messageId,
			const std::string& userText,
			std::string& responseText,
			std::string& errorOut) {
			AgentChatNativeRunner::IncomingPrivmsg msg;
			msg.channel = channel;
			msg.from = from;
			msg.userId = userId;
			msg.userPhone = userPhone;
			msg.messageId = messageId;
			return runner.RouteChatSendAndAwaitFinal(msg, userText, responseText, errorOut);
		}
	}

} // namespace blazeclaw::agentchat
