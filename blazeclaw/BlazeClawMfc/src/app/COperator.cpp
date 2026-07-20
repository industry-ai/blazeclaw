#include "pch.h"
#include "COperator.h"

#include <chrono>
#include <sstream>

namespace blazeclaw::irc {

// static
COperatorPtr COperator::Create(const std::string& nickname,
                                const std::string& username,
                                const std::string& hostname,
                                const std::string& realname,
                                uint64_t join_order) {
    return std::shared_ptr<COperator>(new COperator(nickname, username, hostname, realname, join_order));
}

void COperator::SendOperWall(const std::string& message) {
    // In a real implementation, this would broadcast to all operators
    // For now, just log it
    std::ostringstream oss;
    oss << "OPERWALL from " << GetNickname() << ": " << message;
    LogOperation("OPERWALL", GetNickname(), message);
}

std::string COperator::GetOperWallMessage() const {
    std::ostringstream oss;
    oss << "From " << GetNickname() << "@" << GetHostname() << ": ";
    return oss.str();
}

void COperator::LogOperation(const std::string& action,
                              const std::string& target,
                              const std::string& details) {
    std::lock_guard<std::mutex> lock(oper_log_mutex_);

    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    OperLogEntry entry;
    entry.timestamp_ms = static_cast<uint64_t>(ms);
    entry.action = action;
    entry.target = target;
    entry.details = details;

    oper_log_.push_back(std::move(entry));

    // Prune old entries if over limit
    while (oper_log_.size() > MAX_LOG_ENTRIES) {
        oper_log_.pop_front();
    }
}

} // namespace blazeclaw::irc
