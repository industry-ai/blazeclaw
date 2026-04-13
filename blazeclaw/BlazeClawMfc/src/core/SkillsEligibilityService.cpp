#include "pch.h"
#include "SkillsEligibilityService.h"
#include "SkillsConfigEval.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>

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

		std::wstring NormalizeSkillKeyForConfigPath(const std::wstring& skillKey) {
			std::wstring normalized;
			normalized.reserve(skillKey.size());
			for (const wchar_t ch : skillKey) {
				const wchar_t lowered = static_cast<wchar_t>(std::towlower(ch));
				if (std::iswalnum(lowered) != 0 || lowered == L'_') {
					normalized.push_back(lowered);
					continue;
				}

				if (lowered == L'-' || lowered == L'.' || lowered == L'/' || lowered == L'\\') {
					normalized.push_back(L'-');
				}
			}

			while (!normalized.empty() && normalized.front() == L'-') {
				normalized.erase(normalized.begin());
			}
			while (!normalized.empty() && normalized.back() == L'-') {
				normalized.pop_back();
			}

			if (normalized.empty()) {
				return L"unknown-skill";
			}

			return normalized;
		}

		std::filesystem::path ResolveSkillConfigRoot() {
			wchar_t profilePath[MAX_PATH]{};
			const DWORD chars = GetEnvironmentVariableW(
				L"USERPROFILE",
				profilePath,
				MAX_PATH);
			if (chars > 0 && chars < MAX_PATH) {
				return std::filesystem::path(profilePath) / L".config";
			}

			std::error_code ec;
			const auto cwd = std::filesystem::current_path(ec);
			if (ec) {
				return {};
			}

			return cwd / L".config";
		}

		std::vector<std::filesystem::path> BuildSkillConfigCandidates(
			const std::wstring& skillKey,
			const std::wstring& skillName) {
			std::vector<std::filesystem::path> candidates;

			const auto addFor = [&candidates](const std::wstring& rawKey) {
				const auto normalized = NormalizeSkillKeyForConfigPath(rawKey);
				if (normalized.empty()) {
					return;
				}

				const auto root = ResolveSkillConfigRoot();
				if (!root.empty()) {
					candidates.push_back(root / normalized / L".env");
				}

				std::error_code ec;
				const auto cwd = std::filesystem::current_path(ec);
				if (!ec) {
					candidates.push_back(cwd / L"blazeclaw" / L"skills" / normalized / L".env");
					candidates.push_back(cwd / L"skills" / normalized / L".env");
				}
				};

			addFor(skillKey);
			if (ToLower(Trim(skillName)) != ToLower(Trim(skillKey))) {
				addFor(skillName);
			}

			return candidates;
		}

		std::map<std::wstring, std::wstring> ParseDotEnvFile(
			const std::filesystem::path& path) {
			std::map<std::wstring, std::wstring> values;
			std::error_code ec;
			if (!std::filesystem::exists(path, ec) || ec) {
				return values;
			}

			std::wifstream input(path);
			if (!input.is_open()) {
				return values;
			}

			std::wstring line;
			while (std::getline(input, line)) {
				const std::wstring trimmed = Trim(line);
				if (trimmed.empty() || trimmed.starts_with(L"#")) {
					continue;
				}

				const auto eqPos = trimmed.find(L'=');
				if (eqPos == std::wstring::npos || eqPos == 0) {
					continue;
				}

				const std::wstring key = ToLower(Trim(trimmed.substr(0, eqPos)));
				std::wstring value = Trim(trimmed.substr(eqPos + 1));
				if (key.empty() || value.empty()) {
					continue;
				}

				if ((value.size() >= 2 && value.front() == L'"' && value.back() == L'"') ||
					(value.size() >= 2 && value.front() == L'\'' && value.back() == L'\'')) {
					value = value.substr(1, value.size() - 2);
				}

				values.insert_or_assign(key, value);
			}

			return values;
		}

		std::map<std::wstring, std::wstring> LoadPersistedSkillEnv(
			const std::wstring& skillKey,
			const std::wstring& skillName) {
			for (const auto& candidate : BuildSkillConfigCandidates(skillKey, skillName)) {
				const auto parsed = ParseDotEnvFile(candidate);
				if (!parsed.empty()) {
					return parsed;
				}
			}

			return {};
		}

		std::vector<std::wstring> SplitList(const std::wstring& raw) {
			std::vector<std::wstring> values;
			std::wstring current;
			current.reserve(raw.size());

			const auto flush = [&values, &current]() {
				const std::wstring trimmed = Trim(current);
				if (!trimmed.empty()) {
					values.push_back(ToLower(trimmed));
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
			std::sort(values.begin(), values.end());
			values.erase(std::unique(values.begin(), values.end()), values.end());
			return values;
		}

		std::wstring GetFrontmatterField(
			const SkillFrontmatter& frontmatter,
			std::initializer_list<const wchar_t*> keys) {
			for (const auto* key : keys) {
				const auto it = frontmatter.fields.find(ToLower(key));
				if (it != frontmatter.fields.end()) {
					return it->second;
				}
			}

			return {};
		}

		bool HasEnvironmentValue(const std::wstring& envName) {
			wchar_t* value = nullptr;
			std::size_t valueLength = 0;
			const errno_t status = _wdupenv_s(
				&value,
				&valueLength,
				envName.c_str());
			if (status != 0 || value == nullptr || valueLength == 0) {
				if (value != nullptr) {
					free(value);
				}
				return false;
			}

			const bool hasValue = value[0] != L'\0';
			free(value);
			return hasValue;
		}

		bool IsCurrentPlatformAllowed(const std::vector<std::wstring>& osList) {
			if (osList.empty()) {
				return true;
			}

			constexpr wchar_t kPlatformA[] = L"win32";
			constexpr wchar_t kPlatformB[] = L"windows";
			return std::find(osList.begin(), osList.end(), kPlatformA) != osList.end() ||
				std::find(osList.begin(), osList.end(), kPlatformB) != osList.end();
		}

		bool IsBundledAllowed(
			const SkillsCatalogEntry& catalogEntry,
			const SkillsEligibilityEntry& eligibility,
			const blazeclaw::config::AppConfig& appConfig) {
			if (catalogEntry.sourceKind != SkillsSourceKind::Bundled) {
				return true;
			}

			if (appConfig.skills.allowBundled.empty()) {
				return true;
			}

			const std::wstring normalizedKey = ToLower(eligibility.skillKey);
			const std::wstring normalizedName = ToLower(catalogEntry.skillName);
			for (const auto& item : appConfig.skills.allowBundled) {
				const std::wstring normalized = ToLower(Trim(item));
				if (normalized == normalizedKey || normalized == normalizedName) {
					return true;
				}
			}

			return false;
		}

		const blazeclaw::config::SkillEntryConfig* ResolveSkillConfig(
			const blazeclaw::config::AppConfig& appConfig,
			const std::wstring& skillKey,
			const std::wstring& skillName) {
			const auto keyIt = appConfig.skills.entries.find(skillKey);
			if (keyIt != appConfig.skills.entries.end()) {
				return &keyIt->second;
			}

			const auto nameIt = appConfig.skills.entries.find(skillName);
			if (nameIt != appConfig.skills.entries.end()) {
				return &nameIt->second;
			}

			return nullptr;
		}

	} // namespace

	std::wstring SkillsEligibilityService::ResolveSkillKey(
		const SkillsCatalogEntry& entry) {
		const auto direct = GetFrontmatterField(
			entry.frontmatter,
			{ L"skillkey", L"skill-key", L"openclaw.skillkey" });
		if (!Trim(direct).empty()) {
			return Trim(direct);
		}

		return entry.skillName;
	}

	SkillsEligibilitySnapshot SkillsEligibilityService::Evaluate(
		const SkillsCatalogSnapshot& catalog,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsEligibilitySnapshot snapshot;
		snapshot.entries.reserve(catalog.entries.size());

		for (const auto& catalogEntry : catalog.entries) {
			SkillsEligibilityEntry result;
			result.skillName = catalogEntry.skillName;
			result.skillKey = ResolveSkillKey(catalogEntry);

			const auto* skillConfig = ResolveSkillConfig(
				appConfig,
				result.skillKey,
				result.skillName);
			const auto persistedEnv = LoadPersistedSkillEnv(
				result.skillKey,
				result.skillName);

			if (skillConfig != nullptr && skillConfig->enabled.has_value() &&
				!skillConfig->enabled.value()) {
				result.disabled = true;
				++snapshot.disabledCount;
			}

			result.userInvocable = ParseBoolField(
				GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"user-invocable", L"user_invocable" }),
				true);
			result.disableModelInvocation = ParseBoolField(
				GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"disable-model-invocation", L"disable_model_invocation", L"disablemodelinvocation" }),
				false);

			if (!IsBundledAllowed(catalogEntry, result, appConfig)) {
				result.blockedByAllowlist = true;
				++snapshot.blockedByAllowlistCount;
			}

			const auto requiredOs = SplitList(
				GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"openclaw.os", L"os" }));
			if (!IsCurrentPlatformAllowed(requiredOs)) {
				result.missingOs = requiredOs;
			}

			const auto requiredBins = SplitList(
				GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"openclaw.requires.bins", L"requires.bins" }));
			for (const auto& bin : requiredBins) {
				if (!SkillsHasBinary(bin)) {
					result.missingBins.push_back(bin);
				}
			}

			const auto requiredAnyBins = SplitList(
				GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"openclaw.requires.anybins", L"requires.anybins" }));
			if (!requiredAnyBins.empty()) {
				bool foundAny = false;
				for (const auto& bin : requiredAnyBins) {
					if (SkillsHasBinary(bin)) {
						foundAny = true;
						break;
					}
				}

				if (!foundAny) {
					result.missingAnyBins = requiredAnyBins;
				}
			}

			const auto primaryEnv = ToLower(Trim(GetFrontmatterField(
				catalogEntry.frontmatter,
				{ L"openclaw.primaryenv", L"primaryenv", L"primary-env" })));
			const auto requiredEnv = SplitList(
				GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"openclaw.requires.env", L"requires.env" }));
			for (const auto& envName : requiredEnv) {
				const std::wstring envWide = envName;
				bool hasValue = HasEnvironmentValue(envWide);
				if (!hasValue && skillConfig != nullptr) {
					const auto envIt = skillConfig->env.find(envWide);
					if (envIt != skillConfig->env.end() && !Trim(envIt->second).empty()) {
						hasValue = true;
					}

					if (!hasValue && envName == primaryEnv &&
						!Trim(skillConfig->apiKey).empty()) {
						hasValue = true;
					}
				}

				if (!hasValue) {
					const auto persistedIt = persistedEnv.find(ToLower(envWide));
					if (persistedIt != persistedEnv.end() &&
						!Trim(persistedIt->second).empty()) {
						hasValue = true;
					}
				}

				if (!hasValue) {
					result.missingEnv.push_back(envName);
				}
			}

			const auto requiredConfig = SplitList(
				GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"openclaw.requires.config", L"requires.config" }));
			for (const auto& configPath : requiredConfig) {
				if (!IsSkillsConfigPathTruthy(
					appConfig,
					std::wstring(configPath.begin(), configPath.end()))) {
					result.missingConfig.push_back(configPath);
				}
			}

			const bool hasMissingRequirements =
				!result.missingOs.empty() ||
				!result.missingBins.empty() ||
				!result.missingAnyBins.empty() ||
				!result.missingEnv.empty() ||
				!result.missingConfig.empty();

			if (hasMissingRequirements) {
				++snapshot.missingRequirementsCount;
			}

			result.eligible =
				!result.disabled &&
				!result.blockedByAllowlist &&
				!hasMissingRequirements;
			if (result.eligible) {
				++snapshot.eligibleCount;
			}

			snapshot.entries.push_back(std::move(result));
		}

		return snapshot;
	}

	bool SkillsEligibilityService::ValidateFixtureScenarios(
		const std::filesystem::path& fixturesRoot,
		std::wstring& outError) const {
		outError.clear();

		const auto root = fixturesRoot / L"s2-eligibility" / L"workspace";
		blazeclaw::config::AppConfig appConfig;
		appConfig.skills.allowBundled = { L"allowed-bundled" };
		appConfig.skills.load.extraDirs = { L"../extra" };
		appConfig.skills.limits.maxCandidatesPerRoot = 32;
		appConfig.skills.limits.maxSkillsLoadedPerSource = 32;
		appConfig.skills.entries[L"disabled-skill"].enabled = false;

		const SkillsCatalogService catalogService;
		const auto catalog = catalogService.LoadCatalog(root, appConfig);
		const auto eligibility = Evaluate(catalog, appConfig);

		const auto findByName = [&eligibility](const std::wstring& name) {
			return std::find_if(
				eligibility.entries.begin(),
				eligibility.entries.end(),
				[&name](const SkillsEligibilityEntry& entry) {
					return ToLower(Trim(entry.skillName)) == ToLower(Trim(name));
				});
			};

		const auto allowed = findByName(L"allowed-bundled");
		if (allowed == eligibility.entries.end() || !allowed->eligible) {
			outError = L"S2 eligibility fixture failed: allowed-bundled should be eligible.";
			return false;
		}

		const auto blocked = findByName(L"blocked-bundled");
		if (blocked == eligibility.entries.end() || !blocked->blockedByAllowlist) {
			outError = L"S2 eligibility fixture failed: blocked-bundled should be blocked by allowlist.";
			return false;
		}

		const auto disabled = findByName(L"disabled-skill");
		if (disabled == eligibility.entries.end() || !disabled->disabled) {
			outError = L"S2 eligibility fixture failed: disabled-skill should be disabled by config.";
			return false;
		}

		const auto envRequired = findByName(L"env-required-skill");
		if (envRequired == eligibility.entries.end() || envRequired->missingEnv.empty()) {
			outError = L"S2 eligibility fixture failed: env-required-skill should have missing env requirement.";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core
