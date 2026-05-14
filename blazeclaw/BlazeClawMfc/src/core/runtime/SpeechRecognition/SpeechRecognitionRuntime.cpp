#include "pch.h"
#include "SpeechRecognitionRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#if __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#define BLAZECLAW_HAS_ONNXRUNTIME 1
#else
#define BLAZECLAW_HAS_ONNXRUNTIME 0
#endif

namespace blazeclaw::core::speechrecognition {

	namespace {

#if BLAZECLAW_HAS_ONNXRUNTIME
		std::vector<int> DetectLoadedModuleMajors(
			const std::wstring& modulePattern,
			int minMajor,
			int maxMajor) {
			std::vector<int> majors;
			for (int major = minMajor; major <= maxMajor; ++major) {
				wchar_t moduleName[MAX_PATH]{};
				swprintf_s(moduleName, modulePattern.c_str(), major);
				if (::GetModuleHandleW(moduleName) != nullptr) {
					majors.push_back(major);
				}
			}
			return majors;
		}

		bool IsLoadedMajorCompatible(
			const std::wstring& modulePattern,
			const char* moduleLabel,
			int expectedMajor,
			std::vector<std::string>& violations,
			std::vector<std::string>& observed) {
			const auto loadedMajors = DetectLoadedModuleMajors(modulePattern, 0, 20);
			for (const auto major : loadedMajors) {
				observed.push_back(std::string(moduleLabel) + "=" + std::to_string(major));
			}

			for (const auto major : loadedMajors) {
				if (major != expectedMajor) {
					violations.push_back(
						std::string(moduleLabel) +
						" major=" + std::to_string(major) +
						" expected=" + std::to_string(expectedMajor));
				}
			}

			return violations.empty();
		}

		bool PassesSpeechCudaCompatibilityGuard(std::string& outReason) {
			outReason.clear();
			std::vector<std::string> violations;
			std::vector<std::string> observed;

			IsLoadedMajorCompatible(L"cublas64_%d.dll", "cublas", 12, violations, observed);
			IsLoadedMajorCompatible(L"cublasLt64_%d.dll", "cublasLt", 12, violations, observed);
			IsLoadedMajorCompatible(L"cufft64_%d.dll", "cufft", 12, violations, observed);
			IsLoadedMajorCompatible(L"cudnn64_%d.dll", "cudnn", 9, violations, observed);
			IsLoadedMajorCompatible(L"cudnn_graph64_%d.dll", "cudnn_graph", 9, violations, observed);
			IsLoadedMajorCompatible(L"cudnn_engines_precompiled64_%d.dll", "cudnn_engines_precompiled", 9, violations, observed);
			IsLoadedMajorCompatible(L"cudnn_engines_runtime_compiled64_%d.dll", "cudnn_engines_runtime_compiled", 9, violations, observed);

			if (violations.empty()) {
				return true;
			}

			std::ostringstream oss;
			oss << "compatibility_guard_blocked";
			if (!observed.empty()) {
				oss << " observed=";
				for (std::size_t i = 0; i < observed.size(); ++i) {
					if (i > 0) {
						oss << ",";
					}
					oss << observed[i];
				}
			}
			oss << " violations=";
			for (std::size_t i = 0; i < violations.size(); ++i) {
				if (i > 0) {
					oss << ";";
				}
				oss << violations[i];
			}

			outReason = oss.str();
			return false;
		}

		void ConfigureDefaultSessionOptions(
			Ort::SessionOptions& options,
			const SpeechRecognitionRuntimeSnapshot& snapshot) {
			const bool useParallelMode = snapshot.executionMode == "parallel";
			options.SetExecutionMode(
				useParallelMode
				? ExecutionMode::ORT_PARALLEL
				: ExecutionMode::ORT_SEQUENTIAL);

			if (snapshot.threads > 0) {
				options.SetIntraOpNumThreads(static_cast<int>(snapshot.threads));
				options.SetInterOpNumThreads(static_cast<int>(snapshot.threads));
			}
		}

		bool TryAppendCudaExecutionProvider(
			Ort::SessionOptions& options,
			bool& outApiAvailable,
			std::string& outReason) {
			outApiAvailable = false;
			outReason.clear();

			using AppendCudaFn = OrtStatus * (ORT_API_CALL*)(
				OrtSessionOptions*,
				int);

			HMODULE onnxRuntimeModule = ::GetModuleHandleW(L"onnxruntime.dll");
			if (onnxRuntimeModule == nullptr) {
				outReason = "onnxruntime.dll not loaded";
				return false;
			}

			const auto appendCuda = reinterpret_cast<AppendCudaFn>(
				::GetProcAddress(
					onnxRuntimeModule,
					"OrtSessionOptionsAppendExecutionProvider_CUDA"));
			if (appendCuda == nullptr) {
				outReason = "CUDA execution provider API unavailable";
				return false;
			}

			outApiAvailable = true;
			OrtStatus* status = appendCuda(
				options,
				0);
			if (status != nullptr) {
				const OrtApi& api = Ort::GetApi();
				const char* message = api.GetErrorMessage(status);
				outReason = message == nullptr
					? "CUDA provider append failed"
					: message;
				api.ReleaseStatus(status);
				return false;
			}

			return true;
		}
#endif

		constexpr std::uint32_t kDefaultSampleRate = 16000;
		constexpr std::size_t kMaxDecodeSteps = 128;
		// Budget applies to the autoregressive decode loop only (encoder is excluded).
		// decoder_init-only is slow; keep aligned with the WebView transcribe RPC budget.
		constexpr std::uint32_t kMaxDecodeWallClockMs = 90000;
		constexpr std::int64_t kTokenEndOfText = 151643;
		constexpr std::int64_t kTokenImEnd = 151645;
		constexpr std::int64_t kTokenImStart = 151644;
		constexpr std::int64_t kTokenAudioStart = 151669;
		constexpr std::int64_t kTokenAudioEnd = 151670;
		constexpr std::int64_t kTokenAudioPad = 151676;
		// ASR chat template matches `qwen3-asr-onnx` `src/prompt.py` `build_prompt_ids`
		// (validated at export time). Do not substitute nearby vocab ids — wrong role
		// tokens collapse decoding to garbage / repetition.
		constexpr std::int64_t kTokenNewline = 198;
		constexpr std::int64_t kPromptTextSystem = 9125;
		constexpr std::int64_t kPromptTextUser = 882;
		constexpr std::int64_t kPromptTextAssistant = 77091;
		// Qwen3-ASR assistant output may emit this marker token; do not treat as EOS.
		constexpr std::int64_t kTokenAsrText = 151704;

