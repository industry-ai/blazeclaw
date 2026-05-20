#include "pch.h"
#include "Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace {
    static const char* to_string(LogLevel level) {
        switch (level) {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info:  return "INFO";
            case LogLevel::Warn:  return "WARN";
            case LogLevel::Error: return "ERROR";
            default:              return "NONE";
        }
    }

    struct ThreadContext {
        uint64_t trace_id{ 0 };
        uint64_t session_id{ 0 };
        bool has_trace{ false };
        bool has_session{ false };
    };

    thread_local ThreadContext t_ctx;

    static uint64_t to_u64_thread_id(std::thread::id id) {
        // Stable for process lifetime and printable. Not guaranteed globally unique, but fine for logs.
        return static_cast<uint64_t>(std::hash<std::thread::id>{}(id));
    }
}

Logger& Logger::Instance() {
    static Logger g;
    return g;
}

void Logger::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::GetLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::AddSink(std::shared_ptr<ILogSink> sink) {
    if (!sink) return;
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::ClearSinks() {
    std::lock_guard<std::mutex> lock(mutex_);
    sinks_.clear();
}

void Logger::SetThreadContext(uint64_t trace_id, uint64_t session_id) {
    t_ctx.trace_id = trace_id;
    t_ctx.session_id = session_id;
    t_ctx.has_trace = (trace_id != 0);
    t_ctx.has_session = (session_id != 0);
}

void Logger::ClearThreadContext() {
    t_ctx = ThreadContext{};
}

std::string Logger::FormatLine(LogLevel level, const std::string& msg,
                               const char* file, int line, const char* func) const {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto t = clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::ostringstream oss;

    // Timestamp with millisecond precision.
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms;

    // Thread id + optional correlation context.
    const uint64_t tid = to_u64_thread_id(std::this_thread::get_id());
    oss << " [tid=" << tid;
    if (t_ctx.has_trace) {
        oss << " trace=" << t_ctx.trace_id;
    }
    if (t_ctx.has_session) {
        oss << " session=" << t_ctx.session_id;
    }
    oss << "]";

    oss << " [" << to_string(level) << "] ";
    oss << msg;

    if (file && func) {
        oss << " (" << file << ":" << line << " " << func << ")";
    }

    return oss.str();
}

void Logger::Log(LogLevel level, const std::string& msg, const char* file, int line, const char* func) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (level_ == LogLevel::None || level < level_) return;

    const std::string lineStr = FormatLine(level, msg, file, line, func);

    if (sinks_.empty()) return;
    for (auto& s : sinks_) {
        s->Write(level, lineStr);
    }
}

ScopeTimer::ScopeTimer(std::string name)
    : name_(std::move(name)), start_(std::chrono::steady_clock::now()) {
}

ScopeTimer::~ScopeTimer() {
    const auto end = std::chrono::steady_clock::now();
    const auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
    LOG_DEBUG("[ScopeTimer] {} duration_ms={}", name_, dur_ms);
}