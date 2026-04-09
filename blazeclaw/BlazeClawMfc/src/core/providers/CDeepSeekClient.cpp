#include "pch.h"
#include "CDeepSeekClient.h"

#include "../../gateway/GatewayJsonUtils.h"

#include <chrono>
#include <string>
#include <Windows.h>
#include <winhttp.h>

#pragma comment(lib, "Winhttp.lib")

namespace blazeclaw::core {

	void CDeepSeekClient::EmitDiagnostic(
		const char* stage,
		const std::string& detail) const
	{
		const std::string safeStage =
			(stage == nullptr || std::string(stage).empty())
			? "unknown"
			: std::string(stage);
		TRACE("[DeepSeek][%s] %s\n", safeStage.c_str(), detail.c_str());
	}

	std::wstring CDeepSeekClient::ToWide(const std::string& value) const
	{
		if (value.empty())
		{
			return {};
		}

		const int needed = MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			nullptr,
			0);
		if (needed <= 0)
		{
			return {};
		}

		std::wstring output(static_cast<std::size_t>(needed), L'\0');
		MultiByteToWideChar(
			CP_UTF8,
			0,
			value.c_str(),
			static_cast<int>(value.size()),
			output.data(),
			needed);
		return output;
	}

	std::string CDeepSeekClient::EscapeJsonUtf8(const std::string& value) const
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

	std::optional<std::string> CDeepSeekClient::ParseHttpsUrl(
		const std::string& url,
		std::wstring& host,
		std::wstring& path,
		INTERNET_PORT& port,
		bool& secure) const
	{
		host.clear();
		path.clear();
		port = INTERNET_DEFAULT_HTTPS_PORT;
		secure = true;

		const std::wstring urlW = ToWide(url);
		if (urlW.empty())
		{
			return std::string("invalid_url");
		}

		URL_COMPONENTSW components{};
		components.dwStructSize = sizeof(components);
		components.dwHostNameLength = static_cast<DWORD>(-1);
		components.dwUrlPathLength = static_cast<DWORD>(-1);
		components.dwExtraInfoLength = static_cast<DWORD>(-1);

		if (!WinHttpCrackUrl(urlW.c_str(), 0, 0, &components))
		{
			return std::string("invalid_url");
		}

		if (components.nScheme != INTERNET_SCHEME_HTTPS)
		{
			return std::string("deepseek_https_required");
		}

		secure = true;
		port = components.nPort;
		host.assign(components.lpszHostName, components.dwHostNameLength);
		path.assign(components.lpszUrlPath, components.dwUrlPathLength);
		if (components.dwExtraInfoLength > 0 && components.lpszExtraInfo != nullptr)
		{
			path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
		}
		if (path.empty())
		{
			path = L"/";
		}

		return std::nullopt;
	}

	std::optional<std::string> CDeepSeekClient::ExtractAssistantText(
		const std::string& responseJson) const
	{
		std::string choicesRaw;
		if (!blazeclaw::gateway::json::FindRawField(responseJson, "choices", choicesRaw))
		{
			return std::nullopt;
		}

		const std::string contentKey = "\"content\":\"";
		const auto contentPos = choicesRaw.find(contentKey);
		if (contentPos == std::string::npos)
		{
			return std::nullopt;
		}

		const std::size_t start = contentPos + contentKey.size();
		std::string text;
		text.reserve(256);
		bool escaping = false;
		for (std::size_t i = start; i < choicesRaw.size(); ++i)
		{
			const char ch = choicesRaw[i];
			if (escaping)
			{
				switch (ch)
				{
				case 'n':
					text.push_back('\n');
					break;
				case 'r':
					text.push_back('\r');
					break;
				case 't':
					text.push_back('\t');
					break;
				case '"':
				case '\\':
				case '/':
					text.push_back(ch);
					break;
				default:
					text.push_back(ch);
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
				break;
			}

			text.push_back(ch);
		}

		if (text.empty())
		{
			return std::nullopt;
		}

		return text;
	}

	std::vector<std::string> CDeepSeekClient::ExtractAssistantDeltas(
		const std::string& responseBody) const
	{
		std::vector<std::string> deltas;
		std::string cumulative;

		std::size_t cursor = 0;
		while (cursor < responseBody.size())
		{
			const std::size_t lineEnd = responseBody.find('\n', cursor);
			const std::size_t end =
				lineEnd == std::string::npos ? responseBody.size() : lineEnd;
			std::string line = responseBody.substr(cursor, end - cursor);
			cursor = lineEnd == std::string::npos ? responseBody.size() : lineEnd + 1;

			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}

			const std::string prefix = "data:";
			if (line.rfind(prefix, 0) != 0)
			{
				continue;
			}

			std::string payload =
				blazeclaw::gateway::json::Trim(line.substr(prefix.size()));
			if (payload.empty() || payload == "[DONE]")
			{
				continue;
			}

			std::string choicesRaw;
			if (!blazeclaw::gateway::json::FindRawField(payload, "choices", choicesRaw))
			{
				continue;
			}

			const std::string contentKey = "\"content\":\"";
			const auto contentPos = choicesRaw.find(contentKey);
			if (contentPos == std::string::npos)
			{
				continue;
			}

			const std::size_t start = contentPos + contentKey.size();
			std::string piece;
			bool escaping = false;
			for (std::size_t i = start; i < choicesRaw.size(); ++i)
			{
				const char ch = choicesRaw[i];
				if (escaping)
				{
					switch (ch)
					{
					case 'n':
						piece.push_back('\n');
						break;
					case 'r':
						piece.push_back('\r');
						break;
					case 't':
						piece.push_back('\t');
						break;
					case '"':
					case '\\':
					case '/':
						piece.push_back(ch);
						break;
					default:
						piece.push_back(ch);
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
					break;
				}

				piece.push_back(ch);
			}

			if (piece.empty())
			{
				continue;
			}

			cumulative += piece;
			deltas.push_back(cumulative);
		}

		return deltas;
	}

	std::optional<std::string> CDeepSeekClient::ExtractErrorMessage(
		const std::string& responseJson) const
	{
		std::string errorRaw;
		if (!blazeclaw::gateway::json::FindRawField(responseJson, "error", errorRaw))
		{
			return std::nullopt;
		}

		std::string message;
		if (blazeclaw::gateway::json::FindStringField(errorRaw, "message", message) &&
			!message.empty())
		{
			return message;
		}

		return std::string("DeepSeek request failed.");
	}

	blazeclaw::gateway::GatewayHost::ChatRuntimeResult CDeepSeekClient::InvokeChat(
		const ChatRequest& request,
		const std::function<bool(const std::string& runId)>& isCancelled) const
	{
		const auto startedAt = std::chrono::steady_clock::now();
		const std::string deepSeekBaseUrl = "https://api.deepseek.com";
		const std::string endpoint = deepSeekBaseUrl + "/chat/completions";

		EmitDiagnostic(
			"connect",
			std::string("begin runId=") +
			request.runId +
			" session=" +
			request.sessionKey +
			" model=" +
			request.modelId +
			" stream=true endpoint=" +
			endpoint);

		std::wstring host;
		std::wstring path;
		INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
		bool secure = true;
		if (const auto parseError = ParseHttpsUrl(endpoint, host, path, port, secure);
			parseError.has_value())
		{
			EmitDiagnostic(
				"error",
				std::string("invalid endpoint runId=") +
				request.runId +
				" code=" +
				parseError.value());
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = request.modelId,
				.errorCode = parseError.value(),
				.errorMessage = "DeepSeek endpoint URL is invalid.",
			};
		}

		HINTERNET session = WinHttpOpen(
			L"BlazeClaw/1.0",
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS,
			0);
		if (session == nullptr)
		{
			EmitDiagnostic("error", std::string("WinHttpOpen failed runId=") + request.runId);
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = request.modelId,
				.errorCode = "deepseek_http_open_failed",
				.errorMessage = "Failed to initialize DeepSeek HTTP session.",
			};
		}

		auto closeSession = [&session]()
			{
				if (session != nullptr)
				{
					WinHttpCloseHandle(session);
					session = nullptr;
				}
			};

		HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
		if (connection == nullptr)
		{
			EmitDiagnostic(
				"error",
				std::string("WinHttpConnect failed runId=") + request.runId);
			closeSession();
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = request.modelId,
				.errorCode = "deepseek_http_connect_failed",
				.errorMessage = "Failed to connect to DeepSeek endpoint.",
			};
		}

		auto closeConnection = [&connection]()
			{
				if (connection != nullptr)
				{
					WinHttpCloseHandle(connection);
					connection = nullptr;
				}
			};

		HINTERNET requestHandle = WinHttpOpenRequest(
			connection,
			L"POST",
			path.c_str(),
			nullptr,
			WINHTTP_NO_REFERER,
			WINHTTP_DEFAULT_ACCEPT_TYPES,
			secure ? WINHTTP_FLAG_SECURE : 0);
		if (requestHandle == nullptr)
		{
			EmitDiagnostic(
				"error",
				std::string("WinHttpOpenRequest failed runId=") + request.runId);
			closeConnection();
			closeSession();
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = request.modelId,
				.errorCode = "deepseek_http_request_failed",
				.errorMessage = "Failed to create DeepSeek request.",
			};
		}

		auto closeRequest = [&requestHandle]()
			{
				if (requestHandle != nullptr)
				{
					WinHttpCloseHandle(requestHandle);
					requestHandle = nullptr;
				}
			};

		const std::wstring authHeaderW =
			L"Authorization: Bearer " + ToWide(request.apiKey) + L"\r\n";
		const std::wstring contentTypeW = L"Content-Type: application/json\r\n";
		const std::wstring allHeadersW = authHeaderW + contentTypeW;

		const std::string escapedMessage = EscapeJsonUtf8(request.message);
		const std::string payload =
			std::string("{\"model\":\"") +
			request.modelId +
			"\",\"stream\":true,\"messages\":[{\"role\":\"user\",\"content\":\"" +
			escapedMessage +
			"\"}]}";

		if (!WinHttpSendRequest(
			requestHandle,
			allHeadersW.c_str(),
			static_cast<DWORD>(allHeadersW.size()),
			(LPVOID)payload.data(),
			static_cast<DWORD>(payload.size()),
			static_cast<DWORD>(payload.size()),
			0))
		{
			EmitDiagnostic(
				"error",
				std::string("WinHttpSendRequest failed runId=") + request.runId);
			closeRequest();
			closeConnection();
			closeSession();
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = request.modelId,
				.errorCode = "deepseek_http_send_failed",
				.errorMessage = "DeepSeek request transmission failed.",
			};
		}

		if (!WinHttpReceiveResponse(requestHandle, nullptr))
		{
			EmitDiagnostic(
				"error",
				std::string("WinHttpReceiveResponse failed runId=") + request.runId);
			closeRequest();
			closeConnection();
			closeSession();
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = request.modelId,
				.errorCode = "deepseek_http_receive_failed",
				.errorMessage = "DeepSeek response reception failed.",
			};
		}

		DWORD statusCode = 0;
		DWORD statusCodeSize = sizeof(statusCode);
		WinHttpQueryHeaders(
			requestHandle,
			WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX,
			&statusCode,
			&statusCodeSize,
			WINHTTP_NO_HEADER_INDEX);
		EmitDiagnostic(
			"connect",
			std::string("response headers runId=") +
			request.runId +
			" status=" +
			std::to_string(statusCode));

		std::string responseBody;
		std::size_t chunkCount = 0;
		std::string streamBuffer;
		std::string streamedCumulative;
		std::vector<std::string> streamedSnapshots;

		auto processStreamBuffer = [&](const bool flushTail)
			{
				while (true)
				{
					const std::size_t lineEnd = streamBuffer.find('\n');
					if (lineEnd == std::string::npos)
					{
						break;
					}

					std::string line = streamBuffer.substr(0, lineEnd);
					streamBuffer.erase(0, lineEnd + 1);
					if (!line.empty() && line.back() == '\r')
					{
						line.pop_back();
					}

					const std::string prefix = "data:";
					if (line.rfind(prefix, 0) != 0)
					{
						continue;
					}

					const std::string payload =
						blazeclaw::gateway::json::Trim(line.substr(prefix.size()));
					if (payload.empty() || payload == "[DONE]")
					{
						continue;
					}

					const auto piece = ExtractAssistantText(payload);
					if (!piece.has_value() || piece->empty())
					{
						continue;
					}

					streamedCumulative += piece.value();
					streamedSnapshots.push_back(streamedCumulative);
					if (request.onAssistantDelta)
					{
						request.onAssistantDelta(streamedCumulative);
					}
				}

				if (flushTail && !streamBuffer.empty())
				{
					std::string tailLine = streamBuffer;
					streamBuffer.clear();
					if (!tailLine.empty() && tailLine.back() == '\r')
					{
						tailLine.pop_back();
					}

					const std::string prefix = "data:";
					if (tailLine.rfind(prefix, 0) == 0)
					{
						const std::string payload =
							blazeclaw::gateway::json::Trim(tailLine.substr(prefix.size()));
						if (!payload.empty() && payload != "[DONE]")
						{
							const auto piece = ExtractAssistantText(payload);
							if (piece.has_value() && !piece->empty())
							{
								streamedCumulative += piece.value();
								streamedSnapshots.push_back(streamedCumulative);
								if (request.onAssistantDelta)
								{
									request.onAssistantDelta(streamedCumulative);
								}
							}
						}
					}
				}
			};

		while (true)
		{
			if (isCancelled && isCancelled(request.runId))
			{
				EmitDiagnostic("cancel", std::string("cancel observed runId=") + request.runId);
				closeRequest();
				closeConnection();
				closeSession();
				return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
					.ok = false,
					.assistantText = {},
					.modelId = request.modelId,
					.errorCode = "deepseek_request_cancelled",
					.errorMessage = "DeepSeek request cancelled.",
				};
			}

			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(requestHandle, &available))
			{
				break;
			}
			if (available == 0)
			{
				break;
			}

			std::string chunk(static_cast<std::size_t>(available), '\0');
			DWORD downloaded = 0;
			if (!WinHttpReadData(requestHandle, chunk.data(), available, &downloaded))
			{
				EmitDiagnostic(
					"error",
					std::string("WinHttpReadData failed runId=") + request.runId);
				break;
			}

			responseBody.append(chunk.data(), static_cast<std::size_t>(downloaded));
			streamBuffer.append(chunk.data(), static_cast<std::size_t>(downloaded));
			processStreamBuffer(false);
			++chunkCount;
		}
		processStreamBuffer(true);

		EmitDiagnostic(
			"stream",
			std::string("read completed runId=") +
			request.runId +
			" chunks=" +
			std::to_string(chunkCount) +
			" bytes=" +
			std::to_string(responseBody.size()));

		closeRequest();
		closeConnection();
		closeSession();

		if (statusCode < 200 || statusCode >= 300)
		{
			const std::string errorMessage =
				ExtractErrorMessage(responseBody).value_or("DeepSeek service returned an error.");
			EmitDiagnostic(
				"error",
				std::string("status error runId=") +
				request.runId +
				" status=" +
				std::to_string(statusCode) +
				" message=" +
				errorMessage);
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.modelId = request.modelId,
				.errorCode = "deepseek_http_status_error",
				.errorMessage = errorMessage,
			};
		}

		auto assistantDeltas = streamedSnapshots;
		if (assistantDeltas.empty())
		{
			assistantDeltas = ExtractAssistantDeltas(responseBody);
		}
		const std::string assistantText =
			assistantDeltas.empty() ? std::string() : assistantDeltas.back();

		EmitDiagnostic(
			"stream",
			std::string("delta parsed runId=") +
			request.runId +
			" snapshots=" +
			std::to_string(assistantDeltas.size()) +
			" finalChars=" +
			std::to_string(assistantText.size()));

		if (assistantText.empty())
		{
			EmitDiagnostic(
				"error",
				std::string("invalid response runId=") + request.runId);
			return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
				.ok = false,
				.assistantText = {},
				.assistantDeltas = {},
				.modelId = request.modelId,
				.errorCode = "deepseek_invalid_response",
				.errorMessage = "DeepSeek response did not contain assistant text.",
			};
		}

		const auto latencyMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - startedAt)
			.count();
		EmitDiagnostic(
			"final",
			std::string("completed runId=") +
			request.runId +
			" latencyMs=" +
			std::to_string(latencyMs) +
			" finalChars=" +
			std::to_string(assistantText.size()));

		return blazeclaw::gateway::GatewayHost::ChatRuntimeResult{
			.ok = true,
			.assistantText = assistantText,
			.assistantDeltas = assistantDeltas,
			.modelId = request.modelId,
			.errorCode = {},
			.errorMessage = {},
		};
	}

} // namespace blazeclaw::core
