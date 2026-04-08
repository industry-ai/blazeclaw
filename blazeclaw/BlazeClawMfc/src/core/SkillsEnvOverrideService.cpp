#include "pch.h"
#include "SkillsEnvOverrideService.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <set>
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
			const std::wstring& skillKey) {
			std::vector<std::filesystem::path> candidates;
			const auto normalized = NormalizeSkillKeyForConfigPath(skillKey);
			if (normalized.empty()) {
				return candidates;
			}

			const auto root = ResolveSkillConfigRoot();
			if (!root.empty()) {
				candidates.push_back(root / normalized / L".env");
			}

			std::error_code ec;
			const auto cwd = std::filesystem::current_path(ec);
			if (!ec) {
				candidates.push_back(
					cwd / L"blazeclaw" / L"skills" / normalized / L".env");
				candidates.push_back(
					cwd / L"skills" / normalized / L".env");
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
				const std::wstring trimmedLine = Trim(line);
				if (trimmedLine.empty() || trimmedLine.starts_with(L"#")) {
					continue;
				}

				const auto equals = trimmedLine.find(L'=');
				if (equals == std::wstring::npos || equals == 0) {
					continue;
				}

				const std::wstring key = Trim(trimmedLine.substr(0, equals));
				std::wstring value = Trim(trimmedLine.substr(equals + 1));
				if (key.empty() || value.empty()) {
					continue;
				}

				if ((value.size() >= 2 && value.front() == L'"' && value.back() == L'"') ||
					(value.size() >= 2 && value.front() == L'\'' && value.back() == L'\'')) {
					value = value.substr(1, value.size() - 2);
				}

				values[key] = value;
			}

			return values;
		}

		std::map<std::wstring, std::wstring> LoadPersistedSkillEnv(
			const std::wstring& skillKey) {
			for (const auto& candidate : BuildSkillConfigCandidates(skillKey)) {
				const auto parsed = ParseDotEnvFile(candidate);
				if (!parsed.empty()) {
					return parsed;
				}
			}

			return {};
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

		bool IsBlockedEnvKey(const std::wstring& key) {
			const std::wstring normalized = ToLower(Trim(key));
			static const std::set<std::wstring> blocked = {
				L"path",
				L"pathext",
				L"comspec",
				L"windir",
				L"systemroot",
				L"ld_preload",
				L"dyld_insert_libraries",
				L"openssl_conf",
			};
			return blocked.find(normalized) != blocked.end();
		}

		std::optional<std::wstring> ReadEnv(const std::wstring& key) {
			wchar_t* value = nullptr;
			std::size_t length = 0;
			const errno_t status = _wdupenv_s(&value, &length, key.c_str());
			if (status != 0 || value == nullptr || length == 0) {
				if (value != nullptr) {
					free(value);
				}
				return std::nullopt;
			}

			const std::wstring result(value);
			free(value);
			return result;
		}

		void SetEnvValue(const std::wstring& key, const std::wstring& value) {
			_wputenv_s(key.c_str(), value.c_str());
		}

		void UnsetEnvValue(const std::wstring& key) {
			_wputenv_s(key.c_str(), L"");
		}

	} // namespace

	SkillsEnvOverrideSnapshot SkillsEnvOverrideService::BuildSnapshot(
		const SkillsCatalogSnapshot& catalog,
		const SkillsEligibilitySnapshot& eligibility,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsEnvOverrideSnapshot snapshot;

		std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityByName;
		for (const auto& item : eligibility.entries) {
			eligibilityByName.emplace(ToLower(item.skillName), item);
		}

		for (const auto& entry : catalog.entries) {
			const auto eligibilityIt = eligibilityByName.find(ToLower(entry.skillName));
			if (eligibilityIt == eligibilityByName.end() || !eligibilityIt->second.eligible) {
				continue;
			}

			const std::wstring skillKey = eligibilityIt->second.skillKey;
			const auto persistedEnv = LoadPersistedSkillEnv(skillKey);
			const auto configIt = appConfig.skills.entries.find(eligibilityIt->second.skillKey);
			const bool hasRuntimeConfig = configIt != appConfig.skills.entries.end();

			const auto appendItem = [&snapshot, &entry](
				const std::wstring& key,
				const std::wstring& value,
				const std::wstring& reason) {
					SkillsEnvOverrideItem item;
					item.key = key;
					item.value = value;
					item.skillName = entry.skillName;
					item.blocked = !reason.empty();
					item.blockedReason = reason;
					snapshot.items.push_back(item);
					if (item.blocked) {
						++snapshot.blockedCount;
					}
					else {
						++snapshot.allowedCount;
					}
				};

			std::set<std::wstring> emittedKeys;
			const auto appendEnvValue = [&](const std::wstring& key, const std::wstring& value) {
				const std::wstring envKey = Trim(key);
				const std::wstring envValue = Trim(value);
				if (envKey.empty() || envValue.empty()) {
					return;
				}

				emittedKeys.insert(ToLower(envKey));
				if (IsBlockedEnvKey(envKey)) {
					appendItem(envKey, envValue, L"blocked-sensitive-key");
				}
				else {
					appendItem(envKey, envValue, L"");
				}
				};

			if (hasRuntimeConfig) {
				for (const auto& [key, value] : configIt->second.env) {
					appendEnvValue(key, value);
				}
			}

			for (const auto& [key, value] : persistedEnv) {
				const std::wstring loweredKey = ToLower(Trim(key));
				if (loweredKey.empty() || emittedKeys.find(loweredKey) != emittedKeys.end()) {
					continue;
				}

				appendEnvValue(key, value);
			}

			const std::wstring primaryEnv = Trim(GetFrontmatterField(
				entry.frontmatter,
				{ L"openclaw.primaryenv", L"primaryenv", L"primary-env" }));
			const std::wstring apiKey = hasRuntimeConfig
				? Trim(configIt->second.apiKey)
				: std::wstring{};
			if (!primaryEnv.empty() && !apiKey.empty()) {
				if (IsBlockedEnvKey(primaryEnv)) {
					appendItem(primaryEnv, apiKey, L"blocked-sensitive-key");
				}
				else {
					appendItem(primaryEnv, apiKey, L"");
				}
			}
		}

		return snapshot;
	}

	void SkillsEnvOverrideService::Apply(const SkillsEnvOverrideSnapshot& snapshot) {
		RevertAll();

		for (const auto& item : snapshot.items) {
			if (item.blocked) {
				continue;
			}

			auto& active = m_active[item.key];
			if (active.refCount == 0) {
				active.baseline = ReadEnv(item.key);
				active.managedValue = item.value;
				active.refCount = 1;
				SetEnvValue(item.key, item.value);
				m_lastApplied.push_back(item.key);
				continue;
			}

			++active.refCount;
		}
	}

	void SkillsEnvOverrideService::RevertAll() {
		for (const auto& key : m_lastApplied) {
			auto it = m_active.find(key);
			if (it == m_active.end()) {
				continue;
			}

			auto& active = it->second;
			if (active.refCount > 0) {
				--active.refCount;
			}

			if (active.refCount > 0) {
				continue;
			}

			if (active.baseline.has_value()) {
				SetEnvValue(key, active.baseline.value());
			}
			else {
				UnsetEnvValue(key);
			}

			m_active.erase(it);
		}

		m_lastApplied.clear();
	}

	bool SkillsEnvOverrideService::ValidateFixtureScenarios(
		const std::filesystem::path& fixturesRoot,
		std::wstring& outError) {
		outError.clear();

		const auto root = fixturesRoot / L"s4-env" / L"workspace";
		blazeclaw::config::AppConfig appConfig;
		appConfig.skills.entries[L"env-safe"].env[L"S4_ENV_SAFE"] = L"safe-value";
		appConfig.skills.entries[L"env-safe"].env[L"PATH"] = L"blocked-value";
		appConfig.skills.entries[L"env-safe"].apiKey = L"secret-api-key";

		const SkillsCatalogService catalogService;
		const SkillsEligibilityService eligibilityService;
		const auto catalog = catalogService.LoadCatalog(root, appConfig);
		const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);
		const auto snapshot = BuildSnapshot(catalog, eligibility, appConfig);

		if (snapshot.allowedCount == 0 || snapshot.blockedCount == 0) {
			outError = L"S4 env fixture failed: expected both allowed and blocked overrides.";
			return false;
		}

		Apply(snapshot);
		const auto safeValue = ReadEnv(L"S4_ENV_SAFE");
		if (!safeValue.has_value() || safeValue.value() != L"safe-value") {
			outError = L"S4 env fixture failed: expected safe env override application.";
			RevertAll();
			return false;
		}

		RevertAll();
		return true;
	}

} // namespace blazeclaw::core
