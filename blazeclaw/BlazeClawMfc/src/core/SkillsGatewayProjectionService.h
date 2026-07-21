#pragma once

#include "SkillsCatalogService.h"
#include "SkillsCommandService.h"
#include "SkillsEligibilityService.h"
#include "SkillsInstallService.h"
#include "SkillsFrontmatterCompat.h"
#include "../gateway/GatewayHost.h"
#include "../gateway/GatewayJsonUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace blazeclaw::core {

	class SkillsGatewayProjectionService {
	public:
		[[nodiscard]] blazeclaw::gateway::SkillsCatalogGatewayEntry BuildGatewaySkillEntry(
			const SkillsCatalogEntry& entry,
			const SkillsEligibilityEntry* eligibility,
			const SkillsCommandSpec* command,
			const SkillsInstallPlanEntry* install) const {
			std::vector<std::string> metadataSources;

			std::string commandName;
			std::string commandToolName;
			std::string commandArgMode;
			std::string commandArgSchema;
			std::string commandResultSchema;
			std::string commandIdempotencyHint;
			std::string commandRetryPolicyHint;
			bool commandRequiresApproval = false;
			if (command != nullptr) {
				commandName = ToNarrow(command->name);
				commandToolName = ToNarrow(command->dispatch.toolName);
				commandArgMode = ToNarrow(command->dispatch.argMode);
				commandArgSchema = ToNarrow(command->dispatch.argSchema);
				commandResultSchema = ToNarrow(command->dispatch.resultSchema);
				commandIdempotencyHint = ToNarrow(command->dispatch.idempotencyHint);
				commandRetryPolicyHint = ToNarrow(command->dispatch.retryPolicyHint);
				commandRequiresApproval = command->dispatch.requiresApproval;
			}
			else if (
				entry.sourceKind == SkillsSourceKind::OpenClawOriginal &&
				entry.openClawOriginalActivationState.has_value() &&
				entry.openClawOriginalActivationState.value() ==
				SkillsOpenClawOriginalActivationState::ToolEnabled &&
				entry.openClawOriginalExtractedRuntimeContract.has_value() &&
				entry.openClawOriginalExtractedRuntimeContract->complete) {
				const auto& extracted =
					entry.openClawOriginalExtractedRuntimeContract.value();
				commandToolName = BuildGeneratedOpenClawCommandToolName(
					extracted,
					entry.skillName);
				commandName = BuildGeneratedOpenClawCommandName(
					extracted,
					entry.skillName);
				commandArgMode = "raw";
				commandResultSchema = "openclaw.generated.runtime-contract";
			}

			std::string installKind;
			std::string installCommand;
			std::string installReason;
			bool installExecutable = false;
			if (install != nullptr) {
				installKind = ToNarrow(install->kind);
				installCommand = ToNarrow(install->command);
				installReason = ToNarrow(install->reason);
				installExecutable = install->executable;
			}

			std::string primaryEnv;
			std::vector<std::string> requiresBins;
			std::vector<std::string> requiresEnv;
			std::vector<std::string> requiresConfig;
			std::string pluginConfigSchemaJson;
			std::string pluginConfigUiHintsJson;
			std::string channelConfigSchemasJson;
			std::string channelConfigUiHintsJson;

			if (const auto* value = ResolveNormalizedField(
				entry.frontmatter,
				{ L"metadata.blazeclaw.primaryenv", L"metadata.blazeclaw.primary_env" },
				{ L"metadata.openclaw.primaryenv", L"metadata.openclaw.primary_env" },
				metadataSources);
				value != nullptr) {
				primaryEnv = ToNarrow(*value);
			}

			if (const auto* value = ResolveNormalizedField(
				entry.frontmatter,
				{ L"metadata.blazeclaw.requires.bins" },
				{ L"metadata.openclaw.requires.bins" },
				metadataSources);
				value != nullptr) {
				requiresBins = UniqueNarrowValues(SplitCommaDelimitedWide(*value));
			}

			if (const auto* value = ResolveNormalizedField(
				entry.frontmatter,
				{ L"metadata.blazeclaw.requires.env" },
				{ L"metadata.openclaw.requires.env" },
				metadataSources);
				value != nullptr) {
				requiresEnv = UniqueNarrowValues(SplitCommaDelimitedWide(*value));
			}

			if (const auto* value = ResolveNormalizedField(
				entry.frontmatter,
				{ L"metadata.blazeclaw.requires.config" },
				{ L"metadata.openclaw.requires.config" },
				metadataSources);
				value != nullptr) {
				requiresConfig = UniqueNarrowValues(SplitCommaDelimitedWide(*value));
			}

			if (const auto* value = ResolveNormalizedField(
				entry.frontmatter,
				{ L"metadata.blazeclaw.plugin.configschema", L"metadata.blazeclaw.plugin.config_schema" },
				{ L"metadata.openclaw.plugin.configschema", L"metadata.openclaw.plugin.config_schema" },
				metadataSources);
				value != nullptr) {
				pluginConfigSchemaJson = NarrowTrimmedOrEmpty(*value);
			}

			if (const auto* value = ResolveNormalizedField(
				entry.frontmatter,
				{ L"metadata.blazeclaw.plugin.configuihints", L"metadata.blazeclaw.plugin.config_ui_hints" },
				{ L"metadata.openclaw.plugin.configuihints", L"metadata.openclaw.plugin.config_ui_hints" },
				metadataSources);
				value != nullptr) {
				pluginConfigUiHintsJson = NarrowTrimmedOrEmpty(*value);
			}

			if (const auto* value = ResolveNormalizedField(
				entry.frontmatter,
				{ L"metadata.blazeclaw.channels.configschemas", L"metadata.blazeclaw.channels.config_schemas" },
				{ L"metadata.openclaw.channels.configschemas", L"metadata.openclaw.channels.config_schemas" },
				metadataSources);
				value != nullptr) {
				channelConfigSchemasJson = NormalizeFlatJsonMapToObject(*value);
			}

			if (const auto* value = ResolveNormalizedField(
				entry.frontmatter,
				{ L"metadata.blazeclaw.channels.configuihints", L"metadata.blazeclaw.channels.config_ui_hints" },
				{ L"metadata.openclaw.channels.configuihints", L"metadata.openclaw.channels.config_ui_hints" },
				metadataSources);
				value != nullptr) {
				channelConfigUiHintsJson = NormalizeFlatJsonMapToObject(*value);
			}

			std::vector<std::string> configPathHints;
			for (const auto& configKey : requiresConfig) {
				if (configKey == "channels.discord.token") {
					configPathHints.push_back("credentials.discord.token");
				}
				else if (configKey == "channels.slack") {
					configPathHints.push_back("channels.slack.default");
				}
				else if (configKey == "plugins.entries.voice-call.enabled") {
					configPathHints.push_back("plugins.voice-call.enabled");
				}
				else {
					configPathHints.push_back(configKey);
				}

				if (std::find(
					configPathHints.begin(),
					configPathHints.end(),
					configKey) == configPathHints.end()) {
					configPathHints.push_back(configKey);
				}
			}

			blazeclaw::gateway::SkillsCatalogGatewayEntry gatewayEntry;
			gatewayEntry.name = ToNarrow(entry.skillName);
			const std::wstring resolvedSkillKey = eligibility != nullptr
				? eligibility->skillKey
				: ResolveSkillKeyCompat(
					entry.metadata.has_value() ? &entry.metadata.value() : nullptr,
					entry.skillName);
			gatewayEntry.skillKey = ToNarrow(resolvedSkillKey);
			gatewayEntry.commandName = commandName;
			gatewayEntry.commandToolName = commandToolName;
			gatewayEntry.commandArgMode = commandArgMode;
			gatewayEntry.commandArgSchema = commandArgSchema;
			gatewayEntry.commandResultSchema = commandResultSchema;
			gatewayEntry.commandIdempotencyHint = commandIdempotencyHint;
			gatewayEntry.commandRetryPolicyHint = commandRetryPolicyHint;
			gatewayEntry.commandRequiresApproval = commandRequiresApproval;
			gatewayEntry.installKind = installKind;
			gatewayEntry.installCommand = installCommand;
			gatewayEntry.installExecutable = installExecutable;
			gatewayEntry.installReason = installReason;
			gatewayEntry.description = ToNarrow(entry.description);
			gatewayEntry.source = ToNarrow(
				SkillsCatalogService::SourceKindLabel(entry.sourceKind));
			gatewayEntry.browserGroup = entry.sourceKind == SkillsSourceKind::OpenClawOriginal
				? "imported"
				: std::string();
			gatewayEntry.browserDisplayName = gatewayEntry.name;
			gatewayEntry.browserSourceLabel = gatewayEntry.source;
			gatewayEntry.browserVariantLabel.clear();
			gatewayEntry.precedence = entry.precedence;
			gatewayEntry.eligible = eligibility != nullptr ? eligibility->eligible : false;
			gatewayEntry.disabled = eligibility != nullptr ? eligibility->disabled : false;
			gatewayEntry.blockedByAllowlist = eligibility != nullptr
				? eligibility->blockedByAllowlist
				: false;
			gatewayEntry.disableModelInvocation = eligibility != nullptr
				? eligibility->disableModelInvocation
				: false;
			gatewayEntry.validFrontmatter = entry.validFrontmatter;
			gatewayEntry.validationErrorCount = entry.validationErrors.size();
			gatewayEntry.primaryEnv = primaryEnv;
			gatewayEntry.requiresBins = std::move(requiresBins);
			gatewayEntry.requiresEnv = std::move(requiresEnv);
			gatewayEntry.requiresConfig = std::move(requiresConfig);
			gatewayEntry.configPathHints = std::move(configPathHints);
			gatewayEntry.pluginConfigSchemaJson = std::move(pluginConfigSchemaJson);
			gatewayEntry.pluginConfigUiHintsJson = std::move(pluginConfigUiHintsJson);
			gatewayEntry.channelConfigSchemasJson = std::move(channelConfigSchemasJson);
			gatewayEntry.channelConfigUiHintsJson = std::move(channelConfigUiHintsJson);
			gatewayEntry.normalizedMetadataSources = std::move(metadataSources);
			if (eligibility != nullptr) {
				gatewayEntry.missingEnv = UniqueNarrowValues(eligibility->missingEnv);
				gatewayEntry.missingConfig = UniqueNarrowValues(eligibility->missingConfig);
				gatewayEntry.missingBins = UniqueNarrowValues(eligibility->missingBins);
				gatewayEntry.missingAnyBins =
					UniqueNarrowValues(eligibility->missingAnyBins);
			}

			if (entry.openClawOriginalActivationState.has_value()) {
				gatewayEntry.openClawOriginalActivationState = ToNarrow(
					SkillsCatalogService::OpenClawOriginalActivationStateLabel(
						entry.openClawOriginalActivationState.value()));
			}
			gatewayEntry.openClawOriginalOrigin =
				ToNarrow(entry.openClawOriginalOrigin);
			gatewayEntry.openClawOriginalImportDiagnostics =
				UniqueNarrowValues(entry.openClawOriginalImportDiagnostics);
			if (entry.openClawOriginalExtractedRuntimeContract.has_value()) {
				const auto& extracted =
					entry.openClawOriginalExtractedRuntimeContract.value();
				gatewayEntry.openClawOriginalTriggerHints =
					UniqueNarrowValues(extracted.triggerHints);
				if (extracted.output.has_value()) {
					gatewayEntry.openClawOriginalOutputKind =
						ToNarrow(TrimWide(extracted.output->kind));
					gatewayEntry.openClawOriginalOutputTitle =
						ToNarrow(TrimWide(extracted.output->title));
					gatewayEntry.openClawOriginalOutputUrl =
						ToNarrow(TrimWide(extracted.output->url));
				}
			}
			gatewayEntry.openClawOriginalMetadataConvertedFromClawdbot =
				entry.openClawOriginalMetadataConvertedFromClawdbot;
			gatewayEntry.openClawOriginalMissingToolManifest = std::any_of(
				entry.openClawOriginalImportDiagnostics.begin(),
				entry.openClawOriginalImportDiagnostics.end(),
				[](const std::wstring& diagnostic) {
					std::wstring lowered = diagnostic;
					std::transform(
						lowered.begin(),
						lowered.end(),
						lowered.begin(),
						[](const wchar_t ch) {
							return static_cast<wchar_t>(std::towlower(ch));
						});
					return lowered.find(L"missing tool manifest") !=
						std::wstring::npos;
				});
			if (!gatewayEntry.openClawOriginalActivationState.empty()) {
				if (gatewayEntry.openClawOriginalActivationState == "failed") {
					gatewayEntry.browserGroup = "failed";
				}
				else if (gatewayEntry.openClawOriginalActivationState == "tool_enabled") {
					gatewayEntry.browserGroup = "enabled";
				}
				else if (gatewayEntry.openClawOriginalActivationState == "imported") {
					gatewayEntry.browserGroup = "imported";
				}
			}
			const std::string directoryName =
				ToNarrow(entry.skillDir.filename().wstring());
			if (entry.sourceKind == SkillsSourceKind::OpenClawOriginal) {
				gatewayEntry.browserSourceLabel = "openclaw-original";
				gatewayEntry.browserVariantLabel = directoryName.empty()
					? gatewayEntry.browserSourceLabel
					: directoryName;
			}
			else if (entry.sourceKind == SkillsSourceKind::Bundled ||
				entry.sourceKind == SkillsSourceKind::Managed) {
				gatewayEntry.browserVariantLabel = directoryName.empty()
					? gatewayEntry.browserSourceLabel
					: directoryName;
			}
			if (!gatewayEntry.browserVariantLabel.empty() &&
				gatewayEntry.browserVariantLabel != gatewayEntry.name) {
				gatewayEntry.browserDisplayName +=
					" [" + gatewayEntry.browserVariantLabel + "]";
			}
			if (!gatewayEntry.browserSourceLabel.empty() &&
				gatewayEntry.browserSourceLabel != gatewayEntry.browserVariantLabel &&
				gatewayEntry.browserSourceLabel != gatewayEntry.name) {
				gatewayEntry.browserDisplayName +=
					" — " + gatewayEntry.browserSourceLabel;
			}
			return gatewayEntry;
		}

	private:
		[[nodiscard]] static std::wstring TrimWide(const std::wstring& value) {
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
				})
				.base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		[[nodiscard]] static std::wstring ToLowerWide(std::wstring value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return value;
		}

		[[nodiscard]] static std::string ToNarrow(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());

			for (const wchar_t ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}

		[[nodiscard]] static std::wstring TrimWideLocal(
			const std::wstring& value) {
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
				})
				.base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		[[nodiscard]] static std::string NarrowTrimmedOrEmpty(
			const std::wstring& value) {
			const std::wstring trimmed = TrimWideLocal(value);
			if (trimmed.empty()) {
				return {};
			}

			return ToNarrow(trimmed);
		}

		[[nodiscard]] static std::string NormalizeFlatJsonMapToObject(
			const std::wstring& rawValue) {
			const std::wstring trimmedWide = TrimWideLocal(rawValue);
			if (trimmedWide.empty()) {
				return {};
			}

			const std::string narrow = ToNarrow(trimmedWide);
			if (blazeclaw::gateway::json::Trim(narrow).empty()) {
				return {};
			}

			nlohmann::json parsed =
				nlohmann::json::parse(narrow, nullptr, false);
			if (parsed.is_discarded()) {
				return {};
			}

			if (parsed.is_object()) {
				return parsed.dump();
			}

			if (!parsed.is_array()) {
				return {};
			}

			nlohmann::json normalized = nlohmann::json::object();
			for (const auto& entry : parsed) {
				if (!entry.is_object()) {
					continue;
				}

				const auto idIt = entry.find("id");
				if (idIt == entry.end() || !idIt->is_string()) {
					continue;
				}

				const std::string id =
					blazeclaw::gateway::json::Trim(idIt->get<std::string>());
				if (id.empty()) {
					continue;
				}

				normalized[id] = entry;
			}

			return normalized.dump();
		}

		[[nodiscard]] static std::vector<std::wstring> SplitCommaDelimitedWide(
			const std::wstring& rawValue) {
			std::vector<std::wstring> values;
			std::wstring token;
			for (const wchar_t ch : rawValue) {
				if (ch == L',' || ch == L';') {
					const std::wstring trimmed = TrimWide(token);
					if (!trimmed.empty()) {
						values.push_back(trimmed);
					}
					token.clear();
					continue;
				}

				token.push_back(ch);
			}

			const std::wstring trimmed = TrimWide(token);
			if (!trimmed.empty()) {
				values.push_back(trimmed);
			}

			return values;
		}

		[[nodiscard]] static std::vector<std::string> UniqueNarrowValues(
			const std::vector<std::wstring>& values) {
			std::vector<std::string> output;
			for (const auto& value : values) {
				const std::string narrow = ToNarrow(value);
				if (narrow.empty()) {
					continue;
				}

				if (std::find(output.begin(), output.end(), narrow) != output.end()) {
					continue;
				}

				output.push_back(narrow);
			}

			return output;
		}

		[[nodiscard]] static const std::wstring* FindFrontmatterFieldCaseInsensitive(
			const SkillFrontmatter& frontmatter,
			const std::wstring& key) {
			const std::wstring loweredKey = ToLowerWide(key);
			for (const auto& item : frontmatter.fields) {
				if (ToLowerWide(item.first) == loweredKey) {
					return &item.second;
				}
			}

			return nullptr;
		}

		[[nodiscard]] static const std::wstring* ResolveNormalizedField(
			const SkillFrontmatter& frontmatter,
			const std::vector<std::wstring>& blazeclawKeys,
			const std::vector<std::wstring>& openclawKeys,
			std::vector<std::string>& outSources) {
			for (const auto& key : blazeclawKeys) {
				if (const auto* value = FindFrontmatterFieldCaseInsensitive(frontmatter, key);
					value != nullptr && !TrimWide(*value).empty()) {
					outSources.push_back("metadata.blazeclaw");
					return value;
				}
			}

			for (const auto& key : openclawKeys) {
				if (const auto* value = FindFrontmatterFieldCaseInsensitive(frontmatter, key);
					value != nullptr && !TrimWide(*value).empty()) {
					outSources.push_back("metadata.openclaw");
					return value;
				}
			}

			return nullptr;
		}

		[[nodiscard]] static std::wstring NormalizeToolTokenWide(
			const std::wstring& raw) {
			std::wstring token;
			token.reserve(raw.size());
			for (const wchar_t ch : raw) {
				const wchar_t lowered = static_cast<wchar_t>(std::towlower(ch));
				const bool alphaNum =
					(lowered >= L'a' && lowered <= L'z') ||
					(lowered >= L'0' && lowered <= L'9');
				if (alphaNum) {
					token.push_back(lowered);
					continue;
				}

				if (lowered == L'-' || lowered == L'_' || lowered == L'.' ||
					lowered == L'/' || lowered == L'\\') {
					if (!token.empty() && token.back() != L'_') {
						token.push_back(L'_');
					}
				}
			}

			while (!token.empty() && token.front() == L'_') {
				token.erase(token.begin());
			}
			while (!token.empty() && token.back() == L'_') {
				token.pop_back();
			}

			if (token.empty()) {
				token = L"openclaw_skill";
			}

			return token;
		}

		[[nodiscard]] static std::string BuildGeneratedOpenClawCommandToolName(
			const OpenClawOriginalExtractedRuntimeContractSpec& extracted,
			const std::wstring& fallbackSkillName) {
			std::wstring key = TrimWide(extracted.skillKey);
			if (key.empty()) {
				key = TrimWide(fallbackSkillName);
			}
			const std::wstring normalized = NormalizeToolTokenWide(key);
			return ToNarrow(normalized) + ".openclaw.generated";
		}

		[[nodiscard]] static std::string BuildGeneratedOpenClawCommandName(
			const OpenClawOriginalExtractedRuntimeContractSpec& extracted,
			const std::wstring& fallbackSkillName) {
			if (extracted.output.has_value()) {
				const std::wstring title = TrimWide(extracted.output->title);
				if (!title.empty()) {
					return ToNarrow(title);
				}
			}

			std::wstring key = TrimWide(extracted.skillKey);
			if (key.empty()) {
				key = TrimWide(fallbackSkillName);
			}
			return ToNarrow(key);
		}
	};

} // namespace blazeclaw::core
