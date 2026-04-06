// This MFC Samples source code demonstrates using MFC Microsoft Office Fluent User Interface
// (the "Fluent UI") and is provided only as referential material to supplement the
// Microsoft Foundation Classes Reference and related electronic documentation
// included with the MFC C++ library software.
// License terms to copy, use or distribute the Fluent UI are available separately.
// To learn more about our Fluent UI licensing program, please visit
// https://go.microsoft.com/fwlink/?LinkId=238214.
//
// Copyright (C) Microsoft Corporation
// All rights reserved.

// BlazeClaw.MFCView.cpp : implementation of the CBlazeClawMFCView class
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS can be defined in an ATL project implementing preview, thumbnail
// and search filter handlers and allows sharing of document code with that project.
#ifndef SHARED_HANDLERS
#include "BlazeClawMFCApp.h"
#endif

#include "BlazeClawMFCDoc.h"
#include "BlazeClawMFCView.h"
#include "MainFrame.h"
#include "../gateway/GatewayJsonUtils.h"
#include "../gateway/GatewayProtocolModels.h"

#include <cwctype>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {
	std::wstring g_pendingStartupUrl;
	std::string g_pendingSkillKey;
	std::string g_pendingSkillPropertiesJson;
	std::string g_generatedSkillConfigHtml;
	std::wstring BuildFileUrl(const std::filesystem::path& filePath);
	std::wstring ResolveEmailConfigStartupUrl();
	std::string JsonString(const std::string& value);
	std::string ToLowerAscii(const std::string& value);
	std::unordered_map<std::string, std::string> ParseDotEnvPairs(
		const std::string& envContent);

	constexpr UINT_PTR kBridgeLifecycleTimerId = 0x4A21;
	constexpr UINT kBridgeLifecycleTimerMs = 1000;
	constexpr std::uint64_t kBridgeTraceFlushIntervalMs = 1000;

	std::string ToNarrow(const std::wstring& value)
	{
		if (value.empty())
		{
			return {};
		}

		const int sizeNeeded = WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0,
			nullptr,
			nullptr);
		if (sizeNeeded <= 0)
		{
			std::string fallback;
			fallback.reserve(value.size());
			for (const wchar_t ch : value)
			{
				fallback.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			return fallback;
		}

		std::string output(sizeNeeded, '\0');
		const int written = WideCharToMultiByte(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			output.data(),
			sizeNeeded,
			nullptr,
			nullptr);
		if (written <= 0)
		{
			return {};
		}

		return output;
	}

	std::optional<std::filesystem::path> FindEmailConfigHtml(
		const std::filesystem::path& start)
	{
		std::filesystem::path cursor = start;
		while (!cursor.empty())
		{
			const auto configHtml =
				cursor /
				L"blazeclaw" /
				L"skills" /
				L"imap-smtp-email" /
				L"config.html";
			if (std::filesystem::exists(configHtml))
			{
				return configHtml;
			}

			if (!cursor.has_parent_path())
			{
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor)
			{
				break;
			}

			cursor = parent;
		}

		return std::nullopt;
	}

	std::string NormalizeSkillKeyForPath(const std::string& skillKey)
	{
		std::string normalized;
		normalized.reserve(skillKey.size());
		for (const char ch : skillKey)
		{
			if (ch == '_')
			{
				normalized.push_back('-');
				continue;
			}

			normalized.push_back(static_cast<char>(
				std::tolower(static_cast<unsigned char>(ch))));
		}

		return normalized;
	}

	std::optional<std::filesystem::path> FindSkillConfigHtml(
		const std::filesystem::path& start,
		const std::string& skillKey)
	{
		const std::string normalizedSkillKey = NormalizeSkillKeyForPath(skillKey);
		if (normalizedSkillKey.empty())
		{
			return std::nullopt;
		}

		std::filesystem::path cursor = start;
		while (!cursor.empty())
		{
			const std::wstring skillDir(
				normalizedSkillKey.begin(),
				normalizedSkillKey.end());
			const auto configHtml =
				cursor /
				L"blazeclaw" /
				L"skills" /
				std::filesystem::path(skillDir) /
				L"config.html";
			if (std::filesystem::exists(configHtml))
			{
				return configHtml;
			}

			if (!cursor.has_parent_path())
			{
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor)
			{
				break;
			}

			cursor = parent;
		}

		return std::nullopt;
	}

	std::wstring ResolveSkillConfigStartupUrl(const std::string& skillKey)
	{
		if (skillKey == "imap-smtp-email")
		{
			return ResolveEmailConfigStartupUrl();
		}

		std::vector<std::filesystem::path> roots;
		roots.push_back(std::filesystem::current_path());

		wchar_t modulePath[MAX_PATH]{};
		if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
		{
			roots.push_back(std::filesystem::path(modulePath).parent_path());
		}

		for (const auto& root : roots)
		{
			if (const auto found = FindSkillConfigHtml(root, skillKey); found.has_value())
			{
				return BuildFileUrl(found.value());
			}
		}

		return {};
	}

	std::vector<std::string> ParseJsonStringArray(const std::string& arrayJson)
	{
		std::vector<std::string> values;
		const std::string trimmed = blazeclaw::gateway::json::Trim(arrayJson);
		if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']')
		{
			return values;
		}

		std::size_t cursor = 1;
		while (cursor + 1 < trimmed.size())
		{
			cursor = blazeclaw::gateway::json::SkipWhitespace(trimmed, cursor);
			if (cursor >= trimmed.size() || trimmed[cursor] == ']')
			{
				break;
			}

			if (trimmed[cursor] == ',')
			{
				++cursor;
				continue;
			}

			if (trimmed[cursor] != '"')
			{
				++cursor;
				continue;
			}

			std::size_t parseIndex = cursor;
			std::string value;
			if (!blazeclaw::gateway::json::ParseJsonStringAt(trimmed, parseIndex, value))
			{
				break;
			}

			values.push_back(value);
			cursor = parseIndex;
		}

		return values;
	}

	std::string BuildGeneratedSkillConfigHtml(
		const std::string& skillKey,
		const std::string& propertiesJson)
	{
		std::string requiresEnvRaw;
		std::string requiresConfigRaw;
		std::string configPathHintsRaw;
		std::string primaryEnv;
		blazeclaw::gateway::json::FindRawField(
			propertiesJson,
			"requiresEnv",
			requiresEnvRaw);
		blazeclaw::gateway::json::FindRawField(
			propertiesJson,
			"requiresConfig",
			requiresConfigRaw);
		blazeclaw::gateway::json::FindRawField(
			propertiesJson,
			"configPathHints",
			configPathHintsRaw);
		blazeclaw::gateway::json::FindStringField(
			propertiesJson,
			"primaryEnv",
			primaryEnv);

		const auto requiresEnv = ParseJsonStringArray(requiresEnvRaw);
		const auto requiresConfig = ParseJsonStringArray(requiresConfigRaw);
		std::vector<std::string> envFields = requiresEnv;
		if (!primaryEnv.empty() &&
			std::find(envFields.begin(), envFields.end(), primaryEnv) == envFields.end())
		{
			envFields.insert(envFields.begin(), primaryEnv);
		}

		std::ostringstream html;
		html
			<< "<!doctype html><html><head><meta charset=\"utf-8\"/>"
			<< "<title>Skill Config</title>"
			<< "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:20px;}"
			<< "h1{font-size:18px;margin-bottom:8px;}"
			<< "h2{font-size:14px;margin-top:18px;}"
			<< "label{display:block;margin-top:10px;font-size:12px;color:#444;}"
			<< "input{width:100%;padding:8px;margin-top:4px;}"
			<< "button{margin-right:8px;margin-top:14px;padding:8px 12px;}"
			<< "pre{background:#f6f6f6;padding:8px;border-radius:4px;}"
			<< "</style></head><body>"
			<< "<h1>Generated Skill Configuration</h1>"
			<< "<div>skillKey: <b id=\"skill-key\"></b></div>"
			<< "<h2>Credentials</h2><div id=\"env-fields\"></div>"
			<< "<h2>Configuration Paths</h2><div id=\"config-fields\"></div>"
			<< "<h2>Hints</h2><pre id=\"hints\"></pre>"
			<< "<div><button id=\"btn-validate\">Validate</button>"
			<< "<button id=\"btn-save\">Save</button>"
			<< "<button id=\"btn-cancel\">Cancel</button></div>"
			<< "<script>"
			<< "const skillKey=" << JsonString(skillKey) << ";"
			<< "const requiresEnv="
			<< "[";
		for (std::size_t i = 0; i < envFields.size(); ++i)
		{
			if (i > 0)
			{
				html << ",";
			}
			html << JsonString(envFields[i]);
		}
		html << "];"
			<< "const requiresConfig=";
		if (requiresConfig.empty())
		{
			html << "[]";
		}
		else
		{
			html << "[";
			for (std::size_t i = 0; i < requiresConfig.size(); ++i)
			{
				if (i > 0)
				{
					html << ",";
				}
				html << JsonString(requiresConfig[i]);
			}
			html << "]";
		}
		html
			<< ";"
			<< "const configHints=" << (configPathHintsRaw.empty() ? "[]" : configPathHintsRaw) << ";"
			<< "const state={};"
			<< "document.getElementById('skill-key').textContent=skillKey;"
			<< "document.getElementById('hints').textContent=JSON.stringify(configHints,null,2);"
			<< "function send(msg){window.chrome.webview.postMessage(msg);}"
			<< "function addField(container,key){"
			<< "const l=document.createElement('label');l.textContent=key;"
			<< "const i=document.createElement('input');i.id='f_'+key.replace(/[^a-zA-Z0-9_]/g,'_');"
			<< "i.oninput=()=>{state[key]=i.value;};l.appendChild(i);container.appendChild(l);}"
			<< "const envContainer=document.getElementById('env-fields');requiresEnv.forEach(k=>addField(envContainer,k));"
			<< "const cfgContainer=document.getElementById('config-fields');requiresConfig.forEach(k=>addField(cfgContainer,k));"
			<< "window.chrome.webview.addEventListener('message',ev=>{const m=ev.data||{};"
			<< "if(m.channel==='blazeclaw.skill.config.loaded'&&m.skillKey===skillKey){"
			<< "const payload=m.payload||{};Object.keys(payload).forEach(k=>{state[k]=payload[k];"
			<< "const id='f_'+k.replace(/[^a-zA-Z0-9_]/g,'_');const el=document.getElementById(id);if(el){el.value=payload[k];}});}});"
			<< "document.getElementById('btn-save').onclick=()=>send({channel:'blazeclaw.skill.config.save',skillKey,id:'generated-save',payload:state});"
			<< "document.getElementById('btn-validate').onclick=()=>send({channel:'blazeclaw.skill.config.validate',skillKey,id:'generated-validate',payload:state});"
			<< "document.getElementById('btn-cancel').onclick=()=>send({channel:'blazeclaw.skill.config.cancel',skillKey,id:'generated-cancel'});"
			<< "send({channel:'blazeclaw.skill.config.ready',skillKey,id:'generated-ready'});"
			<< "</script></body></html>";

		return html.str();
	}

	bool IsSensitiveConfigKey(const std::string& key)
	{
		const std::string lowered = ToLowerAscii(key);
		return lowered.find("pass") != std::string::npos ||
			lowered.find("secret") != std::string::npos ||
			lowered.find("token") != std::string::npos ||
			lowered.find("apikey") != std::string::npos ||
			lowered.find("api_key") != std::string::npos;
	}

	std::string RedactSensitiveJsonPayload(const std::string& payloadJson)
	{
		const auto pairs = ParseDotEnvPairs(payloadJson);
		if (pairs.empty())
		{
			return payloadJson;
		}

		std::string json = "{";
		bool first = true;
		for (const auto& pair : pairs)
		{
			if (!first)
			{
				json += ",";
			}

			json += JsonString(pair.first);
			json += ":";
			json += JsonString(
				IsSensitiveConfigKey(pair.first)
				? std::string("***REDACTED***")
				: pair.second);
			first = false;
		}
		json += "}";
		return json;
	}

	std::string TruncateForDiagnostics(const std::string& value, const std::size_t maxChars)
	{
		if (value.size() <= maxChars)
		{
			return value;
		}

		return value.substr(0, maxChars) + "...";
	}

	bool IsLikelyEmailAddress(const std::string& value)
	{
		const auto atPos = value.find('@');
		if (atPos == std::string::npos || atPos == 0 || atPos + 1 >= value.size())
		{
			return false;
		}

		return value.find('.', atPos + 1) != std::string::npos;
	}

	bool TryParsePort(
		const std::string& value,
		int& parsed)
	{
		if (value.empty())
		{
			return false;
		}

		for (const char ch : value)
		{
			if (!std::isdigit(static_cast<unsigned char>(ch)))
			{
				return false;
			}
		}

		try
		{
			parsed = std::stoi(value);
		}
		catch (...)
		{
			return false;
		}

		return parsed > 0 && parsed <= 65535;
	}

	void AppendChatProcedureStatusLine(const CString& line)
	{
		auto* mainFrame =
			dynamic_cast<CMainFrame*>(AfxGetMainWnd());
		if (mainFrame == nullptr)
		{
			return;
		}

		mainFrame->AddChatStatusLine(line);
	}

	void AppendChatProcedureStatusBlock(const std::string& text)
	{
		auto* mainFrame =
			dynamic_cast<CMainFrame*>(AfxGetMainWnd());
		if (mainFrame == nullptr)
		{
			return;
		}

		mainFrame->AddChatStatusBlock(CString(CA2W(text.c_str(), CP_UTF8)));
	}

	void AppendChatProcedureStatusLine(const wchar_t* stage)
	{
		if (stage == nullptr)
		{
			return;
		}

		CString line;
		line.Format(L"[Chat] %s", stage);
		AppendChatProcedureStatusLine(line);
	}

	void AppendChatProcedureStatusLine(
		const wchar_t* stage,
		const std::string& detail)
	{
		CStringW detailW(CA2W(detail.c_str(), CP_UTF8));
		CString line;
		line.Format(
			L"[Chat] %s - %s",
			(stage != nullptr ? stage : L"stage"),
			detailW.GetString());
		AppendChatProcedureStatusLine(line);
	}

	std::wstring ToWide(const std::string& value)
	{
		if (value.empty())
		{
			return {};
		}

		const int sizeNeeded = MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0);
		if (sizeNeeded <= 0)
		{
			std::wstring fallback;
			fallback.reserve(value.size());
			for (const char ch : value)
			{
				fallback.push_back(static_cast<wchar_t>(
					static_cast<unsigned char>(ch)));
			}
			return fallback;
		}

		std::wstring output(sizeNeeded, L'\0');
		const int written = MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			output.data(),
			sizeNeeded);
		if (written <= 0)
		{
			return {};
		}

		return output;
	}

	std::string EscapeJson(const std::string& value)
	{
		std::string escaped;
		escaped.reserve(value.size() + 8);
		for (const char ch : value)
		{
			switch (ch)
			{
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

	bool TryExtractJsonStringAfterKey(
		const std::string& json,
		const std::size_t start,
		const std::string& key,
		std::string& outValue)
	{
		outValue.clear();
		if (start >= json.size())
		{
			return false;
		}

		const std::string token = "\"" + key + "\":\"";
		const std::size_t keyPos = json.find(token, start);
		if (keyPos == std::string::npos)
		{
			return false;
		}

		std::size_t cursor = keyPos + token.size();
		bool escaping = false;
		while (cursor < json.size())
		{
			const char ch = json[cursor++];
			if (escaping)
			{
				switch (ch)
				{
				case 'n':
					outValue.push_back('\n');
					break;
				case 'r':
					outValue.push_back('\r');
					break;
				case 't':
					outValue.push_back('\t');
					break;
				case '\\':
				case '"':
					outValue.push_back(ch);
					break;
				default:
					outValue.push_back(ch);
					break;
				}
				escaping = false;
				continue;
			}

			if (ch == '\\')
			{
				escaping = true;
				continue;
			}

			if (ch == '"')
			{
				return true;
			}

			outValue.push_back(ch);
		}

		return false;
	}

	std::string TryExtractFinalAssistantText(
		const std::string& eventsRaw)
	{
		const std::size_t finalPos =
			eventsRaw.find("\"state\":\"final\"");
		if (finalPos == std::string::npos)
		{
			return {};
		}

		std::string extracted;
		if (TryExtractJsonStringAfterKey(
			eventsRaw,
			finalPos,
			"text",
			extracted))
		{
			return extracted;
		}

		return {};
	}

	std::string JsonString(const std::string& value)
	{
		return std::string("\"") + EscapeJson(value) + "\"";
	}

	std::string TrimAscii(const std::string& value)
	{
		const std::size_t first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos)
		{
			return {};
		}

		const std::size_t last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	std::string ToLowerAscii(const std::string& value)
	{
		std::string lowered = value;
		std::transform(
			lowered.begin(),
			lowered.end(),
			lowered.begin(),
			[](const unsigned char ch)
			{
				return static_cast<char>(std::tolower(ch));
			});
		return lowered;
	}

	bool ParseEnvBool(
		const std::string& value,
		const bool fallback)
	{
		const std::string lowered = ToLowerAscii(TrimAscii(value));
		if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
		{
			return true;
		}

		if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
		{
			return false;
		}

		return fallback;
	}

	std::unordered_map<std::string, std::string> ParseDotEnvPairs(
		const std::string& envContent)
	{
		std::unordered_map<std::string, std::string> values;
		std::istringstream stream(envContent);
		std::string line;
		while (std::getline(stream, line))
		{
			std::string trimmed = TrimAscii(line);
			if (trimmed.empty() || trimmed[0] == '#')
			{
				continue;
			}

			if (!trimmed.empty() && trimmed.back() == '\r')
			{
				trimmed.pop_back();
				trimmed = TrimAscii(trimmed);
			}

			const std::size_t eqPos = trimmed.find('=');
			if (eqPos == std::string::npos || eqPos == 0)
			{
				continue;
			}

			std::string key = TrimAscii(trimmed.substr(0, eqPos));
			std::string value = TrimAscii(trimmed.substr(eqPos + 1));
			if (!value.empty() &&
				value.size() >= 2 &&
				((value.front() == '"' && value.back() == '"') ||
					(value.front() == '\'' && value.back() == '\'')))
			{
				value = value.substr(1, value.size() - 2);
			}

			if (!key.empty())
			{
				values.insert_or_assign(key, value);
			}
		}

		return values;
	}

	std::string ResolvePreferredServerPreset(const std::string& smtpHost)
	{
		const std::string lowered = ToLowerAscii(TrimAscii(smtpHost));
		if (lowered == "smtp.163.com") return "163.com";
		if (lowered == "smtp.vip.163.com") return "vip.163.com";
		if (lowered == "smtp.126.com") return "126.com";
		if (lowered == "smtp.vip.126.com") return "vip.126.com";
		if (lowered == "smtp.188.com") return "188.com";
		if (lowered == "smtp.vip.188.com") return "vip.188.com";
		if (lowered == "smtp.yeah.net") return "yeah.net";
		if (lowered == "smtp.gmail.com") return "gmail.com";
		if (lowered == "smtp.office365.com") return "outlook.com";
		if (lowered == "smtp.qq.com") return "qq.com";
		return {};
	}

	std::string BuildBridgeRpcResultJson(
		const blazeclaw::gateway::protocol::ResponseFrame& response,
		const std::string& correlationId)
	{
		std::string json =
			"{\"channel\":\"blazeclaw.gateway.rpc.result\",\"id\":" +
			JsonString(correlationId) +
			",\"ok\":" +
			(response.ok ? "true" : "false");

		if (response.ok)
		{
			if (response.payloadJson.has_value())
			{
				json += ",\"payload\":" + response.payloadJson.value();
			}
			else
			{
				json += ",\"payload\":null";
			}
		}
		else
		{
			json += ",\"error\":";
			if (response.error.has_value())
			{
				const auto& error = response.error.value();
				json += "{";
				json += "\"code\":" + JsonString(error.code);
				json += ",\"message\":" + JsonString(error.message);
				if (error.detailsJson.has_value())
				{
					json += ",\"details\":" + error.detailsJson.value();
				}
				json += "}";
			}
			else
			{
				json +=
					"{\"code\":\"error_unknown\",\"message\":\"Gateway request failed.\"}";
			}
		}

		json += "}";
		return json;
	}

	bool IsToolExecuteMethod(const std::string& method)
	{
		return method == "gateway.tools.call.execute";
	}

	struct ToolLifecycleStartInfo
	{
		std::string tool = "unknown";
		std::string action;
	};

	struct ToolLifecycleResultInfo
	{
		std::string tool = "unknown";
		std::string status = "ok";
		bool executed = false;
		std::string phase = "result";
		std::string code;
	};

	ToolLifecycleStartInfo ParseToolStartInfo(
		const std::optional<std::string>& paramsJson)
	{
		ToolLifecycleStartInfo info;
		if (!paramsJson.has_value())
		{
			return info;
		}

		blazeclaw::gateway::json::FindStringField(
			paramsJson.value(),
			"tool",
			info.tool);
		if (info.tool.empty())
		{
			info.tool = "unknown";
		}

		std::string argsRaw;
		if (blazeclaw::gateway::json::FindRawField(
			paramsJson.value(),
			"args",
			argsRaw))
		{
			blazeclaw::gateway::json::FindStringField(
				argsRaw,
				"action",
				info.action);
		}

		return info;
	}

	std::string BuildToolStartDetail(
		const std::optional<std::string>& paramsJson)
	{
		const auto info = ParseToolStartInfo(paramsJson);
		std::string detail = "tool=" + info.tool;
		if (!info.action.empty())
		{
			detail += " action=" + info.action;
		}

		return detail;
	}

	ToolLifecycleResultInfo ParseToolResultInfo(
		const blazeclaw::gateway::protocol::ResponseFrame& response)
	{
		ToolLifecycleResultInfo info;
		if (!response.ok)
		{
			info.phase = "error";
			info.status = "error";
			if (response.error.has_value())
			{
				info.code = response.error->code;
			}
			return info;
		}

		if (!response.payloadJson.has_value())
		{
			return info;
		}

		const std::string& payload = response.payloadJson.value();
		blazeclaw::gateway::json::FindStringField(payload, "tool", info.tool);
		blazeclaw::gateway::json::FindStringField(payload, "status", info.status);
		blazeclaw::gateway::json::FindBoolField(payload, "executed", info.executed);

		if (info.tool.empty())
		{
			info.tool = "unknown";
		}
		if (info.status.empty())
		{
			info.status = "ok";
		}

		if (info.status == "needs_approval")
		{
			info.phase = "approval-needed";
		}
		else if (info.status == "cancelled")
		{
			info.phase = "approval-resolved";
		}
		else if (info.status == "ok")
		{
			info.phase = "result";
		}
		else
		{
			info.phase = "error";
		}

		return info;
	}

	std::string BuildToolResultDetail(
		const blazeclaw::gateway::protocol::ResponseFrame& response)
	{
		const auto info = ParseToolResultInfo(response);
		if (info.phase == "error" && !info.code.empty())
		{
			return "status=error code=" + info.code;
		}

		std::string detail =
			"tool=" + info.tool +
			" status=" + info.status +
			" executed=" + (info.executed ? "true" : "false");

		if (!info.phase.empty())
		{
			detail += " phase=" + info.phase;
		}

		if (response.ok && response.payloadJson.has_value())
		{
			std::string output;
			if (blazeclaw::gateway::json::FindStringField(
				response.payloadJson.value(),
				"output",
				output) && !output.empty())
			{
				detail += " output=" + output;
			}
		}

		return detail;
	}

	std::string BuildToolLifecycleStartJson(
		const std::string& channel,
		const std::string& requestId,
		const std::optional<std::string>& paramsJson)
	{
		const auto info = ParseToolStartInfo(paramsJson);
		std::string json =
			"{\"channel\":\"blazeclaw.gateway.tools.lifecycle\",\"phase\":\"start\","
			"\"source\":" + JsonString(channel) +
			",\"id\":" + JsonString(requestId) +
			",\"tool\":" + JsonString(info.tool);
		if (!info.action.empty())
		{
			json += ",\"action\":" + JsonString(info.action);
		}

		json += "}";
		return json;
	}

	std::string BuildToolLifecycleResultJson(
		const std::string& channel,
		const std::string& requestId,
		const blazeclaw::gateway::protocol::ResponseFrame& response)
	{
		const auto info = ParseToolResultInfo(response);
		std::string json =
			"{\"channel\":\"blazeclaw.gateway.tools.lifecycle\",\"phase\":" +
			JsonString(info.phase.empty() ? "result" : info.phase) +
			",\"source\":" + JsonString(channel) +
			",\"id\":" + JsonString(requestId) +
			",\"tool\":" + JsonString(info.tool) +
			",\"status\":" + JsonString(info.status) +
			",\"executed\":" + std::string(info.executed ? "true" : "false");

		if (!info.code.empty())
		{
			json += ",\"code\":" + JsonString(info.code);
		}

		json += "}";
		return json;
	}

	std::string BuildOpenClawWsResponseFrameJson(
		const blazeclaw::gateway::protocol::ResponseFrame& response,
		const std::string& correlationId)
	{
		std::string frame =
			"{\"type\":\"res\",\"id\":" +
			JsonString(correlationId) +
			",\"ok\":" +
			(response.ok ? "true" : "false");

		if (response.ok)
		{
			if (response.payloadJson.has_value())
			{
				frame += ",\"payload\":" + response.payloadJson.value();
			}
		}
		else
		{
			frame += ",\"error\":";
			if (response.error.has_value())
			{
				const auto& error = response.error.value();
				frame += "{";
				frame += "\"code\":" + JsonString(error.code);
				frame += ",\"message\":" + JsonString(error.message);
				if (error.detailsJson.has_value())
				{
					frame += ",\"details\":" + error.detailsJson.value();
				}
				frame += "}";
			}
			else
			{
				frame +=
					"{\"code\":\"UNAVAILABLE\",\"message\":\"request failed\"}";
			}
		}

		frame += "}";
		return frame;
	}

	std::string BuildOpenClawHelloPayloadJson()
	{
		return
			"{"
			"\"type\":\"hello-ok\","
			"\"protocol\":3,"
			"\"server\":{\"version\":\"blazeclaw-mfc\",\"connId\":\"webview2-bridge\"},"
			"\"features\":{"
			"\"methods\":[\"chat.history\",\"chat.send\",\"chat.abort\",\"chat.events.poll\"],"
			"\"events\":[\"chat\"]"
			"},"
			"\"snapshot\":{},"
			"\"policy\":{\"tickIntervalMs\":1000}"
			"}";
	}

	std::optional<std::wstring> GetEnvValue(const wchar_t* name)
	{
		if (name == nullptr || *name == L'\0')
		{
			return std::nullopt;
		}

		size_t valueLength = 0;
		wchar_t* rawValue = nullptr;
		if (_wdupenv_s(&rawValue, &valueLength, name) != 0 || rawValue == nullptr)
		{
			return std::nullopt;
		}

		std::wstring value(rawValue);
		free(rawValue);
		if (value.empty())
		{
			return std::nullopt;
		}

		return value;
	}

	std::wstring TrimCopy(std::wstring value)
	{
		auto isSpace = [](wchar_t ch)
			{
				return ::iswspace(ch) != 0;
			};

		while (!value.empty() && isSpace(value.front()))
		{
			value.erase(value.begin());
		}

		while (!value.empty() && isSpace(value.back()))
		{
			value.pop_back();
		}

		return value;
	}

	std::wstring BuildFileUrl(const std::filesystem::path& filePath)
	{
		std::wstring genericPath = filePath.lexically_normal().generic_wstring();
		if (!genericPath.empty() && genericPath.front() != L'/')
		{
			genericPath.insert(genericPath.begin(), L'/');
		}

		std::wstring escaped;
		escaped.reserve(genericPath.size() + 8);
		for (const wchar_t ch : genericPath)
		{
			switch (ch)
			{
			case L' ':
				escaped += L"%20";
				break;
			case L'#':
				escaped += L"%23";
				break;
			default:
				escaped.push_back(ch);
				break;
			}
		}

		return L"file://" + escaped;
	}

	std::wstring ResolveEmailConfigStartupUrl()
	{
		std::vector<std::filesystem::path> roots;
		roots.push_back(std::filesystem::current_path());

		wchar_t modulePath[MAX_PATH]{};
		if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
		{
			roots.push_back(std::filesystem::path(modulePath).parent_path());
		}

		for (const auto& root : roots)
		{
			if (const auto found = FindEmailConfigHtml(root); found.has_value())
			{
				return BuildFileUrl(found.value());
			}
		}

		return {};
	}

	std::optional<std::filesystem::path> FindOpenClawUiIndex(const std::filesystem::path& start)
	{
		std::filesystem::path cursor = start;
		while (!cursor.empty())
		{
			const auto dist =
				cursor /
				L"blazeclaw" /
				L"BlazeClawMfc" /
				L"web" /
				L"chat" /
				L"dist" /
				L"index.html";
			if (std::filesystem::exists(dist))
			{
				return dist;
			}

			const auto direct =
				cursor /
				L"blazeclaw" /
				L"BlazeClawMfc" /
				L"web" /
				L"chat" /
				L"index.html";
			if (std::filesystem::exists(direct))
			{
				return direct;
			}

			if (!cursor.has_parent_path())
			{
				break;
			}

			auto parent = cursor.parent_path();
			if (parent == cursor)
			{
				break;
			}

			cursor = parent;
		}

		return std::nullopt;
	}

	std::wstring ResolveChatStartupUrl()
	{
		if (const auto envUrl = GetEnvValue(L"OPENCLAW_UI_DEV_URL"); envUrl.has_value())
		{
			const std::wstring value = TrimCopy(envUrl.value());
			if (!value.empty())
			{
				return value;
			}
		}

		if (const auto envUrl = GetEnvValue(L"BLAZECLAW_CHAT_DEV_URL"); envUrl.has_value())
		{
			const std::wstring value = TrimCopy(envUrl.value());
			if (!value.empty())
			{
				return value;
			}
		}

		const auto mode = GetEnvValue(L"BLAZECLAW_CHAT_UI_MODE");
		if (mode.has_value())
		{
			std::wstring normalized = TrimCopy(mode.value());
			for (wchar_t& ch : normalized)
			{
				ch = static_cast<wchar_t>(::towlower(ch));
			}

			if (normalized == L"dev")
			{
				return L"http://127.0.0.1:5173";
			}
		}

		std::vector<std::filesystem::path> roots;
		roots.push_back(std::filesystem::current_path());

		wchar_t modulePath[MAX_PATH]{};
		if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0)
		{
			roots.push_back(std::filesystem::path(modulePath).parent_path());
		}

		for (const auto& root : roots)
		{
			if (const auto found = FindOpenClawUiIndex(root); found.has_value())
			{
				return BuildFileUrl(found.value());
			}
		}

		return {};
	}

	void ShowChatStartupError(
		ICoreWebView2* webView,
		const wchar_t* title,
		const wchar_t* details)
	{
		if (webView == nullptr)
		{
			return;
		}

		std::wstring html =
			L"<html><body style='font-family:Segoe UI;padding:20px;'>"
			L"<h2>BlazeClaw Chat Startup Error</h2>"
			L"<p><strong>";
		html += (title != nullptr ? title : L"Unable to load chat UI");
		html += L"</strong></p><p>";
		html += (details != nullptr ? details : L"");
		html += L"</p></body></html>";

		webView->NavigateToString(html.c_str());
	}

} // namespace


// CBlazeClawMFCView

IMPLEMENT_DYNCREATE(CBlazeClawMFCView, CView)

BEGIN_MESSAGE_MAP(CBlazeClawMFCView, CView)
	// Standard printing commands
	ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CBlazeClawMFCView::OnFilePrintPreview)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_TIMER()
