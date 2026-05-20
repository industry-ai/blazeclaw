#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

#include "IoData_c.h"

class TaskQueue {
public:
    TaskQueue();    // capacity initialized from config_client.h

    // Returns true if the item was enqueued; false if the queue is stopped.
    bool push(std::shared_ptr<IoData_c> item);
    std::shared_ptr<IoData_c> pop();
    std::shared_ptr<IoData_c> try_pop();
    void stop();

private:
    void maybe_log_stats_locked_(std::size_t queue_size);

private:
    std::deque<std::shared_ptr<IoData_c>> queue_;
    std::mutex m_;
    std::condition_variable cv_;
    std::condition_variable cv_full_;
    std::atomic<bool> running_{ true };

    // capacity read at construction time from config_client.h
    size_t capacity_{ 1 };

    // Counters (minimal noise via sampling).
    std::atomic<uint64_t> enqueued_{ 0 };
    std::atomic<uint64_t> dequeued_{ 0 };
    std::atomic<uint64_t> blocked_ms_push_{ 0 };
    std::atomic<uint64_t> blocked_ms_pop_{ 0 };

    // Sample every N operations.
    static constexpr uint64_t kLogEveryN_{ 512 };
};
