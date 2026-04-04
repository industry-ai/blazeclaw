#include "pch.h"
#include "EmbeddedPythonRuntimeHost.h"

#include "../GatewayJsonUtils.h"
#include "../Telemetry.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <windows.h>

namespace blazeclaw::gateway::python {

	namespace {
		struct _object;
		using PyObject = _object;
		using PyGILState_STATE = int;

		using FnPyInitializeEx = void(*)(int);
		using FnPyIsInitialized = int(*)();
		using FnPyGILStateEnsure = PyGILState_STATE(*)();
		using FnPyGILStateRelease = void(*)(PyGILState_STATE);
		using FnPyRunSimpleString = int(*)(const char*);

		struct EmbeddedPythonApi {
			HMODULE module = nullptr;
			FnPyInitializeEx Py_InitializeEx = nullptr;
			FnPyIsInitialized Py_IsInitialized = nullptr;
			FnPyGILStateEnsure PyGILState_Ensure = nullptr;
			FnPyGILStateRelease PyGILState_Release = nullptr;
			FnPyRunSimpleString PyRun_SimpleString = nullptr;
			bool loaded = false;
			bool initialized = false;
			std::string lastError;
			std::mutex guard;
		};

		EmbeddedPythonApi& SharedApi() {
			static EmbeddedPythonApi api;
			return api;
		}

		std::mutex& SharedExecutionMutex() {
			static std::mutex executionMutex;
			return executionMutex;
		}

