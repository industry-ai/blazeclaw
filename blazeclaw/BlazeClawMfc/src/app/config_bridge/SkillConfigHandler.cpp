#include "pch.h"
#include "SkillConfigHandler.h"
#include "ConfigBridgeUtils.h"
#include "../BlazeClawMFCDoc.h"
#include "../BlazeClawMFCApp.h"
#include "../../gateway/GatewayJsonUtils.h"
#include "../../gateway/GatewayProtocolModels.h"
#include <nlohmann/json.hpp>
#include <sstream>

namespace blazeclaw
{
namespace config_bridge
{

bool SkillConfigHandler::HandleMessage(
	const std::string& messageJson,
	const ConfigBridgeContext& ctx)
{
	std::string channel;
	if (!blazeclaw::gateway::json::FindStringField(messageJson, "channel", channel))
	{
		return false;
	}

	if (channel != "blazeclaw.skill.config.ready" &&
		channel != "blazeclaw.skill.config.save" &&
		channel != "blazeclaw.skill.config.cancel" &&
		channel != "blazeclaw.skill.config.validate")
	{
		return false;
	}

	std::string skillKey;
	blazeclaw::gateway::json::FindStringField(messageJson, "skillKey", skillKey);
	if (blazeclaw::gateway::json::Trim(skillKey).empty())
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":\"\",\"code\":\"missing_skill_key\",\"message\":\"skillKey is required.\"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
		return true;
	}

	std::string correlationId;
	blazeclaw::gateway::json::FindStringField(messageJson, "id", correlationId);
	if (correlationId.empty())
	{
		correlationId = "skill-config";
	}

	if (channel == "blazeclaw.skill.config.ready")
	{
		LoadSkillConfigToBridge(skillKey, correlationId, ctx);
		return true;
	}

	if (channel == "blazeclaw.skill.config.save")
	{
		std::string payloadJson;
		if (blazeclaw::gateway::json::FindRawField(messageJson, "payload", payloadJson))
		{
			PersistSkillConfigFromPayload(skillKey, correlationId, payloadJson, ctx);
		}
		else
		{
			PersistSkillConfigFromPayload(skillKey, correlationId, messageJson, ctx);
		}

		return true;
	}

	if (channel == "blazeclaw.skill.config.validate")
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.validation\",\"skillKey\":" +
			ConfigBridgeUtils::JsonString(skillKey) +
			",\"id\":" +
			ConfigBridgeUtils::JsonString(correlationId) +
			",\"ok\":true,\"fieldErrors\":[]}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
		return true;
	}

	if (channel == "blazeclaw.skill.config.cancel")
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.cancelled\",\"skillKey\":" +
			ConfigBridgeUtils::JsonString(skillKey) +
			",\"id\":" +
			ConfigBridgeUtils::JsonString(correlationId) +
			",\"ok\":true}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
		return true;
	}

	return false;
}

void SkillConfigHandler::LoadSkillConfigToBridge(
	const std::string& skillKey,
	const std::string& correlationId,
	const ConfigBridgeContext& ctx)
{
	::CBlazeClawMFCDoc* doc = ctx.getDocument();
	if (doc == nullptr)
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
			ConfigBridgeUtils::JsonString(skillKey) +
			",\"id\":" +
			ConfigBridgeUtils::JsonString(correlationId) +
			",\"code\":\"doc_unavailable\",\"message\":\"Document context is unavailable.\"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
		return;
	}

	std::string envContent;
	std::string loadError;
	std::filesystem::path loadedPath;
	if (!doc->LoadSkillConfigEnv(skillKey, envContent, loadError, &loadedPath))
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.loaded\",\"skillKey\":" +
			ConfigBridgeUtils::JsonString(skillKey) +
			",\"id\":" +
			ConfigBridgeUtils::JsonString(correlationId) +
			",\"ok\":true,\"payload\":{},\"sourceMeta\":{\"configPath\":\"\",\"exists\":false}}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
		return;
	}

	const auto pairs = ConfigBridgeUtils::ParseDotEnvPairs(envContent);
	std::string payload = "{";
	bool first = true;
	for (const auto& pair : pairs)
	{
		if (!first)
		{
			payload += ",";
		}

		payload += ConfigBridgeUtils::JsonString(pair.first);
		payload += ":";
		payload += ConfigBridgeUtils::JsonString(pair.second);
		first = false;
	}
	payload += "}";

	const std::string response =
		"{\"channel\":\"blazeclaw.skill.config.loaded\",\"skillKey\":" +
		ConfigBridgeUtils::JsonString(skillKey) +
		",\"id\":" +
		ConfigBridgeUtils::JsonString(correlationId) +
		",\"ok\":true,\"payload\":" +
		payload +
		",\"sourceMeta\":{\"configPath\":" +
		ConfigBridgeUtils::JsonString(ConfigBridgeUtils::ToNarrow(loadedPath.wstring())) +
		",\"exists\":true,\"sourceOfTruth\":" +
		ConfigBridgeUtils::JsonString("canonical") +
		"}}";
	ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
}

