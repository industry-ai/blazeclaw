#include "pch.h"
#include "GatewayHost.h"
#include "GatewayJsonUtils.h"

#include <chrono>
#include <sstream>

namespace blazeclaw::gateway {

    namespace {
        constexpr char kSilentReplyToken[] = "NO_REPLY";

        std::string EscapeJsonLocal(const std::string& value) {
            std::string escaped;
            escaped.reserve(value.size() + 8);

            for (const char ch : value) {
                switch (ch) {
                case '"':
                    escaped += "\\\"";
                    break;
                case '\\':
                    escaped += "\\\\";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                case '\t':
                    escaped += "\\t";
                    break;
                default:
                    escaped.push_back(ch);
                    break;
                }
            }

            return escaped;
        }

        std::string ExtractStringParam(
            const std::optional<std::string>& paramsJson,
            const std::string& fieldName) {
            if (!paramsJson.has_value()) {
                return {};
            }

            std::string value;
            if (!json::FindStringField(paramsJson.value(), fieldName, value)) {
                return {};
            }

            return value;
        }

        std::string SerializeSkillCatalogEntry(
            const SkillsCatalogGatewayEntry& entry) {
            return "{\"name\":\"" +
                EscapeJsonLocal(entry.name) +
                "\",\"skillKey\":\"" +
                EscapeJsonLocal(entry.skillKey) +
                "\",\"command\":\"" +
                EscapeJsonLocal(entry.commandName) +
                "\",\"installKind\":\"" +
                EscapeJsonLocal(entry.installKind) +
                "\",\"installCommand\":\"" +
                EscapeJsonLocal(entry.installCommand) +
                "\",\"installExecutable\":" +
                std::string(entry.installExecutable ? "true" : "false") +
                ",\"installReason\":\"" +
                EscapeJsonLocal(entry.installReason) +
                "\"" +
                "\",\"description\":\"" +
                EscapeJsonLocal(entry.description) +
                "\",\"source\":\"" +
                EscapeJsonLocal(entry.source) +
                "\",\"precedence\":" +
                std::to_string(entry.precedence) +
                ",\"eligible\":" +
                std::string(entry.eligible ? "true" : "false") +
                ",\"disabled\":" +
                std::string(entry.disabled ? "true" : "false") +
                ",\"blockedByAllowlist\":" +
                std::string(entry.blockedByAllowlist ? "true" : "false") +
                ",\"disableModelInvocation\":" +
                std::string(entry.disableModelInvocation ? "true" : "false") +
                ",\"validFrontmatter\":" +
                std::string(entry.validFrontmatter ? "true" : "false") +
                ",\"validationErrorCount\":" +
                std::to_string(entry.validationErrorCount) +
                "}";
        }

        std::optional<std::size_t> ExtractSizeParam(
            const std::optional<std::string>& paramsJson,
            const std::string& fieldName) {
            if (!paramsJson.has_value()) {
                return std::nullopt;
            }

            std::uint64_t value = 0;
            if (!json::FindUInt64Field(paramsJson.value(), fieldName, value)) {
                return std::nullopt;
            }

            return static_cast<std::size_t>(value);
        }

        std::optional<bool> ExtractBoolParam(
            const std::optional<std::string>& paramsJson,
            const std::string& fieldName) {
            if (!paramsJson.has_value()) {
                return std::nullopt;
            }

            bool value = false;
            if (!json::FindBoolField(paramsJson.value(), fieldName, value)) {
                return std::nullopt;
            }

            return value;
        }

        bool HasAgentId(
            const GatewayAgentRegistry& registry,
            const std::string& agentId) {
            if (agentId.empty()) {
                return false;
            }

            const auto agents = registry.List();
            return std::any_of(
                agents.begin(),
                agents.end(),
                [&](const AgentEntry& entry) {
                    return entry.id == agentId;
                });
        }

        bool HasSessionId(
            const GatewaySessionRegistry& registry,
            const std::string& sessionId) {
            if (sessionId.empty()) {
                return false;
            }

            const auto sessions = registry.List();
            return std::any_of(
                sessions.begin(),
                sessions.end(),
                [&](const SessionEntry& entry) {
                    return entry.id == sessionId;
                });
        }

