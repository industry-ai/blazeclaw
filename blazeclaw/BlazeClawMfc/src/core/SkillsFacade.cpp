#include "pch.h"
#include "SkillsFacade.h"

#include <algorithm>
#include <cwctype>
#include <set>
#include <unordered_map>

namespace blazeclaw::core {

	namespace {

		std::wstring Trim(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				}).base();

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

		bool ParseBoolField(const std::wstring& value, const bool fallback) {
			const std::wstring normalized = ToLower(Trim(value));
			if (normalized == L"true" ||
				normalized == L"1" ||
				normalized == L"yes") {
				return true;
			}

			if (normalized == L"false" ||
				normalized == L"0" ||
				normalized == L"no") {
				return false;
			}

			return fallback;
		}

		std::wstring GetFrontmatterField(
			const SkillFrontmatter& frontmatter,
			std::initializer_list<const wchar_t*> keys) {
			for (const auto* key : keys) {
				const auto it = frontmatter.fields.find(ToLower(key));
				if (it != frontmatter.fields.end()) {
					return Trim(it->second);
				}
			}

			return {};
		}

		std::vector<std::wstring> ParseListField(const std::wstring& raw) {
			std::vector<std::wstring> values;
			std::wstring current;
			current.reserve(raw.size());

			const auto flush = [&values, &current]() {
				const std::wstring trimmed = Trim(current);
				if (!trimmed.empty()) {
					values.push_back(trimmed);
				}
				current.clear();
				};

			for (const wchar_t ch : raw) {
				if (ch == L',' || ch == L';' || ch == L'|') {
					flush();
					continue;
				}
				current.push_back(ch);
			}
			flush();

			std::sort(
				values.begin(),
				values.end(),
				[](const std::wstring& lhs, const std::wstring& rhs) {
					return ToLower(lhs) < ToLower(rhs);
				});
			values.erase(
				std::unique(
					values.begin(),
					values.end(),
					[](const std::wstring& lhs, const std::wstring& rhs) {
						return ToLower(lhs) == ToLower(rhs);
					}),
				values.end());

			return values;
		}

	} // namespace

	SkillsInstallPreferences SkillsFacade::ResolveInstallPreferences(
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsInstallPreferences resolved;
		resolved.preferBrew = appConfig.skills.install.preferBrew;

		const std::wstring manager = ToLower(Trim(appConfig.skills.install.nodeManager));
		if (manager == L"pnpm" ||
			manager == L"yarn" ||
			manager == L"bun" ||
			manager == L"npm") {
			resolved.nodeManager = manager;
		}
		else {
			resolved.nodeManager = L"npm";
		}

		return resolved;
	}

	SkillsRunSnapshot SkillsFacade::BuildRunSnapshot(
		const SkillsCatalogSnapshot& catalog,
		const SkillsEligibilitySnapshot& eligibility,
		const SkillsPromptSnapshot& prompt,
		const SkillsWatchSnapshot& watch,
		const std::optional<std::vector<std::wstring>>& skillFilter) const {
		SkillsRunSnapshot snapshot;
		snapshot.prompt = prompt.prompt;
		snapshot.version = watch.version;
		if (skillFilter.has_value()) {
			snapshot.skillFilter = SkillsPromptService::NormalizeFilter(skillFilter);
		}

		std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
		for (const auto& item : eligibility.entries) {
			eligibilityByName.emplace(ToLower(Trim(item.skillName)), item);
		}

		std::set<std::wstring> includedPromptSkills;
		for (const auto& name : prompt.includedSkills) {
			includedPromptSkills.insert(ToLower(Trim(name)));
		}

		for (const auto& entry : catalog.entries) {
			const std::wstring key = ToLower(Trim(entry.skillName));
			const auto eligibilityIt = eligibilityByName.find(key);
			if (eligibilityIt == eligibilityByName.end() || !eligibilityIt->second.eligible) {
				continue;
			}

			snapshot.resolvedSkills.push_back(entry.skillName);

			if (includedPromptSkills.find(key) == includedPromptSkills.end()) {
				continue;
			}

			SkillsRunSnapshotSkill runSkill;
			runSkill.name = entry.skillName;
			runSkill.primaryEnv = GetFrontmatterField(
				entry.frontmatter,
				{ L"metadata.openclaw.primaryenv",
				  L"metadata.openclaw.primary_env",
				  L"metadata.blazeclaw.primaryenv",
				  L"metadata.blazeclaw.primary_env" });
			const std::wstring requiredEnvRaw = GetFrontmatterField(
				entry.frontmatter,
				{ L"metadata.openclaw.requires.env",
				  L"metadata.blazeclaw.requires.env" });
			runSkill.requiredEnv = ParseListField(requiredEnvRaw);
			snapshot.skills.push_back(std::move(runSkill));
		}

		return snapshot;
	}

	std::wstring SkillsFacade::ResolvePromptForRun(
		const SkillsRunSnapshot* runSnapshot,
		const SkillsPromptSnapshot* promptSnapshot,
		const SkillsCatalogSnapshot& catalog,
		const SkillsEligibilitySnapshot& eligibility,
		const blazeclaw::config::AppConfig& appConfig,
		const std::optional<std::vector<std::wstring>>& skillFilter,
		const bool enableSelfEvolvingPromptFallback,
		const SkillsPromptService& promptService) const {
		if (runSnapshot != nullptr) {
			const std::wstring snapshotPrompt = Trim(runSnapshot->prompt);
			if (!snapshotPrompt.empty()) {
				return snapshotPrompt;
			}
		}

		if (promptSnapshot != nullptr) {
			const std::wstring prompt = Trim(promptSnapshot->prompt);
			if (!prompt.empty()) {
				return prompt;
			}
		}

		const auto rebuilt = promptService.BuildSnapshot(
			catalog,
			eligibility,
			appConfig,
			skillFilter,
			enableSelfEvolvingPromptFallback);
		return Trim(rebuilt.prompt);
	}

	bool SkillsFacade::ValidateFixtureScenarios(
		const std::filesystem::path& fixturesRoot,
		std::wstring& outError,
		const SkillsPromptService& promptService) const {
		outError.clear();

		blazeclaw::config::AppConfig appConfig;
		appConfig.skills.install.nodeManager = L"unexpected-manager";
		const auto prefs = ResolveInstallPreferences(appConfig);
		if (prefs.nodeManager != L"npm") {
			outError = L"SkillsFacade fixture failed: expected invalid node manager fallback to npm.";
			return false;
		}

		const SkillsPromptSnapshot promptSnapshot{
			.prompt = L"resolved prompt from prompt snapshot",
			.includedSkills = {},
			.plannerContext = {},
			.filter = std::nullopt,
			.truncated = false,
			.totalEligible = 0,
			.includedCount = 0,
			.promptChars = 0,
		};
		const SkillsRunSnapshot runSnapshot{
			.prompt = L"resolved prompt from run snapshot",
			.skills = {},
			.skillFilter = std::nullopt,
			.resolvedSkills = {},
			.version = 1,
		};

		SkillsCatalogSnapshot emptyCatalog;
		emptyCatalog.entries.clear();
		SkillsEligibilitySnapshot emptyEligibility;
		emptyEligibility.entries.clear();

		const std::wstring fromRunSnapshot = ResolvePromptForRun(
			&runSnapshot,
			&promptSnapshot,
			emptyCatalog,
			emptyEligibility,
			appConfig,
			std::nullopt,
			false,
			promptService);
		if (fromRunSnapshot != L"resolved prompt from run snapshot") {
			outError = L"SkillsFacade fixture failed: expected run snapshot prompt reuse.";
			return false;
		}

		const SkillsRunSnapshot emptyRunSnapshot;
		const std::wstring fromPromptSnapshot = ResolvePromptForRun(
			&emptyRunSnapshot,
			&promptSnapshot,
			emptyCatalog,
			emptyEligibility,
			appConfig,
			std::nullopt,
			false,
			promptService);
		if (fromPromptSnapshot != L"resolved prompt from prompt snapshot") {
			outError = L"SkillsFacade fixture failed: expected fallback to prompt snapshot.";
			return false;
		}

		const SkillsCatalogService catalogService;
		const SkillsEligibilityService eligibilityService;
		const auto promptRoot = fixturesRoot / L"s2-prompt" / L"workspace";
		blazeclaw::config::AppConfig promptConfig;
		promptConfig.skills.limits.maxSkillsInPrompt = 1;
		promptConfig.skills.limits.maxSkillsPromptChars = 150;
		const auto promptCatalog = catalogService.LoadCatalog(promptRoot, promptConfig);
		const auto promptEligibility = eligibilityService.Evaluate(promptCatalog, promptConfig);

		const std::wstring rebuiltPrompt = ResolvePromptForRun(
			nullptr,
			nullptr,
			promptCatalog,
			promptEligibility,
			promptConfig,
			std::nullopt,
			false,
			promptService);
		if (rebuiltPrompt.empty()) {
			outError = L"SkillsFacade fixture failed: expected resolver rebuild fallback prompt.";
			return false;
		}

		return true;
	}

	SkillsRefreshResult SkillsFacade::RefreshSkillsState(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& appConfig,
		const bool forceRefresh,
		const std::wstring& reason,
		const bool enableSelfEvolvingPromptFallback,
		SkillsCatalogService& catalogService,
		SkillsEligibilityService& eligibilityService,
		SkillsPromptService& promptService,
		SkillsCommandService& commandService,
		SkillsSyncService& syncService,
		SkillsEnvOverrideService& envOverrideService,
		SkillsInstallService& installService,
		SkillSecurityScanService& securityScanService,
		SkillsWatchService& watchService) const {
		SkillsRefreshResult result;

		result.catalog = catalogService.LoadCatalog(workspaceRoot, appConfig);
		result.eligibility = eligibilityService.Evaluate(result.catalog, appConfig);
		result.prompt = promptService.BuildSnapshot(
			result.catalog,
			result.eligibility,
			appConfig,
			std::nullopt,
			enableSelfEvolvingPromptFallback);
		result.commands = commandService.BuildSnapshot(
			result.catalog,
			result.eligibility);
		result.sync = syncService.SyncToSandbox(
			workspaceRoot,
			result.catalog,
			result.eligibility,
			appConfig);
		result.envOverrides = envOverrideService.BuildSnapshot(
			result.catalog,
			result.eligibility,
			appConfig);
		result.install = installService.BuildSnapshot(
			result.catalog,
			result.eligibility,
			appConfig,
			ResolveInstallPreferences(appConfig));
		result.securityScan = securityScanService.BuildSnapshot(
			result.catalog,
			result.eligibility,
			appConfig);

		envOverrideService.Apply(result.envOverrides);

		result.watch = watchService.Observe(
			result.catalog,
			appConfig,
			forceRefresh,
			reason);
		result.runSnapshot = BuildRunSnapshot(
			result.catalog,
			result.eligibility,
			result.prompt,
			result.watch,
			std::nullopt);

		return result;
	}

} // namespace blazeclaw::core
