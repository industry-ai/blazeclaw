#include "pch.h"
#include "SkillsCatalogService.h"
#include "filesystem/SafeOpenSync.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <optional>
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

		std::wstring ToLower(const std::wstring& value);

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

		std::vector<std::wstring> SplitList(const std::wstring& raw) {
			std::vector<std::wstring> values;
			std::wstring current;
			current.reserve(raw.size());

			const auto flush = [&values, &current]() {
				const auto trimmed = Trim(current);
				if (!trimmed.empty()) {
					values.push_back(trimmed);
				}
				current.clear();
				};

			for (const auto ch : raw) {
				if (ch == L',' || ch == L';' || ch == L'|') {
					flush();
					continue;
				}
				current.push_back(ch);
			}

			flush();
			return values;
		}

		std::wstring GetFrontmatterField(
			const SkillFrontmatter& frontmatter,
			std::initializer_list<const wchar_t*> keys) {
			for (const auto* key : keys) {
				const auto it = frontmatter.fields.find(ToLower(Trim(key)));
				if (it != frontmatter.fields.end()) {
					return Trim(it->second);
				}
			}

			return {};
		}

		SkillsMetadataSpec BuildNormalizedSkillsMetadata(
			const SkillsCatalogEntry& entry) {
			SkillsMetadataSpec metadata;
			metadata.skillKey = GetFrontmatterField(
				entry.frontmatter,
				{ L"skillkey", L"skill-key", L"openclaw.skillkey" });
			metadata.primaryEnv = GetFrontmatterField(
				entry.frontmatter,
				{ L"primary-env", L"primary_env", L"openclaw.primary-env" });
			metadata.emoji = GetFrontmatterField(
				entry.frontmatter,
				{ L"emoji", L"openclaw.emoji" });
			metadata.homepage = GetFrontmatterField(
				entry.frontmatter,
				{ L"homepage", L"openclaw.homepage" });

			const auto alwaysRaw = GetFrontmatterField(
				entry.frontmatter,
				{ L"always", L"openclaw.always" });
			if (!alwaysRaw.empty()) {
				metadata.always = ParseBoolField(alwaysRaw, false);
			}

			metadata.os = SplitList(GetFrontmatterField(
				entry.frontmatter,
				{ L"os", L"openclaw.os" }));

			metadata.requirements.bins = SplitList(GetFrontmatterField(
				entry.frontmatter,
				{ L"requires-bins", L"requires_bins", L"requires.bins" }));
			metadata.requirements.anyBins = SplitList(GetFrontmatterField(
				entry.frontmatter,
				{ L"requires-any-bins", L"requires_any_bins", L"requires.anybins" }));
			metadata.requirements.env = SplitList(GetFrontmatterField(
				entry.frontmatter,
				{ L"requires-env", L"requires_env", L"requires.env" }));
			metadata.requirements.config = SplitList(GetFrontmatterField(
				entry.frontmatter,
				{ L"requires-config", L"requires_config", L"requires.config" }));

			const auto installKind = GetFrontmatterField(
				entry.frontmatter,
				{ L"install-kind", L"install_kind", L"install.kind" });
			if (!installKind.empty()) {
				SkillInstallSpec install;
				install.kind = installKind;
				install.id = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-id", L"install_id", L"install.id" });
				install.label = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-label", L"install_label", L"install.label" });
				install.formula = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-formula", L"install_formula", L"install.formula" });
				install.package = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-package", L"install_package", L"install.package" });
				install.module = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-module", L"install_module", L"install.module" });
				install.url = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-url", L"install_url", L"install.url" });
				install.archive = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-archive", L"install_archive", L"install.archive" });
				install.targetDir = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-target-dir", L"install_target_dir", L"install.targetDir" });
				install.bins = SplitList(GetFrontmatterField(
					entry.frontmatter,
					{ L"install-bins", L"install_bins", L"install.bins" }));
				install.os = SplitList(GetFrontmatterField(
					entry.frontmatter,
					{ L"install-os", L"install_os", L"install.os" }));

				const auto extractRaw = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-extract", L"install_extract", L"install.extract" });
				if (!extractRaw.empty()) {
					install.extract = ParseBoolField(extractRaw, false);
				}

				const auto stripRaw = GetFrontmatterField(
					entry.frontmatter,
					{ L"install-strip-components", L"install_strip_components", L"install.stripComponents" });
				if (!stripRaw.empty()) {
					try {
						install.stripComponents = static_cast<std::uint32_t>(std::stoul(stripRaw));
					}
					catch (...) {
						install.stripComponents.reset();
					}
				}

				metadata.install.push_back(std::move(install));
			}

			return metadata;
		}

		SkillInvocationPolicySpec BuildNormalizedInvocationPolicy(
			const SkillsCatalogEntry& entry) {
			SkillInvocationPolicySpec policy;
			const auto userInvocableRaw = GetFrontmatterField(
				entry.frontmatter,
				{ L"user-invocable", L"user_invocable", L"openclaw.user-invocable" });
			if (!userInvocableRaw.empty()) {
				policy.userInvocable = ParseBoolField(userInvocableRaw, true);
			}

			const auto disableModelInvocationRaw = GetFrontmatterField(
				entry.frontmatter,
				{ L"disable-model-invocation", L"disable_model_invocation", L"disablemodelinvocation" });
			if (!disableModelInvocationRaw.empty()) {
				policy.disableModelInvocation =
					ParseBoolField(disableModelInvocationRaw, false);
			}

			return policy;
		}

		SkillExposureSpec BuildNormalizedExposurePolicy(
			const SkillsCatalogEntry& entry,
			const SkillInvocationPolicySpec& invocation) {
			SkillExposureSpec exposure;
			exposure.userInvocable = invocation.userInvocable;

			const auto includeRegistryRaw = GetFrontmatterField(
				entry.frontmatter,
				{ L"include-runtime-registry", L"include_runtime_registry", L"exposure.includeInRuntimeRegistry" });
			if (!includeRegistryRaw.empty()) {
				exposure.includeInRuntimeRegistry =
					ParseBoolField(includeRegistryRaw, true);
			}

			const auto includePromptRaw = GetFrontmatterField(
				entry.frontmatter,
				{ L"include-available-skills-prompt", L"include_available_skills_prompt", L"exposure.includeInAvailableSkillsPrompt" });
			if (!includePromptRaw.empty()) {
				exposure.includeInAvailableSkillsPrompt =
					ParseBoolField(includePromptRaw, true);
			}

			return exposure;
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

		std::wstring TrimQuotes(const std::wstring& value) {
			const std::wstring trimmed = Trim(value);
			if (trimmed.size() >= 2) {
				const wchar_t first = trimmed.front();
				const wchar_t last = trimmed.back();
				if ((first == L'"' && last == L'"') ||
					(first == L'\'' && last == L'\'')) {
					return trimmed.substr(1, trimmed.size() - 2);
				}
			}

			return trimmed;
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

		SkillSourceScope ResolveSourceScope(const SkillsSourceKind sourceKind) {
			switch (sourceKind) {
			case SkillsSourceKind::Personal:
				return SkillSourceScope::User;
			case SkillsSourceKind::Project:
			case SkillsSourceKind::Workspace:
			case SkillsSourceKind::Bundled:
			case SkillsSourceKind::Managed:
			case SkillsSourceKind::Plugin:
			case SkillsSourceKind::OpenClawOriginal:
				return SkillSourceScope::Project;
			case SkillsSourceKind::Extra:
			default:
				return SkillSourceScope::Temporary;
			}
		}

		SkillSourceOrigin ResolveSourceOrigin(const SkillsSourceKind sourceKind) {
			switch (sourceKind) {
			case SkillsSourceKind::Bundled:
			case SkillsSourceKind::Plugin:
			case SkillsSourceKind::OpenClawOriginal:
				return SkillSourceOrigin::Package;
			case SkillsSourceKind::Extra:
			case SkillsSourceKind::Managed:
			case SkillsSourceKind::Personal:
			case SkillsSourceKind::Project:
			case SkillsSourceKind::Workspace:
			default:
				return SkillSourceOrigin::TopLevel;
			}
		}

		std::wstring Utf8ToWide(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			const int required = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0);
			if (required <= 0) {
				return std::wstring(value.begin(), value.end());
			}

			std::wstring output(static_cast<std::size_t>(required), L'\0');
			const int converted = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				required);

			if (converted <= 0) {
				return std::wstring(value.begin(), value.end());
			}

			return output;
		}

		std::filesystem::path ResolveRootPath(
			const std::filesystem::path& workspaceRoot,
			const std::wstring& configuredPath) {
			std::filesystem::path root(configuredPath);
			if (root.is_relative()) {
				root = workspaceRoot / root;
			}

			std::error_code ec;
			const auto absolute = std::filesystem::absolute(root, ec);
			if (ec) {
				return root.lexically_normal();
			}

			return absolute.lexically_normal();
		}

		std::filesystem::path CanonicalOrSelf(const std::filesystem::path& pathValue) {
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(pathValue, ec);
			if (ec) {
				return pathValue.lexically_normal();
			}

			return canonical;
		}

		bool IsPathInside(
			const std::filesystem::path& rootPath,
			const std::filesystem::path& candidatePath) {
			const auto canonicalRoot = CanonicalOrSelf(rootPath);
			const auto canonicalCandidate = CanonicalOrSelf(candidatePath);

			auto rootIt = canonicalRoot.begin();
			auto rootEnd = canonicalRoot.end();
			auto candidateIt = canonicalCandidate.begin();
			auto candidateEnd = canonicalCandidate.end();

			for (; rootIt != rootEnd; ++rootIt, ++candidateIt) {
				if (candidateIt == candidateEnd || *rootIt != *candidateIt) {
					return false;
				}
			}

			return true;
		}

		std::filesystem::path ResolveNestedSkillsRoot(
			const std::filesystem::path& root,
			const std::uint32_t maxCandidatesPerRoot) {
			const auto nested = root / L"skills";
			std::error_code ec;
			if (!std::filesystem::is_directory(nested, ec)) {
				return root;
			}

			std::uint32_t scanned = 0;
			for (const auto& item : std::filesystem::directory_iterator(nested, ec)) {
				if (ec) {
					break;
				}

				if (scanned >= maxCandidatesPerRoot) {
					break;
				}

				++scanned;
				if (!item.is_directory()) {
					continue;
				}

				const auto skillFile = item.path() / L"SKILL.md";
				std::error_code skillEc;
				if (std::filesystem::is_regular_file(skillFile, skillEc) && !skillEc) {
					return nested;
				}
			}

			return root;
		}

		std::vector<std::filesystem::path> CollectCandidateSkillDirs(
			const std::filesystem::path& root,
			const std::uint32_t maxCandidatesPerRoot) {
			std::vector<std::filesystem::path> candidates;

			std::error_code ec;
			const auto rootSkillFile = root / L"SKILL.md";
			if (std::filesystem::is_regular_file(rootSkillFile, ec) && !ec) {
				candidates.push_back(root);
				return candidates;
			}

			for (const auto& item : std::filesystem::directory_iterator(root, ec)) {
				if (ec) {
					break;
				}

				if (!item.is_directory()) {
					continue;
				}

				const auto name = item.path().filename().wstring();
				if (!name.empty() && name.front() == L'.') {
					continue;
				}

				const auto skillFile = item.path() / L"SKILL.md";
				std::error_code skillEc;
				if (std::filesystem::is_regular_file(skillFile, skillEc) && !skillEc) {
					candidates.push_back(item.path());
				}
			}

			std::sort(candidates.begin(), candidates.end());
			if (candidates.size() > maxCandidatesPerRoot) {
				candidates.resize(maxCandidatesPerRoot);
			}

			return candidates;
		}

		struct VerifiedSkillFileReadResult {
			bool ok = false;
			bool rejectedBySymlinkPolicy = false;
			bool rejectedByMaxBytesPolicy = false;
			std::wstring detail;
			std::wstring content;
		};

		VerifiedSkillFileReadResult ReadSkillFileVerified(
			const std::filesystem::path& rootRealPath,
			const std::filesystem::path& skillFile,
			const bool rejectPathSymlink,
			const std::uint64_t maxBytes,
			SkillsCatalogDiagnostics& outDiagnostics) {
			std::error_code ec;
			const auto canonicalFile = std::filesystem::weakly_canonical(skillFile, ec);
			if (ec) {
				return VerifiedSkillFileReadResult{
					.ok = false,
					.detail = L"canonicalize-failed",
				};
			}

			if (!IsPathInside(rootRealPath, canonicalFile)) {
				return VerifiedSkillFileReadResult{
					.ok = false,
					.detail = L"outside-root",
				};
			}

			blazeclaw::core::filesystem::VerifiedOpenRequest request;
			request.filePath = skillFile;
			request.resolvedPath = canonicalFile;
			request.policy.rejectPathSymlink = rejectPathSymlink;
			request.policy.maxBytes = maxBytes;
			request.policy.allowedType =
				blazeclaw::core::filesystem::VerifiedOpenAllowedType::File;

			const auto verifiedOpenResult =
				blazeclaw::core::filesystem::OpenVerifiedFileUtf8Sync(request);
			if (!verifiedOpenResult.ok) {
				if (verifiedOpenResult.reason ==
					blazeclaw::core::filesystem::VerifiedOpenFailureReason::Path) {
					outDiagnostics.verifiedOpenPathFailures += 1;
				}
				else if (verifiedOpenResult.reason ==
					blazeclaw::core::filesystem::VerifiedOpenFailureReason::Validation) {
					outDiagnostics.verifiedOpenValidationFailures += 1;
				}
				else {
					outDiagnostics.verifiedOpenIoFailures += 1;
				}

				return VerifiedSkillFileReadResult{
					.ok = false,
					.rejectedBySymlinkPolicy =
						verifiedOpenResult.rejectedBySymlinkPolicy,
					.rejectedByMaxBytesPolicy =
						verifiedOpenResult.detail.find(L"size-rejected") !=
						std::wstring::npos,
					.detail = verifiedOpenResult.detail,
				};
			}

			return VerifiedSkillFileReadResult{
				.ok = true,
				.content = verifiedOpenResult.utf8Content,
			};
		}

		bool TryGetHomeDir(std::filesystem::path& outHomeDir) {
			wchar_t* homeValue = nullptr;
			std::size_t homeLength = 0;
			if (_wdupenv_s(&homeValue, &homeLength, L"USERPROFILE") == 0 &&
				homeValue != nullptr &&
				homeLength > 0) {
				outHomeDir = std::filesystem::path(homeValue);
				free(homeValue);
				return true;
			}

			if (homeValue != nullptr) {
				free(homeValue);
			}

			if (_wdupenv_s(&homeValue, &homeLength, L"HOME") == 0 &&
				homeValue != nullptr &&
				homeLength > 0) {
				outHomeDir = std::filesystem::path(homeValue);
				free(homeValue);
				return true;
			}

			if (homeValue != nullptr) {
				free(homeValue);
			}

			return false;
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

			const std::wstring result(value);
			free(value);
			return result;
		}

		std::filesystem::path ResolveDefaultSkillsRoot(
			const std::filesystem::path& directPath,
			const std::filesystem::path& nestedPath) {
			std::error_code ec;
			if (std::filesystem::is_directory(directPath, ec) && !ec) {
				return directPath;
			}

			ec.clear();
			if (std::filesystem::is_directory(nestedPath, ec) && !ec) {
				return nestedPath;
			}

			return directPath;
		}

	} // namespace

	SkillsLoaderPolicy SkillsCatalogService::ResolveLoaderPolicy(
		const blazeclaw::config::AppConfig& appConfig) {
		return SkillsLoaderPolicy{
			.rejectPathSymlink = appConfig.skills.load.rejectPathSymlink,
			.strictFrontmatter = appConfig.skills.load.strictFrontmatter,
		};
	}

	std::wstring SkillsCatalogService::SourceKindLabel(const SkillsSourceKind kind) {
		switch (kind) {
		case SkillsSourceKind::Extra:
			return L"extra";
		case SkillsSourceKind::Plugin:
			return L"plugin";
		case SkillsSourceKind::Bundled:
			return L"bundled";
		case SkillsSourceKind::Managed:
			return L"managed";
		case SkillsSourceKind::Personal:
			return L"personal";
		case SkillsSourceKind::Project:
			return L"project";
		case SkillsSourceKind::Workspace:
			return L"workspace";
		case SkillsSourceKind::OpenClawOriginal:
			return L"openclaw-original";
		default:
			return L"unknown";
		}
	}

	bool SkillsCatalogService::ValidateFixtureScenarios(
		const std::filesystem::path& fixturesRoot,
		std::wstring& outError) const {
		outError.clear();

		const auto precedenceWorkspace =
			fixturesRoot / L"precedence" / L"workspace";
		blazeclaw::config::AppConfig precedenceConfig;
		precedenceConfig.skills.load.extraDirs = { L"../extra" };
		precedenceConfig.skills.limits.maxCandidatesPerRoot = 32;
		precedenceConfig.skills.limits.maxSkillsLoadedPerSource = 32;
		precedenceConfig.skills.limits.maxSkillFileBytes = 32 * 1024;

		const auto precedenceSnapshot =
			LoadCatalog(precedenceWorkspace, precedenceConfig);
		const auto precedenceEntry = std::find_if(
			precedenceSnapshot.entries.begin(),
			precedenceSnapshot.entries.end(),
			[](const SkillsCatalogEntry& entry) {
				return ToLower(Trim(entry.skillName)) == L"demo-skill";
			});
		if (precedenceEntry == precedenceSnapshot.entries.end()) {
			outError =
				L"Fixture validation failed: precedence scenario missing demo-skill.";
			return false;
		}

		if (ToLower(Trim(precedenceEntry->description)) !=
			L"workspace takes precedence") {
			outError =
				L"Fixture validation failed: expected workspace precedence for demo-skill.";
			return false;
		}

		const auto invalidWorkspace =
			fixturesRoot / L"invalid-size" / L"workspace";
		blazeclaw::config::AppConfig invalidConfig;
		invalidConfig.skills.limits.maxCandidatesPerRoot = 32;
		invalidConfig.skills.limits.maxSkillsLoadedPerSource = 32;
		invalidConfig.skills.limits.maxSkillFileBytes = 220;

		const auto invalidSnapshot =
			LoadCatalog(invalidWorkspace, invalidConfig);
		if (invalidSnapshot.diagnostics.invalidFrontmatterFiles == 0) {
			outError =
				L"Fixture validation failed: expected invalid frontmatter diagnostics.";
			return false;
		}

		if (invalidSnapshot.diagnostics.oversizedSkillFiles == 0) {
			outError =
				L"Fixture validation failed: expected oversized SKILL.md diagnostics.";
			return false;
		}

		const auto hasInvalidEntry = std::any_of(
			invalidSnapshot.entries.begin(),
			invalidSnapshot.entries.end(),
			[](const SkillsCatalogEntry& entry) {
				return ToLower(Trim(entry.skillName)) == L"invalid-frontmatter" &&
					!entry.validFrontmatter;
			});
		if (!hasInvalidEntry) {
			outError =
				L"Fixture validation failed: invalid-frontmatter entry diagnostics missing.";
			return false;
		}

		const auto hasOversizedEntry = std::any_of(
			invalidSnapshot.entries.begin(),
			invalidSnapshot.entries.end(),
			[](const SkillsCatalogEntry& entry) {
				return ToLower(Trim(entry.skillName)) == L"oversized-fixture";
			});
		if (hasOversizedEntry) {
			outError =
				L"Fixture validation failed: oversized fixture should be excluded from catalog.";
			return false;
		}

		const auto selfEvolvingWorkspace =
			fixturesRoot / L"s7-self-evolving" / L"workspace";
		blazeclaw::config::AppConfig selfEvolvingConfig;
		selfEvolvingConfig.skills.limits.maxCandidatesPerRoot = 32;
		selfEvolvingConfig.skills.limits.maxSkillsLoadedPerSource = 32;
		selfEvolvingConfig.skills.limits.maxSkillFileBytes = 32 * 1024;

		const auto selfEvolvingSnapshot =
			LoadCatalog(selfEvolvingWorkspace, selfEvolvingConfig);
		const auto selfEvolvingEntry = std::find_if(
			selfEvolvingSnapshot.entries.begin(),
			selfEvolvingSnapshot.entries.end(),
			[](const SkillsCatalogEntry& entry) {
				return ToLower(Trim(entry.skillName)) == L"self-evolving";
			});
		if (selfEvolvingEntry == selfEvolvingSnapshot.entries.end()) {
			outError =
				L"Fixture validation failed: expected self-evolving skill from nested blazeclaw/skills path.";
			return false;
		}

		if (!selfEvolvingEntry->validFrontmatter) {
			outError =
				L"Fixture validation failed: self-evolving skill frontmatter should be valid.";
			return false;
		}

		if (ToLower(Trim(selfEvolvingEntry->description)).empty()) {
			outError =
				L"Fixture validation failed: self-evolving skill description should be parsed.";
			return false;
		}

		const auto pluginWorkspace =
			fixturesRoot / L"s10-plugin" / L"workspace";
		const auto pluginSkillDir =
			fixturesRoot / L"s10-plugin" / L"plugin-skills";
		const auto pluginEnvValue = pluginSkillDir.wstring();
		_wputenv_s(L"BLAZECLAW_PLUGIN_SKILL_DIRS", pluginEnvValue.c_str());

		blazeclaw::config::AppConfig pluginConfig;
		pluginConfig.skills.limits.maxCandidatesPerRoot = 32;
		pluginConfig.skills.limits.maxSkillsLoadedPerSource = 32;
		pluginConfig.skills.limits.maxSkillFileBytes = 32 * 1024;

		const auto pluginSnapshot =
			LoadCatalog(pluginWorkspace, pluginConfig);
		_wputenv_s(L"BLAZECLAW_PLUGIN_SKILL_DIRS", L"");

		const auto pluginEntry = std::find_if(
			pluginSnapshot.entries.begin(),
			pluginSnapshot.entries.end(),
			[](const SkillsCatalogEntry& entry) {
				return ToLower(Trim(entry.skillName)) == L"plugin-added";
			});
		if (pluginEntry == pluginSnapshot.entries.end()) {
			outError =
				L"Fixture validation failed: expected plugin-contributed skill directory to be discovered.";
			return false;
		}

		const auto symlinkFixtureWorkspace =
			fixturesRoot / L"s11-local-loader" / L"workspace";
		blazeclaw::config::AppConfig symlinkConfig;
		symlinkConfig.skills.limits.maxCandidatesPerRoot = 32;
		symlinkConfig.skills.limits.maxSkillsLoadedPerSource = 32;
		symlinkConfig.skills.limits.maxSkillFileBytes = 32 * 1024;
		symlinkConfig.skills.load.rejectPathSymlink = true;

		const auto symlinkSnapshot = LoadCatalog(symlinkFixtureWorkspace, symlinkConfig);
		if (symlinkSnapshot.diagnostics.symlinkRejectedFiles == 0) {
			outError =
				L"Fixture validation failed: expected symlink-rejected SKILL.md diagnostics in local-loader parity fixture.";
			return false;
		}
		const auto boundaryEscapeEntry = std::find_if(
			symlinkSnapshot.entries.begin(),
			symlinkSnapshot.entries.end(),
			[](const SkillsCatalogEntry& entry) {
				return ToLower(Trim(entry.skillName)) == L"outside";
			});
		if (boundaryEscapeEntry != symlinkSnapshot.entries.end()) {
			outError =
				L"Fixture validation failed: boundary-escape junction target should not be loadable.";
			return false;
		}

		blazeclaw::config::AppConfig strictOffConfig = symlinkConfig;
		strictOffConfig.skills.load.strictFrontmatter = false;
		const auto strictOffSnapshot = LoadCatalog(symlinkFixtureWorkspace, strictOffConfig);
		const auto strictOffInvalidEntry = std::find_if(
			strictOffSnapshot.entries.begin(),
			strictOffSnapshot.entries.end(),
			[](const SkillsCatalogEntry& entry) {
				return ToLower(Trim(entry.skillName)) == L"strict-invalid";
			});
		if (strictOffInvalidEntry == strictOffSnapshot.entries.end()) {
			outError =
				L"Fixture validation failed: strictFrontmatter=false should keep invalid entry with diagnostics.";
			return false;
		}

		blazeclaw::config::AppConfig strictOnConfig = symlinkConfig;
		strictOnConfig.skills.load.strictFrontmatter = true;
		const auto strictOnSnapshot = LoadCatalog(symlinkFixtureWorkspace, strictOnConfig);
		const auto strictOnInvalidEntry = std::find_if(
			strictOnSnapshot.entries.begin(),
			strictOnSnapshot.entries.end(),
			[](const SkillsCatalogEntry& entry) {
				return ToLower(Trim(entry.skillName)) == L"strict-invalid";
			});
		if (strictOnInvalidEntry != strictOnSnapshot.entries.end()) {
			outError =
				L"Fixture validation failed: strictFrontmatter=true should omit invalid frontmatter entries.";
			return false;
		}
		if (strictOnSnapshot.diagnostics.strictFrontmatterOmittedFiles == 0) {
			outError =
				L"Fixture validation failed: strictFrontmatter=true should increment strict omission diagnostics counter.";
			return false;
		}

		return true;
	}

	std::optional<SkillFrontmatter> SkillsCatalogService::ParseFrontmatter(
		const std::wstring& skillContent,
		std::vector<std::wstring>& outValidationErrors) {
		std::vector<std::wstring> lines;
		std::size_t cursor = 0;
		while (cursor <= skillContent.size()) {
			const auto next = skillContent.find(L'\n', cursor);
			if (next == std::wstring::npos) {
				lines.push_back(skillContent.substr(cursor));
				break;
			}

			lines.push_back(skillContent.substr(cursor, next - cursor));
			cursor = next + 1;
		}

		if (lines.empty() || Trim(lines[0]) != L"---") {
			outValidationErrors.push_back(L"Missing frontmatter start marker (---).");
			return std::nullopt;
		}

		SkillFrontmatter parsed;
		std::size_t lineIndex = 1;
		bool closed = false;
		for (; lineIndex < lines.size(); ++lineIndex) {
			const std::wstring line = Trim(lines[lineIndex]);
			if (line == L"---") {
				closed = true;
				++lineIndex;
				break;
			}

			if (line.empty() || line.starts_with(L"#")) {
				continue;
			}

			const auto colonPos = line.find(L':');
			if (colonPos == std::wstring::npos) {
				outValidationErrors.push_back(
					L"Invalid frontmatter line: " + line);
				continue;
			}

			const std::wstring key = ToLower(Trim(line.substr(0, colonPos)));
			const std::wstring value = TrimQuotes(line.substr(colonPos + 1));
			if (key.empty()) {
				outValidationErrors.push_back(L"Empty frontmatter key detected.");
				continue;
			}

			parsed.fields[key] = value;
			if (key == L"name") {
				parsed.name = value;
			}
			else if (key == L"description") {
				parsed.description = value;
			}
		}

		if (!closed) {
			outValidationErrors.push_back(L"Missing frontmatter closing marker (---).");
		}

		if (Trim(parsed.name).empty()) {
			outValidationErrors.push_back(L"Missing required frontmatter field: name.");
		}

		if (Trim(parsed.description).empty()) {
			outValidationErrors.push_back(L"Missing required frontmatter field: description.");
		}

		if (!outValidationErrors.empty()) {
			return std::nullopt;
		}

		return parsed;
	}

	std::vector<SkillsSourceRoot> SkillsCatalogService::BuildSourceRoots(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& appConfig) const {
		std::vector<SkillsSourceRoot> roots;

		for (const auto& extraDir : appConfig.skills.load.extraDirs) {
			if (Trim(extraDir).empty()) {
				continue;
			}

			roots.push_back({
				.kind = SkillsSourceKind::Extra,
				.precedence = 0,
				.configuredRoot = std::filesystem::path(extraDir),
				.resolvedRoot = ResolveRootPath(workspaceRoot, extraDir),
				});
		}

		const auto pluginSkillDirs = ReadEnvVar(L"BLAZECLAW_PLUGIN_SKILL_DIRS");
		if (pluginSkillDirs.has_value()) {
			for (const auto& pluginDir : ParseEnvPathList(pluginSkillDirs.value())) {
				roots.push_back({
					.kind = SkillsSourceKind::Plugin,
					.precedence = 0,
					.configuredRoot = std::filesystem::path(pluginDir),
					.resolvedRoot = ResolveRootPath(workspaceRoot, pluginDir),
					});
			}
		}

		const auto bundledOverride = ReadEnvVar(L"BLAZECLAW_BUNDLED_SKILLS_DIR");
		const std::filesystem::path bundledRoot = bundledOverride.has_value()
			? ResolveRootPath(workspaceRoot, bundledOverride.value())
			: ResolveDefaultSkillsRoot(
				workspaceRoot / L"skills-bundled",
				workspaceRoot / L"blazeclaw" / L"skills-bundled");
		roots.push_back({
			.kind = SkillsSourceKind::Bundled,
			.precedence = 1,
			.configuredRoot = bundledRoot,
			.resolvedRoot = CanonicalOrSelf(bundledRoot),
			});

		const auto managedOverride = ReadEnvVar(L"BLAZECLAW_MANAGED_SKILLS_DIR");
		const std::filesystem::path managedRoot = managedOverride.has_value()
			? ResolveRootPath(workspaceRoot, managedOverride.value())
			: (workspaceRoot / L".blazeclaw" / L"skills");
		roots.push_back({
			.kind = SkillsSourceKind::Managed,
			.precedence = 2,
			.configuredRoot = managedRoot,
			.resolvedRoot = CanonicalOrSelf(managedRoot),
			});

		std::filesystem::path homeDir;
		if (TryGetHomeDir(homeDir)) {
			const auto personalRoot = homeDir / L".agents" / L"skills";
			roots.push_back({
				.kind = SkillsSourceKind::Personal,
				.precedence = 3,
				.configuredRoot = personalRoot,
				.resolvedRoot = CanonicalOrSelf(personalRoot),
				});
		}

		const auto projectRoot = workspaceRoot / L".agents" / L"skills";
		roots.push_back({
			.kind = SkillsSourceKind::Project,
			.precedence = 4,
			.configuredRoot = projectRoot,
			.resolvedRoot = CanonicalOrSelf(projectRoot),
			});

		const auto workspaceSkillsRoot = ResolveDefaultSkillsRoot(
			workspaceRoot / L"skills",
			workspaceRoot / L"blazeclaw" / L"skills");
		roots.push_back({
			.kind = SkillsSourceKind::Workspace,
			.precedence = 5,
			.configuredRoot = workspaceSkillsRoot,
			.resolvedRoot = CanonicalOrSelf(workspaceSkillsRoot),
			});

		const auto openClawOriginalOverride =
			ReadEnvVar(L"BLAZECLAW_OPENCLAW_ORIGINAL_SKILLS_DIR");
		const std::filesystem::path openClawOriginalRoot =
			openClawOriginalOverride.has_value()
			? ResolveRootPath(workspaceRoot, openClawOriginalOverride.value())
			: ResolveDefaultSkillsRoot(
				workspaceRoot / L"skills-openclaw-original",
				workspaceRoot / L"blazeclaw" / L"skills-openclaw-original");
		roots.push_back({
			.kind = SkillsSourceKind::OpenClawOriginal,
			.precedence = 6,
			.configuredRoot = openClawOriginalRoot,
			.resolvedRoot = CanonicalOrSelf(openClawOriginalRoot),
			});

		std::set<std::filesystem::path> seenRoots;
		std::vector<SkillsSourceRoot> uniqueRoots;
		for (const auto& root : roots) {
			const auto normalized = root.resolvedRoot.lexically_normal();
			if (seenRoots.insert(normalized).second) {
				uniqueRoots.push_back(root);
			}
		}

		return uniqueRoots;
	}

	SkillsCatalogSnapshot SkillsCatalogService::LoadCatalog(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsCatalogSnapshot snapshot;
		const auto loaderPolicy = ResolveLoaderPolicy(appConfig);
		snapshot.diagnostics.loaderPolicyRejectPathSymlinkCount =
			loaderPolicy.rejectPathSymlink ? 1u : 0u;
		snapshot.diagnostics.loaderPolicyStrictFrontmatterCount =
			loaderPolicy.strictFrontmatter ? 1u : 0u;

		const auto sourceRoots = BuildSourceRoots(workspaceRoot, appConfig);
		snapshot.diagnostics.pluginRootsConfigured = static_cast<std::uint32_t>(
			std::count_if(
				sourceRoots.begin(),
				sourceRoots.end(),
				[](const SkillsSourceRoot& root) {
					return root.kind == SkillsSourceKind::Plugin;
				}));
		std::unordered_map<std::wstring, std::size_t> byName;

		for (const auto& sourceRoot : sourceRoots) {
			std::error_code rootEc;
			const auto rootPath = CanonicalOrSelf(sourceRoot.resolvedRoot);
			if (!std::filesystem::is_directory(rootPath, rootEc) || rootEc) {
				++snapshot.diagnostics.rootsSkipped;
				continue;
			}

			++snapshot.diagnostics.rootsScanned;
			if (sourceRoot.kind == SkillsSourceKind::Plugin) {
				++snapshot.diagnostics.pluginRootsScanned;
			}

			const auto discoveryRoot = ResolveNestedSkillsRoot(
				rootPath,
				appConfig.skills.limits.maxCandidatesPerRoot);
			const auto discoveryRootCanonical = CanonicalOrSelf(discoveryRoot);
			const auto candidates = CollectCandidateSkillDirs(
				discoveryRoot,
				appConfig.skills.limits.maxCandidatesPerRoot);
			std::size_t loadedInSource = 0;

			for (const auto& skillDir : candidates) {
				if (!IsPathInside(discoveryRoot, skillDir)) {
					snapshot.diagnostics.warnings.push_back(
						L"Skipped skill directory outside discovery root: " +
						skillDir.wstring());
					continue;
				}

				const auto skillFile = skillDir / L"SKILL.md";
				if (!IsPathInside(discoveryRoot, skillFile)) {
					snapshot.diagnostics.warnings.push_back(
						L"Skipped SKILL.md outside discovery root: " +
						skillFile.wstring());
					continue;
				}

				const auto verifiedRead = ReadSkillFileVerified(
					discoveryRootCanonical,
					skillFile,
					loaderPolicy.rejectPathSymlink,
					static_cast<std::uint64_t>(
						appConfig.skills.limits.maxSkillFileBytes),
					snapshot.diagnostics);
				if (!verifiedRead.ok) {
					if (verifiedRead.rejectedBySymlinkPolicy) {
						++snapshot.diagnostics.symlinkRejectedFiles;
					}
					if (verifiedRead.rejectedByMaxBytesPolicy) {
						++snapshot.diagnostics.oversizedSkillFiles;
					}
					snapshot.diagnostics.warnings.push_back(
						L"Failed verified SKILL.md read (" + verifiedRead.detail + L"): " +
						skillFile.wstring());
					continue;
				}

				SkillsCatalogEntry entry;
				entry.skillDir = CanonicalOrSelf(skillDir);
				entry.skillFile = CanonicalOrSelf(skillFile);
				entry.sourceKind = sourceRoot.kind;
				entry.precedence = sourceRoot.precedence;
				entry.sourceInfo = CreateSyntheticSkillSourceInfoCompat(
					entry.skillFile,
					SkillsCatalogService::SourceKindLabel(sourceRoot.kind),
					ResolveSourceScope(sourceRoot.kind),
					ResolveSourceOrigin(sourceRoot.kind),
					entry.skillDir);

				std::vector<std::wstring> validationErrors;
				const auto frontmatter = ParseFrontmatter(verifiedRead.content, validationErrors);
				if (frontmatter.has_value()) {
					entry.frontmatter = frontmatter.value();
					entry.skillName = frontmatter->name;
					entry.description = frontmatter->description;
					entry.validFrontmatter = true;
					entry.metadata = BuildNormalizedSkillsMetadata(entry);
					entry.invocation = BuildNormalizedInvocationPolicy(entry);
					entry.exposure = BuildNormalizedExposurePolicy(
						entry,
						entry.invocation.value());
				}
				else {
					if (loaderPolicy.strictFrontmatter) {
						++snapshot.diagnostics.invalidFrontmatterFiles;
						++snapshot.diagnostics.strictFrontmatterOmittedFiles;
						snapshot.diagnostics.warnings.push_back(
							L"Omitted invalid frontmatter entry in strict mode: " +
							skillDir.wstring());
						continue;
					}

					entry.skillName = skillDir.filename().wstring();
					entry.description = L"";
					entry.validFrontmatter = false;
					entry.validationErrors = validationErrors;
					entry.metadata.reset();
					entry.invocation.reset();
					entry.exposure.reset();
					++snapshot.diagnostics.invalidFrontmatterFiles;
				}

				const std::wstring key = ToLower(Trim(entry.skillName));
				if (key.empty()) {
					snapshot.diagnostics.warnings.push_back(
						L"Skipped unnamed skill entry at: " + skillDir.wstring());
					continue;
				}

				auto existing = byName.find(key);
				if (existing == byName.end()) {
					snapshot.entries.push_back(entry);
					byName.emplace(key, snapshot.entries.size() - 1);
				}
				else {
					snapshot.entries[existing->second] = entry;
				}

				++loadedInSource;

				if (loadedInSource >=
					appConfig.skills.limits.maxSkillsLoadedPerSource) {
					break;
				}
			}
		}

		std::sort(
			snapshot.entries.begin(),
			snapshot.entries.end(),
			[](const SkillsCatalogEntry& left, const SkillsCatalogEntry& right) {
				const auto leftKey = ToLower(left.skillName);
				const auto rightKey = ToLower(right.skillName);
				if (leftKey == rightKey) {
					return left.precedence < right.precedence;
				}

				return leftKey < rightKey;
			});

		return snapshot;
	}

} // namespace blazeclaw::core
