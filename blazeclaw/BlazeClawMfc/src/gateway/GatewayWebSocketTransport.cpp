#include "pch.h"
#include "GatewayWebSocketTransport.h"

#include <algorithm>
#include <array>
#include <ws2tcpip.h>
#include <wincrypt.h>
#include <ws2tcpip.h>

namespace blazeclaw::gateway {
	namespace {

		constexpr std::uint64_t kMaxFramePayloadBytes = 1024ULL * 1024ULL;
        constexpr std::size_t kMaxReadBufferBytes = 4ULL * 1024ULL * 1024ULL;
		constexpr std::size_t kMaxFragmentBufferBytes = 2ULL * 1024ULL * 1024ULL;
		constexpr std::size_t kMaxOutboundFramesPerConnection = 256;
		constexpr std::size_t kMaxOutboundNetworkFramesPerConnection = 256;
        constexpr std::uint64_t kHandshakeTimeoutMs = 10'000;
		constexpr std::uint64_t kIdleConnectionTimeoutMs = 120'000;
		constexpr std::uint16_t kCloseCodeGoingAway = 1001;
		constexpr std::uint16_t kCloseCodeInvalidPayloadData = 1007;
		constexpr std::uint16_t kCloseCodeMessageTooBig = 1009;

		std::uint64_t NowMs() {
			return static_cast<std::uint64_t>(GetTickCount64());
		}

