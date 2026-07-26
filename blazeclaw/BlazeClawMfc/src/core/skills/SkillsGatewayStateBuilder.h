// SkillsGatewayStateBuilder.h
#pragma once

#include "SkillsHooksCoordinator.h"

namespace blazeclaw::core {

	class SkillsGatewayStateBuilder {
	public:
		using EntryBuilder = CSkillsHooksCoordinator::EntryBuilder;

		[[nodiscard]] blazeclaw::gateway::SkillsCatalogGatewayState Build(
			const CSkillsHooksCoordinator::GatewayStateContext& context,
			const EntryBuilder& entryBuilder,
			const std::function<std::string(const std::wstring&)>& toNarrow) const;
	};

}