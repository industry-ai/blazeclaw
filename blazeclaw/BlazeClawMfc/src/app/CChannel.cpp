#include "pch.h"
#include "CChannel.h"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace blazeclaw::irc {

// static
std::shared_ptr<CChannel> CChannel::Create(const std::string& name,
                                           CPeerPtr founder) {
    auto channel = std::shared_ptr<CChannel>(new CChannel(name, founder));
    // Founder automatically becomes operator
    channel->GrantOperator(founder->GetNickname());
    return channel;
}

CChannel::CChannel(const std::string& name, CPeerPtr founder)
    : name_(name)
    , topic_()
    , topic_setter_()
    , topic_set_at_ms_(0)
    , created_at_ms_(GetCurrentTimestampMs())
    , mode_flags_(ChannelMode::None)
    , user_limit_(0) {
    if (founder) {
        founder_nick_ = founder->GetNickname();
        AddMember(founder);
    }
}

uint64_t CChannel::GetCurrentTimestampMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

void CChannel::SetTopic(const std::string& new_topic) {
    std::lock_guard<std::mutex> lock(mutex_);
    topic_ = new_topic;
    topic_set_at_ms_ = GetCurrentTimestampMs();
}

void CChannel::SetMode(ChannelMode mode, bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enable) {
        mode_flags_ |= mode;
    } else {
        mode_flags_ &= ~mode;
    }
}

void CChannel::SetKey(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    channel_key_ = key;
    if (!key.empty()) {
        mode_flags_ |= ChannelMode::KeyLock;
    } else {
        mode_flags_ &= ~ChannelMode::KeyLock;
    }
}

void CChannel::SetUserLimit(uint32_t limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    user_limit_ = limit;
    if (limit > 0) {
        mode_flags_ |= ChannelMode::UserLimit;
    } else {
        mode_flags_ &= ~ChannelMode::UserLimit;
    }
}

bool CChannel::AddMember(CPeerPtr peer) {
    if (!peer) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto& nick = peer->GetNickname();

    if (members_.contains(nick)) {
        return false;  // Already a member
    }

    members_[nick] = peer;
    return true;
}

bool CChannel::RemoveMember(const std::string& nick) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto member_it = members_.find(nick);
    if (member_it == members_.end()) {
        return false;
    }

    // Remove from operators and voiced
    operators_.erase(nick);
    voiced_.erase(nick);

    members_.erase(member_it);
    return true;
}

bool CChannel::HasMember(const std::string& nick) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return members_.contains(nick);
}

std::shared_ptr<CPeer> CChannel::GetMember(const std::string& nick) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = members_.find(nick);
    if (it != members_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> CChannel::GetMemberNicks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    result.reserve(members_.size());
    for (const auto& [nick, peer] : members_) {
        (void)peer;
        result.push_back(nick);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool CChannel::GrantOperator(const std::string& nick) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto member_it = members_.find(nick);
    if (member_it == members_.end()) {
        return false;
    }

    operators_.insert(nick);
    member_it->second->SetMode(Mode::Operator, true);

    // Fire event (use Unsafe since we already hold the lock)
    ChannelEvent event;
    event.type = ChannelEventType::OperatorGranted;
    event.channel_name = name_;
    event.actor_nick = nick;
    event.target_nick = nick;
    event.timestamp_ms = GetCurrentTimestampMs();
    FireEventUnsafe(event);

    return true;
}

bool CChannel::RevokeOperator(const std::string& nick) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!operators_.contains(nick)) {
        return false;
    }

    operators_.erase(nick);
    auto member_it = members_.find(nick);
    if (member_it != members_.end()) {
        member_it->second->SetMode(Mode::Operator, false);
    }

    // Fire event (use Unsafe since we already hold the lock)
    ChannelEvent event;
    event.type = ChannelEventType::OperatorRevoked;
    event.channel_name = name_;
    event.actor_nick = nick;
    event.target_nick = nick;
    event.timestamp_ms = GetCurrentTimestampMs();
    FireEventUnsafe(event);

    return true;
}

