#include "pch.h"
#include "PythonRuntimeDispatcher.h"

#include "EmbeddedPythonRuntimeHost.h"
#include "ExternalPythonRuntimeHost.h"
#include "PythonRuntimeSelector.h"
#include "../GatewayJsonUtils.h"
#include "../Telemetry.h"

#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace blazeclaw::gateway::python {
	namespace {
		struct PolicyGateResult {
			bool allowed = true;
			std::string code;
			std::string message;
		};

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

		bool ParseBool(const std::string& raw, const bool fallback) {
			const std::string lowered = TrimLower(raw);
			if (lowered.empty()) {
				return fallback;
			}

			if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
				return true;
			}

			if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
				return false;
			}

			return fallback;
		}

		std::unordered_set<std::string> ParseCsvSet(const std::string& raw) {
			std::unordered_set<std::string> values;
			std::string token;
			for (const char ch : raw) {
				if (ch == ',' || ch == ';') {
					const std::string normalized = TrimLower(token);
					if (!normalized.empty()) {
						values.insert(normalized);
					}
					token.clear();
					continue;
				}

				token.push_back(ch);
			}

			const std::string normalized = TrimLower(token);
			if (!normalized.empty()) {
				values.insert(normalized);
			}

			return values;
		}

		nlohmann::json ParseArgsObject(const std::optional<std::string>& argsJson) {
			if (!argsJson.has_value()) {
				return nlohmann::json::object();
			}

			try {
				nlohmann::json parsed = nlohmann::json::parse(argsJson.value());
				if (parsed.is_object()) {
					return parsed;
				}
			}
			catch (...) {
			}

			return nlohmann::json::object();
		}

		std::optional<bool> TryGetNestedBool(
			const nlohmann::json& root,
			std::initializer_list<const char*> path) {
			const nlohmann::json* cursor = &root;
			for (const auto* key : path) {
				if (!cursor->is_object() || !cursor->contains(key)) {
					return std::nullopt;
				}

				cursor = &(*cursor)[key];
			}

			if (cursor->is_boolean()) {
				return cursor->get<bool>();
			}

			return std::nullopt;
		}

		std::optional<std::string> TryGetNestedString(
			const nlohmann::json& root,
			std::initializer_list<const char*> path) {
			const nlohmann::json* cursor = &root;
			for (const auto* key : path) {
				if (!cursor->is_object() || !cursor->contains(key)) {
					return std::nullopt;
				}

				cursor = &(*cursor)[key];
			}

			if (cursor->is_string()) {
				const std::string trimmed = blazeclaw::gateway::json::Trim(cursor->get<std::string>());
				if (!trimmed.empty()) {
					return trimmed;
				}
			}

			return std::nullopt;
		}

		void EmitPolicyAuditTelemetry(
			const std::string& tool,
			const std::string& mode,
			const std::string& decision,
			const std::string& code,
			const std::string& reason,
			const bool requiresApproval,
			const bool networkRequested,
			const bool envRequested) {
			const std::string payload =
				"{\"tool\":" + JsonString(tool) +
				",\"runtimeMode\":" + JsonString(mode) +
				",\"decision\":" + JsonString(decision) +
				",\"code\":" + JsonString(code) +
				",\"reason\":" + JsonString(reason) +
				",\"requiresApproval\":" + std::string(requiresApproval ? "true" : "false") +
				",\"networkRequested\":" + std::string(networkRequested ? "true" : "false") +
				",\"envRequested\":" + std::string(envRequested ? "true" : "false") + "}";
			EmitTelemetryEvent("python.runtime.policy.audit", payload);
		}

		PolicyGateResult EvaluatePolicyGate(
			const std::string& requestedTool,
			const std::string& resolvedMode,
			const std::optional<std::string>& argsJson) {
			const nlohmann::json argsRoot = ParseArgsObject(argsJson);

			const bool modeRequiresApproval = ParseBool(
				ReadEnvironmentVariable(
					resolvedMode == "embedded"
					? "BLAZECLAW_PYTHON_EMBEDDED_REQUIRES_APPROVAL"
					: "BLAZECLAW_PYTHON_EXTERNAL_REQUIRES_APPROVAL"),
				false);
			const bool approvalRequested =
				TryGetNestedBool(argsRoot, { "policy", "requiresApproval" }).value_or(false);
			const bool requiresApproval = modeRequiresApproval || approvalRequested;

			const std::string expectedApprovalToken = blazeclaw::gateway::json::Trim(
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_APPROVAL_TOKEN"));
			const std::string approvalToken =
				TryGetNestedString(argsRoot, { "policy", "approvalToken" }).value_or("");

			const bool networkRequested =
				TryGetNestedBool(argsRoot, { "policy", "allowNetwork" }).value_or(false);
			const bool allowNetwork = ParseBool(
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_ALLOW_NETWORK"),
				false);

			const bool envRequested =
				argsRoot.contains("env") && argsRoot["env"].is_object() && !argsRoot["env"].empty();
			const bool allowCustomEnv = ParseBool(
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_ALLOW_CUSTOM_ENV"),
				false);

			const bool denyUnknownOrigins = ParseBool(
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_DENY_UNKNOWN_ORIGINS"),
				true);
			const std::unordered_set<std::string> allowedOrigins = ParseCsvSet(
				ReadEnvironmentVariable("BLAZECLAW_PYTHON_ALLOWED_ORIGINS"));
			const std::string origin = TrimLower(
				TryGetNestedString(argsRoot, { "policy", "origin" }).value_or("unknown"));

			if (requiresApproval && (!expectedApprovalToken.empty() || !approvalToken.empty())) {
				if (expectedApprovalToken.empty() || approvalToken != expectedApprovalToken) {
					EmitPolicyAuditTelemetry(
						requestedTool,
						resolvedMode,
						"blocked",
						"python_approval_required",
						"approval token missing or invalid",
						requiresApproval,
						networkRequested,
						envRequested);
					return PolicyGateResult{
						.allowed = false,
						.code = "python_approval_required",
						.message = "approval token missing or invalid",
					};
				}
			}

			if (networkRequested && !allowNetwork) {
				EmitPolicyAuditTelemetry(
					requestedTool,
					resolvedMode,
					"blocked",
					"python_policy_blocked",
					"network access is disabled by policy",
					requiresApproval,
					networkRequested,
					envRequested);
				return PolicyGateResult{
					.allowed = false,
					.code = "python_policy_blocked",
					.message = "network access is disabled by policy",
				};
			}

			if (envRequested && !allowCustomEnv) {
				EmitPolicyAuditTelemetry(
					requestedTool,
					resolvedMode,
					"blocked",
					"python_policy_blocked",
					"custom env injection is disabled by policy",
					requiresApproval,
					networkRequested,
					envRequested);
				return PolicyGateResult{
					.allowed = false,
					.code = "python_policy_blocked",
					.message = "custom env injection is disabled by policy",
				};
			}

			if (denyUnknownOrigins && !allowedOrigins.empty() &&
				allowedOrigins.find(origin) == allowedOrigins.end()) {
				EmitPolicyAuditTelemetry(
					requestedTool,
					resolvedMode,
					"blocked",
					"python_policy_blocked",
					"untrusted script origin",
					requiresApproval,
					networkRequested,
					envRequested);
				return PolicyGateResult{
					.allowed = false,
					.code = "python_policy_blocked",
					.message = "untrusted script origin",
				};
			}

			EmitPolicyAuditTelemetry(
				requestedTool,
				resolvedMode,
				"allowed",
				"",
				"policy checks passed",
				requiresApproval,
				networkRequested,
				envRequested);

			return PolicyGateResult{};
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
				const PolicyGateResult policy = EvaluatePolicyGate(
					requestedTool,
					"external",
					argsJson);
				if (!policy.allowed) {
					EmitSelectionTelemetry(
						requestedTool,
						selection,
						false,
						"blocked",
						policy.code);
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "blocked",
						.output = BuildErrorEnvelope(
							"external",
							policy.code.empty() ? "python_policy_blocked" : policy.code,
							policy.message.empty() ? "policy blocked request" : policy.message),
					};
				}

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
				const PolicyGateResult policy = EvaluatePolicyGate(
					requestedTool,
					"embedded",
					argsJson);
				if (!policy.allowed) {
					EmitSelectionTelemetry(
						requestedTool,
						selection,
						false,
						"blocked",
						policy.code);
					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = false,
						.status = "blocked",
						.output = BuildErrorEnvelope(
							"embedded",
							policy.code.empty() ? "python_policy_blocked" : policy.code,
							policy.message.empty() ? "policy blocked request" : policy.message),
					};
				}

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
