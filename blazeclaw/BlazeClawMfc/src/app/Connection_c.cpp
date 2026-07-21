#include "pch.h"
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "Connection_c.h"
#include "Logger.h"
#include "TcpReceiverWnd.h"

#include <cstdint>
#include <sstream>
#include <string>

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
#include <sstream>
#include <iomanip>
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

#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
#else
    ssl_ctx_ = SSL_CTX_new(SSLv23_client_method());
#endif
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
    DWORD timeout = blazeclaw::net::kSocketRecvTimeoutMs;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    // POSIX: timeval
    struct timeval tv;
    tv.tv_sec = 5;
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

    // 启动应用层心跳 (PING/PONG)，保活长连接。
    //StartHeartbeat();

    return true;
}

// ─── TCP keep-alive ───
// 启用 SO_KEEPALIVE + 平台特定的 TCP_KEEPALIVE/CNT/INTVL。
// 返回 true 表示设置成功（即使平台不支持某些选项，也保证 SO_KEEPALIVE 成功）。
bool CConnection_c::EnableTcpKeepAlive(SOCKET s) {
    if (s == INVALID_SOCKET) return false;

    int on = 1;
#ifdef _WIN32
    // SO_KEEPALIVE：开启 TCP 心跳
    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE,
                   reinterpret_cast<const char*>(&on), sizeof(on)) == SOCKET_ERROR) {
        LOG_WARN("conn={} SO_KEEPALIVE failed err={}", conn_id_, get_last_error());
        return false;
    }

    // Windows Vista+: TCP_KEEPALIVE = idle time (毫秒)
    // 默认 2 小时太长，改为 30 秒没活动就开始探测。
    DWORD idle_ms = static_cast<DWORD>(blazeclaw::net::kTcpKeepaliveIdleMs);
    if (setsockopt(s, IPPROTO_TCP, TCP_KEEPALIVE,
                   reinterpret_cast<const char*>(&idle_ms), sizeof(idle_ms)) == SOCKET_ERROR) {
        LOG_WARN("conn={} TCP_KEEPALIVE (idle) failed err={}", conn_id_, get_last_error());
        // 非致命，继续
    }

    // TCP_KEEPINTVL = 探测间隔（秒）
    DWORD interval_s = 5;
    if (setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL,
                   reinterpret_cast<const char*>(&interval_s), sizeof(interval_s)) == SOCKET_ERROR) {
        LOG_WARN("conn={} TCP_KEEPINTVL failed err={}", conn_id_, get_last_error());
    }
#else
    // POSIX
    if (setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) == SOCKET_ERROR) {
        LOG_WARN("conn={} SO_KEEPALIVE failed err={}", conn_id_, get_last_error());
        return false;
    }
    int idle_s = 30;
    int interval_s = 5;
    int cnt = 3;
    setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE,  &idle_s,    sizeof(idle_s));
    setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL, &interval_s, sizeof(interval_s));
    setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,        sizeof(cnt));
#endif
    LOG_INFO("conn={} TCP keep-alive enabled: idle=30s interval=5s cnt=3", conn_id_);
    return true;
}

// ─── 应用层心跳 ───
void CConnection_c::StartHeartbeat() {
    if (heartbeat_running_.exchange(true)) {
        return;  // 已在运行
    }
    try {
        heartbeat_thread_ = std::thread([this]() { HeartbeatLoop(); });
    } catch (const std::exception& e) {
        LOG_ERROR("conn={} StartHeartbeat failed: {}", conn_id_, e.what());
        heartbeat_running_.store(false);
    }
}

void CConnection_c::StopHeartbeat() {
    if (!heartbeat_running_.exchange(false)) {
        return;  // 已停
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
}

void CConnection_c::HeartbeatLoop() {
    LOG_INFO("conn={} Heartbeat thread started (interval={}ms)",
             conn_id_, static_cast<long long>(heartbeat_interval_ms_.count()));

    // 用 sleep_for 分段，每秒检查一次 running 标志，可及时退出。
    int slept_ms = 0;
    while (heartbeat_running_.load() && is_connected_.load()) {
        const int step_ms = blazeclaw::net::kHeartbeatStepMs;
        std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
        slept_ms += step_ms;
        if (slept_ms < static_cast<int>(heartbeat_interval_ms_.count())) {
            continue;
        }
        slept_ms = 0;

        // PING：使用 SendNoWait 走 send_mutex_，不会和业务写串扰。
        // 心跳失败 = 连接死了，立即退出循环，让 PushReceiver / disconnect_callback 接管。
        const bool ok = SendNoWait(static_cast<uint16_t>(MsgType::Ping), std::string{});
        if (!ok) {
            LOG_WARN("conn={} Heartbeat: SendNoWait(Ping) failed, stopping heartbeat", conn_id_);
            break;
        }
        LOG_DEBUG("conn={} Heartbeat: Ping sent", conn_id_);
    }
    LOG_INFO("conn={} Heartbeat thread stopped", conn_id_);
}

void CConnection_c::Close() {
    // 先停心跳线程，避免它在 socket 已关闭后还尝试 send。
    //StopHeartbeat();

    const bool was_connected = is_connected_.load();
    if (was_connected) {
        TRACE(_T("conn=%d Close() CALLED: clearing is_connected_=%d. Stack hint: see caller LOG_WARN\n"),
              conn_id_, is_connected_.load() ? 1 : 0);
    }

    // 停止 PushReceiver 线程，确保重连时能正确重启
    StopPushReceiver();

    if (use_tls_ && ssl_) {
        SSL_shutdown(ssl_);
        SSL_free(ssl_);
        ssl_ = nullptr;
    }

    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
    }

    is_connected_.store(false);

    if (was_connected) {
        LOG_INFO("conn=%d Closed (TCP/TLS connection terminated)", conn_id_);
    }

    // 连接关闭时清空所有 pending async，避免 callback 永远不被调用
    // Fire-and-forget-with-callback 的 callback 注册在 pending_async_done_，
    // 这里把它清掉；后续到达的响应在 receiver 路径找不到 callback 直接丢弃。
    {
        std::lock_guard<std::mutex> lock(async_mutex_);
        pending_async_done_.clear();
    }
}

