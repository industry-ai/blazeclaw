#pragma once

#include <functional>

namespace blazeclaw::core {

	class HooksStartupCoordinator {
	public:
		struct ExecutionContext {
			bool startupSkillsRefreshEnabled = false;
			bool startupHookBootstrapEnabled = false;
			std::function<void()> runHookBootstrap;
			std::function<void(const char*)> appendTrace;
		};

		void Execute(const ExecutionContext& context) const {
			if (context.startupSkillsRefreshEnabled &&
				context.startupHookBootstrapEnabled) {
				if (context.runHookBootstrap) {
					context.runHookBootstrap();
				}
				return;
			}

			if (context.appendTrace) {
				context.appendTrace("ServiceManager.Start.hooks.bootstrap.skipped");
			}
		}
	};

} // namespace blazeclaw::core
