#include "pch.h"
#include "LlamaTextGenerationRuntime.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>
#include <Windows.h>

#if __has_include(<llama.h>)
#include <llama.h>
#define BLAZECLAW_HAS_LLAMACPP 1
#else
#define BLAZECLAW_HAS_LLAMACPP 0
#endif

namespace blazeclaw::core::localmodel {

	namespace {

		struct ProcessStreamResult {
			bool started = false;
			bool cancelled = false;
			DWORD exitCode = static_cast<DWORD>(-1);
			std::string output;
			std::string errorCode;
			std::string errorMessage;
		};

		std::string ToNarrow(const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const wchar_t ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return output;
		}

		std::wstring ToWide(const std::string& value) {
			std::wstring output;
			output.reserve(value.size());
			for (const char ch : value) {
				output.push_back(static_cast<wchar_t>(static_cast<unsigned char>(ch)));
			}

			return output;
		}

		std::string ToLowerAscii(const std::string& value) {
			std::string lowered = value;
			for (char& ch : lowered) {
				if (ch >= 'A' && ch <= 'Z') {
					ch = static_cast<char>(ch - 'A' + 'a');
				}
			}

			return lowered;
		}

		std::filesystem::path ResolveConfiguredPath(
			const std::string& configuredPath,
			const std::string& storageRoot) {
			const std::filesystem::path configured(configuredPath);
			if (configured.empty()) {
				return configured;
			}

			if (configured.is_absolute()) {
				return configured.lexically_normal();
			}

			std::filesystem::path root(storageRoot);
			if (root.empty()) {
				return configured.lexically_normal();
			}

			if (!root.is_absolute()) {
				std::error_code ec;
				root = std::filesystem::current_path(ec) / root;
			}

			return (root / configured).lexically_normal();
		}

