#pragma once

#include <cstdint>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::irc {

// Inline timestamp helper
inline uint64_t GetCurrentTimestampMs() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());
}

// Message types for chat system
enum class ChatMessageType : uint32_t {
    Privmsg,      // Regular private message
    Notice,       // Notice (no auto-response)
    Join,         // Channel join
    Part,         // Channel part
    Quit,         // User quit
    Kick,         // User kicked
    Ban,          // User banned
    Invite,       // User invited
    Mode,         // Mode change
    Topic,        // Topic change
    Oper,         // Operator command
    Whois,        // Whois query
    Who,          // Who query
    Names,        // Names list
    List,         // Channel list
    AiPrompt,     // AI prompt/response
    Auth,         // Authentication
    Token,        // Token refresh
};

// Message content structure
struct ChatMessageContent {
    std::string text;
    std::vector<std::string> attachments;
    std::optional<std::string> reply_to_message_id;
    std::optional<std::string> topic_id;
};

// Base message class
class ChatMessage {
public:
    ChatMessage() = default;
    virtual ~ChatMessage() = default;

    // Message metadata
    void SetId(const std::string& id) { id_ = id; }
    const std::string& GetId() const { return id_; }

    void SetType(ChatMessageType type) { type_ = type; }
    ChatMessageType GetType() const { return type_; }

    void SetTimestamp(uint64_t ts) { timestamp_ms_ = ts; }
    uint64_t GetTimestamp() const { return timestamp_ms_; }

    // Channel/target info
    void SetChannel(const std::string& channel) { channel_ = channel; }
    const std::string& GetChannel() const { return channel_; }

    void SetTargetNick(const std::string& nick) { target_nick_ = nick; }
    const std::string& GetTargetNick() const { return target_nick_; }

    void SetSenderNick(const std::string& nick) { sender_nick_ = nick; }
    const std::string& GetSenderNick() const { return sender_nick_; }

    // Content
    void SetContent(const ChatMessageContent& content) { content_ = content; }
    const ChatMessageContent& GetContent() const { return content_; }
    ChatMessageContent& GetContent() { return content_; }

    // Serialization
    virtual std::string ToJson() const;
    virtual bool FromJson(const std::string& json);

protected:
    std::string id_;
    ChatMessageType type_ = ChatMessageType::Privmsg;
    uint64_t timestamp_ms_ = 0;

    std::string channel_;      // Target channel (e.g., #general)
    std::string sender_nick_;  // Sender's nickname
    std::string target_nick_; // Target nickname (for privmsg, kick, etc.)

    ChatMessageContent content_;
};

// CMessage - TLS encrypted message (for sensitive operations)
class CMessage : public ChatMessage {
public:
    CMessage() {
        SetType(ChatMessageType::Privmsg);
    }

    // Whether this message requires TLS encryption
    static bool UseTls() { return true; }

    // Check if this is an operator/sensitive command
    bool IsSensitive() const;
};

// CPrompt - TCP plaintext message (for AI/control commands)
class CPrompt : public ChatMessage {
public:
    CPrompt() {
        SetType(ChatMessageType::Privmsg);
    }

    // Whether this message uses plaintext TCP
    static bool UseTls() { return false; }

    // AI-specific fields
    void SetAiContext(const std::string& context) { ai_context_ = context; }
    const std::string& GetAiContext() const { return ai_context_; }

    void SetSystemPrompt(const std::string& prompt) { system_prompt_ = prompt; }
    const std::string& GetSystemPrompt() const { return system_prompt_; }

    void SetTemperature(float temp) { temperature_ = temp; }
    float GetTemperature() const { return temperature_; }

    void SetMaxTokens(int tokens) { max_tokens_ = tokens; }
    int GetMaxTokens() const { return max_tokens_; }

    // Serialization
    std::string ToJson() const override;
    bool FromJson(const std::string& json) override;

private:
    std::string ai_context_;
    std::string system_prompt_;
    float temperature_ = 0.7f;
    int max_tokens_ = 2048;
};

// Shared pointer types
using ChatMessagePtr = std::shared_ptr<ChatMessage>;
using CMessagePtr = std::shared_ptr<CMessage>;
using CPromptPtr = std::shared_ptr<CPrompt>;

// Message builder helpers
CMessagePtr CreateMessage(const std::string& channel,
                          const std::string& sender,
                          const std::string& text);

CPromptPtr CreatePrompt(const std::string& channel,
                        const std::string& sender,
                        const std::string& text);

CPromptPtr CreateAiPrompt(const std::string& channel,
                          const std::string& sender,
                          const std::string& text,
                          const std::string& system_prompt = "");

// Generate a unique message ID
std::string GenerateMessageId();

} // namespace blazeclaw::irc
