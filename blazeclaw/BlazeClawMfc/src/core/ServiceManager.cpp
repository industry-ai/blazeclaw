#include "pch.h"
#include "ServiceManager.h"
#include "GatewayHostBindingCoordinator.h"
#include "ServiceLifecycleStartupCoordinator.h"
#include "SkillsAgentCommandDescriptorPolicy.h"
#include "SkillsGatewayPublicationCoordinator.h"
#include "../app/CredentialStore.h"
#include "../app/BlazeClawMFCDoc.h"
#include "../app/BlazeClawMFCView.h"
#include "../app/MainFrame.h"

#include "../config/ConfigLoader.h"
#include "../gateway/GatewayProtocolModels.h"
#include "../gateway/GatewayJsonUtils.h"
#include "../gateway/Telemetry.h"
#include "../gateway/executors/EmailScheduleExecutor.h"
#include "diagnostics/DiagnosticsSnapshot.h"
#include "diagnostics/DiagnosticsRegressionComparator.h"
#include "bootstrap/StartupFixtureValidator.h"
#include "filesystem/SafeOpenSync.h"
#include "SkillsFrontmatterCompat.h"
#include "tools/ToolArgumentValidators.h"
#include "tools/ToolProcessRunner.h"
#include "runtime/LocalModel/LlamaTextGenerationRuntime.h"
#include "runtime/SpeechRecognition/SpeechRecognitionRuntime.h"
#include "ServiceManagerTextHelpers.h"
#include "ServiceManagerSkillRootsHelpers.h"
#include "ServiceManagerBaiduEnvHelpers.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <Windows.h>
#include <nlohmann/json.hpp>

namespace blazeclaw::core {

	namespace {

		std::wstring Trim(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				}).base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::wstring ToLower(const std::wstring& value) {
			std::wstring lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return lowered;
		}

		bool SuppressStartupMigrationsFromEnv() {
			return servicemanager_text::SuppressStartupMigrationsFromEnv();
		}

		std::wstring Utf8ToWideLocal(const std::string& value) {
			return servicemanager_text::Utf8ToWideLocal(value);
		}

