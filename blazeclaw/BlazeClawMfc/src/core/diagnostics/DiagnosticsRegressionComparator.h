#pragma once

#include "DiagnosticsSnapshot.h"

#include <string>
#include <vector>

namespace blazeclaw::core {

	class DiagnosticsRegressionComparator {
	public:
		struct Difference {
			std::string field;
			std::string beforeValue;
			std::string afterValue;
		};

		struct ComparisonResult {
			bool equivalent = true;
			std::vector<Difference> differences;
			std::string summary;
		};

		ComparisonResult CompareSelectedFields(
			const DiagnosticsSnapshot& before,
			const DiagnosticsSnapshot& after) const;
	};

} // namespace blazeclaw::core
