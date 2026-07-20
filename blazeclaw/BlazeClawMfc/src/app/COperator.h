#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "CPeer.h"

namespace blazeclaw::irc {

// Operator type (level)
enum class OperType : uint32_t {
    None           = 0,
    LocalOperator  = 1 << 0,  // #oper - Local network operator
    GlobalOperator = 1 << 1,   // *oper* - Global network operator
};

constexpr OperType operator|(OperType a, OperType b) {
    return static_cast<OperType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr OperType operator&(OperType a, OperType b) {
    return static_cast<OperType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr OperType operator~(OperType m) {
    return static_cast<OperType>(~static_cast<uint32_t>(m));
}

constexpr OperType& operator|=(OperType& a, OperType b) {
    a = a | b;
    return a;
}

// Operator privilege flags (fine-grained permissions)
enum class OperPrivilege : uint32_t {
    None         = 0,
    KillUsers    = 1 << 0,  // KILL command
    SquitServers = 1 << 1,  // SQUIT command
    ChannelOps   = 1 << 2,  // Channel operations (KICK, MODE, BAN)
    SetHost      = 1 << 3,  // Set hostname
    GLine        = 1 << 4,  // Global ban
    ServerLink   = 1 << 5,  // Server link authorization
    OperWallPriv = 1 << 6,  // OPERWALL broadcast
};

constexpr OperPrivilege operator|(OperPrivilege a, OperPrivilege b) {
    return static_cast<OperPrivilege>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr OperPrivilege operator&(OperPrivilege a, OperPrivilege b) {
    return static_cast<OperPrivilege>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr OperPrivilege operator~(OperPrivilege m) {
    return static_cast<OperPrivilege>(~static_cast<uint32_t>(m));
}

constexpr OperPrivilege& operator|=(OperPrivilege& a, OperPrivilege b) {
    a = a | b;
    return a;
}

constexpr OperPrivilege& operator&=(OperPrivilege& a, OperPrivilege b) {
    a = a & b;
    return a;
}

// Operation log entry
struct OperLogEntry {
    uint64_t timestamp_ms;
    std::string action;       // e.g., "KICK", "MODE", "BAN"
    std::string target;       // Target channel or user
    std::string details;     // Additional details
};

// IRC Operator class (inherits from CPeer)
class COperator : public CPeer {
public:
    COperator() = delete;
    COperator(const COperator&) = delete;
    COperator& operator=(const COperator&) = delete;
    COperator(COperator&&) = delete;
    COperator& operator=(COperator&&) = delete;
    ~COperator() override = default;

    // Factory method
    static std::shared_ptr<COperator> Create(const std::string& nickname,
                                              const std::string& username = "",
                                              const std::string& hostname = "",
                                              const std::string& realname = "",
                                              uint64_t join_order = 0);

    // Operator type management
    void SetOperType(OperType type) noexcept { oper_type_ = type; }
    OperType GetOperType() const noexcept { return oper_type_; }
    bool IsLocalOperatorType() const noexcept {
        return (oper_type_ & OperType::LocalOperator) != OperType::None;
    }
    bool IsGlobalOperatorType() const noexcept {
        return (oper_type_ & OperType::GlobalOperator) != OperType::None;
    }

    // Privilege management
    void GrantPrivilege(OperPrivilege priv) noexcept { privileges_ = privileges_ | priv; }
    void RevokePrivilege(OperPrivilege priv) noexcept { privileges_ = privileges_ & (~priv); }
    bool HasPrivilege(OperPrivilege priv) const noexcept {
        return (privileges_ & priv) == priv;
    }
    OperPrivilege GetPrivileges() const noexcept { return privileges_; }

    // Override from CPeer
    bool IsOperator() const noexcept override { return true; }
    bool IsLocalOperator() const noexcept override {
        return (oper_type_ & OperType::LocalOperator) != OperType::None;
    }
    bool IsGlobalOperator() const noexcept override {
        return (oper_type_ & OperType::GlobalOperator) != OperType::None;
    }

    // Permission checks
    bool CanKillUsers() const noexcept { return HasPrivilege(OperPrivilege::KillUsers); }
    bool CanSquitServers() const noexcept { return HasPrivilege(OperPrivilege::SquitServers); }
    bool CanOperateChannels() const noexcept { return HasPrivilege(OperPrivilege::ChannelOps); }
    bool CanSetHost() const noexcept { return HasPrivilege(OperPrivilege::SetHost); }
    bool CanGline() const noexcept { return HasPrivilege(OperPrivilege::GLine); }
    bool CanServerLink() const noexcept { return HasPrivilege(OperPrivilege::ServerLink); }
    bool CanOperWall() const noexcept { return HasPrivilege(OperPrivilege::OperWallPriv); }

    // OperWall broadcast
    void SendOperWall(const std::string& message);
    std::string GetOperWallMessage() const;

    // Operation logging
    void LogOperation(const std::string& action,
                     const std::string& target,
                     const std::string& details = "");
    const std::deque<OperLogEntry>& GetOperLog() const noexcept { return oper_log_; }

    // Auto OPER support (connect and automatically authenticate)
    bool IsAutoOperEnabled() const noexcept { return auto_oper_enabled_; }
    void SetAutoOperEnabled(bool enabled) noexcept { auto_oper_enabled_ = enabled; }

protected:
    explicit COperator(const std::string& nickname,
                       const std::string& username = "",
                       const std::string& hostname = "",
                       const std::string& realname = "",
                       uint64_t join_order = 0)
        : CPeer(nickname, username, hostname, realname, join_order)
        , oper_type_(OperType::None)
        , privileges_(OperPrivilege::None)
        , auto_oper_enabled_(false) {}

private:
    OperType oper_type_;
    OperPrivilege privileges_;
    bool auto_oper_enabled_;

    static constexpr size_t MAX_LOG_ENTRIES = 1000;
    std::deque<OperLogEntry> oper_log_;
    mutable std::mutex oper_log_mutex_;
};

// Shared pointer type alias
using COperatorPtr = std::shared_ptr<COperator>;

} // namespace blazeclaw::irc