		std::size_t FindSubstringCaseInsensitive(const std::string& haystack, const std::string& needle) {
			if (needle.empty() || haystack.size() < needle.size()) {
				return std::string::npos;
			}
			for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
				bool match = true;
				for (std::size_t j = 0; j < needle.size(); ++j) {
					if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
						std::tolower(static_cast<unsigned char>(needle[j]))) {
						match = false;
						break;
					}
				}
				if (match) {
					return i;
				}
			}
			return std::string::npos;
		}

		bool StartsWithLanguageKeywordIgnoreCase(const std::string& value) {
			static const char kLit[] = "language";
			constexpr std::size_t n = sizeof(kLit) - 1;
			if (value.size() < n) {
				return false;
			}
			for (std::size_t i = 0; i < n; ++i) {
				if (std::tolower(static_cast<unsigned char>(value[i])) !=
					static_cast<unsigned char>(kLit[i])) {
					return false;
				}
			}
			return true;
		}

		bool PrefixIsQwen3AsrLanguagePreamble(const std::string& prefix) {
			std::size_t end = prefix.size();
			while (end > 0 && std::isspace(static_cast<unsigned char>(prefix[end - 1]))) {
				--end;
			}
			if (end == 0) {
				return false;
			}
			const std::string p = prefix.substr(0, end);
			if (!StartsWithLanguageKeywordIgnoreCase(p)) {
				return false;
			}
			constexpr std::size_t kLanguageLen = sizeof("language") - 1;
			std::size_t pos = kLanguageLen;
			if (pos >= p.size() || !std::isspace(static_cast<unsigned char>(p[pos]))) {
				return false;
			}
			while (pos < p.size() && std::isspace(static_cast<unsigned char>(p[pos]))) {
				++pos;
			}
			if (pos >= p.size()) {
				return false;
			}
			const std::size_t langNameStart = pos;
			while (pos < p.size() && !std::isspace(static_cast<unsigned char>(p[pos]))) {
				const unsigned char c = static_cast<unsigned char>(p[pos]);
				if (c < 0x80u) {
					++pos;
					continue;
				}
				if ((c & 0xE0u) == 0xC0u) {
					pos += (pos + 2 <= p.size()) ? 2 : 1;
					continue;
				}
				if ((c & 0xF0u) == 0xE0u) {
					pos += (pos + 3 <= p.size()) ? 3 : 1;
					continue;
				}
				if ((c & 0xF8u) == 0xF0u) {
					pos += (pos + 4 <= p.size()) ? 4 : 1;
					continue;
				}
				++pos;
			}
			if (pos <= langNameStart || pos - langNameStart < 2) {
				return false;
			}
			return pos == p.size();
		}

		void StripQwen3AsrTemplateNoise(std::string& value) {
			for (;;) {
				const std::size_t tagPos = FindSubstringCaseInsensitive(value, "<asr_text>");
				if (tagPos == std::string::npos) {
					break;
				}
				constexpr std::size_t kTagLen = 10;
				if (PrefixIsQwen3AsrLanguagePreamble(value.substr(0, tagPos))) {
					value.erase(0, tagPos + kTagLen);
				}
				else {
					value.erase(tagPos, kTagLen);
				}
				while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
					value.erase(value.begin());
				}
				while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
					value.pop_back();
				}
			}
		}

		std::string ToNarrowLocal(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const wchar_t ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			return output;
		}

		std::wstring ToWideLocal(const std::string& value) {
			if (value.empty()) {
				return {};
			}
			const int required = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0);
			if (required <= 0) {
				return std::wstring(value.begin(), value.end());
			}
			std::wstring output(static_cast<std::size_t>(required), L'\0');
			const int converted = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				required);
			if (converted <= 0) {
				return std::wstring(value.begin(), value.end());
			}
			return output;
		}

		std::string NormalizeProvider(const std::wstring& value) {
			std::string normalized = ToNarrowLocal(value.empty() ? L"onnx" : value);
			std::transform(
				normalized.begin(),
				normalized.end(),
				normalized.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return normalized;
		}

		std::string NormalizeExecutionMode(const std::wstring& value) {
			const std::string mode = ToNarrowLocal(value);
			return mode == "parallel" ? "parallel" : "sequential";
		}

		std::filesystem::path ResolveConfiguredPath(
			const std::wstring& configuredPath,
			const std::wstring& storageRoot) {
			std::filesystem::path configured = configuredPath.empty()
				? std::filesystem::path(storageRoot)
				: std::filesystem::path(configuredPath);
			if (configured.empty()) {
				return {};
			}

			if (configured.is_absolute()) {
				return configured.lexically_normal();
			}

			std::error_code ec;
			const std::filesystem::path configDirectory =
				std::filesystem::current_path(ec);
			ec.clear();

			std::filesystem::path executableDirectory;
			std::array<wchar_t, MAX_PATH> modulePath{};
			const DWORD moduleLength = ::GetModuleFileNameW(
				nullptr,
				modulePath.data(),
				static_cast<DWORD>(modulePath.size()));
			if (moduleLength > 0) {
				executableDirectory = std::filesystem::path(
					std::wstring(modulePath.data(), moduleLength))
					.parent_path();
			}

			std::vector<std::filesystem::path> candidates;
			candidates.reserve(12);

			auto appendWithAncestors = [&candidates](
				const std::filesystem::path& base,
				const std::filesystem::path& relativePath) {
					if (base.empty()) {
						return;
					}

					std::filesystem::path cursor = base;
					while (!cursor.empty()) {
						candidates.push_back(cursor / relativePath);
						if (!cursor.has_parent_path()) {
							break;
						}

						const std::filesystem::path parent = cursor.parent_path();
						if (parent == cursor) {
							break;
						}

						cursor = parent;
					}
				};

			appendWithAncestors(configDirectory, configured);
			appendWithAncestors(executableDirectory, configured);
			candidates.push_back(configured);

			for (const auto& candidate : candidates) {
				const std::filesystem::path normalized = candidate.lexically_normal();
				ec.clear();
				if (std::filesystem::exists(normalized, ec) && !ec) {
					return normalized;
				}
			}

			return candidates.empty() ? configured.lexically_normal() :
				candidates.front().lexically_normal();
		}

		std::uint32_t ReadLe32(const std::uint8_t* ptr) {
			return static_cast<std::uint32_t>(ptr[0]) |
				(static_cast<std::uint32_t>(ptr[1]) << 8) |
				(static_cast<std::uint32_t>(ptr[2]) << 16) |
				(static_cast<std::uint32_t>(ptr[3]) << 24);
		}

		std::uint16_t ReadLe16(const std::uint8_t* ptr) {
			return static_cast<std::uint16_t>(ptr[0]) |
				(static_cast<std::uint16_t>(ptr[1]) << 8);
		}

		float HannWindow(std::size_t idx, std::size_t n) {
			if (n <= 1) {
				return 1.0f;
			}
			const float angle = static_cast<float>(2.0 * 3.14159265358979323846 * idx / (n - 1));
			return 0.5f - 0.5f * std::cos(angle);
		}

		float HzToMelSlaney(const float hz) {
			constexpr float kFSp = 200.0f / 3.0f;
			constexpr float kMinLogHz = 1000.0f;
			constexpr float kMinLogMel = kMinLogHz / kFSp;
			constexpr float kLogStep = 0.06875177742094912f;
			if (hz < kMinLogHz) {
				return hz / kFSp;
			}
			return kMinLogMel + std::log(hz / kMinLogHz) / kLogStep;
		}

		float MelToHzSlaney(const float mel) {
			constexpr float kFSp = 200.0f / 3.0f;
			constexpr float kMinLogHz = 1000.0f;
			constexpr float kMinLogMel = kMinLogHz / kFSp;
			constexpr float kLogStep = 0.06875177742094912f;
			if (mel < kMinLogMel) {
				return mel * kFSp;
			}
			return kMinLogHz * std::exp(kLogStep * (mel - kMinLogMel));
		}

		std::vector<float> BuildMelFilterbank(
			std::uint32_t sampleRate,
			std::size_t nFft,
			std::size_t nMels,
			float fMin,
			float fMax) {
			const std::size_t nFreqBins = (nFft / 2) + 1;
			std::vector<float> filters(nMels * nFreqBins, 0.0f);
			const float melMin = HzToMelSlaney(fMin);
			const float melMax = HzToMelSlaney(fMax);
			std::vector<float> melPoints(nMels + 2, 0.0f);
			for (std::size_t i = 0; i < melPoints.size(); ++i) {
				melPoints[i] = melMin + (melMax - melMin) * static_cast<float>(i) /
					static_cast<float>(melPoints.size() - 1);
			}
			std::vector<float> hzPoints(melPoints.size(), 0.0f);
			for (std::size_t i = 0; i < melPoints.size(); ++i) {
				hzPoints[i] = MelToHzSlaney(melPoints[i]);
			}

			for (std::size_t m = 1; m <= nMels; ++m) {
				const float leftHz = hzPoints[m - 1];
				const float centerHz = hzPoints[m];
				const float rightHz = hzPoints[m + 1];
				if (centerHz <= leftHz || rightHz <= centerHz) {
					continue;
				}
				const float enorm = 2.0f / (rightHz - leftHz);
				for (std::size_t k = 0; k < nFreqBins; ++k) {
					const float hz = static_cast<float>(sampleRate) * static_cast<float>(k) / static_cast<float>(nFft);
					float weight = 0.0f;
					if (hz >= leftHz && hz < centerHz) {
						weight = (hz - leftHz) / (centerHz - leftHz);
					}
					else if (hz >= centerHz && hz <= rightHz) {
						weight = (rightHz - hz) / (rightHz - centerHz);
					}
					filters[(m - 1) * nFreqBins + k] = weight * enorm;
				}
			}
			return filters;
		}

		std::unordered_map<char32_t, std::uint8_t> BuildByteLevelCharToByteMap() {
			std::vector<int> bytes;
			for (int b = static_cast<int>('!'); b <= static_cast<int>('~'); ++b) {
				bytes.push_back(b);
			}
			for (int b = 0xA1; b <= 0xAC; ++b) {
				bytes.push_back(b);
			}
			for (int b = 0xAE; b <= 0xFF; ++b) {
				bytes.push_back(b);
			}

			std::vector<int> chars = bytes;
			int n = 0;
			for (int b = 0; b < 256; ++b) {
				if (std::find(bytes.begin(), bytes.end(), b) != bytes.end()) {
					continue;
				}
				bytes.push_back(b);
				chars.push_back(256 + n);
				++n;
			}

			std::unordered_map<char32_t, std::uint8_t> map;
			for (std::size_t i = 0; i < bytes.size() && i < chars.size(); ++i) {
				map[static_cast<char32_t>(chars[i])] = static_cast<std::uint8_t>(bytes[i]);
			}
			return map;
		}

		std::vector<char32_t> DecodeUtf8Codepoints(const std::string& value) {
			std::vector<char32_t> codepoints;
			for (std::size_t i = 0; i < value.size();) {
				const unsigned char lead = static_cast<unsigned char>(value[i]);
				if (lead < 0x80) {
					codepoints.push_back(static_cast<char32_t>(lead));
					++i;
					continue;
				}

				std::size_t count = 0;
				char32_t cp = 0;
				if ((lead & 0xE0) == 0xC0) {
					count = 2;
					cp = lead & 0x1F;
				}
				else if ((lead & 0xF0) == 0xE0) {
					count = 3;
					cp = lead & 0x0F;
				}
				else if ((lead & 0xF8) == 0xF0) {
					count = 4;
					cp = lead & 0x07;
				}
				else {
					codepoints.push_back(static_cast<char32_t>(lead));
					++i;
					continue;
				}

				if (i + count > value.size()) {
					codepoints.push_back(static_cast<char32_t>(lead));
					++i;
					continue;
				}

				bool valid = true;
				for (std::size_t j = 1; j < count; ++j) {
					const unsigned char tail = static_cast<unsigned char>(value[i + j]);
					if ((tail & 0xC0) != 0x80) {
						valid = false;
						break;
					}
					cp = (cp << 6) | static_cast<char32_t>(tail & 0x3F);
				}
				if (!valid) {
					codepoints.push_back(static_cast<char32_t>(lead));
					++i;
					continue;
				}

				codepoints.push_back(cp);
				i += count;
			}
			return codepoints;
		}

		void AppendUtf8Codepoint(char32_t cp, std::string& output) {
			if (cp > 0x10FFFFu) {
				return;
			}
			if (cp <= 0x7Fu) {
				output.push_back(static_cast<char>(cp));
				return;
			}
			if (cp <= 0x7FFu) {
				output.push_back(static_cast<char>(0xC0u | ((cp >> 6) & 0x1Fu)));
				output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
				return;
			}
			if (cp <= 0xFFFFu) {
				output.push_back(static_cast<char>(0xE0u | ((cp >> 12) & 0x0Fu)));
				output.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
				output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
				return;
			}
			output.push_back(static_cast<char>(0xF0u | ((cp >> 18) & 0x07u)));
			output.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
			output.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
			output.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
		}

		std::string DecodeByteLevelToken(
			const std::string& token,
			const std::unordered_map<char32_t, std::uint8_t>& byteMap) {
			if (token.empty()) {
				return token;
			}
			std::string output;
			const auto cps = DecodeUtf8Codepoints(token);
			output.reserve(cps.size());
			for (const auto cp : cps) {
				const auto it = byteMap.find(cp);
				if (it != byteMap.end()) {
					output.push_back(static_cast<char>(it->second));
					continue;
				}
				if (cp <= 0x7F) {
					output.push_back(static_cast<char>(cp));
					continue;
				}
				// Literal CJK (etc.) merged with byte-level prefix in one vocab piece.
				AppendUtf8Codepoint(cp, output);
			}
			return output;
		}

	} // namespace

	struct SpeechRecognitionRuntime::SessionState {
#if BLAZECLAW_HAS_ONNXRUNTIME
		std::unique_ptr<Ort::Env> env;
		std::unique_ptr<Ort::SessionOptions> options;
		std::unique_ptr<Ort::Session> encoder;
		std::unique_ptr<Ort::Session> decoderInit;
		std::unique_ptr<Ort::Session> decoderStep;
#endif
		std::unordered_map<std::int64_t, std::string> tokenById;
		std::unordered_set<std::int64_t> specialTokenIds;
		std::unordered_map<char32_t, std::uint8_t> byteLevelCharToByte;
		std::string tokenizerMode = "vocab_fallback";
		bool initialized = false;
	};

	SpeechRecognitionRuntime::SpeechRecognitionRuntime() = default;
	SpeechRecognitionRuntime::~SpeechRecognitionRuntime() = default;

	void SpeechRecognitionRuntime::Configure(const blazeclaw::config::AppConfig& appConfig) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config = appConfig;
		ResetSnapshotLocked();
	}

	SpeechRecognitionRuntimeSnapshot SpeechRecognitionRuntime::Snapshot() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

	void SpeechRecognitionRuntime::ResetSnapshotLocked() {
		m_sessionState = std::make_unique<SessionState>();
		{
			std::lock_guard<std::mutex> cancelLock(m_cancelMutex);
			m_cancelFlagsByRunId.clear();
		}
		m_snapshot = SpeechRecognitionRuntimeSnapshot{};
		m_snapshot.enabled = m_config.speechRecognition.enabled;
		m_snapshot.provider = NormalizeProvider(m_config.speechRecognition.provider);
		m_snapshot.modelPath = ToNarrow(m_config.speechRecognition.modelPath);
		m_snapshot.language = ToNarrow(m_config.speechRecognition.language);
		m_snapshot.sampleRate = m_config.speechRecognition.sampleRate;
		m_snapshot.threads = m_config.speechRecognition.threads;
		m_snapshot.executionMode = NormalizeExecutionMode(m_config.speechRecognition.executionMode);
		m_snapshot.cudaExecutionProviderAvailable = false;
		m_snapshot.cudaExecutionProviderEnabled = false;
		m_snapshot.cudaExecutionProviderReason.clear();
		m_snapshot.effectiveExecutionProvider = "cpu";
		m_snapshot.verboseMetrics = m_config.speechRecognition.verboseMetrics;
		m_snapshot.status = "configured";
	}

	bool SpeechRecognitionRuntime::EnsureLoadedLocked(SpeechTranscribeResult& outResult) {
		if (!m_sessionState) {
			m_sessionState = std::make_unique<SessionState>();
		}

		if (!m_snapshot.enabled) {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::SpeechRecognitionDisabled,
				.message = "speech recognition is disabled",
			};
			m_snapshot.ready = false;
			m_snapshot.status = "disabled";
			m_snapshot.error = outResult.error;
			return false;
		}

		if (m_snapshot.provider != "onnx") {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::ProviderNotSupported,
				.message = "only onnx provider is supported",
			};
			m_snapshot.ready = false;
			m_snapshot.status = "unsupported_provider";
			m_snapshot.error = outResult.error;
			return false;
		}

		const std::filesystem::path rootPath = ResolveConfiguredPath(
			m_config.speechRecognition.modelPath,
			m_config.speechRecognition.storageRoot);
		m_snapshot.modelPath = ToNarrow(rootPath.wstring());
		if (rootPath.empty() || !std::filesystem::exists(rootPath)) {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::ModelNotFound,
				.message = "speech model path is not available",
			};
			m_snapshot.ready = false;
			m_snapshot.status = "model_missing";
			m_snapshot.error = outResult.error;
			return false;
		}

		const bool preferInt4 =
			std::filesystem::exists(rootPath / L"decoder_init.int4.onnx") &&
			std::filesystem::exists(rootPath / L"encoder.int4.onnx");
		const std::filesystem::path encoderPath = preferInt4
			? (rootPath / L"encoder.int4.onnx")
			: (rootPath / L"encoder.onnx");
		const std::filesystem::path decoderInitPath = preferInt4
			? (rootPath / L"decoder_init.int4.onnx")
			: (rootPath / L"decoder_init.onnx");
		const std::filesystem::path decoderStepPath = preferInt4
			? (rootPath / L"decoder_step.int4.onnx")
			: (rootPath / L"decoder_step.onnx");
		const std::filesystem::path tokenizerJsonPath = rootPath / L"tokenizer.json";
		const std::filesystem::path fallbackVocabPath = rootPath / L"vocab.json";

		if (!std::filesystem::exists(encoderPath) ||
			!std::filesystem::exists(decoderInitPath) ||
			(!std::filesystem::exists(tokenizerJsonPath) && !std::filesystem::exists(fallbackVocabPath))) {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::ModelNotFound,
				.message = "required ASR model artifacts are missing (encoder/decoder_init/tokenizer)",
			};
			m_snapshot.ready = false;
			m_snapshot.status = "model_artifact_missing";
			m_snapshot.error = outResult.error;
			return false;
		}

		m_snapshot.modelVariant = preferInt4 ? "int4" : "fp32";
		m_snapshot.encoderModelPath = ToNarrow(encoderPath.wstring());
		m_snapshot.decoderInitModelPath = ToNarrow(decoderInitPath.wstring());
		m_snapshot.decoderStepModelPath = std::filesystem::exists(decoderStepPath)
			? ToNarrow(decoderStepPath.wstring())
			: std::string();
		m_snapshot.tokenizerPath = std::filesystem::exists(tokenizerJsonPath)
			? ToNarrow(tokenizerJsonPath.wstring())
			: ToNarrow(fallbackVocabPath.wstring());

