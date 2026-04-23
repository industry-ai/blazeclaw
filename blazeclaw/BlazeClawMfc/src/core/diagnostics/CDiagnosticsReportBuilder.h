#pragma once

#include "DiagnosticsSnapshot.h"

#include <string>

namespace blazeclaw::core {

	class CDiagnosticsReportBuilder {
	public:
		std::string BuildOperatorDiagnosticsReport(
			const DiagnosticsSnapshot& snapshot) const;
		[[nodiscard]] std::string SerializeParityLifecycleContractJson(
			const GatewayParityLifecycleContract& contract) const;
	};

} // namespace blazeclaw::core
