#include "pch.h"
#include "ConfigSchemaService.h"

#include "../gateway/GatewayJsonUtils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <numeric>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

namespace blazeclaw::core {

	namespace {

		using Json = nlohmann::json;

		constexpr std::array<const char*, 3> kForbiddenLookupSegments = {
			"__proto__",
			"prototype",
			"constructor",
		};

		constexpr std::array<const char*, 22> kKnownChannelIds = {
			"internal",
			"whatsapp",
			"telegram",
			"slack",
			"discord",
			"msteams",
			"googlechat",
			"matrix",
			"line",
			"feishu",
			"mattermost",
			"nextcloudtalk",
			"signal",
			"irc",
			"nostr",
			"bluebubbles",
			"synologychat",
			"webchat",
			"tlon",
			"twitch",
			"zalo",
			"teams",
		};

		std::string ToLowerCopy(const std::string& value) {
			std::string lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered;
		}

		std::string EscapeJsonString(const std::string& value) {
			std::string escaped;
			escaped.reserve(value.size() + 8);
			for (const char ch : value) {
				switch (ch) {
				case '"':
					escaped += "\\\"";
					break;
				case '\\':
					escaped += "\\\\";
					break;
				case '\n':
					escaped += "\\n";
					break;
				case '\r':
					escaped += "\\r";
					break;
				case '\t':
					escaped += "\\t";
					break;
				default:
					escaped.push_back(ch);
					break;
				}
			}
			return escaped;
		}

		bool ContainsAnySubstring(
			const std::string& loweredValue,
			std::initializer_list<const char*> fragments) {
			for (const auto* fragment : fragments) {
				if (fragment == nullptr || *fragment == '\0') {
					continue;
				}

				if (loweredValue.find(fragment) != std::string::npos) {
					return true;
				}
			}

			return false;
		}

		bool IsSensitiveHintPath(const std::string& path) {
			const std::string lowered = ToLowerCopy(path);
			return ContainsAnySubstring(
				lowered,
				{ "token", "apikey", "api_key", "secret", "password", "credential" });
		}

		bool IsSensitiveUrlHintPath(const std::string& path) {
			const std::string lowered = ToLowerCopy(path);
			const bool looksLikeUrlField =
				ContainsAnySubstring(lowered, { "url", "uri", "endpoint", "webhook" });
			if (!looksLikeUrlField) {
				return false;
			}

			return ContainsAnySubstring(
				lowered,
				{ "token", "key", "secret", "password", "credential", "auth" });
		}

		Json BuildHintTags(
			const bool sensitive,
			const bool sensitiveUrl) {
			Json tags = Json::array();
			tags.push_back("config");
			tags.push_back("skill");

			if (sensitive) {
				tags.push_back("sensitive");
			}

			if (sensitiveUrl) {
				tags.push_back("sensitive-url");
			}

			return tags;
		}

		std::uint64_t Fnv1a64(
			const std::uint64_t seed,
			const std::string& value) {
			std::uint64_t hash = seed;
			for (const unsigned char ch : value) {
				hash ^= static_cast<std::uint64_t>(ch);
				hash *= 1099511628211ull;
			}

			return hash;
		}

		std::string ToAsciiLowerTrimmed(const std::wstring& input) {
			std::string output;
			output.reserve(input.size());
			for (const wchar_t ch : input) {
				if (ch <= 0x7F) {
					output.push_back(static_cast<char>(ch));
				}
			}

			return ToLowerCopy(blazeclaw::gateway::json::Trim(output));
		}

		std::string BuildIso8601NowUtc() {
			const auto now = std::chrono::system_clock::now();
			const auto asTime = std::chrono::system_clock::to_time_t(now);
			std::tm utc{};
			gmtime_s(&utc, &asTime);
			std::ostringstream out;
			out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
			return out.str();
		}

		bool IsForbiddenSegment(const std::string& segment) {
			for (const auto* forbidden : kForbiddenLookupSegments) {
				if (segment == forbidden) {
					return true;
				}
			}
			return false;
		}

