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

		constexpr std::uint32_t kDefaultSampleRate = 16000;
		constexpr std::size_t kMaxDecodeSteps = 512;
		constexpr std::int64_t kTokenEndOfText = 151643;
		constexpr std::int64_t kTokenImEnd = 151645;
		constexpr std::int64_t kTokenImStart = 151644;
		constexpr std::int64_t kTokenAudioStart = 151669;
		constexpr std::int64_t kTokenAudioEnd = 151670;
		constexpr std::int64_t kTokenAudioPad = 151676;
		constexpr std::int64_t kTokenAsrText = 151704;

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

		std::vector<float> BuildMelFilterbank(
			std::uint32_t sampleRate,
			std::size_t nFft,
			std::size_t nMels,
			float fMin,
			float fMax) {
			auto hzToMel = [](float hz) {
				return 2595.0f * std::log10(1.0f + hz / 700.0f);
			};
			auto melToHz = [](float mel) {
				return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f);
			};

			const std::size_t nFreqBins = (nFft / 2) + 1;
			std::vector<float> filters(nMels * nFreqBins, 0.0f);
			const float melMin = hzToMel(fMin);
			const float melMax = hzToMel(fMax);
			std::vector<float> melPoints(nMels + 2, 0.0f);
			for (std::size_t i = 0; i < melPoints.size(); ++i) {
				melPoints[i] = melMin + (melMax - melMin) * static_cast<float>(i) /
					static_cast<float>(melPoints.size() - 1);
			}
			std::vector<std::size_t> bins(melPoints.size(), 0);
			for (std::size_t i = 0; i < melPoints.size(); ++i) {
				const float hz = melToHz(melPoints[i]);
				const float scaled = (static_cast<float>(nFft + 1) * hz) /
					static_cast<float>(sampleRate);
				bins[i] = static_cast<std::size_t>(std::floor((std::max)(0.0f, scaled)));
				if (bins[i] >= nFreqBins) {
					bins[i] = nFreqBins - 1;
				}
			}

			for (std::size_t m = 1; m <= nMels; ++m) {
				const std::size_t left = bins[m - 1];
				const std::size_t center = bins[m];
				const std::size_t right = bins[m + 1];
				if (center <= left || right <= center) {
					continue;
				}
				for (std::size_t k = left; k < center; ++k) {
					filters[(m - 1) * nFreqBins + k] =
						(static_cast<float>(k - left)) /
						static_cast<float>(center - left);
				}
				for (std::size_t k = center; k < right; ++k) {
					filters[(m - 1) * nFreqBins + k] =
						(static_cast<float>(right - k)) /
						static_cast<float>(right - center);
				}
			}
			return filters;
		}

	} // namespace

	struct SpeechRecognitionRuntime::SessionState {
#if BLAZECLAW_HAS_ONNXRUNTIME
		std::unique_ptr<Ort::Env> env;
		std::unique_ptr<Ort::SessionOptions> options;
		std::unique_ptr<Ort::Session> encoder;
		std::unique_ptr<Ort::Session> decoderInit;
#endif
		std::unordered_map<std::int64_t, std::string> tokenById;
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
		const std::filesystem::path tokenizerPath = rootPath / L"vocab.json";

		if (!std::filesystem::exists(encoderPath) ||
			!std::filesystem::exists(decoderInitPath) ||
			!std::filesystem::exists(tokenizerPath)) {
			outResult.ok = false;
			outResult.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::ModelNotFound,
				.message = "required ASR model artifacts are missing (encoder/decoder_init/vocab)",
			};
			m_snapshot.ready = false;
			m_snapshot.status = "model_artifact_missing";
			m_snapshot.error = outResult.error;
			return false;
		}

		m_snapshot.modelVariant = preferInt4 ? "int4" : "fp32";
		m_snapshot.encoderModelPath = ToNarrow(encoderPath.wstring());
		m_snapshot.decoderInitModelPath = ToNarrow(decoderInitPath.wstring());
		m_snapshot.decoderStepModelPath.clear();
		m_snapshot.tokenizerPath = ToNarrow(tokenizerPath.wstring());

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
			if (m_snapshot.threads > 0) {
				m_sessionState->options->SetIntraOpNumThreads(static_cast<int>(m_snapshot.threads));
				m_sessionState->options->SetInterOpNumThreads(static_cast<int>(m_snapshot.threads));
			}
			if (m_snapshot.executionMode == "parallel") {
				m_sessionState->options->SetExecutionMode(ExecutionMode::ORT_PARALLEL);
			}
			else {
				m_sessionState->options->SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
			}

			m_sessionState->encoder = std::make_unique<Ort::Session>(
				*m_sessionState->env,
				encoderPath.c_str(),
				*m_sessionState->options);
			m_sessionState->decoderInit = std::make_unique<Ort::Session>(
				*m_sessionState->env,
				decoderInitPath.c_str(),
				*m_sessionState->options);

			std::ifstream tokenizerInput(tokenizerPath, std::ios::in | std::ios::binary);
			if (!tokenizerInput.is_open()) {
				throw std::runtime_error("failed to open tokenizer vocab.json");
			}
			std::stringstream tokenizerBuffer;
			tokenizerBuffer << tokenizerInput.rdbuf();
			const auto vocabJson = nlohmann::json::parse(tokenizerBuffer.str(), nullptr, true);
			if (!vocabJson.is_object()) {
				throw std::runtime_error("vocab.json is not a JSON object");
			}
			m_sessionState->tokenById.clear();
			for (auto it = vocabJson.begin(); it != vocabJson.end(); ++it) {
				if (!it.value().is_number_integer()) {
					continue;
				}
				m_sessionState->tokenById.insert_or_assign(
					it.value().get<std::int64_t>(),
					it.key());
			}
			if (m_sessionState->tokenById.empty()) {
				throw std::runtime_error("vocab.json produced an empty token map");
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
			const std::size_t frames = 1 + ((samples.size() - nFft) / hop);
			std::vector<float> output(nMels * frames, 0.0f);
			std::vector<float> window(nFft, 0.0f);
			for (std::size_t i = 0; i < nFft; ++i) {
				window[i] = HannWindow(i, nFft);
			}
			std::vector<float> spectrum(nBins, 0.0f);
			for (std::size_t frame = 0; frame < frames; ++frame) {
				const std::size_t base = frame * hop;
				for (std::size_t k = 0; k < nBins; ++k) {
					double real = 0.0;
					double imag = 0.0;
					for (std::size_t n = 0; n < nFft; ++n) {
						const double x = static_cast<double>(samples[base + n] * window[n]);
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
					output[m * frames + frame] = static_cast<float>(std::log10((std::max)(1e-10, melEnergy)));
				}
			}
			return output;
		};

		auto decodeTokens = [](const std::vector<std::int64_t>& ids,
			const std::unordered_map<std::int64_t, std::string>& tokenById) {
			std::string text;
			for (const auto id : ids) {
				if (id == kTokenEndOfText || id == kTokenImEnd) {
					break;
				}
				const auto it = tokenById.find(id);
				if (it == tokenById.end()) {
					continue;
				}
				std::string token = it->second;
				if (token.rfind("<|", 0) == 0) {
					continue;
				}
				std::size_t pos = 0;
				while ((pos = token.find("▁", pos)) != std::string::npos) {
					token.replace(pos, std::strlen("▁"), " ");
					pos += 1;
				}
				text += token;
			}
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
				text.erase(text.begin());
			}
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
				text.pop_back();
			}
			return text;
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
			return result;
		}

		const auto inferenceStart = std::chrono::steady_clock::now();
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
			float* encodedPtr = encoderOutputs[0].GetTensorMutableData<float>();
			std::vector<float> encodedFeatures(encodedPtr, encodedPtr + encodedCount);

			std::vector<std::int64_t> promptIds = {
				kTokenImStart,
				kTokenAsrText,
				kTokenAudioStart,
			};
			const std::size_t audioPadCount = (std::min<std::size_t>)(256, frames > 0 ? (frames / 8 + 1) : 1);
			for (std::size_t i = 0; i < audioPadCount; ++i) {
				promptIds.push_back(kTokenAudioPad);
			}
			promptIds.push_back(kTokenAudioEnd);
			promptIds.push_back(kTokenImEnd);
			promptIds.push_back(kTokenImStart);

			std::vector<std::int64_t> generatedIds;
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
					return result;
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

				const std::vector<std::int64_t> audioOffsetShape = { 1 };
				std::array<std::int64_t, 1> audioOffset{ 0 };
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

				const auto decoderInputCount = m_sessionState->decoderInit->GetInputCount();
				std::vector<std::string> decoderInputNamesOwned;
				std::vector<const char*> decoderInputNames;
				std::vector<Ort::Value> decoderInputs;
				std::vector<std::vector<std::int64_t>> ownedInt64Buffers;
				std::vector<std::vector<float>> ownedFloatBuffers;
				decoderInputNamesOwned.reserve(decoderInputCount);
				decoderInputNames.reserve(decoderInputCount);
				decoderInputs.reserve(decoderInputCount);

				for (std::size_t i = 0; i < decoderInputCount; ++i) {
					auto nameAlloc = m_sessionState->decoderInit->GetInputNameAllocated(i, allocator);
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
					else {
						auto shape = m_sessionState->decoderInit->GetInputTypeInfo(i)
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
						auto elemType = m_sessionState->decoderInit->GetInputTypeInfo(i)
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

				const auto decoderOutputCount = m_sessionState->decoderInit->GetOutputCount();
				std::vector<std::string> decoderOutputNamesOwned;
				std::vector<const char*> decoderOutputNames;
				decoderOutputNamesOwned.reserve(decoderOutputCount);
				decoderOutputNames.reserve(decoderOutputCount);
				for (std::size_t i = 0; i < decoderOutputCount; ++i) {
					auto nameAlloc = m_sessionState->decoderInit->GetOutputNameAllocated(i, allocator);
					decoderOutputNamesOwned.push_back(nameAlloc.get());
					decoderOutputNames.push_back(decoderOutputNamesOwned.back().c_str());
				}

				auto decoderOutputs = m_sessionState->decoderInit->Run(
					Ort::RunOptions{ nullptr },
					decoderInputNames.data(),
					decoderInputs.data(),
					decoderInputs.size(),
					decoderOutputNames.data(),
					decoderOutputNames.size());
				if (decoderOutputs.empty()) {
					throw std::runtime_error("decoder_init produced no outputs");
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

				if (nextToken == kTokenEndOfText || nextToken == kTokenImEnd) {
					break;
				}
				generatedIds.push_back(nextToken);
			}

			m_snapshot.lastInferenceLatencyMs = static_cast<std::uint32_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - inferenceStart)
					.count());

			const auto decodeStart = std::chrono::steady_clock::now();
			result.text = decodeTokens(generatedIds, m_sessionState->tokenById);
			m_snapshot.lastDecodeLatencyMs = static_cast<std::uint32_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - decodeStart)
					.count());
			if (result.text.empty()) {
				result.error = SpeechRecognitionError{
					.code = SpeechRecognitionErrorCode::DecoderFailed,
					.message = "decoder produced an empty transcript",
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
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InferenceFailed,
				.message = ex.what(),
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
