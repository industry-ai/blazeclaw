#include "pch.h"
#include "SkillsAgentCommandDescriptorPolicy.h"

#include "../gateway/GatewayHost.h"

#include <algorithm>
#include <cwctype>
#include <string>

namespace blazeclaw::core {

namespace {

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

std::wstring Utf8ToWide(const std::string& value) {
	if (value.empty()) {
		return {};
	}

	const int needed = MultiByteToWideChar(
		CP_UTF8,
		0,
		value.c_str(),
		static_cast<int>(value.size()),
		nullptr,
		0);
	if (needed <= 0) {
		return {};
	}

	std::wstring output(static_cast<std::size_t>(needed), L'\0');
	MultiByteToWideChar(
		CP_UTF8,
		0,
		value.c_str(),
		static_cast<int>(value.size()),
		output.data(),
		needed);
	return output;
}

} // namespace

std::vector<AgentSkillCommandDescriptor> SkillsAgentCommandDescriptorPolicy::BuildDescriptors(
	const blazeclaw::config::AppConfig& config,
	const AgentScopeSnapshot& agentsScope) {
	std::vector<AgentSkillCommandDescriptor> commandDescriptors;
	commandDescriptors.reserve(agentsScope.entries.size());
	const auto defaultSkillFilter = config.agents.defaults.skills;
	for (const auto& entry : agentsScope.entries) {
		std::optional<std::vector<std::wstring>> skillFilter = defaultSkillFilter;
		const auto configEntryIt =
			config.agents.entries.find(AgentsCatalogService::NormalizeAgentId(entry.id));
		if (configEntryIt != config.agents.entries.end() &&
			configEntryIt->second.skills.has_value()) {
			skillFilter = configEntryIt->second.skills;
		}

		commandDescriptors.push_back(AgentSkillCommandDescriptor{
			.agentId = entry.id,
			.workspaceDir = entry.workspaceDir,
			.skillFilter = skillFilter,
		});
	}
	return commandDescriptors;
}

std::vector<std::wstring>
	SkillsAgentCommandDescriptorPolicy::BuildReservedChatSlashCommandNamesNormalized() {
	std::vector<std::wstring> reservedSkillCommandNames;
	for (const auto& name :
		blazeclaw::gateway::GatewayHost::ListReservedChatSlashCommandNames()) {
		const auto normalized = Trim(Utf8ToWide(name));
		if (!normalized.empty()) {
			reservedSkillCommandNames.push_back(normalized);
		}
	}
	return reservedSkillCommandNames;
}

} // namespace blazeclaw::core
