#include "pch.h"
#include "SkillCommandInvocationService.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <string>

namespace blazeclaw::core {

	namespace {

		std::wstring Trim(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; });
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; })
				.base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::wstring ToLower(const std::wstring& value) {
			std::wstring lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return lowered;
		}

		std::wstring NormalizeSkillCommandLookup(const std::wstring& value) {
			const auto lowered = ToLower(Trim(value));
			std::wstring normalized;
			normalized.reserve(lowered.size());

			bool previousWasDash = false;
			for (const auto ch : lowered) {
				if (std::iswspace(ch) != 0 || ch == L'_') {
					if (!previousWasDash) {
						normalized.push_back(L'-');
						previousWasDash = true;
					}
					continue;
				}

				normalized.push_back(ch);
				previousWasDash = (ch == L'-');
			}

			while (!normalized.empty() && normalized.front() == L'-') {
				normalized.erase(normalized.begin());
			}
			while (!normalized.empty() && normalized.back() == L'-') {
				normalized.pop_back();
			}

			return normalized;
		}

		std::wstring Utf8ToWide(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			const int needed = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0);
			if (needed <= 0) {
				return {};
			}

			std::wstring output(static_cast<std::size_t>(needed), L'\0');
			MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				needed);
			return output;
		}

		std::string WideToNarrowAsciiLossy(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const auto ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}

		std::optional<SkillCommandInvocationResult> FindSkillCommand(
			const std::vector<SkillsCommandSpec>& skillCommands,
			const std::wstring& rawName,
			const std::optional<std::wstring>& args) {
			const auto lowered = ToLower(Trim(rawName));
			if (lowered.empty()) {
				return std::nullopt;
			}

			const auto normalized = NormalizeSkillCommandLookup(rawName);
			for (const auto& entry : skillCommands) {
				if (ToLower(Trim(entry.name)) == lowered ||
					ToLower(Trim(entry.skillName)) == lowered ||
					NormalizeSkillCommandLookup(entry.name) == normalized ||
					NormalizeSkillCommandLookup(entry.skillName) == normalized) {
					return SkillCommandInvocationResult{
						.command = entry,
						.args = args,
					};
				}
			}

			return std::nullopt;
		}

	} // namespace

	std::optional<SkillCommandInvocationResult>
		SkillCommandInvocationService::ResolveInvocation(
			const std::wstring& commandBodyNormalized,
			const std::vector<SkillsCommandSpec>& skillCommands) const {
		const auto trimmed = Trim(commandBodyNormalized);
		if (trimmed.empty() || trimmed.front() != L'/') {
			return std::nullopt;
		}

		auto splitNameArgs = [](const std::wstring& raw) {
			const auto localTrimmed = Trim(raw);
			const auto splitAt = localTrimmed.find_first_of(L" \t\r\n");
			if (splitAt == std::wstring::npos) {
				return std::pair<std::wstring, std::optional<std::wstring>>{
					localTrimmed,
						std::nullopt,
				};
			}

			const auto name = Trim(localTrimmed.substr(0, splitAt));
			const auto remainder = Trim(localTrimmed.substr(splitAt + 1));
			return std::pair<std::wstring, std::optional<std::wstring>>{
				name,
					remainder.empty() ? std::nullopt : std::optional<std::wstring>(remainder),
			};
			};

		const auto withoutSlash = Trim(trimmed.substr(1));
		const auto [commandName, commandArgs] = splitNameArgs(withoutSlash);
		if (commandName.empty()) {
			return std::nullopt;
		}

		if (ToLower(commandName) == L"skill") {
			if (!commandArgs.has_value()) {
				return std::nullopt;
			}

			const auto [skillName, skillArgs] = splitNameArgs(commandArgs.value());
			if (skillName.empty()) {
				return std::nullopt;
			}

			return FindSkillCommand(skillCommands, skillName, skillArgs);
		}

		for (const auto& entry : skillCommands) {
			if (ToLower(Trim(entry.name)) == ToLower(commandName)) {
				return SkillCommandInvocationResult{
					.command = entry,
					.args = commandArgs,
				};
			}
		}

		return std::nullopt;
	}

	std::optional<std::string> SkillCommandInvocationService::RewriteInvocationPromptUtf8(
		const std::string& commandBodyNormalizedUtf8,
		const std::vector<SkillsCommandSpec>& skillCommands) const {
		const auto resolvedSkillInvocation = ResolveInvocation(
			Utf8ToWide(commandBodyNormalizedUtf8),
			skillCommands);
		if (!resolvedSkillInvocation.has_value()) {
			return std::nullopt;
		}

		const auto& command = resolvedSkillInvocation->command;
		if (command.dispatch.enabled &&
			_wcsicmp(command.dispatch.kind.c_str(), L"tool") == 0) {
			return std::nullopt;
		}

		const std::string skillName = WideToNarrowAsciiLossy(command.skillName);
		const std::string args = resolvedSkillInvocation->args.has_value()
			? WideToNarrowAsciiLossy(Trim(resolvedSkillInvocation->args.value()))
			: std::string();

		std::string rewrittenMessage;
		const std::wstring promptTemplateWide = Trim(command.promptTemplate);
		if (!promptTemplateWide.empty()) {
			rewrittenMessage = WideToNarrowAsciiLossy(promptTemplateWide);
			const std::string placeholder = "{{args}}";
			const std::size_t placeholderPos = rewrittenMessage.find(placeholder);
			if (placeholderPos != std::string::npos) {
				rewrittenMessage.replace(
					placeholderPos,
					placeholder.size(),
					args);
			}
		}
		else {
			rewrittenMessage =
				"Use the \"" + skillName + "\" skill for this request.";
			if (!args.empty()) {
				rewrittenMessage += "\n\nUser input:\n" + args;
			}
		}

		const std::string normalized =
			WideToNarrowAsciiLossy(Trim(Utf8ToWide(rewrittenMessage)));
		if (normalized.empty()) {
			return std::nullopt;
		}

		return normalized;
	}

} // namespace blazeclaw::core