END_MESSAGE_MAP()

// CBlazeClawMFCView construction/destruction

CBlazeClawMFCView::CBlazeClawMFCView() noexcept
{
	EnableActiveAccessibility();
}

CBlazeClawMFCView::~CBlazeClawMFCView()
{}

BOOL CBlazeClawMFCView::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	return CView::PreCreateWindow(cs);
}

// CBlazeClawMFCView drawing

void CBlazeClawMFCView::OnDraw(CDC* pDC)
{
	//CBlazeClawMFCDoc* pDoc = GetDocument();
	//ASSERT_VALID(pDoc);
	//if (!pDoc)
	//	return;

	// If WebView2 is available and initialized it will paint itself. Otherwise draw a placeholder.
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webView)
	{
		// nothing: WebView2 paints itself
		return;
	}
#endif
	if (!pDC)
		return;

	CRect rc;
	GetClientRect(&rc);
	COLORREF bgcolor = GetSysColor(COLOR_WINDOW);
	pDC->FillSolidRect(&rc, bgcolor);
	CString msg = L"WebView2 content not available. Ensure WebView2 runtime is installed.";
	pDC->SetTextColor(RGB(0, 0, 0));
	pDC->SetBkMode(TRANSPARENT);
	pDC->DrawText(msg, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}


