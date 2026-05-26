#include "pch.h"
#include "SpeechModelLayoutProbe.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::core::speechrecognition {

	namespace {

		bool EndsWithIgnoreCase(
			const std::string& value,
			const std::string& suffix) {
			if (suffix.size() > value.size()) {
				return false;
			}

			const std::size_t start = value.size() - suffix.size();
			for (std::size_t i = 0; i < suffix.size(); ++i) {
				const auto lhs = static_cast<unsigned char>(value[start + i]);
				const auto rhs = static_cast<unsigned char>(suffix[i]);
				if (std::tolower(lhs) != std::tolower(rhs)) {
					return false;
				}
			}

			return true;
		}

		bool StartsWithIgnoreCase(
			const std::string& value,
			const std::string& prefix) {
			if (prefix.size() > value.size()) {
				return false;
			}

			for (std::size_t i = 0; i < prefix.size(); ++i) {
				const auto lhs = static_cast<unsigned char>(value[i]);
				const auto rhs = static_cast<unsigned char>(prefix[i]);
				if (std::tolower(lhs) != std::tolower(rhs)) {
					return false;
				}
			}

			return true;
		}

		std::vector<std::string> BuildQwenCandidateVariants() {
			return { "int4", "fp16", "fp32" };
		}

		std::pair<std::filesystem::path, std::filesystem::path> BuildQwenPaths(
			const std::filesystem::path& rootPath,
			const std::string& variant) {
			if (variant == "int4") {
				return {
					rootPath / L"encoder.int4.onnx",
					rootPath / L"decoder_init.int4.onnx",
				};
			}

			if (variant == "fp16") {
				return {
					rootPath / L"encoder.fp16.onnx",
					rootPath / L"decoder_init.fp16.onnx",
				};
			}

			return {
				rootPath / L"encoder.onnx",
				rootPath / L"decoder_init.onnx",
			};
		}

	} // namespace

	SpeechModelLayoutProbeResult ProbeSpeechModelLayout(
		const std::filesystem::path& rootPath) {
		SpeechModelLayoutProbeResult result;
		if (rootPath.empty() || !std::filesystem::exists(rootPath)) {
			result.missingArtifacts.push_back("model_root_missing");
			return result;
		}

		const auto candidateVariants = BuildQwenCandidateVariants();
		for (const auto& variant : candidateVariants) {
			const auto paths = BuildQwenPaths(rootPath, variant);
			if (std::filesystem::exists(paths.first) &&
				std::filesystem::exists(paths.second)) {
				result.availableQwenVariants.push_back(variant);
			}
		}

		if (!result.availableQwenVariants.empty()) {
			result.kind = SpeechModelLayoutKind::QwenDecoderInitStep;
			result.layout = "qwen_decoder_init_step";
			result.availableArtifacts = {
				"encoder(.variant).onnx",
				"decoder_init(.variant).onnx",
			};
			return result;
		}

		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(rootPath, ec)) {
			if (ec || !entry.is_regular_file()) {
				continue;
			}

			const std::string filename =
				entry.path().filename().string();
			if (result.sherpaEncoderPath.empty() &&
				StartsWithIgnoreCase(filename, "encoder-") &&
				EndsWithIgnoreCase(filename, ".onnx")) {
				result.sherpaEncoderPath = entry.path();
				result.availableArtifacts.push_back("encoder-*.onnx");
				continue;
			}

			if (result.sherpaDecoderPath.empty() &&
				StartsWithIgnoreCase(filename, "decoder-") &&
				EndsWithIgnoreCase(filename, ".onnx")) {
				result.sherpaDecoderPath = entry.path();
				result.availableArtifacts.push_back("decoder-*.onnx");
				continue;
			}

			if (result.sherpaJoinerPath.empty() &&
				StartsWithIgnoreCase(filename, "joiner-") &&
				EndsWithIgnoreCase(filename, ".onnx")) {
				result.sherpaJoinerPath = entry.path();
				result.availableArtifacts.push_back("joiner-*.onnx");
				continue;
			}

			if (result.sherpaTokensPath.empty() &&
				filename == "tokens.txt") {
				result.sherpaTokensPath = entry.path();
				result.availableArtifacts.push_back("tokens.txt");
			}
		}

		if (result.sherpaEncoderPath.empty()) {
			result.missingArtifacts.push_back("encoder-*.onnx");
		}
		if (result.sherpaDecoderPath.empty()) {
			result.missingArtifacts.push_back("decoder-*.onnx");
		}
		if (result.sherpaJoinerPath.empty()) {
			result.missingArtifacts.push_back("joiner-*.onnx");
		}
		if (result.sherpaTokensPath.empty()) {
			result.missingArtifacts.push_back("tokens.txt");
		}

		if (result.missingArtifacts.empty()) {
			result.kind = SpeechModelLayoutKind::SherpaZipformerTransducer;
			result.layout = "sherpa_zipformer_transducer";
			return result;
		}

		if (!result.availableArtifacts.empty()) {
			result.layout = "incomplete_sherpa_zipformer_transducer";
		}

		return result;
	}

} // namespace blazeclaw::core::speechrecognition
