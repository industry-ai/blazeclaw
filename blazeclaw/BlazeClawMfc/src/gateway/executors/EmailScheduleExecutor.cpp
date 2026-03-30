#include "pch.h"
#include "EmailScheduleExecutor.h"

#include "../ApprovalTokenStore.h"
#include "../GatewayJsonUtils.h"
#include "../GatewayPersistencePaths.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway::executors {
namespace {

std::string EscapeJson(const std::string& value) {
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

std::uint64_t CurrentEpochMs() {
    const auto now = std::chrono::system_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch())
            .count());
}

ApprovalTokenStore& SharedApprovalStore() {
    static ApprovalTokenStore store;
    static std::once_flag initFlag;

    std::call_once(initFlag, []() {
        store.Initialize(ResolveGatewayStateFilePath("approvals.json").string());
    });

    return store;
}

std::string BuildNeedsApprovalEnvelope(
    const std::string& recipient,
    const std::string& subject,
    const std::string& sendAt,
    const std::string& approvalToken,
    const std::uint64_t expiresAtEpochMs) {
    return std::string("{\"protocolVersion\":1,\"ok\":true,\"status\":\"needs_approval\",\"output\":[],\"requiresApproval\":{\"type\":\"approval_request\",\"prompt\":\"Approve outbound email scheduling?\",\"items\":[{\"to\":\"") +
        EscapeJson(recipient) +
        "\",\"subject\":\"" +
        EscapeJson(subject) +
        "\",\"sendAt\":\"" +
        EscapeJson(sendAt) +
        "\"}],\"approvalToken\":\"" +
        EscapeJson(approvalToken) +
        "\",\"approvalTokenExpiresAtEpochMs\":" +
        std::to_string(expiresAtEpochMs) +
        "}}";
}

std::string BuildResultEnvelope(
    const bool approved,
    const std::string& recipient,
    const std::string& subject,
    const std::string& sendAt) {
    if (!approved) {
        return "{\"protocolVersion\":1,\"ok\":true,\"status\":\"cancelled\",\"output\":[],\"requiresApproval\":null}";
    }

    return std::string("{\"protocolVersion\":1,\"ok\":true,\"status\":\"ok\",\"output\":[{\"summary\":{\"to\":\"") +
        EscapeJson(recipient) +
        "\",\"subject\":\"" +
        EscapeJson(subject) +
        "\",\"sendAt\":\"" +
        EscapeJson(sendAt) +
        "\",\"scheduled\":true,\"engine\":\"blazeclaw.email.scheduler.backend.v1\"}}],\"requiresApproval\":null}";
}

std::string BuildErrorEnvelope(
    const std::string& code,
    const std::string& message) {
    return std::string("{\"protocolVersion\":1,\"ok\":false,\"status\":\"error\",\"output\":[],\"requiresApproval\":null,\"error\":{\"code\":\"") +
        EscapeJson(code) +
        "\",\"message\":\"" +
        EscapeJson(message) +
        "\"}}";
}

} // namespace

