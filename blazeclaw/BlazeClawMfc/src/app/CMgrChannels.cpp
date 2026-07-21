#include "pch.h"
#include "CMgrChannels.h"
#include "ChatMessage.h"

#include <algorithm>
#include <chrono>

namespace blazeclaw::irc {

std::shared_ptr<CChannel> CMgrChannels::CreateChannel(const std::string& name,
                                                      CPeerPtr founder,
                                                      const std::string& key) {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);

    auto it = channels_.find(name);
    if (it != channels_.end()) {
        return it->second;  // Already exists, return existing
    }

    auto channel = CChannel::Create(name, founder);
    if (!key.empty()) {
        channel->SetKey(key);
    }

    channels_[name] = channel;
    ++channels_created_;

    return channel;
}

bool CMgrChannels::DestroyChannel(const std::string& name) {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);

    auto it = channels_.find(name);
    if (it == channels_.end()) {
        return false;
    }

    channels_.erase(it);
    ++channels_destroyed_;
    return true;
}

std::shared_ptr<CChannel> CMgrChannels::GetChannel(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    auto it = channels_.find(name);
    if (it != channels_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> CMgrChannels::GetChannelList() const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    std::vector<std::string> result;
    result.reserve(channels_.size());
    for (const auto& [name, channel] : channels_) {
        (void)channel;
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

size_t CMgrChannels::GetChannelCount() const noexcept {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    return channels_.size();
}

void CMgrChannels::RegisterMember(CPeerPtr peer) {
    if (!peer) return;

    MemberEvent event;
    {
        std::unique_lock<std::shared_mutex> lock(members_mutex_);

        const auto& nick = peer->GetNickname();
        members_[nick] = peer;

        event.type = MemberEventType::Registered;
        event.nick = nick;
        event.timestamp_ms = GetCurrentTimestampMs();
    }
    FireMemberEvent(event);
}

void CMgrChannels::UnregisterMember(const std::string& nick) {
    // First part from all channels (needs channels_mutex_)
    {
        std::shared_lock<std::shared_mutex> ch_lock(channels_mutex_);
        for (const auto& [ch_name, channel] : channels_) {
            (void)ch_name;
            if (channel->HasMember(nick)) {
                channel->RemoveMember(nick);
            }
        }
    }

    // Then remove from members list
    MemberEvent event;
    {
        std::unique_lock<std::shared_mutex> lock(members_mutex_);

        auto it = members_.find(nick);
        if (it == members_.end()) {
            return;
        }

        members_.erase(it);

        event.type = MemberEventType::Unregistered;
        event.nick = nick;
        event.timestamp_ms = GetCurrentTimestampMs();
    }
    FireMemberEvent(event);
}

std::shared_ptr<CPeer> CMgrChannels::GetMember(const std::string& nick) const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);

    auto it = members_.find(nick);
    if (it != members_.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::string> CMgrChannels::GetRegisteredMemberNicks() const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);

    std::vector<std::string> result;
    result.reserve(members_.size());
    for (const auto& [nick, peer] : members_) {
        (void)peer;
        result.push_back(nick);
    }
    return result;
}

bool CMgrChannels::IsMemberRegistered(const std::string& nick) const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);
    return members_.contains(nick);
}

JoinResult CMgrChannels::Join(CPeerPtr peer, const std::string& channel,
                               const std::string& key) {
    JoinResult result;
    result.success = false;

    if (!peer) {
        result.error_message = "Invalid peer";
        return result;
    }

    const auto& nick = peer->GetNickname();

    // Get or create channel
    std::shared_ptr<CChannel> ch;
    {
        std::shared_lock<std::shared_mutex> lock(channels_mutex_);
        auto it = channels_.find(channel);
        if (it != channels_.end()) {
            ch = it->second;
        }
    }

    if (!ch) {
        // Auto-create channel
        std::unique_lock<std::shared_mutex> lock(channels_mutex_);
        // Double-check after acquiring write lock
        auto it = channels_.find(channel);
        if (it != channels_.end()) {
            ch = it->second;
        } else {
            ch = CChannel::Create(channel, peer);
            if (!key.empty()) {
                ch->SetKey(key);
            }
            channels_[channel] = ch;
            ++channels_created_;
        }
    }

    // Check if can join
    if (!ch->CanJoin(nick, key)) {
        // Check specific reasons
        if (ch->HasMode(ChannelMode::InviteOnly)) {
            result.error_message = "Channel is invite-only";
        } else if (ch->HasMode(ChannelMode::KeyLock)) {
            result.error_message = "Channel requires a key";
        } else if (ch->HasMode(ChannelMode::UserLimit)) {
            result.error_message = "Channel is full";
        } else {
            result.error_message = "You are banned from this channel";
        }
        return result;
    }

    // Add member
    if (!ch->AddMember(peer)) {
        result.error_message = "Already in channel";
        return result;
    }

    ++joins_;

    MemberEvent event;
    event.type = MemberEventType::JoinedChannel;
    event.nick = nick;
    event.channel_name = channel;
    event.timestamp_ms = GetCurrentTimestampMs();
    FireMemberEvent(std::move(event));
    
    result.success = true;
    result.channel = ch;
    return result;
}