// CBlazeClawMFCView printing


void CBlazeClawMFCView::OnFilePrintPreview()
{
#ifndef SHARED_HANDLERS
	AFXPrintPreview(this);
#endif
}

void CBlazeClawMFCView::TraceBridgeTraffic(
	const char* kind,
	const std::string& detail)
{
#ifdef _DEBUG
	if (kind == nullptr)
	{
		return;
	}

	const CStringW kindW(CA2W(kind, CP_UTF8));
	const CStringW detailW(CA2W(detail.c_str(), CP_UTF8));
	if (!detail.empty())
	{
		TRACE(
			L"[Bridge][%s] %s\n",
			kindW.GetString(),
			detailW.GetString());
	}
	else
	{
		TRACE(L"[Bridge][%s]\n", kindW.GetString());
	}
#else
	UNREFERENCED_PARAMETER(kind);
	UNREFERENCED_PARAMETER(detail);
#endif
}

void CBlazeClawMFCView::FlushBridgeTraceIfNeeded()
{
#ifdef _DEBUG
	const std::uint64_t nowMs = GetTickCount64();
	if (m_bridgeTraceLastFlushTickMs != 0 &&
		(nowMs - m_bridgeTraceLastFlushTickMs) < kBridgeTraceFlushIntervalMs)
	{
		return;
	}

	m_bridgeTraceLastFlushTickMs = nowMs;
	TRACE(
		L"[Bridge][Counters] req=%llu res=%llu evt=%llu seq=%llu\n",
		static_cast<unsigned long long>(m_bridgeTraceReqCount),
		static_cast<unsigned long long>(m_bridgeTraceResCount),
		static_cast<unsigned long long>(m_bridgeTraceEventCount),
		static_cast<unsigned long long>(m_bridgeEventSeq));
#endif
}

