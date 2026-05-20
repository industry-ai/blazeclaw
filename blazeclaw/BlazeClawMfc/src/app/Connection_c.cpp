#include "pch.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "Connection_c.h"
#include "Logger.h"

#include <cstdint>

#ifdef _WIN32
  #include <windows.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  #pragma comment(lib, "libssl.lib")
  #pragma comment(lib, "libcrypto.lib")
#else
  #include <sys/time.h>
#endif

#include <cstring>
#include <vector>
#include <iostream>
#include <chrono>
#include <thread>
#include <cerrno>
#include <algorithm>

// Portable 64-bit hton/ntoh helpers for timestamp
inline uint64_t hton64(uint64_t host64) {
    // Break into two 32-bit halves and use htonl
    uint32_t hi = static_cast<uint32_t>(host64 >> 32);
    uint32_t lo = static_cast<uint32_t>(host64 & 0xFFFFFFFFu);
    uint32_t nhi = htonl(hi);
    uint32_t nlo = htonl(lo);
    return (static_cast<uint64_t>(nhi) << 32) | static_cast<uint64_t>(nlo);
}

inline uint64_t ntoh64(uint64_t net64) {
    uint32_t hi = static_cast<uint32_t>(net64 >> 32);
    uint32_t lo = static_cast<uint32_t>(net64 & 0xFFFFFFFFu);
    uint32_t hhi = ntohl(hi);
    uint32_t hlo = ntohl(lo);
    return (static_cast<uint64_t>(hlo) << 32) | static_cast<uint64_t>(hhi);
}

// Convenience getters/setters for header numeric fields (explicitly use network order on-wire)
inline void AppProtoHeader_SetPayloadLen(AppProtoHeader* h, uint32_t payload_len) {
    h->payload_len = htonl(payload_len);
}
inline uint32_t AppProtoHeader_GetPayloadLen(const AppProtoHeader* h) {
    return ntohl(h->payload_len);
}
inline void AppProtoHeader_SetSeq(AppProtoHeader* h, uint32_t seq) {
    h->seq = htonl(seq);
}
inline uint32_t AppProtoHeader_GetSeq(const AppProtoHeader* h) {
    return ntohl(h->seq);
}
inline void AppProtoHeader_SetTimestampMs(AppProtoHeader* h, uint64_t ts_ms) {
    h->timestamp_ms = hton64(ts_ms);
}
inline uint64_t AppProtoHeader_GetTimestampMs(const AppProtoHeader* h) {
    return ntoh64(h->timestamp_ms);
}
inline void AppProtoHeader_SetSessionId(AppProtoHeader* h, uint64_t session_id) {
    h->session_id = hton64(session_id);
}
inline uint64_t AppProtoHeader_GetSessionId(const AppProtoHeader* h) {
    return ntoh64(h->session_id);
}

namespace {

    // Delegate to header-level helper generated from the X-macro list.
    inline bool is_known_msg_type(uint8_t t) {
        return IsKnownMsgType(static_cast<MsgType>(t));
    }

    static void drain_openssl_error_stack(std::ostream& os) {
        unsigned long e = 0;
        char buf[256];
        bool any = false;

        while ((e = ERR_get_error()) != 0) {
            any = true;
            ERR_error_string_n(e, buf, sizeof(buf));
            os << "\n[Error] OpenSSL: " << buf;
        }

        if (!any) {
            os << "\n[Error] OpenSSL: (no error details in error queue)";
        }
        os << std::endl;
    }

#ifdef _WIN32
    inline int get_last_error() { return WSAGetLastError(); }
    static std::string last_error_msg(int err) {
        LPSTR errorMsg = nullptr;
        DWORD msgLen = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPSTR>(&errorMsg), 0, nullptr);
        std::string s;
        if (msgLen > 0 && errorMsg) {
            s.assign(errorMsg, msgLen);
            LocalFree(errorMsg);
        } else {
            s = "Unknown error";
        }
        return s;
    }
#else
    inline int get_last_error() { return errno; }
    static std::string last_error_msg(int err) {
        char buf[256];
#if defined(__GNUC__) && ((_GNU_SOURCE) || (_POSIX_C_SOURCE >= 200112L))
        // GNU strerror_r returns char*, XSI strerror_r returns int
        char* r = strerror_r(err, buf, sizeof(buf));
        return std::string(r);
#else
        if (strerror_r(err, buf, sizeof(buf)) == 0) return std::string(buf);
        return std::string("Unknown error");
#endif
    }
