#include "pch.h"
#include "SharedFrontmatterCompat.h"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace blazeclaw::core {

	namespace {

		std::string WideToUtf8Compat(const std::wstring& value) {
			if (value.empty()) {
				return {};
			}

			auto asciiFallback = [&value]() {
				std::string fallback;
				fallback.reserve(value.size());
				for (const wchar_t ch : value) {
					if (ch >= 0 && ch <= 0x7F) {
						fallback.push_back(static_cast<char>(ch));
					}
					else {
						fallback.push_back('?');
					}
				}
				return fallback;
				};

			const int required = WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (required <= 0) {
				return asciiFallback();
			}

			std::string output(static_cast<std::size_t>(required), '\0');
			const int converted = WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				required,
				nullptr,
				nullptr);
			if (converted <= 0) {
				return asciiFallback();
			}

			return output;
		}

		std::wstring Utf8ToWideCompat(const std::string& value) {
			if (value.empty()) {
				return {};
			}

			const int required = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0);
			if (required <= 0) {
				return std::wstring(value.begin(), value.end());
			}

			std::wstring output(static_cast<std::size_t>(required), L'\0');
			const int converted = MultiByteToWideChar(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				required);
			if (converted <= 0) {
				return std::wstring(value.begin(), value.end());
			}

			return output;
		}

		std::wstring TrimCompat(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; });
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) { return std::iswspace(ch) != 0; })
				.base();
			if (first >= last) {
				return {};
			}
			return std::wstring(first, last);
		}

		std::wstring ToLowerCompat(const std::wstring& value) {
			std::wstring lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return lowered;
		}

		std::wstring JsonToWStringCompat(const nlohmann::json& value) {
			if (value.is_string()) {
				return TrimCompat(value.get<std::wstring>());
			}
			if (value.is_boolean()) {
				return value.get<bool>() ? L"true" : L"false";
			}
			if (value.is_number_integer()) {
				return std::to_wstring(value.get<long long>());
			}
			if (value.is_number_unsigned()) {
				return std::to_wstring(value.get<unsigned long long>());
			}
			if (value.is_number_float()) {
				std::wstringstream stream;
				stream << value.get<double>();
				return stream.str();
			}
			if (value.is_object() || value.is_array()) {
				return Utf8ToWideCompat(nlohmann::json(value).dump());
			}
			return {};
		}

		nlohmann::json ParseJson5LikeCompat(const std::wstring& raw) {
			nlohmann::json parsed =
				nlohmann::json::parse(WideToUtf8Compat(raw), nullptr, false);
			if (!parsed.is_discarded()) {
				return parsed;
			}

			std::wstring normalized = raw;
			std::replace(normalized.begin(), normalized.end(), L'\'', L'"');
			parsed = nlohmann::json::parse(
				WideToUtf8Compat(normalized),
				nullptr,
				false);
			return parsed;
		}

	} // namespace

	std::vector<std::wstring> NormalizeStringListCompat(const nlohmann::json& input) {
		std::vector<std::wstring> normalized;
		auto pushValue = [&normalized](const std::wstring& value) {
			const std::wstring trimmed = TrimCompat(value);
			if (!trimmed.empty()) {
				normalized.push_back(trimmed);
			}
			};

		if (input.is_array()) {
			for (const auto& item : input) {
				if (item.is_string()) {
					pushValue(item.get<std::wstring>());
				}
				else if (!item.is_null()) {
					pushValue(JsonToWStringCompat(item));
				}
			}
			return normalized;
		}

		if (input.is_string()) {
			std::wstring text = input.get<std::wstring>();
			std::wstring current;
			for (const wchar_t ch : text) {
				if (ch == L',' || ch == L';' || ch == L'|') {
					pushValue(current);
					current.clear();
					continue;
				}
				current.push_back(ch);
			}
			pushValue(current);
		}

		return normalized;
	}

	std::wstring GetFrontmatterStringCompat(
		const std::map<std::wstring, std::wstring>& frontmatter,
		const std::wstring& key) {
		const auto it = frontmatter.find(ToLowerCompat(TrimCompat(key)));
		if (it == frontmatter.end()) {
			return {};
		}
		return TrimCompat(it->second);
	}

	bool ParseFrontmatterBoolCompat(const std::wstring& value, bool fallback) {
		const std::wstring normalized = ToLowerCompat(TrimCompat(value));
		if (normalized == L"true" || normalized == L"1" || normalized == L"yes") {
			return true;
		}
		if (normalized == L"false" || normalized == L"0" || normalized == L"no") {
			return false;
		}
		return fallback;
	}

	std::optional<nlohmann::json> ResolveOpenClawManifestBlockCompat(
		const std::map<std::wstring, std::wstring>& frontmatter,
		const std::wstring& key) {
		const std::wstring raw = GetFrontmatterStringCompat(frontmatter, key);
		if (raw.empty()) {
			return std::nullopt;
		}

		nlohmann::json parsed = ParseJson5LikeCompat(raw);
		if (parsed.is_discarded() || !parsed.is_object()) {
			return std::nullopt;
		}

		static const std::vector<std::wstring> manifestKeys = {
			L"openclaw",
			L"blazeclaw",
		};
		for (const auto& manifestKey : manifestKeys) {
			const std::string keyUtf8 = WideToUtf8Compat(manifestKey);
			if (parsed.contains(keyUtf8) && parsed[keyUtf8].is_object()) {
				return parsed[keyUtf8];
			}
		}

		return std::nullopt;
	}

	std::optional<OpenClawManifestRequiresCompat>
		ResolveOpenClawManifestRequiresCompat(const nlohmann::json& metadataObj) {
		if (!metadataObj.is_object() ||
			!metadataObj.contains("requires") ||
			!metadataObj["requires"].is_object()) {
			return std::nullopt;
		}

		const auto& requiresObj = metadataObj["requires"];
		OpenClawManifestRequiresCompat output;
		output.bins = NormalizeStringListCompat(requiresObj.value("bins", nlohmann::json::array()));
		output.anyBins = NormalizeStringListCompat(requiresObj.value("anyBins", nlohmann::json::array()));
		output.env = NormalizeStringListCompat(requiresObj.value("env", nlohmann::json::array()));
		output.config = NormalizeStringListCompat(requiresObj.value("config", nlohmann::json::array()));
		return output;
	}

	std::vector<nlohmann::json> ResolveOpenClawManifestInstallCompat(
		const nlohmann::json& metadataObj) {
		if (!metadataObj.is_object() ||
			!metadataObj.contains("install") ||
			!metadataObj["install"].is_array()) {
			return {};
		}

		std::vector<nlohmann::json> installs;
		for (const auto& item : metadataObj["install"]) {
			if (item.is_object()) {
				installs.push_back(item);
			}
		}
		return installs;
	}

	std::vector<std::wstring> ResolveOpenClawManifestOsCompat(
		const nlohmann::json& metadataObj) {
		if (!metadataObj.is_object() || !metadataObj.contains("os")) {
			return {};
		}
		return NormalizeStringListCompat(metadataObj["os"]);
	}

	std::optional<ParsedOpenClawManifestInstallBaseCompat>
		ParseOpenClawManifestInstallBaseCompat(
			const nlohmann::json& input,
			const std::vector<std::wstring>& allowedKinds) {
		if (!input.is_object()) {
			return std::nullopt;
		}

		std::wstring kindRaw;
		if (input.contains("kind") && input["kind"].is_string()) {
			kindRaw = input["kind"].get<std::wstring>();
		}
		else if (input.contains("type") && input["type"].is_string()) {
			kindRaw = input["type"].get<std::wstring>();
		}
		const std::wstring kind = ToLowerCompat(TrimCompat(kindRaw));
		if (kind.empty()) {
			return std::nullopt;
		}
		if (std::find(allowedKinds.begin(), allowedKinds.end(), kind) == allowedKinds.end()) {
			return std::nullopt;
		}

		ParsedOpenClawManifestInstallBaseCompat parsed;
		parsed.raw = input;
		parsed.kind = kind;
		if (input.contains("id") && input["id"].is_string()) {
			parsed.id = input["id"].get<std::wstring>();
		}
		if (input.contains("label") && input["label"].is_string()) {
			parsed.label = input["label"].get<std::wstring>();
		}
		if (input.contains("bins")) {
			parsed.bins = NormalizeStringListCompat(input["bins"]);
		}

		return parsed;
	}

	void ApplyOpenClawManifestInstallCommonFieldsCompat(
		std::wstring& id,
		std::wstring& label,
		std::vector<std::wstring>& bins,
		const ParsedOpenClawManifestInstallBaseCompat& parsed) {
		if (parsed.id.has_value()) {
			id = parsed.id.value();
		}
		if (parsed.label.has_value()) {
			label = parsed.label.value();
		}
		if (!parsed.bins.empty()) {
			bins = parsed.bins;
		}
	}

} // namespace blazeclaw::core