void CBlazeClawMFCView::PostOpenClawWsFrameJson(const std::string& frameJson)
{
	if (frameJson.empty())
	{
		return;
	}

	std::string frameType;
	blazeclaw::gateway::json::FindStringField(frameJson, "type", frameType);
	if (frameType == "res")
	{
		++m_bridgeTraceResCount;
		TraceBridgeTraffic("ws.res", frameJson);
		AppendChatProcedureStatusLine(L"bridge.ws.res");
	}
	else if (frameType == "event")
	{
		++m_bridgeTraceEventCount;
		TraceBridgeTraffic("ws.event", frameJson);
		AppendChatProcedureStatusLine(L"bridge.ws.event");
	}

	FlushBridgeTraceIfNeeded();

	const std::wstring wide = ToWide(
		std::string(
			"{\"channel\":\"openclaw.ws.frame\",\"frame\":") +
		frameJson +
		"}");
	PostBridgeMessageJson(wide);
}

void CBlazeClawMFCView::PostOpenClawWsClose(
	const std::uint16_t code,
	const char* reason)
{
	TraceBridgeTraffic(
		"ws.close",
		std::string("code=") + std::to_string(code) +
		",reason=" + (reason != nullptr ? reason : "closed"));
	AppendChatProcedureStatusLine(L"bridge.ws.close");
	FlushBridgeTraceIfNeeded();

	const std::string closeJson =
		std::string("{\"channel\":\"openclaw.ws.close\",\"code\":") +
		std::to_string(code) +
		",\"reason\":" +
		JsonString(reason != nullptr ? reason : "closed") +
		"}";
	PostBridgeMessageJson(ToWide(closeJson));
}

void CBlazeClawMFCView::EmitOpenClawChatEvents(
	const std::string& eventsArrayJson)
{
	std::string seqRaw = "1";
	if (!eventsArrayJson.empty())
	{
		const auto array = blazeclaw::gateway::json::Trim(eventsArrayJson);
		if (array.size() >= 2 && array.front() == '[' && array.back() == ']')
		{
			std::size_t index = 1;
			while (index < array.size() - 1)
			{
				while (index < array.size() - 1 &&
					(std::isspace(static_cast<unsigned char>(array[index])) != 0 ||
						array[index] == ','))
				{
					++index;
				}

				if (index >= array.size() - 1 || array[index] != '{')
				{
					break;
				}

				const std::size_t start = index;
				int depth = 0;
				bool inString = false;
				for (; index < array.size() - 1; ++index)
				{
					const char ch = array[index];
					if (inString)
					{
						if (ch == '\\')
						{
							++index;
							continue;
						}
						if (ch == '"')
						{
							inString = false;
						}
						continue;
					}

					if (ch == '"')
					{
						inString = true;
						continue;
					}

					if (ch == '{')
					{
						++depth;
					}
					else if (ch == '}')
					{
						--depth;
						if (depth == 0)
						{
							const std::string payload = array.substr(start, (index - start) + 1);
							++m_bridgeEventSeq;
							const std::string frame =
								"{\"type\":\"event\",\"event\":\"chat\",\"payload\":" +
								payload +
								",\"seq\":" +
								std::to_string(m_bridgeEventSeq) +
								"}";
							PostOpenClawWsFrameJson(frame);
							++index;
							break;
						}
					}
				}
			}
		}
	}
}

void CBlazeClawMFCView::EnsureOpenClawBridgeShim()
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (!m_webView)
	{
		return;
	}

	const wchar_t* shimScript = LR"JS(
(function() {
  if (window.__blazeclawBridgeInjected) return;
  window.__blazeclawBridgeInjected = true;

  if (!window.chrome || !window.chrome.webview) return;

  window.chrome.webview.postMessage({
	channel: 'openclaw.ws.shim.ready',
	phase: 'boot',
	href: String(window.location && window.location.href ? window.location.href : '')
  });

  const listeners = { open: [], message: [], close: [], error: [] };
  let activeSocket = null;
  let syntheticUrl = 'ws://127.0.0.1:18789';
  let connectTimer = null;

  function emit(type, evt) {
	const arr = listeners[type] || [];
	for (const fn of arr) {
	  try { fn(evt); } catch (_) {}
	}
  }

  function scheduleOpen() {
	if (connectTimer) clearTimeout(connectTimer);
	connectTimer = setTimeout(() => {
     if (!activeSocket) return;
	  activeSocket.readyState = WebViewGatewaySocket.OPEN;
     window.chrome.webview.postMessage({
		channel: 'openclaw.ws.shim.ready',
		url: syntheticUrl,
		readyState: activeSocket.readyState
	  });
	  emit('open', { type: 'open' });
	  window.chrome.webview.postMessage({
		channel: 'openclaw.ws.req',
		frame: {
		  type: 'req',
		  id: 'bridge-connect-challenge',
		  method: 'connect.challenge',
		  params: {}
		}
	  });
	}, 0);
  }

  class WebViewGatewaySocket {
	constructor(url) {
      activeSocket = this;
	  syntheticUrl = typeof url === 'string' && url.length ? url : syntheticUrl;
	  this.url = syntheticUrl;
    this.readyState = WebViewGatewaySocket.CONNECTING;
	  this.binaryType = 'arraybuffer';
	  scheduleOpen();
	}

	addEventListener(type, handler) {
	  if (!listeners[type]) return;
	  listeners[type].push(handler);
	}

	removeEventListener(type, handler) {
	  if (!listeners[type]) return;
	  const index = listeners[type].indexOf(handler);
	  if (index >= 0) listeners[type].splice(index, 1);
	}

	send(raw) {
   if (this.readyState !== WebViewGatewaySocket.OPEN) return;
	  let frame = null;
	  try { frame = JSON.parse(String(raw || '')); } catch (_) {}
	  if (!frame || frame.type !== 'req') return;
	  window.chrome.webview.postMessage({
		channel: 'openclaw.ws.req',
		frame
	  });
	}

	close(code, reason) {
     this.readyState = WebViewGatewaySocket.CLOSED;
	  if (activeSocket === this) {
		activeSocket = null;
	  }
	  emit('close', {
		type: 'close',
		code: typeof code === 'number' ? code : 1000,
		reason: typeof reason === 'string' ? reason : 'closed'
	  });
	}
  }

  WebViewGatewaySocket.CONNECTING = 0;
  WebViewGatewaySocket.OPEN = 1;
  WebViewGatewaySocket.CLOSING = 2;
  WebViewGatewaySocket.CLOSED = 3;

  window.chrome.webview.addEventListener('message', (event) => {
	const msg = event && event.data;
	if (!msg || typeof msg !== 'object') return;
	if (msg.channel === 'openclaw.ws.frame' && msg.frame) {
	  emit('message', { data: JSON.stringify(msg.frame) });
	  return;
	}
	if (msg.channel === 'openclaw.ws.close') {
     if (activeSocket) {
		activeSocket.readyState = WebViewGatewaySocket.CLOSED;
		activeSocket = null;
	  }
	  emit('close', {
		type: 'close',
		code: typeof msg.code === 'number' ? msg.code : 1006,
		reason: typeof msg.reason === 'string' ? msg.reason : 'closed'
	  });
	}
  });

  window.WebSocket = WebViewGatewaySocket;
  window.__OPENCLAW_CONTROL_UI_BASE_PATH__ = '/';
})();
)JS";

	m_webView->AddScriptToExecuteOnDocumentCreated(shimScript, nullptr);
