#include "pch.h"
#include "SkillsFrontmatterCompat.h"

#include <algorithm>
#include <cwctype>

namespace blazeclaw::core {

	namespace {

		std::wstring TrimCompat(const std::wstring& value) {
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

		std::wstring ToLowerCompat(const std::wstring& value) {
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

		std::wstring TrimQuotesCompat(const std::wstring& value) {
			const std::wstring trimmed = TrimCompat(value);
			if (trimmed.size() >= 2) {
				const wchar_t first = trimmed.front();
				const wchar_t last = trimmed.back();
				if ((first == L'"' && last == L'"') ||
					(first == L'\'' && last == L'\'')) {
					return trimmed.substr(1, trimmed.size() - 2);
				}
			}

			return trimmed;
		}

		bool ParseBoolFieldCompat(const std::wstring& value, const bool fallback) {
			const std::wstring normalized = ToLowerCompat(TrimCompat(value));
			if (normalized == L"true" || normalized == L"1" || normalized == L"yes") {
				return true;
			}

			if (normalized == L"false" || normalized == L"0" || normalized == L"no") {
				return false;
			}

			return fallback;
		}

		std::vector<std::wstring> SplitListCompat(const std::wstring& raw) {
			std::vector<std::wstring> values;
			std::wstring current;
			current.reserve(raw.size());

			const auto flush = [&values, &current]() {
				const auto trimmed = TrimCompat(current);
				if (!trimmed.empty()) {
					values.push_back(trimmed);
				}
				current.clear();
				};

			for (const auto ch : raw) {
				if (ch == L',' || ch == L';' || ch == L'|') {
					flush();
					continue;
				}
				current.push_back(ch);
			}

			flush();
			return values;
		}

	} // namespace

	std::optional<ParsedSkillFrontmatterCompat> ParseSkillFrontmatterCompat(
		const std::wstring& skillContent,
		std::vector<std::wstring>& outValidationErrors) {
		std::vector<std::wstring> lines;
		std::size_t cursor = 0;
		while (cursor <= skillContent.size()) {
			const auto next = skillContent.find(L'\n', cursor);
			if (next == std::wstring::npos) {
				lines.push_back(skillContent.substr(cursor));
				break;
			}

			lines.push_back(skillContent.substr(cursor, next - cursor));
			cursor = next + 1;
		}

		if (lines.empty() || TrimCompat(lines[0]) != L"---") {
			outValidationErrors.push_back(
				L"Missing frontmatter start marker (---).");
			return std::nullopt;
		}

		ParsedSkillFrontmatterCompat parsed;
		std::size_t lineIndex = 1;
		bool closed = false;
		for (; lineIndex < lines.size(); ++lineIndex) {
			const std::wstring line = TrimCompat(lines[lineIndex]);
			if (line == L"---") {
				closed = true;
				++lineIndex;
				break;
			}

			if (line.empty() || line.starts_with(L"#")) {
				continue;
			}

			const auto colonPos = line.find(L':');
			if (colonPos == std::wstring::npos) {
				outValidationErrors.push_back(L"Invalid frontmatter line: " + line);
				continue;
			}

			const std::wstring key =
				ToLowerCompat(TrimCompat(line.substr(0, colonPos)));
			const std::wstring value = TrimQuotesCompat(line.substr(colonPos + 1));
			if (key.empty()) {
				outValidationErrors.push_back(L"Empty frontmatter key detected.");
				continue;
			}

			parsed.fields[key] = value;
			if (key == L"name") {
				parsed.name = value;
			}
			else if (key == L"description") {
				parsed.description = value;
			}
		}

		if (!closed) {
			outValidationErrors.push_back(
				L"Missing frontmatter closing marker (---).");
		}

		if (TrimCompat(parsed.name).empty()) {
			outValidationErrors.push_back(
				L"Missing required frontmatter field: name.");
		}

		if (TrimCompat(parsed.description).empty()) {
			outValidationErrors.push_back(
				L"Missing required frontmatter field: description.");
		}

		if (!outValidationErrors.empty()) {
			return std::nullopt;
		}

		return parsed;
	}

	std::wstring GetSkillFrontmatterFieldCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		std::initializer_list<const wchar_t*> keys) {
		for (const auto* key : keys) {
			const auto it = frontmatter.fields.find(
				ToLowerCompat(TrimCompat(key)));
			if (it != frontmatter.fields.end()) {
				return TrimCompat(it->second);
			}
		}

		return {};
	}

