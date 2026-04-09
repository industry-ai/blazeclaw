#include "core/tools/CToolRuntimeRegistry.h"
#include "core/tools/ToolArgumentValidators.h"

#include <catch2/catch_all.hpp>

TEST_CASE("Tool runtime spec builders expose expected tool ids", "[tools][runtime][registry]") {
    const auto imapSpecs = blazeclaw::core::tools::BuildImapSmtpToolRuntimeSpecs();
    const auto braveSpecs = blazeclaw::core::tools::BuildBraveSearchToolRuntimeSpecs();
    const auto baiduSpecs = blazeclaw::core::tools::BuildBaiduSearchToolRuntimeSpecs();
    const auto polishSpecs = blazeclaw::core::tools::BuildContentPolishingToolRuntimeSpecs();

    REQUIRE(!imapSpecs.empty());
    REQUIRE(!braveSpecs.empty());
    REQUIRE(!baiduSpecs.empty());
    REQUIRE(!polishSpecs.empty());

    REQUIRE(std::find_if(
        imapSpecs.begin(),
        imapSpecs.end(),
        [](const auto& spec) { return spec.id == "imap_smtp_email.smtp.send"; }) !=
        imapSpecs.end());
    REQUIRE(std::find_if(
        braveSpecs.begin(),
        braveSpecs.end(),
        [](const auto& spec) { return spec.id == "web_browsing.search.web"; }) !=
        braveSpecs.end());
    REQUIRE(baiduSpecs.front().id == "baidu-search.search.web");
    REQUIRE(std::find_if(
        polishSpecs.begin(),
        polishSpecs.end(),
        [](const auto& spec) { return spec.id == "humanizer.rewrite"; }) !=
        polishSpecs.end());
}

TEST_CASE("Tool argument validators preserve error taxonomy", "[tools][runtime][validators]") {
    std::string errorCode;
    std::string errorMessage;

    const blazeclaw::core::tools::ImapSmtpToolRuntimeSpec imapFetch{
        .id = "imap_smtp_email.imap.fetch",
        .label = "IMAP Fetch",
        .script = "scripts/imap.js",
        .command = "fetch",
    };
    const auto imapArgs = blazeclaw::core::tools::BuildImapSmtpCliArgs(
        imapFetch,
        nlohmann::json::object(),
        errorCode,
        errorMessage);
    REQUIRE(!imapArgs.has_value());
    REQUIRE(errorCode == "invalid_arguments");
    REQUIRE(errorMessage == "uid is required");

    const blazeclaw::core::tools::BraveSearchToolRuntimeSpec braveFetch{
        .id = "brave_search.fetch.content",
        .label = "Brave Fetch Content",
        .script = "scripts/content.js",
    };
    errorCode.clear();
    errorMessage.clear();
    const auto braveArgs = blazeclaw::core::tools::BuildBraveSearchCliArgs(
        braveFetch,
        nlohmann::json::object({ {"url", "ftp://invalid"} }),
        errorCode,
        errorMessage);
    REQUIRE(!braveArgs.has_value());
    REQUIRE(errorCode == "invalid_arguments");
    REQUIRE(errorMessage == "url failed safety validation");

    const blazeclaw::core::tools::BaiduSearchToolRuntimeSpec baiduSearch{
        .id = "baidu-search.search.web",
        .label = "Baidu Web Search",
        .script = "scripts/search.py",
    };
    errorCode.clear();
    errorMessage.clear();
    const auto baiduArgs = blazeclaw::core::tools::BuildBaiduSearchCliArgs(
        baiduSearch,
        nlohmann::json::object({
            {"query", "blazeclaw"},
            {"freshness", "bad-range"},
        }),
        errorCode,
        errorMessage);
    REQUIRE(!baiduArgs.has_value());
    REQUIRE(errorCode == "invalid_arguments");
    REQUIRE(errorMessage.find("freshness") != std::string::npos);
}

TEST_CASE("Tool runtime classifiers and truncation keep behavior parity", "[tools][runtime][classifiers]") {
    const std::string longOutput(26050, 'a');
    const auto truncated = blazeclaw::core::tools::TruncateBraveToolOutput(longOutput);
    REQUIRE(truncated.size() < longOutput.size());
    REQUIRE(truncated.find("truncated by blazeclaw runtime output limit") != std::string::npos);

    REQUIRE(
        blazeclaw::core::tools::ClassifyBraveFailureCode("HTTP 429 from brave") ==
        "rate_limited");
    REQUIRE(
        blazeclaw::core::tools::ClassifyBaiduFailureCode("HTTP error: 503") ==
        "upstream_unavailable");
    REQUIRE(
        blazeclaw::core::tools::IsBraveNetworkTimeoutFailure("UND_ERR_CONNECT_TIMEOUT"));
}

TEST_CASE("CToolRuntimeRegistry invokes dependency registrations", "[tools][runtime][registry]") {
    blazeclaw::core::CToolRuntimeRegistry registry;
    blazeclaw::gateway::GatewayHost host;

    int callCount = 0;
    blazeclaw::core::CToolRuntimeRegistry::Dependencies deps;
    deps.registerImapSmtp = [&](blazeclaw::gateway::GatewayHost&) { ++callCount; };
    deps.registerContentPolishing = [&](blazeclaw::gateway::GatewayHost&) { ++callCount; };
    deps.registerBraveSearch = [&](blazeclaw::gateway::GatewayHost&) { ++callCount; };
    deps.registerBaiduSearch = [&](blazeclaw::gateway::GatewayHost&) { ++callCount; };

    registry.RegisterAll(host, deps);
    REQUIRE(callCount == 4);
}
