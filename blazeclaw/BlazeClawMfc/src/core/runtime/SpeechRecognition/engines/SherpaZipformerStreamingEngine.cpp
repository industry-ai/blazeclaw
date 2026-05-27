#include "pch.h"
#include "SherpaZipformerStreamingEngine.h"

#include "../StreamingAudioSourceRegistry.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <numeric>
#include <sstream>

namespace blazeclaw::core::speechrecognition::engines {

	namespace {

		constexpr float kSpeechEnergyThreshold = 0.0001f;
		constexpr std::size_t kMinSherpaFeatureFrames = 16;
		constexpr std::size_t kSherpaFeatureHopSamples = 160;

		bool IsFiniteSample(float value) {
			return std::isfinite(value) != 0;
		}
		float PoveyWindow(std::size_t idx, std::size_t n) {
			if (n <= 1) {
				return 1.0f;
			}
			const float angle = static_cast<float>(2.0 * 3.14159265358979323846 * idx / (n - 1));
			const float hann = 0.5f - 0.5f * std::cos(angle);
			return std::pow((std::max)(0.0f, hann), 0.85f);
		}

		float HzToMelSlaney(float hz) {
			constexpr float kFSp = 200.0f / 3.0f;
			constexpr float kMinLogHz = 1000.0f;
			constexpr float kMinLogMel = kMinLogHz / kFSp;
			constexpr float kLogStep = 0.06875177742094912f;
			if (hz < kMinLogHz) {
				return hz / kFSp;
			}
			return kMinLogMel + std::log(hz / kMinLogHz) / kLogStep;
		}

		float MelToHzSlaney(float mel) {
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
			const std::size_t nBins = (nFft / 2) + 1;
			std::vector<float> filters(nMels * nBins, 0.0f);
			const float melMin = HzToMelSlaney(fMin);
			const float melMax = HzToMelSlaney(fMax);
			std::vector<float> melPoints(nMels + 2, 0.0f);
			for (std::size_t i = 0; i < melPoints.size(); ++i) {
				melPoints[i] = melMin + (melMax - melMin) *
					static_cast<float>(i) /
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
				const float scale = 2.0f / (rightHz - leftHz);
				for (std::size_t b = 0; b < nBins; ++b) {
					const float hz = static_cast<float>(sampleRate) *
						static_cast<float>(b) /
						static_cast<float>(nFft);
					float weight = 0.0f;
					if (hz >= leftHz && hz < centerHz) {
						weight = (hz - leftHz) / (centerHz - leftHz);
					}
					else if (hz >= centerHz && hz <= rightHz) {
						weight = (rightHz - hz) / (rightHz - centerHz);
					}
					filters[(m - 1) * nBins + b] = weight * scale;
				}
			}

			return filters;
		}