#endif
}

BOOL CBlazeClawMFCView::OnPreparePrinting(CPrintInfo* pInfo)
{
	// default preparation
	return DoPreparePrinting(pInfo);
}

void CBlazeClawMFCView::PostBridgeMessageJson(const std::wstring& jsonMessage)
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (!m_webView || jsonMessage.empty())
	{
		return;
	}

	m_webView->PostWebMessageAsJson(jsonMessage.c_str());
#else
	UNREFERENCED_PARAMETER(jsonMessage);
#endif
}

void CBlazeClawMFCView::PostBridgeLifecycleEvent(
	const wchar_t* state,
	const wchar_t* reason,
	const std::string& provider,
	const std::string& model,
	const std::string& runtimeKind)
{
	std::string payload =
		"{\"channel\":\"blazeclaw.gateway.lifecycle\",\"sessionId\":" +
		JsonString(m_bridgeSessionId) +
		",\"state\":" +
		JsonString(state != nullptr ? ToNarrow(state) : std::string("unknown"));

	if (reason != nullptr && *reason != L'\0')
	{
		payload += ",\"reason\":" + JsonString(ToNarrow(reason));
	}

	if (!provider.empty())
	{
		payload += ",\"provider\":" + JsonString(provider);
	}

	if (!model.empty())
	{
		payload += ",\"model\":" + JsonString(model);
	}

	if (!runtimeKind.empty())
	{
		payload += ",\"runtimeKind\":" + JsonString(runtimeKind);
	}

	payload += "}";
	PostBridgeMessageJson(ToWide(payload));
}

void CBlazeClawMFCView::PumpBridgeLifecycle()
{
	const auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	const bool connected = app != nullptr && app->Services().IsRunning();
	const std::string provider = (connected && app != nullptr)
		? app->Services().ActiveChatProvider()
		: std::string();
	const std::string model = (connected && app != nullptr)
		? app->Services().ActiveChatModel()
		: std::string();
	const std::string runtimeKind = provider == "deepseek"
		? "remote"
		: (provider.empty() ? std::string() : "local");

	if (!m_bridgeLifecycleSent)
	{
		AppendChatProcedureStatusLine(
			connected ? L"lifecycle.connected" : L"lifecycle.disconnected");
		PostBridgeLifecycleEvent(
			connected ? L"connected" : L"disconnected",
			connected ? L"service-ready" : L"service-not-running",
			provider,
			model,
			runtimeKind);
		m_bridgeLifecycleSent = true;
		m_bridgeLastConnected = connected;
		m_bridgeLastProvider = provider;
		m_bridgeLastModel = model;
		m_bridgeLastRuntimeKind = runtimeKind;
	}
	else if (connected != m_bridgeLastConnected)
	{
		if (connected)
		{
			AppendChatProcedureStatusLine(L"lifecycle.reconnected");
			PostBridgeLifecycleEvent(
				L"reconnected",
				L"service-ready",
				provider,
				model,
				runtimeKind);
		}
		else
		{
			AppendChatProcedureStatusLine(L"lifecycle.service-stopped");
			PostBridgeLifecycleEvent(L"disconnected", L"service-stopped");
			PostOpenClawWsClose(1001, "gateway disconnected");
		}

		m_bridgeLastConnected = connected;
		m_bridgeLastProvider = connected ? provider : std::string();
		m_bridgeLastModel = connected ? model : std::string();
		m_bridgeLastRuntimeKind = connected ? runtimeKind : std::string();
	}

	if (connected &&
		m_bridgeLifecycleSent &&
		(provider != m_bridgeLastProvider ||
			model != m_bridgeLastModel ||
			runtimeKind != m_bridgeLastRuntimeKind))
	{
		AppendChatProcedureStatusLine(L"lifecycle.runtime-updated");
		PostBridgeLifecycleEvent(
			L"connected",
			L"runtime-updated",
			provider,
			model,
			runtimeKind);
		m_bridgeLastProvider = provider;
		m_bridgeLastModel = model;
		m_bridgeLastRuntimeKind = runtimeKind;
	}

	if (!connected || app == nullptr)
	{
		return;
	}

	const blazeclaw::gateway::protocol::RequestFrame pollRequest{
		.id = "bridge-chat-events",
		.method = "chat.events.poll",
		.paramsJson =
			std::string("{\"sessionKey\":\"") +
			m_bridgeSessionId +
			"\",\"limit\":20}",
	};

	const auto pollResponse = app->Services().RouteGatewayRequest(pollRequest);
	if (!pollResponse.ok || !pollResponse.payloadJson.has_value())
	{
		AppendChatProcedureStatusLine(L"events.poll.failed");
		return;
	}

	std::string eventsRaw;
	if (!blazeclaw::gateway::json::FindRawField(
		pollResponse.payloadJson.value(),
		"events",
		eventsRaw))
	{
		return;
	}

	if (blazeclaw::gateway::json::Trim(eventsRaw) == "[]")
	{
		return;
	}

	AppendChatProcedureStatusLine(L"events.poll.batch", eventsRaw);

	const std::string finalAssistantText =
		TryExtractFinalAssistantText(eventsRaw);
	if (!finalAssistantText.empty())
	{
		AppendChatProcedureStatusLine(
			L"localModel.response",
			finalAssistantText);
	}

	const std::string envelope =
		"{\"channel\":\"blazeclaw.gateway.chat.events\",\"sessionId\":" +
		JsonString(m_bridgeSessionId) +
		",\"events\":" +
		eventsRaw +
		"}";
	PostBridgeMessageJson(ToWide(envelope));
	EmitOpenClawChatEvents(eventsRaw);
}

