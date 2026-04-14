#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace blazeclaw::core {

	struct OpenClawManifestRequiresCompat {
		std::vector<std::wstring> bins;
		std::vector<std::wstring> anyBins;
		std::vector<std::wstring> env;
		std::vector<std::wstring> config;
	};

	struct ParsedOpenClawManifestInstallBaseCompat {
		nlohmann::json raw;
		std::wstring kind;
		std::optional<std::wstring> id;
		std::optional<std::wstring> label;
		std::vector<std::wstring> bins;
	};

	[[nodiscard]] std::vector<std::wstring> NormalizeStringListCompat(
		const nlohmann::json& input);

	[[nodiscard]] std::wstring GetFrontmatterStringCompat(
		const std::map<std::wstring, std::wstring>& frontmatter,
		const std::wstring& key);

	[[nodiscard]] bool ParseFrontmatterBoolCompat(
		const std::wstring& value,
		bool fallback);

	[[nodiscard]] std::optional<nlohmann::json> ResolveOpenClawManifestBlockCompat(
		const std::map<std::wstring, std::wstring>& frontmatter,
		const std::wstring& key = L"metadata");

	[[nodiscard]] std::optional<OpenClawManifestRequiresCompat>
		ResolveOpenClawManifestRequiresCompat(
			const nlohmann::json& metadataObj);

	[[nodiscard]] std::vector<nlohmann::json> ResolveOpenClawManifestInstallCompat(
		const nlohmann::json& metadataObj);

	[[nodiscard]] std::vector<std::wstring> ResolveOpenClawManifestOsCompat(
		const nlohmann::json& metadataObj);

	[[nodiscard]] std::optional<ParsedOpenClawManifestInstallBaseCompat>
		ParseOpenClawManifestInstallBaseCompat(
			const nlohmann::json& input,
			const std::vector<std::wstring>& allowedKinds);

	void ApplyOpenClawManifestInstallCommonFieldsCompat(
		std::wstring& id,
		std::wstring& label,
		std::vector<std::wstring>& bins,
		const ParsedOpenClawManifestInstallBaseCompat& parsed);

} // namespace blazeclaw::core
