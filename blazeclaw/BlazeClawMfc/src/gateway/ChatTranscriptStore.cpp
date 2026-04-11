#include "pch.h"
#include "ChatTranscriptStore.h"
#include "GatewayPersistencePaths.h"
#include "Telemetry.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace blazeclaw::gateway {
	namespace {
		bool TranscriptHasIdempotencyKey(
			const std::filesystem::path& transcriptPath,
			const std::string& idempotencyKey) {
			if (idempotencyKey.empty()) {
				return false;
			}

			try {
				std::ifstream input(transcriptPath, std::ios::in | std::ios::binary);
				if (!input.is_open()) {
					return false;
				}

				std::string line;
				while (std::getline(input, line)) {
					if (line.find("\"idempotencyKey\":") == std::string::npos) {
						continue;
					}

					if (line.find(std::string("\"idempotencyKey\":") + JsonString(idempotencyKey)) !=
						std::string::npos) {
						return true;
					}
				}
			}
			catch (...) {
				return false;
			}

			return false;
		}

		std::string NormalizeSessionKeyForFileName(const std::string& sessionKey) {
			const std::string fallback = "main";
			const std::string source = sessionKey.empty() ? fallback : sessionKey;
			std::string normalized;
			normalized.reserve(source.size());
			for (const unsigned char ch : source) {
				const bool isSafe =
					(ch >= 'a' && ch <= 'z') ||
					(ch >= 'A' && ch <= 'Z') ||
					(ch >= '0' && ch <= '9') ||
					ch == '-' ||
					ch == '_' ||
					ch == '.';
				normalized.push_back(isSafe ? static_cast<char>(ch) : '_');
			}

			if (normalized.empty()) {
				return fallback;
			}

			return normalized;
		}

		std::uint64_t CurrentEpochMs() {
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch())
				.count());
		}

		std::string BuildAssistantMessageJson(
			const std::string& messageId,
			const std::string& text,
			const std::string& label,
			const std::string& idempotencyKey,
			const std::uint64_t timestampMs) {
			std::string payload =
				"{\"id\":" + JsonString(messageId) +
				",\"role\":\"assistant\"" +
				",\"text\":" + JsonString(text) +
				",\"content\":[{\"type\":\"text\",\"text\":" + JsonString(text) + "}]" +
				",\"timestamp\":" + std::to_string(timestampMs);
			if (!label.empty()) {
				payload += ",\"label\":" + JsonString(label);
			}
			if (!idempotencyKey.empty()) {
				payload += ",\"idempotencyKey\":" + JsonString(idempotencyKey);
			}
			payload += "}";
			return payload;
		}
	}

	ChatTranscriptStore::AppendResult ChatTranscriptStore::AppendAssistantMessage(
		const AppendParams& params) const {
		static std::atomic<std::uint64_t> sequence{ 0 };

		const std::uint64_t timestampMs = CurrentEpochMs();
		const std::string effectiveSessionKey =
			params.sessionKey.empty() ? std::string("main") : params.sessionKey;
		const std::string messageId =
			"msg-" + std::to_string(timestampMs) +
			"-" + std::to_string(++sequence);
		const std::string messageJson = BuildAssistantMessageJson(
			messageId,
			params.message,
			params.label,
			params.idempotencyKey,
			timestampMs);

		try {
			const std::filesystem::path transcriptPath =
				ResolveGatewayStateFilePath("chat-transcripts") /
				(NormalizeSessionKeyForFileName(effectiveSessionKey) + ".jsonl");
			std::filesystem::create_directories(transcriptPath.parent_path());
			if (TranscriptHasIdempotencyKey(transcriptPath, params.idempotencyKey)) {
				return AppendResult{
					.ok = true,
					.messageId = {},
					.messageJson = {},
					.error = {},
				};
			}

			std::ofstream output(
				transcriptPath,
				std::ios::out | std::ios::app | std::ios::binary);
			if (!output.is_open()) {
				return AppendResult{
					.ok = false,
					.messageId = {},
					.messageJson = {},
					.error = "failed to open transcript file",
				};
			}

			const std::string line =
				"{\"messageId\":" + JsonString(messageId) +
				",\"sessionKey\":" + JsonString(effectiveSessionKey) +
				(params.idempotencyKey.empty()
					? std::string()
					: (",\"idempotencyKey\":" + JsonString(params.idempotencyKey))) +
				",\"message\":" + messageJson + "}";
			output << line << "\n";
			output.flush();
			if (!output.good()) {
				return AppendResult{
					.ok = false,
					.messageId = {},
					.messageJson = {},
					.error = "failed to flush transcript file",
				};
			}
		}
		catch (const std::exception& ex) {
			return AppendResult{
				.ok = false,
				.messageId = {},
				.messageJson = {},
				.error = ex.what(),
			};
		}
		catch (...) {
			return AppendResult{
				.ok = false,
				.messageId = {},
				.messageJson = {},
				.error = "unknown transcript persistence failure",
			};
		}

		return AppendResult{
			.ok = true,
			.messageId = messageId,
			.messageJson = messageJson,
			.error = {},
		};
	}

} // namespace blazeclaw::gateway