		std::string NormalizeLookupPath(const std::string& rawPath) {
			std::string trimmed = blazeclaw::gateway::json::Trim(rawPath);
			if (trimmed.empty()) {
				return {};
			}

			std::string normalized;
			normalized.reserve(trimmed.size() + 8);
			for (std::size_t i = 0; i < trimmed.size(); ++i) {
				const char ch = trimmed[i];
				if (ch == '[') {
					normalized.push_back('.');
					std::size_t end = trimmed.find(']', i + 1);
					if (end == std::string::npos) {
						return {};
					}
					const std::string inside = trimmed.substr(i + 1, end - i - 1);
					normalized += inside.empty() ? "*" : inside;
					i = end;
					continue;
				}
				normalized.push_back(ch);
			}

			while (!normalized.empty() && normalized.front() == '.') {
				normalized.erase(normalized.begin());
			}
			while (!normalized.empty() && normalized.back() == '.') {
				normalized.pop_back();
			}

			std::string collapsed;
			collapsed.reserve(normalized.size());
			bool previousDot = false;
			for (const char ch : normalized) {
				if (ch == '.') {
					if (!previousDot) {
						collapsed.push_back(ch);
					}
					previousDot = true;
					continue;
				}
				collapsed.push_back(ch);
				previousDot = false;
			}

			return collapsed;
		}

		std::vector<std::string> SplitLookupPath(const std::string& rawPath) {
			const std::string normalized = NormalizeLookupPath(rawPath);
			if (normalized.empty()) {
				return {};
			}

			std::vector<std::string> parts;
			std::size_t start = 0;
			while (start <= normalized.size()) {
				const std::size_t next = normalized.find('.', start);
				const std::string part =
					next == std::string::npos
					? normalized.substr(start)
					: normalized.substr(start, next - start);
				if (!part.empty()) {
					parts.push_back(part);
				}
				if (next == std::string::npos) {
					break;
				}
				start = next + 1;
			}
			return parts;
		}

		Json* EnsureObjectNode(Json& parent, const std::string& key) {
			if (!parent.is_object()) {
				return nullptr;
			}

			if (!parent.contains("properties") || !parent["properties"].is_object()) {
				parent["properties"] = Json::object();
			}

			Json& properties = parent["properties"];
			if (!properties.contains(key) || !properties[key].is_object()) {
				properties[key] = Json{
					{ "type", "object" },
					{ "properties", Json::object() },
				};
			}

			return &properties[key];
		}

		Json* EnsureWildcardObjectNode(Json& parent) {
			if (!parent.is_object()) {
				return nullptr;
			}

			if (!parent.contains("additionalProperties") ||
				!parent["additionalProperties"].is_object()) {
				parent["additionalProperties"] = Json{
					{ "type", "object" },
					{ "properties", Json::object() },
				};
			}

			return &parent["additionalProperties"];
		}

		void EnsurePathProjectedSchemaNode(
			Json& root,
			const std::string& rawPath) {
			const auto parts = SplitLookupPath(rawPath);
			if (parts.empty()) {
				return;
			}

			Json* current = &root;
			for (std::size_t index = 0; index < parts.size(); ++index) {
				const bool isLast = index + 1 == parts.size();
				const std::string& segment = parts[index];
				if (segment.empty() || IsForbiddenSegment(segment)) {
					return;
				}

				if (segment == "*") {
					current = EnsureWildcardObjectNode(*current);
				}
				else {
					current = EnsureObjectNode(*current, segment);
				}

				if (current == nullptr) {
					return;
				}

				if (isLast) {
					(*current)["type"] = "string";
					if (!current->contains("minLength")) {
						(*current)["minLength"] = 1;
					}
				}
			}
		}

		std::vector<std::string> ListKnownChannels(
			const blazeclaw::config::AppConfig& config) {
			std::set<std::string> seen;
			std::vector<std::string> ordered;

			for (const auto* channel : kKnownChannelIds) {
				if (channel == nullptr || *channel == '\0') {
					continue;
				}

				const std::string normalized = ToLowerCopy(channel);
				if (seen.insert(normalized).second) {
					ordered.push_back(normalized);
				}
			}

			for (const auto& channel : config.enabledChannels) {
				const std::string normalized = ToAsciiLowerTrimmed(channel);
				if (normalized.empty()) {
					continue;
				}

				if (seen.insert(normalized).second) {
					ordered.push_back(normalized);
				}
			}

			return ordered;
		}


