#include "pch.h"
#include "SherpaZipformerStreamingEngine.h"

#include "../StreamingAudioSourceRegistry.h"

#include <kaldi-native-fbank/csrc/online-feature.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace blazeclaw::core::speechrecognition::engines {

	namespace {
		struct Utf8Symbol {
			std::uint32_t codePoint = 0;
			std::string text;
		};

		struct DecodedRepeatClassification {
			bool degenerate = false;
			std::string repeatedUnit;
			std::size_t repeatedUnitLength = 0;
			std::size_t repeatedUnitCount = 0;
			double repeatedUnitCoverage = 0.0;
			std::string longestRepeatedChar;
			std::size_t longestRepeatedCharRun = 0;
		};

		constexpr float kSpeechEnergyThreshold = 0.0001f;
		constexpr std::size_t kMinSherpaFeatureFrames = 16;
		constexpr std::size_t kSherpaFinalPartialMinRealFrames = 16;
		constexpr std::size_t kSherpaMaxSymbolsPerFrame = 8;
		constexpr std::size_t kSherpaMaxTokensPerUtterance = 512;

		enum class SherpaFbankSampleScalingMode {
			KaldiInt16,
			NormalizedFloat,
		};

		struct SherpaFbankSampleScalingPolicy {
			SherpaFbankSampleScalingMode mode = SherpaFbankSampleScalingMode::NormalizedFloat;
			std::string diagnosticName = "reference_normalized_float";
		};

		bool IsFiniteSample(float value) {
			return std::isfinite(value) != 0;
		}

		bool IsCjkCodePoint(std::uint32_t value) {
			return (value >= 0x3400U && value <= 0x4DBFU) ||
				(value >= 0x4E00U && value <= 0x9FFFU) ||
				(value >= 0xF900U && value <= 0xFAFFU) ||
				(value >= 0x20000U && value <= 0x2A6DFU) ||
				(value >= 0x2A700U && value <= 0x2B73FU) ||
				(value >= 0x2B740U && value <= 0x2B81FU) ||
				(value >= 0x2B820U && value <= 0x2CEAFU);
		}

		std::vector<Utf8Symbol> DecodeUtf8Symbols(const std::string& value) {
			std::vector<Utf8Symbol> symbols;
			for (std::size_t index = 0; index < value.size();) {
				const auto lead = static_cast<unsigned char>(value[index]);
				std::size_t width = 1;
				std::uint32_t codePoint = lead;
				if ((lead & 0x80U) == 0U) {
					width = 1;
					codePoint = lead;
				}
				else if ((lead & 0xE0U) == 0xC0U && index + 1 < value.size()) {
					width = 2;
					codePoint = ((lead & 0x1FU) << 6) |
						(static_cast<unsigned char>(value[index + 1]) & 0x3FU);
				}
				else if ((lead & 0xF0U) == 0xE0U && index + 2 < value.size()) {
					width = 3;
					codePoint = ((lead & 0x0FU) << 12) |
						((static_cast<unsigned char>(value[index + 1]) & 0x3FU) << 6) |
						(static_cast<unsigned char>(value[index + 2]) & 0x3FU);
				}
				else if ((lead & 0xF8U) == 0xF0U && index + 3 < value.size()) {
					width = 4;
					codePoint = ((lead & 0x07U) << 18) |
						((static_cast<unsigned char>(value[index + 1]) & 0x3FU) << 12) |
						((static_cast<unsigned char>(value[index + 2]) & 0x3FU) << 6) |
						(static_cast<unsigned char>(value[index + 3]) & 0x3FU);
				}

				symbols.push_back(Utf8Symbol{
					.codePoint = codePoint,
					.text = value.substr(index, width),
				});
				index += width;
			}
			return symbols;
		}

		bool SameUtf8Unit(
			const std::vector<Utf8Symbol>& symbols,
			std::size_t left,
			std::size_t right,
			std::size_t length) {
			if (left + length > symbols.size() || right + length > symbols.size()) {
				return false;
			}
			for (std::size_t offset = 0; offset < length; ++offset) {
				if (symbols[left + offset].text != symbols[right + offset].text) {
					return false;
				}
			}
			return true;
		}

		std::string JoinUtf8Unit(
			const std::vector<Utf8Symbol>& symbols,
			std::size_t start,
			std::size_t length) {
			std::string unit;
			for (std::size_t offset = 0; offset < length && start + offset < symbols.size(); ++offset) {
				unit += symbols[start + offset].text;
			}
			return unit;
		}

		DecodedRepeatClassification ClassifyDecodedRepeats(const std::string& decodedText) {
			DecodedRepeatClassification result;
			std::vector<Utf8Symbol> symbols;
			for (const auto& symbol : DecodeUtf8Symbols(decodedText)) {
				if (!std::isspace(static_cast<unsigned char>(symbol.text.front()))) {
					symbols.push_back(symbol);
				}
			}
			if (symbols.empty()) {
				return result;
			}

			std::size_t currentRun = 0;
			std::string currentChar;
			for (const auto& symbol : symbols) {
				if (symbol.text == currentChar) {
					++currentRun;
				}
				else {
					currentChar = symbol.text;
					currentRun = 1;
				}
				if (currentRun > result.longestRepeatedCharRun) {
					result.longestRepeatedCharRun = currentRun;
					result.longestRepeatedChar = symbol.text;
				}
			}

			for (std::size_t unitLength = 2; unitLength <= 6; ++unitLength) {
				if (symbols.size() < unitLength * 2) {
					continue;
				}
				for (std::size_t start = 0; start + (unitLength * 2) <= symbols.size(); ++start) {
					bool allCjk = true;
					for (std::size_t offset = 0; offset < unitLength; ++offset) {
						if (!IsCjkCodePoint(symbols[start + offset].codePoint)) {
							allCjk = false;
							break;
						}
					}
					if (!allCjk) {
						continue;
					}

					std::size_t repeatCount = 1;
					std::size_t cursor = start + unitLength;
					while (SameUtf8Unit(symbols, start, cursor, unitLength)) {
						++repeatCount;
						cursor += unitLength;
					}
					const double coverage = static_cast<double>(repeatCount * unitLength) /
						static_cast<double>(symbols.size());
					if (repeatCount > result.repeatedUnitCount ||
						(repeatCount == result.repeatedUnitCount && coverage > result.repeatedUnitCoverage)) {
						result.repeatedUnit = JoinUtf8Unit(symbols, start, unitLength);
						result.repeatedUnitLength = unitLength;
						result.repeatedUnitCount = repeatCount;
						result.repeatedUnitCoverage = coverage;
					}
				}
			}

			const bool phraseDegenerate =
				(result.repeatedUnitLength == 2 && result.repeatedUnitCount >= 5) ||
				(result.repeatedUnitLength >= 3 && result.repeatedUnitLength <= 6 && result.repeatedUnitCount >= 4) ||
				(result.repeatedUnitCount >= 3 && result.repeatedUnitCoverage >= 0.35);
			const bool charDegenerate = result.longestRepeatedCharRun >= 4 &&
				!result.longestRepeatedChar.empty() &&
				!DecodeUtf8Symbols(result.longestRepeatedChar).empty() &&
				IsCjkCodePoint(DecodeUtf8Symbols(result.longestRepeatedChar).front().codePoint);
			result.degenerate = phraseDegenerate || charDegenerate;
			return result;
		}

		std::string FormatShape(const std::vector<std::int64_t>& shape) {
			std::ostringstream stream;
			stream << '[';
			for (std::size_t index = 0; index < shape.size(); ++index) {
				if (index > 0) {
					stream << ',';
				}
				stream << shape[index];
			}
			stream << ']';
			return stream.str();
		}

		std::string ToLowerAscii(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		std::string ReadEnvironmentString(const char* name) {
			char* raw = nullptr;
			size_t size = 0;
			const int readStatus = _dupenv_s(&raw, &size, name);
			if (readStatus != 0 || raw == nullptr) {
				return {};
			}

			std::string value(raw);
			free(raw);
			return value;
		}

		bool ReadEnvironmentFlag(const char* name) {
			const auto value = ToLowerAscii(ReadEnvironmentString(name));
			return value == "1" ||
				value == "true" ||
				value == "yes" ||
				value == "on" ||
				value == "verbose";
		}

		bool IsSherpaVerboseTraceEnabled() {
			static const bool enabled = ReadEnvironmentFlag("BLAZECLAW_SHERPA_VERBOSE_TRACE");
			return enabled;
		}

		SherpaFbankSampleScalingPolicy ResolveSherpaFbankSampleScalingPolicy() {
			const auto configuredValue = ToLowerAscii(ReadEnvironmentString(
				"BLAZECLAW_SHERPA_FBANK_SAMPLE_SCALING"));
			if (configuredValue == "normalized" ||
				configuredValue == "normalized_float" ||
				configuredValue == "float" ||
				configuredValue == "none" ||
				configuredValue == "unscaled") {
				return SherpaFbankSampleScalingPolicy{
					.mode = SherpaFbankSampleScalingMode::NormalizedFloat,
					.diagnosticName = "normalized_float",
				};
			}

			if (configuredValue == "kaldi" ||
				configuredValue == "kaldi_int16" ||
				configuredValue == "int16" ||
				configuredValue == "scaled") {
				return SherpaFbankSampleScalingPolicy{
					.mode = SherpaFbankSampleScalingMode::KaldiInt16,
					.diagnosticName = "kaldi_int16",
				};
			}

			return SherpaFbankSampleScalingPolicy{
				.mode = SherpaFbankSampleScalingMode::NormalizedFloat,
				.diagnosticName = configuredValue.empty() ||
					configuredValue == "reference" ||
					configuredValue == "default"
					? "reference_normalized_float"
					: "normalized_float",
			};
		}

		std::string FormatInt64Vector(
			const std::vector<std::int64_t>& values,
			std::size_t maxCount = 8) {
			std::ostringstream stream;
			stream << '[';
			const std::size_t count = (std::min)(values.size(), maxCount);
			for (std::size_t index = 0; index < count; ++index) {
				if (index > 0) {
					stream << ',';
				}
				stream << values[index];
			}
			if (values.size() > count) {
				if (count > 0) {
					stream << ',';
				}
				stream << "...";
			}
			stream << ']';
			return stream.str();
		}

		std::string FormatFrameStats(
			const float* frame,
			std::size_t frameSize) {
			if (frame == nullptr || frameSize == 0) {
				return {};
			}

			double sum = 0.0;
			float minValue = frame[0];
			float maxValue = frame[0];
			for (std::size_t index = 0; index < frameSize; ++index) {
				const float value = frame[index];
				if (!IsFiniteSample(value)) {
					continue;
				}
				sum += value;
				minValue = (std::min)(minValue, value);
				maxValue = (std::max)(maxValue, value);
			}

			std::ostringstream stream;
			stream << std::fixed << std::setprecision(6)
				<< "mean=" << (sum / static_cast<double>(frameSize))
				<< ",min=" << minValue
				<< ",max=" << maxValue;
			return stream.str();
		}

		std::string FormatTopTokens(
			const float* logits,
			std::size_t vocabSize,
			std::size_t maxCount = 5) {
			if (logits == nullptr || vocabSize == 0) {
				return {};
			}

			std::vector<std::pair<float, std::int64_t>> top;
			top.reserve((std::min)(vocabSize, maxCount));
			for (std::size_t index = 0; index < vocabSize; ++index) {
				const auto tokenId = static_cast<std::int64_t>(index);
				const float score = logits[index];
				if (top.size() < maxCount) {
					top.emplace_back(score, tokenId);
					std::sort(top.begin(), top.end(), std::greater<>());
				}
				else if (score > top.back().first) {
					top.back() = std::make_pair(score, tokenId);
					std::sort(top.begin(), top.end(), std::greater<>());
				}
			}

			std::ostringstream stream;
			stream << std::fixed << std::setprecision(6);
			for (std::size_t index = 0; index < top.size(); ++index) {
				if (index > 0) {
					stream << ',';
				}
				stream << top[index].second << ':' << top[index].first;
			}
			return stream.str();
		}

		knf::FbankOptions CreateOnlineFbankOptions(
			std::uint32_t sampleRate,
			std::size_t nMels) {
			knf::FbankOptions options;
			options.frame_opts.samp_freq = static_cast<float>(sampleRate == 0 ? 16000U : sampleRate);
			options.frame_opts.frame_length_ms = 25.0f;
			options.frame_opts.frame_shift_ms = 10.0f;
			options.frame_opts.dither = 0.0f;
			options.frame_opts.preemph_coeff = 0.97f;
			options.frame_opts.remove_dc_offset = true;
			options.frame_opts.window_type = "povey";
			options.frame_opts.round_to_power_of_two = true;
			options.frame_opts.snip_edges = true;
			options.mel_opts.num_bins = static_cast<int32_t>(nMels);
			options.mel_opts.low_freq = 0.0f;
			options.mel_opts.high_freq = -400.0f;
			options.use_energy = false;
			options.use_log_fbank = true;
			options.use_power = true;
			return options;
		}

		std::vector<float> ScaleSamplesForOnlineFbank(
			const std::vector<float>& samples,
			SherpaFbankSampleScalingMode scalingMode) {
			if (scalingMode == SherpaFbankSampleScalingMode::NormalizedFloat) {
				return samples;
			}

			std::vector<float> scaledSamples(samples.size(), 0.0f);
			std::transform(
				samples.begin(),
				samples.end(),
				scaledSamples.begin(),
				[](float sample) {
					return sample * 32768.0f;
				});
			return scaledSamples;
		}

	} // namespace

	struct SherpaOnlineFbankFrontend {
		SherpaOnlineFbankFrontend(
			std::uint32_t initialSampleRate,
			std::size_t initialMelBinCount,
			SherpaFbankSampleScalingPolicy initialSampleScalingPolicy)
			: sampleRate(initialSampleRate == 0 ? 16000U : initialSampleRate),
			melBinCount(initialMelBinCount),
			sampleScalingPolicy(std::move(initialSampleScalingPolicy)),
			options(CreateOnlineFbankOptions(sampleRate, melBinCount)),
			fbank(options) {
		}

		void AcceptSamples(
			const std::vector<float>& samples) {
			if (samples.empty()) {
				return;
			}

			const auto scaledSamples = ScaleSamplesForOnlineFbank(
				samples,
				sampleScalingPolicy.mode);
			fbank.AcceptWaveform(options.frame_opts.samp_freq, scaledSamples.data(), static_cast<int32_t>(scaledSamples.size()));
		}

		void FinishInputOnce() {
			if (!inputFinished) {
				fbank.InputFinished();
				inputFinished = true;
			}
		}

		std::vector<float> ExtractNewFrames(
			std::size_t& outFrames) {
			outFrames = 0;
			if (melBinCount == 0) {
				return {};
			}

			const auto readyFrames = static_cast<std::size_t>((std::max)(0, fbank.NumFramesReady()));
			if (readyFrames <= consumedFrameCount) {
				return {};
			}

			outFrames = readyFrames - consumedFrameCount;
			std::vector<float> output(melBinCount * outFrames, 0.0f);
			for (std::size_t frame = 0; frame < outFrames; ++frame) {
				const float* data = fbank.GetFrame(static_cast<int32_t>(consumedFrameCount + frame));
				std::copy(
					data,
					data + static_cast<std::ptrdiff_t>(melBinCount),
					output.begin() + static_cast<std::ptrdiff_t>(frame * melBinCount));
			}
			consumedFrameCount = readyFrames;
			return output;
		}

		std::uint32_t sampleRate = 16000U;
		std::size_t melBinCount = 0;
		SherpaFbankSampleScalingPolicy sampleScalingPolicy;
		knf::FbankOptions options;
		knf::OnlineFbank fbank;
		std::size_t consumedFrameCount = 0;
		bool inputFinished = false;
	};

	namespace {

		float ComputeFrameEnergy(const std::vector<float>& samples) {
			if (samples.empty()) {
				return 0.0f;
			}

			double sumSquares = 0.0;
			for (const auto sample : samples) {
				if (!IsFiniteSample(sample)) {
					continue;
				}
				sumSquares += static_cast<double>(sample) * static_cast<double>(sample);
			}
			const double meanSquare = sumSquares / static_cast<double>(samples.size());
			return static_cast<float>(std::sqrt((std::max)(0.0, meanSquare)));
		}

		bool IsFinalStreamRequest(
			const SpeechStreamingInputContract& streamingInput,
			const std::optional<SpeechAudioArtifact>& audioArtifact) {
			if (streamingInput.source.sequenceEnd == 0) {
				return false;
			}

			if (audioArtifact.has_value() &&
				audioArtifact->handoffMode == SpeechAudioHandoffMode::PcmStream &&
				audioArtifact->sequenceEnd >= streamingInput.source.sequenceEnd &&
				audioArtifact->durationMs > 0) {
				return true;
			}

			return streamingInput.cursor.nextSequence >= streamingInput.source.sequenceEnd;
		}

		std::string ToLowerCopy(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		bool ContainsIgnoreCase(const std::string& value, const std::string& needle) {
			const auto lowered = ToLowerCopy(value);
			return lowered.find(ToLowerCopy(needle)) != std::string::npos;
		}

		std::string NormalizeEncoderStateName(std::string value) {
			value = ToLowerCopy(std::move(value));
			for (const auto& prefix : { std::string("new_"), std::string("old_"), std::string("in_") }) {
				if (value.rfind(prefix, 0) == 0) {
					value.erase(0, prefix.size());
				}
			}
			if (value.rfind("new", 0) == 0) {
				value.erase(0, 3);
			}
			if (value.rfind("old", 0) == 0) {
				value.erase(0, 3);
			}
			return value;
		}

		std::wstring ToWideString(const std::string& value) {
			return std::wstring(value.begin(), value.end());
		}

		std::wstring ShapeToWideString(const std::vector<std::int64_t>& shape) {
			std::wstringstream stream;
			stream << L"[";
			for (std::size_t i = 0; i < shape.size(); ++i) {
				if (i > 0) {
					stream << L",";
				}
				stream << shape[i];
			}
			stream << L"]";
			return stream.str();
		}

		const wchar_t* TensorBindingKindToWideString(
			SherpaZipformerStreamingEngine::TensorBindingKind kind) {
			switch (kind) {
			case SherpaZipformerStreamingEngine::TensorBindingKind::Features: return L"features";
			case SherpaZipformerStreamingEngine::TensorBindingKind::FeatureLengths: return L"feature_lengths";
			case SherpaZipformerStreamingEngine::TensorBindingKind::EncoderOut: return L"encoder_out";
			case SherpaZipformerStreamingEngine::TensorBindingKind::EncoderOutLengths: return L"encoder_out_lengths";
			case SherpaZipformerStreamingEngine::TensorBindingKind::EncoderState: return L"encoder_state";
			case SherpaZipformerStreamingEngine::TensorBindingKind::ProcessedLengths: return L"processed_lengths";
			case SherpaZipformerStreamingEngine::TensorBindingKind::DecoderInputTokens: return L"decoder_input_tokens";
			case SherpaZipformerStreamingEngine::TensorBindingKind::DecoderOut: return L"decoder_out";
			case SherpaZipformerStreamingEngine::TensorBindingKind::JoinerLogits: return L"joiner_logits";
			case SherpaZipformerStreamingEngine::TensorBindingKind::UnknownInt64: return L"unknown_int64";
			case SherpaZipformerStreamingEngine::TensorBindingKind::UnknownFloat: return L"unknown_float";
			default: return L"unknown";
			}
		}

		bool ShapeHasDynamicDimension(const std::vector<std::int64_t>& shape) {
			return std::any_of(shape.begin(), shape.end(), [](std::int64_t dim) {
				return dim <= 0;
			});
		}

		std::uint64_t ComputeStaticElementCount(
			const std::vector<std::int64_t>& shape) {
			if (shape.empty()) {
				return 0;
			}

			std::uint64_t elements = 1;
			for (const auto dim : shape) {
				if (dim <= 0) {
					return 0;
				}
				elements *= static_cast<std::uint64_t>(dim);
			}
			return elements;
		}

		std::optional<std::vector<std::int64_t>> ResolveStateCacheInputShape(
			const SherpaZipformerStreamingEngine::EncoderStateCacheBinding& mapping,
			std::size_t cachedElementCount) {
			std::vector<std::int64_t> shape = mapping.inputShape;
			if (shape.empty()) {
				shape = mapping.outputShape;
			}

			if (shape.empty()) {
				return std::nullopt;
			}

			const std::uint64_t inputElements = ComputeStaticElementCount(shape);
			if (inputElements > 0) {
				return shape;
			}

			const std::uint64_t outputElements = ComputeStaticElementCount(mapping.outputShape);
			if (outputElements > 0) {
				shape = mapping.outputShape;
				return shape;
			}

			std::int64_t knownProduct = 1;
			std::size_t dynamicIndex = shape.size();
			std::size_t dynamicCount = 0;
			for (std::size_t index = 0; index < shape.size(); ++index) {
				if (shape[index] <= 0) {
					dynamicIndex = index;
					++dynamicCount;
				}
				else {
					knownProduct *= shape[index];
				}
			}

			if (dynamicCount == 1 && knownProduct > 0) {
				const auto known = static_cast<std::size_t>(knownProduct);
				if (cachedElementCount == 0) {
					shape[dynamicIndex] = 1;
					return shape;
				}
				if (cachedElementCount % known != 0) {
					return std::nullopt;
				}
				shape[dynamicIndex] = static_cast<std::int64_t>(cachedElementCount / known);
				return shape;
			}

			if (shape.size() == 1 && shape[0] <= 0) {
				if (cachedElementCount == 0) {
					shape[0] = 1;
					return shape;
				}
				shape[0] = static_cast<std::int64_t>(cachedElementCount);
				return shape;
			}

			if (cachedElementCount == 0) {
				for (auto& dim : shape) {
					if (dim <= 0) {
						dim = 1;
					}
				}
				return shape;
			}

			return std::nullopt;
		}

		std::uint64_t ComputeResolvedElementCount(
			const std::vector<std::int64_t>& shape) {
			const auto count = ComputeStaticElementCount(shape);
			return count == 0 ? 0 : count;
		}

		std::size_t CountDynamicDimensions(
			const std::vector<std::int64_t>& shape,
			std::size_t* dynamicIndex = nullptr,
			std::int64_t* knownProduct = nullptr) {
			std::size_t dynamicCount = 0;
			std::int64_t product = 1;
			std::size_t lastDynamicIndex = shape.size();
			for (std::size_t index = 0; index < shape.size(); ++index) {
				if (shape[index] <= 0) {
					lastDynamicIndex = index;
					++dynamicCount;
				}
				else {
					product *= shape[index];
				}
			}

			if (dynamicIndex != nullptr) {
				*dynamicIndex = lastDynamicIndex;
			}
			if (knownProduct != nullptr) {
				*knownProduct = product;
			}
			return dynamicCount;
		}

		std::optional<std::vector<std::int64_t>> ResolveTensorShapeForElementCount(
			std::vector<std::int64_t> shape,
			std::size_t elementCount) {
			if (shape.empty()) {
				return std::nullopt;
			}

			const auto staticElements = ComputeStaticElementCount(shape);
			if (staticElements > 0) {
				return staticElements == elementCount ? std::optional{ shape } : std::nullopt;
			}

			std::size_t dynamicIndex = shape.size();
			std::int64_t knownProduct = 1;
			const auto dynamicCount = CountDynamicDimensions(shape, &dynamicIndex, &knownProduct);
			if (dynamicCount == 1 && knownProduct > 0) {
				const auto known = static_cast<std::size_t>(knownProduct);
				if (known > 0 && elementCount % known == 0) {
					shape[dynamicIndex] = static_cast<std::int64_t>(elementCount / known);
					return shape;
				}
			}

			return std::nullopt;
		}

		std::optional<std::vector<std::int64_t>> ResolveDecoderInputShape(
			const SherpaZipformerStreamingEngine::TensorBinding& binding,
			std::size_t contextSize) {
			std::vector<std::int64_t> shape = binding.shape;
			if (shape.empty()) {
				shape = { 1, static_cast<std::int64_t>(contextSize) };
			}

			if (shape.size() == 1) {
				shape[0] = static_cast<std::int64_t>(contextSize);
				return shape;
			}

			shape[0] = 1;
			shape[shape.size() - 1] = static_cast<std::int64_t>(contextSize);
			for (std::size_t index = 1; index + 1 < shape.size(); ++index) {
				if (shape[index] <= 0) {
					shape[index] = 1;
				}
			}
			return shape;
		}

		std::optional<std::vector<std::int64_t>> ResolveJoinerInputShape(
			const SherpaZipformerStreamingEngine::TensorBinding& binding,
			std::size_t vectorSize) {
			std::vector<std::int64_t> shape = binding.shape;
			if (shape.empty()) {
				shape = { 1, static_cast<std::int64_t>(vectorSize) };
			}

			if (shape.size() == 1) {
				shape[0] = static_cast<std::int64_t>(vectorSize);
				return shape;
			}

			shape[0] = 1;
			shape[shape.size() - 1] = static_cast<std::int64_t>(vectorSize);
			for (std::size_t index = 1; index + 1 < shape.size(); ++index) {
				if (shape[index] <= 0) {
					shape[index] = 1;
				}
			}
			return shape;
		}

		std::string FormatDecoderJoinerContractError(
			const char* operation,
			const std::string& name,
			const std::vector<std::int64_t>& modelShape,
			const std::vector<std::int64_t>& resolvedShape,
			std::size_t expectedElements,
			std::size_t actualElements) {
			std::ostringstream stream;
			stream << "decoder/joiner " << operation
				<< " mismatch name=" << name
				<< " modelShape=" << FormatShape(modelShape)
				<< " resolvedShape=" << FormatShape(resolvedShape)
				<< " expectedElements=" << expectedElements
				<< " actualElements=" << actualElements;
			return stream.str();
		}

		std::string FormatStateCacheContractError(
			const char* operation,
			const SherpaZipformerStreamingEngine::EncoderStateCacheBinding& mapping,
			const std::vector<std::int64_t>& shape,
			std::size_t expectedElements,
			std::size_t actualElements) {
			std::ostringstream stream;
			stream << "state cache " << operation
				<< " mismatch normalized=" << mapping.normalizedStateName
				<< " cacheIndex=" << mapping.cacheIndex
				<< " inputShape=" << FormatShape(mapping.inputShape)
				<< " outputShape=" << FormatShape(mapping.outputShape)
				<< " resolvedShape=" << FormatShape(shape)
				<< " expectedElements=" << expectedElements
				<< " actualElements=" << actualElements;
			return stream.str();
		}

		std::string FormatStateCacheSummary(
			const std::vector<SherpaZipformerStreamingEngine::EncoderStateCacheBinding>& mappings) {
			std::ostringstream stream;
			stream << "mappings=" << mappings.size();
			for (std::size_t index = 0; index < mappings.size(); ++index) {
				const auto& mapping = mappings[index];
				stream << (index == 0 ? ";" : "|")
					<< mapping.normalizedStateName
					<< ":type=" << (mapping.isInt64 ? "int64" : "float")
					<< ",cacheIndex=" << mapping.cacheIndex
					<< ",input=" << FormatShape(mapping.inputShape)
					<< ",output=" << FormatShape(mapping.outputShape)
					<< ",inputElements=" << mapping.inputStaticElementCount
					<< ",outputElements=" << mapping.outputStaticElementCount;
			}
			return stream.str();
		}

		bool IsLengthLikeName(const std::string& loweredName) {
			return loweredName.find("x_lens") != std::string::npos ||
				loweredName.find("processed_lens") != std::string::npos ||
				loweredName.find("length") != std::string::npos ||
				loweredName.find("lens") != std::string::npos;
		}

		bool IsStateLikeName(const std::string& loweredName) {
			return loweredName.find("cached") != std::string::npos ||
				loweredName.find("cache") != std::string::npos ||
				loweredName.find("processed_lens") != std::string::npos;
		}


#if BLAZECLAW_HAS_ONNXRUNTIME
		SherpaZipformerStreamingEngine::TensorBindingKind ClassifyInputBindingKind(
			const std::string& name,
			bool isEncoder,
			bool isJoiner,
			ONNXTensorElementDataType elementType) {
			const auto lowered = ToLowerCopy(name);
			if (isEncoder) {
				if (lowered.find("processed_lens") != std::string::npos) {
					return SherpaZipformerStreamingEngine::TensorBindingKind::ProcessedLengths;
				}
				if (IsStateLikeName(lowered)) {
					return SherpaZipformerStreamingEngine::TensorBindingKind::EncoderState;
				}
				if (IsLengthLikeName(lowered)) {
					return SherpaZipformerStreamingEngine::TensorBindingKind::FeatureLengths;
				}
				if (lowered == "x" ||
					lowered.find("speech") != std::string::npos ||
					lowered.find("feat") != std::string::npos ||
					lowered.find("input") != std::string::npos) {
					return SherpaZipformerStreamingEngine::TensorBindingKind::Features;
				}
			}

			if (!isEncoder && !isJoiner &&
				(lowered == "y" || lowered.find("token") != std::string::npos || lowered.find("decoder_input") != std::string::npos)) {
				return SherpaZipformerStreamingEngine::TensorBindingKind::DecoderInputTokens;
			}
			if (lowered.find("encoder_out") != std::string::npos || lowered.find("encoder") != std::string::npos) {
				return SherpaZipformerStreamingEngine::TensorBindingKind::EncoderOut;
			}
			if (lowered.find("decoder_out") != std::string::npos || lowered.find("decoder") != std::string::npos) {
				return SherpaZipformerStreamingEngine::TensorBindingKind::DecoderOut;
			}

			return elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64
				? SherpaZipformerStreamingEngine::TensorBindingKind::UnknownInt64
				: SherpaZipformerStreamingEngine::TensorBindingKind::UnknownFloat;
		}

		SherpaZipformerStreamingEngine::TensorBindingKind ClassifyOutputBindingKind(
			const std::string& name,
			bool isEncoder,
			bool isJoiner,
			ONNXTensorElementDataType elementType) {
			const auto lowered = ToLowerCopy(name);
			if (isEncoder) {
				if (lowered.find("processed_lens") != std::string::npos) {
					return SherpaZipformerStreamingEngine::TensorBindingKind::ProcessedLengths;
				}
				if (IsLengthLikeName(lowered)) {
					return SherpaZipformerStreamingEngine::TensorBindingKind::EncoderOutLengths;
				}
				if (IsStateLikeName(lowered)) {
					return SherpaZipformerStreamingEngine::TensorBindingKind::EncoderState;
				}
				if (elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
					return SherpaZipformerStreamingEngine::TensorBindingKind::EncoderOut;
				}
			}
			if (isJoiner && (lowered.find("logit") != std::string::npos || lowered.find("out") != std::string::npos)) {
				return SherpaZipformerStreamingEngine::TensorBindingKind::JoinerLogits;
			}
			if (!isEncoder && !isJoiner && elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
				return SherpaZipformerStreamingEngine::TensorBindingKind::DecoderOut;
			}
			return elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64
				? SherpaZipformerStreamingEngine::TensorBindingKind::UnknownInt64
				: SherpaZipformerStreamingEngine::TensorBindingKind::UnknownFloat;
		}

		std::vector<SherpaZipformerStreamingEngine::TensorBinding> BuildTensorBindings(
			Ort::Session& session,
			bool isEncoder,
			bool isJoiner,
			std::size_t& outLikelyMainOutputIndex,
			std::vector<std::string>& outOutputNames,
			std::vector<SherpaZipformerStreamingEngine::TensorBinding>* outOutputBindings = nullptr) {
			Ort::AllocatorWithDefaultOptions allocator;
			const auto inputCount = session.GetInputCount();
			std::vector<SherpaZipformerStreamingEngine::TensorBinding> bindings;
			bindings.reserve(inputCount);
			std::size_t encoderInt64StateCount = 0;
			std::size_t encoderFloatStateCount = 0;
			std::size_t unknownInt64Count = 0;
			std::size_t unknownFloatCount = 0;

			for (std::size_t i = 0; i < inputCount; ++i) {
				auto inputNameAlloc = session.GetInputNameAllocated(i, allocator);
				SherpaZipformerStreamingEngine::TensorBinding binding;
				binding.name = inputNameAlloc.get();
				binding.ordinal = i;
				binding.normalizedStateName = NormalizeEncoderStateName(binding.name);
				auto tensorInfo = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
				binding.shape = tensorInfo.GetShape();
				binding.elementType = tensorInfo.GetElementType();
				binding.hasDynamicShape = ShapeHasDynamicDimension(binding.shape);
				const auto lowered = ToLowerCopy(binding.name);
				binding.isLengthLike = IsLengthLikeName(lowered);
				binding.kind = ClassifyInputBindingKind(binding.name, isEncoder, isJoiner, binding.elementType);
				binding.isStateTensor =
					binding.kind == SherpaZipformerStreamingEngine::TensorBindingKind::EncoderState ||
					binding.kind == SherpaZipformerStreamingEngine::TensorBindingKind::ProcessedLengths;
				if (binding.isStateTensor) {
					if (binding.elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
						binding.bufferIndex = encoderInt64StateCount++;
					}
					else {
						binding.bufferIndex = encoderFloatStateCount++;
					}
				}

				if (binding.kind == SherpaZipformerStreamingEngine::TensorBindingKind::UnknownFloat ||
					binding.kind == SherpaZipformerStreamingEngine::TensorBindingKind::UnknownInt64) {
					if (binding.elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
						binding.kind = SherpaZipformerStreamingEngine::TensorBindingKind::UnknownInt64;
						binding.bufferIndex = unknownInt64Count++;
					}
					else {
						binding.kind = SherpaZipformerStreamingEngine::TensorBindingKind::UnknownFloat;
						binding.bufferIndex = unknownFloatCount++;
					}
				}

				bindings.push_back(std::move(binding));
			}

			const auto outputCount = session.GetOutputCount();
			outOutputNames.clear();
			outOutputNames.reserve(outputCount);
			if (outOutputBindings != nullptr) {
				outOutputBindings->clear();
				outOutputBindings->reserve(outputCount);
			}
			outLikelyMainOutputIndex = 0;
			int bestMainScore = (std::numeric_limits<int>::min)();
			for (std::size_t i = 0; i < outputCount; ++i) {
				auto outputNameAlloc = session.GetOutputNameAllocated(i, allocator);
				outOutputNames.push_back(outputNameAlloc.get());
				SherpaZipformerStreamingEngine::TensorBinding outputBinding;
				outputBinding.name = outOutputNames.back();
				outputBinding.ordinal = i;
				outputBinding.normalizedStateName = NormalizeEncoderStateName(outputBinding.name);

				int score = 0;
				const std::string lowered = ToLowerCopy(outOutputNames.back());
				const bool isLens = IsLengthLikeName(lowered);
				if (isLens) {
					score -= 30;
				}

				try {
					auto outInfo = session.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
					const auto outType = outInfo.GetElementType();
					outputBinding.shape = outInfo.GetShape();
					outputBinding.elementType = outType;
					outputBinding.hasDynamicShape = ShapeHasDynamicDimension(outputBinding.shape);
					outputBinding.isLengthLike = isLens;
					outputBinding.kind = ClassifyOutputBindingKind(outputBinding.name, isEncoder, isJoiner, outType);
					outputBinding.isStateTensor =
						outputBinding.kind == SherpaZipformerStreamingEngine::TensorBindingKind::EncoderState ||
						outputBinding.kind == SherpaZipformerStreamingEngine::TensorBindingKind::ProcessedLengths;
					if (outType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
						score += 20;
					}
					else {
						score -= 20;
					}
				}
				catch (...) {
					// Keep heuristic-name based score only.
				}
				if (outOutputBindings != nullptr) {
					outOutputBindings->push_back(std::move(outputBinding));
				}

				if (isJoiner) {
					if (lowered.find("logit") != std::string::npos) {
						score += 30;
					}
					if (lowered.find("joiner") != std::string::npos || lowered.find("out") != std::string::npos) {
						score += 5;
					}
				}
				else if (isEncoder) {
					if (lowered.find("encoder_out") != std::string::npos) {
						score += 30;
					}
					else if (lowered.find("encoder") != std::string::npos) {
						score += 15;
					}
					if (lowered.find("out") != std::string::npos) {
						score += 5;
					}
				}
				else {
					if (lowered.find("decoder_out") != std::string::npos) {
						score += 30;
					}
					else if (lowered.find("decoder") != std::string::npos) {
						score += 15;
					}
					if (lowered.find("out") != std::string::npos) {
						score += 5;
					}
				}

				if (score > bestMainScore) {
					bestMainScore = score;
					outLikelyMainOutputIndex = i;
				}
			}

			return bindings;
		}

		void TraceTensorBindings(
			const wchar_t* sessionName,
			const std::vector<SherpaZipformerStreamingEngine::TensorBinding>& bindings,
			const std::vector<std::string>& outputNames,
			std::size_t mainOutputIndex) {
			TRACE(L"[SherpaStreaming][Bindings] session=%s mainOutputIndex=%llu outputCount=%llu\n",
				sessionName,
				static_cast<unsigned long long>(mainOutputIndex),
				static_cast<unsigned long long>(outputNames.size()));
			for (const auto& binding : bindings) {
				TRACE(L"[SherpaStreaming][Bindings] session=%s input name=%s normalized=%s kind=%s shape=%s bufferIndex=%llu\n",
					sessionName,
					ToWideString(binding.name).c_str(),
					ToWideString(binding.normalizedStateName).c_str(),
					TensorBindingKindToWideString(binding.kind),
					ShapeToWideString(binding.shape).c_str(),
					static_cast<unsigned long long>(binding.bufferIndex));
			}
			for (std::size_t i = 0; i < outputNames.size(); ++i) {
				TRACE(L"[SherpaStreaming][Bindings] session=%s output index=%llu name=%s normalized=%s%s\n",
					sessionName,
					static_cast<unsigned long long>(i),
					ToWideString(outputNames[i]).c_str(),
					ToWideString(NormalizeEncoderStateName(outputNames[i])).c_str(),
					i == mainOutputIndex ? L" main" : L"");
			}
		}

		std::vector<SherpaZipformerStreamingEngine::EncoderStateCacheBinding> BuildEncoderStateCacheBindings(
			const std::vector<SherpaZipformerStreamingEngine::TensorBinding>& inputBindings,
			const std::vector<SherpaZipformerStreamingEngine::TensorBinding>& outputBindings) {
			std::vector<SherpaZipformerStreamingEngine::EncoderStateCacheBinding> mappings;
			for (std::size_t inputIndex = 0; inputIndex < inputBindings.size(); ++inputIndex) {
				const auto& input = inputBindings[inputIndex];
				if (!input.isStateTensor) {
					continue;
				}
				for (std::size_t outputIndex = 0; outputIndex < outputBindings.size(); ++outputIndex) {
					const auto& output = outputBindings[outputIndex];
					if (!output.isStateTensor || output.normalizedStateName != input.normalizedStateName) {
						continue;
					}
					if (output.elementType != input.elementType) {
						continue;
					}
					SherpaZipformerStreamingEngine::EncoderStateCacheBinding mapping;
					mapping.inputBindingIndex = inputIndex;
					mapping.outputBindingIndex = outputIndex;
					mapping.cacheIndex = input.bufferIndex;
					mapping.normalizedStateName = input.normalizedStateName;
					mapping.inputShape = input.shape;
					mapping.outputShape = output.shape;
					mapping.inputStaticElementCount = ComputeStaticElementCount(input.shape);
					mapping.outputStaticElementCount = ComputeStaticElementCount(output.shape);
					mapping.inputHasDynamicShape = input.hasDynamicShape;
					mapping.outputHasDynamicShape = output.hasDynamicShape;
					mapping.isInt64 = input.elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
					mappings.push_back(std::move(mapping));
					break;
				}
			}
			return mappings;
		}

		void TraceEncoderStateCacheBindings(
			const std::vector<SherpaZipformerStreamingEngine::TensorBinding>& inputBindings,
			const std::vector<SherpaZipformerStreamingEngine::TensorBinding>& outputBindings,
			const std::vector<SherpaZipformerStreamingEngine::EncoderStateCacheBinding>& mappings) {
			TRACE(L"[SherpaStreaming][CacheMap] mappingCount=%llu\n", static_cast<unsigned long long>(mappings.size()));
			for (const auto& mapping : mappings) {
				const auto& input = inputBindings[mapping.inputBindingIndex];
				const auto& output = outputBindings[mapping.outputBindingIndex];
				TRACE(L"[SherpaStreaming][CacheMap] input=%s output=%s normalized=%s cacheIndex=%llu type=%s inputShape=%s outputShape=%s inputElements=%llu outputElements=%llu\n",
					ToWideString(input.name).c_str(),
					ToWideString(output.name).c_str(),
					ToWideString(mapping.normalizedStateName).c_str(),
					static_cast<unsigned long long>(mapping.cacheIndex),
					mapping.isInt64 ? L"int64" : L"float",
					ShapeToWideString(mapping.inputShape).c_str(),
					ShapeToWideString(mapping.outputShape).c_str(),
					static_cast<unsigned long long>(mapping.inputStaticElementCount),
					static_cast<unsigned long long>(mapping.outputStaticElementCount));
			}
		}
#endif

	} // namespace

	bool SherpaZipformerStreamingEngine::Load(
		const std::filesystem::path& rootPath,
		const SpeechModelLayoutProbeResult& layout,
		std::string& outError) {
		outError.clear();
		m_loaded = false;
		m_artifacts = {};
		m_tokenById.clear();
		m_blankId = 0;
		m_eosId = 1;
		m_unkId = 2;
#if BLAZECLAW_HAS_ONNXRUNTIME
		m_encoderInputBindings.clear();
		m_encoderOutputBindings.clear();
		m_encoderStateCacheBindings.clear();
		m_encoderOutputNames.clear();
		m_decoderInputBindings.clear();
		m_decoderOutputNames.clear();
		m_joinerInputBindings.clear();
		m_joinerOutputNames.clear();
#endif

		if (layout.kind != SpeechModelLayoutKind::SherpaZipformerTransducer) {
			outError = "sherpa layout not detected";
			return false;
		}

		m_artifacts.encoderPath = layout.sherpaEncoderPath;
		m_artifacts.decoderPath = layout.sherpaDecoderPath;
		m_artifacts.joinerPath = layout.sherpaJoinerPath;
		m_artifacts.tokensPath = layout.sherpaTokensPath;
		m_artifacts.bpeModelPath = m_artifacts.tokensPath.parent_path() / L"bpe.model";
		m_artifacts.bpeVocabPath = m_artifacts.tokensPath.parent_path() / L"bpe.vocab";
		m_artifacts.bpeModelPresent = std::filesystem::exists(m_artifacts.bpeModelPath);
		m_artifacts.bpeVocabPresent = std::filesystem::exists(m_artifacts.bpeVocabPath);

		if (!std::filesystem::exists(m_artifacts.encoderPath) ||
			!std::filesystem::exists(m_artifacts.decoderPath) ||
			!std::filesystem::exists(m_artifacts.joinerPath) ||
			!std::filesystem::exists(m_artifacts.tokensPath)) {
			outError = "required sherpa artifacts are missing";
			return false;
		}

		m_artifacts.tokenCount = CountTokens(m_artifacts.tokensPath);
		if (m_artifacts.tokenCount == 0) {
			outError = "tokens file is empty";
			return false;
		}

		std::ifstream tokenStream(m_artifacts.tokensPath);
		if (!tokenStream.is_open()) {
			outError = "failed to open tokens file";
			return false;
		}

		std::string tokenLine;
		while (std::getline(tokenStream, tokenLine)) {
			if (tokenLine.empty()) {
				continue;
			}

			const auto sep = tokenLine.find_last_of(" \t");
			if (sep == std::string::npos || sep + 1 >= tokenLine.size()) {
				continue;
			}

			const auto token = tokenLine.substr(0, sep);
			const auto idText = tokenLine.substr(sep + 1);
			try {
				const auto tokenId = static_cast<std::int64_t>(std::stoll(idText));
				m_tokenById[tokenId] = token;
				if (token == "<blk>" || token == "<blank>") {
					m_blankId = tokenId;
				}
				else if (token == "<sos/eos>" || token == "<eos>") {
					m_eosId = tokenId;
				}
				else if (token == "<unk>") {
					m_unkId = tokenId;
				}
			}
			catch (...) {
				continue;
			}
		}

		if (m_tokenById.empty()) {
			outError = "failed to parse tokens.txt";
			return false;
		}

#if !BLAZECLAW_HAS_ONNXRUNTIME
		outError = "onnxruntime headers not available at compile time";
		return false;
#else
		try {
			m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "blazeclaw-sherpa-streaming");
			m_options = std::make_unique<Ort::SessionOptions>();
			m_options->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

			m_encoderSession = std::make_unique<Ort::Session>(
				*m_env,
				m_artifacts.encoderPath.c_str(),
				*m_options);
			m_decoderSession = std::make_unique<Ort::Session>(
				*m_env,
				m_artifacts.decoderPath.c_str(),
				*m_options);
			m_joinerSession = std::make_unique<Ort::Session>(
				*m_env,
				m_artifacts.joinerPath.c_str(),
				*m_options);

			m_encoderInputBindings = BuildTensorBindings(
				*m_encoderSession,
				true,
				false,
				m_encoderMainOutputIndex,
				m_encoderOutputNames,
				&m_encoderOutputBindings);
			m_decoderInputBindings = BuildTensorBindings(
				*m_decoderSession,
				false,
				false,
				m_decoderMainOutputIndex,
				m_decoderOutputNames);
			m_joinerInputBindings = BuildTensorBindings(
				*m_joinerSession,
				false,
				true,
				m_joinerMainOutputIndex,
				m_joinerOutputNames);

			TraceTensorBindings(
				L"encoder",
				m_encoderInputBindings,
				m_encoderOutputNames,
				m_encoderMainOutputIndex);
			m_encoderStateCacheBindings = BuildEncoderStateCacheBindings(
				m_encoderInputBindings,
				m_encoderOutputBindings);
			TraceEncoderStateCacheBindings(
				m_encoderInputBindings,
				m_encoderOutputBindings,
				m_encoderStateCacheBindings);
			TraceTensorBindings(
				L"decoder",
				m_decoderInputBindings,
				m_decoderOutputNames,
				m_decoderMainOutputIndex);
			TraceTensorBindings(
				L"joiner",
				m_joinerInputBindings,
				m_joinerOutputNames,
				m_joinerMainOutputIndex);

			m_decoderContextSize = 2;
			for (const auto& binding : m_decoderInputBindings) {
				if (binding.kind == TensorBindingKind::DecoderInputTokens &&
					binding.shape.size() >= 2 &&
					binding.shape[1] > 0) {
					m_decoderContextSize = static_cast<std::size_t>(binding.shape[1]);
					break;
				}
			}
		}
		catch (const std::exception& ex) {
			outError = ex.what();
			m_env.reset();
			m_options.reset();
			m_encoderSession.reset();
			m_decoderSession.reset();
			m_joinerSession.reset();
			return false;
		}
#endif

		UNREFERENCED_PARAMETER(rootPath);
		m_loaded = true;
		return true;
	}

	bool SherpaZipformerStreamingEngine::IsLoaded() const {
		return m_loaded;
	}

	const SherpaZipformerStreamingEngine::LoadedArtifacts&
	SherpaZipformerStreamingEngine::Artifacts() const {
		return m_artifacts;
	}

	SherpaZipformerStreamingEngine::TensorBindingKind
		SherpaZipformerStreamingEngine::ClassifyBindingKind(
			const std::string& name,
			bool isEncoder,
			bool isJoinerOutput) {
		UNREFERENCED_PARAMETER(isJoinerOutput);
		const auto lowered = ToLowerCopy(name);
		if (isEncoder) {
			if (lowered.find("x_lens") != std::string::npos ||
				lowered.find("lens") != std::string::npos ||
				lowered.find("length") != std::string::npos) {
				return TensorBindingKind::FeatureLengths;
			}
			if (lowered == "x" ||
				lowered.find("speech") != std::string::npos ||
				lowered.find("feat") != std::string::npos ||
				lowered.find("input") != std::string::npos) {
				return TensorBindingKind::Features;
			}
		}

		if (lowered == "y" ||
			lowered.find("token") != std::string::npos ||
			lowered.find("decoder_input") != std::string::npos) {
			return TensorBindingKind::DecoderInputTokens;
		}
		if (lowered.find("encoder_out") != std::string::npos ||
			lowered.find("encoder") != std::string::npos) {
			return TensorBindingKind::EncoderOut;
		}
		if (lowered.find("decoder_out") != std::string::npos ||
			lowered.find("decoder") != std::string::npos) {
			return TensorBindingKind::DecoderOut;
		}

		return TensorBindingKind::UnknownFloat;
	}

	std::string SherpaZipformerStreamingEngine::DecodeTokenPiece(
		const std::string& piece) {
		if (piece.size() == 6 &&
			piece[0] == '<' &&
			(piece[1] == '0') &&
			(piece[2] == 'x' || piece[2] == 'X') &&
			piece[5] == '>') {
			auto hexValue = [](char ch) -> int {
				if (ch >= '0' && ch <= '9') {
					return ch - '0';
				}
				if (ch >= 'a' && ch <= 'f') {
					return 10 + (ch - 'a');
				}
				if (ch >= 'A' && ch <= 'F') {
					return 10 + (ch - 'A');
				}
				return -1;
			};

			const int hi = hexValue(piece[3]);
			const int lo = hexValue(piece[4]);
			if (hi >= 0 && lo >= 0) {
				const unsigned char byteValue = static_cast<unsigned char>((hi << 4) | lo);
				return std::string(1, static_cast<char>(byteValue));
			}
		}

		std::string token = piece;
		std::size_t pos = 0;
		while ((pos = token.find("▁", pos)) != std::string::npos) {
			token.replace(pos, std::string("▁").size(), " ");
			pos += 1;
		}
		return token;
	}

	bool SherpaZipformerStreamingEngine::IsSpecialTokenPiece(
		const std::string& piece) {
		return piece == "<blk>" ||
			piece == "<blank>" ||
			piece == "<sos/eos>" ||
			piece == "<eos>";
	}

	std::string SherpaZipformerStreamingEngine::NormalizeDecodedBpeText(
		std::string text) {
		auto replaceAll = [](std::string& value, const std::string& from, const std::string& to) {
			std::size_t pos = 0;
			while ((pos = value.find(from, pos)) != std::string::npos) {
				value.replace(pos, from.size(), to);
				pos += to.size();
			}
		};

		replaceAll(text, "▁", " ");
		replaceAll(text, "@@ ", "");
		replaceAll(text, "@@", "");
		replaceAll(text, " ##", "");
		replaceAll(text, "##", "");

		std::string compact;
		compact.reserve(text.size());
		bool previousWhitespace = false;
		for (const unsigned char ch : text) {
			const bool whitespace = ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
			if (whitespace) {
				if (!previousWhitespace) {
					compact.push_back(' ');
				}
				previousWhitespace = true;
				continue;
			}
			compact.push_back(static_cast<char>(ch));
			previousWhitespace = false;
		}

		while (!compact.empty() && std::isspace(static_cast<unsigned char>(compact.front()))) {
			compact.erase(compact.begin());
		}
		while (!compact.empty() && std::isspace(static_cast<unsigned char>(compact.back()))) {
			compact.pop_back();
		}
		return compact;
	}

	std::string SherpaZipformerStreamingEngine::DecodeTokenIdsToText(
		const std::vector<std::int64_t>& tokenIds) const {
		std::string text;
		for (const auto tokenId : tokenIds) {
			const auto it = m_tokenById.find(tokenId);
			if (it == m_tokenById.end()) {
				continue;
			}

			const auto& piece = it->second;
			if (IsSpecialTokenPiece(piece)) {
				continue;
			}
			if (tokenId == m_unkId || piece == "<unk>") {
				text.push_back('?');
				continue;
			}
			text += DecodeTokenPiece(piece);
		}

		return NormalizeDecodedBpeText(std::move(text));
	}

	std::string SherpaZipformerStreamingEngine::EscapeJsonString(
		const std::string& value) {
		std::ostringstream stream;
		for (const unsigned char ch : value) {
			switch (ch) {
			case '\\': stream << "\\\\"; break;
			case '"': stream << "\\\""; break;
			case '\b': stream << "\\b"; break;
			case '\f': stream << "\\f"; break;
			case '\n': stream << "\\n"; break;
			case '\r': stream << "\\r"; break;
			case '\t': stream << "\\t"; break;
			default:
				if (ch < 0x20) {
					stream << "\\u"
						<< std::hex
						<< std::setw(4)
						<< std::setfill('0')
						<< static_cast<int>(ch)
						<< std::dec;
				}
				else {
					stream << static_cast<char>(ch);
				}
				break;
			}
		}
		return stream.str();
	}

	std::string SherpaZipformerStreamingEngine::JoinTokenIds(
		const std::vector<std::int64_t>& tokenIds) {
		std::ostringstream stream;
		for (std::size_t i = 0; i < tokenIds.size(); ++i) {
			if (i > 0) {
				stream << ' ';
			}
			stream << tokenIds[i];
		}
		return stream.str();
	}

	std::string SherpaZipformerStreamingEngine::JoinTokenPieces(
		const std::vector<std::int64_t>& tokenIds) const {
		std::ostringstream stream;
		for (std::size_t i = 0; i < tokenIds.size(); ++i) {
			if (i > 0) {
				stream << ' ';
			}
			const auto it = m_tokenById.find(tokenIds[i]);
			if (it == m_tokenById.end()) {
				stream << "<missing:" << tokenIds[i] << ">";
				continue;
			}
			stream << it->second;
		}
		return stream.str();
	}

	std::optional<std::filesystem::path> SherpaZipformerStreamingEngine::ResolveBaselineDiagnosticsDirectory() {
		std::string value = ReadEnvironmentString("BLAZECLAW_SHERPA_BASELINE_DIR");
		if (value.empty()) {
			return std::nullopt;
		}

		return std::filesystem::path(value);
	}

	std::string SherpaZipformerStreamingEngine::ResolveBaselineExpectedText() {
		return ReadEnvironmentString("BLAZECLAW_SHERPA_BASELINE_EXPECTED_TEXT");
	}

	bool SherpaZipformerStreamingEngine::IsBaselinePersistenceEnabled() {
		return ResolveBaselineDiagnosticsDirectory().has_value();
	}

	std::optional<std::filesystem::path> SherpaZipformerStreamingEngine::PersistBaselineDiagnostics(
		const SpeechTranscribeRequest& request,
		const SpeechStreamingInputContract& streamingInput,
		const StreamState& streamState,
		std::uint32_t sampleRate,
		std::size_t chunkSamples,
		std::uint64_t loopGuard,
		std::uint64_t maxLoops,
		bool finalFlush,
		const std::string& expectedText,
		const std::string& decodedText,
		const std::string& finalOutcome,
		bool hasSegment,
		bool fallbackUsed) const {
		const auto directory = ResolveBaselineDiagnosticsDirectory();
		if (!directory.has_value()) {
			return std::nullopt;
		}

		std::error_code ec;
		std::filesystem::create_directories(*directory, ec);
		if (ec) {
			return std::nullopt;
		}

		std::string fileStem = request.runId.empty() ? request.sessionId : request.runId;
		if (fileStem.empty()) {
			fileStem = streamingInput.source.streamId.empty() ? "sherpa-baseline" : streamingInput.source.streamId;
		}
		for (auto& ch : fileStem) {
			const bool safe =
				(ch >= 'a' && ch <= 'z') ||
				(ch >= 'A' && ch <= 'Z') ||
				(ch >= '0' && ch <= '9') ||
				ch == '-' ||
				ch == '_';
			if (!safe) {
				ch = '_';
			}
		}

		const auto path = *directory / (fileStem + ".sherpa-baseline.json");
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			return std::nullopt;
		}

		const std::string tokenIds = JoinTokenIds(streamState.baselineTokenIds);
		const std::string tokenPieces = JoinTokenPieces(streamState.baselineTokenIds);
		const auto repeatClassification = ClassifyDecodedRepeats(decodedText);
		std::ostringstream repeatCoverageStream;
		repeatCoverageStream << std::fixed << std::setprecision(6)
			<< repeatClassification.repeatedUnitCoverage;
		stream << "{\n";
		stream << "  \"runId\": \"" << EscapeJsonString(request.runId) << "\",\n";
		stream << "  \"sessionId\": \"" << EscapeJsonString(request.sessionId) << "\",\n";
		stream << "  \"streamId\": \"" << EscapeJsonString(streamingInput.source.streamId) << "\",\n";
		stream << "  \"audioPath\": \"" << EscapeJsonString(request.audioPath) << "\",\n";
		stream << "  \"expectedText\": \"" << EscapeJsonString(expectedText) << "\",\n";
		stream << "  \"decodedText\": \"" << EscapeJsonString(decodedText) << "\",\n";
		stream << "  \"fbankSampleScalingMode\": \"" << EscapeJsonString(streamState.contractFbankSampleScalingMode) << "\",\n";
		stream << "  \"sampleRate\": " << sampleRate << ",\n";
		stream << "  \"chunkSamples\": " << static_cast<std::uint64_t>(chunkSamples) << ",\n";
		stream << "  \"sequenceStart\": " << streamingInput.source.sequenceStart << ",\n";
		stream << "  \"sequenceEnd\": " << streamingInput.source.sequenceEnd << ",\n";
		stream << "  \"cursorNextSequence\": " << streamState.nextSequence << ",\n";
		stream << "  \"finalRemainingSamples\": "
			<< (streamingInput.source.sequenceEnd > streamState.nextSequence
				? streamingInput.source.sequenceEnd - streamState.nextSequence
				: 0ULL) << ",\n";
		stream << "  \"loopCount\": " << loopGuard << ",\n";
		stream << "  \"maxLoopCount\": " << maxLoops << ",\n";
		stream << "  \"finalFlush\": " << (finalFlush ? "true" : "false") << ",\n";
		stream << "  \"finalDrainComplete\": "
			<< (!streamingInput.source.sequenceEnd || streamState.nextSequence >= streamingInput.source.sequenceEnd
				? "true"
				: "false") << ",\n";
		stream << "  \"finalOutcome\": \"" << EscapeJsonString(finalOutcome) << "\",\n";
		stream << "  \"hasSegment\": " << (hasSegment ? "true" : "false") << ",\n";
		stream << "  \"fallbackUsed\": " << (fallbackUsed ? "true" : "false") << ",\n";
		stream << "  \"fbankFrameCount\": " << streamState.encoderFrameCount << ",\n";
		stream << "  \"featureRealFrameCount\": " << streamState.contractFeatureRealFrameCount << ",\n";
		stream << "  \"featurePaddedFrameCount\": " << streamState.contractFeaturePaddedFrameCount << ",\n";
		stream << "  \"encoderFrameCount\": " << streamState.encoderFrameCount << ",\n";
		stream << "  \"joinerCallCount\": " << streamState.joinerCallCount << ",\n";
		stream << "  \"blankTokenCount\": " << streamState.blankTokenCount << ",\n";
		stream << "  \"decodedTokenCount\": " << streamState.decodedTokenCount << ",\n";
		stream << "  \"rnntInnerLoopCount\": " << streamState.rnntInnerLoopCount << ",\n";
		stream << "  \"rnntMaxSymbolsHitCount\": " << streamState.rnntMaxSymbolsHitCount << ",\n";
		stream << "  \"rnntRepeatedTokenCount\": " << streamState.rnntRepeatedTokenCount << ",\n";
		stream << "  \"rnntMultiSymbolFrameCount\": " << streamState.rnntMultiSymbolFrameCount << ",\n";
		stream << "  \"rnntMaxSymbolsPerFrame\": " << kSherpaMaxSymbolsPerFrame << ",\n";
		stream << "  \"decodedRepeatDegenerate\": " << (repeatClassification.degenerate ? "true" : "false") << ",\n";
		stream << "  \"decodedRepeatUnit\": \"" << EscapeJsonString(repeatClassification.repeatedUnit) << "\",\n";
		stream << "  \"decodedRepeatUnitLength\": " << static_cast<std::uint64_t>(repeatClassification.repeatedUnitLength) << ",\n";
		stream << "  \"decodedRepeatUnitCount\": " << static_cast<std::uint64_t>(repeatClassification.repeatedUnitCount) << ",\n";
		stream << "  \"decodedRepeatUnitCoverage\": " << repeatCoverageStream.str() << ",\n";
		stream << "  \"decodedLongestRepeatedChar\": \"" << EscapeJsonString(repeatClassification.longestRepeatedChar) << "\",\n";
		stream << "  \"decodedLongestRepeatedCharRun\": " << static_cast<std::uint64_t>(repeatClassification.longestRepeatedCharRun) << ",\n";
		stream << "  \"encoderStateCacheValidatedUpdateCount\": " << streamState.encoderStateCacheValidatedUpdateCount << ",\n";
		stream << "  \"encoderStateCacheContractFailureCount\": " << streamState.encoderStateCacheContractFailureCount << ",\n";
		stream << "  \"encoderStateCacheSummary\": \"" << EscapeJsonString(streamState.contractStateCacheSummary) << "\",\n";
		stream << "  \"encoderStateCacheLastError\": \"" << EscapeJsonString(streamState.contractStateCacheLastError) << "\",\n";
		stream << "  \"decoderInputContext\": \"" << EscapeJsonString(streamState.contractDecoderInputContext) << "\",\n";
		stream << "  \"decoderInputShape\": \"" << EscapeJsonString(streamState.contractDecoderInputShape) << "\",\n";
		stream << "  \"decoderOutputShape\": \"" << EscapeJsonString(streamState.contractDecoderOutputShape) << "\",\n";
		stream << "  \"decoderVectorSlice\": \"" << EscapeJsonString(streamState.contractDecoderVectorSlice) << "\",\n";
		stream << "  \"joinerEncoderInputShape\": \"" << EscapeJsonString(streamState.contractJoinerEncoderInputShape) << "\",\n";
		stream << "  \"joinerDecoderInputShape\": \"" << EscapeJsonString(streamState.contractJoinerDecoderInputShape) << "\",\n";
		stream << "  \"joinerOutputShape\": \"" << EscapeJsonString(streamState.contractJoinerOutputShape) << "\",\n";
		stream << "  \"joinerLogitsSlice\": \"" << EscapeJsonString(streamState.contractJoinerLogitsSlice) << "\",\n";
		stream << "  \"decoderJoinerContractFailureCount\": " << streamState.decoderJoinerContractFailureCount << ",\n";
		stream << "  \"decoderJoinerValidatedCallCount\": " << streamState.decoderJoinerValidatedCallCount << ",\n";
		stream << "  \"decoderJoinerContractSummary\": \"" << EscapeJsonString(streamState.contractDecoderJoinerSummary) << "\",\n";
		stream << "  \"decoderJoinerLastError\": \"" << EscapeJsonString(streamState.contractDecoderJoinerLastError) << "\",\n";
		stream << "  \"featureFirstFrameStats\": \"" << EscapeJsonString(streamState.contractFeatureFirstFrameStats) << "\",\n";
		stream << "  \"featureLastFrameStats\": \"" << EscapeJsonString(streamState.contractFeatureLastFrameStats) << "\",\n";
		stream << "  \"joinerTopTokens\": \"" << EscapeJsonString(streamState.contractJoinerTopTokens) << "\",\n";
		stream << "  \"pendingSampleCount\": 0,\n";
		stream << "  \"tokenIds\": \"" << EscapeJsonString(tokenIds) << "\",\n";
		stream << "  \"tokenPieces\": \"" << EscapeJsonString(tokenPieces) << "\"\n";
		stream << "}\n";
		stream.close();

		return path;
	}

	void SherpaZipformerStreamingEngine::ClearStreamState(
		const std::string& streamId) const {
		std::lock_guard<std::mutex> lock(m_streamMutex);
		m_streamStateByStreamId.erase(streamId);
	}

	SpeechTranscribeResult SherpaZipformerStreamingEngine::TranscribeStreaming(
		const SpeechTranscribeRequest& request,
		const std::function<bool(const std::string&)>& isCancelled) const {
		SpeechTranscribeResult result;
		result.sessionState.sessionId = request.sessionId;
		result.sessionState.runId = request.runId;
		result.sessionState.audioPath = request.audioPath;
		result.sessionState.audioArtifact = request.audioArtifact;
		result.sessionState.streamingInput = request.streamingInput;
		result.sessionState.language = request.language.empty() ? "und" : request.language;
		result.sessionState.stage = SpeechSessionStage::Transcribing;

		if (!m_loaded) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
				.message = "sherpa streaming engine is not loaded",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}

		if (!request.streamingInput.has_value()) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InvalidInput,
				.message = "streamingInput is required for sherpa streaming transcription",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}

		const auto& streamingInput = *request.streamingInput;
		if (streamingInput.source.streamId.empty()) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InvalidInput,
				.message = "streamingInput.source.streamId is required",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}
