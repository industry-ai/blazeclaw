#include "pch.h"
#include "StreamingAudioSourceRegistry.h"

#include <mutex>
#include <unordered_map>

namespace blazeclaw::core::speechrecognition {

	namespace {

		std::mutex g_streamingSourceMutex;
		std::unordered_map<std::string, StreamingAudioSourceReader> g_streamingSourceById;

	}

	void RegisterStreamingAudioSource(
		const std::string& streamId,
		StreamingAudioSourceReader reader) {
		if (streamId.empty() || !reader.readBySequence) {
			return;
		}

		std::lock_guard<std::mutex> lock(g_streamingSourceMutex);
		g_streamingSourceById[streamId] = std::move(reader);
	}

	void UnregisterStreamingAudioSource(const std::string& streamId) {
		if (streamId.empty()) {
			return;
		}

		std::lock_guard<std::mutex> lock(g_streamingSourceMutex);
		g_streamingSourceById.erase(streamId);
	}

	bool ReadStreamingAudioBySequence(
		const std::string& streamId,
		const std::uint64_t startSequence,
		const std::size_t sampleCount,
		std::vector<float>& outSamples) {
		StreamingReadBySequenceCallback reader;
		{
			std::lock_guard<std::mutex> lock(g_streamingSourceMutex);
			const auto it = g_streamingSourceById.find(streamId);
			if (it == g_streamingSourceById.end() || !it->second.readBySequence) {
				outSamples.clear();
				return false;
			}
			reader = it->second.readBySequence;
		}

		return reader(startSequence, sampleCount, outSamples);
	}

	std::optional<std::uint64_t> GetStreamingAudioLatestSequence(
		const std::string& streamId) {
		StreamingLatestSequenceCallback latest;
		{
			std::lock_guard<std::mutex> lock(g_streamingSourceMutex);
			const auto it = g_streamingSourceById.find(streamId);
			if (it == g_streamingSourceById.end() || !it->second.latestSequence) {
				return std::nullopt;
			}
			latest = it->second.latestSequence;
		}

		return latest();
	}

	std::optional<std::uint64_t> GetStreamingAudioOldestSequence(
		const std::string& streamId) {
		StreamingOldestSequenceCallback oldest;
		{
			std::lock_guard<std::mutex> lock(g_streamingSourceMutex);
			const auto it = g_streamingSourceById.find(streamId);
			if (it == g_streamingSourceById.end() || !it->second.oldestSequence) {
				return std::nullopt;
			}
			oldest = it->second.oldestSequence;
		}

		return oldest();
	}

} // namespace blazeclaw::core::speechrecognition
