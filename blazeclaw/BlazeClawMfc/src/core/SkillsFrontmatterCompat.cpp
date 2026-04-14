#include "pch.h"
#include "SkillsFrontmatterCompat.h"
#include "MarkdownFrontmatterCompat.h"
#include "SharedFrontmatterCompat.h"

#include <algorithm>
#include <cwctype>
#include <regex>
#include <nlohmann/json.hpp>

namespace blazeclaw::core {

	namespace {

		std::optional<std::wstring> ReadJsonStringCompat(
			const nlohmann::json& value,
			const char* key) {
			if (!value.is_object() ||
				!value.contains(key) ||
				!value[key].is_string()) {
				return std::nullopt;
			}

			return value[key].get<std::wstring>();
		}

		const std::wregex kBrewFormulaPattern(
			L"^[A-Za-z0-9][A-Za-z0-9@+._/-]*$");
		const std::wregex kGoModulePattern(
			L"^[A-Za-z0-9][A-Za-z0-9._~+\\-/]*(?:@[A-Za-z0-9][A-Za-z0-9._~+\\-/]*)?$");
		const std::wregex kUvPackagePattern(
			LR"(^[A-Za-z0-9][A-Za-z0-9._\-[\]=<>!~+,]*$)");

		std::wstring TrimCompat(const std::wstring& value) {
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

		std::wstring ToLowerCompat(const std::wstring& value) {
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

		std::wstring TrimQuotesCompat(const std::wstring& value) {
			const std::wstring trimmed = TrimCompat(value);
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

		bool ParseBoolFieldCompat(const std::wstring& value, const bool fallback) {
			return ParseFrontmatterBoolCompat(value, fallback);
		}

		std::vector<std::wstring> SplitListCompat(const std::wstring& raw) {
			return NormalizeStringListCompat(nlohmann::json(raw));
		}

		std::optional<std::wstring> NormalizeSafeBrewFormulaCompat(
			const std::wstring& raw) {
			const std::wstring formula = TrimCompat(raw);
			if (formula.empty() ||
				formula.starts_with(L"-") ||
				formula.find(L"\\") != std::wstring::npos ||
				formula.find(L"..") != std::wstring::npos) {
				return std::nullopt;
			}

			if (!std::regex_match(formula, kBrewFormulaPattern)) {
				return std::nullopt;
			}

			return formula;
		}

		std::optional<std::wstring> NormalizeSafeNpmSpecCompat(
			const std::wstring& raw) {
			const std::wstring spec = TrimCompat(raw);
			if (spec.empty() || spec.starts_with(L"-")) {
				return std::nullopt;
			}

			if (spec.find_first_of(L"\r\n\t") != std::wstring::npos) {
				return std::nullopt;
			}

			return spec;
		}

		std::optional<std::wstring> NormalizeSafeGoModuleCompat(
			const std::wstring& raw) {
			const std::wstring moduleSpec = TrimCompat(raw);
			if (moduleSpec.empty() ||
				moduleSpec.starts_with(L"-") ||
				moduleSpec.find(L"\\") != std::wstring::npos ||
				moduleSpec.find(L"://") != std::wstring::npos) {
				return std::nullopt;
			}

			if (!std::regex_match(moduleSpec, kGoModulePattern)) {
				return std::nullopt;
			}

			return moduleSpec;
		}

		std::optional<std::wstring> NormalizeSafeUvPackageCompat(
			const std::wstring& raw) {
			const std::wstring package = TrimCompat(raw);
			if (package.empty() ||
				package.starts_with(L"-") ||
				package.find(L"\\") != std::wstring::npos ||
				package.find(L"://") != std::wstring::npos) {
				return std::nullopt;
			}

			if (!std::regex_match(package, kUvPackagePattern)) {
				return std::nullopt;
			}

			return package;
		}

		std::optional<std::wstring> NormalizeSafeDownloadUrlCompat(
			const std::wstring& raw) {
			const std::wstring url = TrimCompat(raw);
			if (url.empty() ||
				url.find_first_of(L" \t\r\n") != std::wstring::npos) {
				return std::nullopt;
			}

			const std::wstring lowerUrl = ToLowerCompat(url);
			if (!lowerUrl.starts_with(L"http://") &&
				!lowerUrl.starts_with(L"https://")) {
				return std::nullopt;
			}

			return url;
		}

		std::optional<SkillsMetadataSpec> ResolveOpenClawMetadataFromManifestCompat(
			const ParsedSkillFrontmatterCompat& frontmatter) {
			const auto manifest = ResolveOpenClawManifestBlockCompat(
				frontmatter.fields,
				L"metadata");
			if (!manifest.has_value()) {
				return std::nullopt;
			}

			SkillsMetadataSpec metadata;
			if (manifest->contains("always") && (*manifest)["always"].is_boolean()) {
				metadata.always = (*manifest)["always"].get<bool>();
			}

			if (const auto skillKey = ReadJsonStringCompat(*manifest, "skillKey");
				skillKey.has_value()) {
				metadata.skillKey = skillKey.value();
			}
			if (const auto primaryEnv = ReadJsonStringCompat(*manifest, "primaryEnv");
				primaryEnv.has_value()) {
				metadata.primaryEnv = primaryEnv.value();
			}
			if (const auto emoji = ReadJsonStringCompat(*manifest, "emoji");
				emoji.has_value()) {
				metadata.emoji = emoji.value();
			}
			if (const auto homepage = ReadJsonStringCompat(*manifest, "homepage");
				homepage.has_value()) {
				metadata.homepage = homepage.value();
			}

			metadata.os = ResolveOpenClawManifestOsCompat(*manifest);
			if (const auto requiresResult = ResolveOpenClawManifestRequiresCompat(*manifest);
				requiresResult.has_value()) {
				metadata.requirements.bins = requiresResult->bins;
				metadata.requirements.anyBins = requiresResult->anyBins;
				metadata.requirements.env = requiresResult->env;
				metadata.requirements.config = requiresResult->config;
			}

			const auto installs = ResolveOpenClawManifestInstallCompat(*manifest);
			for (const auto& installEntry : installs) {
				const auto parsedBase = ParseOpenClawManifestInstallBaseCompat(
					installEntry,
					std::vector<std::wstring>{
					L"brew",
						L"node",
						L"go",
						L"uv",
						L"download",
				});
				if (!parsedBase.has_value()) {
					continue;
				}

				SkillInstallSpec install;
				install.kind = parsedBase->kind;
				ApplyOpenClawManifestInstallCommonFieldsCompat(
					install.id,
					install.label,
					install.bins,
					parsedBase.value());

				if (installEntry.contains("os")) {
					install.os = NormalizeStringListCompat(installEntry["os"]);
				}

				if (const auto formulaRaw = ReadJsonStringCompat(installEntry, "formula");
					formulaRaw.has_value()) {
					const auto formula = NormalizeSafeBrewFormulaCompat(formulaRaw.value());
					if (formula.has_value()) {
						install.formula = formula.value();
					}
				}
				if (install.formula.empty()) {
					if (const auto caskRaw = ReadJsonStringCompat(installEntry, "cask");
						caskRaw.has_value()) {
						const auto cask = NormalizeSafeBrewFormulaCompat(caskRaw.value());
						if (cask.has_value()) {
							install.formula = cask.value();
						}
					}
				}

				if (const auto packageRaw = ReadJsonStringCompat(installEntry, "package");
					packageRaw.has_value()) {
					if (ToLowerCompat(install.kind) == L"node") {
						const auto npm = NormalizeSafeNpmSpecCompat(packageRaw.value());
						if (npm.has_value()) {
							install.package = npm.value();
						}
					}
					else if (ToLowerCompat(install.kind) == L"uv") {
						const auto uv = NormalizeSafeUvPackageCompat(packageRaw.value());
						if (uv.has_value()) {
							install.package = uv.value();
						}
					}
				}

				if (const auto moduleRaw = ReadJsonStringCompat(installEntry, "module");
					moduleRaw.has_value()) {
					const auto module = NormalizeSafeGoModuleCompat(moduleRaw.value());
					if (module.has_value()) {
						install.module = module.value();
					}
				}

				if (const auto urlRaw = ReadJsonStringCompat(installEntry, "url");
					urlRaw.has_value()) {
					const auto url = NormalizeSafeDownloadUrlCompat(urlRaw.value());
					if (url.has_value()) {
						install.url = url.value();
					}
				}

				if (const auto archive = ReadJsonStringCompat(installEntry, "archive");
					archive.has_value()) {
					install.archive = archive.value();
				}
				if (const auto targetDir = ReadJsonStringCompat(installEntry, "targetDir");
					targetDir.has_value()) {
					install.targetDir = targetDir.value();
				}
				if (installEntry.contains("extract") && installEntry["extract"].is_boolean()) {
					install.extract = installEntry["extract"].get<bool>();
				}
				if (installEntry.contains("stripComponents") &&
					installEntry["stripComponents"].is_number_unsigned()) {
					install.stripComponents =
						static_cast<std::uint32_t>(installEntry["stripComponents"].get<std::uint64_t>());
				}

				const std::wstring normalizedKind = ToLowerCompat(install.kind);
				const bool validInstall =
					(normalizedKind == L"brew" && !install.formula.empty()) ||
					(normalizedKind == L"node" && !install.package.empty()) ||
					(normalizedKind == L"go" && !install.module.empty()) ||
					(normalizedKind == L"uv" && !install.package.empty()) ||
					(normalizedKind == L"download" && !install.url.empty());
				if (validInstall) {
					metadata.install.push_back(std::move(install));
				}
			}

			return metadata;
		}

	} // namespace

	std::optional<ParsedSkillFrontmatterCompat> ParseSkillFrontmatterCompat(
		const std::wstring& skillContent,
		std::vector<std::wstring>& outValidationErrors) {
		const auto parsedMarkdown =
			ParseMarkdownFrontmatterBlockCompat(skillContent);
		if (!parsedMarkdown.hasFrontmatterStart) {
			outValidationErrors.push_back(
				L"Missing frontmatter start marker (---).");
			return std::nullopt;
		}
		if (!parsedMarkdown.hasFrontmatterEnd) {
			outValidationErrors.push_back(
				L"Missing frontmatter closing marker (---).");
			return std::nullopt;
		}

		ParsedSkillFrontmatterCompat parsed;
		for (const auto& [rawKey, value] : parsedMarkdown.fields) {
			const std::wstring key = ToLowerCompat(TrimCompat(rawKey));
			if (key.empty()) {
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

		if (TrimCompat(parsed.name).empty()) {
			outValidationErrors.push_back(
				L"Missing required frontmatter field: name.");
		}

		if (TrimCompat(parsed.description).empty()) {
			outValidationErrors.push_back(
				L"Missing required frontmatter field: description.");
		}

		if (!outValidationErrors.empty()) {
			return std::nullopt;
		}

		return parsed;
	}

	std::wstring GetSkillFrontmatterFieldCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		std::initializer_list<const wchar_t*> keys) {
		for (const auto* key : keys) {
			const auto it = frontmatter.fields.find(
				ToLowerCompat(TrimCompat(key)));
			if (it != frontmatter.fields.end()) {
				return TrimCompat(it->second);
			}
		}

		return {};
	}

	SkillsMetadataSpec ResolveOpenClawMetadataCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		if (const auto manifestMetadata =
			ResolveOpenClawMetadataFromManifestCompat(frontmatter);
			manifestMetadata.has_value()) {
			return manifestMetadata.value();
		}

		SkillsMetadataSpec metadata;
		metadata.skillKey = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"skillkey", L"skill-key", L"openclaw.skillkey" });
		metadata.primaryEnv = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"primary-env", L"primary_env", L"openclaw.primary-env" });
		metadata.emoji = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"emoji", L"openclaw.emoji" });
		metadata.homepage = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"homepage", L"openclaw.homepage" });

		const auto alwaysRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"always", L"openclaw.always" });
		if (!alwaysRaw.empty()) {
			metadata.always = ParseBoolFieldCompat(alwaysRaw, false);
		}

		metadata.os = SplitListCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"os", L"openclaw.os" }));

		metadata.requirements.bins = SplitListCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"requires-bins", L"requires_bins", L"requires.bins" }));
		metadata.requirements.anyBins = SplitListCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"requires-any-bins", L"requires_any_bins", L"requires.anybins" }));
		metadata.requirements.env = SplitListCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"requires-env", L"requires_env", L"requires.env" }));
		metadata.requirements.config = SplitListCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"requires-config", L"requires_config", L"requires.config" }));

		const auto installKind = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"install-kind", L"install_kind", L"install.kind" });
		if (!installKind.empty()) {
			SkillInstallSpec install;
			install.kind = installKind;
			install.id = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-id", L"install_id", L"install.id" });
			install.label = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-label", L"install_label", L"install.label" });
			const auto formula = NormalizeSafeBrewFormulaCompat(
				GetSkillFrontmatterFieldCompat(
					frontmatter,
					{ L"install-formula", L"install_formula", L"install.formula" }));
			if (formula.has_value()) {
				install.formula = formula.value();
			}

			const auto cask = NormalizeSafeBrewFormulaCompat(
				GetSkillFrontmatterFieldCompat(
					frontmatter,
					{ L"install-cask", L"install_cask", L"install.cask" }));
			if (install.formula.empty() && cask.has_value()) {
				install.formula = cask.value();
			}

			const auto package = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-package", L"install_package", L"install.package" });
			if (ToLowerCompat(install.kind) == L"node") {
				const auto npm = NormalizeSafeNpmSpecCompat(package);
				if (npm.has_value()) {
					install.package = npm.value();
				}
			}
			else if (ToLowerCompat(install.kind) == L"uv") {
				const auto uv = NormalizeSafeUvPackageCompat(package);
				if (uv.has_value()) {
					install.package = uv.value();
				}
			}

			const auto module = NormalizeSafeGoModuleCompat(
				GetSkillFrontmatterFieldCompat(
					frontmatter,
					{ L"install-module", L"install_module", L"install.module" }));
			if (module.has_value()) {
				install.module = module.value();
			}

			const auto url = NormalizeSafeDownloadUrlCompat(
				GetSkillFrontmatterFieldCompat(
					frontmatter,
					{ L"install-url", L"install_url", L"install.url" }));
			if (url.has_value()) {
				install.url = url.value();
			}
			install.archive = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-archive", L"install_archive", L"install.archive" });
			install.targetDir = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-target-dir", L"install_target_dir", L"install.targetDir" });
			install.bins = SplitListCompat(GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-bins", L"install_bins", L"install.bins" }));
			install.os = SplitListCompat(GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-os", L"install_os", L"install.os" }));

			const auto extractRaw = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-extract", L"install_extract", L"install.extract" });
			if (!extractRaw.empty()) {
				install.extract = ParseBoolFieldCompat(extractRaw, false);
			}

			const auto stripRaw = GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"install-strip-components", L"install_strip_components", L"install.stripComponents" });
			if (!stripRaw.empty()) {
				try {
					install.stripComponents = static_cast<std::uint32_t>(
						std::stoul(stripRaw));
				}
				catch (...) {
					install.stripComponents.reset();
				}
			}

			const std::wstring normalizedKind = ToLowerCompat(install.kind);
			const bool validInstall =
				(normalizedKind == L"brew" && !install.formula.empty()) ||
				(normalizedKind == L"node" && !install.package.empty()) ||
				(normalizedKind == L"go" && !install.module.empty()) ||
				(normalizedKind == L"uv" && !install.package.empty()) ||
				(normalizedKind == L"download" && !install.url.empty());
			if (validInstall) {
				metadata.install.push_back(std::move(install));
			}
		}

		return metadata;
	}

	SkillInvocationPolicySpec ResolveSkillInvocationPolicyCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		SkillInvocationPolicySpec policy;
		const auto userInvocableRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"user-invocable", L"user_invocable", L"openclaw.user-invocable" });
		if (!userInvocableRaw.empty()) {
			policy.userInvocable = ParseBoolFieldCompat(userInvocableRaw, true);
		}

		const auto disableModelInvocationRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"disable-model-invocation", L"disable_model_invocation", L"disablemodelinvocation" });
		if (!disableModelInvocationRaw.empty()) {
			policy.disableModelInvocation = ParseBoolFieldCompat(
				disableModelInvocationRaw,
				false);
		}

		return policy;
	}

	SkillExposureSpec ResolveSkillExposurePolicyCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		const SkillInvocationPolicySpec& invocation) {
		SkillExposureSpec exposure;
		exposure.userInvocable = invocation.userInvocable;

		const auto includeRegistryRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"include-runtime-registry", L"include_runtime_registry", L"exposure.includeInRuntimeRegistry" });
		if (!includeRegistryRaw.empty()) {
			exposure.includeInRuntimeRegistry =
				ParseBoolFieldCompat(includeRegistryRaw, true);
		}

		const auto includePromptRaw = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"include-available-skills-prompt", L"include_available_skills_prompt", L"exposure.includeInAvailableSkillsPrompt" });
		if (!includePromptRaw.empty()) {
			exposure.includeInAvailableSkillsPrompt =
				ParseBoolFieldCompat(includePromptRaw, true);
		}

		return exposure;
	}

	std::wstring ResolveSkillInstallKindCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		return ToLowerCompat(TrimCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"openclaw.install.kind", L"install.kind" })));
	}

	std::wstring ResolveSkillInstallLabelCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		return TrimCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"openclaw.install.label", L"install.label" }));
	}

	std::wstring ResolveSkillInstallFormulaCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		const auto formula = NormalizeSafeBrewFormulaCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"openclaw.install.formula", L"install.formula" }));
		if (formula.has_value()) {
			return formula.value();
		}

		const auto cask = NormalizeSafeBrewFormulaCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"openclaw.install.cask", L"install.cask" }));
		if (cask.has_value()) {
			return cask.value();
		}

		return {};
	}

	std::wstring ResolveSkillInstallPackageCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		const std::wstring& installKind) {
		const auto package = GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"openclaw.install.package", L"install.package" });
		const auto kind = ToLowerCompat(TrimCompat(installKind));
		if (kind == L"node") {
			const auto npm = NormalizeSafeNpmSpecCompat(package);
			return npm.has_value() ? npm.value() : std::wstring{};
		}

		if (kind == L"uv") {
			const auto uv = NormalizeSafeUvPackageCompat(package);
			return uv.has_value() ? uv.value() : std::wstring{};
		}

		return {};
	}

	std::wstring ResolveSkillInstallModuleCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		const auto module = NormalizeSafeGoModuleCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"openclaw.install.module", L"install.module" }));
		return module.has_value() ? module.value() : std::wstring{};
	}

	std::wstring ResolveSkillInstallUrlCompat(
		const ParsedSkillFrontmatterCompat& frontmatter) {
		const auto url = NormalizeSafeDownloadUrlCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"openclaw.install.url", L"install.url" }));
		return url.has_value() ? url.value() : std::wstring{};
	}

	bool ResolveSkillInstallPreferBrewCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		const bool fallback) {
		return ParseBoolFieldCompat(
			GetSkillFrontmatterFieldCompat(
				frontmatter,
				{ L"openclaw.install.preferbrew", L"install.preferbrew" }),
			fallback);
	}

	std::wstring ResolveSkillInstallNodeManagerCompat(
		const ParsedSkillFrontmatterCompat& frontmatter,
		const std::wstring& fallback) {
		const std::wstring manager = ToLowerCompat(TrimCompat(GetSkillFrontmatterFieldCompat(
			frontmatter,
			{ L"openclaw.install.nodemanager", L"install.nodemanager" })));
		if (manager == L"pnpm" ||
			manager == L"yarn" ||
			manager == L"bun" ||
			manager == L"npm") {
			return manager;
		}

		return fallback;
	}

	std::wstring ResolveSkillKeyCompat(
		const SkillsMetadataSpec* metadata,
		const std::wstring& skillName) {
		if (metadata != nullptr) {
			const std::wstring key = TrimCompat(metadata->skillKey);
			if (!key.empty()) {
				return key;
			}
		}

		return skillName;
	}

} // namespace blazeclaw::core