#if !BLAZECLAW_HAS_ONNXRUNTIME
		result.ok = false;
		result.error = SpeechRecognitionError{
			.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
			.message = "onnxruntime is unavailable for sherpa streaming",
		};
		result.sessionState.stage = SpeechSessionStage::Failed;
		result.sessionState.error = result.error;
		return result;
#else
		if (!m_encoderSession || !m_decoderSession || !m_joinerSession) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
				.message = "sherpa encoder/decoder/joiner sessions are unavailable",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}
#endif

#if !BLAZECLAW_HAS_ONNXRUNTIME
		result.ok = false;
		result.error = SpeechRecognitionError{
			.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
			.message = "onnxruntime is unavailable for sherpa streaming",
		};
		result.sessionState.stage = SpeechSessionStage::Failed;
		result.sessionState.error = result.error;
		return result;
#else
		if (!m_encoderSession || !m_decoderSession || !m_joinerSession) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::RuntimeUnavailable,
				.message = "sherpa encoder/decoder/joiner sessions are unavailable",
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}
#endif

		const std::uint32_t sampleRate =
			streamingInput.source.sampleRate == 0
			? 16000U
			: streamingInput.source.sampleRate;
		std::uint32_t chunkMs = streamingInput.chunkPolicy.chunkMs;
		if (chunkMs == 0) {
			chunkMs = 20;
		}

		const std::uint64_t chunkSamplesRaw =
			(static_cast<std::uint64_t>(sampleRate) * chunkMs) / 1000ULL;
		const std::size_t chunkSamples = static_cast<std::size_t>((std::max)(
			std::uint64_t{ 1 },
			chunkSamplesRaw));
		const std::size_t maxSpinCount =
			streamingInput.chunkPolicy.maxSpinCount == 0
			? std::size_t{ 64 }
			: streamingInput.chunkPolicy.maxSpinCount;
		const bool isPcmStream =
			request.audioArtifact.has_value() &&
			request.audioArtifact->handoffMode == SpeechAudioHandoffMode::PcmStream;
		const bool isFinalStreamRequest =
			IsFinalStreamRequest(streamingInput, request.audioArtifact);
		const bool isLivePcmStream = isPcmStream && !isFinalStreamRequest;
		const std::string baselineExpectedText = ResolveBaselineExpectedText();
		const bool baselinePersistenceEnabled = IsBaselinePersistenceEnabled();
		const auto sampleScalingPolicy = ResolveSherpaFbankSampleScalingPolicy();
