#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CChannel.h"
#include "Logger.h"

namespace blazeclaw::irc {

// Join result
struct JoinResult {
    bool success;
    std::string error_message;
    std::shared_ptr<CChannel> channel;
};

// Part result
struct PartResult {
    bool success;
    std::string error_message;
};

// Kick result
struct KickResult {
    bool success;
    std::string error_message;
};

// Topic result
struct TopicResult {
    bool success;
    std::string error_message;
    std::string topic;
};

// Mode result
struct ModeResult {
    bool success;
    std::string error_message;
    ChannelMode new_mode;
};

// Ban result
struct BanResult {
    bool success;
    std::string error_message;
};

// Whois info
struct WhoisInfo {
    std::string nickname;
    std::string username;
    std::string hostname;
    std::string realname;
    std::vector<std::string> channels;  // Channels they are in
    bool is_operator;
    bool is_away;
    std::string away_message;
};

// Stored chat message (for message history)
struct StoredMessage {
    std::string sender;
    std::string message;
    std::string channel;
    uint64_t timestamp_ms;
    std::string raw_line;
};

// Member event types
enum class MemberEventType {
    Registered,     // Member registered globally
    Unregistered,   // Member unregistered globally
    JoinedChannel,
    PartedChannel,
};

// Member event
struct MemberEvent {
    MemberEventType type;
    std::string nick;
    std::optional<std::string> channel_name;  // For JOIN/PART events
    uint64_t timestamp_ms;
};

// Diagnostics snapshot
struct Diagnostics {
    uint64_t channels_created = 0;
    uint64_t channels_destroyed = 0;
    uint64_t joins = 0;
    uint64_t parts = 0;
    uint64_t kicks = 0;
    uint64_t bans = 0;
    uint64_t total_members = 0;
    uint64_t total_channels = 0;
};

// Channel event callback
using ChannelEventCallback = std::function<void(const ChannelEvent&)>;

// Member event callback
using MemberEventCallback = std::function<void(const MemberEvent&)>;

// Channel manager - central management for all channels
class CMgrChannels {
public:
    CMgrChannels() = default;
    ~CMgrChannels() = default;

    CMgrChannels(const CMgrChannels&) = delete;
    CMgrChannels& operator=(const CMgrChannels&) = delete;
    CMgrChannels(CMgrChannels&&) = delete;
    CMgrChannels& operator=(CMgrChannels&&) = delete;

public:
    // Singleton access
    static CMgrChannels& Instance() {
        static CMgrChannels instance;
        return instance;
    }

    // Channel management
    std::shared_ptr<CChannel> CreateChannel(const std::string& name,
                                           CPeerPtr founder,
                                           const std::string& key = "");
    bool DestroyChannel(const std::string& name);
    std::shared_ptr<CChannel> GetChannel(const std::string& name) const;
    std::vector<std::string> GetChannelList() const;
    size_t GetChannelCount() const noexcept;

    // Member management
    void RegisterMember(CPeerPtr peer);
    void UnregisterMember(const std::string& nick);
    std::shared_ptr<CPeer> GetMember(const std::string& nick) const;
    std::vector<std::string> GetRegisteredMemberNicks() const;
    bool IsMemberRegistered(const std::string& nick) const;

    // JOIN / PART
    JoinResult Join(CPeerPtr peer, const std::string& channel, const std::string& key = "");
    PartResult Part(CPeerPtr peer, const std::string& channel, const std::string& reason = "");

    // KICK
    KickResult Kick(const std::string& op_nick, const std::string& channel,
                   const std::string& target, const std::string& reason = "");

    // TOPIC
    TopicResult SetTopic(const std::string& nick, const std::string& channel,
                        const std::string& topic);

    // MODE
    ModeResult SetChannelMode(const std::string& op_nick, const std::string& channel,
                              const std::string& mode_str);

    // BAN
    BanResult Ban(const std::string& op_nick, const std::string& channel,
                 const std::string& mask);
    BanResult Unban(const std::string& op_nick, const std::string& channel,
                   const std::string& mask);

    // INVITE
    bool Invite(const std::string& op_nick, const std::string& channel,
               const std::string& target_nick);

    // Operator management
    bool PromoteToOperator(const std::string& oper_nick,
                          const std::string& channel,
                          const std::string& target_nick);
    bool DemoteOperator(const std::string& oper_nick,
                        const std::string& channel,
                        const std::string& target_nick);

    // Query commands
    std::optional<WhoisInfo> Whois(const std::string& nick) const;
    std::vector<std::string> GetNamesList(const std::string& channel) const;

    // Permission checks
    bool CanOperate(const std::string& nick, const std::string& channel) const;

    // Event callbacks
    void SetChannelEventCallback(ChannelEventCallback callback);
    void SetMemberEventCallback(MemberEventCallback callback);

    // Message storage for history
    void AddMessage(const StoredMessage& msg);
    std::vector<StoredMessage> GetMessages(const std::string& channel, int limit = 50);

    // Diagnostics
    Diagnostics GetDiagnostics() const;
    void ResetDiagnostics();

    // Channel event forwarding
    void ForwardChannelEvent(const ChannelEvent& event);

private:
    void FireChannelEvent(const ChannelEvent& event);
    void FireMemberEvent(const MemberEvent& event);
    void FireMemberEvent(MemberEvent&& event);

    mutable std::shared_mutex channels_mutex_;
    mutable std::shared_mutex members_mutex_;
    mutable std::mutex messages_mutex_;

    std::unordered_map<std::string, CChannelPtr> channels_;  // name -> channel
    std::unordered_map<std::string, CPeerPtr> members_;      // nick -> peer
    std::unordered_map<std::string, std::vector<StoredMessage>> messages_;  // channel -> messages
    static constexpr size_t kMaxMessagesPerChannel = 1000;

    ChannelEventCallback channel_event_callback_;
    MemberEventCallback member_event_callback_;

    // Counters
    std::atomic<uint64_t> channels_created_{0};
    std::atomic<uint64_t> channels_destroyed_{0};
    std::atomic<uint64_t> joins_{0};
    std::atomic<uint64_t> parts_{0};
    std::atomic<uint64_t> kicks_{0};
    std::atomic<uint64_t> bans_{0};
};

// Shared pointer type alias
using CMgrChannelsPtr = std::shared_ptr<CMgrChannels>;

} // namespace blazeclaw::irc
