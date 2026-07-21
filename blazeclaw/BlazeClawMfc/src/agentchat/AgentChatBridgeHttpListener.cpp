#include "pch.h"
#include "AgentChatBridgeHttpListener.h"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <algorithm>
#include <sstream>
#include <unordered_map>

#pragma comment(lib, "Ws2_32.lib")

namespace blazeclaw::agentchat {
	namespace {
		constexpr std::size_t kMaxRequestBytes = 1024 * 1024;

		std::string ToLowerCopy(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::string TrimCopy(const std::string& value) {
			const auto begin = std::find_if_not(
				value.begin(),
				value.end(),
				[](unsigned char ch) {
					return std::isspace(ch) != 0;
				});
			const auto end = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](unsigned char ch) {
					return std::isspace(ch) != 0;
				}).base();
			if (begin >= end) {
				return {};
			}
			return std::string(begin, end);
		}

		std::string StatusText(const int statusCode) {
			switch (statusCode) {
			case 200:
				return "OK";
			case 400:
				return "Bad Request";
			case 404:
				return "Not Found";
			case 405:
				return "Method Not Allowed";
			case 500:
				return "Internal Server Error";
			case 501:
				return "Not Implemented";
			default:
				return "OK";
			}
		}

		std::string StripQuery(const std::string& path) {
			const std::size_t index = path.find('?');
			if (index == std::string::npos) {
				return path;
			}
			return path.substr(0, index);
		}