#if BLAZECLAW_HAS_ONNXRUNTIME
		const std::string encoderStateCacheSummary = FormatStateCacheSummary(m_encoderStateCacheBindings);
		std::ostringstream decoderJoinerSummaryStream;
		decoderJoinerSummaryStream
			<< "decoderInputs=" << m_decoderInputBindings.size()
			<< ";decoderOutputs=" << m_decoderOutputNames.size()
			<< ";decoderContextSize=" << m_decoderContextSize
			<< ";joinerInputs=" << m_joinerInputBindings.size()
			<< ";joinerOutputs=" << m_joinerOutputNames.size()
			<< ";blankId=" << m_blankId
			<< ";eosId=" << m_eosId;
		const std::string decoderJoinerContractSummary = decoderJoinerSummaryStream.str();
#else
		const std::string encoderStateCacheSummary;
		const std::string decoderJoinerContractSummary;
#endif

		StreamState streamState;
		{
			std::lock_guard<std::mutex> lock(m_streamMutex);
			auto& cachedState = m_streamStateByStreamId[streamingInput.source.streamId];
			if (cachedState.decoderContext.empty()) {
				cachedState.decoderContext.assign(
					(std::max)(std::size_t{ 1 },
#if BLAZECLAW_HAS_ONNXRUNTIME
						m_decoderContextSize
#else
						std::size_t{ 2 }
#endif
					),
					m_blankId);
			}
			if (cachedState.nextSequence == 0) {
				cachedState.nextSequence = streamingInput.cursor.nextSequence > 0
					? streamingInput.cursor.nextSequence
					: streamingInput.source.sequenceStart;
			}
			cachedState.contractStateCacheSummary = encoderStateCacheSummary;
			cachedState.contractDecoderJoinerSummary = decoderJoinerContractSummary;
			streamState = cachedState;
		}

		std::uint64_t nextSequence = streamState.nextSequence;

		const auto oldestOpt =
			GetStreamingAudioOldestSequence(streamingInput.source.streamId);
		if (oldestOpt.has_value() && nextSequence < *oldestOpt) {
			nextSequence = *oldestOpt;
			streamState.pendingFeatureFrames.clear();
			streamState.pendingFeatureFrameCount = 0;
			streamState.onlineFbank.reset();
		}

		constexpr std::size_t sherpaMelBinCount = 80;
		if (!streamState.onlineFbank ||
			streamState.onlineFbank->sampleRate != (sampleRate == 0 ? 16000U : sampleRate) ||
			streamState.onlineFbank->melBinCount != sherpaMelBinCount ||
			streamState.onlineFbank->sampleScalingPolicy.diagnosticName != sampleScalingPolicy.diagnosticName) {
			streamState.onlineFbank = std::make_shared<SherpaOnlineFbankFrontend>(
				sampleRate,
				sherpaMelBinCount,
				sampleScalingPolicy);
			streamState.pendingFeatureFrames.clear();
			streamState.pendingFeatureFrameCount = 0;
		}
		streamState.contractFbankSampleScalingMode = streamState.onlineFbank->sampleScalingPolicy.diagnosticName;

		std::uint64_t loopGuard = 0;
		std::vector<float> chunk;
		std::uint64_t maxLoops = static_cast<std::uint64_t>(
			(std::max)(std::size_t{ 1 }, maxSpinCount));
		if (!isLivePcmStream &&
			streamingInput.source.sequenceEnd > 0 &&
			nextSequence < streamingInput.source.sequenceEnd) {
			const std::uint64_t remainingSamples =
				streamingInput.source.sequenceEnd - nextSequence;
			const std::uint64_t chunkSamplesU64 = static_cast<std::uint64_t>((std::max)(std::size_t{ 1 }, chunkSamples));
			const std::uint64_t loopsForFullRange =
				(remainingSamples + chunkSamplesU64 - 1ULL) / chunkSamplesU64;
			const std::uint64_t safetyLoops = loopsForFullRange + 2ULL;
			const std::uint64_t kHardLoopCap = 8192ULL;
			maxLoops = (std::min)(kHardLoopCap, (std::max)(maxLoops, safetyLoops));
		}
		bool inferenceFailed = false;
		std::string inferenceFailureMessage;

		auto updateDecoderFromContext =
			[&streamState, &inferenceFailed, &inferenceFailureMessage, this](std::vector<float>& decoderVector) -> bool {
#if !BLAZECLAW_HAS_ONNXRUNTIME
			UNREFERENCED_PARAMETER(decoderVector);
			return false;
#else
			Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
				OrtArenaAllocator,
				OrtMemTypeDefault);

			std::vector<Ort::Value> decoderInputs;
			std::vector<const char*> decoderInputNames;
			std::vector<std::vector<std::int64_t>> decoderInt64Buffers;
			std::vector<std::vector<float>> decoderFloatBuffers;
			decoderInputs.reserve(m_decoderInputBindings.size());
			decoderInputNames.reserve(m_decoderInputBindings.size());
			decoderInt64Buffers.reserve(m_decoderInputBindings.size());
			decoderFloatBuffers.reserve(m_decoderInputBindings.size());

			for (const auto& binding : m_decoderInputBindings) {
				std::vector<std::int64_t> shape = binding.shape;
				for (auto& dim : shape) {
					if (dim <= 0) {
						dim = 1;
					}
				}

				if (binding.kind == TensorBindingKind::DecoderInputTokens) {
					const auto resolvedShape = ResolveDecoderInputShape(
						binding,
						streamState.decoderContext.size());
					if (!resolvedShape.has_value()) {
						++streamState.decoderJoinerContractFailureCount;
						streamState.contractDecoderJoinerLastError = FormatDecoderJoinerContractError(
							"decoder input resolve",
							binding.name,
							binding.shape,
							{},
							streamState.decoderContext.size(),
							0);
						inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
						inferenceFailed = true;
						return false;
					}

					shape = *resolvedShape;
					streamState.contractDecoderInputShape = FormatShape(shape);

					const std::size_t elements = static_cast<std::size_t>(
						(std::max)(std::int64_t{ 1 },
							std::accumulate(
								shape.begin(),
								shape.end(),
								std::int64_t{ 1 },
								[](std::int64_t a, std::int64_t b) {
									return a * ((std::max)(std::int64_t{ 1 }, b));
								}))); 
					if (elements != streamState.decoderContext.size()) {
						++streamState.decoderJoinerContractFailureCount;
						streamState.contractDecoderJoinerLastError = FormatDecoderJoinerContractError(
							"decoder input elements",
							binding.name,
							binding.shape,
							shape,
							streamState.decoderContext.size(),
							elements);
						inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
						inferenceFailed = true;
						return false;
					}

					std::vector<std::int64_t> tokenBuffer(elements, m_blankId);
					const std::size_t copyCount = (std::min)(elements, streamState.decoderContext.size());
					for (std::size_t i = 0; i < copyCount; ++i) {
						tokenBuffer[elements - copyCount + i] = streamState.decoderContext[streamState.decoderContext.size() - copyCount + i];
					}
					decoderInt64Buffers.push_back(std::move(tokenBuffer));
					auto& tokenBufferRef = decoderInt64Buffers.back();

					decoderInputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
						memoryInfo,
						tokenBufferRef.data(),
						tokenBufferRef.size(),
						shape.data(),
						shape.size()));
					decoderInputNames.push_back(binding.name.c_str());
				}
				else {
					const std::size_t elements = static_cast<std::size_t>(
						(std::max)(std::int64_t{ 1 },
							std::accumulate(
								shape.begin(),
								shape.end(),
								std::int64_t{ 1 },
								[](std::int64_t a, std::int64_t b) {
									return a * ((std::max)(std::int64_t{ 1 }, b));
								}))); 

					if (binding.elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
						decoderInt64Buffers.emplace_back(elements, 0);
						auto& zerosRef = decoderInt64Buffers.back();
						decoderInputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
							memoryInfo,
							zerosRef.data(),
							zerosRef.size(),
							shape.data(),
							shape.size()));
					}
					else {
						decoderFloatBuffers.emplace_back(elements, 0.0f);
						auto& zerosRef = decoderFloatBuffers.back();
						decoderInputs.push_back(Ort::Value::CreateTensor<float>(
							memoryInfo,
							zerosRef.data(),
							zerosRef.size(),
							shape.data(),
							shape.size()));
					}
					decoderInputNames.push_back(binding.name.c_str());
				}
			}

			std::vector<const char*> decoderOutputNames;
			decoderOutputNames.reserve(m_decoderOutputNames.size());
			for (const auto& outputName : m_decoderOutputNames) {
				decoderOutputNames.push_back(outputName.c_str());
			}

			auto decoderOutputs = m_decoderSession->Run(
				Ort::RunOptions{ nullptr },
				decoderInputNames.data(),
				decoderInputs.data(),
				decoderInputs.size(),
				decoderOutputNames.data(),
				decoderOutputNames.size());

			if (decoderOutputs.empty()) {
				return false;
			}

			auto isUsableFloatTensor = [](const Ort::Value& value) {
				if (!value.IsTensor()) {
					return false;
				}
				auto info = value.GetTensorTypeAndShapeInfo();
				if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
					return false;
				}
				return info.GetElementCount() > 0;
			};

			std::size_t decoderMainIndex = m_decoderMainOutputIndex;
			if (decoderMainIndex >= decoderOutputs.size() || !isUsableFloatTensor(decoderOutputs[decoderMainIndex])) {
				std::size_t bestIndex = static_cast<std::size_t>(-1);
				size_t bestElements = 0;
				for (std::size_t i = 0; i < decoderOutputs.size(); ++i) {
					if (!isUsableFloatTensor(decoderOutputs[i])) {
						continue;
					}
					auto info = decoderOutputs[i].GetTensorTypeAndShapeInfo();
					const size_t elements = static_cast<size_t>(info.GetElementCount());
					if (bestIndex == static_cast<std::size_t>(-1) || elements > bestElements) {
						bestIndex = i;
						bestElements = elements;
					}
				}
				if (bestIndex == static_cast<std::size_t>(-1)) {
					return false;
				}
				decoderMainIndex = bestIndex;
			}

			auto& decoderMain = decoderOutputs[decoderMainIndex];
			if (!decoderMain.IsTensor()) {
				return false;
			}

			auto info = decoderMain.GetTensorTypeAndShapeInfo();
			if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
				return false;
			}

			const auto shape = info.GetShape();
			if (shape.empty()) {
				++streamState.decoderJoinerContractFailureCount;
				streamState.contractDecoderJoinerLastError = "decoder/joiner decoder output mismatch: empty output shape";
				inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
				inferenceFailed = true;
				return false;
			}

			const std::size_t total =
				info.GetElementCount() > 0
				? static_cast<std::size_t>(info.GetElementCount())
				: std::size_t{ 1 };
			if (total == 0) {
				++streamState.decoderJoinerContractFailureCount;
				streamState.contractDecoderJoinerLastError = "decoder/joiner decoder output mismatch: zero output elements";
				inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
				inferenceFailed = true;
				return false;
			}

			const float* data = decoderMain.GetTensorData<float>();
			if (data == nullptr) {
				return false;
			}

			streamState.contractDecoderInputContext = FormatInt64Vector(streamState.decoderContext);
			streamState.contractDecoderOutputShape = FormatShape(shape);

			std::size_t vectorSize = static_cast<std::size_t>((std::max)(std::int64_t{ 1 }, shape.back()));
			if (vectorSize > total) {
				++streamState.decoderJoinerContractFailureCount;
				streamState.contractDecoderJoinerLastError = FormatDecoderJoinerContractError(
					"decoder output slice",
					m_decoderOutputNames.empty() ? std::string{} : m_decoderOutputNames[decoderMainIndex],
					shape,
					shape,
					vectorSize,
					total);
				inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
				inferenceFailed = true;
				return false;
			}

			const std::size_t vectorOffset = total - vectorSize;
			{
				std::ostringstream slice;
				slice << "offset=" << vectorOffset
					<< ",size=" << vectorSize
					<< ",total=" << total;
				streamState.contractDecoderVectorSlice = slice.str();
			}

			decoderVector.assign(data + vectorOffset, data + total);
			return !decoderVector.empty();