	SkillsMetadataSpec ResolveOpenClawMetadataCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		SkillsMetadataSpec metadata;
		metadata.skillKey = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"skillkey", L"skill-key", L"openclaw.skillkey" });
		metadata.primaryEnv = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"primary-env", L"primary_env", L"openclaw.primary-env" });
		metadata.emoji = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"emoji", L"openclaw.emoji" });
		metadata.homepage = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"homepage", L"openclaw.homepage" });

		const auto alwaysRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"always", L"openclaw.always" });
		if (!alwaysRaw.empty()) {
			metadata.always = ParseBoolFieldCompat(alwaysRaw, false);
		}

		metadata.os = SplitListCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"os", L"openclaw.os" }));

		metadata.requirements.bins = SplitListCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"requires-bins", L"requires_bins", L"requires.bins" }));
		metadata.requirements.anyBins = SplitListCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"requires-any-bins", L"requires_any_bins", L"requires.anybins" }));
		metadata.requirements.env = SplitListCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"requires-env", L"requires_env", L"requires.env" }));
		metadata.requirements.config = SplitListCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"requires-config", L"requires_config", L"requires.config" }));

		const auto installKind = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"install-kind", L"install_kind", L"install.kind" });
		if (!installKind.empty()) {
			SkillInstallSpec install;
			install.kind = installKind;
			install.id = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-id", L"install_id", L"install.id" });
			install.label = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-label", L"install_label", L"install.label" });
			install.formula = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-formula", L"install_formula", L"install.formula" });
			install.package = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-package", L"install_package", L"install.package" });
			install.module = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-module", L"install_module", L"install.module" });
			install.url = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-url", L"install_url", L"install.url" });
			install.archive = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-archive", L"install_archive", L"install.archive" });
			install.targetDir = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-target-dir", L"install_target_dir", L"install.targetDir" });
			install.bins = SplitListCompat(GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-bins", L"install_bins", L"install.bins" }));
			install.os = SplitListCompat(GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-os", L"install_os", L"install.os" }));

			const auto extractRaw = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-extract", L"install_extract", L"install.extract" });
			if (!extractRaw.empty()) {
				install.extract = ParseBoolFieldCompat(extractRaw, false);
			}

			const auto stripRaw = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-strip-components", L"install_strip_components", L"install.stripComponents" });
			if (!stripRaw.empty()) {
				try {
					install.stripComponents = static_cast<std::uint32_t>(
						std::stoul(stripRaw));
				}
				catch (...) {
					install.stripComponents.reset();
				}
			}

			metadata.install.push_back(std::move(install));
		}

		return metadata;
	}

	SkillInvocationPolicySpec ResolveSkillInvocationPolicyCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		SkillInvocationPolicySpec policy;
		const auto userInvocableRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"user-invocable", L"user_invocable", L"openclaw.user-invocable" });
		if (!userInvocableRaw.empty()) {
			policy.userInvocable = ParseBoolFieldCompat(userInvocableRaw, true);
		}

		const auto disableModelInvocationRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"disable-model-invocation", L"disable_model_invocation", L"disablemodelinvocation" });
		if (!disableModelInvocationRaw.empty()) {
			policy.disableModelInvocation = ParseBoolFieldCompat(
				disableModelInvocationRaw,
				false);
		}

		return policy;
	}

	SkillExposureSpec ResolveSkillExposurePolicyCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		const SkillInvocationPolicySpec& invocation) {
		SkillExposureSpec exposure;
		exposure.userInvocable = invocation.userInvocable;

		const auto includeRegistryRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"include-runtime-registry", L"include_runtime_registry", L"exposure.includeInRuntimeRegistry" });
		if (!includeRegistryRaw.empty()) {
			exposure.includeInRuntimeRegistry =
				ParseBoolFieldCompat(includeRegistryRaw, true);
		}

		const auto includePromptRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"include-available-skills-prompt", L"include_available_skills_prompt", L"exposure.includeInAvailableSkillsPrompt" });
		if (!includePromptRaw.empty()) {
			exposure.includeInAvailableSkillsPrompt =
				ParseBoolFieldCompat(includePromptRaw, true);
		}

		return exposure;
	}

} // namespace blazeclaw::core
