#include "pch.h"
#include "PythonRuntimeSelector.h"

#include "../GatewayJsonUtils.h"

#include <algorithm>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway::python {
	namespace {
		std::string ReadEnvironmentVariable(const char* name) {
			if (name == nullptr) {
				return {};
			}

			char* raw = nullptr;
			size_t size = 0;
			if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
				return {};
			}

			std::string value(raw);
			free(raw);
			return value;
		}

		std::string NormalizeLower(std::string value) {
			value = blazeclaw::gateway::json::Trim(value);
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		bool ParseEnvBool(const std::string& raw, const bool fallback) {
			const std::string lowered = NormalizeLower(raw);
			if (lowered.empty()) {
				return fallback;
			}

			if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on") {
				return true;
			}

			if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off") {
				return false;
			}

			return fallback;
		}

		bool IsValidMode(const std::string& mode) {
			return mode == "external" || mode == "embedded";
		}

		std::optional<std::string> TryReadNestedString(
			const nlohmann::json& root,
			std::initializer_list<const char*> path) {
			const nlohmann::json* cursor = &root;
			for (const auto* key : path) {
				if (cursor == nullptr || !cursor->is_object() || !cursor->contains(key)) {
					return std::nullopt;
				}

				cursor = &(*cursor)[key];
			}

			if (cursor != nullptr && cursor->is_string()) {
				return NormalizeLower(cursor->get<std::string>());
			}

			return std::nullopt;
		}

		std::optional<bool> TryReadNestedBool(
			const nlohmann::json& root,
			std::initializer_list<const char*> path) {
			const nlohmann::json* cursor = &root;
			for (const auto* key : path) {
				if (cursor == nullptr || !cursor->is_object() || !cursor->contains(key)) {
					return std::nullopt;
				}

				cursor = &(*cursor)[key];
			}

			if (cursor != nullptr && cursor->is_boolean()) {
				return cursor->get<bool>();
			}

			return std::nullopt;
		}
	}

	PythonRuntimeSelection PythonRuntimeSelector::Resolve(
		const std::optional<std::string>& argsJson) const {
		PythonRuntimeSelection selection;

		nlohmann::json argsRoot = nlohmann::json::object();
		if (argsJson.has_value()) {
			try {
				argsRoot = nlohmann::json::parse(argsJson.value());
			}
			catch (...) {
				argsRoot = nlohmann::json::object();
			}
		}

		const std::optional<std::string> toolMode =
			TryReadNestedString(argsRoot, { "runtime", "mode" });
		const std::optional<std::string> extensionMode =
			TryReadNestedString(argsRoot, { "extension", "runtime", "mode" });
		const std::string globalMode = NormalizeLower(
			ReadEnvironmentVariable("BLAZECLAW_PYTHON_RUNTIME_MODE_DEFAULT"));

		if (toolMode.has_value()) {
			selection.mode = toolMode.value();
			selection.modeSource = "tool";
		}
		else if (extensionMode.has_value()) {
			selection.mode = extensionMode.value();
			selection.modeSource = "extension";
		}
		else if (!globalMode.empty()) {
			selection.mode = globalMode;
			selection.modeSource = "global";
		}
		else {
			selection.mode = "external";
			selection.modeSource = "global_default";
		}

		selection.strictMode = ParseEnvBool(
			ReadEnvironmentVariable("BLAZECLAW_PYTHON_RUNTIME_STRICT_MODE"),
			true);
		selection.allowFallbackToExternal = ParseEnvBool(
			ReadEnvironmentVariable("BLAZECLAW_PYTHON_RUNTIME_ALLOW_FALLBACK_TO_EXTERNAL"),
			false);

		const std::optional<bool> strictModeOverride =
			TryReadNestedBool(argsRoot, { "runtime", "strictMode" });
		const std::optional<bool> fallbackOverride =
			TryReadNestedBool(argsRoot, { "runtime", "allowFallbackToExternal" });
		if (strictModeOverride.has_value()) {
			selection.strictMode = strictModeOverride.value();
		}
		if (fallbackOverride.has_value()) {
			selection.allowFallbackToExternal = fallbackOverride.value();
		}

		if (selection.mode.empty()) {
			selection.resolved = false;
			selection.errorCode = "python_runtime_mode_unresolved";
			selection.errorMessage = "runtime mode could not be resolved";
			return selection;
		}

		if (!IsValidMode(selection.mode)) {
			selection.resolved = false;
			selection.errorCode = "python_runtime_mode_invalid";
			selection.errorMessage = selection.mode;
			return selection;
		}

		selection.resolved = true;
		return selection;
	}

} // namespace blazeclaw::gateway::python
