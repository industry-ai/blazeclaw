#include "pch.h"
#include "ToolEventRecipientPolicy.h"

#include <algorithm>

namespace blazeclaw::gateway {
	namespace {
		std::string UpperTrim(const std::string& value) {
			std::string out = value;
			out.erase(out.begin(), std::find_if(out.begin(), out.end(), [](unsigned char ch) {
				return !std::isspace(ch);
				}));
			out.erase(std::find_if(out.rbegin(), out.rend(), [](unsigned char ch) {
				return !std::isspace(ch);
				}).base(), out.end());
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
				return static_cast<char>(std::toupper(ch));
				});
			return out;
		}
	}

	ToolEventRecipientPolicy::Output ToolEventRecipientPolicy::Evaluate(
		const Input& input) const {
		if (input.runId.empty() || input.sessionKey.empty()) {
			return Output{
				.wantsToolEvents = false,
				.reasonCode = "missing_run_or_session",
			};
		}

		const bool hasToolEventsCap = std::any_of(
			input.clientCaps.begin(),
			input.clientCaps.end(),
			[](const std::string& cap) {
				return UpperTrim(cap) == "TOOL_EVENTS";
			});
		if (!hasToolEventsCap) {
			return Output{};
		}

		return Output{
			.wantsToolEvents = true,
			.reasonCode = "capability_granted",
		};
	}

} // namespace blazeclaw::gateway
