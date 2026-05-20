#include "pch.h"
#include "TaskQueue.h"

#include <algorithm>
#include <chrono>

#include "Logger.h"
#include "config_client.h"

TaskQueue::TaskQueue() {
    // Use configured clients as a sensible capacity proxy.
    // (If you later add an explicit queue_capacity key, switch to that.)
    const int cfgClients = ConfigClient::instance().getClients();
    const int effective = std::clamp(cfgClients > 0 ? cfgClients : 64, 1, 4096);
    capacity_ = static_cast<size_t>(effective);
}

void TaskQueue::maybe_log_stats_locked_(std::size_t queue_size) {
    const uint64_t enq = enqueued_.load(std::memory_order_relaxed);
    if (enq == 0) {
        return;
    }

    if ((enq % kLogEveryN_) != 0) {
        return;
    }

    const uint64_t deq = dequeued_.load(std::memory_order_relaxed);
    const uint64_t bpush = blocked_ms_push_.load(std::memory_order_relaxed);
    const uint64_t bpop = blocked_ms_pop_.load(std::memory_order_relaxed);

    LOG_DEBUG("[TaskQueue] stats enqueued={} dequeued={} queue_size={} capacity={} blocked_ms_push={} blocked_ms_pop={}",
              enq, deq, queue_size, capacity_, bpush, bpop);
}

bool TaskQueue::push(std::shared_ptr<IoData_c> item) {
    const auto t0 = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(m_);
    cv_full_.wait(lock, [this]() { return queue_.size() < capacity_ || !running_.load(); });

    const auto t1 = std::chrono::steady_clock::now();
    blocked_ms_push_.fetch_add(
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()),
        std::memory_order_relaxed);

    if (!running_.load()) {
        // Queue stopped; do not enqueue the item.
        return false;
    }

    queue_.push_back(std::move(item));
    enqueued_.fetch_add(1, std::memory_order_relaxed);

    maybe_log_stats_locked_(queue_.size());

    lock.unlock();
    cv_.notify_one();

    return true;
}

std::shared_ptr<IoData_c> TaskQueue::pop() {
    const auto t0 = std::chrono::steady_clock::now();

    std::unique_lock<std::mutex> lock(m_);
    cv_.wait(lock, [this]() { return !queue_.empty() || !running_.load(); });

    const auto t1 = std::chrono::steady_clock::now();
    blocked_ms_pop_.fetch_add(
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()),
        std::memory_order_relaxed);

    if (!running_.load() && queue_.empty()) {
        return nullptr;
    }

    auto item = std::move(queue_.front());
    queue_.pop_front();
    dequeued_.fetch_add(1, std::memory_order_relaxed);

    // Equivalent sampling check after dequeue as well.
    maybe_log_stats_locked_(queue_.size());

    lock.unlock();
    cv_full_.notify_one();

    return item;
}

std::shared_ptr<IoData_c> TaskQueue::try_pop() {
    std::unique_lock<std::mutex> lock(m_);
    if (queue_.empty()) {
        return nullptr;
    }

    auto item = std::move(queue_.front());
    queue_.pop_front();
    dequeued_.fetch_add(1, std::memory_order_relaxed);

    // Equivalent sampling check after dequeue as well.
    maybe_log_stats_locked_(queue_.size());

    lock.unlock();
    cv_full_.notify_one();
    return item;
}

void TaskQueue::stop() {
    running_.store(false);
    cv_.notify_all();
    cv_full_.notify_all();
}