#endif
			};

		for (;;) {
			if (isCancelled && isCancelled(request.runId)) {
				ClearStreamState(streamingInput.source.streamId);
				result.ok = false;
				result.cancelled = true;
				result.error = SpeechRecognitionError{
					.code = SpeechRecognitionErrorCode::Cancelled,
					.message = "transcription cancelled",
				};
				result.sessionState.cancelled = true;
				result.sessionState.stage = SpeechSessionStage::Failed;
				result.sessionState.error = result.error;
				return result;
			}

			if (++loopGuard > maxLoops) {
				break;
			}

			const auto latestOpt =
				GetStreamingAudioLatestSequence(streamingInput.source.streamId);
			if (!latestOpt.has_value()) {
				break;
			}

			const std::uint64_t readableEnd =
				(!isLivePcmStream && streamingInput.source.sequenceEnd > 0)
				? (std::min)(*latestOpt, streamingInput.source.sequenceEnd)
				: *latestOpt;
			if (nextSequence >= readableEnd) {
				break;
			}

			const std::size_t requestSamples = static_cast<std::size_t>((std::min)(
				static_cast<std::uint64_t>(chunkSamples),
				readableEnd - nextSequence));
			if (requestSamples == 0) {
				break;
			}

			if (!ReadStreamingAudioBySequence(
				streamingInput.source.streamId,
				nextSequence,
				requestSamples,
				chunk)) {
				break;
			}

			++streamState.chunkCount;

			const float energy = ComputeFrameEnergy(chunk);
			const bool frameSpeech = energy >= kSpeechEnergyThreshold;
			if (IsSherpaVerboseTraceEnabled() && streamState.chunkCount % 10 == 1) {
				TRACE(L"[SherpaStreaming] chunkCount=%llu energy=%f frameSpeech=%d\n",
					streamState.chunkCount, energy, frameSpeech);
			}
			if (frameSpeech) {
				streamState.speechActive = true;
				streamState.silenceChunkCount = 0;
			}
			else if (streamState.speechActive) {
				++streamState.silenceChunkCount;
			}

			std::size_t newFeatureFrames = 0;
			std::size_t consumedFeatureFrames = 0;
			const bool forceFlushFeatures = !isLivePcmStream &&
				streamingInput.source.sequenceEnd > 0 &&
				nextSequence + static_cast<std::uint64_t>(requestSamples) >= streamingInput.source.sequenceEnd;
			streamState.onlineFbank->AcceptSamples(chunk);
			if (forceFlushFeatures) {
				streamState.onlineFbank->FinishInputOnce();
			}
			const auto newLogMel = streamState.onlineFbank->ExtractNewFrames(newFeatureFrames);
			if (!newLogMel.empty() && newFeatureFrames > 0) {
				streamState.pendingFeatureFrames.insert(
					streamState.pendingFeatureFrames.end(),
					newLogMel.begin(),
					newLogMel.end());
				streamState.pendingFeatureFrameCount += newFeatureFrames;
			}
			if (IsSherpaVerboseTraceEnabled() && streamState.chunkCount % 10 == 1) {
				TRACE(L"[SherpaStreaming] onlineFbankNewFrames=%llu pendingFeatureFrames=%llu finalFlush=%d\n",
					(unsigned long long)newFeatureFrames,
					(unsigned long long)streamState.pendingFeatureFrameCount,
					forceFlushFeatures ? 1 : 0);
			}

			bool processMorePendingFeatureChunks = true;
			while (processMorePendingFeatureChunks) {
				processMorePendingFeatureChunks = false;
				consumedFeatureFrames = 0;
				const std::size_t featureFrames = streamState.pendingFeatureFrameCount;
				const auto& logMel = streamState.pendingFeatureFrames;
				if (logMel.empty() || featureFrames == 0) {
					break;
				}
#if BLAZECLAW_HAS_ONNXRUNTIME
				try {
					std::size_t fixedEncoderChunkFrames = 0;
					for (const auto& binding : m_encoderInputBindings) {
						if (binding.kind == TensorBindingKind::Features &&
							binding.shape.size() >= 2 &&
							binding.shape[1] > 0) {
							fixedEncoderChunkFrames = static_cast<std::size_t>(binding.shape[1]);
							break;
						}
					}

					const bool hasFixedEncoderChunk = fixedEncoderChunkFrames > 0;
					const bool hasFullFixedChunk = hasFixedEncoderChunk && featureFrames >= fixedEncoderChunkFrames;
					const bool hasEnoughDynamicFrames = !hasFixedEncoderChunk &&
						(featureFrames >= kMinSherpaFeatureFrames || forceFlushFeatures);
					const bool hasFinalPartialChunk = forceFlushFeatures &&
						featureFrames >= (std::min)(
							hasFixedEncoderChunk ? fixedEncoderChunkFrames : kMinSherpaFeatureFrames,
							kSherpaFinalPartialMinRealFrames);
					const bool shouldRunEncoder = hasFixedEncoderChunk
						? (hasFullFixedChunk || hasFinalPartialChunk)
						: hasEnoughDynamicFrames;
					if (!shouldRunEncoder) {
						break;
					}

					std::size_t effectiveFrames = featureFrames;
					if (hasFixedEncoderChunk) {
						effectiveFrames = fixedEncoderChunkFrames;
					}
					if (effectiveFrames == 0) {
						break;
					}

					const std::size_t frameStride = 80;
					std::vector<float> featureInputBuffer(effectiveFrames * frameStride, 0.0f);
					const std::size_t copiedFrames = (std::min)(featureFrames, effectiveFrames);
					if (copiedFrames > 0) {
						const std::size_t sourceOffset = 0;
						const std::size_t destOffset = 0;
						std::copy(
							logMel.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
							logMel.begin() + static_cast<std::ptrdiff_t>(sourceOffset + (copiedFrames * frameStride)),
							featureInputBuffer.begin() + static_cast<std::ptrdiff_t>(destOffset));
					}
					const float* featureDataPtr = featureInputBuffer.data();
					const std::size_t featureElementCount = featureInputBuffer.size();
					consumedFeatureFrames = copiedFrames;
					streamState.contractFeatureFrameCount = static_cast<std::uint64_t>(effectiveFrames);
					streamState.contractFeatureRealFrameCount = static_cast<std::uint64_t>(copiedFrames);
					streamState.contractFeaturePaddedFrameCount = static_cast<std::uint64_t>(
						effectiveFrames > copiedFrames ? effectiveFrames - copiedFrames : 0);
					if (!featureInputBuffer.empty()) {
						streamState.contractFeatureFirstFrameStats = FormatFrameStats(
							featureInputBuffer.data(),
							frameStride);
						streamState.contractFeatureLastFrameStats = FormatFrameStats(
							featureInputBuffer.data() + ((effectiveFrames - 1) * frameStride),
							frameStride);
					}

					Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
						OrtArenaAllocator,
						OrtMemTypeDefault);

					std::vector<Ort::Value> encoderInputs;
					std::vector<const char*> encoderInputNames;
					std::vector<std::vector<std::int64_t>> encoderInt64Buffers;
					std::vector<std::vector<float>> encoderFloatBuffers;
					encoderInputs.reserve(m_encoderInputBindings.size());
					encoderInputNames.reserve(m_encoderInputBindings.size());
					encoderInt64Buffers.reserve(m_encoderInputBindings.size());
					encoderFloatBuffers.reserve(m_encoderInputBindings.size());

					auto findStateMappingForInput = [this](std::size_t inputIndex) -> const EncoderStateCacheBinding* {
						const auto mappingIt = std::find_if(
							m_encoderStateCacheBindings.begin(),
							m_encoderStateCacheBindings.end(),
							[inputIndex](const EncoderStateCacheBinding& mapping) {
								return mapping.inputBindingIndex == inputIndex;
							});
						return mappingIt == m_encoderStateCacheBindings.end() ? nullptr : &(*mappingIt);
					};

					for (const auto& binding : m_encoderInputBindings) {
						std::vector<std::int64_t> shape = binding.shape;
						for (auto& dim : shape) {
							if (dim <= 0) {
								dim = 1;
							}
						}

						if (binding.kind == TensorBindingKind::Features) {
							if (shape.size() >= 3) {
								shape[0] = 1;
								shape[1] = static_cast<std::int64_t>(effectiveFrames);
								shape[2] = 80;
							}
							else if (shape.size() == 2) {
								shape[0] = static_cast<std::int64_t>(effectiveFrames);
								shape[1] = 80;
							}
							else {
								shape = { 1, static_cast<std::int64_t>(effectiveFrames), 80 };
							}
							streamState.contractFeatureInputShape = FormatShape(shape);

							encoderInputs.push_back(Ort::Value::CreateTensor<float>(
								memoryInfo,
								const_cast<float*>(featureDataPtr),
								featureElementCount,
								shape.data(),
								shape.size()));
						}
						else if (binding.kind == TensorBindingKind::FeatureLengths) {
							const std::int64_t featureLength = static_cast<std::int64_t>((std::max)(std::size_t{ 1 }, copiedFrames));
							streamState.contractFeatureLengthValue = std::to_string(featureLength);
							encoderInt64Buffers.push_back({ featureLength });
							auto& lengths = encoderInt64Buffers.back();
							if (shape.empty()) {
								shape = { 1 };
							}
							encoderInputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
								memoryInfo,
								lengths.data(),
								lengths.size(),
								shape.data(),
								shape.size()));
						}
						else if (binding.isStateTensor) {
							const auto* mapping = findStateMappingForInput(binding.ordinal);
							if (mapping == nullptr) {
								++streamState.encoderStateCacheContractFailureCount;
								streamState.contractStateCacheLastError = "state cache input has no mapped output: " + binding.name;
								inferenceFailed = true;
								inferenceFailureMessage = streamState.contractStateCacheLastError;
								break;
							}

							if (binding.elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
								if (streamState.encoderInt64StateCaches.size() <= binding.bufferIndex) {
									streamState.encoderInt64StateCaches.resize(binding.bufferIndex + 1);
								}
								if (streamState.encoderInt64StateCacheShapes.size() <= binding.bufferIndex) {
									streamState.encoderInt64StateCacheShapes.resize(binding.bufferIndex + 1);
								}
								auto& cache = streamState.encoderInt64StateCaches[binding.bufferIndex];
								auto& cacheShape = streamState.encoderInt64StateCacheShapes[binding.bufferIndex];
								const auto resolvedShape = ResolveStateCacheInputShape(*mapping, cache.size());
								if (!resolvedShape.has_value()) {
									++streamState.encoderStateCacheContractFailureCount;
									streamState.contractStateCacheLastError = FormatStateCacheContractError(
										"initialize",
										*mapping,
										shape,
										0,
										cache.size());
									inferenceFailed = true;
									inferenceFailureMessage = streamState.contractStateCacheLastError;
									break;
								}
								shape = *resolvedShape;
								const auto expectedElements = static_cast<std::size_t>(ComputeResolvedElementCount(shape));
								if (expectedElements == 0 && cache.size() > 0) {
									++streamState.encoderStateCacheContractFailureCount;
									streamState.contractStateCacheLastError = FormatStateCacheContractError(
										"initialize",
										*mapping,
										shape,
									expectedElements,
										cache.size());
									inferenceFailed = true;
									inferenceFailureMessage = streamState.contractStateCacheLastError;
									break;
								}
								if (cache.size() != expectedElements) {
									cache.assign(expectedElements, 0);
								}
								cacheShape = shape;
								encoderInt64Buffers.push_back(cache);
								auto& stateRef = encoderInt64Buffers.back();
								if (stateRef.empty()) {
									stateRef.push_back(0);
								}
								encoderInputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
									memoryInfo,
									stateRef.data(),
									expectedElements,
									shape.data(),
									shape.size()));
							}
							else {
								if (streamState.encoderFloatStateCaches.size() <= binding.bufferIndex) {
									streamState.encoderFloatStateCaches.resize(binding.bufferIndex + 1);
								}
								if (streamState.encoderFloatStateCacheShapes.size() <= binding.bufferIndex) {
									streamState.encoderFloatStateCacheShapes.resize(binding.bufferIndex + 1);
								}
								auto& cache = streamState.encoderFloatStateCaches[binding.bufferIndex];
								auto& cacheShape = streamState.encoderFloatStateCacheShapes[binding.bufferIndex];
								const auto resolvedShape = ResolveStateCacheInputShape(*mapping, cache.size());
								if (!resolvedShape.has_value()) {
									++streamState.encoderStateCacheContractFailureCount;
									streamState.contractStateCacheLastError = FormatStateCacheContractError(
										"initialize",
										*mapping,
										shape,
										0,
										cache.size());
									inferenceFailed = true;
									inferenceFailureMessage = streamState.contractStateCacheLastError;
									break;
								}
								shape = *resolvedShape;
								const auto expectedElements = static_cast<std::size_t>(ComputeResolvedElementCount(shape));
								if (expectedElements == 0 && cache.size() > 0) {
									++streamState.encoderStateCacheContractFailureCount;
									streamState.contractStateCacheLastError = FormatStateCacheContractError(
										"initialize",
										*mapping,
										shape,
									expectedElements,
										cache.size());
									inferenceFailed = true;
									inferenceFailureMessage = streamState.contractStateCacheLastError;
									break;
								}
								if (cache.size() != expectedElements) {
									cache.assign(expectedElements, 0.0f);
								}
								cacheShape = shape;
								encoderFloatBuffers.push_back(cache);
								auto& stateRef = encoderFloatBuffers.back();
								if (stateRef.empty()) {
									stateRef.push_back(0.0f);
								}
								encoderInputs.push_back(Ort::Value::CreateTensor<float>(
									memoryInfo,
									stateRef.data(),
									expectedElements,
									shape.data(),
									shape.size()));
							}
						}
						else {
							const std::size_t elements = static_cast<std::size_t>((std::max)(
								std::int64_t{ 1 },
								std::accumulate(
									shape.begin(),
									shape.end(),
									std::int64_t{ 1 },
									[](std::int64_t a, std::int64_t b) {
										return a * ((std::max)(std::int64_t{ 1 }, b));
									})));
							if (binding.elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
								encoderInt64Buffers.emplace_back(elements, 0);
								auto& zeros = encoderInt64Buffers.back();
								encoderInputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
									memoryInfo,
									zeros.data(),
									zeros.size(),
									shape.data(),
									shape.size()));
							}
							else {
								encoderFloatBuffers.emplace_back(elements, 0.0f);
								auto& zeros = encoderFloatBuffers.back();
								encoderInputs.push_back(Ort::Value::CreateTensor<float>(
									memoryInfo,
									zeros.data(),
									zeros.size(),
									shape.data(),
									shape.size()));
							}
						}
						encoderInputNames.push_back(binding.name.c_str());
					}

					if (inferenceFailed) {
						break;
					}

					if (encoderInputs.size() != m_encoderInputBindings.size() ||
						encoderInputNames.size() != m_encoderInputBindings.size()) {
						++streamState.encoderStateCacheContractFailureCount;
						std::ostringstream stream;
						stream << "encoder input assembly incomplete expected="
							<< m_encoderInputBindings.size()
							<< " actual=" << encoderInputs.size();
						streamState.contractStateCacheLastError = stream.str();
						inferenceFailed = true;
						inferenceFailureMessage = streamState.contractStateCacheLastError;
						break;
					}

					std::vector<const char*> encoderOutputNames;
					encoderOutputNames.reserve(m_encoderOutputNames.size());
					for (const auto& outputName : m_encoderOutputNames) {
						encoderOutputNames.push_back(outputName.c_str());
					}

					auto encoderOutputs = m_encoderSession->Run(
						Ort::RunOptions{ nullptr },
						encoderInputNames.data(),
						encoderInputs.data(),
						encoderInputs.size(),
						encoderOutputNames.data(),
						encoderOutputNames.size());

					if (!encoderOutputs.empty()) {
						std::optional<std::size_t> encoderOutputFrameLimit;
						bool hasModelLengthOutput = false;
						for (std::size_t outputIndex = 0; outputIndex < encoderOutputs.size(); ++outputIndex) {
							if (!encoderOutputs[outputIndex].IsTensor()) {
								continue;
							}

							auto outputInfo = encoderOutputs[outputIndex].GetTensorTypeAndShapeInfo();
							const auto outputType = outputInfo.GetElementType();
							const auto* outputBinding = outputIndex < m_encoderOutputBindings.size()
								? &m_encoderOutputBindings[outputIndex]
								: nullptr;
							const bool looksLikeLengthOutput = outputBinding != nullptr && outputBinding->isLengthLike;
							if (looksLikeLengthOutput) {
								hasModelLengthOutput = true;
							}
							if (outputType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
								const auto rawElementCount = outputInfo.GetElementCount();
								const std::size_t elements = rawElementCount > 0
									? static_cast<std::size_t>(rawElementCount)
									: std::size_t{ 0 };
								const auto* data = encoderOutputs[outputIndex].GetTensorData<std::int64_t>();
								if (looksLikeLengthOutput && data != nullptr && elements > 0 && data[0] > 0) {
									++streamState.encoderLengthOutputCount;
									streamState.encoderLengthOutputUsed = true;
									encoderOutputFrameLimit = static_cast<std::size_t>(data[0]);
								}
								if (data != nullptr && elements > 0) {
									const auto mappingIt = std::find_if(
										m_encoderStateCacheBindings.begin(),
										m_encoderStateCacheBindings.end(),
										[outputIndex](const EncoderStateCacheBinding& mapping) {
											return mapping.outputBindingIndex == outputIndex && mapping.isInt64;
										});
									if (mappingIt == m_encoderStateCacheBindings.end()) {
										continue;
									}
									const std::size_t stateIndex = mappingIt->cacheIndex;
									const auto outputShape = outputInfo.GetShape();
									const auto expectedElements = static_cast<std::size_t>(ComputeResolvedElementCount(outputShape));
									if (expectedElements == 0 || expectedElements != elements) {
										++streamState.encoderStateCacheContractFailureCount;
										streamState.contractStateCacheLastError = FormatStateCacheContractError(
											"update",
											*mappingIt,
											outputShape,
											expectedElements,
											elements);
										inferenceFailed = true;
										inferenceFailureMessage = streamState.contractStateCacheLastError;
										break;
									}
									if (streamState.encoderInt64StateCaches.size() <= stateIndex) {
										streamState.encoderInt64StateCaches.resize(stateIndex + 1);
									}
									if (streamState.encoderInt64StateCacheShapes.size() <= stateIndex) {
										streamState.encoderInt64StateCacheShapes.resize(stateIndex + 1);
									}
									streamState.encoderInt64StateCaches[stateIndex].assign(data, data + elements);
									streamState.encoderInt64StateCacheShapes[stateIndex] = outputShape;
									++streamState.encoderStateCacheUpdateCount;
									++streamState.encoderStateCacheValidatedUpdateCount;
								}
							}
							else if (outputType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
								const auto rawElementCount = outputInfo.GetElementCount();
								const std::size_t elements = rawElementCount > 0
									? static_cast<std::size_t>(rawElementCount)
									: std::size_t{ 0 };
								const auto* data = encoderOutputs[outputIndex].GetTensorData<float>();
								if (data != nullptr && elements > 0) {
									const auto mappingIt = std::find_if(
										m_encoderStateCacheBindings.begin(),
										m_encoderStateCacheBindings.end(),
										[outputIndex](const EncoderStateCacheBinding& mapping) {
											return mapping.outputBindingIndex == outputIndex && !mapping.isInt64;
										});
									if (mappingIt == m_encoderStateCacheBindings.end()) {
										continue;
									}
									const std::size_t stateIndex = mappingIt->cacheIndex;
									const auto outputShape = outputInfo.GetShape();
									const auto expectedElements = static_cast<std::size_t>(ComputeResolvedElementCount(outputShape));
									if (expectedElements == 0 || expectedElements != elements) {
										++streamState.encoderStateCacheContractFailureCount;
										streamState.contractStateCacheLastError = FormatStateCacheContractError(
											"update",
											*mappingIt,
											outputShape,
											expectedElements,
											elements);
										inferenceFailed = true;
										inferenceFailureMessage = streamState.contractStateCacheLastError;
										break;
									}
									if (streamState.encoderFloatStateCaches.size() <= stateIndex) {
										streamState.encoderFloatStateCaches.resize(stateIndex + 1);
									}
									if (streamState.encoderFloatStateCacheShapes.size() <= stateIndex) {
										streamState.encoderFloatStateCacheShapes.resize(stateIndex + 1);
									}
									streamState.encoderFloatStateCaches[stateIndex].assign(data, data + elements);
									streamState.encoderFloatStateCacheShapes[stateIndex] = outputShape;
									++streamState.encoderStateCacheUpdateCount;
									++streamState.encoderStateCacheValidatedUpdateCount;
								}
							}
							if (inferenceFailed) {
								break;
							}
						}

						if (inferenceFailed) {
							break;
						}

						auto isUsableEncoderTensor = [](const Ort::Value& value) {
							if (!value.IsTensor()) {
								return false;
							}
							auto info = value.GetTensorTypeAndShapeInfo();
							if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
								return false;
							}
							const auto shape = info.GetShape();
							return shape.size() >= 2 && info.GetElementCount() > 0;
						};

						std::size_t encoderMainIndex = m_encoderMainOutputIndex;
						if (encoderMainIndex >= encoderOutputs.size() || !isUsableEncoderTensor(encoderOutputs[encoderMainIndex])) {
							std::size_t bestIndex = static_cast<std::size_t>(-1);
							size_t bestElements = 0;
							for (std::size_t i = 0; i < encoderOutputs.size(); ++i) {
								if (!isUsableEncoderTensor(encoderOutputs[i])) {
									continue;
								}
								auto info = encoderOutputs[i].GetTensorTypeAndShapeInfo();
								const size_t elements = static_cast<size_t>(info.GetElementCount());
								if (bestIndex == static_cast<std::size_t>(-1) || elements > bestElements) {
									bestIndex = i;
									bestElements = elements;
								}
							}
							if (bestIndex != static_cast<std::size_t>(-1)) {
								encoderMainIndex = bestIndex;
							}
						}

						auto& encoderMain = encoderOutputs[encoderMainIndex];
						if (encoderMain.IsTensor()) {
							auto encoderInfo = encoderMain.GetTensorTypeAndShapeInfo();
							if (encoderInfo.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
								const auto encoderShape = encoderInfo.GetShape();
								streamState.contractEncoderOutputShape = FormatShape(encoderShape);
								const float* encoderData = encoderMain.GetTensorData<float>();
								if (encoderData != nullptr) {
									std::size_t encoderFrames = 0;
									std::size_t encoderDim = 0;
									if (encoderShape.size() >= 3) {
										encoderFrames = static_cast<std::size_t>((std::max)(std::int64_t{ 0 }, encoderShape[1]));
										encoderDim = static_cast<std::size_t>((std::max)(std::int64_t{ 0 }, encoderShape[2]));
									}
									else if (encoderShape.size() == 2) {
										encoderFrames = static_cast<std::size_t>((std::max)(std::int64_t{ 0 }, encoderShape[0]));
										encoderDim = static_cast<std::size_t>((std::max)(std::int64_t{ 0 }, encoderShape[1]));
									}
									if (encoderOutputFrameLimit.has_value()) {
										encoderFrames = (std::min)(encoderFrames, *encoderOutputFrameLimit);
									}
									else if (hasModelLengthOutput) {
										encoderFrames = 0;
									}
									streamState.contractEncoderValidFrameCount = static_cast<std::uint64_t>(encoderFrames);

									if (encoderFrames > 0 && encoderDim > 0) {
										streamState.encoderFrameCount += static_cast<std::uint64_t>(encoderFrames);
										std::vector<float> decoderVector;
										if (updateDecoderFromContext(decoderVector)) {
											for (std::size_t frameIdx = 0; frameIdx < encoderFrames; ++frameIdx) {
												if (streamState.emittedTokenIds.size() >= kSherpaMaxTokensPerUtterance) {
													break;
												}
												const float* framePtr = encoderData + (frameIdx * encoderDim);
												std::vector<float> encoderFrame(framePtr, framePtr + encoderDim);
												std::size_t symbolsThisFrame = 0;
												bool advanceFrame = false;
												while (!advanceFrame && symbolsThisFrame < kSherpaMaxSymbolsPerFrame) {
													++streamState.rnntInnerLoopCount;

									std::vector<Ort::Value> joinerInputs;
									std::vector<const char*> joinerInputNames;
									std::vector<std::vector<std::int64_t>> joinerInt64Buffers;
									std::vector<std::vector<float>> joinerFloatBuffers;
												joinerInputs.reserve(m_joinerInputBindings.size());
												joinerInputNames.reserve(m_joinerInputBindings.size());
									joinerInt64Buffers.reserve(m_joinerInputBindings.size());
									joinerFloatBuffers.reserve(m_joinerInputBindings.size());

												for (const auto& binding : m_joinerInputBindings) {
													std::vector<std::int64_t> shape = binding.shape;
													for (auto& dim : shape) {
														if (dim <= 0) {
															dim = 1;
														}
													}

													if (binding.kind == TensorBindingKind::EncoderOut) {
											const auto resolvedShape = ResolveJoinerInputShape(binding, encoderFrame.size());
											if (!resolvedShape.has_value()) {
												++streamState.decoderJoinerContractFailureCount;
												streamState.contractDecoderJoinerLastError = FormatDecoderJoinerContractError(
													"joiner encoder input resolve",
													binding.name,
													binding.shape,
													{},
													encoderFrame.size(),
													0);
												inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
												inferenceFailed = true;
												break;
											}
											shape = *resolvedShape;
											const auto expectedElements = ComputeResolvedElementCount(shape);
											if (expectedElements != encoderFrame.size()) {
												++streamState.decoderJoinerContractFailureCount;
												streamState.contractDecoderJoinerLastError = FormatDecoderJoinerContractError(
													"joiner encoder input elements",
													binding.name,
													binding.shape,
													shape,
													encoderFrame.size(),
													static_cast<std::size_t>(expectedElements));
												inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
												inferenceFailed = true;
												break;
											}
														streamState.contractJoinerEncoderInputShape = FormatShape(shape);
														joinerInputs.push_back(Ort::Value::CreateTensor<float>(
															memoryInfo,
															encoderFrame.data(),
															encoderFrame.size(),
															shape.data(),
															shape.size()));
													}
													else if (binding.kind == TensorBindingKind::DecoderOut) {
											const auto resolvedShape = ResolveJoinerInputShape(binding, decoderVector.size());
											if (!resolvedShape.has_value()) {
												++streamState.decoderJoinerContractFailureCount;
												streamState.contractDecoderJoinerLastError = FormatDecoderJoinerContractError(
													"joiner decoder input resolve",
													binding.name,
													binding.shape,
													{},
													decoderVector.size(),
													0);
												inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
												inferenceFailed = true;
												break;
											}
											shape = *resolvedShape;
											const auto expectedElements = ComputeResolvedElementCount(shape);
											if (expectedElements != decoderVector.size()) {
												++streamState.decoderJoinerContractFailureCount;
												streamState.contractDecoderJoinerLastError = FormatDecoderJoinerContractError(
													"joiner decoder input elements",
													binding.name,
													binding.shape,
													shape,
													decoderVector.size(),
													static_cast<std::size_t>(expectedElements));
												inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
												inferenceFailed = true;
												break;
											}
														streamState.contractJoinerDecoderInputShape = FormatShape(shape);
														joinerInputs.push_back(Ort::Value::CreateTensor<float>(
															memoryInfo,
															decoderVector.data(),
															decoderVector.size(),
															shape.data(),
															shape.size()));
													}
													else {
														const std::size_t elements = static_cast<std::size_t>((std::max)(
															std::int64_t{ 1 },
															std::accumulate(
																shape.begin(),
																shape.end(),
																std::int64_t{ 1 },
																[](std::int64_t a, std::int64_t b) {
																	return a * ((std::max)(std::int64_t{ 1 }, b));
																})));

														if (binding.elementType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
									joinerInt64Buffers.emplace_back(elements, 0);
									auto& zerosRef = joinerInt64Buffers.back();
															joinerInputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
																memoryInfo,
										zerosRef.data(),
										zerosRef.size(),
																shape.data(),
																shape.size()));
														}
														else {
									joinerFloatBuffers.emplace_back(elements, 0.0f);
									auto& zerosRef = joinerFloatBuffers.back();
															joinerInputs.push_back(Ort::Value::CreateTensor<float>(
																memoryInfo,
										zerosRef.data(),
										zerosRef.size(),
																shape.data(),
																shape.size()));
														}
													}

													joinerInputNames.push_back(binding.name.c_str());
												}
									if (inferenceFailed) {
										break;
									}

												std::vector<const char*> joinerOutputNames;
												joinerOutputNames.reserve(m_joinerOutputNames.size());
												for (const auto& outputName : m_joinerOutputNames) {
													joinerOutputNames.push_back(outputName.c_str());
												}

											++streamState.joinerCallCount;
											auto joinerOutputs = m_joinerSession->Run(
													Ort::RunOptions{ nullptr },
													joinerInputNames.data(),
													joinerInputs.data(),
													joinerInputs.size(),
													joinerOutputNames.data(),
													joinerOutputNames.size());

													if (joinerOutputs.empty()) {
														advanceFrame = true;
													continue;
												}

											auto isUsableJoinerTensor = [](const Ort::Value& value) {
												if (!value.IsTensor()) {
													return false;
												}
												auto info = value.GetTensorTypeAndShapeInfo();
												if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
													return false;
												}
												const auto shape = info.GetShape();
												return !shape.empty() && info.GetElementCount() > 0;
											};

										 std::size_t joinerMainIndex = m_joinerMainOutputIndex;
										 if (joinerMainIndex >= joinerOutputs.size() || !isUsableJoinerTensor(joinerOutputs[joinerMainIndex])) {
											 std::size_t bestIndex = static_cast<std::size_t>(-1);
											 size_t bestLastDim = 0;
											 for (std::size_t i = 0; i < joinerOutputs.size(); ++i) {
												 if (!isUsableJoinerTensor(joinerOutputs[i])) {
													 continue;
												 }
												 auto info = joinerOutputs[i].GetTensorTypeAndShapeInfo();
												 const auto shape = info.GetShape();
												 const size_t lastDim = shape.empty() ? 0 : static_cast<size_t>((std::max)(std::int64_t{ 0 }, shape.back()));
												 if (bestIndex == static_cast<std::size_t>(-1) || lastDim > bestLastDim) {
													 bestIndex = i;
													 bestLastDim = lastDim;
												 }
											 }
														 if (bestIndex == static_cast<std::size_t>(-1)) {
															 advanceFrame = true;
												 continue;
											 }
											 joinerMainIndex = bestIndex;
										 }

										 auto& joinerMain = joinerOutputs[joinerMainIndex];
															if (!joinerMain.IsTensor()) {
																advanceFrame = true;
													continue;
												}

												auto joinerInfo = joinerMain.GetTensorTypeAndShapeInfo();
												if (joinerInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
																advanceFrame = true;
													continue;
												}

												const std::size_t logitsCount =
													joinerInfo.GetElementCount() > 0
													? static_cast<std::size_t>(joinerInfo.GetElementCount())
													: std::size_t{ 1 };
												const auto joinerShape = joinerInfo.GetShape();
													streamState.contractJoinerOutputShape = FormatShape(joinerShape);
												const float* logits = joinerMain.GetTensorData<float>();
															if (logits == nullptr || logitsCount == 0) {
																advanceFrame = true;
													continue;
												}

												std::size_t vocabSize = logitsCount;
												if (!joinerShape.empty() && joinerShape.back() > 0) {
													vocabSize = static_cast<std::size_t>(joinerShape.back());
												}
												if (vocabSize == 0 || vocabSize > logitsCount) {
											++streamState.decoderJoinerContractFailureCount;
											streamState.contractDecoderJoinerLastError = FormatDecoderJoinerContractError(
												"joiner logits vocab axis",
												m_joinerOutputNames.empty() ? std::string{} : m_joinerOutputNames[joinerMainIndex],
												joinerShape,
												joinerShape,
												vocabSize,
												logitsCount);
											inferenceFailureMessage = streamState.contractDecoderJoinerLastError;
											inferenceFailed = true;
											break;
												}
												const std::size_t logitsOffset = logitsCount - vocabSize;
										{
											std::ostringstream slice;
											slice << "offset=" << logitsOffset
												<< ",vocabSize=" << vocabSize
												<< ",total=" << logitsCount;
											streamState.contractJoinerLogitsSlice = slice.str();
										}
										++streamState.decoderJoinerValidatedCallCount;
													streamState.contractJoinerTopTokens = FormatTopTokens(
														logits + logitsOffset,
														vocabSize);
													if (IsSherpaVerboseTraceEnabled() &&
														(streamState.joinerCallCount <= 3 || streamState.joinerCallCount % 50 == 0)) {
				TRACE(L"[SherpaContract] fbankScaling=%S featureShape=%S featureLength=%S real=%llu padded=%llu encoderShape=%S validFrames=%llu decoderContext=%S decoderShape=%S joinerEncoderShape=%S joinerDecoderShape=%S joinerOutputShape=%S topTokens=%S\n",
					streamState.contractFbankSampleScalingMode.c_str(),
															streamState.contractFeatureInputShape.c_str(),
															streamState.contractFeatureLengthValue.c_str(),
															(unsigned long long)streamState.contractFeatureRealFrameCount,
															(unsigned long long)streamState.contractFeaturePaddedFrameCount,
															streamState.contractEncoderOutputShape.c_str(),
															(unsigned long long)streamState.contractEncoderValidFrameCount,
															streamState.contractDecoderInputContext.c_str(),
															streamState.contractDecoderOutputShape.c_str(),
															streamState.contractJoinerEncoderInputShape.c_str(),
															streamState.contractJoinerDecoderInputShape.c_str(),
															streamState.contractJoinerOutputShape.c_str(),
															streamState.contractJoinerTopTokens.c_str());
													}

												const auto bestIt = std::max_element(
													logits + logitsOffset,
													logits + logitsOffset + vocabSize);
															if (bestIt == logits + logitsOffset + vocabSize) {
																advanceFrame = true;
													continue;
												}

												const std::int64_t tokenId = static_cast<std::int64_t>(bestIt - (logits + logitsOffset));
												streamState.lastBestTokenId = tokenId;
												streamState.lastBestTokenScore = *bestIt;
												streamState.lastSecondBestTokenId = -1;
												streamState.lastSecondBestTokenScore = 0.0f;
												for (std::size_t i = 0; i < vocabSize; ++i) {
													const auto* candidate = logits + logitsOffset + i;
													if (candidate == bestIt) {
														continue;
													}
													if (streamState.lastSecondBestTokenId < 0 ||
														*candidate > streamState.lastSecondBestTokenScore) {
														streamState.lastSecondBestTokenId = static_cast<std::int64_t>(i);
														streamState.lastSecondBestTokenScore = *candidate;
													}
												}
														if (IsSherpaVerboseTraceEnabled() && tokenId != m_blankId) {
													TRACE(L"[SherpaStreaming] emitted tokenId=%lld value=%f blankId=%lld\n",
														(long long)tokenId, *bestIt, (long long)m_blankId);
												}
												if (tokenId == m_blankId || tokenId == m_eosId) {
													if (tokenId == m_blankId) {
														++streamState.blankTokenCount;
													}
													advanceFrame = true;
													continue;
												}

										if (!streamState.emittedTokenIds.empty() && streamState.emittedTokenIds.back() == tokenId) {
											++streamState.rnntRepeatedTokenCount;
											advanceFrame = true;
											continue;
										}
												streamState.emittedTokenIds.push_back(tokenId);
										streamState.baselineTokenIds.push_back(tokenId);
												++streamState.decodedTokenCount;
												++symbolsThisFrame;
												streamState.decoderContext.push_back(tokenId);
												const std::size_t contextSize = (std::max)(std::size_t{ 1 }, m_decoderContextSize);
												if (streamState.decoderContext.size() > contextSize) {
													streamState.decoderContext.erase(
														streamState.decoderContext.begin(),
														streamState.decoderContext.end() - static_cast<std::ptrdiff_t>(contextSize));
												}

												if (streamState.emittedTokenIds.size() >= kSherpaMaxTokensPerUtterance) {
													advanceFrame = true;
												}
												else {
													updateDecoderFromContext(decoderVector);
												}
											}
											if (symbolsThisFrame > 1) {
												++streamState.rnntMultiSymbolFrameCount;
											}
											if (!advanceFrame && symbolsThisFrame >= kSherpaMaxSymbolsPerFrame) {
												++streamState.rnntMaxSymbolsHitCount;
											}
										}
									}
								}
							}
						}
					}
				}
				}
				catch (const std::exception& ex) {
					inferenceFailed = true;
					inferenceFailureMessage = ex.what();
					break;
				}
				catch (...) {
					inferenceFailed = true;
					inferenceFailureMessage = "unknown sherpa streaming inference failure";
					break;
				}
