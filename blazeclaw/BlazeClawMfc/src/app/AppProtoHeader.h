#pragma once

#include <cstddef>
#include <cstdint>
#include <array>

//// 跨平台头文件包含
//#ifdef _WIN32
//  #include <winsock2.h>
//  #include <ws2tcpip.h>
//#else
//  #include <arpa/inet.h> // for htonl/ntohl
//#endif

//
// Application protocol header exposed to clients.
// Fixed-size 64-byte header (cache-line aware).
//
// This header is intentionally minimal and portable — avoid pulling
// heavy platform headers so it can be included by client code.
//
static constexpr std::size_t kProtoHeaderSize = 64;

// Protocol contract: canonical protocol version.
// Keep this as the single source of truth and reference it everywhere.
static constexpr uint8_t kProtoVersion = 1;

// Protocol contract: maximum allowed payload size (bytes).
// Used to prevent oversized allocations / memory DoS and to bound message framing.
static constexpr uint32_t kMaxPayloadSize = 64u * 1024u; // 64 KiB

// X-Macro list of message types.
// Each entry: X(Name, Value, IsKnown)
// - IsKnown: 1 => the message type is considered "known" by the runtime lookup
//            0 => present in the enum but treated as not-known for validation
//
// To add a new message type: add a new X(...) line here, then rebuild.
// The enum and any consumers can derive from this single source of truth.
#define MSG_TYPE_LIST \
    X(Unknown, 0, 1) \
    X(ShutdownRequest, 5, 1) \
    X(ShutdownConfirm, 6, 1) \
    X(ShutdownWithdraw, 7, 1) \
    X(Session_failed, 8, 1) \
    X(Session_verify, 9, 1) \
    X(ConnectTcp, 10, 0) \
    X(ConnectTls, 11, 0) \
    X(Test, 18, 1) \
    X(Ping, 21, 1) \
    X(Pong, 22, 1) \
    X(Data, 23, 1) \
    X(Auth, 24, 1) \
    /* SMS OTP messages */ \
    X(TYPE_AUTH_REQ, 101, 1) \
    X(TYPE_AUTH_RESP, 102, 1) \
    X(TYPE_OTP_VERIFY_REQ, 103, 1) \
    X(TYPE_OTP_VERIFY_RESP, 104, 1) \
    X(OtpRequest, 105, 1) \
    X(OtpResponse, 106, 1) \
    X(MedicalInfo, 107, 1) \
    X(MedicalInfoResp, 108, 1) \
    X(TYPE_OTP_SEND_REQ, 115, 1) \
    X(TYPE_OTP_SEND_RESP, 116, 1) \
    /* Authentication messages */ \
    X(AuthRequest, 151, 1) \
    X(AuthResponse, 152, 1) \
    X(LoginSms, 153, 1) \
    X(LoginSmsResponse, 154, 1) \
    X(PhoneOtpSend, 155, 1) \
    X(PhoneOtpVerify, 156, 1) \
    X(PhoneOtpResponse, 157, 1) \
    /* Supabase related messages (201-210) */ \
    X(SupabaseQuery, 201, 1) \
    X(SupabaseInsert, 202, 1) \
    X(SupabaseDelete, 203, 1) \
    X(SupabaseUpdate, 204, 1) \
    X(SupabaseResult, 205, 1) \
    /* COS related messages (211-220) */ \
    X(CosUpload, 211, 1) \
    X(CosDownload, 212, 1) \
    X(CosDelete, 213, 1) \
    X(CosList, 214, 1) \
    X(CosResult, 215, 1) \
    /* Node Bind messages (230-231) - 扫码绑定 */ \
    X(NodeBindReq, 230, 1) \
    X(NodeBindResp, 231, 1)

// Generate the enum from the X-macro list.
enum class MsgType : uint8_t {
#define X(name, val, is_known) name = val,
    MSG_TYPE_LIST
#undef X
};

