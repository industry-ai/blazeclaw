#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace blazeclaw::app::chatui {

enum class StartupPreference {
	PreferSource,
	PreferDist,
};

struct StartupSelection {
	std::filesystem::path selectedPath;
	bool selectedDist = false;
	std::vector<std::filesystem::path> inspectedRoots;
	std::vector<std::filesystem::path> inspectedCandidates;
};

inline void PushUniqueNormalizedPath(
	std::vector<std::filesystem::path>& ordered,
	std::set<std::filesystem::path>& seen,
	const std::filesystem::path& value)
{
	if (value.empty()) {
		return;
	}

	std::error_code ec;
	const auto normalized = std::filesystem::weakly_canonical(value, ec);
	const auto candidate = ec ? value.lexically_normal() : normalized;
	if (candidate.empty()) {
		return;
	}

	if (seen.insert(candidate).second) {
		ordered.push_back(candidate);
	}
}

inline std::vector<std::filesystem::path> BuildOrderedRoots(
	const std::filesystem::path& moduleDir,
	const std::filesystem::path& currentDir)
{
	std::vector<std::filesystem::path> ordered;
	std::set<std::filesystem::path> seen;

	PushUniqueNormalizedPath(ordered, seen, moduleDir);
	if (!moduleDir.empty() && moduleDir.has_parent_path()) {
		PushUniqueNormalizedPath(ordered, seen, moduleDir.parent_path());
	}

	PushUniqueNormalizedPath(ordered, seen, currentDir);

	return ordered;
}

inline std::optional<StartupSelection> FindChatUiIndex(
	const std::filesystem::path& start,
	const StartupPreference preference)
{
	std::filesystem::path cursor = start;
	StartupSelection trace;

	while (!cursor.empty()) {
		trace.inspectedRoots.push_back(cursor);

		const auto projectSource =
			cursor /
			L"BlazeClawMfc" /
			L"web" /
			L"chat" /
			L"index.html";
		const auto projectDist =
			cursor /
			L"BlazeClawMfc" /
			L"web" /
			L"chat" /
			L"dist" /
			L"index.html";
		const auto repoSource =
			cursor /
			L"blazeclaw" /
			L"BlazeClawMfc" /
			L"web" /
			L"chat" /
			L"index.html";
		const auto repoDist =
			cursor /
			L"blazeclaw" /
			L"BlazeClawMfc" /
			L"web" /
			L"chat" /
			L"dist" /
			L"index.html";

		std::vector<std::pair<std::filesystem::path, bool>> orderedCandidates;
		if (preference == StartupPreference::PreferSource) {
			orderedCandidates.push_back({ projectSource, false });
			orderedCandidates.push_back({ repoSource, false });
			orderedCandidates.push_back({ projectDist, true });
			orderedCandidates.push_back({ repoDist, true });
		} else {
			orderedCandidates.push_back({ projectDist, true });
			orderedCandidates.push_back({ repoDist, true });
			orderedCandidates.push_back({ projectSource, false });
			orderedCandidates.push_back({ repoSource, false });
		}

		for (const auto& candidate : orderedCandidates) {
			trace.inspectedCandidates.push_back(candidate.first);
			if (std::filesystem::exists(candidate.first)) {
				trace.selectedPath = candidate.first;
				trace.selectedDist = candidate.second;
				return trace;
			}
		}

		if (!cursor.has_parent_path()) {
			break;
		}

		auto parent = cursor.parent_path();
		if (parent == cursor) {
			break;
		}

		cursor = parent;
	}

	return std::nullopt;
}

inline std::optional<StartupSelection> FindDashboardUiEntry(
	const std::filesystem::path& start,
	const StartupPreference preference,
	const std::wstring& entryFileName)
{
	std::filesystem::path cursor = start;
	StartupSelection trace;
	const std::wstring preferredEntry = entryFileName.empty()
		? L"dashboard.html"
		: entryFileName;

	while (!cursor.empty()) {
		trace.inspectedRoots.push_back(cursor);

		const auto projectSource =
			cursor /
			L"BlazeClawMfc" /
			L"web" /
			L"chat" /
			preferredEntry;
		const auto projectDist =
			cursor /
			L"BlazeClawMfc" /
			L"web" /
			L"chat" /
			L"dist" /
			preferredEntry;
		const auto repoSource =
			cursor /
			L"blazeclaw" /
			L"BlazeClawMfc" /
			L"web" /
			L"chat" /
			preferredEntry;
		const auto repoDist =
			cursor /
			L"blazeclaw" /
			L"BlazeClawMfc" /
			L"web" /
			L"chat" /
			L"dist" /
			preferredEntry;

		std::vector<std::pair<std::filesystem::path, bool>> orderedCandidates;
		if (preference == StartupPreference::PreferSource) {
			orderedCandidates.push_back({ projectSource, false });
			orderedCandidates.push_back({ repoSource, false });
			orderedCandidates.push_back({ projectDist, true });
			orderedCandidates.push_back({ repoDist, true });
		} else {
			orderedCandidates.push_back({ projectDist, true });
			orderedCandidates.push_back({ repoDist, true });
			orderedCandidates.push_back({ projectSource, false });
			orderedCandidates.push_back({ repoSource, false });
		}

		for (const auto& candidate : orderedCandidates) {
			trace.inspectedCandidates.push_back(candidate.first);
			if (std::filesystem::exists(candidate.first)) {
				trace.selectedPath = candidate.first;
				trace.selectedDist = candidate.second;
				return trace;
			}
		}

		if (!cursor.has_parent_path()) {
			break;
		}

		auto parent = cursor.parent_path();
		if (parent == cursor) {
			break;
		}

		cursor = parent;
	}

	return std::nullopt;
}

inline std::optional<StartupSelection> FindDashboardUiIndex(
	const std::filesystem::path& start,
	const StartupPreference preference)
{
	return FindDashboardUiEntry(start, preference, L"dashboard.html");
}

} // namespace blazeclaw::app::chatui
