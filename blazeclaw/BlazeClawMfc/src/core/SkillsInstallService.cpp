#include "pch.h"
#include "SkillsInstallService.h"
#include "SkillsFacade.h"
#include "SkillsFrontmatterCompat.h"

#include <algorithm>
#include <cwctype>
#include <unordered_map>

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

		const SkillInstallSpec* ResolveInstallSpec(const SkillsCatalogEntry& entry) {
			if (entry.metadata.has_value() && !entry.metadata->install.empty()) {
				return &entry.metadata->install.front();
			}

			return nullptr;
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
			if (normalized == L"true" || normalized == L"1" || normalized == L"yes") {
				return true;
			}

			if (normalized == L"false" || normalized == L"0" || normalized == L"no") {
				return false;
			}

			return fallback;
		}

		std::wstring ResolveNodeInstallCommand(
			const std::wstring& packageName,
			const std::wstring& nodeManager) {
			const std::wstring manager = ToLower(Trim(nodeManager));
			if (manager == L"pnpm") {
				return L"pnpm add -g --ignore-scripts " + packageName;
			}

			if (manager == L"yarn") {
				return L"yarn global add --ignore-scripts " + packageName;
			}

			if (manager == L"bun") {
				return L"bun add -g --ignore-scripts " + packageName;
			}

			return L"npm install -g --ignore-scripts " + packageName;
		}

	} // namespace

	SkillsInstallSnapshot SkillsInstallService::BuildSnapshot(
		const SkillsCatalogSnapshot& catalog,
		const SkillsEligibilitySnapshot& eligibility,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsFacade facade;
		return BuildSnapshot(
			catalog,
			eligibility,
			appConfig,
			facade.ResolveInstallPreferences(appConfig));
	}

	SkillsInstallSnapshot SkillsInstallService::BuildSnapshot(
		const SkillsCatalogSnapshot& catalog,
		const SkillsEligibilitySnapshot& eligibility,
		const blazeclaw::config::AppConfig& appConfig,
		const SkillsInstallPreferences& installPreferences) const {
		SkillsInstallSnapshot snapshot;

		std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
		for (const auto& item : eligibility.entries) {
			eligibilityByName.emplace(ToLower(item.skillName), item);
		}

		for (const auto& entry : catalog.entries) {
			const auto eligibilityIt = eligibilityByName.find(ToLower(entry.skillName));
			if (eligibilityIt == eligibilityByName.end() || !eligibilityIt->second.eligible) {
				continue;
			}

			SkillsInstallPlanEntry plan;
			plan.skillName = entry.skillName;
			const ParsedSkillFrontmatterCompat frontmatterCompat{
				.name = entry.frontmatter.name,
				.description = entry.frontmatter.description,
				.fields = entry.frontmatter.fields,
			};
			const SkillInstallSpec* installSpec = ResolveInstallSpec(entry);

			plan.kind = installSpec == nullptr
				? ResolveSkillInstallKindCompat(frontmatterCompat)
				: ToLower(Trim(installSpec->kind));
			if (plan.kind.empty()) {
				continue;
			}

			plan.label = installSpec == nullptr
				? ResolveSkillInstallLabelCompat(frontmatterCompat)
				: Trim(installSpec->label);

			const std::wstring formula = installSpec == nullptr
				? ResolveSkillInstallFormulaCompat(frontmatterCompat)
				: Trim(installSpec->formula);
			const std::wstring packageName = installSpec == nullptr
				? ResolveSkillInstallPackageCompat(frontmatterCompat, plan.kind)
				: Trim(installSpec->package);
			const std::wstring moduleName = installSpec == nullptr
				? ResolveSkillInstallModuleCompat(frontmatterCompat)
				: Trim(installSpec->module);
			const std::wstring downloadUrl = installSpec == nullptr
				? ResolveSkillInstallUrlCompat(frontmatterCompat)
				: Trim(installSpec->url);

			const bool installPreferBrew = ResolveSkillInstallPreferBrewCompat(
				frontmatterCompat,
				installPreferences.preferBrew);

			const std::wstring installNodeManager = [&]() {
				return ResolveSkillInstallNodeManagerCompat(
					frontmatterCompat,
					installPreferences.nodeManager);
				}();

			const bool hasBrewFormula = !formula.empty();

			if (plan.kind == L"node" && installPreferBrew && hasBrewFormula) {
				plan.kind = L"brew";
			}

			if (plan.kind == L"go" && installPreferBrew && hasBrewFormula) {
				plan.kind = L"brew";
			}

			if (plan.kind == L"uv" && installPreferBrew && hasBrewFormula) {
				plan.kind = L"brew";
			}

			if (plan.kind == L"brew") {
				if (formula.empty()) {
					plan.executable = false;
					plan.reason = L"missing-formula";
				}
				else {
					plan.executable = true;
					plan.command = L"brew install " + formula;
				}
			}
			else if (plan.kind == L"node") {
				if (packageName.empty()) {
					plan.executable = false;
					plan.reason = L"missing-package";
				}
				else {
					plan.executable = true;
					plan.command = ResolveNodeInstallCommand(
						packageName,
						installNodeManager);
				}
			}
			else if (plan.kind == L"go") {
				if (moduleName.empty()) {
					plan.executable = false;
					plan.reason = L"missing-module";
				}
				else {
					plan.executable = true;
					plan.command = L"go install " + moduleName;
				}
			}
			else if (plan.kind == L"uv") {
				if (packageName.empty()) {
					plan.executable = false;
					plan.reason = L"missing-package";
				}
				else {
					plan.executable = true;
					plan.command = L"uv tool install " + packageName;
				}
			}
			else if (plan.kind == L"download") {
				if (downloadUrl.empty()) {
					plan.executable = false;
					plan.reason = L"missing-url";
				}
				else {
					plan.executable = true;
					plan.command = L"download " + downloadUrl;
				}
			}
			else {
				plan.executable = false;
				plan.reason = L"unsupported-kind";
			}

			if (plan.executable) {
				++snapshot.executableCount;
				if (plan.label.empty()) {
					plan.label = L"Install " + plan.skillName;
				}
			}
			else {
				++snapshot.blockedCount;
				if (plan.label.empty()) {
					plan.label = L"Installer unavailable";
				}
			}

			snapshot.entries.push_back(std::move(plan));
		}

		return snapshot;
	}

	bool SkillsInstallService::ValidateFixtureScenarios(
		const std::filesystem::path& fixturesRoot,
		std::wstring& outError) const {
		outError.clear();

		const auto root = fixturesRoot / L"s5-install" / L"workspace";
		blazeclaw::config::AppConfig appConfig;
		appConfig.skills.install.nodeManager = L"pnpm";
		appConfig.skills.install.preferBrew = true;

		const SkillsCatalogService catalogService;
		const SkillsEligibilityService eligibilityService;

		const auto catalog = catalogService.LoadCatalog(root, appConfig);
		const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);
		const auto snapshot = BuildSnapshot(catalog, eligibility, appConfig);

		const auto hasNodePlan = std::any_of(
			snapshot.entries.begin(),
			snapshot.entries.end(),
			[](const SkillsInstallPlanEntry& item) {
				return item.kind == L"node" && item.executable &&
					item.command.find(L"pnpm add -g") != std::wstring::npos;
			});
		if (!hasNodePlan) {
			outError = L"S5 install fixture failed: expected executable pnpm node install plan.";
			return false;
		}

		const auto hasBlocked = std::any_of(
			snapshot.entries.begin(),
			snapshot.entries.end(),
			[](const SkillsInstallPlanEntry& item) {
				return item.kind == L"download" && !item.executable;
			});
		if (!hasBlocked) {
			outError = L"S5 install fixture failed: expected blocked install plan entry.";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core
