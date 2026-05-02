#include "pch.h"
#include "PythonProcessExecutor.h"

#include "../GatewayJsonUtils.h"
#include "../Telemetry.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>
#include <vector>
#include <windows.h>

namespace blazeclaw::gateway::executors {
	namespace {

		struct ProcessResult {
			bool launched = false;
			bool timedOut = false;
			bool outputLimitExceeded = false;
			int exitCode = -1;
			std::string output;
		};

		struct PolicyConfig {
			std::unordered_set<std::string> allowedInterpreterNames;
			std::unordered_set<std::string> allowedProfiles;
			std::vector<std::filesystem::path> trustedScriptRoots;
			std::uint64_t defaultTimeoutMs = 30000;
			std::uint64_t maxTimeoutMs = 120000;
			std::size_t defaultMaxOutputBytes = 32768;
			std::size_t maxOutputBytes = 262144;
		};

		std::string EscapeJson(const std::string& value) {
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

		std::string ReadEnvironmentVariable(const char* name) {
			if (name == nullptr) {
				return {};
			}

			char* raw = nullptr;
			size_t size = 0;
			if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
				return {};
			}

			std::string value(raw);
			free(raw);
			return value;
		}

		std::string ToLowerTrimmed(std::string value) {
			value = blazeclaw::gateway::json::Trim(value);
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		std::vector<std::string> SplitValues(const std::string& raw) {
			std::vector<std::string> values;
			std::string token;
			for (const char ch : raw) {
				if (ch == ',' || ch == ';') {
					const std::string trimmed = blazeclaw::gateway::json::Trim(token);
					if (!trimmed.empty()) {
						values.push_back(trimmed);
					}
					token.clear();
					continue;
				}

				token.push_back(ch);
			}

			const std::string trimmed = blazeclaw::gateway::json::Trim(token);
			if (!trimmed.empty()) {
				values.push_back(trimmed);
			}

			return values;
		}

		std::unordered_set<std::string> ResolveAllowedInterpreterNames() {
			const std::string configured =
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_ALLOWED_INTERPRETERS");
			const std::vector<std::string> configuredValues = SplitValues(configured);
			std::unordered_set<std::string> names;
			if (configuredValues.empty()) {
				names.insert("python");
				names.insert("python.exe");
				names.insert("python3");
				names.insert("python3.exe");
				names.insert("py");
				names.insert("py.exe");
				return names;
			}

			for (const auto& value : configuredValues) {
				const std::string lowered = ToLowerTrimmed(value);
				if (!lowered.empty()) {
					names.insert(lowered);
				}
			}

			return names;
		}

		std::unordered_set<std::string> ResolveAllowedProfiles() {
			const std::string configured =
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_ALLOWED_PROFILES");
			std::unordered_set<std::string> profiles;
			for (const auto& value : SplitValues(configured)) {
				const std::string lowered = ToLowerTrimmed(value);
				if (!lowered.empty()) {
					profiles.insert(lowered);
				}
			}
			return profiles;
		}

		std::vector<std::filesystem::path> ResolveTrustedScriptRoots() {
			std::vector<std::filesystem::path> roots;
			const std::string configured =
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_TRUSTED_SCRIPT_DIRS");
			std::vector<std::string> values = SplitValues(configured);
			if (values.empty()) {
				values = {
					"blazeclaw/skills",
					"skills",
					"blazeclaw/skills-bundled",
					"blazeclaw/skills-openclaw-original",
					"skills-openclaw-original"
				};
			}

			auto addCanonical = [&roots](const std::filesystem::path& candidate) {
				std::error_code ec;
				const std::filesystem::path canonical =
					std::filesystem::weakly_canonical(candidate, ec);
				if (ec) {
					return;
				}
				if (std::find(roots.begin(), roots.end(), canonical) == roots.end()) {
					roots.push_back(canonical);
				}
			};

			std::vector<std::filesystem::path> baseDirectories;
			baseDirectories.push_back(std::filesystem::current_path());
			baseDirectories.push_back(std::filesystem::current_path() / "..");
			baseDirectories.push_back(std::filesystem::current_path() / ".." / "..");

			char modulePathBuffer[MAX_PATH] = {};
			const DWORD moduleChars =
				GetModuleFileNameA(nullptr, modulePathBuffer, MAX_PATH);
			if (moduleChars > 0 && moduleChars < MAX_PATH) {
				const std::filesystem::path exeDir =
					std::filesystem::path(modulePathBuffer).parent_path();
				baseDirectories.push_back(exeDir);
				baseDirectories.push_back(exeDir / "..");
				baseDirectories.push_back(exeDir / ".." / "..");
			}

			for (const auto& value : values) {
				const std::filesystem::path configuredPath(value);
				if (configuredPath.is_absolute()) {
					addCanonical(configuredPath);
					continue;
				}

				for (const auto& baseDir : baseDirectories) {
					addCanonical(baseDir / configuredPath);
				}
			}

			return roots;
		}

		bool IsPathWithinRoot(
			const std::filesystem::path& candidate,
			const std::filesystem::path& root) {
			auto candidateText = candidate.string();
			auto rootText = root.string();
			if (!rootText.empty() &&
				rootText.back() != std::filesystem::path::preferred_separator) {
				rootText.push_back(std::filesystem::path::preferred_separator);
			}

			return candidateText.rfind(rootText, 0) == 0;
		}

		std::string BaseNameLower(const std::string& rawPath) {
			const std::filesystem::path path(rawPath);
			return ToLowerTrimmed(path.filename().string());
		}

		std::optional<std::filesystem::path> ResolveScriptPath(const std::string& scriptPath) {
			if (scriptPath.empty()) {
				return std::nullopt;
			}

			std::vector<std::filesystem::path> candidates;
			candidates.emplace_back(scriptPath);
			candidates.emplace_back(std::filesystem::path("..") / scriptPath);
			candidates.emplace_back(std::filesystem::path("..") / ".." / scriptPath);
			candidates.emplace_back(std::filesystem::path("blazeclaw") / scriptPath);
			candidates.emplace_back(std::filesystem::path("..") / "blazeclaw" / scriptPath);

			char modulePathBuffer[MAX_PATH] = {};
			const DWORD moduleChars =
				GetModuleFileNameA(nullptr, modulePathBuffer, MAX_PATH);
			if (moduleChars > 0 && moduleChars < MAX_PATH) {
				const std::filesystem::path exeDir =
					std::filesystem::path(modulePathBuffer).parent_path();
				candidates.emplace_back(exeDir / scriptPath);
				candidates.emplace_back(exeDir / ".." / scriptPath);
				candidates.emplace_back(exeDir / ".." / ".." / scriptPath);
			}

			for (const auto& candidate : candidates) {
				std::error_code ec;
				const auto canonical = std::filesystem::weakly_canonical(candidate, ec);
				if (!ec && std::filesystem::exists(canonical, ec) && !ec) {
					return canonical;
				}
			}

			return std::nullopt;
		}

		std::optional<std::filesystem::path> WriteToolArgsToTempFile(const std::string& argsJson) {
			char tempDirectory[MAX_PATH] = {};
			const DWORD tempDirectoryChars = GetTempPathA(MAX_PATH, tempDirectory);
			if (tempDirectoryChars == 0 || tempDirectoryChars >= MAX_PATH) {
				return std::nullopt;
			}

			char tempFilePath[MAX_PATH] = {};
			if (GetTempFileNameA(tempDirectory, "bcp", 0, tempFilePath) == 0) {
				return std::nullopt;
			}

			std::ofstream output(tempFilePath, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				std::error_code cleanupEc;
				std::filesystem::remove(std::filesystem::path(tempFilePath), cleanupEc);
				return std::nullopt;
			}

			output.write(argsJson.data(), static_cast<std::streamsize>(argsJson.size()));
			output.close();
			if (!output) {
				std::error_code cleanupEc;
				std::filesystem::remove(std::filesystem::path(tempFilePath), cleanupEc);
				return std::nullopt;
			}

			return std::filesystem::path(tempFilePath);
		}

		std::string ResolveExecutablePath(const std::string& interpreter) {
			const std::string trimmed = blazeclaw::gateway::json::Trim(interpreter);
			if (trimmed.empty()) {
				return {};
			}

			const bool directPath =
				trimmed.find('\\') != std::string::npos ||
				trimmed.find('/') != std::string::npos ||
				trimmed.find(':') != std::string::npos;
			if (directPath) {
				std::error_code ec;
				if (std::filesystem::exists(trimmed, ec) && !ec) {
					const auto canonical =
						std::filesystem::weakly_canonical(std::filesystem::path(trimmed), ec);
					if (!ec) {
						return canonical.string();
					}
					return trimmed;
				}
				return {};
			}

			char resolved[MAX_PATH] = {};
			const DWORD chars = SearchPathA(
				nullptr,
				trimmed.c_str(),
				nullptr,
				MAX_PATH,
				resolved,
				nullptr);
			if (chars == 0 || chars >= MAX_PATH) {
				return {};
			}

			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(
				std::filesystem::path(resolved),
				ec);
			if (!ec) {
				return canonical.string();
			}

			return std::string(resolved);
		}

		std::string QuoteArg(const std::string& value) {
			if (value.find_first_of(" \t\"") == std::string::npos) {
				return value;
			}

			std::string escaped = "\"";
			for (const char ch : value) {
				if (ch == '\"') {
					escaped += "\\\"";
					continue;
				}
				escaped.push_back(ch);
			}
			escaped += "\"";
			return escaped;
		}

		ProcessResult RunProcess(
			const std::string& executablePath,
			const std::vector<std::string>& argv,
			const std::string& cwd,
			const std::uint64_t timeoutMs,
			const std::size_t maxOutputBytes,
			const std::optional<std::string>& toolArgsJson = std::nullopt) {
			std::ostringstream command;
			command << QuoteArg(executablePath);
			for (const auto& arg : argv) {
				command << ' ' << QuoteArg(arg);
			}

			SECURITY_ATTRIBUTES saAttr{};
			saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
			saAttr.bInheritHandle = TRUE;
			saAttr.lpSecurityDescriptor = nullptr;

			HANDLE hStdOutRead = nullptr;
			HANDLE hStdOutWrite = nullptr;
			if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &saAttr, 0)) {
				return ProcessResult{};
			}