#if !BLAZECLAW_HAS_ONNXRUNTIME
		outResult.ok = false;
		outResult.error = SpeechRecognitionError{
			.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
			.message = "onnxruntime headers not available at compile time",
		};
		m_snapshot.ready = false;
		m_snapshot.status = "runtime_unavailable";
		m_snapshot.error = outResult.error;
		return false;
#else
		if (m_sessionState->initialized) {
			return true;
		}

		try {
			m_sessionState->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "blazeclaw-speech");
			m_sessionState->options = std::make_unique<Ort::SessionOptions>();
			ConfigureDefaultSessionOptions(*m_sessionState->options, m_snapshot);

			bool usingCuda = false;
			bool cudaApiAvailable = false;
			std::string cudaFallbackReason;
			if (!m_config.speechRecognition.cudaEnabled) {
				m_snapshot.cudaExecutionProviderAvailable = false;
				m_snapshot.cudaExecutionProviderEnabled = false;
				m_snapshot.cudaExecutionProviderReason = "disabled_by_config";
				m_snapshot.effectiveExecutionProvider = "cpu";
			}
			else if (m_cudaCompatibilityGuardLatched) {
				m_snapshot.cudaExecutionProviderAvailable = false;
				m_snapshot.cudaExecutionProviderEnabled = false;
				m_snapshot.cudaExecutionProviderReason =
					"compatibility_guard_latched_in_process: " +
					(m_cudaCompatibilityGuardLatchedReason.empty()
						? std::string("previous_guard_failure")
						: m_cudaCompatibilityGuardLatchedReason);
				m_snapshot.effectiveExecutionProvider = "cpu";
				TraceRuntime(
					"runtime.execution_provider.compatibility_guard.latch",
					std::string(),
					"state=latched action=skip_cuda_ep_append reason=" +
					m_snapshot.cudaExecutionProviderReason);
			}
			else if (!PassesSpeechCudaCompatibilityGuard(cudaFallbackReason)) {
				m_snapshot.cudaExecutionProviderAvailable = false;
				m_snapshot.cudaExecutionProviderEnabled = false;
				m_snapshot.cudaExecutionProviderReason = cudaFallbackReason;
				m_snapshot.effectiveExecutionProvider = "cpu";
				m_cudaCompatibilityGuardLatched = true;
				m_cudaCompatibilityGuardLatchedReason = cudaFallbackReason;
				TraceRuntime(
					"runtime.execution_provider.compatibility_guard.blocked",
					std::string(),
					"state=latched action=force_cpu reason=" +
					m_snapshot.cudaExecutionProviderReason);
			}
			else if (TryAppendCudaExecutionProvider(
				*m_sessionState->options,
				cudaApiAvailable,
				cudaFallbackReason)) {
				usingCuda = true;
				m_snapshot.cudaExecutionProviderAvailable = true;
				m_snapshot.cudaExecutionProviderEnabled = true;
				m_snapshot.cudaExecutionProviderReason = "active";
				m_snapshot.effectiveExecutionProvider = "cuda";
			}
			else {
				m_snapshot.cudaExecutionProviderAvailable = cudaApiAvailable;
				m_snapshot.cudaExecutionProviderEnabled = false;
				m_snapshot.cudaExecutionProviderReason = cudaFallbackReason;
				m_snapshot.effectiveExecutionProvider = "cpu";
			}
			TraceRuntime(
				"runtime.execution_provider",
				std::string(),
				"provider=" + m_snapshot.effectiveExecutionProvider +
				" cudaAvailable=" + (m_snapshot.cudaExecutionProviderAvailable ? std::string("true") : std::string("false")) +
				" cudaEnabled=" + (m_snapshot.cudaExecutionProviderEnabled ? std::string("true") : std::string("false")) +
				" reason=" + (m_snapshot.cudaExecutionProviderReason.empty() ? std::string("none") : m_snapshot.cudaExecutionProviderReason));

			try {
				m_sessionState->encoder = std::make_unique<Ort::Session>(
					*m_sessionState->env,
					encoderPath.c_str(),
					*m_sessionState->options);
				m_sessionState->decoderInit = std::make_unique<Ort::Session>(
					*m_sessionState->env,
					decoderInitPath.c_str(),
					*m_sessionState->options);
			}
			catch (const std::exception& ex) {
				if (!usingCuda) {
					throw;
				}

				m_snapshot.cudaExecutionProviderEnabled = false;
				m_snapshot.cudaExecutionProviderReason =
					"cuda_session_init_failed: " + std::string(ex.what());
				m_snapshot.effectiveExecutionProvider = "cpu";
				TraceRuntime(
					"runtime.execution_provider",
					std::string(),
					"provider=cpu cudaEnabled=false reason=" + m_snapshot.cudaExecutionProviderReason);

				m_sessionState->options = std::make_unique<Ort::SessionOptions>();
				ConfigureDefaultSessionOptions(*m_sessionState->options, m_snapshot);
				m_sessionState->encoder = std::make_unique<Ort::Session>(
					*m_sessionState->env,
					encoderPath.c_str(),
					*m_sessionState->options);
				m_sessionState->decoderInit = std::make_unique<Ort::Session>(
					*m_sessionState->env,
					decoderInitPath.c_str(),
					*m_sessionState->options);
			}

			if (std::filesystem::exists(decoderStepPath)) {
				m_sessionState->decoderStep = std::make_unique<Ort::Session>(
					*m_sessionState->env,
					decoderStepPath.c_str(),
					*m_sessionState->options);
				TraceRuntime(
					"runtime.decoder_step.loaded_but_disabled",
					std::string(),
					"reason=requires_input_embeds_and_kv_cache_support before safe runtime use");
			}

			m_sessionState->tokenById.clear();
			m_sessionState->specialTokenIds.clear();
			m_sessionState->byteLevelCharToByte = BuildByteLevelCharToByteMap();
			m_sessionState->tokenizerMode = "vocab_fallback";

			bool tokenizerLoaded = false;
			if (std::filesystem::exists(tokenizerJsonPath)) {
				std::ifstream tokenizerInput(tokenizerJsonPath, std::ios::in | std::ios::binary);
				if (tokenizerInput.is_open()) {
					std::stringstream tokenizerBuffer;
					tokenizerBuffer << tokenizerInput.rdbuf();
					const auto tokenizerJson = nlohmann::json::parse(tokenizerBuffer.str(), nullptr, true);
					if (tokenizerJson.is_object()) {
						const auto modelIt = tokenizerJson.find("model");
						if (modelIt != tokenizerJson.end() && modelIt->is_object()) {
							const auto vocabIt = modelIt->find("vocab");
							if (vocabIt != modelIt->end() && vocabIt->is_object()) {
								for (auto it = vocabIt->begin(); it != vocabIt->end(); ++it) {
									if (!it.value().is_number_integer()) {
										continue;
									}
									m_sessionState->tokenById.insert_or_assign(
										it.value().get<std::int64_t>(),
										it.key());
								}
							}
						}

						const auto addedIt = tokenizerJson.find("added_tokens");
						if (addedIt != tokenizerJson.end() && addedIt->is_array()) {
							for (const auto& added : *addedIt) {
								if (!added.is_object()) {
									continue;
								}
								const auto idIt = added.find("id");
								const auto contentIt = added.find("content");
								if (idIt == added.end() || !idIt->is_number_integer() ||
									contentIt == added.end() || !contentIt->is_string()) {
									continue;
								}
								const auto tokenId = idIt->get<std::int64_t>();
								m_sessionState->tokenById.insert_or_assign(tokenId, contentIt->get<std::string>());
								const auto specialIt = added.find("special");
								if (specialIt != added.end() && specialIt->is_boolean() && specialIt->get<bool>()) {
									m_sessionState->specialTokenIds.insert(tokenId);
								}
							}
						}
					}
				}
				if (!m_sessionState->tokenById.empty()) {
					tokenizerLoaded = true;
					m_sessionState->tokenizerMode = "tokenizer_json";
				}
			}

			if (!tokenizerLoaded && std::filesystem::exists(fallbackVocabPath)) {
				std::ifstream vocabInput(fallbackVocabPath, std::ios::in | std::ios::binary);
				if (vocabInput.is_open()) {
					std::stringstream vocabBuffer;
					vocabBuffer << vocabInput.rdbuf();
					const auto vocabJson = nlohmann::json::parse(vocabBuffer.str(), nullptr, true);
					if (!vocabJson.is_object()) {
						throw std::runtime_error("vocab.json is not a JSON object");
					}
					for (auto it = vocabJson.begin(); it != vocabJson.end(); ++it) {
						if (!it.value().is_number_integer()) {
							continue;
						}
						m_sessionState->tokenById.insert_or_assign(
							it.value().get<std::int64_t>(),
							it.key());
					}
				}
			}

			m_sessionState->specialTokenIds.insert(kTokenEndOfText);
			m_sessionState->specialTokenIds.insert(kTokenImStart);
			m_sessionState->specialTokenIds.insert(kTokenImEnd);
			if (m_sessionState->tokenById.empty()) {
				throw std::runtime_error("tokenizer produced an empty token map");
			}

			m_sessionState->initialized = true;
			m_snapshot.ready = true;
			m_snapshot.status = "ready";
			m_snapshot.error = std::nullopt;
			return true;
		}
		catch (const std::exception& ex) {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::ModelLoadFailed,
				.message = ex.what(),
			};
			m_snapshot.ready = false;
			m_snapshot.status = "model_load_failed";
			m_snapshot.error = outResult.error;
			return false;
		}
