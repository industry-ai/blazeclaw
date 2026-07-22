#include "pch.h"

#include "../src/app/agent-chat/IrcMessageParser.h"

#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

TEST_CASE("IrcMessageParser parses PRIVMSG lines", "[agent-chat][parser][privmsg]") {
	std::string nick;
	std::string user;
	std::string host;
	std::string channel;
	std::string message;

	const bool ok = blazeclaw::irc::IrcMessageParser::ParsePrivmsgLine(
		":alice!user1@example.com PRIVMSG #room :hello world",
		nick,
		user,
		host,
		channel,
		message);

	REQUIRE(ok);
	REQUIRE(nick == "alice");
	REQUIRE(user == "user1");
	REQUIRE(host == "example.com");
	REQUIRE(channel == "#room");
	REQUIRE(message == "hello world");
}

TEST_CASE("IrcMessageParser parses JOIN/PART lines", "[agent-chat][parser][joinpart]") {
	std::string nick;
	std::string user;
	std::string host;
	std::string channel;

	const bool ok = blazeclaw::irc::IrcMessageParser::ParseJoinPartLine(
		":bob!u2@host.local JOIN :#general",
		nick,
		user,
		host,
		channel);

	REQUIRE(ok);
	REQUIRE(nick == "bob");
	REQUIRE(user == "u2");
	REQUIRE(host == "host.local");
	REQUIRE(channel == "#general");
}

TEST_CASE("IrcMessageParser builds PRIVMSG JSON payload", "[agent-chat][parser][payload]") {
	const std::string payload = blazeclaw::irc::IrcMessageParser::BuildPrivmsgPayload(
		"#room-a",
		"hello \"blazeclaw\"");

	auto json = nlohmann::json::parse(payload);
	REQUIRE(json.value("cmd", std::string()) == "PRIVMSG");
	REQUIRE(json.value("channel", std::string()) == "#room-a");
	REQUIRE(json.value("message", std::string()) == "hello \"blazeclaw\"");
	REQUIRE(json.contains("ts"));
	REQUIRE(json["ts"].is_number_integer());
	REQUIRE(json["ts"].get<std::int64_t>() > 0);
}
