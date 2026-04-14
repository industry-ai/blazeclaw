#pragma once

#include "SkillsContracts.h"

#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct ParsedSkillFrontmatterCompat {
		std::wstring name;
		std::wstring description;
		std::map<std::wstring, std::wstring> fields;
	};

	[[nodiscard]] std::optional<ParsedSkillFrontmatterCompat>
		ParseSkillFrontmatterCompat(
			const std::wstring& skillContent,
			std::vector<std::wstring>& outValidationErrors);

	[[nodiscard]] std::wstring GetSkillFrontmatterFieldCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		std::initializer_list<const wchar_t*> keys);

	[[nodiscard]] SkillsMetadataSpec ResolveOpenClawMetadataCompat(
		const ParsedSkillFrontmatterCompat& frontmatter);

	[[nodiscard]] SkillInvocationPolicySpec ResolveSkillInvocationPolicyCompat(
		const ParsedSkillFrontmatterCompat& frontmatter);

	[[nodiscard]] SkillExposureSpec ResolveSkillExposurePolicyCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		const SkillInvocationPolicySpec& invocation);

	[[nodiscard]] std::wstring ResolveSkillInstallKindCompat(
		const ParsedSkillFrontmatterCompat& frontmatter);

	[[nodiscard]] std::wstring ResolveSkillInstallLabelCompat(
		const ParsedSkillFrontmatterCompat& frontmatter);

	[[nodiscard]] std::wstring ResolveSkillInstallFormulaCompat(
		const ParsedSkillFrontmatterCompat& frontmatter);

	[[nodiscard]] std::wstring ResolveSkillInstallPackageCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		const std::wstring& installKind);

	[[nodiscard]] std::wstring ResolveSkillInstallModuleCompat(
		const ParsedSkillFrontmatterCompat& frontmatter);

	[[nodiscard]] std::wstring ResolveSkillInstallUrlCompat(
		const ParsedSkillFrontmatterCompat& frontmatter);

	[[nodiscard]] bool ResolveSkillInstallPreferBrewCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		bool fallback);

	[[nodiscard]] std::wstring ResolveSkillInstallNodeManagerCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		const std::wstring& fallback);

	[[nodiscard]] std::wstring ResolveSkillKeyCompat(
		const SkillsMetadataSpec* metadata,
		const std::wstring& skillName);

} // namespace blazeclaw::core