PartResult CMgrChannels::Part(CPeerPtr peer, const std::string& channel,
                                const std::string& reason) {
    PartResult result;
    result.success = false;

    if (!peer) {
        result.error_message = "Invalid peer";
        return result;
    }

    const auto& nick = peer->GetNickname();

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        result.error_message = "Channel not found";
        return result;
    }

    auto ch = it->second;
    if (!ch->HasMember(nick)) {
        result.error_message = "Not in channel";
        return result;
    }

    // Check if operator and trigger handover
    if (ch->IsOperator(nick)) {
        ch->OnOperatorLeft(nick);
    }

    ch->RemoveMember(nick);
    ++parts_;

    MemberEvent event;
    event.type = MemberEventType::PartedChannel;
    event.nick = nick;
    event.channel_name = channel;
    event.timestamp_ms = GetCurrentTimestampMs();
    FireMemberEvent(event);

    result.success = true;
    return result;
}

KickResult CMgrChannels::Kick(const std::string& op_nick,
                                const std::string& channel,
                                const std::string& target,
                                const std::string& reason) {
    KickResult result;
    result.success = false;

    if (!CanOperate(op_nick, channel)) {
        result.error_message = "You are not an operator";
        return result;
    }

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        result.error_message = "Channel not found";
        return result;
    }

    auto ch = it->second;
    if (!ch->HasMember(target)) {
        result.error_message = "Target not in channel";
        return result;
    }

    // Check if trying to kick an operator (requires global op or higher)
    if (ch->IsOperator(target) && op_nick != target) {
        // Need higher privilege - for now, allow channel ops to kick other ops
        // In real IRC, this would be restricted
    }

    // Trigger operator handover if kicking an op
    if (ch->IsOperator(target)) {
        ch->OnOperatorLeft(target);
    }

    ch->RemoveMember(target);
    ++kicks_;

    // Fire channel event
    ChannelEvent evt;
    evt.type = ChannelEventType::MemberKicked;
    evt.channel_name = channel;
    evt.actor_nick = op_nick;
    evt.target_nick = target;
    evt.data = reason;
    evt.timestamp_ms = GetCurrentTimestampMs();
    FireChannelEvent(evt);

    result.success = true;
    return result;
}

TopicResult CMgrChannels::SetTopic(const std::string& nick,
                                    const std::string& channel,
                                    const std::string& topic) {
    TopicResult result;
    result.success = false;

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        result.error_message = "Channel not found";
        return result;
    }

    auto ch = it->second;
    if (!ch->CanModifyTopic(nick)) {
        result.error_message = "You cannot change the topic";
        return result;
    }

    ch->SetTopic(topic);
    result.success = true;
    result.topic = topic;

    // Fire channel event
    ChannelEvent evt;
    evt.type = ChannelEventType::TopicChanged;
    evt.channel_name = channel;
    evt.actor_nick = nick;
    evt.data = topic;
    evt.timestamp_ms = GetCurrentTimestampMs();
    FireChannelEvent(evt);

    return result;
}