bool CChannel::IsOperator(const std::string& nick) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return operators_.contains(nick);
}

std::vector<std::string> CChannel::GetOperators() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<std::string>(operators_.begin(), operators_.end());
}

bool CChannel::CanOperate(const std::string& nick) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Check if operator in THIS channel
    if (operators_.contains(nick)) {
        return true;
    }
    // Also check if global operator
    auto member_it = members_.find(nick);
    if (member_it != members_.end()) {
        return member_it->second->IsOperator();
    }
    return false;
}

bool CChannel::OnOperatorLeft(const std::string& nick) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!operators_.contains(nick)) {
        return false;  // Wasn't an operator
    }

    operators_.erase(nick);
    auto member_it = members_.find(nick);
    if (member_it != members_.end()) {
        member_it->second->SetMode(Mode::Operator, false);
    }

    // Check if any operators remain
    if (!operators_.empty()) {
        return true;  // Still have operators, no handover needed
    }

    // Need to select next operator
    std::string next_op = SelectNextOperator();

    if (!next_op.empty()) {
        // Grant operator to next in line
        operators_.insert(next_op);
        auto next_member_it = members_.find(next_op);
        if (next_member_it != members_.end()) {
            next_member_it->second->SetMode(Mode::Operator, true);
        }

        // Fire OP_SWITCH event
        ChannelEvent event;
        event.type = ChannelEventType::OpSwitch;
        event.channel_name = name_;
        event.actor_nick = nick;
        event.target_nick = next_op;
        event.timestamp_ms = GetCurrentTimestampMs();
        FireEventUnsafe(event);  // Already holding lock
    } else {
        // No operators available
        ChannelEvent event;
        event.type = ChannelEventType::OpVacant;
        event.channel_name = name_;
        event.actor_nick = nick;
        event.target_nick = "";
        event.timestamp_ms = GetCurrentTimestampMs();
        FireEventUnsafe(event);  // Already holding lock
    }

    return true;
}

std::string CChannel::SelectNextOperator() {
    // Priority 1: Channel founder (if still in channel)
    if (!founder_nick_.empty() && members_.contains(founder_nick_)) {
        return founder_nick_;
    }

    // Priority 2: Earliest join non-operator (by join_order)
    // For simplicity, just return the first non-operator member
    // In a real implementation, would track join_order in CPeer
    for (const auto& [nick, peer] : members_) {
        (void)peer;
        if (!operators_.contains(nick)) {
            return nick;
        }
    }

    return "";  // No suitable candidate
}

bool CChannel::CanSpeak(const std::string& nick) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // No moderated mode = everyone can speak
    if ((mode_flags_ & ChannelMode::Moderate) == ChannelMode::None) {
        return true;
    }

    // Operators can always speak
    if (operators_.contains(nick)) {
        return true;
    }

    // Voiced users can speak
    if (voiced_.contains(nick)) {
        return true;
    }

    return false;
}

bool CChannel::CanModifyTopic(const std::string& nick) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // No topic lock = everyone can modify
    if ((mode_flags_ & ChannelMode::TopicLock) == ChannelMode::None) {
        return true;
    }

    // Operators can always modify
    return CanOperate(nick);
}

bool CChannel::CanKick(const std::string& nick, const std::string& target) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Can't kick yourself (use PART instead)
    if (nick == target) {
        return false;
    }

    // Operators can kick
    return CanOperate(nick);
}

bool CChannel::CanBan(const std::string& nick) const {
    return CanOperate(nick);
}

bool CChannel::AddInvite(const std::string& nick, const std::string& set_by) {
    std::lock_guard<std::mutex> lock(mutex_);
    invites_.insert(nick);

    // Fire event (use Unsafe since we already hold the lock)
    ChannelEvent event;
    event.type = ChannelEventType::MemberInvited;
    event.channel_name = name_;
    event.actor_nick = set_by;
    event.target_nick = nick;
    event.timestamp_ms = GetCurrentTimestampMs();
    FireEventUnsafe(event);

    return true;
}

