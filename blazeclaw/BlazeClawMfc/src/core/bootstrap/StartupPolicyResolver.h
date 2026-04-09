#pragma once

namespace blazeclaw::core::bootstrap {

	class StartupPolicyResolver {
	public:
		void AppendStartupTrace(const char* stage) const;
	};

} // namespace blazeclaw::core::bootstrap
