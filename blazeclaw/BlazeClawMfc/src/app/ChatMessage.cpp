#include "pch.h"
#include "ChatMessage.h"

#include <sstream>

namespace blazeclaw::irc {

bool CMessage::IsSensitive() const {
    switch (type_) {
    case ChatMessageType::Oper:
    case ChatMessageType::Ban:
    case ChatMessageType::Kick:
    case ChatMessageType::Auth:
    case ChatMessageType::Token:
        return true;
    default:
        return false;
    }
}

std::string ChatMessage::ToJson() const {
    std::ostringstream oss;

    oss << "{";
    oss << "\"id\":\"" << id_ << "\",";
    oss << "\"type\":" << static_cast<int>(type_) << ",";
    oss << "\"timestamp\":" << timestamp_ms_ << ",";

    if (!channel_.empty()) {
        oss << "\"channel\":\"" << channel_ << "\",";
    }
    if (!sender_nick_.empty()) {
        oss << "\"sender\":\"" << sender_nick_ << "\",";
    }
    if (!target_nick_.empty()) {
        oss << "\"target\":\"" << target_nick_ << "\",";
    }

    // Content
    oss << "\"content\":{";
    oss << "\"text\":\"" << content_.text << "\"";
    if (!content_.attachments.empty()) {
        oss << ",\"attachments\":[";
        for (size_t i = 0; i < content_.attachments.size(); ++i) {
            if (i > 0) oss << ",";
            oss << "\"" << content_.attachments[i] << "\"";
        }
        oss << "]";
    }
    if (content_.reply_to_message_id) {
        oss << ",\"reply_to\":\"" << *content_.reply_to_message_id << "\"";
    }
    if (content_.topic_id) {
        oss << ",\"topic_id\":\"" << *content_.topic_id << "\"";
    }
    oss << "}";

    oss << "}";
    return oss.str();
}

bool ChatMessage::FromJson(const std::string& json) {
    // Simplified JSON parsing - in real implementation, use nlohmann::json
    // For now, just validate it's not empty
    return !json.empty();
}

std::string CPrompt::ToJson() const {
    std::ostringstream oss;

    oss << ChatMessage::ToJson();

    // Add AI-specific fields
    if (!ai_context_.empty()) {
        // Would need proper JSON merging here
    }
    if (!system_prompt_.empty()) {
        // Would need proper JSON merging here
    }

    return oss.str();
}

bool CPrompt::FromJson(const std::string& json) {
    return ChatMessage::FromJson(json);
}

// Factory functions
CMessagePtr CreateMessage(const std::string& channel,
                          const std::string& sender,
                          const std::string& text) {
    auto msg = std::make_shared<CMessage>();
    msg->SetId(GenerateMessageId());
    msg->SetChannel(channel);
    msg->SetSenderNick(sender);
    msg->SetTimestamp(GetCurrentTimestampMs());

    ChatMessageContent content;
    content.text = text;
    msg->SetContent(content);

    return msg;
}

CPromptPtr CreatePrompt(const std::string& channel,
                        const std::string& sender,
                        const std::string& text) {
    auto msg = std::make_shared<CPrompt>();
    msg->SetId(GenerateMessageId());
    msg->SetChannel(channel);
    msg->SetSenderNick(sender);
    msg->SetTimestamp(GetCurrentTimestampMs());

    ChatMessageContent content;
    content.text = text;
    msg->SetContent(content);

    return msg;
}

CPromptPtr CreateAiPrompt(const std::string& channel,
                          const std::string& sender,
                          const std::string& text,
                          const std::string& system_prompt) {
    auto msg = std::make_shared<CPrompt>();
    msg->SetId(GenerateMessageId());
    msg->SetChannel(channel);
    msg->SetSenderNick(sender);
    msg->SetTimestamp(GetCurrentTimestampMs());
    msg->SetType(ChatMessageType::AiPrompt);

    if (!system_prompt.empty()) {
        msg->SetSystemPrompt(system_prompt);
    }

    ChatMessageContent content;
    content.text = text;
    msg->SetContent(content);

    return msg;
}

std::string GenerateMessageId() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    std::ostringstream oss;
    oss << "msg-" << std::hex << ms;
    return oss.str();
}

} // namespace blazeclaw::irc
