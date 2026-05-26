#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core::speechrecognition {

	using StreamingReadBySequenceCallback = std::function<bool(
		std::uint64_t startSequence,
		std::size_t sampleCount,
		std::vector<float>& outSamples)>;

	using StreamingLatestSequenceCallback = std::function<std::uint64_t()>;
	using StreamingOldestSequenceCallback = std::function<std::uint64_t()>;

	struct StreamingAudioSourceReader {
		StreamingReadBySequenceCallback readBySequence;
		StreamingLatestSequenceCallback latestSequence;
		StreamingOldestSequenceCallback oldestSequence;
	};

	void RegisterStreamingAudioSource(
		const std::string& streamId,
		StreamingAudioSourceReader reader);

	void UnregisterStreamingAudioSource(const std::string& streamId);

	[[nodiscard]] bool ReadStreamingAudioBySequence(
		const std::string& streamId,
		std::uint64_t startSequence,
		std::size_t sampleCount,
		std::vector<float>& outSamples);

	[[nodiscard]] std::optional<std::uint64_t> GetStreamingAudioLatestSequence(
		const std::string& streamId);

	[[nodiscard]] std::optional<std::uint64_t> GetStreamingAudioOldestSequence(
		const std::string& streamId);

} // namespace blazeclaw::core::speechrecognition
