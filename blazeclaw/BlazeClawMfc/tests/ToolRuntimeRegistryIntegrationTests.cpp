#include "core/tools/CToolRuntimeRegistry.h"
#include "core/tools/ToolArgumentValidators.h"
#include "gateway/GatewayRequestParams.h"
#include "gateway/GatewayHost.h"
#include "gateway/GatewayToolRegistry.h"

#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {
	class ScopedEnvVar {
	public:
		ScopedEnvVar(const char* name, const char* value)
			: m_name(name), m_hadOriginal(false) {
			const char* existing = std::getenv(name);
			if (existing != nullptr) {
				m_hadOriginal = true;
				m_original = existing;
			}
			_putenv_s(name, value != nullptr ? value : "");
		}

		~ScopedEnvVar() {
			if (m_hadOriginal) {
				_putenv_s(m_name.c_str(), m_original.c_str());
			}
			else {
				_putenv_s(m_name.c_str(), "");
			}
		}

	private:
		std::string m_name;
		std::string m_original;
		bool m_hadOriginal;
	};
}

TEST_CASE("Tool runtime spec builders expose expected tool ids", "[tools][runtime][registry]") {
	const auto imapSpecs = blazeclaw::core::tools::BuildImapSmtpToolRuntimeSpecs();
	const auto braveSpecs = blazeclaw::core::tools::BuildBraveSearchToolRuntimeSpecs();
	const auto baiduSpecs = blazeclaw::core::tools::BuildBaiduSearchToolRuntimeSpecs();
	const auto polishSpecs = blazeclaw::core::tools::BuildContentPolishingToolRuntimeSpecs();
	const auto nanoPdfSpecs = blazeclaw::core::tools::BuildNanoPdfToolRuntimeSpecs();

	REQUIRE(!imapSpecs.empty());
	REQUIRE(!braveSpecs.empty());
	REQUIRE(!baiduSpecs.empty());
	REQUIRE(!polishSpecs.empty());
	REQUIRE(!nanoPdfSpecs.empty());

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
	REQUIRE(std::find_if(
		nanoPdfSpecs.begin(),
		nanoPdfSpecs.end(),
		[](const auto& spec) { return spec.id == "nano_pdf.edit"; }) !=
		nanoPdfSpecs.end());
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

	const blazeclaw::core::tools::NanoPdfToolRuntimeSpec nanoPdfEdit{
		.id = "nano_pdf.edit",
		.label = "Nano PDF Edit",
		.script = "scripts/nano_pdf_bridge.py",
	};
	errorCode.clear();
	errorMessage.clear();
	const auto nanoPdfArgsMissingInstruction = blazeclaw::core::tools::BuildNanoPdfCliArgs(
		nanoPdfEdit,
		nlohmann::json::object({
			{"inputPath", "deck.pdf"},
			{"pageIndex", 1},
			}),
			errorCode,
			errorMessage);
	REQUIRE(!nanoPdfArgsMissingInstruction.has_value());
	REQUIRE(errorCode == "invalid_args");
	REQUIRE(errorMessage == "instruction is required");

	errorCode.clear();
	errorMessage.clear();
	const auto nanoPdfArgsValid = blazeclaw::core::tools::BuildNanoPdfCliArgs(
		nanoPdfEdit,
		nlohmann::json::object({
			{"inputPath", "deck.pdf"},
			{"pageIndex", 0},
			{"instruction", "Replace title text"},
			{"outputPath", "deck.edited.pdf"},
			}),
			errorCode,
			errorMessage);
	REQUIRE(nanoPdfArgsValid.has_value());
	REQUIRE_FALSE(nanoPdfArgsValid->empty());
	REQUIRE(errorCode.empty());
	REQUIRE(errorMessage.empty());
}

TEST_CASE("Content polishing extracts quoted draft over control instructions", "[tools][runtime][polish][extract]") {
	const std::string wrappedChinesePrompt = R"(请执行内容润色分发流：1. 读取我提供的这段口语化草稿：“那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加”；2. 调用 summarize 提取时间地点人物和核心诉求；3. 调用 humanizer 去 AI 化重写并发送给邮箱。)";
	const auto selected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", wrappedChinesePrompt} }));
	REQUIRE(selected.has_value());
	REQUIRE(selected->find("下周三下午两点") != std::string::npos);
	REQUIRE(selected->find("二楼会议室") != std::string::npos);
	REQUIRE(selected->find("去 AI 化") == std::string::npos);
}

TEST_CASE("Summarize extract keeps core request aligned with quoted Chinese draft", "[tools][runtime][polish][extract]") {
	const std::string wrappedChinesePrompt = R"(请执行内容润色分发流：草稿是“那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加”；另外说明：去 AI 化。)";
	const auto selected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", wrappedChinesePrompt} }));
	REQUIRE(selected.has_value());

	const auto summarized = blazeclaw::core::tools::BuildSummarizeExtractOutput(*selected);
	REQUIRE(summarized.find("Core request: 那个新版本的 UI 需求改得差不多了") != std::string::npos);
	REQUIRE(summarized.find("去 AI 化") == std::string::npos);
}

