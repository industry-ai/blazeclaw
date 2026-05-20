#pragma once

#include <filesystem>

namespace blazeclaw::cron::test_hooks {

	/// Replaces the process-wide cron ops singleton with a fresh default instance.
	void ResetCronOpsServiceForTest();

	/// Replaces the cron ops singleton with an isolated on-disk store at the given paths.
	void ConfigureCronOpsServiceForTest(
		std::filesystem::path jobsPath,
		std::filesystem::path runsPath);

} // namespace blazeclaw::cron::test_hooks