bool CChannel::RemoveInvite(const std::string& nick) {
    std::lock_guard<std::mutex> lock(mutex_);
    return invites_.erase(nick) > 0;
}

bool CChannel::IsInvited(const std::string& nick) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return invites_.contains(nick);
}

bool CChannel::CanJoin(const std::string& nick, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check user limit
    if ((mode_flags_ & ChannelMode::UserLimit) != ChannelMode::None) {
        if (members_.size() >= user_limit_) {
            return false;
        }
    }

    // Check key
    if ((mode_flags_ & ChannelMode::KeyLock) != ChannelMode::None) {
        if (channel_key_ != key) {
            return false;
        }
    }

    // Check invite-only (bypass if invited)
    if ((mode_flags_ & ChannelMode::InviteOnly) != ChannelMode::None) {
        if (!invites_.contains(nick)) {
            return false;
        }
    }

    // Check ban list (internal helper, no lock needed)
    if (CheckBannedInternal(nick)) {
        return false;
    }

    return true;
}

bool CChannel::CheckBannedInternal(const std::string& nick) const {
    // Get member info if exists
    std::string username, hostname;
    auto member_it = members_.find(nick);
    if (member_it != members_.end()) {
        username = member_it->second->GetUsername();
        hostname = member_it->second->GetHostname();
    }

    for (const auto& [mask, entry] : bans_) {
        (void)entry;
        std::string pattern = mask;

        // Find the nick!user@host parts
        size_t nick_end = pattern.find('!');
        size_t user_end = pattern.find('@', nick_end != std::string::npos ? nick_end + 1 : 0);

        std::string mask_nick = (nick_end != std::string::npos) ? pattern.substr(0, nick_end) : pattern;
        std::string mask_user = (nick_end != std::string::npos && user_end != std::string::npos)
            ? pattern.substr(nick_end + 1, user_end - nick_end - 1) : "*";
        std::string mask_host = (user_end != std::string::npos)
            ? pattern.substr(user_end + 1) : "*";

        if (WildcardMatch(nick, mask_nick) &&
            WildcardMatch(username, mask_user) &&
            WildcardMatch(hostname, mask_host)) {
            return true;
        }
    }
    return false;
}

bool CChannel::AddBan(const std::string& mask, const std::string& set_by) {
    std::lock_guard<std::mutex> lock(mutex_);

    BanEntry entry;
    entry.mask = mask;
    entry.set_by = set_by;
    entry.set_at_ms = GetCurrentTimestampMs();

    bans_[mask] = std::move(entry);

    // Fire event (use Unsafe since we already hold the lock)
    ChannelEvent event;
    event.type = ChannelEventType::MemberBanned;
    event.channel_name = name_;
    event.actor_nick = set_by;
    event.target_nick = mask;
    event.timestamp_ms = GetCurrentTimestampMs();
    FireEventUnsafe(event);

    return true;
}

bool CChannel::RemoveBan(const std::string& mask) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = bans_.find(mask);
    if (it == bans_.end()) {
        return false;
    }

    // Fire event (use Unsafe since we already hold the lock)
    ChannelEvent event;
    event.type = ChannelEventType::MemberUnbanned;
    event.channel_name = name_;
    event.actor_nick = "";  // Unknown who removed
    event.target_nick = mask;
    event.timestamp_ms = GetCurrentTimestampMs();
    FireEventUnsafe(event);

    bans_.erase(it);
    return true;
}

bool CChannel::IsBanned(const std::string& nick) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return CheckBannedInternal(nick);
}

std::vector<BanEntry> CChannel::GetBans() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BanEntry> result;
    for (const auto& [mask, entry] : bans_) {
        (void)mask;
        result.push_back(entry);
    }
    return result;
}

bool CChannel::WildcardMatch(const std::string& str, const std::string& pattern) {
    if (pattern == "*") return true;
    if (pattern.empty()) return str.empty();

    size_t s = 0, p = 0;
    while (s < str.size() && p < pattern.size()) {
        if (pattern[p] == '*') {
            if (++p >= pattern.size()) return true;
            while (s < str.size() && !WildcardMatchChar(str[s], pattern[p])) {
                ++s;
            }
        } else if (WildcardMatchChar(str[s], pattern[p])) {
            ++s; ++p;
        } else {
            return false;
        }
    }

    while (p < pattern.size() && pattern[p] == '*') ++p;
    return s == str.size() && p == pattern.size();
}

