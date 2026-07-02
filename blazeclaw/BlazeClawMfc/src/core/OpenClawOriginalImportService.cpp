#include "pch.h"
#include "OpenClawOriginalImportService.h"
#include "SharedFrontmatterCompat.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <set>

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

		std::wstring ReadUtf8FileToWide(
			const std::filesystem::path& path,
			std::vector<std::wstring>& diagnostics) {
			std::wifstream wideInput(path);
			if (wideInput.is_open()) {
				const std::wstring wideContent(
					(std::istreambuf_iterator<wchar_t>(wideInput)),
					std::istreambuf_iterator<wchar_t>());
				if (!wideContent.empty()) {
					return wideContent;
				}
			}

			std::ifstream input(path, std::ios::binary);
			if (!input.is_open()) {
				diagnostics.push_back(
					L"failed to read SKILL.md for extracted runtime contract");
				return {};
			}

			const std::string content(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			if (content.empty()) {
				diagnostics.push_back(
					L"empty SKILL.md content while extracting runtime contract");
				return {};
			}

			const auto maybeUtf16Le = [&content]() -> std::optional<std::wstring> {
				if (content.size() < 4 || (content.size() % 2) != 0) {
					return std::nullopt;
				}

				std::size_t zeroHighByteCount = 0;
				for (std::size_t i = 1; i < content.size(); i += 2) {
					if (content[i] == '\0') {
						++zeroHighByteCount;
					}
				}

				if (zeroHighByteCount < (content.size() / 4)) {
					return std::nullopt;
				}

				std::wstring decoded;
				decoded.reserve(content.size() / 2);
				for (std::size_t i = 0; i + 1 < content.size(); i += 2) {
					const auto low = static_cast<unsigned char>(content[i]);
					const auto high = static_cast<unsigned char>(content[i + 1]);
					decoded.push_back(static_cast<wchar_t>((high << 8) | low));
				}
				return decoded;
			};

			if (const auto utf16 = maybeUtf16Le(); utf16.has_value()) {
				return utf16.value();
			}

			return Utf8ToWide(content);
		}

		std::wstring ExtractMarkdownBody(const std::wstring& markdown) {
			if (markdown.empty()) {
				return {};
			}

			const std::size_t firstFence = markdown.find(L"---");
			if (firstFence == std::wstring::npos || firstFence != 0) {
				return markdown;
			}

			const std::size_t secondFence = markdown.find(L"\n---", firstFence + 3);
			if (secondFence == std::wstring::npos) {
				return markdown;
			}

			const std::size_t bodyStart = secondFence + 4;
			if (bodyStart >= markdown.size()) {
				return {};
			}

			return Trim(markdown.substr(bodyStart));
		}

		std::optional<std::wstring> ExtractFirstUrl(const std::wstring& value) {
			if (value.empty()) {
				return std::nullopt;
			}

			std::size_t begin = value.find(L"https://");
			if (begin == std::wstring::npos) {
				begin = value.find(L"http://");
			}
			if (begin == std::wstring::npos) {
				return std::nullopt;
			}

			std::size_t end = begin;
			while (end < value.size()) {
				const wchar_t ch = value[end];
				if (std::iswspace(ch) != 0 || ch == L'`' || ch == L'"' || ch == L'\'' || ch == L')' || ch == L']') {
					break;
				}
				++end;
			}

			if (end <= begin) {
				return std::nullopt;
			}

			return value.substr(begin, end - begin);
		}

		std::optional<std::wstring> ExtractJsonStringField(
			const std::wstring& text,
			const std::wstring& fieldName) {
			if (text.empty() || fieldName.empty()) {
				return std::nullopt;
			}

			const std::wstring key = L"\"" + fieldName + L"\"";
			const std::size_t keyPos = text.find(key);
			if (keyPos == std::wstring::npos) {
				return std::nullopt;
			}

			const std::size_t colonPos = text.find(L':', keyPos + key.size());
			if (colonPos == std::wstring::npos) {
				return std::nullopt;
			}

			const std::size_t quoteBegin = text.find(L'"', colonPos + 1);
			if (quoteBegin == std::wstring::npos) {
				return std::nullopt;
			}

			const std::size_t quoteEnd = text.find(L'"', quoteBegin + 1);
			if (quoteEnd == std::wstring::npos || quoteEnd <= quoteBegin + 1) {
				return std::nullopt;
			}

			return text.substr(quoteBegin + 1, quoteEnd - quoteBegin - 1);
		}

		std::optional<std::wstring> ExtractJsonFenceBlock(const std::wstring& body) {
			const std::size_t fenceStart = ToLower(body).find(L"```json");
			if (fenceStart == std::wstring::npos) {
				return std::nullopt;
			}

			const std::size_t payloadStart = body.find(L'\n', fenceStart);
			if (payloadStart == std::wstring::npos) {
				return std::nullopt;
			}

			const std::size_t fenceEnd = body.find(L"```", payloadStart + 1);
			if (fenceEnd == std::wstring::npos || fenceEnd <= payloadStart + 1) {
				return std::nullopt;
			}

			return Trim(body.substr(payloadStart + 1, fenceEnd - payloadStart - 1));
		}

		std::vector<std::wstring> ExtractTriggerHints(const std::wstring& body) {
			std::set<std::wstring> unique;

			std::size_t cursor = 0;
			while (cursor < body.size()) {
				const std::size_t left = body.find(L'“', cursor);
				if (left == std::wstring::npos) {
					break;
				}
				const std::size_t right = body.find(L'”', left + 1);
				if (right == std::wstring::npos) {
					break;
				}
				const std::wstring quoted = Trim(body.substr(left + 1, right - left - 1));
				if (!quoted.empty()) {
					unique.insert(quoted);
				}
				cursor = right + 1;
			}

			std::size_t lineBegin = 0;
			while (lineBegin < body.size()) {
				const std::size_t lineEnd = body.find(L'\n', lineBegin);
				const std::size_t len = lineEnd == std::wstring::npos
					? body.size() - lineBegin
					: lineEnd - lineBegin;
				std::wstring line = Trim(body.substr(lineBegin, len));
				if (line.size() > 2 && (line.rfind(L"- ", 0) == 0 || line.rfind(L"* ", 0) == 0)) {
					line = Trim(line.substr(2));
					if (!line.empty() && line.front() == L'“' && line.back() == L'”' && line.size() >= 2) {
						line = Trim(line.substr(1, line.size() - 2));
					}
					if (!line.empty() && line.size() <= 128) {
						unique.insert(line);
					}
				}

				if (lineEnd == std::wstring::npos) {
					break;
				}
				lineBegin = lineEnd + 1;
			}

			return std::vector<std::wstring>(unique.begin(), unique.end());
		}

		OpenClawOriginalExtractedRuntimeContractSpec BuildExtractedRuntimeContract(
			const OpenClawOriginalImportRequest& request,
			const std::optional<SkillsMetadataSpec>& normalizedMetadata,
			std::vector<std::wstring>& diagnostics) {
			OpenClawOriginalExtractedRuntimeContractSpec contract;
			if (normalizedMetadata.has_value()) {
				contract.skillKey = Trim(normalizedMetadata->skillKey);
			}
			if (contract.skillKey.empty() && request.frontmatter.has_value()) {
				contract.skillKey = Trim(request.frontmatter->name);
			}

			const std::wstring markdown = ReadUtf8FileToWide(request.skillFile, diagnostics);
			const std::wstring body = ExtractMarkdownBody(markdown);
			contract.triggerHints = ExtractTriggerHints(body);

			auto output = OpenClawOriginalExtractedOutputSpec{};
			const auto jsonBlock = ExtractJsonFenceBlock(body);
			if (jsonBlock.has_value()) {
				output.jsonPayload = jsonBlock.value();
				if (const auto type = ExtractJsonStringField(output.jsonPayload, L"type"); type.has_value()) {
					output.kind = type.value();
				}
				if (const auto title = ExtractJsonStringField(output.jsonPayload, L"title"); title.has_value()) {
					output.title = title.value();
				}
				if (const auto url = ExtractJsonStringField(output.jsonPayload, L"url"); url.has_value()) {
					output.url = url.value();
				}
			}

			if (output.url.empty()) {
				if (const auto url = ExtractFirstUrl(body); url.has_value()) {
					output.url = url.value();
				}
			}

			if (output.kind.empty() && !output.url.empty()) {
				output.kind = L"webview";
			}
			if (output.kind.empty() && jsonBlock.has_value()) {
				output.kind = L"json";
			}

			if (!output.kind.empty() || !output.url.empty() || !output.jsonPayload.empty()) {
				contract.output = output;
			}

			if (contract.triggerHints.empty()) {
				contract.diagnostics.push_back(
					L"extracted runtime contract missing trigger hints");
			}
			if (!contract.output.has_value()) {
				contract.diagnostics.push_back(
					L"extracted runtime contract missing output payload");
			}
			if (contract.skillKey.empty()) {
				contract.diagnostics.push_back(
					L"extracted runtime contract missing normalized skill key");
			}

			contract.complete =
				!contract.skillKey.empty() &&
				!contract.triggerHints.empty() &&
				contract.output.has_value();

			return contract;
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

		const std::wstring metadataRaw = GetFrontmatterStringCompat(
			request.frontmatter->fields,
			L"metadata");
		const auto metadataObject = ResolveOpenClawManifestBlockCompat(
			request.frontmatter->fields,
			L"metadata");
		if (!metadataObject.has_value()) {
			result.diagnostics.push_back(
				L"metadata block missing or not parseable; using defaults");
		}

		if (!metadataRaw.empty() &&
			ToLower(metadataRaw).find(L"clawdbot") != std::wstring::npos) {
			result.metadataConvertedFromClawdbot = true;
			result.diagnostics.push_back(
				L"metadata converted from clawdbot to openclaw compatibility");
		}

		result.normalizedMetadata =
			ResolveOpenClawMetadataCompat(request.frontmatter.value());
		result.extractedRuntimeContract = BuildExtractedRuntimeContract(
			request,
			result.normalizedMetadata,
			result.diagnostics);
		for (const auto& contractDiagnostic :
			result.extractedRuntimeContract->diagnostics) {
			result.diagnostics.push_back(contractDiagnostic);
		}
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

		const bool hasGeneratedInvokableContract =
			result.extractedRuntimeContract.has_value() &&
			result.extractedRuntimeContract->complete;

		if ((result.hasToolManifest || hasGeneratedInvokableContract) &&
			appConfig.skills.openclawOriginal.autoImportTools) {
			result.activationState = OpenClawOriginalImportActivationState::ToolEnabled;
			if (!result.hasToolManifest && hasGeneratedInvokableContract) {
				result.diagnostics.push_back(
					L"tool-enabled via generated manifestless runtime contract");
			}
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