		std::vector<float> BuildLogMel(
			const std::vector<float>& samples,
			std::uint32_t sampleRate,
			std::size_t nMels,
			std::size_t& outFrames) {
			constexpr std::size_t kFrameLength = 400;
			constexpr std::size_t kNfft = 512;
			constexpr std::size_t kHop = 160;
			constexpr std::size_t kMinSamples = kFrameLength;
			outFrames = 0;
			if (samples.size() < kMinSamples) {
				return {};
			}

			const auto filters = BuildMelFilterbank(sampleRate, kNfft, nMels, 0.0f, 8000.0f);
			const std::size_t nBins = (kNfft / 2) + 1;
			outFrames = 1 + ((samples.size() - kFrameLength) / kHop);
			std::vector<float> output(nMels * outFrames, 0.0f);
			std::vector<float> window(kFrameLength, 0.0f);
			for (std::size_t i = 0; i < kFrameLength; ++i) {
				window[i] = PoveyWindow(i, kFrameLength);
			}

			std::vector<float> spectrum(nBins, 0.0f);
			std::vector<float> frameBuffer(kNfft, 0.0f);
			for (std::size_t frame = 0; frame < outFrames; ++frame) {
				const std::size_t base = frame * kHop;
				frameBuffer.assign(kNfft, 0.0f);
				double mean = 0.0;
				for (std::size_t n = 0; n < kFrameLength; ++n) {
					mean += static_cast<double>(samples[base + n]);
				}
				mean /= static_cast<double>(kFrameLength);

				float previous = 0.0f;
				for (std::size_t n = 0; n < kFrameLength; ++n) {
					const float current = static_cast<float>(static_cast<double>(samples[base + n]) - mean);
					const float emphasized = n == 0
						? current
						: current - (0.97f * previous);
					frameBuffer[n] = emphasized * window[n];
					previous = current;
				}

				for (std::size_t k = 0; k < nBins; ++k) {
					double real = 0.0;
					double imag = 0.0;
					for (std::size_t n = 0; n < kNfft; ++n) {
						const double x = static_cast<double>(frameBuffer[n]);
						const double angle = (2.0 * 3.14159265358979323846 * static_cast<double>(k * n)) /
							static_cast<double>(kNfft);
						real += x * std::cos(angle);
						imag -= x * std::sin(angle);
					}
					spectrum[k] = static_cast<float>(real * real + imag * imag);
				}

				for (std::size_t m = 0; m < nMels; ++m) {
					double melEnergy = 0.0;
					for (std::size_t b = 0; b < nBins; ++b) {
						melEnergy += static_cast<double>(filters[m * nBins + b]) *
							static_cast<double>(spectrum[b]);
					}
					output[frame * nMels + m] = static_cast<float>(std::log((std::max)(1e-10, melEnergy)));
				}
			}

			return output;
		}

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
			case SherpaZipformerStreamingEngine::TensorBindingKind::DecoderInputTokens: return L"decoder_input_tokens";
			case SherpaZipformerStreamingEngine::TensorBindingKind::DecoderOut: return L"decoder_out";
			case SherpaZipformerStreamingEngine::TensorBindingKind::UnknownInt64: return L"unknown_int64";
			case SherpaZipformerStreamingEngine::TensorBindingKind::UnknownFloat: return L"unknown_float";
			default: return L"unknown";
			}
		}

