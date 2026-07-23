#pragma once

#include <string>

namespace blazeclaw::core::servicemanager_routing_intent {

	std::string CanonicalizeForRouting(const std::string& message);
	bool LooksLikeInboxReplyUrgencyIntent(const std::string& message);
	bool LooksLikeInboxIntentAnyLanguage(const std::string& message);
	bool LooksLikeTwoHourUrgencyAnyLanguage(const std::string& message);

} // namespace blazeclaw::core::servicemanager_routing_intent
