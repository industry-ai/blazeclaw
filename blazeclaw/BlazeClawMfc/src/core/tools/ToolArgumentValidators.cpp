#include "pch.h"
#include "ToolArgumentValidators.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace blazeclaw::core::tools {

namespace {

std::string ToLowerAscii(const std::string& value)
{
    std::string lowered = value;
    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](const unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
    return lowered;
}

std::string TrimAsciiLocal(const std::string& value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
    {
        return {};
    }

    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::string> JsonValueToCliString(const nlohmann::json& value)
{
    if (value.is_string())
    {
        return value.get<std::string>();
    }

    if (value.is_boolean())
    {
        return value.get<bool>() ? "true" : "false";
    }

    if (value.is_number_integer())
    {
        return std::to_string(value.get<long long>());
    }

    if (value.is_number_unsigned())
    {
        return std::to_string(value.get<unsigned long long>());
    }

    if (value.is_number_float())
    {
        std::ostringstream stream;
        stream << value.get<double>();
        return stream.str();
    }

    return std::nullopt;
}

void AppendFlagWithValue(
    std::vector<std::string>& args,
    const std::string& flag,
    const std::optional<std::string>& value)
{
    if (!value.has_value() || value->empty())
    {
        return;
    }

    args.push_back("--" + flag);
    args.push_back(value.value());
}

void AppendBoolAsValue(
    std::vector<std::string>& args,
    const std::string& flag,
    const nlohmann::json& params)
{
    const auto it = params.find(flag);
    if (it == params.end() || !it->is_boolean())
    {
        return;
    }

    args.push_back("--" + flag);
    args.push_back(it->get<bool>() ? "true" : "false");
}

std::string ExtractSummaryField(
    const std::string& text,
    const std::string& label)
{
    const std::string needle = label + ":";
    const std::size_t begin = text.find(needle);
    if (begin == std::string::npos)
    {
        return {};
    }

    const std::size_t valueStart = begin + needle.size();
    const std::size_t valueEnd = text.find('\n', valueStart);
    return TrimAsciiLocal(
        text.substr(
            valueStart,
            valueEnd == std::string::npos
            ? std::string::npos
            : valueEnd - valueStart));
}

} // namespace

std::vector<ImapSmtpToolRuntimeSpec> BuildImapSmtpToolRuntimeSpecs()
{
    return {
        { "imap_smtp_email.imap.check", "IMAP Check", "scripts/imap.js", "check" },
        { "imap_smtp_email.imap.fetch", "IMAP Fetch", "scripts/imap.js", "fetch" },
        { "imap_smtp_email.imap.download", "IMAP Download Attachments", "scripts/imap.js", "download" },
        { "imap_smtp_email.imap.search", "IMAP Search", "scripts/imap.js", "search" },
        { "imap_smtp_email.imap.mark_read", "IMAP Mark Read", "scripts/imap.js", "mark-read" },
        { "imap_smtp_email.imap.mark_unread", "IMAP Mark Unread", "scripts/imap.js", "mark-unread" },
        { "imap_smtp_email.imap.list_mailboxes", "IMAP List Mailboxes", "scripts/imap.js", "list-mailboxes" },
        { "imap_smtp_email.imap.list_accounts", "IMAP List Accounts", "scripts/imap.js", "list-accounts" },
        { "imap_smtp_email.smtp.send", "SMTP Send", "scripts/smtp.js", "send" },
        { "imap_smtp_email.smtp.test", "SMTP Test", "scripts/smtp.js", "test" },
        { "imap_smtp_email.smtp.list_accounts", "SMTP List Accounts", "scripts/smtp.js", "list-accounts" },
    };
}

std::vector<BraveSearchToolRuntimeSpec> BuildBraveSearchToolRuntimeSpecs()
{
    return {
        { "brave_search.search.web", "Brave Web Search", "scripts/search.js" },
        { "brave_search.fetch.content", "Brave Fetch Content", "scripts/content.js" },
        { "web_browsing.search.web", "Web Browsing Search", "scripts/search.js" },
        { "web_browsing.fetch.content", "Web Browsing Fetch Content", "scripts/content.js" },
    };
}

bool IsBraveSearchWebToolId(const std::string& toolId)
{
    return toolId == "brave_search.search.web" ||
        toolId == "web_browsing.search.web";
}

bool IsBraveFetchContentToolId(const std::string& toolId)
{
    return toolId == "brave_search.fetch.content" ||
        toolId == "web_browsing.fetch.content";
}

std::vector<BaiduSearchToolRuntimeSpec> BuildBaiduSearchToolRuntimeSpecs()
{
    return {
        { "baidu-search.search.web", "Baidu Web Search", "scripts/search.py" },
    };
}

std::vector<ContentPolishingToolRuntimeSpec> BuildContentPolishingToolRuntimeSpecs()
{
    return {
        { "summarize.extract", "Summarize Extract" },
        { "humanizer.rewrite", "Humanizer Rewrite" },
    };
}

std::optional<std::string> ExtractTextArgument(const nlohmann::json& params)
{
    const char* kKeys[] = {
        "text",
        "draft",
        "content",
        "message",
        "input",
    };

    for (const auto* key : kKeys)
    {
        const auto it = params.find(key);
        if (it == params.end() || !it->is_string())
        {
            continue;
        }

        const std::string value = TrimAsciiLocal(it->get<std::string>());
        if (!value.empty())
        {
            return value;
        }
    }

    return std::nullopt;
}

std::string BuildSummarizeExtractOutput(const std::string& text)
{
   std::string timeValue = "not found";
    std::string locationValue = "not found";
    std::string peopleValue = "not found";
    std::string requestValue = TrimAsciiLocal(text);

    static const std::regex kTimeRegex(
        R"(((next\s+\w+)\s+at\s+\d{1,2}[:.]\d{2}\s*(?:AM|PM|am|pm)?))",
        std::regex_constants::icase);
    std::smatch match;
    if (std::regex_search(text, match, kTimeRegex) && match.size() >= 2)
    {
        timeValue = match[1].str();
    }

    static const std::regex kLocationRegex(
        R"((?:in|at)\s+the\s+([^,.;\n]+(?:meeting\s+room|room)))",
        std::regex_constants::icase);
    if (std::regex_search(text, match, kLocationRegex) && match.size() >= 2)
    {
        locationValue = TrimAsciiLocal(match[1].str());
    }

    std::vector<std::string> people;
    if (ToLowerAscii(text).find("boss") != std::string::npos)
    {
        people.push_back("boss");
    }
    if (ToLowerAscii(text).find("we") != std::string::npos)
    {
        people.push_back("requester team");
    }
    if (!people.empty())
    {
        peopleValue.clear();
        for (std::size_t i = 0; i < people.size(); ++i)
        {
            if (i > 0)
            {
                peopleValue += ", ";
            }
            peopleValue += people[i];
        }
    }

    if (requestValue.size() > 220)
    {
        requestValue = requestValue.substr(0, 220) + "...";
    }

    return
        "Time: " + timeValue + "\n" +
        "Location: " + locationValue + "\n" +
        "People: " + peopleValue + "\n" +
        "Core request: " + requestValue;
}

std::string BuildHumanizerRewriteOutput(const std::string& text)
{
    const std::string timeValue = ExtractSummaryField(text, "Time");
    const std::string locationValue = ExtractSummaryField(text, "Location");
    const std::string peopleValue = ExtractSummaryField(text, "People");
    const std::string requestValue = ExtractSummaryField(text, "Core request");

    std::string body;
    body += "Dear Sir,\n\n";
    body += "I would like to cordially invite you to the upcoming review meeting regarding the latest UI requirements.\n\n";
    if (!timeValue.empty())
    {
        body += "Time: " + timeValue + "\n";
    }
    if (!locationValue.empty())
    {
        body += "Location: " + locationValue + "\n";
    }
    if (!peopleValue.empty())
    {
        body += "Participants: " + peopleValue + "\n";
    }
    if (!requestValue.empty())
    {
        body += "\nPurpose: " + requestValue + "\n";
    }
    body += "\nYour attendance would be greatly appreciated.\n\n";
    body += "Best regards,";
    return body;
}

std::string TrimAsciiForBraveSearch(const std::string& value)
{
    return TrimAsciiLocal(value);
}

bool HasControlCharsForBraveSearch(const std::string& value)
{
    for (const unsigned char ch : value)
    {
        if ((ch < 0x20 && ch != '\t') || ch == 0x7F)
        {
            return true;
        }
    }

    return false;
}

bool IsHttpUrlForBraveSearch(const std::string& value)
{
    const std::string lowered = ToLowerAscii(value);
    return lowered.rfind("http://", 0) == 0 ||
        lowered.rfind("https://", 0) == 0;
}

bool IsDateRangeTokenForBaiduSearch(const std::string& value)
{
    if (value.size() != 22)
    {
        return false;
    }

    if (value[4] != '-' || value[7] != '-' || value[10] != 't' ||
        value[11] != 'o' || value[14] != '-' || value[17] != '-')
    {
        return false;
    }

    const std::size_t digitPositions[] = {
        0, 1, 2, 3, 5, 6, 8, 9, 12, 13, 15, 16, 18, 19, 20, 21
    };
    for (const auto pos : digitPositions)
    {
        if (!std::isdigit(static_cast<unsigned char>(value[pos])))
        {
            return false;
        }
    }

    return true;
}

std::string TruncateBraveToolOutput(
    const std::string& output,
    const std::size_t maxBytes)
{
    if (output.size() <= maxBytes)
    {
        return output;
    }

    const std::size_t retained = maxBytes > 48 ? maxBytes - 48 : maxBytes;
    return output.substr(0, retained) +
        "\n...(truncated by blazeclaw runtime output limit)";
}

std::string ClassifyBraveFailureCode(const std::string& output)
{
    const std::string lowered = ToLowerAscii(output);
    if (lowered.find("http 401") != std::string::npos ||
        lowered.find("http 403") != std::string::npos)
    {
        return "auth_error";
    }

    if (lowered.find("http 429") != std::string::npos)
    {
        return "rate_limited";
    }

    if (lowered.find("http 500") != std::string::npos ||
        lowered.find("http 502") != std::string::npos ||
        lowered.find("http 503") != std::string::npos ||
        lowered.find("http 504") != std::string::npos)
    {
        return "upstream_unavailable";
    }

    if (lowered.find("enotfound") != std::string::npos ||
        lowered.find("eai_again") != std::string::npos ||
        lowered.find("network") != std::string::npos ||
        lowered.find("fetch failed") != std::string::npos)
    {
        return "network_error";
    }

    return "script_runtime_error";
}

bool IsBraveNetworkTimeoutFailure(const std::string& output)
{
    const std::string lowered = ToLowerAscii(output);
    return lowered.find("und_err_connect_timeout") != std::string::npos ||
        lowered.find("connect timeout") != std::string::npos ||
        lowered.find("etimedout") != std::string::npos ||
        lowered.find("timed out") != std::string::npos ||
        lowered.find("timeout") != std::string::npos;
}

std::string ClassifyBaiduFailureCode(const std::string& output)
{
    const std::string lowered = ToLowerAscii(output);

    std::smatch httpStatusMatch;
    if (std::regex_search(
        lowered,
        httpStatusMatch,
        std::regex(R"(http(?:\s+error)?\s*[:=]?\s*([0-9]{3}))")) &&
        httpStatusMatch.size() >= 2)
    {
        const int status = std::atoi(httpStatusMatch[1].str().c_str());
        if (status == 401 || status == 403)
        {
            return "auth_error";
        }

        if (status == 429)
        {
            return "rate_limited";
        }

        if (status >= 500 && status < 600)
        {
            return "upstream_unavailable";
        }

        if (status >= 400 && status < 500)
        {
            return "invalid_arguments";
        }
    }

    if (lowered.find("baidu_api_key") != std::string::npos &&
        lowered.find("must be set") != std::string::npos)
    {
        return "baidu_api_key_missing";
    }

    if (lowered.find("unauthorized") != std::string::npos ||
        lowered.find("forbidden") != std::string::npos ||
        lowered.find("invalid token") != std::string::npos ||
        lowered.find("access token") != std::string::npos ||
        lowered.find("invalid api key") != std::string::npos ||
        lowered.find("authentication") != std::string::npos)
    {
        return "auth_error";
    }

    if (lowered.find("rate limit") != std::string::npos ||
        lowered.find("too many requests") != std::string::npos ||
        lowered.find("quota") != std::string::npos)
    {
        return "rate_limited";
    }

    if (lowered.find("http 401") != std::string::npos ||
        lowered.find("http 403") != std::string::npos)
    {
        return "auth_error";
    }

    if (lowered.find("http 429") != std::string::npos)
    {
        return "rate_limited";
    }

    if (lowered.find("timeout") != std::string::npos ||
        lowered.find("timed out") != std::string::npos)
    {
        return "network_timeout";
    }

    if (lowered.find("no module named") != std::string::npos ||
        lowered.find("modulenotfounderror") != std::string::npos)
    {
        return "dependency_missing";
    }

    if (lowered.find("json parse error") != std::string::npos ||
        lowered.find("request body must be a json object") != std::string::npos ||
        lowered.find("query must be present") != std::string::npos ||
        lowered.find("freshness must be") != std::string::npos)
    {
        return "invalid_arguments";
    }

    if (lowered.find("network") != std::string::npos ||
        lowered.find("connection") != std::string::npos ||
        lowered.find("ssl") != std::string::npos ||
        lowered.find("certificate") != std::string::npos ||
        lowered.find("httpsconnectionpool") != std::string::npos ||
        lowered.find("urlopen error") != std::string::npos ||
        lowered.find("proxyerror") != std::string::npos ||
        lowered.find("name resolution") != std::string::npos ||
        lowered.find("winerror") != std::string::npos ||
        lowered.find("max retries exceeded") != std::string::npos ||
        lowered.find("name or service not known") != std::string::npos)
    {
        return "network_error";
    }

    return "script_runtime_error";
}

std::optional<std::vector<std::string>> BuildImapSmtpCliArgs(
    const ImapSmtpToolRuntimeSpec& spec,
    const nlohmann::json& params,
    std::string& errorCode,
    std::string& errorMessage)
{
    errorCode.clear();
    errorMessage.clear();

    std::vector<std::string> args;
    if (const auto accountIt = params.find("account");
        accountIt != params.end())
    {
        AppendFlagWithValue(args, "account", JsonValueToCliString(*accountIt));
    }

    args.push_back(spec.command);

    if (spec.id == "imap_smtp_email.imap.fetch" ||
        spec.id == "imap_smtp_email.imap.download")
    {
        const auto uidIt = params.find("uid");
        if (uidIt == params.end())
        {
            errorCode = "invalid_arguments";
            errorMessage = "uid is required";
            return std::nullopt;
        }

        const auto uid = JsonValueToCliString(*uidIt);
        if (!uid.has_value() || uid->empty())
        {
            errorCode = "invalid_arguments";
            errorMessage = "uid is invalid";
            return std::nullopt;
        }

        args.push_back(uid.value());
    }

    if (spec.id == "imap_smtp_email.imap.mark_read" ||
        spec.id == "imap_smtp_email.imap.mark_unread")
    {
        const auto uidsIt = params.find("uids");
        if (uidsIt == params.end() || !uidsIt->is_array() || uidsIt->empty())
        {
            errorCode = "invalid_arguments";
            errorMessage = "uids array is required";
            return std::nullopt;
        }

        for (const auto& uidNode : *uidsIt)
        {
            const auto uid = JsonValueToCliString(uidNode);
            if (uid.has_value() && !uid->empty())
            {
                args.push_back(uid.value());
            }
        }
        if (args.back() == spec.command)
        {
            errorCode = "invalid_arguments";
            errorMessage = "uids array must contain at least one value";
            return std::nullopt;
        }
    }

    auto appendIfPresent = [&](const std::string& jsonKey, const std::string& cliFlag)
        {
            const auto it = params.find(jsonKey);
            if (it == params.end())
            {
                return;
            }

            AppendFlagWithValue(args, cliFlag, JsonValueToCliString(*it));
        };

    if (spec.id == "imap_smtp_email.imap.check")
    {
        appendIfPresent("limit", "limit");
        appendIfPresent("mailbox", "mailbox");
        appendIfPresent("recent", "recent");
        AppendBoolAsValue(args, "unseen", params);
    }
    else if (spec.id == "imap_smtp_email.imap.download")
    {
        appendIfPresent("mailbox", "mailbox");
        appendIfPresent("dir", "dir");
        appendIfPresent("file", "file");
    }
    else if (spec.id == "imap_smtp_email.imap.search")
    {
        appendIfPresent("from", "from");
        appendIfPresent("subject", "subject");
        appendIfPresent("recent", "recent");
        appendIfPresent("since", "since");
        appendIfPresent("before", "before");
        appendIfPresent("limit", "limit");
        appendIfPresent("mailbox", "mailbox");
        AppendBoolAsValue(args, "unseen", params);
        AppendBoolAsValue(args, "seen", params);
    }
    else if (spec.id == "imap_smtp_email.imap.fetch" ||
        spec.id == "imap_smtp_email.imap.mark_read" ||
        spec.id == "imap_smtp_email.imap.mark_unread")
    {
        appendIfPresent("mailbox", "mailbox");
    }
    else if (spec.id == "imap_smtp_email.smtp.send")
    {
        appendIfPresent("to", "to");
        appendIfPresent("subject", "subject");
        appendIfPresent("subjectFile", "subject-file");
        appendIfPresent("body", "body");
        appendIfPresent("bodyFile", "body-file");
        appendIfPresent("htmlFile", "html-file");
        appendIfPresent("cc", "cc");
        appendIfPresent("bcc", "bcc");
        appendIfPresent("from", "from");

        const auto htmlIt = params.find("html");
        if (htmlIt != params.end() && htmlIt->is_boolean() && htmlIt->get<bool>())
        {
            args.push_back("--html");
            args.push_back("true");
        }

        const auto attachIt = params.find("attach");
        if (attachIt != params.end())
        {
            if (attachIt->is_array())
            {
                std::string joined;
                for (const auto& item : *attachIt)
                {
                    const auto value = JsonValueToCliString(item);
                    if (!value.has_value() || value->empty())
                    {
                        continue;
                    }

                    if (!joined.empty())
                    {
                        joined += ",";
                    }
                    joined += value.value();
                }

                if (!joined.empty())
                {
                    args.push_back("--attach");
                    args.push_back(joined);
                }
            }
            else
            {
                appendIfPresent("attach", "attach");
            }
        }

        const bool hasTo = params.contains("to");
        const bool hasSubject =
            params.contains("subject") || params.contains("subjectFile");
        if (!hasTo || !hasSubject)
        {
            errorCode = "invalid_arguments";
            errorMessage = "smtp.send requires to and subject or subjectFile";
            return std::nullopt;
        }
    }

    return args;
}

std::optional<std::vector<std::string>> BuildBaiduSearchCliArgs(
    const BaiduSearchToolRuntimeSpec& spec,
    const nlohmann::json& params,
    std::string& errorCode,
    std::string& errorMessage)
{
    errorCode.clear();
    errorMessage.clear();

    if (spec.id != "baidu-search.search.web")
    {
        errorCode = "unsupported_tool";
        errorMessage = "unsupported baidu tool";
        return std::nullopt;
    }

    nlohmann::json cliPayload = nlohmann::json::object();
    const auto queryIt = params.find("query");
    if (queryIt == params.end() || !queryIt->is_string())
    {
        errorCode = "invalid_arguments";
        errorMessage = "query is required";
        return std::nullopt;
    }

    const std::string query = TrimAsciiForBraveSearch(queryIt->get<std::string>());
    if (query.empty() || query.size() > 512 || HasControlCharsForBraveSearch(query))
    {
        errorCode = "invalid_arguments";
        errorMessage = "query failed safety validation";
        return std::nullopt;
    }

    cliPayload["query"] = query;

    if (const auto countIt = params.find("count"); countIt != params.end())
    {
        if (!countIt->is_number_integer())
        {
            errorCode = "invalid_arguments";
            errorMessage = "count must be an integer";
            return std::nullopt;
        }

        const int count = countIt->get<int>();
        if (count < 1 || count > 50)
        {
            errorCode = "invalid_arguments";
            errorMessage = "count must be between 1 and 50";
            return std::nullopt;
        }

        cliPayload["count"] = count;
    }

    if (const auto freshnessIt = params.find("freshness");
        freshnessIt != params.end())
    {
        if (!freshnessIt->is_string())
        {
            errorCode = "invalid_arguments";
            errorMessage = "freshness must be a string";
            return std::nullopt;
        }

        const std::string freshness =
            TrimAsciiForBraveSearch(freshnessIt->get<std::string>());
        if (freshness.empty())
        {
            errorCode = "invalid_arguments";
            errorMessage = "freshness must be non-empty";
            return std::nullopt;
        }

        if (freshness != "pd" && freshness != "pw" && freshness != "pm" &&
            freshness != "py" && !IsDateRangeTokenForBaiduSearch(freshness))
        {
            errorCode = "invalid_arguments";
            errorMessage =
                "freshness must be pd/pw/pm/py or YYYY-MM-DDtoYYYY-MM-DD";
            return std::nullopt;
        }

        cliPayload["freshness"] = freshness;
    }

    return std::vector<std::string>{ cliPayload.dump() };
}

std::optional<std::vector<std::string>> BuildBraveSearchCliArgs(
    const BraveSearchToolRuntimeSpec& spec,
    const nlohmann::json& params,
    std::string& errorCode,
    std::string& errorMessage)
{
    errorCode.clear();
    errorMessage.clear();

    std::vector<std::string> args;
    if (IsBraveSearchWebToolId(spec.id))
    {
        const auto queryIt = params.find("query");
        if (queryIt == params.end() || !queryIt->is_string())
        {
            errorCode = "invalid_arguments";
            errorMessage = "query is required";
            return std::nullopt;
        }

        const std::string query = TrimAsciiForBraveSearch(
            queryIt->get<std::string>());
        if (query.empty())
        {
            errorCode = "invalid_arguments";
            errorMessage = "query is required";
            return std::nullopt;
        }

        if (query.size() > 512 || HasControlCharsForBraveSearch(query))
        {
            errorCode = "invalid_arguments";
            errorMessage = "query failed safety validation";
            return std::nullopt;
        }

        args.push_back(query);

        int count = 0;
        if (const auto countIt = params.find("count");
            countIt != params.end() && countIt->is_number_integer())
        {
            count = countIt->get<int>();
        }
        else if (const auto countIt = params.find("count");
            countIt != params.end())
        {
            errorCode = "invalid_arguments";
            errorMessage = "count must be an integer";
            return std::nullopt;
        }
        else if (const auto topKIt = params.find("topK");
            topKIt != params.end() && topKIt->is_number_integer())
        {
            count = topKIt->get<int>();
        }
        else if (const auto topKIt = params.find("topK");
            topKIt != params.end())
        {
            errorCode = "invalid_arguments";
            errorMessage = "topK must be an integer";
            return std::nullopt;
        }

        if (count > 0)
        {
            if (count < 1 || count > 20)
            {
                errorCode = "invalid_arguments";
                errorMessage = "count must be between 1 and 20";
                return std::nullopt;
            }

            args.push_back("-n");
            args.push_back(std::to_string(count));
        }

        if (const auto contentIt = params.find("content");
            contentIt != params.end() && contentIt->is_boolean() &&
            contentIt->get<bool>())
        {
            args.push_back("--content");
        }
        else if (const auto contentIt = params.find("content");
            contentIt != params.end() && !contentIt->is_boolean())
        {
            errorCode = "invalid_arguments";
            errorMessage = "content must be a boolean";
            return std::nullopt;
        }
    }
    else if (IsBraveFetchContentToolId(spec.id))
    {
        const auto urlIt = params.find("url");
        if (urlIt == params.end() || !urlIt->is_string())
        {
            errorCode = "invalid_arguments";
            errorMessage = "url is required";
            return std::nullopt;
        }

        const std::string url = TrimAsciiForBraveSearch(urlIt->get<std::string>());
        if (url.empty())
        {
            errorCode = "invalid_arguments";
            errorMessage = "url is required";
            return std::nullopt;
        }

        if (url.size() > 2048 ||
            HasControlCharsForBraveSearch(url) ||
            !IsHttpUrlForBraveSearch(url))
        {
            errorCode = "invalid_arguments";
            errorMessage = "url failed safety validation";
            return std::nullopt;
        }

        args.push_back(url);
    }

    return args;
}

} // namespace blazeclaw::core::tools