		std::string WideToUtf8Local(const std::wstring& value) {
			if (value.empty()) {
				return {};
			}

			const int required = WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (required <= 0) {
				return {};
			}

			std::string output(static_cast<std::size_t>(required), '\0');
			WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				required,
				nullptr,
				nullptr);
			return output;
		}

		std::uint64_t CurrentEpochMs();

		bool IsLlamaLocalModelId(const std::string& modelId) {
			return servicemanager_text::IsLlamaLocalModelId(modelId);
		}

		std::string ToNarrow(const std::wstring& value) {
			return servicemanager_text::ToNarrowAscii(value);
		}


		std::wstring TrimWideLocal(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				})
				.base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::string NarrowTrimmedOrEmpty(const std::wstring& value) {
			const std::wstring trimmed = TrimWideLocal(value);
			if (trimmed.empty()) {
				return {};
			}

			return ToNarrow(trimmed);
		}

		std::string NormalizeFlatJsonMapToObject(const std::wstring& rawValue) {
			const std::wstring trimmedWide = TrimWideLocal(rawValue);
			if (trimmedWide.empty()) {
				return {};
			}

			const std::string narrow = ToNarrow(trimmedWide);
			if (blazeclaw::gateway::json::Trim(narrow).empty()) {
				return {};
			}

			nlohmann::json parsed =
				nlohmann::json::parse(narrow, nullptr, false);
			if (parsed.is_discarded()) {
				return {};
			}

			if (parsed.is_object()) {
				return parsed.dump();
			}

			if (!parsed.is_array()) {
				return {};
			}

			nlohmann::json normalized = nlohmann::json::object();
			for (const auto& entry : parsed) {
				if (!entry.is_object()) {
					continue;
				}

				const auto idIt = entry.find("id");
				if (idIt == entry.end() || !idIt->is_string()) {
					continue;
				}

				const std::string id =
					blazeclaw::gateway::json::Trim(idIt->get<std::string>());
				if (id.empty()) {
					continue;
				}

				normalized[id] = entry;
			}

			return normalized.dump();
		}

		void DrainPipeAvailable(HANDLE readPipe, std::string& output) {
			if (readPipe == nullptr || readPipe == INVALID_HANDLE_VALUE) {
				return;
			}

			for (;;) {
				DWORD available = 0;
				if (!PeekNamedPipe(
					readPipe,
					nullptr,
					0,
					nullptr,
					&available,
					nullptr) || available == 0) {
					break;
				}

				char buffer[4096]{};
				const DWORD toRead =
					available > sizeof(buffer)
					? static_cast<DWORD>(sizeof(buffer))
					: available;
				DWORD bytesRead = 0;
				if (!ReadFile(readPipe, buffer, toRead, &bytesRead, nullptr) ||
					bytesRead == 0) {
					break;
				}

				output.append(buffer, buffer + bytesRead);
			}
		}

		std::uint64_t Fnv1a64(const std::string& value) {
			constexpr std::uint64_t kOffset = 14695981039346656037ULL;
			constexpr std::uint64_t kPrime = 1099511628211ULL;
			std::uint64_t hash = kOffset;
			for (const unsigned char ch : value) {
				hash ^= static_cast<std::uint64_t>(ch);
				hash *= kPrime;
			}
			return hash;
		}

		std::string BuildHexLower(std::uint64_t value) {
			std::ostringstream out;
			out << std::hex << std::nouppercase << value;
			return out.str();
		}

		std::string BuildNormalizedPromptPreview(
			const std::wstring& normalizedPrompt,
			const std::size_t maxChars) {
			if (maxChars == 0) {
				return {};
			}

			const std::wstring truncated =
				normalizedPrompt.size() > maxChars
				? normalizedPrompt.substr(0, maxChars)
				: normalizedPrompt;
			return WideToUtf8Local(truncated);
		}

		void AppendStartupTrace(const char* stage) {
			CServiceBootstrapCoordinator coordinator;
			coordinator.AppendStartupTrace(stage);
		}

		std::wstring ToWide(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			const int needed = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0);
			if (needed <= 0) {
				return {};
			}

			std::wstring output(static_cast<std::size_t>(needed), L'\0');
			MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				needed);
			return output;
		}

		std::string EscapeJsonUtf8(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);
			for (const char ch : value) {
				switch (ch) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}

			return escaped;
		}

		void EmitDeepSeekDiagnostic(
			const char* stage,
			const std::string& detail) {
			const std::string safeStage =
				(stage == nullptr || std::string(stage).empty())
				? "unknown"
				: std::string(stage);
			TRACE(
				"[DeepSeek][%s] %s\n",
				safeStage.c_str(),
				detail.c_str());
		}

		std::string MaskSecretForTrace(const std::wstring& value) {
			if (value.empty()) {
				return "<empty>";
			}

			if (value.size() <= 4) {
				return "<len=" + std::to_string(value.size()) + ">";
			}

			const std::wstring masked =
				value.substr(0, 2) +
				L"***" +
				value.substr(value.size() - 2) +
				L"<len=" + std::to_wstring(value.size()) + L">";
			return ToNarrow(masked);
		}

		void EmitBaiduRuntimeDiagnostic(
			const char* stage,
			const std::string& detail) {
			const std::string safeStage =
				(stage == nullptr || std::string(stage).empty())
				? "unknown"
				: std::string(stage);
			TRACE(
				"[BaiduRuntime][%s] %s\n",
				safeStage.c_str(),
				detail.c_str());
		}

		std::string TruncateDiagnosticText(
			const std::string& value,
			const std::size_t maxChars = 1200) {
			if (value.size() <= maxChars) {
				return value;
			}

			if (maxChars <= 24) {
				return value.substr(0, maxChars);
			}

			return value.substr(0, maxChars - 24) + "...(truncated)";
		}

		std::string LastNonEmptyLine(const std::string& text) {
			std::string line;
			for (std::size_t i = text.size(); i > 0; --i) {
				const char ch = text[i - 1];
				if (ch == '\n' || ch == '\r') {
					if (!line.empty()) {
						std::reverse(line.begin(), line.end());
						return blazeclaw::gateway::json::Trim(line);
					}
					continue;
				}

				line.push_back(ch);
			}

			if (line.empty()) {
				return {};
			}

			std::reverse(line.begin(), line.end());
			return blazeclaw::gateway::json::Trim(line);
		}

		std::optional<nlohmann::json> TryParseTrailingJsonObject(const std::string& text) {
			const std::string candidate = LastNonEmptyLine(text);
			if (candidate.empty()) {
				return std::nullopt;
			}

			nlohmann::json parsed = nlohmann::json::parse(candidate, nullptr, false);
			if (!parsed.is_object()) {
				return std::nullopt;
			}

			return parsed;
		}

		void EnsureBaiduApiKeyRuntimeEnv() {
			wchar_t* inheritedValue = nullptr;
			std::size_t inheritedLength = 0;
			std::wstring inheritedKey;
			if (_wdupenv_s(
				&inheritedValue,
				&inheritedLength,
				L"BAIDU_API_KEY") == 0 &&
				inheritedValue != nullptr) {
				inheritedKey.assign(inheritedValue);
				free(inheritedValue);
			}

			const auto persisted = servicemanager_baidu_env::ResolveBaiduApiKeyFromPersistedConfig();
			if (persisted.has_value() && !persisted->empty()) {
				_wputenv_s(L"BAIDU_API_KEY", persisted.value().c_str());
				EmitBaiduRuntimeDiagnostic(
					"env",
					"BAIDU_API_KEY source=persisted set=true value=" +
					MaskSecretForTrace(persisted.value()));
				return;
			}

			if (!inheritedKey.empty()) {
				EmitBaiduRuntimeDiagnostic(
					"env",
					"BAIDU_API_KEY source=process set=false inherited=true value=" +
					MaskSecretForTrace(inheritedKey));
				return;
			}

			EmitBaiduRuntimeDiagnostic(
				"env",
				"BAIDU_API_KEY source=none set=false inherited=false value=<empty>");

			wchar_t* envValue = nullptr;
			std::size_t envLength = 0;
			if (_wdupenv_s(
				&envValue,
				&envLength,
				L"BAIDU_API_KEY") == 0 &&
				envValue != nullptr) {
				free(envValue);
			}
		}

		bool ReadBoolEnvOrDefault(const wchar_t* key, const bool fallback) {
			wchar_t* value = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&value, &length, key) != 0 || value == nullptr ||
				length == 0) {
				if (value != nullptr) {
					free(value);
				}

				return fallback;
			}

			std::wstring normalized;
			normalized.reserve(length);
			for (std::size_t i = 0; i < length && value[i] != L'\0'; ++i) {
				normalized.push_back(static_cast<wchar_t>(std::towlower(value[i])));
			}
			free(value);

			if (normalized == L"1" || normalized == L"true" || normalized == L"yes" ||
				normalized == L"on") {
				return true;
			}

			if (normalized == L"0" || normalized == L"false" || normalized == L"no" ||
				normalized == L"off") {
				return false;
			}

			return fallback;
		}

		bool PersistSkillConfigEnvViaHostDocument(
			const std::string& skill,
			const std::string& envContent,
			std::string& outError,
			std::filesystem::path& outPath) {
			CBlazeClawMFCDoc* activeDoc = nullptr;
			auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
			if (mainFrame != nullptr) {
				auto* activeChild = DYNAMIC_DOWNCAST(
					CMDIChildWndEx,
					mainFrame->MDIGetActive());
				if (activeChild != nullptr) {
					auto* activeView = DYNAMIC_DOWNCAST(
						CBlazeClawMFCView,
						activeChild->GetActiveView());
					if (activeView != nullptr) {
						activeDoc = activeView->GetDocument();
					}
				}
			}

			if (activeDoc == nullptr) {
				outError = "No active document context for skill update.";
				return false;
			}

			return activeDoc->SaveSkillConfigEnv(
				skill,
				envContent,
				outError,
				&outPath);
		}

		void RefreshSkillViewViaHostWindow() {
			auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
			if (mainFrame != nullptr) {
				mainFrame->RefreshSkillView();
			}
		}

		std::string ToLowerAscii(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		bool ContainsAnyFragment(
			const std::string& lowerText,
			std::initializer_list<const char*> fragments) {
			for (const auto* fragment : fragments) {
				if (fragment == nullptr || *fragment == '\0') {
					continue;
				}
				if (lowerText.find(fragment) != std::string::npos) {
					return true;
				}
			}
			return false;
		}

		bool ContainsAnyWideFragment(
			const std::wstring& text,
			std::initializer_list<const wchar_t*> fragments) {
			for (const auto* fragment : fragments) {
				if (fragment == nullptr || *fragment == L'\0') {
					continue;
				}
				if (text.find(fragment) != std::wstring::npos) {
					return true;
				}
			}
			return false;
		}

		std::string CanonicalizeForRouting(const std::string& message) {
			std::string canonical = message;
			const std::wstring wide = Utf8ToWideLocal(message);
			auto appendToken = [&](const char* token) {
				if (token == nullptr || *token == '\0') {
					return;
				}
				if (canonical.find(token) == std::string::npos) {
					canonical.append(" ");
					canonical.append(token);
				}
				};

			if (ContainsAnyWideFragment(
				wide,
				{ L"邮箱", L"收件箱", L"邮件", L"新邮件", L"查邮箱", L"查一下邮箱" })) {
				appendToken("inbox");
				appendToken("email");
			}
			if (ContainsAnyWideFragment(wide, { L"回复", L"回信", L"需要回复", L"尽快回复" })) {
				appendToken("reply");
			}
			if (ContainsAnyWideFragment(wide, { L"2小时", L"两小时", L"两个小时" })) {
				appendToken("within 2 hours");
			}
			return canonical;
		}

		bool LooksLikeInboxReplyUrgencyIntent(const std::string& message) {
			const std::string lower = ToLowerAscii(message);
			const std::wstring wide = Utf8ToWideLocal(message);
			const bool inboxSignalZh = ContainsAnyWideFragment(
				wide,
				{ L"邮箱", L"收件箱", L"邮件", L"新邮件", L"查邮箱", L"查一下邮箱" });
			const bool inboxSignal = ContainsAnyFragment(
				lower,
				{ "inbox", "unread", "mailbox", "email", "mail" }) || inboxSignalZh;
			const bool replySignalZh = ContainsAnyWideFragment(
				wide,
				{ L"回复", L"回信", L"需要回复", L"尽快回复" });
			const bool replySignal = ContainsAnyFragment(
				lower,
				{ "reply", "respond", "needs a reply", "need a reply" }) || replySignalZh;
			const bool urgencySignalZh = ContainsAnyWideFragment(
				wide,
				{ L"2小时", L"两小时", L"两个小时", L"紧急", L"尽快" });
			const bool urgencySignal = ContainsAnyFragment(
				lower,
				{ "within 2 hours", "within two hours", "2h", "2 hours", "urgent" }) ||
				urgencySignalZh;
			return (inboxSignal && replySignal) || (inboxSignal && urgencySignal);
		}

		bool LooksLikeInboxIntentAnyLanguage(const std::string& message) {
			const std::string lower = ToLowerAscii(message);
			const std::wstring wide = Utf8ToWideLocal(message);
			const bool inboxSignalZh = ContainsAnyWideFragment(
				wide,
				{ L"邮箱", L"收件箱", L"邮件", L"新邮件", L"查邮箱", L"查一下邮箱" });
			const bool replySignalZh = ContainsAnyWideFragment(
				wide,
				{ L"回复", L"回信", L"需要回复", L"尽快回复" });
			const bool inboxSignal = ContainsAnyFragment(
				lower,
				{ "inbox", "unread", "mailbox", "email", "mail" }) || inboxSignalZh;
			const bool replySignal = ContainsAnyFragment(
				lower,
				{ "reply", "respond", "needs a reply", "need a reply" }) || replySignalZh;
			return inboxSignal && replySignal;
		}

		std::string TrimAsciiLocal(const std::string& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const unsigned char ch) {
					return std::isspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const unsigned char ch) {
					return std::isspace(ch) != 0;
				}).base();

			if (first >= last) {
				return {};
			}

			return std::string(first, last);
		}

		std::optional<std::string> TryExtractImageGeneratorPrompt(
			const std::string& commandBodyNormalized) {
			const std::string trimmed = TrimAsciiLocal(commandBodyNormalized);
			if (trimmed.empty() || trimmed.front() == '/') {
				return std::nullopt;
			}

			const std::string lower = ToLowerAscii(trimmed);
			const bool hasImageGenerator =
				lower.find("image-generator") != std::string::npos ||
				lower.find("image generator") != std::string::npos;
			if (!hasImageGenerator) {
				return std::nullopt;
			}

			const std::wstring wide = Utf8ToWideLocal(trimmed);
			const bool hasInvokeSignal =
				ContainsAnyFragment(
					lower,
					{ "call", "invoke", "use", "run", "with" }) ||
				ContainsAnyWideFragment(
					wide,
					{ L"调用", L"使用", L"用", L"请用" });
			if (!hasInvokeSignal) {
				return std::nullopt;
			}

			std::string prompt;
			const std::size_t generatePos = lower.find("生成");
			if (generatePos != std::string::npos) {
				prompt = TrimAsciiLocal(trimmed.substr(generatePos + std::string("生成").size()));
			}

			if (prompt.empty()) {
				const std::size_t tokenPos = lower.find("image-generator");
				if (tokenPos != std::string::npos) {
					prompt = TrimAsciiLocal(trimmed.substr(tokenPos + std::string("image-generator").size()));
				}
			}

			if (prompt.empty()) {
				const std::size_t tokenPos = lower.find("image generator");
				if (tokenPos != std::string::npos) {
					prompt = TrimAsciiLocal(trimmed.substr(tokenPos + std::string("image generator").size()));
				}
			}

			if (!prompt.empty()) {
				while (!prompt.empty() &&
					(prompt.front() == '`' ||
						prompt.front() == '"' ||
						prompt.front() == '\'' ||
						prompt.front() == ',' ||
						prompt.front() == ':' ||
						prompt.front() == ';')) {
					prompt.erase(prompt.begin());
				}
				prompt = TrimAsciiLocal(prompt);
			}

			return prompt;
		}

		bool LooksLikeTwoHourUrgencyAnyLanguage(const std::string& message) {
			const std::string lower = ToLowerAscii(message);
			const std::wstring wide = Utf8ToWideLocal(message);
			return ContainsAnyFragment(
				lower,
				{ "within 2 hours", "within two hours", "2h", "2 hours" }) ||
				ContainsAnyWideFragment(wide, { L"2小时", L"两小时", L"两个小时" });
		}

		bool LooksLikeJsonObjectShapeLocal(const std::string& value) {
			const std::wstring trimmed = Trim(Utf8ToWideLocal(value));
			if (trimmed.size() < 2) {
				return false;
			}

			return trimmed.front() == L'{' && trimmed.back() == L'}';
		}

		std::wstring NormalizeInlineTriggerText(const std::wstring& input) {
			std::wstring normalized;
			normalized.reserve(input.size());
			bool previousSpace = false;
			for (const auto ch : input) {
				const wchar_t lowered =
					static_cast<wchar_t>(std::towlower(ch));
				const bool isAlphaNum =
					(lowered >= L'a' && lowered <= L'z') ||
					(lowered >= L'0' && lowered <= L'9') ||
					(lowered >= 0x4E00 && lowered <= 0x9FFF);
				if (isAlphaNum) {
					normalized.push_back(lowered);
					previousSpace = false;
					continue;
				}

				if (!previousSpace) {
					normalized.push_back(L' ');
					previousSpace = true;
				}
			}

			return Trim(normalized);
		}

		bool ContainsNormalizedTriggerHint(
			const std::wstring& normalizedPrompt,
			const std::wstring& triggerHint) {
			const std::wstring normalizedHint =
				NormalizeInlineTriggerText(triggerHint);
			if (normalizedHint.empty()) {
				return false;
			}

			const std::wstring promptNoSpace = [&normalizedPrompt]() {
				std::wstring value;
				value.reserve(normalizedPrompt.size());
				for (const auto ch : normalizedPrompt) {
					if (ch != L' ') {
						value.push_back(ch);
					}
				}
				return value;
			}();
			const std::wstring hintNoSpace = [&normalizedHint]() {
				std::wstring value;
				value.reserve(normalizedHint.size());
				for (const auto ch : normalizedHint) {
					if (ch != L' ') {
						value.push_back(ch);
					}
				}
				return value;
			}();

			if (promptNoSpace.empty() || hintNoSpace.empty()) {
				return false;
			}

			if (normalizedPrompt == normalizedHint) {
				return true;
			}

			if (promptNoSpace == hintNoSpace) {
				return true;
			}

			if (normalizedPrompt.find(normalizedHint) != std::wstring::npos) {
				return true;
			}

			return promptNoSpace.find(hintNoSpace) != std::wstring::npos;
		}

		enum class GeneratedTriggerMatchMode {
			None = 0,
			SpaceStrippedContains = 1,
			Contains = 2,
			NormalizedExact = 3,
			Exact = 4,
		};

		struct GeneratedTriggerHintMatchResult {
			GeneratedTriggerMatchMode mode = GeneratedTriggerMatchMode::None;
			int score = 0;
			std::wstring normalizedHint;
			std::size_t normalizedHintNoSpaceLength = 0;
		};

		struct GeneratedOpenClawRoutingDecisionDiagnostics {
			std::wstring normalizedPrompt;
			std::size_t candidateCountConsidered = 0;
			std::wstring matchedSkillKey;
			std::wstring matchedTriggerHint;
			GeneratedTriggerMatchMode matchMode = GeneratedTriggerMatchMode::None;
		};

		std::wstring RemoveWideSpaces(const std::wstring& value) {
			std::wstring collapsed;
			collapsed.reserve(value.size());
			for (const auto ch : value) {
				if (ch != L' ') {
					collapsed.push_back(ch);
				}
			}
			return collapsed;
		}

		GeneratedTriggerHintMatchResult EvaluateGeneratedTriggerHintMatch(
			const std::wstring& normalizedPrompt,
			const std::wstring& triggerHint) {
			GeneratedTriggerHintMatchResult result;
			if (!ContainsNormalizedTriggerHint(normalizedPrompt, triggerHint)) {
				return result;
			}

			result.normalizedHint = NormalizeInlineTriggerText(triggerHint);
			if (result.normalizedHint.empty() || normalizedPrompt.empty()) {
				return result;
			}

			const std::wstring promptNoSpace = RemoveWideSpaces(normalizedPrompt);
			const std::wstring hintNoSpace = RemoveWideSpaces(result.normalizedHint);
			if (promptNoSpace.empty() || hintNoSpace.empty()) {
				return result;
			}

			result.normalizedHintNoSpaceLength = hintNoSpace.size();
			if (normalizedPrompt == result.normalizedHint) {
				result.mode = GeneratedTriggerMatchMode::Exact;
				result.score = 400;
				return result;
			}

			if (promptNoSpace == hintNoSpace) {
				result.mode = GeneratedTriggerMatchMode::NormalizedExact;
				result.score = 300;
				return result;
			}

			if (normalizedPrompt.find(result.normalizedHint) != std::wstring::npos) {
				result.mode = GeneratedTriggerMatchMode::Contains;
				result.score = 200;
				return result;
			}

			if (promptNoSpace.find(hintNoSpace) != std::wstring::npos) {
				result.mode = GeneratedTriggerMatchMode::SpaceStrippedContains;
				result.score = 100;
				return result;
			}

			return result;
		}

		std::string GeneratedTriggerMatchModeToTelemetry(
			const GeneratedTriggerMatchMode mode) {
			switch (mode) {
			case GeneratedTriggerMatchMode::Exact:
				return "exact";
			case GeneratedTriggerMatchMode::NormalizedExact:
				return "normalized-exact";
			case GeneratedTriggerMatchMode::Contains:
				return "contains";
			case GeneratedTriggerMatchMode::SpaceStrippedContains:
				return "space-stripped contains";
			default:
				return "none";
			}
		}

		std::wstring NormalizeOpenClawGeneratedToolToken(
			const std::wstring& rawToken) {
			std::wstring token;
			token.reserve(rawToken.size());
			for (const auto ch : rawToken) {
				const wchar_t lowered =
					static_cast<wchar_t>(std::towlower(ch));
				const bool alphaNum =
					(lowered >= L'a' && lowered <= L'z') ||
					(lowered >= L'0' && lowered <= L'9');
				if (alphaNum) {
					token.push_back(lowered);
					continue;
				}

				if (lowered == L'-' || lowered == L'_' || lowered == L'.' ||
					lowered == L'/' || lowered == L'\\') {
					if (!token.empty() && token.back() != L'_') {
						token.push_back(L'_');
					}
				}
			}

			while (!token.empty() && token.front() == L'_') {
				token.erase(token.begin());
			}
			while (!token.empty() && token.back() == L'_') {
				token.pop_back();
			}

			if (token.empty()) {
				token = L"openclaw_skill";
			}

			return token;
		}

		std::string BuildGeneratedOpenClawToolName(
			const OpenClawOriginalExtractedRuntimeContractSpec& extracted,
			const std::wstring& fallbackSkillName) {
			std::wstring key = Trim(extracted.skillKey);
			if (key.empty()) {
				key = Trim(fallbackSkillName);
			}

			const std::wstring normalizedToken =
				NormalizeOpenClawGeneratedToolToken(key);
			return ToNarrow(normalizedToken) + ".openclaw.generated";
		}

		bool IsGeneratedOpenClawToolId(const std::string& toolId) {
			const std::string trimmed =
				blazeclaw::gateway::json::Trim(toolId);
			return !trimmed.empty() &&
				trimmed.size() >= std::string(".openclaw.generated").size() &&
				trimmed.rfind(".openclaw.generated") ==
				(trimmed.size() - std::string(".openclaw.generated").size());
		}

		const SkillsCatalogEntry* FindGeneratedOpenClawCatalogEntryByToolId(
			const std::vector<SkillsCatalogEntry>& catalogEntries,
			const std::string& toolId) {
			for (const auto& entry : catalogEntries) {
				if (entry.sourceKind != SkillsSourceKind::OpenClawOriginal ||
					!entry.openClawOriginalActivationState.has_value() ||
					entry.openClawOriginalActivationState.value() !=
					SkillsOpenClawOriginalActivationState::ToolEnabled ||
					!entry.openClawOriginalExtractedRuntimeContract.has_value() ||
					!entry.openClawOriginalExtractedRuntimeContract->complete) {
					continue;
				}

				const std::string generatedToolName =
					BuildGeneratedOpenClawToolName(
						entry.openClawOriginalExtractedRuntimeContract.value(),
						entry.skillName);
				if (generatedToolName == toolId) {
					return &entry;
				}
			}

			return nullptr;
		}

		std::optional<blazeclaw::gateway::ToolExecuteResultV2>
			TryExecuteGeneratedOpenClawConstantOutputTool(
				const std::vector<SkillsCatalogEntry>& catalogEntries,
				const blazeclaw::gateway::ToolExecuteRequestV2& request) {
			if (!IsGeneratedOpenClawToolId(request.tool)) {
				return std::nullopt;
			}

			const SkillsCatalogEntry* matchedEntry =
				FindGeneratedOpenClawCatalogEntryByToolId(catalogEntries, request.tool);
			if (matchedEntry == nullptr) {
				return std::nullopt;
			}

			const auto& extracted =
				matchedEntry->openClawOriginalExtractedRuntimeContract.value();
			if (!extracted.output.has_value()) {
				return std::nullopt;
			}

			const auto startedAtMs = CurrentEpochMs();
			blazeclaw::gateway::ToolExecuteResultV2 result;
			result.tool = request.tool;
			result.executed = true;
			result.status = "ok";
			result.errorCode.clear();
			result.errorMessage.clear();
			result.correlationId = request.correlationId;
			result.startedAtMs = startedAtMs;

			nlohmann::json payload = nlohmann::json::object();
			const auto& output = extracted.output.value();
			const std::string outputKind = ToNarrow(Trim(output.kind));
			const std::string outputTitle = ToNarrow(Trim(output.title));
			const std::string outputUrl = ToNarrow(Trim(output.url));

			if (!outputKind.empty()) {
				payload["kind"] = outputKind;
			}
			if (!outputTitle.empty()) {
				payload["title"] = outputTitle;
			}
			if (!outputUrl.empty()) {
				payload["url"] = outputUrl;
			}

			if (!outputKind.empty() || !outputUrl.empty()) {
				nlohmann::json outputItem = nlohmann::json::object();
				if (!outputKind.empty()) {
					outputItem["type"] = outputKind;
				}
				if (!outputTitle.empty()) {
					outputItem["title"] = outputTitle;
				}
				if (!outputUrl.empty()) {
					outputItem["url"] = outputUrl;
				}
				payload["outputs"] = nlohmann::json::array({ outputItem });
			}

			const std::wstring rawJsonPayload = Trim(output.jsonPayload);
			if (!rawJsonPayload.empty()) {
				const std::string rawPayload = ToNarrow(rawJsonPayload);
				const auto parsed = nlohmann::json::parse(
					rawPayload,
					nullptr,
					false);
				if (!parsed.is_discarded()) {
					payload["contractPayload"] = parsed;
				}
				else {
					payload["contractPayloadRaw"] = rawPayload;
				}
			}

			payload["source"] = "openclaw.generated.runtime-contract";
			payload["skill"] = ToNarrow(matchedEntry->skillName);
			result.result = payload.dump();

			result.completedAtMs = CurrentEpochMs();
			result.latencyMs = result.completedAtMs >= result.startedAtMs
				? (result.completedAtMs - result.startedAtMs)
				: 0;
			return result;
		}

		struct GeneratedOpenClawOutputTuple {
			std::string kind;
			std::string title;
			std::string url;
		};

		std::optional<GeneratedOpenClawOutputTuple>
			ExtractGeneratedOpenClawOutputKindTitleAndUrl(
				const blazeclaw::gateway::ToolExecuteResultV2& result) {
			const std::string trimmedResult =
				blazeclaw::gateway::json::Trim(result.result);
			if (trimmedResult.empty()) {
				return std::nullopt;
			}

			auto readKindTitleAndUrl = [](const nlohmann::json& node)
				-> std::optional<GeneratedOpenClawOutputTuple> {
				if (!node.is_object()) {
					return std::nullopt;
				}

				std::string kind;
				const auto typeIt = node.find("type");
				if (typeIt != node.end() && typeIt->is_string()) {
					kind = blazeclaw::gateway::json::Trim(
						typeIt->get<std::string>());
				}
				const auto kindIt = node.find("kind");
				if (kind.empty() && kindIt != node.end() && kindIt->is_string()) {
					kind = blazeclaw::gateway::json::Trim(
						kindIt->get<std::string>());
				}

				const auto urlIt = node.find("url");
				if (urlIt == node.end() || !urlIt->is_string()) {
					return std::nullopt;
				}

				std::string title;
				const auto titleIt = node.find("title");
				if (titleIt != node.end() && titleIt->is_string()) {
					title = blazeclaw::gateway::json::Trim(
						titleIt->get<std::string>());
				}
				return GeneratedOpenClawOutputTuple{
					.kind = kind,
					.title = title,
					.url = blazeclaw::gateway::json::Trim(urlIt->get<std::string>()),
				};
			};

			if (trimmedResult.front() == '{' && trimmedResult.back() == '}') {
				const auto parsed = nlohmann::json::parse(
					trimmedResult,
					nullptr,
					false);
				if (!parsed.is_discarded() && parsed.is_object()) {
					if (const auto direct = readKindTitleAndUrl(parsed);
						direct.has_value() && !direct->url.empty()) {
						return direct;
					}

					const auto outputsIt = parsed.find("outputs");
					if (outputsIt != parsed.end() && outputsIt->is_array()) {
						for (const auto& item : *outputsIt) {
							if (const auto nested = readKindTitleAndUrl(item);
								nested.has_value() && !nested->url.empty()) {
								return nested;
							}
						}
					}
				}
			}

			if (trimmedResult.rfind("http://", 0) == 0 ||
				trimmedResult.rfind("https://", 0) == 0) {
				return GeneratedOpenClawOutputTuple{
					.kind = std::string(),
					.title = std::string(),
					.url = trimmedResult,
				};
			}

			return std::nullopt;
		}

		std::optional<std::string> ResolveGeneratedOpenClawToolTargetFromTriggerHints(
			const std::vector<SkillsCatalogEntry>& catalogEntries,
			const std::string& commandBodyNormalized,
			GeneratedOpenClawRoutingDecisionDiagnostics* diagnostics = nullptr) {
			GeneratedOpenClawRoutingDecisionDiagnostics localDiagnostics;
			GeneratedOpenClawRoutingDecisionDiagnostics& activeDiagnostics =
				diagnostics == nullptr ? localDiagnostics : *diagnostics;
			activeDiagnostics = GeneratedOpenClawRoutingDecisionDiagnostics{};

			activeDiagnostics.normalizedPrompt =
				NormalizeInlineTriggerText(Utf8ToWideLocal(commandBodyNormalized));
			const std::wstring& normalizedPrompt = activeDiagnostics.normalizedPrompt;
			if (normalizedPrompt.empty()) {
				return std::nullopt;
			}

			struct CandidateMatch {
				int score = 0;
				std::size_t hintLength = 0;
				std::wstring skillKey;
				std::wstring triggerHint;
				std::string generatedToolName;
				GeneratedTriggerMatchMode matchMode = GeneratedTriggerMatchMode::None;
			};

			std::optional<CandidateMatch> bestMatch;

			for (const auto& entry : catalogEntries) {
				if (entry.sourceKind != SkillsSourceKind::OpenClawOriginal ||
					!entry.openClawOriginalActivationState.has_value() ||
					entry.openClawOriginalActivationState.value() !=
					SkillsOpenClawOriginalActivationState::ToolEnabled ||
					!entry.openClawOriginalExtractedRuntimeContract.has_value() ||
					!entry.openClawOriginalExtractedRuntimeContract->complete) {
					continue;
				}

				const auto& extracted =
					entry.openClawOriginalExtractedRuntimeContract.value();
				if (extracted.triggerHints.empty()) {
					continue;
				}

				const std::string generatedToolName =
					BuildGeneratedOpenClawToolName(
						extracted,
						entry.skillName);
				if (generatedToolName.empty()) {
					continue;
				}

				for (const auto& triggerHint : extracted.triggerHints) {
					++activeDiagnostics.candidateCountConsidered;
					const auto matchResult = EvaluateGeneratedTriggerHintMatch(
						normalizedPrompt,
						triggerHint);
					if (matchResult.mode == GeneratedTriggerMatchMode::None) {
						continue;
					}

					CandidateMatch candidate;
					candidate.score = matchResult.score;
					candidate.hintLength = matchResult.normalizedHintNoSpaceLength;
					candidate.skillKey = extracted.skillKey.empty()
						? entry.skillName
						: extracted.skillKey;
					candidate.triggerHint = triggerHint;
					candidate.generatedToolName = generatedToolName;
					candidate.matchMode = matchResult.mode;

					const bool shouldReplace = !bestMatch.has_value() ||
						candidate.score > bestMatch->score ||
						(candidate.score == bestMatch->score &&
							candidate.hintLength > bestMatch->hintLength) ||
						(candidate.score == bestMatch->score &&
							candidate.hintLength == bestMatch->hintLength &&
							candidate.skillKey < bestMatch->skillKey);
					if (shouldReplace) {
						bestMatch = candidate;
					}
				}
			}

			if (bestMatch.has_value()) {
				activeDiagnostics.matchedSkillKey = bestMatch->skillKey;
				activeDiagnostics.matchedTriggerHint = bestMatch->triggerHint;
				activeDiagnostics.matchMode = bestMatch->matchMode;
				return bestMatch->generatedToolName;
			}

			return std::nullopt;
		}

		std::optional<std::string> BuildInlineArgsForResolvedTool(
			const std::string& resolvedToolId,
			const std::string& commandBodyNormalized) {
			if (resolvedToolId == "image-generator.generate") {
				nlohmann::json params = nlohmann::json::object();
				params["prompt"] = TryExtractImageGeneratorPrompt(commandBodyNormalized)
					.value_or(commandBodyNormalized);
				return params.dump();
			}

			if (resolvedToolId != "imap_smtp_email.imap.search") {
				return std::nullopt;
			}

			// Respect explicit JSON object payloads from advanced callers.
			if (LooksLikeJsonObjectShapeLocal(commandBodyNormalized)) {
				return std::nullopt;
			}

			if (!LooksLikeInboxIntentAnyLanguage(commandBodyNormalized)) {
				return std::nullopt;
			}

			nlohmann::json params = nlohmann::json::object();
			params["unseen"] = true;
			params["recent"] = LooksLikeTwoHourUrgencyAnyLanguage(commandBodyNormalized)
				? "2h"
				: "24h";
			params["limit"] = 20;
			return params.dump();
		}

		std::optional<std::string> BuildInlineFriendlyTextForResolvedTool(
			const std::string& resolvedToolId,
			const blazeclaw::gateway::ToolExecuteResultV2& result) {
			if (IsGeneratedOpenClawToolId(resolvedToolId)) {
				const auto output =
					ExtractGeneratedOpenClawOutputKindTitleAndUrl(result);
				if (output.has_value()) {
					std::string formatted = "url=" + output->url;
					if (!output->title.empty()) {
						formatted = "title=" + output->title + "; " + formatted;
					}
					if (!output->kind.empty()) {
						formatted = "type=" + output->kind + "; " + formatted;
					}
					return formatted;
				}
			}

			if (resolvedToolId != "imap_smtp_email.imap.search") {
				return std::nullopt;
			}

			const std::string trimmedResult =
				ToNarrow(Trim(Utf8ToWideLocal(result.result)));
			if (trimmedResult.empty()) {
				return std::nullopt;
			}

			try {
				const auto parsed = nlohmann::json::parse(trimmedResult);
				if (parsed.is_array()) {
					if (parsed.empty()) {
						return std::string(
							"I checked your inbox in the recent window and found no messages "
							"that need a reply.");
					}
					return std::string("I found ") +
						std::to_string(parsed.size()) +
						" inbox message(s) from the recent window for reply triage.";
				}
			}
			catch (...) {
				// Preserve default rendering when tool output is not JSON.
			}

			return std::nullopt;
		}

		std::filesystem::path ResolveWorkspaceRootForSkills(
			const std::filesystem::path& startPath) {
			std::error_code ec;
			auto cursor = std::filesystem::absolute(startPath, ec);
			if (ec) {
				return startPath;
			}

			while (!cursor.empty()) {
				const auto directSkills = cursor / L"skills";
				if (std::filesystem::is_directory(directSkills, ec) && !ec) {
					return cursor;
				}

				const auto nestedSkills = cursor / L"blazeclaw" / L"skills";
				if (std::filesystem::is_directory(nestedSkills, ec) && !ec) {
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

			return startPath;
		}

		std::string NormalizeDirectoryPathUtf8(const std::filesystem::path& path) {
			return servicemanager_skill_roots::NormalizeDirectoryPathUtf8(path);
		}

		std::vector<std::string> BuildCanonicalSkillRootSnapshot(
			const std::filesystem::path& workspaceRoot,
			const blazeclaw::config::AppConfig& config) {
			return servicemanager_skill_roots::BuildCanonicalSkillRootSnapshot(
				workspaceRoot,
				config);
		}

		std::vector<std::string> ParseCsvEnvValues(const wchar_t* key) {
			std::vector<std::string> values;
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, key) != 0 || env == nullptr || len == 0) {
				if (env != nullptr) {
					free(env);
				}
				return values;
			}

			std::wstring token;
			for (std::size_t i = 0; i < len && env[i] != L'\0'; ++i) {
				if (env[i] == L',' || env[i] == L';') {
					const auto trimmed = Trim(token);
					if (!trimmed.empty()) {
						values.push_back(ToNarrow(trimmed));
					}
					token.clear();
					continue;
				}

				token.push_back(env[i]);
			}

			const auto trimmed = Trim(token);
			if (!trimmed.empty()) {
				values.push_back(ToNarrow(trimmed));
			}

			free(env);
			return values;
		}

		std::uint64_t ParseUInt64EnvValue(
			const wchar_t* key,
			const std::uint64_t fallback) {
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, key) != 0 || env == nullptr || len == 0) {
				if (env != nullptr) {
					free(env);
				}
				return fallback;
			}

			const std::wstring rawValue(env);
			free(env);

			const std::wstring trimmed = Trim(rawValue);
			if (trimmed.empty()) {
				return fallback;
			}

			try {
				return static_cast<std::uint64_t>(std::stoull(trimmed));
			}
			catch (...) {
				return fallback;
			}
		}

		double ParseDoubleEnvValue(const wchar_t* key, const double fallback) {
			wchar_t* env = nullptr;
			std::size_t len = 0;
			if (_wdupenv_s(&env, &len, key) != 0 || env == nullptr || len == 0) {
				if (env != nullptr) {
					free(env);
				}
				return fallback;
			}

			const std::wstring rawValue(env);
			free(env);

			const std::wstring trimmed = Trim(rawValue);
			if (trimmed.empty()) {
				return fallback;
			}

			try {
				return std::stod(trimmed);
			}
			catch (...) {
				return fallback;
			}
		}

		bool ContainsCaseInsensitive(
			const std::vector<std::string>& values,
			const std::string& candidate) {
			if (candidate.empty()) {
				return false;
			}

			const std::string loweredCandidate = ToLowerAscii(candidate);
			for (const auto& value : values) {
				if (ToLowerAscii(value) == loweredCandidate) {
					return true;
				}
			}

			return false;
		}

		std::wstring TrimWide(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				})
				.base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::wstring ToLowerWide(std::wstring value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return value;
		}

		std::vector<std::wstring> SplitCommaDelimitedWide(
			const std::wstring& rawValue) {
			std::vector<std::wstring> values;
			std::wstring token;
			for (const wchar_t ch : rawValue) {
				if (ch == L',' || ch == L';') {
					const std::wstring trimmed = TrimWide(token);
					if (!trimmed.empty()) {
						values.push_back(trimmed);
					}
					token.clear();
					continue;
				}

				token.push_back(ch);
			}

			const std::wstring trimmed = TrimWide(token);
			if (!trimmed.empty()) {
				values.push_back(trimmed);
			}

			return values;
		}

		const std::wstring* FindFrontmatterFieldCaseInsensitive(
			const SkillFrontmatter& frontmatter,
			const std::wstring& key) {
			const std::wstring loweredKey = ToLowerWide(key);
			for (const auto& item : frontmatter.fields) {
				if (ToLowerWide(item.first) == loweredKey) {
					return &item.second;
				}
			}

			return nullptr;
		}

		const std::wstring* ResolveNormalizedField(
			const SkillFrontmatter& frontmatter,
			const std::vector<std::wstring>& blazeclawKeys,
			const std::vector<std::wstring>& openclawKeys,
			std::vector<std::string>& outSources) {
			for (const auto& key : blazeclawKeys) {
				if (const auto* value = FindFrontmatterFieldCaseInsensitive(frontmatter, key);
					value != nullptr && !TrimWide(*value).empty()) {
					outSources.push_back("metadata.blazeclaw");
					return value;
				}
			}

			for (const auto& key : openclawKeys) {
				if (const auto* value = FindFrontmatterFieldCaseInsensitive(frontmatter, key);
					value != nullptr && !TrimWide(*value).empty()) {
					outSources.push_back("metadata.openclaw");
					return value;
				}
			}

			return nullptr;
		}

		std::vector<std::string> UniqueNarrowValues(
			const std::vector<std::wstring>& values) {
			std::vector<std::string> output;
			for (const auto& value : values) {
				const std::string narrow = ToNarrow(value);
				if (narrow.empty()) {
					continue;
				}

				if (std::find(output.begin(), output.end(), narrow) != output.end()) {
					continue;
				}

				output.push_back(narrow);
			}

			return output;
		}

		std::wstring BuildAuthProfilesSignature(
			const blazeclaw::config::AuthProfilesConfig& config) {
			std::wostringstream stream;
			stream << L"order=";
			for (const auto& entryId : config.order) {
				stream << entryId << L";";
			}

			stream << L"|entries=";
			for (const auto& [entryId, entry] : config.entries) {
				stream << entryId << L":"
					<< entry.provider << L":"
					<< entry.credentialRef << L":"
					<< entry.cooldownSeconds << L":"
					<< (entry.enabled ? L"1" : L"0") << L";";
			}

			return stream.str();
		}

		bool HasAuthSensitiveConfigChanges(
			const blazeclaw::config::AppConfig& currentConfig,
			const blazeclaw::config::AppConfig& nextConfig) {
			if (currentConfig.chat.activeProvider !=
				nextConfig.chat.activeProvider) {
				return true;
			}

			if (currentConfig.deepseekApiKey != nextConfig.deepseekApiKey) {
				return true;
			}

			return BuildAuthProfilesSignature(currentConfig.authProfiles) !=
				BuildAuthProfilesSignature(nextConfig.authProfiles);
		}


		std::uint64_t CurrentEpochMs() {
			const auto now = std::chrono::system_clock::now();
			return static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					now.time_since_epoch())
				.count());
		}

		void RegisterImageGeneratorRuntimeTools(
			blazeclaw::gateway::GatewayHost& host,
			const CToolRuntimeRegistry::ToolRuntimePolicySettings& toolPolicy) {
			const auto skillRoot = toolPolicy.imageGeneratorSkillRoot;
			for (const auto& spec : tools::BuildImageGeneratorToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "image",
						.enabled = true,
					},
					[spec, skillRoot](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

						if (!skillRoot.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "skill_runtime_missing";
							result.errorMessage = "image-generator skill root not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto scriptPath = skillRoot.value() / ToWide(spec.script);
						if (!std::filesystem::exists(scriptPath)) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "script_missing";
							result.errorMessage = "image-generator script not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						nlohmann::json params = nlohmann::json::object();
						if (request.argsJson.has_value() && !request.argsJson->empty()) {
							try {
								params = nlohmann::json::parse(request.argsJson.value());
							}
							catch (...) {
								result.executed = false;
								result.status = "error";
								result.errorCode = "invalid_args_json";
								result.errorMessage = "argsJson is not valid JSON";
								result.completedAtMs = CurrentEpochMs();
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}
						}

						if (params.is_string()) {
							params = nlohmann::json::object(
								{ { "prompt", params.get<std::string>() } });
						}

						if (!params.is_object()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "tool args must be a JSON object";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::string argsErrorCode;
						std::string argsErrorMessage;
						const auto cliArgs = tools::BuildImageGeneratorCliArgs(
							spec,
							params,
							argsErrorCode,
							argsErrorMessage);
						if (!cliArgs.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = argsErrorCode.empty()
								? "invalid_arguments"
								: argsErrorCode;
							result.errorMessage = argsErrorMessage.empty()
								? "tool arguments are invalid"
								: argsErrorMessage;
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::uint64_t timeoutMs = 180000;
						if (request.deadlineEpochMs.has_value()) {
							const std::uint64_t now = CurrentEpochMs();
							if (request.deadlineEpochMs.value() <= now) {
								result.executed = false;
								result.status = "timed_out";
								result.errorCode = "deadline_exceeded";
								result.errorMessage = "request deadline already elapsed";
								result.completedAtMs = now;
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}

							timeoutMs = request.deadlineEpochMs.value() - now;
						}

						const auto process = tools::ExecutePythonSkillProcess(
							scriptPath,
							cliArgs.value(),
							timeoutMs);

						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;

						if (!process.started) {
							result.executed = false;
							result.status = "error";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "process_start_failed"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "failed to start image-generator process"
								: process.errorMessage;
							return result;
						}

						if (process.timedOut) {
							result.executed = false;
							result.status = "timed_out";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "deadline_exceeded"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "image-generator execution timed out"
								: process.errorMessage;
							return result;
						}

						if (process.exitCode != 0) {
							result.executed = false;
							result.status = "script_runtime_error";
							result.result = process.output;
							result.errorCode = "script_runtime_error";
							result.errorMessage = process.output.empty()
								? "image-generator process exited non-zero"
								: TruncateDiagnosticText(process.output);
							return result;
						}

						nlohmann::json outputEnvelope = nlohmann::json::object();
						if (const auto generatedJson = TryParseTrailingJsonObject(process.output);
							generatedJson.has_value()) {
							outputEnvelope = generatedJson.value();
						}
						else {
							outputEnvelope["success"] = true;
							outputEnvelope["raw"] = process.output;
						}

						const std::string localPath = outputEnvelope.value("local_path", "");
						const std::string cosKey = outputEnvelope.value("cos_key", "");
						const auto uploadScript =
							skillRoot.value().parent_path() / L"upload-to-oss" / L"main.py";
						if (!localPath.empty() &&
							!cosKey.empty() &&
							std::filesystem::exists(uploadScript)) {
							const auto uploadResult = tools::ExecutePythonSkillProcess(
								uploadScript,
								{ "upload", localPath, "image-generator/" + cosKey },
								60000);
							outputEnvelope["uploadExecuted"] = uploadResult.started;
							if (uploadResult.started &&
								!uploadResult.timedOut &&
								uploadResult.exitCode == 0) {
								const std::string uploadedUrl = LastNonEmptyLine(uploadResult.output);
								if (!uploadedUrl.empty()) {
									outputEnvelope["url"] = uploadedUrl;
								}
							}
							else {
								outputEnvelope["uploadError"] = TruncateDiagnosticText(uploadResult.output);
							}
						}

						result.executed = true;
						result.status = "ok";
						result.errorCode.clear();
						result.errorMessage.clear();
						result.result = outputEnvelope.dump();
						return result;
					});
			}
		}




		void RegisterBaiduSearchRuntimeTools(
			blazeclaw::gateway::GatewayHost& host,
			const CToolRuntimeRegistry::ToolRuntimePolicySettings& toolPolicy) {
			const auto skillRoot = toolPolicy.baiduSearchSkillRoot;
			for (const auto& spec : tools::BuildBaiduSearchToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "search",
						.enabled = true,
					},
					[spec, skillRoot](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

						EnsureBaiduApiKeyRuntimeEnv();
						wchar_t* baiduApiKey = nullptr;
						std::size_t baiduApiKeyLen = 0;
						const bool hasBaiduApiKey =
							(_wdupenv_s(&baiduApiKey, &baiduApiKeyLen, L"BAIDU_API_KEY") == 0 &&
								baiduApiKey != nullptr &&
								baiduApiKeyLen > 0);
						if (baiduApiKey != nullptr) {
							free(baiduApiKey);
						}
						if (!hasBaiduApiKey) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "baidu_api_key_missing";
							result.errorMessage =
								"BAIDU_API_KEY missing in runtime environment and persisted skill config.";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						if (!skillRoot.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "skill_runtime_missing";
							result.errorMessage = "baidu-search skill root not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto scriptPath = skillRoot.value() / ToWide(spec.script);
						if (!std::filesystem::exists(scriptPath)) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "script_missing";
							result.errorMessage = "tool script not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						nlohmann::json params = nlohmann::json::object();
						if (request.argsJson.has_value() && !request.argsJson->empty()) {
							try {
								params = nlohmann::json::parse(request.argsJson.value());
							}
							catch (...) {
								result.executed = false;
								result.status = "error";
								result.errorCode = "invalid_args_json";
								result.errorMessage = "argsJson is not valid JSON";
								result.completedAtMs = CurrentEpochMs();
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}
						}

						if (params.is_string()) {
							params = nlohmann::json::object(
								{ {"query", params.get<std::string>()} });
						}

						if (!params.is_object()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "tool args must be a JSON object";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::string argsErrorCode;
						std::string argsErrorMessage;
						const auto cliArgs = tools::BuildBaiduSearchCliArgs(
							spec,
							params,
							argsErrorCode,
							argsErrorMessage);
						if (!cliArgs.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = argsErrorCode.empty()
								? "invalid_arguments"
								: argsErrorCode;
							result.errorMessage = argsErrorMessage.empty()
								? "tool arguments are invalid"
								: argsErrorMessage;
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::uint64_t timeoutMs = 45000;
						if (request.deadlineEpochMs.has_value()) {
							const std::uint64_t now = CurrentEpochMs();
							if (request.deadlineEpochMs.value() <= now) {
								result.executed = false;
								result.status = "timed_out";
								result.errorCode = "deadline_exceeded";
								result.errorMessage = "request deadline already elapsed";
								result.completedAtMs = now;
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}

							timeoutMs = request.deadlineEpochMs.value() - now;
						}

						const auto process = tools::ExecutePythonSkillProcess(
							scriptPath,
							cliArgs.value(),
							timeoutMs);

						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;

						if (!process.started) {
							EmitBaiduRuntimeDiagnostic(
								"process_start_failed",
								"errorCode=" + process.errorCode +
								" message=" + process.errorMessage);
							result.executed = false;
							result.status = "error";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "process_start_failed"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "failed to start tool process"
								: process.errorMessage;
							return result;
						}

						if (process.timedOut) {
							EmitBaiduRuntimeDiagnostic(
								"process_timeout",
								"errorCode=" + process.errorCode +
								" output=" +
								TruncateDiagnosticText(process.output));
							result.executed = false;
							result.status = "timed_out";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "deadline_exceeded"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "tool execution timed out"
								: process.errorMessage;
							return result;
						}

						result.result = process.output;
						if (process.exitCode == 0) {
							result.executed = true;
							result.status = "ok";
							result.errorCode.clear();
							result.errorMessage.clear();
							return result;
						}

						result.executed = false;
						const std::string classifiedFailure =
							tools::ClassifyBaiduFailureCode(process.output);
						const std::string normalizedFailure =
							classifiedFailure == "process_exit_nonzero"
							? "script_runtime_error"
							: classifiedFailure;
						EmitBaiduRuntimeDiagnostic(
							"process_nonzero",
							"exitCode=" +
							std::to_string(static_cast<unsigned long long>(process.exitCode)) +
							" classifiedFailure=" + classifiedFailure +
							" normalizedFailure=" + normalizedFailure +
							" output=" + TruncateDiagnosticText(process.output));
						result.status = normalizedFailure;
						result.errorCode = normalizedFailure;
						if (!result.result.empty()) {
							result.errorMessage = result.result;
						}
						else {
							result.errorMessage =
								"tool process returned non-zero exit code " +
								std::to_string(static_cast<unsigned long long>(process.exitCode));
						}
						return result;
					});
			}
		}

		void RegisterContentPolishingRuntimeTools(
			blazeclaw::gateway::GatewayHost& host) {
			for (const auto& spec : tools::BuildContentPolishingToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "transform",
						.enabled = true,
					},
					[spec](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

						nlohmann::json params = nlohmann::json::object();
						if (request.argsJson.has_value() && !request.argsJson->empty()) {
							try {
								params = nlohmann::json::parse(request.argsJson.value());
							}
							catch (...) {
								result.executed = false;
								result.status = "error";
								result.errorCode = "invalid_args_json";
								result.errorMessage = "argsJson is not valid JSON";
								result.completedAtMs = CurrentEpochMs();
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}
						}

						if (params.is_string()) {
							params = nlohmann::json::object(
								{ { "text", params.get<std::string>() } });
						}

						if (params.is_array()) {
							nlohmann::json coerced = nlohmann::json::object();
							for (const auto& el : params) {
								if (el.is_object()) {
									coerced = el;
									break;
								}
							}
							params = std::move(coerced);
						}

						if (!params.is_object()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "tool args must be a JSON object";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto text =
							spec.id == "summarize.extract"
							? tools::ExtractTextArgument(params)
							: tools::ExtractHumanizerTextArgument(params);
						if (!text.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage =
								spec.id == "summarize.extract"
								? "text_must_include_a_usable_draft_content_segment"
								: "text_or_summary_is_required";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						if (spec.id == "summarize.extract") {
							result.executed = true;
							result.status = "ok";
							result.result = tools::BuildSummarizeExtractOutput(text.value());
						}
						else {
							result.executed = true;
							result.status = "ok";
							result.result = tools::BuildHumanizerRewriteOutput(text.value());
						}

						result.errorCode.clear();
						result.errorMessage.clear();
						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;
						return result;
					});
			}
		}

		void RegisterImapSmtpRuntimeTools(
			blazeclaw::gateway::GatewayHost& host,
			const CToolRuntimeRegistry::ToolRuntimePolicySettings& toolPolicy) {
			const auto skillRoot = toolPolicy.imapSmtpSkillRoot;
			for (const auto& spec : tools::BuildImapSmtpToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "email",
						.enabled = true,
					},
					[spec, skillRoot](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

						if (!skillRoot.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "skill_runtime_missing";
							result.errorMessage = "imap-smtp-email skill root not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto scriptPath = skillRoot.value() / ToWide(spec.script);
						if (!std::filesystem::exists(scriptPath)) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "script_missing";
							result.errorMessage = "tool script not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto nodeModulesImap =
							skillRoot.value() / L"node_modules" / L"imap";
						if (!std::filesystem::exists(nodeModulesImap)) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "node_dependencies_missing";
							result.errorMessage =
								"imap-smtp-email Node dependencies are not installed "
								"(missing node_modules/imap). From the skill directory run: "
								"npm ci   (Windows: .\\setup.ps1 installs dependencies.)";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						nlohmann::json params = nlohmann::json::object();
						if (request.argsJson.has_value() && !request.argsJson->empty()) {
							try {
								params = nlohmann::json::parse(request.argsJson.value());
							}
							catch (...) {
								result.executed = false;
								result.status = "error";
								result.errorCode = "invalid_args_json";
								result.errorMessage = "argsJson is not valid JSON";
								result.completedAtMs = CurrentEpochMs();
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}
						}

						if (!params.is_object()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "tool args must be a JSON object";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::string argsErrorCode;
						std::string argsErrorMessage;
						const auto cliArgs = tools::BuildImapSmtpCliArgs(
							spec,
							params,
							argsErrorCode,
							argsErrorMessage);
						if (!cliArgs.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = argsErrorCode.empty()
								? "invalid_arguments"
								: argsErrorCode;
							result.errorMessage = argsErrorMessage.empty()
								? "tool arguments are invalid"
								: argsErrorMessage;
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::uint64_t timeoutMs = 120000;
						if (request.deadlineEpochMs.has_value()) {
							const std::uint64_t now = CurrentEpochMs();
							if (request.deadlineEpochMs.value() <= now) {
								result.executed = false;
								result.status = "timed_out";
								result.errorCode = "deadline_exceeded";
								result.errorMessage = "request deadline already elapsed";
								result.completedAtMs = now;
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}

							timeoutMs = request.deadlineEpochMs.value() - now;
						}

						const auto process = tools::ExecuteNodeSkillProcess(
							scriptPath,
							cliArgs.value(),
							timeoutMs);

						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;

						if (!process.started) {
							result.executed = false;
							result.status = "error";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "process_start_failed"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "failed to start tool process"
								: process.errorMessage;
							return result;
						}

						if (process.timedOut) {
							result.executed = false;
							result.status = "timed_out";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "deadline_exceeded"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "tool execution timed out"
								: process.errorMessage;
							return result;
						}

						result.result = process.output;
						if (process.exitCode == 0) {
							result.executed = true;
							result.status = "ok";
							result.errorCode.clear();
							result.errorMessage.clear();
							return result;
						}

						result.executed = false;
						result.status = "error";
						result.errorCode = "process_exit_nonzero";
						{
							std::string nonZeroMsg =
								"tool process returned non-zero exit code " +
								std::to_string(
									static_cast<unsigned long long>(process.exitCode));
							if (!process.output.empty()) {
								nonZeroMsg += " output=";
								std::string snippet = process.output;
								constexpr std::size_t kMaxSnippet = 800;
								if (snippet.size() > kMaxSnippet) {
									snippet.resize(kMaxSnippet);
									snippet += "...[truncated]";
								}
								for (char& ch : snippet) {
									if (ch == '\r' || ch == '\n' || ch == '\t') {
										ch = ' ';
									}
								}
								nonZeroMsg += snippet;
							}
							result.errorMessage = std::move(nonZeroMsg);
						}
						return result;
					});
			}
		}

		void RegisterBraveSearchRuntimeTools(
			blazeclaw::gateway::GatewayHost& host,
			const CToolRuntimeRegistry::ToolRuntimePolicySettings& toolPolicy) {
			const auto skillRoot = toolPolicy.braveSearchSkillRoot;
			const auto openClawWebBrowsingSkillRoot = toolPolicy.openClawWebBrowsingSkillRoot;
			const auto webBrowsingSkillRoot = toolPolicy.webBrowsingSkillRoot;
			const auto baiduSearchSkillRoot = toolPolicy.baiduSearchSkillRoot;
			const bool enableOpenClawWebBrowsingFallback =
				toolPolicy.enableOpenClawWebBrowsingFallback;
			const bool requireApiKey = toolPolicy.braveRequireApiKey;
			const bool hasApiKey = toolPolicy.braveApiKeyPresent;
			const std::uint64_t searchTimeoutMsDefault = ParseUInt64EnvValue(
				L"BLAZECLAW_WEB_SEARCH_TIMEOUT_MS",
				45000);
			const std::uint64_t fallbackTimeoutMsDefault = ParseUInt64EnvValue(
				L"BLAZECLAW_WEB_SEARCH_FALLBACK_TIMEOUT_MS",
				15000);
			for (const auto& spec : tools::BuildBraveSearchToolRuntimeSpecs()) {
				host.RegisterRuntimeToolV2(
					blazeclaw::gateway::ToolCatalogEntry{
						.id = spec.id,
						.label = spec.label,
						.category = "search",
						.enabled = true,
					},
					[spec,
					skillRoot,
					openClawWebBrowsingSkillRoot,
					webBrowsingSkillRoot,
					baiduSearchSkillRoot,
					enableOpenClawWebBrowsingFallback,
					requireApiKey,
					hasApiKey,
					searchTimeoutMsDefault,
					fallbackTimeoutMsDefault](const blazeclaw::gateway::ToolExecuteRequestV2& request) {
						blazeclaw::gateway::ToolExecuteResultV2 result;
						result.tool = request.tool.empty() ? spec.id : request.tool;
						result.correlationId = request.correlationId;
						result.startedAtMs = CurrentEpochMs();

						if (requireApiKey && !hasApiKey) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "brave_api_key_missing";
							result.errorMessage =
								"BRAVE_API_KEY is required by runtime policy";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						if (!skillRoot.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "skill_runtime_missing";
							result.errorMessage = "brave-search skill root not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						const auto scriptPath = skillRoot.value() / ToWide(spec.script);
						if (!std::filesystem::exists(scriptPath)) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "script_missing";
							result.errorMessage = "tool script not found";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						nlohmann::json params = nlohmann::json::object();
						if (request.argsJson.has_value() && !request.argsJson->empty()) {
							try {
								params = nlohmann::json::parse(request.argsJson.value());
							}
							catch (...) {
								result.executed = false;
								result.status = "error";
								result.errorCode = "invalid_args_json";
								result.errorMessage = "argsJson is not valid JSON";
								result.completedAtMs = CurrentEpochMs();
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}
						}

						if (params.is_string()) {
							if (tools::IsBraveSearchWebToolId(spec.id)) {
								params = nlohmann::json::object(
									{ {"query", params.get<std::string>()} });
							}
							else if (tools::IsBraveFetchContentToolId(spec.id)) {
								params = nlohmann::json::object(
									{ {"url", params.get<std::string>()} });
							}
						}

						if (!params.is_object()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = "invalid_arguments";
							result.errorMessage = "tool args must be a JSON object";
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::string argsErrorCode;
						std::string argsErrorMessage;
						const auto cliArgs = tools::BuildBraveSearchCliArgs(
							spec,
							params,
							argsErrorCode,
							argsErrorMessage);
						if (!cliArgs.has_value()) {
							result.executed = false;
							result.status = "error";
							result.errorCode = argsErrorCode.empty()
								? "invalid_arguments"
								: argsErrorCode;
							result.errorMessage = argsErrorMessage.empty()
								? "tool arguments are invalid"
								: argsErrorMessage;
							result.completedAtMs = CurrentEpochMs();
							result.latencyMs = result.completedAtMs - result.startedAtMs;
							return result;
						}

						std::uint64_t timeoutMs =
							tools::IsBraveSearchWebToolId(spec.id) ? searchTimeoutMsDefault : 30000;
						if (request.deadlineEpochMs.has_value()) {
							const std::uint64_t now = CurrentEpochMs();
							if (request.deadlineEpochMs.value() <= now) {
								result.executed = false;
								result.status = "timed_out";
								result.errorCode = "deadline_exceeded";
								result.errorMessage = "request deadline already elapsed";
								result.completedAtMs = now;
								result.latencyMs = result.completedAtMs - result.startedAtMs;
								return result;
							}

							timeoutMs = request.deadlineEpochMs.value() - now;
						}

						const auto process = tools::ExecuteNodeSkillProcess(
							scriptPath,
							cliArgs.value(),
							timeoutMs);

						result.completedAtMs = CurrentEpochMs();
						result.latencyMs = result.completedAtMs - result.startedAtMs;

						if (!process.started) {
							result.executed = false;
							result.status = "error";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "process_start_failed"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "failed to start tool process"
								: process.errorMessage;
							if (result.result.empty()) {
								result.result = result.errorMessage;
							}

							if (tools::IsBraveSearchWebToolId(spec.id) &&
								spec.id == "web_browsing.search.web" &&
								webBrowsingSkillRoot.has_value() &&
								params.contains("query") &&
								params["query"].is_string()) {
								const std::string query =
									tools::TrimAsciiForBraveSearch(params["query"].get<std::string>());
								if (!query.empty()) {
									std::uint64_t fallbackTimeoutMs = fallbackTimeoutMsDefault;
									if (request.deadlineEpochMs.has_value()) {
										const std::uint64_t now = CurrentEpochMs();
										if (request.deadlineEpochMs.value() <= now) {
											fallbackTimeoutMs = 0;
										}
										else {
											fallbackTimeoutMs = request.deadlineEpochMs.value() - now;
										}
									}

									if (fallbackTimeoutMs > 0) {
										const auto primaryPythonScriptPath =
											webBrowsingSkillRoot.value() /
											L"scripts" /
											L"search_web.py";
										if (std::filesystem::exists(primaryPythonScriptPath)) {
											const auto pythonFallbackProcess = tools::ExecutePythonSkillProcess(
												primaryPythonScriptPath,
												std::vector<std::string>{ query },
												fallbackTimeoutMs);
											if (pythonFallbackProcess.started &&
												!pythonFallbackProcess.timedOut &&
												pythonFallbackProcess.exitCode == 0) {
												result.executed = true;
												result.status = "ok";
												result.result =
													pythonFallbackProcess.output +
													"\n[fallback=web_browsing_python_primary]";
												result.errorCode.clear();
												result.errorMessage.clear();
												result.completedAtMs = CurrentEpochMs();
												result.latencyMs =
													result.completedAtMs - result.startedAtMs;
												return result;
											}
										}
									}
								}
							}

							if (spec.id == "web_browsing.search.web" &&
								baiduSearchSkillRoot.has_value() &&
								params.contains("query") &&
								params["query"].is_string()) {
								const std::string query =
									tools::TrimAsciiForBraveSearch(params["query"].get<std::string>());
								if (!query.empty()) {
									std::uint64_t fallbackTimeoutMs = fallbackTimeoutMsDefault;
									if (request.deadlineEpochMs.has_value()) {
										const std::uint64_t now = CurrentEpochMs();
										if (request.deadlineEpochMs.value() <= now) {
											fallbackTimeoutMs = 0;
										}
										else {
											fallbackTimeoutMs = request.deadlineEpochMs.value() - now;
										}
									}

									if (fallbackTimeoutMs > 0) {
										const auto fallbackScriptPath =
											baiduSearchSkillRoot.value() /
											L"scripts" /
											L"search.py";
										if (std::filesystem::exists(fallbackScriptPath)) {
											nlohmann::json fallbackParams = nlohmann::json::object();
											fallbackParams["query"] = query;
											if (params.contains("count") &&
												params["count"].is_number_integer()) {
												fallbackParams["count"] = params["count"];
											}

											std::string fallbackArgsErrorCode;
											std::string fallbackArgsErrorMessage;
											const auto fallbackCliArgs =
												tools::BuildBaiduSearchCliArgs(
													tools::BaiduSearchToolRuntimeSpec{
														.id = "baidu-search.search.web",
														.label = "Baidu Web Search",
														.script = "scripts/search.py",
													},
													fallbackParams,
													fallbackArgsErrorCode,
													fallbackArgsErrorMessage);
											if (fallbackCliArgs.has_value()) {
												const auto fallbackProcess = tools::ExecutePythonSkillProcess(
													fallbackScriptPath,
													fallbackCliArgs.value(),
													fallbackTimeoutMs);
												if (fallbackProcess.started &&
													!fallbackProcess.timedOut &&
													fallbackProcess.exitCode == 0) {
													result.executed = true;
													result.status = "ok";
													result.result =
														fallbackProcess.output +
														"\n[fallback=baidu_search_python]";
													result.errorCode.clear();
													result.errorMessage.clear();
													result.completedAtMs = CurrentEpochMs();
													result.latencyMs =
														result.completedAtMs - result.startedAtMs;
													return result;
												}
											}
										}
									}
								}
							}
							return result;
						}

						if (process.timedOut) {
							result.executed = false;
							result.status = "timed_out";
							result.result = process.output;
							result.errorCode = process.errorCode.empty()
								? "deadline_exceeded"
								: process.errorCode;
							result.errorMessage = process.errorMessage.empty()
								? "tool execution timed out"
								: process.errorMessage;
							return result;
						}

						result.result = process.output;
						if (process.exitCode == 0) {
							result.executed = true;
							result.status = "ok";
							result.errorCode.clear();
							result.errorMessage.clear();
							return result;
						}

						result.executed = false;
						const std::string classifiedFailure =
							tools::ClassifyBraveFailureCode(process.output);
						const bool canAttemptBaiduFallback =
							spec.id == "web_browsing.search.web" &&
							classifiedFailure == "network_error" &&
							tools::IsBraveNetworkTimeoutFailure(process.output) &&
							baiduSearchSkillRoot.has_value() &&
							params.contains("query") &&
							params["query"].is_string();
						if (canAttemptBaiduFallback) {
							const std::string query =
								tools::TrimAsciiForBraveSearch(params["query"].get<std::string>());
							if (!query.empty()) {
								std::uint64_t fallbackTimeoutMs = fallbackTimeoutMsDefault;
								if (request.deadlineEpochMs.has_value()) {
									const std::uint64_t now = CurrentEpochMs();
									if (request.deadlineEpochMs.value() <= now) {
										fallbackTimeoutMs = 0;
									}
									else {
										fallbackTimeoutMs = request.deadlineEpochMs.value() - now;
									}
								}

								if (fallbackTimeoutMs > 0) {
									const auto fallbackScriptPath =
										baiduSearchSkillRoot.value() /
										L"scripts" /
										L"search.py";
									if (std::filesystem::exists(fallbackScriptPath)) {
										nlohmann::json fallbackParams = nlohmann::json::object();
										fallbackParams["query"] = query;
										if (params.contains("count") &&
											params["count"].is_number_integer()) {
											fallbackParams["count"] = params["count"];
										}

										std::string fallbackArgsErrorCode;
										std::string fallbackArgsErrorMessage;
										const auto fallbackCliArgs =
											tools::BuildBaiduSearchCliArgs(
												tools::BaiduSearchToolRuntimeSpec{
													.id = "baidu-search.search.web",
													.label = "Baidu Web Search",
													.script = "scripts/search.py",
												},
												fallbackParams,
												fallbackArgsErrorCode,
												fallbackArgsErrorMessage);
										if (fallbackCliArgs.has_value()) {
											const auto fallbackProcess = tools::ExecutePythonSkillProcess(
												fallbackScriptPath,
												fallbackCliArgs.value(),
												fallbackTimeoutMs);
											if (fallbackProcess.started &&
												!fallbackProcess.timedOut &&
												fallbackProcess.exitCode == 0) {
												result.executed = true;
												result.status = "ok";
												result.result =
													fallbackProcess.output +
													"\n[fallback=baidu_search_python_network_error]";
												result.errorCode.clear();
												result.errorMessage.clear();
												result.completedAtMs = CurrentEpochMs();
												result.latencyMs =
													result.completedAtMs - result.startedAtMs;
												return result;
											}
										}
									}
								}
							}
						}
						const bool canAttemptOpenClawFallback =
							enableOpenClawWebBrowsingFallback &&
							spec.id == "web_browsing.search.web" &&
							classifiedFailure == "network_error" &&
							tools::IsBraveNetworkTimeoutFailure(process.output) &&
							openClawWebBrowsingSkillRoot.has_value() &&
							params.contains("query") &&
							params["query"].is_string();

						if (canAttemptOpenClawFallback) {
							const std::string query =
								tools::TrimAsciiForBraveSearch(params["query"].get<std::string>());
							if (!query.empty()) {
								std::uint64_t fallbackTimeoutMs = fallbackTimeoutMsDefault;
								if (request.deadlineEpochMs.has_value()) {
									const std::uint64_t now = CurrentEpochMs();
									if (request.deadlineEpochMs.value() <= now) {
										fallbackTimeoutMs = 0;
									}
									else {
										fallbackTimeoutMs = request.deadlineEpochMs.value() - now;
									}
								}

								if (fallbackTimeoutMs > 0) {
									const auto fallbackScriptPath =
										openClawWebBrowsingSkillRoot.value() /
										L"scripts" /
										L"search_web.py";
									if (std::filesystem::exists(fallbackScriptPath)) {
										const auto fallbackProcess = tools::ExecutePythonSkillProcess(
											fallbackScriptPath,
											std::vector<std::string>{ query },
											fallbackTimeoutMs);
										if (fallbackProcess.started &&
											!fallbackProcess.timedOut &&
											fallbackProcess.exitCode == 0) {
											result.executed = true;
											result.status = "ok";
											result.result =
												fallbackProcess.output +
												"\n[fallback=openclaw_python]";
											result.errorCode.clear();
											result.errorMessage.clear();
											result.completedAtMs = CurrentEpochMs();
											result.latencyMs =
												result.completedAtMs - result.startedAtMs;
											return result;
										}
									}
								}
							}
						}

						result.status = classifiedFailure;
						result.errorCode = classifiedFailure;
						if (!result.result.empty()) {
							result.errorMessage = result.result;
						}
						else {
							result.errorMessage =
								"tool process returned non-zero exit code " +
								std::to_string(static_cast<unsigned long long>(process.exitCode));
						}
						return result;
					});
			}
		}

		std::string WideToNarrowAscii(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const auto ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}

		bool IsOneOfChannels(
			const std::vector<std::wstring>& enabledChannels,
			const std::wstring& candidate) {
			for (const auto& channel : enabledChannels) {
				if (_wcsicmp(channel.c_str(), candidate.c_str()) == 0) {
					return true;
				}
			}

			return false;
		}

		std::string BuildOpenClawOriginalTelemetryPayload(
			const std::wstring& skillName,
			const std::string& activationState,
			const std::size_t diagnosticsCount) {
			return std::string("{\"skill\":") +
				blazeclaw::gateway::JsonString(WideToNarrowAscii(skillName)) +
				",\"state\":" +
				blazeclaw::gateway::JsonString(activationState) +
				",\"diagnostics\":" +
				std::to_string(diagnosticsCount) +
				"}";
		}

		std::wstring ToWideLocal(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			std::wstring wide;
			wide.reserve(value.size());
			for (const unsigned char ch : value) {
				wide.push_back(static_cast<wchar_t>(std::towlower(ch)));
			}
			return wide;
		}

	} // namespace

	ServiceManager::ServiceManager()
		: m_operatorDiagnosticsAssembler(
			m_gatewayLifecycleDiagnosticsProjector,
			m_emailRuntimeDiagnosticsProjector,
			m_embeddedRuntimeDiagnosticsProjector,
			m_modelRuntimeDiagnosticsProjector,
			m_hooksDiagnosticsProjector,
			m_diagnosticsReportBuilder) {
		m_localModelRuntime =
			std::make_unique<localmodel::OnnxTextGenerationRuntime>();

		m_skillsHostCallbacks.persistSkillConfigEnv =
			[](const std::string& skill,
				const std::string& envContent,
				std::string& outError,
				std::filesystem::path& outPath) {
					return PersistSkillConfigEnvViaHostDocument(
						skill,
						envContent,
						outError,
						outPath);
			};

		m_skillsHostCallbacks.refreshSkillView = []() {
			RefreshSkillViewViaHostWindow();
			};
	}

	ServiceManager::~ServiceManager()
	{
		Stop();
	}

	void ServiceManager::SetSkillsHostCallbacks(
		SkillsHostCallbacks callbacks) {
		m_skillsHostCallbacks = std::move(callbacks);
	}

	blazeclaw::gateway::SkillsCatalogGatewayState ServiceManager::BuildGatewaySkillsState() const {
		return m_skillsHooksCoordinator.BuildGatewaySkillsState(
			CSkillsHooksCoordinator::GatewayStateContext{
				.catalog = m_skillsCatalog,
				.eligibility = m_skillsEligibility,
				.prompt = m_skillsPrompt,
				.commands = m_skillsCommands,
				.watch = m_skillsWatch,
				.sync = m_skillsSync,
				.envOverrides = m_skillsEnvOverrides,
				.install = m_skillsInstall,
				.securityScan = m_skillSecurityScan,
				.hookExecution = m_hookExecution,
				.skillsConfig = m_activeConfig.skills,
				.effectiveSkillRoots = &m_effectiveSkillRoots,
				.hooksGovernanceReportingEnabled = m_state.hooks.governanceReportingEnabled,
				.hooksLastGovernanceReportPath = m_state.hooks.lastGovernanceReportPath,
				.hooksGovernanceReportsGenerated = m_state.hooks.governanceReportsGenerated,
				.hooksAutoRemediationEnabled = m_state.hooks.autoRemediationEnabled,
				.hooksAutoRemediationRequiresApproval = m_state.hooks.autoRemediationRequiresApproval,
				.hooksAutoRemediationExecuted = m_state.hooks.autoRemediationExecuted,
				.hooksLastAutoRemediationStatus = m_state.hooks.lastAutoRemediationStatus,
				.hooksAutoRemediationTenantId = m_state.hooks.autoRemediationTenantId,
				.hooksLastAutoRemediationPlaybookPath = m_state.hooks.lastAutoRemediationPlaybookPath,
				.hooksAutoRemediationTokenMaxAgeMinutes = m_state.hooks.autoRemediationTokenMaxAgeMinutes,
				.hooksAutoRemediationTokenRotations = m_state.hooks.autoRemediationTokenRotations,
				.hooksLastRemediationTelemetryPath = m_state.hooks.lastRemediationTelemetryPath,
				.hooksLastRemediationAuditPath = m_state.hooks.lastRemediationAuditPath,
				.hooksRemediationSloStatus = m_state.hooks.remediationSloStatus,
				.hooksRemediationSloMaxDriftDetected = m_state.hooks.remediationSloMaxDriftDetected,
				.hooksRemediationSloMaxPolicyBlocked = m_state.hooks.remediationSloMaxPolicyBlocked,
				.hooksLastComplianceAttestationPath = m_state.hooks.lastComplianceAttestationPath,
				.hooksEnterpriseSlaPolicyId = m_state.hooks.enterpriseSlaPolicyId,
				.hooksCrossTenantAttestationAggregationEnabled =
					m_state.hooks.crossTenantAttestationAggregationEnabled,
				.hooksCrossTenantAttestationAggregationStatus =
					m_state.hooks.crossTenantAttestationAggregationStatus,
				.hooksCrossTenantAttestationAggregationCount =
					m_state.hooks.crossTenantAttestationAggregationCount,
				.hooksLastCrossTenantAttestationAggregationPath =
					m_state.hooks.lastCrossTenantAttestationAggregationPath,
			},
			[this](
				const SkillsCatalogEntry& entry,
				const SkillsEligibilityEntry* eligibility,
				const SkillsCommandSpec* command,
				const SkillsInstallPlanEntry* install)
			{
				return BuildGatewaySkillEntry(entry, eligibility, command, install);
			},
			[](const std::wstring& value)
			{
				std::string output;
				output.reserve(value.size());
				for (const wchar_t ch : value)
				{
					output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
				}
				return output;
			});
	}

	void ServiceManager::RefreshGatewaySkillsStateProjection()
	{
		SkillsGatewayPublicationCoordinator::RefreshProjection(*this);
	}

	void ServiceManager::PublishGatewaySkillsStateProjection()
	{
		SkillsGatewayPublicationCoordinator::PublishProjection(*this);
	}

	blazeclaw::gateway::SkillsCatalogGatewayEntry
		ServiceManager::BuildGatewaySkillEntry(
			const SkillsCatalogEntry& entry,
			const SkillsEligibilityEntry* eligibility,
			const SkillsCommandSpec* command,
			const SkillsInstallPlanEntry* install) const {
		return m_skillsGatewayProjectionService.BuildGatewaySkillEntry(
			entry,
			eligibility,
			command,
			install);
	}

	void ServiceManager::RefreshSkillsState(
		const blazeclaw::config::AppConfig& config,
		const bool forceRefresh,
		const std::wstring& reason) {
		const auto commandSourceAdapters = BuildRuntimeSkillCommandSourceAdapters();
		const auto commandDescriptors =
			SkillsAgentCommandDescriptorPolicy::BuildDescriptors(config, m_agentsScope);

		const auto resolvedWorkspaceRoot = ResolveWorkspaceRootForSkills(
			std::filesystem::current_path());
		m_effectiveSkillRoots = BuildCanonicalSkillRootSnapshot(
			resolvedWorkspaceRoot,
			config);
		m_gatewayHost.SetPreferredSkillRootDirectories(m_effectiveSkillRoots);

		m_skillsHooksCoordinator.RefreshSkillsState(
			config,
			forceRefresh,
			reason,
			CSkillsHooksCoordinator::RefreshContext{
			   .refreshDependencies = SkillsRefreshDependencies{
					.catalogService = m_skillsCatalogService,
					.eligibilityService = m_skillsEligibilityService,
					.promptService = m_skillsPromptService,
					.commandService = m_skillsCommandService,
				 .commandSourceAdapters = &commandSourceAdapters,
					.syncService = m_skillsSyncService,
					.envOverrideService = m_skillsEnvOverrideService,
					.installService = m_skillsInstallService,
					.securityScanService = m_skillSecurityScanService,
					.watchService = m_skillsWatchService,
				},
				.hookCatalogService = m_hookCatalogService,
				.hookExecutionService = m_hookExecutionService,
				.hookEventService = m_hookEventService,
			   .skillsFacade = m_skillsFacade,
				.catalog = m_skillsCatalog,
				.eligibility = m_skillsEligibility,
				.hookCatalog = m_hookCatalog,
				.hookExecution = m_hookExecution,
				.prompt = m_skillsPrompt,
				.events = m_hookEvents,
				.commands = m_skillsCommands,
				.sync = m_skillsSync,
				.envOverrides = m_skillsEnvOverrides,
				.install = m_skillsInstall,
				.runSnapshot = m_skillsRunSnapshot,
				.securityScan = m_skillSecurityScan,
				.watch = m_skillsWatch,
			 .workspaceRoot = resolvedWorkspaceRoot,
				.hooksFallbackPromptInjection = m_state.hooks.fallbackPromptInjection,
			});

		const auto aggregatedCommands =
			m_skillCommandsAggregationService.BuildSnapshot(
				AgentSkillCommandAggregationContext{
					.descriptors = commandDescriptors,
					.appConfig = config,
					.catalogService = m_skillsCatalogService,
					.eligibilityService = m_skillsEligibilityService,
					.commandService = m_skillsCommandService,
					.commandSourceAdapters = &commandSourceAdapters,
					.reservedNames =
						SkillsAgentCommandDescriptorPolicy::BuildReservedChatSlashCommandNamesNormalized(),
				});
		if (!aggregatedCommands.commandSnapshot.commands.empty()) {
			m_skillsCommands = aggregatedCommands.commandSnapshot;
		}

		m_configSchemaService.Invalidate();
		SkillsGatewayPublicationCoordinator::RefreshProjection(*this);
		RefreshOpenClawOriginalRuntimeTools(config);
		EmitOpenClawOriginalTelemetry();
	}

	void ServiceManager::RefreshOpenClawOriginalRuntimeTools(
		const blazeclaw::config::AppConfig& config) {
		if (!config.skills.openclawOriginal.enabled ||
			!config.skills.openclawOriginal.autoImportTools) {
			return;
		}

		bool hasToolEnabledCandidate = false;
		for (const auto& entry : m_skillsCatalog.entries) {
			if (entry.sourceKind != SkillsSourceKind::OpenClawOriginal ||
				!entry.openClawOriginalActivationState.has_value()) {
				continue;
			}

			if (entry.openClawOriginalActivationState.value() ==
				SkillsOpenClawOriginalActivationState::ToolEnabled) {
				hasToolEnabledCandidate = true;
				break;
			}
		}

		if (!hasToolEnabledCandidate) {
			return;
		}

		std::vector<std::string> openClawToolRoots = m_effectiveSkillRoots;
		if (openClawToolRoots.empty()) {
			const auto workspaceRoot = ResolveWorkspaceRootForSkills(
				std::filesystem::current_path());
			openClawToolRoots = BuildCanonicalSkillRootSnapshot(workspaceRoot, config);
		}

		m_gatewayHost.SetPreferredSkillRootDirectories(openClawToolRoots);
		m_gatewayHost.ReloadSkillToolsFromDirectories(openClawToolRoots, true);
	}

	void ServiceManager::EmitOpenClawOriginalTelemetry() const {
		for (const auto& entry : m_skillsCatalog.entries) {
			if (entry.sourceKind != SkillsSourceKind::OpenClawOriginal ||
				!entry.openClawOriginalActivationState.has_value()) {
				continue;
			}

			const auto stateLabel = SkillsCatalogService::OpenClawOriginalActivationStateLabel(
				entry.openClawOriginalActivationState.value());
			const std::string stateNarrow = WideToNarrowAscii(stateLabel);
			const std::string payload = BuildOpenClawOriginalTelemetryPayload(
				entry.skillName,
				stateNarrow,
				entry.openClawOriginalImportDiagnostics.size());

			if (stateNarrow == "detected") {
				blazeclaw::gateway::EmitTelemetryEvent(
					"skills.openclaw_original.detected",
					payload);
			}
			else if (stateNarrow == "imported") {
				blazeclaw::gateway::EmitTelemetryEvent(
					"skills.openclaw_original.imported",
					payload);
			}
			else if (stateNarrow == "tool_enabled") {
				blazeclaw::gateway::EmitTelemetryEvent(
					"skills.openclaw_original.tool_enabled",
					payload);
			}
			else {
				blazeclaw::gateway::EmitTelemetryEvent(
					"skills.openclaw_original.failed",
					payload);
			}
		}
	}

	std::vector<extensions::IRuntimeSkillCommandSourceAdapter*>
		ServiceManager::BuildRuntimeSkillCommandSourceAdapters() {
		if (!m_extensionBundleCommandSourceAdapter) {
			m_extensionBundleCommandSourceAdapter =
				std::make_unique<ExtensionBundleCommandSourceAdapter>(
					std::filesystem::path(L"blazeclaw") / L"extensions");
		}

		return std::vector<extensions::IRuntimeSkillCommandSourceAdapter*>{
			m_extensionBundleCommandSourceAdapter.get(),
		};
	}

	bool ServiceManager::Start(const blazeclaw::config::AppConfig& config) {
		if (m_running) {
			return true;
		}
		AppendStartupTrace("ServiceManager.Start.begin");

		ConfigurePolicies(config);
		InitializeModules();
		WireGatewayCallbacks();
		return FinalizeStartup(config);
	}

	void ServiceManager::ConfigurePolicies(
		const blazeclaw::config::AppConfig& config)
	{
		ServiceLifecycleStartupCoordinator::ApplyConfigurePolicies(*this, config);
	}

	void ServiceManager::InitializeModules()
	{
		ServiceLifecycleStartupCoordinator::RunInitializeModules(*this);
	}


	void ServiceManager::WireGatewayCallbacks()
	{
		GatewayHostBindingCoordinator::WireAllGatewayServiceCallbacks(*this);
	}

	void ServiceManager::BindToolRuntimeCallbacks()
	{
		const auto toolPolicy =
			m_serviceBootstrapCoordinator.ResolveToolRuntimePolicySettings();
		m_toolRuntimeRegistry.RegisterAll(
			m_gatewayHost,
			CToolRuntimeRegistry::ToolRuntimePolicySettings{
				.imapSmtpSkillRoot = toolPolicy.imapSmtpSkillRoot,
				.baiduSearchSkillRoot = toolPolicy.baiduSearchSkillRoot,
				.braveSearchSkillRoot = toolPolicy.braveSearchSkillRoot,
				.imageGeneratorSkillRoot = toolPolicy.imageGeneratorSkillRoot,
				.openClawWebBrowsingSkillRoot =
					toolPolicy.openClawWebBrowsingSkillRoot,
				.webBrowsingSkillRoot = toolPolicy.webBrowsingSkillRoot,
				.braveRequireApiKey = toolPolicy.braveRequireApiKey,
				.braveApiKeyPresent = toolPolicy.braveApiKeyPresent,
				.enableOpenClawWebBrowsingFallback =
					toolPolicy.enableOpenClawWebBrowsingFallback,
			},
			CToolRuntimeRegistry::Dependencies{
				.registerImapSmtp = [](
					blazeclaw::gateway::GatewayHost& host,
					const CToolRuntimeRegistry::ToolRuntimePolicySettings& injectedPolicy) {
					RegisterImapSmtpRuntimeTools(host, injectedPolicy);
				},
				.registerContentPolishing = [](blazeclaw::gateway::GatewayHost& host) {
					RegisterContentPolishingRuntimeTools(host);
				},
				.registerBraveSearch = [](
					blazeclaw::gateway::GatewayHost& host,
					const CToolRuntimeRegistry::ToolRuntimePolicySettings& injectedPolicy) {
					RegisterBraveSearchRuntimeTools(host, injectedPolicy);
				},
				.registerBaiduSearch = [](
					blazeclaw::gateway::GatewayHost& host,
					const CToolRuntimeRegistry::ToolRuntimePolicySettings& injectedPolicy) {
					RegisterBaiduSearchRuntimeTools(host, injectedPolicy);
				},
				.registerImageGenerator = [](
					blazeclaw::gateway::GatewayHost& host,
					const CToolRuntimeRegistry::ToolRuntimePolicySettings& injectedPolicy) {
					RegisterImageGeneratorRuntimeTools(host, injectedPolicy);
				},
			});
	}

	std::vector<EmbeddedToolBinding> ServiceManager::BuildEmbeddedToolBindings() const
	{
		return m_skillsCommandService.BuildEmbeddedToolBindings(m_skillsCommands);
	}

	std::optional<std::string> ServiceManager::ResolveSkillInvocationToolTarget(
		const std::string& commandBodyNormalized) const
	{
		const std::string canonicalCommandBody =
			CanonicalizeForRouting(commandBodyNormalized);
		const auto resolvedSkillInvocation =
			m_skillCommandInvocationService.ResolveInvocation(
				ToWide(canonicalCommandBody),
				m_skillsCommands.commands);
		if (!resolvedSkillInvocation.has_value() ||
			!resolvedSkillInvocation->command.dispatch.enabled ||
			_wcsicmp(
				resolvedSkillInvocation->command.dispatch.kind.c_str(),
				L"tool") != 0 ||
			resolvedSkillInvocation->command.dispatch.toolName.empty()) {
			if (const auto generatedTarget =
				ResolveGeneratedOpenClawToolTargetFromTriggerHints(
					m_skillsCatalog.entries,
					canonicalCommandBody);
				generatedTarget.has_value()) {
				return generatedTarget.value();
			}

			if (TryExtractImageGeneratorPrompt(canonicalCommandBody).has_value()) {
				return std::string("image-generator.generate");
			}

			if (LooksLikeInboxIntentAnyLanguage(canonicalCommandBody) ||
				LooksLikeInboxReplyUrgencyIntent(canonicalCommandBody)) {
				return std::string("imap_smtp_email.imap.search");
			}
			return std::nullopt;
		}

		return WideToNarrowAscii(
			resolvedSkillInvocation->command.dispatch.toolName);
	}

	std::optional<std::string> ServiceManager::ResolveSkillInvocationPromptRewrite(
		const std::string& commandBodyNormalized) const
	{
		const std::string canonicalCommandBody =
			CanonicalizeForRouting(commandBodyNormalized);
		const auto slashRewrite =
			m_skillCommandInvocationService.RewriteInvocationPromptUtf8(
				canonicalCommandBody,
				m_skillsCommands.commands);
		if (slashRewrite.has_value()) {
			return slashRewrite;
		}

		const auto imagePrompt =
			TryExtractImageGeneratorPrompt(canonicalCommandBody);
		if (!imagePrompt.has_value()) {
			return std::nullopt;
		}

		if (imagePrompt->empty()) {
			return std::string("/skill image-generator");
		}

		return std::string("/skill image-generator ") + imagePrompt.value();
	}

	std::optional<std::string> ServiceManager::ResolveSkillInvocationMissReason(
		const std::string& commandBodyNormalized,
		const std::optional<std::string>& resolvedToolTarget) const
	{
		const std::string canonicalCommandBody =
			CanonicalizeForRouting(commandBodyNormalized);

		if (resolvedToolTarget.has_value()) {
			const auto runtimeTools = m_gatewayHost.ListRuntimeTools();
			const auto toolIt = std::find_if(
				runtimeTools.begin(),
				runtimeTools.end(),
				[&resolvedToolTarget](const blazeclaw::gateway::ToolCatalogEntry& tool) {
					return tool.enabled &&
						blazeclaw::gateway::json::Trim(tool.id) ==
						blazeclaw::gateway::json::Trim(resolvedToolTarget.value());
				});
			if (toolIt == runtimeTools.end()) {
				return std::string("runtime_tool_not_registered");
			}

			return std::nullopt;
		}

		const auto resolvedSkillInvocation =
			m_skillCommandInvocationService.ResolveInvocation(
				ToWide(canonicalCommandBody),
				m_skillsCommands.commands);
		if (resolvedSkillInvocation.has_value() &&
			(!resolvedSkillInvocation->command.dispatch.enabled ||
				_wcsicmp(
					resolvedSkillInvocation->command.dispatch.kind.c_str(),
					L"tool") != 0 ||
				resolvedSkillInvocation->command.dispatch.toolName.empty())) {
			return std::string("skill_missing_command_dispatch");
		}

		if (canonicalCommandBody.empty() || canonicalCommandBody.front() != '/') {
			return std::string("invocation_not_slash_command");
		}

		return std::nullopt;
	}

	std::optional<std::string>
		ServiceManager::BuildGeneratedOpenClawRoutingDecisionTelemetry(
			const std::string& commandBodyNormalized) const
	{
		const std::string canonicalCommandBody =
			CanonicalizeForRouting(commandBodyNormalized);

		GeneratedOpenClawRoutingDecisionDiagnostics diagnostics;
		const std::optional<std::string> resolvedGeneratedToolTarget =
			ResolveGeneratedOpenClawToolTargetFromTriggerHints(
				m_skillsCatalog.entries,
				canonicalCommandBody,
				&diagnostics);

		if (diagnostics.normalizedPrompt.empty() &&
			diagnostics.candidateCountConsidered == 0 &&
			!resolvedGeneratedToolTarget.has_value()) {
			return std::nullopt;
		}

		const std::string normalizedPromptUtf8 =
			WideToUtf8Local(diagnostics.normalizedPrompt);
		const std::string normalizedPromptPreview =
			BuildNormalizedPromptPreview(diagnostics.normalizedPrompt, 96);
		const std::string normalizedPromptHash =
			BuildHexLower(Fnv1a64(normalizedPromptUtf8));

		return std::string("{") +
			"\"normalizedPromptPreview\":" +
			blazeclaw::gateway::JsonString(normalizedPromptPreview) +
			",\"normalizedPromptHash\":" +
			blazeclaw::gateway::JsonString(normalizedPromptHash) +
			",\"candidateCountConsidered\":" +
			std::to_string(diagnostics.candidateCountConsidered) +
			",\"matchMode\":" +
			blazeclaw::gateway::JsonString(
				GeneratedTriggerMatchModeToTelemetry(diagnostics.matchMode)) +
			",\"matchedSkillKey\":" +
			blazeclaw::gateway::JsonString(
				WideToUtf8Local(diagnostics.matchedSkillKey)) +
			",\"matchedTriggerHint\":" +
			blazeclaw::gateway::JsonString(
				WideToUtf8Local(diagnostics.matchedTriggerHint)) +
			",\"resolvedGeneratedToolTarget\":" +
			(resolvedGeneratedToolTarget.has_value()
				? blazeclaw::gateway::JsonString(
					resolvedGeneratedToolTarget.value())
				: std::string("null")) +
			"}";
	}

	bool ServiceManager::ShouldLoadSkillCommandsForInlineActions(
		const bool allowTextCommands,
		const std::string& commandBodyNormalized) const
	{
		const auto slashCommandName =
			m_inlineActionsOrchestrationService.ResolveSlashCommandName(
				commandBodyNormalized);
		std::unordered_set<std::string> reserved;
		for (const auto& name :
			blazeclaw::gateway::GatewayHost::ListReservedChatSlashCommandNames()) {
			reserved.insert(ToLowerAscii(name));
		}

		const auto builtin =
			m_inlineActionsOrchestrationService.BuildBuiltinSlashCommands(reserved);
		return m_inlineActionsOrchestrationService.ShouldLoadSkillCommandsForSlash(
			allowTextCommands,
			slashCommandName,
			builtin);
	}

	std::vector<std::string> ServiceManager::BuildOrderedAllowedToolTargets(
		const std::vector<std::string>& requestedTargets,
		const std::optional<std::string>& resolvedTarget) const
	{
		std::vector<std::string> merged;
		merged.reserve(requestedTargets.size() + 1);
		if (resolvedTarget.has_value()) {
			merged.push_back(resolvedTarget.value());
		}

		for (const auto& target : requestedTargets) {
			if (std::find(merged.begin(), merged.end(), target) !=
				merged.end()) {
				continue;
			}

			merged.push_back(target);
		}

		return merged;
	}

	std::optional<std::string> ServiceManager::ExtractInlineToolResultText(
		const blazeclaw::gateway::ToolExecuteResultV2& result) const
	{
		const std::string trimmedResult =
			WideToNarrowAscii(Trim(Utf8ToWideLocal(result.result)));
		if (!trimmedResult.empty()) {
			return trimmedResult;
		}

		const std::string trimmedError =
			WideToNarrowAscii(Trim(Utf8ToWideLocal(result.errorMessage)));
		if (!trimmedError.empty()) {
			return trimmedError;
		}

		return std::nullopt;
	}

	std::optional<blazeclaw::gateway::GatewayHost::ChatRuntimeResult>
		ServiceManager::TryExecuteInlineToolInvocation(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
			const std::string& activeModel,
			const std::optional<std::string>& resolvedSkillInvocationToolTarget)
	{
		if (!request.allowInlineToolImmediateExecution ||
			!resolvedSkillInvocationToolTarget.has_value()) {
			return std::nullopt;
		}

		if (!request.inlineInvocationAuthorizedSender) {
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.assistantDeltas = {},
				.taskDeltas = {},
				.modelId = activeModel,
				.errorCode = "inline_invocation_unauthorized_sender",
				.errorMessage = "inline invocation rejected: unauthorized sender",
			};
		}

		if (!request.inlineInvocationSenderIsOwner) {
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.assistantDeltas = {},
				.taskDeltas = {},
				.modelId = activeModel,
				.errorCode = "inline_invocation_owner_required",
				.errorMessage = "inline invocation rejected: owner-only tool policy",
			};
		}

		if (!request.enforceOrderedAllowlist &&
			!request.orderedAllowedToolTargets.empty()) {
			const bool explicitAllowed =
				std::find(
					request.orderedAllowedToolTargets.begin(),
					request.orderedAllowedToolTargets.end(),
					resolvedSkillInvocationToolTarget.value()) !=
				request.orderedAllowedToolTargets.end();
			if (!explicitAllowed) {
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.assistantDeltas = {},
					.taskDeltas = {},
					.modelId = activeModel,
					.errorCode = "inline_invocation_tool_not_allowlisted",
					.errorMessage = "inline invocation rejected: tool not in allowlist",
				};
			}
		}

		std::string inlineArgs = request.message;
		if (const auto normalizedInlineArgs = BuildInlineArgsForResolvedTool(
			resolvedSkillInvocationToolTarget.value(),
			request.message);
			normalizedInlineArgs.has_value()) {
			inlineArgs = normalizedInlineArgs.value();
		}
		const blazeclaw::gateway::ToolExecuteResultV2 toolResult =
			[&]() {
				const blazeclaw::gateway::ToolExecuteRequestV2 executeRequest{
					.tool = resolvedSkillInvocationToolTarget.value(),
					.argsJson = std::optional<std::string>(inlineArgs),
					.correlationId = request.runId,
					.deadlineEpochMs = std::nullopt,
				};

				if (const auto generatedResult =
					TryExecuteGeneratedOpenClawConstantOutputTool(
						m_skillsCatalog.entries,
						executeRequest);
					generatedResult.has_value()) {
					return generatedResult.value();
				}

				return m_gatewayHost.ExecuteRuntimeToolV2(executeRequest);
			}();

		if (!toolResult.executed) {
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.assistantDeltas = {},
				.taskDeltas = {},
				.modelId = activeModel,
				.errorCode = toolResult.errorCode.empty()
					? "inline_invocation_tool_execute_failed"
					: toolResult.errorCode,
				.errorMessage = toolResult.errorMessage.empty()
					? "inline invocation tool execution failed"
					: toolResult.errorMessage,
			};
		}

		const auto inlineText = BuildInlineFriendlyTextForResolvedTool(
			resolvedSkillInvocationToolTarget.value(),
			toolResult).value_or(
				ExtractInlineToolResultText(toolResult).value_or("Done."));
		return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
			.ok = true,
			.assistantText = inlineText,
			.assistantDeltas = {},
			.taskDeltas = {},
			.modelId = activeModel,
			.errorCode = {},
			.errorMessage = {},
		};
	}

	std::vector<blazeclaw::gateway::GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
		ServiceManager::ConvertEmbeddedTaskDeltas(
			const std::vector<EmbeddedTaskDelta>& taskDeltas) const
	{
		std::vector<blazeclaw::gateway::GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
			runtimeDeltas;
		runtimeDeltas.reserve(taskDeltas.size());
		for (const auto& delta : taskDeltas) {
			runtimeDeltas.push_back(
				blazeclaw::gateway::GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = delta.index,
					.runId = delta.runId,
					.sessionId = delta.sessionId,
					.phase = delta.phase,
					.toolName = delta.toolName,
					.fallbackBackend = delta.fallbackBackend,
					.fallbackAction = delta.fallbackAction,
					.fallbackAttempt = delta.fallbackAttempt,
					.fallbackMaxAttempts = delta.fallbackMaxAttempts,
					.argsJson = delta.argsJson,
					.resultJson = delta.resultJson,
					.status = delta.status,
					.errorCode = delta.errorCode,
					.errorMessage = delta.errorMessage,
					.startedAtMs = delta.startedAtMs,
					.completedAtMs = delta.completedAtMs,
					.latencyMs = delta.latencyMs,
					.modelTurnId = delta.modelTurnId,
					.stepLabel = delta.stepLabel,
				});
		}

		return runtimeDeltas;
	}

	void ServiceManager::ApplyEmbeddedExecutionTelemetry(
		const EmbeddedRuntimeExecutionResult& embeddedExecution)
	{
		m_state.embeddedRuntime.taskDeltaTransitionCount +=
			static_cast<std::uint64_t>(embeddedExecution.taskDeltas.size());
		if (embeddedExecution.success) {
			++m_state.embeddedRuntime.runSuccessCount;
		}
		else {
			++m_state.embeddedRuntime.runFailureCount;
		}

		const std::string normalizedEmbeddedError =
			ToLowerAscii(embeddedExecution.errorCode);
		if (normalizedEmbeddedError == "embedded_deadline_exceeded") {
			++m_state.embeddedRuntime.runTimeoutCount;
		}

		if (normalizedEmbeddedError == "embedded_run_cancelled") {
			++m_state.embeddedRuntime.runCancelledCount;
		}
	}

	ChatProviderRuntimeBindings ServiceManager::BuildChatProviderRuntimeBindings() {
		ChatProviderRuntimeBindings b{};
		b.config = &m_activeConfig;
		b.embeddingsService = &m_embeddingsService;
		b.retrievalMemoryService = &m_retrievalMemoryService;
		b.retrievalMemorySnapshot = &m_retrievalMemory;
		b.modelRouting = &m_agentsModelRoutingService;
		b.piEmbedded = &m_piEmbeddedService;
		b.localModelRuntime = m_localModelRuntime.get();
		b.localModelRuntimeSnapshot = &m_localModelRuntimeSnapshot;
		b.localModelActivationEnabled = m_localModelActivationEnabled;
		b.localModelRolloutEligible = m_localModelRolloutEligible;
		b.localModelActivationReason = &m_localModelActivationReason;
		b.clearDeepSeekRunCancelled = [this](const std::string& id) {
			ClearDeepSeekRunCancelled(id);
			};
		b.hasDeepSeekCredential = [this]() { return HasDeepSeekCredential(); };
		b.resolveDeepSeekCredentialUtf8 = [this]() {
			return ResolveDeepSeekCredentialUtf8();
			};
		b.invokeDeepSeekRemoteChat =
			[this](const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& req,
				const std::string& modelId,
				const std::string& apiKey) {
					return m_deepSeekClient.InvokeGatewayChat(
						req,
						modelId,
						apiKey,
						[this](const std::string& runId) {
							return IsDeepSeekRunCancelled(runId);
						});
			};
		b.getAgentModelUtf8 = [this]() {
			return m_activeConfig.agent.model.empty()
				? std::string()
				: ToNarrow(m_activeConfig.agent.model);
			};
		b.currentEpochMs = [this]() { return CurrentEpochMs(); };
		return b;
	}

	blazeclaw::gateway::GatewayHost::ChatRuntimeResult
		ServiceManager::ExecuteProviderChatRuntimePath(
			const blazeclaw::gateway::GatewayHost::ChatRuntimeRequest& request,
			const std::string& sessionId,
			const std::string& runtimeMessage,
			const std::string& activeProvider,
			const std::string& activeModel)
	{
		return m_chatProviderRuntimeService.ExecuteProviderPath(
			BuildChatProviderRuntimeBindings(),
			request,
			sessionId,
			runtimeMessage,
			activeProvider,
			activeModel);
	}

	bool ServiceManager::FinalizeStartup(
		const blazeclaw::config::AppConfig& config)
	{
		AppendStartupTrace("ServiceManager.Start.gateway.beforeStart");
		m_state.gatewayLifecycle.transitions.clear();
		m_state.gatewayLifecycle.startupMigrationsApplied.clear();
		RecordGatewayStartupConfigSnapshot();
		RefreshGatewayAuthBootstrapDiagnostics(config);
		RecordGatewayLifecycleTransition("startup.begin");

		GatewayRuntimeBootstrapCoordinator::StartupResult startupResult;
		try {
			startupResult = m_gatewayRuntimeBootstrapCoordinator.ExecuteStartup(
				GatewayRuntimeBootstrapCoordinator::StartupContext{
					.config = config,
					.gatewayHost = m_gatewayHost,
					.appendTrace = [this](const char* stage) {
						AppendStartupTrace(stage);
					},
					.queueManagedConfigInternalWriteHash =
						[this](const std::uint64_t hash) {
						QueueManagedConfigInternalWriteHash(hash);
					},
					.suppressStartupMigrations = SuppressStartupMigrationsFromEnv(),
					.appliedStartupMigrationsOut =
						&m_state.gatewayLifecycle.startupMigrationsApplied,
				});
		}
		catch (...) {
			startupResult.success = false;
			startupResult.failedStage = "unexpected_exception";
			startupResult.warnings.push_back(
				L"gateway startup bootstrap threw an exception.");
		}

		m_state.gatewayLifecycle.resolvedRuntimeConfig = startupResult.resolvedRuntime;
		m_state.gatewayLifecycle.startupMode = startupResult.selectedMode;
		m_state.gatewayLifecycle.startupModeSource =
			startupResult.selectedModeSource;
		m_state.gatewayLifecycle.startupDegraded = startupResult.degraded;
		m_state.gatewayLifecycle.failedStage = startupResult.failedStage;
		m_state.gatewayLifecycle.managedConfigReloaderStarted =
			startupResult.managedConfigReloaderStarted;
		m_state.gatewayLifecycle.startupFailureCleanupExecuted = false;
		m_state.gatewayLifecycle.cleanupPath = "none";
		m_state.gatewayLifecycle.authSessionGenerationCurrent =
			startupResult.resolvedRuntime.authSessionGeneration;
		m_state.gatewayLifecycle.authSessionGenerationRequired =
			startupResult.resolvedRuntime.authSessionGeneration;
		m_state.gatewayLifecycle.authSessionGenerationRejectCount = 0;
		ResetGatewayOwnedRuntimeCleanup();
		m_state.gatewayLiveRuntime.runtimeStateCreated = true;
		m_state.gatewayLiveRuntime.runtimeServicesStarted =
			startupResult.success;
		m_state.gatewayLiveRuntime.transportHandlersAttached =
			startupResult.gatewayStarted;
		m_state.gatewayLiveRuntime.runtimeSubscriptionsStarted =
			startupResult.success;

		for (const auto& warning : startupResult.warnings) {
			m_skillsCatalog.diagnostics.warnings.push_back(warning);
		}

		m_state.gatewayLiveRuntime.managedConfigReloaderEnabled =
			startupResult.managedConfigReloaderStarted;
		if (startupResult.managedConfigReloaderStarted) {
			const auto initialInternalWriteHash =
				ConsumeManagedConfigInternalWriteHash();
			m_gatewayManagedConfigReloader.Start(
				m_activeConfig,
				GatewayManagedConfigReloader::Options{
					.configPath = m_state.gatewayLiveRuntime.managedConfigPath,
					.pollIntervalMs = 1000,
				  .initialInternalWriteHash = initialInternalWriteHash,
					.pollInternalWriteHash = [this]() {
						return ConsumeManagedConfigInternalWriteHash();
					},
				},
				GatewayManagedConfigReloader::Callbacks{
					.appendTrace = [this](const char* stage) {
						AppendStartupTrace(stage);
					},
					.onWarning = [this](const std::wstring& warning) {
						m_skillsCatalog.diagnostics.warnings.push_back(warning);
					},
					.applyConfigDiff = [this](
						const blazeclaw::config::AppConfig& nextConfig,
						std::wstring& warningMessage) {
						return ApplyManagedRuntimeConfigDiff(
							nextConfig,
							warningMessage);
					},
				});
			m_state.gatewayLiveRuntime.managedConfigReloaderRunning =
				m_gatewayManagedConfigReloader.IsRunning();
			RecordGatewayLifecycleTransition("managed_reloader.start");
			RegisterGatewayOwnedRuntimeCleanup(
				"managed_config_reloader_stop",
				[this]() {
					m_gatewayManagedConfigReloader.Stop();
					m_state.gatewayLiveRuntime.managedConfigReloaderRunning = false;
					RecordGatewayLifecycleTransition("managed_reloader.stop");
				});
		}

		RegisterGatewayOwnedRuntimeCleanup(
			"non_gateway_runtime_cleanup",
			[this]() {
				ExecuteNonGatewayRuntimeCleanup();
			});

		RegisterGatewayOwnedRuntimeCleanup(
			"gateway_close_prelude",
			[this]() {
				const auto closePreludeWarnings =
					m_gatewayRuntimeBootstrapCoordinator.RunClosePrelude(
						GatewayRuntimeBootstrapCoordinator::CloseContext{
							.gatewayHost = m_gatewayHost,
							.appendTrace = [this](const char* stage) {
								AppendStartupTrace(stage);
							},
						});
				for (const auto& warning : closePreludeWarnings) {
					m_skillsCatalog.diagnostics.warnings.push_back(warning);
				}
				m_state.gatewayLifecycle.closePreludeExecuted = true;
				RecordGatewayLifecycleTransition("gateway.close_prelude.executed");
			});

		RegisterGatewayOwnedRuntimeCleanup(
			"gateway_host_stop",
			[this]() {
				m_gatewayHost.Stop();
				m_state.gatewayLiveRuntime.runtimeSubscriptionsStarted = false;
				m_state.gatewayLiveRuntime.transportHandlersAttached = false;
				m_state.gatewayLiveRuntime.runtimeServicesStarted = false;
				RecordGatewayLifecycleTransition("gateway.host.stop");
			});

		RegisterGatewayOwnedRuntimeCleanup(
			"speech_transcription_shutdown",
			[this]() {
				const auto preCancelledCount = static_cast<std::int64_t>(m_speechRecognition.transcribeRequestsCancelled);
				m_speechTranscriptionCoordinator.Shutdown(m_speechRecognitionRuntime);
				m_speechRecognition = m_speechRecognitionRuntime.Snapshot();
				const std::int64_t cancelledDelta = static_cast<std::int64_t>(m_speechRecognition.transcribeRequestsCancelled) -
					preCancelledCount;
				TRACE(
					"[ServiceManager][speech.shutdown.summary] started=%u completed=%u failed=%u cancelled=%u cancelledDelta=%lld status=%S\n",
					m_speechRecognition.transcribeRequestsStarted,
					m_speechRecognition.transcribeRequestsCompleted,
					m_speechRecognition.transcribeRequestsFailed,
					m_speechRecognition.transcribeRequestsCancelled,
					cancelledDelta,
					m_speechRecognition.status.c_str());
				RecordGatewayLifecycleTransition("speech.transcription.shutdown");
			});

		RegisterGatewayOwnedRuntimeCleanup(
			"native_recording_stop",
			[this]() {
				auto stopResult = m_gatewayHost.StopNativeRecording();
				TRACE(
					"[ServiceManager][native_recording.stop] ok=%d audioPath=%S error=%S\n",
					stopResult.ok ? 1 : 0,
					stopResult.audioPath.c_str(),
					stopResult.errorMessage.c_str());
				RecordGatewayLifecycleTransition("native_recording.stop");
			});

		RegisterGatewayOwnedRuntimeCleanup(
			"plugin_global_stop",
			[this]() {
				m_gatewayHost.NotifyPluginGlobalStopPrelude();
			});

		if (!startupResult.success) {
			ExecuteGatewayStartupFailureCleanup(config, startupResult);
			m_skillsCatalog.diagnostics.warnings.push_back(
				L"gateway startup failed; running in degraded local mode.");
			AppendStartupTrace("ServiceManager.Start.gateway.failed");
			RecordGatewayLifecycleTransition("startup.failed.degraded_mode");
			m_running = true;
			PublishGatewaySkillsStateProjection();
			return true;
		}

		if (startupResult.degraded) {
			AppendStartupTrace("ServiceManager.Start.gateway.degraded");
			RecordGatewayLifecycleTransition("startup.degraded");
		}

		m_running = true;
		RefreshOpenClawOriginalRuntimeTools(config);
		EmitOpenClawOriginalTelemetry();
		AppendStartupTrace("ServiceManager.Start.gateway.afterStart");
		RecordGatewayLifecycleTransition("startup.ready");
		return true;
	}

	void ServiceManager::Stop() {
		m_state.gatewayLifecycle.cleanupPath = "normal_stop";
		RecordGatewayLifecycleTransition("stop.begin");
		BeginShutdownPreludeRecording();
		RecordShutdownPreludePhase("non_gateway_runtime_cleanup");
		ExecuteNonGatewayRuntimeCleanup();   // unconditional
		ExecuteGatewayOwnedRuntimeCleanup();
		FinalizeShutdownPreludeEvidence();

		m_running = false;
		RecordGatewayLifecycleTransition("stop.done");
	}

	void ServiceManager::ResetGatewayOwnedRuntimeCleanup() {
		m_state.gatewayLiveRuntime.ownedCleanup.clear();
		m_state.gatewayLiveRuntime.ownedCleanupOrder.clear();
	}

	void ServiceManager::RegisterGatewayOwnedRuntimeCleanup(
		const std::string& name,
		std::function<void()> action) {
		if (!action) {
			return;
		}

		m_state.gatewayLiveRuntime.ownedCleanup.push_back(
			ServiceManagerState::GatewayLiveRuntimeState::OwnedCleanupEntry{
				.name = name,
				.action = std::move(action),
			});
	}

	void ServiceManager::ExecuteGatewayOwnedRuntimeCleanup() {
		for (auto it = m_state.gatewayLiveRuntime.ownedCleanup.rbegin();
			it != m_state.gatewayLiveRuntime.ownedCleanup.rend();
			++it) {
			m_state.gatewayLiveRuntime.ownedCleanupOrder.push_back(it->name);
			if (m_state.gatewayLifecycle.shutdownPreludeRecordingActive) {
				RecordShutdownPreludePhase(it->name);
			}
			if (it->action) {
				it->action();
			}
		}

		ResetGatewayOwnedRuntimeCleanup();
	}

	void ServiceManager::ExecuteGatewayStartupFailureCleanup(
		const blazeclaw::config::AppConfig& config,
		const GatewayRuntimeBootstrapCoordinator::StartupResult& startupResult) {
		AppendStartupTrace("ServiceManager.Start.gateway.startupFailureCleanup.begin");
		m_state.gatewayLifecycle.cleanupPath = "startup_failure";
		RecordGatewayLifecycleTransition("startup_failure_cleanup.begin");
		BeginShutdownPreludeRecording();

		if (m_gatewayManagedConfigReloader.IsRunning()) {
			RecordShutdownPreludePhase("managed_config_reloader.stop.startup_failure");
			m_gatewayManagedConfigReloader.Stop();
			m_state.gatewayLiveRuntime.managedConfigReloaderRunning = false;
			RecordGatewayLifecycleTransition("managed_reloader.stop.startup_failure");
		}

		RecordShutdownPreludePhase("non_gateway_runtime_cleanup");
		ExecuteNonGatewayRuntimeCleanup();

		ExecuteGatewayOwnedRuntimeCleanup();

		RecordShutdownPreludePhase("gateway.bootstrap.startup_failure_finalize");
		m_gatewayRuntimeBootstrapCoordinator.HandleStartupFailure(
			GatewayRuntimeBootstrapCoordinator::StartupContext{
				.config = config,
				.gatewayHost = m_gatewayHost,
				.appendTrace = [this](const char* stage) {
					AppendStartupTrace(stage);
				},
			},
			startupResult);

		FinalizeShutdownPreludeEvidence();
		m_state.gatewayLifecycle.startupFailureCleanupExecuted = true;
		RecordGatewayLifecycleTransition("startup_failure_cleanup.done");
		AppendStartupTrace("ServiceManager.Start.gateway.startupFailureCleanup.done");
	}

	void ServiceManager::ExecuteNonGatewayRuntimeCleanup() {
		m_skillsEnvOverrideService.RevertAll();
		RecordGatewayLifecycleTransition("skills_env_override.revert_all");

		if (m_state.chatRuntime.asyncQueueEnabled) {
			m_chatRuntime.StopWorker();
			RecordGatewayLifecycleTransition("chat_runtime.worker.stop");
		}

		{
			std::scoped_lock lock(m_deepSeekCancelMutex);
			m_deepSeekCancelledRuns.clear();
		}
		{
			std::scoped_lock lock(m_embeddedCancelMutex);
			m_embeddedCancelledRuns.clear();
		}
		RecordGatewayLifecycleTransition("runtime_cancellation_state.cleared");
	}

	void ServiceManager::RecordGatewayStartupConfigSnapshot() {
		blazeclaw::config::BuildGatewayStartupConfigFileSnapshot(
			m_state.gatewayLiveRuntime.managedConfigPath,
			m_state.gatewayLiveRuntime.pendingInternalWriteHashes,
			m_state.gatewayLiveRuntime.startupConfigSnapshot);
	}

	void ServiceManager::RefreshGatewayAuthBootstrapDiagnostics(
		const blazeclaw::config::AppConfig& config) {
		wchar_t* raw = nullptr;
		size_t rawSize = 0;
		if (_wdupenv_s(&raw, &rawSize, L"BLAZECLAW_GATEWAY_TOKEN") == 0 && raw != nullptr) {
			const std::wstring token = Trim(std::wstring(raw));
			free(raw);
			if (!token.empty()) {
				m_state.gatewayLifecycle.authBootstrapPathTag = "env";
				m_state.gatewayLifecycle.authBootstrapStatus = "ok";
				m_state.gatewayLifecycle.authBootstrapDetail =
					"BLAZECLAW_GATEWAY_TOKEN present";
				return;
			}
		}

		if (config.gateway.authSessionGeneration > 0) {
			m_state.gatewayLifecycle.authBootstrapPathTag = "session_generation_persisted";
			m_state.gatewayLifecycle.authBootstrapStatus = "ok";
			m_state.gatewayLifecycle.authBootstrapDetail =
				"gateway.authSessionGeneration>0 (persisted session policy marker)";
			return;
		}

		m_state.gatewayLifecycle.authBootstrapPathTag = "runtime_ephemeral";
		m_state.gatewayLifecycle.authBootstrapStatus = "warning";
		m_state.gatewayLifecycle.authBootstrapDetail =
			"no env token and authSessionGeneration is 0; OpenClaw-equivalent: generated-ephemeral vs not-yet-persisted";
	}

	void ServiceManager::RecordGatewayLifecycleTransition(
		const std::string& transition) {
		if (transition.empty()) {
			return;
		}

		m_state.gatewayLifecycle.transitions.push_back(transition);
		if (m_state.gatewayLifecycle.transitions.size() > 64) {
			m_state.gatewayLifecycle.transitions.erase(
				m_state.gatewayLifecycle.transitions.begin());
		}
	}

	void ServiceManager::BeginShutdownPreludeRecording() {
		if (m_state.gatewayLifecycle.shutdownPreludeRecordingActive) {
			return;
		}

		m_state.gatewayLifecycle.shutdownPreludeRecordingActive = true;
		m_state.gatewayLifecycle.lastShutdownPrelude.phaseOrderUtf8.clear();
		m_state.gatewayLifecycle.lastShutdownPrelude.pluginGlobalStopInvoked = false;
		m_state.gatewayLifecycle.lastShutdownPrelude.snapshotCleanupPathUtf8.clear();
		m_state.gatewayLifecycle.lastShutdownPrelude.recordedAtEpochMs =
			static_cast<std::uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch())
				.count());
	}

	void ServiceManager::RecordShutdownPreludePhase(const std::string& phaseName) {
		if (!m_state.gatewayLifecycle.shutdownPreludeRecordingActive || phaseName.empty()) {
			return;
		}

		auto& phases = m_state.gatewayLifecycle.lastShutdownPrelude.phaseOrderUtf8;
		if (phases.size() >= 48) {
			return;
		}

		phases.push_back(phaseName);
	}

	void ServiceManager::FinalizeShutdownPreludeEvidence() {
		if (!m_state.gatewayLifecycle.shutdownPreludeRecordingActive) {
			return;
		}

		m_state.gatewayLifecycle.shutdownPreludeRecordingActive = false;
		m_state.gatewayLifecycle.lastShutdownPrelude.snapshotCleanupPathUtf8 =
			m_state.gatewayLifecycle.cleanupPath;

		bool pluginRan = false;
		for (const std::string& phase :
			m_state.gatewayLifecycle.lastShutdownPrelude.phaseOrderUtf8) {
			if (phase == "plugin_global_stop") {
				pluginRan = true;
				break;
			}
		}

		m_state.gatewayLifecycle.lastShutdownPrelude.pluginGlobalStopInvoked = pluginRan;
		++m_state.gatewayLifecycle.gatewayShutdownInvocationCount;
	}

	void ServiceManager::QueueManagedConfigInternalWriteHash(
		std::uint64_t hash) {
		m_state.gatewayLiveRuntime.pendingInternalWriteHashes.push_back(hash);
		if (m_state.gatewayLiveRuntime.pendingInternalWriteHashes.size() > 16) {
			m_state.gatewayLiveRuntime.pendingInternalWriteHashes.erase(
				m_state.gatewayLiveRuntime.pendingInternalWriteHashes.begin());
		}
		RecordGatewayLifecycleTransition("managed_reloader.internal_write.queued");
	}

	std::optional<std::uint64_t>
		ServiceManager::ConsumeManagedConfigInternalWriteHash() {
		if (m_state.gatewayLiveRuntime.pendingInternalWriteHashes.empty()) {
			return std::nullopt;
		}

		const std::uint64_t hash =
			m_state.gatewayLiveRuntime.pendingInternalWriteHashes.front();
		m_state.gatewayLiveRuntime.pendingInternalWriteHashes.erase(
			m_state.gatewayLiveRuntime.pendingInternalWriteHashes.begin());
		RecordGatewayLifecycleTransition("managed_reloader.internal_write.consumed");
		return hash;
	}

	bool ServiceManager::ApplyManagedRuntimeConfigDiff(
		const blazeclaw::config::AppConfig& nextConfig,
		std::wstring& warningMessage) {
		const bool authSensitiveChanged = HasAuthSensitiveConfigChanges(
			m_activeConfig,
			nextConfig);
		const auto plan = m_managedRuntimeConfigDiffCoordinator.EvaluateApplyPlan(
			m_activeConfig,
			nextConfig,
			authSensitiveChanged,
			m_state.gatewayLifecycle.authSessionGenerationCurrent);
		if (!plan.accepted) {
			ApplyManagedRuntimeAuthReject(plan.authReject, warningMessage);
			return false;
		}
		return ApplyManagedRuntimeApplyPlan(plan, warningMessage);
	}

	void ServiceManager::ApplyManagedRuntimeAuthReject(
		const ManagedRuntimeConfigDiffCoordinator::AuthGuardResult& authGuard,
		std::wstring& warningMessage) {
		m_state.gatewayLifecycle.authSessionGenerationRequired =
			authGuard.requiredGeneration;
		++m_state.gatewayLifecycle.authSessionGenerationRejectCount;
		warningMessage = authGuard.warningMessage;
		m_skillsCatalog.diagnostics.warnings.push_back(warningMessage);
		RecordGatewayLifecycleTransition("managed_reload.auth_generation_reject");
	}

	bool ServiceManager::ApplyManagedRuntimeApplyPlan(
		const ManagedRuntimeApplyPlan& plan,
		std::wstring& warningMessage) {
		const blazeclaw::config::AppConfig& nextConfig = plan.nextConfig;

		if (plan.gatewayBindPortWarning.has_value()) {
			warningMessage = *plan.gatewayBindPortWarning;
		}

		m_activeConfig.chat.activeProvider = nextConfig.chat.activeProvider;
		m_activeConfig.chat.activeModel = nextConfig.chat.activeModel;
		m_activeChatProvider = m_activeConfig.chat.activeProvider.empty()
			? "local"
			: ToNarrow(m_activeConfig.chat.activeProvider);
		m_activeChatModel = m_activeConfig.chat.activeModel.empty()
			? "default"
			: ToNarrow(m_activeConfig.chat.activeModel);
		const auto previousLocalModelConfig = m_activeConfig.localModel;
		const auto previousLocalModelSnapshot = m_localModelRuntimeSnapshot;
		const bool previousLocalModelRolloutEligible = m_localModelRolloutEligible;
		const bool previousLocalModelActivationEnabled =
			m_localModelActivationEnabled;
		const std::string previousLocalModelActivationReason =
			m_localModelActivationReason;
		auto previousLocalModelRuntime = std::move(m_localModelRuntime);

		auto buildLocalModelRuntime = [](
			const std::wstring& provider)
			-> std::unique_ptr<localmodel::ITextGenerationRuntime> {
			const std::wstring normalizedProvider = ToLower(provider);
			if (normalizedProvider == L"llama" ||
				normalizedProvider == L"llama.cpp") {
				return std::make_unique<localmodel::LlamaTextGenerationRuntime>();
			}

			return std::make_unique<localmodel::OnnxTextGenerationRuntime>();
			};

		const auto runtimeOrchestrationPolicy =
			m_serviceBootstrapCoordinator.ResolveRuntimeOrchestrationPolicySettings();
		const bool localModelStartupLoadEnabled =
			runtimeOrchestrationPolicy.localModelStartupLoadEnabled;

		const auto localModelReload =
			m_managedRuntimeConfigDiffCoordinator.CoordinateLocalModelReload(
				m_activeConfig,
				nextConfig,
				m_localModelActivationEnabled,
				[this](const blazeclaw::config::AppConfig& config) {
					const std::wstring stage = config.localModel.rolloutStage;
					if (_wcsicmp(stage.c_str(), L"stable") == 0) {
						return true;
					}
					if (_wcsicmp(stage.c_str(), L"nightly") == 0) {
						return IsOneOfChannels(config.enabledChannels, L"nightly");
					}
					return true;
				},
				[this,
				&buildLocalModelRuntime,
				localModelStartupLoadEnabled](blazeclaw::config::AppConfig& candidateConfig) {
					if (m_activeChatProvider == "local" &&
						IsLlamaLocalModelId(m_activeChatModel)) {
						candidateConfig.localModel.provider = L"llama.cpp";
					}

					m_localModelRuntime =
						buildLocalModelRuntime(candidateConfig.localModel.provider);
					m_localModelRuntime->Configure(candidateConfig);

					bool localModelLoaded = false;
					const bool rolloutEligible =
						(_wcsicmp(candidateConfig.localModel.rolloutStage.c_str(), L"stable") == 0) ||
						((_wcsicmp(candidateConfig.localModel.rolloutStage.c_str(), L"nightly") == 0)
							? IsOneOfChannels(candidateConfig.enabledChannels, L"nightly")
							: true);
					if (candidateConfig.localModel.enabled &&
						rolloutEligible &&
						localModelStartupLoadEnabled) {
						localModelLoaded = m_localModelRuntime->LoadModel();
					}

					m_localModelRuntimeSnapshot = m_localModelRuntime->Snapshot();
					if (!localModelLoaded && m_localModelRuntimeSnapshot.status.empty()) {
						m_localModelRuntimeSnapshot.status = localModelStartupLoadEnabled
							? "load_failed"
							: "startup_load_deferred";
					}

					return candidateConfig.localModel.enabled
						? m_localModelRuntimeSnapshot.ready
						: false;
				});

		m_activeConfig.localModel = localModelReload.updatedConfig.localModel;
		m_localModelRolloutEligible = localModelReload.rolloutEligible;
		m_localModelActivationEnabled = localModelReload.activationEnabled;
		m_localModelActivationReason = localModelReload.activationReason;

		if (nextConfig.localModel.enabled &&
			!m_localModelActivationEnabled &&
			previousLocalModelActivationEnabled &&
			previousLocalModelRuntime != nullptr) {
			m_activeConfig.localModel = previousLocalModelConfig;
			m_localModelRuntime = std::move(previousLocalModelRuntime);
			m_localModelRuntimeSnapshot = previousLocalModelSnapshot;
			m_localModelRolloutEligible = previousLocalModelRolloutEligible;
			m_localModelActivationEnabled = previousLocalModelActivationEnabled;
			m_localModelActivationReason = previousLocalModelActivationReason;

			if (!warningMessage.empty()) {
				warningMessage += L" ";
			}
			warningMessage += localModelReload.warningMessage.empty()
				? L"local model activation failed for reloaded config; reverted to last known-good local model runtime settings."
				: localModelReload.warningMessage;
			m_skillsCatalog.diagnostics.warnings.push_back(
				L"local model activation failed on managed reload; fallback to previous active local model config.");
			RecordGatewayLifecycleTransition("managed_reload.local_model_fallback");
		}

		m_activeConfig.embedded.orchestrationPath =
			nextConfig.embedded.orchestrationPath;
		m_activeConfig.embedded.nodeParityEnabled =
			nextConfig.embedded.nodeParityEnabled;
		m_activeConfig.embedded.nodeParityDiagnosticsEnabled =
			nextConfig.embedded.nodeParityDiagnosticsEnabled;
		m_activeConfig.embedded.nodeParityRolloutMode =
			nextConfig.embedded.nodeParityRolloutMode;
		m_activeConfig.embedded.extensionSurfaceApplyEpoch =
			nextConfig.embedded.extensionSurfaceApplyEpoch;
		m_activeConfig.email = nextConfig.email;
		m_activeConfig.authProfiles = nextConfig.authProfiles;
		m_activeConfig.deepseekApiKey = nextConfig.deepseekApiKey;
		m_activeConfig.gateway.startupMode = nextConfig.gateway.startupMode;
		m_activeConfig.gateway.bindMode = nextConfig.gateway.bindMode;
		m_activeConfig.gateway.bindAddress = nextConfig.gateway.bindAddress;
		m_activeConfig.gateway.port = nextConfig.gateway.port;
		m_activeConfig.gateway.authSessionGeneration =
			nextConfig.gateway.authSessionGeneration;
		{
			std::string speechReloadStatus;
			const bool speechReloaded = ApplySpeechRecognitionConfigReload(
				nextConfig.speechRecognition.enabled,
				nextConfig.speechRecognition.provider,
				nextConfig.speechRecognition.storageRoot,
				nextConfig.speechRecognition.activeModelId,
				nextConfig.speechRecognition.modelPath,
				&speechReloadStatus);
			if (!speechReloaded) {
				const std::wstring warning =
					L"speech runtime reload failed; retaining configured state. status=" +
					ToWide(speechReloadStatus.empty()
						? std::string("unknown")
						: speechReloadStatus);
				m_skillsCatalog.diagnostics.warnings.push_back(warning);
				RecordGatewayLifecycleTransition("managed_reload.speech_runtime_reload_failed");
			}
			else if (speechReloadStatus == "startup_load_deferred") {
				RecordGatewayLifecycleTransition("managed_reload.speech_runtime_deferred");
			}
			else {
				RecordGatewayLifecycleTransition("managed_reload.speech_runtime_reloaded");
			}
		}

		RefreshOpenClawOriginalRuntimeTools(nextConfig);
		EmitOpenClawOriginalTelemetry();

		if (plan.authSensitiveChanged) {
			m_state.gatewayLifecycle.authSessionGenerationCurrent =
				nextConfig.gateway.authSessionGeneration;
			m_state.gatewayLifecycle.authSessionGenerationRequired =
				nextConfig.gateway.authSessionGeneration;
			RecordGatewayLifecycleTransition("managed_reload.auth_generation_accepted");
		}

		m_activeChatProvider = m_activeConfig.chat.activeProvider.empty()
			? "local"
			: ToNarrow(m_activeConfig.chat.activeProvider);
		m_activeChatModel = m_activeConfig.chat.activeModel.empty()
			? "default"
			: ToNarrow(m_activeConfig.chat.activeModel);

		m_gatewayHost.SetEmbeddedOrchestrationPath(
			ToNarrow(m_activeConfig.embedded.orchestrationPath));
		m_gatewayHost.SetNodeParityRuntimeFlags(
			m_activeConfig.embedded.nodeParityEnabled,
			m_activeConfig.embedded.nodeParityDiagnosticsEnabled,
			ToNarrow(m_activeConfig.embedded.nodeParityRolloutMode));

		const auto emailPolicy =
			m_serviceBootstrapCoordinator.ResolveEmailPolicySettings(m_activeConfig);
		m_state.emailPolicy.runtimeEnabled = emailPolicy.runtimeEnabled;
		m_state.emailPolicy.runtimeEnforce = emailPolicy.runtimeEnforce;
		m_state.emailPolicy.rolloutMode = emailPolicy.rolloutMode;
		m_state.emailPolicy.enforceChannel = emailPolicy.enforceChannel;
		m_state.emailPolicy.rollbackBridgeEnabled = emailPolicy.rollbackBridgeEnabled;
		m_state.emailPolicy.canaryEligible = emailPolicy.canaryEligible;

		m_emailFallbackResolvedPolicy =
			m_emailPolicyOrchestrationService.ResolveFallbackPolicy(
				m_activeConfig,
				L"email.schedule",
				L"email.send");

		const auto gatewayEmailBinding =
			m_emailPolicyOrchestrationService.BuildGatewayPolicyBinding(
				m_activeConfig.email,
				m_state.emailPolicy.runtimeEnabled,
				m_state.emailPolicy.runtimeEnforce,
				m_emailFallbackResolvedPolicy);
		m_gatewayHost.SetEmailFallbackRuntimeFlags(
			gatewayEmailBinding.preflightEnabled,
			gatewayEmailBinding.runtimeEnabled,
			gatewayEmailBinding.runtimeEnforce);
		m_gatewayHost.SetEmailFallbackResolvedPolicy(
			gatewayEmailBinding.backends,
			gatewayEmailBinding.onUnavailable,
			gatewayEmailBinding.onAuthError,
			gatewayEmailBinding.onExecError,
			gatewayEmailBinding.retryMaxAttempts,
			gatewayEmailBinding.retryDelayMs,
			gatewayEmailBinding.requiresApproval,
			gatewayEmailBinding.approvalTokenTtlMinutes,
			gatewayEmailBinding.profileId);

		m_configSchemaService.Invalidate();
		RefreshGatewaySkillsStateProjection();
		PublishGatewaySkillsStateProjection();

		if (m_state.gatewayLiveRuntime.runtimeServicesStarted &&
			nextConfig.embedded.extensionSurfaceApplyEpoch >
			m_state.gatewayLiveRuntime.lastAppliedExtensionSurfaceEpoch) {
			std::string deltaJson;
			if (m_gatewayHost.PerformDeferredExtensionCatalogReloadWithMethodSurfaceTelemetry(
				deltaJson)) {
				m_state.gatewayLiveRuntime.lastAppliedExtensionSurfaceEpoch =
					nextConfig.embedded.extensionSurfaceApplyEpoch;
				++m_state.gatewayLiveRuntime.extensionSurfaceReloadCount;
				m_state.gatewayLiveRuntime.lastExtensionSurfaceMethodDeltaJson =
					std::move(deltaJson);
				RecordGatewayLifecycleTransition(
					"managed_reload.extension_surface_method_snapshot");
				AppendStartupTrace(
					"GatewayRuntimeExtensionSurface.reload.method_surface.delta");
			}
		}

		++m_state.gatewayLiveRuntime.managedConfigApplyCount;
		RecordGatewayLifecycleTransition("managed_reload.applied");
		return true;
	}

	bool ServiceManager::IsRunning() const noexcept {
		return m_running;
	}

	const FeatureRegistry& ServiceManager::Registry() const noexcept {
		return m_registry;
	}

	const AgentScopeSnapshot& ServiceManager::AgentsScope() const noexcept {
		return m_agentsScope;
	}

	const AgentsWorkspaceSnapshot& ServiceManager::AgentsWorkspace() const noexcept {
		return m_agentsWorkspace;
	}

	const SubagentRegistrySnapshot& ServiceManager::SubagentRegistry() const noexcept {
		return m_subagentRegistry;
	}

	const AcpSpawnDecision& ServiceManager::LastAcpDecision() const noexcept {
		return m_lastAcpDecision;
	}

	std::size_t ServiceManager::ActiveEmbeddedRuns() const noexcept {
		return m_piEmbeddedService.ActiveRuns();
	}

	const AgentsToolPolicySnapshot& ServiceManager::ToolPolicy() const noexcept {
		return m_agentsToolPolicy;
	}

	std::size_t ServiceManager::ShellProcessCount() const noexcept {
		return m_agentsShellRuntimeService.ListProcesses().size();
	}

	const ModelRoutingSnapshot& ServiceManager::ModelRouting() const noexcept {
		return m_modelRouting;
	}

	const AuthProfileSnapshot& ServiceManager::AuthProfiles() const noexcept {
		return m_authProfiles;
	}

	const SandboxSnapshot& ServiceManager::Sandbox() const noexcept {
		return m_sandbox;
	}

	const EmbeddingsServiceSnapshot& ServiceManager::Embeddings() const noexcept {
		return m_embeddings;
	}

	const speechrecognition::SpeechRecognitionRuntimeSnapshot& ServiceManager::SpeechRecognition() const noexcept {
		return m_speechRecognition;
	}

	const localmodel::LocalModelRuntimeSnapshot& ServiceManager::LocalModelRuntime() const noexcept {
		return m_localModelRuntimeSnapshot;
	}

	bool ServiceManager::LocalModelRolloutEligible() const noexcept {
		return m_localModelRolloutEligible;
	}

	bool ServiceManager::LocalModelActivationEnabled() const noexcept {
		return m_localModelActivationEnabled;
	}

	const std::string& ServiceManager::LocalModelActivationReason() const noexcept {
		return m_localModelActivationReason;
	}

	const RetrievalMemorySnapshot& ServiceManager::RetrievalMemory() const noexcept {
		return m_retrievalMemory;
	}

	std::string ServiceManager::BuildOperatorDiagnosticsReport() const {
		const auto routing = ModelRouting();
		const auto auth = AuthProfiles();
		const auto sandbox = Sandbox();
		const auto embeddings = Embeddings();
		const auto localModel = LocalModelRuntime();
		const auto retrieval = RetrievalMemory();
		const auto emailHealth =
			m_emailPreflightHealthService.BuildRuntimeHealthIndex(false);

		const OperatorDiagnosticsInputs in{
			.gatewayLifecycle = GatewayLifecycleDiagnosticsProjector::Context{
			.runtimeRunning = m_running,
			.gatewayWarning = m_gatewayHost.LastWarning(),
			.startupMode = m_state.gatewayLifecycle.startupMode,
			.startupModeSource = m_state.gatewayLifecycle.startupModeSource,
			.startupFailedStage = m_state.gatewayLifecycle.failedStage,
			.startupDegraded = m_state.gatewayLifecycle.startupDegraded,
			.managedConfigReloaderStarted =
				m_state.gatewayLifecycle.managedConfigReloaderStarted,
			.managedConfigReloaderRunning =
				m_state.gatewayLiveRuntime.managedConfigReloaderRunning,
			.closePreludeExecuted =
				m_state.gatewayLifecycle.closePreludeExecuted,
			.startupFailureCleanupExecuted =
				m_state.gatewayLifecycle.startupFailureCleanupExecuted,
			.cleanupPath = m_state.gatewayLifecycle.cleanupPath,
			.runtimeStateCreated =
				m_state.gatewayLiveRuntime.runtimeStateCreated,
			.runtimeServicesStarted =
				m_state.gatewayLiveRuntime.runtimeServicesStarted,
			.transportHandlersAttached =
				m_state.gatewayLiveRuntime.transportHandlersAttached,
			.runtimeSubscriptionsStarted =
				m_state.gatewayLiveRuntime.runtimeSubscriptionsStarted,
			.managedConfigPath =
				ToNarrow(m_state.gatewayLiveRuntime.managedConfigPath),
			.managedConfigApplyCount =
				m_state.gatewayLiveRuntime.managedConfigApplyCount,
			.managedConfigRejectCount =
				m_state.gatewayLiveRuntime.managedConfigRejectCount,
			.authSessionGenerationCurrent =
				m_state.gatewayLifecycle.authSessionGenerationCurrent,
			.authSessionGenerationRequired =
				m_state.gatewayLifecycle.authSessionGenerationRequired,
			.authSessionGenerationRejectCount =
				m_state.gatewayLifecycle.authSessionGenerationRejectCount,
			.transitions = m_state.gatewayLifecycle.transitions,
			.startupConfigPathUtf8 = ToNarrow(
				m_state.gatewayLiveRuntime.startupConfigSnapshot.path),
			.startupConfigContentDigest =
				m_state.gatewayLiveRuntime.startupConfigSnapshot.contentDigest,
			.startupConfigRecordedAtEpochMs =
				m_state.gatewayLiveRuntime.startupConfigSnapshot.recordedAtEpochMs,
			.startupConfigFileExisted =
				m_state.gatewayLiveRuntime.startupConfigSnapshot.fileExisted,
			.startupConfigInternalWriteHashes =
				m_state.gatewayLiveRuntime.startupConfigSnapshot.internalWriteHashesAtRecord,
			.startupMigrationsApplied = m_state.gatewayLifecycle.startupMigrationsApplied,
			.authBootstrapPathTag = m_state.gatewayLifecycle.authBootstrapPathTag,
			.authBootstrapStatus = m_state.gatewayLifecycle.authBootstrapStatus,
			.authBootstrapDetail = m_state.gatewayLifecycle.authBootstrapDetail,
			.resolvedRuntime = m_state.gatewayLifecycle.resolvedRuntimeConfig,
			.extensionSurfaceReloadCount =
				m_state.gatewayLiveRuntime.extensionSurfaceReloadCount,
			.lastAppliedExtensionSurfaceEpoch =
				m_state.gatewayLiveRuntime.lastAppliedExtensionSurfaceEpoch,
			.lastExtensionSurfaceMethodDeltaJson =
				m_state.gatewayLiveRuntime.lastExtensionSurfaceMethodDeltaJson,
			.gatewayShutdownInvocationCount =
				m_state.gatewayLifecycle.gatewayShutdownInvocationCount,
			.lastShutdownPrelude = m_state.gatewayLifecycle.lastShutdownPrelude,
		},
			.email = EmailRuntimeDiagnosticsProjector::Context{
			.emailConfig = m_activeConfig.email,
			.policyRolloutMode = m_state.emailPolicy.rolloutMode,
			.policyEnforceChannel = m_state.emailPolicy.enforceChannel,
			.policyCanaryEligible = m_state.emailPolicy.canaryEligible,
			.rollbackBridgeEnabled = m_state.emailPolicy.rollbackBridgeEnabled,
			.runtimeEnabled = m_state.emailPolicy.runtimeEnabled,
			.runtimeEnforce = m_state.emailPolicy.runtimeEnforce,
			.resolvedPolicy = m_emailFallbackResolvedPolicy,
			.healthIndex = emailHealth,
			.fallbackAttempts =
				m_state.embeddedRuntime.emailFallbackAttemptCount,
			.fallbackSuccess =
				m_state.embeddedRuntime.emailFallbackSuccessCount,
			.fallbackFailure =
				m_state.embeddedRuntime.emailFallbackFailureCount,
		},
			.embedded = EmbeddedRuntimeDiagnosticsProjector::Context{
			.activeRuns = ActiveEmbeddedRuns(),
			.dynamicLoopEnabled =
				m_state.embeddedRuntime.lastDynamicLoopEnabled,
			.canaryEligible = m_state.embeddedRuntime.lastCanaryEligible,
			.promotionReady = m_state.embeddedRuntime.lastPromotionReady,
			.promotionMinRuns =
				m_state.embeddedRuntime.dynamicLoopPromotionMinRuns,
			.promotionMinSuccessRate =
				m_state.embeddedRuntime.dynamicLoopPromotionMinSuccessRate,
			.fallbackUsed = m_state.embeddedRuntime.lastFallbackUsed,
			.fallbackReason = m_state.embeddedRuntime.lastFallbackReason,
			.runSuccess = m_state.embeddedRuntime.runSuccessCount,
			.runFailure = m_state.embeddedRuntime.runFailureCount,
			.runTimeout = m_state.embeddedRuntime.runTimeoutCount,
			.runCancelled = m_state.embeddedRuntime.runCancelledCount,
			.runFallback = m_state.embeddedRuntime.runFallbackCount,
			.taskDeltaTransitions =
				m_state.embeddedRuntime.taskDeltaTransitionCount,
		},
			.modelRuntime = ModelRuntimeDiagnosticsProjector::Context{
			.embeddings = &embeddings,
			.localModel = &localModel,
			.retrieval = &retrieval,
			.localModelRolloutEligible = m_localModelRolloutEligible,
			.localModelActivationEnabled = m_localModelActivationEnabled,
			.localModelActivationReason = m_localModelActivationReason,
			.embeddingsConfigFeatureImplemented =
				m_registry.IsImplemented(L"embeddings-config-foundation"),
		},
			.hooks = HooksDiagnosticsProjector::Context{
			.engineEnabled = m_state.hooks.engineEnabled,
			.fallbackPromptInjection =
				m_state.hooks.fallbackPromptInjection,
			.reminderEnabled = m_state.hooks.reminderEnabled,
			.reminderVerbosity = m_state.hooks.reminderVerbosity,
			.strictPolicyEnforcement =
				m_state.hooks.strictPolicyEnforcement,
			.allowedPackagesCount = m_state.hooks.allowedPackages.size(),
			.governanceReportingEnabled =
				m_state.hooks.governanceReportingEnabled,
			.governanceReportsGenerated =
				m_state.hooks.governanceReportsGenerated,
			.lastGovernanceReportPath =
				m_state.hooks.lastGovernanceReportPath,
			.autoRemediationEnabled =
				m_state.hooks.autoRemediationEnabled,
			.autoRemediationRequiresApproval =
				m_state.hooks.autoRemediationRequiresApproval,
			.autoRemediationExecuted =
				m_state.hooks.autoRemediationExecuted,
			.lastAutoRemediationStatus =
				m_state.hooks.lastAutoRemediationStatus,
			.autoRemediationTenantId =
				m_state.hooks.autoRemediationTenantId,
			.lastAutoRemediationPlaybookPath =
				m_state.hooks.lastAutoRemediationPlaybookPath,
			.autoRemediationTokenMaxAgeMinutes =
				m_state.hooks.autoRemediationTokenMaxAgeMinutes,
			.autoRemediationTokenRotations =
				m_state.hooks.autoRemediationTokenRotations,
			.remediationTelemetryEnabled =
				m_state.hooks.remediationTelemetryEnabled,
			.remediationAuditEnabled =
				m_state.hooks.remediationAuditEnabled,
			.lastRemediationTelemetryPath =
				m_state.hooks.lastRemediationTelemetryPath,
			.lastRemediationAuditPath =
				m_state.hooks.lastRemediationAuditPath,
			.remediationSloStatus = m_state.hooks.remediationSloStatus,
			.remediationSloMaxDriftDetected =
				m_state.hooks.remediationSloMaxDriftDetected,
			.remediationSloMaxPolicyBlocked =
				m_state.hooks.remediationSloMaxPolicyBlocked,
			.complianceAttestationEnabled =
				m_state.hooks.complianceAttestationEnabled,
			.lastComplianceAttestationPath =
				m_state.hooks.lastComplianceAttestationPath,
			.enterpriseSlaGovernanceEnabled =
				m_state.hooks.enterpriseSlaGovernanceEnabled,
			.enterpriseSlaPolicyId = m_state.hooks.enterpriseSlaPolicyId,
			.crossTenantAttestationAggregationEnabled =
				m_state.hooks.crossTenantAttestationAggregationEnabled,
			.crossTenantAttestationAggregationStatus =
				m_state.hooks.crossTenantAttestationAggregationStatus,
			.crossTenantAttestationAggregationCount =
				m_state.hooks.crossTenantAttestationAggregationCount,
			.lastCrossTenantAttestationAggregationPath =
				m_state.hooks.lastCrossTenantAttestationAggregationPath,
			.selfEvolvingHookTriggered =
				m_state.hooks.selfEvolvingHookTriggered,
			.hookCatalog = &m_hookCatalog,
			.hookEvents = &m_hookEvents,
			.hookExecution = &m_hookExecution,
		},
			.featureRegistry = &m_registry,
			.agentsCount = m_agentsScope.entries.size(),
			.agentsDefaultAgent = ToNarrow(m_agentsScope.defaultAgentId),
			.subagentsActive = m_subagentRegistry.activeRuns,
			.subagentsPendingAnnounce = m_subagentRegistry.pendingAnnounce,
			.acpLastAllowed = m_lastAcpDecision.allowed,
			.acpReason = m_lastAcpDecision.reason,
			.toolsPolicyEntries = m_agentsToolPolicy.entries.size(),
			.toolsShellProcesses = ShellProcessCount(),
			.modelPrimary = routing.primaryModel,
			.modelFallback = routing.fallbackModel,
			.modelFailovers = routing.failoverHistory.size(),
			.authProfiles = auth.entries.size(),
			.sandboxEnabledCount = sandbox.enabledCount,
			.sandboxBrowserEnabledCount = sandbox.browserEnabledCount,
			.skillsCatalogEntries = m_skillsCatalog.entries.size(),
			.skillsPromptIncluded = m_skillsPrompt.includedCount,
			.skillsPromptWide = &m_skillsPrompt.prompt,
		};

		return m_operatorDiagnosticsAssembler.Build(in);
	}

	std::string ServiceManager::BuildGatewayParityLifecycleTraceJson() const {
		DiagnosticsSnapshot snapshot;
		const GatewayLifecycleDiagnosticsProjector::Context ctx{
			.runtimeRunning = m_running,
			.gatewayWarning = m_gatewayHost.LastWarning(),
			.startupMode = m_state.gatewayLifecycle.startupMode,
			.startupModeSource = m_state.gatewayLifecycle.startupModeSource,
			.startupFailedStage = m_state.gatewayLifecycle.failedStage,
			.startupDegraded = m_state.gatewayLifecycle.startupDegraded,
			.managedConfigReloaderStarted =
				m_state.gatewayLifecycle.managedConfigReloaderStarted,
			.managedConfigReloaderRunning =
				m_state.gatewayLiveRuntime.managedConfigReloaderRunning,
			.closePreludeExecuted =
				m_state.gatewayLifecycle.closePreludeExecuted,
			.startupFailureCleanupExecuted =
				m_state.gatewayLifecycle.startupFailureCleanupExecuted,
			.cleanupPath = m_state.gatewayLifecycle.cleanupPath,
			.runtimeStateCreated =
				m_state.gatewayLiveRuntime.runtimeStateCreated,
			.runtimeServicesStarted =
				m_state.gatewayLiveRuntime.runtimeServicesStarted,
			.transportHandlersAttached =
				m_state.gatewayLiveRuntime.transportHandlersAttached,
			.runtimeSubscriptionsStarted =
				m_state.gatewayLiveRuntime.runtimeSubscriptionsStarted,
			.managedConfigPath =
				ToNarrow(m_state.gatewayLiveRuntime.managedConfigPath),
			.managedConfigApplyCount =
				m_state.gatewayLiveRuntime.managedConfigApplyCount,
			.managedConfigRejectCount =
				m_state.gatewayLiveRuntime.managedConfigRejectCount,
			.authSessionGenerationCurrent =
				m_state.gatewayLifecycle.authSessionGenerationCurrent,
			.authSessionGenerationRequired =
				m_state.gatewayLifecycle.authSessionGenerationRequired,
			.authSessionGenerationRejectCount =
				m_state.gatewayLifecycle.authSessionGenerationRejectCount,
			.transitions = m_state.gatewayLifecycle.transitions,
			.startupConfigPathUtf8 = ToNarrow(
				m_state.gatewayLiveRuntime.startupConfigSnapshot.path),
			.startupConfigContentDigest =
				m_state.gatewayLiveRuntime.startupConfigSnapshot.contentDigest,
			.startupConfigRecordedAtEpochMs =
				m_state.gatewayLiveRuntime.startupConfigSnapshot.recordedAtEpochMs,
			.startupConfigFileExisted =
				m_state.gatewayLiveRuntime.startupConfigSnapshot.fileExisted,
			.startupConfigInternalWriteHashes =
				m_state.gatewayLiveRuntime.startupConfigSnapshot.internalWriteHashesAtRecord,
			.startupMigrationsApplied = m_state.gatewayLifecycle.startupMigrationsApplied,
			.authBootstrapPathTag = m_state.gatewayLifecycle.authBootstrapPathTag,
			.authBootstrapStatus = m_state.gatewayLifecycle.authBootstrapStatus,
			.authBootstrapDetail = m_state.gatewayLifecycle.authBootstrapDetail,
			.resolvedRuntime = m_state.gatewayLifecycle.resolvedRuntimeConfig,
			.extensionSurfaceReloadCount =
				m_state.gatewayLiveRuntime.extensionSurfaceReloadCount,
			.lastAppliedExtensionSurfaceEpoch =
				m_state.gatewayLiveRuntime.lastAppliedExtensionSurfaceEpoch,
			.lastExtensionSurfaceMethodDeltaJson =
				m_state.gatewayLiveRuntime.lastExtensionSurfaceMethodDeltaJson,
			.gatewayShutdownInvocationCount =
				m_state.gatewayLifecycle.gatewayShutdownInvocationCount,
			.lastShutdownPrelude = m_state.gatewayLifecycle.lastShutdownPrelude,
		};
		m_gatewayLifecycleDiagnosticsProjector.Apply(ctx, snapshot);
		return m_diagnosticsReportBuilder.SerializeParityLifecycleContractJson(
			snapshot.gatewayParityLifecycle);
	}

	void ServiceManager::SetActiveChatProvider(
		const std::string& provider,
		const std::string& model) {
		const std::string nextProvider = provider.empty() ? "local" : provider;
		const std::string nextModel = model.empty() ? "default" : model;
		const bool providerChanged = (nextProvider != m_activeChatProvider);
		const bool modelChanged = (nextModel != m_activeChatModel);

		if (providerChanged) {
			const std::uint64_t nextGeneration =
				(std::max)(
					m_state.gatewayLifecycle.authSessionGenerationCurrent,
					m_state.gatewayLifecycle.authSessionGenerationRequired) + 1;
			m_state.gatewayLifecycle.authSessionGenerationCurrent = nextGeneration;
			m_state.gatewayLifecycle.authSessionGenerationRequired = nextGeneration;
			RecordGatewayLifecycleTransition("runtime_mutation.auth_generation_bumped");
		}

		m_activeConfig.chat.activeProvider = ToWide(nextProvider);
		m_activeConfig.chat.activeModel = ToWide(nextModel);
		m_activeChatProvider = nextProvider;
		m_activeChatModel = nextModel;

		if (!providerChanged && !modelChanged) {
			RecordGatewayLifecycleTransition("runtime_mutation.chat_provider_noop");
			return;
		}

		RecordGatewayLifecycleTransition("runtime_mutation.chat_provider_applied");
	}

	bool ServiceManager::ApplySpeechRecognitionConfigReload(
		const bool speechEnabled,
		const std::wstring& speechProvider,
		const std::wstring& speechStorageRoot,
		const std::wstring& speechActiveModelId,
		const std::wstring& speechModelPath,
		std::string* outStatusMessage) {
		if (!m_running) {
			if (outStatusMessage != nullptr) {
				*outStatusMessage = "service_manager_not_running";
			}
			return false;
		}

		m_activeConfig.speechRecognition.enabled = speechEnabled;
		m_activeConfig.speechRecognition.provider = speechProvider;
		m_activeConfig.speechRecognition.storageRoot = speechStorageRoot;
		m_activeConfig.speechRecognition.activeModelId = speechActiveModelId;
		m_activeConfig.speechRecognition.modelPath = speechModelPath;

		m_speechTranscriptionCoordinator.Shutdown(m_speechRecognitionRuntime);
		m_speechRecognitionRuntime.Configure(m_activeConfig);

		const std::wstring runtimeHotMode =
			ToLower(Trim(m_activeConfig.speechRecognition.runtimeHotMode));
		const bool startupLoadEnabled =
			runtimeHotMode != L"on_demand" &&
			runtimeHotMode != L"idle_timeout";
		const bool loaded = startupLoadEnabled
			? m_speechRecognitionRuntime.LoadModel()
			: true;

		m_speechRecognition = m_speechRecognitionRuntime.Snapshot();
		if (!startupLoadEnabled) {
			m_speechRecognition.status = "startup_load_deferred";
		}
		else if (!loaded && m_speechRecognition.status.empty()) {
			m_speechRecognition.status = "load_failed";
		}

		if (outStatusMessage != nullptr) {
			*outStatusMessage = m_speechRecognition.status.empty()
				? (startupLoadEnabled ? std::string("loaded") : std::string("startup_load_deferred"))
				: m_speechRecognition.status;
		}

		return loaded;
	}

	const std::string& ServiceManager::ActiveChatProvider() const noexcept {
		return m_activeChatProvider;
	}

	const std::string& ServiceManager::ActiveChatModel() const noexcept {
		return m_activeChatModel;
	}

	std::optional<std::string> ServiceManager::ResolveDeepSeekCredentialUtf8() const {
		if (const auto cred = blazeclaw::app::CredentialStore::LoadCredential(
			L"blazeclaw.deepseek");
			cred.has_value() && !cred->empty()) {
			return cred;
		}

		wchar_t appdataBuf[MAX_PATH];
		if (GetEnvironmentVariableW(
			L"APPDATA",
			appdataBuf,
			static_cast<DWORD>(std::size(appdataBuf))) > 0) {
			const std::wstring dpPath =
				std::wstring(appdataBuf) + L"\\BlazeClaw\\deepseek.key";
			if (const auto dp = blazeclaw::app::CredentialStore::LoadCredentialDPAPI(
				dpPath);
				dp.has_value() && !dp->empty()) {
				return dp;
			}
		}

		return std::nullopt;
	}

	bool ServiceManager::HasDeepSeekCredential() const {
		const auto cred = ResolveDeepSeekCredentialUtf8();
		return cred.has_value() && !cred->empty();
	}

	bool ServiceManager::IsDeepSeekRunCancelled(const std::string& runId) const {
		std::scoped_lock lock(m_deepSeekCancelMutex);
		const auto it = m_deepSeekCancelledRuns.find(runId);
		return it != m_deepSeekCancelledRuns.end() && it->second;
	}

	void ServiceManager::MarkDeepSeekRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		EmitDeepSeekDiagnostic(
			"cancel",
			std::string("mark cancelled runId=") + runId);

		std::scoped_lock lock(m_deepSeekCancelMutex);
		m_deepSeekCancelledRuns.insert_or_assign(runId, true);
	}

	void ServiceManager::ClearDeepSeekRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_deepSeekCancelMutex);
		m_deepSeekCancelledRuns.erase(runId);
	}

	bool ServiceManager::IsEmbeddedRunCancelled(const std::string& runId) const {
		std::scoped_lock lock(m_embeddedCancelMutex);
		const auto it = m_embeddedCancelledRuns.find(runId);
		return it != m_embeddedCancelledRuns.end() && it->second;
	}

	void ServiceManager::MarkEmbeddedRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_embeddedCancelMutex);
		m_embeddedCancelledRuns.insert_or_assign(runId, true);
	}

	void ServiceManager::ClearEmbeddedRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_embeddedCancelMutex);
		m_embeddedCancelledRuns.erase(runId);
	}


	const SkillsCatalogSnapshot& ServiceManager::SkillsCatalog() const noexcept {
		return m_skillsCatalog;
	}

	const SkillsEligibilitySnapshot& ServiceManager::SkillsEligibility() const noexcept {
		return m_skillsEligibility;
	}

	const SkillsPromptSnapshot& ServiceManager::SkillsPrompt() const noexcept {
		return m_skillsPrompt;
	}

	const SkillsRunSnapshot& ServiceManager::RunSkillsSnapshot() const noexcept {
		return m_skillsRunSnapshot;
	}

	blazeclaw::gateway::ConfigSchemaGatewayState
		ServiceManager::BuildConfigSchemaGatewayState() const {
		return m_configSchemaService.BuildGatewayState(
			m_activeConfig,
			m_gatewaySkillsStateProjection);
	}

	std::optional<blazeclaw::gateway::ConfigSchemaGatewayLookupResult>
		ServiceManager::LookupConfigSchemaGatewayPath(
			const std::string& path) const {
		const auto state = BuildConfigSchemaGatewayState();
		return m_configSchemaService.Lookup(state, path);
	}

	bool ServiceManager::WriteConfigSchemaDocumentationSnapshot(
		const std::filesystem::path& outputPath,
		std::wstring& outError) const {
		const auto state = BuildConfigSchemaGatewayState();
		return m_configSchemaService.WriteDocumentationSnapshot(
			state,
			outputPath,
			outError);
	}

	std::string ServiceManager::InvokeGatewayMethod(
		const std::string& method,
		const std::optional<std::string>& paramsJson) const {
		const blazeclaw::gateway::protocol::RequestFrame request{
			.id = "ui-probe",
			.method = method,
			.paramsJson = paramsJson,
		};

		const auto response = RouteGatewayRequest(request);
		if (response.ok) {
			return response.payloadJson.has_value() ? response.payloadJson.value()
				: "ok";
		}

		if (!response.error.has_value()) {
			return "error_unknown";
		}

		const auto& error = response.error.value();
		return error.code + ":" + error.message;
	}

	blazeclaw::gateway::protocol::ResponseFrame ServiceManager::RouteGatewayRequest(
		const blazeclaw::gateway::protocol::RequestFrame& request) const {
		if (!m_running) {
			return blazeclaw::gateway::protocol::ResponseFrame{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = blazeclaw::gateway::protocol::ErrorShape{
					.code = "service_not_running",
					.message = "Service manager is not running.",
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};
		}

		if (request.method.empty()) {
			return blazeclaw::gateway::protocol::ResponseFrame{
				.id = request.id,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = blazeclaw::gateway::protocol::ErrorShape{
					.code = "invalid_method",
					.message = "Gateway method must not be empty.",
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};
		}

		return m_gatewayHost.RouteRequest(request);
	}

	bool ServiceManager::PumpGatewayNetworkOnce(std::string& error) {
		if (!m_running) {
			error = "service manager is not running";
			return false;
		}

		const std::uint64_t applyCountBefore =
			m_gatewayManagedConfigReloader.ApplyCount();
		const std::uint64_t rejectCountBefore =
			m_gatewayManagedConfigReloader.RejectCount();

		m_gatewayManagedConfigReloader.Pump();
		m_state.gatewayLiveRuntime.managedConfigReloaderRunning =
			m_gatewayManagedConfigReloader.IsRunning();
		m_state.gatewayLiveRuntime.managedConfigApplyCount =
			m_gatewayManagedConfigReloader.ApplyCount();
		m_state.gatewayLiveRuntime.managedConfigRejectCount =
			m_gatewayManagedConfigReloader.RejectCount();

		if (m_gatewayManagedConfigReloader.ApplyCount() > applyCountBefore) {
			RecordGatewayLifecycleTransition("managed_reload.apply_observed");
		}
		if (m_gatewayManagedConfigReloader.RejectCount() > rejectCountBefore) {
			RecordGatewayLifecycleTransition("managed_reload.reject_observed");
		}

		return m_gatewayHost.PumpNetworkOnce(error);
	}

	bool ServiceManager::IsLocalModelRolloutEligible() const {
		const std::wstring stage = m_activeConfig.localModel.rolloutStage;
		if (_wcsicmp(stage.c_str(), L"stable") == 0) {
			return true;
		}

		if (_wcsicmp(stage.c_str(), L"nightly") == 0) {
			return IsOneOfChannels(m_activeConfig.enabledChannels, L"nightly");
		}

		return true;
	}

	bool ServiceManager::IsEmbeddedDynamicLoopCanaryEligible(
		const std::string& provider,
		const std::string& sessionId) const {
		if (!m_activeConfig.embedded.dynamicToolLoopEnabled) {
			return false;
		}

		if (m_state.embeddedRuntime.dynamicLoopCanaryProviders.empty() &&
			m_state.embeddedRuntime.dynamicLoopCanarySessions.empty()) {
			return true;
		}

		const bool providerAllowed = m_state.embeddedRuntime.dynamicLoopCanaryProviders.empty() ||
			ContainsCaseInsensitive(m_state.embeddedRuntime.dynamicLoopCanaryProviders, provider);
		const bool sessionAllowed = m_state.embeddedRuntime.dynamicLoopCanarySessions.empty() ||
			ContainsCaseInsensitive(m_state.embeddedRuntime.dynamicLoopCanarySessions, sessionId);
		return providerAllowed && sessionAllowed;
	}

	bool ServiceManager::IsEmbeddedDynamicLoopPromotionReady() const {
		if (!m_activeConfig.embedded.dynamicToolLoopEnabled) {
			return false;
		}

		const std::uint64_t totalRuns =
			m_state.embeddedRuntime.runSuccessCount + m_state.embeddedRuntime.runFailureCount;
		if (totalRuns < m_state.embeddedRuntime.dynamicLoopPromotionMinRuns) {
			return false;
		}

		const double successRate =
			static_cast<double>(m_state.embeddedRuntime.runSuccessCount) /
			static_cast<double>(totalRuns);
		return successRate >= m_state.embeddedRuntime.dynamicLoopPromotionMinSuccessRate;
	}

} // namespace blazeclaw::core

