#pragma once

#include "CronModels.h"

#include <istream>
#include <string>

namespace blazeclaw::cron {

	// Parse JSON with a lightweight JSON5-style fallback (comments, trailing commas).
	CronJson ParseJsonWithJson5Fallback(const std::string& raw);
	CronJson ParseJsonStreamWithJson5Fallback(std::istream& stream);
	bool IsUsableJsonDocument(const CronJson& parsed);

} // namespace blazeclaw::cron