bool CChannel::WildcardMatchChar(char c, char p) {
    return (p == '?' || p == c);
}

void CChannel::Broadcast(const std::string& message, CPeerPtr exclude) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string sender;
    if (exclude) {
        sender = exclude->GetPrefix();
    }

    for (const auto& [nick, peer] : members_) {
        (void)nick;
        if (exclude && peer == exclude) continue;
        // In real implementation, would send message to each member
        (void)peer;
    }

    total_messages_.fetch_add(1);
}

void CChannel::BroadcastToOps(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& nick : operators_) {
        auto it = members_.find(nick);
        if (it != members_.end()) {
            // In real implementation, would send to operator
            (void)it->second;
        }
    }
}

void CChannel::BroadcastWithPrefix(const std::string& message, const std::string& prefix) {
    Broadcast(message, nullptr);
}

void CChannel::SetEventCallback(ChannelEventCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = std::move(callback);
}

void CChannel::FireEvent(const ChannelEvent& event) {
    ChannelEventCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = event_callback_;
    }
    
    if (callback) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            LOG_ERROR("[CChannel] Exception in event_callback_: {}", e.what());
        } catch (...) {
            LOG_ERROR("[CChannel] Unknown exception in event_callback_");
        }
    }
}

void CChannel::FireEventUnsafe(const ChannelEvent& event) {
    // NOTE: Caller must hold mutex_ - do NOT lock here to avoid deadlock
    if (event_callback_) {
        try {
            event_callback_(event);
        } catch (const std::exception& e) {
            LOG_ERROR("[CChannel] Exception in event_callback_: {}", e.what());
        } catch (...) {
            LOG_ERROR("[CChannel] Unknown exception in event_callback_");
        }
    }
}

std::shared_ptr<CTopic> CChannel::CreateTopic(const std::string& title,
                                               const std::string& creator_nick,
                                               const std::string& initial_content) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto topic = CTopic::Create(title, creator_nick, initial_content);
    topics_[topic->GetId()] = topic;
    return topic;
}

std::shared_ptr<CTopic> CChannel::GetTopic(const std::string& topic_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = topics_.find(topic_id);
    if (it != topics_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<CTopic>> CChannel::ListTopics(const TopicFilter& filter) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<CTopic>> result;
    for (const auto& [id, topic] : topics_) {
        (void)id;
        // Apply filters
        if (filter.status && topic->GetStatus() != *filter.status) {
            continue;
        }
        if (filter.include_archived == false &&
            topic->GetStatus() == TopicStatus::Archived) {
            continue;
        }
        if (filter.search_text) {
            // Search in title
            if (topic->GetTitle().find(*filter.search_text) == std::string::npos) {
                continue;
            }
        }
        result.push_back(topic);
    }

    // Sort: pinned first, then by date
    std::sort(result.begin(), result.end(),
        [](const CTopicPtr& a, const CTopicPtr& b) {
            if (a->GetStatus() == TopicStatus::Pinned && b->GetStatus() != TopicStatus::Pinned) {
                return true;
            }
            if (b->GetStatus() == TopicStatus::Pinned && a->GetStatus() != TopicStatus::Pinned) {
                return false;
            }
            return a->GetCreatedAt() > b->GetCreatedAt();
        });

    return result;
}

bool CChannel::CloseTopic(const std::string& topic_id, const std::string& operator_nick) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!CanOperate(operator_nick)) {
        return false;
    }

    auto it = topics_.find(topic_id);
    if (it == topics_.end()) {
        return false;
    }

    it->second->SetStatus(TopicStatus::Closed);
    return true;
}

bool CChannel::DeleteTopic(const std::string& topic_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return topics_.erase(topic_id) > 0;
}

} // namespace blazeclaw::irc
