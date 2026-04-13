#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace blazeclaw::core::filesystem {

	enum class VerifiedOpenFailureReason {
		Path,
		Validation,
		Io,
	};

	enum class VerifiedOpenAllowedType {
		File,
		Directory,
	};

	struct VerifiedOpenPolicy {
		bool rejectPathSymlink = false;
		bool rejectHardlinks = false;
		std::optional<std::uint64_t> maxBytes;
		VerifiedOpenAllowedType allowedType = VerifiedOpenAllowedType::File;
	};

	struct VerifiedOpenRequest {
		std::filesystem::path filePath;
		std::optional<std::filesystem::path> resolvedPath;
		VerifiedOpenPolicy policy;
	};

	struct VerifiedOpenFileMetadata {
		bool isDirectory = false;
		bool isFile = false;
		std::uint64_t sizeBytes = 0;
		std::uint32_t hardLinkCount = 0;
		std::uint64_t volumeSerialNumber = 0;
		std::uint64_t fileIndex = 0;
	};

	struct VerifiedOpenFileResult {
		bool ok = false;
		VerifiedOpenFailureReason reason = VerifiedOpenFailureReason::Io;
		bool rejectedBySymlinkPolicy = false;
		std::wstring detail;
		std::filesystem::path resolvedPath;
		std::wstring utf8Content;
		VerifiedOpenFileMetadata preOpen;
		VerifiedOpenFileMetadata opened;
	};

	[[nodiscard]] bool SameFileIdentity(
		const VerifiedOpenFileMetadata& left,
		const VerifiedOpenFileMetadata& right);

	[[nodiscard]] VerifiedOpenFileResult OpenVerifiedFileUtf8Sync(
		const VerifiedOpenRequest& request);

} // namespace blazeclaw::core::filesystem
