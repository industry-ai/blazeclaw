#include "pch.h"
#include "SkillsInventoryValidationService.h"

#include "SkillsFrontmatterCompat.h"
#include "filesystem/SafeOpenSync.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <sstream>

namespace blazeclaw::core {

	namespace {

		constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
		constexpr std::uint64_t kFnvPrime = 1099511628211ull;

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

		std::optional<std::wstring> ReadEnvVar(const wchar_t* key) {
			wchar_t* value = nullptr;
			std::size_t length = 0;
			if (_wdupenv_s(&value, &length, key) != 0 ||
				value == nullptr ||
				length == 0) {
				if (value != nullptr) {
					free(value);
				}
				return std::nullopt;
			}

			const std::wstring result(value);
			free(value);
			return result;
		}

		std::vector<std::wstring> ParseEnvPathList(const std::wstring& raw) {
			std::vector<std::wstring> values;
			std::wstring current;
			current.reserve(raw.size());

			const auto flush = [&values, &current]() {
				const std::wstring trimmed = Trim(current);
				if (!trimmed.empty()) {
					values.push_back(trimmed);
				}
				current.clear();
			};

			for (const wchar_t ch : raw) {
				if (ch == L';' || ch == L',' || ch == L'|') {
					flush();
					continue;
				}
				current.push_back(ch);
			}

			flush();
			return values;
		}

