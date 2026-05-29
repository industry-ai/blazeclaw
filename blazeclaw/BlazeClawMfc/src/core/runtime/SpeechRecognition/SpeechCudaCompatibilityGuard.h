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

	struct SpeechCudaDllLoadRequest {
		bool preloadEnabled = false;
		std::vector<std::wstring> directories;
		std::vector<std::wstring> preloadNames;
	};

	struct SpeechCudaDllLoadResult {
		bool attempted = false;
		bool succeeded = true;
		std::vector<std::wstring> addedDirectories;
		std::vector<std::wstring> preloadedDlls;
		std::vector<std::wstring> failures;
		std::wstring summary;
	};

	[[nodiscard]] const std::vector<std::wstring>& DefaultSpeechCudaDllPreloadNames();

	[[nodiscard]] SpeechCudaDllLoadResult ConfigureSpeechCudaDllLoading(
		const SpeechCudaDllLoadRequest& request);

	[[nodiscard]] SpeechCudaCompatibilityGuardResult EvaluateSpeechCudaCompatibilityGuard(
		const std::vector<SpeechCudaLoadedModule>& loadedModules);

	[[nodiscard]] SpeechCudaCompatibilityGuardResult EvaluateLoadedSpeechCudaCompatibilityGuard();

	[[nodiscard]] bool PassesSpeechCudaCompatibilityGuard(
		std::string& outReason);

} // namespace blazeclaw::core::speechrecognition