std::string CConnection_c::SendRequest(uint16_t type, const std::string& payload) {
    const uint64_t req_start = now_ms();
    // 关键修复：next_seq_ 自增必须用 seq_mutex_ 串行化。
    // 之前 next_seq_++ 没有锁，与 SendNoWaitWithCallback 并发时会拿到相同 seq，
    uint32_t seq;
    {
        std::lock_guard<std::mutex> seq_lock(seq_mutex_);
        seq = next_seq_++;
    }

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
    // 关键修复：读 next_session_id_ 必须持 session_mutex_，避免与 SetSessionId
    // 并发写竞争读到部分写入值。
    uint64_t session_id;
    {
        std::lock_guard<std::mutex> session_lock(session_mutex_);
        // 显式设置了 next_session_id_（如设备绑定设 0）时，优先用它
        if (next_session_id_ > 0) {
            session_id = next_session_id_;
        }
        else if (type8 == static_cast<uint8_t>(MsgType::LoginSms) ||
            type8 == static_cast<uint8_t>(MsgType::AuthRequest) ||
            type8 == static_cast<uint8_t>(MsgType::OtpRequest)) {
            // 登录相关请求用固定 session_id=179
            session_id = 179;
        }
        else {
            session_id = 179;
        }
    }

    AppProtoHeader_SetSessionId(&hdr, session_id);
    AppProtoHeader_SetTimestampMs(&hdr, timestamp_ms_host);

    std::memset(hdr.auth_token, 0, sizeof(hdr.auth_token));
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));


    // 否则另一个线程的 send_all 可能插入字节流，服务端解析失败 → RST → ECONNABORTED。

    // 注意：手动锁而非 lock_guard 是为了 send_all 失败调 Close() 时能精确释放锁，
    // 避免 Close → StopHeartbeat → join 心跳线程时心跳线程正等 send_mutex_ 而死锁。
    send_mutex_.lock();

    const uint64_t t_send_hdr0 = now_ms();
    if (!send_all(reinterpret_cast<char*>(&hdr), static_cast<int>(sizeof(hdr)))) {
        LOG_ERROR("conn={} seq={} send header failed (fatal) — closing connection", conn_id_, seq);
        send_mutex_.unlock();
        Close();
        return "";
    }
    const uint64_t t_send_hdr1 = now_ms();

    const uint64_t t_send_payload0 = now_ms();
    if (!send_all(payload.c_str(), static_cast<int>(payload.length()))) {
        LOG_ERROR("conn={} seq={} send payload failed (fatal) — closing connection", conn_id_, seq);
        send_mutex_.unlock();
        Close();
        return "";
    }
    const uint64_t t_send_payload1 = now_ms();

    // 关键修复(Bug #3)：send 完毕立即释放 send_mutex_，让其他写线程可以并发
    // send。recv 阶段不再持 send_mutex_。
    send_mutex_.unlock();

    AppProtoHeader resp_hdr{};
    std::string resp_payload;
    const uint64_t t_recv_hdr0 = now_ms();
    // ReadMessage 内部会拿 recv_mutex_ 锁，与 PushReceiver 线程的 ReadMessage
    // 在 recv 上严格串行化 —— 一次只一个线程从 socket 读完整帧，不会瓜分字节流。
    // recv 超时由 socket 层 SO_RCVTIMEO 控制(见 Connect() 里 setsockopt)。
    // 默认参数 -1 让 ReadMessage 走"无超时"语义,但 socket 层 SO_RCVTIMEO 仍然生效。
    if (!ReadMessage(resp_hdr, resp_payload)) {
        RecvStatus last_status = get_last_recv_status();
        if (last_status == RecvStatus::Timeout) {
            // 5s 同步等待内没拿到响应 —— 这是调用方主动选择的行为(用 SendRequest 同步路径),
            // 不应该误杀整个连接,只让本次请求失败。返回空串让上层走 fallback 路径。
            LOG_WARN("conn={} seq={} recv response timed out (5s) — NOT closing connection",
                     conn_id_, seq);
            return std::string();
        }
        // Eof / FatalError：连接真的挂了，关 socket 让上层 reconnect。
        LOG_ERROR("conn={} seq={} recv response failed (status={}) — closing connection",
                  conn_id_, seq, static_cast<int>(last_status));
        Close();
        return std::string();
    }
    const uint64_t t_recv_hdr1 = now_ms();

    // Save the session ID from the response
    // 关键修复：跨线程写必须持 session_mutex_，否则 PushReceiver 线程（line ~1235）
    // 也写同一字段会导致数据竞争。
    {
        std::lock_guard<std::mutex> session_lock(session_mutex_);
        last_response_session_id_ = resp_hdr.session_id;
        LOG_INFO("conn={} seq={} received session_id={}", conn_id_, seq, last_response_session_id_);
    }

    return resp_payload;
}