TEST_CASE("Content polishing rejects control-only fragments as source text", "[tools][runtime][polish][extract]") {
	const auto selected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", "去 AI 化"} }));
	REQUIRE(!selected.has_value());
}

TEST_CASE("Summarize extract accepts body field and unquoted meeting-only cue text", "[tools][runtime][polish][extract]") {
	const std::string meetingOnly =
		"跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。";
	const auto fromBody = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"body", meetingOnly} }));
	REQUIRE(fromBody.has_value());
	REQUIRE(*fromBody == meetingOnly);

	const auto summarized = blazeclaw::core::tools::BuildSummarizeExtractOutput(*fromBody);
	REQUIRE(summarized.find("Time: 下周三下午两点") != std::string::npos);
	REQUIRE(summarized.find("Location: 二楼会议室") != std::string::npos);
}

TEST_CASE("Summarize extract unwraps nested arguments object from tool dispatch", "[tools][runtime][polish][extract]") {
	const std::string meetingOnly =
		"跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。";
	const auto nested = blazeclaw::core::tools::ExtractTextArgument(nlohmann::json::object({
		{"arguments", nlohmann::json::object({ {"text", meetingOnly} })},
		}));
	REQUIRE(nested.has_value());
	REQUIRE(*nested == meetingOnly);
}

TEST_CASE("Summarize extract coerces OpenAI-style content parts array", "[tools][runtime][polish][extract]") {
	const std::string meetingOnly =
		"跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。";
	const auto fromParts = blazeclaw::core::tools::ExtractTextArgument(nlohmann::json::object({
		{"content",
			nlohmann::json::array({
				nlohmann::json::object({ {"type", "text"}, {"text", meetingOnly} }),
			})},
		}));
	REQUIRE(fromParts.has_value());
	REQUIRE(*fromParts == meetingOnly);
}

TEST_CASE("Summarize extract reads user draft from messages array", "[tools][runtime][polish][extract]") {
	const std::string meetingOnly =
		"跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。";
	const auto fromMessages = blazeclaw::core::tools::ExtractTextArgument(nlohmann::json::object({
		{"messages",
			nlohmann::json::array({
				nlohmann::json::object(
					{ {"role", "user"}, {"content", meetingOnly} }),
			})},
		}));
	REQUIRE(fromMessages.has_value());
	REQUIRE(*fromMessages == meetingOnly);
}

TEST_CASE("Summarize extract unwraps stringified JSON in text field", "[tools][runtime][polish][extract]") {
	const std::string meetingOnly =
		"跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。";
	const std::string wrapped =
		std::string("{\"text\":") + nlohmann::json(meetingOnly).dump() + "}";
	const auto fromStringified = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", wrapped} }));
	REQUIRE(fromStringified.has_value());
	REQUIRE(*fromStringified == meetingOnly);
}

TEST_CASE("gateway.tools.call.execute params resolve args JSON sent as string", "[gateway][tools][args]") {
	const std::string argsObject = R"({"text":"下周三下午两点在二楼会议室"})";
	const std::string params =
		std::string("{\"tool\":\"summarize.extract\",\"args\":") +
		nlohmann::json(argsObject).dump() + "}";
	const auto resolved = blazeclaw::gateway::RequestParamsView(
		std::optional<std::string>(params)).GetToolExecuteArgsJson();
	REQUIRE(resolved.has_value());
	const auto parsed = nlohmann::json::parse(*resolved);
	REQUIRE(parsed["text"].get<std::string>() == "下周三下午两点在二楼会议室");
}

TEST_CASE("gateway.tools.call.execute resolves alias argument containers", "[gateway][tools][args]") {
	const std::string payload = R"({"text":"跟老板说一声，我们下周三下午两点在二楼会议室过一遍"})";
	const std::vector<std::string> aliases = {
		"arguments",
		"parameters",
		"tool_arguments",
		"toolArguments",
		"payload",
	};

	for (const auto& alias : aliases) {
		const std::string params =
			std::string("{\"tool\":\"summarize.extract\",\"") +
			alias +
			"\":" +
			nlohmann::json(payload).dump() +
			"}";
		const auto resolved = blazeclaw::gateway::RequestParamsView(
			std::optional<std::string>(params)).GetToolExecuteArgsJson();
		REQUIRE(resolved.has_value());
		const auto parsed = nlohmann::json::parse(*resolved);
		REQUIRE(parsed["text"].get<std::string>().find("下周三下午两点") != std::string::npos);
	}
}