		std::uint32_t ElapsedMs(
			const std::chrono::steady_clock::time_point& startedAt) {
			return static_cast<std::uint32_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - startedAt)
				.count());
		}

		double TokensPerSecond(
			const std::uint32_t generatedTokens,
			const std::uint32_t latencyMs) {
			if (generatedTokens == 0 || latencyMs == 0) {
				return 0.0;
			}

			return static_cast<double>(generatedTokens) * 1000.0 /
				static_cast<double>(latencyMs);
		}

		std::wstring QuoteCommandToken(const std::wstring& token) {
			if (token.empty()) {
				return L"\"\"";
			}

			const bool hasWhitespace =
				token.find_first_of(L" \t\r\n\"") != std::wstring::npos;
			if (!hasWhitespace) {
				return token;
			}

			std::wstring quoted;
			quoted.reserve(token.size() + 2);
			quoted.push_back(L'\"');
			for (const wchar_t ch : token) {
				if (ch == L'\"') {
					quoted += L"\\\"";
				}
				else {
					quoted.push_back(ch);
				}
			}
			quoted.push_back(L'\"');
			return quoted;
		}

		std::wstring BuildCommandLine(const std::vector<std::wstring>& tokens) {
			std::wstring commandLine;
			for (std::size_t i = 0; i < tokens.size(); ++i) {
				if (i > 0) {
					commandLine.push_back(L' ');
				}
				commandLine += QuoteCommandToken(tokens[i]);
			}
			return commandLine;
		}

		std::optional<std::filesystem::path> ResolveLlamaCliExecutable() {
			std::vector<std::filesystem::path> baseDirs;
			auto appendEnvDir = [&baseDirs](const char* envName) {
				char* raw = nullptr;
				size_t rawSize = 0;
				if (_dupenv_s(&raw, &rawSize, envName) == 0 && raw != nullptr) {
					const std::string value(raw);
					free(raw);
					if (!value.empty()) {
						baseDirs.emplace_back(value);
					}
				}
			};

			appendEnvDir("BLAZECLAW_LLAMA_CPP_BIN_DIR");

			char* rawRoot = nullptr;
			size_t rawRootSize = 0;
			if (_dupenv_s(&rawRoot, &rawRootSize, "BLAZECLAW_LLAMA_CPP_ROOT") == 0 && rawRoot != nullptr) {
				const std::filesystem::path root(rawRoot);
				free(rawRoot);
				if (!root.empty()) {
					baseDirs.emplace_back(root / "build" / "bin");
					baseDirs.emplace_back(root / "bin");
				}
			}

			std::error_code cwdEc;
			const auto cwd = std::filesystem::current_path(cwdEc);
			if (!cwdEc) {
				baseDirs.emplace_back(cwd / "llama.cpp" / "build" / "bin");
				baseDirs.emplace_back(cwd / ".." / "llama.cpp" / "build" / "bin");
				baseDirs.emplace_back(cwd / ".." / ".." / "llama.cpp" / "build" / "bin");
				baseDirs.emplace_back(cwd / ".." / ".." / ".." / "llama.cpp" / "build" / "bin");
			}

			baseDirs.emplace_back(std::filesystem::path("llama.cpp") / "build" / "bin");
			baseDirs.emplace_back(std::filesystem::path("..") / "llama.cpp" / "build" / "bin");
			baseDirs.emplace_back(std::filesystem::path("..") / ".." / "llama.cpp" / "build" / "bin");
			baseDirs.emplace_back(std::filesystem::path("..") / ".." / ".." / "llama.cpp" / "build" / "bin");

			const std::vector<std::wstring> exeNames = {
				L"llama-cli.exe",
				L"main.exe",
			};

			for (const auto& dir : baseDirs) {
				std::error_code ec;
				const auto normalized = std::filesystem::weakly_canonical(dir, ec);
				const auto root = ec ? dir : normalized;
				for (const auto& exeName : exeNames) {
					const auto candidate = root / exeName;
					if (std::filesystem::exists(candidate, ec) && !ec) {
						return candidate;
					}
				}
			}

			for (const auto& exeName : exeNames) {
				const std::filesystem::path candidate(exeName);
				std::error_code ec;
				if (std::filesystem::exists(candidate, ec) && !ec) {
					return candidate;
				}
			}

			return std::nullopt;
		}

		void DrainPipeAvailable(
			HANDLE readPipe,
			std::string& output,
			const std::function<void(const std::string&)>& onChunk) {
			if (readPipe == nullptr || readPipe == INVALID_HANDLE_VALUE) {
				return;
			}

			for (;;) {
				DWORD available = 0;
				if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) ||
					available == 0) {
					break;
				}

				char buffer[4096]{};
				DWORD bytesRead = 0;
				if (!ReadFile(
					readPipe,
					buffer,
					(std::min)(available, static_cast<DWORD>(sizeof(buffer))),
					&bytesRead,
					nullptr) || bytesRead == 0) {
					break;
				}

				output.append(buffer, buffer + bytesRead);
				if (onChunk) {
					onChunk(std::string(buffer, buffer + bytesRead));
				}
			}
		}

		ProcessStreamResult RunProcessWithStreaming(
			const std::vector<std::wstring>& commandTokens,
			const std::function<bool()>& shouldCancel,
			const std::function<void(const std::string&)>& onChunk) {
			ProcessStreamResult result;
			if (commandTokens.empty()) {
				result.errorCode = "invalid_args";
				result.errorMessage = "command is empty";
				return result;
			}

			SECURITY_ATTRIBUTES security{};
			security.nLength = sizeof(security);
			security.bInheritHandle = TRUE;
			security.lpSecurityDescriptor = nullptr;

			HANDLE outputRead = nullptr;
			HANDLE outputWrite = nullptr;
			if (!CreatePipe(&outputRead, &outputWrite, &security, 0)) {
				result.errorCode = "pipe_create_failed";
				result.errorMessage = "failed to create process pipe";
				return result;
			}
			SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0);

			STARTUPINFOW startupInfo{};
			startupInfo.cb = sizeof(startupInfo);
			startupInfo.dwFlags = STARTF_USESTDHANDLES;
			startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			startupInfo.hStdOutput = outputWrite;
			startupInfo.hStdError = outputWrite;

			PROCESS_INFORMATION processInfo{};
			std::wstring commandLine = BuildCommandLine(commandTokens);
			std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
			mutableCommand.push_back(L'\0');

			const BOOL created = CreateProcessW(
				nullptr,
				mutableCommand.data(),
				nullptr,
				nullptr,
				TRUE,
				CREATE_NO_WINDOW,
				nullptr,
				nullptr,
				&startupInfo,
				&processInfo);

			CloseHandle(outputWrite);
			outputWrite = nullptr;

			if (!created) {
				result.errorCode = "process_start_failed";
				result.errorMessage = "failed to start llama process";
				if (outputRead != nullptr) {
					CloseHandle(outputRead);
				}
				return result;
			}

			result.started = true;
			for (;;) {
				if (shouldCancel && shouldCancel()) {
					TerminateProcess(processInfo.hProcess, 125);
					WaitForSingleObject(processInfo.hProcess, 2000);
					result.cancelled = true;
					break;
				}

				const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 50);
				DrainPipeAvailable(outputRead, result.output, onChunk);
				if (waitResult == WAIT_OBJECT_0) {
					break;
				}
				if (waitResult == WAIT_FAILED) {
					result.errorCode = "process_wait_failed";
					result.errorMessage = "failed to wait for llama process";
					break;
				}
			}

			DrainPipeAvailable(outputRead, result.output, onChunk);
			if (outputRead != nullptr) {
				CloseHandle(outputRead);
			}

			GetExitCodeProcess(processInfo.hProcess, &result.exitCode);
			CloseHandle(processInfo.hThread);
			CloseHandle(processInfo.hProcess);
			return result;
		}

		std::string TrimAscii(const std::string& value) {
			std::size_t start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}
			std::size_t end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}
			return value.substr(start, end - start);
		}

		std::uint32_t ApproximateGeneratedTokens(const std::string& text) {
			const auto trimmed = TrimAscii(text);
			if (trimmed.empty()) {
				return 0;
			}

			std::istringstream stream(trimmed);
			std::string token;
			std::uint32_t count = 0;
			while (stream >> token) {
				++count;
			}
			return count > 0 ? count : 1;
		}

	} // namespace

	struct LlamaTextGenerationRuntime::SessionState {
		bool loaded = false;
	};

	LlamaTextGenerationRuntime::LlamaTextGenerationRuntime() {
		m_sessionState = std::make_unique<SessionState>();
		ResetSnapshotLocked();
	}

	LlamaTextGenerationRuntime::~LlamaTextGenerationRuntime() = default;

	void LlamaTextGenerationRuntime::Configure(
		const blazeclaw::config::AppConfig& appConfig) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_config = appConfig;
		m_sessionState = std::make_unique<SessionState>();
		m_cancelFlagsByRunId.clear();
		ResetSnapshotLocked();
	}

	LocalModelRuntimeSnapshot LlamaTextGenerationRuntime::Snapshot() const {
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_snapshot;
	}

	void LlamaTextGenerationRuntime::TraceRuntime(
		const char* stage,
		const std::string& runId,
		const std::string& details) {
		TRACE(
			"[LocalModel][llama.cpp] %s runId=%s %s\n",
			stage == nullptr ? "<null>" : stage,
			runId.c_str(),
			details.c_str());
	}

	void LlamaTextGenerationRuntime::ResetSnapshotLocked() {
		m_snapshot = LocalModelRuntimeSnapshot{};
		m_snapshot.enabled = m_config.localModel.enabled;
		m_snapshot.provider = ToNarrow(m_config.localModel.provider);
		m_snapshot.rolloutStage = ToNarrow(m_config.localModel.rolloutStage);
		m_snapshot.storageRoot = ToNarrow(m_config.localModel.storageRoot);
		m_snapshot.version = ToNarrow(m_config.localModel.version);
		m_snapshot.modelPath = ToNarrow(m_config.localModel.modelPath);
		m_snapshot.modelExpectedSha256 = ToNarrow(m_config.localModel.modelSha256);
		m_snapshot.tokenizerPath = ToNarrow(m_config.localModel.tokenizerPath);
		m_snapshot.tokenizerExpectedSha256 =
			ToNarrow(m_config.localModel.tokenizerSha256);
		m_snapshot.maxTokens = m_config.localModel.maxTokens;
		m_snapshot.temperature = m_config.localModel.temperature;
		m_snapshot.intraThreads = m_config.localModel.intraThreads;
		m_snapshot.interThreads = m_config.localModel.interThreads;
		m_snapshot.executionMode = ToNarrow(m_config.localModel.executionMode);
		m_snapshot.verboseMetrics = m_config.localModel.verboseMetrics;
		m_snapshot.status = m_snapshot.enabled ? "configured" : "disabled";
	}

	bool LlamaTextGenerationRuntime::LoadModel() {
		std::lock_guard<std::mutex> lock(m_mutex);
		ResetSnapshotLocked();
		++m_snapshot.modelLoadAttempts;

		m_snapshot.modelPath = ResolveConfiguredPath(
			m_snapshot.modelPath,
			m_snapshot.storageRoot)
			.string();

		const std::string provider = ToLowerAscii(m_snapshot.provider);
		const bool providerSupported =
			provider == "llama.cpp" || provider == "llama";

		TraceRuntime(
			"model.load.start",
			{},
			"provider=" + m_snapshot.provider +
			" storageRoot=" + m_snapshot.storageRoot +
			" model=" + m_snapshot.modelPath);

		if (!m_snapshot.enabled) {
			m_snapshot.ready = false;
			m_snapshot.status = "disabled";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::LocalModelDisabled,
				.message = "chat.localModel.enabled=false",
			};
			return false;
		}

		if (!providerSupported) {
			m_snapshot.ready = false;
			m_snapshot.status = "provider_not_supported";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::ProviderNotSupported,
				.message = "Local llama runtime requires provider llama.cpp.",
			};
			return false;
		}

		if (m_snapshot.modelPath.empty()) {
			m_snapshot.ready = false;
			m_snapshot.status = "model_missing";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::ModelNotFound,
				.message = "chat.localModel.modelPath is not configured.",
			};
			return false;
		}

		const std::filesystem::path modelPath(m_snapshot.modelPath);
		std::error_code ec;
		if (!std::filesystem::exists(modelPath, ec) || ec) {
			m_snapshot.ready = false;
			m_snapshot.status = "model_missing";
			++m_snapshot.modelLoadFailures;
			m_snapshot.error = TextGenerationError{
				.code = TextGenerationErrorCode::ModelNotFound,
				.message = "Local GGUF model file was not found.",
			};
			return false;
		}

