#include "pch.h"
#include "CTopic.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <sstream>
#include <iomanip>
#include <unordered_set>

namespace blazeclaw::irc {

namespace {

uint64_t GetTimestampMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

std::atomic<uint64_t> g_reply_counter{ 0 };

} // anonymous namespace

// static
std::shared_ptr<CTopic> CTopic::Create(const std::string& title,
                                        const std::string& creator_nick,
                                        const std::string& initial_content) {
    auto topic = std::shared_ptr<CTopic>(new CTopic(title, creator_nick, initial_content));
    // Add initial post if content is provided
    if (!initial_content.empty()) {
        topic->AddReply(creator_nick, initial_content, std::nullopt, false);
    }
    return topic;
}

CTopic::CTopic(const std::string& title,
               const std::string& creator_nick,
               const std::string& /*initial_content*/)
    : topic_id_("T-" + GenerateReplyId())
    , title_(title)
    , creator_nick_(creator_nick)
    , created_at_ms_(GetTimestampMs())
    , status_(TopicStatus::Open)
    , ai_enabled_(false) {
}

std::string CTopic::GenerateReplyId() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    uint64_t counter = g_reply_counter.fetch_add(1);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << ms
        << "-" << std::setw(8) << counter;
    return oss.str();
}

uint64_t CTopic::GetCurrentTimestampMs() {
    return GetTimestampMs();
}

uint64_t CTopic::GetNextCounter() {
    return g_reply_counter.fetch_add(1);
}

std::string CTopic::AddReply(const std::string& author_nick,
                              const std::string& content,
                              const std::optional<std::string>& parent_reply_id,
                              bool is_ai_generated) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Validate parent exists if specified
    if (parent_reply_id && !replies_.contains(*parent_reply_id)) {
        return "";  // Invalid parent
    }

    // Check if topic is closed (only ops can reply to closed topics)
    if (status_ == TopicStatus::Closed && !is_ai_generated) {
        // In real implementation, would check if author is operator
    }

    std::string reply_id = GenerateReplyId();
    TopicReply reply;
    reply.reply_id = reply_id;
    reply.parent_reply_id = parent_reply_id;
    reply.author_nick = author_nick;
    reply.content = content;
    reply.timestamp_ms = GetTimestampMs();
    reply.is_ai_generated = is_ai_generated;

    replies_[reply_id] = std::move(reply);

    // Update children map
    if (parent_reply_id) {
        children_[*parent_reply_id].push_back(reply_id);
    }
    reply_parent_[reply_id] = parent_reply_id.value_or("");

    return reply_id;
}

bool CTopic::RemoveReply(const std::string& reply_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = replies_.find(reply_id);
    if (it == replies_.end()) {
        return false;
    }

    // Remove from parent's children list
    auto parent_it = reply_parent_.find(reply_id);
    if (parent_it != reply_parent_.end() && !parent_it->second.empty()) {
        auto& siblings = children_[parent_it->second];
        siblings.erase(
            std::remove(siblings.begin(), siblings.end(), reply_id),
            siblings.end());
    }

    // Remove descendants recursively
    std::vector<std::string> to_remove;
    std::function<void(const std::string&)> collect_descendants =
        [this, &to_remove, &collect_descendants](const std::string& id) {
            auto child_it = children_.find(id);
            if (child_it != children_.end()) {
                for (const auto& child_id : child_it->second) {
                    to_remove.push_back(child_id);
                    collect_descendants(child_id);
                }
            }
        };
    collect_descendants(reply_id);

    for (const auto& id : to_remove) {
        replies_.erase(id);
        reply_parent_.erase(id);
        children_.erase(id);
    }

    // Remove the reply itself
    replies_.erase(reply_id);
    reply_parent_.erase(reply_id);
    children_.erase(reply_id);

    return true;
}

std::optional<TopicReply> CTopic::GetReply(const std::string& reply_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = replies_.find(reply_id);
    if (it != replies_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<TopicReply> CTopic::GetReplies(
    const std::optional<std::string>& parent_reply_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TopicReply> result;

    if (parent_reply_id) {
        auto it = children_.find(*parent_reply_id);
        if (it != children_.end()) {
            for (const auto& child_id : it->second) {
                auto reply_it = replies_.find(child_id);
                if (reply_it != replies_.end()) {
                    result.push_back(reply_it->second);
                }
            }
        }
    } else {
        // Top-level replies (no parent)
        for (const auto& [reply_id, parent_id] : reply_parent_) {
            (void)reply_id;
            if (parent_id.empty()) {
                auto reply_it = replies_.find(reply_id);
                if (reply_it != replies_.end()) {
                    result.push_back(reply_it->second);
                }
            }
        }
    }

    // Sort by timestamp
    std::sort(result.begin(), result.end(),
        [](const TopicReply& a, const TopicReply& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

    return result;
}

std::vector<TopicReply> CTopic::GetAllReplies() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TopicReply> result;
    result.reserve(replies_.size());

    for (const auto& [id, reply] : replies_) {
        (void)id;
        result.push_back(reply);
    }

    std::sort(result.begin(), result.end(),
        [](const TopicReply& a, const TopicReply& b) {
            return a.timestamp_ms < b.timestamp_ms;
        });

    return result;
}

std::vector<TopicReply> CTopic::GetReplyChain(const std::string& reply_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TopicReply> chain;

    std::optional<std::string> current_id = reply_id;
    while (current_id) {
        auto it = replies_.find(*current_id);
        if (it == replies_.end()) {
            break;
        }
        chain.push_back(it->second);

        auto parent_it = reply_parent_.find(*current_id);
        if (parent_it != reply_parent_.end() && !parent_it->second.empty()) {
            current_id = parent_it->second;
        } else {
            current_id = std::nullopt;
        }
    }

    // Reverse to get from root to leaf
    std::reverse(chain.begin(), chain.end());

    return chain;
}

size_t CTopic::GetDescendantCount(const std::string& reply_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = 0;
    std::function<void(const std::string&)> count_descendants =
        [this, &count, &count_descendants](const std::string& id) {
            auto it = children_.find(id);
            if (it != children_.end()) {
                for (const auto& child_id : it->second) {
                    ++count;
                    count_descendants(child_id);
                }
            }
        };

    count_descendants(reply_id);
    return count;
}

size_t CTopic::GetParticipantCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_set<std::string> participants;
    for (const auto& [id, reply] : replies_) {
        (void)id;
        participants.insert(reply.author_nick);
    }
    return participants.size();
}

std::vector<std::string> CTopic::GetParticipantNicks() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_set<std::string> participants;
    for (const auto& [id, reply] : replies_) {
        (void)id;
        participants.insert(reply.author_nick);
    }

    return std::vector<std::string>(participants.begin(), participants.end());
}

} // namespace blazeclaw::irc