void CBlazeClawMFCView::HandleWebMessageJson(const std::wstring& webMessageJson)
{
#ifdef HAVE_WEBVIEW2_HEADER
	const std::string message = ToNarrow(webMessageJson);
	if (HandleSkillConfigBridgeMessage(message))
	{
		return;
	}

	if (HandleEmailConfigBridgeMessage(message))
	{
		return;
	}

	std::string channel;
	if (!blazeclaw::gateway::json::FindStringField(message, "channel", channel))
	{
		return;
	}

	if (channel == "blazeclaw.gateway.lifecycle.subscribe")
	{
		m_bridgeLifecycleSent = false;
		PumpBridgeLifecycle();
		return;
	}

	if (channel == "openclaw.ws.shim.ready")
	{
		AppendChatProcedureStatusLine(L"runtime.shim.ready", message);
		return;
	}

	if (channel == "openclaw.ws.req")
	{
		++m_bridgeTraceReqCount;
		TraceBridgeTraffic("ws.req.channel", message);
		AppendChatProcedureStatusLine(L"bridge.ws.req");
		FlushBridgeTraceIfNeeded();

		std::string frameRaw;
		if (!blazeclaw::gateway::json::FindRawField(message, "frame", frameRaw))
		{
			TraceBridgeTraffic("ws.req.invalid", "missing frame field");
			return;
		}

		std::string frameType;
		blazeclaw::gateway::json::FindStringField(frameRaw, "type", frameType);
		if (frameType != "req")
		{
			TraceBridgeTraffic("ws.req.ignored", frameType);
			FlushBridgeTraceIfNeeded();
			return;
		}

		TraceBridgeTraffic("ws.req", frameRaw);

		std::string correlationId;
		if (!blazeclaw::gateway::json::FindStringField(frameRaw, "id", correlationId))
		{
			correlationId = "openclaw-unknown";
		}

		std::string method;
		blazeclaw::gateway::json::FindStringField(frameRaw, "method", method);
		if (method.empty())
		{
			TraceBridgeTraffic("ws.req.invalid", "missing method");
			AppendChatProcedureStatusLine(L"bridge.ws.req.invalid");
			const blazeclaw::gateway::protocol::ResponseFrame errorResponse{
				.id = correlationId,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = blazeclaw::gateway::protocol::ErrorShape{
					.code = "invalid_frame",
					.message = "WebView bridge frame missing method.",
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};
			PostOpenClawWsFrameJson(
				BuildOpenClawWsResponseFrameJson(errorResponse, correlationId));
			return;
		}

		if (method == "connect.challenge")
		{
			TraceBridgeTraffic("ws.req.challenge", correlationId);
			AppendChatProcedureStatusLine(L"bridge.connect.challenge");
			AppendChatProcedureStatusLine(
				L"runtime.handshake",
				"connect.challenge handled");
			++m_bridgeEventSeq;
			const std::string eventFrame =
				"{\"type\":\"event\",\"event\":\"connect.challenge\","
				"\"payload\":{\"nonce\":\"blazeclaw-bridge\"},"
				"\"seq\":" +
				std::to_string(m_bridgeEventSeq) +
				"}";
			PostOpenClawWsFrameJson(eventFrame);
			return;
		}

		if (method == "connect")
		{
			TraceBridgeTraffic("ws.req.connect", correlationId);
			AppendChatProcedureStatusLine(L"bridge.connect");
			AppendChatProcedureStatusLine(
				L"runtime.handshake",
				"connect handled");
			const blazeclaw::gateway::protocol::ResponseFrame helloResponse{
				.id = correlationId,
				.ok = true,
				.payloadJson = BuildOpenClawHelloPayloadJson(),
				.error = std::nullopt,
			};
			PostOpenClawWsFrameJson(
				BuildOpenClawWsResponseFrameJson(helloResponse, correlationId));
			return;
		}

		std::optional<std::string> paramsJson;
		std::string paramsRaw;
		if (blazeclaw::gateway::json::FindRawField(frameRaw, "params", paramsRaw))
		{
			paramsJson = blazeclaw::gateway::json::Trim(paramsRaw);
		}

		if (IsToolExecuteMethod(method))
		{
			AppendChatProcedureStatusLine(
				L"tools.execute.start",
				BuildToolStartDetail(paramsJson));
			PostBridgeMessageJson(ToWide(
				BuildToolLifecycleStartJson(
					"openclaw.ws.req",
					correlationId,
					paramsJson)));
		}

		auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
		if (app == nullptr)
		{
			TraceBridgeTraffic("ws.req.error", "app unavailable");
			AppendChatProcedureStatusLine(L"bridge.req.app_unavailable");
			if (IsToolExecuteMethod(method))
			{
				AppendChatProcedureStatusLine(
					L"tools.execute.error",
					"status=error code=app_unavailable");
			}
			const blazeclaw::gateway::protocol::ResponseFrame errorResponse{
				.id = correlationId,
				.ok = false,
				.payloadJson = std::nullopt,
				.error = blazeclaw::gateway::protocol::ErrorShape{
					.code = "app_unavailable",
					.message = "Application context unavailable.",
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				},
			};
			PostOpenClawWsFrameJson(
				BuildOpenClawWsResponseFrameJson(errorResponse, correlationId));
			return;
		}

		const blazeclaw::gateway::protocol::RequestFrame request{
			.id = correlationId,
			.method = method,
			.paramsJson = paramsJson,
		};
		const auto response = app->Services().RouteGatewayRequest(request);
		TraceBridgeTraffic("ws.req.route", method);
		AppendChatProcedureStatusLine(L"bridge.req.route", method);
		if (IsToolExecuteMethod(method))
		{
			AppendChatProcedureStatusLine(
				response.ok
				? L"tools.execute.result"
				: L"tools.execute.error",
				BuildToolResultDetail(response));
			PostBridgeMessageJson(ToWide(
				BuildToolLifecycleResultJson(
					"openclaw.ws.req",
					correlationId,
					response)));
		}
		PostOpenClawWsFrameJson(
			BuildOpenClawWsResponseFrameJson(response, correlationId));
		return;
	}

	if (channel != "blazeclaw.gateway.rpc")
	{
		return;
	}

	std::string correlationId;
	if (!blazeclaw::gateway::json::FindStringField(message, "id", correlationId))
	{
		correlationId = "rpc-unknown";
	}

	std::string method;
	blazeclaw::gateway::json::FindStringField(message, "method", method);

	std::string paramsJsonRaw;
	std::optional<std::string> paramsJson;
	if (blazeclaw::gateway::json::FindRawField(message, "params", paramsJsonRaw))
	{
		paramsJson = blazeclaw::gateway::json::Trim(paramsJsonRaw);
	}

	if (IsToolExecuteMethod(method))
	{
		AppendChatProcedureStatusLine(
			L"tools.execute.start",
			BuildToolStartDetail(paramsJson));
		PostBridgeMessageJson(ToWide(
			BuildToolLifecycleStartJson(
				"blazeclaw.gateway.rpc",
				correlationId,
				paramsJson)));
	}

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		if (IsToolExecuteMethod(method))
		{
			AppendChatProcedureStatusLine(
				L"tools.execute.error",
				"status=error code=app_unavailable");
		}
		const std::string errorJson =
			"{\"channel\":\"blazeclaw.gateway.rpc.result\",\"id\":" +
			JsonString(correlationId) +
			",\"ok\":false,\"error\":{\"code\":\"app_unavailable\",\"message\":\"Application context unavailable.\"}}";
		PostBridgeMessageJson(ToWide(errorJson));
		return;
	}

	const blazeclaw::gateway::protocol::RequestFrame request{
		.id = correlationId,
		.method = method,
		.paramsJson = paramsJson,
	};
	const auto response = app->Services().RouteGatewayRequest(request);
	if (IsToolExecuteMethod(method))
	{
		AppendChatProcedureStatusLine(
			response.ok
			? L"tools.execute.result"
			: L"tools.execute.error",
			BuildToolResultDetail(response));
		PostBridgeMessageJson(ToWide(
			BuildToolLifecycleResultJson(
				"blazeclaw.gateway.rpc",
				correlationId,
				response)));
	}
	const std::string responseJson = BuildBridgeRpcResultJson(response, correlationId);
	PostBridgeMessageJson(ToWide(responseJson));
#else
	UNREFERENCED_PARAMETER(webMessageJson);
#endif
}

bool CBlazeClawMFCView::HandleSkillConfigBridgeMessage(
	const std::string& messageJson)
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
		PostBridgeMessageJson(ToWide(response));
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
		LoadSkillConfigToBridge(skillKey, correlationId);
		return true;
	}

	if (channel == "blazeclaw.skill.config.save")
	{
		std::string payloadJson;
		if (blazeclaw::gateway::json::FindRawField(messageJson, "payload", payloadJson))
		{
			PersistSkillConfigFromPayload(skillKey, correlationId, payloadJson);
		}
		else
		{
			PersistSkillConfigFromPayload(skillKey, correlationId, messageJson);
		}

		return true;
	}

	if (channel == "blazeclaw.skill.config.validate")
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.validation\",\"skillKey\":" +
			JsonString(skillKey) +
			",\"id\":" +
			JsonString(correlationId) +
			",\"ok\":true,\"fieldErrors\":[]}";
		PostBridgeMessageJson(ToWide(response));
		return true;
	}

	if (channel == "blazeclaw.skill.config.cancel")
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.cancelled\",\"skillKey\":" +
			JsonString(skillKey) +
			",\"id\":" +
			JsonString(correlationId) +
			",\"ok\":true}";
		PostBridgeMessageJson(ToWide(response));
		return true;
	}

	return false;
}

void CBlazeClawMFCView::LoadSkillConfigToBridge(
	const std::string& skillKey,
	const std::string& correlationId)
{
	CBlazeClawMFCDoc* doc = GetDocument();
	if (doc == nullptr)
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
			JsonString(skillKey) +
			",\"id\":" +
			JsonString(correlationId) +
			",\"code\":\"doc_unavailable\",\"message\":\"Document context is unavailable.\"}";
		PostBridgeMessageJson(ToWide(response));
		return;
	}

	std::string envContent;
	std::string loadError;
	std::filesystem::path loadedPath;
	if (!doc->LoadSkillConfigEnv(skillKey, envContent, loadError, &loadedPath))
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.loaded\",\"skillKey\":" +
			JsonString(skillKey) +
			",\"id\":" +
			JsonString(correlationId) +
			",\"ok\":true,\"payload\":{},\"sourceMeta\":{\"configPath\":\"\",\"exists\":false}}";
		PostBridgeMessageJson(ToWide(response));
		return;
	}

	const auto pairs = ParseDotEnvPairs(envContent);
	std::string payload = "{";
	bool first = true;
	for (const auto& pair : pairs)
	{
		if (!first)
		{
			payload += ",";
		}

		payload += JsonString(pair.first);
		payload += ":";
		payload += JsonString(pair.second);
		first = false;
	}
	payload += "}";

	const std::string response =
		"{\"channel\":\"blazeclaw.skill.config.loaded\",\"skillKey\":" +
		JsonString(skillKey) +
		",\"id\":" +
		JsonString(correlationId) +
		",\"ok\":true,\"payload\":" +
		payload +
		",\"sourceMeta\":{\"configPath\":" +
		JsonString(ToNarrow(loadedPath.wstring())) +
		",\"exists\":true,\"sourceOfTruth\":" +
		JsonString("canonical") +
		",\"migratedFromLegacy\":" +
		std::string(loadedPath == doc->GetSkillConfigPath(skillKey) ? "false" : "true") +
		"}}";
	PostBridgeMessageJson(ToWide(response));

	AppendChatProcedureStatusLine(
		L"skills.config.loaded",
		"skill=" + skillKey +
		" path=" + ToNarrow(loadedPath.wstring()) +
		" source=" +
		(loadedPath == doc->GetSkillConfigPath(skillKey) ? "canonical" : "legacy-migrated"));
}

