#pragma once

#include "../SkillsCatalogService.h"
#include "../SkillsCommandService.h"
#include "../SkillsEligibilityService.h"
#include "../SkillsInstallService.h"
#include "../SkillsContracts.h"

#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core::skills {

	/// Domain-level canonical representation of a skill used inside the core services.
	/// - Immutable-ish value object: constructed from catalog/eligibility/command/install snapshots.
	/// - Mapping to/from gateway DTOs (e.g., `SkillsCatalogGatewayEntry`, `ToolCatalogEntry`)
	///   should be implemented in separate mapper functions (`CSkillMappers`).
	class CSkill {
	public:
		// Identity / source
		std::wstring skillName;                  // human-facing name (from frontmatter or derived)
		std::string  skillKey;                   // normalized key (ASCII-friendly, stable id)
		SkillsSourceKind sourceKind = SkillsSourceKind::Extra;
		int precedence = 0;

		// Location
		std::filesystem::path skillDir;
		std::filesystem::path skillFile;

		// Content / metadata
		std::wstring description;
		bool validFrontmatter = false;
		std::optional<SkillsMetadataSpec> metadata;    // normalized metadata (requirements, install, etc.)
		std::optional<SkillInvocationPolicySpec> invocation;
		std::optional<SkillExposureSpec> exposure;

		// Policy / eligibility/runtime hints
		bool eligible = false;
		bool disabled = false;
		bool blockedByAllowlist = false;
		bool disableModelInvocation = false;
		bool userInvocable = true;
		std::vector<std::wstring> missingOs;
		std::vector<std::wstring> missingBins;
		std::vector<std::wstring> missingAnyBins;
		std::vector<std::wstring> missingEnv;
		std::vector<std::wstring> missingConfig;

		// Install & command facets (optional)
		std::optional<SkillsCommandSpec> commandSpec;
		std::optional<SkillsInstallPlanEntry> installPlan;

		// OpenClaw compatibility / import diagnostics
		std::optional<OpenClawOriginalExtractedRuntimeContractSpec> openClawExtractedRuntimeContract;
		std::optional<SkillsOpenClawOriginalActivationState> openClawActivationState;
		std::wstring openClawOrigin;
		std::vector<std::wstring> openClawImportDiagnostics;
		bool openClawMetadataConvertedFromClawdbot = false;

		// Diagnostics / validation
		std::vector<std::wstring> validationErrors;

	public:
		CSkill() = default;

		// Stable construction helper: populate a CSkill from catalog/eligibility/command/install snapshots.
		// Implementation lives in `CSkillMappers` (keeps mapping logic centralized and testable).
		static CSkill FromCatalogAndRelated(
			const SkillsCatalogEntry& catalogEntry,
			const SkillsEligibilityEntry* eligibility = nullptr,
			const SkillsCommandSpec* command = nullptr,
			const SkillsInstallPlanEntry* install = nullptr);

		// Lightweight helpers useful for projection/registration consumers.
		std::string ToAsciiSkillKey() const;
		std::string ToNarrowName() const;
	};

} // namespace blazeclaw::core::skills