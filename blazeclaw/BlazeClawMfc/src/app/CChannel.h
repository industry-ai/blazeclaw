#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CPeer.h"
#include "CTopic.h"
#include "Logger.h"

namespace blazeclaw::irc {

// Forward declaration
class CMgrChannels;

// Channel mode flags (RFC 1459)
enum class ChannelMode : uint32_t {
    None         = 0,
    Private      = 1 << 0,  // +p - Channel is private
    Secret       = 1 << 1,  // +s - Channel is secret
    InviteOnly   = 1 << 2,  // +i - Invite only
    Moderate     = 1 << 3,  // +m - Moderated (only voiced/ops can speak)
    NoOutside    = 1 << 4,  // +n - No outside messages
    Quiet        = 1 << 5,  // +q - Quiet (no messages at all)
    TopicLock    = 1 << 6,  // +t - Only ops can change topic
    KeyLock      = 1 << 7,  // +k - Key required to join
    UserLimit    = 1 << 8,  // +l - User limit set
};

constexpr ChannelMode operator|(ChannelMode a, ChannelMode b) {
    return static_cast<ChannelMode>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr ChannelMode operator&(ChannelMode a, ChannelMode b) {
    return static_cast<ChannelMode>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr ChannelMode operator~(ChannelMode m) {
    return static_cast<ChannelMode>(~static_cast<uint32_t>(m));
}

constexpr ChannelMode& operator|=(ChannelMode& a, ChannelMode b) {
    a = a | b;
    return a;
}

constexpr ChannelMode& operator&=(ChannelMode& a, ChannelMode b) {
    a = a & b;
    return a;
}

// Ban list entry
struct BanEntry {
    std::string mask;         // e.g., "*!*@example.com"
    std::string set_by;      // Operator who set the ban
    uint64_t set_at_ms;      // Timestamp
};

// Invite list entry
struct InviteEntry {
    std::string nick;         // Nickname allowed to invite
    std::string set_by;       // Operator who added
    uint64_t set_at_ms;       // Timestamp
};

// Channel event types
enum class ChannelEventType {
    MemberJoined,
    MemberParted,
    MemberKicked,
    MemberBanned,
    MemberUnbanned,
    MemberInvited,
    OperatorGranted,
    OperatorRevoked,
    OpSwitch,        // Operator handover occurred
    OpVacant,        // No operators left
    TopicChanged,
    ModeChanged,
    MessageSent,
};

// Channel event
struct ChannelEvent {
    ChannelEventType type;
    std::string channel_name;
    std::string actor_nick;    // Who triggered the event
    std::string target_nick;    // Who was affected
    std::string data;          // Additional data (reason, new mode, etc.)
    uint64_t timestamp_ms;
};

// Callback type for channel events
using ChannelEventCallback = std::function<void(const ChannelEvent&)>;

// Single channel class
class CChannel {
public:
    CChannel() = delete;
    CChannel(const CChannel&) = delete;
    CChannel& operator=(const CChannel&) = delete;
    CChannel(CChannel&&) = delete;
    CChannel& operator=(CChannel&&) = delete;
    ~CChannel() = default;

    // Factory
    static std::shared_ptr<CChannel> Create(const std::string& name,
                                             CPeerPtr founder);

    // Properties
    const std::string& GetName() const noexcept { return name_; }
    const std::string& GetTopic() const noexcept { return topic_; }
    void SetTopic(const std::string& new_topic);
    const std::string& GetTopicSetter() const noexcept { return topic_setter_; }
    uint64_t GetTopicSetAt() const noexcept { return topic_set_at_ms_; }

    // Channel modes
    void SetMode(ChannelMode mode, bool enable);
    bool HasMode(ChannelMode mode) const noexcept {
        return (mode_flags_ & mode) == mode;
    }
    ChannelMode GetModes() const noexcept { return mode_flags_; }

    // Key (for +k mode)
    const std::string& GetKey() const noexcept { return channel_key_; }
    void SetKey(const std::string& key);

    // User limit (for +l mode)
    uint32_t GetUserLimit() const noexcept { return user_limit_; }
    void SetUserLimit(uint32_t limit);

    // Founder
    const std::string& GetFounderNick() const noexcept { return founder_nick_; }

    // Member management
    bool AddMember(CPeerPtr peer);
    bool RemoveMember(const std::string& nick);
    bool HasMember(const std::string& nick) const;
    std::shared_ptr<CPeer> GetMember(const std::string& nick) const;
    std::vector<std::string> GetMemberNicks() const;
    size_t GetMemberCount() const noexcept { return members_.size(); }

    // Operator management
    bool GrantOperator(const std::string& nick);
    bool RevokeOperator(const std::string& nick);
    bool IsOperator(const std::string& nick) const;
    std::vector<std::string> GetOperators() const;
    size_t GetOperatorCount() const noexcept { return operators_.size(); }

    // Operator handover when last operator leaves
    bool OnOperatorLeft(const std::string& nick);
    std::string SelectNextOperator();  // Returns empty if no one available

    // Permission checks
    bool CanSpeak(const std::string& nick) const;
    bool CanModifyTopic(const std::string& nick) const;
    bool CanKick(const std::string& nick, const std::string& target) const;
    bool CanBan(const std::string& nick) const;

    // Invite management
    bool AddInvite(const std::string& nick, const std::string& set_by);
    bool RemoveInvite(const std::string& nick);
    bool IsInvited(const std::string& nick) const;
    bool CanJoin(const std::string& nick, const std::string& key) const;

    // Ban management
    bool AddBan(const std::string& mask, const std::string& set_by);
    bool RemoveBan(const std::string& mask);
    bool IsBanned(const std::string& nick) const;
    std::vector<BanEntry> GetBans() const;

    // Broadcast
    void Broadcast(const std::string& message, CPeerPtr exclude = nullptr);
    void BroadcastToOps(const std::string& message);
    void BroadcastWithPrefix(const std::string& message, const std::string& prefix);

    // Event callbacks
    void SetEventCallback(ChannelEventCallback callback);
    void FireEvent(const ChannelEvent& event);

    // Topic management (Newsgroup-style)
    std::shared_ptr<CTopic> CreateTopic(const std::string& title,
                                        const std::string& creator_nick,
                                        const std::string& initial_content = "");
    std::shared_ptr<CTopic> GetTopic(const std::string& topic_id) const;
    std::vector<std::shared_ptr<CTopic>> ListTopics(const TopicFilter& filter = {}) const;
    bool CloseTopic(const std::string& topic_id, const std::string& operator_nick);
    bool DeleteTopic(const std::string& topic_id);

private:
    void FireEventUnsafe(const ChannelEvent& event);  // Called without lock

    // Diagnostics
    uint64_t GetCreatedAt() const noexcept { return created_at_ms_; }
    uint64_t GetTotalMessages() const noexcept { return total_messages_.load(); }
    void IncrementMessageCount() { total_messages_.fetch_add(1); }

private:
    explicit CChannel(const std::string& name, CPeerPtr founder);

    bool CanOperate(const std::string& nick) const;

    // Static wildcard matching helpers for ban masks
    static bool WildcardMatch(const std::string& str, const std::string& pattern);
    static bool WildcardMatchChar(char c, char p);
    static uint64_t GetCurrentTimestampMs();

    // Internal helper for ban checking (assumes lock is held)
    bool CheckBannedInternal(const std::string& nick) const;

    std::string name_;
    std::string topic_;
    std::string topic_setter_;
    uint64_t topic_set_at_ms_;
    uint64_t created_at_ms_;

    ChannelMode mode_flags_;
    std::string channel_key_;
    uint32_t user_limit_;

    std::string founder_nick_;

    std::unordered_map<std::string, CPeerPtr> members_;  // nick -> peer
    std::unordered_set<std::string> operators_;  // nick set
    std::unordered_set<std::string> voiced_;     // nick set (+v)

    std::unordered_map<std::string, BanEntry> bans_;
    std::unordered_set<std::string> invites_;  // nicks that can bypass +i

    std::unordered_map<std::string, CTopicPtr> topics_;  // topic_id -> topic

    ChannelEventCallback event_callback_;

    std::atomic<uint64_t> total_messages_{0};
    mutable std::mutex mutex_;
};

// Shared pointer type alias
using CChannelPtr = std::shared_ptr<CChannel>;

} // namespace blazeclaw::irc