// Build a compile-time lookup bitset for known message types.
// Using a constexpr std::array<bool,256> provides an efficient O(1)
// check by index while keeping the source-of-truth (the X-macro list).
inline constexpr std::array<bool, 256> BuildKnownMsgTypeArray() {
    std::array<bool, 256> a{};
#define X(name, val, is_known) if ((is_known) != 0) a[(val)] = true;
    MSG_TYPE_LIST
#undef X
        return a;
}

inline constexpr auto KnownMsgTypeBits = BuildKnownMsgTypeArray();

inline constexpr bool IsKnownMsgType(MsgType t) {
    return KnownMsgTypeBits[static_cast<uint8_t>(t)];
}

// Flags (bitmask)
struct MsgFlags {
    static constexpr uint8_t None = 0x00;
    static constexpr uint8_t Compressed = 0x01;
    static constexpr uint8_t FinalFragment = 0x02;
    static constexpr uint8_t AckRequested = 0x04;
    // more flags can be added while keeping header layout stable
};

#pragma pack(push, 1)
struct AppProtoHeader {
    uint8_t magic[4];         // protocol magic, e.g. {'H','B','P','C'}
    uint8_t version;          // protocol version
    uint8_t type;             // MsgType
    uint8_t flags;            // MsgFlags bitmask
    uint8_t reserved1;        // reserved/padding to keep alignment explicit
    uint32_t seq;             // sequence number (network byte order on the wire)
    uint32_t payload_len;     // length of payload in bytes (<= kMaxPayloadSize) (network byte order)
    uint64_t session_id;      // session identifier (8 bytes)
    uint64_t timestamp_ms;    // sender timestamp (ms since epoch) (network byte order)
    uint8_t auth_token[16];    // opaque auth/session id (16 bytes)
    uint8_t reserved[16];     // padding / future use to reach 64 bytes
};
#pragma pack(pop)

static_assert(sizeof(AppProtoHeader) == kProtoHeaderSize, "AppProtoHeader must be 64 bytes");

////由于windows相关头文件的声明需要在MFC头文件之后，并且在Connection中已经声明了这些函数，所以在将下方注释的函数移动到Connection_c.h中
//// Portable 64-bit hton/ntoh helpers for timestamp
//static inline uint64_t hton64(uint64_t host64) noexcept {
//    // Break into two 32-bit halves and use htonl
//    uint32_t hi = static_cast<uint32_t>(host64 >> 32);
//    uint32_t lo = static_cast<uint32_t>(host64 & 0xFFFFFFFFu);
//    uint32_t nhi = htonl(hi);
//    uint32_t nlo = htonl(lo);
//    return (static_cast<uint64_t>(nlo) << 32) | static_cast<uint64_t>(nhi);
//}
//
//static inline uint64_t ntoh64(uint64_t net64) noexcept {
//    uint32_t hi = static_cast<uint32_t>(net64 >> 32);
//    uint32_t lo = static_cast<uint32_t>(net64 & 0xFFFFFFFFu);
//    uint32_t hhi = ntohl(hi);
//    uint32_t hlo = ntohl(lo);
//    return (static_cast<uint64_t>(hhi) << 32) | static_cast<uint64_t>(hlo);
//}
//
//// Convenience getters/setters for header numeric fields (explicitly use network order on-wire)
//static inline void AppProtoHeader_SetPayloadLen(AppProtoHeader* h, uint32_t payload_len) noexcept {
//    h->payload_len = htonl(payload_len);
//}
//static inline uint32_t AppProtoHeader_GetPayloadLen(const AppProtoHeader* h) noexcept {
//    return ntohl(h->payload_len);
//}
//static inline void AppProtoHeader_SetSeq(AppProtoHeader* h, uint32_t seq) noexcept {
//    h->seq = htonl(seq);
//}
//static inline uint32_t AppProtoHeader_GetSeq(const AppProtoHeader* h) noexcept {
//    return ntohl(h->seq);
//}
//static inline void AppProtoHeader_SetTimestampMs(AppProtoHeader* h, uint64_t ts_ms) noexcept {
//    h->timestamp_ms = hton64(ts_ms);
//}
//static inline uint64_t AppProtoHeader_GetTimestampMs(const AppProtoHeader* h) noexcept {
//    return ntoh64(h->timestamp_ms);
//}

