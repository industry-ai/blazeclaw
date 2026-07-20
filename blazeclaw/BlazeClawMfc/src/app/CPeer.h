#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace blazeclaw::irc {

// User mode flags (RFC 1459)
enum class Mode : uint32_t {
    None       = 0,
    Operator   = 1 << 0,  // +o - IRC Operator
    Voice      = 1 << 1,  // +v - Can speak in moderated channel
    Away       = 1 << 2,  // +a - User is away
    Invisible  = 1 << 3,  // +i - Invisible (not in WHO/WHOIS global list)
};

constexpr Mode operator|(Mode a, Mode b) {
    return static_cast<Mode>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr Mode operator&(Mode a, Mode b) {
    return static_cast<Mode>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr Mode operator~(Mode m) {
    return static_cast<Mode>(~static_cast<uint32_t>(m));
}

constexpr Mode& operator|=(Mode& a, Mode b) {
    a = a | b;
    return a;
}

constexpr Mode& operator&=(Mode& a, Mode b) {
    a = a & b;
    return a;
}

constexpr bool operator!(Mode m) {
    return static_cast<uint32_t>(m) == 0;
}

// Base class for channel members
class CPeer {
public:
    CPeer() = delete;
    CPeer(const CPeer&) = delete;
    CPeer& operator=(const CPeer&) = delete;
    CPeer(CPeer&&) = delete;
    CPeer& operator=(CPeer&&) = delete;
    virtual ~CPeer() = default;

    // Identity accessors
    const std::string& GetNickname() const noexcept { return nickname_; }
    const std::string& GetUsername() const noexcept { return username_; }
    const std::string& GetHostname() const noexcept { return hostname_; }
    const std::string& GetRealname() const noexcept { return realname_; }

    // Full prefix: nick!user@host
    std::string GetPrefix() const;
    std::string GetHostmask() const;  // nick!user@host (alias for GetPrefix)

    // Mode management
    bool HasMode(Mode m) const noexcept {
        return (mode_ & m) == m;
    }
    void SetMode(Mode m, bool enable) noexcept {
        if (enable) {
            mode_ |= m;
        } else {
            mode_ &= ~m;
        }
    }
    Mode GetModes() const noexcept { return mode_; }

    // Join order (for operator handover priority)
    uint64_t GetJoinOrder() const noexcept { return join_order_; }

    // Virtual operator check (overridden by COperator)
    virtual bool IsOperator() const noexcept { return false; }
    virtual bool IsLocalOperator() const noexcept { return false; }
    virtual bool IsGlobalOperator() const noexcept { return false; }

    // Convenience helpers
    bool IsAway() const noexcept { return HasMode(Mode::Away); }
    bool IsInvisible() const noexcept { return HasMode(Mode::Invisible); }
    bool HasVoice() const noexcept { return HasMode(Mode::Voice) || IsOperator(); }

    // Factory method
    static std::shared_ptr<CPeer> Create(const std::string& nickname);

protected:
    explicit CPeer(const std::string& nickname,
                   const std::string& username = "",
                   const std::string& hostname = "",
                   const std::string& realname = "",
                   uint64_t join_order = 0)
        : nickname_(nickname)
        , username_(username)
        , hostname_(hostname)
        , realname_(realname)
        , mode_(Mode::None)
        , join_order_(join_order) {}

    // Setters for mutable fields
    void SetNickname(const std::string& nick) { nickname_ = nick; }
    void SetUsername(const std::string& user) { username_ = user; }
    void SetHostname(const std::string& host) { hostname_ = host; }
    void SetRealname(const std::string& name) { realname_ = name; }

private:
    std::string nickname_;
    std::string username_;
    std::string hostname_;
    std::string realname_;
    Mode mode_;
    uint64_t join_order_;
};

// Shared pointer type alias
using CPeerPtr = std::shared_ptr<CPeer>;

} // namespace blazeclaw::irc
