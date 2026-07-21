#pragma once

#include <functional>
#include <mutex>
#include <thread>

namespace blazeclaw::app {

class WorkerThread {
public:
    WorkerThread() = default;
    ~WorkerThread() noexcept {
        Join();
    }

    WorkerThread(const WorkerThread&) = delete;
    WorkerThread& operator=(const WorkerThread&) = delete;

    WorkerThread(WorkerThread&&) = delete;
    WorkerThread& operator=(WorkerThread&&) = delete;

    bool Start(std::function<void()> entry) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (thread_.joinable()) {
            return false;
        }
        try {
            thread_ = std::thread(std::move(entry));
            return true;
        } catch (...) {
            return false;
        }
    }

    void Join() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    bool Joinable() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return thread_.joinable();
    }

private:
    mutable std::mutex mutex_;
    std::thread thread_;
};

} // namespace blazeclaw::app