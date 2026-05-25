#include "pch.h"
#include "AudioRingBuffer.h"

#include <algorithm>

namespace {

void UpdateAtomicMax(std::atomic<uint64_t>& target, const uint64_t value)
{
    uint64_t current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(
               current,
               value,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

float Pcm16ToFloat(const int16_t sample)
{
    return static_cast<float>(sample) / 32768.0f;
}

} // namespace

void AudioRingBuffer::Push(const float* data, size_t count)
{
    if (data == nullptr || count == 0 || maxSamples_ == 0) {
        return;
    }

    writeEpoch_.fetch_add(1, std::memory_order_acq_rel);

    const uint64_t previousCommitted =
        committedWriteCount_.load(std::memory_order_relaxed);

    const size_t retainedCount = (std::min)(count, maxSamples_);
    const size_t droppedCount = count - retainedCount;
    if (droppedCount > 0) {
        droppedSamples_.fetch_add(
            static_cast<uint64_t>(droppedCount),
            std::memory_order_relaxed);
    }

    const float* source = data + droppedCount;

    const uint64_t firstSequence =
        previousCommitted + static_cast<uint64_t>(droppedCount);
    const size_t writeIndex =
        static_cast<size_t>(firstSequence % maxSamples_);

    const size_t firstChunk =
        (std::min)(retainedCount, maxSamples_ - writeIndex);
    std::copy_n(source, firstChunk, buffer_.data() + writeIndex);

    const size_t secondChunk = retainedCount - firstChunk;
    if (secondChunk > 0) {
        std::copy_n(source + firstChunk, secondChunk, buffer_.data());
    }

    const uint64_t newCommitted =
        previousCommitted + static_cast<uint64_t>(count);

    const uint64_t availableAfterWrite =
        (std::min)(newCommitted, static_cast<uint64_t>(maxSamples_));
    UpdateAtomicMax(highWatermark_, availableAfterWrite);

    committedWriteCount_.store(newCommitted, std::memory_order_release);
    writeCount_.fetch_add(1, std::memory_order_relaxed);

    writeEpoch_.fetch_add(1, std::memory_order_release);
}

void AudioRingBuffer::PushInterleavedPcm16(
    const int16_t* data,
    size_t frameCount,
    size_t channelCount,
    size_t channelIndex)
{
    if (data == nullptr || frameCount == 0 || maxSamples_ == 0) {
        return;
    }

    if (channelCount == 0 || channelIndex >= channelCount) {
        return;
    }

    writeEpoch_.fetch_add(1, std::memory_order_acq_rel);

    const uint64_t previousCommitted =
        committedWriteCount_.load(std::memory_order_relaxed);

    const size_t retainedCount = (std::min)(frameCount, maxSamples_);
    const size_t droppedCount = frameCount - retainedCount;
    if (droppedCount > 0) {
        droppedSamples_.fetch_add(
            static_cast<uint64_t>(droppedCount),
            std::memory_order_relaxed);
    }

    const uint64_t firstSequence =
        previousCommitted + static_cast<uint64_t>(droppedCount);
    const size_t writeIndex =
        static_cast<size_t>(firstSequence % maxSamples_);

    const size_t firstChunk =
        (std::min)(retainedCount, maxSamples_ - writeIndex);
    for (size_t i = 0; i < firstChunk; ++i) {
        const size_t frame = droppedCount + i;
        buffer_[writeIndex + i] =
            Pcm16ToFloat(data[frame * channelCount + channelIndex]);
    }

    const size_t secondChunk = retainedCount - firstChunk;
    for (size_t i = 0; i < secondChunk; ++i) {
        const size_t frame = droppedCount + firstChunk + i;
        buffer_[i] = Pcm16ToFloat(data[frame * channelCount + channelIndex]);
    }

    const uint64_t newCommitted =
        previousCommitted + static_cast<uint64_t>(frameCount);
    const uint64_t availableAfterWrite =
        (std::min)(newCommitted, static_cast<uint64_t>(maxSamples_));
    UpdateAtomicMax(highWatermark_, availableAfterWrite);

    committedWriteCount_.store(newCommitted, std::memory_order_release);
    writeCount_.fetch_add(1, std::memory_order_relaxed);

    writeEpoch_.fetch_add(1, std::memory_order_release);
}

bool AudioRingBuffer::ReadLatest(std::vector<float>& out, size_t count)
{
    return PeekLatest(out, count, kDefaultReadSpinCount);
}

bool AudioRingBuffer::PeekLatest(
    std::vector<float>& out,
    size_t count,
    size_t maxSpinCount) const
{
    if (count == 0) {
        out.clear();
        return true;
    }

    if (maxSamples_ == 0 || count > maxSamples_) {
        return false;
    }

    const size_t spinBudget = maxSpinCount == 0 ? 1 : maxSpinCount;
    for (size_t spin = 0; spin < spinBudget; ++spin) {
        const uint64_t epochBefore = writeEpoch_.load(std::memory_order_acquire);
        if ((epochBefore & 1ULL) != 0) {
            continue;
        }

        const uint64_t committed =
            committedWriteCount_.load(std::memory_order_acquire);
        const uint64_t available =
            (std::min)(committed, static_cast<uint64_t>(maxSamples_));

        if (static_cast<uint64_t>(count) > available) {
            return false;
        }

        const uint64_t startSequence =
            committed - static_cast<uint64_t>(count);
        if (ReadSnapshot(out, startSequence, count, 1)) {
            const uint64_t epochAfter =
                writeEpoch_.load(std::memory_order_acquire);
            if (epochBefore == epochAfter) {
                return true;
            }
        }
    }

    return false;
}

bool AudioRingBuffer::ReadWindowBySequence(
    std::vector<float>& out,
    uint64_t startSequence,
    size_t count,
    size_t maxSpinCount) const
{
    return ReadSnapshot(out, startSequence, count, maxSpinCount);
}

bool AudioRingBuffer::ReadSnapshot(
    std::vector<float>& out,
    uint64_t startSequence,
    size_t count,
    size_t maxSpinCount) const
{
    if (count == 0) {
        out.clear();
        return true;
    }

    if (maxSamples_ == 0 || count > maxSamples_) {
        return false;
    }

    if (startSequence >
        (std::numeric_limits<uint64_t>::max)() - static_cast<uint64_t>(count)) {
        return false;
    }

    const uint64_t requestedEnd = startSequence + static_cast<uint64_t>(count);
    const size_t spinBudget = maxSpinCount == 0 ? 1 : maxSpinCount;
    for (size_t spin = 0; spin < spinBudget; ++spin) {
        const uint64_t epochBefore = writeEpoch_.load(std::memory_order_acquire);
        if ((epochBefore & 1ULL) != 0) {
            continue;
        }

        const uint64_t committed =
            committedWriteCount_.load(std::memory_order_acquire);
        const uint64_t oldestAvailable =
            committed > static_cast<uint64_t>(maxSamples_)
            ? committed - static_cast<uint64_t>(maxSamples_)
            : 0;

        if (startSequence < oldestAvailable || requestedEnd > committed) {
            return false;
        }

        const size_t readIndex =
            static_cast<size_t>(startSequence % maxSamples_);
        out.resize(count);

        const size_t firstChunk =
            (std::min)(count, maxSamples_ - readIndex);
        std::copy_n(buffer_.data() + readIndex, firstChunk, out.data());

        const size_t secondChunk = count - firstChunk;
        if (secondChunk > 0) {
            std::copy_n(buffer_.data(), secondChunk, out.data() + firstChunk);
        }

        const uint64_t epochAfter = writeEpoch_.load(std::memory_order_acquire);
        if (epochBefore == epochAfter) {
            return true;
        }
    }

    return false;
}
