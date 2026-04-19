#include "pch.h"
#include "GatewayJsonBuilder.h"

#include "GatewayJsonSerializers.h"

namespace blazeclaw::gateway {

std::string JsonBool(bool value) {
	return value ? "true" : "false";
}

std::string JsonNumber(std::uint64_t value) {
	return std::to_string(value);
}

std::string JsonNumber(std::int64_t value) {
	return std::to_string(value);
}

std::string JsonObject(std::initializer_list<std::pair<const char*, std::string>> fields) {
	std::string out;
	out.push_back('{');
	bool first = true;
	for (const auto& entry : fields) {
		if (!first) {
			out.push_back(',');
		}
		first = false;
		out.push_back('"');
		out += EscapeJsonString(std::string(entry.first));
		out += "\":";
		out += entry.second;
	}
	out.push_back('}');
	return out;
}

std::string JsonArray(std::initializer_list<std::string> elements) {
	std::string out;
	out.push_back('[');
	bool first = true;
	for (const auto& element : elements) {
		if (!first) {
			out.push_back(',');
		}
		first = false;
		out += element;
	}
	out.push_back(']');
	return out;
}

std::string JsonArray(const std::vector<std::string>& elements) {
	std::string out;
	out.push_back('[');
	for (std::size_t i = 0; i < elements.size(); ++i) {
		if (i > 0) {
			out.push_back(',');
		}
		out += elements[i];
	}
	out.push_back(']');
	return out;
}

} // namespace blazeclaw::gateway
