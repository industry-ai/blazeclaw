#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace blazeclaw::irc {

class PushDispatcher {
public:
	PushDispatcher() = default;

	~PushDispatcher() noexcept {
		Stop();
	}

	PushDispatcher(const PushDispatcher&) = delete;
	PushDispatcher& operator=(const PushDispatcher&) = delete;

	PushDispatcher(PushDispatcher&&) = delete;
	PushDispatcher& operator=(PushDispatcher&&) = delete;

	bool Start() {
		std::lock_guard<std::mutex> lock(mutex_);
		if (running_) {
			return true;
		}

		stop_requested_ = false;
		running_ = true;
		try {
			worker_ = std::thread([this]() { RunLoop(); });
			return true;
		} catch (...) {
			running_ = false;
			stop_requested_ = true;
			return false;
		}
	}

	void Stop() noexcept {
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!running_) {
				return;
			}
			stop_requested_ = true;
		}

		cv_.notify_all();

		if (worker_.joinable()) {
			worker_.join();
		}

		std::lock_guard<std::mutex> lock(mutex_);
		running_ = false;
		tasks_.clear();
	}

	void Post(std::function<void()> task) {
		if (!task) {
			return;
		}

		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!running_ || stop_requested_) {
				return;
			}
			tasks_.push_back(std::move(task));
		}

		cv_.notify_one();
	}

private:
	void RunLoop() {
		for (;;) {
			std::function<void()> task;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				cv_.wait(lock, [this]() {
					return stop_requested_ || !tasks_.empty();
				});

				if (stop_requested_ && tasks_.empty()) {
					return;
				}

				task = std::move(tasks_.front());
				tasks_.pop_front();
			}

			try {
				task();
			} catch (...) {
				// Keep dispatcher alive even if callback throws.
			}
		}
	}

	std::mutex mutex_;
	std::condition_variable cv_;
	std::deque<std::function<void()>> tasks_;
	bool running_{ false };
	bool stop_requested_{ false };
	std::thread worker_;
};

} // namespace blazeclaw::irc
