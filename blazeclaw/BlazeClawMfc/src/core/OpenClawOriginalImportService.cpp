#include "pch.h"
#include "OpenClawOriginalImportService.h"
#include "SharedFrontmatterCompat.h"

#include <algorithm>
#include <cwctype>
#include <fstream>

namespace blazeclaw::core {

	namespace {

		std::wstring Trim(const std::wstring& value) {
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

		void CopyFileIfExists(
			const std::filesystem::path& source,
			const std::filesystem::path& target,
			std::vector<std::wstring>& diagnostics) {
			std::error_code ec;
			if (!std::filesystem::is_regular_file(source, ec) || ec) {
				return;
			}

			std::filesystem::create_directories(target.parent_path(), ec);
			if (ec) {
				diagnostics.push_back(
					L"failed to create promotion directory: " +
					target.parent_path().wstring());
				return;
			}

			ec.clear();
			std::filesystem::copy_file(
				source,
				target,
				std::filesystem::copy_options::overwrite_existing,
				ec);
			if (ec) {
				diagnostics.push_back(
					L"failed to promote artifact: " + source.wstring());
			}
		}

	} // namespace

	std::filesystem::path OpenClawOriginalImportService::ResolveManagedPromotionDir(
		const std::filesystem::path& workspaceRoot,
		const std::wstring& skillName) {
		std::filesystem::path managedRoot =
			workspaceRoot / L".blazeclaw" / L"skills" / L"openclaw-original";
		managedRoot /= skillName;
		return managedRoot;
	}

	OpenClawOriginalImportResult OpenClawOriginalImportService::ImportSkill(
		const OpenClawOriginalImportRequest& request,
		const blazeclaw::config::AppConfig& appConfig) const {
		OpenClawOriginalImportResult result;

		if (!appConfig.skills.openclawOriginal.enabled) {
			result.activationState = OpenClawOriginalImportActivationState::Failed;
			result.diagnostics.push_back(
				L"openclaw-original import disabled by policy");
			return result;
		}

		if (!request.validFrontmatter || !request.frontmatter.has_value()) {
			result.activationState = OpenClawOriginalImportActivationState::Failed;
			result.diagnostics.push_back(
				L"malformed metadata: SKILL frontmatter parse failed");
			return result;
		}

		const auto metadataObject = ResolveOpenClawManifestBlockCompat(
			request.frontmatter->fields,
			L"metadata");
		if (!metadataObject.has_value()) {
			result.diagnostics.push_back(
				L"metadata block missing or not parseable; using defaults");
		}
		else {
			const auto clawdbotIt = metadataObject->find("clawdbot");
			if (clawdbotIt != metadataObject->end() && clawdbotIt->is_object()) {
				result.metadataConvertedFromClawdbot = true;
				result.diagnostics.push_back(
					L"metadata converted from clawdbot to openclaw compatibility");
			}
		}

		result.normalizedMetadata =
			ResolveOpenClawMetadataCompat(request.frontmatter.value());
		result.activationState = OpenClawOriginalImportActivationState::Imported;

		const auto toolManifestPath = request.skillDir / L"tool-manifest.json";
		std::error_code ec;
		result.hasToolManifest =
			std::filesystem::is_regular_file(toolManifestPath, ec) && !ec;
		if (!result.hasToolManifest) {
			result.diagnostics.push_back(
				L"missing tool manifest: tool-manifest.json");
			if (appConfig.skills.openclawOriginal.autoImportTools) {
				result.diagnostics.push_back(
					L"autoImportTools enabled but manifest missing");
			}
		}
		else {
			std::ifstream manifestInput(toolManifestPath, std::ios::binary);
			if (!manifestInput.is_open()) {
				result.diagnostics.push_back(
					L"tool manifest unreadable: " + toolManifestPath.wstring());
			}
			else {
				const std::string manifestText(
					(std::istreambuf_iterator<char>(manifestInput)),
					std::istreambuf_iterator<char>());
				const std::wstring lowerManifest = ToLower(Utf8ToWide(manifestText));
				if (lowerManifest.find(L"\"tools\"") == std::wstring::npos) {
					result.diagnostics.push_back(
						L"malformed metadata: tool-manifest.json missing tools array");
				}
				if (lowerManifest.find(L"\"commands\"") != std::wstring::npos &&
					lowerManifest.find(L"\"command\"") == std::wstring::npos) {
					result.diagnostics.push_back(
						L"unresolved command dependencies in tool-manifest.json");
				}
			}
		}

		if (result.hasToolManifest && appConfig.skills.openclawOriginal.autoImportTools) {
			result.activationState = OpenClawOriginalImportActivationState::ToolEnabled;
		}

		if (appConfig.skills.openclawOriginal.promoteToManaged) {
			const std::wstring skillName = Trim(request.frontmatter->name);
			if (skillName.empty()) {
				result.diagnostics.push_back(
					L"promotion skipped: unnamed skill");
			}
			else {
				result.promotedDir = ResolveManagedPromotionDir(
					request.workspaceRoot,
					skillName);

				CopyFileIfExists(
					request.skillFile,
					result.promotedDir / L"SKILL.md",
					result.diagnostics);
				CopyFileIfExists(
					request.skillDir / L"tool-manifest.json",
					result.promotedDir / L"tool-manifest.json",
					result.diagnostics);
				CopyFileIfExists(
					request.skillDir / L"_meta.json",
					result.promotedDir / L"_meta.json",
					result.diagnostics);
			}
		}

		return result;
	}

} // namespace blazeclaw::core