GatewayToolRegistry::RuntimeToolExecutor EmailScheduleExecutor::Create() {
    return [](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
        if (!argsJson.has_value()) {
            return ToolExecuteResult{
                .tool = requestedTool,
                .executed = false,
                .status = "invalid_args",
                .output = BuildErrorEnvelope("missing_args", "missing_args"),
            };
        }

        std::string action;
        json::FindStringField(argsJson.value(), "action", action);
        if (action.empty()) {
            action = "prepare";
        }

        auto& store = SharedApprovalStore();

        if (action == "prepare") {
            std::string recipient;
            std::string subject;
            std::string body;
            std::string sendAt;
            json::FindStringField(argsJson.value(), "to", recipient);
            json::FindStringField(argsJson.value(), "subject", subject);
            json::FindStringField(argsJson.value(), "body", body);
            json::FindStringField(argsJson.value(), "sendAt", sendAt);

            if (json::Trim(recipient).empty() ||
                json::Trim(subject).empty() ||
                json::Trim(body).empty() ||
                json::Trim(sendAt).empty()) {
                return ToolExecuteResult{
                    .tool = requestedTool,
                    .executed = false,
                    .status = "invalid_args",
                    .output = BuildErrorEnvelope(
                        "to_subject_body_sendAt_required",
                        "to_subject_body_sendAt_required"),
                };
            }

            std::uint64_t ttlMinutes = 60;
            json::FindUInt64Field(argsJson.value(), "ttlMinutes", ttlMinutes);
            ttlMinutes = (std::max)(std::uint64_t{ 1 }, (std::min)(ttlMinutes, std::uint64_t{ 1440 }));

            const auto nowEpochMs = CurrentEpochMs();
            const auto expiresAtEpochMs = nowEpochMs + ttlMinutes * std::uint64_t{ 60000 };
            const std::string token =
                "email-approval-" +
                std::to_string(nowEpochMs) +
                "-" +
                std::to_string(ttlMinutes);

            const std::string payload =
                "{\"to\":\"" +
                EscapeJson(recipient) +
                "\",\"subject\":\"" +
                EscapeJson(subject) +
                "\",\"body\":\"" +
                EscapeJson(body) +
                "\",\"sendAt\":\"" +
                EscapeJson(sendAt) +
                "\"}";

            const ApprovalSessionRecord session{
                .token = token,
                .type = "email.schedule",
                .payloadJson = payload,
                .createdAtEpochMs = nowEpochMs,
                .expiresAtEpochMs = expiresAtEpochMs,
            };

            if (!store.SaveSession(session)) {
                return ToolExecuteResult{
                    .tool = requestedTool,
                    .executed = false,
                    .status = "error",
                    .output = BuildErrorEnvelope(
                        "approval_persist_failed",
                        "approval_token_persist_failed"),
                };
            }

            store.PruneExpired(nowEpochMs);

            return ToolExecuteResult{
                .tool = requestedTool,
                .executed = true,
                .status = "needs_approval",
                .output = BuildNeedsApprovalEnvelope(
                    recipient,
                    subject,
                    sendAt,
                    token,
                    expiresAtEpochMs),
            };
        }

        if (action == "approve") {
            std::string token;
            bool approve = false;
            try {
                const auto parsedArgs = nlohmann::json::parse(argsJson.value());
                if (parsedArgs.contains("approvalToken") &&
                    parsedArgs["approvalToken"].is_string()) {
                    token = parsedArgs["approvalToken"].get<std::string>();
                }
                if (parsedArgs.contains("approve") &&
                    parsedArgs["approve"].is_boolean()) {
                    approve = parsedArgs["approve"].get<bool>();
                }
                else {
                    throw std::runtime_error("approve_bool_required");
                }
            }
            catch (...) {
                return ToolExecuteResult{
                    .tool = requestedTool,
                    .executed = false,
                    .status = "invalid_args",
                    .output = BuildErrorEnvelope(
                        "approvalToken_and_approve_required",
                        "approvalToken_and_approve_required"),
                };
            }

            if (json::Trim(token).empty()) {
                return ToolExecuteResult{
                    .tool = requestedTool,
                    .executed = false,
                    .status = "invalid_args",
                    .output = BuildErrorEnvelope(
                        "approvalToken_and_approve_required",
                        "approvalToken_and_approve_required"),
                };
            }

            ApprovalSessionRecord session;
            const auto nowEpochMs = CurrentEpochMs();
            if (!store.IsTokenValid(token, nowEpochMs, &session)) {
                const auto existing = store.LoadSession(token);
                return ToolExecuteResult{
                    .tool = requestedTool,
                    .executed = false,
                    .status = "invalid_args",
                    .output = BuildErrorEnvelope(
                        existing.has_value() ? "approval_token_expired" : "approval_token_invalid",
                        existing.has_value() ? "approval_token_expired" : "approval_token_invalid"),
                };
            }

            if (session.type != "email.schedule") {
                return ToolExecuteResult{
                    .tool = requestedTool,
                    .executed = false,
                    .status = "invalid_args",
                    .output = BuildErrorEnvelope(
                        "approval_token_orphaned",
                        "approval_token_orphaned"),
                };
            }

            std::string recipient;
            std::string subject;
            std::string sendAt;
            json::FindStringField(session.payloadJson, "to", recipient);
            json::FindStringField(session.payloadJson, "subject", subject);
            json::FindStringField(session.payloadJson, "sendAt", sendAt);

            store.RemoveToken(token);

            return ToolExecuteResult{
                .tool = requestedTool,
                .executed = true,
                .status = approve ? "ok" : "cancelled",
                .output = BuildResultEnvelope(
                    approve,
                    recipient,
                    subject,
                    sendAt),
            };
        }

        return ToolExecuteResult{
            .tool = requestedTool,
            .executed = false,
            .status = "invalid_args",
            .output = BuildErrorEnvelope(
                "action_prepare_or_approve_required",
                "action_prepare_or_approve_required"),
        };
    };
}

} // namespace blazeclaw::gateway::executors
