#include "gateway/GatewayUtf8CloseReason.h"
#include "gateway/GatewayTestHooks.h"

#include <catch2/catch_all.hpp>

#include <string>

using blazeclaw::gateway::TruncateUtf8CloseReason;
using blazeclaw::gateway::kGatewayCloseReasonMaxUtf8BytesOpenClawPolicy;
using blazeclaw::gateway::kRfc6455CloseReasonMaxUtf8Bytes;
using blazeclaw::gateway::test_hooks::ResetGatewayModelCatalogCacheForTest;

TEST_CASE("TruncateUtf8CloseReason empty maps to OpenClaw default", "[gateway][close-reason]") {
	REQUIRE(TruncateUtf8CloseReason("") == "invalid handshake");
	REQUIRE(TruncateUtf8CloseReason("", 50) == "invalid handshake");
}

TEST_CASE("TruncateUtf8CloseReason short ASCII unchanged", "[gateway][close-reason]") {
	REQUIRE(TruncateUtf8CloseReason("Idle timeout") == "Idle timeout");
}

TEST_CASE("TruncateUtf8CloseReason ASCII capped at OpenClaw policy 120 bytes", "[gateway][close-reason]") {
	std::string longAscii(121, 'a');
	const auto out = TruncateUtf8CloseReason(longAscii, kGatewayCloseReasonMaxUtf8BytesOpenClawPolicy);
	REQUIRE(out.size() == 120);
	REQUIRE(out == std::string(120, 'a'));
}

TEST_CASE("TruncateUtf8CloseReason does not split UTF-8 code point", "[gateway][close-reason]") {
	// U+20AC Euro — 3 UTF-8 bytes each
	const std::string euro = "\xE2\x82\xAC";
	REQUIRE(euro.size() == 3);
	std::string s;
	for (int i = 0; i < 50; ++i) {
		s += euro;
	}
	REQUIRE(s.size() == 150);
	const auto out = TruncateUtf8CloseReason(s, kGatewayCloseReasonMaxUtf8BytesOpenClawPolicy);
	REQUIRE(out.size() == 120);
	// 120 / 3 = 40 complete U+20AC sequences (same as a naive byte prefix of `s`).
	REQUIRE(out == s.substr(0, 120));
}

TEST_CASE("TruncateUtf8CloseReason RFC 123-byte cap", "[gateway][close-reason]") {
	std::string longAscii(130, 'b');
	const auto out = TruncateUtf8CloseReason(longAscii, kRfc6455CloseReasonMaxUtf8Bytes);
	REQUIRE(out.size() == 123);
}

TEST_CASE("TruncateUtf8CloseReason stops on ill-formed UTF-8", "[gateway][close-reason]") {
	std::string s = "ok";
	s.push_back(static_cast<char>(0xFF));
	s += "tail";
	const auto out = TruncateUtf8CloseReason(s, 100);
	REQUIRE(out == "ok");
}

TEST_CASE("TruncateUtf8CloseReason well-formed payload fits RFC close body", "[gateway][close-reason]") {
	const std::uint16_t code = 1001;
	std::string reason(200, 'z');
	const std::string adjusted = TruncateUtf8CloseReason(reason, kGatewayCloseReasonMaxUtf8BytesOpenClawPolicy);
	REQUIRE(adjusted.size() == 120);
	const std::string body = std::string{static_cast<char>((code >> 8) & 0xFF), static_cast<char>(code & 0xFF)} + adjusted;
	REQUIRE(body.size() == 122);
	REQUIRE(body.size() <= blazeclaw::gateway::kRfc6455CloseFrameBodyMaxBytes);
}

TEST_CASE("ResetGatewayModelCatalogCacheForTest is callable no-op", "[gateway][close-reason]") {
	REQUIRE_NOTHROW(ResetGatewayModelCatalogCacheForTest());
}
