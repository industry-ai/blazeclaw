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

} // namespace blazeclaw::gateway
