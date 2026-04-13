#include "pch.h"
#include "AgentsWorkspaceService.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>

namespace blazeclaw::core {

	namespace {

		constexpr std::wstring_view kWorkspaceStatePath =
			L".openclaw/workspace-state.json";
		constexpr std::wstring_view kExtraBootstrapEnvKey =
			L"BLAZECLAW_AGENT_EXTRA_BOOTSTRAP_FILES";

		const std::vector<std::wstring> kRequiredBootstrapFiles = {
			L"AGENTS.md",
			L"SOUL.md",
			L"TOOLS.md",
			L"IDENTITY.md",
			L"USER.md",
			L"HEARTBEAT.md",
			L"BOOTSTRAP.md",
		};

		const std::set<std::wstring> kValidBootstrapNames = {
			L"AGENTS.md",
			L"SOUL.md",
			L"TOOLS.md",
			L"IDENTITY.md",
			L"USER.md",
			L"HEARTBEAT.md",
			L"BOOTSTRAP.md",
			L"MEMORY.md",
			L"memory.md",
		};

		struct WorkspaceStateInfo {
			bool bootstrapSeededAt = false;
			bool setupCompletedAt = false;
			bool onboardingCompletedAt = false;
		};

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

			const std::wstring output(value);
			free(value);
			return output;
		}

