#pragma once

#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

enum class LogLevel {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    None = 5
};

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(LogLevel level, const std::string& line) = 0;
};

class Logger {
public:
    static Logger& Instance();

    void SetLevel(LogLevel level);
    LogLevel GetLevel() const;

    void AddSink(std::shared_ptr<ILogSink> sink);
    void ClearSinks();

    // Optional per-thread context (for correlation).
    void SetThreadContext(uint64_t trace_id, uint64_t session_id);
    void ClearThreadContext();

    void Log(LogLevel level, const std::string& msg,
             const char* file = nullptr, int line = 0, const char* func = nullptr);

private:
    Logger() = default;

    std::string FormatLine(LogLevel level, const std::string& msg,
                           const char* file, int line, const char* func) const;

private:
    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    std::vector<std::shared_ptr<ILogSink>> sinks_;
};

// Logs a duration at scope exit (LOG_DEBUG).
// Use for cheap, localized latency tracking without changing existing log flow.
class ScopeTimer final {
public:
    explicit ScopeTimer(std::string name);
    ~ScopeTimer();

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

    ScopeTimer(ScopeTimer&&) = delete;
    ScopeTimer& operator=(ScopeTimer&&) = delete;

private:
    std::string name_;
    std::chrono::steady_clock::time_point start_;
};

// -----------------------------------------------------------------------------
// Lightweight "{}" formatter (sequential replacement).
// Supports patterns like: "a={} b={}" with arbitrary streamable args.
// -----------------------------------------------------------------------------
namespace LoggerFmt {
    inline void Append(std::ostringstream& oss, const char* s) { oss << s; }

    template <typename T>
    inline void Append(std::ostringstream& oss, T&& v) { oss << std::forward<T>(v); }

    inline std::string Format(std::string fmt) { return fmt; }

    template <typename T, typename... Ts>
    inline std::string Format(std::string fmt, T&& v, Ts&&... vs) {
        const std::string token = "{}";
        const std::size_t pos = fmt.find(token);
        if (pos == std::string::npos) {
            // No more placeholders; append remaining args space-separated.
            std::ostringstream oss;
            oss << fmt << " ";
            Append(oss, std::forward<T>(v));
            ((oss << " ", Append(oss, std::forward<Ts>(vs))), ...);
            return oss.str();
        }

        std::ostringstream oss;
        oss << fmt.substr(0, pos);
        Append(oss, std::forward<T>(v));
        oss << fmt.substr(pos + token.size());
        return Format(oss.str(), std::forward<Ts>(vs)...);
    }
}

// Convenience macros (keeps call sites short)
#define LOG_TRACE(...) ::Logger::Instance().Log(LogLevel::Trace, ::LoggerFmt::Format(__VA_ARGS__), __FILE__, __LINE__, __func__)
#define LOG_DEBUG(...) ::Logger::Instance().Log(LogLevel::Debug, ::LoggerFmt::Format(__VA_ARGS__), __FILE__, __LINE__, __func__)
#define LOG_INFO(...)  ::Logger::Instance().Log(LogLevel::Info,  ::LoggerFmt::Format(__VA_ARGS__), __FILE__, __LINE__, __func__)
#define LOG_WARN(...)  ::Logger::Instance().Log(LogLevel::Warn,  ::LoggerFmt::Format(__VA_ARGS__), __FILE__, __LINE__, __func__)
#define LOG_ERROR(...) ::Logger::Instance().Log(LogLevel::Error, ::LoggerFmt::Format(__VA_ARGS__), __FILE__, __LINE__, __func__)