		Json BuildBaseSchemaJson(
			const blazeclaw::config::AppConfig& config,
			const blazeclaw::gateway::SkillsCatalogGatewayState& skillsState) {
			Json root = {
				{ "type", "object" },
				{ "properties", Json::object() },
			};

			Json channelsNode = {
				{ "type", "object" },
				{ "properties", Json::object() },
				{ "additionalProperties", Json{ { "type", "object" } } },
			};

			for (const auto& channel : config.enabledChannels) {
				std::string channelId;
				channelId.reserve(channel.size());
				for (const wchar_t ch : channel) {
					if (ch <= 0x7F) {
						channelId.push_back(static_cast<char>(ch));
					}
				}
				channelId = ToLowerCopy(blazeclaw::gateway::json::Trim(channelId));
				if (channelId.empty()) {
					continue;
				}
				channelsNode["properties"][channelId] = Json{
					{ "type", "object" },
					{ "properties", Json::object() },
				};
			}

			Json pluginsNode = {
				{ "type", "object" },
				{ "properties", Json::object() },
			};
			pluginsNode["properties"]["entries"] = Json{
				{ "type", "object" },
				{ "additionalProperties", Json{ { "type", "object" } } },
			};

			Json agentsNode = {
				{ "type", "object" },
				{ "properties", Json::object() },
			};
			agentsNode["properties"]["defaults"] = Json{
				{ "type", "object" },
				{ "properties", Json::object() },
			};
			agentsNode["properties"]["list"] = Json{
				{ "type", "array" },
				{ "items", Json{ { "type", "object" } } },
			};

			root["properties"]["channels"] = std::move(channelsNode);
			root["properties"]["plugins"] = std::move(pluginsNode);
			root["properties"]["agents"] = std::move(agentsNode);

			for (const auto& skill : skillsState.entries) {
				for (const auto& path : skill.requiresConfig) {
					EnsurePathProjectedSchemaNode(root, path);
				}
				for (const auto& path : skill.configPathHints) {
					EnsurePathProjectedSchemaNode(root, path);
				}
			}

			return root;
		}

		Json BuildUiHintsJson(
			const blazeclaw::config::AppConfig& config,
			const blazeclaw::gateway::SkillsCatalogGatewayState& skillsState) {
			Json hints = Json::object();
			for (const auto& skill : skillsState.entries) {
				std::vector<std::string> hintPaths = skill.requiresConfig;
				hintPaths.insert(
					hintPaths.end(),
					skill.configPathHints.begin(),
					skill.configPathHints.end());

				for (const auto& path : hintPaths) {
					const std::string normalized = blazeclaw::gateway::json::Trim(path);
					if (normalized.empty()) {
						continue;
					}

					Json hint = Json::object();
					hint["label"] = normalized;
					hint["help"] =
						"Config path required by skill " + skill.skillKey + ".";

					const bool sensitive = IsSensitiveHintPath(normalized);
					const bool sensitiveUrl = IsSensitiveUrlHintPath(normalized);
					if (sensitive || sensitiveUrl) {
						hint["sensitive"] = true;
					}

					hint["tags"] = BuildHintTags(sensitive || sensitiveUrl, sensitiveUrl);
					if (sensitiveUrl) {
						hint["placeholder"] = "https://***:***@example.com/path";
					}

					hints[normalized] = std::move(hint);
				}
			}

			const auto channels = ListKnownChannels(config);
			const std::string knownChannels = channels.empty()
				? std::string()
				: std::string(" Known channels: ") +
				std::accumulate(
					std::next(channels.begin()),
					channels.end(),
					channels.front(),
					[](const std::string& left, const std::string& right) {
						return left + ", " + right;
					}) +
				".";
					const std::string heartbeatHelp =
						"Delivery target (\"last\", \"none\", or a channel id)." +
						knownChannels;
					for (const auto* heartbeatPath : {
						"agents.defaults.heartbeat.target",
						"agents.list.*.heartbeat.target",
						}) {
						if (heartbeatPath == nullptr) {
							continue;
						}

						Json hint = hints.contains(heartbeatPath) && hints[heartbeatPath].is_object()
							? hints[heartbeatPath]
							: Json::object();
						if (!hint.contains("help") || !hint["help"].is_string()) {
							hint["help"] = heartbeatHelp;
						}
						if (!hint.contains("placeholder") || !hint["placeholder"].is_string()) {
							hint["placeholder"] = "last";
						}
						hints[heartbeatPath] = std::move(hint);
					}

					return hints;
		}

