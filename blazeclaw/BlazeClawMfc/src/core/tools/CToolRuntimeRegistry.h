#pragma once

#include "../../gateway/GatewayHost.h"

#include <functional>

namespace blazeclaw::core {

	class CToolRuntimeRegistry {
	public:
		struct Dependencies {
			std::function<void(blazeclaw::gateway::GatewayHost&)> registerImapSmtp;
			std::function<void(blazeclaw::gateway::GatewayHost&)> registerContentPolishing;
			std::function<void(blazeclaw::gateway::GatewayHost&)> registerBraveSearch;
			std::function<void(blazeclaw::gateway::GatewayHost&)> registerBaiduSearch;
		};

		void RegisterAll(
			blazeclaw::gateway::GatewayHost& host,
			const Dependencies& deps) const;
	};

} // namespace blazeclaw::core
