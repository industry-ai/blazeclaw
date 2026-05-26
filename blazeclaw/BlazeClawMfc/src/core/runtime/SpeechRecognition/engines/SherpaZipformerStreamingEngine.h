#pragma once

#include "../ISpeechRecognitionRuntime.h"
#include "../SpeechModelLayoutProbe.h"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace blazeclaw::core::speechrecognition::engines {

	class SherpaZipformerStreamingEngine {
	public:
		struct LoadedArtifacts {
			std::filesystem::path encoderPath;
			std::filesystem::path decoderPath;
			std::filesystem::path joinerPath;
			std::filesystem::path tokensPath;
			std::size_t tokenCount = 0;
		};

		bool Load(
			const std::filesystem::path& rootPath,
			const SpeechModelLayoutProbeResult& layout,
			std::string& outError);

		[[nodiscard]] bool IsLoaded() const;
		[[nodiscard]] const LoadedArtifacts& Artifacts() const;

		SpeechTranscribeResult TranscribeStreaming(
			const SpeechTranscribeRequest& request,
			const std::function<bool(const std::string&)>& isCancelled) const;

	private:
		[[nodiscard]] static std::size_t CountTokens(
			const std::filesystem::path& tokensPath);

		LoadedArtifacts m_artifacts;
		bool m_loaded = false;
	};

} // namespace blazeclaw::core::speechrecognition::engines
