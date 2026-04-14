#include "pch.h"
#include "SkillCommandInvocationService.h"

#include <algorithm>
#include <cwctype>

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

} // namespace blazeclaw::core
