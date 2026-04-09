#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace blazeclaw::core::tools {

struct ImapSmtpToolRuntimeSpec {
    std::string id;
    std::string label;
    std::string script;
    std::string command;
};

struct BraveSearchToolRuntimeSpec {
    std::string id;
    std::string label;
    std::string script;
};

struct BaiduSearchToolRuntimeSpec {
    std::string id;
    std::string label;
    std::string script;
};

struct ContentPolishingToolRuntimeSpec {
    std::string id;
    std::string label;
};

std::vector<ImapSmtpToolRuntimeSpec> BuildImapSmtpToolRuntimeSpecs();
std::vector<BraveSearchToolRuntimeSpec> BuildBraveSearchToolRuntimeSpecs();
std::vector<BaiduSearchToolRuntimeSpec> BuildBaiduSearchToolRuntimeSpecs();
std::vector<ContentPolishingToolRuntimeSpec> BuildContentPolishingToolRuntimeSpecs();

bool IsBraveSearchWebToolId(const std::string& toolId);
bool IsBraveFetchContentToolId(const std::string& toolId);

std::optional<std::string> ExtractTextArgument(const nlohmann::json& params);
std::string BuildSummarizeExtractOutput(const std::string& text);
std::string BuildHumanizerRewriteOutput(const std::string& text);

std::string TrimAsciiForBraveSearch(const std::string& value);
bool HasControlCharsForBraveSearch(const std::string& value);
bool IsHttpUrlForBraveSearch(const std::string& value);
bool IsDateRangeTokenForBaiduSearch(const std::string& value);

std::string TruncateBraveToolOutput(
    const std::string& output,
    std::size_t maxBytes = 24000);
std::string ClassifyBraveFailureCode(const std::string& output);
bool IsBraveNetworkTimeoutFailure(const std::string& output);
std::string ClassifyBaiduFailureCode(const std::string& output);

std::optional<std::vector<std::string>> BuildImapSmtpCliArgs(
    const ImapSmtpToolRuntimeSpec& spec,
    const nlohmann::json& params,
    std::string& errorCode,
    std::string& errorMessage);

std::optional<std::vector<std::string>> BuildBaiduSearchCliArgs(
    const BaiduSearchToolRuntimeSpec& spec,
    const nlohmann::json& params,
    std::string& errorCode,
    std::string& errorMessage);

std::optional<std::vector<std::string>> BuildBraveSearchCliArgs(
    const BraveSearchToolRuntimeSpec& spec,
    const nlohmann::json& params,
    std::string& errorCode,
    std::string& errorMessage);

} // namespace blazeclaw::core::tools
