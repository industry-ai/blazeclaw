#include "pch.h"
#include "ChatHistoryPolicy.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <optional>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {
	namespace {
		constexpr std::size_t kTextMaxChars = 12000;
		constexpr std::size_t kMaxEntriesPerSession = 500;
		constexpr std::size_t kMaxSingleMessageBytes = 128 * 1024;
		constexpr std::size_t kMaxHistoryBytes = 256 * 1024;
		constexpr const char* kOversizedPlaceholder =
			"[chat.history omitted: message too large]";

		std::string TruncateText(const std::string& text, bool& changed) {
			if (text.size() <= kTextMaxChars) {
				return text;
			}

			changed = true;
			return text.substr(0, kTextMaxChars) + "\n...(truncated)...";
		}

		std::size_t JsonBytes(const nlohmann::json& value) {
			return value.dump().size();
		}

		std::optional<std::string> ExtractAssistantText(
			const nlohmann::json& message) {
			if (!message.is_object()) {
				return std::nullopt;
			}

			const auto roleIt = message.find("role");
			if (roleIt == message.end() || !roleIt->is_string() ||
				roleIt->get<std::string>() != "assistant") {
				return std::nullopt;
			}

			const auto textIt = message.find("text");
			if (textIt != message.end() && textIt->is_string()) {
				return textIt->get<std::string>();
			}

			const auto contentIt = message.find("content");
			if (contentIt == message.end()) {
				return std::nullopt;
			}

			if (contentIt->is_string()) {
				return contentIt->get<std::string>();
			}

			if (!contentIt->is_array()) {
				return std::nullopt;
			}

			std::string joined;
			for (const auto& block : *contentIt) {
				if (!block.is_object()) {
					return std::nullopt;
				}

				const auto typeIt = block.find("type");
				const auto textBlockIt = block.find("text");
				if (typeIt == block.end() || !typeIt->is_string() ||
					typeIt->get<std::string>() != "text" ||
					textBlockIt == block.end() || !textBlockIt->is_string()) {
					return std::nullopt;
				}

				if (!joined.empty()) {
					joined += "\n";
				}
				joined += textBlockIt->get<std::string>();
			}

			if (joined.empty()) {
				return std::nullopt;
			}

			return joined;
		}

		bool IsSilentAssistantMessage(const nlohmann::json& message) {
			const auto text = ExtractAssistantText(message);
			if (!text.has_value()) {
				return false;
			}

			std::string trimmed = text.value();
			trimmed.erase(trimmed.begin(), std::find_if(trimmed.begin(), trimmed.end(), [](unsigned char ch) {
				return !std::isspace(ch);
				}));
			trimmed.erase(std::find_if(trimmed.rbegin(), trimmed.rend(), [](unsigned char ch) {
				return !std::isspace(ch);
				}).base(), trimmed.end());

			return trimmed == "NO_REPLY";
		}

		void SanitizeContentBlock(nlohmann::json& block, bool& changed) {
			if (!block.is_object()) {
				return;
			}

			auto truncateField = [&block, &changed](const char* fieldName) {
				auto it = block.find(fieldName);
				if (it != block.end() && it->is_string()) {
					bool fieldChanged = false;
					const std::string truncated =
						TruncateText(it->get<std::string>(), fieldChanged);
					if (fieldChanged) {
						*it = truncated;
						changed = true;
					}
				}
				};

			truncateField("text");
			truncateField("partialJson");
			truncateField("arguments");
			truncateField("thinking");

			if (block.contains("thinkingSignature")) {
				block.erase("thinkingSignature");
				changed = true;
			}

			const auto typeIt = block.find("type");
			if (typeIt != block.end() && typeIt->is_string() &&
				typeIt->get<std::string>() == "image") {
				auto dataIt = block.find("data");
				if (dataIt != block.end() && dataIt->is_string()) {
					const auto bytes = dataIt->get<std::string>().size();
					block.erase("data");
					block["omitted"] = true;
					block["bytes"] = bytes;
					changed = true;
				}
			}
		}

		nlohmann::json BuildOversizedPlaceholder(const nlohmann::json& original) {
			std::string role = "assistant";
			if (original.is_object()) {
				const auto roleIt = original.find("role");
				if (roleIt != original.end() && roleIt->is_string()) {
					role = roleIt->get<std::string>();
				}
			}

			std::uint64_t timestamp = static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch())
				.count());
			if (original.is_object()) {
				const auto tsIt = original.find("timestamp");
				if (tsIt != original.end() && tsIt->is_number_unsigned()) {
					timestamp = tsIt->get<std::uint64_t>();
				}
			}

			return nlohmann::json{
				{ "role", role },
				{ "timestamp", timestamp },
				{ "content", nlohmann::json::array({
					nlohmann::json{
						{ "type", "text" },
						{ "text", kOversizedPlaceholder },
					}
				}) },
				{ "__blazeclaw", nlohmann::json{
					{ "truncated", true },
					{ "reason", "oversized" },
				} },
			};
		}

		nlohmann::json SanitizeMessage(
			nlohmann::json message,
			bool& changed) {
			if (!message.is_object()) {
				return message;
			}

			if (message.contains("details")) {
				message.erase("details");
				changed = true;
			}
			if (message.contains("usage")) {
				message.erase("usage");
				changed = true;
			}
			if (message.contains("cost")) {
				message.erase("cost");
				changed = true;
			}

			auto truncateField = [&message, &changed](const char* fieldName) {
				auto it = message.find(fieldName);
				if (it != message.end() && it->is_string()) {
					bool fieldChanged = false;
					const std::string truncated =
						TruncateText(it->get<std::string>(), fieldChanged);
					if (fieldChanged) {
						*it = truncated;
						changed = true;
					}
				}
				};

			truncateField("text");

			auto contentIt = message.find("content");
			if (contentIt != message.end()) {
				if (contentIt->is_string()) {
					bool contentChanged = false;
					const std::string truncated =
						TruncateText(contentIt->get<std::string>(), contentChanged);
					if (contentChanged) {
						*contentIt = truncated;
						changed = true;
					}
				}
				else if (contentIt->is_array()) {
					for (auto& block : *contentIt) {
						SanitizeContentBlock(block, changed);
					}
				}
			}

			return message;
		}
	}

	ChatHistoryPolicy::BuildResult ChatHistoryPolicy::Build(
		const BuildParams& params) const {
		const std::size_t boundedLimit =
			(std::max)(std::size_t{ 1 },
				(std::min)(params.requestedLimit, kMaxEntriesPerSession));
		const std::size_t begin =
			params.history.size() > boundedLimit
			? params.history.size() - boundedLimit
			: 0;

		std::vector<nlohmann::json> messages;
		messages.reserve(params.history.size() - begin);
		std::size_t placeholderCount = 0;
		for (std::size_t i = begin; i < params.history.size(); ++i) {
			try {
				nlohmann::json message = nlohmann::json::parse(params.history[i]);
				if (JsonBytes(message) > kMaxSingleMessageBytes) {
					message = BuildOversizedPlaceholder(message);
					++placeholderCount;
					messages.push_back(std::move(message));
					continue;
				}

				bool changed = false;
				message = SanitizeMessage(std::move(message), changed);
				if (IsSilentAssistantMessage(message)) {
					continue;
				}

				if (JsonBytes(message) > kMaxSingleMessageBytes) {
					message = BuildOversizedPlaceholder(message);
					++placeholderCount;
				}
				messages.push_back(std::move(message));
			}
			catch (...) {
				continue;
			}
		}

		nlohmann::json messageArray = nlohmann::json::array();
		for (auto& message : messages) {
			messageArray.push_back(std::move(message));
		}

		while (!messageArray.empty() && JsonBytes(messageArray) > kMaxHistoryBytes) {
			messageArray.erase(messageArray.begin());
		}

		if (!messageArray.empty() && JsonBytes(messageArray) > kMaxHistoryBytes) {
			nlohmann::json placeholder =
				BuildOversizedPlaceholder(messageArray.back());
			++placeholderCount;
			if (JsonBytes(placeholder) <= kMaxHistoryBytes) {
				messageArray = nlohmann::json::array({ placeholder });
			}
			else {
				messageArray = nlohmann::json::array();
			}
		}

		return BuildResult{
			.messagesJson = messageArray.dump(),
			.placeholderCount = placeholderCount,
		};
	}

} // namespace blazeclaw::gateway
