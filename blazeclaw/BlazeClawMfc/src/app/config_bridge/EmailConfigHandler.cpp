#include "pch.h"
#include "EmailConfigHandler.h"
#include "ConfigBridgeUtils.h"
#include "../BlazeClawMFCDoc.h"
#include "../BlazeClawMFCApp.h"
#include "../../gateway/GatewayJsonUtils.h"
#include <sstream>

namespace blazeclaw
{
namespace config_bridge
{

namespace
{
	/// <summary>
	/// Open email config document in new window.
	/// Returns true if successful, false otherwise.
	/// </summary>
	bool OpenEmailConfigDocumentImpl(const ConfigBridgeContext& ctx)
	{
		// Get email config URL resolver from app context
		auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
		if (app == nullptr)
		{
			ctx.appendChatProcedureStatusLine(
				L"email.config.open.failed",
				"application context unavailable");
			return false;
		}

		// NOTE: This implementation requires ResolveEmailConfigStartupUrl() and SetPendingStartupUrl()
		// which are view-specific state methods. For now, we defer the full extraction and keep
		// this as a placeholder. The actual OpenEmailConfigDocument() will remain in the view
		// until further refactoring of document template coordination.
		return false;
	}
}

bool EmailConfigHandler::HandleMessage(
	const std::string& messageJson,
	const ConfigBridgeContext& ctx)
{
	std::string channel;
	if (!blazeclaw::gateway::json::FindStringField(messageJson, "channel", channel))
	{
		return false;
	}

	if (channel == "blazeclaw.email.config.open")
	{
		// NOTE: OpenEmailConfigDocument requires view-specific state (pending URL).
		// For now, we signal that email config open is not fully extracted.
		// This will be completed in a follow-up refactor pass.
		const std::string json =
			"{\"channel\":\"blazeclaw.email.config.opened\",\"ok\":false,"
			"\"error\":\"Email config document open not yet extracted to handler.\"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(json));
		return true;
	}

	if (channel == "blazeclaw.email.config.ready")
	{
		LoadEmailConfigToBridge(ctx);
		return true;
	}

	if (channel == "blazeclaw.chat.email.config.open")
	{
		// NOTE: Same as above - requires view-specific pending URL state.
		const std::string json =
			"{\"channel\":\"blazeclaw.chat.email.config.opened\",\"ok\":false,"
			"\"error\":\"Email config document open not yet extracted to handler.\"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(json));
		ctx.appendChatProcedureStatusLine(
			L"email.config.chat.open.failed",
			"");
		return true;
	}

	if (channel == "blazeclaw.email.config.save")
	{
		std::string payloadJson;
		if (blazeclaw::gateway::json::FindRawField(messageJson, "payload", payloadJson))
		{
			PersistEmailConfigFromPayload(payloadJson, ctx);
		}
		else
		{
			PersistEmailConfigFromPayload(messageJson, ctx);
		}

		return true;
	}

	if (channel == "blazeclaw.email.config.cancel")
	{
		ctx.appendChatProcedureStatusLine(L"email.config.cancelled", "");
		ctx.postBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.cancelled\",\"ok\":true}");
		return true;
	}

