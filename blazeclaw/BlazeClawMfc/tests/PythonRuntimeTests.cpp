#include "gateway/GatewayHost.h"
#include "gateway/GatewayJsonUtils.h"
#include "config/ConfigModels.h"
#include "gateway/python/PythonRuntimeSelector.h"

#include <catch2/catch_all.hpp>
#include <optional>
#include <string>
#include <vector>

namespace {
	class ScopedEnvVar {
	public:
		explicit ScopedEnvVar(std::string name)
			: m_name(std::move(name)) {
			char* raw = nullptr;
			size_t len = 0;
			_dupenv_s(&raw, &len, m_name.c_str());
			if (raw != nullptr) {
				m_previous = std::string(raw);
				free(raw);
			}
		}

		~ScopedEnvVar() {
			if (m_previous.has_value()) {
				_putenv_s(m_name.c_str(), m_previous->c_str());
				return;
			}

			_putenv_s(m_name.c_str(), "");
		}

		void Set(const std::string& value) {
			_putenv_s(m_name.c_str(), value.c_str());
		}

	private:
		std::string m_name;
		std::optional<std::string> m_previous;
	};

	blazeclaw::gateway::protocol::ResponseFrame ExecuteTool(
		blazeclaw::gateway::GatewayHost& host,
		const std::string& tool,
		const std::string& argsObjectJson) {
		const std::string params =
			"{\"tool\":\"" + tool + "\",\"args\":" + argsObjectJson + "}";
		return host.RouteRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "python-runtime-tests",
				.method = "gateway.tools.call.execute",
				.paramsJson = params,
			});
	}

	std::string ExtractToolOutput(
		const blazeclaw::gateway::protocol::ResponseFrame& response) {
		std::string output;
		if (!response.payloadJson.has_value()) {
			return output;
		}

		blazeclaw::gateway::json::FindStringField(
			response.payloadJson.value(),
			"output",
			output);
		return output;
	}
}

TEST_CASE("Python runtime selector honors tool-over-extension-over-global precedence", "[python][runtime][selector]") {
	ScopedEnvVar modeDefault("BLAZECLAW_PYTHON_RUNTIME_MODE_DEFAULT");
	ScopedEnvVar runtimeEnabled("BLAZECLAW_PYTHON_RUNTIME_ENABLED");
	ScopedEnvVar embeddedEnabled("BLAZECLAW_PYTHON_EMBEDDED_ENABLED");

	modeDefault.Set("external");
	runtimeEnabled.Set("true");
	embeddedEnabled.Set("true");

	blazeclaw::gateway::python::PythonRuntimeSelector selector;
	const auto selection = selector.Resolve(
		R"({"runtime":{"mode":"embedded"},"extension":{"runtime":{"mode":"external"}}})");

	REQUIRE(selection.resolved);
	REQUIRE(selection.mode == "embedded");
	REQUIRE(selection.modeSource == "tool");
}

TEST_CASE("Python runtime diagnostics tool is exposed and returns snapshot", "[python][runtime][diagnostics]") {
	ScopedEnvVar runtimeEnabled("BLAZECLAW_PYTHON_RUNTIME_ENABLED");
	ScopedEnvVar embeddedEnabled("BLAZECLAW_PYTHON_EMBEDDED_ENABLED");
	runtimeEnabled.Set("true");
	embeddedEnabled.Set("false");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.Start(config));

	const auto toolsResponse = host.RouteRequest(
		blazeclaw::gateway::protocol::RequestFrame{
			.id = "python-runtime-tools-list",
			.method = "gateway.tools.list",
			.paramsJson = std::nullopt,
		});
	REQUIRE(toolsResponse.ok);
	REQUIRE(toolsResponse.payloadJson.has_value());
	REQUIRE(toolsResponse.payloadJson->find("\"python.runtime.health\"") != std::string::npos);

	const auto executeResponse = ExecuteTool(host, "python.runtime.health", "{}");
	REQUIRE(executeResponse.ok);
	const std::string output = ExtractToolOutput(executeResponse);
	REQUIRE(output.find("\"pythonRuntime\"") != std::string::npos);
	REQUIRE(output.find("\"activeMode\"") != std::string::npos);
	REQUIRE(output.find("\"runtimeAvailability\"") != std::string::npos);

	host.Stop();
}

