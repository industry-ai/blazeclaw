#pragma once

#include "Telemetry.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace blazeclaw::gateway {

/// Minimal JSON value builders for concatenated gateway payloads (string-table JSON, not a full DOM).
/// String values use `JsonString` from `Telemetry.h` (quoted + escaped). Other values passed into
/// `JsonObject` must already be JSON fragments (`JsonBool` / nested `JsonObject` / `JsonArray` output).

[[nodiscard]] std::string JsonBool(bool value);
[[nodiscard]] std::string JsonNumber(std::uint64_t value);
[[nodiscard]] std::string JsonNumber(std::int64_t value);

[[nodiscard]] std::string JsonObject(
	std::initializer_list<std::pair<const char*, std::string>> fields);

[[nodiscard]] std::string JsonArray(std::initializer_list<std::string> elements);
[[nodiscard]] std::string JsonArray(const std::vector<std::string>& elements);

/// Compact success payloads for common gateway introspection shapes (pair with `protocol::OkResponse`).
[[nodiscard]] std::string JsonPayloadExists(bool exists);
[[nodiscard]] std::string JsonPayloadCount(std::uint64_t count);
[[nodiscard]] std::string JsonPayloadFoundCount(bool found, std::uint64_t count);
[[nodiscard]] std::string JsonPayloadPathExists(const std::string& path, bool exists);

} // namespace blazeclaw::gateway
