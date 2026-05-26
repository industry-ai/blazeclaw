#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace blazeclaw::core::speechrecognition {

	enum class SpeechModelLayoutKind {
		Unknown,
		QwenDecoderInitStep,
		SherpaZipformerTransducer,
	};

	struct SpeechModelLayoutProbeResult {
		SpeechModelLayoutKind kind = SpeechModelLayoutKind::Unknown;
		std::string layout = "unknown";
		std::vector<std::string> availableQwenVariants;
		std::vector<std::string> availableArtifacts;
		std::vector<std::string> missingArtifacts;
		std::filesystem::path sherpaEncoderPath;
		std::filesystem::path sherpaDecoderPath;
		std::filesystem::path sherpaJoinerPath;
		std::filesystem::path sherpaTokensPath;
	};

	[[nodiscard]] SpeechModelLayoutProbeResult ProbeSpeechModelLayout(
		const std::filesystem::path& rootPath);

} // namespace blazeclaw::core::speechrecognition
