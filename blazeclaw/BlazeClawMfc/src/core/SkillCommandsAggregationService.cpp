#include "pch.h"
#include "SkillCommandsAggregationService.h"

#include <algorithm>
#include <cwctype>
#include <map>
#include <set>
#include <unordered_set>

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

		void DedupeBySkillNameInPlace(SkillsCommandSnapshot& snapshot) {
			std::unordered_set<std::wstring> seenSkillNames;
			std::vector<SkillsCommandSpec> deduped;
			deduped.reserve(snapshot.commands.size());

			for (const auto& command : snapshot.commands) {
				const auto skillKey = ToLower(Trim(command.skillName));
				if (!skillKey.empty()) {
					if (seenSkillNames.find(skillKey) != seenSkillNames.end()) {
						++snapshot.skillNameDedupeCount;
						continue;
					}

					seenSkillNames.insert(skillKey);
				}

				deduped.push_back(command);
			}

			snapshot.commands = std::move(deduped);
		}

		std::vector<std::wstring> NormalizeSkillFilter(
			const std::vector<std::wstring>& source) {
			std::vector<std::wstring> normalized;
			normalized.reserve(source.size());
			for (const auto& item : source) {
				const auto key = ToLower(Trim(item));
				if (!key.empty()) {
					normalized.push_back(key);
				}
			}

			std::sort(normalized.begin(), normalized.end());
			normalized.erase(
				std::unique(normalized.begin(), normalized.end()),
				normalized.end());
			return normalized;
		}

		std::optional<std::vector<std::wstring>> MergeSkillFilters(
			const std::optional<std::vector<std::wstring>>& existing,
			const std::optional<std::vector<std::wstring>>& incoming) {
			if (!existing.has_value() || !incoming.has_value()) {
				return std::nullopt;
			}

			const auto normalizedExisting = NormalizeSkillFilter(existing.value());
			const auto normalizedIncoming = NormalizeSkillFilter(incoming.value());

			if (normalizedExisting.empty()) {
				return normalizedIncoming;
			}

			if (normalizedIncoming.empty()) {
				return normalizedExisting;
			}

			std::vector<std::wstring> merged = normalizedExisting;
			merged.insert(
				merged.end(),
				normalizedIncoming.begin(),
				normalizedIncoming.end());
			std::sort(merged.begin(), merged.end());
			merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
			return merged;
		}

		bool SkillAllowedByFilter(
			const SkillsCommandSpec& command,
			const std::optional<std::vector<std::wstring>>& filter) {
			if (!filter.has_value()) {
				return true;
			}

			if (filter->empty()) {
				return false;
			}

			const auto skillName = ToLower(Trim(command.skillName));
			if (skillName.empty()) {
				return false;
			}

			return std::find(filter->begin(), filter->end(), skillName) !=
				filter->end();
		}

		void AppendReservedNamesFromCommands(
			const std::vector<SkillsCommandSpec>& commands,
			std::vector<std::wstring>& inOutReservedNames,
			std::unordered_set<std::wstring>& inOutSeenNames) {
			for (const auto& command : commands) {
				const auto normalized = ToLower(Trim(command.name));
				if (normalized.empty()) {
					continue;
				}
				if (inOutSeenNames.insert(normalized).second) {
					inOutReservedNames.push_back(normalized);
				}
			}
		}

	} // namespace

	AgentSkillCommandAggregationSnapshot
		SkillCommandsAggregationService::BuildSnapshot(
			const AgentSkillCommandAggregationContext& context) const {
		AgentSkillCommandAggregationSnapshot aggregate;
		aggregate.descriptorsConsidered =
			static_cast<std::uint32_t>(context.descriptors.size());

		struct WorkspaceGroupEntry {
			std::filesystem::path workspaceDir;
			std::optional<std::vector<std::wstring>> skillFilter;
		};

		std::map<std::wstring, WorkspaceGroupEntry> groupedWorkspaces;
		for (const auto& descriptor : context.descriptors) {
			const auto workspaceDir = descriptor.workspaceDir;
			if (workspaceDir.empty() || !std::filesystem::exists(workspaceDir)) {
				++aggregate.missingWorkspaceSkipped;
				continue;
			}

			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(workspaceDir, ec);
			if (ec || canonical.empty()) {
				++aggregate.unresolvedWorkspaceSkipped;
				continue;
			}

			const auto canonicalKey = ToLower(canonical.lexically_normal().wstring());
			auto groupIt = groupedWorkspaces.find(canonicalKey);
			if (groupIt == groupedWorkspaces.end()) {
				groupedWorkspaces.emplace(
					canonicalKey,
					WorkspaceGroupEntry{
						.workspaceDir = workspaceDir,
						.skillFilter = descriptor.skillFilter,
					});
				continue;
			}

			groupIt->second.skillFilter = MergeSkillFilters(
				groupIt->second.skillFilter,
				descriptor.skillFilter);
		}

		aggregate.workspaceGroupsConsidered =
			static_cast<std::uint32_t>(groupedWorkspaces.size());

		std::vector<std::wstring> rollingReservedNames = context.reservedNames;
		std::unordered_set<std::wstring> seenReservedNames;
		for (const auto& reservedName : rollingReservedNames) {
			const auto normalized = ToLower(Trim(reservedName));
			if (!normalized.empty()) {
				seenReservedNames.insert(normalized);
			}
		}

		for (const auto& [_, group] : groupedWorkspaces) {
			const auto catalog =
				context.catalogService.LoadCatalog(group.workspaceDir, context.appConfig);
			const auto eligibility =
				context.eligibilityService.Evaluate(catalog, context.appConfig);

			SkillsCommandSnapshot perWorkspace;
			if (context.commandSourceAdapters != nullptr) {
				perWorkspace = context.commandService.BuildSnapshotWithAdapters(
					catalog,
					eligibility,
					{},
					*context.commandSourceAdapters,
					rollingReservedNames);
			}
			else {
				perWorkspace = context.commandService.BuildSnapshot(
					catalog,
					eligibility,
					rollingReservedNames);
			}

			auto& aggregatedCommands = aggregate.commandSnapshot.commands;
			for (const auto& command : perWorkspace.commands) {
				if (!SkillAllowedByFilter(command, group.skillFilter)) {
					continue;
				}
				aggregatedCommands.push_back(command);
			}

			aggregate.commandSnapshot.sanitizeCount +=
				perWorkspace.sanitizeCount;
			aggregate.commandSnapshot.dedupeCount +=
				perWorkspace.dedupeCount;
			aggregate.commandSnapshot.missingToolDispatchCount +=
				perWorkspace.missingToolDispatchCount;
			aggregate.commandSnapshot.invalidArgModeFallbackCount +=
				perWorkspace.invalidArgModeFallbackCount;
			aggregate.commandSnapshot.commandSourceContributionCount +=
				perWorkspace.commandSourceContributionCount;

			AppendReservedNamesFromCommands(
				perWorkspace.commands,
				rollingReservedNames,
				seenReservedNames);
		}

		DedupeBySkillNameInPlace(aggregate.commandSnapshot);

		return aggregate;
	}

} // namespace blazeclaw::core