bool CConnection_c::SendNoWait(uint16_t type, const std::string& payload) {
    if (!is_connected_) {
        return false;
    }

    // Fire-and-forget：seq 仅作协议格式填充，不计入 next_seq_。
    // 否则每次 NoWait 都会让 next_seq_ 漂移，导致后续 SendNoWaitWithCallback
    // 的响应 seq 永远对不上 pending_async_done_ 里的 seq。
    // P2 修复：next_seq_ 自增必须和 SendRequest 串行化，避免并发调用产生重复 seq。
    uint32_t seq;
    {
        std::lock_guard<std::mutex> seq_lock(seq_mutex_);
        seq = next_seq_++;
    }  // 服务器忽略此值，不影响响应匹配

    AppProtoHeader hdr{};
    hdr.magic[0] = 'H';
    hdr.magic[1] = 'B';
    hdr.magic[2] = 'P';
    hdr.magic[3] = 'C';
    hdr.version = kProtoVersion;
    hdr.type = static_cast<uint8_t>(type);
    hdr.flags = MsgFlags::None;
    hdr.reserved1 = 0;

    const uint32_t payload_len_host = static_cast<uint32_t>(payload.length());
    const uint64_t timestamp_ms_host = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    AppProtoHeader_SetSeq(&hdr, seq);
    AppProtoHeader_SetPayloadLen(&hdr, payload_len_host);
    {
        // 关键修复：next_session_id_ 在多线程下被 SetSessionId 写入，被 SendRequest
        // / SendNoWait / SendNoWaitWithCallback 读出写入 hdr，读/写都要加 session_mutex_。
        std::lock_guard<std::mutex> session_lock(session_mutex_);
        AppProtoHeader_SetSessionId(&hdr, next_session_id_);
    }
    AppProtoHeader_SetTimestampMs(&hdr, timestamp_ms_host);

    std::memset(hdr.auth_token, 0, sizeof(hdr.auth_token));
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    // 帧串扰防护：整个 fire-and-forget 帧也必须在 send_mutex_ 内写完。
    // 这是 IRC PRIVMSG/JOIN/PART 等 221 命令的写入路径，也是帧串扰最常见的源头。
    //
    // 关键修复：手动锁而非 lock_guard 是为了 send_all 失败调 Close() 时
    // 能精确释放锁，避免 Close → StopHeartbeat → join 心跳线程时心跳线程
    // 正等 send_mutex_ 而死锁。
    send_mutex_.lock();

    if (!send_all(reinterpret_cast<const char*>(&hdr), static_cast<int>(sizeof(hdr)))) {
        // 关键修复：send_all 失败说明 socket 已经死了，仅 mark flag / 清 pending
        // 不够——socket fd 与 SSL 句柄必须真正释放，避免悬挂 fd 被后续（错误的）写访问。
        // Close() 内部已经做了：停心跳 + SSL_shutdown/free + closesocket +
        // is_connected_=false + 清空 pending_async_done_ + 触发 disconnect_callback_。
        LOG_WARN("conn={} seq={} SendNoWait header send failed, closing connection",
                 conn_id_, seq);
        send_mutex_.unlock();
        Close();
        return false;
    }

    if (!payload.empty()) {
        if (!send_all(payload.c_str(), static_cast<int>(payload.length()))) {
            LOG_WARN("conn={} seq={} SendNoWait payload send failed, closing connection",
                     conn_id_, seq);
            send_mutex_.unlock();
            Close();
            return false;
        }
    }

    LOG_DEBUG("conn={} seq={} SendNoWait type={} payload_bytes={}",
              conn_id_, seq, static_cast<int>(type), payload.size());
    send_mutex_.unlock();
    return true;
}

