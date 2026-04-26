#pragma once

#include "GatewayJsonUtils.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace blazeclaw::gateway {

	/// Thin view over `RequestFrame::paramsJson` for typed field reads (same behavior as the former
	/// `Extract*Param` helpers in `GatewayHost.cpp`).
	class RequestParamsView {
	public:
		explicit RequestParamsView(const std::optional<std::string>& paramsJson) noexcept
			: m_paramsJson(paramsJson) {}

		[[nodiscard]] std::string GetString(std::string_view fieldName) const;
		[[nodiscard]] std::optional<bool> GetBool(std::string_view fieldName) const;
		[[nodiscard]] std::optional<std::size_t> GetSize(std::string_view fieldName) const;
		[[nodiscard]] std::optional<std::string> GetObject(std::string_view fieldName) const;

		struct ToolExecuteArgsResolution {
			std::optional<std::string> argsJson;
			std::string selectedKey;
			std::string parseMode;
		};

		/// Resolves tool args for `gateway.tools.call.execute` from supported containers
		/// (`args`, `arguments`, `parameters`, `tool_arguments`, `toolArguments`, `payload`).
		/// Returns both normalized args JSON and non-sensitive metadata for diagnostics.
		[[nodiscard]] ToolExecuteArgsResolution ResolveToolExecuteArgs() const;

		/// Backward-compatible convenience wrapper returning only normalized args JSON.
		[[nodiscard]] std::optional<std::string> GetToolExecuteArgsJson() const;

	private:
		const std::optional<std::string>& m_paramsJson;
	};

} // namespace blazeclaw::gateway
