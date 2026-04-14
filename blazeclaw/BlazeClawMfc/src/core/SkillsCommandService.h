#pragma once

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"
#include "extensions/RuntimeCapabilityAdapterContracts.h"

#include <filesystem>
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
		std::wstring promptTemplate;
		std::wstring sourceFilePath;
	};

	struct SkillsCommandSnapshot {
		std::vector<SkillsCommandSpec> commands;
		std::uint32_t sanitizeCount = 0;
		std::uint32_t dedupeCount = 0;
		std::uint32_t skillNameDedupeCount = 0;
		std::uint32_t missingToolDispatchCount = 0;
		std::uint32_t invalidArgModeFallbackCount = 0;
		std::uint32_t commandSourceContributionCount = 0;
	};

	class SkillsCommandService {
	public:
		[[nodiscard]] SkillsCommandSnapshot BuildSnapshot(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility) const;

		[[nodiscard]] SkillsCommandSnapshot BuildSnapshot(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const std::vector<std::wstring>& reservedNames) const;

		[[nodiscard]] SkillsCommandSnapshot BuildSnapshotWithAdapters(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const std::vector<extensions::IRuntimeSkillCapabilityAdapter*>& adapters) const;

		[[nodiscard]] SkillsCommandSnapshot BuildSnapshotWithAdapters(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const std::vector<extensions::IRuntimeSkillCapabilityAdapter*>& adapters,
			const std::vector<extensions::IRuntimeSkillCommandSourceAdapter*>&
			commandSourceAdapters) const;

		[[nodiscard]] SkillsCommandSnapshot BuildSnapshotWithAdapters(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const std::vector<extensions::IRuntimeSkillCapabilityAdapter*>& adapters,
			const std::vector<std::wstring>& reservedNames) const;

		[[nodiscard]] SkillsCommandSnapshot BuildSnapshotWithAdapters(
			const SkillsCatalogSnapshot& catalog,
			const SkillsEligibilitySnapshot& eligibility,
			const std::vector<extensions::IRuntimeSkillCapabilityAdapter*>& adapters,
			const std::vector<extensions::IRuntimeSkillCommandSourceAdapter*>&
			commandSourceAdapters,
			const std::vector<std::wstring>& reservedNames) const;

		[[nodiscard]] bool ValidateFixtureScenarios(
			const std::filesystem::path& fixturesRoot,
			std::wstring& outError) const;
	};

} // namespace blazeclaw::core