bool CConnection_c::SendNoWaitWithSession(uint16_t type,
                                          const std::string& payload,
                                          uint64_t session_id) {
    if (!is_connected_) {
        return false;
    }

    uint32_t seq;
    {
        std::lock_guard<std::mutex> seq_lock(seq_mutex_);
        seq = next_seq_++;
    }

    AppProtoHeader hdr{};
    hdr.magic[0] = 'H';
    hdr.magic[1] = 'B';
    hdr.magic[2] = 'P';
    hdr.magic[3] = 'C';
    hdr.version = kProtoVersion;
    hdr.type = static_cast<uint8_t>(type);
    hdr.flags = MsgFlags::None;
    hdr.reserved1 = 0;

    const uint32_t payload_len_host = static_cast<uint32_t>(payload.length());
    const uint64_t timestamp_ms_host = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    AppProtoHeader_SetSeq(&hdr, seq);
    AppProtoHeader_SetPayloadLen(&hdr, payload_len_host);
    // 直接使用调用方传入的 session_id，不读 next_session_id_
    AppProtoHeader_SetSessionId(&hdr, session_id);
    AppProtoHeader_SetTimestampMs(&hdr, timestamp_ms_host);

    std::memset(hdr.auth_token, 0, sizeof(hdr.auth_token));
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    send_mutex_.lock();

    if (!send_all(reinterpret_cast<const char*>(&hdr), static_cast<int>(sizeof(hdr)))) {
        LOG_WARN("conn={} seq={} SendNoWaitWithSession header send failed", conn_id_, seq);
        send_mutex_.unlock();
        Close();
        return false;
    }

    if (!payload.empty()) {
        if (!send_all(payload.c_str(), static_cast<int>(payload.length()))) {
            LOG_WARN("conn={} seq={} SendNoWaitWithSession payload send failed", conn_id_, seq);
            send_mutex_.unlock();
            Close();
            return false;
        }
    }

    LOG_DEBUG("conn={} seq={} SendNoWaitWithSession type={} session_id={} payload_bytes={}",
              conn_id_, seq, static_cast<int>(type), session_id, payload.size());
    send_mutex_.unlock();
    return true;
}

uint32_t CConnection_c::SendNoWaitWithCallback(uint16_t type,
                                               const std::string& payload,
                                               std::function<void(uint32_t seq, const std::string& payload)> callback) {
    if (!is_connected_) {
        TRACE(_T("conn=%d SendNoWaitWithCallback type=%d: is_connected_=false\n"),
              conn_id_, static_cast<int>(type));
        return 0;
    }

    // 分配 seq（计入 next_seq_，以便 PushReceiver 能匹配到响应）
    uint32_t seq;
    {
        std::lock_guard<std::mutex> seq_lock(seq_mutex_);
        seq = next_seq_++;
    }

    // 注册 callback 到 pending_async_done_，响应到达时通过 seq 匹配调用
    if (callback) {
        std::lock_guard<std::mutex> lock(async_mutex_);
        pending_async_done_[seq] = std::move(callback);
    }

    // 构建并发送帧
    AppProtoHeader hdr{};
    hdr.magic[0] = 'H';
    hdr.magic[1] = 'B';
    hdr.magic[2] = 'P';
    hdr.magic[3] = 'C';
    hdr.version = kProtoVersion;
    hdr.type = static_cast<uint8_t>(type);
    hdr.flags = MsgFlags::None;
    hdr.reserved1 = 0;

    const uint32_t payload_len_host = static_cast<uint32_t>(payload.length());
    const uint64_t timestamp_ms_host = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    AppProtoHeader_SetSeq(&hdr, seq);
    AppProtoHeader_SetPayloadLen(&hdr, payload_len_host);
    {
        std::lock_guard<std::mutex> session_lock(session_mutex_);
        AppProtoHeader_SetSessionId(&hdr, next_session_id_);
    }
    AppProtoHeader_SetTimestampMs(&hdr, timestamp_ms_host);

    std::memset(hdr.auth_token, 0, sizeof(hdr.auth_token));
    std::memset(hdr.reserved, 0, sizeof(hdr.reserved));

    send_mutex_.lock();
    if (!send_all(reinterpret_cast<const char*>(&hdr), static_cast<int>(sizeof(hdr)))) {
        LOG_WARN("conn={} seq={} SendNoWaitWithCallback header send failed", conn_id_, seq);
        send_mutex_.unlock();
        {
            std::lock_guard<std::mutex> lock(async_mutex_);
            pending_async_done_.erase(seq);
        }
        Close();
        return 0;
    }

    if (!payload.empty()) {
        if (!send_all(payload.c_str(), static_cast<int>(payload.length()))) {
            LOG_WARN("conn={} seq={} SendNoWaitWithCallback payload send failed", conn_id_, seq);
            send_mutex_.unlock();
            {
                std::lock_guard<std::mutex> lock(async_mutex_);
                pending_async_done_.erase(seq);
            }
            Close();
            return 0;
        }
    }

    LOG_DEBUG("conn={} seq={} SendNoWaitWithCallback type={} payload_bytes={}",
              conn_id_, seq, static_cast<int>(type), payload.size());
    send_mutex_.unlock();
    return seq;
}

namespace {
// 将一行 UTF-8 / ASCII 文本投递到 CTcpReceiverWnd 富文本界面。
// 线程安全：内部 SendMessage 同步切到 UI 线程。
// 当 sink 未注册（例如启动早期）时回落 OutputDebugString。
// UTF-8 → Unicode 转换：直接使用 MultiByteToWideChar(CP_UTF8)，
// 避免 CA2W 默认 CP_ACP 对 UTF-8 字节产生乱码。
void LogToTcpReceiverWnd(const std::string& line) {
    if (auto* wnd = TcpReceiverSink::Get()) {
        const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wbuf(static_cast<size_t>(wlen) - 1, L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, &wbuf[0], wlen);
            CStringW wide(wbuf.c_str());
            wnd->EnqueueIncomingLogLine(CString(wide));
        }
        return;
    }
    // 启动早期或窗口未创建 — 退回到 OutputDebugString。
    OutputDebugStringA(("[ReadMessage] " + line + "\n").c_str());
}
}

