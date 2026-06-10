#include "pch.h"

#include "CronCliGatewayShim.h"
#include "CronModels.h"

#include <unordered_map>

namespace blazeclaw::cron {

	std::optional<CronCliGatewayRoute> MapCronCliCommandToGatewayRoute(
		const std::string& command) {
		static const std::unordered_map<std::string, CronCliGatewayRoute> kRoutes = {
			{ "help", { "help", "", false } },
			{ "status", { "status", "cron.status", false } },
			{ "list", { "list", "cron.list", false } },
			{ "add", { "add", "cron.add", true } },
			{ "edit", { "edit", "cron.update", true } },
			{ "remove", { "remove", "cron.remove", true } },
			{ "run", { "run", "cron.run", true } },
			{ "runs", { "runs", "cron.runs", false } },
			{ "wake", { "wake", "wake", true } },
		};

		const std::string normalized = ToLowerCopy(TrimCopy(command));
		const auto found = kRoutes.find(normalized);
		if (found == kRoutes.end()) {
			return std::nullopt;
		}

		return found->second;
	}

} // namespace blazeclaw::cron
