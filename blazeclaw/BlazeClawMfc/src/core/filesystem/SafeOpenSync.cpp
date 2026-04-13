#include "pch.h"
#include "SafeOpenSync.h"

#include <fstream>
#include <vector>

namespace blazeclaw::core::filesystem {

	namespace {

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

		bool IsAllowedType(
			const VerifiedOpenFileMetadata& metadata,
			const VerifiedOpenAllowedType allowedType) {
			if (allowedType == VerifiedOpenAllowedType::Directory) {
				return metadata.isDirectory;
			}

			return metadata.isFile;
		}

		bool IsExpectedPathError(const DWORD errorCode) {
			return errorCode == ERROR_FILE_NOT_FOUND ||
				errorCode == ERROR_PATH_NOT_FOUND ||
				errorCode == ERROR_DIRECTORY ||
				errorCode == ERROR_CANT_RESOLVE_FILENAME;
		}

		std::wstring BuildWin32ErrorDetail(
			const wchar_t* phase,
			const DWORD errorCode) {
			return std::wstring(phase) + L":" + std::to_wstring(errorCode);
		}

		VerifiedOpenFileMetadata BuildPreOpenMetadata(
			const std::filesystem::path& path,
			std::error_code& outEc) {
			VerifiedOpenFileMetadata metadata;

			const auto status = std::filesystem::status(path, outEc);
			if (outEc) {
				return metadata;
			}

			metadata.isDirectory = std::filesystem::is_directory(status);
			metadata.isFile = std::filesystem::is_regular_file(status);
			if (metadata.isFile) {
				metadata.sizeBytes = static_cast<std::uint64_t>(
					std::filesystem::file_size(path, outEc));
				if (outEc) {
					return metadata;
				}
			}

			const auto hardLinkCount = std::filesystem::hard_link_count(path, outEc);
			if (outEc) {
				return metadata;
			}

			metadata.hardLinkCount =
				static_cast<std::uint32_t>(hardLinkCount);
			return metadata;
		}