			if (!SetHandleInformation(hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
				CloseHandle(hStdOutRead);
				CloseHandle(hStdOutWrite);
				return ProcessResult{};
			}

			STARTUPINFOA startup{};
			PROCESS_INFORMATION process{};
			startup.cb = sizeof(STARTUPINFOA);
			startup.hStdError = hStdOutWrite;
			startup.hStdOutput = hStdOutWrite;
			startup.dwFlags |= STARTF_USESTDHANDLES;

			const std::string commandLine = command.str();
			auto commandBuffer = std::make_unique<char[]>(commandLine.size() + 1);
			std::copy(commandLine.begin(), commandLine.end(), commandBuffer.get());
			commandBuffer[commandLine.size()] = '\0';

			std::string previousToolArgsJson;
			bool hadPreviousToolArgsJson = false;
			if (toolArgsJson.has_value()) {
				const DWORD requiredSize = GetEnvironmentVariableA(
					"BLAZECLAW_TOOL_ARGS_JSON",
					nullptr,
					0);
				if (requiredSize > 0) {
					std::string buffer(requiredSize, '\0');
					const DWORD copied = GetEnvironmentVariableA(
						"BLAZECLAW_TOOL_ARGS_JSON",
						buffer.data(),
						requiredSize);
					if (copied > 0 && copied < requiredSize) {
						previousToolArgsJson.assign(buffer.data(), copied);
						hadPreviousToolArgsJson = true;
					}
				}
				SetEnvironmentVariableA("BLAZECLAW_TOOL_ARGS_JSON", toolArgsJson->c_str());
			}

			const BOOL created = CreateProcessA(
				executablePath.c_str(),
				commandBuffer.get(),
				nullptr,
				nullptr,
				TRUE,
				CREATE_NO_WINDOW,
				nullptr,
				cwd.empty() ? nullptr : cwd.c_str(),
				&startup,
				&process);

			if (toolArgsJson.has_value()) {
				if (hadPreviousToolArgsJson) {
					SetEnvironmentVariableA("BLAZECLAW_TOOL_ARGS_JSON", previousToolArgsJson.c_str());
				}
				else {
					SetEnvironmentVariableA("BLAZECLAW_TOOL_ARGS_JSON", nullptr);
				}
			}

			CloseHandle(hStdOutWrite);
			if (!created) {
				CloseHandle(hStdOutRead);
				return ProcessResult{};
			}

			ProcessResult result;
			result.launched = true;

			const DWORD startTick = GetTickCount();
			std::array<char, 4096> buffer{};
			while (true) {
				if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
					DWORD bytesRead = 0;
					while (ReadFile(hStdOutRead, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) &&
						bytesRead > 0) {
						result.output.append(buffer.data(), buffer.data() + bytesRead);
						if (result.output.size() > maxOutputBytes) {
							result.outputLimitExceeded = true;
							TerminateProcess(process.hProcess, 1);
							break;
						}
					}
					break;
				}

				DWORD available = 0;
				if (PeekNamedPipe(hStdOutRead, nullptr, 0, nullptr, &available, nullptr) &&
					available > 0) {
					DWORD bytesRead = 0;
					const DWORD toRead =
						(static_cast<DWORD>((std::min)(std::size_t(available), buffer.size())));
					if (ReadFile(hStdOutRead, buffer.data(), toRead, &bytesRead, nullptr) &&
						bytesRead > 0) {
						result.output.append(buffer.data(), buffer.data() + bytesRead);
						if (result.output.size() > maxOutputBytes) {
							result.outputLimitExceeded = true;
							TerminateProcess(process.hProcess, 1);
							break;
						}
					}
				}

				const DWORD elapsed = GetTickCount() - startTick;
				if (elapsed > timeoutMs) {
					result.timedOut = true;
					TerminateProcess(process.hProcess, 1);
					break;
				}

				Sleep(15);
			}

