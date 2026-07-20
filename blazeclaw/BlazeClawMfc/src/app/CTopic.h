#pragma once

#include <atomic>
#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace blazeclaw::irc {

// Forward declaration
class CTopic;

// Topic status (Newsgroup-style)
enum class TopicStatus : uint32_t {
    Open     = 0,  // Active discussion, everyone can reply
    Closed   = 1,  // Closed, only ops can reopen
    Pinned   = 2,  // Pinned at top
    Archived = 3,  // Read-only archive
};

constexpr TopicStatus operator|(TopicStatus a, TopicStatus b) {
    return static_cast<TopicStatus>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

// Reply in a topic
struct TopicReply {
    std::string reply_id;         // UUID
    std::optional<std::string> parent_reply_id;  // Empty = direct reply to topic
    std::string author_nick;      // Author's nickname
    std::string content;           // Reply content
    uint64_t timestamp_ms;        // Creation timestamp
    bool is_ai_generated;         // AI auto-generated flag
    std::vector<std::string> attachments;  // File attachments

    TopicReply()
        : timestamp_ms(0)
        , is_ai_generated(false) {}
};

// CTopic - Newsgroup-style discussion thread
class CTopic {
public:
    CTopic() = delete;
    CTopic(const CTopic&) = delete;
    CTopic& operator=(const CTopic&) = delete;
    CTopic(CTopic&&) = delete;
    CTopic& operator=(CTopic&&) = delete;
    ~CTopic() = default;

    // Factory
    static std::shared_ptr<CTopic> Create(const std::string& title,
                                           const std::string& creator_nick,
                                           const std::string& initial_content = "");

    // Properties
    const std::string& GetId() const noexcept { return topic_id_; }
    const std::string& GetTitle() const noexcept { return title_; }
    const std::string& GetCreatorNick() const noexcept { return creator_nick_; }
    uint64_t GetCreatedAt() const noexcept { return created_at_ms_; }
    TopicStatus GetStatus() const noexcept { return status_; }

    void SetStatus(TopicStatus status) noexcept { status_ = status; }
    void SetTitle(const std::string& title) { title_ = title; }

    // AI participation
    bool IsAiParticipationEnabled() const noexcept { return ai_enabled_; }
    void EnableAiParticipation(bool enable) noexcept { ai_enabled_ = enable; }

    const std::string& GetAiSystemPrompt() const noexcept { return ai_system_prompt_; }
    void SetAiSystemPrompt(const std::string& prompt) { ai_system_prompt_ = prompt; }

    // Reply management
    std::string AddReply(const std::string& author_nick,
                         const std::string& content,
                         const std::optional<std::string>& parent_reply_id = std::nullopt,
                         bool is_ai_generated = false);

    bool RemoveReply(const std::string& reply_id);
    std::optional<TopicReply> GetReply(const std::string& reply_id) const;
    std::vector<TopicReply> GetReplies(const std::optional<std::string>& parent_reply_id = std::nullopt) const;
    std::vector<TopicReply> GetAllReplies() const;
    size_t GetReplyCount() const noexcept { return replies_.size(); }

    // Thread tree helpers
    std::vector<TopicReply> GetReplyChain(const std::string& reply_id) const;
    size_t GetDescendantCount(const std::string& reply_id) const;

    // Statistics
    size_t GetParticipantCount() const;
    std::vector<std::string> GetParticipantNicks() const;

private:
    explicit CTopic(const std::string& title,
                    const std::string& creator_nick,
                    const std::string& initial_content);

    static std::string GenerateReplyId();
    static uint64_t GetCurrentTimestampMs();
    static uint64_t GetNextCounter();

    std::string topic_id_;
    std::string title_;
    std::string creator_nick_;
    uint64_t created_at_ms_;
    TopicStatus status_;

    bool ai_enabled_;
    std::string ai_system_prompt_;

    std::unordered_map<std::string, TopicReply> replies_;  // reply_id -> reply
    std::unordered_map<std::string, std::vector<std::string>> children_;  // parent_id -> child_ids
    std::unordered_map<std::string, std::string> reply_parent_;  // reply_id -> parent_id

    mutable std::mutex mutex_;
};

// Shared pointer type alias
using CTopicPtr = std::shared_ptr<CTopic>;

// Filter options for listing topics
struct TopicFilter {
    std::optional<TopicStatus> status;
    std::optional<std::string> author_nick;
    std::optional<std::string> search_text;  // Search in title/content
    bool include_archived = false;
};

} // namespace blazeclaw::irc