#endif
	}

	bool SpeechRecognitionRuntime::LoadModel() {
		std::lock_guard<std::mutex> lock(m_mutex);
		++m_snapshot.modelLoadAttempts;
		SpeechTranscribeResult result;
		if (!EnsureLoadedLocked(result)) {
			++m_snapshot.modelLoadFailures;
			return false;
		}
		return true;
	}

	SpeechTranscribeResult SpeechRecognitionRuntime::Transcribe(const SpeechTranscribeRequest& request) {
		std::lock_guard<std::mutex> lock(m_mutex);
		SpeechTranscribeResult result;
		result.sessionState.sessionId = request.sessionId;
		result.sessionState.runId = request.runId;
		result.sessionState.audioPath = request.audioPath;
		result.sessionState.language = request.language.empty() ? ToNarrow(m_config.speechRecognition.language) : request.language;
		result.sessionState.stage = SpeechSessionStage::Transcribing;
		result.sessionState.segment = std::nullopt;
		const auto startedAt = std::chrono::steady_clock::now();
		++m_snapshot.transcribeRequestsStarted;
		TraceRuntime(
			"transcribe.request.accepted",
			request.runId,
			"sessionId=" + request.sessionId + " audioPath=" + request.audioPath);

		if (request.audioPath.empty()) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InvalidInput,
				.message = "audioPath is required for ASR transcription",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			++m_snapshot.transcribeRequestsFailed;
			m_snapshot.status = "invalid_input";
			m_snapshot.error = result.error;
			return result;
		}

		if (!EnsureLoadedLocked(result)) {
			++m_snapshot.transcribeRequestsFailed;
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}

		const std::wstring audioPathWide = ToWideLocal(request.audioPath);
		if (!std::filesystem::exists(audioPathWide)) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::AudioNotFound,
				.message = "audio path not found",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			++m_snapshot.transcribeRequestsFailed;
			m_snapshot.status = "audio_missing";
			m_snapshot.error = result.error;
			return result;
		}