			DWORD exitCode = 0;
			GetExitCodeProcess(process.hProcess, &exitCode);
			result.exitCode = static_cast<int>(exitCode);

			CloseHandle(hStdOutRead);
			CloseHandle(process.hProcess);
			CloseHandle(process.hThread);
			return result;
		}

		std::string BuildErrorEnvelope(
			const std::string& code,
			const std::string& message) {
			return "{\"protocolVersion\":1,\"ok\":false,\"status\":\"error\","
				"\"runtimeMode\":\"external\",\"output\":[],\"requiresApproval\":null,"
				"\"error\":{\"code\":\"" + EscapeJson(code) +
				"\",\"message\":\"" + EscapeJson(message) + "\"}}";
		}

		std::string BuildSuccessEnvelope(
			const std::string& scriptPath,
			const std::string& output,
			const int exitCode) {
			return "{\"protocolVersion\":1,\"ok\":true,\"status\":\"ok\","
				"\"runtimeMode\":\"external\","
				"\"output\":[{\"scriptPath\":\"" + EscapeJson(scriptPath) +
				"\",\"exitCode\":" + std::to_string(exitCode) +
				",\"stdout\":\"" + EscapeJson(output) + "\"}],"
				"\"requiresApproval\":null}";
		}

		PolicyConfig ResolvePolicyConfig() {
			PolicyConfig config;
			config.allowedInterpreterNames = ResolveAllowedInterpreterNames();
			config.allowedProfiles = ResolveAllowedProfiles();
			config.trustedScriptRoots = ResolveTrustedScriptRoots();
			return config;
		}