		Json StripSchemaForLookup(const Json& schemaNode) {
			Json stripped = Json::object();
			if (!schemaNode.is_object()) {
				return stripped;
			}

			static constexpr std::array<const char*, 19> kAllowedKeys = {
				"type",
				"$id",
				"$schema",
				"title",
				"description",
				"format",
				"pattern",
				"contentEncoding",
				"contentMediaType",
				"minimum",
				"maximum",
				"exclusiveMinimum",
				"exclusiveMaximum",
				"multipleOf",
				"minLength",
				"maxLength",
				"minItems",
				"maxItems",
				"enum",
			};

			for (const auto* key : kAllowedKeys) {
				const auto it = schemaNode.find(key);
				if (it != schemaNode.end()) {
					stripped[key] = *it;
				}
			}

			const auto constIt = schemaNode.find("const");
			if (constIt != schemaNode.end()) {
				stripped["const"] = *constIt;
			}

			return stripped;
		}

		std::optional<blazeclaw::config::ConfigUiHintModel> TryParseHint(
			const Json& hintJson) {
			if (!hintJson.is_object()) {
				return std::nullopt;
			}

			blazeclaw::config::ConfigUiHintModel hint;
			if (const auto it = hintJson.find("label");
				it != hintJson.end() && it->is_string()) {
				hint.label = it->get<std::string>();
			}
			if (const auto it = hintJson.find("help");
				it != hintJson.end() && it->is_string()) {
				hint.help = it->get<std::string>();
			}
			if (const auto it = hintJson.find("tags");
				it != hintJson.end() && it->is_array()) {
				for (const auto& entry : *it) {
					if (entry.is_string()) {
						hint.tags.push_back(entry.get<std::string>());
					}
				}
			}
			if (const auto it = hintJson.find("advanced");
				it != hintJson.end() && it->is_boolean()) {
				hint.advanced = it->get<bool>();
			}
			if (const auto it = hintJson.find("sensitive");
				it != hintJson.end() && it->is_boolean()) {
				hint.sensitive = it->get<bool>();
			}
			if (const auto it = hintJson.find("placeholder");
				it != hintJson.end() && it->is_string()) {
				hint.placeholder = it->get<std::string>();
			}
			return hint;
		}

		std::optional<std::pair<std::string, blazeclaw::config::ConfigUiHintModel>>
			ResolveHintMatch(const Json& hintsJson, const std::string& path) {
			if (!hintsJson.is_object() || path.empty()) {
				return std::nullopt;
			}

			const auto exactIt = hintsJson.find(path);
			if (exactIt != hintsJson.end()) {
				const auto parsed = TryParseHint(*exactIt);
				if (parsed.has_value()) {
					return std::make_pair(path, parsed.value());
				}
			}

			const auto parts = SplitLookupPath(path);
			for (const auto& [key, value] : hintsJson.items()) {
				const auto hintParts = SplitLookupPath(key);
				if (hintParts.size() != parts.size()) {
					continue;
				}

				bool matched = true;
				for (std::size_t index = 0; index < parts.size(); ++index) {
					if (hintParts[index] != "*" && hintParts[index] != parts[index]) {
						matched = false;
						break;
					}
				}

				if (!matched) {
					continue;
				}

				const auto parsed = TryParseHint(value);
				if (parsed.has_value()) {
					return std::make_pair(key, parsed.value());
				}
			}

			return std::nullopt;
		}

	} // namespace

