#pragma once

#include <string>
#include <vector>

namespace blazeclaw::core::speechrecognition {

	struct SpeechCudaLoadedModule {
		std::string label;
		int major = 0;
		int expectedMajor = 0;
	};

	struct SpeechCudaCompatibilityGuardResult {
		bool compatible = true;
		std::vector<std::string> observed;
		std::vector<std::string> violations;
		std::string reason;
	};

	[[nodiscard]] SpeechCudaCompatibilityGuardResult EvaluateSpeechCudaCompatibilityGuard(
		const std::vector<SpeechCudaLoadedModule>& loadedModules);

	[[nodiscard]] SpeechCudaCompatibilityGuardResult EvaluateLoadedSpeechCudaCompatibilityGuard();

	[[nodiscard]] bool PassesSpeechCudaCompatibilityGuard(
		std::string& outReason);

} // namespace blazeclaw::core::speechrecognition