		std::wstring Utf8ToWide(const std::string& value) {
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

		std::string WideToUtf8(const std::wstring& value) {
			if (value.empty()) {
				return {};
			}

			const auto toAsciiFallback = [&value]() {
				std::string output;
				output.reserve(value.size());
				for (const wchar_t ch : value) {
					output.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
				}
				return output;
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
				return toAsciiFallback();
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
				return toAsciiFallback();
			}

			return output;
		}

		std::filesystem::path CanonicalOrSelf(const std::filesystem::path& pathValue) {
			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(pathValue, ec);
			if (ec) {
				return pathValue.lexically_normal();
			}
			return canonical;
		}

		std::filesystem::path ResolveDefaultRoot(
			const std::filesystem::path& workspaceRoot,
			const std::wstring& envKey,
			const std::filesystem::path& directPath,
			const std::filesystem::path& nestedPath) {
			if (const auto overridePath = ReadEnvVar(envKey.c_str()); overridePath.has_value()) {
				std::filesystem::path root(overridePath.value());
				if (root.is_relative()) {
					root = workspaceRoot / root;
				}
				return CanonicalOrSelf(root);
			}

			std::error_code ec;
			if (std::filesystem::is_directory(directPath, ec) && !ec) {
				return CanonicalOrSelf(directPath);
			}

			ec.clear();
			if (std::filesystem::is_directory(nestedPath, ec) && !ec) {
				return CanonicalOrSelf(nestedPath);
			}

			return CanonicalOrSelf(directPath);
		}

		std::vector<std::filesystem::path> ResolvePluginRoots(
			const std::filesystem::path& workspaceRoot,
			std::vector<std::wstring>& outWarnings) {
			std::vector<std::filesystem::path> roots;
			if (const auto raw = ReadEnvVar(L"BLAZECLAW_PLUGIN_SKILL_DIRS"); raw.has_value()) {
				for (const auto& item : ParseEnvPathList(raw.value())) {
					std::filesystem::path root(item);
					if (root.is_relative()) {
						root = workspaceRoot / root;
					}
					roots.push_back(CanonicalOrSelf(root));
				}
			}

			std::set<std::filesystem::path> dedupe;
			std::vector<std::filesystem::path> unique;
			for (const auto& root : roots) {
				if (dedupe.insert(root).second) {
					unique.push_back(root);
				}
			}
			if (unique.empty()) {
				outWarnings.push_back(L"No plugin roots configured via BLAZECLAW_PLUGIN_SKILL_DIRS.");
			}
			return unique;
		}

		std::map<std::wstring, std::filesystem::path> CollectSkillDirsByName(
			const std::filesystem::path& root,
			bool& outRootFound,
			std::vector<std::wstring>& outWarnings) {
			std::map<std::wstring, std::filesystem::path> entries;
			std::error_code ec;
			if (!std::filesystem::is_directory(root, ec) || ec) {
				outRootFound = false;
				outWarnings.push_back(L"Skill root not found: " + root.wstring());
				return entries;
			}
			outRootFound = true;

			for (const auto& item : std::filesystem::directory_iterator(root, ec)) {
				if (ec) {
					break;
				}
				if (!item.is_directory()) {
					continue;
				}
				const auto skillFile = item.path() / L"SKILL.md";
				std::error_code skillEc;
				if (!std::filesystem::is_regular_file(skillFile, skillEc) || skillEc) {
					continue;
				}

				const std::wstring skillName = ToLower(item.path().filename().wstring());
				if (skillName.empty()) {
					continue;
				}
				entries.emplace(skillName, CanonicalOrSelf(item.path()));
			}

			return entries;
		}

		bool IsPathInside(
			const std::filesystem::path& root,
			const std::filesystem::path& candidate) {
			const auto canonicalRoot = CanonicalOrSelf(root);
			const auto canonicalCandidate = CanonicalOrSelf(candidate);
			auto rootIt = canonicalRoot.begin();
			auto candidateIt = canonicalCandidate.begin();

			for (; rootIt != canonicalRoot.end(); ++rootIt, ++candidateIt) {
				if (candidateIt == canonicalCandidate.end() || *rootIt != *candidateIt) {
					return false;
				}
			}
			return true;
		}

		std::string NormalizeNewlines(const std::string& input) {
			std::string normalized;
			normalized.reserve(input.size());
			for (std::size_t i = 0; i < input.size(); ++i) {
				const char ch = input[i];
				if (ch == '\r') {
					if (i + 1 < input.size() && input[i + 1] == '\n') {
						++i;
					}
					normalized.push_back('\n');
					continue;
				}
				normalized.push_back(ch);
			}
			return normalized;
		}

		std::uint64_t HashUtf8(const std::string& text) {
			std::uint64_t hash = kFnvOffsetBasis;
			for (const unsigned char byte : text) {
				hash ^= static_cast<std::uint64_t>(byte);
				hash *= kFnvPrime;
			}
			return hash;
		}

		struct SkillFileReadResult {
			bool ok = false;
			std::string content;
			std::wstring detail;
		};

		SkillFileReadResult ReadUtf8File(
			const std::filesystem::path& file,
			const std::uint64_t maxBytes,
			const bool rejectPathSymlink,
			std::vector<std::wstring>& outWarnings) {
			const auto canonical = CanonicalOrSelf(file);
			blazeclaw::core::filesystem::VerifiedOpenRequest request;
			request.filePath = file;
			request.resolvedPath = canonical;
			request.policy.rejectPathSymlink = rejectPathSymlink;
			request.policy.maxBytes = maxBytes;
			request.policy.allowedType =
				blazeclaw::core::filesystem::VerifiedOpenAllowedType::File;
			const auto result = blazeclaw::core::filesystem::OpenVerifiedFileUtf8Sync(request);
			if (!result.ok) {
				outWarnings.push_back(L"Read failure for " + file.wstring() + L": " + result.detail);
				SkillFileReadResult failure;
				failure.ok = false;
				failure.detail = result.detail;
				return failure;
			}

			SkillFileReadResult success;
			success.ok = true;
			success.content = WideToUtf8(result.utf8Content);
			return success;
		}

		std::map<std::wstring, std::filesystem::path> CollectExtensionSkillDirs(
			const std::filesystem::path& extensionsRoot,
			bool& outFound,
			std::vector<std::wstring>& outWarnings) {
			std::map<std::wstring, std::filesystem::path> entries;
			std::error_code ec;
			if (!std::filesystem::is_directory(extensionsRoot, ec) || ec) {
				outFound = false;
				outWarnings.push_back(L"Extensions root not found: " + extensionsRoot.wstring());
				return entries;
			}
			outFound = true;

			for (const auto& item : std::filesystem::recursive_directory_iterator(extensionsRoot, ec)) {
				if (ec) {
					break;
				}
				if (!item.is_regular_file()) {
					continue;
				}
				if (ToLower(item.path().filename().wstring()) != L"skill.md") {
					continue;
				}
				const auto skillDir = CanonicalOrSelf(item.path().parent_path());
				const std::wstring skillName = ToLower(skillDir.filename().wstring());
				if (skillName.empty()) {
					continue;
				}
				entries.emplace(skillName, skillDir);
			}
			return entries;
		}

		bool IsReachableFromAnyRoot(
			const std::filesystem::path& skillDir,
			const std::vector<std::filesystem::path>& roots) {
			for (const auto& root : roots) {
				std::error_code ec;
				if (!std::filesystem::is_directory(root, ec) || ec) {
					continue;
				}
				if (IsPathInside(root, skillDir)) {
					return true;
				}
			}
			return false;
		}

		void AppendStringListMarkdown(
			std::ostringstream& out,
			const std::string& title,
			const std::vector<std::wstring>& values) {
			out << "## " << title << "\n\n";
			if (values.empty()) {
				out << "- (none)\n\n";
				return;
			}
			for (const auto& value : values) {
				out << "- " << WideToUtf8(value) << "\n";
			}
			out << "\n";
		}

	} // namespace

