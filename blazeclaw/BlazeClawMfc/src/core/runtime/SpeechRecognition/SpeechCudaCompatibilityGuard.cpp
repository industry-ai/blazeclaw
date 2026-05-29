#include "pch.h"
#include "SpeechCudaCompatibilityGuard.h"

#include <array>
#include <sstream>

namespace blazeclaw::core::speechrecognition {

	namespace {
		struct ExpectedCudaModule {
			const wchar_t* pattern = nullptr;
			const char* label = nullptr;
			int expectedMajor = 0;
		};

		constexpr std::array<ExpectedCudaModule, 7> kExpectedCudaModules = {{
			{ L"cublas64_%d.dll", "cublas", 12 },
			{ L"cublasLt64_%d.dll", "cublasLt", 12 },
			{ L"cufft64_%d.dll", "cufft", 12 },
			{ L"cudnn64_%d.dll", "cudnn", 9 },
			{ L"cudnn_graph64_%d.dll", "cudnn_graph", 9 },
			{ L"cudnn_engines_precompiled64_%d.dll", "cudnn_engines_precompiled", 9 },
			{ L"cudnn_engines_runtime_compiled64_%d.dll", "cudnn_engines_runtime_compiled", 9 },
		}};

		std::vector<int> DetectLoadedModuleMajors(
			const wchar_t* modulePattern,
			int minMajor,
			int maxMajor) {
			std::vector<int> majors;
			for (int major = minMajor; major <= maxMajor; ++major) {
				wchar_t moduleName[MAX_PATH]{};
				swprintf_s(moduleName, modulePattern, major);
				if (::GetModuleHandleW(moduleName) != nullptr) {
					majors.push_back(major);
				}
			}
			return majors;
		}

		std::string FormatGuardReason(
			const std::vector<std::string>& observed,
			const std::vector<std::string>& violations) {
			if (violations.empty()) {
				return {};
			}

			std::ostringstream oss;
			oss << "compatibility_guard_blocked";
			if (!observed.empty()) {
				oss << " observed=";
				for (std::size_t i = 0; i < observed.size(); ++i) {
					if (i > 0) {
						oss << ',';
					}
					oss << observed[i];
				}
			}

			oss << " violations=";
			for (std::size_t i = 0; i < violations.size(); ++i) {
				if (i > 0) {
					oss << ';';
				}
				oss << violations[i];
			}
			return oss.str();
		}
	} // namespace

	SpeechCudaCompatibilityGuardResult EvaluateSpeechCudaCompatibilityGuard(
		const std::vector<SpeechCudaLoadedModule>& loadedModules) {
		SpeechCudaCompatibilityGuardResult result;
		for (const auto& module : loadedModules) {
			result.observed.push_back(
				module.label + "=" + std::to_string(module.major));
			if (module.major != module.expectedMajor) {
				result.violations.push_back(
					module.label +
					" major=" + std::to_string(module.major) +
					" expected=" + std::to_string(module.expectedMajor));
			}
		}

		result.compatible = result.violations.empty();
		result.reason = FormatGuardReason(result.observed, result.violations);
		return result;
	}

	SpeechCudaCompatibilityGuardResult EvaluateLoadedSpeechCudaCompatibilityGuard() {
		std::vector<SpeechCudaLoadedModule> loadedModules;
		for (const auto& expected : kExpectedCudaModules) {
			const auto loadedMajors = DetectLoadedModuleMajors(
				expected.pattern,
				0,
				20);
			for (const auto major : loadedMajors) {
				loadedModules.push_back(SpeechCudaLoadedModule{
					.label = expected.label,
					.major = major,
					.expectedMajor = expected.expectedMajor,
				});
			}
		}

		return EvaluateSpeechCudaCompatibilityGuard(loadedModules);
	}

	bool PassesSpeechCudaCompatibilityGuard(std::string& outReason) {
		const auto result = EvaluateLoadedSpeechCudaCompatibilityGuard();
		outReason = result.reason;
		return result.compatible;
	}

} // namespace blazeclaw::core::speechrecognition
