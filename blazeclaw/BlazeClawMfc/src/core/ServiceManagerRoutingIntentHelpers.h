#pragma once

#include <string>

namespace blazeclaw::core::servicemanager_routing_intent {

	// Invariant: preserves original UTF-8 text and only appends canonical routing
	// tokens when the detected signal token is not already present.
	std::string CanonicalizeForRouting(const std::string& message);

	// Invariant: returns true only when the message contains inbox +
	// (reply or urgency) signals across supported English/Chinese fragments.
	bool LooksLikeInboxReplyUrgencyIntent(const std::string& message);

	// Invariant: returns true only when both inbox and reply signals are present
	// across supported English/Chinese fragments.
	bool LooksLikeInboxIntentAnyLanguage(const std::string& message);

	// Invariant: returns true only when a "within two hours" urgency signal is
	// detected across supported English/Chinese fragments.
	bool LooksLikeTwoHourUrgencyAnyLanguage(const std::string& message);

} // namespace blazeclaw::core::servicemanager_routing_intent
