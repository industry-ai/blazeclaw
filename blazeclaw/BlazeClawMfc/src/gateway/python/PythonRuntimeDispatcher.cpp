#include "pch.h"
#include "PythonRuntimeDispatcher.h"

#include "EmbeddedPythonRuntimeHost.h"
#include "ExternalPythonRuntimeHost.h"
#include "PythonRuntimeSelector.h"
#include "../Telemetry.h"

namespace blazeclaw::gateway::python {
	namespace {
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

		std::string BuildErrorEnvelope(
			const std::string& mode,
			const std::string& code,
			const std::string& message) {
			return "{\"protocolVersion\":1,\"ok\":false,\"status\":\"error\","
				"\"runtimeMode\":\"" + EscapeJson(mode) +
				"\",\"output\":[],\"requiresApproval\":null,\"error\":{"
				"\"code\":\"" + EscapeJson(code) +
				"\",\"message\":\"" + EscapeJson(message) + "\"}}";
		}

		ToolExecuteResult BuildSelectionFailure(
			const std::string& requestedTool,
			const PythonRuntimeSelection& selection) {
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "error",
				.output = BuildErrorEnvelope(
					"unknown",
					selection.errorCode.empty()
						? "python_runtime_mode_unresolved"
						: selection.errorCode,
					selection.errorMessage.empty()
						? "runtime mode selection failed"
						: selection.errorMessage),
			};
		}

		void EmitSelectionTelemetry(
			const std::string& tool,
			const PythonRuntimeSelection& selection,
			const bool fallbackUsed,
			const std::string& status,
			const std::string& code) {
			const std::string payload =
				"{\"tool\":" + JsonString(tool) +
				",\"mode\":" + JsonString(selection.mode) +
				",\"modeSource\":" + JsonString(selection.modeSource) +
				",\"strictMode\":" +
				std::string(selection.strictMode ? "true" : "false") +
				",\"allowFallbackToExternal\":" +
				std::string(selection.allowFallbackToExternal ? "true" : "false") +
				",\"fallbackUsed\":" +
				std::string(fallbackUsed ? "true" : "false") +
				",\"status\":" + JsonString(status) +
				",\"code\":" + JsonString(code) + "}";
			EmitTelemetryEvent("python.runtime.selection", payload);
		}
	}

	GatewayToolRegistry::RuntimeToolExecutor PythonRuntimeDispatcher::CreateExecutor() {
		return [](const std::string& requestedTool, const std::optional<std::string>& argsJson) {
			PythonRuntimeSelector selector;
			const PythonRuntimeSelection selection = selector.Resolve(argsJson);
			if (!selection.resolved) {
				EmitSelectionTelemetry(
					requestedTool,
					selection,
					false,
					"error",
					selection.errorCode);
				return BuildSelectionFailure(requestedTool, selection);
			}

			ExternalPythonRuntimeHost externalHost;
			EmbeddedPythonRuntimeHost embeddedHost;

			if (selection.mode == "external") {
				const ToolExecuteResult result =
					externalHost.Execute(requestedTool, argsJson);
				EmitSelectionTelemetry(
					requestedTool,
					selection,
					false,
					result.status,
					"");
				return result;
			}

			if (selection.mode == "embedded") {
				if (embeddedHost.IsAvailable()) {
					const ToolExecuteResult result =
						embeddedHost.Execute(requestedTool, argsJson);
					EmitSelectionTelemetry(
						requestedTool,
						selection,
						false,
						result.status,
						"");
					return result;
				}

				if (selection.allowFallbackToExternal && externalHost.IsAvailable()) {
					const ToolExecuteResult result =
						externalHost.Execute(requestedTool, argsJson);
					EmitSelectionTelemetry(
						requestedTool,
						selection,
						true,
						result.status,
						"embedded_unavailable_fallback_external");
					return result;
				}

				EmitSelectionTelemetry(
					requestedTool,
					selection,
					false,
					"error",
					"python_embedded_runtime_unavailable");
				return ToolExecuteResult{
					.tool = requestedTool,
					.executed = false,
					.status = "error",
					.output = BuildErrorEnvelope(
						"embedded",
						"python_embedded_runtime_unavailable",
						"embedded runtime unavailable and no allowed fallback"),
				};
			}

			EmitSelectionTelemetry(
				requestedTool,
				selection,
				false,
				"error",
				"python_runtime_mode_invalid");
			return ToolExecuteResult{
				.tool = requestedTool,
				.executed = false,
				.status = "error",
				.output = BuildErrorEnvelope(
					selection.mode,
					"python_runtime_mode_invalid",
					selection.mode),
			};
			};
	}

} // namespace blazeclaw::gateway::python