TEST_CASE("gateway.tools.call.execute keeps args priority over alias containers", "[gateway][tools][args]") {
	const std::string params =
		R"({"tool":"summarize.extract","args":{"text":"args-primary"},"arguments":{"text":"alias-secondary"}})";
	const auto resolved = blazeclaw::gateway::RequestParamsView(
		std::optional<std::string>(params)).GetToolExecuteArgsJson();
	REQUIRE(resolved.has_value());
	const auto parsed = nlohmann::json::parse(*resolved);
	REQUIRE(parsed["text"].get<std::string>() == "args-primary");
}

TEST_CASE("Summarize extract after array-wrapped args matches ServiceManager coercion", "[tools][runtime][polish][extract]") {
	const std::string meetingOnly =
		"跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。";
	nlohmann::json params = nlohmann::json::array({
		nlohmann::json::object({ {"text", meetingOnly} }),
		});
	REQUIRE(params.is_array());
	nlohmann::json coerced = nlohmann::json::object();
	for (const auto& el : params) {
		if (el.is_object()) {
			coerced = el;
			break;
		}
	}
	params = std::move(coerced);
	const auto selected = blazeclaw::core::tools::ExtractTextArgument(params);
	REQUIRE(selected.has_value());
	REQUIRE(*selected == meetingOnly);
}

TEST_CASE("Humanizer accepts summary key and control text for downstream guardrails", "[tools][runtime][polish][humanizer]") {
	const auto fromSummary = blazeclaw::core::tools::ExtractHumanizerTextArgument(
		nlohmann::json::object({ {"summary", "Time: not found\nLocation: not found\nPeople: not found\nCore request: 去 AI 化"} }));
	REQUIRE(fromSummary.has_value());
	REQUIRE(fromSummary->find("Core request: 去 AI 化") != std::string::npos);

	const auto fromText = blazeclaw::core::tools::ExtractHumanizerTextArgument(
		nlohmann::json::object({ {"text", "去 AI 化"} }));
	REQUIRE(fromText.has_value());
	REQUIRE(*fromText == "去 AI 化");
}

TEST_CASE("Summarize extract captures structured fields from Chinese draft", "[tools][runtime][polish][extract]") {
	const std::string chineseDraft = "那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。";
	const auto summarized = blazeclaw::core::tools::BuildSummarizeExtractOutput(chineseDraft);
	REQUIRE(summarized.find("Time: 下周三下午两点") != std::string::npos);
	REQUIRE(summarized.find("Location: 二楼会议室") != std::string::npos);
	REQUIRE(summarized.find("People: boss, requester team") != std::string::npos);
}

TEST_CASE("Summarize extract supports mixed-language draft patterns", "[tools][runtime][polish][extract]") {
	const std::string mixedDraft = "UI requirements are almost done. 我们将在14:00在二楼会议室 review，并请老板参加。";
	const auto summarized = blazeclaw::core::tools::BuildSummarizeExtractOutput(mixedDraft);
	REQUIRE(summarized.find("Time: 14:00") != std::string::npos);
	REQUIRE(summarized.find("Location: 二楼会议室") != std::string::npos);
	REQUIRE(summarized.find("People: boss, requester team") != std::string::npos);
}

TEST_CASE("Summarize multilingual extractor can be disabled by feature flag", "[tools][runtime][polish][extract][feature-flag]") {
	ScopedEnvVar multilingualFlag("BLAZECLAW_SUMMARIZE_MULTILINGUAL_EXTRACTOR_ENABLED", "false");
	const std::string chineseDraft = "我们下周三下午两点在二楼会议室过一遍，让老板参加。";
	const auto summarized = blazeclaw::core::tools::BuildSummarizeExtractOutput(chineseDraft);
	REQUIRE(summarized.find("Time: 下周三下午两点") != std::string::npos);
	REQUIRE(summarized.find("Location: 二楼会议室") != std::string::npos);
	REQUIRE(summarized.find("People: boss, requester team") != std::string::npos);
}

TEST_CASE("Humanizer strips instruction-artifact purpose and returns confirmation scaffold on low confidence", "[tools][runtime][polish][humanizer]") {
	const std::string lowConfidenceSummary =
		"Time: not found\n"
		"Location: not found\n"
		"People: not found\n"
		"Core request: 去 AI 化";
	const auto rewritten = blazeclaw::core::tools::BuildHumanizerRewriteOutput(lowConfidenceSummary);
	REQUIRE(rewritten.find("incomplete and need confirmation before sending") != std::string::npos);
	REQUIRE(rewritten.find("Draft intent: 去 AI 化") == std::string::npos);
}