	void ConfigSchemaService::Invalidate() {
		std::scoped_lock lock(m_cacheMutex);
		m_cacheEntries.clear();
	}

	std::string ConfigSchemaService::BuildCacheKey(
		const blazeclaw::config::AppConfig& config,
		const blazeclaw::gateway::SkillsCatalogGatewayState& skillsState) {
		std::vector<std::string> normalizedChannels;
		normalizedChannels.reserve(config.enabledChannels.size());
		for (const auto& channel : config.enabledChannels) {
			const std::string normalized = ToAsciiLowerTrimmed(channel);
			if (!normalized.empty()) {
				normalizedChannels.push_back(normalized);
			}
		}
		std::sort(normalizedChannels.begin(), normalizedChannels.end());

		std::vector<std::string> normalizedSkillFragments;
		normalizedSkillFragments.reserve(skillsState.entries.size());
		for (const auto& entry : skillsState.entries) {
			std::vector<std::string> requiresConfig = entry.requiresConfig;
			std::vector<std::string> configPathHints = entry.configPathHints;
			std::sort(requiresConfig.begin(), requiresConfig.end());
			std::sort(configPathHints.begin(), configPathHints.end());

			std::ostringstream fragment;
			fragment << entry.skillKey << "|";
			for (const auto& path : requiresConfig) {
				fragment << "r:" << path << ";";
			}
			for (const auto& hint : configPathHints) {
				fragment << "h:" << hint << ";";
			}
			normalizedSkillFragments.push_back(fragment.str());
		}
		std::sort(normalizedSkillFragments.begin(), normalizedSkillFragments.end());

		std::uint64_t hash = 1469598103934665603ull;
		hash = Fnv1a64(hash, "config-schema-cache-v2");
		hash = Fnv1a64(hash, std::to_string(skillsState.snapshotVersion));
		for (const auto& channel : normalizedChannels) {
			hash = Fnv1a64(hash, channel);
		}
		for (const auto& fragment : normalizedSkillFragments) {
			hash = Fnv1a64(hash, fragment);
		}

		std::ostringstream key;
		key << "v2-" << std::hex << hash;
		return key.str();
	}

	blazeclaw::gateway::ConfigSchemaGatewayState ConfigSchemaService::BuildGatewayState(
		const blazeclaw::config::AppConfig& config,
		const blazeclaw::gateway::SkillsCatalogGatewayState& skillsState) const {
		const std::string key = BuildCacheKey(config, skillsState);
		{
			std::scoped_lock lock(m_cacheMutex);
			for (const auto& entry : m_cacheEntries) {
				if (entry.key == key) {
					return entry.state;
				}
			}
		}

		const Json schema = BuildBaseSchemaJson(config, skillsState);
		const Json hints = BuildUiHintsJson(config, skillsState);

		blazeclaw::gateway::ConfigSchemaGatewayState next;
		next.schemaJson = schema.dump();
		next.uiHintsJson = hints.dump();
		next.version = "schema-v1";
		next.generatedAt = BuildIso8601NowUtc();

		{
			std::scoped_lock lock(m_cacheMutex);
			m_cacheEntries.push_back(CacheEntry{
				 .key = key,
				 .state = next,
				});
			while (m_cacheEntries.size() >
				blazeclaw::config::kConfigSchemaMergeCacheMax) {
				m_cacheEntries.pop_front();
			}
		}

		return next;
	}