		bool IsPathChainSymlinkOrReparse(
			const std::filesystem::path& filePath,
			const std::filesystem::path& stopPath) {
			std::filesystem::path probe = filePath;
			while (true) {
				const DWORD attrs = GetFileAttributesW(probe.c_str());
				if (attrs != INVALID_FILE_ATTRIBUTES &&
					(attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
					return true;
				}

				std::error_code statusEc;
				const auto status =
					std::filesystem::symlink_status(probe, statusEc);
				if (!statusEc && std::filesystem::is_symlink(status)) {
					return true;
				}

				if (probe == stopPath || !probe.has_relative_path()) {
					break;
				}

				probe = probe.parent_path();
			}

			return false;
		}

		bool PopulateOpenedMetadata(
			const HANDLE handle,
			VerifiedOpenFileMetadata& outMetadata,
			std::wstring& outDetail) {
			BY_HANDLE_FILE_INFORMATION info{};
			if (!GetFileInformationByHandle(handle, &info)) {
				const DWORD errorCode = GetLastError();
				outDetail = BuildWin32ErrorDetail(
					L"fstat-failed",
					errorCode);
				return false;
			}

			outMetadata.isDirectory =
				(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
			outMetadata.isFile = !outMetadata.isDirectory;
			outMetadata.hardLinkCount = info.nNumberOfLinks;
			outMetadata.volumeSerialNumber = info.dwVolumeSerialNumber;
			outMetadata.fileIndex =
				(static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) |
				static_cast<std::uint64_t>(info.nFileIndexLow);
			outMetadata.sizeBytes =
				(static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) |
				static_cast<std::uint64_t>(info.nFileSizeLow);
			return true;
		}

		std::wstring ReadHandleUtf8Content(
			const std::filesystem::path& resolvedPath,
			std::wstring& outDetail) {
			std::ifstream input(resolvedPath, std::ios::binary);
			if (!input.is_open()) {
				outDetail = L"read-failed";
				return {};
			}

			std::string content(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			return Utf8ToWide(content);
		}

	} // namespace

	VerifiedOpenFileResult OpenVerifiedFileUtf8Sync(
		const VerifiedOpenRequest& request) {
		VerifiedOpenFileResult result;
		result.reason = VerifiedOpenFailureReason::Io;

		std::error_code resolveEc;
		const std::filesystem::path resolvedPath =
			request.resolvedPath.has_value()
			? request.resolvedPath.value().lexically_normal()
			: std::filesystem::weakly_canonical(request.filePath, resolveEc);
		if (!request.resolvedPath.has_value() && resolveEc) {
			result.reason = VerifiedOpenFailureReason::Path;
			result.detail = L"canonicalize-failed";
			return result;
		}

		result.resolvedPath = resolvedPath;

		if (request.policy.rejectPathSymlink) {
			if (IsPathChainSymlinkOrReparse(request.filePath, resolvedPath.parent_path())) {
				result.reason = VerifiedOpenFailureReason::Validation;
				result.rejectedBySymlinkPolicy = true;
				result.detail = L"path-symlink-or-reparse-rejected";
				return result;
			}
		}

		std::error_code preOpenEc;
		result.preOpen = BuildPreOpenMetadata(resolvedPath, preOpenEc);
		if (preOpenEc) {
			result.reason = VerifiedOpenFailureReason::Path;
			result.detail = L"preopen-stat-failed";
			return result;
		}

		if (!IsAllowedType(result.preOpen, request.policy.allowedType)) {
			result.reason = VerifiedOpenFailureReason::Validation;
			result.detail = L"preopen-type-rejected";
			return result;
		}

		if (request.policy.rejectHardlinks &&
			result.preOpen.isFile &&
			result.preOpen.hardLinkCount > 1) {
			result.reason = VerifiedOpenFailureReason::Validation;
			result.detail = L"preopen-hardlink-rejected";
			return result;
		}

		if (request.policy.maxBytes.has_value() &&
			result.preOpen.isFile &&
			result.preOpen.sizeBytes > request.policy.maxBytes.value()) {
			result.reason = VerifiedOpenFailureReason::Validation;
			result.detail = L"preopen-size-rejected";
			return result;
		}

		DWORD openFlags = FILE_ATTRIBUTE_NORMAL;
		if (request.policy.allowedType == VerifiedOpenAllowedType::Directory ||
			result.preOpen.isDirectory) {
			openFlags |= FILE_FLAG_BACKUP_SEMANTICS;
		}
		if (request.policy.rejectPathSymlink) {
			openFlags |= FILE_FLAG_OPEN_REPARSE_POINT;
		}

		const HANDLE handle = CreateFileW(
			resolvedPath.c_str(),
			GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_EXISTING,
			openFlags,
			nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			const DWORD errorCode = GetLastError();
			result.reason = IsExpectedPathError(errorCode)
				? VerifiedOpenFailureReason::Path
				: VerifiedOpenFailureReason::Io;
			result.detail = BuildWin32ErrorDetail(
				L"open-failed",
				errorCode);
			return result;
		}

		std::wstring openedDetail;
		if (!PopulateOpenedMetadata(handle, result.opened, openedDetail)) {
			CloseHandle(handle);
			result.reason = VerifiedOpenFailureReason::Io;
			result.detail = openedDetail;
			return result;
		}

		if (!IsAllowedType(result.opened, request.policy.allowedType)) {
			CloseHandle(handle);
			result.reason = VerifiedOpenFailureReason::Validation;
			result.detail = L"opened-type-rejected";
			return result;
		}

		if (request.policy.rejectHardlinks &&
			result.opened.isFile &&
			result.opened.hardLinkCount > 1) {
			CloseHandle(handle);
			result.reason = VerifiedOpenFailureReason::Validation;
			result.detail = L"opened-hardlink-rejected";
			return result;
		}

		if (request.policy.maxBytes.has_value() &&
			result.opened.isFile &&
			result.opened.sizeBytes > request.policy.maxBytes.value()) {
			CloseHandle(handle);
			result.reason = VerifiedOpenFailureReason::Validation;
			result.detail = L"opened-size-rejected";
			return result;
		}

		if (result.opened.isFile) {
			std::wstring readDetail;
			result.utf8Content = ReadHandleUtf8Content(resolvedPath, readDetail);
			if (!readDetail.empty()) {
				CloseHandle(handle);
				result.reason = VerifiedOpenFailureReason::Io;
				result.detail = readDetail;
				return result;
			}
		}

		CloseHandle(handle);
		result.ok = true;
		result.reason = VerifiedOpenFailureReason::Io;
		result.detail.clear();
		return result;
	}

} // namespace blazeclaw::core::filesystem
