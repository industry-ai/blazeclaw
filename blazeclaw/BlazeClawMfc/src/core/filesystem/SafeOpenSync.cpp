#include "pch.h"
#include "SafeOpenSync.h"

#include <vector>

namespace blazeclaw::core::filesystem {

	namespace {

		bool IsZeroIdentityValue(const std::uint64_t value) {
			return value == 0;
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

		bool IsAllowedType(
			const VerifiedOpenFileMetadata& metadata,
			const VerifiedOpenAllowedType allowedType) {
			if (allowedType == VerifiedOpenAllowedType::Directory) {
				return metadata.isDirectory;
			}

			return metadata.isFile;
		}

		bool ValidateAllowedType(
			const VerifiedOpenFileMetadata& metadata,
			const VerifiedOpenPolicy& policy,
			const wchar_t* phase,
			VerifiedOpenFileResult& outResult) {
			if (IsAllowedType(metadata, policy.allowedType)) {
				return true;
			}

			outResult.reason = VerifiedOpenFailureReason::Validation;
			outResult.detail = std::wstring(phase) + L"-type-rejected";
			return false;
		}

		bool ValidateHardlinkPolicy(
			const VerifiedOpenFileMetadata& metadata,
			const VerifiedOpenPolicy& policy,
			const wchar_t* phase,
			VerifiedOpenFileResult& outResult) {
			if (!policy.rejectHardlinks || !metadata.isFile) {
				return true;
			}

			if (metadata.hardLinkCount <= 1) {
				return true;
			}

			outResult.reason = VerifiedOpenFailureReason::Validation;
			outResult.detail = std::wstring(phase) + L"-hardlink-rejected";
			return false;
		}

		bool ValidateMaxBytes(
			const VerifiedOpenFileMetadata& metadata,
			const VerifiedOpenPolicy& policy,
			const wchar_t* phase,
			VerifiedOpenFileResult& outResult) {
			if (!policy.maxBytes.has_value() || !metadata.isFile) {
				return true;
			}

			if (metadata.sizeBytes <= policy.maxBytes.value()) {
				return true;
			}

			outResult.reason = VerifiedOpenFailureReason::Validation;
			outResult.detail = std::wstring(phase) + L"-size-rejected";
			return false;
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

		bool PopulateMetadataFromHandle(
			const HANDLE handle,
			VerifiedOpenFileMetadata& outMetadata,
			std::wstring& outDetail) {
			BY_HANDLE_FILE_INFORMATION info{};
			if (!GetFileInformationByHandle(handle, &info)) {
				outDetail = BuildWin32ErrorDetail(L"fstat-failed", GetLastError());
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

		bool PopulateMetadataFromPathHandle(
			const std::filesystem::path& path,
			const bool rejectPathSymlink,
			VerifiedOpenFileMetadata& outMetadata,
			VerifiedOpenFailureReason& outReason,
			std::wstring& outDetail) {
			DWORD openFlags = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS;
			if (rejectPathSymlink) {
				openFlags |= FILE_FLAG_OPEN_REPARSE_POINT;
			}

			const HANDLE handle = CreateFileW(
				path.c_str(),
				FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr,
				OPEN_EXISTING,
				openFlags,
				nullptr);
			if (handle == INVALID_HANDLE_VALUE) {
				const DWORD errorCode = GetLastError();
				outReason = IsExpectedPathError(errorCode)
					? VerifiedOpenFailureReason::Path
					: VerifiedOpenFailureReason::Io;
				outDetail = BuildWin32ErrorDetail(L"preopen-open-failed", errorCode);
				return false;
			}

			const bool ok = PopulateMetadataFromHandle(handle, outMetadata, outDetail);
			CloseHandle(handle);
			if (!ok) {
				outReason = VerifiedOpenFailureReason::Io;
			}

			return ok;
		}

		std::wstring ReadHandleUtf8Content(
			const HANDLE handle,
			std::wstring& outDetail) {
			LARGE_INTEGER zeroOffset{};
			if (!SetFilePointerEx(handle, zeroOffset, nullptr, FILE_BEGIN)) {
				outDetail = BuildWin32ErrorDetail(L"seek-failed", GetLastError());
				return {};
			}

			std::string bytes;
			bytes.reserve(4096);
			std::vector<char> buffer(4096);
			while (true) {
				DWORD bytesRead = 0;
				if (!ReadFile(
					handle,
					buffer.data(),
					static_cast<DWORD>(buffer.size()),
					&bytesRead,
					nullptr)) {
					outDetail = BuildWin32ErrorDetail(L"read-failed", GetLastError());
					return {};
				}

				if (bytesRead == 0) {
					break;
				}

				bytes.append(buffer.data(), buffer.data() + bytesRead);
			}

			return Utf8ToWide(bytes);
		}

	} // namespace

	bool SameFileIdentity(
		const VerifiedOpenFileMetadata& left,
		const VerifiedOpenFileMetadata& right) {
		if (left.fileIndex != right.fileIndex) {
			return false;
		}

		if (left.volumeSerialNumber == right.volumeSerialNumber) {
			return true;
		}

		return IsZeroIdentityValue(left.volumeSerialNumber) ||
			IsZeroIdentityValue(right.volumeSerialNumber);
	}

	VerifiedOpenFileResult OpenVerifiedFileUtf8Sync(
		const VerifiedOpenRequest& request) {
		VerifiedOpenFileResult result;

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

		if (request.policy.rejectPathSymlink &&
			IsPathChainSymlinkOrReparse(request.filePath, resolvedPath.parent_path())) {
			result.reason = VerifiedOpenFailureReason::Validation;
			result.rejectedBySymlinkPolicy = true;
			result.detail = L"path-symlink-or-reparse-rejected";
			return result;
		}

		VerifiedOpenFailureReason preOpenReason = VerifiedOpenFailureReason::Io;
		std::wstring preOpenDetail;
		if (!PopulateMetadataFromPathHandle(
			resolvedPath,
			request.policy.rejectPathSymlink,
			result.preOpen,
			preOpenReason,
			preOpenDetail)) {
			result.reason = preOpenReason;
			result.detail = preOpenDetail;
			return result;
		}

		if (!ValidateAllowedType(result.preOpen, request.policy, L"preopen", result)) {
			return result;
		}

		if (!ValidateHardlinkPolicy(result.preOpen, request.policy, L"preopen", result)) {
			return result;
		}

		if (!ValidateMaxBytes(result.preOpen, request.policy, L"preopen", result)) {
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
			result.detail = BuildWin32ErrorDetail(L"open-failed", errorCode);
			return result;
		}

		std::wstring openedDetail;
		if (!PopulateMetadataFromHandle(handle, result.opened, openedDetail)) {
			CloseHandle(handle);
			result.reason = VerifiedOpenFailureReason::Io;
			result.detail = openedDetail;
			return result;
		}

		if (!ValidateAllowedType(result.opened, request.policy, L"opened", result)) {
			CloseHandle(handle);
			return result;
		}

		if (!ValidateHardlinkPolicy(result.opened, request.policy, L"opened", result)) {
			CloseHandle(handle);
			return result;
		}

		if (!ValidateMaxBytes(result.opened, request.policy, L"opened", result)) {
			CloseHandle(handle);
			return result;
		}

		if (!SameFileIdentity(result.preOpen, result.opened)) {
			CloseHandle(handle);
			result.reason = VerifiedOpenFailureReason::Validation;
			result.detail = L"identity-mismatch";
			return result;
		}

		if (result.opened.isFile) {
			std::wstring readDetail;
			result.utf8Content = ReadHandleUtf8Content(handle, readDetail);
			if (!readDetail.empty()) {
				CloseHandle(handle);
				result.reason = VerifiedOpenFailureReason::Io;
				result.detail = readDetail;
				return result;
			}
		}

		CloseHandle(handle);
		result.ok = true;
		result.detail.clear();
		return result;
	}

} // namespace blazeclaw::core::filesystem
