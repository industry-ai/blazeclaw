#include "pch.h"
#include "AgentsWorkspaceService.h"

#include <fstream>

namespace blazeclaw::core {

	namespace {

		constexpr std::wstring_view kWorkspaceStatePath =
			L".openclaw/workspace-state.json";

		const std::vector<std::wstring> kRequiredBootstrapFiles = {
			L"AGENTS.md",
			L"SOUL.md",
			L"TOOLS.md",
			L"IDENTITY.md",
			L"USER.md",
			L"HEARTBEAT.md",
			L"BOOTSTRAP.md",
		};

		bool ContainsNonEmptyJsonStringField(
			const std::wstring& payload,
			const std::wstring_view fieldName) {
			const std::wstring key = L"\"" + std::wstring(fieldName) + L"\"";
			const std::size_t keyPos = payload.find(key);
			if (keyPos == std::wstring::npos) {
				return false;
			}

			const std::size_t colonPos = payload.find(L':', keyPos + key.size());
			if (colonPos == std::wstring::npos) {
				return false;
			}

			std::size_t valueStart = colonPos + 1;
			while (valueStart < payload.size() &&
				std::iswspace(payload[valueStart]) != 0) {
				++valueStart;
			}

			if (valueStart >= payload.size() || payload[valueStart] != L'"') {
				return false;
			}

			const std::size_t valueEnd = payload.find(L'"', valueStart + 1);
			if (valueEnd == std::wstring::npos) {
				return false;
			}

			return valueEnd > valueStart + 1;
		}

		bool HasMemoryBootstrapFile(const std::filesystem::path& workspaceDir) {
			constexpr std::wstring_view kPrimaryMemoryFile = L"MEMORY.md";
			constexpr std::wstring_view kFallbackMemoryFile = L"memory.md";

			std::error_code ec;
			if (std::filesystem::is_regular_file(workspaceDir / kPrimaryMemoryFile, ec) &&
				!ec) {
				return true;
			}

			ec.clear();
			return std::filesystem::is_regular_file(workspaceDir / kFallbackMemoryFile, ec) &&
				!ec;
		}

		std::wstring ReadUtf8AsWide(const std::filesystem::path& filePath) {
			std::ifstream input(filePath, std::ios::binary);
			if (!input.is_open()) {
				return {};
			}

			const std::string bytes(
				(std::istreambuf_iterator<char>(input)),
				std::istreambuf_iterator<char>());
			if (bytes.empty()) {
				return {};
			}

			const int required = MultiByteToWideChar(
				CP_UTF8,
				0,
				bytes.c_str(),
				static_cast<int>(bytes.size()),
				nullptr,
				0);
			if (required <= 0) {
				return std::wstring(bytes.begin(), bytes.end());
			}

			std::wstring output(static_cast<std::size_t>(required), L'\0');
			const int converted = MultiByteToWideChar(
				CP_UTF8,
				0,
				bytes.c_str(),
				static_cast<int>(bytes.size()),
				output.data(),
				required);
			if (converted <= 0) {
				return std::wstring(bytes.begin(), bytes.end());
			}

			return output;
		}

	} // namespace

	bool AgentsWorkspaceService::IsOnboardingCompleted(
		const std::filesystem::path& workspaceDir) {
		const auto statePath = workspaceDir / kWorkspaceStatePath;
		const std::wstring payload = ReadUtf8AsWide(statePath);
		if (payload.empty()) {
			return false;
		}

		if (ContainsNonEmptyJsonStringField(payload, L"setupCompletedAt")) {
			return true;
		}

		return ContainsNonEmptyJsonStringField(payload, L"onboardingCompletedAt");
	}

	AgentsWorkspaceSnapshot AgentsWorkspaceService::BuildSnapshot(
		const AgentScopeSnapshot& scopeSnapshot) const {
		AgentsWorkspaceSnapshot snapshot;
		snapshot.entries.reserve(scopeSnapshot.entries.size());

		for (const auto& scopeEntry : scopeSnapshot.entries) {
			AgentWorkspaceEntry workspaceEntry;
			workspaceEntry.agentId = scopeEntry.id;
			workspaceEntry.workspaceDir = scopeEntry.workspaceDir;
			workspaceEntry.onboardingCompleted =
				IsOnboardingCompleted(scopeEntry.workspaceDir);

			for (const auto& bootstrapFile : kRequiredBootstrapFiles) {
				std::error_code ec;
				const auto filePath = scopeEntry.workspaceDir / bootstrapFile;
				if (!std::filesystem::is_regular_file(filePath, ec) || ec) {
					workspaceEntry.missingBootstrapFiles.push_back(bootstrapFile);
				}
			}

			if (!HasMemoryBootstrapFile(scopeEntry.workspaceDir)) {
				workspaceEntry.missingBootstrapFiles.push_back(L"MEMORY.md");
			}

			if (workspaceEntry.onboardingCompleted) {
				++snapshot.onboardingCompletedCount;
			}

			if (!workspaceEntry.onboardingCompleted &&
				workspaceEntry.missingBootstrapFiles.empty()) {
				snapshot.warnings.push_back(
					L"Agent workspace has bootstrap files but onboarding is incomplete: " +
					workspaceEntry.agentId);
			}

			snapshot.entries.push_back(std::move(workspaceEntry));
		}

		return snapshot;
	}

	bool AgentsWorkspaceService::ValidateFixtureScenarios(
		const std::filesystem::path& fixturesRoot,
		std::wstring& outError) const {
		outError.clear();

		AgentScopeSnapshot scope;
		scope.defaultAgentId = L"alpha";

		AgentScopeEntry alpha;
		alpha.id = L"alpha";
		alpha.workspaceDir = fixturesRoot / L"a1-workspace" / L"alpha";
		scope.entries.push_back(alpha);

		AgentScopeEntry beta;
		beta.id = L"beta";
		beta.workspaceDir = fixturesRoot / L"a1-workspace" / L"beta";
		scope.entries.push_back(beta);

		const auto snapshot = BuildSnapshot(scope);
		if (snapshot.entries.size() != 2) {
			outError = L"Fixture validation failed: expected two workspace entries.";
			return false;
		}

		const auto alphaIt = std::find_if(
			snapshot.entries.begin(),
			snapshot.entries.end(),
			[](const AgentWorkspaceEntry& entry) {
				return entry.agentId == L"alpha";
			});
		if (alphaIt == snapshot.entries.end() || !alphaIt->onboardingCompleted) {
			outError =
				L"Fixture validation failed: alpha should be onboarding-complete.";
			return false;
		}

		const auto betaIt = std::find_if(
			snapshot.entries.begin(),
			snapshot.entries.end(),
			[](const AgentWorkspaceEntry& entry) {
				return entry.agentId == L"beta";
			});
		if (betaIt == snapshot.entries.end()) {
			outError = L"Fixture validation failed: beta workspace entry missing.";
			return false;
		}

		if (betaIt->missingBootstrapFiles.empty()) {
			outError =
				L"Fixture validation failed: beta should report missing bootstrap files.";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core