#if BLAZECLAW_HAS_ONNXRUNTIME
		std::vector<SherpaZipformerStreamingEngine::TensorBinding> BuildTensorBindings(
			Ort::Session& session,
			bool isEncoder,
			bool isJoiner,
			std::size_t& outLikelyMainOutputIndex,
			std::vector<std::string>& outOutputNames) {
			Ort::AllocatorWithDefaultOptions allocator;
			const auto inputCount = session.GetInputCount();
			std::vector<SherpaZipformerStreamingEngine::TensorBinding> bindings;
			bindings.reserve(inputCount);
			std::size_t unknownInt64Count = 0;
			std::size_t unknownFloatCount = 0;

			for (std::size_t i = 0; i < inputCount; ++i) {
				auto inputNameAlloc = session.GetInputNameAllocated(i, allocator);
				SherpaZipformerStreamingEngine::TensorBinding binding;
				binding.name = inputNameAlloc.get();
				binding.normalizedStateName = NormalizeEncoderStateName(binding.name);
				auto tensorInfo = session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
				binding.shape = tensorInfo.GetShape();
				binding.elementType = tensorInfo.GetElementType();
				const auto lowered = ToLowerCopy(binding.name);
				const bool looksLikeEncoderCache =
					lowered.find("cached") != std::string::npos ||
					lowered.find("cache") != std::string::npos ||
					lowered.find("processed_lens") != std::string::npos;
				if (isEncoder) {
					if (!looksLikeEncoderCache &&
						(lowered.find("x_lens") != std::string::npos ||
						lowered.find("lens") != std::string::npos ||
						lowered.find("length") != std::string::npos)) {
						binding.kind = SherpaZipformerStreamingEngine::TensorBindingKind::FeatureLengths;
					}
					else if (!looksLikeEncoderCache &&
						(lowered == "x" ||
						lowered.find("speech") != std::string::npos ||
						lowered.find("feat") != std::string::npos ||
						lowered.find("input") != std::string::npos)) {
						binding.kind = SherpaZipformerStreamingEngine::TensorBindingKind::Features;
					}
				}
				else if (lowered == "y" || lowered.find("token") != std::string::npos) {
					binding.kind = SherpaZipformerStreamingEngine::TensorBindingKind::DecoderInputTokens;
				}
				else if (lowered.find("encoder") != std::string::npos) {
					binding.kind = SherpaZipformerStreamingEngine::TensorBindingKind::EncoderOut;
				}
				else if (lowered.find("decoder") != std::string::npos) {
					binding.kind = SherpaZipformerStreamingEngine::TensorBindingKind::DecoderOut;
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
			outLikelyMainOutputIndex = 0;
			int bestMainScore = (std::numeric_limits<int>::min)();
			for (std::size_t i = 0; i < outputCount; ++i) {
				auto outputNameAlloc = session.GetOutputNameAllocated(i, allocator);
				outOutputNames.push_back(outputNameAlloc.get());

				int score = 0;
				const std::string lowered = ToLowerCopy(outOutputNames.back());
				const bool isLens =
					lowered.find("_lens") != std::string::npos ||
					lowered.find("length") != std::string::npos ||
					lowered.find("lens") != std::string::npos;
				if (isLens) {
					score -= 30;
				}

				try {
					auto outInfo = session.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
					const auto outType = outInfo.GetElementType();
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

		if (layout.kind != SpeechModelLayoutKind::SherpaZipformerTransducer) {
			outError = "sherpa layout not detected";
			return false;
		}

		m_artifacts.encoderPath = layout.sherpaEncoderPath;
		m_artifacts.decoderPath = layout.sherpaDecoderPath;
		m_artifacts.joinerPath = layout.sherpaJoinerPath;
		m_artifacts.tokensPath = layout.sherpaTokensPath;

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
				m_encoderOutputNames);
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

	std::string SherpaZipformerStreamingEngine::TokenIdsToText(
		const std::vector<std::int64_t>& tokenIds,
		const std::unordered_map<std::int64_t, std::string>& tokenById,
		std::int64_t unkId) {
		std::string text;
		for (const auto tokenId : tokenIds) {
			const auto it = tokenById.find(tokenId);
			if (it == tokenById.end()) {
				continue;
			}
			const auto decoded = DecodeTokenPiece(it->second);
			if (decoded == "<blk>" ||
				decoded == "<blank>" ||
				decoded == "<sos/eos>" ||
				decoded == "<eos>") {
				continue;
			}
			if (tokenId == unkId || decoded == "<unk>") {
				text.push_back('?');
				continue;
			}
			text += decoded;
		}

		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
			text.erase(text.begin());
		}
		while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
			text.pop_back();
		}
		return text;
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
			streamState = cachedState;
		}

		std::uint64_t nextSequence = streamState.nextSequence;

		const auto oldestOpt =
			GetStreamingAudioOldestSequence(streamingInput.source.streamId);
		if (oldestOpt.has_value() && nextSequence < *oldestOpt) {
			nextSequence = *oldestOpt;
			streamState.pendingSamples.clear();
		}

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
			[&streamState, this](std::vector<float>& decoderVector) -> bool {
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
					if (shape.empty()) {
						shape = { 1, static_cast<std::int64_t>(streamState.decoderContext.size()) };
					}
					if (shape.size() == 1) {
						shape[0] = static_cast<std::int64_t>(streamState.decoderContext.size());
					}
					else {
						shape[0] = 1;
						shape[shape.size() - 1] = static_cast<std::int64_t>(streamState.decoderContext.size());
					}

					const std::size_t elements = static_cast<std::size_t>(
						(std::max)(std::int64_t{ 1 },
							std::accumulate(
								shape.begin(),
								shape.end(),
								std::int64_t{ 1 },
								[](std::int64_t a, std::int64_t b) {
									return a * ((std::max)(std::int64_t{ 1 }, b));
								}))); 

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
				return false;
			}

			const std::size_t total =
				info.GetElementCount() > 0
				? static_cast<std::size_t>(info.GetElementCount())
				: std::size_t{ 1 };
			if (total == 0) {
				return false;
			}

			const float* data = decoderMain.GetTensorData<float>();
			if (data == nullptr) {
				return false;
			}

			std::size_t vectorSize = static_cast<std::size_t>((std::max)(std::int64_t{ 1 }, shape.back()));
			if (vectorSize > total) {
				vectorSize = total;
			}

			decoderVector.assign(data + (total - vectorSize), data + total);
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
			streamState.pendingSamples.insert(
				streamState.pendingSamples.end(),
				chunk.begin(),
				chunk.end());

			const float energy = ComputeFrameEnergy(chunk);
			const bool frameSpeech = energy >= kSpeechEnergyThreshold;
			if (streamState.chunkCount % 10 == 1) {
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

			std::size_t featureFrames = 0;
			std::size_t consumedFeatureFrames = 0;
			const auto logMel = BuildLogMel(
				streamState.pendingSamples,
				sampleRate,
				80,
				featureFrames);
			if (streamState.chunkCount % 10 == 1) {
				TRACE(L"[SherpaStreaming] pendingSamples=%llu featureFrames=%llu\n",
					(unsigned long long)streamState.pendingSamples.size(), (unsigned long long)featureFrames);
			}

			const bool forceFlushFeatures = !isLivePcmStream &&
				streamingInput.source.sequenceEnd > 0 &&
				nextSequence + static_cast<std::uint64_t>(requestSamples) >= streamingInput.source.sequenceEnd;
			if (!logMel.empty() &&
				featureFrames > 0 &&
				(featureFrames >= kMinSherpaFeatureFrames || forceFlushFeatures)) {
#if BLAZECLAW_HAS_ONNXRUNTIME
				try {
					std::size_t frameLimit = featureFrames;
					for (const auto& binding : m_encoderInputBindings) {
						if (binding.kind == TensorBindingKind::Features &&
							binding.shape.size() >= 2 &&
							binding.shape[1] > 0) {
							frameLimit = static_cast<std::size_t>(binding.shape[1]);
							break;
						}
					}

					if (frameLimit == 0) {
						frameLimit = featureFrames;
					}

					std::size_t effectiveFrames = featureFrames;
					if (frameLimit > 0) {
						effectiveFrames = frameLimit;
					}
					if (effectiveFrames == 0) {
						break;
					}

					const std::size_t frameStride = 80;
					std::vector<float> featureInputBuffer(effectiveFrames * frameStride, 0.0f);
					const std::size_t copiedFrames = (std::min)(featureFrames, effectiveFrames);
					if (copiedFrames > 0) {
						const std::size_t sourceOffset = (featureFrames - copiedFrames) * frameStride;
						const std::size_t destOffset = (effectiveFrames - copiedFrames) * frameStride;
						std::copy(
							logMel.begin() + static_cast<std::ptrdiff_t>(sourceOffset),
							logMel.begin() + static_cast<std::ptrdiff_t>(sourceOffset + (copiedFrames * frameStride)),
							featureInputBuffer.begin() + static_cast<std::ptrdiff_t>(destOffset));
					}
					const float* featureDataPtr = featureInputBuffer.data();
					const std::size_t featureElementCount = featureInputBuffer.size();
					consumedFeatureFrames = copiedFrames;

					Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(
						OrtArenaAllocator,
						OrtMemTypeDefault);

					std::vector<Ort::Value> encoderInputs;
					std::vector<const char*> encoderInputNames;
					std::vector<std::vector<std::int64_t>> encoderInt64Buffers;
					std::vector<std::vector<float>> encoderFloatBuffers;
					std::vector<std::size_t> encoderInt64BindingIndexes;
					std::vector<std::size_t> encoderFloatBindingIndexes;
					std::vector<std::string> encoderInt64StateNames;
					std::vector<std::string> encoderFloatStateNames;
					encoderInputs.reserve(m_encoderInputBindings.size());
					encoderInputNames.reserve(m_encoderInputBindings.size());
					encoderInt64Buffers.reserve(m_encoderInputBindings.size());
					encoderFloatBuffers.reserve(m_encoderInputBindings.size());
					encoderInt64BindingIndexes.reserve(m_encoderInputBindings.size());
					encoderFloatBindingIndexes.reserve(m_encoderInputBindings.size());
					encoderInt64StateNames.reserve(m_encoderInputBindings.size());
					encoderFloatStateNames.reserve(m_encoderInputBindings.size());

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

							encoderInputs.push_back(Ort::Value::CreateTensor<float>(
								memoryInfo,
								const_cast<float*>(featureDataPtr),
								featureElementCount,
								shape.data(),
								shape.size()));
						}
						else if (binding.kind == TensorBindingKind::FeatureLengths) {
							const std::int64_t featureLength = static_cast<std::int64_t>((std::max)(std::size_t{ 1 }, copiedFrames));
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
								if (streamState.encoderInt64StateCaches.size() <= binding.bufferIndex) {
									streamState.encoderInt64StateCaches.resize(binding.bufferIndex + 1);
								}
								auto& cache = streamState.encoderInt64StateCaches[binding.bufferIndex];
								if (cache.size() != elements) {
									cache.assign(elements, 0);
								}
								encoderInt64Buffers.push_back(cache);
								encoderInt64BindingIndexes.push_back(binding.bufferIndex);
								encoderInt64StateNames.push_back(binding.normalizedStateName);
								auto& stateRef = encoderInt64Buffers.back();
								encoderInputs.push_back(Ort::Value::CreateTensor<std::int64_t>(
									memoryInfo,
									stateRef.data(),
									stateRef.size(),
									shape.data(),
									shape.size()));
							}
							else {
								if (streamState.encoderFloatStateCaches.size() <= binding.bufferIndex) {
									streamState.encoderFloatStateCaches.resize(binding.bufferIndex + 1);
								}
								auto& cache = streamState.encoderFloatStateCaches[binding.bufferIndex];
								if (cache.size() != elements) {
									cache.assign(elements, 0.0f);
								}
								encoderFloatBuffers.push_back(cache);
								encoderFloatBindingIndexes.push_back(binding.bufferIndex);
								encoderFloatStateNames.push_back(binding.normalizedStateName);
								auto& stateRef = encoderFloatBuffers.back();
								encoderInputs.push_back(Ort::Value::CreateTensor<float>(
									memoryInfo,
									stateRef.data(),
									stateRef.size(),
									shape.data(),
									shape.size()));
							}
						}
						encoderInputNames.push_back(binding.name.c_str());
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
						std::size_t nextInt64StateOutput = 0;
						std::size_t nextFloatStateOutput = 0;
						auto findStateIndexByName = [](const std::vector<std::string>& names, const std::string& outputName) -> std::optional<std::size_t> {
							if (outputName.empty()) {
								return std::nullopt;
							}
							for (std::size_t i = 0; i < names.size(); ++i) {
								if (!names[i].empty() && names[i] == outputName) {
									return i;
								}
							}
							return std::nullopt;
						};
						auto isCompatibleStateShape = [](const std::vector<std::int64_t>& expectedShape, std::size_t actualElements) {
							if (expectedShape.empty()) {
								return actualElements > 0;
							}
							std::uint64_t expectedElements = 1;
							bool hasDynamicDim = false;
							for (const auto dim : expectedShape) {
								if (dim <= 0) {
									hasDynamicDim = true;
									continue;
								}
								expectedElements *= static_cast<std::uint64_t>(dim);
							}
							return hasDynamicDim || expectedElements == static_cast<std::uint64_t>(actualElements);
						};
						for (std::size_t outputIndex = 0; outputIndex < encoderOutputs.size(); ++outputIndex) {
							if (outputIndex == m_encoderMainOutputIndex || !encoderOutputs[outputIndex].IsTensor()) {
								continue;
							}

							auto outputInfo = encoderOutputs[outputIndex].GetTensorTypeAndShapeInfo();
							const auto outputType = outputInfo.GetElementType();
							const std::string normalizedOutputName = outputIndex < m_encoderOutputNames.size()
								? NormalizeEncoderStateName(m_encoderOutputNames[outputIndex])
								: std::string();
							const bool looksLikeLengthOutput =
								normalizedOutputName.find("len") != std::string::npos ||
								normalizedOutputName.find("length") != std::string::npos ||
								normalizedOutputName.find("processed_lens") != std::string::npos;
							if (outputType == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64) {
								const auto rawElementCount = outputInfo.GetElementCount();
								const std::size_t elements = rawElementCount > 0
									? static_cast<std::size_t>(rawElementCount)
									: std::size_t{ 0 };
								const auto* data = encoderOutputs[outputIndex].GetTensorData<std::int64_t>();
								if (looksLikeLengthOutput && data != nullptr && elements > 0 && data[0] > 0) {
									encoderOutputFrameLimit = static_cast<std::size_t>(data[0]);
								}
								if (nextInt64StateOutput >= encoderInt64BindingIndexes.size()) {
									continue;
								}
								if (data != nullptr && elements > 0) {
									const auto matchedIndex = findStateIndexByName(encoderInt64StateNames, normalizedOutputName);
									const std::size_t bindingVectorIndex = matchedIndex.value_or(nextInt64StateOutput++);
									if (bindingVectorIndex >= encoderInt64BindingIndexes.size()) {
										continue;
									}
									const std::size_t stateIndex = encoderInt64BindingIndexes[bindingVectorIndex];
									if (stateIndex >= m_encoderInputBindings.size() ||
										!isCompatibleStateShape(m_encoderInputBindings[stateIndex].shape, elements)) {
										continue;
									}
									if (streamState.encoderInt64StateCaches.size() <= stateIndex) {
										streamState.encoderInt64StateCaches.resize(stateIndex + 1);
									}
									streamState.encoderInt64StateCaches[stateIndex].assign(data, data + elements);
								}
							}
							else if (outputType == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
								nextFloatStateOutput < encoderFloatBindingIndexes.size()) {
								const auto rawElementCount = outputInfo.GetElementCount();
								const std::size_t elements = rawElementCount > 0
									? static_cast<std::size_t>(rawElementCount)
									: std::size_t{ 0 };
								const auto* data = encoderOutputs[outputIndex].GetTensorData<float>();
								if (data != nullptr && elements > 0) {
									const auto matchedIndex = findStateIndexByName(encoderFloatStateNames, normalizedOutputName);
									const std::size_t bindingVectorIndex = matchedIndex.value_or(nextFloatStateOutput++);
									if (bindingVectorIndex >= encoderFloatBindingIndexes.size()) {
										continue;
									}
									const std::size_t stateIndex = encoderFloatBindingIndexes[bindingVectorIndex];
									if (stateIndex >= m_encoderInputBindings.size() ||
										!isCompatibleStateShape(m_encoderInputBindings[stateIndex].shape, elements)) {
										continue;
									}
									if (streamState.encoderFloatStateCaches.size() <= stateIndex) {
										streamState.encoderFloatStateCaches.resize(stateIndex + 1);
									}
									streamState.encoderFloatStateCaches[stateIndex].assign(data, data + elements);
								}
							}
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
									if (encoderOutputFrameLimit.has_value() && *encoderOutputFrameLimit < encoderFrames) {
										encoderFrames = *encoderOutputFrameLimit;
									}

									if (encoderFrames > 0 && encoderDim > 0) {
										streamState.encoderFrameCount += static_cast<std::uint64_t>(encoderFrames);
										std::vector<float> decoderVector;
										if (updateDecoderFromContext(decoderVector)) {
											for (std::size_t frameIdx = 0; frameIdx < encoderFrames; ++frameIdx) {
												const float* framePtr = encoderData + (frameIdx * encoderDim);
												std::vector<float> encoderFrame(framePtr, framePtr + encoderDim);

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
														if (shape.size() >= 3) {
															shape[0] = 1;
															shape[1] = 1;
															shape[2] = static_cast<std::int64_t>(encoderFrame.size());
														}
														else if (shape.size() == 2) {
															shape[0] = 1;
															shape[1] = static_cast<std::int64_t>(encoderFrame.size());
														}
														else {
															shape = { 1, 1, static_cast<std::int64_t>(encoderFrame.size()) };
														}
														joinerInputs.push_back(Ort::Value::CreateTensor<float>(
															memoryInfo,
															encoderFrame.data(),
															encoderFrame.size(),
															shape.data(),
															shape.size()));
													}
													else if (binding.kind == TensorBindingKind::DecoderOut) {
														if (shape.size() >= 2) {
															shape[0] = 1;
															shape[shape.size() - 1] = static_cast<std::int64_t>(decoderVector.size());
														}
														else {
															shape = { 1, static_cast<std::int64_t>(decoderVector.size()) };
														}
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
												 continue;
											 }
											 joinerMainIndex = bestIndex;
										 }

										 auto& joinerMain = joinerOutputs[joinerMainIndex];
												if (!joinerMain.IsTensor()) {
													continue;
												}

												auto joinerInfo = joinerMain.GetTensorTypeAndShapeInfo();
												if (joinerInfo.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
													continue;
												}

												const std::size_t logitsCount =
													joinerInfo.GetElementCount() > 0
													? static_cast<std::size_t>(joinerInfo.GetElementCount())
													: std::size_t{ 1 };
												const auto joinerShape = joinerInfo.GetShape();
												const float* logits = joinerMain.GetTensorData<float>();
												if (logits == nullptr || logitsCount == 0) {
													continue;
												}

												std::size_t vocabSize = logitsCount;
												if (!joinerShape.empty() && joinerShape.back() > 0) {
													vocabSize = static_cast<std::size_t>(joinerShape.back());
												}
												if (vocabSize == 0 || vocabSize > logitsCount) {
													vocabSize = logitsCount;
												}
												const std::size_t logitsOffset = logitsCount - vocabSize;

												const auto bestIt = std::max_element(
													logits + logitsOffset,
													logits + logitsOffset + vocabSize);
												if (bestIt == logits + logitsOffset + vocabSize) {
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
												if (tokenId != m_blankId) {
													TRACE(L"[SherpaStreaming] emitted tokenId=%lld value=%f blankId=%lld\n",
														(long long)tokenId, *bestIt, (long long)m_blankId);
												}
												if (tokenId == m_blankId || tokenId == m_eosId) {
													if (tokenId == m_blankId) {
														++streamState.blankTokenCount;
													}
													continue;
												}

												streamState.emittedTokenIds.push_back(tokenId);
												++streamState.decodedTokenCount;
												streamState.decoderContext.push_back(tokenId);
												const std::size_t contextSize = (std::max)(std::size_t{ 1 }, m_decoderContextSize);
												if (streamState.decoderContext.size() > contextSize) {
													streamState.decoderContext.erase(
														streamState.decoderContext.begin(),
														streamState.decoderContext.end() - static_cast<std::ptrdiff_t>(contextSize));
												}

												updateDecoderFromContext(decoderVector);
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
				streamState.partialText = TokenIdsToText(
					streamState.emittedTokenIds,
					m_tokenById,
					m_unkId);
				result.text = streamState.partialText;

					std::size_t consumedSamples = consumedFeatureFrames * kSherpaFeatureHopSamples;
					if (!isLivePcmStream &&
						streamingInput.source.sequenceEnd > 0 &&
						nextSequence >= streamingInput.source.sequenceEnd) {
						consumedSamples = streamState.pendingSamples.size();
					}
				if (consumedSamples > 0 && consumedSamples <= streamState.pendingSamples.size()) {
					streamState.pendingSamples.erase(
						streamState.pendingSamples.begin(),
						streamState.pendingSamples.begin() + static_cast<std::ptrdiff_t>(consumedSamples));
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
		const bool shouldFinalizeByVad =
			streamState.speechActive && streamState.silenceChunkCount >= 3;
		const bool shouldFinalize = shouldFinalizeByVad || shouldTreatInputAsFinal;
		if (!streamState.partialText.empty() && shouldTreatInputAsFinal) {
			result.sessionState.transcriptText = streamState.partialText;
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
				streamState.pendingSamples.clear();
				streamState.silenceChunkCount = 0;
				streamState.speechActive = false;
			}
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
			.sherpaEmittedTokenCount = static_cast<std::uint64_t>(streamState.emittedTokenIds.size()),
			.sherpaPendingSampleCount = static_cast<std::uint64_t>(streamState.pendingSamples.size()),
			.sherpaPartialTextLength = static_cast<std::uint64_t>(streamState.partialText.size()),
			.sherpaLoopCount = loopGuard,
			.sherpaMaxLoopCount = maxLoops,
			.sherpaEncoderFrameCount = streamState.encoderFrameCount,
			.sherpaJoinerCallCount = streamState.joinerCallCount,
			.sherpaBlankTokenCount = streamState.blankTokenCount,
			.sherpaLastBestTokenId = streamState.lastBestTokenId,
			.sherpaLastSecondBestTokenId = streamState.lastSecondBestTokenId,
			.sherpaSpeechActive = streamState.speechActive,
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