		bool TryParseContentLength(
			const std::unordered_map<std::string, std::string>& headers,
			std::size_t& outLength) {
			const auto it = headers.find("content-length");
			if (it == headers.end()) {
				outLength = 0;
				return true;
			}

			try {
				const std::size_t parsed = static_cast<std::size_t>(std::stoull(it->second));
				if (parsed > kMaxRequestBytes) {
					return false;
				}
				outLength = parsed;
				return true;
			}
			catch (...) {
				return false;
			}
		}
	}

	AgentChatBridgeHttpListener::AgentChatBridgeHttpListener() = default;

	AgentChatBridgeHttpListener::~AgentChatBridgeHttpListener() {
		Stop();
	}

	bool AgentChatBridgeHttpListener::Start(
		const std::string& bindAddress,
		const std::uint16_t port,
		RequestHandler handler) {
		Stop();

		if (!static_cast<bool>(handler)) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastError = "missing request handler";
			return false;
		}

		WSADATA wsaData{};
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastError = "WSAStartup failed";
			return false;
		}

		SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSocket == INVALID_SOCKET) {
			WSACleanup();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastError = "socket create failed";
			return false;
		}

		const char optValue = 1;
		setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &optValue, sizeof(optValue));

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		if (inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1) {
			closesocket(listenSocket);
			WSACleanup();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastError = "invalid bind address";
			return false;
		}

		if (bind(listenSocket, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
			closesocket(listenSocket);
			WSACleanup();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastError = "bind failed";
			return false;
		}

		if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
			closesocket(listenSocket);
			WSACleanup();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_lastError = "listen failed";
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_handler = std::move(handler);
			m_bindAddress = bindAddress;
			m_port = port;
			m_lastError.clear();
			m_listenSocketRaw = reinterpret_cast<void*>(static_cast<std::uintptr_t>(listenSocket));
		}

		m_running.store(true);
		m_worker = std::thread([this]() {
			WorkerLoop();
		});
		return true;
	}

	void AgentChatBridgeHttpListener::Stop() {
		const bool wasRunning = m_running.exchange(false);
		SOCKET listenSocket = INVALID_SOCKET;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_listenSocketRaw != nullptr) {
				listenSocket = static_cast<SOCKET>(reinterpret_cast<std::uintptr_t>(m_listenSocketRaw));
				m_listenSocketRaw = nullptr;
			}
		}

		if (listenSocket != INVALID_SOCKET) {
			shutdown(listenSocket, SD_BOTH);
			closesocket(listenSocket);
		}

		if (m_worker.joinable()) {
			m_worker.join();
		}

		if (wasRunning || listenSocket != INVALID_SOCKET) {
			WSACleanup();
		}
	}

	bool AgentChatBridgeHttpListener::IsRunning() const noexcept {
		return m_running.load();
	}

	std::string AgentChatBridgeHttpListener::LastError() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_lastError;
	}

	void AgentChatBridgeHttpListener::WorkerLoop() {
		SOCKET listenSocket = INVALID_SOCKET;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (m_listenSocketRaw != nullptr) {
				listenSocket = static_cast<SOCKET>(reinterpret_cast<std::uintptr_t>(m_listenSocketRaw));
			}
		}
		if (listenSocket == INVALID_SOCKET) {
			return;
		}

		while (m_running.load()) {
			sockaddr_in clientAddr{};
			int clientLen = sizeof(clientAddr);
			const SOCKET accepted = accept(
				listenSocket,
				reinterpret_cast<sockaddr*>(&clientAddr),
				&clientLen);
			if (accepted == INVALID_SOCKET) {
				if (!m_running.load()) {
					break;
				}
				continue;
			}

			HandleAcceptedSocket(reinterpret_cast<void*>(static_cast<std::uintptr_t>(accepted)));
		}
	}

	void AgentChatBridgeHttpListener::HandleAcceptedSocket(void* acceptedSocketRaw) const {
		const SOCKET clientSocket =
			static_cast<SOCKET>(reinterpret_cast<std::uintptr_t>(acceptedSocketRaw));
		if (clientSocket == INVALID_SOCKET) {
			return;
		}

		std::string requestData;
		requestData.reserve(4096);
		char buffer[4096] = {};
		int received = 0;
		std::size_t headersEnd = std::string::npos;
		while ((received = recv(clientSocket, buffer, sizeof(buffer), 0)) > 0) {
			requestData.append(buffer, static_cast<std::size_t>(received));
			headersEnd = requestData.find("\r\n\r\n");
			if (headersEnd != std::string::npos) {
				break;
			}
			if (requestData.size() > kMaxRequestBytes) {
				break;
			}
		}

		AgentChatBridgeHttpResponse response;
		if (headersEnd == std::string::npos) {
			response.statusCode = 400;
			response.body = "{\"ok\":false,\"error\":\"malformed_http_request\"}";
		}
		else {
			const std::string headersPart = requestData.substr(0, headersEnd);
			const std::size_t bodyOffset = headersEnd + 4;
			std::string body = requestData.size() > bodyOffset
				? requestData.substr(bodyOffset)
				: std::string();

			std::istringstream stream(headersPart);
			std::string requestLine;
			if (!std::getline(stream, requestLine)) {
				response.statusCode = 400;
				response.body = "{\"ok\":false,\"error\":\"missing_request_line\"}";
			}
			else {
				if (!requestLine.empty() && requestLine.back() == '\r') {
					requestLine.pop_back();
				}
				std::istringstream requestLineStream(requestLine);
				std::string method;
				std::string path;
				std::string version;
				requestLineStream >> method >> path >> version;

				std::unordered_map<std::string, std::string> headers;
				std::string headerLine;
				while (std::getline(stream, headerLine)) {
					if (!headerLine.empty() && headerLine.back() == '\r') {
						headerLine.pop_back();
					}
					const std::size_t sep = headerLine.find(':');
					if (sep == std::string::npos) {
						continue;
					}
					const std::string key = ToLowerCopy(TrimCopy(headerLine.substr(0, sep)));
					const std::string value = TrimCopy(headerLine.substr(sep + 1));
					headers.insert_or_assign(key, value);
				}

				std::size_t contentLength = 0;
				if (!TryParseContentLength(headers, contentLength)) {
					response.statusCode = 400;
					response.body = "{\"ok\":false,\"error\":\"invalid_content_length\"}";
				}
				else {
					while (body.size() < contentLength) {
						received = recv(clientSocket, buffer, sizeof(buffer), 0);
						if (received <= 0) {
							break;
						}
						body.append(buffer, static_cast<std::size_t>(received));
						if (body.size() > kMaxRequestBytes) {
							break;
						}
					}

					if (body.size() < contentLength) {
						response.statusCode = 400;
						response.body = "{\"ok\":false,\"error\":\"incomplete_request_body\"}";
					}
					else {
						body.resize(contentLength);
						RequestHandler handler;
						{
							std::lock_guard<std::mutex> lock(m_mutex);
							handler = m_handler;
						}

						if (!static_cast<bool>(handler)) {
							response.statusCode = 500;
							response.body = "{\"ok\":false,\"error\":\"missing_handler\"}";
						}
						else {
							try {
								response = handler(method, StripQuery(path), body);
							}
							catch (...) {
								response.statusCode = 500;
								response.body = "{\"ok\":false,\"error\":\"native_bridge_exception\"}";
							}
						}
					}
				}
			}
		}

		const std::string rawResponse = BuildHttpResponse(response);
		send(
			clientSocket,
			rawResponse.data(),
			static_cast<int>(rawResponse.size()),
			0);
		shutdown(clientSocket, SD_BOTH);
		closesocket(clientSocket);
	}

	std::string AgentChatBridgeHttpListener::BuildHttpResponse(
		const AgentChatBridgeHttpResponse& response) {
		const int statusCode = response.statusCode > 0 ? response.statusCode : 200;
		const std::string contentType = response.contentType.empty()
			? "application/json; charset=utf-8"
			: response.contentType;
		const std::string body = response.body;

		std::ostringstream stream;
		stream << "HTTP/1.1 " << statusCode << ' ' << StatusText(statusCode) << "\r\n"
			<< "Content-Type: " << contentType << "\r\n"
			<< "Content-Length: " << body.size() << "\r\n"
			<< "Connection: close\r\n"
			<< "Access-Control-Allow-Origin: *\r\n"
			<< "\r\n"
			<< body;
		return stream.str();
	}

} // namespace blazeclaw::agentchat