void CBlazeClawMFCView::PersistSkillConfigFromPayload(
	const std::string& skillKey,
	const std::string& correlationId,
	const std::string& payloadJson)
{
	CBlazeClawMFCDoc* doc = GetDocument();
	if (doc == nullptr)
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
			JsonString(skillKey) +
			",\"id\":" +
			JsonString(correlationId) +
			",\"code\":\"doc_unavailable\",\"message\":\"Document context is unavailable.\"}";
		PostBridgeMessageJson(ToWide(response));
		return;
	}

	auto pairs = ParseDotEnvPairs(payloadJson);
	if (pairs.empty())
	{
		std::string payloadRaw;
		if (blazeclaw::gateway::json::FindRawField(payloadJson, "payload", payloadRaw))
		{
			pairs = ParseDotEnvPairs(payloadRaw);
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
				JsonString(skillKey) +
				",\"id\":" +
				JsonString(correlationId) +
				",\"code\":\"invalid_payload\",\"message\":\"No key-value payload was provided.\",\"fieldErrors\":[{\"field\":\"payload\",\"code\":\"required\",\"message\":\"Provide at least one key-value pair.\"}]}";
			PostBridgeMessageJson(ToWide(response));
			return;
		}
	}

	if (pairs.size() > 64)
	{
		const std::string response =
			"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
			JsonString(skillKey) +
			",\"id\":" +
			JsonString(correlationId) +
			",\"code\":\"too_many_fields\",\"message\":\"Too many config fields.\",\"fieldErrors\":[{\"field\":\"payload\",\"code\":\"max_fields\",\"message\":\"Maximum 64 fields are allowed.\"}]}";
		PostBridgeMessageJson(ToWide(response));
		return;
	}

	for (const auto& pair : pairs)
	{
		if (pair.first.empty() || pair.first.size() > 128)
		{
			const std::string response =
				"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
				JsonString(skillKey) +
				",\"id\":" +
				JsonString(correlationId) +
				",\"code\":\"invalid_field_name\",\"message\":\"Invalid field name length.\",\"fieldErrors\":[{\"field\":" +
				JsonString(pair.first) +
				",\"code\":\"max_length\",\"message\":\"Field name must be between 1 and 128 characters.\"}]}";
			PostBridgeMessageJson(ToWide(response));
			return;
		}

		if (pair.second.size() > 4096)
		{
			const std::string response =
				"{\"channel\":\"blazeclaw.skill.config.error\",\"skillKey\":" +
				JsonString(skillKey) +
				",\"id\":" +
				JsonString(correlationId) +
				",\"code\":\"invalid_field_value\",\"message\":\"Field value exceeds maximum length.\",\"fieldErrors\":[{\"field\":" +
				JsonString(pair.first) +
				",\"code\":\"max_length\",\"message\":\"Field value must not exceed 4096 characters.\"}]}";
			PostBridgeMessageJson(ToWide(response));
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
			JsonString(skillKey) +
			",\"id\":" +
			JsonString(correlationId) +
			",\"code\":\"persist_failed\",\"message\":" +
			JsonString(error.empty() ? "Failed to persist skill config." : error) +
			"}";
		PostBridgeMessageJson(ToWide(response));
		return;
	}

	const std::string response =
		"{\"channel\":\"blazeclaw.skill.config.saved\",\"skillKey\":" +
		JsonString(skillKey) +
		",\"id\":" +
		JsonString(correlationId) +
		",\"configPath\":" +
		JsonString(ToNarrow(savedPath.wstring())) +
		",\"updatedChecks\":{},\"ok\":true}";
	PostBridgeMessageJson(ToWide(response));

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app != nullptr)
	{
		// Capture the response to avoid discarding a [[nodiscard]] return value.
		const auto refreshResponse = app->Services().RouteGatewayRequest(
			blazeclaw::gateway::protocol::RequestFrame{
				.id = "skill.config.refresh",
				.method = "gateway.skills.refresh",
				.paramsJson = std::nullopt,
			});

		// Log a status line when refresh fails to aid diagnostics.
		if (!refreshResponse.ok)
		{
			AppendChatProcedureStatusLine(
				L"warning.skills.refresh.failed",
				refreshResponse.error.has_value() ? refreshResponse.error->message : "gateway refresh failed");
		}
	}

	AppendChatProcedureStatusLine(
		L"skills.config.persisted",
		"skill=" + skillKey +
		" path=" + ToNarrow(savedPath.wstring()) +
		" payload=" + TruncateForDiagnostics(
			RedactSensitiveJsonPayload(payloadJson),
			256));

	auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
	if (mainFrame != nullptr)
	{
		mainFrame->RefreshSkillView();
	}
}

bool CBlazeClawMFCView::HandleEmailConfigBridgeMessage(
	const std::string& messageJson)
{
	std::string channel;
	if (!blazeclaw::gateway::json::FindStringField(messageJson, "channel", channel))
	{
		return false;
	}

	if (channel == "blazeclaw.email.config.open")
	{
		const bool opened = OpenEmailConfigDocument();
		const std::string json =
			std::string("{\"channel\":\"blazeclaw.email.config.opened\",\"ok\":") +
			(opened ? "true" : "false") +
			(opened
				? "}"
				: ",\"error\":\"Failed to open email config document.\"}");
		PostBridgeMessageJson(ToWide(json));
		return true;
	}

	if (channel == "blazeclaw.email.config.ready")
	{
		LoadEmailConfigToBridge();
		return true;
	}

	if (channel == "blazeclaw.chat.email.config.open")
	{
		const bool opened = OpenEmailConfigDocument();
		const std::string json =
			std::string("{\"channel\":\"blazeclaw.chat.email.config.opened\",\"ok\":") +
			(opened ? "true" : "false") +
			(opened
				? "}"
				: ",\"error\":\"Failed to open email config document.\"}");
		PostBridgeMessageJson(ToWide(json));
		AppendChatProcedureStatusLine(
			opened
			? L"email.config.chat.opened"
			: L"email.config.chat.open.failed");
		return true;
	}

	if (channel == "blazeclaw.email.config.save")
	{
		std::string payloadJson;
		if (blazeclaw::gateway::json::FindRawField(messageJson, "payload", payloadJson))
		{
			PersistEmailConfigFromPayload(payloadJson);
		}
		else
		{
			PersistEmailConfigFromPayload(messageJson);
		}

		return true;
	}

	if (channel == "blazeclaw.email.config.cancel")
	{
		AppendChatProcedureStatusLine(L"email.config.cancelled");
		PostBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.cancelled\",\"ok\":true}");
		return true;
	}

	return false;
}

void CBlazeClawMFCView::LoadEmailConfigToBridge()
{
	CBlazeClawMFCDoc* doc = GetDocument();
	if (doc == nullptr)
	{
		const std::string json =
			"{\"channel\":\"blazeclaw.email.config.load.error\",\"ok\":false,"
			"\"message\":\"Document context is unavailable.\"}";
		PostBridgeMessageJson(ToWide(json));
		return;
	}

	std::string envContent;
	std::string loadError;
	if (!doc->LoadEmailSkillConfigEnv(envContent, loadError))
	{
		const std::string json =
			"{\"channel\":\"blazeclaw.email.config.load.empty\",\"ok\":false,"
			"\"message\":" +
			JsonString(loadError.empty() ? "No saved config found." : loadError) +
			"}";
		PostBridgeMessageJson(ToWide(json));
		return;
	}

	const auto pairs = ParseDotEnvPairs(envContent);
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
		JsonString(ResolvePreferredServerPreset(smtpHost)) +
		",\"smtpHost\":" + JsonString(smtpHost) +
		",\"smtpPort\":" + JsonString(readOrDefault("SMTP_PORT", "587")) +
		",\"imapHost\":" + JsonString(readOrDefault("IMAP_HOST", "")) +
		",\"imapPort\":" + JsonString(readOrDefault("IMAP_PORT", "993")) +
		",\"email\":" + JsonString(email) +
		",\"password\":" + JsonString(password) +
		",\"allowedReadDirs\":" +
		JsonString(readOrDefault("ALLOWED_READ_DIRS", "~/Downloads,~/Documents")) +
		",\"allowedWriteDirs\":" +
		JsonString(readOrDefault("ALLOWED_WRITE_DIRS", "~/Downloads")) +
		",\"imapTls\":" +
		std::string(ParseEnvBool(readOrDefault("IMAP_TLS", "true"), true)
			? "true"
			: "false") +
		",\"smtpSecure\":" +
		std::string(ParseEnvBool(readOrDefault("SMTP_SECURE", "false"), false)
			? "true"
			: "false") +
		",\"rejectUnauthorized\":" +
		std::string(ParseEnvBool(imapRejectUnauthorized, true)
			? "true"
			: "false") +
		"}}";

	PostBridgeMessageJson(ToWide(json));
}

bool CBlazeClawMFCView::OpenEmailConfigDocument()
{
	const std::wstring configUrl = ResolveEmailConfigStartupUrl();
	if (configUrl.empty())
	{
		AppendChatProcedureStatusLine(
			L"email.config.open.failed",
			"config.html not found");
		return false;
	}

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		AppendChatProcedureStatusLine(
			L"email.config.open.failed",
			"application context unavailable");
		return false;
	}

	auto* chatTemplate = app->GetChatDocTemplate();
	if (chatTemplate == nullptr)
	{
		AppendChatProcedureStatusLine(
			L"email.config.open.failed",
			"chat document template unavailable");
		return false;
	}

	SetPendingStartupUrl(configUrl);
	CDocument* opened = chatTemplate->OpenDocumentFile(nullptr);
	if (opened == nullptr)
	{
		AppendChatProcedureStatusLine(
			L"email.config.open.failed",
			"failed to create document");
		return false;
	}

	AppendChatProcedureStatusLine(L"email.config.opened");
	return true;
}

