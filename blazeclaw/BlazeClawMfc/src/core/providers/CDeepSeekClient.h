#pragma once

#include "../../gateway/GatewayHost.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <Windows.h>
#include <winhttp.h>

namespace blazeclaw::core {

	class CDeepSeekClient {
	public:
		struct ChatRequest {
			std::string runId;
			std::string sessionKey;
			std::string message;
			std::string modelId;
			std::string apiKey;
			std::function<void(const std::string&)> onAssistantDelta;
		};

		blazeclaw::gateway::GatewayHost::ChatRuntimeResult InvokeChat(
			const ChatRequest& request,
			const std::function<bool(const std::string& runId)>& isCancelled) const;

		[[nodiscard]] std::vector<std::string> ParseAssistantDeltasForTest(
			const std::string& responseBody) const;

	private:
		std::optional<std::string> ParseHttpsUrl(
			const std::string& url,
			std::wstring& host,
			std::wstring& path,
			INTERNET_PORT& port,
			bool& secure) const;
		std::optional<std::string> ExtractAssistantText(
			const std::string& responseJson) const;
		std::vector<std::string> ExtractAssistantDeltas(
			const std::string& responseBody) const;
		std::optional<std::string> ExtractErrorMessage(
			const std::string& responseJson) const;
		std::wstring ToWide(const std::string& value) const;
		std::string EscapeJsonUtf8(const std::string& value) const;
		void EmitDiagnostic(const char* stage, const std::string& detail) const;
	};

} // namespace blazeclaw::core
