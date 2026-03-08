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
		m_listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (m_listenSocket == INVALID_SOCKET) {
			error = ToWsaErrorText("socket() failed.", WSAGetLastError());
			Stop();
			return false;
		}

		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		const int ptonResult = InetPtonA(AF_INET, bindAddress.c_str(), &addr.sin_addr);
		if (ptonResult != 1) {
			error = "Bind address must be an IPv4 literal for current transport implementation.";
			Stop();
			return false;
		}

		if (bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
			error = ToWsaErrorText("bind() failed.", WSAGetLastError());
			Stop();
			return false;
		}

		if (listen(m_listenSocket, SOMAXCONN) == SOCKET_ERROR) {
			error = ToWsaErrorText("listen() failed.", WSAGetLastError());
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
			if (ioctlsocket(accepted, FIONBIO, &nonBlocking) != 0) {
				error = ToWsaErrorText("ioctlsocket(FIONBIO) for accepted socket failed.", WSAGetLastError());
				closesocket(accepted);
				return false;
			}

			ConnectionSession session{};
			session.isNetworkConnection = true;
			session.socket = accepted;

			const std::string id = "net-" + std::to_string(m_nextConnectionId++);
			m_connections.insert_or_assign(id, std::move(session));
		}
	}

	bool GatewayWebSocketTransport::PumpNetworkConnection(ConnectionSession& session, std::string& error) {
		std::array<char, 4096> recvBuffer{};

		while (true) {
			const int bytesRead = recv(session.socket, recvBuffer.data(), static_cast<int>(recvBuffer.size()), 0);
			if (bytesRead > 0) {
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
				session.outboundNetworkFrames.push_back(
					PendingNetworkFrame{ .bytes = EncodeServerFrame(0x8, true, frame.payload), .sentBytes = 0 });
				session.closeAfterFlush = true;
				continue;
			}

			if (frame.opcode == 0x9) {
				session.outboundNetworkFrames.push_back(
					PendingNetworkFrame{ .bytes = EncodeServerFrame(0xA, true, frame.payload), .sentBytes = 0 });
				continue;
			}

			if (frame.opcode == 0xA) {
				continue;
			}

			std::string assembledText;
			if (frame.opcode == 0x1) {
				if (session.awaitingContinuation) {
					error = "Received new text frame while continuation sequence is in progress.";
					return false;
				}

				if (frame.fin) {
					assembledText = frame.payload;
				}
				else {
					session.fragmentedTextBuffer = frame.payload;
					session.awaitingContinuation = true;
				}
			}
			else if (frame.opcode == 0x0) {
				if (!session.awaitingContinuation) {
					error = "Received continuation frame without an active fragmented message.";
					return false;
				}

				session.fragmentedTextBuffer += frame.payload;
				if (frame.fin) {
					assembledText = session.fragmentedTextBuffer;
					session.fragmentedTextBuffer.clear();
					session.awaitingContinuation = false;
				}
			}
			else {
				error = "Unsupported WebSocket opcode.";
				return false;
			}

			if (!assembledText.empty() && m_inboundFrameHandler != nullptr) {
				const std::string outbound = m_inboundFrameHandler(assembledText);
				if (!outbound.empty()) {
					session.outboundFrames.push_back(outbound);
					session.outboundNetworkFrames.push_back(
						PendingNetworkFrame{ .bytes = EncodeServerFrame(0x1, true, outbound), .sentBytes = 0 });
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

		const std::string lowerRequest = ToLowerCopy(request);
  if (lowerRequest.rfind("get ", 0) != 0 || lowerRequest.find(" http/1.1\r\n") == std::string::npos) {
	error = "Handshake must be an HTTP/1.1 GET request.";
	return false;
  }

		if (lowerRequest.find("upgrade: websocket") == std::string::npos) {
			error = "Missing `Upgrade: websocket` header.";
			return false;
		}

  if (lowerRequest.find("connection: upgrade") == std::string::npos) {
	error = "Missing `Connection: Upgrade` header.";
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
		error.clear();
		return true;
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
			if (pending.sentBytes >= pending.bytes.size()) {
				session.outboundNetworkFrames.pop_front();
			}
		}

		error.clear();
		return true;
	}

} // namespace blazeclaw::gateway
