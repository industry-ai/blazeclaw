#include "pch.h"
#include "SkillsCatalogPromptParityValidationService.h"

#include "SkillsCatalogService.h"
#include "SkillsEligibilityService.h"
#include "SkillsFrontmatterCompat.h"
#include "SkillsPromptService.h"
#include "filesystem/SafeOpenSync.h"

#include <algorithm>
#include <cstdlib>
#include <cwctype>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace blazeclaw::core {

	namespace {

		struct OpenClawSkillSnapshot {
			std::wstring key;
			std::wstring expectedName;
			std::wstring expectedDescription;
			bool parsedFrontmatter = false;
			std::filesystem::path skillFile;
			std::vector<std::wstring> parseErrors;
		};

		struct SourceRootProbe {
			std::wstring label;
			int order = 0;
			std::filesystem::path root;
		};

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

		std::optional<std::wstring> ReadEnvVar(const wchar_t* key) {
			wchar_t* value = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&value, &length, key) != 0 || value == nullptr || length == 0) {
				if (value != nullptr) {
					free(value);
				}
				return std::nullopt;
			}

			const std::wstring resolved(value);
			free(value);
			return resolved;
		}

		std::vector<std::wstring> ParseEnvPathList(const std::wstring& raw) {
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
				if (ch == L';' || ch == L',' || ch == L'|') {
					flush();
					continue;
				}
				current.push_back(ch);
			}

			flush();
			return values;
		}

		std::filesystem::path CanonicalOrSelf(const std::filesystem::path& value) {
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(value, ec);
			if (ec) {
				return value.lexically_normal();
			}

			return canonical;
		}

		std::filesystem::path ResolveRootPath(
			const std::filesystem::path& workspaceRoot,
			const std::wstring& configuredPath) {
			std::filesystem::path root(configuredPath);
			if (root.is_relative()) {
				root = workspaceRoot / root;
			}
			return CanonicalOrSelf(root);
		}

		std::filesystem::path ResolveDefaultRoot(
			const std::filesystem::path& directPath,
			const std::filesystem::path& nestedPath) {
			std::error_code ec;
			if (std::filesystem::is_directory(directPath, ec) && !ec) {
				return CanonicalOrSelf(directPath);
			}

			ec.clear();
			if (std::filesystem::is_directory(nestedPath, ec) && !ec) {
				return CanonicalOrSelf(nestedPath);
			}

			return CanonicalOrSelf(directPath);
		}

		std::string WideToUtf8(const std::wstring& value) {
			if (value.empty()) {
				return {};
			}

			const auto toAsciiFallback = [&value]() {
				std::string output;
				output.reserve(value.size());
				for (const wchar_t ch : value) {
					output.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
				}
				return output;
			};

			const int required = WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (required <= 0) {
				return toAsciiFallback();
			}

			std::string output(static_cast<std::size_t>(required), '\0');
			const int converted = WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				required,
				nullptr,
				nullptr);
			if (converted <= 0) {
				return toAsciiFallback();
			}

			return output;
		}

		std::wstring ReadVerifiedUtf8AsWide(
			const std::filesystem::path& skillFile,
			const std::uint64_t maxBytes,
			const bool rejectPathSymlink,
			std::wstring& outDetail) {
			blazeclaw::core::filesystem::VerifiedOpenRequest request;
			request.filePath = skillFile;
			request.resolvedPath = CanonicalOrSelf(skillFile);
			request.policy.rejectPathSymlink = rejectPathSymlink;
			request.policy.maxBytes = maxBytes;
			request.policy.allowedType =
				blazeclaw::core::filesystem::VerifiedOpenAllowedType::File;

			const auto result = blazeclaw::core::filesystem::OpenVerifiedFileUtf8Sync(request);
			if (!result.ok) {
				outDetail = result.detail;
				return {};
			}

			outDetail.clear();
			return result.utf8Content;
		}

		std::map<std::wstring, OpenClawSkillSnapshot> CollectOpenClawSkills(
			const std::filesystem::path& openClawRoot,
			const std::uint64_t maxSkillFileBytes,
			const bool rejectPathSymlink,
			bool& outRootFound,
			std::vector<std::wstring>& outWarnings) {
			std::map<std::wstring, OpenClawSkillSnapshot> snapshots;
			std::error_code ec;
			if (!std::filesystem::is_directory(openClawRoot, ec) || ec) {
				outRootFound = false;
				outWarnings.push_back(L"OpenClaw bundled root not found: " + openClawRoot.wstring());
				return snapshots;
			}
			outRootFound = true;

			for (const auto& item : std::filesystem::directory_iterator(openClawRoot, ec)) {
				if (ec) {
					break;
				}

				if (!item.is_directory()) {
					continue;
				}

				const auto skillFile = item.path() / L"SKILL.md";
				std::error_code fileEc;
				if (!std::filesystem::is_regular_file(skillFile, fileEc) || fileEc) {
					continue;
				}

				std::wstring readDetail;
				const std::wstring content = ReadVerifiedUtf8AsWide(
					skillFile,
					maxSkillFileBytes,
					rejectPathSymlink,
					readDetail);
				if (!readDetail.empty()) {
					outWarnings.push_back(
						L"OpenClaw skill read failure: " + skillFile.wstring() + L" (" + readDetail + L")");
					continue;
				}

				OpenClawSkillSnapshot skill;
				skill.skillFile = CanonicalOrSelf(skillFile);
				skill.expectedName = item.path().filename().wstring();
				skill.expectedDescription.clear();

				std::vector<std::wstring> validationErrors;
				const auto parsed = ParseSkillFrontmatterCompat(content, validationErrors);
				if (parsed.has_value()) {
					skill.parsedFrontmatter = true;
					skill.expectedName = parsed->name;
					skill.expectedDescription = parsed->description;
				}
				else {
					skill.parsedFrontmatter = false;
					skill.parseErrors = validationErrors;
				}

				skill.key = ToLower(Trim(skill.expectedName));
				if (skill.key.empty()) {
					skill.key = ToLower(item.path().filename().wstring());
				}

				snapshots.insert_or_assign(skill.key, skill);
			}

			return snapshots;
		}

		std::vector<std::filesystem::path> CollectCandidateSkillDirs(
			const std::filesystem::path& root) {
			std::vector<std::filesystem::path> candidates;
			std::error_code ec;
			if (!std::filesystem::is_directory(root, ec) || ec) {
				return candidates;
			}

			for (const auto& item : std::filesystem::directory_iterator(root, ec)) {
				if (ec) {
					break;
				}
				if (!item.is_directory()) {
					continue;
				}

				const auto skillFile = item.path() / L"SKILL.md";
				std::error_code skillEc;
				if (std::filesystem::is_regular_file(skillFile, skillEc) && !skillEc) {
					candidates.push_back(item.path());
				}
			}

			std::sort(candidates.begin(), candidates.end());
			return candidates;
		}

		std::vector<SourceRootProbe> BuildSourceRootProbes(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& appConfig) {
			std::vector<SourceRootProbe> roots;
			int order = 0;

			for (const auto& extraDir : appConfig.skills.load.extraDirs) {
				if (Trim(extraDir).empty()) {
					continue;
				}

				roots.push_back(SourceRootProbe{
					.label = L"extra",
					.order = order++,
					.root = ResolveRootPath(workspaceRoot, extraDir),
				});
			}

			if (const auto pluginDirs = ReadEnvVar(L"BLAZECLAW_PLUGIN_SKILL_DIRS"); pluginDirs.has_value()) {
				for (const auto& pluginDir : ParseEnvPathList(pluginDirs.value())) {
					roots.push_back(SourceRootProbe{
						.label = L"plugin",
						.order = order++,
						.root = ResolveRootPath(workspaceRoot, pluginDir),
					});
				}
			}

			const auto bundledOverride = ReadEnvVar(L"BLAZECLAW_BUNDLED_SKILLS_DIR");
			const std::filesystem::path bundledRoot = bundledOverride.has_value()
				? ResolveRootPath(workspaceRoot, bundledOverride.value())
				: ResolveDefaultRoot(
					workspaceRoot / L"skills-bundled",
					workspaceRoot / L"blazeclaw" / L"skills-bundled");
			roots.push_back(SourceRootProbe{
				.label = L"bundled",
				.order = order++,
				.root = bundledRoot,
			});

			const auto managedOverride = ReadEnvVar(L"BLAZECLAW_MANAGED_SKILLS_DIR");
			const std::filesystem::path managedRoot = managedOverride.has_value()
				? ResolveRootPath(workspaceRoot, managedOverride.value())
				: CanonicalOrSelf(workspaceRoot / L".blazeclaw" / L"skills");
			roots.push_back(SourceRootProbe{
				.label = L"managed",
				.order = order++,
				.root = managedRoot,
			});

			std::filesystem::path homeDir;
			if (const auto profile = ReadEnvVar(L"USERPROFILE"); profile.has_value()) {
				homeDir = std::filesystem::path(profile.value());
			}
			else if (const auto home = ReadEnvVar(L"HOME"); home.has_value()) {
				homeDir = std::filesystem::path(home.value());
			}
			if (!homeDir.empty()) {
				roots.push_back(SourceRootProbe{
					.label = L"personal",
					.order = order++,
					.root = CanonicalOrSelf(homeDir / L".agents" / L"skills"),
				});
			}

			roots.push_back(SourceRootProbe{
				.label = L"project",
				.order = order++,
				.root = CanonicalOrSelf(workspaceRoot / L".agents" / L"skills"),
			});

			roots.push_back(SourceRootProbe{
				.label = L"workspace",
				.order = order++,
				.root = ResolveDefaultRoot(
					workspaceRoot / L"skills",
					workspaceRoot / L"blazeclaw" / L"skills"),
			});

			const auto openClawOriginalOverride = ReadEnvVar(L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
			const std::wstring configuredOpenClawOriginal = Trim(appConfig.skills.openclawOriginal.sourceDir);
			const std::filesystem::path openClawOriginalDefault = ResolveDefaultRoot(
				workspaceRoot / L"skills-openclaw-original",
				workspaceRoot / L"blazeclaw" / L"skills-openclaw-original");
			const std::filesystem::path openClawOriginalConfigured = configuredOpenClawOriginal.empty()
				? openClawOriginalDefault
				: ResolveRootPath(workspaceRoot, configuredOpenClawOriginal);
			const std::filesystem::path openClawOriginalRoot = openClawOriginalOverride.has_value()
				? ResolveRootPath(workspaceRoot, openClawOriginalOverride.value())
				: ResolveDefaultRoot(openClawOriginalConfigured, openClawOriginalDefault);
			roots.push_back(SourceRootProbe{
				.label = L"openclaw-original",
				.order = order++,
				.root = openClawOriginalRoot,
			});

			std::set<std::filesystem::path> seen;
			std::vector<SourceRootProbe> unique;
			for (const auto& root : roots) {
				const auto canonical = CanonicalOrSelf(root.root);
				if (seen.insert(canonical).second) {
					SourceRootProbe normalized = root;
					normalized.root = canonical;
					unique.push_back(normalized);
				}
			}

			return unique;
		}

	} // namespace

	SkillsCatalogPromptParityValidationReport SkillsCatalogPromptParityValidationService::BuildReport(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsCatalogPromptParityValidationReport report;
		report.workspaceRoot = CanonicalOrSelf(workspaceRoot);

		report.openClawBundledRoot = ResolveDefaultRoot(
			workspaceRoot / L"openclaw" / L"skills",
			workspaceRoot / L"blazeclaw" / L"openclaw" / L"skills");
		report.blazeClawBundledRoot = ResolveDefaultRoot(
			workspaceRoot / L"skills-bundled",
			workspaceRoot / L"blazeclaw" / L"skills-bundled");

		const auto openClawSkills = CollectOpenClawSkills(
			report.openClawBundledRoot,
			static_cast<std::uint64_t>(appConfig.skills.limits.maxSkillFileBytes),
			appConfig.skills.load.rejectPathSymlink,
			report.openClawBundledRootFound,
			report.warnings);

		std::error_code bundledEc;
		report.blazeClawBundledRootFound =
			std::filesystem::is_directory(report.blazeClawBundledRoot, bundledEc) && !bundledEc;
		if (!report.blazeClawBundledRootFound) {
			report.warnings.push_back(
				L"BlazeClaw bundled root not found: " + report.blazeClawBundledRoot.wstring());
		}

		SkillsCatalogService catalogService;
		const auto catalog = catalogService.LoadCatalog(workspaceRoot, appConfig);
		SkillsEligibilityService eligibilityService;
		const auto eligibility = eligibilityService.Evaluate(catalog, appConfig);

		SkillsPromptService promptService;
		auto fullPromptConfig = appConfig;
		fullPromptConfig.skills.limits.maxSkillsInPrompt =
			std::max<std::uint32_t>(
				static_cast<std::uint32_t>(catalog.entries.size()),
				appConfig.skills.limits.maxSkillsInPrompt);
		fullPromptConfig.skills.limits.maxSkillsPromptChars =
			std::max<std::uint32_t>(
				120000,
				appConfig.skills.limits.maxSkillsPromptChars);
		const auto fullPrompt = promptService.BuildSnapshot(
			catalog,
			eligibility,
			fullPromptConfig,
			std::nullopt,
			false);

		auto compactPromptConfig = fullPromptConfig;
		compactPromptConfig.skills.limits.maxSkillsPromptChars = 240;
		const auto compactPrompt = promptService.BuildSnapshot(
			catalog,
			eligibility,
			compactPromptConfig,
			std::nullopt,
			false);

		std::map<std::wstring, const SkillsCatalogEntry*> catalogByKey;
		for (const auto& entry : catalog.entries) {
			catalogByKey.insert_or_assign(ToLower(Trim(entry.skillName)), &entry);
		}

		std::map<std::wstring, const SkillsEligibilityEntry*> eligibilityByKey;
		for (const auto& entry : eligibility.entries) {
			eligibilityByKey.insert_or_assign(ToLower(Trim(entry.skillName)), &entry);
		}

		std::set<std::wstring> fullPromptKeys;
		for (const auto& name : fullPrompt.includedSkills) {
			fullPromptKeys.insert(ToLower(Trim(name)));
		}

		std::set<std::wstring> compactPromptKeys;
		for (const auto& name : compactPrompt.includedSkills) {
			compactPromptKeys.insert(ToLower(Trim(name)));
		}

		for (const auto& [skillKey, openClawSkill] : openClawSkills) {
			const auto catalogIt = catalogByKey.find(skillKey);
			const SkillsCatalogEntry* catalogEntry =
				catalogIt == catalogByKey.end() ? nullptr : catalogIt->second;

			SkillsCatalogPromptParityCatalogEntry catalogResult;
			catalogResult.skillName = openClawSkill.expectedName;
			catalogResult.expectedName = openClawSkill.expectedName;
			catalogResult.expectedDescription = openClawSkill.expectedDescription;
			catalogResult.catalogPresent = catalogEntry != nullptr;
			if (catalogEntry != nullptr) {
				catalogResult.catalogName = catalogEntry->skillName;
				catalogResult.catalogDescription = catalogEntry->description;
				catalogResult.nameMatches =
					ToLower(Trim(catalogResult.expectedName)) ==
					ToLower(Trim(catalogResult.catalogName));
				catalogResult.descriptionMatches =
					ToLower(Trim(catalogResult.expectedDescription)) ==
					ToLower(Trim(catalogResult.catalogDescription));
				catalogResult.sourceLabel =
					SkillsCatalogService::SourceKindLabel(catalogEntry->sourceKind);
			}
			report.catalogEntries.push_back(std::move(catalogResult));

			SkillsCatalogPromptParityMetadataEntry metadataResult;
			metadataResult.skillName = openClawSkill.expectedName;
			metadataResult.openClawFrontmatterParsed = openClawSkill.parsedFrontmatter;
			metadataResult.catalogFrontmatterValid =
				catalogEntry != nullptr && catalogEntry->validFrontmatter;
			metadataResult.catalogMetadataPresent =
				catalogEntry != nullptr && catalogEntry->metadata.has_value();
			if (!openClawSkill.parsedFrontmatter) {
				metadataResult.issues.push_back(L"openclaw-frontmatter-parse-failed");
			}
			if (catalogEntry == nullptr) {
				metadataResult.issues.push_back(L"catalog-entry-missing");
			}
			else if (!catalogEntry->validFrontmatter) {
				metadataResult.issues.push_back(L"catalog-frontmatter-invalid");
			}
			else if (!catalogEntry->metadata.has_value()) {
				metadataResult.issues.push_back(L"catalog-metadata-missing");
			}
			report.metadataEntries.push_back(std::move(metadataResult));

			const auto eligibilityIt = eligibilityByKey.find(skillKey);
			const SkillsEligibilityEntry* eligibilityEntry =
				eligibilityIt == eligibilityByKey.end() ? nullptr : eligibilityIt->second;

			SkillsCatalogPromptParityEligibilityEntry eligibilityResult;
			eligibilityResult.skillName = openClawSkill.expectedName;
			eligibilityResult.expectedEligible = true;
			eligibilityResult.actualEligible =
				eligibilityEntry != nullptr && eligibilityEntry->eligible;
			eligibilityResult.disabled =
				eligibilityEntry != nullptr && eligibilityEntry->disabled;
			eligibilityResult.blockedByAllowlist =
				eligibilityEntry != nullptr && eligibilityEntry->blockedByAllowlist;
			if (catalogEntry == nullptr) {
				eligibilityResult.issues.push_back(L"catalog-entry-missing");
			}
			if (eligibilityEntry == nullptr) {
				eligibilityResult.issues.push_back(L"eligibility-entry-missing");
			}
			else if (!eligibilityEntry->eligible) {
				eligibilityResult.issues.push_back(L"eligible-false");
			}
			report.eligibilityEntries.push_back(std::move(eligibilityResult));

			SkillsCatalogPromptParityVisibilityEntry visibilityResult;
			visibilityResult.skillName = openClawSkill.expectedName;
			visibilityResult.hiddenByDisableModelInvocation =
				eligibilityEntry != nullptr && eligibilityEntry->disableModelInvocation;
			visibilityResult.expectedVisible =
				!visibilityResult.hiddenByDisableModelInvocation;
			visibilityResult.visibleInPrompt =
				fullPromptKeys.find(skillKey) != fullPromptKeys.end();
			if (visibilityResult.expectedVisible != visibilityResult.visibleInPrompt) {
				visibilityResult.issues.push_back(L"prompt-visibility-mismatch");
			}
			report.visibilityEntries.push_back(std::move(visibilityResult));

			SkillsCatalogPromptParityCompactFallbackEntry compactResult;
			compactResult.skillName = openClawSkill.expectedName;
			compactResult.includedInFullPromptSnapshot =
				fullPromptKeys.find(skillKey) != fullPromptKeys.end();
			compactResult.retainedInCompactFallbackSnapshot =
				compactPromptKeys.find(skillKey) != compactPromptKeys.end();
			if (compactResult.includedInFullPromptSnapshot &&
				!compactResult.retainedInCompactFallbackSnapshot &&
				!compactPrompt.truncated) {
				compactResult.issues.push_back(L"missing-from-compact-fallback");
			}
			report.compactFallbackEntries.push_back(std::move(compactResult));
		}

		auto sourceRoots = BuildSourceRootProbes(workspaceRoot, appConfig);
		std::map<std::wstring, std::vector<std::wstring>> sourceCandidatesBySkill;
		for (const auto& sourceRoot : sourceRoots) {
			for (const auto& skillDir : CollectCandidateSkillDirs(sourceRoot.root)) {
				const std::wstring skillKey = ToLower(Trim(skillDir.filename().wstring()));
				if (skillKey.empty()) {
					continue;
				}
				sourceCandidatesBySkill[skillKey].push_back(sourceRoot.label);
			}
		}

		for (const auto& [skillKey, openClawSkill] : openClawSkills) {
			SkillsCatalogPromptParityPrecedenceEntry precedenceResult;
			precedenceResult.skillName = openClawSkill.expectedName;

			const auto candidatesIt = sourceCandidatesBySkill.find(skillKey);
			if (candidatesIt != sourceCandidatesBySkill.end()) {
				precedenceResult.candidateSourceLabels = candidatesIt->second;
				if (!candidatesIt->second.empty()) {
					precedenceResult.winnerSourceLabel = candidatesIt->second.back();
				}
			}

			const auto catalogIt = catalogByKey.find(skillKey);
			if (catalogIt == catalogByKey.end()) {
				precedenceResult.deterministic = false;
				precedenceResult.issues.push_back(L"catalog-entry-missing");
			}
			else {
				const std::wstring actualWinner =
					SkillsCatalogService::SourceKindLabel(catalogIt->second->sourceKind);
				if (precedenceResult.winnerSourceLabel.empty()) {
					precedenceResult.winnerSourceLabel = actualWinner;
				}
				if (ToLower(Trim(actualWinner)) !=
					ToLower(Trim(precedenceResult.winnerSourceLabel))) {
					precedenceResult.deterministic = false;
					precedenceResult.issues.push_back(L"source-precedence-mismatch");
				}
			}

			report.precedenceEntries.push_back(std::move(precedenceResult));
		}

		const auto keyByName = [](const auto& entry) {
			return ToLower(Trim(entry.skillName));
		};
		std::sort(
			report.catalogEntries.begin(),
			report.catalogEntries.end(),
			[&keyByName](
				const SkillsCatalogPromptParityCatalogEntry& left,
				const SkillsCatalogPromptParityCatalogEntry& right) {
				return keyByName(left) < keyByName(right);
			});
		std::sort(
			report.metadataEntries.begin(),
			report.metadataEntries.end(),
			[&keyByName](
				const SkillsCatalogPromptParityMetadataEntry& left,
				const SkillsCatalogPromptParityMetadataEntry& right) {
				return keyByName(left) < keyByName(right);
			});
		std::sort(
			report.eligibilityEntries.begin(),
			report.eligibilityEntries.end(),
			[&keyByName](
				const SkillsCatalogPromptParityEligibilityEntry& left,
				const SkillsCatalogPromptParityEligibilityEntry& right) {
				return keyByName(left) < keyByName(right);
			});
		std::sort(
			report.visibilityEntries.begin(),
			report.visibilityEntries.end(),
			[&keyByName](
				const SkillsCatalogPromptParityVisibilityEntry& left,
				const SkillsCatalogPromptParityVisibilityEntry& right) {
				return keyByName(left) < keyByName(right);
			});
		std::sort(
			report.compactFallbackEntries.begin(),
			report.compactFallbackEntries.end(),
			[&keyByName](
				const SkillsCatalogPromptParityCompactFallbackEntry& left,
				const SkillsCatalogPromptParityCompactFallbackEntry& right) {
				return keyByName(left) < keyByName(right);
			});
		std::sort(
			report.precedenceEntries.begin(),
			report.precedenceEntries.end(),
			[&keyByName](
				const SkillsCatalogPromptParityPrecedenceEntry& left,
				const SkillsCatalogPromptParityPrecedenceEntry& right) {
				return keyByName(left) < keyByName(right);
			});

		return report;
	}

	std::string SkillsCatalogPromptParityValidationService::BuildMarkdownReport(
		const SkillsCatalogPromptParityValidationReport& report) const {
		std::ostringstream output;
		output << "# Skills Catalog and Prompt Parity Validation Report (Phase 4)\n\n";
		output << "OpenClaw root found: " << (report.openClawBundledRootFound ? "true" : "false") << "\n";
		output << "BlazeClaw bundled root found: " << (report.blazeClawBundledRootFound ? "true" : "false") << "\n\n";

		output << "## Catalog snapshot parity (presence/name/description)\n\n";
		if (report.catalogEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& entry : report.catalogEntries) {
				output << "- skill=`" << WideToUtf8(entry.skillName) << "`"
					<< "; present=" << (entry.catalogPresent ? "true" : "false")
					<< "; nameMatch=" << (entry.nameMatches ? "true" : "false")
					<< "; descriptionMatch=" << (entry.descriptionMatches ? "true" : "false")
					<< "; source=" << WideToUtf8(entry.sourceLabel)
					<< "\n";
			}
			output << "\n";
		}

		output << "## Metadata parse parity\n\n";
		if (report.metadataEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& entry : report.metadataEntries) {
				output << "- skill=`" << WideToUtf8(entry.skillName) << "`"
					<< "; openClawParsed=" << (entry.openClawFrontmatterParsed ? "true" : "false")
					<< "; catalogFrontmatterValid=" << (entry.catalogFrontmatterValid ? "true" : "false")
					<< "; catalogMetadataPresent=" << (entry.catalogMetadataPresent ? "true" : "false")
					<< "\n";
				for (const auto& issue : entry.issues) {
					output << "  - issue: " << WideToUtf8(issue) << "\n";
				}
			}
			output << "\n";
		}

		output << "## Eligibility parity\n\n";
		if (report.eligibilityEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& entry : report.eligibilityEntries) {
				output << "- skill=`" << WideToUtf8(entry.skillName) << "`"
					<< "; expectedEligible=" << (entry.expectedEligible ? "true" : "false")
					<< "; actualEligible=" << (entry.actualEligible ? "true" : "false")
					<< "; blockedByAllowlist=" << (entry.blockedByAllowlist ? "true" : "false")
					<< "\n";
				for (const auto& issue : entry.issues) {
					output << "  - issue: " << WideToUtf8(issue) << "\n";
				}
			}
			output << "\n";
		}

		output << "## Prompt visibility / intentional hiding parity\n\n";
		if (report.visibilityEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& entry : report.visibilityEntries) {
				output << "- skill=`" << WideToUtf8(entry.skillName) << "`"
					<< "; expectedVisible=" << (entry.expectedVisible ? "true" : "false")
					<< "; visibleInPrompt=" << (entry.visibleInPrompt ? "true" : "false")
					<< "; hiddenByDisableModelInvocation="
					<< (entry.hiddenByDisableModelInvocation ? "true" : "false")
					<< "\n";
				for (const auto& issue : entry.issues) {
					output << "  - issue: " << WideToUtf8(issue) << "\n";
				}
			}
			output << "\n";
		}

		output << "## Compact fallback retention parity\n\n";
		if (report.compactFallbackEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& entry : report.compactFallbackEntries) {
				output << "- skill=`" << WideToUtf8(entry.skillName) << "`"
					<< "; inFullPrompt="
					<< (entry.includedInFullPromptSnapshot ? "true" : "false")
					<< "; retainedInCompactFallback="
					<< (entry.retainedInCompactFallbackSnapshot ? "true" : "false")
					<< "\n";
				for (const auto& issue : entry.issues) {
					output << "  - issue: " << WideToUtf8(issue) << "\n";
				}
			}
			output << "\n";
		}

		output << "## Source precedence parity\n\n";
		if (report.precedenceEntries.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& entry : report.precedenceEntries) {
				output << "- skill=`" << WideToUtf8(entry.skillName) << "`"
					<< "; winner=" << WideToUtf8(entry.winnerSourceLabel)
					<< "; deterministic=" << (entry.deterministic ? "true" : "false")
					<< "\n";
				for (const auto& candidate : entry.candidateSourceLabels) {
					output << "  - candidate: " << WideToUtf8(candidate) << "\n";
				}
				for (const auto& issue : entry.issues) {
					output << "  - issue: " << WideToUtf8(issue) << "\n";
				}
			}
			output << "\n";
		}

		output << "## Warnings\n\n";
		if (report.warnings.empty()) {
			output << "- (none)\n\n";
		}
		else {
			for (const auto& warning : report.warnings) {
				output << "- " << WideToUtf8(warning) << "\n";
			}
			output << "\n";
		}

		return output.str();
	}

	std::string SkillsCatalogPromptParityValidationService::BuildJsonReport(
		const SkillsCatalogPromptParityValidationReport& report) const {
		nlohmann::json root;
		root["workspaceRoot"] = report.workspaceRoot.generic_string();
		root["openClawBundledRoot"] = report.openClawBundledRoot.generic_string();
		root["blazeClawBundledRoot"] = report.blazeClawBundledRoot.generic_string();
		root["openClawBundledRootFound"] = report.openClawBundledRootFound;
		root["blazeClawBundledRootFound"] = report.blazeClawBundledRootFound;

		root["catalogEntries"] = nlohmann::json::array();
		for (const auto& entry : report.catalogEntries) {
			root["catalogEntries"].push_back({
				{ "skillName", WideToUtf8(entry.skillName) },
				{ "catalogPresent", entry.catalogPresent },
				{ "expectedName", WideToUtf8(entry.expectedName) },
				{ "catalogName", WideToUtf8(entry.catalogName) },
				{ "nameMatches", entry.nameMatches },
				{ "expectedDescription", WideToUtf8(entry.expectedDescription) },
				{ "catalogDescription", WideToUtf8(entry.catalogDescription) },
				{ "descriptionMatches", entry.descriptionMatches },
				{ "sourceLabel", WideToUtf8(entry.sourceLabel) },
			});
		}

		root["metadataEntries"] = nlohmann::json::array();
		for (const auto& entry : report.metadataEntries) {
			nlohmann::json issues = nlohmann::json::array();
			for (const auto& issue : entry.issues) {
				issues.push_back(WideToUtf8(issue));
			}
			root["metadataEntries"].push_back({
				{ "skillName", WideToUtf8(entry.skillName) },
				{ "openClawFrontmatterParsed", entry.openClawFrontmatterParsed },
				{ "catalogFrontmatterValid", entry.catalogFrontmatterValid },
				{ "catalogMetadataPresent", entry.catalogMetadataPresent },
				{ "issues", issues },
			});
		}

		root["eligibilityEntries"] = nlohmann::json::array();
		for (const auto& entry : report.eligibilityEntries) {
			nlohmann::json issues = nlohmann::json::array();
			for (const auto& issue : entry.issues) {
				issues.push_back(WideToUtf8(issue));
			}
			root["eligibilityEntries"].push_back({
				{ "skillName", WideToUtf8(entry.skillName) },
				{ "expectedEligible", entry.expectedEligible },
				{ "actualEligible", entry.actualEligible },
				{ "disabled", entry.disabled },
				{ "blockedByAllowlist", entry.blockedByAllowlist },
				{ "issues", issues },
			});
		}

		root["visibilityEntries"] = nlohmann::json::array();
		for (const auto& entry : report.visibilityEntries) {
			nlohmann::json issues = nlohmann::json::array();
			for (const auto& issue : entry.issues) {
				issues.push_back(WideToUtf8(issue));
			}
			root["visibilityEntries"].push_back({
				{ "skillName", WideToUtf8(entry.skillName) },
				{ "expectedVisible", entry.expectedVisible },
				{ "visibleInPrompt", entry.visibleInPrompt },
				{ "hiddenByDisableModelInvocation", entry.hiddenByDisableModelInvocation },
				{ "issues", issues },
			});
		}

		root["compactFallbackEntries"] = nlohmann::json::array();
		for (const auto& entry : report.compactFallbackEntries) {
			nlohmann::json issues = nlohmann::json::array();
			for (const auto& issue : entry.issues) {
				issues.push_back(WideToUtf8(issue));
			}
			root["compactFallbackEntries"].push_back({
				{ "skillName", WideToUtf8(entry.skillName) },
				{ "includedInFullPromptSnapshot", entry.includedInFullPromptSnapshot },
				{ "retainedInCompactFallbackSnapshot", entry.retainedInCompactFallbackSnapshot },
				{ "issues", issues },
			});
		}

		root["precedenceEntries"] = nlohmann::json::array();
		for (const auto& entry : report.precedenceEntries) {
			nlohmann::json candidates = nlohmann::json::array();
			for (const auto& candidate : entry.candidateSourceLabels) {
				candidates.push_back(WideToUtf8(candidate));
			}
			nlohmann::json issues = nlohmann::json::array();
			for (const auto& issue : entry.issues) {
				issues.push_back(WideToUtf8(issue));
			}
			root["precedenceEntries"].push_back({
				{ "skillName", WideToUtf8(entry.skillName) },
				{ "winnerSourceLabel", WideToUtf8(entry.winnerSourceLabel) },
				{ "candidateSourceLabels", candidates },
				{ "deterministic", entry.deterministic },
				{ "issues", issues },
			});
		}

		root["warnings"] = nlohmann::json::array();
		for (const auto& warning : report.warnings) {
			root["warnings"].push_back(WideToUtf8(warning));
		}

		return root.dump(2);
	}

} // namespace blazeclaw::core
