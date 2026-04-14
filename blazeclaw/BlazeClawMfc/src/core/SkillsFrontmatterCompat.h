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

	[[nodiscard]] std::wstring ResolveSkillKeyCompat(
		const SkillsMetadataSpec* metadata,
		const std::wstring& skillName);

} // namespace blazeclaw::core