// recv_timeout_ms:
//   -1 (默认) = 永久阻塞(给 PushReceiver 用,不应被 idle 杀掉,bug #7 修复)
//   >= 0      = 单次 recv 最多等 N 毫秒(给 SendRequest 同步路径用,避免阻塞 UI)
//
// 返回 RecvStatus:
///   - Success：成功读出一帧，header + payload 已填充。
///   - Timeout：socket SO_RCVTIMEO 到期（非 fatal，PushReceiver 应继续循环）。
///   - Eof：peer FIN（PushReceiver 应触发重连）。
///   - FatalError：ECONNRESET 等（PushReceiver 应触发重连）。
bool CConnection_c::ReadMessage(AppProtoHeader& outHeader, std::string& outPayload,
                                      int recv_timeout_ms) {
    outPayload.clear();

    if (!is_connected_) {
        last_recv_status_.store(RecvStatus::FatalError);
        return false;
    }

    // 帧串扰防护：recv_all 必须串行化，避免 PushReceiver 线程和
    // SendRequest 调用线程同时从同一 socket 读取，把 TCP 字节流瓜分。
    std::lock_guard<std::mutex> recv_lock(recv_mutex_);

    RecvStatus status = recv_all(reinterpret_cast<char*>(&outHeader),
                                 static_cast<int>(sizeof(outHeader)),
                                 recv_timeout_ms);
    if (status != RecvStatus::Success) {
        last_recv_status_.store(status);
        return false;
    }

    outHeader.seq = AppProtoHeader_GetSeq(&outHeader);
    outHeader.payload_len = AppProtoHeader_GetPayloadLen(&outHeader);
    outHeader.timestamp_ms = AppProtoHeader_GetTimestampMs(&outHeader);
    outHeader.session_id = ntoh64(outHeader.session_id);

 /*   {
        std::ostringstream oss;
        oss << "RECV [ReadMessage] conn=" << conn_id_
            << " type=" << static_cast<int>(outHeader.type)
            << " seq=" << outHeader.seq
            << " payload_len=" << outHeader.payload_len
            << " session_id=" << static_cast<unsigned long long>(outHeader.session_id);
        LogToTcpReceiverWnd(oss.str());
        TRACE(_T("[CConnection_c] %hs\n"), oss.str().c_str());
    }*/

    if (std::memcmp(outHeader.magic, "HBPC", 4) != 0) {
        LOG_ERROR("ReadMessage: bad magic");
        LogToTcpReceiverWnd("ERROR ReadMessage: bad magic");
        Close();
        last_recv_status_.store(RecvStatus::FatalError);
        return false;
    }
    if (outHeader.version != kProtoVersion) {
        LOG_ERROR("ReadMessage: bad version={} expected={}", static_cast<int>(outHeader.version),
                  static_cast<int>(kProtoVersion));
        LogToTcpReceiverWnd(std::string("ERROR ReadMessage: bad version=") +
                            std::to_string(static_cast<int>(outHeader.version)) +
                            " expected=" + std::to_string(static_cast<int>(kProtoVersion)));
        Close();
        last_recv_status_.store(RecvStatus::FatalError);
        return false;
    }
    if (!is_known_msg_type(outHeader.type)) {
        LOG_ERROR("ReadMessage: unknown MsgType={}", static_cast<int>(outHeader.type));
        LogToTcpReceiverWnd(std::string("ERROR ReadMessage: unknown MsgType=") +
                            std::to_string(static_cast<int>(outHeader.type)));
        //Close();
        last_recv_status_.store(RecvStatus::FatalError);
        return false;
    }

    const uint32_t len = outHeader.payload_len;
    if (len > kMaxPayloadSize) {
        LOG_ERROR("ReadMessage: payload too large bytes={} max={}", len, kMaxPayloadSize);
        LogToTcpReceiverWnd(std::string("ERROR ReadMessage: payload too large bytes=") +
                            std::to_string(len) + " max=" + std::to_string(kMaxPayloadSize));
        Close();
        last_recv_status_.store(RecvStatus::FatalError);
        return false;
    }

    std::vector<char> buffer(len);
    // payload 的 recv 沿用同一个超时预算。ReadMessage 整体超时上限约 5s(SendRequest 路径),
    // 因为服务端在响应 header 之后通常立即 flush payload,中间空隙很短。
    status = recv_all(buffer.data(), static_cast<int>(len), recv_timeout_ms);
    if (status != RecvStatus::Success) {
        // header 已收到但 payload 没收完：把读到的部分 buffer 丢掉（已被释放）。
        // 这里返回 false 让 PushReceiver 区分 timeout（继续）和 Eof/fatal（断开）。
        last_recv_status_.store(status);
        return false;
    }

    outPayload.assign(buffer.begin(), buffer.end());

    // 关键诊断日志：payload 读取完成后打印主体内容，长度上限 2KB
    {
        constexpr size_t kPreviewLimit = 2048;
        const size_t total = outPayload.size();
        const size_t shown = std::min<size_t>(total, kPreviewLimit);
        std::string preview_str(outPayload.data(), shown);
        for (char& c : preview_str) {
            if (c == '\r' || c == '\n' || c == '\0') c = ' ';
        }
        std::ostringstream oss;
        oss << "RECV [ReadMessage] conn=" << conn_id_
            << " type=" << static_cast<int>(outHeader.type)
            << " seq=" << outHeader.seq
            << " payload_len=" << outHeader.payload_len
            << " payload=" << preview_str;
        if (total > shown) {
            oss << "...(+" << (total - shown) << " bytes)";
        }
        LogToTcpReceiverWnd(oss.str());
        TRACE(_T("[CConnection_c] %hs\n"), oss.str().c_str());
    }
    last_recv_status_.store(RecvStatus::Success);
    return true;
}

