#pragma once

namespace blazeclaw::core::bootstrap {

	class StartupFixtureValidator {
	public:
		static constexpr const wchar_t* kEnvStartupValidationEnabled =
			L"BLAZECLAW_FIXTURES_STARTUP_VALIDATION_ENABLED";
	};

} // namespace blazeclaw::core::bootstrap