		std::string EscapeJson(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);
			for (const char ch : value) {
				switch (ch) {
				case '\\':
					escaped += "\\\\";
					break;
				case '"':
					escaped += "\\\"";
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

		std::string TrimLower(std::string value) {
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

		std::string BuildErrorEnvelope(
			const std::string& code,
			const std::string& message) {
			return "{\"protocolVersion\":1,\"ok\":false,\"status\":\"error\","
				"\"runtimeMode\":\"embedded\",\"output\":[],\"requiresApproval\":null,"
				"\"error\":{\"code\":\"" + EscapeJson(code) +
				"\",\"message\":\"" + EscapeJson(message) + "\"}}";
		}

		std::string BuildSuccessEnvelope(
			const std::string& scriptPath,
			const std::string& stdoutText,
			const std::string& stderrText) {
			return "{\"protocolVersion\":1,\"ok\":true,\"status\":\"ok\","
				"\"runtimeMode\":\"embedded\",\"output\":[{\"scriptPath\":\"" +
				EscapeJson(scriptPath) +
				"\",\"stdout\":\"" + EscapeJson(stdoutText) +
				"\",\"stderr\":\"" + EscapeJson(stderrText) +
				"\"}],\"requiresApproval\":null}";
		}

		void EmitEmbeddedTelemetry(
			const std::string& status,
			const std::string& code) {
			const std::string payload =
				"{\"runtimeMode\":\"embedded\",\"status\":" +
				JsonString(status) +
				",\"code\":" + JsonString(code) + "}";
			EmitTelemetryEvent("python.embedded.execute", payload);
		}

		bool ResolveFunctionPointers(EmbeddedPythonApi& api) {
			api.Py_InitializeEx = reinterpret_cast<FnPyInitializeEx>(
				GetProcAddress(api.module, "Py_InitializeEx"));
			api.Py_IsInitialized = reinterpret_cast<FnPyIsInitialized>(
				GetProcAddress(api.module, "Py_IsInitialized"));
			api.PyGILState_Ensure = reinterpret_cast<FnPyGILStateEnsure>(
				GetProcAddress(api.module, "PyGILState_Ensure"));
			api.PyGILState_Release = reinterpret_cast<FnPyGILStateRelease>(
				GetProcAddress(api.module, "PyGILState_Release"));
			api.PyRun_SimpleString = reinterpret_cast<FnPyRunSimpleString>(
				GetProcAddress(api.module, "PyRun_SimpleString"));

			return api.Py_InitializeEx != nullptr &&
				api.Py_IsInitialized != nullptr &&
				api.PyGILState_Ensure != nullptr &&
				api.PyGILState_Release != nullptr &&
				api.PyRun_SimpleString != nullptr;
		}

		bool EnsureRuntimeLoaded(std::string& error) {
			auto& api = SharedApi();
			std::lock_guard<std::mutex> lock(api.guard);
			if (api.loaded) {
				error.clear();
				return true;
			}

			std::vector<std::string> candidates;
			const std::string explicitDll =
				blazeclaw::gateway::json::Trim(
					ReadEnvironmentVariable("BLAZECLAW_PYTHON_EMBEDDED_DLL"));
			if (!explicitDll.empty()) {
				candidates.push_back(explicitDll);
			}

			candidates.push_back("python313.dll");
			candidates.push_back("python312.dll");
			candidates.push_back("python311.dll");
			candidates.push_back("python310.dll");

			for (const auto& candidate : candidates) {
				HMODULE module = LoadLibraryA(candidate.c_str());
				if (module == nullptr) {
					continue;
				}

				api.module = module;
				if (!ResolveFunctionPointers(api)) {
					FreeLibrary(module);
					api.module = nullptr;
					continue;
				}

				api.loaded = true;
				api.lastError.clear();
				error.clear();
				return true;
			}

			api.lastError = "python_embedded_runtime_unavailable";
			error = api.lastError;
			return false;
		}

		bool EnsureInterpreterInitialized(std::string& error) {
			if (!EnsureRuntimeLoaded(error)) {
				return false;
			}

			auto& api = SharedApi();
			std::lock_guard<std::mutex> lock(api.guard);
			if (api.initialized) {
				error.clear();
				return true;
			}

			api.Py_InitializeEx(0);
			if (api.Py_IsInitialized() == 0) {
				api.lastError = "python_embedded_init_failed";
				error = api.lastError;
				return false;
			}

			api.initialized = true;
			api.lastError.clear();
			error.clear();
			return true;
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
					"blazeclaw/skills-bundled"
				};
			}

			for (const auto& value : values) {
				std::error_code ec;
				const std::filesystem::path canonical =
					std::filesystem::weakly_canonical(std::filesystem::path(value), ec);
				if (!ec) {
					roots.push_back(canonical);
				}
			}

			return roots;
		}

		std::vector<std::filesystem::path> ResolveEmbeddedSysPath(
			const std::filesystem::path& scriptPath) {
			std::vector<std::filesystem::path> paths;
			paths.push_back(scriptPath.parent_path());

			const std::string configured =
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_EMBEDDED_SYS_PATH");
			for (const auto& value : SplitValues(configured)) {
				std::error_code ec;
				const std::filesystem::path canonical =
					std::filesystem::weakly_canonical(std::filesystem::path(value), ec);
				if (!ec) {
					paths.push_back(canonical);
				}
			}

			for (const auto& trustedRoot : ResolveTrustedScriptRoots()) {
				paths.push_back(trustedRoot);
			}

			std::sort(paths.begin(), paths.end());
			paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
			return paths;
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

		std::string EscapePythonStringLiteral(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);
			for (const char ch : value) {
				if (ch == '\\') {
					escaped += "\\\\";
					continue;
				}
				if (ch == '\'') {
					escaped += "\\'";
					continue;
				}
				escaped.push_back(ch);
			}
			return escaped;
		}

		std::string ReadTextFile(const std::filesystem::path& path) {
			std::ifstream input(path, std::ios::binary);
			if (!input.is_open()) {
				return {};
			}

			std::string content(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			return content;
		}

		std::unordered_set<std::string> ResolveAllowedImports() {
			std::unordered_set<std::string> imports;
			const std::string configured =
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_EMBEDDED_ALLOWED_IMPORTS");
			for (const auto& value : SplitValues(configured)) {
				const std::string lowered = TrimLower(value);
				if (!lowered.empty()) {
					imports.insert(lowered);
				}
			}
			return imports;
		}

		std::string FirstBlockedImport(
			const std::string& scriptText,
			const std::unordered_set<std::string>& allowedImports) {
			if (allowedImports.empty()) {
				return {};
			}

			auto tokenAllowed = [&](const std::string& token) {
				if (token.empty()) {
					return true;
				}
				const std::string root = token.substr(0, token.find('.'));
				return allowedImports.find(root) != allowedImports.end();
				};

			std::istringstream stream(scriptText);
			std::string line;
			while (std::getline(stream, line)) {
				const std::string trimmed = blazeclaw::gateway::json::Trim(line);
				if (trimmed.rfind("import ", 0) == 0) {
					std::string list = trimmed.substr(7);
					for (auto& ch : list) {
						if (ch == ',') {
							ch = ' ';
						}
					}
					std::istringstream imports(list);
					std::string item;
					while (imports >> item) {
						if (item == "as") {
							imports >> item;
							continue;
						}
						const std::string token =
							TrimLower(item.substr(0, item.find('.')));
						if (!tokenAllowed(token)) {
							return token;
						}
					}
				}

				if (trimmed.rfind("from ", 0) == 0) {
					const std::size_t importPos = trimmed.find(" import ");
					if (importPos == std::string::npos) {
						continue;
					}
					const std::string module = TrimLower(
						trimmed.substr(5, importPos - 5));
					if (!tokenAllowed(module)) {
						return module;
					}
				}
			}

			return {};
		}

		std::string CreateTempFilePath(const char* prefix) {
			char tempPath[MAX_PATH] = {};
			if (GetTempPathA(MAX_PATH, tempPath) == 0) {
				return {};
			}

			char tempFile[MAX_PATH] = {};
			if (GetTempFileNameA(tempPath, prefix, 0, tempFile) == 0) {
				return {};
			}

			return std::string(tempFile);
		}

		std::string BuildEmbeddedRunnerScript(
			const std::filesystem::path& scriptPath,
			const std::vector<std::filesystem::path>& sysPathEntries,
			const std::unordered_set<std::string>& allowedImports,
			const std::string& stdoutPath,
			const std::string& stderrPath,
			const std::string& statusPath) {
			std::string command;
			command += "import builtins, io, runpy, sys, traceback\n";
			command += "sys.path = [";
			for (std::size_t i = 0; i < sysPathEntries.size(); ++i) {
				if (i > 0) {
					command += ",";
				}
				command += "'";
				command += EscapePythonStringLiteral(sysPathEntries[i].string());
				command += "'";
			}
			command += "]\n";

			if (!allowedImports.empty()) {
				command += "_bc_allowed_imports = {";
				std::vector<std::string> orderedImports(
					allowedImports.begin(),
					allowedImports.end());
				std::sort(orderedImports.begin(), orderedImports.end());
				for (std::size_t i = 0; i < orderedImports.size(); ++i) {
					if (i > 0) {
						command += ",";
					}
					command += "'";
					command += EscapePythonStringLiteral(orderedImports[i]);
					command += "'";
				}
				command += "}\n";
				command += "_bc_orig_import = builtins.__import__\n";
				command += "def _bc_guarded_import(name, globals=None, locals=None, fromlist=(), level=0):\n";
				command += "    root = name.split('.', 1)[0]\n";
				command += "    if root not in _bc_allowed_imports:\n";
				command += "        raise ImportError('blocked import: ' + root)\n";
				command += "    return _bc_orig_import(name, globals, locals, fromlist, level)\n";
				command += "builtins.__import__ = _bc_guarded_import\n";
			}

			command += "_bc_stdout = io.StringIO()\n";
			command += "_bc_stderr = io.StringIO()\n";
			command += "_bc_old_out = sys.stdout\n";
			command += "_bc_old_err = sys.stderr\n";
			command += "_bc_status = 'ok'\n";
			command += "sys.stdout = _bc_stdout\n";
			command += "sys.stderr = _bc_stderr\n";
			command += "try:\n";
			command += "    runpy.run_path('";
			command += EscapePythonStringLiteral(scriptPath.string());
			command += "', run_name='__main__')\n";
			command += "except Exception:\n";
			command += "    _bc_status = 'error'\n";
			command += "    traceback.print_exc(file=_bc_stderr)\n";
			command += "finally:\n";
			command += "    sys.stdout = _bc_old_out\n";
			command += "    sys.stderr = _bc_old_err\n";
			command += "open('";
			command += EscapePythonStringLiteral(stdoutPath);
			command += "', 'w', encoding='utf-8').write(_bc_stdout.getvalue())\n";
			command += "open('";
			command += EscapePythonStringLiteral(stderrPath);
			command += "', 'w', encoding='utf-8').write(_bc_stderr.getvalue())\n";
			command += "open('";
			command += EscapePythonStringLiteral(statusPath);
			command += "', 'w', encoding='utf-8').write(_bc_status)\n";

			return command;
		}

		void RemoveBestEffort(const std::string& path) {
			if (path.empty()) {
				return;
			}
			std::error_code ec;
			std::filesystem::remove(std::filesystem::path(path), ec);
		}
	}

	const char* EmbeddedPythonRuntimeHost::ModeName() const {
		return "embedded";
	}

	bool EmbeddedPythonRuntimeHost::IsAvailable() const {
		std::string error;
		return EnsureRuntimeLoaded(error);
	}

	ToolExecuteResult EmbeddedPythonRuntimeHost::Execute(
		const std::string& requestedTool,
		const std::optional<std::string>& argsJson) {
		if (!argsJson.has_value()) {
			EmitEmbeddedTelemetry("invalid_args", "missing_args");
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
			EmitEmbeddedTelemetry("invalid_args", "args_parse_failed");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "invalid_args",
				.output = BuildErrorEnvelope("args_parse_failed", "invalid args JSON"),
			};
		}

		if (!args.is_object()) {
			EmitEmbeddedTelemetry("invalid_args", "args_not_object");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "invalid_args",
				.output = BuildErrorEnvelope("args_not_object", "args must be an object"),
			};
		}

