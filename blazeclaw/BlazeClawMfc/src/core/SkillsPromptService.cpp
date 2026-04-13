#include "pch.h"
#include "SkillsPromptService.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <sstream>
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

		std::wstring CompactHomePath(const std::filesystem::path& pathValue) {
			std::filesystem::path home;
			wchar_t* homeValue = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&homeValue, &length, L"USERPROFILE") == 0 &&
				homeValue != nullptr &&
				length > 0) {
				home = std::filesystem::path(homeValue);
			}
			if (homeValue != nullptr) {
				free(homeValue);
			}

			const auto pathText = pathValue.wstring();
			if (!home.empty()) {
				const auto homeText = home.wstring();
				if (!homeText.empty() &&
					pathText.size() > homeText.size() &&
					pathText.rfind(homeText, 0) == 0) {
					return L"~" + pathText.substr(homeText.size());
				}
			}

			return pathText;
		}

		bool IncludesSkill(
			const std::vector<std::wstring>& includedSkills,
			const std::wstring& skillName) {
			const auto target = ToLower(Trim(skillName));
			return std::any_of(
				includedSkills.begin(),
				includedSkills.end(),
				[&](const std::wstring& item) {
					return ToLower(Trim(item)) == target;
				});
		}

		bool IsSelfEvolvingHookBridgeReady(const SkillsCatalogSnapshot& catalog) {
			const std::wstring target = L"self-evolving";
			for (const auto& entry : catalog.entries) {
				if (ToLower(Trim(entry.skillName)) != target) {
					continue;
				}

				std::error_code ec;
				const auto hookDoc = entry.skillDir / L"hooks" / L"blazeclaw" / L"HOOK.md";
				const auto hookHandler =
					entry.skillDir / L"hooks" / L"blazeclaw" / L"handler.ts";
				const bool hasHookDoc = std::filesystem::is_regular_file(hookDoc, ec) && !ec;
				ec.clear();
				const bool hasHookHandler =
					std::filesystem::is_regular_file(hookHandler, ec) && !ec;
				return hasHookDoc && hasHookHandler;
			}

			return false;
		}

		std::wstring BuildSelfEvolvingReminderBlock() {
			std::wstringstream reminder;
			reminder << L"\n## Self-Evolving Reminder\n";
			reminder << L"When tasks finish, capture reusable learnings:\n";
			reminder << L"- corrections -> .learnings/LEARNINGS.md\n";
			reminder << L"- failures -> .learnings/ERRORS.md\n";
			reminder << L"- missing capabilities -> .learnings/FEATURE_REQUESTS.md\n";
			reminder << L"Promote proven patterns to AGENTS.md / SOUL.md / TOOLS.md.\n";
			return reminder.str();
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

		std::wstring TruncateField(
			const std::wstring& value,
			const std::size_t maxChars) {
			if (value.size() <= maxChars) {
				return value;
			}

			if (maxChars <= 3) {
				return value.substr(0, maxChars);
			}

			std::wstring truncated = value.substr(0, maxChars - 3);
			truncated += L"...";
			return truncated;
		}

		std::wstring EscapeXml(const std::wstring& value) {
			std::wstring escaped;
			escaped.reserve(value.size());
			for (const wchar_t ch : value) {
				switch (ch) {
				case L'&':
					escaped += L"&amp;";
					break;
				case L'<':
					escaped += L"&lt;";
					break;
				case L'>':
					escaped += L"&gt;";
					break;
				case L'"':
					escaped += L"&quot;";
					break;
				case L'\'':
					escaped += L"&apos;";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}

			return escaped;
		}

		std::wstring BuildCompactPrompt(
			const std::vector<SkillsCatalogEntry>& entries) {
			if (entries.empty()) {
				return {};
			}

			std::wstringstream builder;
			builder
				<< L"\n\nThe following skills provide specialized instructions for specific tasks.\n"
				<< L"Use the read tool to load a skill's file when the task matches its name.\n"
				<< L"When a skill file references a relative path, resolve it against the skill directory (parent of SKILL.md / dirname of the path) and use that absolute path in tool commands.\n\n"
				<< L"<available_skills>\n";

			for (const auto& entry : entries) {
				builder << L"  <skill>\n";
				builder << L"    <name>" << EscapeXml(entry.skillName) << L"</name>\n";
				builder << L"    <location>" << EscapeXml(CompactHomePath(entry.skillFile)) << L"</location>\n";
				builder << L"  </skill>\n";
			}

			builder << L"</available_skills>";
			return builder.str();
		}

		std::wstring BuildFullPrompt(
			const std::vector<SkillsCatalogEntry>& entries,
			const std::vector<SkillsPromptSnapshot::PlannerSkillContext>& plannerContext,
			const bool includeSelfEvolvingReminder,
			const bool compact,
			const bool truncated,
			const std::size_t totalVisibleSkillCount,
			const std::wstring& compactPromptBody) {
			std::wstringstream builder;
			builder << L"# Skills\n";
			builder << L"Available skill instructions for this run:\n";

			if (compact) {
				if (truncated) {
					builder
						<< L"⚠️ Skills truncated: included "
						<< entries.size()
						<< L" of "
						<< totalVisibleSkillCount
						<< L" (compact format, descriptions omitted). Run `openclaw skills check` to audit.\n";
				}
				else {
					builder
						<< L"⚠️ Skills catalog using compact format (descriptions omitted). Run `openclaw skills check` to audit.\n";
				}
				builder << compactPromptBody;
			}
			else {
				for (const auto& entry : entries) {
					builder << L"- " << entry.skillName << L": " << entry.description;
					builder << L" (" << CompactHomePath(entry.skillFile) << L")\n";
				}
			}

			if (!plannerContext.empty()) {
				builder << L"\n## Planner Context\n";
				for (const auto& context : plannerContext) {
					builder << L"- skill=" << context.skillName;
					builder << L"; capability=" << context.capability;
					builder << L"; preconditions=" << context.preconditions;
					builder << L"; sideEffects=" << context.sideEffects;
					if (!context.commandToolName.empty()) {
						builder << L"; tool=" << context.commandToolName;
					}
					builder << L"\n";
				}
			}

			if (includeSelfEvolvingReminder) {
				builder << BuildSelfEvolvingReminderBlock();
			}

			return builder.str();
		}

		SkillsPromptSnapshot::PlannerSkillContext BuildPlannerContext(
			const SkillsCatalogEntry& entry) {
			SkillsPromptSnapshot::PlannerSkillContext context;
			context.skillName = entry.skillName;

			const std::wstring fallbackCapability = entry.description.empty()
				? entry.skillName
				: entry.description;
			context.capability = TruncateField(
				GetFrontmatterField(
					entry.frontmatter,
					{ L"planner-capability", L"planner_capability" })
				.empty()
				? fallbackCapability
				: GetFrontmatterField(
					entry.frontmatter,
					{ L"planner-capability", L"planner_capability" }),
				120);

			const std::wstring preconditions = GetFrontmatterField(
				entry.frontmatter,
				{ L"planner-preconditions", L"planner_preconditions" });
			context.preconditions = TruncateField(
				preconditions.empty() ? L"none" : preconditions,
				120);

			const std::wstring sideEffects = GetFrontmatterField(
				entry.frontmatter,
				{ L"planner-side-effects", L"planner_side_effects" });
			context.sideEffects = TruncateField(
				sideEffects.empty() ? L"none" : sideEffects,
				120);

			context.commandToolName = TruncateField(
				GetFrontmatterField(
					entry.frontmatter,
					{ L"command-tool", L"command_tool" }),
				80);
			return context;
		}

	} // namespace

	std::optional<std::vector<std::wstring>> SkillsPromptService::NormalizeFilter(
		const std::optional<std::vector<std::wstring>>& input) {
		if (!input.has_value()) {
			return std::nullopt;
		}

		std::vector<std::wstring> normalized;
		for (const auto& item : input.value()) {
			const auto lowered = ToLower(Trim(item));
			if (!lowered.empty()) {
				normalized.push_back(lowered);
			}
		}

		std::sort(normalized.begin(), normalized.end());
		normalized.erase(
			std::unique(normalized.begin(), normalized.end()),
			normalized.end());
		return normalized;
	}

	SkillsPromptSnapshot SkillsPromptService::BuildSnapshot(
		const SkillsCatalogSnapshot& catalog,
		const SkillsEligibilitySnapshot& eligibility,
		const blazeclaw::config::AppConfig& appConfig,
		const std::optional<std::vector<std::wstring>>& skillFilter,
		const bool enableSelfEvolvingPromptFallback) const {
		SkillsPromptSnapshot snapshot;
		snapshot.totalEligible = eligibility.eligibleCount;
		snapshot.filter = NormalizeFilter(skillFilter);

		std::unordered_map<std::wstring, bool> eligibilityMap;
		std::unordered_map<std::wstring, bool> modelInvocationMap;
		for (const auto& item : eligibility.entries) {
			const auto key = ToLower(Trim(item.skillName));
			eligibilityMap[key] = item.eligible;
			modelInvocationMap[key] = item.disableModelInvocation;
		}

		const auto includeFilter = snapshot.filter.has_value() &&
			!snapshot.filter->empty();

		std::vector<const SkillsCatalogEntry*> visibleEntries;
		visibleEntries.reserve(catalog.entries.size());
		for (const auto& entry : catalog.entries) {
			const auto key = ToLower(Trim(entry.skillName));
			const auto eligibilityIt = eligibilityMap.find(key);
			if (eligibilityIt == eligibilityMap.end() || !eligibilityIt->second) {
				continue;
			}

			const auto modelInvocationIt = modelInvocationMap.find(key);
			if (modelInvocationIt != modelInvocationMap.end() &&
				modelInvocationIt->second) {
				continue;
			}

			if (includeFilter &&
				std::find(snapshot.filter->begin(), snapshot.filter->end(), key) ==
				snapshot.filter->end()) {
				continue;
			}

			visibleEntries.push_back(&entry);
		}

		std::vector<SkillsCatalogEntry> entriesForPrompt;
		entriesForPrompt.reserve(
			std::min<std::size_t>(
				visibleEntries.size(),
				appConfig.skills.limits.maxSkillsInPrompt));

		for (const auto* entry : visibleEntries) {
			if (entriesForPrompt.size() >= appConfig.skills.limits.maxSkillsInPrompt) {
				snapshot.truncated = true;
				break;
			}

			entriesForPrompt.push_back(*entry);
			snapshot.includedSkills.push_back(entry->skillName);
			snapshot.plannerContext.push_back(BuildPlannerContext(*entry));
		}

		const bool includeSelfEvolvingReminder =
			enableSelfEvolvingPromptFallback &&
			IncludesSkill(snapshot.includedSkills, L"self-evolving") &&
			IsSelfEvolvingHookBridgeReady(catalog);

		const std::wstring fullPrompt = BuildFullPrompt(
			entriesForPrompt,
			snapshot.plannerContext,
			includeSelfEvolvingReminder,
			false,
			snapshot.truncated,
			visibleEntries.size(),
			L"");

		const std::size_t maxPromptChars = appConfig.skills.limits.maxSkillsPromptChars;
		const std::size_t compactWarningOverhead = 180;
		const std::size_t compactBudget =
			maxPromptChars > compactWarningOverhead
			? maxPromptChars - compactWarningOverhead
			: maxPromptChars;

		if (fullPrompt.size() <= maxPromptChars) {
			snapshot.prompt = fullPrompt;
		}
		else {
			bool compact = false;
			std::vector<SkillsCatalogEntry> compactEntries = entriesForPrompt;
			std::wstring compactBody = BuildCompactPrompt(compactEntries);

			auto compactFits = [&](const std::vector<SkillsCatalogEntry>& testEntries) {
				return BuildCompactPrompt(testEntries).size() <= compactBudget;
				};

			if (compactFits(compactEntries)) {
				compact = true;
			}
			else {
				compact = true;
				std::size_t lo = 0;
				std::size_t hi = compactEntries.size();
				while (lo < hi) {
					const std::size_t mid = static_cast<std::size_t>(
						std::ceil((static_cast<double>(lo + hi)) / 2.0));
					std::vector<SkillsCatalogEntry> candidate(
						compactEntries.begin(),
						compactEntries.begin() + static_cast<std::ptrdiff_t>(mid));
					if (compactFits(candidate)) {
						lo = mid;
					}
					else {
						hi = mid - 1;
					}
				}

				if (lo < compactEntries.size()) {
					compactEntries.resize(lo);
					snapshot.truncated = true;
					snapshot.includedSkills.resize(lo);
					snapshot.plannerContext.resize(lo);
				}
				compactBody = BuildCompactPrompt(compactEntries);
			}

			snapshot.prompt = BuildFullPrompt(
				compactEntries,
				snapshot.plannerContext,
				includeSelfEvolvingReminder,
				compact,
				snapshot.truncated,
				visibleEntries.size(),
				compactBody);

			if (snapshot.prompt.size() > maxPromptChars) {
				snapshot.prompt = snapshot.prompt.substr(0, maxPromptChars);
				snapshot.truncated = true;
			}
		}

		snapshot.includedCount = static_cast<std::uint32_t>(snapshot.includedSkills.size());
		snapshot.promptChars = static_cast<std::uint32_t>(snapshot.prompt.size());
		return snapshot;
	}

	bool SkillsPromptService::ValidateFixtureScenarios(
		const std::filesystem::path& fixturesRoot,
		std::wstring& outError) const {
		outError.clear();

		const auto root = fixturesRoot / L"s2-prompt" / L"workspace";
		blazeclaw::config::AppConfig appConfig;
		appConfig.skills.limits.maxSkillsInPrompt = 1;
		appConfig.skills.limits.maxSkillsPromptChars = 150;

		const SkillsCatalogService catalogService;
		const SkillsEligibilityService eligibilityService;

		const auto catalog = catalogService.LoadCatalog(root, appConfig);
		const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);
		const auto snapshot = BuildSnapshot(catalog, eligibility, appConfig);
		if (!snapshot.truncated) {
			outError = L"S2 prompt fixture failed: expected prompt truncation.";
			return false;
		}

		if (snapshot.prompt.find(L"## Self-Evolving Reminder") != std::wstring::npos) {
			outError = L"S2 prompt fixture failed: self-evolving reminder should not appear without self-evolving skill.";
			return false;
		}

		if (snapshot.includedCount != 1) {
			outError = L"S2 prompt fixture failed: expected maxSkillsInPrompt limit of 1.";
			return false;
		}

		if (snapshot.prompt.find(L"compact format") == std::wstring::npos &&
			snapshot.prompt.find(L"Skills catalog using compact format") == std::wstring::npos) {
			outError = L"S2 prompt fixture failed: expected compact fallback warning in truncated fixture prompt.";
			return false;
		}

		const std::optional<std::vector<std::wstring>> filter =
			std::vector<std::wstring>{ L"prompt-skill-b" };
		const auto filtered = BuildSnapshot(catalog, eligibility, appConfig, filter);
		if (filtered.includedSkills.empty() ||
			ToLower(filtered.includedSkills.front()) != L"prompt-skill-b") {
			outError = L"S2 prompt fixture failed: expected filter to include prompt-skill-b.";
			return false;
		}

		if (filtered.plannerContext.empty()) {
			outError = L"S2 prompt fixture failed: expected planner context entries.";
			return false;
		}

		const auto plannerIt = std::find_if(
			filtered.plannerContext.begin(),
			filtered.plannerContext.end(),
			[](const SkillsPromptSnapshot::PlannerSkillContext& context) {
				return ToLower(context.skillName) == L"prompt-skill-b";
			});
		if (plannerIt == filtered.plannerContext.end()) {
			outError = L"S2 prompt fixture failed: expected planner context for prompt-skill-b.";
			return false;
		}

		if (plannerIt->capability.find(L"summarize fixture insights") == std::wstring::npos ||
			plannerIt->preconditions.find(L"fixture workspace access") == std::wstring::npos ||
			plannerIt->sideEffects.find(L"diagnostic summary") == std::wstring::npos ||
			plannerIt->commandToolName != L"summarize") {
			outError = L"S2 prompt fixture failed: expected planner metadata fields in context.";
			return false;
		}

		if (filtered.prompt.find(L"## Planner Context") == std::wstring::npos) {
			outError = L"S2 prompt fixture failed: expected planner context prompt block.";
			return false;
		}

		const auto legacyVisibilitySnapshot = BuildSnapshot(
			catalog,
			eligibility,
			appConfig,
			std::nullopt,
			false);
		if (IncludesSkill(legacyVisibilitySnapshot.includedSkills, L"prompt-legacy-hidden")) {
			outError = L"S2 prompt fixture failed: expected legacy disableModelInvocation field to hide skill from available prompt list.";
			return false;
		}

		const auto selfEvolvingRoot =
			fixturesRoot / L"s7-self-evolving" / L"workspace";
		blazeclaw::config::AppConfig selfEvolvingConfig;
		selfEvolvingConfig.skills.limits.maxSkillsInPrompt = 8;
		selfEvolvingConfig.skills.limits.maxSkillsPromptChars = 4000;
		selfEvolvingConfig.skills.limits.maxCandidatesPerRoot = 32;
		selfEvolvingConfig.skills.limits.maxSkillsLoadedPerSource = 32;
		selfEvolvingConfig.skills.limits.maxSkillFileBytes = 32 * 1024;

		const auto selfCatalog = catalogService.LoadCatalog(selfEvolvingRoot, selfEvolvingConfig);
		const auto selfEligibility = eligibilityService.Evaluate(selfCatalog, selfEvolvingConfig);
		const auto selfSnapshot = BuildSnapshot(
			selfCatalog,
			selfEligibility,
			selfEvolvingConfig,
			std::nullopt,
			false);
		if (selfSnapshot.prompt.find(L"## Self-Evolving Reminder") != std::wstring::npos) {
			outError = L"S7 self-evolving fixture failed: reminder should be absent when prompt fallback is disabled.";
			return false;
		}

		const auto selfSnapshotWithFallback = BuildSnapshot(
			selfCatalog,
			selfEligibility,
			selfEvolvingConfig,
			std::nullopt,
			true);
		if (selfSnapshotWithFallback.prompt.find(L"## Self-Evolving Reminder") == std::wstring::npos) {
			outError = L"S7 self-evolving fixture failed: expected reminder when prompt fallback is enabled.";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core
