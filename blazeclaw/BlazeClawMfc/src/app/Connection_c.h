#pragma once

#include <string>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  // POSIX sockets
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <unistd.h>
  #include <errno.h>

  using SOCKET = int;
  #ifndef INVALID_SOCKET
    #define INVALID_SOCKET (-1)
  #endif
  #ifndef SOCKET_ERROR
    #define SOCKET_ERROR (-1)
  #endif
  // Provide closesocket alias to minimize platform-specific code in .cpp
  static inline int closesocket(SOCKET s) { return ::close(s); }
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "AppProtoHeader.h"

// 字节序转换函数声明
inline uint64_t hton64(uint64_t value);
inline uint64_t ntoh64(uint64_t value);

// 协议头字段设置和获取函数
inline void AppProtoHeader_SetPayloadLen(AppProtoHeader* h, uint32_t payload_len);
inline uint32_t AppProtoHeader_GetPayloadLen(const AppProtoHeader* h);
inline void AppProtoHeader_SetSeq(AppProtoHeader* h, uint32_t seq);
inline uint32_t AppProtoHeader_GetSeq(const AppProtoHeader* h);
inline void AppProtoHeader_SetTimestampMs(AppProtoHeader* h, uint64_t ts_ms);
inline uint64_t AppProtoHeader_GetTimestampMs(const AppProtoHeader* h);

// Avoid macro collision: some system/third-party headers or project settings may
// define a macro named 'Connect'. Undefine it so the class method `Connect` can exist.
#ifdef Connect
#undef Connect
#endif

class CConnection_c {
public:
    CConnection_c();
    ~CConnection_c();

    // Connect to server.
    // `use_tls`: true = TLS connection (default), false = plaintext TCP
    // `host` may be a DNS name (preferred) or an IP.
    // When `host` is a DNS name, SNI and hostname verification are enabled.
    bool Connect(const std::string& host, int port, bool use_tls = true);

    // Send request and get response
    std::string SendRequest(uint16_t type, const std::string& payload);

    // Disconnect
    void Close();

    // Read a framed message; returns false on EOF/error.
    bool ReadMessage(AppProtoHeader& outHeader, std::string& outPayload);

    // Get the session ID from the last response
    uint64_t GetLastResponseSessionId() const {
        return last_response_session_id_;
    }

    // Set session ID for outgoing requests (used for device binding, session_id = 0)
    void SetSessionId(uint64_t session_id) {
        next_session_id_ = session_id;
    }

private:
    SOCKET sock_;
    bool is_connected_;
    bool use_tls_;
    SSL_CTX* ssl_ctx_;
    SSL* ssl_;

    uint64_t conn_id_;
    uint32_t next_seq_;
    uint64_t last_response_session_id_ = 0;
    uint64_t next_session_id_ = 0;  // 下一个请求使用的 session_id（设备绑定时设为0）

    bool send_all(const char* data, int len);
    bool recv_all(char* buf, int len);

    void initOpenSSL();
    void cleanupOpenSSL();
};


