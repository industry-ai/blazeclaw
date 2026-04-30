#include "pch.h"
#include "GatewayRequestParams.h"

namespace blazeclaw::gateway {

	std::string RequestParamsView::GetString(std::string_view fieldName) const {
		if (!m_paramsJson.has_value()) {
			return {};
		}

		std::string value;
		if (!json::FindStringField(m_paramsJson.value(), std::string(fieldName), value)) {
			return {};
		}

		return value;
	}

	std::optional<bool> RequestParamsView::GetBool(std::string_view fieldName) const {
		if (!m_paramsJson.has_value()) {
			return std::nullopt;
		}

		bool value = false;
		if (!json::FindBoolField(m_paramsJson.value(), std::string(fieldName), value)) {
			return std::nullopt;
		}

		return value;
	}

	std::optional<std::size_t> RequestParamsView::GetSize(std::string_view fieldName) const {
		if (!m_paramsJson.has_value()) {
			return std::nullopt;
		}

		std::uint64_t value = 0;
		if (!json::FindUInt64Field(m_paramsJson.value(), std::string(fieldName), value)) {
			return std::nullopt;
		}

		return static_cast<std::size_t>(value);
	}

	std::optional<std::string> RequestParamsView::GetObject(std::string_view fieldName) const {
		if (!m_paramsJson.has_value()) {
			return std::nullopt;
		}

		std::string raw;
		if (!json::FindRawField(m_paramsJson.value(), std::string(fieldName), raw)) {
			return std::nullopt;
		}

		if (!json::IsJsonObjectShape(raw)) {
			return std::nullopt;
		}

		return raw;
	}

	RequestParamsView::ToolExecuteArgsResolution RequestParamsView::ResolveToolExecuteArgs() const {
		ToolExecuteArgsResolution resolved;
		if (!m_paramsJson.has_value()) {
			resolved.selectedKey = "none";
			resolved.parseMode = "no_params";
			return resolved;
		}

		const std::string& root = m_paramsJson.value();
		const std::string argumentAliases[] = {
			"args",
			"arguments",
			"parameters",
			"tool_arguments",
			"toolArguments",
			"payload",
		};

		auto tryCaptureArgs = [&resolved](
			const std::string& candidate,
			const std::string& key,
			const std::string& modePrefix) {
			const std::string trimmed = json::Trim(candidate);
			if (json::IsJsonObjectShape(trimmed)) {
				resolved.argsJson = trimmed;
				resolved.selectedKey = key;
				resolved.parseMode = modePrefix + "_object";
				return true;
			}
			if (json::IsJsonArrayShape(trimmed)) {
				resolved.argsJson = trimmed;
				resolved.selectedKey = key;
				resolved.parseMode = modePrefix + "_array";
				return true;
			}
			return false;
			};

		for (const std::string& fieldName : argumentAliases) {
			std::string raw;
			if (json::FindRawField(root, fieldName, raw) &&
				tryCaptureArgs(raw, fieldName, "raw")) {
				return resolved;
			}

			std::string decoded;
			if (json::FindStringField(root, fieldName, decoded) &&
				tryCaptureArgs(decoded, fieldName, "string_decoded")) {
				return resolved;
			}
		}

		if (json::IsJsonObjectShape(json::Trim(root))) {
			bool hasToolShape = false;
			std::string toolMarker;
			hasToolShape = json::FindStringField(root, "tool", toolMarker) ||
				json::FindStringField(root, "action", toolMarker) ||
				json::FindStringField(root, "approvalToken", toolMarker);
			if (hasToolShape) {
				resolved.argsJson = json::Trim(root);
				resolved.selectedKey = "params";
				resolved.parseMode = "root_object_fallback";
				return resolved;
			}
		}

		resolved.selectedKey = "none";
		resolved.parseMode = "unrecognized_or_missing";
		return resolved;
	}

	std::optional<std::string> RequestParamsView::GetToolExecuteArgsJson() const {
		return ResolveToolExecuteArgs().argsJson;
	}

} // namespace blazeclaw::gateway