void CBlazeClawMFCView::PersistEmailConfigFromPayload(
	const std::string& payloadJson)
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
		PostBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"smtp_host_required\",\"message\":\"SMTP host is required.\",\"fieldErrors\":[{\"field\":\"smtpHost\",\"code\":\"required\",\"message\":\"SMTP host is required.\"}]}"
		);
		return;
	}

	if (!TryParsePort(smtpPortRaw, smtpPort))
	{
		PostBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"smtp_port_invalid\",\"message\":\"SMTP port must be an integer between 1 and 65535.\",\"fieldErrors\":[{\"field\":\"smtpPort\",\"code\":\"invalid\",\"message\":\"SMTP port must be an integer between 1 and 65535.\"}]}"
		);
		return;
	}

	if (!TryParsePort(imapPortRaw, imapPort))
	{
		PostBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"imap_port_invalid\",\"message\":\"IMAP port must be an integer between 1 and 65535.\",\"fieldErrors\":[{\"field\":\"imapPort\",\"code\":\"invalid\",\"message\":\"IMAP port must be an integer between 1 and 65535.\"}]}"
		);
		return;
	}

	if (!IsLikelyEmailAddress(email))
	{
		PostBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"code\":\"email_invalid\",\"message\":\"Valid email account is required.\",\"fieldErrors\":[{\"field\":\"email\",\"code\":\"invalid\",\"message\":\"Valid email account is required.\"}]}"
		);
		return;
	}

	if (password.empty())
	{
		PostBridgeMessageJson(
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

	CBlazeClawMFCDoc* doc = GetDocument();
	if (doc == nullptr)
	{
		PostBridgeMessageJson(
			L"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"message\":\"Document context is unavailable.\"}");
		return;
	}

	std::string error;
	if (!doc->SaveEmailSkillConfigEnv(env.str(), error))
	{
		const std::string json =
			"{\"channel\":\"blazeclaw.email.config.error\",\"ok\":false,\"message\":" +
			JsonString(error.empty() ? "Failed to save email config." : error) +
			"}";
		PostBridgeMessageJson(ToWide(json));
		return;
	}

	AppendChatProcedureStatusLine(
		L"email.config.saved",
		ToNarrow(doc->GetEmailSkillConfigPath().wstring()));

	const std::string json =
		"{\"channel\":\"blazeclaw.email.config.saved\",\"ok\":true,\"configPath\":" +
		JsonString(ToNarrow(doc->GetEmailSkillConfigPath().wstring())) +
		"}";
	PostBridgeMessageJson(ToWide(json));
}

void CBlazeClawMFCView::ShowSkillSelection(
	const std::string& skillKey,
	const std::string& propertiesJson)
{
	const std::string normalizedSkillKey =
		blazeclaw::gateway::json::Trim(skillKey).empty()
		? std::string("unknown")
		: skillKey;

	AppendChatProcedureStatusLine(
		L"skills.selection",
		normalizedSkillKey);
	if (!propertiesJson.empty())
	{
		AppendChatProcedureStatusBlock(propertiesJson);
	}

	const std::string payload =
		"{\"channel\":\"blazeclaw.skills.selection\",\"skillKey\":" +
		JsonString(normalizedSkillKey) +
		",\"properties\":" +
		(propertiesJson.empty() ? std::string("{}") : propertiesJson) +
		"}";
	PostBridgeMessageJson(ToWide(payload));

	const bool opened = OpenSkillConfigDocument(normalizedSkillKey, propertiesJson);
	if (opened)
	{
		AppendChatProcedureStatusLine(
			L"skills.config.opened",
			normalizedSkillKey);
	}
}

bool CBlazeClawMFCView::OpenSkillConfigDocument(
	const std::string& skillKey,
	const std::string& propertiesJson)
{
	if (blazeclaw::gateway::json::Trim(skillKey).empty())
	{
		return false;
	}

	const std::wstring configUrl = ResolveSkillConfigStartupUrl(skillKey);
	if (configUrl.empty())
	{
		const std::wstring generatedUrl = BuildGeneratedSkillConfigPageUrl(
			skillKey,
			propertiesJson);
		if (generatedUrl.empty())
		{
			return false;
		}

		SetPendingStartupUrl(generatedUrl);
		auto* appFallback = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
		if (appFallback == nullptr)
		{
			return false;
		}

		auto* templateFallback = appFallback->GetChatDocTemplate();
		if (templateFallback == nullptr)
		{
			return false;
		}

		return templateFallback->OpenDocumentFile(nullptr) != nullptr;
	}

	auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
	if (app == nullptr)
	{
		return false;
	}

	auto* chatTemplate = app->GetChatDocTemplate();
	if (chatTemplate == nullptr)
	{
		return false;
	}

	SetPendingStartupUrl(configUrl);
	CDocument* opened = chatTemplate->OpenDocumentFile(nullptr);
	return opened != nullptr;
}

std::wstring CBlazeClawMFCView::BuildGeneratedSkillConfigPageUrl(
	const std::string& skillKey,
	const std::string& propertiesJson) const
{
	UNREFERENCED_PARAMETER(propertiesJson);
	g_pendingSkillKey = skillKey;
	g_pendingSkillPropertiesJson = propertiesJson;
	g_generatedSkillConfigHtml = BuildGeneratedSkillConfigHtml(skillKey, propertiesJson);
	return L"about:blank";
}

void CBlazeClawMFCView::InitializeWebViewBridge()
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (!m_webView)
	{
		return;
	}

	AppendChatProcedureStatusLine(
		L"startup.view",
		"CBlazeClawMFCView(webview2)");
	AppendChatProcedureStatusLine(
		L"startup.bridge.session",
		m_bridgeSessionId);

	EnsureOpenClawBridgeShim();
	AppendChatProcedureStatusLine(
		L"startup.shim.injected");

	m_webView->add_WebMessageReceived(
		Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
			[this](
				ICoreWebView2* sender,
				ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
			{
				UNREFERENCED_PARAMETER(sender);
				if (args == nullptr)
				{
					return S_OK;
				}

				LPWSTR rawJson = nullptr;
				if (FAILED(args->get_WebMessageAsJson(&rawJson)) || rawJson == nullptr)
				{
					return S_OK;
				}

				std::wstring jsonMessage(rawJson);
				CoTaskMemFree(rawJson);
				HandleWebMessageJson(jsonMessage);
				return S_OK;
			}).Get(),
				&m_webMessageToken);

	if (m_bridgeTimerId == 0)
	{
		m_bridgeTimerId = SetTimer(
			kBridgeLifecycleTimerId,
			kBridgeLifecycleTimerMs,
			nullptr);
		AppendChatProcedureStatusLine(
			L"startup.timer",
			"bridge lifecycle timer started");
	}

	m_bridgeLifecycleSent = false;
	PumpBridgeLifecycle();
#endif
}

void CBlazeClawMFCView::OnInitialUpdate()
{
	CView::OnInitialUpdate();

#ifdef HAVE_WEBVIEW2_HEADER
	HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
		Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
			[&](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
			{
				if (FAILED(result) || !env)
					return result;

				env->CreateCoreWebView2Controller(this->GetSafeHwnd(),
					Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
						[&, env](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT
						{
							if (FAILED(result) || !controller)
								return result;

							m_webViewController = controller;
							m_webViewController->get_CoreWebView2(&m_webView);

							// Resize to fit
							CRect rc;
							GetClientRect(&rc);
							m_webViewController->put_Bounds(rc);

							// Make controller visible
							m_webViewController->put_IsVisible(TRUE);
							InitializeWebViewBridge();

							// Navigate to default URL
							if (m_webView)
							{
								m_webView->add_NavigationCompleted(
									Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
										[this](
											ICoreWebView2* sender,
											ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
										{
											BOOL isSuccess = FALSE;
											if (args != nullptr)
											{
												args->get_IsSuccess(&isSuccess);
											}

											if (!isSuccess)
											{
												ShowChatStartupError(
													sender,
													L"Navigation failed",
													L"Verify OpenClaw UI assets or dev server availability.");
												return S_OK;
											}

											if (!g_generatedSkillConfigHtml.empty() && sender != nullptr)
											{
												const std::wstring generatedHtmlW = ToWide(g_generatedSkillConfigHtml);
												if (!generatedHtmlW.empty())
												{
													sender->NavigateToString(generatedHtmlW.c_str());
													g_generatedSkillConfigHtml.clear();
													g_pendingSkillKey.clear();
													g_pendingSkillPropertiesJson.clear();
												}
											}

											return S_OK;
										}).Get(),
											nullptr);

								const std::wstring startupUrl = ResolveInitialNavigationUrl();
								AppendChatProcedureStatusLine(
									L"startup.url",
									ToNarrow(startupUrl));
								if (startupUrl.empty())
								{
									AppendChatProcedureStatusLine(
										L"warning.startup.asset.missing",
										"No BlazeClaw web/chat startup asset resolved");
									ShowChatStartupError(
										m_webView.Get(),
										L"No startup URL resolved",
										L"Set BLAZECLAW_CHAT_DEV_URL or provide blazeclaw/BlazeClawMfc/web/chat assets.");
								}
								else
								{
									m_webView->Navigate(startupUrl.c_str());
								}
							}

							return S_OK;
						}).Get());

				return S_OK;
			}).Get());

	if (FAILED(hr))
	{
		TRACE(_T("Failed to initialize WebView2 environment."));
		AfxMessageBox(L"Failed to initialize WebView2 environment. Please install the WebView2 runtime.");
	}
#else
	TRACE(_T("WebView2 headers not found at compile time."));
	AfxMessageBox(L"WebView2 headers not found at compile time.");
#endif
}

void CBlazeClawMFCView::SetPendingStartupUrl(const std::wstring& url)
{
	g_pendingStartupUrl = url;
}

std::wstring CBlazeClawMFCView::ResolveInitialNavigationUrl() const
{
	if (!g_generatedSkillConfigHtml.empty())
	{
		return L"about:blank";
	}

	if (!g_pendingStartupUrl.empty())
	{
		std::wstring startupUrl = g_pendingStartupUrl;
		g_pendingStartupUrl.clear();
		return startupUrl;
	}

	return ResolveChatStartupUrl();
}

void CBlazeClawMFCView::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: add extra initialization before printing
}

void CBlazeClawMFCView::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: add cleanup after printing
}

void CBlazeClawMFCView::OnRButtonUp(UINT /* nFlags */, CPoint point)
{
	ClientToScreen(&point);
	OnContextMenu(this, point);
}

void CBlazeClawMFCView::OnContextMenu(CWnd* /* pWnd */, CPoint point)
{
#ifndef SHARED_HANDLERS
	theApp.GetContextMenuManager()->ShowPopupMenu(IDR_POPUP_EDIT, point.x, point.y, this, TRUE);
#endif
}


// CBlazeClawMFCView diagnostics

#ifdef _DEBUG
void CBlazeClawMFCView::AssertValid() const
{
	CView::AssertValid();
}

void CBlazeClawMFCView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CBlazeClawMFCDoc* CBlazeClawMFCView::GetDocument() const // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CBlazeClawMFCDoc)));
	return (CBlazeClawMFCDoc*)m_pDocument;
}
#endif //_DEBUG


// CBlazeClawMFCView message handlers

void CBlazeClawMFCView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);

#ifdef HAVE_WEBVIEW2_HEADER
	if (m_webViewController)
	{
		CRect rc;
		GetClientRect(&rc);
		m_webViewController->put_Bounds(rc);
	}
#endif
}

void CBlazeClawMFCView::OnDestroy()
{
#ifdef HAVE_WEBVIEW2_HEADER
	if (m_bridgeTimerId != 0)
	{
		KillTimer(m_bridgeTimerId);
		m_bridgeTimerId = 0;
	}

	if (m_webView)
	{
		m_webView->remove_WebMessageReceived(m_webMessageToken);
	}

	if (m_webViewController)
	{
		m_webViewController->Close();
		m_webViewController.Reset();
	}
	m_webView.Reset();
#endif

	CView::OnDestroy();
}

void CBlazeClawMFCView::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == m_bridgeTimerId && nIDEvent != 0)
	{
		PumpBridgeLifecycle();
	}

	CView::OnTimer(nIDEvent);
}