		void EmitExecutionTelemetry(
			const std::string& eventName,
			const std::string& tool,
			const std::string& status,
			const std::string& code,
			const bool fallbackUsed) {
			const std::string payload =
				"{\"tool\":" + JsonString(tool) +
				",\"status\":" + JsonString(status) +
				",\"code\":" + JsonString(code) +
				",\"fallbackUsed\":" + std::string(fallbackUsed ? "true" : "false") + "}";
			EmitTelemetryEvent(eventName, payload);
		}

		std::string TruncateForOutput(const std::string& text, const std::size_t maxBytes) {
			if (text.size() <= maxBytes) {
				return text;
			}

			return text.substr(0, maxBytes);
		}

	} // namespace

	GatewayToolRegistry::RuntimeToolExecutor PythonProcessExecutor::Create() {
		return [](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
			const PolicyConfig policy = ResolvePolicyConfig();
			if (!argsJson.has_value()) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"invalid_args",
					"missing_args",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "invalid_args",
					.output = BuildErrorEnvelope("missing_args", "python args payload is required"),
				};
			}

			nlohmann::json args;
			try {
				args = nlohmann::json::parse(argsJson.value());
			}
			catch (...) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"invalid_args",
					"args_parse_failed",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "invalid_args",
					.output = BuildErrorEnvelope("args_parse_failed", "invalid args JSON"),
				};
			}

			if (!args.is_object()) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"invalid_args",
					"args_not_object",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "invalid_args",
					.output = BuildErrorEnvelope("args_not_object", "args must be an object"),
				};
			}

			std::string interpreter = args.value("interpreter", std::string("python3"));
			const bool interpreterProvided =
				args.contains("interpreter") &&
				args["interpreter"].is_string() &&
				!blazeclaw::gateway::json::Trim(args["interpreter"].get<std::string>()).empty();
			std::string scriptPath = blazeclaw::gateway::json::Trim(args.value("scriptPath", std::string{}));
			const std::string profile = ToLowerTrimmed(args.value("profile", std::string{}));
			const std::string requestedCwd = blazeclaw::gateway::json::Trim(args.value("cwd", std::string{}));
			const std::uint64_t requestedTimeout = args.value("timeoutMs", policy.defaultTimeoutMs);
			const std::size_t requestedMaxOutput = args.value("maxOutputBytes", policy.defaultMaxOutputBytes);
			const std::uint64_t timeoutMs = (std::min)(requestedTimeout, policy.maxTimeoutMs);
			const std::size_t maxOutputBytes = (std::min)(requestedMaxOutput, policy.maxOutputBytes);

			if (scriptPath.empty() && requestedTool == "pdf_generator.generate") {
				scriptPath = "skills-openclaw-original/pdf-generator/scripts/pdf_generator_bridge.py";
				if (!args.contains("input_md") && args.contains("input")) {
					args["input_md"] = args["input"];
				}
				if (!args.contains("output_pdf") && args.contains("output")) {
					args["output_pdf"] = args["output"];
				}
			}

			if (scriptPath.empty()) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"invalid_args",
					"script_path_required",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "invalid_args",
					.output = BuildErrorEnvelope("script_path_required", "scriptPath is required"),
				};
			}

			if (!policy.allowedProfiles.empty()) {
				if (profile.empty() || policy.allowedProfiles.find(profile) == policy.allowedProfiles.end()) {
					EmitExecutionTelemetry(
						"python.external.execute.complete",
						requestedTool,
						"blocked",
						"python_policy_blocked",
						false);
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "blocked",
						.output = BuildErrorEnvelope(
							"python_policy_blocked",
							"requested profile is not allowed"),
					};
				}
			}

			std::string resolvedInterpreterPath = ResolveExecutablePath(interpreter);
			if (resolvedInterpreterPath.empty() && !interpreterProvided) {
				for (const auto& candidate : { "python", "python.exe", "py", "py.exe", "python3", "python3.exe" }) {
					resolvedInterpreterPath = ResolveExecutablePath(candidate);
					if (!resolvedInterpreterPath.empty()) {
						interpreter = candidate;
						break;
					}
				}
			}
			if (resolvedInterpreterPath.empty()) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"error",
					"python_external_interpreter_not_found",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope(
						"python_external_interpreter_not_found",
						interpreter),
				};
			}

			const std::string interpreterName = BaseNameLower(resolvedInterpreterPath);
			if (policy.allowedInterpreterNames.find(interpreterName) == policy.allowedInterpreterNames.end()) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"blocked",
					"python_policy_blocked",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "blocked",
					.output = BuildErrorEnvelope(
						"python_policy_blocked",
						"interpreter is not in allowed list"),
				};
			}

			const std::optional<std::filesystem::path> resolvedScript = ResolveScriptPath(scriptPath);
			if (!resolvedScript.has_value()) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"invalid_args",
					"script_not_found",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "invalid_args",
					.output = BuildErrorEnvelope("script_not_found", scriptPath),
				};
			}
			const std::filesystem::path canonicalScript = resolvedScript.value();

			bool trustedScript = false;
			for (const auto& root : policy.trustedScriptRoots) {
				if (IsPathWithinRoot(canonicalScript, root)) {
					trustedScript = true;
					break;
				}
			}

			if (!trustedScript) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"blocked",
					"python_policy_blocked",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "blocked",
					.output = BuildErrorEnvelope(
						"python_policy_blocked",
						"script path is outside trusted roots"),
				};
			}

			std::vector<std::string> argv;
			std::optional<std::string> toolArgsJsonForEnvironment;
			std::optional<std::filesystem::path> toolArgsJsonTempFile;
			argv.push_back(canonicalScript.string());
			if (requestedTool == "pdf_generator.generate") {
				args["tool"] = requestedTool;
				const std::string serializedArgs = args.dump(-1, ' ', true);
				toolArgsJsonTempFile = WriteToolArgsToTempFile(serializedArgs);
				if (toolArgsJsonTempFile.has_value()) {
					argv.push_back("@" + toolArgsJsonTempFile->string());
				}
				else {
					toolArgsJsonForEnvironment = serializedArgs;
					argv.push_back("{}");
				}
			}
			else if (args.contains("args") && args["args"].is_array()) {
				for (const auto& item : args["args"]) {
					if (item.is_string()) {
						argv.push_back(item.get<std::string>());
					}
				}
			}

			const std::string cwd = requestedCwd.empty()
				? canonicalScript.parent_path().string()
				: requestedCwd;

			EmitExecutionTelemetry(
				"python.external.execute.start",
				requestedTool,
				"start",
				"",
				false);

			const ProcessResult runResult = RunProcess(
				resolvedInterpreterPath,
				argv,
				cwd,
				timeoutMs,
				maxOutputBytes,
				toolArgsJsonForEnvironment);
			if (toolArgsJsonTempFile.has_value()) {
				std::error_code cleanupEc;
				std::filesystem::remove(toolArgsJsonTempFile.value(), cleanupEc);
			}
			if (!runResult.launched) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"error",
					"process_launch_failed",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope("process_launch_failed", resolvedInterpreterPath),
				};
			}

			if (runResult.timedOut) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"error",
					"python_execution_timeout",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope("python_execution_timeout", canonicalScript.string()),
				};
			}

			if (runResult.outputLimitExceeded) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"error",
					"python_output_limit_exceeded",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope("python_output_limit_exceeded", canonicalScript.string()),
				};
			}

			if (runResult.exitCode != 0) {
				EmitExecutionTelemetry(
					"python.external.execute.complete",
					requestedTool,
					"error",
					"python_process_failed",
					false);
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope(
						"python_process_failed",
						TruncateForOutput(runResult.output, maxOutputBytes)),
				};
			}

			EmitExecutionTelemetry(
				"python.external.execute.complete",
				requestedTool,
				"ok",
				"",
				false);

			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = true,
				.status = "ok",
				.output = BuildSuccessEnvelope(
					canonicalScript.string(),
					TruncateForOutput(runResult.output, maxOutputBytes),
					runResult.exitCode),
			};
			};
	}

} // namespace blazeclaw::gateway::executors