#if !BLAZECLAW_HAS_ONNXRUNTIME
		result.ok = false;
		result.error = SpeechRecognitionError{
			.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
			.message = "onnxruntime is unavailable",
		};
		++m_snapshot.transcribeRequestsFailed;
		m_snapshot.status = "runtime_unavailable";
		m_snapshot.error = result.error;
		return result;
#else
		auto isCancelled = [this, &request]() {
			if (request.runId.empty()) {
				return false;
			}
			std::lock_guard<std::mutex> cancelLock(m_cancelMutex);
			const auto it = m_cancelFlagsByRunId.find(request.runId);
			return it != m_cancelFlagsByRunId.end() && it->second;
		};

		struct ParsedWave {
			std::vector<float> samples;
			std::uint32_t sampleRate = 0;
			std::uint16_t channels = 0;
		};

		auto parseWave = [](const std::wstring& path, std::string& error) -> std::optional<ParsedWave> {
			std::ifstream input(path, std::ios::in | std::ios::binary);
			if (!input.is_open()) {
				error = "failed to open audio file";
				return std::nullopt;
			}
			std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			if (bytes.size() < 44) {
				error = "wav file too small";
				return std::nullopt;
			}
			if (std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
				std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
				error = "only RIFF/WAVE audio is supported";
				return std::nullopt;
			}

			std::size_t cursor = 12;
			std::uint16_t audioFormat = 0;
			std::uint16_t channels = 0;
			std::uint32_t sampleRate = 0;
			std::uint16_t bitsPerSample = 0;
			std::size_t dataOffset = 0;
			std::size_t dataSize = 0;
			while (cursor + 8 <= bytes.size()) {
				const char* chunkId = reinterpret_cast<const char*>(bytes.data() + cursor);
				const std::uint32_t chunkSize = ReadLe32(bytes.data() + cursor + 4);
				cursor += 8;
				if (cursor + chunkSize > bytes.size()) {
					break;
				}
				if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16) {
					audioFormat = ReadLe16(bytes.data() + cursor + 0);
					channels = ReadLe16(bytes.data() + cursor + 2);
					sampleRate = ReadLe32(bytes.data() + cursor + 4);
					bitsPerSample = ReadLe16(bytes.data() + cursor + 14);
				}
				else if (std::memcmp(chunkId, "data", 4) == 0) {
					dataOffset = cursor;
					dataSize = chunkSize;
				}
				cursor += chunkSize + (chunkSize % 2);
			}

			if (dataOffset == 0 || dataSize == 0 || sampleRate == 0 || channels == 0) {
				error = "wav missing fmt/data chunk";
				return std::nullopt;
			}

			ParsedWave parsed;
			parsed.sampleRate = sampleRate;
			parsed.channels = channels;
			const std::size_t frameSizeBytes = static_cast<std::size_t>(channels) * (bitsPerSample / 8);
			if (frameSizeBytes == 0) {
				error = "invalid wav frame size";
				return std::nullopt;
			}
			const std::size_t totalFrames = dataSize / frameSizeBytes;
			parsed.samples.reserve(totalFrames);

			if (audioFormat == 1 && bitsPerSample == 16) {
				for (std::size_t i = 0; i < totalFrames; ++i) {
					float mono = 0.0f;
					for (std::size_t c = 0; c < channels; ++c) {
						const std::size_t idx = dataOffset + i * frameSizeBytes + c * 2;
						const std::int16_t sample = static_cast<std::int16_t>(
							static_cast<std::uint16_t>(bytes[idx]) |
							(static_cast<std::uint16_t>(bytes[idx + 1]) << 8));
						mono += static_cast<float>(sample) / 32768.0f;
					}
					parsed.samples.push_back(mono / static_cast<float>(channels));
				}
			}
			else if (audioFormat == 3 && bitsPerSample == 32) {
				for (std::size_t i = 0; i < totalFrames; ++i) {
					float mono = 0.0f;
					for (std::size_t c = 0; c < channels; ++c) {
						const std::size_t idx = dataOffset + i * frameSizeBytes + c * 4;
						float sample = 0.0f;
						std::memcpy(&sample, bytes.data() + idx, sizeof(float));
						mono += sample;
					}
					parsed.samples.push_back(mono / static_cast<float>(channels));
				}
			}
			else {
				error = "unsupported wav format (only PCM16 and float32 are supported)";
				return std::nullopt;
			}

			return parsed;
		};

		auto resampleLinear = [](const std::vector<float>& input, std::uint32_t inRate, std::uint32_t outRate) {
			if (input.empty() || inRate == 0 || outRate == 0 || inRate == outRate) {
				return input;
			}
			const double ratio = static_cast<double>(outRate) / static_cast<double>(inRate);
			const std::size_t outCount = static_cast<std::size_t>((std::max)(1.0, std::floor(input.size() * ratio)));
			std::vector<float> output(outCount, 0.0f);
			for (std::size_t i = 0; i < outCount; ++i) {
				const double src = static_cast<double>(i) / ratio;
				const std::size_t left = static_cast<std::size_t>(std::floor(src));
				const std::size_t right = (std::min)(left + 1, input.size() - 1);
				const double frac = src - static_cast<double>(left);
				output[i] = static_cast<float>((1.0 - frac) * input[left] + frac * input[right]);
			}
			return output;
		};

		auto buildLogMel = [](const std::vector<float>& samples, std::uint32_t sampleRate) {
			const std::size_t nFft = 400;
			const std::size_t hop = 160;
			const std::size_t nMels = 128;
			const std::size_t nBins = (nFft / 2) + 1;
			const auto melFilters = BuildMelFilterbank(sampleRate, nFft, nMels, 0.0f, 8000.0f);
			if (samples.size() < nFft) {
				return std::vector<float>{};
			}
			std::vector<float> padded = samples;
			constexpr std::size_t kTargetSamples = 480000;
			if (padded.size() < kTargetSamples) {
				padded.resize(kTargetSamples, 0.0f);
			}
			else if (padded.size() > kTargetSamples) {
				padded.resize(kTargetSamples);
			}
			const std::size_t frames = 1 + ((padded.size() - nFft) / hop);
			std::vector<float> output(nMels * frames, 0.0f);
			std::vector<float> window(nFft, 0.0f);
			for (std::size_t i = 0; i < nFft; ++i) {
				window[i] = HannWindow(i, nFft);
			}
			std::vector<float> spectrum(nBins, 0.0f);
			float globalMax = -std::numeric_limits<float>::infinity();
			for (std::size_t frame = 0; frame < frames; ++frame) {
				const std::size_t base = frame * hop;
				for (std::size_t k = 0; k < nBins; ++k) {
					double real = 0.0;
					double imag = 0.0;
					for (std::size_t n = 0; n < nFft; ++n) {
						const double x = static_cast<double>(padded[base + n] * window[n]);
						const double angle = (2.0 * 3.14159265358979323846 * static_cast<double>(k * n)) /
							static_cast<double>(nFft);
						real += x * std::cos(angle);
						imag -= x * std::sin(angle);
					}
					spectrum[k] = static_cast<float>(real * real + imag * imag);
				}
				for (std::size_t m = 0; m < nMels; ++m) {
					double melEnergy = 0.0;
					for (std::size_t b = 0; b < nBins; ++b) {
						melEnergy += static_cast<double>(melFilters[m * nBins + b]) *
							static_cast<double>(spectrum[b]);
					}
					const float logValue = static_cast<float>(std::log10((std::max)(1e-10, melEnergy)));
					output[m * frames + frame] = logValue;
					globalMax = (std::max)(globalMax, logValue);
				}
			}
			const float floorValue = globalMax - 8.0f;
			for (auto& value : output) {
				value = (std::max)(value, floorValue);
				value = (value + 4.0f) / 4.0f;
			}
			return output;
		};

		auto decodeTokens = [](
			const std::vector<std::int64_t>& ids,
			const std::unordered_map<std::int64_t, std::string>& tokenById,
			const std::unordered_set<std::int64_t>& specialTokenIds,
			const std::unordered_map<char32_t, std::uint8_t>& byteLevelMap,
			std::string& stopReason) {
			std::string text;
			for (const auto id : ids) {
				if (id == kTokenAsrText) {
					continue;
				}
				if (specialTokenIds.find(id) != specialTokenIds.end()) {
					if (id == kTokenEndOfText) {
						stopReason = "eos";
					}
					else if (id == kTokenImStart || id == kTokenImEnd) {
						stopReason = "special_token";
					}
					break;
				}
				const auto it = tokenById.find(id);
				if (it == tokenById.end()) {
					continue;
				}
				const std::string decoded = DecodeByteLevelToken(it->second, byteLevelMap);
				const std::string token = decoded.empty() ? it->second : decoded;
				if (token.rfind("<|", 0) == 0) {
					continue;
				}
				text += token;
			}

			auto sanitizeTranscript = [](std::string value) {
				for (;;) {
					const auto markerStart = value.find("<|");
					if (markerStart == std::string::npos) {
						break;
					}
					const auto markerEnd = value.find("|>", markerStart + 2);
					if (markerEnd == std::string::npos) {
						value.erase(markerStart);
						break;
					}
					value.erase(markerStart, markerEnd - markerStart + 2);
				}
				const std::array<std::string, 6> scrubPhrases = {
					"[assistant_response]",
					"assistant_response",
					"[user_message]",
					"\nuser\n",
					"\nassistant\n",
					"\rim_start",
				};
				for (const auto& phrase : scrubPhrases) {
					std::size_t offset = 0;
					while ((offset = value.find(phrase, offset)) != std::string::npos) {
						value.erase(offset, phrase.size());
					}
				}
				StripQwen3AsrTemplateNoise(value);
				while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
					value.erase(value.begin());
				}
				while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
					value.pop_back();
				}
				return value;
			};

			return sanitizeTranscript(text);
		};

		if (isCancelled()) {
			result.ok = false;
			result.cancelled = true;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::Cancelled,
				.message = "speech transcription cancelled before preprocessing",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.cancelled = true;
			result.sessionState.error = result.error;
			++m_snapshot.transcribeRequestsCancelled;
			m_snapshot.status = "cancelled";
			m_snapshot.error = result.error;
			TraceRuntime(
				"transcribe.cancelled",
				request.runId,
				"source=runtime_checkpoint stage=before_preprocessing");
			return result;
		}

		const auto preprocessStart = std::chrono::steady_clock::now();
		std::string wavError;
		auto parsed = parseWave(audioPathWide, wavError);
		if (!parsed.has_value()) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InvalidAudioFormat,
				.message = wavError.empty() ? "failed to parse wav audio" : wavError,
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			++m_snapshot.transcribeRequestsFailed;
			m_snapshot.status = "invalid_audio";
			m_snapshot.error = result.error;
			return result;
		}

		m_snapshot.lastInputSampleRate = parsed->sampleRate;
		m_snapshot.lastInputChannels = parsed->channels;
		m_snapshot.lastAudioDurationMs = parsed->sampleRate == 0
			? 0
			: static_cast<std::uint32_t>((parsed->samples.size() * 1000ULL) / parsed->sampleRate);

		auto mono = parsed->samples;
		if (parsed->sampleRate != kDefaultSampleRate) {
			mono = resampleLinear(parsed->samples, parsed->sampleRate, kDefaultSampleRate);
			m_snapshot.lastInputResampled = true;
		}
		else {
			m_snapshot.lastInputResampled = false;
		}

		const auto logMel = buildLogMel(mono, kDefaultSampleRate);
		if (logMel.empty()) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::FeatureExtractionFailed,
				.message = "audio too short or feature extraction failed",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			++m_snapshot.transcribeRequestsFailed;
			m_snapshot.status = "feature_extraction_failed";
			m_snapshot.error = result.error;
			return result;
		}
		const std::size_t nMels = 128;
		const std::size_t frames = logMel.size() / nMels;
		m_snapshot.lastFeatureBins = static_cast<std::uint32_t>(nMels);
		m_snapshot.lastFeatureFrames = static_cast<std::uint32_t>(frames);
		m_snapshot.lastPreprocessLatencyMs = static_cast<std::uint32_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - preprocessStart)
				.count());
		TraceRuntime(
			"transcribe.preprocessing.completed",
			request.runId,
			"sampleRate=" + std::to_string(m_snapshot.lastInputSampleRate) +
			" durationMs=" + std::to_string(m_snapshot.lastAudioDurationMs) +
			" frames=" + std::to_string(m_snapshot.lastFeatureFrames));

		if (isCancelled()) {
			result.ok = false;
			result.cancelled = true;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::Cancelled,
				.message = "speech transcription cancelled before inference",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.cancelled = true;
			result.sessionState.error = result.error;
			++m_snapshot.transcribeRequestsCancelled;
			m_snapshot.status = "cancelled";
			m_snapshot.error = result.error;
			TraceRuntime(
				"transcribe.cancelled",
				request.runId,
				"source=runtime_checkpoint stage=before_inference");
			return result;
		}

		const auto inferenceStart = std::chrono::steady_clock::now();
		const std::string decoderStrategy = m_sessionState->decoderStep
			? "decoder_init_only(step_model_disabled_pending_input_embeds_and_kv_cache)"
			: "decoder_init_only";
		std::size_t generatedTokenCount = 0;
		try {
			Ort::AllocatorWithDefaultOptions allocator;
			auto encoderInputNameAlloc = m_sessionState->encoder->GetInputNameAllocated(0, allocator);
			const char* encoderInputName = encoderInputNameAlloc.get();
			auto encoderOutputNameAlloc = m_sessionState->encoder->GetOutputNameAllocated(0, allocator);
			const char* encoderOutputName = encoderOutputNameAlloc.get();

			auto encoderInputShape = m_sessionState->encoder->GetInputTypeInfo(0)
				.GetTensorTypeAndShapeInfo()
				.GetShape();
			std::vector<std::int64_t> encoderDims;
			if (encoderInputShape.size() == 3) {
				if (encoderInputShape[1] == 128 || encoderInputShape[1] < 0) {
					encoderDims = { 1, static_cast<std::int64_t>(nMels), static_cast<std::int64_t>(frames) };
				}
				else {
					encoderDims = { 1, static_cast<std::int64_t>(frames), static_cast<std::int64_t>(nMels) };
				}
			}
			else {
				encoderDims = { 1, static_cast<std::int64_t>(nMels), static_cast<std::int64_t>(frames) };
			}

			std::vector<float> encoderInputData;
			encoderInputData.resize(logMel.size());
			if (encoderDims[1] == static_cast<std::int64_t>(nMels)) {
				encoderInputData = logMel;
			}
			else {
				for (std::size_t t = 0; t < frames; ++t) {
					for (std::size_t m = 0; m < nMels; ++m) {
						encoderInputData[t * nMels + m] = logMel[m * frames + t];
					}
				}
			}

			auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
			Ort::Value encoderInputTensor = Ort::Value::CreateTensor<float>(
				memInfo,
				encoderInputData.data(),
				encoderInputData.size(),
				encoderDims.data(),
				encoderDims.size());
			std::array<const char*, 1> encoderInputNames{ encoderInputName };
			std::array<const char*, 1> encoderOutputNames{ encoderOutputName };
			auto encoderOutputs = m_sessionState->encoder->Run(
				Ort::RunOptions{ nullptr },
				encoderInputNames.data(),
				&encoderInputTensor,
				1,
				encoderOutputNames.data(),
				1);
			if (encoderOutputs.empty() || !encoderOutputs[0].IsTensor()) {
				throw std::runtime_error("encoder output is empty or invalid");
			}

			auto encodedInfo = encoderOutputs[0].GetTensorTypeAndShapeInfo();
			auto encodedShape = encodedInfo.GetShape();
			if (encodedShape.empty()) {
				throw std::runtime_error("encoder output shape is empty");
			}
			const auto encodedCount = encodedInfo.GetElementCount();
			if (encodedCount == 0) {
				throw std::runtime_error("encoder output has zero elements");
			}
			std::size_t encodedSequenceLength = 0;
			if (encodedShape.size() >= 2 && encodedShape[1] > 0) {
				encodedSequenceLength = static_cast<std::size_t>(encodedShape[1]);
			}
			else if (encodedShape.size() == 3 && encodedShape[2] > 0) {
				encodedSequenceLength = static_cast<std::size_t>(encodedShape[2]);
			}
			if (encodedSequenceLength == 0) {
				throw std::runtime_error("encoder output sequence length is invalid");
			}
			float* encodedPtr = encoderOutputs[0].GetTensorMutableData<float>();
			std::vector<float> encodedFeatures(encodedPtr, encodedPtr + encodedCount);

			std::vector<std::int64_t> promptIds = {
				kTokenImStart,
				kPromptTextSystem,
				kTokenNewline,
				kTokenImEnd,
				kTokenNewline,
				kTokenImStart,
				kPromptTextUser,
				kTokenNewline,
				kTokenAudioStart,
			};
			const std::size_t audioPadCount = encodedSequenceLength;
			const std::size_t audioPadStartIndex = promptIds.size();
			for (std::size_t i = 0; i < audioPadCount; ++i) {
				promptIds.push_back(kTokenAudioPad);
			}
			promptIds.push_back(kTokenAudioEnd);
			promptIds.push_back(kTokenImEnd);
			promptIds.push_back(kTokenNewline);
			promptIds.push_back(kTokenImStart);
			promptIds.push_back(kPromptTextAssistant);
			promptIds.push_back(kTokenNewline);

			std::vector<std::int64_t> generatedIds;
			std::string decodeStopReason = "max_steps";
			const auto decodeLoopStart = std::chrono::steady_clock::now();
			for (std::size_t step = 0; step < kMaxDecodeSteps; ++step) {
				if (isCancelled()) {
					result.ok = false;
					result.cancelled = true;
					result.error = SpeechRecognitionError{
						.code = SpeechRecognitionErrorCode::Cancelled,
						.message = "speech transcription cancelled during decode",
					};
					result.sessionState.stage = SpeechSessionStage::Failed;
					result.sessionState.cancelled = true;
					result.sessionState.error = result.error;
					++m_snapshot.transcribeRequestsCancelled;
					m_snapshot.status = "cancelled";
					m_snapshot.error = result.error;
					TraceRuntime(
						"transcribe.cancelled",
						request.runId,
						"source=runtime_checkpoint stage=during_decode");
					return result;
				}
				const auto decodeElapsedMs = static_cast<std::uint32_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::steady_clock::now() - decodeLoopStart)
						.count());
				if (decodeElapsedMs >= kMaxDecodeWallClockMs) {
					decodeStopReason = "timeout";
					TraceRuntime(
						"transcribe.decode.timeout",
						request.runId,
						"elapsedMs=" + std::to_string(decodeElapsedMs) +
						" maxMs=" + std::to_string(kMaxDecodeWallClockMs) +
						" generatedTokens=" + std::to_string(generatedIds.size()));
					break;
				}

				std::vector<std::int64_t> ids = promptIds;
				ids.insert(ids.end(), generatedIds.begin(), generatedIds.end());
				const std::vector<std::int64_t> inputIdsShape = {
					1,
					static_cast<std::int64_t>(ids.size()),
				};
				Ort::Value inputIdsTensor = Ort::Value::CreateTensor<std::int64_t>(
					memInfo,
					ids.data(),
					ids.size(),
					inputIdsShape.data(),
					inputIdsShape.size());

				std::vector<std::int64_t> positionIds(ids.size());
				std::iota(
					positionIds.begin(),
					positionIds.end(),
					static_cast<std::int64_t>(0));
				Ort::Value positionIdsTensor = Ort::Value::CreateTensor<std::int64_t>(
					memInfo,
					positionIds.data(),
					positionIds.size(),
					inputIdsShape.data(),
					inputIdsShape.size());

				const std::vector<std::int64_t> audioOffsetShape = { 1 };
				std::array<std::int64_t, 1> audioOffset{
					static_cast<std::int64_t>(audioPadStartIndex),
				};
				Ort::Value audioOffsetTensor = Ort::Value::CreateTensor<std::int64_t>(
					memInfo,
					audioOffset.data(),
					audioOffset.size(),
					audioOffsetShape.data(),
					audioOffsetShape.size());

				Ort::Value audioFeaturesTensor = Ort::Value::CreateTensor<float>(
					memInfo,
					encodedFeatures.data(),
					encodedFeatures.size(),
					encodedShape.data(),
					encodedShape.size());

				Ort::Session* activeDecoder = m_sessionState->decoderInit.get();
				const std::string activeDecoderName = "decoder_init";

				const auto decoderInputCount = activeDecoder->GetInputCount();
				std::vector<std::string> decoderInputNamesOwned;
				std::vector<const char*> decoderInputNames;
				std::vector<Ort::Value> decoderInputs;
				std::vector<std::vector<std::int64_t>> ownedInt64Buffers;
				std::vector<std::vector<float>> ownedFloatBuffers;
				decoderInputNamesOwned.reserve(decoderInputCount);
				decoderInputNames.reserve(decoderInputCount);
				decoderInputs.reserve(decoderInputCount);

				for (std::size_t i = 0; i < decoderInputCount; ++i) {
					auto nameAlloc = activeDecoder->GetInputNameAllocated(i, allocator);
					const std::string inputName = nameAlloc.get();
					decoderInputNamesOwned.push_back(inputName);
					decoderInputNames.push_back(decoderInputNamesOwned.back().c_str());
					if (inputName.find("input_ids") != std::string::npos) {
						decoderInputs.push_back(std::move(inputIdsTensor));
					}
					else if (inputName.find("audio_features") != std::string::npos) {
						decoderInputs.push_back(std::move(audioFeaturesTensor));
					}
					else if (inputName.find("audio_offset") != std::string::npos) {
						decoderInputs.push_back(std::move(audioOffsetTensor));
					}
					else if (inputName.find("position_ids") != std::string::npos) {
						decoderInputs.push_back(std::move(positionIdsTensor));
					}
					else {
						auto shape = activeDecoder->GetInputTypeInfo(i)
							.GetTensorTypeAndShapeInfo()
							.GetShape();
						for (auto& dim : shape) {
							if (dim <= 0) {
								dim = 1;
							}
						}
						std::size_t count = 1;
						for (const auto dim : shape) {
							count *= static_cast<std::size_t>(dim);
						}
						auto elemType = activeDecoder->GetInputTypeInfo(i)
							.GetTensorTypeAndShapeInfo()
							.GetElementType();
						if (elemType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
							ownedInt64Buffers.push_back(std::vector<std::int64_t>(count, 0));
							auto& zeros = ownedInt64Buffers.back();
							decoderInputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
								memInfo,
								zeros.data(),
								zeros.size(),
								shape.data(),
								shape.size()));
						}
						else {
							ownedFloatBuffers.push_back(std::vector<float>(count, 0.0f));
							auto& zeros = ownedFloatBuffers.back();
							decoderInputs.push_back(Ort::Value::CreateTensor<float>(
								memInfo,
								zeros.data(),
								zeros.size(),
								shape.data(),
								shape.size()));
						}
					}
				}

				const auto decoderOutputCount = activeDecoder->GetOutputCount();
				std::vector<std::string> decoderOutputNamesOwned;
				std::vector<const char*> decoderOutputNames;
				decoderOutputNamesOwned.reserve(decoderOutputCount);
				decoderOutputNames.reserve(decoderOutputCount);
				for (std::size_t i = 0; i < decoderOutputCount; ++i) {
					auto nameAlloc = activeDecoder->GetOutputNameAllocated(i, allocator);
					decoderOutputNamesOwned.push_back(nameAlloc.get());
					decoderOutputNames.push_back(decoderOutputNamesOwned.back().c_str());
				}

				auto decoderOutputs = activeDecoder->Run(
					Ort::RunOptions{ nullptr },
					decoderInputNames.data(),
					decoderInputs.data(),
					decoderInputs.size(),
					decoderOutputNames.data(),
					decoderOutputNames.size());
				if (decoderOutputs.empty()) {
					throw std::runtime_error(activeDecoderName + " produced no outputs");
				}

				const Ort::Value* logitsTensor = nullptr;
				for (auto& out : decoderOutputs) {
					if (!out.IsTensor()) {
						continue;
					}
					auto info = out.GetTensorTypeAndShapeInfo();
					auto shape = info.GetShape();
					if (shape.size() >= 3) {
						logitsTensor = &out;
						break;
					}
				}
				if (logitsTensor == nullptr) {
					throw std::runtime_error("failed to locate logits tensor in decoder outputs");
				}

				auto logitsInfo = logitsTensor->GetTensorTypeAndShapeInfo();
				auto logitsShape = logitsInfo.GetShape();
				if (logitsShape.size() < 3) {
					throw std::runtime_error("logits tensor shape is invalid");
				}
				const std::size_t vocab = static_cast<std::size_t>(logitsShape.back() <= 0 ? 0 : logitsShape.back());
				if (vocab == 0) {
					throw std::runtime_error("logits vocab dimension is zero");
				}

				const std::size_t total = logitsInfo.GetElementCount();
				if (total < vocab) {
					throw std::runtime_error("logits tensor element count is smaller than vocab");
				}
				auto elemType = logitsInfo.GetElementType();
				if (elemType != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
					throw std::runtime_error("decoder logits tensor type is not float32");
				}
				const std::size_t lastOffset = total - vocab;
				const float* logits = logitsTensor->GetTensorData<float>();
				auto maxIt = std::max_element(logits + lastOffset, logits + lastOffset + vocab);
				const std::int64_t nextToken =
					static_cast<std::int64_t>(std::distance(logits + lastOffset, maxIt));

				if (m_sessionState->specialTokenIds.find(nextToken) != m_sessionState->specialTokenIds.end()) {
					if (nextToken == kTokenEndOfText) {
						decodeStopReason = "eos";
					}
					else {
						decodeStopReason = "special_token";
					}
					break;
				}
				generatedIds.push_back(nextToken);
			}
			generatedTokenCount = generatedIds.size();

			m_snapshot.lastInferenceLatencyMs = static_cast<std::uint32_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - inferenceStart)
					.count());
			TraceRuntime(
				"transcribe.inference.completed",
				request.runId,
				"latencyMs=" + std::to_string(m_snapshot.lastInferenceLatencyMs) +
				" generatedTokens=" + std::to_string(generatedTokenCount) +
				" stopReason=" + decodeStopReason +
				" tokenizerMode=" + m_sessionState->tokenizerMode +
				" decoderStrategy=" + decoderStrategy +
				" promptTokenCount=" + std::to_string(promptIds.size()) +
				" audioPadStartIndex=" + std::to_string(audioPadStartIndex) +
				" audioPadCount=" + std::to_string(audioPadCount) +
				" encodedSequenceLength=" + std::to_string(encodedSequenceLength));

			const auto decodeStart = std::chrono::steady_clock::now();
			result.text = decodeTokens(
				generatedIds,
				m_sessionState->tokenById,
				m_sessionState->specialTokenIds,
				m_sessionState->byteLevelCharToByte,
				decodeStopReason);
			m_snapshot.lastDecodeLatencyMs = static_cast<std::uint32_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - decodeStart)
					.count());
			if (result.text.empty()) {
				result.error = SpeechRecognitionError{
					.code = SpeechRecognitionErrorCode::DecoderFailed,
					.message = "decoder produced an empty transcript (generatedTokens=" +
						std::to_string(generatedTokenCount) +
						", stopReason=" + decodeStopReason +
						", decoderStrategy=" + decoderStrategy + ")",
				};
				result.ok = false;
				result.sessionState.stage = SpeechSessionStage::Failed;
				result.sessionState.error = result.error;
				++m_snapshot.transcribeRequestsFailed;
				m_snapshot.status = "decoder_failed";
				m_snapshot.error = result.error;
				return result;
			}

			result.ok = true;
			result.language = request.language.empty() ? m_snapshot.language : request.language;
			result.latencyMs = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - startedAt).count());
			TraceRuntime(
				"transcribe.transcript.completed",
				request.runId,
				"length=" + std::to_string(result.text.size()) +
				" language=" + result.language +
				" latencyMs=" + std::to_string(result.latencyMs));
			result.sessionState.stage = SpeechSessionStage::Completed;
			result.sessionState.transcriptText = result.text;
			result.sessionState.language = result.language;
			result.sessionState.latencyMs = result.latencyMs;
			result.sessionState.segment = SpeechTranscriptSegment{
				.text = result.text,
				.final = true,
				.sequence = 1,
			};
			++m_snapshot.transcribeRequestsCompleted;
			m_snapshot.lastLatencyMs = result.latencyMs;
			m_snapshot.cumulativeLatencyMs += result.latencyMs;
			m_snapshot.status = "transcribed";
			m_snapshot.error = std::nullopt;
			return result;
		}
		catch (const std::exception& ex) {
			const std::string inferenceMessage = std::string(ex.what()) +
				" (generatedTokens=" + std::to_string(generatedTokenCount) +
				", decoderStrategy=" + decoderStrategy + ")";
			TraceRuntime(
				"transcribe.inference.failed",
				request.runId,
				"message=" + inferenceMessage);
			TRACE("[SpeechRecognition][transcribe.inference.failed] runId=%S %S\n", request.runId.c_str(), inferenceMessage.c_str());
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InferenceFailed,
				.message = "inference exception: " + inferenceMessage,
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			++m_snapshot.transcribeRequestsFailed;
			m_snapshot.status = "inference_failed";
			m_snapshot.error = result.error;
			return result;
		}