#endif

    static uint64_t now_ms() {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    static std::string redact_json_like(std::string s) {
        const char* keys[] = {
            "\"passwd\"", "\"password\"",
            "\"token\"", "\"access_token\"",
            "\"sessionToken\"", "\"session_token\"",
            "\"refresh_token\""
        };

        for (const char* key : keys) {
            std::size_t pos = 0;
            while ((pos = s.find(key, pos)) != std::string::npos) {
                std::size_t colon = s.find(':', pos);
                if (colon == std::string::npos) break;

                std::size_t q1 = s.find('\"', colon + 1);
                if (q1 == std::string::npos) { pos = colon + 1; continue; }

                std::size_t q2 = s.find('\"', q1 + 1);
                if (q2 == std::string::npos) { pos = q1 + 1; continue; }

                s.replace(q1 + 1, q2 - (q1 + 1), "***");
                pos = q2 + 1;
            }
        }
        return s;
    }

    static std::string payload_preview(const std::string& payload, std::size_t maxBytes = 256) {
        std::size_t len = (maxBytes < payload.size()) ? maxBytes : payload.size();
        std::string s = payload.substr(0, len);
        s = redact_json_like(std::move(s));
        if (payload.size() > maxBytes) s += "...";
        return s;
    }

    static std::string try_get_peer(SOCKET s) {
        sockaddr_in addr{};
#ifdef _WIN32
        int len = sizeof(addr);
#else
        socklen_t len = sizeof(addr);
#endif
        if (getpeername(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            return "unknown";
        }

        char ip[INET_ADDRSTRLEN]{};
        const char* r = inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
        if (!r) return "unknown";

        return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
    }

    static bool is_ipv4_literal(const std::string& host) {
        in_addr a{};
        return inet_pton(AF_INET, host.c_str(), &a) == 1;
    }

    static bool configure_sni_and_verify_host(SSL* ssl, const std::string& host) {
        if (!ssl) return false;
        if (host.empty()) return false;

        // Enable RFC2818 verification: configure expected DNS name or IP address.
        X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
        if (!param) return false;

        // Some OpenSSL builds don't expose certain hostflag constants; hostname/IP checks
        // are still enforced via set1_host/set1_ip.
#if defined(X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS)
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
#endif

        if (is_ipv4_literal(host)) {
            in_addr a{};
            if (inet_pton(AF_INET, host.c_str(), &a) != 1) return false;

            if (X509_VERIFY_PARAM_set1_ip(param,
                                          reinterpret_cast<const unsigned char*>(&a),
                                          sizeof(a)) != 1) {
                return false;
            }
            return true;
        }

        // DNS name: set SNI and verify hostname.
        if (SSL_set_tlsext_host_name(ssl, host.c_str()) != 1) {
            return false;
        }
        if (X509_VERIFY_PARAM_set1_host(param, host.c_str(), 0) != 1) {
            return false;
        }
        return true;
    }
}

CConnection_c::CConnection_c()
    : sock_(INVALID_SOCKET),
      is_connected_(false),
      use_tls_(false),
      ssl_ctx_(nullptr),
      ssl_(nullptr),
      conn_id_(now_ms()),
      next_seq_(1) {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
    initOpenSSL();
}

CConnection_c::~CConnection_c() {
    Close();
    cleanupOpenSSL();
#ifdef _WIN32
    WSACleanup();
#endif
}

void CConnection_c::initOpenSSL() {
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx_) {
        LOG_ERROR("conn={} SSL_CTX_new failed", conn_id_);
        return;
    }

    // 在 initOpenSSL 方法中
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);

    //// Enable certificate verification (required for real TLS security).
    //SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);

    //// Load default system trust store (works on most platforms when OpenSSL is configured accordingly).
    //if (SSL_CTX_set_default_verify_paths(ssl_ctx_) != 1) {
    //    LOG_ERROR("conn={} SSL_CTX_set_default_verify_paths failed", conn_id_);
    //    drain_openssl_error_stack(std::cerr);
    //}
}

void CConnection_c::cleanupOpenSSL() {
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }

    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
}

