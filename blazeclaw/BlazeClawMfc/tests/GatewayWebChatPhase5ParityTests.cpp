#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

	std::filesystem::path ResolveRepoRoot()
	{
		std::filesystem::path cursor = std::filesystem::current_path();
		for (int depth = 0; depth < 8; ++depth) {
			const auto directCandidate =
				cursor / "BlazeClawMfc" / "web" / "chat" / "index.html";
			const auto nestedCandidate =
				cursor / "blazeclaw" / "BlazeClawMfc" / "web" / "chat" / "index.html";
			if (std::filesystem::exists(directCandidate) ||
				std::filesystem::exists(nestedCandidate)) {
				return cursor;
			}

			if (!cursor.has_parent_path()) {
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor) {
				break;
			}
			cursor = parent;
		}

		return std::filesystem::current_path();
	}

	std::filesystem::path ResolveProjectPath(const std::filesystem::path& relative)
	{
		const auto root = ResolveRepoRoot();
		const auto direct = root / relative;
		if (std::filesystem::exists(direct)) {
			return direct;
		}

		return root / "blazeclaw" / relative;
	}

	std::string ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream in(path.string());
		REQUIRE(in.is_open());
		return std::string(
			(std::istreambuf_iterator<char>(in)),
			std::istreambuf_iterator<char>());
	}

} // namespace

TEST_CASE(
	"Phase 5 web parity: composer surface covers input send abort and selectors",
	"[parity][phase5][web][composer]")
{
	const auto indexPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"index.html";
	const std::string indexHtml = ReadTextFile(ResolveProjectPath(indexPath));

	REQUIRE(indexHtml.find("id=\"input\"") != std::string::npos);
	REQUIRE(indexHtml.find("id=\"sendBtn\"") != std::string::npos);
	REQUIRE(indexHtml.find("id=\"abortBtn\"") != std::string::npos);
	REQUIRE(indexHtml.find("id=\"sessionSelect\"") != std::string::npos);
	REQUIRE(indexHtml.find("id=\"modelSelect\"") != std::string::npos);
	REQUIRE(indexHtml.find("id=\"thinkingSelect\"") != std::string::npos);
	REQUIRE(indexHtml.find("id=\"slashMenu\"") != std::string::npos);
}

TEST_CASE(
	"Phase 5 web parity: controller includes queue slash drafts and history semantics",
	"[parity][phase5][web][controller]")
{
	const auto controllerPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-controller.js";
	const std::string controllerJs = ReadTextFile(ResolveProjectPath(controllerPath));

	REQUIRE(controllerJs.find("state.sendQueue") != std::string::npos);
	REQUIRE(controllerJs.find("queuePendingSend") != std::string::npos);
	REQUIRE(controllerJs.find("processSendQueue") != std::string::npos);
	REQUIRE(controllerJs.find("handleLocalSlashCommand") != std::string::npos);
	REQUIRE(controllerJs.find("gateway.skills.commands") != std::string::npos);
	REQUIRE(controllerJs.find("persistDraftForSession") != std::string::npos);
	REQUIRE(controllerJs.find("restoreDraftForSession") != std::string::npos);
	REQUIRE(controllerJs.find("recallInputHistory") != std::string::npos);
}

TEST_CASE(
	"Phase 5 web parity: composer module includes slash keyboard navigation",
	"[parity][phase5][web][slash]")
{
	const auto composerPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-composer.js";
	const std::string composerJs = ReadTextFile(ResolveProjectPath(composerPath));

	REQUIRE(composerJs.find("updateSlashMenuFromInput") != std::string::npos);
	REQUIRE(composerJs.find("applySlashHint") != std::string::npos);
	REQUIRE(composerJs.find("event.key === \"Tab\"") != std::string::npos);
	REQUIRE(composerJs.find("event.key === \"ArrowDown\"") != std::string::npos);
	REQUIRE(composerJs.find("event.key === \"ArrowUp\"") != std::string::npos);
	REQUIRE(composerJs.find("event.key === \"Escape\"") != std::string::npos);
}