#endif
	}

	bool SpeechRecognitionRuntime::Cancel(const std::string& runId) {
		if (runId.empty()) {
			return false;
		}
		std::lock_guard<std::mutex> lock(m_cancelMutex);
		m_cancelFlagsByRunId[runId] = true;
		TraceRuntime("cancel.requested", runId, "source=coordinator_or_shutdown");
		return true;
	}

	std::string SpeechRecognitionRuntime::ToNarrow(const std::wstring& value) {
		return ToNarrowLocal(value);
	}

	void SpeechRecognitionRuntime::TraceRuntime(
		const char* stage,
		const std::string& runId,
		const std::string& details) {
		TRACE("[SpeechRecognition][%S] runId=%S %S\n", stage, runId.c_str(), details.c_str());
	}

	std::string SpeechRecognitionErrorCodeToString(SpeechRecognitionErrorCode code) {
		switch (code) {
		case SpeechRecognitionErrorCode::None: return "none";
		case SpeechRecognitionErrorCode::SpeechRecognitionDisabled: return "speech_recognition_disabled";
		case SpeechRecognitionErrorCode::ProviderNotSupported: return "provider_not_supported";
		case SpeechRecognitionErrorCode::ModelNotFound: return "model_not_found";
		case SpeechRecognitionErrorCode::ModelLoadFailed: return "model_load_failed";
		case SpeechRecognitionErrorCode::InvalidInput: return "invalid_input";
		case SpeechRecognitionErrorCode::AudioNotFound: return "audio_not_found";
		case SpeechRecognitionErrorCode::InvalidAudioFormat: return "invalid_audio_format";
		case SpeechRecognitionErrorCode::AudioDecodeFailed: return "audio_decode_failed";
		case SpeechRecognitionErrorCode::FeatureExtractionFailed: return "feature_extraction_failed";
		case SpeechRecognitionErrorCode::TokenizerLoadFailed: return "tokenizer_load_failed";
		case SpeechRecognitionErrorCode::DecoderFailed: return "decoder_failed";
		case SpeechRecognitionErrorCode::InferenceFailed: return "inference_failed";
		case SpeechRecognitionErrorCode::RuntimeUnavailable: return "runtime_unavailable";
		case SpeechRecognitionErrorCode::Cancelled: return "cancelled";
		default: return "unknown";
		}
	}

} // namespace blazeclaw::core::speechrecognition
