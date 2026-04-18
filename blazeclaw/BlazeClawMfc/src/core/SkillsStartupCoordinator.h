#pragma once

#include <exception>
#include <functional>

namespace blazeclaw::core {

	class SkillsStartupCoordinator {
	public:
		struct ExecutionContext {
			bool startupSkillsRefreshEnabled = false;
			std::function<void()> runFullRefresh;
			std::function<void()> runMinimalRefresh;
			std::function<void(const std::exception&)> onStdException;
			std::function<void()> onUnknownException;
			std::function<void(const char*)> appendTrace;
		};

		void Execute(const ExecutionContext& context) const {
			if (context.appendTrace) {
				context.appendTrace("ServiceManager.Start.skills.refresh.begin");
			}

			try {
				if (context.startupSkillsRefreshEnabled) {
					if (context.runFullRefresh) {
						context.runFullRefresh();
					}
				}
				else if (context.runMinimalRefresh) {
					context.runMinimalRefresh();
				}
			}
			catch (const std::exception& ex) {
				if (context.onStdException) {
					context.onStdException(ex);
				}
			}
			catch (...) {
				if (context.onUnknownException) {
					context.onUnknownException();
				}
			}

			if (context.appendTrace) {
				context.appendTrace("ServiceManager.Start.skills.refresh.end");
			}
		}
	};

} // namespace blazeclaw::core
