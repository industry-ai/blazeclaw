#include "pch.h"

#include "Skill.h"

#include <windows.h>
#include <algorithm>
#include <cctype>

namespace blazeclaw::core::skills {

	namespace {
		// Narrow wide ASCII-like string used for stable keys: lower-case, alnum and '-' only.
		std::string WideToAsciiKey(const std::wstring& w) {
			std::string out;
			out.reserve(w.size());
			bool lastWasDash = false;
			for (wchar_t wc : w) {
				// normalize whitespace -> dash
				if (std::iswspace(wc)) {
					if (!lastWasDash) {
						out.push_back('-');
						lastWasDash = true;
					}
					continue;
				}
				// try to keep basic ASCII letters/digits
				if (wc <= 0x7F) {
					const char c = static_cast<char>(wc);
					if (std::isalnum(static_cast<unsigned char>(c))) {
						out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
						lastWasDash = false;
						continue;
					}
					if (c == '-' || c == '_') {
						if (!lastWasDash) {
							out.push_back('-');
							lastWasDash = true;
						}
						continue;
					}
					// other ASCII -> skip
					continue;
				}
				// non-ASCII -> replace with dash
				if (!lastWasDash) {
					out.push_back('-');
					lastWasDash = true;
				}
			}
			// trim leading/trailing dashes
			while (!out.empty() && out.front() == '-') out.erase(out.begin());
			while (!out.empty() && out.back() == '-') out.pop_back();
			if (out.empty()) {
				out = "skill";
			}
			return out;
		}

		// UTF-8 narrow for presentation (best-effort).
		std::string WideToUtf8(const std::wstring& w) {
			if (w.empty()) return {};
			const int required = WideCharToMultiByte(
				CP_UTF8, 0,
				w.c_str(), static_cast<int>(w.size()),
				nullptr, 0,
				nullptr, nullptr);
			if (required <= 0) return {};
			std::string out(static_cast<std::size_t>(required), '\0');
			WideCharToMultiByte(
				CP_UTF8, 0,
				w.c_str(), static_cast<int>(w.size()),
				out.data(), required,
				nullptr, nullptr);
			return out;
		}
	} // namespace

	CSkill CSkill::FromCatalogAndRelated(
		const SkillsCatalogEntry& catalogEntry,
		const SkillsEligibilityEntry* eligibility,
		const SkillsCommandSpec* command,
		const SkillsInstallPlanEntry* install) {

		CSkill s;
		// identity + location
		s.skillName = catalogEntry.skillName;
		s.skillDir = catalogEntry.skillDir;
		s.skillFile = catalogEntry.skillFile;
		s.sourceKind = catalogEntry.sourceKind;
		s.precedence = catalogEntry.precedence;

		// description + frontmatter validity
		s.description = catalogEntry.description;
		s.validFrontmatter = catalogEntry.validFrontmatter;
		s.validationErrors = catalogEntry.validationErrors;

		// metadata / invocation / exposure
		if (catalogEntry.metadata.has_value()) {
			s.metadata = catalogEntry.metadata;
			if (!catalogEntry.metadata->skillKey.empty()) {
				s.skillKey = WideToAsciiKey(catalogEntry.metadata->skillKey);
			}
		}
		if (s.skillKey.empty()) {
			// fallback derive from skill name
			s.skillKey = WideToAsciiKey(catalogEntry.skillName);
		}

		if (catalogEntry.invocation.has_value()) {
			s.invocation = catalogEntry.invocation;
		}
		if (catalogEntry.exposure.has_value()) {
			s.exposure = catalogEntry.exposure;
		}

		// OpenClaw compatibility
		if (catalogEntry.openClawOriginalExtractedRuntimeContract.has_value()) {
			s.openClawExtractedRuntimeContract = catalogEntry.openClawOriginalExtractedRuntimeContract;
		}
		if (catalogEntry.openClawOriginalActivationState.has_value()) {
			s.openClawActivationState = catalogEntry.openClawOriginalActivationState;
		}
		s.openClawOrigin = catalogEntry.openClawOriginalOrigin;
		s.openClawImportDiagnostics = catalogEntry.openClawOriginalImportDiagnostics;
		s.openClawMetadataConvertedFromClawdbot = catalogEntry.openClawOriginalMetadataConvertedFromClawdbot;

		// eligibility mapping (if present)
		if (eligibility != nullptr) {
			s.eligible = eligibility->eligible;
			s.disabled = eligibility->disabled;
			s.blockedByAllowlist = eligibility->blockedByAllowlist;
			s.disableModelInvocation = eligibility->disableModelInvocation;
			s.userInvocable = eligibility->userInvocable;
			s.missingOs = eligibility->missingOs;
			s.missingBins = eligibility->missingBins;
			s.missingAnyBins = eligibility->missingAnyBins;
			s.missingEnv = eligibility->missingEnv;
			s.missingConfig = eligibility->missingConfig;
		}
		else {
			// defaults
			s.eligible = false;
			s.disabled = false;
			s.blockedByAllowlist = false;
			s.disableModelInvocation = false;
			s.userInvocable = true;
		}

		// command / install facets
		if (command != nullptr) {
			s.commandSpec = *command;
		}
		if (install != nullptr) {
			s.installPlan = *install;
		}

		return s;
	}

	std::string CSkill::ToAsciiSkillKey() const {
		if (!skillKey.empty()) return skillKey;
		// derive from name if empty
		return WideToAsciiKey(skillName);
	}

	std::string CSkill::ToNarrowName() const {
		return WideToUtf8(skillName);
	}

} // namespace blazeclaw::core::skills