ModeResult CMgrChannels::SetChannelMode(const std::string& op_nick,
                                          const std::string& channel,
                                          const std::string& mode_str) {
    ModeResult result;
    result.success = false;

    if (!CanOperate(op_nick, channel)) {
        result.error_message = "You are not an operator";
        return result;
    }

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        result.error_message = "Channel not found";
        return result;
    }

    auto ch = it->second;

    // Parse and apply mode string
    // Format: +/-<mode flags>
    // +i, -i, +m, -m, +n, -n, +p, -p, +s, -s, +t, -t, +k <key>, +l <limit>
    // +o <nick>, -o <nick>, +v <nick>, -v <nick>, +b <mask>, -b <mask>

    bool add = true;
    for (size_t i = 0; i < mode_str.size(); ++i) {
        char c = mode_str[i];

        if (c == '+') {
            add = true;
            continue;
        } else if (c == '-') {
            add = false;
            continue;
        }

        switch (c) {
        case 'i':
            ch->SetMode(ChannelMode::InviteOnly, add);
            result.success = true;
            break;
        case 'm':
            ch->SetMode(ChannelMode::Moderate, add);
            result.success = true;
            break;
        case 'n':
            ch->SetMode(ChannelMode::NoOutside, add);
            result.success = true;
            break;
        case 'p':
            ch->SetMode(ChannelMode::Private, add);
            result.success = true;
            break;
        case 's':
            ch->SetMode(ChannelMode::Secret, add);
            result.success = true;
            break;
        case 't':
            ch->SetMode(ChannelMode::TopicLock, add);
            result.success = true;
            break;
        case 'o':
            // Operator mode: +o/-o <nick>
            if (i + 1 < mode_str.size() && mode_str[i + 1] == ' ') {
                // Skip to nick
                ++i;
                while (i + 1 < mode_str.size() && mode_str[i + 1] == ' ') ++i;
                size_t start = ++i;
                while (i < mode_str.size() && mode_str[i] != ' ') ++i;
                std::string target_nick = mode_str.substr(start, i - start);
                if (add) {
                    ch->GrantOperator(target_nick);
                } else {
                    ch->RevokeOperator(target_nick);
                }
                result.success = true;
            }
            break;
        case 'v':
            // Voice mode: +v/-v <nick>
            if (i + 1 < mode_str.size() && mode_str[i + 1] == ' ') {
                ++i;
                while (i + 1 < mode_str.size() && mode_str[i + 1] == ' ') ++i;
                size_t start = ++i;
                while (i < mode_str.size() && mode_str[i] != ' ') ++i;
                std::string target_nick = mode_str.substr(start, i - start);
                // Voice management would go here
                result.success = true;
            }
            break;
        case 'k':
            // Key mode: +k <key>
            if (add && i + 1 < mode_str.size() && mode_str[i + 1] == ' ') {
                ++i;
                while (i + 1 < mode_str.size() && mode_str[i + 1] == ' ') ++i;
                size_t start = ++i;
                while (i < mode_str.size() && mode_str[i] != ' ') ++i;
                std::string key = mode_str.substr(start, i - start);
                ch->SetKey(key);
                result.success = true;
            } else if (!add) {
                ch->SetKey("");
                result.success = true;
            }
            break;
        case 'l':
            // Limit mode: +l <limit>
            if (add && i + 1 < mode_str.size() && mode_str[i + 1] == ' ') {
                ++i;
                while (i + 1 < mode_str.size() && mode_str[i + 1] == ' ') ++i;
                size_t start = ++i;
                while (i < mode_str.size() && mode_str[i] != ' ') ++i;
                std::string limit_str = mode_str.substr(start, i - start);
                try {
                    uint32_t limit = std::stoul(limit_str);
                    ch->SetUserLimit(limit);
                    result.success = true;
                } catch (...) {
                    result.error_message = "Invalid limit";
                }
            } else if (!add) {
                ch->SetUserLimit(0);
                result.success = true;
            }
            break;
        case 'b':
            // Ban mode: +b/-b <mask>
            if (i + 1 < mode_str.size() && mode_str[i + 1] == ' ') {
                ++i;
                while (i + 1 < mode_str.size() && mode_str[i + 1] == ' ') ++i;
                size_t start = ++i;
                while (i < mode_str.size() && mode_str[i] != ' ') ++i;
                std::string mask = mode_str.substr(start, i - start);
                if (add) {
                    ch->AddBan(mask, op_nick);
                    ++bans_;
                } else {
                    ch->RemoveBan(mask);
                }
                result.success = true;
            }
            break;
        }
    }

    // Fire channel event
    ChannelEvent evt;
    evt.type = ChannelEventType::ModeChanged;
    evt.channel_name = channel;
    evt.actor_nick = op_nick;
    evt.data = mode_str;
    evt.timestamp_ms = GetCurrentTimestampMs();
    FireChannelEvent(evt);

    return result;
}

