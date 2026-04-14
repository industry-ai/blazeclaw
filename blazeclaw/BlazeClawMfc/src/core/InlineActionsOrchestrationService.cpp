#include "pch.h"
#include "InlineActionsOrchestrationService.h"

#include <algorithm>
#include <cctype>

namespace blazeclaw::core {

	namespace {

		std::string ToLowerAscii(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		std::string TrimAscii(const std::string& value) {
			auto isSpace = [](const unsigned char ch) {
				return std::isspace(ch) != 0;
				};

			auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[&](const char ch) { return isSpace(static_cast<unsigned char>(ch)); });
			auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[&](const char ch) { return isSpace(static_cast<unsigned char>(ch)); })
				.base();

			if (first >= last) {
				return {};
			}

			return std::string(first, last);
		}

	} // namespace

	std::string InlineActionsOrchestrationService::ResolveSlashCommandName(
		const std::string& commandBodyNormalized) const {
		const std::string trimmed = TrimAscii(commandBodyNormalized);
		if (trimmed.empty() || trimmed.front() != '/') {
			return {};
		}

		const std::size_t split = trimmed.find_first_of(" \t\r\n:", 1);
		const std::string rawName = split == std::string::npos
			? trimmed.substr(1)
			: trimmed.substr(1, split - 1);
		return ToLowerAscii(TrimAscii(rawName));
	}

	std::unordered_set<std::string>
		InlineActionsOrchestrationService::BuildBuiltinSlashCommands(
			const std::unordered_set<std::string>& reservedNames) const {
		std::unordered_set<std::string> builtin = reservedNames;
		builtin.insert("btw");
		builtin.insert("think");
		builtin.insert("verbose");
		builtin.insert("reasoning");
		builtin.insert("elevated");
		builtin.insert("exec");
		builtin.insert("model");
		builtin.insert("status");
		builtin.insert("queue");
		builtin.insert("skill");
		return builtin;
	}

	bool InlineActionsOrchestrationService::ShouldLoadSkillCommandsForSlash(
		const bool allowTextCommands,
		const std::string& slashCommandName,
		const std::unordered_set<std::string>& builtinSlashCommands) const {
		if (!allowTextCommands) {
			return false;
		}

		if (slashCommandName.empty()) {
			return false;
		}

		if (slashCommandName == "skill") {
			return true;
		}

		return builtinSlashCommands.find(slashCommandName) ==
			builtinSlashCommands.end();
	}

} // namespace blazeclaw::core