	SkillsInventoryValidationReport SkillsInventoryValidationService::BuildReport(
		const std::filesystem::path& workspaceRoot,
		const blazeclaw::config::AppConfig& appConfig) const {
		SkillsInventoryValidationReport report;

		report.openClawBundledRoot = ResolveDefaultRoot(
			workspaceRoot,
			L"BLAZECLAW_OPENCLAW_BUNDLED_SKILLS_DIR",
			workspaceRoot / L"openclaw" / L"skills",
			workspaceRoot / L"docs" / L"OpenClaw" / L"claw-ui-hzw" / L"skills");
		report.blazeClawBundledRoot = ResolveDefaultRoot(
			workspaceRoot,
			L"BLAZECLAW_BUNDLED_SKILLS_DIR",
			workspaceRoot / L"skills-bundled",
			workspaceRoot / L"blazeclaw" / L"skills-bundled");
		report.extensionsRoot = ResolveDefaultRoot(
			workspaceRoot,
			L"BLAZECLAW_EXTENSIONS_DIR",
			workspaceRoot / L"extensions",
			workspaceRoot / L"blazeclaw" / L"extensions");
		report.pluginRoots = ResolvePluginRoots(workspaceRoot, report.warnings);

		auto openClawSkills = CollectSkillDirsByName(
			report.openClawBundledRoot,
			report.openClawBundledRootFound,
			report.warnings);
		auto blazeClawBundledSkills = CollectSkillDirsByName(
			report.blazeClawBundledRoot,
			report.blazeClawBundledRootFound,
			report.warnings);

		for (const auto& [name, _] : openClawSkills) {
			if (blazeClawBundledSkills.find(name) == blazeClawBundledSkills.end()) {
				report.missingOpenClawBundledSkills.push_back(name);
			}
		}
		for (const auto& [name, _] : blazeClawBundledSkills) {
			if (openClawSkills.find(name) == openClawSkills.end()) {
				report.extraBlazeClawBundledSkills.push_back(name);
			}
		}

		const std::uint64_t maxBytes =
			static_cast<std::uint64_t>(appConfig.skills.limits.maxSkillFileBytes);
		for (const auto& [name, openDir] : openClawSkills) {
			const auto blazeIt = blazeClawBundledSkills.find(name);
			if (blazeIt == blazeClawBundledSkills.end()) {
				continue;
			}
			const auto openFile = openDir / L"SKILL.md";
			const auto blazeFile = blazeIt->second / L"SKILL.md";
			const auto openRead = ReadUtf8File(
				openFile,
				maxBytes,
				appConfig.skills.load.rejectPathSymlink,
				report.warnings);
			const auto blazeRead = ReadUtf8File(
				blazeFile,
				maxBytes,
				appConfig.skills.load.rejectPathSymlink,
				report.warnings);
			if (!openRead.ok || !blazeRead.ok) {
				continue;
			}

			const auto openNormalized = NormalizeNewlines(openRead.content);
			const auto blazeNormalized = NormalizeNewlines(blazeRead.content);
			const auto openHash = HashUtf8(openNormalized);
			const auto blazeHash = HashUtf8(blazeNormalized);
			if (openHash != blazeHash) {
				report.driftedCommonSkills.push_back(
					SkillsInventoryValidationDriftEntry{
						.skillName = name,
						.openClawSkillFile = openFile,
						.blazeClawSkillFile = blazeFile,
						.openClawHash = openHash,
						.blazeClawHash = blazeHash,
					});
			}
		}

		bool extensionsFound = false;
		const auto extensionSkills = CollectExtensionSkillDirs(
			report.extensionsRoot,
			extensionsFound,
			report.warnings);
		report.extensionsRootFound = extensionsFound;
		for (const auto& [name, dir] : extensionSkills) {
			if (!IsReachableFromAnyRoot(dir, report.pluginRoots)) {
				report.unreachableExtensionSkills.push_back(
					SkillsInventoryValidationReachabilityEntry{
						.skillName = name,
						.skillDir = dir,
						.reason = L"not under any configured plugin root",
					});
			}
		}

		auto inspectFrontmatter = [&report, &appConfig, maxBytes](
			const std::filesystem::path& skillDir,
			const std::wstring& sourceTag) {
			const auto skillFile = skillDir / L"SKILL.md";
			std::error_code ec;
			if (!std::filesystem::is_regular_file(skillFile, ec) || ec) {
				return;
			}

			const auto fileSize = std::filesystem::file_size(skillFile, ec);
			if (!ec && fileSize > maxBytes) {
				report.frontmatterIssues.push_back(
					SkillsInventoryValidationFrontmatterIssue{
						.skillFile = skillFile,
						.issueType = L"oversized-frontmatter",
						.detail = sourceTag,
						.sizeBytes = static_cast<std::uint64_t>(fileSize),
					});
				return;
			}

			const auto read = ReadUtf8File(
				skillFile,
				maxBytes,
				appConfig.skills.load.rejectPathSymlink,
				report.warnings);
			if (!read.ok) {
				report.frontmatterIssues.push_back(
					SkillsInventoryValidationFrontmatterIssue{
						.skillFile = skillFile,
						.issueType = L"invalid-frontmatter",
						.detail = L"read-failed",
						.sizeBytes = 0,
					});
				return;
			}

			std::vector<std::wstring> validationErrors;
			const auto parsed = ParseSkillFrontmatterCompat(
				Utf8ToWide(read.content),
				validationErrors);
			if (!parsed.has_value()) {
				std::wstring detail = sourceTag;
				if (!validationErrors.empty()) {
					detail += L"; ";
					detail += validationErrors.front();
				}
				report.frontmatterIssues.push_back(
					SkillsInventoryValidationFrontmatterIssue{
						.skillFile = skillFile,
						.issueType = L"invalid-frontmatter",
						.detail = detail,
						.sizeBytes = static_cast<std::uint64_t>(read.content.size()),
					});
			}
		};

		for (const auto& [_, dir] : openClawSkills) {
			inspectFrontmatter(dir, L"openclaw-bundled");
		}
		for (const auto& [_, dir] : blazeClawBundledSkills) {
			inspectFrontmatter(dir, L"blazeclaw-bundled");
		}
		for (const auto& [_, dir] : extensionSkills) {
			inspectFrontmatter(dir, L"extensions");
		}

		std::sort(
			report.missingOpenClawBundledSkills.begin(),
			report.missingOpenClawBundledSkills.end());
		std::sort(
			report.extraBlazeClawBundledSkills.begin(),
			report.extraBlazeClawBundledSkills.end());
		std::sort(
			report.driftedCommonSkills.begin(),
			report.driftedCommonSkills.end(),
			[](const SkillsInventoryValidationDriftEntry& left,
				const SkillsInventoryValidationDriftEntry& right) {
				return left.skillName < right.skillName;
			});
		std::sort(
			report.unreachableExtensionSkills.begin(),
			report.unreachableExtensionSkills.end(),
			[](const SkillsInventoryValidationReachabilityEntry& left,
				const SkillsInventoryValidationReachabilityEntry& right) {
				return left.skillName < right.skillName;
			});
		std::sort(
			report.frontmatterIssues.begin(),
			report.frontmatterIssues.end(),
			[](const SkillsInventoryValidationFrontmatterIssue& left,
				const SkillsInventoryValidationFrontmatterIssue& right) {
				if (left.issueType == right.issueType) {
					return left.skillFile < right.skillFile;
				}
				return left.issueType < right.issueType;
			});

		return report;
	}

