#include "pch.h"
#include "SkillsCommandService.h"

#include <algorithm>
#include <cwctype>
#include <set>
#include <unordered_map>

namespace blazeclaw::core {

	namespace {

		constexpr std::size_t kMaxCommandLength = 32;
		constexpr std::size_t kMaxDescriptionLength = 100;

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

		std::wstring SanitizeCommandName(const std::wstring& raw) {
			std::wstring normalized;
			normalized.reserve(raw.size());

			for (const wchar_t ch : ToLower(raw)) {
				const bool isAlphaNum =
					(ch >= L'a' && ch <= L'z') ||
					(ch >= L'0' && ch <= L'9') ||
					ch == L'_';
				if (isAlphaNum) {
					normalized.push_back(ch);
					continue;
				}

				if (!normalized.empty() && normalized.back() != L'_') {
					normalized.push_back(L'_');
				}
			}

			while (!normalized.empty() && normalized.front() == L'_') {
				normalized.erase(normalized.begin());
			}

			while (!normalized.empty() && normalized.back() == L'_') {
				normalized.pop_back();
			}

			if (normalized.empty()) {
				normalized = L"skill";
			}

			if (normalized.size() > kMaxCommandLength) {
				normalized.resize(kMaxCommandLength);
			}

			return normalized;
		}

		std::wstring ResolveUniqueName(
			const std::wstring& base,
			std::set<std::wstring>& used) {
			if (used.insert(base).second) {
				return base;
			}

			for (int index = 2; index < 1000; ++index) {
				const std::wstring suffix = L"_" + std::to_wstring(index);
				std::wstring candidate = base;
				if (candidate.size() + suffix.size() > kMaxCommandLength) {
					candidate.resize(kMaxCommandLength - suffix.size());
				}

				candidate += suffix;
				if (used.insert(candidate).second) {
					return candidate;
				}
			}

			return base;
		}

	} // namespace

