#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>


class AudioRingBuffer
{
public:
    static constexpr size_t kDefaultReadSpinCount = 64;

    explicit AudioRingBuffer(size_t maxSamples)
        : buffer_(maxSamples),
          maxSamples_(maxSamples),
          writeEpoch_(0),
          committedWriteCount_(0),
          droppedSamples_(0),
          highWatermark_(0),
          writeCount_(0) {}

    void Push(const float* data, size_t count);
    void PushInterleavedPcm16(
        const int16_t* data,
        size_t frameCount,
        size_t channelCount,
        size_t channelIndex = 0);

    bool ReadLatest(std::vector<float>& out, size_t count);
    bool PeekLatest(
        std::vector<float>& out,
        size_t count,
        size_t maxSpinCount = kDefaultReadSpinCount) const;
    bool ReadWindowBySequence(
        std::vector<float>& out,
        uint64_t startSequence,
        size_t count,
        size_t maxSpinCount = kDefaultReadSpinCount) const;

    [[nodiscard]] size_t GetCapacitySamples() const { return maxSamples_; }
    [[nodiscard]] uint64_t GetDroppedSamples() const
    {
        return droppedSamples_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64_t GetHighWatermark() const
    {
        return highWatermark_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64_t GetWriteCount() const
    {
        return writeCount_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64_t GetCommittedWriteCount() const
    {
        return committedWriteCount_.load(std::memory_order_acquire);
    }
    [[nodiscard]] uint64_t GetOldestAvailableSequence() const
    {
        const uint64_t committed =
            committedWriteCount_.load(std::memory_order_acquire);
        if (committed <= static_cast<uint64_t>(maxSamples_)) {
            return 0;
        }
        return committed - static_cast<uint64_t>(maxSamples_);
    }
    [[nodiscard]] uint64_t GetLatestSequence() const
    {
        return committedWriteCount_.load(std::memory_order_acquire);
    }

private:
    bool ReadSnapshot(
        std::vector<float>& out,
        uint64_t startSequence,
        size_t count,
        size_t maxSpinCount) const;

    std::vector<float> buffer_;
    size_t maxSamples_;
    std::atomic<uint64_t> writeEpoch_;
    std::atomic<uint64_t> committedWriteCount_;
    std::atomic<uint64_t> droppedSamples_;
    std::atomic<uint64_t> highWatermark_;
    std::atomic<uint64_t> writeCount_;
};