	return false;
}

void EmailConfigHandler::LoadEmailConfigToBridge(const ConfigBridgeContext& ctx)
{
	::CBlazeClawMFCDoc* doc = ctx.getDocument();
	if (doc == nullptr)
	{
		const std::string json =
			"{\"channel\":\"blazeclaw.email.config.load.error\",\"ok\":false,"
			"\"message\":\"Document context is unavailable.\"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(json));
		return;
	}

	std::string envContent;
	std::string loadError;
	if (!doc->LoadEmailSkillConfigEnv(envContent, loadError))
	{
		const std::string json =
			"{\"channel\":\"blazeclaw.email.config.load.empty\",\"ok\":false,"
			"\"message\":" +
			ConfigBridgeUtils::JsonString(loadError.empty() ? "No saved config found." : loadError) +
			"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(json));
		return;
	}

	const auto pairs = ConfigBridgeUtils::ParseDotEnvPairs(envContent);
	const auto readOrDefault = [&](const char* key, const char* fallback)
		{
			auto it = pairs.find(key);
			if (it == pairs.end() || it->second.empty())
			{
				return std::string(fallback);
			}

			return it->second;
		};

	const std::string smtpHost = readOrDefault("SMTP_HOST", "");
	const std::string smtpUser = readOrDefault("SMTP_USER", "");
	const std::string imapUser = readOrDefault("IMAP_USER", "");
	const std::string email = smtpUser.empty() ? imapUser : smtpUser;
	const std::string smtpPass = readOrDefault("SMTP_PASS", "");
	const std::string imapPass = readOrDefault("IMAP_PASS", "");
	const std::string password = smtpPass.empty() ? imapPass : smtpPass;
	const std::string smtpRejectUnauthorized =
		readOrDefault("SMTP_REJECT_UNAUTHORIZED", "true");
	const std::string imapRejectUnauthorized =
		readOrDefault("IMAP_REJECT_UNAUTHORIZED", smtpRejectUnauthorized.c_str());

	const std::string json =
		"{\"channel\":\"blazeclaw.email.config.loaded\",\"ok\":true,"
		"\"payload\":{"
		"\"serverPreset\":" +
		ConfigBridgeUtils::JsonString(ConfigBridgeUtils::ResolvePreferredServerPreset(smtpHost)) +
		",\"smtpHost\":" + ConfigBridgeUtils::JsonString(smtpHost) +
		",\"smtpPort\":" + ConfigBridgeUtils::JsonString(readOrDefault("SMTP_PORT", "587")) +
		",\"imapHost\":" + ConfigBridgeUtils::JsonString(readOrDefault("IMAP_HOST", "")) +
		",\"imapPort\":" + ConfigBridgeUtils::JsonString(readOrDefault("IMAP_PORT", "993")) +
		",\"email\":" + ConfigBridgeUtils::JsonString(email) +
		",\"password\":" + ConfigBridgeUtils::JsonString(password) +
		",\"allowedReadDirs\":" +
		ConfigBridgeUtils::JsonString(readOrDefault("ALLOWED_READ_DIRS", "~/Downloads,~/Documents")) +
		",\"allowedWriteDirs\":" +
		ConfigBridgeUtils::JsonString(readOrDefault("ALLOWED_WRITE_DIRS", "~/Downloads")) +
		",\"imapTls\":" +
		std::string(ConfigBridgeUtils::ParseEnvBool(readOrDefault("IMAP_TLS", "true"), true)
			? "true"
			: "false") +
		",\"smtpSecure\":" +
		std::string(ConfigBridgeUtils::ParseEnvBool(readOrDefault("SMTP_SECURE", "false"), false)
			? "true"
			: "false") +
		",\"rejectUnauthorized\":" +
		std::string(ConfigBridgeUtils::ParseEnvBool(imapRejectUnauthorized, true)
			? "true"
			: "false") +
		"}}";

	ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(json));
}