	SkillsCommandSnapshot SkillsCommandService::BuildSnapshot(
		const SkillsCatalogSnapshot& catalog,
		const SkillsEligibilitySnapshot& eligibility) const {
		SkillsCommandSnapshot snapshot;

		std::unordered_map<std::wstring, SkillsEligibilityEntry> eligibilityBySkill;
		for (const auto& entry : eligibility.entries) {
			eligibilityBySkill.emplace(ToLower(entry.skillName), entry);
		}

		std::set<std::wstring> usedNames;
		for (const auto& catalogEntry : catalog.entries) {
			const auto eligibilityIt =
				eligibilityBySkill.find(ToLower(catalogEntry.skillName));
			if (eligibilityIt == eligibilityBySkill.end()) {
				continue;
			}

			const auto& eligibilityEntry = eligibilityIt->second;
			if (!eligibilityEntry.eligible || !eligibilityEntry.userInvocable) {
				continue;
			}

			SkillsCommandSpec spec;
			spec.skillName = catalogEntry.skillName;
			spec.name = ResolveUniqueName(
				SanitizeCommandName(catalogEntry.skillName),
				usedNames);

			spec.description = Trim(catalogEntry.description);
			if (spec.description.empty()) {
				spec.description = catalogEntry.skillName;
			}

			if (spec.description.size() > kMaxDescriptionLength) {
				spec.description.resize(kMaxDescriptionLength - 3);
				spec.description += L"...";
			}

			const auto dispatchType = ToLower(Trim(GetFrontmatterField(
				catalogEntry.frontmatter,
				{ L"command-dispatch", L"command_dispatch" })));
			if (dispatchType == L"tool") {
				spec.dispatch.enabled = true;
				spec.dispatch.kind = L"tool";
				spec.dispatch.toolName = Trim(GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"command-tool", L"command_tool" }));
				spec.dispatch.argMode = Trim(GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"command-arg-mode", L"command_arg_mode" }));
				if (spec.dispatch.argMode.empty()) {
					spec.dispatch.argMode = L"raw";
				}

				spec.dispatch.argSchema = Trim(GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"command-arg-schema", L"command_arg_schema" }));
				spec.dispatch.resultSchema = Trim(GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"command-result-schema", L"command_result_schema" }));
				spec.dispatch.idempotencyHint = Trim(GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"command-idempotency-hint", L"command_idempotency_hint" }));
				spec.dispatch.retryPolicyHint = Trim(GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"command-retry-policy-hint", L"command_retry_policy_hint" }));
				spec.dispatch.requiresApproval = ToLower(Trim(GetFrontmatterField(
					catalogEntry.frontmatter,
					{ L"command-requires-approval", L"command_requires_approval" })))
					== L"true";
			}

			snapshot.commands.push_back(std::move(spec));
		}

		return snapshot;
	}

	SkillsCommandSnapshot SkillsCommandService::BuildSnapshotWithAdapters(
		const SkillsCatalogSnapshot& catalog,
		const SkillsEligibilitySnapshot& eligibility,
		const std::vector<extensions::IRuntimeSkillCapabilityAdapter*>& adapters) const {
		auto snapshot = BuildSnapshot(catalog, eligibility);

		for (const auto* adapter : adapters) {
			if (adapter == nullptr) {
				continue;
			}

			adapter->EnrichSkillsCommandSnapshot(
				extensions::RuntimeSkillAdapterContext{
					.commandSnapshot = snapshot,
					.mutableCommands = snapshot.commands,
				});
		}

		return snapshot;
	}

	bool SkillsCommandService::ValidateFixtureScenarios(
		const std::filesystem::path& fixturesRoot,
		std::wstring& outError) const {
		outError.clear();

		const auto root = fixturesRoot / L"s3-commands" / L"workspace";
		blazeclaw::config::AppConfig appConfig;

		const SkillsCatalogService catalogService;
		const SkillsEligibilityService eligibilityService;
		const auto catalog = catalogService.LoadCatalog(root, appConfig);
		const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);
		const auto snapshot = BuildSnapshot(catalog, eligibility);

		if (snapshot.commands.size() < 2) {
			outError = L"S3 commands fixture failed: expected at least two command specs.";
			return false;
		}

		const auto hasDedupe = std::any_of(
			snapshot.commands.begin(),
			snapshot.commands.end(),
			[](const SkillsCommandSpec& item) {
				return item.name == L"alpha_skill_2";
			});
		if (!hasDedupe) {
			outError = L"S3 commands fixture failed: expected deduped command name alpha_skill_2.";
			return false;
		}

		const auto hasDispatch = std::any_of(
			snapshot.commands.begin(),
			snapshot.commands.end(),
			[](const SkillsCommandSpec& item) {
				return item.dispatch.enabled &&
					item.dispatch.kind == L"tool" &&
					item.dispatch.toolName == L"tool.dispatch";
			});
		if (!hasDispatch) {
			outError = L"S3 commands fixture failed: expected tool dispatch metadata.";
			return false;
		}

		const auto hardenedDispatch = std::find_if(
			snapshot.commands.begin(),
			snapshot.commands.end(),
			[](const SkillsCommandSpec& item) {
				return item.dispatch.enabled &&
					item.dispatch.toolName == L"tool.dispatch";
			});
		if (hardenedDispatch == snapshot.commands.end()) {
			outError = L"S3 commands fixture failed: missing hardened dispatch command.";
			return false;
		}

		if (hardenedDispatch->dispatch.argSchema != L"schema://tool.dispatch.args.v1" ||
			hardenedDispatch->dispatch.resultSchema != L"schema://tool.dispatch.result.v1" ||
			hardenedDispatch->dispatch.idempotencyHint != L"safe" ||
			hardenedDispatch->dispatch.retryPolicyHint != L"transient-network" ||
			!hardenedDispatch->dispatch.requiresApproval) {
			outError = L"S3 commands fixture failed: expected command schema hardening metadata.";
			return false;
		}

		const auto legacyDispatch = std::find_if(
			snapshot.commands.begin(),
			snapshot.commands.end(),
			[](const SkillsCommandSpec& item) {
				return item.dispatch.enabled == false;
			});
		if (legacyDispatch == snapshot.commands.end()) {
			outError = L"S3 commands fixture failed: expected legacy command fallback entry.";
			return false;
		}

		if (!legacyDispatch->dispatch.argSchema.empty() ||
			!legacyDispatch->dispatch.resultSchema.empty() ||
			!legacyDispatch->dispatch.idempotencyHint.empty() ||
			!legacyDispatch->dispatch.retryPolicyHint.empty() ||
			legacyDispatch->dispatch.requiresApproval) {
			outError = L"S3 commands fixture failed: expected legacy default metadata values.";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core
