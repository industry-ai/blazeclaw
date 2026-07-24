#include "pch.h"

#include "../src/core/ServiceManagerRoutingIntentHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("CanonicalizeForRouting appends tokens for detected fragments", "[servicemanager][routing]")
{
	const std::string input = "Please check my 收件箱 and 回复 within 两小时";
	const std::string out = blazeclaw::core::servicemanager_routing_intent::CanonicalizeForRouting(input);
	REQUIRE(out.find("inbox") != std::string::npos);
	REQUIRE(out.find("reply") != std::string::npos);
	REQUIRE(out.find("within 2 hours") != std::string::npos);
}

TEST_CASE("LooksLikeInboxReplyUrgencyIntent detects combined inbox+reply or inbox+urgency", "[servicemanager][routing]")
{
	REQUIRE(blazeclaw::core::servicemanager_routing_intent::LooksLikeInboxReplyUrgencyIntent("New email in inbox, please reply urgent") == true);
	REQUIRE(blazeclaw::core::servicemanager_routing_intent::LooksLikeInboxReplyUrgencyIntent("random text without signals") == false);
}

TEST_CASE("LooksLikeInboxIntentAnyLanguage detects Chinese inbox+reply", "[servicemanager][routing]")
{
	REQUIRE(blazeclaw::core::servicemanager_routing_intent::LooksLikeInboxIntentAnyLanguage("请查看收件箱并回复") == true);
}

TEST_CASE("LooksLikeTwoHourUrgencyAnyLanguage detects 2-hour signals", "[servicemanager][routing]")
{
	REQUIRE(blazeclaw::core::servicemanager_routing_intent::LooksLikeTwoHourUrgencyAnyLanguage("两小时内回复") == true);
	REQUIRE(blazeclaw::core::servicemanager_routing_intent::LooksLikeTwoHourUrgencyAnyLanguage("no time mentioned") == false);
}
