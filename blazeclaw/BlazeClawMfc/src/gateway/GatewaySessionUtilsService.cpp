#include "pch.h"
#include "GatewaySessionUtilsService.h"

#include "GatewayJsonSerializers.h"
#include "GatewayJsonUtils.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayPersistencePaths.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <wininet.h>

#pragma comment(lib, "wininet.lib")

namespace blazeclaw::gateway {
	namespace {
		std::string Trim(const std::string& value) {
			std::size_t start = 0;
			while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}

			std::size_t end = value.size();
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}

			return value.substr(start, end - start);
		}

		std::string ToLower(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		bool ContainsCaseInsensitive(
			const std::string& haystack,
			const std::string& needle) {
			if (needle.empty()) {
				return true;
			}
			return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
		}

		std::int64_t NowEpochMs() {
			const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now());
			return static_cast<std::int64_t>(now.time_since_epoch().count());
		}

		std::uint64_t EstimateTokenCountFromText(const std::string& text) {
			if (text.empty()) {
				return 0;
			}
			return static_cast<std::uint64_t>((std::max<std::size_t>)(1, text.size() / 4));
		}

		std::string NormalizeSessionKeyForFileName(const std::string& sessionKey) {
			const std::string source = sessionKey.empty() ? "main" : sessionKey;
			std::string normalized;
			normalized.reserve(source.size());
			for (const unsigned char ch : source) {
				const bool isSafe =
					(ch >= 'a' && ch <= 'z') ||
					(ch >= 'A' && ch <= 'Z') ||
					(ch >= '0' && ch <= '9') ||
					ch == '-' ||
					ch == '_' ||
					ch == '.';
				normalized.push_back(isSafe ? static_cast<char>(ch) : '_');
			}
			return normalized.empty() ? std::string("main") : normalized;
		}

		struct TranscriptSnapshot {
			bool found = false;
			std::uint64_t messageCount = 0;
			std::uint64_t inputTokens = 0;
			std::uint64_t outputTokens = 0;
			std::uint64_t totalTokens = 0;
			std::uint64_t lastActiveMs = 0;
			std::string firstUserMessage;
			std::string lastMessagePreview;
			std::string resolvedModelProvider;
			std::string resolvedModel;
			std::string overrideModelProvider;
			std::string overrideModel;
			std::uint64_t overrideContextTokens = 0;
			double overrideInputCostPer1k = 0.0;
			double overrideOutputCostPer1k = 0.0;
			bool hasOverrideContextTokens = false;
			bool hasOverrideInputCostPer1k = false;
			bool hasOverrideOutputCostPer1k = false;
		};

		struct SessionStoreEntryRecord {
			SessionEntry entry;
			std::filesystem::path storePath;
			std::uint64_t freshnessMs = 0;
		};

		struct ModelCatalogCostContext {
			std::string provider;
			std::string model;
			double inputCostPer1k = 0.0;
			double outputCostPer1k = 0.0;
			std::uint64_t contextTokens = 0;
			std::string source = "embedded-defaults";
		};

		struct RuntimeModelCatalogEntry {
			std::string provider;
			std::string model;
			double inputCostPer1k = 0.0;
			double outputCostPer1k = 0.0;
			std::uint64_t contextTokens = 0;
			bool isDefault = false;
			std::string source = "embedded-defaults";
		};

		struct ResolvedModelIdentity {
			std::string provider;
			std::string model;
			std::string source = "runtime-default";
			bool overrideApplied = false;
		};

		bool TryParseDouble(const std::string& value, double& outValue);
		bool TryParseUInt64(const std::string& value, std::uint64_t& outValue);

		TranscriptSnapshot ReadTranscriptSnapshot(const std::string& sessionId) {
			TranscriptSnapshot snapshot{};
			const std::filesystem::path transcriptPath =
				ResolveGatewayStateFilePath("chat-transcripts") /
				(NormalizeSessionKeyForFileName(sessionId) + ".jsonl");
			std::ifstream input(transcriptPath, std::ios::in | std::ios::binary);
			if (!input.is_open()) {
				return snapshot;
			}

			std::string line;
			while (std::getline(input, line)) {
				if (line.empty()) {
					continue;
				}
				snapshot.found = true;
				++snapshot.messageCount;
				std::string role;
				std::string text;
				std::string model;
				std::string modelProvider;
				json::FindStringField(line, "role", role);
				if (!json::FindStringField(line, "text", text)) {
					json::FindStringField(line, "message", text);
				}
				if (json::FindStringField(line, "model", model) && !model.empty()) {
					snapshot.resolvedModel = model;
				}
				if (json::FindStringField(line, "modelOverride", model) && !model.empty()) {
					snapshot.overrideModel = model;
				}
				std::string numericRaw;
				if (json::FindRawField(line, "contextTokensOverride", numericRaw)) {
					std::uint64_t parsedContext = 0;
					if (TryParseUInt64(Trim(numericRaw), parsedContext) && parsedContext > 0) {
						snapshot.overrideContextTokens = parsedContext;
						snapshot.hasOverrideContextTokens = true;
					}
				}
				if (json::FindRawField(line, "inputCostPer1kOverride", numericRaw)) {
					double parsedInput = 0.0;
					if (TryParseDouble(Trim(numericRaw), parsedInput) && parsedInput >= 0.0) {
						snapshot.overrideInputCostPer1k = parsedInput;
						snapshot.hasOverrideInputCostPer1k = true;
					}
				}
				if (json::FindRawField(line, "outputCostPer1kOverride", numericRaw)) {
					double parsedOutput = 0.0;
					if (TryParseDouble(Trim(numericRaw), parsedOutput) && parsedOutput >= 0.0) {
						snapshot.overrideOutputCostPer1k = parsedOutput;
						snapshot.hasOverrideOutputCostPer1k = true;
					}
				}
				if (json::FindStringField(line, "modelProvider", modelProvider) && !modelProvider.empty()) {
					snapshot.resolvedModelProvider = modelProvider;
				}
				if (json::FindStringField(line, "providerOverride", modelProvider) && !modelProvider.empty()) {
					snapshot.overrideModelProvider = modelProvider;
				}
				if (snapshot.resolvedModelProvider.empty() &&
					json::FindStringField(line, "provider", modelProvider) &&
					!modelProvider.empty()) {
					snapshot.resolvedModelProvider = modelProvider;
				}
				if (role == "user") {
					snapshot.inputTokens += EstimateTokenCountFromText(text);
					if (snapshot.firstUserMessage.empty() && !text.empty()) {
						snapshot.firstUserMessage = text;
					}
				}
				else {
					snapshot.outputTokens += EstimateTokenCountFromText(text);
				}
				if (!text.empty()) {
					snapshot.lastMessagePreview = text;
				}
				std::uint64_t timestamp = 0;
				if (json::FindUInt64Field(line, "timestamp", timestamp)) {
					snapshot.lastActiveMs = (std::max)(snapshot.lastActiveMs, timestamp);
				}
			}

			snapshot.totalTokens = snapshot.inputTokens + snapshot.outputTokens;
			if (snapshot.lastActiveMs == 0) {
				snapshot.lastActiveMs = static_cast<std::uint64_t>(NowEpochMs());
			}
			return snapshot;
		}

		std::vector<RuntimeModelCatalogEntry> BuildEmbeddedCatalogDefaults() {
			return {
				RuntimeModelCatalogEntry{
					GatewayModel::kSeedProviderId,
					GatewayModel::kDefaultModelId,
					0.00020,
					0.00060,
					128000,
					true
				},
				RuntimeModelCatalogEntry{
					GatewayModel::kSeedProviderId,
					GatewayModel::kReasonerModelId,
					0.00040,
					0.00120,
					64000,
					false
				},
				RuntimeModelCatalogEntry{
					GatewayModel::kDeepSeekProviderId,
					GatewayModel::kDeepSeekChatModelId,
					0.00014,
					0.00028,
					128000,
					false
				},
				RuntimeModelCatalogEntry{
					GatewayModel::kDeepSeekProviderId,
					GatewayModel::kDeepSeekReasonerModelId,
					0.00055,
					0.00219,
					64000,
					false
				},
			};
		}

		bool TryParseDouble(const std::string& value, double& outValue) {
			if (value.empty()) {
				return false;
			}
			try {
				std::size_t consumed = 0;
				const double parsed = std::stod(value, &consumed);
				if (consumed != value.size()) {
					return false;
				}
				outValue = parsed;
				return true;
			}
			catch (...) {
				return false;
			}
		}

		bool TryParseUInt64(const std::string& value, std::uint64_t& outValue) {
			if (value.empty()) {
				return false;
			}
			try {
				std::size_t consumed = 0;
				const std::uint64_t parsed = std::stoull(value, &consumed);
				if (consumed != value.size()) {
					return false;
				}
				outValue = parsed;
				return true;
			}
			catch (...) {
				return false;
			}
		}

		std::optional<RuntimeModelCatalogEntry> ParseCatalogLine(const std::string& line) {
			std::string provider;
			std::string model;
			if (!json::FindStringField(line, "provider", provider) ||
				!json::FindStringField(line, "model", model)) {
				return std::nullopt;
			}
			std::string inputRaw;
			std::string outputRaw;
			std::string contextRaw;
			json::FindRawField(line, "inputCostPer1k", inputRaw);
			json::FindRawField(line, "outputCostPer1k", outputRaw);
			json::FindRawField(line, "contextTokens", contextRaw);

			RuntimeModelCatalogEntry entry{};
			entry.provider = ToLower(provider);
			entry.model = GatewayModel::NormalizeModelId(model);
			double input = 0.0;
			double output = 0.0;
			std::uint64_t contextTokens = 0;
			if (TryParseDouble(Trim(inputRaw), input)) {
				entry.inputCostPer1k = input;
			}
			if (TryParseDouble(Trim(outputRaw), output)) {
				entry.outputCostPer1k = output;
			}
			if (TryParseUInt64(Trim(contextRaw), contextTokens)) {
				entry.contextTokens = contextTokens;
			}
			bool isDefault = false;
			if (json::FindBoolField(line, "default", isDefault)) {
				entry.isDefault = isDefault;
			}
			return entry;
		}

		std::string ResolveEnvPath(const char* key) {
			char* envPath = nullptr;
			std::size_t required = 0;
			const errno_t err = _dupenv_s(&envPath, &required, key);
			if (err == 0 && envPath != nullptr && required > 0) {
				std::string value(envPath);
				std::free(envPath);
				return Trim(value);
			}
			if (envPath != nullptr) {
				std::free(envPath);
			}
			return {};
		}

		std::filesystem::path ResolveRuntimeCatalogPath() {
			const std::string envPath = ResolveEnvPath("BLAZECLAW_MODEL_CATALOG_PATH");
			if (!envPath.empty()) {
				return std::filesystem::path(envPath);
			}
			return ResolveGatewayStateFilePath("model-catalog.state");
		}

		std::filesystem::path ResolveRuntimeCatalogOverridesPath() {
			const std::string envPath = ResolveEnvPath("BLAZECLAW_MODEL_CATALOG_OVERRIDES_PATH");
			if (!envPath.empty()) {
				return std::filesystem::path(envPath);
			}
			return ResolveGatewayStateFilePath("model-catalog.overrides.state");
		}

		std::filesystem::path ResolveExternalCatalogCachePath() {
			const std::string envPath = ResolveEnvPath("BLAZECLAW_MODEL_CATALOG_EXTERNAL_CACHE_PATH");
			if (!envPath.empty()) {
				return std::filesystem::path(envPath);
			}
			return ResolveGatewayStateFilePath("model-catalog.external.state");
		}

		std::string ResolveExternalCatalogServiceUrl() {
			return ResolveEnvPath("BLAZECLAW_MODEL_CATALOG_SERVICE_URL");
		}

		std::optional<std::string> TryFetchCatalogFromUrl(const std::string& url) {
			if (url.empty()) {
				return std::nullopt;
			}
			HINTERNET session = InternetOpenA(
				"BlazeClawModelCatalog/1.0",
				INTERNET_OPEN_TYPE_PRECONFIG,
				nullptr,
				nullptr,
				0);
			if (session == nullptr) {
				return std::nullopt;
			}
			HINTERNET request = InternetOpenUrlA(
				session,
				url.c_str(),
				nullptr,
				0,
				INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
				0);
			if (request == nullptr) {
				InternetCloseHandle(session);
				return std::nullopt;
			}

			std::string response;
			char buffer[4096];
			DWORD bytesRead = 0;
			while (InternetReadFile(request, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
				response.append(buffer, buffer + bytesRead);
				bytesRead = 0;
			}
			InternetCloseHandle(request);
			InternetCloseHandle(session);
			if (response.empty()) {
				return std::nullopt;
			}
			return response;
		}

		void MergeCatalogText(
			const std::string& catalogText,
			const std::string& source,
			std::unordered_map<std::string, RuntimeModelCatalogEntry>& deduped) {
			std::istringstream stream(catalogText);
			std::string line;
			while (std::getline(stream, line)) {
				const std::string trimmed = Trim(line);
				if (trimmed.empty()) {
					continue;
				}
				const auto parsed = ParseCatalogLine(trimmed);
				if (!parsed.has_value()) {
					continue;
				}
				RuntimeModelCatalogEntry entry = parsed.value();
				entry.source = source;
				const std::string key = entry.provider + "::" + entry.model;
				deduped.insert_or_assign(key, entry);
			}
		}

		void MergeCatalogFile(
			const std::filesystem::path& path,
			const std::string& source,
			std::unordered_map<std::string, RuntimeModelCatalogEntry>& deduped) {
			std::ifstream input(path, std::ios::in | std::ios::binary);
			if (!input.is_open()) {
				return;
			}
			std::string line;
			while (std::getline(input, line)) {
				const std::string trimmed = Trim(line);
				if (trimmed.empty()) {
					continue;
				}
				const auto parsed = ParseCatalogLine(trimmed);
				if (!parsed.has_value()) {
					continue;
				}
				RuntimeModelCatalogEntry entry = parsed.value();
				entry.source = source;
				const std::string key = entry.provider + "::" + entry.model;
				deduped.insert_or_assign(key, entry);
			}
		}

		std::vector<RuntimeModelCatalogEntry> LoadRuntimeModelCatalog() {
			std::unordered_map<std::string, RuntimeModelCatalogEntry> deduped;
			for (auto entry : BuildEmbeddedCatalogDefaults()) {
				entry.source = "embedded-defaults";
				const std::string key = entry.provider + "::" + entry.model;
				deduped.insert_or_assign(key, entry);
			}
			const std::string externalServiceUrl = ResolveExternalCatalogServiceUrl();
			bool loadedFromExternalService = false;
			if (!externalServiceUrl.empty()) {
				const auto external = TryFetchCatalogFromUrl(externalServiceUrl);
				if (external.has_value()) {
					MergeCatalogText(external.value(), "external-service", deduped);
					loadedFromExternalService = true;
					const std::filesystem::path cachePath = ResolveExternalCatalogCachePath();
					std::ofstream cache(cachePath, std::ios::out | std::ios::trunc | std::ios::binary);
					if (cache.is_open()) {
						cache << external.value();
					}
				}
			}
			if (!loadedFromExternalService) {
				MergeCatalogFile(ResolveExternalCatalogCachePath(), "external-cache", deduped);
			}

			MergeCatalogFile(ResolveRuntimeCatalogPath(), "runtime-state", deduped);
			MergeCatalogFile(ResolveRuntimeCatalogOverridesPath(), "runtime-overrides", deduped);

			std::vector<RuntimeModelCatalogEntry> merged;
			merged.reserve(deduped.size());
			for (const auto& [_, entry] : deduped) {
				merged.push_back(entry);
			}
			return merged;
		}

		std::optional<RuntimeModelCatalogEntry> FindCatalogEntry(
			const std::vector<RuntimeModelCatalogEntry>& catalog,
			const std::string& provider,
			const std::string& model) {
			const std::string normalizedProvider = ToLower(provider);
			const std::string normalizedModel = GatewayModel::NormalizeModelId(model);
			for (const auto& entry : catalog) {
				if (entry.provider == normalizedProvider && entry.model == normalizedModel) {
					return entry;
				}
			}
			return std::nullopt;
		}

		std::optional<RuntimeModelCatalogEntry> ResolveCatalogDefaultEntry(
			const std::vector<RuntimeModelCatalogEntry>& catalog) {
			for (const auto& entry : catalog) {
				if (entry.isDefault) {
					return entry;
				}
			}
			return std::nullopt;
		}

		ModelCatalogCostContext ResolveCatalogCostContext(
			const std::vector<RuntimeModelCatalogEntry>& catalog,
			const std::string& provider,
			const std::string& model) {
			const std::string normalizedModel = GatewayModel::NormalizeModelId(model);
			const std::string normalizedProvider = provider.empty()
				? GatewayModel::ResolveModelProvider(normalizedModel)
				: ToLower(provider);
			const auto hit = FindCatalogEntry(catalog, normalizedProvider, normalizedModel);
			if (hit.has_value()) {
				return ModelCatalogCostContext{
					hit->provider,
					hit->model,
					hit->inputCostPer1k,
					hit->outputCostPer1k,
					hit->contextTokens,
					hit->source,
				};
			}
			return ModelCatalogCostContext{
				GatewayModel::kSeedProviderId,
				GatewayModel::kDefaultModelId,
				0.00020,
				0.00060,
				128000,
				"embedded-defaults"
			};
		}

		ModelCatalogCostContext ApplyTranscriptOverrides(
			const ModelCatalogCostContext& base,
			const TranscriptSnapshot& transcript,
			bool& pricingOverrideApplied) {
			ModelCatalogCostContext effective = base;
			pricingOverrideApplied = false;
			if (transcript.hasOverrideContextTokens && transcript.overrideContextTokens > 0) {
				effective.contextTokens = transcript.overrideContextTokens;
				effective.source = "transcript-override";
			}
			if (transcript.hasOverrideInputCostPer1k) {
				effective.inputCostPer1k = transcript.overrideInputCostPer1k;
				effective.source = "transcript-override";
				pricingOverrideApplied = true;
			}
			if (transcript.hasOverrideOutputCostPer1k) {
				effective.outputCostPer1k = transcript.overrideOutputCostPer1k;
				effective.source = "transcript-override";
				pricingOverrideApplied = true;
			}
			return effective;
		}

		double ResolveEstimatedCostUsd(
			const ModelCatalogCostContext& context,
			const std::uint64_t inputTokens,
			const std::uint64_t outputTokens) {
			return (static_cast<double>(inputTokens) / 1000.0 * context.inputCostPer1k) +
				(static_cast<double>(outputTokens) / 1000.0 * context.outputCostPer1k);
		}

		std::string ResolveDerivedTitle(
			const std::string& sessionId,
			const TranscriptSnapshot& snapshot) {
			if (!snapshot.firstUserMessage.empty()) {
				std::string title = snapshot.firstUserMessage;
				if (title.size() > 60) {
					title = title.substr(0, 60);
				}
				return title;
			}
			return "Session " + sessionId;
		}

		std::vector<std::filesystem::path> EnumerateSessionStorePaths() {
			std::vector<std::filesystem::path> paths;
			const std::filesystem::path stateRoot = ResolveGatewayStateDirectory();
			const std::filesystem::path defaultStore = stateRoot / "sessions.state";
			paths.push_back(defaultStore);

			const std::filesystem::path agentsDir = stateRoot / "agents";
			std::error_code ec;
			if (!std::filesystem::exists(agentsDir, ec) || ec) {
				return paths;
			}

			for (const auto& entry : std::filesystem::directory_iterator(agentsDir, ec)) {
				if (ec || !entry.is_directory()) {
					continue;
				}
				paths.push_back(entry.path() / "sessions.state");
			}
			return paths;
		}

		std::vector<SessionStoreEntryRecord> ReadSessionStoreRecords(
			const std::filesystem::path& path) {
			std::vector<SessionStoreEntryRecord> records;
			std::ifstream input(path);
			if (!input.is_open()) {
				return records;
			}

			std::string line;
			while (std::getline(input, line)) {
				if (line.empty()) {
					continue;
				}

				std::istringstream row(line);
				std::string id;
				std::string scope;
				std::string active;
				if (!std::getline(row, id, '|') ||
					!std::getline(row, scope, '|') ||
					!std::getline(row, active)) {
					continue;
				}

				SessionEntry parsed{};
				parsed.id = GatewaySessionUtilsService::CanonicalizeSessionId(id);
				parsed.scope = scope.empty()
					? (parsed.id == "main" ? "default" : "thread")
					: scope;
				parsed.active = active == "1" || active == "true";
				const TranscriptSnapshot transcript = ReadTranscriptSnapshot(parsed.id);
				records.push_back(SessionStoreEntryRecord{
					.entry = parsed,
					.storePath = path,
					.freshnessMs = transcript.lastActiveMs,
				});
			}
			return records;
		}

		bool IsFresherRecord(
			const SessionStoreEntryRecord& candidate,
			const SessionStoreEntryRecord& current) {
			if (candidate.freshnessMs != current.freshnessMs) {
				return candidate.freshnessMs > current.freshnessMs;
			}
			if (candidate.entry.active != current.entry.active) {
				return candidate.entry.active && !current.entry.active;
			}
			const std::string candidatePath = candidate.storePath.string();
			const std::string currentPath = current.storePath.string();
			if (candidatePath != currentPath) {
				return candidatePath < currentPath;
			}
			return candidate.entry.id < current.entry.id;
		}

		ResolvedModelIdentity ResolveSessionModelIdentity(
			const TranscriptSnapshot& transcript,
			const std::vector<RuntimeModelCatalogEntry>& catalog,
			const std::string& defaultModelProvider,
			const std::string& defaultModel) {
			// Precedence:
			// 1) transcript explicit overrides
			// 2) transcript runtime identity
			// 3) runtime/default model argument from host
			// 4) catalog default model/provider
			// 5) framework default model/provider
			ResolvedModelIdentity resolved{};
			std::string model = !transcript.overrideModel.empty()
				? transcript.overrideModel
				: (transcript.resolvedModel.empty() ? defaultModel : transcript.resolvedModel);
			if (!transcript.overrideModel.empty()) {
				resolved.source = "transcript-override";
				resolved.overrideApplied = true;
			}
			else if (!transcript.resolvedModel.empty()) {
				resolved.source = "transcript-runtime";
			}
			else if (!defaultModel.empty()) {
				resolved.source = "runtime-default";
			}
			if (model.empty()) {
				const auto catalogDefault = ResolveCatalogDefaultEntry(catalog);
				model = catalogDefault.has_value()
					? catalogDefault->model
					: std::string(GatewayModel::kDefaultModelId);
				if (catalogDefault.has_value()) {
					resolved.source = "catalog-default";
				}
				else {
					resolved.source = "framework-default";
				}
			}

			std::string provider = !transcript.overrideModelProvider.empty()
				? transcript.overrideModelProvider
				: transcript.resolvedModelProvider;
			if (provider.empty()) {
				provider = defaultModelProvider;
			}
			if (provider.empty()) {
				const auto catalogHit = FindCatalogEntry(catalog, GatewayModel::ResolveModelProvider(model), model);
				if (catalogHit.has_value()) {
					provider = catalogHit->provider;
				}
			}
			if (provider.empty()) {
				provider = GatewayModel::ResolveModelProvider(model);
			}

			model = GatewayModel::NormalizeModelId(model);
			if (provider.empty()) {
				provider = GatewayModel::ResolveModelProvider(model);
			}
			resolved.provider = provider;
			resolved.model = model;
			return resolved;
		}
	} // namespace

	std::vector<SessionEntry> GatewaySessionUtilsService::ListMergedSessions() {
		std::unordered_map<std::string, SessionStoreEntryRecord> merged;
		for (const auto& path : EnumerateSessionStorePaths()) {
			for (const auto& record : ReadSessionStoreRecords(path)) {
				const std::string key = CanonicalizeSessionId(record.entry.id);
				const auto it = merged.find(key);
				if (it == merged.end() || IsFresherRecord(record, it->second)) {
					merged.insert_or_assign(key, record);
				}
			}
		}

		std::vector<SessionEntry> sessions;
		sessions.reserve(merged.size());
		for (const auto& [_, record] : merged) {
			sessions.push_back(record.entry);
		}
		std::sort(
			sessions.begin(),
			sessions.end(),
			[](const SessionEntry& left, const SessionEntry& right) {
				return left.id < right.id;
			});
		return sessions;
	}

	std::optional<SessionEntry> GatewaySessionUtilsService::ResolveFreshestSessionAcrossStores(
		const std::string& requestedId) {
		const std::string canonicalRequested = CanonicalizeSessionId(requestedId);
		std::optional<SessionStoreEntryRecord> chosen;
		for (const auto& path : EnumerateSessionStorePaths()) {
			for (const auto& record : ReadSessionStoreRecords(path)) {
				if (CanonicalizeSessionId(record.entry.id) != canonicalRequested) {
					continue;
				}
				if (!chosen.has_value() || IsFresherRecord(record, *chosen)) {
					chosen = record;
				}
			}
		}
		if (!chosen.has_value()) {
			return std::nullopt;
		}
		return chosen->entry;
	}

	std::string GatewaySessionUtilsService::CanonicalizeSessionId(const std::string& value) {
		const std::string trimmed = Trim(value);
		if (trimmed.empty()) {
			return "main";
		}
		return ToLower(trimmed);
	}

	std::vector<std::string> GatewaySessionUtilsService::FindSessionIdsIgnoreCase(
		const std::vector<SessionEntry>& sessions,
		const std::string& targetId) {
		const std::string loweredTarget = ToLower(CanonicalizeSessionId(targetId));
		std::vector<std::string> matches;
		for (const auto& session : sessions) {
			if (ToLower(CanonicalizeSessionId(session.id)) == loweredTarget) {
				matches.push_back(session.id);
			}
		}
		return matches;
	}

	std::optional<SessionEntry> GatewaySessionUtilsService::ResolveFreshestSessionMatch(
		const std::vector<SessionEntry>& sessions,
		const std::vector<std::string>& candidateIds) {
		std::optional<SessionEntry> chosen;
		std::unordered_set<std::string> normalizedCandidates;
		for (const auto& candidate : candidateIds) {
			normalizedCandidates.insert(ToLower(CanonicalizeSessionId(candidate)));
		}
		if (normalizedCandidates.empty()) {
			return std::nullopt;
		}

		for (const auto& session : sessions) {
			const std::string normalized = ToLower(CanonicalizeSessionId(session.id));
			if (!normalizedCandidates.contains(normalized)) {
				continue;
			}
			if (!chosen.has_value()) {
				chosen = session;
				continue;
			}
			if (session.active && !chosen->active) {
				chosen = session;
				continue;
			}
			// SessionEntry currently does not persist updatedAt, so we keep stable deterministic
			// selection by preferring active sessions and then lexical id ordering.
			if (session.id < chosen->id) {
				chosen = session;
			}
		}
		return chosen;
	}

	GatewaySessionProjection GatewaySessionUtilsService::BuildSessionProjection(
		const SessionEntry& session,
		const std::string& defaultModelProvider,
		const std::string& defaultModel) {
		GatewaySessionProjection projection{};
		const auto catalog = LoadRuntimeModelCatalog();
		const TranscriptSnapshot transcript = ReadTranscriptSnapshot(session.id);
		const ResolvedModelIdentity identity = ResolveSessionModelIdentity(
			transcript,
			catalog,
			defaultModelProvider,
			defaultModel);
		const ModelCatalogCostContext catalogContextBase = ResolveCatalogCostContext(
			catalog,
			identity.provider,
			identity.model);
		bool pricingOverrideApplied = false;
		const ModelCatalogCostContext catalogContext = ApplyTranscriptOverrides(
			catalogContextBase,
			transcript,
			pricingOverrideApplied);
		projection.id = session.id;
		projection.key = session.id;
		projection.scope = session.scope;
		projection.active = session.active;
		projection.kind = (session.id == "global" || session.id == "unknown") ? session.id : "direct";
		projection.label = session.id;
		projection.displayName = "Session " + session.id;
		projection.derivedTitle = ResolveDerivedTitle(session.id, transcript);
		projection.lastMessagePreview = transcript.lastMessagePreview;
		projection.modelProvider = identity.provider;
		projection.model = identity.model;
		projection.contextTokens = catalogContext.contextTokens;
		projection.totalTokens = transcript.totalTokens;
		projection.totalTokensFresh = transcript.found && transcript.totalTokens > 0;
		projection.estimatedCostUsd = ResolveEstimatedCostUsd(
			catalogContext,
			transcript.inputTokens,
			transcript.outputTokens);
		projection.modelSource = identity.source;
		projection.catalogSource = catalogContext.source;
		projection.modelOverrideApplied = identity.overrideApplied;
		projection.pricingOverrideApplied = pricingOverrideApplied;
		projection.fallbackSource = transcript.found ? "transcript" : "registry";
		projection.updatedAt = transcript.found
			? static_cast<std::int64_t>(transcript.lastActiveMs)
			: NowEpochMs();
		return projection;
	}

	std::string GatewaySessionUtilsService::BuildSessionProjectionJson(const GatewaySessionProjection& projection) {
		std::ostringstream out;
		out << "{"
			<< "\"id\":\"" << EscapeJsonString(projection.id) << "\","
			<< "\"key\":\"" << EscapeJsonString(projection.key) << "\","
			<< "\"scope\":\"" << EscapeJsonString(projection.scope) << "\","
			<< "\"active\":" << (projection.active ? "true" : "false") << ","
			<< "\"kind\":\"" << EscapeJsonString(projection.kind) << "\","
			<< "\"label\":\"" << EscapeJsonString(projection.label) << "\","
			<< "\"displayName\":\"" << EscapeJsonString(projection.displayName) << "\","
			<< "\"updatedAt\":" << projection.updatedAt;

		if (!projection.derivedTitle.empty()) {
			out << ",\"derivedTitle\":\"" << EscapeJsonString(projection.derivedTitle) << "\"";
		}
		if (!projection.lastMessagePreview.empty()) {
			out << ",\"lastMessagePreview\":\"" << EscapeJsonString(projection.lastMessagePreview) << "\"";
		}
		out << ",\"modelProvider\":\"" << EscapeJsonString(projection.modelProvider) << "\""
			<< ",\"model\":\"" << EscapeJsonString(projection.model) << "\""
			<< ",\"modelSource\":\"" << EscapeJsonString(projection.modelSource) << "\""
			<< ",\"catalogSource\":\"" << EscapeJsonString(projection.catalogSource) << "\""
			<< ",\"modelOverrideApplied\":" << (projection.modelOverrideApplied ? "true" : "false")
			<< ",\"pricingOverrideApplied\":" << (projection.pricingOverrideApplied ? "true" : "false")
			<< ",\"contextTokens\":" << projection.contextTokens
			<< ",\"totalTokens\":" << projection.totalTokens
			<< ",\"totalTokensFresh\":" << (projection.totalTokensFresh ? "true" : "false")
			<< ",\"estimatedCostUsd\":" << projection.estimatedCostUsd
			<< ",\"fallbackSource\":\"" << EscapeJsonString(projection.fallbackSource) << "\"";
		if (!projection.spawnedBy.empty()) {
			out << ",\"spawnedBy\":\"" << EscapeJsonString(projection.spawnedBy) << "\"";
		}
		if (!projection.parentSessionKey.empty()) {
			out << ",\"parentSessionKey\":\"" << EscapeJsonString(projection.parentSessionKey) << "\"";
		}
		if (!projection.childSessions.empty()) {
			out << ",\"childSessions\":" << SerializeStringArray(projection.childSessions);
		}

		out << "}";
		return out.str();
	}

	std::string GatewaySessionUtilsService::BuildSessionListPayload(
		const std::vector<SessionEntry>& sessions,
		const GatewaySessionListFilters& filters,
		const std::string& defaultModelProvider,
		const std::string& defaultModel) {
		const std::int64_t nowMs = NowEpochMs();
		const std::int64_t activeCutoffMs = filters.activeMinutes.has_value()
			? nowMs - static_cast<std::int64_t>(filters.activeMinutes.value()) * 60 * 1000
			: (std::numeric_limits<std::int64_t>::min)();
		std::vector<GatewaySessionProjection> projected;
		projected.reserve(sessions.size());
		for (const auto& session : sessions) {
			const std::string canonicalId = CanonicalizeSessionId(session.id);
			if (!filters.includeGlobal && canonicalId == "global") {
				continue;
			}
			if (!filters.includeUnknown && canonicalId == "unknown") {
				continue;
			}
			if (filters.active.has_value() && session.active != filters.active.value()) {
				continue;
			}
			if (!filters.scope.empty() && session.scope != filters.scope) {
				continue;
			}
			if (!filters.label.empty() && !ContainsCaseInsensitive(session.id, filters.label)) {
				continue;
			}
			if (!filters.spawnedBy.empty() && !ContainsCaseInsensitive(session.id, filters.spawnedBy)) {
				continue;
			}
			if (!filters.agentId.empty()) {
				const std::string expectedAgentPrefix = "agent:" + ToLower(filters.agentId) + ":";
				if (ToLower(canonicalId).find(expectedAgentPrefix) != 0) {
					continue;
				}
			}
			if (!filters.search.empty() &&
				!ContainsCaseInsensitive(session.id, filters.search) &&
				!ContainsCaseInsensitive(session.scope, filters.search)) {
				continue;
			}

			GatewaySessionProjection row = BuildSessionProjection(
				session,
				defaultModelProvider,
				defaultModel);
			if (!filters.includeDerivedTitles) {
				row.derivedTitle.clear();
			}
			if (!filters.includeLastMessage) {
				row.lastMessagePreview.clear();
			}
			if (row.updatedAt < activeCutoffMs) {
				continue;
			}
			projected.push_back(std::move(row));
		}

		std::sort(
			projected.begin(),
			projected.end(),
			[](const GatewaySessionProjection& left, const GatewaySessionProjection& right) {
				if (left.updatedAt != right.updatedAt) {
					return left.updatedAt > right.updatedAt;
				}
				return left.id < right.id;
			});

		if (filters.limit.has_value()) {
			const std::size_t capped = (std::max<std::size_t>)(1, filters.limit.value());
			if (projected.size() > capped) {
				projected.resize(capped);
			}
		}

		std::ostringstream sessionsJson;
		sessionsJson << "[";
		bool first = true;
		std::string activeSessionId = "none";
		for (const auto& row : projected) {
			if (!first) {
				sessionsJson << ",";
			}
			first = false;
			sessionsJson << BuildSessionProjectionJson(row);
			if (activeSessionId == "none" && row.active) {
				activeSessionId = row.id;
			}
		}
		sessionsJson << "]";

		std::ostringstream payload;
		payload
			<< "{"
			<< "\"sessions\":" << sessionsJson.str() << ","
			<< "\"count\":" << projected.size() << ","
			<< "\"activeSessionId\":\"" << EscapeJsonString(activeSessionId) << "\""
			<< "}";
		return payload.str();
	}

	GatewaySessionPreviewPayload GatewaySessionUtilsService::BuildSessionPreviewPayload(
		const SessionEntry& session,
		const std::string& defaultModelProvider,
		const std::string& defaultModel) {
		const TranscriptSnapshot transcript = ReadTranscriptSnapshot(session.id);
		const std::string title = ResolveDerivedTitle(session.id, transcript);
		const auto catalog = LoadRuntimeModelCatalog();
		const ResolvedModelIdentity identity = ResolveSessionModelIdentity(
			transcript,
			catalog,
			defaultModelProvider,
			defaultModel);
		const ModelCatalogCostContext catalogContextBase = ResolveCatalogCostContext(
			catalog,
			identity.provider,
			identity.model);
		bool pricingOverrideApplied = false;
		const ModelCatalogCostContext catalogContext = ApplyTranscriptOverrides(
			catalogContextBase,
			transcript,
			pricingOverrideApplied);
		std::ostringstream payload;
		payload
			<< "{"
			<< "\"session\":" << SerializeSession(session) << ","
			<< "\"title\":\"" << EscapeJsonString(title) << "\","
			<< "\"lastMessagePreview\":\"" << EscapeJsonString(transcript.lastMessagePreview) << "\","
			<< "\"hasMessages\":" << (transcript.messageCount > 0 ? "true" : "false") << ","
			<< "\"unread\":0,"
			<< "\"usage\":{\"messages\":" << transcript.messageCount
			<< ",\"input\":" << transcript.inputTokens
			<< ",\"output\":" << transcript.outputTokens
			<< ",\"totalTokens\":" << transcript.totalTokens
			<< ",\"totalTokensFresh\":" << ((transcript.found && transcript.totalTokens > 0) ? "true" : "false")
			<< "},"
			<< "\"modelProvider\":\"" << EscapeJsonString(identity.provider) << "\","
			<< "\"model\":\"" << EscapeJsonString(identity.model) << "\","
			<< "\"modelSource\":\"" << EscapeJsonString(identity.source) << "\","
			<< "\"catalogSource\":\"" << EscapeJsonString(catalogContext.source) << "\","
			<< "\"modelOverrideApplied\":" << (identity.overrideApplied ? "true" : "false") << ","
			<< "\"pricingOverrideApplied\":" << (pricingOverrideApplied ? "true" : "false") << ","
			<< "\"contextTokens\":" << catalogContext.contextTokens << ","
			<< "\"fallbackSource\":\"" << (transcript.found ? "transcript" : "registry") << "\""
			<< "}";
		return GatewaySessionPreviewPayload{
			payload.str(),
			transcript.found ? "transcript" : "registry",
			transcript.found && transcript.totalTokens > 0
		};
	}

	GatewaySessionUsagePayload GatewaySessionUtilsService::BuildSessionUsagePayload(
		const SessionEntry& session,
		const std::string& defaultModelProvider,
		const std::string& defaultModel,
		const std::string& startDate,
		const std::string& endDate,
		bool openClawEnvelope) {
		const TranscriptSnapshot transcript = ReadTranscriptSnapshot(session.id);
		const auto catalog = LoadRuntimeModelCatalog();
		const ResolvedModelIdentity identity = ResolveSessionModelIdentity(
			transcript,
			catalog,
			defaultModelProvider,
			defaultModel);
		const ModelCatalogCostContext catalogContextBase = ResolveCatalogCostContext(
			catalog,
			identity.provider,
			identity.model);
		bool pricingOverrideApplied = false;
		const ModelCatalogCostContext catalogContext = ApplyTranscriptOverrides(
			catalogContextBase,
			transcript,
			pricingOverrideApplied);
		const double totalCost = ResolveEstimatedCostUsd(
			catalogContext,
			transcript.inputTokens,
			transcript.outputTokens);
		const std::uint64_t lastActiveMs = transcript.lastActiveMs > 0
			? transcript.lastActiveMs
			: static_cast<std::uint64_t>(NowEpochMs());

		std::ostringstream payload;
		if (!openClawEnvelope) {
			payload
				<< "{"
				<< "\"sessionId\":\"" << EscapeJsonString(session.id) << "\","
				<< "\"messages\":" << transcript.messageCount << ","
				<< "\"tokens\":{\"input\":" << transcript.inputTokens
				<< ",\"output\":" << transcript.outputTokens
				<< ",\"total\":" << transcript.totalTokens
				<< ",\"totalTokensFresh\":" << ((transcript.found && transcript.totalTokens > 0) ? "true" : "false")
				<< "},"
				<< "\"modelProvider\":\"" << EscapeJsonString(identity.provider) << "\","
				<< "\"model\":\"" << EscapeJsonString(identity.model) << "\","
				<< "\"modelSource\":\"" << EscapeJsonString(identity.source) << "\","
				<< "\"catalogSource\":\"" << EscapeJsonString(catalogContext.source) << "\","
				<< "\"modelOverrideApplied\":" << (identity.overrideApplied ? "true" : "false") << ","
				<< "\"pricingOverrideApplied\":" << (pricingOverrideApplied ? "true" : "false") << ","
				<< "\"estimatedCostUsd\":" << totalCost << ","
				<< "\"contextTokens\":" << catalogContext.contextTokens << ","
				<< "\"fallbackSource\":\"" << (transcript.found ? "transcript" : "registry") << "\","
				<< "\"lastActiveMs\":" << lastActiveMs
				<< "}";
			return GatewaySessionUsagePayload{
				payload.str(),
				transcript.found ? "transcript" : "registry",
				transcript.found && transcript.totalTokens > 0
			};
		}

		const std::string usageStart = startDate.empty() ? "2026-01-01" : startDate;
		const std::string usageEnd = endDate.empty() ? usageStart : endDate;
		payload
			<< "{"
			<< "\"updatedAt\":" << lastActiveMs << ","
			<< "\"startDate\":\"" << EscapeJsonString(usageStart) << "\","
			<< "\"endDate\":\"" << EscapeJsonString(usageEnd) << "\","
			<< "\"sessions\":[{"
			<< "\"key\":\"" << EscapeJsonString(session.id) << "\","
			<< "\"label\":\"Session " << EscapeJsonString(session.id) << "\","
			<< "\"sessionId\":\"" << EscapeJsonString(session.id) << "\","
			<< "\"updatedAt\":" << lastActiveMs << ","
			<< "\"modelProvider\":\"" << EscapeJsonString(identity.provider) << "\","
			<< "\"model\":\"" << EscapeJsonString(identity.model) << "\","
			<< "\"modelSource\":\"" << EscapeJsonString(identity.source) << "\","
			<< "\"catalogSource\":\"" << EscapeJsonString(catalogContext.source) << "\","
			<< "\"modelOverrideApplied\":" << (identity.overrideApplied ? "true" : "false") << ","
			<< "\"pricingOverrideApplied\":" << (pricingOverrideApplied ? "true" : "false") << ","
			<< "\"contextTokens\":" << catalogContext.contextTokens << ","
			<< "\"fallbackSource\":\"" << (transcript.found ? "transcript" : "registry") << "\","
			<< "\"usage\":{\"input\":" << transcript.inputTokens
			<< ",\"output\":" << transcript.outputTokens
			<< ",\"cacheRead\":0,\"cacheWrite\":0"
			<< ",\"totalTokens\":" << transcript.totalTokens
			<< ",\"totalTokensFresh\":" << ((transcript.found && transcript.totalTokens > 0) ? "true" : "false")
			<< ",\"totalCost\":" << totalCost
			<< ",\"messages\":" << transcript.messageCount << "}}],"
			<< "\"totals\":{\"input\":" << transcript.inputTokens
			<< ",\"output\":" << transcript.outputTokens
			<< ",\"cacheRead\":0,\"cacheWrite\":0"
			<< ",\"totalTokens\":" << transcript.totalTokens
			<< ",\"totalCost\":" << totalCost
			<< ",\"inputCost\":" << totalCost * 0.33
			<< ",\"outputCost\":" << totalCost * 0.67
			<< ",\"cacheReadCost\":0.0,\"cacheWriteCost\":0.0,\"missingCostEntries\":0}"
			<< "}";
		return GatewaySessionUsagePayload{
			payload.str(),
			transcript.found ? "transcript" : "registry",
			transcript.found && transcript.totalTokens > 0
		};
	}

} // namespace blazeclaw::gateway
