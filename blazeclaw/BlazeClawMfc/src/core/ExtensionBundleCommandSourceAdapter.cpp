#include "pch.h"
#include "ExtensionBundleCommandSourceAdapter.h"

#include "filesystem/SafeOpenSync.h"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>

namespace blazeclaw::core {

	namespace {

		std::wstring Trim(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				}).base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::wstring ToLower(const std::wstring& value) {
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

		std::wstring Utf8ToWideLocal(const std::string& value) {
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

		std::wstring NormalizeBundleCommandName(const std::wstring& raw) {
			std::wstring normalized;
			normalized.reserve(raw.size());

			for (const wchar_t ch : raw) {
				if (ch == L':' || ch == L'/' || ch == L'\\') {
					normalized.push_back(L'-');
					continue;
				}

				normalized.push_back(ch);
			}

			const auto collapsed = std::regex_replace(
				normalized,
				std::wregex(L"[-_]{2,}"),
				L"-");

			std::wstring trimmed = Trim(collapsed);
			while (!trimmed.empty() &&
				(trimmed.front() == L'-' || trimmed.front() == L'_')) {
				trimmed.erase(trimmed.begin());
			}
			while (!trimmed.empty() &&
				(trimmed.back() == L'-' || trimmed.back() == L'_')) {
				trimmed.pop_back();
			}

			return trimmed.empty() ? L"bundle-command" : trimmed;
		}

		std::wstring ParseBundleFrontmatterField(
			const std::wstring& content,
			const std::wstring& key) {
			if (!content.starts_with(L"---")) {
				return {};
			}

			const std::size_t end = content.find(L"\n---", 3);
			if (end == std::wstring::npos) {
				return {};
			}

			const std::wstring block = content.substr(3, end - 3);
			std::wstringstream stream(block);
			std::wstring line;
			const std::wstring keyNormalized = ToLower(Trim(key));
			while (std::getline(stream, line)) {
				const std::size_t colon = line.find(L':');
				if (colon == std::wstring::npos || colon == 0) {
					continue;
				}

				const std::wstring fieldKey = ToLower(Trim(line.substr(0, colon)));
				if (fieldKey != keyNormalized) {
					continue;
				}

				std::wstring value = Trim(line.substr(colon + 1));
				if (value.size() >= 2 &&
					((value.front() == L'"' && value.back() == L'"') ||
						(value.front() == L'\'' && value.back() == L'\''))) {
					value = value.substr(1, value.size() - 2);
				}

				return value;
			}

			return {};
		}

		std::wstring NormalizeBundleLineEndings(const std::wstring& content) {
			std::wstring normalized;
			normalized.reserve(content.size());
			for (std::size_t index = 0; index < content.size(); ++index) {
				const wchar_t ch = content[index];
				if (ch == L'\r') {
					if (index + 1 < content.size() && content[index + 1] == L'\n') {
						continue;
					}
					normalized.push_back(L'\n');
					continue;
				}
				normalized.push_back(ch);
			}
			return normalized;
		}

		std::wstring StripBundleFrontmatter(const std::wstring& content) {
			const std::wstring normalized = NormalizeBundleLineEndings(content);
			if (!normalized.starts_with(L"---")) {
				return Trim(normalized);
			}

			const std::size_t end = normalized.find(L"\n---", 3);
			if (end == std::wstring::npos) {
				return Trim(normalized);
			}

			return Trim(normalized.substr(end + 4));
		}

		std::optional<std::wstring> ReadVerifiedBundleFile(
			const std::filesystem::path& filePath,
			const std::filesystem::path& rootPath,
			const std::uint64_t maxBytes) {
			std::error_code ec;
			const auto canonicalFile =
				std::filesystem::weakly_canonical(filePath, ec);
			if (ec) {
				return std::nullopt;
			}

			blazeclaw::core::filesystem::VerifiedOpenRequest request;
			request.filePath = filePath;
			request.resolvedPath = canonicalFile;
			request.policy.rejectPathSymlink = true;
			request.policy.rejectHardlinks = true;
			request.policy.maxBytes = maxBytes;
			request.policy.allowedType =
				blazeclaw::core::filesystem::VerifiedOpenAllowedType::File;

			const auto result =
				blazeclaw::core::filesystem::OpenVerifiedFileUtf8Sync(request);
			if (!result.ok) {
				return std::nullopt;
			}

			std::error_code insideEc;
			const auto canonicalRoot =
				std::filesystem::weakly_canonical(rootPath, insideEc);
			if (insideEc) {
				return std::nullopt;
			}

			auto rootIt = canonicalRoot.begin();
			const auto rootEnd = canonicalRoot.end();
			auto fileIt = result.resolvedPath.begin();
			const auto fileEnd = result.resolvedPath.end();
			for (; rootIt != rootEnd; ++rootIt, ++fileIt) {
				if (fileIt == fileEnd || *rootIt != *fileIt) {
					return std::nullopt;
				}
			}

			return result.utf8Content;
		}

		std::vector<std::wstring> ResolveBundleCommandRootDirs(
			const std::filesystem::path& extensionRoot) {
			std::vector<std::wstring> roots;
			const auto defaultRoot = extensionRoot / L"commands";
			std::error_code ec;
			if (std::filesystem::is_directory(defaultRoot, ec) && !ec) {
				roots.push_back(L"commands");
			}

			const auto manifestPath =
				extensionRoot / L".claude-plugin" / L"plugin.json";
			const auto rawManifest = ReadVerifiedBundleFile(
				manifestPath,
				extensionRoot,
				256 * 1024);
			if (!rawManifest.has_value()) {
				return roots;
			}

			auto wideToUtf8 = [](const std::wstring& value) {
				std::string output;
				output.reserve(value.size());
				for (const wchar_t ch : value) {
					output.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
				}
				return output;
			};
			std::string manifestUtf8 = wideToUtf8(rawManifest.value());
			nlohmann::json parsed = nlohmann::json::parse(manifestUtf8, nullptr, false);
			if (parsed.is_discarded() || !parsed.is_object()) {
				return roots;
			}

			const auto commandsIt = parsed.find("commands");
			if (commandsIt == parsed.end()) {
				return roots;
			}

			auto addRoot = [&roots](const std::wstring& value) {
				const auto trimmed = Trim(value);
				if (trimmed.empty()) {
					return;
				}
				if (std::find(roots.begin(), roots.end(), trimmed) == roots.end()) {
					roots.push_back(trimmed);
				}
			};

			if (commandsIt->is_string()) {
				addRoot(Utf8ToWideLocal(commandsIt->get<std::string>()));
			}
			else if (commandsIt->is_array()) {
				for (const auto& item : *commandsIt) {
					if (!item.is_string()) {
						continue;
					}
					addRoot(Utf8ToWideLocal(item.get<std::string>()));
				}
			}

			return roots;
		}

		bool ParseBundleFrontmatterBool(
			const std::wstring& content,
			const std::wstring& key,
			const bool fallback) {
			const std::wstring value = ParseBundleFrontmatterField(content, key);
			if (value.empty()) {
				return fallback;
			}

			const std::wstring normalized = ToLower(Trim(value));
			if (normalized == L"true" || normalized == L"yes" || normalized == L"1") {
				return true;
			}
			if (normalized == L"false" || normalized == L"no" || normalized == L"0") {
				return false;
			}

			return fallback;
		}

	} // namespace

	ExtensionBundleCommandSourceAdapter::ExtensionBundleCommandSourceAdapter(
		const std::filesystem::path& extensionsRoot)
		: m_extensionsRoot(extensionsRoot) {
	}

	const ExtensionBundleCommandSourceAdapter::Diagnostics&
		ExtensionBundleCommandSourceAdapter::LastDiagnostics() const {
		return m_lastDiagnostics;
	}

	extensions::RuntimeCapabilityDescriptor ExtensionBundleCommandSourceAdapter::Describe() const {
		return extensions::RuntimeCapabilityDescriptor{
			.capabilityId = "skills.bundle-command-source",
			.owner = "service-manager",
			.version = "v1",
			.source = "internal",
		};
	}

	std::vector<SkillsCommandSpec> ExtensionBundleCommandSourceAdapter::
		BuildAdditionalSkillsCommands(
			const extensions::RuntimeSkillCommandSourceAdapterContext&) const {
		m_lastDiagnostics = Diagnostics{};
		std::vector<SkillsCommandSpec> commands;
		if (m_extensionsRoot.empty()) {
			return commands;
		}

		std::error_code ec;
		if (!std::filesystem::is_directory(m_extensionsRoot, ec) || ec) {
			return commands;
		}

		for (const auto& extensionEntry :
			std::filesystem::directory_iterator(m_extensionsRoot, ec)) {
			if (ec) {
				break;
			}

			if (!extensionEntry.is_directory()) {
				continue;
			}

			for (const auto& relativeRoot :
				ResolveBundleCommandRootDirs(extensionEntry.path())) {
				++m_lastDiagnostics.rootsScanned;
				const auto commandsRoot = extensionEntry.path() / relativeRoot;
				std::error_code commandsEc;
				if (!std::filesystem::is_directory(commandsRoot, commandsEc) ||
					commandsEc) {
					continue;
				}

				for (const auto& commandFile :
					std::filesystem::recursive_directory_iterator(commandsRoot, ec)) {
					if (ec) {
						break;
					}

					if (!commandFile.is_regular_file()) {
						continue;
					}

					const auto extension = ToLower(commandFile.path().extension().wstring());
					if (extension != L".md") {
						continue;
					}

					const auto rawContent = ReadVerifiedBundleFile(
						commandFile.path(),
						extensionEntry.path(),
						2 * 1024 * 1024);
					if (!rawContent.has_value()) {
						++m_lastDiagnostics.filesRejectedUnsafe;
						continue;
					}
					const std::wstring raw = rawContent.value();
					if (ParseBundleFrontmatterBool(
						raw,
						L"disable-model-invocation",
						false)) {
						++m_lastDiagnostics.filesSkippedDisabled;
						continue;
					}
					const std::wstring promptTemplate = StripBundleFrontmatter(raw);
					if (promptTemplate.empty()) {
						++m_lastDiagnostics.filesSkippedEmptyPrompt;
						continue;
					}

					const std::filesystem::path relativePath =
						std::filesystem::relative(commandFile.path(), commandsRoot, commandsEc);
					if (commandsEc) {
						continue;
					}

					std::wstring defaultName = relativePath.wstring();
					if (defaultName.size() > 3) {
						defaultName = defaultName.substr(0, defaultName.size() - 3);
					}
					std::replace(
						defaultName.begin(),
						defaultName.end(),
						std::filesystem::path::preferred_separator,
						L':');

					const std::wstring configuredName =
						ParseBundleFrontmatterField(raw, L"name");
					const std::wstring rawName =
						configuredName.empty() ? defaultName : configuredName;
					if (Trim(rawName).empty()) {
						++m_lastDiagnostics.filesSkippedInvalidName;
						continue;
					}

					const std::wstring configuredDescription =
						ParseBundleFrontmatterField(raw, L"description");
					std::wstring description = configuredDescription;
					if (description.empty()) {
						std::wstringstream lineStream(promptTemplate);
						std::wstring firstLine;
						while (std::getline(lineStream, firstLine)) {
							firstLine = Trim(firstLine);
							if (!firstLine.empty()) {
								description = firstLine;
								break;
							}
						}
					}
					if (description.empty()) {
						description = rawName;
					}

					SkillsCommandSpec spec;
					spec.name = NormalizeBundleCommandName(rawName);
					spec.skillName = rawName;
					spec.description = description;
					spec.promptTemplate = promptTemplate;
					spec.sourceFilePath = Utf8ToWideLocal(
						std::filesystem::relative(commandFile.path(), m_extensionsRoot).string());

					commands.push_back(std::move(spec));
					++m_lastDiagnostics.filesLoaded;
				}
			}
		}

		return commands;
	}

} // namespace blazeclaw::core