void CConnection_c::SetPushCallback(PushCallback callback) {
    std::lock_guard<std::mutex> lock(push_mutex_);
    push_callback_ = std::move(callback);
}

void CConnection_c::SetDisconnectCallback(DisconnectCallback callback) {
    std::lock_guard<std::mutex> lock(push_mutex_);
    disconnect_callback_ = std::move(callback);
}

bool CConnection_c::StartPushReceiver() {
    // 第一步：清理残留的 std::thread 对象
    // std::thread 默认构造的对象 joinable()=false，但被赋值或 std::thread(...) 构造后就 joinable()=true。
    // 关键是：lambda 返回后线程函数结束，但 std::thread 对象的状态不会自动变化 —— 必须显式 join() 或 detach()。
    // 若不清理就执行下面的 `receiver_thread_ = std::thread(...)` 赋值给一个 joinable 的 std::thread，
    // 标准库会直接 std::terminate() → abort()（即用户在生产环境看到的那次崩溃的二次触发点）。
    //
    // 触发顺序：
    //   1) 上次 fatal 分支走完后 receiver_running_ 已被重置为 false
    //   2) PushReceiver lambda 退出但 receiver_thread_ 对象仍是 joinable 状态（没 join/detach）
    //   3) 重连后这里再次赋值 → terminate
    //
    // 修复：每次 StartPushReceiver() 一进来就先把残留的线程对象 join 掉（必须 receiver_finished_=true）。
    // 注意：如果 receiver_finished_=false 表示线程正在跑，这种情况下 receiver_running_=true，
    // 进入下面的 if 分支处理。绝对不会 join 还在跑的线程。
    if (receiver_finished_.load() && receiver_thread_.joinable()) {
        LOG_WARN("conn={} StartPushReceiver: joining stale receiver_thread_ (previous lambda finished)",
                 conn_id_);
        receiver_thread_.join();
    }
    if (push_finished_.load() && push_thread_.joinable()) {
        LOG_WARN("conn={} StartPushReceiver: joining stale push_thread_ (previous lambda finished)",
                 conn_id_);
        push_thread_.join();
    }

    if (receiver_running_.load()) {
        // 走到这里说明 receiver_running_=true 但 receiver_finished_=false（线程真在跑）。
        // 这个分支理论上不应再进入（上面会把残留线程 join 掉），但保留作为防御性 fallback。
        if (receiver_finished_.load()) {
            if (receiver_thread_.joinable()) {
                receiver_thread_.join();
            }
            receiver_running_.store(false);
        } else {
            return true;  // 线程真的还在跑
        }
    }

    if (!is_connected_) {
        return false;
    }

    receiver_running_.store(true);
    LOG_INFO("conn=%d Push receiver thread starting...", conn_id_);
    receiver_finished_.store(false);
    receiver_thread_ = std::thread([this]() {
        LOG_INFO("conn=%d Push receiver thread started", conn_id_);
        try {
        while (receiver_running_.load() && is_connected_.load()) {
            AppProtoHeader header{};
            std::string payload{};

            if (!ReadMessage(header, payload)) {
                RecvStatus last_status = get_last_recv_status();
                if (last_status == RecvStatus::Timeout) {
                    // 5s SO_RCVTIMEO 到期：服务端暂时没推数据。关键修复（原 bug #7）：
                    // 必须继续循环 —— 不许自杀、不要调用 disconnect_callback、不要清 is_connected_。
                    // 整个连接（TCP socket）依然 alive，可能只是服务端 5s+ 没有推消息。
                    LOG_INFO("conn={} Push receiver: 5s idle timeout (keepalive, NOT fatal)",
                             conn_id_);
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                // Eof / FatalError：对端已断开（FIN / RST / TLS close）。
                // 必须走 fatal 分支触发 reconnect，否则 PushReceiver 永远卡死在 "send alive but recv dead" 状态。
                const char* reason = (last_status == RecvStatus::Eof) ? "peer FIN/EOF" : "fatal recv error";
                LOG_WARN("conn={} Push receiver: {} → triggering disconnect", conn_id_, reason);

                // 通知上层断连，让 CIrcChatTransport 触发自动重连
                TRACE(_T("conn=%d Push receiver: BEFORE disconnect callback (%hs)\n"),
                      conn_id_, reason);
                {
                    std::lock_guard<std::mutex> lock(push_mutex_);
                    if (disconnect_callback_) {
                        TRACE(_T("conn=%d Push receiver: calling disconnect callback\n"), conn_id_);
                        disconnect_callback_();
                        TRACE(_T("conn=%d Push receiver: AFTER disconnect callback\n"), conn_id_);
                    }
                }
                // 最后才标记连接已断开 — 上层已收到通知，发送路径已避开 race。
                TRACE(_T("conn=%d Push receiver: setting is_connected_=false NOW\n"), conn_id_);
                is_connected_.store(false);

                // 关键修复(重连后 PushReceiver 死亡)：
                // 必须在退出前把 receiver_running_ / push_running_ 重置为 false，
                // 否则下一次 StartPushReceiver() 会因为 receiver_running_=true 误判
                // "已在运行" 直接 return，重连后的新 TCP 永远没有 PushReceiver 在读，
                // 表现为"发送正常、接收全部丢失"。
                //
                // 注意：不能在当前线程里 join receiver_thread_ / push_thread_，
                // 否则 std::thread::join 自己会触发 std::system_error(EDEADLK)。
                // 只需要把 running flag 置 false，让两个线程自然退出 lambda，
                // 后续 StartPushReceiver() 的 joinable 检查会负责 join 残留线程。
                receiver_running_.store(false);
                push_running_.store(false);
                push_queue_cv_.notify_one();
                break;
            }

            // 诊断日志：每条收到的帧都记录，附带当前 pending_async_done_ 里待匹配的 seq。
            {
                std::ostringstream pend_oss;
                pend_oss << "pending_done_seqs=[";
                std::lock_guard<std::mutex> lock(async_mutex_);
                for (const auto& kv : pending_async_done_) {
                    pend_oss << kv.first << " ";
                }
                pend_oss << "]";
                std::string payload_preview = payload.size() <= 150 ? payload
                    : payload.substr(0, 150) + "...";
                LOG_INFO("conn={} RECV frame: type={} seq={} payload_len={} session_id={}. {} payload={}",
                         conn_id_, static_cast<int>(header.type), header.seq,
                         static_cast<unsigned int>(header.payload_len),
                         static_cast<unsigned long long>(header.session_id),
                         pend_oss.str(), payload_preview);
            }

            // 检查是否是 SendNoWaitWithCallback 发出的请求（注册了 done_callback）。
            // 命中则调用 callback，不命中则作为服务端 push 入队。
            bool handled_as_async = false;
            {
                std::lock_guard<std::mutex> lock(async_mutex_);
                auto done_it = pending_async_done_.find(header.seq);
                if (done_it != pending_async_done_.end()) {
                    try {
                        done_it->second(header.seq, payload);
                    } catch (...) {
                        // callback 异常不影响后续处理
                    }
                    pending_async_done_.erase(done_it);
                    handled_as_async = true;
                }
            }

            if (!handled_as_async) {
                // seq 没命中 或 type 不兼容 → 这就是服务端 push，照常入队给业务层
                std::lock_guard<std::mutex> lock(push_queue_mutex_);
                push_queue_.push_back({header, std::move(payload)});
            }
            push_queue_cv_.notify_one();
            LOG_DEBUG("conn={} Push receiver: enqueued push msg (queue_size={})",
                      conn_id_, push_queue_.size());
        }
        LOG_INFO("conn={} Push receiver thread stopped", conn_id_);
        } catch (const std::exception& e) {
            LOG_ERROR("conn={} Push receiver thread exception: {}", conn_id_, e.what());
        } catch (...) {
            LOG_ERROR("conn={} Push receiver thread unknown exception", conn_id_);
        }
        receiver_finished_.store(true);
    });

    // Push dispatch thread: dequeues messages and invokes user callback.
    // Separated from receiver so async future resolution is never blocked by user callbacks.
    push_finished_.store(false);
    push_running_.store(true);
    push_thread_ = std::thread([this]() {
        LOG_INFO("conn={} Push dispatch thread started", conn_id_);
        try {
        while (push_running_.load()) {
            PushMsg msg;
            {
                std::unique_lock<std::mutex> lock(push_queue_mutex_);
                push_queue_cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                    return !push_queue_.empty() || !push_running_.load();
                });
                if (!push_running_.load() && push_queue_.empty()) {
                    break;
                }
                if (push_queue_.empty()) {
                    continue;
                }
                msg = std::move(push_queue_.front());
                push_queue_.pop_front();
            }

            // Copy header info before moving payload (for logging)
            const auto dispatched_seq = msg.header.seq;
            const auto dispatched_type = msg.header.type;
            const auto dispatched_payload_len = msg.payload.size();

            PushCallback cb;
            {
                std::lock_guard<std::mutex> lock(push_mutex_);
                cb = push_callback_;
            }

            if (cb) {
                try {
                    TRACE(_T("conn=%d Push dispatch: invoking callback for seq=%u type=%d\n"),
                          conn_id_, dispatched_seq, static_cast<int>(dispatched_type));
                    cb(msg.header, msg.payload);
                    LOG_DEBUG("conn={} Push dispatch: invoked callback for seq={} type={} payload_len={}",
                              conn_id_, dispatched_seq, static_cast<int>(dispatched_type), dispatched_payload_len);
                } catch (const std::exception& e) {
                    LOG_ERROR("conn={} Push callback exception: {}", conn_id_, e.what());
                }
            } else {
                TRACE(_T("conn=%d Push dispatch: no callback registered for seq=%u type=%d\n"),
                      conn_id_, dispatched_seq, static_cast<int>(dispatched_type));
                LOG_WARN("conn={} Push dispatch: no callback registered for seq={} type={}",
                         conn_id_, dispatched_seq, static_cast<int>(dispatched_type));
            }
        }
        LOG_INFO("conn=%d Push dispatch thread stopped", conn_id_);
        } catch (const std::exception& e) {
            LOG_ERROR("conn={} Push dispatch thread exception: {}", conn_id_, e.what());
        } catch (...) {
            LOG_ERROR("conn={} Push dispatch thread unknown exception", conn_id_);
        }
        push_finished_.store(true);
    });

    return true;
}