BanResult CMgrChannels::Ban(const std::string& op_nick,
                             const std::string& channel,
                             const std::string& mask) {
    BanResult result;
    result.success = false;

    if (!CanOperate(op_nick, channel)) {
        result.error_message = "You are not an operator";
        return result;
    }

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        result.error_message = "Channel not found";
        return result;
    }

    it->second->AddBan(mask, op_nick);
    ++bans_;
    result.success = true;
    return result;
}

BanResult CMgrChannels::Unban(const std::string& op_nick,
                               const std::string& channel,
                               const std::string& mask) {
    BanResult result;
    result.success = false;

    if (!CanOperate(op_nick, channel)) {
        result.error_message = "You are not an operator";
        return result;
    }

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        result.error_message = "Channel not found";
        return result;
    }

    result.success = it->second->RemoveBan(mask);
    if (!result.success) {
        result.error_message = "Ban not found";
    }
    return result;
}

bool CMgrChannels::Invite(const std::string& op_nick,
                          const std::string& channel,
                          const std::string& target_nick) {
    if (!CanOperate(op_nick, channel)) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return false;
    }

    it->second->AddInvite(target_nick, op_nick);
    return true;
}

bool CMgrChannels::PromoteToOperator(const std::string& oper_nick,
                                      const std::string& channel,
                                      const std::string& target_nick) {
    if (!CanOperate(oper_nick, channel)) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return false;
    }

    bool success = it->second->GrantOperator(target_nick);

    if (success) {
        ChannelEvent evt;
        evt.type = ChannelEventType::OperatorGranted;
        evt.channel_name = channel;
        evt.actor_nick = oper_nick;
        evt.target_nick = target_nick;
        evt.timestamp_ms = GetCurrentTimestampMs();
        FireChannelEvent(evt);
    }

    return success;
}

bool CMgrChannels::DemoteOperator(const std::string& oper_nick,
                                   const std::string& channel,
                                   const std::string& target_nick) {
    if (!CanOperate(oper_nick, channel)) {
        return false;
    }

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return false;
    }

    bool success = it->second->RevokeOperator(target_nick);

    if (success) {
        ChannelEvent evt;
        evt.type = ChannelEventType::OperatorRevoked;
        evt.channel_name = channel;
        evt.actor_nick = oper_nick;
        evt.target_nick = target_nick;
        evt.timestamp_ms = GetCurrentTimestampMs();
        FireChannelEvent(evt);

        // Check for operator handover
        it->second->OnOperatorLeft(target_nick);
    }

    return success;
}

std::optional<WhoisInfo> CMgrChannels::Whois(const std::string& nick) const {
    std::shared_lock<std::shared_mutex> lock(members_mutex_);

    auto member_it = members_.find(nick);
    if (member_it == members_.end()) {
        return std::nullopt;
    }

    auto peer = member_it->second;
    WhoisInfo info;
    info.nickname = peer->GetNickname();
    info.username = peer->GetUsername();
    info.hostname = peer->GetHostname();
    info.realname = peer->GetRealname();
    info.is_operator = peer->IsOperator();
    info.is_away = peer->IsAway();

    // Find channels
    std::shared_lock<std::shared_mutex> ch_lock(channels_mutex_);
    for (const auto& [ch_name, channel] : channels_) {
        (void)ch_name;
        if (channel->HasMember(nick)) {
            info.channels.push_back(channel->GetName());
        }
    }

    return info;
}

std::vector<std::string> CMgrChannels::GetNamesList(const std::string& channel) const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    auto it = channels_.find(channel);
    if (it == channels_.end()) {
        return {};
    }

    return it->second->GetMemberNicks();
}

