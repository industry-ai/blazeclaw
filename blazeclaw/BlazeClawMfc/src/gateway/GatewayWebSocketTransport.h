#pragma once

#include "pch.h"

namespace blazeclaw::gateway {

class GatewayWebSocketTransport {
public:
  using InboundFrameHandler = std::function<std::string(const std::string& inboundFrame)>;

  bool Start(
      const std::string& bindAddress,
      std::uint16_t port,
      InboundFrameHandler inboundFrameHandler,
      std::string& error);
  void Stop();

  bool AcceptConnection(const std::string& connectionId, std::string& error);
  bool ProcessInboundFrame(const std::string& connectionId, const std::string& inboundFrame, std::string& error);
  std::vector<std::string> DrainOutboundFrames(const std::string& connectionId, std::string& error);
  bool PumpNetworkOnce(std::string& error);

  [[nodiscard]] bool IsRunning() const noexcept;
  [[nodiscard]] std::string Endpoint() const;
  [[nodiscard]] std::size_t ConnectionCount() const noexcept;
  [[nodiscard]] std::uint64_t HandshakeTimeoutCount() const noexcept;
  [[nodiscard]] std::uint64_t IdleTimeoutCloseCount() const noexcept;
  [[nodiscard]] std::uint64_t InvalidUtf8CloseCount() const noexcept;
  [[nodiscard]] std::uint64_t MessageTooBigCloseCount() const noexcept;
  [[nodiscard]] std::uint64_t ExtensionRejectCount() const noexcept;

private:
  struct NetworkFrame {
    bool fin = true;
    std::uint8_t opcode = 0;
    std::string payload;
  };

  struct PendingNetworkFrame {
    std::string bytes;
    std::size_t sentBytes = 0;
  };

  struct ConnectionSession {
    bool isNetworkConnection = false;
    bool handshakeComplete = false;
    bool awaitingContinuation = false;
    bool closeAfterFlush = false;
    bool timeoutCloseQueued = false;
    std::uint8_t continuationOpcode = 0;
    std::uint64_t acceptedAtMs = 0;
    std::uint64_t lastActivityAtMs = 0;
    SOCKET socket = INVALID_SOCKET;
    std::string readBuffer;
    std::string fragmentedTextBuffer;
    std::string fragmentedBinaryBuffer;
    std::deque<std::string> outboundFrames;
    std::deque<PendingNetworkFrame> outboundNetworkFrames;
  };

  static bool IsPlausibleBindAddress(const std::string& bindAddress);
  static bool TryExtractHttpHeader(
      const std::string& httpText,
      const std::string& headerName,
      std::string& headerValue);
  static bool BuildWebSocketAcceptValue(const std::string& key, std::string& acceptValue);
  static bool TryExtractClientFrame(std::string& buffer, NetworkFrame& frame, std::string& error);
  static std::string EncodeServerFrame(std::uint8_t opcode, bool fin, const std::string& payload);
  static void CloseSocketIfNeeded(SOCKET& socket);

  bool AcceptPendingNetworkConnections(std::string& error);
  bool PumpNetworkConnection(ConnectionSession& session, std::string& error);
  bool TryCompleteNetworkHandshake(ConnectionSession& session, std::string& error);
  bool FlushNetworkOutbound(ConnectionSession& session, std::string& error);
  bool TryQueueNetworkFrame(ConnectionSession& session, const std::string& bytes, std::string& error);
  bool TryQueueApplicationOutbound(
      ConnectionSession& session,
      const std::string& outbound,
      std::uint8_t opcode,
      std::string& error);

  bool m_running = false;
  std::string m_bindAddress;
  std::uint16_t m_port = 0;
  SOCKET m_listenSocket = INVALID_SOCKET;
  bool m_wsaStarted = false;
  std::uint64_t m_nextConnectionId = 1;
  std::uint64_t m_handshakeTimeoutCount = 0;
  std::uint64_t m_idleTimeoutCloseCount = 0;
  std::uint64_t m_invalidUtf8CloseCount = 0;
  std::uint64_t m_messageTooBigCloseCount = 0;
  std::uint64_t m_extensionRejectCount = 0;
  InboundFrameHandler m_inboundFrameHandler;
  std::unordered_map<std::string, ConnectionSession> m_connections;
};

} // namespace blazeclaw::gateway
