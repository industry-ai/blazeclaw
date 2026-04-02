#pragma once

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"

#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillsCommandDispatch {
		bool enabled = false;
		std::wstring kind;
		std::wstring toolName;
		std::wstring argMode;
		std::wstring argSchema;
		std::wstring resultSchema;
		std::wstring idempotencyHint;
		std::wstring retryPolicyHint;
		bool requiresApproval = false;
	};

	struct SkillsCommandSpec {
		std::wstring name;
		std::wstring skillName;
		std::wstring description;
		SkillsCommandDispatch dispatch;
	};

	struct SkillsCommandSnapshot {
		std::vector<SkillsCommandSpec> commands;
	};

	class SkillsCommandService {
	public:
		[[nodiscard]] SkillsCommandSnapshot BuildSnapshot(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility) const;

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError) const;
	};

} // namespace blazeclaw::core