        std::uint64_t CurrentEpochMsLocal() {
            const auto now = std::chrono::system_clock::now();
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch())
                    .count());
        }

        std::string BuildAssistantFinalMessageJson(
            const std::string& text,
            const std::uint64_t timestampMs) {
            return "{\"role\":\"assistant\",\"content\":[{\"type\":\"text\",\"text\":\"" +
                EscapeJsonLocal(text) +
                "\"}],\"timestamp\":" +
                std::to_string(timestampMs) +
                "}";
        }

        std::string BuildAssistantDeltaMessageJson(const std::string& text) {
            return
                "{\"role\":\"assistant\",\"text\":\"" +
                EscapeJsonLocal(text) +
                "\"}";
        }

        std::string BuildUserMessageJson(
            const std::string& text,
            const bool hasAttachments,
            const std::uint64_t timestampMs) {
            std::string content = "[";
            bool first = true;
            if (!text.empty()) {
                content +=
                    "{\"type\":\"text\",\"text\":\"" +
                    EscapeJsonLocal(text) +
                    "\"}";
                first = false;
            }

            if (hasAttachments) {
                if (!first) {
                    content += ",";
                }

                content +=
                    "{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/*\",\"data\":\"[omitted]\"}}";
            }

            content += "]";

            return "{\"role\":\"user\",\"content\":" +
                content +
                ",\"timestamp\":" +
                std::to_string(timestampMs) +
                "}";
        }

        std::string BuildChatEventJson(
            const std::string& runId,
            const std::string& sessionKey,
            const std::string& state,
            const std::optional<std::string>& messageJson,
            const std::optional<std::string>& errorMessage,
            const std::uint64_t timestampMs) {
            std::string payload =
                "{\"runId\":\"" +
                EscapeJsonLocal(runId) +
                "\",\"sessionKey\":\"" +
                EscapeJsonLocal(sessionKey) +
                "\",\"state\":\"" +
                EscapeJsonLocal(state) +
                "\",\"timestamp\":" +
                std::to_string(timestampMs);

            if (messageJson.has_value()) {
                payload += ",\"message\":" + messageJson.value();
            }

            if (errorMessage.has_value()) {
                payload +=
                    ",\"errorMessage\":\"" +
                    EscapeJsonLocal(errorMessage.value()) +
                    "\"";
            }

            payload += "}";
            return payload;
        }

        bool IsSilentReplyText(const std::string& text) {
            return json::Trim(text) == kSilentReplyToken;
        }

        bool IsSilentAssistantMessageJson(const std::string& messageJson) {
            std::string role;
            if (!json::FindStringField(messageJson, "role", role)) {
                return false;
            }

            if (role != "assistant") {
                return false;
            }

            return messageJson.find("\"text\":\"NO_REPLY\"") !=
                std::string::npos;
        }

        void PushHistoryMessageIfNew(
            std::vector<std::string>& history,
            const std::string& messageJson) {
            if (!history.empty() && history.back() == messageJson) {
                return;
            }

            history.push_back(messageJson);
        }

        bool ValidateAttachmentPayloadShape(
            const std::optional<std::string>& paramsJson,
            bool& hasAttachments,
            std::string& errorCode,
            std::string& errorMessage) {
            hasAttachments = false;
            errorCode.clear();
            errorMessage.clear();
            if (!paramsJson.has_value()) {
                return true;
            }

            std::string attachmentsRaw;
            if (!json::FindRawField(paramsJson.value(), "attachments", attachmentsRaw)) {
                return true;
            }

            const std::string attachmentsTrimmed = json::Trim(attachmentsRaw);
            if (attachmentsTrimmed.empty() || attachmentsTrimmed == "[]") {
                return true;
            }

            if (attachmentsTrimmed.front() != '[' || attachmentsTrimmed.back() != ']') {
                errorCode = "invalid_attachments";
                errorMessage = "attachments must be a JSON array.";
                return false;
            }

            hasAttachments = true;
            if (attachmentsTrimmed.find("\"type\":\"image\"") == std::string::npos ||
                attachmentsTrimmed.find("\"mimeType\":\"") == std::string::npos ||
                attachmentsTrimmed.find("\"content\":\"") == std::string::npos) {
                errorCode = "invalid_attachments";
                errorMessage =
                    "attachments entries must include type=image, mimeType, and content.";
                return false;
            }

            return true;
        }

        std::vector<std::string> ExtractAttachmentMimeTypes(
            const std::optional<std::string>& paramsJson) {
            std::vector<std::string> mimeTypes;
            if (!paramsJson.has_value()) {
                return mimeTypes;
            }

            std::string attachmentsRaw;
            if (!json::FindRawField(
                    paramsJson.value(),
                    "attachments",
                    attachmentsRaw)) {
                return mimeTypes;
            }

            const std::string key = "\"mimeType\":\"";
            std::size_t cursor = 0;
            while (cursor < attachmentsRaw.size()) {
                const auto keyPos = attachmentsRaw.find(key, cursor);
                if (keyPos == std::string::npos) {
                    break;
                }

                const std::size_t valueStart = keyPos + key.size();
                if (valueStart >= attachmentsRaw.size()) {
                    break;
                }

                std::size_t valueEnd = valueStart;
                bool escaped = false;
                while (valueEnd < attachmentsRaw.size()) {
                    const char ch = attachmentsRaw[valueEnd];
                    if (escaped) {
                        escaped = false;
                        ++valueEnd;
                        continue;
                    }

                    if (ch == '\\') {
                        escaped = true;
                        ++valueEnd;
                        continue;
                    }

                    if (ch == '"') {
                        break;
                    }

                    ++valueEnd;
                }

                if (valueEnd > valueStart) {
                    mimeTypes.push_back(
                        attachmentsRaw.substr(valueStart, valueEnd - valueStart));
                }

                cursor = valueEnd == std::string::npos
                    ? attachmentsRaw.size()
                    : valueEnd + 1;
            }

            return mimeTypes;
        }

        std::vector<std::string> ParseJsonStringArrayLocal(
            const std::string& rawArray) {
            std::vector<std::string> values;
            const std::string trimmed = json::Trim(rawArray);
            if (trimmed.size() < 2 ||
                trimmed.front() != '[' ||
                trimmed.back() != ']') {
                return values;
            }

            std::string current;
            bool inString = false;
            bool escaping = false;
            for (std::size_t i = 1; i + 1 < trimmed.size(); ++i) {
                const char ch = trimmed[i];
                if (!inString) {
                    if (ch == '"') {
                        inString = true;
                        current.clear();
                    }
                    continue;
                }

                if (escaping) {
                    current.push_back(ch);
                    escaping = false;
                    continue;
                }

                if (ch == '\\') {
                    escaping = true;
                    continue;
                }

                if (ch == '"') {
                    values.push_back(current);
                    inString = false;
                    continue;
                }

                current.push_back(ch);
            }

            return values;
        }

        std::string SerializeFloatArrayLocal(
            const std::vector<float>& values) {
            std::ostringstream output;
            output.setf(std::ios::fixed);
            output.precision(6);
            output << "[";
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i > 0) {
                    output << ",";
                }

                output << values[i];
            }
            output << "]";
            return output.str();
        }

        std::string SerializeFloatMatrixLocal(
            const std::vector<std::vector<float>>& vectors) {
            std::string output = "[";
            for (std::size_t i = 0; i < vectors.size(); ++i) {
                if (i > 0) {
                    output += ",";
                }

                output += SerializeFloatArrayLocal(vectors[i]);
            }

            output += "]";
            return output;
        }
    }

    void GatewayHost::RegisterRuntimeHandlers() {
        m_dispatcher.Register(
            "gateway.embeddings.generate",
            [this](const protocol::RequestFrame& request) {
                const std::string text =
                    ExtractStringParam(request.paramsJson, "text");
                const std::optional<bool> normalize =
                    ExtractBoolParam(request.paramsJson, "normalize");
                const std::string model =
                    ExtractStringParam(request.paramsJson, "model");
                const std::string traceId =
                    request.id.empty() ? "gateway.embeddings.generate" : request.id;

                if (text.empty()) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "invalid_params",
                            .message = "`text` must be a non-empty string.",
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                const std::vector<std::string> attachmentMimeTypes =
                    ExtractAttachmentMimeTypes(request.paramsJson);

                if (!m_embeddingsGenerateCallback) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "runtime_unavailable",
                            .message = "Embeddings runtime callback is unavailable.",
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                const auto result = m_embeddingsGenerateCallback(
                    EmbeddingsGenerateRequest{
                        .text = text,
                        .normalize = normalize,
                        .model = model,
                        .traceId = traceId,
                    });

                if (!result.ok) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = result.errorCode.empty()
                                ? "embedding_failed"
                                : result.errorCode,
                            .message = result.errorMessage.empty()
                                ? "Embedding generation failed."
                                : result.errorMessage,
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vector\":" + SerializeFloatArrayLocal(result.vector) +
                        ",\"dimension\":" + std::to_string(result.dimension) +
                        ",\"provider\":\"" + EscapeJsonLocal(result.provider) +
                        "\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
                        "\",\"latencyMs\":" + std::to_string(result.latencyMs) +
                        ",\"status\":\"" + EscapeJsonLocal(result.status) +
                        "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.embeddings.batchGenerate",
            [this](const protocol::RequestFrame& request) {
                std::string rawTexts;
                std::vector<std::string> texts;
                if (request.paramsJson.has_value() &&
                    json::FindRawField(request.paramsJson.value(), "texts", rawTexts)) {
                    texts = ParseJsonStringArrayLocal(rawTexts);
                }

                const std::optional<bool> normalize =
                    ExtractBoolParam(request.paramsJson, "normalize");
                const std::string model =
                    ExtractStringParam(request.paramsJson, "model");
                const std::string traceId =
                    request.id.empty() ? "gateway.embeddings.batchGenerate" : request.id;

                if (texts.empty()) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "invalid_params",
                            .message = "`texts` must be a non-empty string array.",
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                if (texts.size() > 64) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "invalid_params",
                            .message = "`texts` exceeds maximum batch size of 64.",
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                if (!m_embeddingsBatchCallback) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "runtime_unavailable",
                            .message = "Embeddings runtime callback is unavailable.",
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                const auto result = m_embeddingsBatchCallback(
                    EmbeddingsBatchRequest{
                        .texts = texts,
                        .normalize = normalize,
                        .model = model,
                        .traceId = traceId,
                    });

                if (!result.ok) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = result.errorCode.empty()
                                ? "embedding_failed"
                                : result.errorCode,
                            .message = result.errorMessage.empty()
                                ? "Embedding batch generation failed."
                                : result.errorMessage,
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectors\":" + SerializeFloatMatrixLocal(result.vectors) +
                        ",\"count\":" + std::to_string(result.vectors.size()) +
                        ",\"dimension\":" + std::to_string(result.dimension) +
                        ",\"provider\":\"" + EscapeJsonLocal(result.provider) +
                        "\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
                        "\",\"latencyMs\":" + std::to_string(result.latencyMs) +
                        ",\"status\":\"" + EscapeJsonLocal(result.status) +
                        "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "chat.history",
            [this](const protocol::RequestFrame& request) {
                const std::string requestedSessionKey =
                    ExtractStringParam(request.paramsJson, "sessionKey");
                const std::string sessionKey =
                    requestedSessionKey.empty() ? "main" : requestedSessionKey;
                const std::size_t requestedLimit =
                    ExtractSizeParam(request.paramsJson, "limit").value_or(200);
                const std::size_t limit =
                    (std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 500 }));

                const auto historyIt = m_chatHistoryBySession.find(sessionKey);
                std::string messagesJson = "[";
                if (historyIt != m_chatHistoryBySession.end()) {
                    const auto& history = historyIt->second;
                    const std::size_t begin = history.size() > limit
                        ? history.size() - limit
                        : 0;
                    bool firstMessage = true;
                    for (std::size_t i = begin; i < history.size(); ++i) {
                        if (IsSilentAssistantMessageJson(history[i])) {
                            continue;
                        }

                        if (!firstMessage) {
                            messagesJson += ",";
                        }

                        messagesJson += history[i];
                        firstMessage = false;
                    }
                }

                messagesJson += "]";
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"messages\":" +
                        messagesJson +
                        ",\"thinkingLevel\":\"normal\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "chat.send",
            [this](const protocol::RequestFrame& request) {
                const std::string requestedSessionKey =
                    ExtractStringParam(request.paramsJson, "sessionKey");
                const std::string sessionKey =
                    requestedSessionKey.empty() ? "main" : requestedSessionKey;
                const std::string message =
                    ExtractStringParam(request.paramsJson, "message");
                const std::string idempotencyKey =
                    ExtractStringParam(request.paramsJson, "idempotencyKey");
                const bool forceError =
                    ExtractBoolParam(request.paramsJson, "forceError").value_or(false);

                bool hasAttachments = false;
                std::string attachmentsErrorCode;
                std::string attachmentsErrorMessage;
                if (!ValidateAttachmentPayloadShape(
                        request.paramsJson,
                        hasAttachments,
                        attachmentsErrorCode,
                        attachmentsErrorMessage)) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = attachmentsErrorCode,
                            .message = attachmentsErrorMessage,
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                if (message.empty() && !hasAttachments) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "invalid_message",
                            .message = "chat.send requires non-empty message or attachments.",
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                const std::vector<std::string> attachmentMimeTypes =
                    ExtractAttachmentMimeTypes(request.paramsJson);

                if (!idempotencyKey.empty()) {
                    const auto dedupeIt =
                        m_chatRunByIdempotency.find(idempotencyKey);
                    if (dedupeIt != m_chatRunByIdempotency.end()) {
                        return protocol::ResponseFrame{
                            .id = request.id,
                            .ok = true,
                            .payloadJson =
                                "{\"runId\":\"" +
                                EscapeJsonLocal(dedupeIt->second) +
                                "\",\"queued\":false,\"deduped\":true}",
                            .error = std::nullopt,
                        };
                    }
                }

                const std::uint64_t nowMs = CurrentEpochMsLocal();
                const std::string runId = !request.id.empty()
                    ? request.id
                    : ("chat-run-" + std::to_string(nowMs) +
                        "-" + std::to_string(m_chatRunsById.size() + 1));

                std::string assistantText = message.empty()
                    ? "Received image attachment."
                    : ("Echo: " + message);
                std::string backendErrorCode;
                std::string backendErrorMessage;
                bool failed = false;

                if (forceError) {
                    failed = true;
                    backendErrorCode = "forced_error";
                    backendErrorMessage = "forced error for deterministic verification";
                }

                if (!forceError && m_chatRuntimeCallback) {
                    const auto runtimeResult = m_chatRuntimeCallback(
                        ChatRuntimeRequest{
                            .runId = runId,
                            .sessionKey = sessionKey,
                            .message = message,
                            .hasAttachments = hasAttachments,
                            .attachmentMimeTypes = attachmentMimeTypes,
                        });

                    if (runtimeResult.ok) {
                        if (!runtimeResult.assistantText.empty()) {
                            assistantText = runtimeResult.assistantText;
                        }
                    }
                    else {
                        failed = true;
                        backendErrorCode = runtimeResult.errorCode.empty()
                            ? "chat_runtime_error"
                            : runtimeResult.errorCode;
                        backendErrorMessage = runtimeResult.errorMessage.empty()
                            ? "chat runtime failed"
                            : runtimeResult.errorMessage;
                    }
                }

                const bool silentAssistantReply = IsSilentReplyText(assistantText);

                auto& sessionHistory = m_chatHistoryBySession[sessionKey];
                PushHistoryMessageIfNew(
                    sessionHistory,
                    BuildUserMessageJson(message, hasAttachments, nowMs));

                auto& sessionEvents = m_chatEventsBySession[sessionKey];
                std::size_t streamCursor = 0;
                if (!failed && !silentAssistantReply) {
                    streamCursor = (std::min)(assistantText.size(), std::size_t{ 6 });
                    if (streamCursor > 0) {
                        sessionEvents.push_back(ChatEventState{
                            .runId = runId,
                            .sessionKey = sessionKey,
                            .state = "delta",
                            .messageJson = BuildAssistantDeltaMessageJson(
                                assistantText.substr(0, streamCursor)),
                            .errorMessage = std::nullopt,
                            .timestampMs = nowMs,
                            });
                    }
                }

                m_chatRunsById.insert_or_assign(
                    runId,
                    ChatRunState{
                        .runId = runId,
                        .sessionKey = sessionKey,
                        .idempotencyKey = idempotencyKey,
                        .userMessage = message,
                        .assistantText = assistantText,
                        .streamCursor = streamCursor,
                        .lastEmitMs = nowMs,
                        .failed = failed,
                        .errorMessage = backendErrorMessage,
                        .startedAtMs = nowMs,
                        .active = true,
                    });

                if (!idempotencyKey.empty()) {
                    m_chatRunByIdempotency.insert_or_assign(idempotencyKey, runId);
                }

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"runId\":\"" +
                        EscapeJsonLocal(runId) +
                        "\",\"backendErrorCode\":" +
                        (backendErrorCode.empty()
                            ? std::string("null")
                            : ("\"" + EscapeJsonLocal(backendErrorCode) + "\"")) +
                        ",\"queued\":true,\"deduped\":false}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "chat.abort",
            [this](const protocol::RequestFrame& request) {
                const std::string requestedSessionKey =
                    ExtractStringParam(request.paramsJson, "sessionKey");
                const std::string sessionKey =
                    requestedSessionKey.empty() ? "main" : requestedSessionKey;
                const std::string requestedRunId =
                    ExtractStringParam(request.paramsJson, "runId");

                auto runIt = m_chatRunsById.end();
                if (!requestedRunId.empty()) {
                    const auto exact = m_chatRunsById.find(requestedRunId);
                    if (exact != m_chatRunsById.end() &&
                        exact->second.sessionKey == sessionKey) {
                        runIt = exact;
                    }
                }
                else {
                    runIt = std::find_if(
                        m_chatRunsById.begin(),
                        m_chatRunsById.end(),
                        [&](const auto& pair) {
                            return pair.second.sessionKey == sessionKey &&
                                pair.second.active;
                        });
                }

                if (runIt == m_chatRunsById.end()) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = true,
                        .payloadJson =
                            "{\"aborted\":false,\"sessionKey\":\"" +
                            EscapeJsonLocal(sessionKey) +
                            "\"}",
                        .error = std::nullopt,
                    };
                }

                const std::string runId = runIt->second.runId;
                if (m_chatAbortCallback) {
                    m_chatAbortCallback(
                        ChatAbortRequest{
                            .runId = runId,
                            .sessionKey = sessionKey,
                        });
                }

                auto& queue = m_chatEventsBySession[sessionKey];
                std::erase_if(
                    queue,
                    [&](const ChatEventState& item) {
                        return item.runId == runId;
                    });

                const std::uint64_t nowMs = CurrentEpochMsLocal();
                const bool silentAssistantReply =
                    IsSilentReplyText(runIt->second.assistantText);
                queue.push_back(ChatEventState{
                    .runId = runIt->second.runId,
                    .sessionKey = sessionKey,
                    .state = "aborted",
                    .messageJson = silentAssistantReply
                        ? std::nullopt
                        : std::optional<std::string>(
                            BuildAssistantFinalMessageJson(
                                runIt->second.assistantText,
                                nowMs)),
                    .errorMessage = std::nullopt,
                    .timestampMs = nowMs,
                    });

                runIt->second.active = false;
                runIt->second.streamCursor = runIt->second.assistantText.size();

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"aborted\":true,\"runId\":\"" +
                        EscapeJsonLocal(runId) +
                        "\",\"sessionKey\":\"" +
                        EscapeJsonLocal(sessionKey) +
                        "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "chat.events.poll",
            [this](const protocol::RequestFrame& request) {
                const std::string requestedSessionKey =
                    ExtractStringParam(request.paramsJson, "sessionKey");
                const std::string sessionKey =
                    requestedSessionKey.empty() ? "main" : requestedSessionKey;
                const std::size_t requestedLimit =
                    ExtractSizeParam(request.paramsJson, "limit").value_or(20);
                const std::size_t limit =
                    (std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 100 }));

                const std::uint64_t nowMs = CurrentEpochMsLocal();
                auto queueIt = m_chatEventsBySession.find(sessionKey);
                auto& queue = m_chatEventsBySession[sessionKey];

                auto runIt = std::find_if(
                    m_chatRunsById.begin(),
                    m_chatRunsById.end(),
                    [&](auto& pair) {
                        return pair.second.sessionKey == sessionKey && pair.second.active;
                    });

                if (runIt != m_chatRunsById.end()) {
                    auto& run = runIt->second;
                    const bool silentAssistantReply = IsSilentReplyText(run.assistantText);
                    const bool enoughTimeElapsed =
                        run.lastEmitMs == 0 || (nowMs - run.lastEmitMs) >= 180;

                    if (!silentAssistantReply && run.streamCursor < run.assistantText.size() &&
                        enoughTimeElapsed) {
                        const std::size_t nextCursor =
                            (std::min)(run.assistantText.size(), run.streamCursor + std::size_t{ 8 });
                        run.streamCursor = nextCursor;
                        run.lastEmitMs = nowMs;

                        queue.push_back(ChatEventState{
                            .runId = run.runId,
                            .sessionKey = run.sessionKey,
                            .state = "delta",
                            .messageJson = BuildAssistantDeltaMessageJson(
                                run.assistantText.substr(0, run.streamCursor)),
                            .errorMessage = std::nullopt,
                            .timestampMs = nowMs,
                            });
                    }

                    const bool streamCompleted =
                        silentAssistantReply || run.streamCursor >= run.assistantText.size();
                    if (streamCompleted) {
                        queue.push_back(ChatEventState{
                            .runId = run.runId,
                            .sessionKey = run.sessionKey,
                            .state = run.failed ? "error" : "final",
                            .messageJson = run.failed || silentAssistantReply
                                ? std::nullopt
                                : std::optional<std::string>(
                                    BuildAssistantFinalMessageJson(run.assistantText, nowMs)),
                            .errorMessage = run.failed
                                ? std::optional<std::string>(run.errorMessage.empty()
                                    ? "chat error"
                                    : run.errorMessage)
                                : std::nullopt,
                            .timestampMs = nowMs,
                            });

                        run.active = false;
                    }
                }

                std::string eventsJson = "[";
                std::size_t emitted = 0;
                if (!queue.empty()) {
                    while (emitted < limit && !queue.empty()) {
                        const ChatEventState eventState = queue.front();
                        queue.pop_front();

                        if (emitted > 0) {
                            eventsJson += ",";
                        }

                        eventsJson += BuildChatEventJson(
                            eventState.runId,
                            eventState.sessionKey,
                            eventState.state,
                            eventState.messageJson,
                            eventState.errorMessage,
                            eventState.timestampMs);
                        ++emitted;

                        if ((eventState.state == "final" ||
                            eventState.state == "aborted") &&
                            eventState.messageJson.has_value() &&
                            !IsSilentAssistantMessageJson(eventState.messageJson.value())) {
                            PushHistoryMessageIfNew(
                                m_chatHistoryBySession[sessionKey],
                                eventState.messageJson.value());
                        }

                        if (eventState.state == "final" ||
                            eventState.state == "aborted" ||
                            eventState.state == "error") {
                            const auto runIt = m_chatRunsById.find(eventState.runId);
                            if (runIt != m_chatRunsById.end()) {
                                if (!runIt->second.idempotencyKey.empty()) {
                                    m_chatRunByIdempotency.erase(
                                        runIt->second.idempotencyKey);
                                }

                                m_chatRunsById.erase(runIt);
                            }
                        }
                    }
                }

                eventsJson += "]";
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"sessionKey\":\"" +
                        EscapeJsonLocal(sessionKey) +
                        "\",\"events\":" +
                        eventsJson +
                        ",\"count\":" +
                        std::to_string(emitted) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.status",
            [this](const protocol::RequestFrame& request) {
                const auto& state = m_skillsCatalogState;

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"total\":" +
                        std::to_string(state.entries.size()) +
                        ",\"rootsScanned\":" +
                        std::to_string(state.rootsScanned) +
                        ",\"rootsSkipped\":" +
                        std::to_string(state.rootsSkipped) +
                        ",\"oversizedSkillFiles\":" +
                        std::to_string(state.oversizedSkillFiles) +
                        ",\"invalidFrontmatterFiles\":" +
                        std::to_string(state.invalidFrontmatterFiles) +
                        ",\"eligible\":" +
                        std::to_string(state.eligibleCount) +
                        ",\"disabled\":" +
                        std::to_string(state.disabledCount) +
                        ",\"blockedByAllowlist\":" +
                        std::to_string(state.blockedByAllowlistCount) +
                        ",\"missingRequirements\":" +
                        std::to_string(state.missingRequirementsCount) +
                        ",\"promptIncluded\":" +
                        std::to_string(state.promptIncludedCount) +
                        ",\"promptChars\":" +
                        std::to_string(state.promptChars) +
                        ",\"promptTruncated\":" +
                        std::string(state.promptTruncated ? "true" : "false") +
                        ",\"snapshotVersion\":" +
                        std::to_string(state.snapshotVersion) +
                        ",\"watchEnabled\":" +
                        std::string(state.watchEnabled ? "true" : "false") +
                        ",\"watchDebounceMs\":" +
                        std::to_string(state.watchDebounceMs) +
                        ",\"watchReason\":\"" +
                        EscapeJsonLocal(state.watchReason) +
                        "\"" +
                        ",\"sandboxSyncOk\":" +
                        std::string(state.sandboxSyncOk ? "true" : "false") +
                        ",\"sandboxSynced\":" +
                        std::to_string(state.sandboxSynced) +
                        ",\"sandboxSkipped\":" +
                        std::to_string(state.sandboxSkipped) +
                        ",\"envAllowed\":" +
                        std::to_string(state.envAllowed) +
                        ",\"envBlocked\":" +
                        std::to_string(state.envBlocked) +
                        ",\"installExecutable\":" +
                        std::to_string(state.installExecutableCount) +
                        ",\"installBlocked\":" +
                        std::to_string(state.installBlockedCount) +
                        ",\"scanInfo\":" +
                        std::to_string(state.scanInfoCount) +
                        ",\"scanWarn\":" +
                        std::to_string(state.scanWarnCount) +
                        ",\"scanCritical\":" +
                        std::to_string(state.scanCriticalCount) +
                        ",\"scanFiles\":" +
                        std::to_string(state.scanScannedFiles) +
                        ",\"warnings\":" +
                        std::to_string(state.warningCount) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.install.options",
            [this](const protocol::RequestFrame& request) {
                std::string optionsJson = "[";
                bool first = true;
                std::size_t count = 0;
                for (const auto& entry : m_skillsCatalogState.entries) {
                    if (entry.installKind.empty()) {
                        continue;
                    }

                    if (!first) {
                        optionsJson += ",";
                    }

                    optionsJson +=
                        "{\"skill\":\"" +
                        EscapeJsonLocal(entry.name) +
                        "\",\"kind\":\"" +
                        EscapeJsonLocal(entry.installKind) +
                        "\",\"command\":\"" +
                        EscapeJsonLocal(entry.installCommand) +
                        "\",\"executable\":" +
                        std::string(entry.installExecutable ? "true" : "false") +
                        ",\"reason\":\"" +
                        EscapeJsonLocal(entry.installReason) +
                        "\"}";
                    first = false;
                    ++count;
                }

                optionsJson += "]";
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"options\":" +
                        optionsJson +
                        ",\"count\":" +
                        std::to_string(count) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.install.execute",
            [this](const protocol::RequestFrame& request) {
                const auto skillName =
                    ExtractStringParam(request.paramsJson, "skill");
                const auto it = std::find_if(
                    m_skillsCatalogState.entries.begin(),
                    m_skillsCatalogState.entries.end(),
                    [&skillName](const SkillsCatalogGatewayEntry& item) {
                        return item.name == skillName;
                    });

                if (it == m_skillsCatalogState.entries.end()) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "skill_not_found",
                            .message = "Requested skill install target was not found.",
                            .detailsJson = "{\"skill\":\"" +
                                EscapeJsonLocal(skillName) +
                                "\"}",
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                const auto warning = m_skillsCatalogState.scanCriticalCount > 0
                    ? "security_scan_critical"
                    : (m_skillsCatalogState.scanWarnCount > 0
                           ? "security_scan_warn"
                           : "none");

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"skill\":\"" +
                        EscapeJsonLocal(it->name) +
                        "\",\"kind\":\"" +
                        EscapeJsonLocal(it->installKind) +
                        "\",\"command\":\"" +
                        EscapeJsonLocal(it->installCommand) +
                        "\",\"executed\":" +
                        std::string(it->installExecutable ? "true" : "false") +
                        ",\"warning\":\"" +
                        warning +
                        "\",\"scanCritical\":" +
                        std::to_string(m_skillsCatalogState.scanCriticalCount) +
                        ",\"scanWarn\":" +
                        std::to_string(m_skillsCatalogState.scanWarnCount) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.scan.status",
            [this](const protocol::RequestFrame& request) {
                const auto& state = m_skillsCatalogState;
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"files\":" +
                        std::to_string(state.scanScannedFiles) +
                        ",\"info\":" +
                        std::to_string(state.scanInfoCount) +
                        ",\"warn\":" +
                        std::to_string(state.scanWarnCount) +
                        ",\"critical\":" +
                        std::to_string(state.scanCriticalCount) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.sandbox.status",
            [this](const protocol::RequestFrame& request) {
                const auto& state = m_skillsCatalogState;
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"ok\":" +
                        std::string(state.sandboxSyncOk ? "true" : "false") +
                        ",\"synced\":" +
                        std::to_string(state.sandboxSynced) +
                        ",\"skipped\":" +
                        std::to_string(state.sandboxSkipped) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.env.status",
            [this](const protocol::RequestFrame& request) {
                const auto& state = m_skillsCatalogState;
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"allowed\":" +
                        std::to_string(state.envAllowed) +
                        ",\"blocked\":" +
                        std::to_string(state.envBlocked) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.info",
            [this](const protocol::RequestFrame& request) {
                const auto skillName =
                    ExtractStringParam(request.paramsJson, "skill");
                if (skillName.empty()) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "missing_skill",
                            .message = "Parameter `skill` is required.",
                            .detailsJson = std::nullopt,
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                const auto it = std::find_if(
                    m_skillsCatalogState.entries.begin(),
                    m_skillsCatalogState.entries.end(),
                    [&skillName](const SkillsCatalogGatewayEntry& entry) {
                        return entry.name == skillName ||
                               entry.skillKey == skillName;
                    });

                if (it == m_skillsCatalogState.entries.end()) {
                    return protocol::ResponseFrame{
                        .id = request.id,
                        .ok = false,
                        .payloadJson = std::nullopt,
                        .error = protocol::ErrorShape{
                            .code = "skill_not_found",
                            .message = "Skill not found.",
                            .detailsJson =
                                "{\"skill\":\"" +
                                EscapeJsonLocal(skillName) +
                                "\"}",
                            .retryable = false,
                            .retryAfterMs = std::nullopt,
                        },
                    };
                }

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"skill\":\"" +
                        EscapeJsonLocal(it->name) +
                        "\",\"skillKey\":\"" +
                        EscapeJsonLocal(it->skillKey) +
                        "\",\"eligible\":" +
                        std::string(it->eligible ? "true" : "false") +
                        ",\"disabled\":" +
                        std::string(it->disabled ? "true" : "false") +
                        ",\"blockedByAllowlist\":" +
                        std::string(it->blockedByAllowlist ? "true" : "false") +
                        ",\"installKind\":\"" +
                        EscapeJsonLocal(it->installKind) +
                        "\",\"installExecutable\":" +
                        std::string(it->installExecutable ? "true" : "false") +
                        ",\"scanCritical\":" +
                        std::to_string(m_skillsCatalogState.scanCriticalCount) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.check",
            [this](const protocol::RequestFrame& request) {
                const auto& state = m_skillsCatalogState;
                const bool ok =
                    state.scanCriticalCount == 0 &&
                    state.installBlockedCount == 0 &&
                    state.sandboxSyncOk;

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"ok\":" +
                        std::string(ok ? "true" : "false") +
                        ",\"sandboxSyncOk\":" +
                        std::string(state.sandboxSyncOk ? "true" : "false") +
                        ",\"installBlocked\":" +
                        std::to_string(state.installBlockedCount) +
                        ",\"scanCritical\":" +
                        std::to_string(state.scanCriticalCount) +
                        ",\"scanWarn\":" +
                        std::to_string(state.scanWarnCount) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.diagnostics",
            [this](const protocol::RequestFrame& request) {
                const auto& state = m_skillsCatalogState;
                std::vector<std::string> hints;
                if (state.installBlockedCount > 0) {
                    hints.push_back("skills.install.options");
                }

                if (state.scanCriticalCount > 0) {
                    hints.push_back("skills.scan.status");
                }

                if (!state.sandboxSyncOk) {
                    hints.push_back("skills.sandbox.status");
                }

                std::string hintsJson = "[";
                for (std::size_t index = 0; index < hints.size(); ++index) {
                    if (index > 0) {
                        hintsJson += ",";
                    }

                    hintsJson +=
                        "\"" +
                        EscapeJsonLocal(hints[index]) +
                        "\"";
                }

                hintsJson += "]";
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"warnings\":" +
                        std::to_string(state.warningCount) +
                        ",\"installBlocked\":" +
                        std::to_string(state.installBlockedCount) +
                        ",\"scanCritical\":" +
                        std::to_string(state.scanCriticalCount) +
                        ",\"scanWarn\":" +
                        std::to_string(state.scanWarnCount) +
                        ",\"hints\":" +
                        hintsJson +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.prompt",
            [this](const protocol::RequestFrame& request) {
                const auto& state = m_skillsCatalogState;

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"prompt\":\"" +
                        EscapeJsonLocal(state.prompt) +
                        "\",\"included\":" +
                        std::to_string(state.promptIncludedCount) +
                        ",\"chars\":" +
                        std::to_string(state.promptChars) +
                        ",\"truncated\":" +
                        std::string(state.promptTruncated ? "true" : "false") +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.commands",
            [this](const protocol::RequestFrame& request) {
                std::string commandsJson = "[";
                bool first = true;
                std::size_t count = 0;

                for (const auto& entry : m_skillsCatalogState.entries) {
                    if (entry.commandName.empty()) {
                        continue;
                    }

                    if (!first) {
                        commandsJson += ",";
                    }

                    commandsJson +=
                        "{\"name\":\"" +
                        EscapeJsonLocal(entry.commandName) +
                        "\",\"skill\":\"" +
                        EscapeJsonLocal(entry.name) +
                        "\",\"description\":\"" +
                        EscapeJsonLocal(entry.description) +
                        "\"}";
                    first = false;
                    ++count;
                }

                commandsJson += "]";
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"commands\":" +
                        commandsJson +
                        ",\"count\":" +
                        std::to_string(count) +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.refresh",
            [this](const protocol::RequestFrame& request) {
                bool refreshed = false;
                if (m_skillsRefreshCallback) {
                    m_skillsCatalogState = m_skillsRefreshCallback();
                    refreshed = true;
                }

                const auto& state = m_skillsCatalogState;
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"refreshed\":" +
                        std::string(refreshed ? "true" : "false") +
                        ",\"version\":" +
                        std::to_string(state.snapshotVersion) +
                        ",\"reason\":\"" +
                        EscapeJsonLocal(state.watchReason) +
                        "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.skills.list",
            [this](const protocol::RequestFrame& request) {
                const auto includeInvalid =
                    ExtractBoolParam(request.paramsJson, "includeInvalid");
                const bool shouldIncludeInvalid =
                    !includeInvalid.has_value() || includeInvalid.value();

                std::string entriesJson = "[";
                bool first = true;
                std::size_t count = 0;
                for (const auto& entry : m_skillsCatalogState.entries) {
                    if (!shouldIncludeInvalid && !entry.validFrontmatter) {
                        continue;
                    }

                    if (!first) {
                        entriesJson += ",";
                    }

                    entriesJson += SerializeSkillCatalogEntry(entry);
                    first = false;
                    ++count;
                }
                entriesJson += "]";

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"skills\":" +
                        entriesJson +
                        ",\"count\":" +
                        std::to_string(count) +
                        ",\"includeInvalid\":" +
                        std::string(shouldIncludeInvalid ? "true" : "false") +
                        "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.status", [this](const protocol::RequestFrame& request) {
            const auto sessions = m_sessionRegistry.List();
            const auto agents = m_agentRegistry.List();
            const std::string activeSession =
                HasSessionId(m_sessionRegistry, m_runtimeAssignedSessionId)
                    ? m_runtimeAssignedSessionId
                    : (sessions.empty() ? "main" : sessions.front().id);
            const std::string activeAgent =
                HasAgentId(m_agentRegistry, m_runtimeAssignedAgentId)
                    ? m_runtimeAssignedAgentId
                    : (agents.empty() ? "default" : agents.front().id);
            const bool busy =
                m_runtimeQueueDepth > 0 || m_runtimeRunningCount > 0;

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"state\":\"" + std::string(busy ? "busy" : "idle") +
                    "\",\"activeSession\":\"" +
                    EscapeJsonLocal(activeSession) + "\",\"activeAgent\":\"" +
                    EscapeJsonLocal(activeAgent) + "\",\"queueDepth\":" +
                    std::to_string(m_runtimeQueueDepth) +
                    ",\"running\":" +
                    std::to_string(m_runtimeRunningCount) +
                    ",\"capacity\":" +
                    std::to_string(m_runtimeQueueCapacity) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseGate4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseGate4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorGate4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorGate4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phasePortal4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phasePortal4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncGate4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncGate4\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandGate4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandGate4\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorPortal4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorPortal4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseBridge4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseBridge4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncPortal4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncPortal4\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandPortal4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandPortal4\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorBridge4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorBridge4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLink4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLink4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncBridge4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncBridge4\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandBridge4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandBridge4\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLink4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLink4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseNode5",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNode5\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLink4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLink4\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLink4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLink4\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorNode5",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorNode5\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseHub3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseHub3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncNode5",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncNode5\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandNode5",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandNode5\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorHub3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorHub3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseGate3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseGate3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncHub3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncHub3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandHub3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandHub3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorGate3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorGate3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRelay3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRelay3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncGate3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncGate3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandGate3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandGate3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRelay3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRelay3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phasePortal3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phasePortal3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRelay3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRelay3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRelay3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRelay3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorPortal3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorPortal3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseBridge3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseBridge3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncPortal3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncPortal3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandPortal3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandPortal3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorBridge3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorBridge3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseMesh3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseMesh3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncBridge3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncBridge3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandBridge3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandBridge3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorMesh3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorMesh3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseNode4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNode4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncMesh3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncMesh3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandMesh3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandMesh3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorNode4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorNode4\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLink3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLink3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncNode4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncNode4\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandNode4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandNode4\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLink3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLink3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseThread2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseThread2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLink3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLink3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLink3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLink3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorThread2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorThread2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseChain2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseChain2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncThread2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncThread2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandThread2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandThread2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorChain2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorChain2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseSpline2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncChain2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncChain2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandChain2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandChain2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorSpline2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRail2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncSpline2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandSpline2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRail2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseTrack2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRail2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRail2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorTrack2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLane2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncTrack2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandTrack2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLane2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseGrid2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLane2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLane2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorGrid2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseBand2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncGrid2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandGrid2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorBand2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseArc2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncBand2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandBand2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorArc2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseMesh2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncArc2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandArc2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorMesh2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLink2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncMesh2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandMesh2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLink2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNode3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLink2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLink2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorNode3\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseHub2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncNode3\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandNode3\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorHub2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseGate2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncHub2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandHub2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorGate2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRelay2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncGate2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandGate2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRelay2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phasePortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phasePortal\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRelay2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRelay2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorPortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorPortal\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseAnchor2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncPortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncPortal\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandPortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandPortal\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorAnchor2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseBridge\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncAnchor2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandAnchor2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorBridge\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseNode",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNode\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncBridge\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandBridge\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorNode2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorNode2\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLink\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncNode2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncNode2\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandNode2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandNode2\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLink\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseThread\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLink\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLink\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorThread\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseChain\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncThread\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandThread\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorChain\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseSpline\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncChain\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandChain\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorSpline\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRail\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncSpline\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandSpline\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRail\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseTrack\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRail\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRail\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorTrack\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLane\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncTrack\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandTrack\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorLane\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseGrid\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncLane\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLane\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorGrid\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseSpan\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncGrid\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandGrid\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorSpan\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseFrame\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncSpan\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandSpan\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorFrame\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseCore\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncFrame\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandFrame\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorCore\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseNet",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNet\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncCore\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandCore\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorNode",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorNode\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseFabric",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseFabric\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncNet",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncNet\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandNode",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandNode\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorMesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorMesh\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseMesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseMesh\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncFabric",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncFabric\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandArc\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorArc\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseArc\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncMesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncMesh\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandLattice",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandLattice\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorSpiral\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseSpiral\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncArc\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandSpiral\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorRibbon\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseHelix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseHelix\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncSpiral\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandHelix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandHelix\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorContour\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseRibbon\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncHelix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncHelix\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandRibbon\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseContour\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncRibbon\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandContour\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.driftVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"driftVector\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLattice",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseLattice\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncContour\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandMatrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandMatrix\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.envelopeDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"envelopeDrift\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseVector\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncMatrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncMatrix\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandVector\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.biasEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"biasEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorPhase",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorPhase\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncEnvelope\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandDrift\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.biasDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"biasDrift\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorField",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectors\":2,\"magnitude\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncDrift\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandStability",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandStability\":100,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phase\":\"steady\",\"amplitude\":1,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vectorDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectorDrift\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseBias",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phase\":\"steady\",\"bias\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register("gateway.runtime.streaming.status", [this](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"enabled\":" +
                    std::string(m_runtimeAgentStreaming ? "true" : "false") +
                    ",\"mode\":\"chunked\",\"heartbeatMs\":1500" +
                    std::string(m_streamingThrottled ? ",\"throttled\":true" : ",\"throttled\":false") +
                    ",\"bufferedFrames\":" + std::to_string(m_streamingBufferedFrames) +
                    ",\"bufferedBytes\":" + std::to_string(m_streamingBufferedBytes) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.cohesion",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cohesive\":true,\"delta\":0,\"samples\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.waveIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"waveIndex\":1,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncBand",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncBand\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.waveDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"waveDrift\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register("gateway.models.failover.status", [this](const protocol::RequestFrame& request) {
            const std::string selectedPrimary =
                m_failoverOverrideActive
                    ? m_failoverOverrideModel
                    : m_runtimeAgentModel;
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"primary\":\"" + EscapeJsonLocal(selectedPrimary) +
                    "\",\"fallbacks\":[\"reasoner\"],\"maxRetries\":2,\"strategy\":\"ordered\",\"overrideActive\":" +
                    std::string(m_failoverOverrideActive ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.queue", [this](const protocol::RequestFrame& request) {
            const auto queued = ExtractSizeParam(request.paramsJson, "queued");
            const auto running = ExtractSizeParam(request.paramsJson, "running");
            const auto capacity = ExtractSizeParam(request.paramsJson, "capacity");

            if (queued.has_value()) {
                m_runtimeQueueDepth = queued.value();
            }

            if (running.has_value()) {
                m_runtimeRunningCount = running.value();
            }

            if (capacity.has_value() && capacity.value() > 0) {
                m_runtimeQueueCapacity = capacity.value();
            }

            if (m_runtimeRunningCount > m_runtimeQueueCapacity) {
                m_runtimeRunningCount = m_runtimeQueueCapacity;
            }

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"queued\":" + std::to_string(m_runtimeQueueDepth) +
                    ",\"running\":" + std::to_string(m_runtimeRunningCount) +
                    ",\"capacity\":" + std::to_string(m_runtimeQueueCapacity) +
                    ",\"updated\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.sample", [this](const protocol::RequestFrame& request) {
            const std::size_t chunks = m_streamingBufferedFrames > 0
                ? (std::min)(m_streamingBufferedFrames, static_cast<std::size_t>(3))
                : 2;
            const bool finalChunk = !m_streamingThrottled;

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"chunks\":[\"hello\",\"world\"],\"count\":" +
                    std::to_string(chunks) +
                    ",\"final\":" +
                    std::string(finalChunk ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.preview", [this](const protocol::RequestFrame& request) {
            std::string requested = ExtractStringParam(request.paramsJson, "model");
            if (requested.empty()) {
                requested = m_runtimeAgentModel;
            }

            const std::string selected =
                m_failoverOverrideActive
                    ? m_failoverOverrideModel
                    : requested;

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"model\":\"" + EscapeJsonLocal(requested) +
                    "\",\"attempts\":[\"" + EscapeJsonLocal(requested) +
                    "\",\"reasoner\"],\"selected\":\"" +
                    EscapeJsonLocal(selected) + "\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.assign", [this](const protocol::RequestFrame& request) {
            std::string agentId = ExtractStringParam(request.paramsJson, "agentId");
            std::string sessionId = ExtractStringParam(request.paramsJson, "sessionId");

            if (agentId.empty()) {
                agentId = m_runtimeAssignedAgentId;
            }

            if (sessionId.empty()) {
                sessionId = m_runtimeAssignedSessionId;
            }

            const bool agentExists = HasAgentId(m_agentRegistry, agentId);
            const bool sessionExists = HasSessionId(m_sessionRegistry, sessionId);
            const bool assigned = agentExists && sessionExists;

            if (assigned) {
                m_runtimeAssignedAgentId = agentId;
                m_runtimeAssignedSessionId = sessionId;
                ++m_runtimeAssignmentCount;

                if (m_runtimeQueueDepth > 0 &&
                    m_runtimeRunningCount < m_runtimeQueueCapacity) {
                    --m_runtimeQueueDepth;
                    ++m_runtimeRunningCount;
                }
            }

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"agentId\":\"" + EscapeJsonLocal(agentId) +
                    "\",\"sessionId\":\"" + EscapeJsonLocal(sessionId) +
                    "\",\"assigned\":" +
                    std::string(assigned ? "true" : "false") +
                    ",\"assignments\":" +
                    std::to_string(m_runtimeAssignmentCount) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.window", [this](const protocol::RequestFrame& request) {
            const auto windowMs = ExtractSizeParam(request.paramsJson, "windowMs");
            if (windowMs.has_value() && windowMs.value() > 0) {
                m_streamingWindowMs = windowMs.value();
            }

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"windowMs\":" + std::to_string(m_streamingWindowMs) +
                    ",\"frames\":" + std::to_string(m_streamingBufferedFrames) +
                    ",\"dropped\":0}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.metrics", [this](const protocol::RequestFrame& request) {
            const double successRate =
                m_failoverAttempts == 0
                    ? 1.0
                    : static_cast<double>(m_failoverAttempts - m_failoverFallbackHits) /
                        static_cast<double>(m_failoverAttempts);

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"attempts\":" + std::to_string(m_failoverAttempts) +
                    ",\"fallbackHits\":" + std::to_string(m_failoverFallbackHits) +
                    ",\"successRate\":" + std::to_string(successRate) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.rebalance", [this](const protocol::RequestFrame& request) {
            std::string strategy = ExtractStringParam(request.paramsJson, "strategy");
            if (strategy.empty()) {
                strategy = "sticky";
            }

            std::size_t moved = 0;
            if (m_runtimeQueueDepth > 0 &&
                m_runtimeRunningCount < m_runtimeQueueCapacity) {
                moved = 1;
                --m_runtimeQueueDepth;
                ++m_runtimeRunningCount;
            }

            ++m_runtimeRebalanceCount;

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"moved\":" + std::to_string(moved) +
                    ",\"remaining\":" +
                    std::to_string(m_runtimeQueueDepth + m_runtimeRunningCount) +
                    ",\"strategy\":\"" + EscapeJsonLocal(strategy) +
                    "\",\"rebalances\":" +
                    std::to_string(m_runtimeRebalanceCount) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.backpressure", [this](const protocol::RequestFrame& request) {
            const std::size_t pressure =
                m_streamingHighWatermark == 0
                    ? 0
                    : (m_streamingBufferedFrames * 100) / m_streamingHighWatermark;
            m_streamingThrottled = pressure >= 80;

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"pressure\":" + std::to_string(pressure) +
                    ",\"throttled\":" +
                    std::string(m_streamingThrottled ? "true" : "false") +
                    ",\"bufferedFrames\":" +
                    std::to_string(m_streamingBufferedFrames) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.simulate", [this](const protocol::RequestFrame& request) {
            std::string requested = ExtractStringParam(request.paramsJson, "requested");
            if (requested.empty()) {
                requested = m_runtimeAgentModel;
            }

            const bool useFallback =
                m_failoverOverrideActive && m_failoverOverrideModel != requested;
            const std::string resolved =
                useFallback ? m_failoverOverrideModel : requested;
            ++m_failoverAttempts;
            if (useFallback) {
                ++m_failoverFallbackHits;
            }

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"requested\":\"" + EscapeJsonLocal(requested) +
                    "\",\"resolved\":\"" + EscapeJsonLocal(resolved) +
                    "\",\"usedFallback\":" +
                    std::string(useFallback ? "true" : "false") + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.orchestration.drain", [this](const protocol::RequestFrame& request) {
            std::string reason = ExtractStringParam(request.paramsJson, "reason");
            if (reason.empty()) {
                reason = "idle";
            }

            const std::size_t drained =
                m_runtimeQueueDepth + m_runtimeRunningCount;
            m_runtimeQueueDepth = 0;
            m_runtimeRunningCount = 0;
            ++m_runtimeDrainCount;

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"drained\":" + std::to_string(drained) +
                    ",\"remaining\":0,\"reason\":\"" +
                    EscapeJsonLocal(reason) +
                    "\",\"drains\":" +
                    std::to_string(m_runtimeDrainCount) + "}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.runtime.streaming.replay", [this](const protocol::RequestFrame& request) {
            const std::size_t replayed =
                (std::min)(m_streamingBufferedFrames, static_cast<std::size_t>(2));

            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"replayed\":" + std::to_string(replayed) +
                    ",\"cursor\":\"stream-cursor-1\",\"complete\":true}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register("gateway.models.failover.audit", [](const protocol::RequestFrame& request) {
            return protocol::ResponseFrame{
                .id = request.id,
                .ok = true,
                .payloadJson =
                    "{\"entries\":2,\"lastModel\":\"default\",\"lastOutcome\":\"primary\"}",
                .error = std::nullopt,
            };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.snapshot",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"sessions\":" +
                        std::to_string(m_sessionRegistry.List().size()) +
                        ",\"agents\":" +
                        std::to_string(m_agentRegistry.List().size()) +
                        ",\"active\":\"" +
                        EscapeJsonLocal(m_runtimeAssignedSessionId) +
                        "\",\"activeAgent\":\"" +
                        EscapeJsonLocal(m_runtimeAssignedAgentId) +
                        "\",\"queue\":" +
                        std::to_string(m_runtimeQueueDepth) +
                        ",\"running\":" +
                        std::to_string(m_runtimeRunningCount) + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.cursor",
            [this](const protocol::RequestFrame& request) {
                const std::size_t lagMs =
                    m_streamingBufferedFrames * 10;
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cursor\":\"stream-cursor-1\",\"lagMs\":" +
                        std::to_string(lagMs) +
                        ",\"hasMore\":" +
                        std::string(m_streamingBufferedFrames > 0 ? "true" : "false") + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.policy",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"policy\":\"ordered\",\"maxRetries\":2,\"stickyPrimary\":true,\"overrideModel\":\"" +
                        EscapeJsonLocal(m_failoverOverrideModel) + "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.timeline",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"ticks\":[" +
                        std::to_string(m_runtimeAssignmentCount) + "," +
                        std::to_string(m_runtimeRebalanceCount) + "," +
                        std::to_string(m_runtimeDrainCount) +
                        "],\"count\":3,\"source\":\"runtime-orchestrator\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.metrics",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"frames\":" +
                        std::to_string(m_streamingBufferedFrames) +
                        ",\"bytes\":" +
                        std::to_string(m_streamingBufferedBytes) +
                        ",\"avgChunkMs\":5}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.history",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"events\":[\"primary\",\"fallback\"],\"count\":" +
                        std::to_string(m_failoverAttempts) +
                        ",\"last\":\"" +
                        std::string(m_failoverFallbackHits > 0 ? "fallback" : "primary") + "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.heartbeat",
            [this](const protocol::RequestFrame& request) {
                const std::size_t backlog =
                    m_runtimeQueueDepth + m_runtimeRunningCount;
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"alive\":true,\"intervalMs\":1000,\"jitterMs\":" +
                        std::to_string(25 + (backlog > 0 ? 5 : 0)) + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.health",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"healthy\":" +
                        std::string(m_streamingBufferedFrames <= m_streamingHighWatermark ? "true" : "false") +
                        ",\"stalls\":0,\"recoveries\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.recent",
            [this](const protocol::RequestFrame& request) {
                const std::string activeModel =
                    m_failoverOverrideActive
                        ? m_failoverOverrideModel
                        : m_runtimeAgentModel;
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"models\":[\"" + EscapeJsonLocal(m_runtimeAgentModel) +
                        "\",\"reasoner\"],\"count\":2,\"active\":\"" +
                        EscapeJsonLocal(activeModel) + "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.pulse",
            [this](const protocol::RequestFrame& request) {
                const std::size_t pulse =
                    m_runtimeAssignmentCount +
                    m_runtimeRebalanceCount +
                    m_runtimeDrainCount;
                const bool busy =
                    m_runtimeQueueDepth > 0 || m_runtimeRunningCount > 0;
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"pulse\":" + std::to_string(pulse) +
                        ",\"driftMs\":0,\"state\":\"" +
                        std::string(busy ? "active" : "steady") + "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.snapshot",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"frames\":" + std::to_string(m_streamingBufferedFrames) +
                        ",\"cursor\":\"stream-cursor-2\",\"sealed\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.window",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"windowSec\":60,\"attempts\":" +
                        std::to_string(m_failoverAttempts) +
                        ",\"fallbacks\":" +
                        std::to_string(m_failoverFallbackHits) + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.cadence",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"periodMs\":1000,\"varianceMs\":5,\"aligned\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.watermark",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"high\":" + std::to_string(m_streamingHighWatermark) +
                        ",\"low\":4,\"current\":" +
                        std::to_string(m_streamingBufferedFrames) + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.digest",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"digest\":\"sha256:failover-v1\",\"entries\":" +
                        std::to_string(m_failoverAttempts) +
                        ",\"fresh\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.beacon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"beacon\":\"orch-1\",\"seq\":1,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.checkpoint",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"checkpoint\":\"cp-1\",\"frames\":2,\"persisted\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.ledger",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"entries\":2,\"primaryHits\":1,\"fallbackHits\":1}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.epoch",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"epoch\":1,\"startedMs\":1735689600000,\"active\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.resume",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"resumed\":true,\"cursor\":\"stream-cursor-3\",\"replayed\":1}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.profile",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"profile\":\"balanced\",\"weights\":[70,30],\"version\":1}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phase",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phase\":\"steady\",\"step\":1,\"locked\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.recovery",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"recovering\":false,\"attempts\":0,\"lastMs\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.baseline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"primary\":\"default\",\"secondary\":\"reasoner\",\"confidence\":100}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.signal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"signal\":\"ok\",\"priority\":1,\"latched\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.continuity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"continuous\":true,\"gaps\":0,\"lastSeq\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.forecast",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"windowSec\":60,\"projectedFallbacks\":1,\"risk\":\"low\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.vector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"axis\":\"primary\",\"magnitude\":1,\"normalized\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.stability",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"stable\":true,\"variance\":0,\"samples\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.threshold",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"minSuccessRate\":90,\"maxFallbacks\":2,\"active\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.matrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"rows\":2,\"cols\":2,\"balanced\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.integrity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"valid\":true,\"violations\":0,\"checked\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.guardrail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"rule\":\"max_fallbacks\",\"limit\":2,\"enforced\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.lattice",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"layers\":2,\"nodes\":4,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.coherence",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"coherent\":true,\"drift\":0,\"segments\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.envelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"windowSec\":60,\"floor\":90,\"ceiling\":100}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.mesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"nodes\":4,\"edges\":3,\"connected\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.fidelity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"fidelity\":100,\"drops\":0,\"verified\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.margin",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"headroom\":10,\"buffer\":2,\"safe\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.fabric",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"threads\":6,\"links\":8,\"resilient\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.accuracy",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"accuracy\":99,\"mismatches\":0,\"calibrated\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.reserve",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"reserve\":1,\"available\":true,\"priority\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.load",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"queueLoad\":0,\"agentLoad\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.buffer",
            [this](const protocol::RequestFrame& request) {
                const auto bufferedFrames =
                    ExtractSizeParam(request.paramsJson, "bufferedFrames");
                const auto bufferedBytes =
                    ExtractSizeParam(request.paramsJson, "bufferedBytes");
                const auto highWatermark =
                    ExtractSizeParam(request.paramsJson, "highWatermark");

                if (bufferedFrames.has_value()) {
                    m_streamingBufferedFrames = bufferedFrames.value();
                }

                if (bufferedBytes.has_value()) {
                    m_streamingBufferedBytes = bufferedBytes.value();
                }

                if (highWatermark.has_value() && highWatermark.value() > 0) {
                    m_streamingHighWatermark = highWatermark.value();
                }

                m_streamingThrottled =
                    m_streamingBufferedFrames >= m_streamingHighWatermark;

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bufferedFrames\":" +
                        std::to_string(m_streamingBufferedFrames) +
                        ",\"bufferedBytes\":" +
                        std::to_string(m_streamingBufferedBytes) +
                        ",\"highWatermark\":" +
                        std::to_string(m_streamingHighWatermark) + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override",
            [this](const protocol::RequestFrame& request) {
                std::string model =
                    ExtractStringParam(request.paramsJson, "model");
                std::string reason =
                    ExtractStringParam(request.paramsJson, "reason");
                const auto activeParam =
                    ExtractBoolParam(request.paramsJson, "active");

                if (model.empty()) {
                    model = m_runtimeAgentModel;
                }

                if (reason.empty()) {
                    reason = "manual";
                }

                const bool active = activeParam.value_or(true);
                m_failoverOverrideActive = active;
                m_failoverOverrideModel = model;
                m_failoverOverrideReason = reason;
                ++m_failoverOverrideChanges;

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":" +
                        std::string(m_failoverOverrideActive ? "true" : "false") +
                        ",\"model\":\"" +
                        EscapeJsonLocal(m_failoverOverrideModel) +
                        "\",\"reason\":\"" +
                        EscapeJsonLocal(m_failoverOverrideReason) +
                        "\",\"changes\":" +
                        std::to_string(m_failoverOverrideChanges) + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.saturation",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"saturation\":0,\"capacity\":8,\"state\":\"stable\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.throttle",
            [this](const protocol::RequestFrame& request) {
                const auto throttled =
                    ExtractBoolParam(request.paramsJson, "throttled");
                const auto limitPerSec =
                    ExtractSizeParam(request.paramsJson, "limitPerSec");

                if (throttled.has_value()) {
                    m_streamingThrottled = throttled.value();
                }

                if (limitPerSec.has_value() && limitPerSec.value() > 0) {
                    m_streamingThrottleLimitPerSec = limitPerSec.value();
                }

                const std::size_t currentPerSec =
                    m_streamingWindowMs == 0
                        ? 0
                        : (m_streamingBufferedFrames * 1000) / m_streamingWindowMs;

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"throttled\":" +
                        std::string(m_streamingThrottled ? "true" : "false") +
                        ",\"limitPerSec\":" +
                        std::to_string(m_streamingThrottleLimitPerSec) +
                        ",\"currentPerSec\":" +
                        std::to_string(currentPerSec) + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.clear",
            [this](const protocol::RequestFrame& request) {
                m_failoverOverrideActive = false;
                m_failoverOverrideModel = m_runtimeAgentModel;
                m_failoverOverrideReason = "cleared";
                ++m_failoverOverrideChanges;

                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cleared\":true,\"active\":false,\"model\":\"" +
                        EscapeJsonLocal(m_failoverOverrideModel) + "\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.pressure",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"pressure\":0,\"threshold\":80,\"state\":\"normal\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.pacing",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"paceMs\":50,\"burst\":1,\"adaptive\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.status",
            [this](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":" +
                        std::string(m_failoverOverrideActive ? "true" : "false") +
                        ",\"model\":\"" +
                        EscapeJsonLocal(m_failoverOverrideModel) +
                        "\",\"reason\":\"" +
                        EscapeJsonLocal(m_failoverOverrideReason) +
                        "\",\"source\":\"runtime\",\"changes\":" +
                        std::to_string(m_failoverOverrideChanges) + "}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.headroom",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"headroom\":8,\"used\":0,\"state\":\"ready\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.jitter",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"jitterMs\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.history",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"entries\":0,\"lastModel\":\"default\",\"active\":false}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.balance",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"balanced\":true,\"skew\":0,\"state\":\"stable\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.drift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"driftMs\":0,\"windowMs\":1000,\"corrected\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.metrics",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"switches\":0,\"lastModel\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.efficiency",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"efficiency\":100,\"waste\":0,\"state\":\"optimized\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.variance",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"variance\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.window",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"windowSec\":60,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.utilization",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"utilization\":0,\"capacity\":8,\"state\":\"idle\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.deviation",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"deviation\":0,\"samples\":2,\"withinBudget\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.digest",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"digest\":\"sha256:override-v1\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.capacity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"capacity\":8,\"used\":0,\"state\":\"ready\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.alignment",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"aligned\":true,\"offsetMs\":0,\"windowMs\":1000}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.timeline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"entries\":0,\"active\":false,\"lastModel\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.occupancy",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"occupancy\":0,\"slots\":8,\"state\":\"idle\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.skew",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"skewMs\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.catalog",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"count\":1,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.elasticity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"elasticity\":100,\"headroom\":8,\"state\":\"expandable\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.dispersion",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"dispersion\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.registry",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"entries\":1,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.cohesion",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cohesion\":100,\"groups\":1,\"state\":\"coherent\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.curvature",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"curvature\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.matrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"rows\":1,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.resilience",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"resilience\":100,\"faults\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.smoothness",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"smoothness\":100,\"jitterMs\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.snapshot",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"revision\":1,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.readiness",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"ready\":true,\"queueDepth\":0,\"state\":\"ready\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.harmonics",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"harmonics\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.pointer",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"pointer\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.contention",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"contention\":0,\"waiters\":0,\"state\":\"clear\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.phase",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phase\":\"steady\",\"step\":1,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.state",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"state\":\"none\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.fairness",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"fairness\":100,\"skew\":0,\"state\":\"balanced\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.tempo",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"tempo\":1,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.profile",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"profile\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.equilibrium",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"equilibrium\":100,\"delta\":0,\"state\":\"balanced\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.steadiness",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"steady\":true,\"variance\":0,\"windowMs\":1000}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.temporal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"temporal\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.consistency",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"consistent\":true,\"deviation\":0,\"samples\":2}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.audit",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"entries\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.parity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"parity\":100,\"gap\":0,\"state\":\"aligned\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.stabilityIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"stabilityIndex\":100,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.spectral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"spectral\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.envelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"floor\":0,\"ceiling\":100,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.checkpoint",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"checkpoint\":\"cp-override-1\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.convergence",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"convergence\":100,\"drift\":0,\"state\":\"locked\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.hysteresis",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"hysteresis\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.resonance",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"resonance\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.vectorField",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"vectors\":2,\"magnitude\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.baseline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"baseline\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.balanceIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"balanceIndex\":100,\"skew\":0,\"state\":\"balanced\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseLock",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"locked\":true,\"phase\":\"steady\",\"drift\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.waveform",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"waveform\":\"flat\",\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.horizon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"horizonMs\":1000,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.manifest",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"manifest\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.symmetry",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"symmetry\":100,\"offset\":0,\"state\":\"aligned\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.gradient",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"gradient\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.vectorClock",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"clock\":1,\"lag\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.trend",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"trend\":\"flat\",\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.ledger",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"entries\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.harmonicity",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"harmonicity\":100,\"detune\":0,\"state\":\"aligned\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.inertia",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"inertia\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.coordination",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"coordinated\":true,\"lag\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.latencyBand",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"minMs\":0,\"maxMs\":0,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.snapshotIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"index\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.cadenceIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"cadenceIndex\":100,\"jitter\":0,\"state\":\"steady\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.damping",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"damping\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.phaseNoise",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseNoise\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.beat",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"beatHz\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.digestIndex",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"digestIndex\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.waveLock",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"locked\":true,\"phase\":\"steady\",\"slip\":0}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.flux",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"flux\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.phaseMatrix",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"phaseMatrix\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.orchestration.driftEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"driftEnvelope\":0,\"windowMs\":1000,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.modulation",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"modulation\":0,\"samples\":2,\"bounded\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.syncVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"syncVector\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.bandEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"bandEnvelope\":0,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.runtime.streaming.pulseTrain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"pulseHz\":1,\"samples\":2,\"stable\":true}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.cursor",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"cursor\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vector\":\"default\",\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorDrift\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.phaseBias",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"phaseBias\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.biasEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"biasEnvelope\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.driftEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"driftEnvelope\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.envelopeDrift",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"envelopeDrift\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.driftVector",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"driftVector\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorEnvelope",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorEnvelope\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorContour",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorContour\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRibbon",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRibbon\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorSpiral",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorSpiral\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorArc",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorArc\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorMesh",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorMesh\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorNode",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorNode\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorCore",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorCore\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorFrame",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorFrame\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorSpan",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorSpan\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorGrid",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorGrid\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLane",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLane\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorTrack",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorTrack\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRail",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRail\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorSpline",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorSpline\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorChain",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorChain\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorThread",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorThread\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLink",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLink\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorNode2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorNode2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorBridge",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorBridge\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorAnchor2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorAnchor2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorPortal",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorPortal\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRelay2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRelay2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorGate2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorGate2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorHub2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorHub2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorNode3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorNode3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLink2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLink2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorMesh2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorMesh2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorArc2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorArc2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorBand2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorBand2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorGrid2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorGrid2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLane2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLane2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorTrack2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorTrack2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRail2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRail2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorSpline2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorSpline2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorChain2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorChain2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorThread2",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorThread2\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLink3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLink3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorNode4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorNode4\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorMesh3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorMesh3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorBridge3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorBridge3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorPortal3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorPortal3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorRelay3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorRelay3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorGate3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorGate3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorHub3",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorHub3\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorNode5",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorNode5\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorLink4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorLink4\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorBridge4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorBridge4\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorPortal4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorPortal4\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });

        m_dispatcher.Register(
            "gateway.models.failover.override.vectorGate4",
            [](const protocol::RequestFrame& request) {
                return protocol::ResponseFrame{
                    .id = request.id,
                    .ok = true,
                    .payloadJson =
                        "{\"active\":false,\"vectorGate4\":0,\"model\":\"default\"}",
                    .error = std::nullopt,
                };
            });
    }

} // namespace blazeclaw::gateway