		const std::string rawScriptPath =
			blazeclaw::gateway::json::Trim(args.value("scriptPath", std::string{}));
		if (rawScriptPath.empty()) {
			EmitEmbeddedTelemetry("invalid_args", "script_path_required");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "invalid_args",
				.output = BuildErrorEnvelope("script_path_required", "scriptPath is required"),
			};
		}

		std::error_code ec;
		const std::filesystem::path canonicalScript =
			std::filesystem::weakly_canonical(std::filesystem::path(rawScriptPath), ec);
		if (ec || !std::filesystem::exists(canonicalScript, ec) || ec) {
			EmitEmbeddedTelemetry("invalid_args", "script_not_found");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "invalid_args",
				.output = BuildErrorEnvelope("script_not_found", rawScriptPath),
			};
		}

		bool trustedScript = false;
		for (const auto& root : ResolveTrustedScriptRoots()) {
			if (IsPathWithinRoot(canonicalScript, root)) {
				trustedScript = true;
				break;
			}
		}

		if (!trustedScript) {
			EmitEmbeddedTelemetry("blocked", "python_policy_blocked");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "blocked",
				.output = BuildErrorEnvelope(
					"python_policy_blocked",
					"script path is outside trusted roots"),
			};
		}

		const std::string scriptText = ReadTextFile(canonicalScript);
		if (scriptText.empty()) {
			EmitEmbeddedTelemetry("invalid_args", "script_read_failed");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "invalid_args",
				.output = BuildErrorEnvelope("script_read_failed", canonicalScript.string()),
			};
		}

		const std::unordered_set<std::string> allowedImports =
			ResolveAllowedImports();
		const std::string blockedImport =
			FirstBlockedImport(scriptText, allowedImports);
		if (!blockedImport.empty()) {
			EmitEmbeddedTelemetry("blocked", "python_policy_blocked");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "blocked",
				.output = BuildErrorEnvelope(
					"python_policy_blocked",
					"blocked import: " + blockedImport),
			};
		}

		std::string initError;
		if (!EnsureInterpreterInitialized(initError)) {
			EmitEmbeddedTelemetry("error", initError);
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "error",
				.output = BuildErrorEnvelope(
					initError.empty()
						? "python_embedded_init_failed"
						: initError,
					"failed to initialize embedded runtime"),
			};
		}

		const std::string stdoutPath = CreateTempFilePath("bcs");
		const std::string stderrPath = CreateTempFilePath("bce");
		const std::string statusPath = CreateTempFilePath("bcp");
		if (stdoutPath.empty() || stderrPath.empty() || statusPath.empty()) {
			EmitEmbeddedTelemetry("error", "temp_file_create_failed");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "error",
				.output = BuildErrorEnvelope(
					"temp_file_create_failed",
					"failed to create embedded runtime temp files"),
			};
		}

		const std::vector<std::filesystem::path> sysPathEntries =
			ResolveEmbeddedSysPath(canonicalScript);
		const std::string runnerScript = BuildEmbeddedRunnerScript(
			canonicalScript,
			sysPathEntries,
			allowedImports,
			stdoutPath,
			stderrPath,
			statusPath);

		auto& api = SharedApi();
		int runCode = -1;
		{
			std::lock_guard<std::mutex> executionLock(SharedExecutionMutex());
			const PyGILState_STATE state = api.PyGILState_Ensure();
			runCode = api.PyRun_SimpleString(runnerScript.c_str());
			api.PyGILState_Release(state);
		}

		const std::string stdoutText = ReadTextFile(stdoutPath);
		const std::string stderrText = ReadTextFile(stderrPath);
		const std::string statusText = TrimLower(ReadTextFile(statusPath));
		RemoveBestEffort(stdoutPath);
		RemoveBestEffort(stderrPath);
		RemoveBestEffort(statusPath);

		if (runCode != 0) {
			EmitEmbeddedTelemetry("error", "python_embedded_exec_failed");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "error",
				.output = BuildErrorEnvelope(
					"python_embedded_exec_failed",
					stderrText.empty() ? "embedded execution failed" : stderrText),
			};
		}

		if (statusText == "error") {
			EmitEmbeddedTelemetry("error", "python_embedded_script_error");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "error",
				.output = BuildErrorEnvelope(
					"python_embedded_script_error",
					stderrText.empty() ? "embedded script raised exception" : stderrText),
			};
		}

		EmitEmbeddedTelemetry("ok", "");
		return ToolExecuteResult{
			.tool = requestedTool,
			.executed = true,
			.status = "ok",
			.output = BuildSuccessEnvelope(canonicalScript.string(), stdoutText, stderrText),
		};
	}

} // namespace blazeclaw::gateway::python