void EmailConfigHandler::PersistEmailConfigFromPayload(
	const std::string& payloadJson,
	const ConfigBridgeContext& ctx)
{
	std::string smtpHost;
	std::string smtpPortRaw;
	std::string imapHost;
	std::string imapPortRaw;
	std::string email;
	std::string password;
	std::string allowedReadDirs;
	std::string allowedWriteDirs;
	bool imapTls = true;
	bool smtpSecure = false;
	bool rejectUnauthorized = true;

	blazeclaw::gateway::json::FindStringField(payloadJson, "smtpHost", smtpHost);
	blazeclaw::gateway::json::FindStringField(payloadJson, "smtpPort", smtpPortRaw);
	blazeclaw::gateway::json::FindStringField(payloadJson, "imapHost", imapHost);
	blazeclaw::gateway::json::FindStringField(payloadJson, "imapPort", imapPortRaw);
	blazeclaw::gateway::json::FindStringField(payloadJson, "email", email);
	blazeclaw::gateway::json::FindStringField(payloadJson, "password", password);
	blazeclaw::gateway::json::FindStringField(payloadJson, "allowedReadDirs", allowedReadDirs);
	blazeclaw::gateway::json::FindStringField(payloadJson, "allowedWriteDirs", allowedWriteDirs);
	blazeclaw::gateway::json::FindBoolField(payloadJson, "imapTls", imapTls);
	blazeclaw::gateway::json::FindBoolField(payloadJson, "smtpSecure", smtpSecure);
	blazeclaw::gateway::json::FindBoolField(payloadJson, "rejectUnauthorized", rejectUnauthorized);

	if (imapHost.empty())
	{
		imapHost = smtpHost;
	}

	if (imapPortRaw.empty())
	{
		imapPortRaw = "993";
	}

	int smtpPort = 0;
	int imapPort = 0;
	if (smtpHost.empty())
	{
		ctx.postBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"smtp_host_required\",\"message\":\"SMTP host is required.\",\"fieldErrors\":[{\"field\":\"smtpHost\",\"code\":\"required\",\"message\":\"SMTP host is required.\"}]}"
		);
		return;
	}

	if (!ConfigBridgeUtils::TryParsePort(smtpPortRaw, smtpPort))
	{
		ctx.postBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"smtp_port_invalid\",\"message\":\"SMTP port must be an integer between 1 and 65535.\",\"fieldErrors\":[{\"field\":\"smtpPort\",\"code\":\"invalid\",\"message\":\"SMTP port must be an integer between 1 and 65535.\"}]}"
		);
		return;
	}

	if (!ConfigBridgeUtils::TryParsePort(imapPortRaw, imapPort))
	{
		ctx.postBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"imap_port_invalid\",\"message\":\"IMAP port must be an integer between 1 and 65535.\",\"fieldErrors\":[{\"field\":\"imapPort\",\"code\":\"invalid\",\"message\":\"IMAP port must be an integer between 1 and 65535.\"}]}"
		);
		return;
	}

	if (!ConfigBridgeUtils::IsLikelyEmailAddress(email))
	{
		ctx.postBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"email_invalid\",\"message\":\"Valid email account is required.\",\"fieldErrors\":[{\"field\":\"email\",\"code\":\"invalid\",\"message\":\"Valid email account is required.\"}]}"
		);
		return;
	}

	if (password.empty())
	{
		ctx.postBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"password_required\",\"message\":\"Password or app password is required.\",\"fieldErrors\":[{\"field\":\"password\",\"code\":\"required\",\"message\":\"Password or app password is required.\"}]}"
		);
		return;
	}

	if (allowedReadDirs.empty())
	{
		allowedReadDirs = "~/Downloads,~/Documents";
	}

	if (allowedWriteDirs.empty())
	{
		allowedWriteDirs = "~/Downloads";
	}

	std::ostringstream env;
	env << "IMAP_HOST=" << imapHost << "\n";
	env << "IMAP_PORT=" << imapPort << "\n";
	env << "IMAP_USER=" << email << "\n";
	env << "IMAP_PASS=" << password << "\n";
	env << "IMAP_TLS=" << (imapTls ? "true" : "false") << "\n";
	env << "IMAP_REJECT_UNAUTHORIZED=" << (rejectUnauthorized ? "true" : "false") << "\n";
	env << "IMAP_MAILBOX=INBOX\n\n";
	env << "SMTP_HOST=" << smtpHost << "\n";
	env << "SMTP_PORT=" << smtpPort << "\n";
	env << "SMTP_SECURE=" << (smtpSecure ? "true" : "false") << "\n";
	env << "SMTP_USER=" << email << "\n";
	env << "SMTP_PASS=" << password << "\n";
	env << "SMTP_FROM=" << email << "\n";
	env << "SMTP_REJECT_UNAUTHORIZED=" << (rejectUnauthorized ? "true" : "false") << "\n\n";
	env << "ALLOWED_READ_DIRS=" << allowedReadDirs << "\n";
	env << "ALLOWED_WRITE_DIRS=" << allowedWriteDirs << "\n";

	::CBlazeClawMFCDoc* doc = ctx.getDocument();
	if (doc == nullptr)
	{
		ctx.postBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"message\":\"Document context is unavailable.\"}");
		return;
	}

	std::string error;
	if (!doc->SaveEmailSkillConfigEnv(env.str(), error))
	{
		const std::string json =
			"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"message\":" +
			ConfigBridgeUtils::JsonString(error.empty() ? "Failed to save email config." : error) +
			"}";
		ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(json));
		return;
	}

	ctx.appendChatProcedureStatusLine(
		L"email.config.saved",
		ConfigBridgeUtils::ToNarrow(doc->GetEmailSkillConfigPath().wstring()));

	const std::string json =
		"{\"channel\":\"blazeclaw.email.config.saved\",\"ok\":true,\"configPath\":" +
		ConfigBridgeUtils::JsonString(ConfigBridgeUtils::ToNarrow(doc->GetEmailSkillConfigPath().wstring())) +
		"}";
	ctx.postBridgeMessageJson(ConfigBridgeUtils::ToWide(json));
}

} // namespace config_bridge
} // namespace blazeclaw
