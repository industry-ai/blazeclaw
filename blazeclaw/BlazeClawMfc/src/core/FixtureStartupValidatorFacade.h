#pragma once

#include <functional>

namespace blazeclaw::core {

	class FixtureStartupValidatorFacade {
	public:
		struct ExecutionContext {
			bool startupFixtureValidationEnabled = false;
			std::function<void()> runValidation;
			std::function<void(const char*)> appendTrace;
		};

		void Execute(const ExecutionContext& context) const {
			if (context.startupFixtureValidationEnabled) {
				if (context.runValidation) {
					context.runValidation();
				}
				return;
			}

			if (context.appendTrace) {
				context.appendTrace("ServiceManager.Start.fixtures.validation.skipped");
			}
		}
	};

} // namespace blazeclaw::core
