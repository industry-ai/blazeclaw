#include "pch.h"
#include "SpeechCudaCompatibilityGuard.h"

#include <array>
#include <cwctype>
#include <filesystem>
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

		const std::vector<std::wstring>& DefaultPreloadNames() {
			static const std::vector<std::wstring> names = {
				L"cublas64_12.dll",
				L"cublasLt64_12.dll",
				L"cufft64_12.dll",
				L"cudnn64_9.dll",
				L"cudnn_graph64_9.dll",
				L"cudnn_engines_precompiled64_9.dll",
				L"cudnn_engines_runtime_compiled64_9.dll",
				L"cudnn_heuristic64_9.dll",
				L"cudnn_ops64_9.dll",
			};
			return names;
		}

		std::wstring TrimPathForLoad(const std::wstring& raw) {
			std::wstring value = raw;
			while (!value.empty() && iswspace(value.front())) {
				value.erase(value.begin());
			}
			while (!value.empty() && iswspace(value.back())) {
				value.pop_back();
			}
			if (value.size() >= 2) {
				const wchar_t first = value.front();
				const wchar_t last = value.back();
				if ((first == L'"' && last == L'"') ||
					(first == L'\'' && last == L'\'')) {
					value = value.substr(1, value.size() - 2);
				}
			}
			return value;
		}

		std::wstring JoinWideValues(
			const std::vector<std::wstring>& values,
			const wchar_t* separator) {
			std::wostringstream oss;
			for (std::size_t index = 0; index < values.size(); ++index) {
				if (index > 0) {
					oss << separator;
				}
				oss << values[index];
			}
			return oss.str();
		}

		void AppendUniqueWide(
			std::vector<std::wstring>& values,
			const std::wstring& value) {
			if (value.empty()) {
				return;
			}
			const auto normalizedValue = std::filesystem::path(value).lexically_normal().wstring();
			const auto exists = std::any_of(
				values.begin(),
				values.end(),
				[&normalizedValue](const std::wstring& existing) {
					return _wcsicmp(
						std::filesystem::path(existing).lexically_normal().c_str(),
						normalizedValue.c_str()) == 0;
				});
			if (!exists) {
				values.push_back(value);
			}
		}

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

	const std::vector<std::wstring>& DefaultSpeechCudaDllPreloadNames() {
		return DefaultPreloadNames();
	}

	SpeechCudaDllLoadResult ConfigureSpeechCudaDllLoading(
		const SpeechCudaDllLoadRequest& request) {
		SpeechCudaDllLoadResult result;
		if (!request.preloadEnabled && request.directories.empty()) {
			result.summary = L"disabled";
			return result;
		}

		result.attempted = true;
		std::vector<std::wstring> directories;
		for (const auto& rawDirectory : request.directories) {
			AppendUniqueWide(directories, TrimPathForLoad(rawDirectory));
		}

		if (directories.empty()) {
			result.succeeded = false;
			result.summary = L"no configured directories";
			result.failures.push_back(L"no configured directories");
			return result;
		}

		::SetDefaultDllDirectories(
			LOAD_LIBRARY_SEARCH_DEFAULT_DIRS |
			LOAD_LIBRARY_SEARCH_USER_DIRS);

		for (const auto& directory : directories) {
			if (!std::filesystem::is_directory(directory)) {
				result.succeeded = false;
				result.failures.push_back(L"directory not found: " + directory);
				continue;
			}

			const auto cookie = ::AddDllDirectory(directory.c_str());
			if (cookie == nullptr) {
				result.succeeded = false;
				result.failures.push_back(L"AddDllDirectory failed: " + directory);
				continue;
			}

			result.addedDirectories.push_back(directory);
		}

		if (request.preloadEnabled) {
			const auto& names = request.preloadNames.empty()
				? DefaultPreloadNames()
				: request.preloadNames;
			for (const auto& rawName : names) {
				const auto name = TrimPathForLoad(rawName);
				if (name.empty()) {
					continue;
				}

				bool found = false;
				for (const auto& directory : directories) {
					const auto fullPath = std::filesystem::path(directory) / name;
					if (!std::filesystem::exists(fullPath)) {
						continue;
					}

					found = true;
					HMODULE handle = ::LoadLibraryExW(
						fullPath.c_str(),
						nullptr,
						LOAD_WITH_ALTERED_SEARCH_PATH);
					if (handle == nullptr) {
						result.succeeded = false;
						result.failures.push_back(L"LoadLibraryEx failed: " + fullPath.wstring());
					}
					else {
						result.preloadedDlls.push_back(fullPath.wstring());
					}
					break;
				}

				if (!found) {
					result.succeeded = false;
					result.failures.push_back(L"preload DLL not found: " + name);
				}
			}
		}

		std::wostringstream summary;
		summary << L"directories=" << result.addedDirectories.size()
			<< L" preloaded=" << result.preloadedDlls.size()
			<< L" failures=" << result.failures.size();
		if (!result.addedDirectories.empty()) {
			summary << L" added=" << JoinWideValues(result.addedDirectories, L"|");
		}
		if (!result.failures.empty()) {
			summary << L" failureDetails=" << JoinWideValues(result.failures, L"|");
		}
		result.summary = summary.str();
		return result;
	}

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