TEST_CASE("Humanizer recovers fields from core request when summary is low confidence", "[tools][runtime][polish][humanizer]") {
	const std::string lowConfidenceSummary =
		"Time: not found\n"
		"Location: not found\n"
		"People: not found\n"
		"Core request: 那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。";
	const auto rewritten = blazeclaw::core::tools::BuildHumanizerRewriteOutput(lowConfidenceSummary);
	REQUIRE(rewritten.find("incomplete and need confirmation before sending") == std::string::npos);
	REQUIRE(rewritten.find("Time: 下周三下午两点") != std::string::npos);
	REQUIRE(rewritten.find("Location: 二楼会议室") != std::string::npos);
	REQUIRE(rewritten.find("Participants: boss, requester team") != std::string::npos);
}

TEST_CASE("Humanizer recovers directly from raw Chinese orchestrated prompt", "[tools][runtime][polish][humanizer]") {
	const std::string prompt =
		R"(请执行内容润色分发流：1. 读取我提供的这段口语化草稿：“那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加”；2. 调用 `summarize` 提取其中的时间、地点、人物和核心诉求；3. 调用 `humanizer` 对提取的内容进行“去 AI 化”重写；4. 发送预览。)";
	const auto rewritten = blazeclaw::core::tools::BuildHumanizerRewriteOutput(prompt);
	REQUIRE(rewritten.find("incomplete and need confirmation before sending") == std::string::npos);
	REQUIRE(rewritten.find("Time: 下周三下午两点") != std::string::npos);
	REQUIRE(rewritten.find("Location: 二楼会议室") != std::string::npos);
	REQUIRE(rewritten.find("Participants: boss, requester team") != std::string::npos);
	REQUIRE(rewritten.find("Purpose: 去 AI 化") == std::string::npos);
}

TEST_CASE("Humanizer recovers from structured summary with not found plus orchestration core request", "[tools][runtime][polish][humanizer]") {
	const std::string summary =
		"Time: not found\n"
		"Location: not found\n"
		"People: not found\n"
		"Core request: 请执行内容润色分发流：1. 读取口语化草稿：“那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加”；2. 调用 summarize 提取；3. 调用 humanizer 去 AI 化。";
	const auto rewritten = blazeclaw::core::tools::BuildHumanizerRewriteOutput(summary);
	REQUIRE(rewritten.find("incomplete and need confirmation before sending") == std::string::npos);
	REQUIRE(rewritten.find("Time: 下周三下午两点") != std::string::npos);
	REQUIRE(rewritten.find("Location: 二楼会议室") != std::string::npos);
}

TEST_CASE("Draft selector workflow-marker fallback handles wrapped Chinese flow text", "[tools][runtime][polish][extract]") {
	const std::string wrappedPrompt =
		R"(请执行内容润色分发流：1. 读取我提供的这段口语化草稿：那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加；2. 调用 summarize 提取其中的时间、地点、人物和核心诉求。)";
	const auto selected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", wrappedPrompt} }));
	REQUIRE(selected.has_value());
	REQUIRE(selected->find("下周三下午两点") != std::string::npos);
	REQUIRE(selected->find("二楼会议室") != std::string::npos);
}

TEST_CASE("Draft selector supports Chinese full-width punctuation and quote variants", "[tools][runtime][polish][extract]") {
	const std::string wrappedPrompt =
		R"(请执行内容润色分发流：步骤一、读取我提供的口语化草稿：「那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加。」。步骤二、调用 summarize 提取其中的时间、地点、人物和核心诉求。)";
	const auto selected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", wrappedPrompt} }));
	REQUIRE(selected.has_value());
	REQUIRE(selected->find("下周三下午两点") != std::string::npos);
	REQUIRE(selected->find("二楼会议室") != std::string::npos);
	REQUIRE(selected->find("调用 summarize") == std::string::npos);
}

TEST_CASE("Bilingual parity: English and Chinese workflow prompts produce meeting fields", "[tools][runtime][polish][extract][parity]") {
	const std::string chinesePrompt =
		R"(请执行内容润色分发流：1. 读取我提供的这段口语化草稿：“那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加”；2. 调用 summarize 提取其中的时间、地点、人物和核心诉求。)";
	const std::string englishPrompt =
		R"(Please run the content polishing distribution flow: 1) read this draft: "The new UI requirements are almost finalized. Please tell the boss that we will review them next Wednesday at 2:00 PM in the second-floor meeting room, and he must attend." 2) call summarize to extract time, location, people, and core request.)";

	const auto chineseSelected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", chinesePrompt} }));
	const auto englishSelected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", englishPrompt} }));
	REQUIRE(chineseSelected.has_value());
	REQUIRE(englishSelected.has_value());

	const auto chineseSummary = blazeclaw::core::tools::BuildSummarizeExtractOutput(*chineseSelected);
	const auto englishSummary = blazeclaw::core::tools::BuildSummarizeExtractOutput(*englishSelected);
	REQUIRE(chineseSummary.find("Time: 下周三下午两点") != std::string::npos);
	REQUIRE(chineseSummary.find("Location: 二楼会议室") != std::string::npos);
	REQUIRE(chineseSummary.find("People: boss, requester team") != std::string::npos);
	REQUIRE(englishSummary.find("Time:") != std::string::npos);
	REQUIRE(englishSummary.find("Location:") != std::string::npos);
	REQUIRE(englishSummary.find("People: boss, requester team") != std::string::npos);
}