#endif
				if (inferenceFailed) {
					break;
				}
				streamState.partialText = DecodeTokenIdsToText(streamState.emittedTokenIds);
				result.text = streamState.partialText;

				if (consumedFeatureFrames > 0 &&
					consumedFeatureFrames <= streamState.pendingFeatureFrameCount) {
					const std::size_t consumedFeatureElements = consumedFeatureFrames * 80;
					streamState.pendingFeatureFrames.erase(
						streamState.pendingFeatureFrames.begin(),
						streamState.pendingFeatureFrames.begin() + static_cast<std::ptrdiff_t>(consumedFeatureElements));
					streamState.pendingFeatureFrameCount -= consumedFeatureFrames;
					processMorePendingFeatureChunks = true;
				}
			}

			nextSequence += static_cast<std::uint64_t>(requestSamples);
		}

		if (inferenceFailed) {
			result.ok = false;
			result.error = SpeechRecognitionError{
				.code = SpeechRecognitionErrorCode::InferenceFailed,
				.message = inferenceFailureMessage.empty()
					? "sherpa streaming inference failed"
					: inferenceFailureMessage,
			};
			result.sessionState.stage = SpeechSessionStage::Failed;
			result.sessionState.error = result.error;
			return result;
		}

		const bool inputFinal = streamingInput.source.sequenceEnd > 0 &&
			nextSequence >= streamingInput.source.sequenceEnd;
		const bool shouldTreatInputAsFinal = inputFinal && !isLivePcmStream;
		const bool finalDrainComplete = !isLivePcmStream &&
			streamingInput.source.sequenceEnd > 0 &&
			nextSequence >= streamingInput.source.sequenceEnd;
		const std::uint64_t finalRemainingSamples =
			streamingInput.source.sequenceEnd > nextSequence
			? streamingInput.source.sequenceEnd - nextSequence
			: 0ULL;
		const bool shouldFinalizeByVad =
			streamState.speechActive && streamState.silenceChunkCount >= 3;
		const bool shouldFinalize = shouldFinalizeByVad || shouldTreatInputAsFinal;
		if (!streamState.partialText.empty() && shouldTreatInputAsFinal) {
			result.sessionState.transcriptText = streamState.partialText;
		}
		const std::string baselineDecodedText = !result.sessionState.transcriptText.empty()
			? result.sessionState.transcriptText
			: streamState.partialText;
		std::string finalOutcome;
		if (shouldTreatInputAsFinal) {
			if (!streamState.speechActive && streamState.decodedTokenCount == 0) {
				finalOutcome = "no_speech_detected";
			}
			else if (streamState.decodedTokenCount == 0) {
				finalOutcome = "no_tokens_emitted";
			}
			else if (baselineDecodedText.empty()) {
				finalOutcome = "tokens_emitted_empty_decoded_text";
			}
			else {
				finalOutcome = "final_transcript";
			}
		}
		else if (isLivePcmStream) {
			finalOutcome = "live_stream_not_final";
		}
		else {
			finalOutcome = "finite_stream_not_drained";
		}
		if (!streamState.partialText.empty()) {
			SpeechTranscriptSegment segment;
			segment.text = streamState.partialText;
			segment.sequence = streamState.segmentSequence + 1;
			segment.final = shouldFinalize;
			result.sessionState.segment = segment;
			result.text = streamState.partialText;

			if (shouldFinalize) {
				++streamState.segmentSequence;
				result.sessionState.transcriptText = streamState.partialText;
				streamState.emittedTokenIds.clear();
				streamState.partialText.clear();
				streamState.decoderContext.assign(
					(std::max)(std::size_t{ 1 },
#if BLAZECLAW_HAS_ONNXRUNTIME
						m_decoderContextSize
#else
						std::size_t{ 2 }
#endif
					),
					m_blankId);
				streamState.pendingFeatureFrames.clear();
				streamState.pendingFeatureFrameCount = 0;
				streamState.onlineFbank.reset();
				streamState.silenceChunkCount = 0;
				streamState.speechActive = false;
			}
		}

		std::optional<std::filesystem::path> baselineDiagnosticPath;
		if (baselinePersistenceEnabled && shouldTreatInputAsFinal) {
			streamState.nextSequence = nextSequence;
			baselineDiagnosticPath = PersistBaselineDiagnostics(
				request,
				streamingInput,
				streamState,
				sampleRate,
				chunkSamples,
				loopGuard,
				maxLoops,
				shouldTreatInputAsFinal,
				baselineExpectedText,
				baselineDecodedText,
				finalOutcome,
				result.sessionState.segment.has_value(),
				false);
		}

		result.ok = true;
		result.cancelled = false;
		result.language = result.sessionState.language;
		result.sessionState.stage = SpeechSessionStage::Completed;
		result.sessionState.error = std::nullopt;
		result.latencyMs = static_cast<std::uint32_t>(streamState.chunkCount * chunkMs);
		result.sessionState.latencyMs = result.latencyMs;
		if (result.sessionState.streamingInput.has_value()) {
			result.sessionState.streamingInput->cursor.startSequence = streamingInput.cursor.startSequence;
			result.sessionState.streamingInput->cursor.nextSequence = nextSequence;
			result.sessionState.streamingInput->source.sequenceStart = streamingInput.source.sequenceStart;
			result.sessionState.streamingInput->source.sequenceEnd = streamingInput.source.sequenceEnd;
		}
		result.sessionState.debugInfo = SpeechRecognitionDebugInfo{
			.sherpaChunkCount = streamState.chunkCount,
			.sherpaDecodedTokenCount = streamState.decodedTokenCount,
			.sherpaEmittedTokenCount = static_cast<std::uint64_t>(streamState.baselineTokenIds.size()),
			.sherpaPendingSampleCount = 0,
			.sherpaPartialTextLength = static_cast<std::uint64_t>(streamState.partialText.size()),
			.sherpaLoopCount = loopGuard,
			.sherpaMaxLoopCount = maxLoops,
			.sherpaEncoderFrameCount = streamState.encoderFrameCount,
			.sherpaJoinerCallCount = streamState.joinerCallCount,
			.sherpaBlankTokenCount = streamState.blankTokenCount,
			.sherpaFbankSampleScalingMode = streamState.contractFbankSampleScalingMode,
			.sherpaContractFeatureFrameCount = streamState.contractFeatureFrameCount,
			.sherpaContractFeatureRealFrameCount = streamState.contractFeatureRealFrameCount,
			.sherpaContractFeaturePaddedFrameCount = streamState.contractFeaturePaddedFrameCount,
			.sherpaContractFeatureInputShape = streamState.contractFeatureInputShape,
			.sherpaContractFeatureLengthValue = streamState.contractFeatureLengthValue,
			.sherpaContractFeatureFirstFrameStats = streamState.contractFeatureFirstFrameStats,
			.sherpaContractFeatureLastFrameStats = streamState.contractFeatureLastFrameStats,
			.sherpaContractEncoderOutputShape = streamState.contractEncoderOutputShape,
			.sherpaContractEncoderValidFrameCount = streamState.contractEncoderValidFrameCount,
			.sherpaContractDecoderInputContext = streamState.contractDecoderInputContext,
			.sherpaContractDecoderInputShape = streamState.contractDecoderInputShape,
			.sherpaContractDecoderOutputShape = streamState.contractDecoderOutputShape,
			.sherpaContractDecoderVectorSlice = streamState.contractDecoderVectorSlice,
			.sherpaContractJoinerEncoderInputShape = streamState.contractJoinerEncoderInputShape,
			.sherpaContractJoinerDecoderInputShape = streamState.contractJoinerDecoderInputShape,
			.sherpaContractJoinerOutputShape = streamState.contractJoinerOutputShape,
			.sherpaContractJoinerLogitsSlice = streamState.contractJoinerLogitsSlice,
			.sherpaContractJoinerTopTokens = streamState.contractJoinerTopTokens,
			.sherpaDecoderJoinerContractSummary = streamState.contractDecoderJoinerSummary,
			.sherpaDecoderJoinerLastError = streamState.contractDecoderJoinerLastError,
			.sherpaDecoderJoinerContractFailureCount = streamState.decoderJoinerContractFailureCount,
			.sherpaDecoderJoinerValidatedCallCount = streamState.decoderJoinerValidatedCallCount,
			.sherpaLastBestTokenId = streamState.lastBestTokenId,
			.sherpaLastSecondBestTokenId = streamState.lastSecondBestTokenId,
			.sherpaSpeechActive = streamState.speechActive,
#if BLAZECLAW_HAS_ONNXRUNTIME
			.sherpaEncoderStateCacheBindingCount = static_cast<std::uint64_t>(m_encoderStateCacheBindings.size()),
#else
			.sherpaEncoderStateCacheBindingCount = 0,
#endif
			.sherpaEncoderStateCacheUpdateCount = streamState.encoderStateCacheUpdateCount,
			.sherpaEncoderStateCacheValidatedUpdateCount = streamState.encoderStateCacheValidatedUpdateCount,
			.sherpaEncoderStateCacheContractFailureCount = streamState.encoderStateCacheContractFailureCount,
			.sherpaEncoderStateCacheSummary = streamState.contractStateCacheSummary,
			.sherpaEncoderStateCacheLastError = streamState.contractStateCacheLastError,
			.sherpaEncoderLengthOutputCount = streamState.encoderLengthOutputCount,
			.sherpaEncoderLengthOutputUsed = streamState.encoderLengthOutputUsed,
			.sherpaRnntInnerLoopCount = streamState.rnntInnerLoopCount,
			.sherpaRnntMaxSymbolsHitCount = streamState.rnntMaxSymbolsHitCount,
			.sherpaRnntRepeatedTokenCount = streamState.rnntRepeatedTokenCount,
			.sherpaRnntMultiSymbolFrameCount = streamState.rnntMultiSymbolFrameCount,
			.sherpaRnntMaxSymbolsPerFrame = kSherpaMaxSymbolsPerFrame,
			.sherpaBpeModelPresent = m_artifacts.bpeModelPresent,
			.sherpaBpeVocabPresent = m_artifacts.bpeVocabPresent,
			.sherpaDecodedText = baselineDecodedText,
			.sherpaRawTokenPieces = JoinTokenPieces(streamState.baselineTokenIds),
			.sherpaFinalStreamRequest = isFinalStreamRequest,
			.sherpaLivePcmStream = isLivePcmStream,
			.sherpaFinalDrainComplete = finalDrainComplete,
			.sherpaFinalFbankFlush = shouldTreatInputAsFinal,
			.sherpaFinalSequenceEnd = streamingInput.source.sequenceEnd,
			.sherpaFinalCursorNext = nextSequence,
			.sherpaFinalRemainingSamples = finalRemainingSamples,
			.sherpaFinalOutcome = finalOutcome,
			.sherpaBaselineSampleRate = sampleRate,
			.sherpaBaselineChunkSamples = static_cast<std::uint64_t>(chunkSamples),
			.sherpaBaselineInputStartSequence = streamingInput.source.sequenceStart,
			.sherpaBaselineInputEndSequence = streamingInput.source.sequenceEnd,
			.sherpaBaselineCursorNextSequence = nextSequence,
			.sherpaBaselineFinalFlush = shouldTreatInputAsFinal,
			.sherpaBaselinePersisted = baselineDiagnosticPath.has_value(),
			.sherpaBaselineExpectedText = baselineExpectedText,
			.sherpaBaselineDecodedText = baselineDecodedText,
			.sherpaBaselineTokenIds = JoinTokenIds(streamState.baselineTokenIds),
			.sherpaBaselineTokenPieces = JoinTokenPieces(streamState.baselineTokenIds),
			.sherpaBaselineDiagnosticPath = baselineDiagnosticPath.has_value()
				? baselineDiagnosticPath->string()
				: std::string{},
		};

		streamState.nextSequence = nextSequence;
		if (shouldTreatInputAsFinal) {
			ClearStreamState(streamingInput.source.streamId);
		}
		else {
			std::lock_guard<std::mutex> lock(m_streamMutex);
			m_streamStateByStreamId[streamingInput.source.streamId] = std::move(streamState);
		}

		return result;
	}

	std::size_t SherpaZipformerStreamingEngine::CountTokens(
		const std::filesystem::path& tokensPath) {
		std::ifstream stream(tokensPath);
		if (!stream.is_open()) {
			return 0;
		}

		std::size_t count = 0;
		std::string line;
		while (std::getline(stream, line)) {
			if (!line.empty()) {
				++count;
			}
		}
		return count;
	}

} // namespace blazeclaw::core::speechrecognition::engines