#if BLAZECLAW_HAS_LLAMACPP
		m_sessionState = std::make_unique<SessionState>();
		m_sessionState->loaded = true;
		m_snapshot.runtimeDllPresent = true;
		m_snapshot.effectiveExecutionProvider = "llama.cpp";
		m_snapshot.ready = true;
		m_snapshot.status = "ready";
		m_snapshot.error.reset();
		TraceRuntime(
			"model.load.success",
			{},
			"status=ready model=" + m_snapshot.modelPath);
		return true;
#else
		m_snapshot.ready = false;
		m_snapshot.status = "runtime_unavailable";
		++m_snapshot.modelLoadFailures;
		m_snapshot.error = TextGenerationError{
			.code = TextGenerationErrorCode::RuntimeUnavailable,
			.message =
				"llama.cpp headers are unavailable at compile time. Build third_party/llama.cpp artifacts and recompile.",
		};
		TraceRuntime(
			"model.load.failure",
			{},
			"status=runtime_unavailable reason=llama_headers_missing");
		return false;
#endif
	}

	bool LlamaTextGenerationRuntime::EnsureLoadedLocked(
		TextGenerationResult& outResult) {
		if (m_snapshot.ready && m_sessionState && m_sessionState->loaded) {
			return true;
		}

		outResult.ok = false;
		outResult.modelId = m_snapshot.modelPath;
		outResult.error = m_snapshot.error.has_value()
			? m_snapshot.error
			: std::optional<TextGenerationError>(TextGenerationError{
				  .code = TextGenerationErrorCode::RuntimeUnavailable,
				  .message = "local llama runtime is not ready",
				});
		return false;
	}

	TextGenerationResult LlamaTextGenerationRuntime::GenerateStream(
		const TextGenerationRequest& request,
		const TextDeltaCallback& onDelta) {
		const auto startedAt = std::chrono::steady_clock::now();
		TextGenerationResult result;

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			++m_snapshot.requestsStarted;
			if (!EnsureLoadedLocked(result)) {
				++m_snapshot.requestsFailed;
				result.latencyMs = ElapsedMs(startedAt);
				m_snapshot.lastLatencyMs = result.latencyMs;
				return result;
			}

			if (request.prompt.empty()) {
				result.ok = false;
				result.modelId = m_snapshot.modelPath;
				result.error = TextGenerationError{
					.code = TextGenerationErrorCode::InvalidInput,
					.message = "Prompt must not be empty.",
				};
				result.latencyMs = ElapsedMs(startedAt);
				++m_snapshot.requestsFailed;
				m_snapshot.lastLatencyMs = result.latencyMs;
				return result;
			}

			if (!request.runId.empty()) {
				m_cancelFlagsByRunId.insert_or_assign(request.runId, false);
			}
		}

		const auto clearCancelFlag = [this, &request](void*) {
			if (request.runId.empty()) {
				return;
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			m_cancelFlagsByRunId.erase(request.runId);
			};
		std::unique_ptr<void, decltype(clearCancelFlag)> cancelFlagGuard(
			reinterpret_cast<void*>(1),
			clearCancelFlag);

		std::string modelPath;
		std::uint32_t maxTokens = 0;
		double temperature = 0.0;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			modelPath = m_snapshot.modelPath;
			maxTokens = request.maxTokens.has_value() ? request.maxTokens.value() : m_snapshot.maxTokens;
			temperature = request.temperature.has_value() ? request.temperature.value() : m_snapshot.temperature;
		}

		auto isCancelled = [this, &request]() {
			if (request.runId.empty()) {
				return false;
			}

			std::lock_guard<std::mutex> lock(m_mutex);
			const auto it = m_cancelFlagsByRunId.find(request.runId);
			return it != m_cancelFlagsByRunId.end() && it->second;
		};

		if (isCancelled()) {
			result.ok = false;
			result.cancelled = true;
			result.modelId = modelPath;
			result.generatedTokens = 0;
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::Cancelled,
				.message = "Generation cancelled.",
			};
			result.latencyMs = ElapsedMs(startedAt);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_snapshot.requestsCancelled;
				m_snapshot.lastLatencyMs = result.latencyMs;
				m_snapshot.lastGeneratedTokens = 0;
			}
			return result;
		}

		auto cliExecutable = ResolveLlamaCliExecutable();
		if (!cliExecutable.has_value()) {
			result.ok = false;
			result.modelId = modelPath;
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::RuntimeUnavailable,
				.message =
				"Unable to resolve llama.cpp CLI executable (llama-cli.exe/main.exe). Set BLAZECLAW_LLAMA_CPP_BIN_DIR.",
			};
			result.latencyMs = ElapsedMs(startedAt);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_snapshot.requestsFailed;
				m_snapshot.lastLatencyMs = result.latencyMs;
				m_snapshot.lastGeneratedTokens = 0;
			}
			return result;
		}

		std::vector<std::wstring> commandTokens;
		commandTokens.push_back(cliExecutable->wstring());
		commandTokens.push_back(L"-m");
		commandTokens.push_back(ToWide(modelPath));
		commandTokens.push_back(L"-p");
		commandTokens.push_back(ToWide(request.prompt));
		commandTokens.push_back(L"-n");
		commandTokens.push_back(ToWide(std::to_string(maxTokens)));
		commandTokens.push_back(L"--temp");
		commandTokens.push_back(ToWide(std::to_string(temperature)));
		commandTokens.push_back(L"--no-display-prompt");

		std::string generated;
		auto processResult = RunProcessWithStreaming(
			commandTokens,
			isCancelled,
			[&generated, &onDelta](const std::string& chunk) {
				if (chunk.empty()) {
					return;
				}

				generated += chunk;
				if (onDelta) {
					onDelta(generated);
				}
			});

		const auto trimmedGenerated = TrimAscii(generated);
		const std::uint32_t generatedTokens = ApproximateGeneratedTokens(trimmedGenerated);

		if (processResult.cancelled || isCancelled()) {
			result.ok = false;
			result.cancelled = true;
			result.modelId = modelPath;
			result.generatedTokens = generatedTokens;
			result.error = TextGenerationError{
				.code = TextGenerationErrorCode::Cancelled,
				.message = "Generation cancelled.",
			};
			result.latencyMs = ElapsedMs(startedAt);
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				++m_snapshot.requestsCancelled;
				m_snapshot.lastLatencyMs = result.latencyMs;
				m_snapshot.lastGeneratedTokens = generatedTokens;
			}
			return result;
		}

		result.ok = !trimmedGenerated.empty();
		result.text = trimmedGenerated;
		result.modelId = modelPath;
		result.generatedTokens = generatedTokens;
		result.latencyMs = ElapsedMs(startedAt);
		if (!result.ok) {
			if (!processResult.started || !processResult.errorCode.empty() || processResult.exitCode != 0) {
				const std::string reason =
					!processResult.errorMessage.empty()
					? processResult.errorMessage
					: ("llama-cli exited with code " + std::to_string(static_cast<unsigned long long>(processResult.exitCode)));
				result.error = TextGenerationError{
					.code = TextGenerationErrorCode::InferenceFailed,
					.message = reason,
				};
			}
			else {
				result.error = TextGenerationError{
					.code = TextGenerationErrorCode::EmptyOutput,
					.message = "No tokens were generated.",
				};
			}
		}

		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (result.ok) {
				++m_snapshot.requestsCompleted;
				m_snapshot.cumulativeTokens += result.generatedTokens;
				m_snapshot.cumulativeLatencyMs += result.latencyMs;
			}
			else {
				++m_snapshot.requestsFailed;
			}

			m_snapshot.lastLatencyMs = result.latencyMs;
			m_snapshot.lastGeneratedTokens = result.generatedTokens;
			m_snapshot.lastTokensPerSecond =
				TokensPerSecond(result.generatedTokens, result.latencyMs);
		}

		return result;
	}

	bool LlamaTextGenerationRuntime::Cancel(const std::string& runId) {
		if (runId.empty()) {
			return false;
		}

		std::lock_guard<std::mutex> lock(m_mutex);
		const auto it = m_cancelFlagsByRunId.find(runId);
		if (it == m_cancelFlagsByRunId.end()) {
			return false;
		}

		it->second = true;
		return true;
	}

	bool LlamaTextGenerationRuntime::VerifyDeterministicContract(
		std::string& outFailureReason) {
		outFailureReason.clear();

		const auto baseline = Snapshot();
		if (!baseline.enabled) {
			outFailureReason = "local model disabled";
			return false;
		}

		if (!baseline.ready) {
			outFailureReason = "local llama runtime not ready";
			return false;
		}

		const auto result = GenerateStream(
			TextGenerationRequest{
				.runId = "phase2-llama-det-contract",
				.prompt = "Respond with pong.",
				.maxTokens = 8,
				.temperature = 0.0,
			},
			nullptr);
		if (!result.ok || result.text.empty()) {
			outFailureReason = result.error.has_value() ? result.error->message : "deterministic generation failed";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core::localmodel