		std::vector<std::wstring> ParsePathList(const std::wstring& raw) {
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

		WorkspaceStateInfo ReadWorkspaceStateInfo(
			const std::filesystem::path& workspaceDir,
			std::wstring* outPayload = nullptr) {
			const auto statePath = workspaceDir / kWorkspaceStatePath;
			const std::wstring payload = ReadUtf8AsWide(statePath);
			if (outPayload != nullptr) {
				*outPayload = payload;
			}

			if (payload.empty()) {
				return WorkspaceStateInfo{};
			}

			return WorkspaceStateInfo{
				.bootstrapSeededAt =
					ContainsNonEmptyJsonStringField(payload, L"bootstrapSeededAt"),
				.setupCompletedAt =
					ContainsNonEmptyJsonStringField(payload, L"setupCompletedAt"),
				.onboardingCompletedAt =
					ContainsNonEmptyJsonStringField(payload, L"onboardingCompletedAt"),
			};
		}

		bool IsPathInside(
			const std::filesystem::path& rootPath,
			const std::filesystem::path& candidatePath) {
			std::error_code rootEc;
			std::error_code candidateEc;
			const auto canonicalRoot =
				std::filesystem::weakly_canonical(rootPath, rootEc);
			const auto canonicalCandidate =
				std::filesystem::weakly_canonical(candidatePath, candidateEc);
			if (rootEc || candidateEc) {
				return false;
			}

			auto rootIt = canonicalRoot.begin();
			auto candidateIt = canonicalCandidate.begin();
			for (; rootIt != canonicalRoot.end(); ++rootIt, ++candidateIt) {
				if (candidateIt == canonicalCandidate.end() || *rootIt != *candidateIt) {
					return false;
				}
			}

			return true;
		}

		std::vector<AgentWorkspaceExtraBootstrapDiagnostic> BuildExtraBootstrapDiagnostics(
			const std::filesystem::path& workspaceDir) {
			const auto patternsRaw = ReadEnvVar(kExtraBootstrapEnvKey.data());
			if (!patternsRaw.has_value()) {
				return {};
			}

			std::vector<AgentWorkspaceExtraBootstrapDiagnostic> diagnostics;
			for (const auto& pattern : ParsePathList(patternsRaw.value())) {
				std::filesystem::path relativePattern(pattern);
				const auto baseName = relativePattern.filename().wstring();
				const auto absolutePath = relativePattern.is_absolute()
					? relativePattern
					: (workspaceDir / relativePattern);

				if (kValidBootstrapNames.find(baseName) == kValidBootstrapNames.end()) {
					diagnostics.push_back(AgentWorkspaceExtraBootstrapDiagnostic{
						.path = absolutePath,
						.reason = L"invalid-bootstrap-filename",
						.detail = L"unsupported bootstrap basename: " + baseName,
						});
					continue;
				}

				if (!IsPathInside(workspaceDir, absolutePath)) {
					diagnostics.push_back(AgentWorkspaceExtraBootstrapDiagnostic{
						.path = absolutePath,
						.reason = L"security",
						.detail = L"path resolves outside workspace root",
						});
					continue;
				}

				std::error_code ec;
				if (!std::filesystem::exists(absolutePath, ec) || ec) {
					diagnostics.push_back(AgentWorkspaceExtraBootstrapDiagnostic{
						.path = absolutePath,
						.reason = L"missing",
						.detail = L"bootstrap path missing",
						});
					continue;
				}

				std::ifstream input(absolutePath, std::ios::binary);
				if (!input.is_open()) {
					diagnostics.push_back(AgentWorkspaceExtraBootstrapDiagnostic{
						.path = absolutePath,
						.reason = L"io",
						.detail = L"unable to read bootstrap path",
						});
				}
			}

			return diagnostics;
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


	} // namespace

	bool AgentsWorkspaceService::IsOnboardingCompleted(
		const std::filesystem::path& workspaceDir) {
		const auto state = ReadWorkspaceStateInfo(workspaceDir);
		return state.setupCompletedAt || state.onboardingCompletedAt;
	}

	AgentsWorkspaceSnapshot AgentsWorkspaceService::BuildSnapshot(
		const AgentScopeSnapshot& scopeSnapshot) const {
		AgentsWorkspaceSnapshot snapshot;
		snapshot.entries.reserve(scopeSnapshot.entries.size());

		for (const auto& scopeEntry : scopeSnapshot.entries) {
			AgentWorkspaceEntry workspaceEntry;
			workspaceEntry.agentId = scopeEntry.id;
			workspaceEntry.workspaceDir = scopeEntry.workspaceDir;

			const WorkspaceStateInfo state = ReadWorkspaceStateInfo(scopeEntry.workspaceDir);
			std::error_code bootstrapEc;
			const bool bootstrapFilePresent = std::filesystem::is_regular_file(
				scopeEntry.workspaceDir / L"BOOTSTRAP.md",
				bootstrapEc) && !bootstrapEc;

			workspaceEntry.lifecycle.bootstrapFilePresent = bootstrapFilePresent;
			workspaceEntry.lifecycle.setupCompleted =
				state.setupCompletedAt || state.onboardingCompletedAt;
			workspaceEntry.lifecycle.legacyOnboardingField =
				state.onboardingCompletedAt && !state.setupCompletedAt;
			workspaceEntry.lifecycle.bootstrapSeeded =
				state.bootstrapSeededAt || bootstrapFilePresent;
			workspaceEntry.lifecycle.bootstrapPending =
				workspaceEntry.lifecycle.bootstrapSeeded &&
				!workspaceEntry.lifecycle.setupCompleted &&
				bootstrapFilePresent;
			workspaceEntry.onboardingCompleted = workspaceEntry.lifecycle.setupCompleted;

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
			if (workspaceEntry.lifecycle.bootstrapSeeded) {
				++snapshot.bootstrapSeededCount;
			}
			if (workspaceEntry.lifecycle.bootstrapPending) {
				++snapshot.bootstrapPendingCount;
			}
			if (workspaceEntry.lifecycle.legacyOnboardingField) {
				++snapshot.legacyOnboardingFieldCount;
			}

			workspaceEntry.extraBootstrapDiagnostics = BuildExtraBootstrapDiagnostics(
				scopeEntry.workspaceDir);
			for (const auto& diagnostic : workspaceEntry.extraBootstrapDiagnostics) {
				++snapshot.extraBootstrapDiagnosticCount;
				if (diagnostic.reason == L"invalid-bootstrap-filename") {
					++snapshot.extraBootstrapInvalidFilenameCount;
				}
				else if (diagnostic.reason == L"missing") {
					++snapshot.extraBootstrapMissingCount;
				}
				else if (diagnostic.reason == L"security") {
					++snapshot.extraBootstrapSecurityCount;
				}
				else if (diagnostic.reason == L"io") {
					++snapshot.extraBootstrapIoCount;
				}
			}

			if (!workspaceEntry.onboardingCompleted &&
				workspaceEntry.missingBootstrapFiles.empty()) {
				snapshot.warnings.push_back(
					L"Agent workspace has bootstrap files but onboarding is incomplete: " +
					workspaceEntry.agentId);
			}

			if (workspaceEntry.lifecycle.bootstrapPending) {
				snapshot.warnings.push_back(
					L"Agent workspace bootstrap is seeded but setup is not completed: " +
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

		AgentScopeEntry gamma;
		gamma.id = L"gamma";
		gamma.workspaceDir = fixturesRoot / L"a1-workspace" / L"gamma";
		scope.entries.push_back(gamma);

		_wputenv_s(
			kExtraBootstrapEnvKey.data(),
			L"AGENTS.md;missing/MEMORY.md;../outside/TOOLS.md;unsupported.txt");

		const auto snapshot = BuildSnapshot(scope);
		_wputenv_s(kExtraBootstrapEnvKey.data(), L"");
		if (snapshot.entries.size() != 3) {
			outError = L"Fixture validation failed: expected three workspace entries.";
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

		const auto gammaIt = std::find_if(
			snapshot.entries.begin(),
			snapshot.entries.end(),
			[](const AgentWorkspaceEntry& entry) {
				return entry.agentId == L"gamma";
			});
		if (gammaIt == snapshot.entries.end() || !gammaIt->onboardingCompleted) {
			outError =
				L"Fixture validation failed: gamma should be setup-complete via setupCompletedAt.";
			return false;
		}

		const auto hasMemoryMissing = std::find(
			gammaIt->missingBootstrapFiles.begin(),
			gammaIt->missingBootstrapFiles.end(),
			L"MEMORY.md");
		if (hasMemoryMissing != gammaIt->missingBootstrapFiles.end()) {
			outError =
				L"Fixture validation failed: gamma lowercase memory.md should satisfy memory bootstrap requirement.";
			return false;
		}

		if (snapshot.extraBootstrapInvalidFilenameCount == 0 ||
			snapshot.extraBootstrapMissingCount == 0 ||
			snapshot.extraBootstrapSecurityCount == 0) {
			outError =
				L"Fixture validation failed: expected extra bootstrap diagnostics for invalid filename, missing path, and security path escape.";
			return false;
		}

		return true;
	}

} // namespace blazeclaw::core
