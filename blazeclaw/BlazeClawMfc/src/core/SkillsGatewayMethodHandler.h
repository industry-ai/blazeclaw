#pragma once

#include "../gateway/GatewayProtocolModels.h"
#include "../gateway/GatewayJsonUtils.h"

#include <filesystem>
#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <sstream>
#include <string>

namespace blazeclaw::core {

	class SkillsGatewayMethodHandler {
	public:
		struct Dependencies {
			std::function<bool(
				const std::string& skill,
				const std::string& envContent,
				std::string& outError,
				std::filesystem::path& outPath)> persistSkillConfigEnv;
			std::function<void()> refreshSkillView;
			std::function<void()> refreshSkillsState;
			std::function<void()> publishGatewaySkillsStateProjection;
		};

		[[nodiscard]] blazeclaw::gateway::protocol::ResponseFrame
			HandleSkillsUpdate(
				const blazeclaw::gateway::protocol::RequestFrame& request,
				const Dependencies& dependencies) const {
			std::string skill;
			if (!request.paramsJson.has_value() ||
				!blazeclaw::gateway::json::FindStringField(
					request.paramsJson.value(),
					"skill",
					skill) ||
				blazeclaw::gateway::json::Trim(skill).empty()) {
				return blazeclaw::gateway::protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = blazeclaw::gateway::protocol::ErrorShape{
						.code = "missing_skill",
						.message = "Parameter `skill` is required.",
						.detailsJson = std::nullopt,
						.retryable = false,
						.retryAfterMs = std::nullopt,
					},
				};
			}

			std::string apiKey;
			blazeclaw::gateway::json::FindStringField(
				request.paramsJson.value(),
				"apiKey",
				apiKey);

			std::string envRaw;
			blazeclaw::gateway::json::FindRawField(
				request.paramsJson.value(),
				"env",
				envRaw);

			std::string configKey;
			std::string configValue;
			blazeclaw::gateway::json::FindStringField(
				request.paramsJson.value(),
				"configKey",
				configKey);
			blazeclaw::gateway::json::FindStringField(
				request.paramsJson.value(),
				"configValue",
				configValue);

			std::ostringstream envOut;
			if (!apiKey.empty()) {
				envOut << "API_KEY=" << apiKey << "\n";
			}

			if (!envRaw.empty()) {
				std::string compactEnv = blazeclaw::gateway::json::Trim(envRaw);
				if (!compactEnv.empty() &&
					compactEnv.front() == '{' &&
					compactEnv.back() == '}') {
					nlohmann::json envObj;
					try {
						envObj = nlohmann::json::parse(compactEnv);
					}
					catch (...) {
						return blazeclaw::gateway::protocol::ResponseFrame{
							.id = request.id,
							.ok = false,
							.payloadJson = std::nullopt,
							.error = blazeclaw::gateway::protocol::ErrorShape{
								.code = "invalid_env_payload",
								.message = "Parameter `env` must be a valid JSON object.",
								.detailsJson = std::nullopt,
								.retryable = false,
								.retryAfterMs = std::nullopt,
							},
						};
					}

					if (envObj.is_object()) {
						for (auto it = envObj.begin(); it != envObj.end(); ++it) {
							if (it.value().is_string()) {
								envOut << it.key() << "="
									<< it.value().get<std::string>() << "\n";
							}
						}
					}
				}
			}

			if (!configKey.empty()) {
				envOut << configKey << "=" << configValue << "\n";
			}

			const std::string envContent = envOut.str();
			if (envContent.empty()) {
				return blazeclaw::gateway::protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = blazeclaw::gateway::protocol::ErrorShape{
						.code = "empty_update",
						.message = "No update payload was provided.",
						.detailsJson = std::nullopt,
						.retryable = false,
						.retryAfterMs = std::nullopt,
					},
				};
			}

			if (!dependencies.persistSkillConfigEnv) {
				return blazeclaw::gateway::protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = blazeclaw::gateway::protocol::ErrorShape{
						.code = "doc_unavailable",
						.message =
							"No host bridge callback configured for skill update persistence.",
						.detailsJson = std::nullopt,
						.retryable = true,
						.retryAfterMs = 100,
					},
				};
			}

			std::string persistError;
			std::filesystem::path savedPath;
			if (!dependencies.persistSkillConfigEnv(
				skill,
				envContent,
				persistError,
				savedPath)) {
				return blazeclaw::gateway::protocol::ResponseFrame{
					.id = request.id,
					.ok = false,
					.payloadJson = std::nullopt,
					.error = blazeclaw::gateway::protocol::ErrorShape{
						.code = "persist_failed",
						.message = persistError.empty()
							? "Failed to persist skill update payload."
							: persistError,
						.detailsJson = std::nullopt,
						.retryable = false,
						.retryAfterMs = std::nullopt,
					},
				};
			}

			if (dependencies.refreshSkillsState) {
				dependencies.refreshSkillsState();
			}
			if (dependencies.publishGatewaySkillsStateProjection) {
				dependencies.publishGatewaySkillsStateProjection();
			}
			if (dependencies.refreshSkillView) {
				dependencies.refreshSkillView();
			}

			const std::string payload =
				"{\"skill\":\"" + EscapeJsonUtf8(skill) +
				"\",\"configPath\":\"" +
				EscapeJsonUtf8(ToNarrow(savedPath.wstring())) +
				"\",\"updated\":true}";

			return blazeclaw::gateway::protocol::ResponseFrame{
				.id = request.id,
				.ok = true,
				.payloadJson = payload,
				.error = std::nullopt,
			};
		}

	private:
		[[nodiscard]] static std::string EscapeJsonUtf8(
			const std::string& value) {
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

		[[nodiscard]] static std::string ToNarrow(
			const std::wstring& value) {
			std::string output;
			output.reserve(value.size());
			for (const wchar_t ch : value) {
				output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			return output;
		}
	};

} // namespace blazeclaw::core
