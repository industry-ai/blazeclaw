#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core {

	struct SkillInstallSpec {
		std::wstring id;
		std::wstring kind;
		std::wstring label;
		std::vector<std::wstring> bins;
		std::vector<std::wstring> os;
		std::wstring formula;
		std::wstring package;
		std::wstring module;
		std::wstring url;
		std::wstring archive;
		std::optional<bool> extract;
		std::optional<std::uint32_t> stripComponents;
		std::wstring targetDir;
	};

	struct SkillRequiresSpec {
		std::vector<std::wstring> bins;
		std::vector<std::wstring> anyBins;
		std::vector<std::wstring> env;
		std::vector<std::wstring> config;
	};

	struct SkillsMetadataSpec {
		std::optional<bool> always;
		std::wstring skillKey;
		std::wstring primaryEnv;
		std::wstring emoji;
		std::wstring homepage;
		std::vector<std::wstring> os;
		SkillRequiresSpec requirements;
		std::vector<SkillInstallSpec> install;
	};

	struct SkillInvocationPolicySpec {
		bool userInvocable = true;
		bool disableModelInvocation = false;
	};

	struct SkillExposureSpec {
		bool includeInRuntimeRegistry = true;
		bool includeInAvailableSkillsPrompt = true;
		bool userInvocable = true;
	};

	struct SkillRunView {
		std::wstring name;
		std::wstring primaryEnv;
		std::vector<std::wstring> requiredEnv;
	};

	struct SkillSnapshotSpec {
		std::wstring prompt;
		std::vector<SkillRunView> skills;
		std::optional<std::vector<std::wstring>> skillFilter;
		std::vector<std::wstring> resolvedSkills;
		std::uint64_t version = 0;
	};

	struct SkillsRemoteEligibilityContract {
		std::vector<std::wstring> platforms;
		std::function<bool(const std::wstring&)> hasBin;
		std::function<bool(const std::vector<std::wstring>&)> hasAnyBin;
		std::wstring note;
	};

} // namespace blazeclaw::core