	std::string SkillsInventoryValidationService::BuildMarkdownReport(
		const SkillsInventoryValidationReport& report) const {
		std::ostringstream out;
		out << "# Phase 3 Skills Inventory Validation Report\n\n";
		out << "## Scope\n\n";
		out << "- OpenClaw bundled root: `" << WideToUtf8(report.openClawBundledRoot.wstring()) << "`";
		out << (report.openClawBundledRootFound ? " (found)\n" : " (missing)\n");
		out << "- BlazeClaw bundled root: `" << WideToUtf8(report.blazeClawBundledRoot.wstring()) << "`";
		out << (report.blazeClawBundledRootFound ? " (found)\n" : " (missing)\n");
		out << "- Extensions root: `" << WideToUtf8(report.extensionsRoot.wstring()) << "`";
		out << (report.extensionsRootFound ? " (found)\n" : " (missing)\n");
		out << "- Plugin roots configured: " << report.pluginRoots.size() << "\n\n";

		AppendStringListMarkdown(
			out,
			"OpenClaw bundled skills missing from BlazeClaw",
			report.missingOpenClawBundledSkills);
		AppendStringListMarkdown(
			out,
			"BlazeClaw bundled skills absent from OpenClaw",
			report.extraBlazeClawBundledSkills);

		out << "## Common skills drift (`SKILL.md`, newline-normalized)\n\n";
		if (report.driftedCommonSkills.empty()) {
			out << "- (none)\n\n";
		}
		else {
			for (const auto& drift : report.driftedCommonSkills) {
				out << "- " << WideToUtf8(drift.skillName)
					<< "\n"
					<< "  - openclaw: `" << WideToUtf8(drift.openClawSkillFile.wstring()) << "` hash=" << drift.openClawHash << "\n"
					<< "  - blazeclaw: `" << WideToUtf8(drift.blazeClawSkillFile.wstring()) << "` hash=" << drift.blazeClawHash << "\n";
			}
			out << "\n";
		}

		out << "## Extension skills not reachable through BlazeClaw plugin roots\n\n";
		if (report.unreachableExtensionSkills.empty()) {
			out << "- (none)\n\n";
		}
		else {
			for (const auto& entry : report.unreachableExtensionSkills) {
				out << "- " << WideToUtf8(entry.skillName)
					<< " (`" << WideToUtf8(entry.skillDir.wstring()) << "`): "
					<< WideToUtf8(entry.reason) << "\n";
			}
			out << "\n";
		}

		out << "## Invalid or oversized frontmatter files\n\n";
		if (report.frontmatterIssues.empty()) {
			out << "- (none)\n\n";
		}
		else {
			for (const auto& issue : report.frontmatterIssues) {
				out << "- [" << WideToUtf8(issue.issueType) << "] `" << WideToUtf8(issue.skillFile.wstring()) << "`";
				if (issue.sizeBytes > 0) {
					out << " size=" << issue.sizeBytes;
				}
				if (!issue.detail.empty()) {
					out << " detail=" << WideToUtf8(issue.detail);
				}
				out << "\n";
			}
			out << "\n";
		}

		out << "## Notes\n\n";
		out << "- This report is audit-only and does not perform rewrites.\n";
		if (!report.warnings.empty()) {
			out << "- Warnings:\n";
			for (const auto& warning : report.warnings) {
				out << "  - " << WideToUtf8(warning) << "\n";
			}
		}

		return out.str();
	}