	std::optional<blazeclaw::gateway::ConfigSchemaGatewayLookupResult> ConfigSchemaService::Lookup(
		const blazeclaw::gateway::ConfigSchemaGatewayState& state,
		const std::string& path) const {
		const std::string normalizedPath = NormalizeLookupPath(path);
		if (normalizedPath.empty()) {
			return std::nullopt;
		}

		auto parts = SplitLookupPath(normalizedPath);
		if (parts.empty() ||
			parts.size() > blazeclaw::config::kConfigSchemaLookupMaxPathSegments) {
			return std::nullopt;
		}

		for (const auto& part : parts) {
			if (IsForbiddenSegment(part)) {
				return std::nullopt;
			}
		}

		Json root;
		Json hintsRoot;
		try {
			root = Json::parse(state.schemaJson.empty() ? "{}" : state.schemaJson);
			hintsRoot = Json::parse(state.uiHintsJson.empty() ? "{}" : state.uiHintsJson);
		}
		catch (...) {
			return std::nullopt;
		}

		if (!root.is_object()) {
			return std::nullopt;
		}

		const Json* current = &root;
		for (const auto& part : parts) {
			const auto propertiesIt = current->find("properties");
			if (propertiesIt != current->end() && propertiesIt->is_object()) {
				const auto childIt = propertiesIt->find(part);
				if (childIt != propertiesIt->end() && childIt->is_object()) {
					current = &(*childIt);
					continue;
				}
			}

			const auto additionalIt = current->find("additionalProperties");
			if (additionalIt != current->end() && additionalIt->is_object()) {
				current = &(*additionalIt);
				continue;
			}

			const auto itemsIt = current->find("items");
			if (itemsIt != current->end() && itemsIt->is_object()) {
				current = &(*itemsIt);
				continue;
			}

			return std::nullopt;
		}

		blazeclaw::gateway::ConfigSchemaGatewayLookupResult result;
		result.path = normalizedPath;
		result.schemaJson = StripSchemaForLookup(*current).dump();

		if (const auto hintMatch = ResolveHintMatch(hintsRoot, normalizedPath);
			hintMatch.has_value()) {
			result.hintPath = hintMatch->first;
			result.hint = hintMatch->second;
		}

		const auto propertiesIt = current->find("properties");
		std::set<std::string> required;
		if (const auto requiredIt = current->find("required");
			requiredIt != current->end() && requiredIt->is_array()) {
			for (const auto& name : *requiredIt) {
				if (name.is_string()) {
					required.insert(name.get<std::string>());
				}
			}
		}

		if (propertiesIt != current->end() && propertiesIt->is_object()) {
			for (const auto& [key, child] : propertiesIt->items()) {
				if (!child.is_object()) {
					continue;
				}
				blazeclaw::gateway::ConfigSchemaGatewayChild childEntry;
				childEntry.key = key;
				childEntry.path = normalizedPath + "." + key;
				if (const auto typeIt = child.find("type");
					typeIt != child.end() && typeIt->is_string()) {
					childEntry.type = typeIt->get<std::string>();
				}
				childEntry.required = required.contains(key);
				childEntry.hasChildren =
					child.contains("properties") ||
					child.contains("additionalProperties") ||
					child.contains("items");

				if (const auto hintMatch = ResolveHintMatch(hintsRoot, childEntry.path);
					hintMatch.has_value()) {
					childEntry.hintPath = hintMatch->first;
					childEntry.hint = hintMatch->second;
				}

				result.children.push_back(std::move(childEntry));
			}
		}

		const auto additionalIt = current->find("additionalProperties");
		if (additionalIt != current->end() && additionalIt->is_object()) {
			blazeclaw::gateway::ConfigSchemaGatewayChild wildcard;
			wildcard.key = "*";
			wildcard.path = normalizedPath + ".*";
			if (const auto typeIt = additionalIt->find("type");
				typeIt != additionalIt->end() && typeIt->is_string()) {
				wildcard.type = typeIt->get<std::string>();
			}
			wildcard.required = false;
			wildcard.hasChildren =
				additionalIt->contains("properties") ||
				additionalIt->contains("additionalProperties") ||
				additionalIt->contains("items");
			if (const auto hintMatch = ResolveHintMatch(hintsRoot, wildcard.path);
				hintMatch.has_value()) {
				wildcard.hintPath = hintMatch->first;
				wildcard.hint = hintMatch->second;
			}
			result.children.push_back(std::move(wildcard));
		}

		return result;
	}

} // namespace blazeclaw::core