void CConnection_c::StopPushReceiver() {
    if (!receiver_running_.load()) {
        return;
    }

    LOG_INFO("conn=%d StopPushReceiver: stopping...", conn_id_);
    receiver_running_.store(false);

    push_running_.store(false);
    push_queue_cv_.notify_one();

    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
        LOG_INFO("conn=%d StopPushReceiver: receiver joined", conn_id_);
    }
    if (push_thread_.joinable()) {
        push_thread_.join();
        LOG_INFO("conn=%d StopPushReceiver: push joined", conn_id_);
    }

    {
        std::lock_guard<std::mutex> lock(push_queue_mutex_);
        push_queue_.clear();
    }
    LOG_INFO("conn=%d StopPushReceiver: done", conn_id_);
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
                TRACE(last_error_msg(error).c_str());
                return false;
            }
            return false;
        }
        total += ret;
    }
    return true;
}

// --- 内部函数：确保全部接收 ---
// 关键修复(原 bug #7+)：recv_all 必须明确返回 RecvStatus，让 PushReceiver 能区分：
//   - Timeout：SO_RCVTIMEO=5s 到期（服务端暂时没推数据），非 fatal → sleep+continue
//   - Eof：peer FIN / TLS zero-return（对端写半边关闭），fatal → 触发 reconnect
//   - FatalError：ECONNRESET / ENOTCONN 等致命错误，fatal → 触发 reconnect
//
// 之前只返回 bool 的设计导致 PushReceiver 必须靠 get_last_error() 推测错误类型，
// 而 Windows 上 recv 返回 0（peer FIN）时 get_last_error() 也是 0，会被误判为
// WSAETIMEDOUT 之外的"未知 0"——结合 fatal 列表 (error==0) 直接走 fatal 分支。
RecvStatus CConnection_c::recv_all(char* buf, int len, int /*recv_timeout_ms*/) {
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
                int ssl_err = SSL_get_error(ssl_, ret);

                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                    // 非阻塞式握手/重协商：短暂 sleep 后重试。这种是"还在等"，不算 timeout
                    // 因为 socket 层 SO_RCVTIMEO 通常还没到（TLS 内部 retry 会消耗时间）。
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    continue;
                }

                if (ssl_err == SSL_ERROR_ZERO_RETURN || ssl_err == SSL_ERROR_SYSCALL) {
                    // TLS 通道被对端关闭（FIN 或 RST）
                    LOG_WARN("conn={} TLS connection closed by peer (ssl_err={})", conn_id_, ssl_err);
                    return ssl_err == SSL_ERROR_ZERO_RETURN ? RecvStatus::Eof : RecvStatus::FatalError;
                }

                LOG_ERROR("conn={} SSL_read failed (ssl_err={}, received {}/{})",
                          conn_id_, ssl_err, total, len);
                drain_openssl_error_stack(std::cerr);
                return RecvStatus::FatalError;
            }

            // plaintext TCP
            if (ret == 0) {
                // peer FIN（对方写半边关闭）：TCP 还可写，但已无法再读出任何数据。
                // 一定要返回 Eof，不要返回 Timeout，否则 PushReceiver 会以为是 5s
                // 超时而被误判为 keepalive 继续空转。
                LOG_INFO("conn={} recv()=0 (peer FIN, will trigger reconnect)", conn_id_);
                return RecvStatus::Eof;
            }

            int err = get_last_error();
            if (err == WSAETIMEDOUT) {
                // socket 层 SO_RCVTIMEO=5s 到期。这是非 fatal 状态。
                return RecvStatus::Timeout;
            }
            // ECONNRESET / ENOTCONN / ESHUTDOWN / EHOSTUNREACH 等
            LOG_WARN("conn={} recv() failed err={} (received {}/{})",
                     conn_id_, err, total, len);
            return RecvStatus::FatalError;
        }
        total += ret;
    }
    return RecvStatus::Success;
}

