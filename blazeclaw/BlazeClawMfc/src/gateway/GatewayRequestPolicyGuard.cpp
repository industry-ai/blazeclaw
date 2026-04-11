#include "pch.h"
#include "GatewayRequestPolicyGuard.h"

#include "GatewayJsonUtils.h"

namespace blazeclaw::gateway {

	namespace {
		bool IsReadOnlyMethod(const std::string& method) {
			return method == "gateway.transport.status" ||
				method == "gateway.transport.connections.count" ||
				method == "gateway.transport.endpoint.get" ||
				method == "gateway.transport.endpoint.exists" ||
				method == "gateway.transport.endpoints.list" ||
				method == "gateway.transport.policy.get" ||
				method == "gateway.transport.policy.status" ||
				method == "gateway.transport.policy.validate" ||
				method == "gateway.transport.policy.history" ||
				method == "gateway.transport.policy.metrics" ||
				method == "gateway.transport.policy.export" ||
				method == "gateway.transport.policy.digest";
		}

		bool IsControlPlaneWriteMethod(const std::string& method) {
			return method == "gateway.transport.endpoint.set" ||
				method == "gateway.transport.policy.set" ||
				method == "gateway.transport.policy.reset" ||
				method == "gateway.transport.policy.import" ||
				method == "gateway.transport.policy.preview" ||
				method == "gateway.transport.policy.commit";
		}

		std::string NormalizeScope(const std::optional<std::string>& paramsJson) {
			if (!paramsJson.has_value()) {
				return "runtime";
			}

			std::string scope;
			if (!json::FindStringField(paramsJson.value(), "scope", scope)) {
				return "runtime";
			}

			scope = json::Trim(scope);
			return scope.empty() ? std::string("runtime") : scope;
		}

		std::optional<protocol::ErrorShape> BuildPolicyError(
			const std::string& code,
			const std::string& message,
			const std::string& method,
			const std::string& details) {
			return protocol::ErrorShape{
				.code = code,
				.message = message,
				.detailsJson = std::string("{\"method\":\"") + method +
				"\",\"details\":\"" + details + "\"}",
				.retryable = false,
				.retryAfterMs = std::nullopt,
			};
		}
	}

	std::optional<protocol::ErrorShape> GatewayRequestPolicyGuard::Evaluate(
		const protocol::RequestFrame& request,
		const Context& context) const {
		if (!context.dispatchInitialized) {
			return BuildPolicyError(
				"service_unavailable",
				"Gateway dispatch is not initialized.",
				request.method,
				"dispatch_uninitialized");
		}

		if (request.method.empty()) {
			return BuildPolicyError(
				"invalid_method",
				"Request method cannot be empty.",
				request.method,
				"method_empty");
		}

		if (IsControlPlaneWriteMethod(request.method) && !context.hostRunning) {
			return BuildPolicyError(
				"method_unavailable",
				"Method unavailable before runtime start.",
				request.method,
				"startup_gated");
		}

		if (request.method == "chat.send") {
			const std::string scope = NormalizeScope(request.paramsJson);
			if (scope != "runtime" && scope != "chat") {
				return BuildPolicyError(
					"insufficient_scope",
					"chat.send requires runtime/chat scope.",
					request.method,
					"scope_denied");
			}

			std::uint64_t requestedWriteBudget = 1;
			if (request.paramsJson.has_value()) {
				json::FindUInt64Field(
					request.paramsJson.value(),
					"writeBudget",
					requestedWriteBudget);
			}
			if (requestedWriteBudget > 128) {
				return BuildPolicyError(
					"rate_limited",
					"writeBudget exceeds transport dispatch budget.",
					request.method,
					"write_budget_exceeded");
			}
		}

		if (request.method == "gateway.transport.status" || IsReadOnlyMethod(request.method)) {
			return std::nullopt;
		}

		if (request.method.rfind("gateway.transport.", 0) == 0) {
			const std::string scope = NormalizeScope(request.paramsJson);
			if (scope != "transport" && scope != "admin") {
				return BuildPolicyError(
					"insufficient_scope",
					"Transport control-plane methods require transport/admin scope.",
					request.method,
					"transport_scope_required");
			}
		}

		return std::nullopt;
	}

} // namespace blazeclaw::gateway