bool CMgrChannels::CanOperate(const std::string& nick, const std::string& channel) const {
    // Check global operator first
    {
        std::shared_lock<std::shared_mutex> lock(members_mutex_);
        auto member_it = members_.find(nick);
        if (member_it != members_.end()) {
            if (member_it->second->IsOperator()) {
                return true;
            }
        }
    }

    // Check channel operator
    std::shared_lock<std::shared_mutex> ch_lock(channels_mutex_);
    auto it = channels_.find(channel);
    if (it != channels_.end()) {
        return it->second->IsOperator(nick);
    }

    return false;
}

void CMgrChannels::SetChannelEventCallback(ChannelEventCallback callback) {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);
    channel_event_callback_ = std::move(callback);
}

void CMgrChannels::SetMemberEventCallback(MemberEventCallback callback) {
    std::unique_lock<std::shared_mutex> lock(members_mutex_);
    member_event_callback_ = std::move(callback);
}

void CMgrChannels::AddMessage(const StoredMessage& msg) {
    std::lock_guard<std::mutex> lock(messages_mutex_);
    auto& msgs = messages_[msg.channel];
    msgs.push_back(msg);
    // Limit stored messages per channel
    if (msgs.size() > kMaxMessagesPerChannel) {
        msgs.erase(msgs.begin(), msgs.begin() + (msgs.size() - kMaxMessagesPerChannel));
    }
}

std::vector<StoredMessage> CMgrChannels::GetMessages(const std::string& channel, int limit) {
    std::lock_guard<std::mutex> lock(messages_mutex_);
    auto it = messages_.find(channel);
    if (it == messages_.end()) {
        return {};
    }
    const auto& msgs = it->second;
    if (msgs.size() <= static_cast<size_t>(limit)) {
        return msgs;
    }
    // Return last 'limit' messages
    return std::vector<StoredMessage>(msgs.end() - limit, msgs.end());
}

Diagnostics CMgrChannels::GetDiagnostics() const {
    Diagnostics diag;

    std::shared_lock<std::shared_mutex> ch_lock(channels_mutex_);
    std::shared_lock<std::shared_mutex> mem_lock(members_mutex_);

    diag.channels_created = channels_created_.load();
    diag.channels_destroyed = channels_destroyed_.load();
    diag.joins = joins_.load();
    diag.parts = parts_.load();
    diag.kicks = kicks_.load();
    diag.bans = bans_.load();
    diag.total_channels = channels_.size();
    diag.total_members = members_.size();

    return diag;
}

void CMgrChannels::ResetDiagnostics() {
    channels_created_ = 0;
    channels_destroyed_ = 0;
    joins_ = 0;
    parts_ = 0;
    kicks_ = 0;
    bans_ = 0;
}

void CMgrChannels::ForwardChannelEvent(const ChannelEvent& event) {
    FireChannelEvent(event);
}

void CMgrChannels::FireChannelEvent(const ChannelEvent& event) {
    ChannelEventCallback callback;
    {
        std::shared_lock<std::shared_mutex> lock(channels_mutex_);
        callback = channel_event_callback_;  // Copy callback to avoid holding lock during callback
    }
    
    if (callback) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            LOG_ERROR("[CMgrChannels] Exception in channel_event_callback_: {}", e.what());
        } catch (...) {
            LOG_ERROR("[CMgrChannels] Unknown exception in channel_event_callback_");
        }
    }
}

void CMgrChannels::FireMemberEvent(const MemberEvent& event) {
    FireMemberEvent(MemberEvent(event));
}

void CMgrChannels::FireMemberEvent(MemberEvent&& event) {
    LOG_DEBUG("[CMgrChannels::FireMemberEvent] type={} nick={}", 
              static_cast<int>(event.type), event.nick);
    
    MemberEventCallback callback;
    {
        std::shared_lock<std::shared_mutex> lock(members_mutex_);
        callback = member_event_callback_;
    }
    
    if (callback) {
        try {
            LOG_DEBUG("[CMgrChannels::FireMemberEvent] invoking callback...");
            callback(event);
            LOG_DEBUG("[CMgrChannels::FireMemberEvent] callback completed");
        } catch (const std::exception& e) {
            LOG_ERROR("[CMgrChannels] Exception in member_event_callback_: {}", e.what());
        } catch (...) {
            LOG_ERROR("[CMgrChannels] Unknown exception in member_event_callback_");
        }
    } else {
        LOG_DEBUG("[CMgrChannels::FireMemberEvent] no callback set");
    }
}

} // namespace blazeclaw::irc