	std::string SkillsInventoryValidationService::BuildJsonReport(
		const SkillsInventoryValidationReport& report) const {
		nlohmann::json payload;
		payload["openClawBundledRoot"] = WideToUtf8(report.openClawBundledRoot.wstring());
		payload["blazeClawBundledRoot"] = WideToUtf8(report.blazeClawBundledRoot.wstring());
		payload["extensionsRoot"] = WideToUtf8(report.extensionsRoot.wstring());
		payload["openClawBundledRootFound"] = report.openClawBundledRootFound;
		payload["blazeClawBundledRootFound"] = report.blazeClawBundledRootFound;
		payload["extensionsRootFound"] = report.extensionsRootFound;

		nlohmann::json pluginRoots = nlohmann::json::array();
		for (const auto& root : report.pluginRoots) {
			pluginRoots.push_back(WideToUtf8(root.wstring()));
		}
		payload["pluginRoots"] = std::move(pluginRoots);

		nlohmann::json missing = nlohmann::json::array();
		for (const auto& entry : report.missingOpenClawBundledSkills) {
			missing.push_back(WideToUtf8(entry));
		}
		payload["missingOpenClawBundledSkills"] = std::move(missing);

		nlohmann::json extra = nlohmann::json::array();
		for (const auto& entry : report.extraBlazeClawBundledSkills) {
			extra.push_back(WideToUtf8(entry));
		}
		payload["extraBlazeClawBundledSkills"] = std::move(extra);

		nlohmann::json drifted = nlohmann::json::array();
		for (const auto& entry : report.driftedCommonSkills) {
			nlohmann::json row;
			row["skillName"] = WideToUtf8(entry.skillName);
			row["openClawSkillFile"] = WideToUtf8(entry.openClawSkillFile.wstring());
			row["blazeClawSkillFile"] = WideToUtf8(entry.blazeClawSkillFile.wstring());
			row["openClawHash"] = entry.openClawHash;
			row["blazeClawHash"] = entry.blazeClawHash;
			drifted.push_back(std::move(row));
		}
		payload["driftedCommonSkills"] = std::move(drifted);

		nlohmann::json unreachable = nlohmann::json::array();
		for (const auto& entry : report.unreachableExtensionSkills) {
			nlohmann::json row;
			row["skillName"] = WideToUtf8(entry.skillName);
			row["skillDir"] = WideToUtf8(entry.skillDir.wstring());
			row["reason"] = WideToUtf8(entry.reason);
			unreachable.push_back(std::move(row));
		}
		payload["unreachableExtensionSkills"] = std::move(unreachable);

		nlohmann::json issues = nlohmann::json::array();
		for (const auto& issue : report.frontmatterIssues) {
			nlohmann::json row;
			row["skillFile"] = WideToUtf8(issue.skillFile.wstring());
			row["issueType"] = WideToUtf8(issue.issueType);
			row["detail"] = WideToUtf8(issue.detail);
			row["sizeBytes"] = issue.sizeBytes;
			issues.push_back(std::move(row));
		}
		payload["frontmatterIssues"] = std::move(issues);

		nlohmann::json warnings = nlohmann::json::array();
		for (const auto& warning : report.warnings) {
			warnings.push_back(WideToUtf8(warning));
		}
		payload["warnings"] = std::move(warnings);

		return payload.dump(2);
	}

} // namespace blazeclaw::core