bool CConnection_c::Connect(const std::string& host, int port, bool use_tls) {
    // use_tls: true = TLS connection, false = plaintext TCP
    use_tls_ = use_tls;

    if (is_connected_) {
        Close();
    }

    const uint64_t t0 = now_ms();
    const uint64_t t_connect_start = now_ms();

    sock_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_ == INVALID_SOCKET) {
        LOG_ERROR("conn={} connect {}:{} socket() failed err={}", conn_id_, host, port, get_last_error());
        return false;
    }

#ifdef _WIN32
    DWORD timeout = 5000; // milliseconds
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    // POSIX: timeval
    struct timeval tv;
    tv.tv_sec = 5; // 5 seconds
    tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif

    // Resolve DNS name or accept IPv4 literal.
    const bool host_is_ip = is_ipv4_literal(host);

    in_addr addr4{};
    if (host_is_ip) {
        if (inet_pton(AF_INET, host.c_str(), &addr4) != 1) {
            LOG_ERROR("conn={} invalid ip={}", conn_id_, host);
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }
    } else {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* res = nullptr;
        const int gai_rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
        if (gai_rc != 0 || !res) {
#ifdef _WIN32
            LOG_ERROR("conn={} getaddrinfo failed host={} rc={}", conn_id_, host, gai_rc);
#else
            LOG_ERROR("conn={} getaddrinfo failed host={} rc={} ({})", conn_id_, host, gai_rc, gai_strerror(gai_rc));
#endif
            if (res) freeaddrinfo(res);
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        addr4 = (reinterpret_cast<sockaddr_in*>(res->ai_addr))->sin_addr;
        freeaddrinfo(res);
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(static_cast<u_short>(port));
    serverAddr.sin_addr = addr4;

    LOG_INFO("Connecting {}:{} (TLS)...", host, port);

    if (connect(sock_, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        int error = get_last_error();
        LOG_ERROR("底层socket连接失败（TLS前置），错误码: {}", error);

#ifdef _WIN32
        if (error == WSAETIMEDOUT) {
            LOG_ERROR(" (连接超时 - 5秒内无法连接到服务器)");
        } else if (error == WSAECONNREFUSED) {
            LOG_ERROR(" (连接被拒绝 - 服务器可能未启动或端口未监听)");
        } else if (error == WSAEHOSTUNREACH) {
            LOG_ERROR(" (主机不可达 - 网络路由问题)");
        } else if (error == WSAENETUNREACH) {
            LOG_ERROR(" (网络不可达)");
        } else if (error == WSAEINVAL) {
            LOG_ERROR(" (无效参数)");
        } else if (error == WSAEFAULT) {
            LOG_ERROR(" (地址错误)");
        }
        LOG_ERROR("系统错误描述: {}", last_error_msg(error));
#else
        if (error == ETIMEDOUT) {
            LOG_ERROR(" (连接超时 - 5秒内无法连接到服务器)");
        } else if (error == ECONNREFUSED) {
            LOG_ERROR(" (连接被拒绝 - 服务器可能未启动或端口未监听)");
        } else if (error == EHOSTUNREACH) {
            LOG_ERROR(" (主机不可达 - 网络路由问题)");
        } else if (error == ENETUNREACH) {
            LOG_ERROR(" (网络不可达)");
        } else if (error == EINVAL) {
            LOG_ERROR(" (无效参数)");
        } else if (error == EFAULT) {
            LOG_ERROR(" (地址错误)");
        }
        LOG_ERROR("系统错误描述: {}", last_error_msg(error));
#endif

        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    const uint64_t t_connect_end = now_ms();
    LOG_INFO("conn={} TCP connected peer={} connect_ms={}", conn_id_, try_get_peer(sock_),
             (t_connect_end - t_connect_start));

    if (use_tls_) {
        // TLS mode: perform TLS handshake
        if (!ssl_ctx_) {
            LOG_ERROR("conn={} SSL_CTX not initialized", conn_id_);
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        ssl_ = SSL_new(ssl_ctx_);
        if (!ssl_) {
            LOG_ERROR("conn={} SSL_new failed", conn_id_);
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        // Configure SNI + hostname/IP verification before handshake.
        if (!configure_sni_and_verify_host(ssl_, host)) {
            LOG_ERROR("conn={} TLS SNI/hostname verification setup failed host={}", conn_id_, host);
            drain_openssl_error_stack(std::cerr);
            Close();
            return false;
        }

        SSL_set_fd(ssl_, static_cast<int>(sock_));

        const uint64_t t_hs_start = now_ms();
        LOG_INFO("conn={} TLS handshake start", conn_id_);

        const int ssl_connect_result = SSL_connect(ssl_);
        if (ssl_connect_result != 1) {
            const int ssl_error = SSL_get_error(ssl_, ssl_connect_result);
            LOG_ERROR("conn={} TLS handshake failed ssl_err={}", conn_id_, ssl_error);
            drain_openssl_error_stack(std::cerr);

            SSL_free(ssl_);
            ssl_ = nullptr;

            closesocket(sock_);
            sock_ = INVALID_SOCKET;
            return false;
        }

        const uint64_t t_hs_end = now_ms();
        LOG_INFO("conn={} TLS connected peer={} tls={} cipher={} handshake_ms={}",
                 conn_id_, try_get_peer(sock_), SSL_get_version(ssl_), SSL_get_cipher(ssl_), (t_hs_end - t_hs_start));
    } else {
        // Plaintext mode: skip TLS handshake
        LOG_INFO("conn={} Using plaintext TCP (no TLS)", conn_id_);
    }

    is_connected_ = true;
    LOG_INFO("conn={} Connected total_ms={}", conn_id_, (now_ms() - t0));
    return true;
}

void CConnection_c::Close() {
    if (use_tls_ && ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }

    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    is_connected_ = false;
}

std::string CConnection_c::SendRequest(uint16_t type, const std::string& payload) {
    const uint64_t req_start = now_ms();
    const uint32_t seq = next_seq_++;

    if (!is_connected_) {
        LOG_ERROR("conn={} seq={} SendRequest denied: not connected", conn_id_, seq);
        return "";
    }

    if (type > 0xFFu) {
        LOG_ERROR("conn={} seq={} invalid type={} (max=255)", conn_id_, seq, type);
        return "";
    }

    const uint8_t type8 = static_cast<uint8_t>(type);
    if (!is_known_msg_type(type8)) {
        LOG_ERROR("conn={} seq={} unknown MsgType={}", conn_id_, seq, static_cast<int>(type8));
        return "";
    }

    if (payload.size() > kMaxPayloadSize) {
        LOG_ERROR("conn={} seq={} payload too large bytes={} max={}", conn_id_, seq, payload.size(), kMaxPayloadSize);
        return "";
    }

    // Safe payload observability: log size + redacted preview (Debug only)
    LOG_DEBUG("conn={} seq={} req type={} payload_bytes={} payload_preview={}",
              conn_id_, seq, static_cast<int>(type8), payload.size(), payload_preview(payload));

    AppProtoHeader hdr{};
    hdr.magic[0] = 'H';
    hdr.magic[1] = 'B';
    hdr.magic[2] = 'P';
    hdr.magic[3] = 'C';

    hdr.version = kProtoVersion;
    hdr.type = type8;
    hdr.flags = MsgFlags::None;
    hdr.reserved1 = 0;

    const uint32_t payload_len_host = static_cast<uint32_t>(payload.length());
    const uint64_t timestamp_ms_host = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    // Wire format (network order)
    AppProtoHeader_SetSeq(&hdr, seq);
    AppProtoHeader_SetPayloadLen(&hdr, payload_len_host);

    // 根据消息类型设置session_id
    uint64_t session_id = 179;

    // 如果显式设置了 next_session_id_（用于设备绑定等场景），优先使用它
    if (next_session_id_ > 0) {
        session_id = next_session_id_;
    }
    // 检查是否是登录相关的消息类型
    else if (type8 == static_cast<uint8_t>(MsgType::LoginSms) ||
             type8 == static_cast<uint8_t>(MsgType::AuthRequest) ||
             type8 == static_cast<uint8_t>(MsgType::OtpRequest)) {
        session_id = 179;
    }
    // 如果不是登录消息且已经有有效的session_id，则使用实际的session_id
    else if (last_response_session_id_ != 0) {
        session_id = last_response_session_id_;
    }

    AppProtoHeader_SetSessionId(&hdr, session_id);
    AppProtoHeader_SetTimestampMs(&hdr, timestamp_ms_host);

    std::memset(hdr.auth_token, 0, sizeof(hdr.auth_token));
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    const uint64_t t_send_hdr0 = now_ms();
    if (!send_all(reinterpret_cast<char*>(&hdr), static_cast<int>(sizeof(hdr)))) {
        LOG_ERROR("conn={} seq={} send header failed (fatal)", conn_id_, seq);
        Close();
        return "";
    }
    const uint64_t t_send_hdr1 = now_ms();

    const uint64_t t_send_payload0 = now_ms();
    if (!send_all(payload.c_str(), static_cast<int>(payload.length()))) {
        LOG_ERROR("conn={} seq={} send payload failed (fatal)", conn_id_, seq);
        Close();
        return "";
    }
    const uint64_t t_send_payload1 = now_ms();

    AppProtoHeader resp_hdr{};
    const uint64_t t_recv_hdr0 = now_ms();
    if (!recv_all(reinterpret_cast<char*>(&resp_hdr), static_cast<int>(sizeof(resp_hdr)))) {
        LOG_ERROR("conn={} seq={} recv header failed (fatal)", conn_id_, seq);
        Close();
        return "";
    }
    const uint64_t t_recv_hdr1 = now_ms();

    // Convert from wire format ASAP
    resp_hdr.seq = AppProtoHeader_GetSeq(&resp_hdr);
    resp_hdr.payload_len = AppProtoHeader_GetPayloadLen(&resp_hdr);
    resp_hdr.timestamp_ms = AppProtoHeader_GetTimestampMs(&resp_hdr);
    resp_hdr.session_id = ntoh64(resp_hdr.session_id);
    
    // Save the session ID from the response
    last_response_session_id_ = resp_hdr.session_id;
    LOG_INFO("conn={} seq={} received session_id={}", conn_id_, seq, last_response_session_id_);

    if (std::memcmp(resp_hdr.magic, "HBPC", 4) != 0) {
        LOG_ERROR("conn={} seq={} bad magic in response (fatal)", conn_id_, seq);
        Close();
        return "";
    }
    if (resp_hdr.version != kProtoVersion) {
        LOG_ERROR("conn={} seq={} bad version={} expected={} (fatal)",
                  conn_id_, seq, static_cast<int>(resp_hdr.version), static_cast<int>(kProtoVersion));
        Close();
        return "";
    }
    if (!is_known_msg_type(resp_hdr.type)) {
        LOG_ERROR("conn={} seq={} bad response type={} (fatal)", conn_id_, seq, static_cast<int>(resp_hdr.type));
        Close();
        return "";
    }

    const uint32_t body_len = resp_hdr.payload_len;
    if (body_len > kMaxPayloadSize) {
        LOG_ERROR("conn={} seq={} response payload too large bytes={} max={} (fatal)",
                  conn_id_, seq, body_len, kMaxPayloadSize);
        Close();
        return "";
    }

    std::vector<unsigned char> buffer(body_len);
    const uint64_t t_recv_body0 = now_ms();
    if (body_len > 0 && !recv_all(reinterpret_cast<char*>(buffer.data()), static_cast<int>(body_len))) {
        LOG_ERROR("conn={} seq={} recv payload failed (fatal)", conn_id_, seq);
        // 不要关闭连接，因为可能是服务器在发送完响应后关闭了连接
        // Close();
        return "";
    }

    // 确保返回的是有效的UTF-8字符串
    // 使用无符号字节创建字符串，确保中文字符能够正确处理
    return std::string(reinterpret_cast<const char*>(buffer.data()), body_len);
}

bool CConnection_c::ReadMessage(AppProtoHeader& outHeader, std::string& outPayload) {
    outPayload.clear();

    if (!is_connected_) {
        return false;
    }

    if (!recv_all(reinterpret_cast<char*>(&outHeader), static_cast<int>(sizeof(outHeader)))) {
        return false;
    }

    outHeader.seq = AppProtoHeader_GetSeq(&outHeader);
    outHeader.payload_len = AppProtoHeader_GetPayloadLen(&outHeader);
    outHeader.timestamp_ms = AppProtoHeader_GetTimestampMs(&outHeader);
    outHeader.session_id = ntoh64(outHeader.session_id);

    if (std::memcmp(outHeader.magic, "HBPC", 4) != 0) {
        LOG_ERROR("ReadMessage: bad magic");
        Close();
        return false;
    }
    if (outHeader.version != kProtoVersion) {
        LOG_ERROR("ReadMessage: bad version={} expected={}", static_cast<int>(outHeader.version),
                  static_cast<int>(kProtoVersion));
        Close();
        return false;
    }
    if (!is_known_msg_type(outHeader.type)) {
        LOG_ERROR("ReadMessage: unknown MsgType={}", static_cast<int>(outHeader.type));
        Close();
        return false;
    }

    const uint32_t len = outHeader.payload_len;
    if (len > kMaxPayloadSize) {
        LOG_ERROR("ReadMessage: payload too large bytes={} max={}", len, kMaxPayloadSize);
        Close();
        return false;
    }

    if (len == 0) {
        return true;
    }

    std::vector<char> buffer(len);
    if (!recv_all(buffer.data(), static_cast<int>(len))) {
        return false;
    }

    outPayload.assign(buffer.begin(), buffer.end());
    return true;
}

// --- 内部函数：确保全部发送 ---
bool CConnection_c::send_all(const char* data, int len) {
    int total = 0;
    while (total < len) {
        int ret;
        if (use_tls_ && ssl_) {
            ret = SSL_write(ssl_, reinterpret_cast<const void*>(data + total), len - total);
        } else {
            ret = static_cast<int>(send(sock_, data + total, len - total, 0));
        }

        if (ret <= 0) {
            if (use_tls_ && ssl_) {
                // improve TLS error handling (WANT_* and drain error stack)
                int ssl_err = SSL_get_error(ssl_, ret);
                LOG_ERROR("SSL_write failed ({}, sent {}/{}))", ssl_err, total, len);

                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                    LOG_ERROR(" (WANT_READ/WANT_WRITE - retrying)");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                LOG_ERROR("");
                drain_openssl_error_stack(std::cerr);
            } else {
                int error = get_last_error();
                LOG_ERROR("send failed ({})", error);
                LOG_ERROR("send failed ({})", last_error_msg(error));
                return false;
            }
            return false;
        }
        total += ret;
    }
    return true;
}

// --- 内部函数：确保全部接收 ---
bool CConnection_c::recv_all(char* buf, int len) {
    int total = 0;
    while (total < len) {
        int ret;
        if (use_tls_ && ssl_) {
            ret = SSL_read(ssl_, reinterpret_cast<void*>(buf + total), len - total);
        } else {
            ret = static_cast<int>(recv(sock_, buf + total, len - total, 0));
        }

        if (ret <= 0) {
            if (use_tls_ && ssl_) {
                // improve TLS error handling (WANT_* and drain error stack)
                int ssl_err = SSL_get_error(ssl_, ret);
                LOG_ERROR("SSL_read failed ({}, received {}/{})");

                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                    LOG_ERROR(" (WANT_READ/WANT_WRITE - retrying)");
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }
                
                // 处理TLS连接被突然关闭的情况
                if (ssl_err == SSL_ERROR_ZERO_RETURN || ssl_err == SSL_ERROR_SYSCALL) {
                    LOG_WARN("TLS connection closed by peer (unexpected eof)");
                    // 返回已经读取的数据
                    return total > 0;
                }

#ifdef _WIN32
                if (ssl_err == SSL_ERROR_SYSCALL) {
                    int ws_err = WSAGetLastError();
                    if (ws_err != 0) {
                        LOG_ERROR(", WSA={}");
                        LPSTR errorMsg = nullptr;
                        DWORD msgLen = FormatMessageA(
                            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, ws_err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                            reinterpret_cast<LPSTR>(&errorMsg), 0, nullptr);
                        if (msgLen > 0 && errorMsg) {
                            LOG_ERROR(" ({})", errorMsg);
                            LocalFree(errorMsg);
                        }
                    }
                }
#else
                if (ssl_err == SSL_ERROR_SYSCALL) {
                    int e = errno;
                    if (e != 0) {
                        LOG_ERROR(", errno={} ({})", e, last_error_msg(e));
                    }
                }
#endif
                LOG_ERROR("");
                drain_openssl_error_stack(std::cerr);
            } else {
                if (ret == 0) {
                    LOG_ERROR("connection closed, received {}/{}", total, len);
                } else {
                    int error = get_last_error();
                    LOG_ERROR("recv failed ({})", error);
                    LOG_ERROR("recv failed ({})", last_error_msg(error));
                }
            }
            return false;
        }
        total += ret;
    }
    return true;
}