TEST_CASE("Draft selector handles Chinese prompt with backtick tool names", "[tools][runtime][polish][extract]") {
	const std::string wrappedPrompt =
		R"(请执行内容润色分发流：1. 读取我提供的这段口语化草稿：“那个新版本的 UI 需求改得差不多了，你跟老板说一声，我们下周三下午两点在二楼会议室过一遍，让他务必参加”；2. 调用 `summarize` 提取其中的时间、地点、人物和核心诉求；3. 调用 `humanizer` 去 AI 化重写。)";
	const auto selected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", wrappedPrompt} }));
	REQUIRE(selected.has_value());
	REQUIRE(selected->find("下周三下午两点") != std::string::npos);
	REQUIRE(selected->find("二楼会议室") != std::string::npos);
	REQUIRE(selected->find("`summarize`") == std::string::npos);
}

TEST_CASE("Chinese control-only fragments remain rejected", "[tools][runtime][polish][extract]") {
	const auto selected = blazeclaw::core::tools::ExtractTextArgument(
		nlohmann::json::object({ {"text", "请调用 `summarize` 提取时间地点人物并发送给邮箱"} }));
	REQUIRE(!selected.has_value());
}

TEST_CASE("gateway.tools.call.execute missing args stays non-blocking with diagnostics", "[gateway][tools][args][hardening]") {
	blazeclaw::gateway::GatewayHost host;
	REQUIRE(host.StartLocalRuntimeDispatchOnly());
	const blazeclaw::gateway::protocol::RequestFrame request{
		.id = "hardening-1",
		.method = "gateway.tools.call.execute",
		.paramsJson = std::string(R"({"tool":"summarize.extract"})"),
	};
	const auto response = host.RouteRequest(request);
	REQUIRE(response.ok);
	REQUIRE(response.payloadJson.has_value());
	const auto payload = nlohmann::json::parse(response.payloadJson.value());
	REQUIRE(payload["executed"].get<bool>() == false);
	REQUIRE(payload["argsProvided"].get<bool>() == false);
	REQUIRE(payload["argsKey"].get<std::string>() == "none");
	REQUIRE(payload["argsParseMode"].get<std::string>() == "unrecognized_or_missing");
	const std::string errorCode = payload["errorCode"].get<std::string>();
	const bool acceptedErrorCode =
		errorCode == "invalid_arguments" ||
		errorCode == "legacy_execution_failed";
	REQUIRE(acceptedErrorCode);
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

TEST_CASE("Skill tool registry loads nano-pdf manifest as enabled runtime entry", "[tools][runtime][manifest][nano-pdf]") {
	const auto root = std::filesystem::temp_directory_path() /
		("blazeclaw_nano_pdf_manifest_" + std::to_string(std::rand()));
	const auto skillDir = root / "nano-pdf";
	std::filesystem::create_directories(skillDir);

	const auto manifestPath = skillDir / "tool-manifest.json";
	{
		std::ofstream out(manifestPath);
		REQUIRE(out.is_open());
		out
			<< "{\n"
			<< "  \"schemaVersion\": 1,\n"
			<< "  \"skill\": \"nano-pdf\",\n"
			<< "  \"namespace\": \"nano_pdf\",\n"
			<< "  \"tools\": [\n"
			<< "    {\n"
			<< "      \"id\": \"nano_pdf.edit\",\n"
			<< "      \"label\": \"Nano PDF Edit\",\n"
			<< "      \"category\": \"document\",\n"
			<< "      \"enabled\": true\n"
			<< "    }\n"
			<< "  ]\n"
			<< "}\n";
	}

	blazeclaw::gateway::GatewayToolRegistry registry;
	const auto loaded = registry.LoadSkillToolsFromDirectory(root.string());
	REQUIRE(loaded == 1);

	const auto tools = registry.List();
	const auto it = std::find_if(
		tools.begin(),
		tools.end(),
		[](const blazeclaw::gateway::ToolCatalogEntry& tool) {
			return tool.id == "nano_pdf.edit";
		});
	REQUIRE(it != tools.end());
	REQUIRE(it->enabled);
	REQUIRE(it->source == "skills.tool-manifest");

	std::filesystem::remove_all(root);
}

TEST_CASE("Web browsing Option B fallback continuity contract remains wired", "[tools][runtime][fallback][contract]") {
	const auto serviceManagerPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"ServiceManager.cpp";
	const auto serviceManagerPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"core" /
		"ServiceManager.cpp";
	std::ifstream in(serviceManagerPathPrimary.string());
	if (!in.is_open()) {
		in.open(serviceManagerPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(source.find("spec.id == \"web_browsing.search.web\"") != std::string::npos);
	REQUIRE(source.find("[fallback=web_browsing_python_primary]") != std::string::npos);
	REQUIRE(source.find("[fallback=baidu_search_python]") != std::string::npos);
	REQUIRE(source.find("process.errorCode.empty()") != std::string::npos);
	REQUIRE(source.find("? \"process_start_failed\"") != std::string::npos);
}

TEST_CASE("Inbox inline invocation maps natural-language to JSON args", "[tools][runtime][imap][contract]") {
	const auto serviceManagerPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"ServiceManager.cpp";
	const auto serviceManagerPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"core" /
		"ServiceManager.cpp";
	std::ifstream in(serviceManagerPathPrimary.string());
	if (!in.is_open()) {
		in.open(serviceManagerPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(source.find("BuildInlineArgsForResolvedTool") != std::string::npos);
	REQUIRE(source.find("imap_smtp_email.imap.search") != std::string::npos);
	REQUIRE(source.find("params[\"recent\"] = LooksLikeTwoHourUrgencyAnyLanguage") != std::string::npos);
	REQUIRE(source.find("? \"2h\"") != std::string::npos);
	REQUIRE(source.find("params[\"unseen\"] = true") != std::string::npos);
}

TEST_CASE("Inbox inline invocation has friendly empty-array response", "[tools][runtime][imap][contract]") {
	const auto serviceManagerPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"ServiceManager.cpp";
	const auto serviceManagerPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"core" /
		"ServiceManager.cpp";
	std::ifstream in(serviceManagerPathPrimary.string());
	if (!in.is_open()) {
		in.open(serviceManagerPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(source.find("BuildInlineFriendlyTextForResolvedTool") != std::string::npos);
	REQUIRE(source.find("found no messages ") != std::string::npos);
	REQUIRE(source.find("that need a reply") != std::string::npos);
}

TEST_CASE("Runtime health dependencies include python and web-browsing probes", "[tools][runtime][health][contract]") {
	const auto executorPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"executors" /
		"EmailScheduleExecutor.cpp";
	const auto executorPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"gateway" /
		"executors" /
		"EmailScheduleExecutor.cpp";
	std::ifstream in(executorPathPrimary.string());
	if (!in.is_open()) {
		in.open(executorPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(source.find("BLAZECLAW_EMAIL_PROBE_PYTHON") != std::string::npos);
	REQUIRE(source.find("BLAZECLAW_EMAIL_PROBE_WEB_BROWSING_PYTHON_SKILL") != std::string::npos);
	REQUIRE(source.find("\"runtime:python\"") != std::string::npos);
	REQUIRE(source.find("\"skill:web_browsing_python\"") != std::string::npos);
}

TEST_CASE("CToolRuntimeRegistry invokes dependency registrations", "[tools][runtime][registry]") {
	blazeclaw::core::CToolRuntimeRegistry registry;
	blazeclaw::gateway::GatewayHost host;
	const blazeclaw::core::CToolRuntimeRegistry::ToolRuntimePolicySettings policy{};

	int callCount = 0;
	blazeclaw::core::CToolRuntimeRegistry::Dependencies deps;
	deps.registerImapSmtp = [&](blazeclaw::gateway::GatewayHost&, const auto&) {
		++callCount;
		};
	deps.registerContentPolishing = [&](blazeclaw::gateway::GatewayHost&) { ++callCount; };
	deps.registerBraveSearch = [&](blazeclaw::gateway::GatewayHost&, const auto&) {
		++callCount;
		};
	deps.registerBaiduSearch = [&](blazeclaw::gateway::GatewayHost&, const auto&) {
		++callCount;
		};
	deps.registerNanoPdf = [&](blazeclaw::gateway::GatewayHost&, const auto&) {
		++callCount;
		};

	registry.RegisterAll(host, policy, deps);
	REQUIRE(callCount == 5);
}

TEST_CASE("Skill invocation includes inbox intent alias contract", "[skills][dispatch][contract]") {
	const auto serviceManagerPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"ServiceManager.cpp";
	const auto serviceManagerPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"core" /
		"ServiceManager.cpp";
	std::ifstream in(serviceManagerPathPrimary.string());
	if (!in.is_open()) {
		in.open(serviceManagerPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	REQUIRE(source.find("LooksLikeInboxReplyUrgencyIntent") != std::string::npos);
	REQUIRE(source.find("ContainsAnyWideFragment") != std::string::npos);
	REQUIRE(source.find("CanonicalizeForRouting") != std::string::npos);
	REQUIRE(source.find("imap_smtp_email.imap.search") != std::string::npos);
}

TEST_CASE("Runtime recovery enforces email-intent cross-skill guard", "[tools][runtime][fallback][contract]") {
	const auto normalizerPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"RuntimeToolCallNormalizer.cpp";
	const auto normalizerPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"gateway" /
		"RuntimeToolCallNormalizer.cpp";
	std::ifstream in(normalizerPathPrimary.string());
	if (!in.is_open()) {
		in.open(normalizerPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	REQUIRE(source.find("cross_skill_retry_blocked_email_intent") != std::string::npos);
	REQUIRE(source.find("intent_not_matched") != std::string::npos);
	REQUIRE(source.find("ContainsAnyWideFragment") != std::string::npos);
	REQUIRE(source.find("IsEmailIntentMessage") != std::string::npos);
}

TEST_CASE("Routing telemetry emits language and intent metadata", "[skills][gateway][routing][contract]") {
	const auto coordinatorPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"GatewayHostBindingCoordinator.cpp";
	const auto coordinatorPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"core" /
		"GatewayHostBindingCoordinator.cpp";
	std::ifstream in(coordinatorPathPrimary.string());
	if (!in.is_open()) {
		in.open(coordinatorPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	REQUIRE(source.find("gateway.chat.routing.decision") != std::string::npos);
	REQUIRE(source.find("detectedLanguage") != std::string::npos);
	REQUIRE(source.find("normalizedIntent") != std::string::npos);
	REQUIRE(source.find("fallbackReason") != std::string::npos);
	REQUIRE(source.find("intent_not_matched") != std::string::npos);
}

TEST_CASE("Bridge hybrid push phases are wired", "[bridge][push][contract]") {
	const auto bridgePathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"app" /
		"CBridge.cpp";
	const auto bridgePathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"app" /
		"CBridge.cpp";
	std::ifstream in(bridgePathPrimary.string());
	if (!in.is_open()) {
		in.open(bridgePathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	REQUIRE(source.find("HandlePushConnected") != std::string::npos);
	REQUIRE(source.find("HandlePushDisconnected") != std::string::npos);
	REQUIRE(source.find("HandlePushChatEventFrame") != std::string::npos);
	REQUIRE(source.find("pushRecoveryPollPending") != std::string::npos);
	REQUIRE(source.find("push-ui-throttled") != std::string::npos);
	REQUIRE(source.find("duplicate-event") != std::string::npos);
}

TEST_CASE("Bridge view exposes push feature flags and channels", "[bridge][push][view][contract]") {
	const auto viewPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"app" /
		"BlazeClawMFCView.cpp";
	const auto viewPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"app" /
		"BlazeClawMFCView.cpp";
	std::ifstream in(viewPathPrimary.string());
	if (!in.is_open()) {
		in.open(viewPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	REQUIRE(source.find("BLAZECLAW_BRIDGE_PUSH_ENABLED") != std::string::npos);
	REQUIRE(source.find("BLAZECLAW_BRIDGE_PUSH_FALLBACK_POLL_ENABLED") != std::string::npos);
	REQUIRE(source.find("BLAZECLAW_BRIDGE_PUSH_RECOVERY_POLL_ENABLED") != std::string::npos);
	REQUIRE(source.find("blazeclaw.gateway.chat.push.state") != std::string::npos);
	REQUIRE(source.find("blazeclaw.gateway.chat.push.event") != std::string::npos);
}

TEST_CASE("Chat poll fast reveal and bounded streaming are wired", "[chat][poll][reveal][contract]") {
	const auto pipelinePathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.ChatPipeline.cpp";
	const auto pipelinePathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.ChatPipeline.cpp";
	std::ifstream in(pipelinePathPrimary.string());
	if (!in.is_open()) {
		in.open(pipelinePathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	REQUIRE(source.find("BLAZECLAW_CHAT_POLL_SYNTHETIC_REVEAL_FAST_MODE") != std::string::npos);
	REQUIRE(source.find("syntheticRevealMaxDurationMs") != std::string::npos);
	REQUIRE(source.find("gateway.chat.poll.reveal.mode") != std::string::npos);
	REQUIRE(source.find("revealMode") != std::string::npos);
	REQUIRE(source.find("pollRevealChunkSize") != std::string::npos);
	REQUIRE(source.find("synthetic_incremental_diff") != std::string::npos);
	REQUIRE(source.find("assistant_text_fully_available") != std::string::npos);
	REQUIRE(source.find("gateway.chat.send.tool_heavy_direct_emit") != std::string::npos);
}

TEST_CASE("Web chat queue guardrails are wired", "[chat][frontend][queue][contract]") {
	const auto controllerPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-controller.js";
	const auto controllerPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"web" /
		"chat" /
		"chat-controller.js";
	std::ifstream controllerIn(controllerPathPrimary.string());
	if (!controllerIn.is_open()) {
		controllerIn.open(controllerPathFallback.string());
	}
	REQUIRE(controllerIn.is_open());
	const std::string controllerSource(
		(std::istreambuf_iterator<char>(controllerIn)),
		std::istreambuf_iterator<char>());

	const auto eventsPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-events.js";
	const auto eventsPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"web" /
		"chat" /
		"chat-events.js";
	std::ifstream eventsIn(eventsPathPrimary.string());
	if (!eventsIn.is_open()) {
		eventsIn.open(eventsPathFallback.string());
	}
	REQUIRE(eventsIn.is_open());
	const std::string eventsSource(
		(std::istreambuf_iterator<char>(eventsIn)),
		std::istreambuf_iterator<char>());

	REQUIRE(controllerSource.find("waiting for terminal event; queued message") != std::string::npos);
	REQUIRE(controllerSource.find("waiting for terminal event; reconciling stalled run") != std::string::npos);
	REQUIRE(controllerSource.find("request(\"chat.events.poll\"") != std::string::npos);
	REQUIRE(controllerSource.find("function noteInboundChatEvent") != std::string::npos);
	REQUIRE(controllerSource.find("chat.queue.stale_run_detected") != std::string::npos);
	REQUIRE(controllerSource.find("chat.queue.run_id_remapped") != std::string::npos);
	REQUIRE(controllerSource.find("chat.abort.stale_run_reconcile") != std::string::npos);
	REQUIRE(eventsSource.find("controller.noteInboundChatEvent(event.state)") != std::string::npos);
}

TEST_CASE("Web chat incident mismatch-recovery wiring is present", "[chat][frontend][incident][contract]") {
	const auto eventsPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-events.js";
	const auto eventsPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"web" /
		"chat" /
		"chat-events.js";
	std::ifstream eventsIn(eventsPathPrimary.string());
	if (!eventsIn.is_open()) {
		eventsIn.open(eventsPathFallback.string());
	}
	REQUIRE(eventsIn.is_open());
	const std::string eventsSource(
		(std::istreambuf_iterator<char>(eventsIn)),
		std::istreambuf_iterator<char>());

	const auto controllerPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-controller.js";
	const auto controllerPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"web" /
		"chat" /
		"chat-controller.js";
	std::ifstream controllerIn(controllerPathPrimary.string());
	if (!controllerIn.is_open()) {
		controllerIn.open(controllerPathFallback.string());
	}
	REQUIRE(controllerIn.is_open());
	const std::string controllerSource(
		(std::istreambuf_iterator<char>(controllerIn)),
		std::istreambuf_iterator<char>());

	REQUIRE(eventsSource.find("const isTerminalMismatch") != std::string::npos);
	REQUIRE(eventsSource.find("controller.clearRunState()") != std::string::npos);
	REQUIRE(eventsSource.find("controller.scheduleHistoryReconcile()") != std::string::npos);
	REQUIRE(controllerSource.find("function extractAbortOutcome") != std::string::npos);
	REQUIRE(controllerSource.find("function extractChatEventsFromPollResponse") != std::string::npos);
	REQUIRE(controllerSource.find("abort fallback reconciled stale run state") != std::string::npos);
	REQUIRE(controllerSource.find("abort diagnostic: target=") != std::string::npos);
}

TEST_CASE("Nano PDF runtime contract wiring is present", "[tools][runtime][nano-pdf][contract]") {
	const auto serviceManagerPathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"core" /
		"ServiceManager.cpp";
	const auto serviceManagerPathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"core" /
		"ServiceManager.cpp";
	std::ifstream in(serviceManagerPathPrimary.string());
	if (!in.is_open()) {
		in.open(serviceManagerPathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());

	REQUIRE(source.find("RegisterNanoPdfRuntimeTools") != std::string::npos);
	REQUIRE(source.find("BuildNanoPdfToolRuntimeSpecs") != std::string::npos);
	REQUIRE(source.find("BuildNanoPdfCliArgs") != std::string::npos);
	REQUIRE(source.find("missing_dependency") != std::string::npos);
	REQUIRE(source.find("invalid_args") != std::string::npos);
	REQUIRE(source.find("execution_failed") != std::string::npos);
}

TEST_CASE("Gateway skills check exposes dispatch-required counters", "[skills][gateway][contract]") {
	const auto pipelinePathPrimary =
		std::filesystem::path("BlazeClawMfc") /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.ChatPipeline.cpp";
	const auto pipelinePathFallback =
		std::filesystem::path("blazeclaw") /
		"BlazeClawMfc" /
		"src" /
		"gateway" /
		"GatewayHost.Handlers.Runtime.ChatPipeline.cpp";
	std::ifstream in(pipelinePathPrimary.string());
	if (!in.is_open()) {
		in.open(pipelinePathFallback.string());
	}
	REQUIRE(in.is_open());

	const std::string source(
		(std::istreambuf_iterator<char>(in)),
		std::istreambuf_iterator<char>());
	REQUIRE(source.find("dispatchRequiredSkills") != std::string::npos);
	REQUIRE(source.find("dispatchRequiredMissing") != std::string::npos);
}