		std::string ToLowerCopy(const std::string& value) {
			std::string lower = value;
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
				});
			return lower;
		}

		std::string ToWsaErrorText(const char* prefix, int code) {
			return std::string(prefix) + " WSA error=" + std::to_string(code);
		}

		std::string TrimCopy(const std::string& value) {
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

		bool IsAllowedRequestTarget(const std::string& requestLine) {
			const std::size_t methodEnd = requestLine.find(' ');
			if (methodEnd == std::string::npos) {
				return false;
			}

			const std::size_t targetEnd = requestLine.find(' ', methodEnd + 1);
			if (targetEnd == std::string::npos || targetEnd <= methodEnd + 1) {
				return false;
			}

			const std::string target = requestLine.substr(methodEnd + 1, targetEnd - methodEnd - 1);
			if (target.empty() || target.front() != '/') {
				return false;
			}

			return target == "/" || target == "/gateway" || target.rfind("/gateway?", 0) == 0;
		}

		bool HeaderContainsTokenPrefix(const std::string& headerValue, const std::string& tokenPrefix) {
			const std::string lower = ToLowerCopy(headerValue);
			const std::string lowerPrefix = ToLowerCopy(tokenPrefix);

			std::size_t start = 0;
			while (start < lower.size()) {
				const std::size_t commaPos = lower.find(',', start);
				const std::size_t end = commaPos == std::string::npos ? lower.size() : commaPos;
				const std::string item = TrimCopy(lower.substr(start, end - start));
				if (item.rfind(lowerPrefix, 0) == 0) {
					return true;
				}

				if (commaPos == std::string::npos) {
					break;
				}

				start = commaPos + 1;
			}

			return false;
		}

		bool HeaderContainsToken(const std::string& headerValue, const std::string& token) {
			std::string lower = ToLowerCopy(headerValue);
			std::string lowerToken = ToLowerCopy(token);

			std::size_t start = 0;
			while (start < lower.size()) {
				const std::size_t commaPos = lower.find(',', start);
				const std::size_t end = commaPos == std::string::npos ? lower.size() : commaPos;
				const std::string item = TrimCopy(lower.substr(start, end - start));
				if (item == lowerToken) {
					return true;
				}

				if (commaPos == std::string::npos) {
					break;
				}

				start = commaPos + 1;
			}

			return false;
		}

		bool IsValidSecWebSocketKey(const std::string& key) {
			if (key.size() != 24) {
				return false;
			}

			for (const char ch : key) {
				const bool isBase64 =
					std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '+' || ch == '/' || ch == '=';
				if (!isBase64) {
					return false;
				}
			}

			DWORD outputSize = 0;
			if (!CryptStringToBinaryA(
				key.c_str(),
				static_cast<DWORD>(key.size()),
				CRYPT_STRING_BASE64,
				nullptr,
				&outputSize,
				nullptr,
				nullptr)) {
				return false;
			}

			return outputSize == 16;
		}

		bool IsAllowedOriginValue(const std::string& origin) {
			if (origin.empty()) {
				return true;
			}

			const std::string lowerOrigin = ToLowerCopy(origin);
			return lowerOrigin.rfind("http://localhost", 0) == 0 ||
				lowerOrigin.rfind("https://localhost", 0) == 0 ||
				lowerOrigin.rfind("http://127.0.0.1", 0) == 0 ||
				lowerOrigin.rfind("https://127.0.0.1", 0) == 0 ||
				lowerOrigin.rfind("http://[::1]", 0) == 0 ||
				lowerOrigin.rfind("https://[::1]", 0) == 0;
		}

		bool IsContinuationByte(std::uint8_t value) {
			return (value & 0xC0) == 0x80;
		}

		bool IsValidUtf8(const std::string& value) {
			std::size_t i = 0;
			const std::size_t size = value.size();

			while (i < size) {
				const std::uint8_t b0 = static_cast<std::uint8_t>(value[i]);
				if (b0 <= 0x7F) {
					++i;
					continue;
				}

				if (b0 >= 0xC2 && b0 <= 0xDF) {
					if (i + 1 >= size || !IsContinuationByte(static_cast<std::uint8_t>(value[i + 1]))) {
						return false;
					}
					i += 2;
					continue;
				}

				if (b0 == 0xE0) {
					if (i + 2 >= size) {
						return false;
					}

					const std::uint8_t b1 = static_cast<std::uint8_t>(value[i + 1]);
					const std::uint8_t b2 = static_cast<std::uint8_t>(value[i + 2]);
					if (b1 < 0xA0 || b1 > 0xBF || !IsContinuationByte(b2)) {
						return false;
					}

					i += 3;
					continue;
				}

				if ((b0 >= 0xE1 && b0 <= 0xEC) || (b0 >= 0xEE && b0 <= 0xEF)) {
					if (i + 2 >= size ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 1])) ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 2]))) {
						return false;
					}

					i += 3;
					continue;
				}

				if (b0 == 0xED) {
					if (i + 2 >= size) {
						return false;
					}

					const std::uint8_t b1 = static_cast<std::uint8_t>(value[i + 1]);
					const std::uint8_t b2 = static_cast<std::uint8_t>(value[i + 2]);
					if (b1 < 0x80 || b1 > 0x9F || !IsContinuationByte(b2)) {
						return false;
					}

					i += 3;
					continue;
				}

				if (b0 == 0xF0) {
					if (i + 3 >= size) {
						return false;
					}

					const std::uint8_t b1 = static_cast<std::uint8_t>(value[i + 1]);
					if (b1 < 0x90 || b1 > 0xBF ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 2])) ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 3]))) {
						return false;
					}

					i += 4;
					continue;
				}

				if (b0 >= 0xF1 && b0 <= 0xF3) {
					if (i + 3 >= size ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 1])) ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 2])) ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 3]))) {
						return false;
					}

					i += 4;
					continue;
				}

				if (b0 == 0xF4) {
					if (i + 3 >= size) {
						return false;
					}

					const std::uint8_t b1 = static_cast<std::uint8_t>(value[i + 1]);
					if (b1 < 0x80 || b1 > 0x8F ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 2])) ||
						!IsContinuationByte(static_cast<std::uint8_t>(value[i + 3]))) {
						return false;
					}

					i += 4;
					continue;
				}

				return false;
			}

			return true;
		}

		std::string BuildClosePayload(std::uint16_t code, const std::string& reason) {
			std::string payload;
			payload.reserve(2 + reason.size());
			payload.push_back(static_cast<char>((code >> 8) & 0xFF));
			payload.push_back(static_cast<char>(code & 0xFF));
			payload += reason;
			return payload;
		}

		bool ConfigureListenerSocket(SOCKET socket, std::string& error) {
			const int exclusiveAddressUse = 1;
			if (setsockopt(
				socket,
				SOL_SOCKET,
				SO_EXCLUSIVEADDRUSE,
				reinterpret_cast<const char*>(&exclusiveAddressUse),
				sizeof(exclusiveAddressUse)) == SOCKET_ERROR) {
				error = ToWsaErrorText("setsockopt(SO_EXCLUSIVEADDRUSE) failed.", WSAGetLastError());
				return false;
			}

			return true;
		}

		bool ConfigureAcceptedSocket(SOCKET socket, std::string& error) {
			const int keepAlive = 1;
			if (setsockopt(
				socket,
				SOL_SOCKET,
				SO_KEEPALIVE,
				reinterpret_cast<const char*>(&keepAlive),
				sizeof(keepAlive)) == SOCKET_ERROR) {
				error = ToWsaErrorText("setsockopt(SO_KEEPALIVE) failed.", WSAGetLastError());
				return false;
			}

			const int noDelay = 1;
			if (setsockopt(
				socket,
				IPPROTO_TCP,
				TCP_NODELAY,
				reinterpret_cast<const char*>(&noDelay),
				sizeof(noDelay)) == SOCKET_ERROR) {
				error = ToWsaErrorText("setsockopt(TCP_NODELAY) failed.", WSAGetLastError());
				return false;
			}

			return true;
		}

	} // namespace

	bool GatewayWebSocketTransport::Start(
		const std::string& bindAddress,
		std::uint16_t port,
		InboundFrameHandler inboundFrameHandler,
		std::string& error) {
		if (m_running) {
			return true;
		}

		if (!IsPlausibleBindAddress(bindAddress)) {
			error = "Invalid bind address for transport start.";
			return false;
		}

		if (port == 0) {
			error = "Invalid bind port for transport start.";
			return false;
		}

		if (inboundFrameHandler == nullptr) {
			error = "Inbound frame handler is required for transport start.";
			return false;
		}

		WSADATA data{};
		if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
			error = "WSAStartup failed.";
			return false;
		}

		m_wsaStarted = true;
     addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		const std::string service = std::to_string(port);
		addrinfo* resolved = nullptr;
		const int resolveResult = getaddrinfo(bindAddress.c_str(), service.c_str(), &hints, &resolved);
		if (resolveResult != 0 || resolved == nullptr) {
			error = "getaddrinfo() failed for bind endpoint.";
			Stop();
			return false;
		}

		std::string bindError;
		for (addrinfo* current = resolved; current != nullptr; current = current->ai_next) {
			SOCKET candidate = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
			if (candidate == INVALID_SOCKET) {
				bindError = ToWsaErrorText("socket() failed.", WSAGetLastError());
				continue;
			}

			if (current->ai_family == AF_INET6) {
				u_long v6only = 0;
				setsockopt(candidate, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&v6only), sizeof(v6only));
			}

			if (!ConfigureListenerSocket(candidate, bindError)) {
				closesocket(candidate);
				continue;
			}

			if (bind(candidate, current->ai_addr, static_cast<int>(current->ai_addrlen)) == SOCKET_ERROR) {
				bindError = ToWsaErrorText("bind() failed.", WSAGetLastError());
				closesocket(candidate);
				continue;
			}

			if (listen(candidate, SOMAXCONN) == SOCKET_ERROR) {
				bindError = ToWsaErrorText("listen() failed.", WSAGetLastError());
				closesocket(candidate);
				continue;
			}

			m_listenSocket = candidate;
			break;
		}

		freeaddrinfo(resolved);

		if (m_listenSocket == INVALID_SOCKET) {
			error = bindError.empty() ? "Unable to bind/listen on resolved endpoint candidates." : bindError;
			Stop();
			return false;
		}

		u_long nonBlocking = 1;
		if (ioctlsocket(m_listenSocket, FIONBIO, &nonBlocking) != 0) {
			error = ToWsaErrorText("ioctlsocket(FIONBIO) for listener failed.", WSAGetLastError());
			Stop();
			return false;
		}

		m_bindAddress = bindAddress;
		m_port = port;
		m_inboundFrameHandler = std::move(inboundFrameHandler);
		m_connections.clear();
       m_handshakeTimeoutCount = 0;
		m_idleTimeoutCloseCount = 0;
       m_invalidUtf8CloseCount = 0;
		m_messageTooBigCloseCount = 0;
		m_extensionRejectCount = 0;
		m_running = true;
		error.clear();

		return true;
	}

	void GatewayWebSocketTransport::Stop() {
		for (auto& [_, session] : m_connections) {
			if (session.isNetworkConnection) {
				CloseSocketIfNeeded(session.socket);
			}
		}

		m_connections.clear();
		CloseSocketIfNeeded(m_listenSocket);

		if (m_wsaStarted) {
			WSACleanup();
			m_wsaStarted = false;
		}

		m_running = false;
		m_bindAddress.clear();
		m_port = 0;
		m_nextConnectionId = 1;
		m_inboundFrameHandler = nullptr;
	}

	bool GatewayWebSocketTransport::AcceptConnection(const std::string& connectionId, std::string& error) {
		if (!m_running) {
			error = "Transport is not running.";
			return false;
		}

		if (connectionId.empty()) {
			error = "Connection id cannot be empty.";
			return false;
		}

		if (m_connections.contains(connectionId)) {
			error = "Connection already accepted.";
			return false;
		}

		m_connections.insert_or_assign(connectionId, ConnectionSession{});
		error.clear();
		return true;
	}

	bool GatewayWebSocketTransport::ProcessInboundFrame(
		const std::string& connectionId,
		const std::string& inboundFrame,
		std::string& error) {
		if (!m_running) {
			error = "Transport is not running.";
			return false;
		}

		const auto it = m_connections.find(connectionId);
		if (it == m_connections.end()) {
			error = "Connection is not accepted.";
			return false;
		}

		if (m_inboundFrameHandler == nullptr) {
			error = "Inbound frame handler is unavailable.";
			return false;
		}

		const std::string outboundFrame = m_inboundFrameHandler(inboundFrame);
		if (!outboundFrame.empty()) {
         if (it->second.outboundFrames.size() >= kMaxOutboundFramesPerConnection) {
				error = "Outbound frame queue pressure limit reached for in-memory connection.";
				return false;
			}

			it->second.outboundFrames.push_back(outboundFrame);
		}

		error.clear();
		return true;
	}

	std::vector<std::string> GatewayWebSocketTransport::DrainOutboundFrames(
		const std::string& connectionId,
		std::string& error) {
		if (!m_running) {
			error = "Transport is not running.";
			return {};
		}

		const auto it = m_connections.find(connectionId);
		if (it == m_connections.end()) {
			error = "Connection is not accepted.";
			return {};
		}

		std::vector<std::string> drained;
		auto& queue = it->second.outboundFrames;
		drained.reserve(queue.size());

		while (!queue.empty()) {
			drained.push_back(std::move(queue.front()));
			queue.pop_front();
		}

		error.clear();
		return drained;
	}

	bool GatewayWebSocketTransport::PumpNetworkOnce(std::string& error) {
		if (!m_running) {
			error = "Transport is not running.";
			return false;
		}

		if (!AcceptPendingNetworkConnections(error)) {
			return false;
		}

		for (auto it = m_connections.begin(); it != m_connections.end();) {
			auto& session = it->second;
			if (!session.isNetworkConnection) {
				++it;
				continue;
			}

			if (!PumpNetworkConnection(session, error)) {
				CloseSocketIfNeeded(session.socket);
				return false;
			}

			if (session.socket == INVALID_SOCKET) {
				it = m_connections.erase(it);
				continue;
			}

			++it;
		}

		error.clear();
		return true;
	}

	bool GatewayWebSocketTransport::IsRunning() const noexcept {
		return m_running;
	}

	std::string GatewayWebSocketTransport::Endpoint() const {
		if (!m_running) {
			return {};
		}

		return "ws://" + m_bindAddress + ":" + std::to_string(m_port);
	}

	std::size_t GatewayWebSocketTransport::ConnectionCount() const noexcept {
		return m_connections.size();
	}

	std::uint64_t GatewayWebSocketTransport::HandshakeTimeoutCount() const noexcept {
		return m_handshakeTimeoutCount;
	}

	std::uint64_t GatewayWebSocketTransport::IdleTimeoutCloseCount() const noexcept {
		return m_idleTimeoutCloseCount;
	}

	std::uint64_t GatewayWebSocketTransport::InvalidUtf8CloseCount() const noexcept {
		return m_invalidUtf8CloseCount;
	}

	std::uint64_t GatewayWebSocketTransport::MessageTooBigCloseCount() const noexcept {
		return m_messageTooBigCloseCount;
	}

	std::uint64_t GatewayWebSocketTransport::ExtensionRejectCount() const noexcept {
		return m_extensionRejectCount;
	}

	bool GatewayWebSocketTransport::IsPlausibleBindAddress(const std::string& bindAddress) {
		if (bindAddress.empty()) {
			return false;
		}

		for (const char ch : bindAddress) {
			const bool isAllowed =
				std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '.' || ch == '-' || ch == ':';
			if (!isAllowed) {
				return false;
			}
		}

		return true;
	}

	bool GatewayWebSocketTransport::TryExtractHttpHeader(
		const std::string& httpText,
		const std::string& headerName,
		std::string& headerValue) {
		const std::string lowerText = ToLowerCopy(httpText);
		const std::string token = ToLowerCopy(headerName) + ":";

		const std::size_t begin = lowerText.find(token);
		if (begin == std::string::npos) {
			return false;
		}

		const std::size_t valueBegin = begin + token.size();
		std::size_t valueEnd = lowerText.find("\r\n", valueBegin);
		if (valueEnd == std::string::npos) {
			valueEnd = lowerText.size();
		}

		std::size_t start = valueBegin;
		while (start < valueEnd && std::isspace(static_cast<unsigned char>(httpText[start])) != 0) {
			++start;
		}

		std::size_t end = valueEnd;
		while (end > start && std::isspace(static_cast<unsigned char>(httpText[end - 1])) != 0) {
			--end;
		}

		headerValue = httpText.substr(start, end - start);
		return !headerValue.empty();
	}

	bool GatewayWebSocketTransport::BuildWebSocketAcceptValue(const std::string& key, std::string& acceptValue) {
		constexpr char kGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		const std::string source = key + kGuid;

		HCRYPTPROV provider = 0;
		HCRYPTHASH hash = 0;
		std::array<std::uint8_t, 20> digest{};
		DWORD digestLength = static_cast<DWORD>(digest.size());

		if (!CryptAcquireContextA(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
			return false;
		}

		bool success = false;
		do {
			if (!CryptCreateHash(provider, CALG_SHA1, 0, 0, &hash)) {
				break;
			}

			if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(source.data()), static_cast<DWORD>(source.size()), 0)) {
				break;
			}

			if (!CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestLength, 0)) {
				break;
			}

			DWORD outputLength = 0;
			if (!CryptBinaryToStringA(
				digest.data(),
				digestLength,
				CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
				nullptr,
				&outputLength)) {
				break;
			}

			std::string encoded(outputLength, '\0');
			if (!CryptBinaryToStringA(
				digest.data(),
				digestLength,
				CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
				encoded.data(),
				&outputLength)) {
				break;
			}

			if (!encoded.empty() && encoded.back() == '\0') {
				encoded.pop_back();
			}

			acceptValue = std::move(encoded);
			success = true;
		} while (false);

		if (hash != 0) {
			CryptDestroyHash(hash);
		}

		if (provider != 0) {
			CryptReleaseContext(provider, 0);
		}

		return success;
	}

	bool GatewayWebSocketTransport::TryExtractClientFrame(
		std::string& buffer,
		NetworkFrame& frame,
		std::string& error) {
		if (buffer.size() < 2) {
			error.clear();
			return false;
		}

		const std::uint8_t b0 = static_cast<std::uint8_t>(buffer[0]);
		const std::uint8_t b1 = static_cast<std::uint8_t>(buffer[1]);

		frame.fin = (b0 & 0x80) != 0;
		frame.opcode = static_cast<std::uint8_t>(b0 & 0x0F);

		if ((b0 & 0x70) != 0) {
			error = "RSV bits are set but no negotiated websocket extensions are active.";
			return false;
		}

		const bool masked = (b1 & 0x80) != 0;
		if (!masked) {
			error = "Client frame must be masked.";
			return false;
		}

		std::size_t offset = 2;
		std::uint64_t payloadLength = static_cast<std::uint64_t>(b1 & 0x7F);

		if (payloadLength == 126) {
			if (buffer.size() < offset + 2) {
				error.clear();
				return false;
			}

			payloadLength =
				(static_cast<std::uint64_t>(static_cast<std::uint8_t>(buffer[offset])) << 8) |
				static_cast<std::uint64_t>(static_cast<std::uint8_t>(buffer[offset + 1]));
			offset += 2;
		}
		else if (payloadLength == 127) {
			if (buffer.size() < offset + 8) {
				error.clear();
				return false;
			}

			payloadLength = 0;
			for (int i = 0; i < 8; ++i) {
				payloadLength <<= 8;
				payloadLength |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(buffer[offset + i]));
			}

			offset += 8;
		}

		if (payloadLength > kMaxFramePayloadBytes) {
			error = "Frame payload exceeds configured maximum size.";
			return false;
		}

		if (buffer.size() < offset + 4) {
			error.clear();
			return false;
		}

		const std::array<std::uint8_t, 4> mask = {
			static_cast<std::uint8_t>(buffer[offset + 0]),
			static_cast<std::uint8_t>(buffer[offset + 1]),
			static_cast<std::uint8_t>(buffer[offset + 2]),
			static_cast<std::uint8_t>(buffer[offset + 3]),
		};

		offset += 4;
		if (buffer.size() < offset + static_cast<std::size_t>(payloadLength)) {
			error.clear();
			return false;
		}

		frame.payload.assign(static_cast<std::size_t>(payloadLength), '\0');
		for (std::size_t i = 0; i < static_cast<std::size_t>(payloadLength); ++i) {
			frame.payload[i] = static_cast<char>(
				static_cast<std::uint8_t>(buffer[offset + i]) ^ mask[i % 4]);
		}

		buffer.erase(0, offset + static_cast<std::size_t>(payloadLength));
		error.clear();
		return true;
	}

	std::string GatewayWebSocketTransport::EncodeServerFrame(
		std::uint8_t opcode,
		bool fin,
		const std::string& payload) {
		const std::uint64_t length = static_cast<std::uint64_t>(payload.size());

		std::string frame;
		frame.reserve(payload.size() + 16);

		const std::uint8_t firstByte = static_cast<std::uint8_t>((fin ? 0x80 : 0x00) | (opcode & 0x0F));
		frame.push_back(static_cast<char>(firstByte));

		if (length <= 125) {
			frame.push_back(static_cast<char>(length));
		}
		else if (length <= 0xFFFF) {
			frame.push_back(static_cast<char>(126));
			frame.push_back(static_cast<char>((length >> 8) & 0xFF));
			frame.push_back(static_cast<char>(length & 0xFF));
		}
		else {
			frame.push_back(static_cast<char>(127));
			for (int i = 7; i >= 0; --i) {
				frame.push_back(static_cast<char>((length >> (i * 8)) & 0xFF));
			}
		}

		frame += payload;
		return frame;
	}

	void GatewayWebSocketTransport::CloseSocketIfNeeded(SOCKET& socket) {
		if (socket != INVALID_SOCKET) {
			closesocket(socket);
			socket = INVALID_SOCKET;
		}
	}

	bool GatewayWebSocketTransport::AcceptPendingNetworkConnections(std::string& error) {
		while (true) {
			SOCKET accepted = accept(m_listenSocket, nullptr, nullptr);
			if (accepted == INVALID_SOCKET) {
				const int code = WSAGetLastError();
				if (code == WSAEWOULDBLOCK) {
					error.clear();
					return true;
				}

				error = ToWsaErrorText("accept() failed.", code);
				return false;
			}

			u_long nonBlocking = 1;
            std::string acceptedSocketPolicyError;
			if (!ConfigureAcceptedSocket(accepted, acceptedSocketPolicyError)) {
				error = acceptedSocketPolicyError;
				closesocket(accepted);
				return false;
			}

			if (ioctlsocket(accepted, FIONBIO, &nonBlocking) != 0) {
				error = ToWsaErrorText("ioctlsocket(FIONBIO) for accepted socket failed.", WSAGetLastError());
				closesocket(accepted);
				return false;
			}

			ConnectionSession session{};
			session.isNetworkConnection = true;
			session.socket = accepted;
			session.acceptedAtMs = NowMs();
			session.lastActivityAtMs = session.acceptedAtMs;

			const std::string id = "net-" + std::to_string(m_nextConnectionId++);
			m_connections.insert_or_assign(id, std::move(session));
		}
	}

	bool GatewayWebSocketTransport::PumpNetworkConnection(ConnectionSession& session, std::string& error) {
        const std::uint64_t now = NowMs();
		if (!session.handshakeComplete && session.acceptedAtMs != 0 && now > session.acceptedAtMs &&
			now - session.acceptedAtMs > kHandshakeTimeoutMs) {
            ++m_handshakeTimeoutCount;
			CloseSocketIfNeeded(session.socket);
			error.clear();
			return true;
		}

		if (session.handshakeComplete && !session.closeAfterFlush && !session.timeoutCloseQueued &&
			session.lastActivityAtMs != 0 && now > session.lastActivityAtMs &&
			now - session.lastActivityAtMs > kIdleConnectionTimeoutMs) {
			if (!TryQueueNetworkFrame(
				session,
				EncodeServerFrame(
					0x8,
					true,
					BuildClosePayload(kCloseCodeGoingAway, "Idle timeout")),
				error)) {
				return false;
			}

			session.timeoutCloseQueued = true;
			session.closeAfterFlush = true;
           ++m_idleTimeoutCloseCount;
		}

		std::array<char, 4096> recvBuffer{};

		while (true) {
			const int bytesRead = recv(session.socket, recvBuffer.data(), static_cast<int>(recvBuffer.size()), 0);
			if (bytesRead > 0) {
              session.lastActivityAtMs = NowMs();
              if (session.readBuffer.size() + static_cast<std::size_t>(bytesRead) > kMaxReadBufferBytes) {
                 ++m_messageTooBigCloseCount;
					std::string queueError;
					if (!TryQueueNetworkFrame(
						session,
						EncodeServerFrame(
							0x8,
							true,
							BuildClosePayload(kCloseCodeMessageTooBig, "Read buffer limit exceeded")),
						queueError)) {
						error = queueError;
						return false;
					}

					session.readBuffer.clear();
					session.closeAfterFlush = true;
					break;
				}

				session.readBuffer.append(recvBuffer.data(), static_cast<std::size_t>(bytesRead));
				continue;
			}

			if (bytesRead == 0) {
				CloseSocketIfNeeded(session.socket);
				error.clear();
				return true;
			}

			const int code = WSAGetLastError();
			if (code == WSAEWOULDBLOCK) {
				break;
			}

			error = ToWsaErrorText("recv() failed.", code);
			return false;
		}

		if (!session.handshakeComplete) {
			if (!TryCompleteNetworkHandshake(session, error)) {
				return false;
			}
		}

		if (!session.handshakeComplete) {
			error.clear();
			return true;
		}

		while (true) {
			NetworkFrame frame;
			std::string frameError;
			if (!TryExtractClientFrame(session.readBuffer, frame, frameError)) {
				if (!frameError.empty()) {
					error = frameError;
					return false;
				}

				break;
			}

			if (frame.opcode == 0x8) {
                if (!TryQueueNetworkFrame(session, EncodeServerFrame(0x8, true, frame.payload), error)) {
					return false;
				}
				session.closeAfterFlush = true;
				continue;
			}

			if (frame.opcode == 0x9) {
                if (!TryQueueNetworkFrame(session, EncodeServerFrame(0xA, true, frame.payload), error)) {
					return false;
				}
				continue;
			}

			if (frame.opcode == 0xA) {
				continue;
			}

            std::string assembledText;
			std::string assembledBinary;
			bool hasCompletedTextMessage = false;
          bool hasCompletedBinaryMessage = false;
			if (frame.opcode == 0x1) {
				if (session.awaitingContinuation) {
					error = "Received new text frame while continuation sequence is in progress.";
					return false;
				}

				if (frame.fin) {
					assembledText = frame.payload;
                   hasCompletedTextMessage = true;
				}
				else {
					session.fragmentedTextBuffer = frame.payload;
                    session.fragmentedBinaryBuffer.clear();
                   if (session.fragmentedTextBuffer.size() > kMaxFragmentBufferBytes) {
                      ++m_messageTooBigCloseCount;
						if (!TryQueueNetworkFrame(
							session,
							EncodeServerFrame(
								0x8,
								true,
								BuildClosePayload(kCloseCodeMessageTooBig, "Fragmented text message too large")),
							error)) {
							return false;
						}

						session.closeAfterFlush = true;
						continue;
					}

					session.continuationOpcode = 0x1;
					session.awaitingContinuation = true;
				}
			}
         else if (frame.opcode == 0x2) {
				if (session.awaitingContinuation) {
					error = "Received binary frame while continuation sequence is in progress.";
					return false;
				}

               if (frame.fin) {
					assembledBinary = frame.payload;
					hasCompletedBinaryMessage = true;
				}
				else {
					session.fragmentedBinaryBuffer = frame.payload;
					session.fragmentedTextBuffer.clear();
                   if (session.fragmentedBinaryBuffer.size() > kMaxFragmentBufferBytes) {
                      ++m_messageTooBigCloseCount;
						if (!TryQueueNetworkFrame(
							session,
							EncodeServerFrame(
								0x8,
								true,
								BuildClosePayload(kCloseCodeMessageTooBig, "Fragmented binary message too large")),
							error)) {
							return false;
						}

						session.closeAfterFlush = true;
						continue;
					}

					session.continuationOpcode = 0x2;
					session.awaitingContinuation = true;
				}
			}
			else if (frame.opcode == 0x0) {
				if (!session.awaitingContinuation) {
					error = "Received continuation frame without an active fragmented message.";
					return false;
				}

              if (session.continuationOpcode == 0x1) {
					session.fragmentedTextBuffer += frame.payload;
                    if (session.fragmentedTextBuffer.size() > kMaxFragmentBufferBytes) {
                      ++m_messageTooBigCloseCount;
						if (!TryQueueNetworkFrame(
							session,
							EncodeServerFrame(
								0x8,
								true,
								BuildClosePayload(kCloseCodeMessageTooBig, "Fragmented text message too large")),
							error)) {
							return false;
						}

						session.closeAfterFlush = true;
						continue;
					}

					if (frame.fin) {
						assembledText = session.fragmentedTextBuffer;
						session.fragmentedTextBuffer.clear();
						session.awaitingContinuation = false;
						session.continuationOpcode = 0;
						hasCompletedTextMessage = true;
					}
				}
				else if (session.continuationOpcode == 0x2) {
					session.fragmentedBinaryBuffer += frame.payload;
                    if (session.fragmentedBinaryBuffer.size() > kMaxFragmentBufferBytes) {
                      ++m_messageTooBigCloseCount;
						if (!TryQueueNetworkFrame(
							session,
							EncodeServerFrame(
								0x8,
								true,
								BuildClosePayload(kCloseCodeMessageTooBig, "Fragmented binary message too large")),
							error)) {
							return false;
						}

						session.closeAfterFlush = true;
						continue;
					}

					if (frame.fin) {
						assembledBinary = session.fragmentedBinaryBuffer;
						session.fragmentedBinaryBuffer.clear();
						session.awaitingContinuation = false;
						session.continuationOpcode = 0;
						hasCompletedBinaryMessage = true;
					}
				}
				else {
					error = "Received continuation frame with unknown message opcode state.";
					return false;
				}
			}
			else {
				error = "Unsupported WebSocket opcode.";
				return false;
			}

           if (hasCompletedTextMessage && !IsValidUtf8(assembledText)) {
              ++m_invalidUtf8CloseCount;
                if (!TryQueueNetworkFrame(
					session,
					EncodeServerFrame(
						0x8,
						true,
						BuildClosePayload(kCloseCodeInvalidPayloadData, "Invalid UTF-8 in text frame")),
					error)) {
					return false;
				}

				session.closeAfterFlush = true;
				continue;
			}

			if (!assembledText.empty() && m_inboundFrameHandler != nullptr) {
				const std::string outbound = m_inboundFrameHandler(assembledText);
				if (!outbound.empty()) {
                 if (!TryQueueApplicationOutbound(session, outbound, 0x1, error)) {
						return false;
					}
				}
			}

			if (hasCompletedBinaryMessage && m_inboundFrameHandler != nullptr) {
				const std::string outbound = m_inboundFrameHandler(assembledBinary);
				if (!outbound.empty()) {
                 if (!TryQueueApplicationOutbound(session, outbound, 0x2, error)) {
						return false;
					}
				}
			}
		}

		if (!FlushNetworkOutbound(session, error)) {
			return false;
		}

		if (session.closeAfterFlush && session.outboundNetworkFrames.empty()) {
			CloseSocketIfNeeded(session.socket);
		}

		error.clear();
		return true;
	}

	bool GatewayWebSocketTransport::TryCompleteNetworkHandshake(ConnectionSession& session, std::string& error) {
		const std::size_t headerEnd = session.readBuffer.find("\r\n\r\n");
		if (headerEnd == std::string::npos) {
			error.clear();
			return true;
		}

		const std::string request = session.readBuffer.substr(0, headerEnd + 4);
		session.readBuffer.erase(0, headerEnd + 4);

		const std::size_t requestLineEnd = request.find("\r\n");
		if (requestLineEnd == std::string::npos) {
			error = "Malformed HTTP request line for websocket handshake.";
			return false;
		}

		const std::string requestLine = request.substr(0, requestLineEnd);

		const std::string lowerRequest = ToLowerCopy(request);
      if (lowerRequest.rfind("get ", 0) != 0 || lowerRequest.find(" http/1.1\r\n") == std::string::npos) {
	error = "Handshake must be an HTTP/1.1 GET request.";
	return false;
     }

		if (!IsAllowedRequestTarget(requestLine)) {
			error = "Handshake request target is not allowed for gateway endpoint.";
			return false;
		}

     std::string host;
		if (!TryExtractHttpHeader(request, "Host", host) || host.empty()) {
			error = "Missing `Host` header.";
			return false;
		}

		std::string upgradeHeader;
		if (!TryExtractHttpHeader(request, "Upgrade", upgradeHeader) || ToLowerCopy(upgradeHeader) != "websocket") {
			error = "Missing `Upgrade: websocket` header.";
			return false;
		}

      std::string connectionHeader;
		if (!TryExtractHttpHeader(request, "Connection", connectionHeader) ||
			!HeaderContainsToken(connectionHeader, "upgrade")) {
			error = "Missing `Connection: Upgrade` token.";
			return false;
		}

      std::string version;
		if (!TryExtractHttpHeader(request, "Sec-WebSocket-Version", version) || version != "13") {
			error = "Unsupported or missing `Sec-WebSocket-Version`; expected `13`.";
			return false;
		}

		std::string websocketKey;
		if (!TryExtractHttpHeader(request, "Sec-WebSocket-Key", websocketKey)) {
			error = "Missing `Sec-WebSocket-Key` header.";
			return false;
		}

		if (!IsValidSecWebSocketKey(websocketKey)) {
			error = "Invalid `Sec-WebSocket-Key` header value.";
			return false;
		}

		std::string origin;
		if (TryExtractHttpHeader(request, "Origin", origin) && !IsAllowedOriginValue(origin)) {
			error = "Origin is not permitted by endpoint policy.";
			return false;
		}

		std::string extensions;
		if (TryExtractHttpHeader(request, "Sec-WebSocket-Extensions", extensions) &&
			HeaderContainsTokenPrefix(extensions, "permessage-deflate")) {
           ++m_extensionRejectCount;
			error = "Unsupported websocket extension requested: permessage-deflate.";
			return false;
		}

		std::string acceptValue;
		if (!BuildWebSocketAcceptValue(websocketKey, acceptValue)) {
			error = "Failed to compute Sec-WebSocket-Accept value.";
			return false;
		}

		const std::string response =
			"HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Accept: " +
			acceptValue + "\r\n\r\n";

		const int sent = send(session.socket, response.data(), static_cast<int>(response.size()), 0);
		if (sent == SOCKET_ERROR) {
			error = ToWsaErrorText("send() handshake failed.", WSAGetLastError());
			return false;
		}

		if (sent != static_cast<int>(response.size())) {
			error = "Partial handshake send is not supported in current transport.";
			return false;
		}

		session.handshakeComplete = true;
      session.lastActivityAtMs = NowMs();
		error.clear();
		return true;
	}

	bool GatewayWebSocketTransport::TryQueueNetworkFrame(
		ConnectionSession& session,
		const std::string& bytes,
		std::string& error) {
		if (session.outboundNetworkFrames.size() >= kMaxOutboundNetworkFramesPerConnection) {
			error = "Outbound network frame queue pressure limit reached.";
			return false;
		}

		session.outboundNetworkFrames.push_back(PendingNetworkFrame{ .bytes = bytes, .sentBytes = 0 });
		error.clear();
		return true;
	}

	bool GatewayWebSocketTransport::TryQueueApplicationOutbound(
		ConnectionSession& session,
		const std::string& outbound,
		std::uint8_t opcode,
		std::string& error) {
		if (session.outboundFrames.size() >= kMaxOutboundFramesPerConnection) {
			error = "Outbound application frame queue pressure limit reached.";
			return false;
		}

		session.outboundFrames.push_back(outbound);
		return TryQueueNetworkFrame(session, EncodeServerFrame(opcode, true, outbound), error);
	}

	bool GatewayWebSocketTransport::FlushNetworkOutbound(ConnectionSession& session, std::string& error) {
		while (!session.outboundNetworkFrames.empty()) {
			auto& pending = session.outboundNetworkFrames.front();
			if (pending.sentBytes >= pending.bytes.size()) {
				session.outboundNetworkFrames.pop_front();
				continue;
			}

			const char* data = pending.bytes.data() + pending.sentBytes;
			const int bytesToSend = static_cast<int>(pending.bytes.size() - pending.sentBytes);
			const int sent = send(session.socket, data, bytesToSend, 0);
			if (sent == SOCKET_ERROR) {
				const int code = WSAGetLastError();
				if (code == WSAEWOULDBLOCK) {
					error.clear();
					return true;
				}

				error = ToWsaErrorText("send() frame failed.", code);
				return false;
			}

			if (sent <= 0) {
				error = "Unexpected send() result while flushing outbound frames.";
				return false;
			}

			pending.sentBytes += static_cast<std::size_t>(sent);
            session.lastActivityAtMs = NowMs();
			if (pending.sentBytes >= pending.bytes.size()) {
				session.outboundNetworkFrames.pop_front();
			}
		}

		error.clear();
		return true;
	}

} // namespace blazeclaw::gateway