TEST_CASE("Python runtime disabled flag blocks python.script.run", "[python][runtime][rollout]") {
	ScopedEnvVar runtimeEnabled("BLAZECLAW_PYTHON_RUNTIME_ENABLED");
	runtimeEnabled.Set("false");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.Start(config));

	const auto executeResponse = ExecuteTool(
		host,
		"python.script.run",
		R"({"scriptPath":"blazeclaw/skills/demo.py"})");
	REQUIRE(executeResponse.ok);
	const std::string output = ExtractToolOutput(executeResponse);
	REQUIRE(output.find("python_runtime_disabled") != std::string::npos);

	host.Stop();
}

TEST_CASE("Embedded runtime disabled flag blocks embedded mode requests", "[python][runtime][rollout]") {
	ScopedEnvVar runtimeEnabled("BLAZECLAW_PYTHON_RUNTIME_ENABLED");
	ScopedEnvVar embeddedEnabled("BLAZECLAW_PYTHON_EMBEDDED_ENABLED");
	runtimeEnabled.Set("true");
	embeddedEnabled.Set("false");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.Start(config));

	const auto executeResponse = ExecuteTool(
		host,
		"python.script.run",
		R"({"runtime":{"mode":"embedded"},"scriptPath":"blazeclaw/skills/demo.py"})");
	REQUIRE(executeResponse.ok);
	const std::string output = ExtractToolOutput(executeResponse);
	REQUIRE(output.find("python_embedded_runtime_disabled") != std::string::npos);

	host.Stop();
}

TEST_CASE("Policy gate blocks network, env, and origin violations", "[python][runtime][policy]") {
	ScopedEnvVar runtimeEnabled("BLAZECLAW_PYTHON_RUNTIME_ENABLED");
	ScopedEnvVar allowNetwork("BLAZECLAW_PYTHON_ALLOW_NETWORK");
	ScopedEnvVar allowCustomEnv("BLAZECLAW_PYTHON_ALLOW_CUSTOM_ENV");
	ScopedEnvVar denyUnknownOrigins("BLAZECLAW_PYTHON_DENY_UNKNOWN_ORIGINS");
	ScopedEnvVar allowedOrigins("BLAZECLAW_PYTHON_ALLOWED_ORIGINS");

	runtimeEnabled.Set("true");
	allowNetwork.Set("false");
	allowCustomEnv.Set("false");
	denyUnknownOrigins.Set("true");
	allowedOrigins.Set("trusted-ui");

	blazeclaw::gateway::GatewayHost host;
	blazeclaw::config::GatewayConfig config;
	REQUIRE(host.Start(config));

	const auto networkBlocked = ExecuteTool(
		host,
		"python.script.run",
		R"({"runtime":{"mode":"external"},"policy":{"allowNetwork":true},"scriptPath":"blazeclaw/skills/demo.py"})");
	REQUIRE(networkBlocked.ok);
	REQUIRE(ExtractToolOutput(networkBlocked).find("python_policy_blocked") != std::string::npos);

	const auto envBlocked = ExecuteTool(
		host,
		"python.script.run",
		R"({"runtime":{"mode":"external"},"env":{"A":"B"},"scriptPath":"blazeclaw/skills/demo.py"})");
	REQUIRE(envBlocked.ok);
	REQUIRE(ExtractToolOutput(envBlocked).find("python_policy_blocked") != std::string::npos);

	const auto originBlocked = ExecuteTool(
		host,
		"python.script.run",
		R"({"runtime":{"mode":"external"},"policy":{"origin":"unknown-ui"},"scriptPath":"blazeclaw/skills/demo.py"})");
	REQUIRE(originBlocked.ok);
	REQUIRE(ExtractToolOutput(originBlocked).find("python_policy_blocked") != std::string::npos);

	host.Stop();
}
