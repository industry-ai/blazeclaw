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

std::optional<std::string> RequestParamsView::GetToolExecuteArgsJson() const {
	if (!m_paramsJson.has_value()) {
		return std::nullopt;
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

	for (const std::string& fieldName : argumentAliases) {
		std::string raw;
		if (json::FindRawField(root, fieldName, raw)) {
			const std::string trimmed = json::Trim(raw);
			if (json::IsJsonObjectShape(trimmed) || json::IsJsonArrayShape(trimmed)) {
				return trimmed;
			}
		}

		std::string decoded;
		if (json::FindStringField(root, fieldName, decoded)) {
			const std::string trimmed = json::Trim(decoded);
			if (json::IsJsonObjectShape(trimmed) || json::IsJsonArrayShape(trimmed)) {
				return trimmed;
			}
		}
	}

	return std::nullopt;
}

} // namespace blazeclaw::gateway