void SkillConfigHandler::PersistSkillConfigFromPayload(
	const std::string& skillKey,
	const std::string& correlationId,
	const std::string& payloadJson,
	const ConfigBridgeContext& ctx)
{
	::CBlazeClawMFCDoc* doc = ctx.getDocument();
	if (doc == nullptr)
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
			ConfigBridgeUtils::JsonString(skillKey) +
			",\"id\":" +
			ConfigBridgeUtils::JsonString(correlationId) +
			",\"code\":\"doc_unavailable\",\"message\":\"Document context is unavailable.\"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
		return;
	}

	auto pairs = ConfigBridgeUtils::ParseDotEnvPairs(payloadJson);
	if (pairs.empty())
	{
		std::string payloadRaw;
		if (blazeclaw::gateway::json::FindRawField(payloadJson, "payload", payloadRaw))
		{
			pairs = ConfigBridgeUtils::ParseDotEnvPairs(payloadRaw);
		}

		if (pairs.empty())
		{
			auto parseJsonObjectPairs = [](const std::string& rawJson)
				{
					std::unordered_map<std::string, std::string> parsedPairs;
					if (rawJson.empty())
					{
						return parsedPairs;
					}

					try
					{
						const auto json = nlohmann::json::parse(rawJson);
						if (!json.is_object())
						{
							return parsedPairs;
						}

						for (auto it = json.begin(); it != json.end(); ++it)
						{
							if (it.value().is_string())
							{
								parsedPairs.insert_or_assign(
									it.key(),
									it.value().get<std::string>());
							}
						}
					}
					catch (...)
					{
					}

					return parsedPairs;
				};

			pairs = parseJsonObjectPairs(payloadJson);
			if (pairs.empty() && !payloadRaw.empty())
			{
				pairs = parseJsonObjectPairs(payloadRaw);
			}
		}

		if (pairs.empty())
		{
			std::string fieldName;
			std::string fieldValue;
			if (blazeclaw::gateway::json::FindStringField(payloadJson, "name", fieldName) &&
				blazeclaw::gateway::json::FindStringField(payloadJson, "value", fieldValue) &&
				!blazeclaw::gateway::json::Trim(fieldName).empty())
			{
				pairs.insert_or_assign(fieldName, fieldValue);
			}
		}

		if (pairs.empty())
		{
			const std::string response =
				"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
				ConfigBridgeUtils::JsonString(skillKey) +
				",\"id\":" +
				ConfigBridgeUtils::JsonString(correlationId) +
				",\"code\":\"invalid_payload\",\"message\":\"No key-value payload was provided.\",\"fieldErrors\":[{\"field\":\"payload\",\"code\":\"required\",\"message\":\"Provide at least one key-value pair.\"}]}";
			ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
			return;
		}
	}

	if (pairs.size() > 64)
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
			ConfigBridgeUtils::JsonString(skillKey) +
			",\"id\":" +
			ConfigBridgeUtils::JsonString(correlationId) +
			",\"code\":\"too_many_fields\",\"message\":\"Too many config fields.\",\"fieldErrors\":[{\"field\":\"payload\",\"code\":\"max_fields\",\"message\":\"Maximum 64 fields are allowed.\"}]}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
		return;
	}

	for (const auto& pair : pairs)
	{
		if (pair.first.empty() || pair.first.size() > 128)
		{
			const std::string response =
				"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
				ConfigBridgeUtils::JsonString(skillKey) +
				",\"id\":" +
				ConfigBridgeUtils::JsonString(correlationId) +
				",\"code\":\"invalid_field_name\",\"message\":\"Invalid field name length.\",\"fieldErrors\":[{\"field\":" +
				ConfigBridgeUtils::JsonString(pair.first) +
				",\"code\":\"max_length\",\"message\":\"Field name must be between 1 and 128 characters.\"}]}";
			ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
			return;
		}

		if (pair.second.size() > 4096)
		{
			const std::string response =
				"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
				ConfigBridgeUtils::JsonString(skillKey) +
				",\"id\":" +
				ConfigBridgeUtils::JsonString(correlationId) +
				",\"code\":\"invalid_field_value\",\"message\":\"Field value exceeds maximum length.\",\"fieldErrors\":[{\"field\":" +
				ConfigBridgeUtils::JsonString(pair.first) +
				",\"code\":\"max_length\",\"message\":\"Field value must not exceed 4096 characters.\"}]}";
			ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
			return;
		}
	}

	std::ostringstream env;
	for (const auto& pair : pairs)
	{
		env << pair.first << "=" << pair.second << "\n";
	}

	std::string error;
	std::filesystem::path savedPath;
	if (!doc->SaveSkillConfigEnv(skillKey, env.str(), error, &savedPath))
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
			ConfigBridgeUtils::JsonString(skillKey) +
			",\"id\":" +
			ConfigBridgeUtils::JsonString(correlationId) +
			",\"code\":\"persist_failed\",\"message\":" +
			ConfigBridgeUtils::JsonString(error.empty() ? "Failed to persist skill config." : error) +
			"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));
		return;
	}

	const std::string response =
		"{\"channel\":\"blazeclaw.skill.config.saved\",\"skillKey\":" +
		ConfigBridgeUtils::JsonString(skillKey) +
		",\"id\":" +
		ConfigBridgeUtils::JsonString(correlationId) +
		",\"configPath\":" +
		ConfigBridgeUtils::JsonString(ConfigBridgeUtils::ToNarrow(savedPath.wstring())) +
		",\"updatedChecks\":{},\"ok\":true}";
	ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(response));

	// Refresh gateway skills after config save
	const std::string refreshError = ctx.refreshGatewaySkills();
	if (!refreshError.empty())
	{
		ctx.appendChatProcedureStatusLine(
			L"warning.skills.refresh.failed",
			refreshError);
	}

	ctx.appendChatProcedureStatusLine(
		L"skills.config.persisted",
		"skill=" + skillKey +
		" path=" + ConfigBridgeUtils::ToNarrow(savedPath.wstring()) +
		" payload=" + ConfigBridgeUtils::TruncateForDiagnostics(
			ConfigBridgeUtils::RedactSensitiveJsonPayload(payloadJson),
			256));

	// Refresh skill view UI
	ctx.refreshSkillView();
}

} // namespace config_bridge
} // namespace blazeclaw