TEST_CASE(
	"Phase 5 web parity: response reconcile enforces terminal dedupe and stale-run guards",
	"[parity][phase5][web][response]")
{
	const auto eventsPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-events.js";
	const std::string eventsJs = ReadTextFile(ResolveProjectPath(eventsPath));

	REQUIRE(eventsJs.find("seenChatTerminalRuns") != std::string::npos);
	REQUIRE(eventsJs.find("controller.hasTerminalRun(runId)") != std::string::npos);
	REQUIRE(eventsJs.find("controller.markTerminalRun") != std::string::npos);
	REQUIRE(eventsJs.find("controller.scheduleHistoryReconcile") != std::string::npos);
	REQUIRE(eventsJs.find("seenToolLifecycleKeys") != std::string::npos);
}

TEST_CASE(
	"Phase 8 compatibility parity: controller preserves legacy export via delegation facade",
	"[parity][phase8][web][compat]")
{
	const auto controllerPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-controller.js";
	const std::string controllerJs = ReadTextFile(ResolveProjectPath(controllerPath));

	REQUIRE(controllerJs.find("createControllerLegacyImplementation") != std::string::npos);
	REQUIRE(controllerJs.find("createControllerCompatibilityFacade") != std::string::npos);
	REQUIRE(controllerJs.find("__compatFacadeVersion") != std::string::npos);
	REQUIRE(controllerJs.find("window.BlazeClawChatController") != std::string::npos);
}

TEST_CASE(
	"Phase 8 compatibility parity: index integration uses controller API adapter",
	"[parity][phase8][web][integration]")
{
	const auto indexJsPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"index.js";
	const std::string indexJs = ReadTextFile(ResolveProjectPath(indexJsPath));

	REQUIRE(indexJs.find("const chatControllerApi = window.BlazeClawChatController || {}") != std::string::npos);
	REQUIRE(indexJs.find("function createChatController(options)") != std::string::npos);
	REQUIRE(indexJs.find("const controller = createChatController({") != std::string::npos);
	REQUIRE(indexJs.find("chatControllerApi.runRegressionChecks()") != std::string::npos);
}

TEST_CASE(
	"Phase 8 compatibility parity: script-order verification is checked before index boot",
	"[parity][phase8][web][script-order]")
{
	const auto indexHtmlPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"index.html";
	const auto indexJsPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"index.js";

	const std::string indexHtml = ReadTextFile(ResolveProjectPath(indexHtmlPath));
	const std::string indexJs = ReadTextFile(ResolveProjectPath(indexJsPath));

	REQUIRE(indexHtml.find("__BLAZECLAW_CHAT_SCRIPT_ORDER_COMPAT__") != std::string::npos);
	REQUIRE(indexHtml.find("BlazeClawChatControllerGui") != std::string::npos);
	REQUIRE(indexHtml.find("BlazeClawChatEvents") != std::string::npos);
	REQUIRE(indexHtml.find("BlazeClawChatComposer") != std::string::npos);
	REQUIRE(indexJs.find("scriptOrderCompat") != std::string::npos);
}

TEST_CASE(
	"Phase 9 parity: multi-active responder labeling keeps cached run labels and event order",
	"[parity][phase9][web][multi-active]")
{
	const auto eventsPath = std::filesystem::path("BlazeClawMfc") /
		"web" /
		"chat" /
		"chat-events.js";
	const std::string eventsJs = ReadTextFile(ResolveProjectPath(eventsPath));

	REQUIRE(eventsJs.find("resolveResponderLabel") != std::string::npos);
	REQUIRE(eventsJs.find("state.runResponderLabels") != std::string::npos);
	REQUIRE(eventsJs.find("runLabels instanceof Map") != std::string::npos);
	REQUIRE(eventsJs.find("for (const event of events)") != std::string::npos);
}
