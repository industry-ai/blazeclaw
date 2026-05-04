constexpr std::size_t kMaxChatHistoryEntriesPerSession = 500;
constexpr std::array<const char*, 3> kConfigSchemaForbiddenSegments = {
	"__proto__",
	"prototype",
	"constructor",
};

std::string SerializeStringArrayLocal(
	const std::vector<std::string>& values);

std::string EscapeJsonLocal(const std::string& value) {
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

std::string NormalizeJsonRawForPayload(
	const std::string& raw,
	const char* fallbackRaw) {
	try {
		const auto parsed = nlohmann::json::parse(raw);
		return parsed.dump();
	}
	catch (...) {
		return fallbackRaw == nullptr ? "{}" : std::string(fallbackRaw);
	}
}

std::string SerializeConfigUiHintLocal(
	const blazeclaw::config::ConfigUiHintModel& hint) {
	std::string tagsJson = "[";
	for (std::size_t index = 0; index < hint.tags.size(); ++index) {
		if (index > 0) {
			tagsJson += ",";
		}

		tagsJson += "\"" + EscapeJsonLocal(hint.tags[index]) + "\"";
	}
	tagsJson += "]";

	return
		"{\"label\":\"" +
		EscapeJsonLocal(hint.label) +
		"\",\"help\":\"" +
		EscapeJsonLocal(hint.help) +
		"\",\"tags\":" +
		tagsJson +
		",\"advanced\":" +
		std::string(hint.advanced ? "true" : "false") +
		",\"sensitive\":" +
		std::string(hint.sensitive ? "true" : "false") +
		",\"placeholder\":\"" +
		EscapeJsonLocal(hint.placeholder) +
		"\"}";
}

std::string SerializeConfigSchemaChildLocal(
	const ConfigSchemaGatewayChild& child) {
	std::string hintJson = "null";
	if (child.hint.has_value()) {
		hintJson = SerializeConfigUiHintLocal(child.hint.value());
	}

	return
		"{\"key\":\"" +
		EscapeJsonLocal(child.key) +
		"\",\"path\":\"" +
		EscapeJsonLocal(child.path) +
		"\",\"type\":\"" +
		EscapeJsonLocal(child.type) +
		"\",\"required\":" +
		std::string(child.required ? "true" : "false") +
		",\"hasChildren\":" +
		std::string(child.hasChildren ? "true" : "false") +
		",\"hint\":" +
		hintJson +
		",\"hintPath\":\"" +
		EscapeJsonLocal(child.hintPath) +
		"\"}";
}

std::string SerializeConfigSchemaLookupResultLocal(
	const ConfigSchemaGatewayLookupResult& result) {
	std::string childrenJson = "[";
	for (std::size_t index = 0; index < result.children.size(); ++index) {
		if (index > 0) {
			childrenJson += ",";
		}

		childrenJson += SerializeConfigSchemaChildLocal(result.children[index]);
	}
	childrenJson += "]";

	std::string hintJson = "null";
	if (result.hint.has_value()) {
		hintJson = SerializeConfigUiHintLocal(result.hint.value());
	}

	return
		"{\"path\":\"" +
		EscapeJsonLocal(result.path) +
		"\",\"schema\":" +
		NormalizeJsonRawForPayload(result.schemaJson, "{}") +
		",\"hint\":" +
		hintJson +
		",\"hintPath\":\"" +
		EscapeJsonLocal(result.hintPath) +
		"\",\"children\":" +
		childrenJson +
		"}";
}

std::optional<std::string> NormalizeSchemaLookupPathRequest(
	const std::string& rawPath) {
	std::string trimmed = json::Trim(rawPath);
	if (trimmed.empty()) {
		return std::nullopt;
	}

	std::string normalized;
	normalized.reserve(trimmed.size() + 8);
	for (std::size_t index = 0; index < trimmed.size(); ++index) {
		const char ch = trimmed[index];
		if (ch == '[') {
			normalized.push_back('.');
			const std::size_t end = trimmed.find(']', index + 1);
			if (end == std::string::npos) {
				return std::nullopt;
			}

			const std::string inside =
				trimmed.substr(index + 1, end - index - 1);
			normalized += inside.empty() ? "*" : inside;
			index = end;
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

	if (collapsed.empty()) {
		return std::nullopt;
	}

	std::size_t segmentCount = 0;
	std::size_t start = 0;
	while (start <= collapsed.size()) {
		const auto next = collapsed.find('.', start);
		const std::string segment =
			next == std::string::npos
			? collapsed.substr(start)
			: collapsed.substr(start, next - start);

		if (!segment.empty()) {
			if (std::any_of(
				kConfigSchemaForbiddenSegments.begin(),
				kConfigSchemaForbiddenSegments.end(),
				[&segment](const char* forbidden) {
					return forbidden != nullptr && segment == forbidden;
				})) {
				return std::nullopt;
			}

			++segmentCount;
		}

		if (next == std::string::npos) {
			break;
		}

		start = next + 1;
	}

	if (segmentCount == 0 ||
		segmentCount > blazeclaw::config::kConfigSchemaLookupMaxPathSegments) {
		return std::nullopt;
	}

	return collapsed;
}

std::string ExtractStringParam(
	const std::optional<std::string>& paramsJson,
	const std::string& fieldName) {
	if (!paramsJson.has_value()) {
		return {};
	}

	std::string value;
	if (!json::FindStringField(paramsJson.value(), fieldName, value)) {
		return {};
	}

	return value;
}

std::string NormalizeSearchQueryTextLocal(const std::string& input) {
	std::string normalized;
	normalized.reserve(input.size());

	bool previousWasSpace = true;
	for (const unsigned char rawCh : input) {
		if (rawCh < 0x20) {
			continue;
		}

		if (std::isspace(rawCh) != 0) {
			if (!previousWasSpace) {
				normalized.push_back(' ');
				previousWasSpace = true;
			}
			continue;
		}

		normalized.push_back(static_cast<char>(rawCh));
		previousWasSpace = false;
	}

	while (!normalized.empty() && normalized.front() == ' ') {
		normalized.erase(normalized.begin());
	}
	while (!normalized.empty() && normalized.back() == ' ') {
		normalized.pop_back();
	}

	return normalized;
}

std::optional<std::string> DeriveCompactSearchQueryLocal(
	const std::string& source) {
	constexpr std::size_t kMaxQueryChars = 240;
	std::string normalized = NormalizeSearchQueryTextLocal(source);
	if (normalized.empty()) {
		return std::nullopt;
	}

	if (normalized.size() <= kMaxQueryChars) {
		return normalized;
	}

	std::string compact = normalized.substr(0, kMaxQueryChars);
	const auto lastSpace = compact.find_last_of(' ');
	if (lastSpace != std::string::npos && lastSpace > 40) {
		compact = compact.substr(0, lastSpace);
	}

	compact = NormalizeSearchQueryTextLocal(compact);
	if (compact.empty()) {
		return std::nullopt;
	}

	return compact;
}

std::string SerializePluginRuntimeSubagentModeLocal(
	const PluginRuntimeSubagentMode mode) {
	switch (mode) {
	case PluginRuntimeSubagentMode::Explicit:
		return "explicit";
	case PluginRuntimeSubagentMode::GatewayBindable:
		return "gateway-bindable";
	case PluginRuntimeSubagentMode::Default:
	default:
		return "default";
	}
}

std::string SerializePluginRuntimeCapabilitiesJsonLocal(
	const std::vector<PluginRuntimeCapabilityContract>& contracts) {
	std::string capabilitiesJson = "[";
	for (std::size_t index = 0; index < contracts.size(); ++index) {
		if (index > 0) {
			capabilitiesJson += ",";
		}

		const auto& contract = contracts[index];
		capabilitiesJson +=
			"{\"capabilityId\":\"" +
			EscapeJsonLocal(contract.capabilityId) +
			"\",\"owner\":\"" +
			EscapeJsonLocal(contract.owner) +
			"\",\"version\":\"" +
			EscapeJsonLocal(contract.version) +
			"\",\"stability\":\"" +
			EscapeJsonLocal(contract.stability) +
			"\",\"description\":\"" +
			EscapeJsonLocal(contract.description) +
			"\"}";
	}

	capabilitiesJson += "]";
	return capabilitiesJson;
}

std::string SerializePluginRuntimeTransitionsJsonLocal(
	const std::vector<PluginRuntimeTransitionEntry>& transitions,
	const std::size_t limit) {
	auto serializeSeverity = [](const PluginRuntimeTransitionSeverity severity) {
		switch (severity) {
		case PluginRuntimeTransitionSeverity::Warn:
			return "warn";
		case PluginRuntimeTransitionSeverity::Error:
			return "error";
		case PluginRuntimeTransitionSeverity::Info:
		default:
			return "info";
		}
		};

	auto serializeLifecyclePhase = [](const PluginRuntimeLifecyclePhase phase) {
		switch (phase) {
		case PluginRuntimeLifecyclePhase::Activation:
			return "activation";
		case PluginRuntimeLifecyclePhase::Mutation:
			return "mutation";
		case PluginRuntimeLifecyclePhase::Deactivation:
			return "deactivation";
		case PluginRuntimeLifecyclePhase::Maintenance:
			return "maintenance";
		case PluginRuntimeLifecyclePhase::SteadyState:
		default:
			return "steady-state";
		}
		};

	const std::size_t startIndex =
		transitions.size() > limit
		? transitions.size() - limit
		: 0;

	std::string transitionsJson = "[";
	for (std::size_t index = startIndex; index < transitions.size(); ++index) {
		if (index > startIndex) {
			transitionsJson += ",";
		}

		const auto& entry = transitions[index];
		transitionsJson +=
			"{\"sequence\":" +
			std::to_string(entry.sequence) +
			",\"timestampMs\":" +
			std::to_string(entry.timestampMs) +
			",\"action\":\"" +
			EscapeJsonLocal(entry.action) +
			"\",\"severity\":\"" +
			serializeSeverity(entry.severity) +
			"\",\"lifecyclePhase\":\"" +
			serializeLifecyclePhase(entry.lifecyclePhase) +
			"\",\"activeVersion\":" +
			std::to_string(entry.activeVersion) +
			",\"httpRouteVersion\":" +
			std::to_string(entry.httpRouteVersion) +
			",\"channelVersion\":" +
			std::to_string(entry.channelVersion) +
			",\"activeRegistryCount\":" +
			std::to_string(entry.activeRegistryCount) +
			",\"httpRoutePinned\":" +
			std::string(entry.httpRoutePinned ? "true" : "false") +
			",\"channelPinned\":" +
			std::string(entry.channelPinned ? "true" : "false") +
			",\"cacheKey\":\"" +
			EscapeJsonLocal(entry.cacheKey) +
			"\",\"workspaceDir\":\"" +
			EscapeJsonLocal(entry.workspaceDir) +
			"\",\"runtimeSubagentMode\":\"" +
			SerializePluginRuntimeSubagentModeLocal(
				entry.runtimeSubagentMode) +
			"\"}";
	}

	transitionsJson += "]";
	return transitionsJson;
}

std::string SerializeSkillCatalogEntry(
	const SkillsCatalogGatewayEntry& entry) {
	return "{\"name\":\"" +
		EscapeJsonLocal(entry.name) +
		"\",\"skillKey\":\"" +
		EscapeJsonLocal(entry.skillKey) +
		"\",\"primaryEnv\":\"" +
		EscapeJsonLocal(entry.primaryEnv) +
		"\",\"requiresBins\":" +
		SerializeStringArrayLocal(entry.requiresBins) +
		",\"requiresEnv\":" +
		SerializeStringArrayLocal(entry.requiresEnv) +
		",\"requiresConfig\":" +
		SerializeStringArrayLocal(entry.requiresConfig) +
		",\"configPathHints\":" +
		SerializeStringArrayLocal(entry.configPathHints) +
		",\"normalizedMetadataSources\":" +
		SerializeStringArrayLocal(entry.normalizedMetadataSources) +
		",\"command\":\"" +
		EscapeJsonLocal(entry.commandName) +
		"\",\"installKind\":\"" +
		EscapeJsonLocal(entry.installKind) +
		"\",\"installCommand\":\"" +
		EscapeJsonLocal(entry.installCommand) +
		"\",\"installExecutable\":" +
		std::string(entry.installExecutable ? "true" : "false") +
		",\"installReason\":\"" +
		EscapeJsonLocal(entry.installReason) +
		"\"" +
		"\",\"description\":\"" +
		EscapeJsonLocal(entry.description) +
		"\",\"source\":\"" +
		EscapeJsonLocal(entry.source) +
		"\",\"precedence\":" +
		std::to_string(entry.precedence) +
		",\"eligible\":" +
		std::string(entry.eligible ? "true" : "false") +
		",\"disabled\":" +
		std::string(entry.disabled ? "true" : "false") +
		",\"blockedByAllowlist\":" +
		std::string(entry.blockedByAllowlist ? "true" : "false") +
		",\"missingEnv\":" +
		SerializeStringArrayLocal(entry.missingEnv) +
		",\"missingConfig\":" +
		SerializeStringArrayLocal(entry.missingConfig) +
		",\"missingBins\":" +
		SerializeStringArrayLocal(entry.missingBins) +
		",\"missingAnyBins\":" +
		SerializeStringArrayLocal(entry.missingAnyBins) +
		",\"openclawOriginalActivationState\":\"" +
		EscapeJsonLocal(entry.openClawOriginalActivationState) +
		"\",\"openclawOriginalOrigin\":\"" +
		EscapeJsonLocal(entry.openClawOriginalOrigin) +
		",\"openclawOriginalOrigin\":\"" +
		EscapeJsonLocal(entry.openClawOriginalOrigin) +
		",\"openclawOriginalImportDiagnostics\":" +
		SerializeStringArrayLocal(entry.openClawOriginalImportDiagnostics) +
		",\"openclawOriginalMetadataConvertedFromClawdbot\":" +
		std::string(entry.openClawOriginalMetadataConvertedFromClawdbot
			? "true"
			: "false") +
		",\"openclawOriginalMissingToolManifest\":" +
		std::string(entry.openClawOriginalMissingToolManifest
			? "true"
			: "false") +
		",\"browserGroup\":\"" +
		EscapeJsonLocal(entry.browserGroup) +
		"\",\"browserDisplayName\":\"" +
		EscapeJsonLocal(entry.browserDisplayName) +
		"\",\"browserSourceLabel\":\"" +
		EscapeJsonLocal(entry.browserSourceLabel) +
		"\",\"browserVariantLabel\":\"" +
		EscapeJsonLocal(entry.browserVariantLabel) +
		"\",\"disableModelInvocation\":" +
		std::string(entry.disableModelInvocation ? "true" : "false") +
		",\"validFrontmatter\":" +
		std::string(entry.validFrontmatter ? "true" : "false") +
		",\"validationErrorCount\":" +
		std::to_string(entry.validationErrorCount) +
		"}";
}

GatewayHost::ChatRuntimeResult::TaskDeltaEntry NormalizeTaskDeltaEntry(
	const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& source,
	const std::string& runId,
	const std::string& sessionKey,
	const std::size_t defaultIndex) {
	GatewayHost::ChatRuntimeResult::TaskDeltaEntry normalized = source;
	normalized.index = source.index == 0 && defaultIndex > 0
		? defaultIndex
		: source.index;
	if (normalized.runId.empty()) {
		normalized.runId = runId;
	}

	if (normalized.sessionId.empty()) {
		normalized.sessionId = sessionKey;
	}

	if (normalized.phase.empty()) {
		normalized.phase = "unknown";
	}

	if (normalized.status.empty()) {
		normalized.status = normalized.phase == "final"
			? "completed"
			: "running";
	}

	if (normalized.stepLabel.empty()) {
		normalized.stepLabel = normalized.phase;
	}

	if (normalized.startedAtMs == 0) {
		normalized.startedAtMs = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch())
			.count());
	}

	if (normalized.completedAtMs == 0 ||
		normalized.completedAtMs < normalized.startedAtMs) {
		normalized.completedAtMs = normalized.startedAtMs;
	}

	normalized.latencyMs =
		normalized.completedAtMs - normalized.startedAtMs;
	return normalized;
}

constexpr char kSilentReplyToken[] = "NO_REPLY";

std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> EnsureRuntimeTaskDeltas(
	const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
	const std::string& runId,
	const std::string& sessionKey,
	const bool success,
	const std::string& assistantText,
	const std::string& errorCode,
	const std::string& errorMessage) {
	if (!taskDeltas.empty()) {
		return taskDeltas;
	}

	const std::uint64_t nowMs = static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
		.count());

	return std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>{
		GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
			.index = 0,
			.runId = runId,
			.sessionId = sessionKey,
			.phase = "final",
			.resultJson = success ? assistantText : errorMessage,
			.status = success ? "completed" : "failed",
			.errorCode = success ? std::string() : errorCode,
			.startedAtMs = nowMs,
			.completedAtMs = nowMs,
			.latencyMs = 0,
			.stepLabel = "run_terminal",
		}
	};
}

std::optional<std::size_t> ExtractSizeParam(
	const std::optional<std::string>& paramsJson,
	const std::string& fieldName) {
	if (!paramsJson.has_value()) {
		return std::nullopt;
	}

	std::uint64_t value = 0;
	if (!json::FindUInt64Field(paramsJson.value(), fieldName, value)) {
		return std::nullopt;
	}

	return static_cast<std::size_t>(value);
}

std::optional<bool> ExtractBoolParam(
	const std::optional<std::string>& paramsJson,
	const std::string& fieldName) {
	if (!paramsJson.has_value()) {
		return std::nullopt;
	}

	bool value = false;
	if (!json::FindBoolField(paramsJson.value(), fieldName, value)) {
		return std::nullopt;
	}

	return value;
}

bool HasAgentId(
	const GatewayAgentRegistry& registry,
	const std::string& agentId) {
	if (agentId.empty()) {
		return false;
	}

	const auto agents = registry.List();
	return std::any_of(
		agents.begin(),
		agents.end(),
		[&](const AgentEntry& entry) {
			return entry.id == agentId;
		});
}

bool HasSessionId(
	const GatewaySessionRegistry& registry,
	const std::string& sessionId) {
	if (sessionId.empty()) {
		return false;
	}

	const auto sessions = registry.List();
	return std::any_of(
		sessions.begin(),
		sessions.end(),
		[&](const SessionEntry& entry) {
			return entry.id == sessionId;
		});
}

std::uint64_t CurrentEpochMsLocal() {
	const auto now = std::chrono::system_clock::now();
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch())
		.count());
}

std::string BuildAssistantFinalMessageJson(
	const std::string& text,
	const std::uint64_t timestampMs) {
	return "{\"role\":\"assistant\",\"text\":\"" +
		EscapeJsonLocal(text) +
		"\",\"content\":[{\"type\":\"text\",\"text\":\"" +
		EscapeJsonLocal(text) +
		"\"}],\"timestamp\":" +
		std::to_string(timestampMs) +
		"}";
}

std::string BuildAssistantDeltaMessageJson(const std::string& text) {
	return
		"{\"role\":\"assistant\",\"text\":\"" +
		EscapeJsonLocal(text) +
		"\"}";
}

std::string BuildUserMessageJson(
	const std::string& text,
	const bool hasAttachments,
	const std::uint64_t timestampMs) {
	std::string content = "[";
	bool first = true;
	if (!text.empty()) {
		content +=
			"{\"type\":\"text\",\"text\":\"" +
			EscapeJsonLocal(text) +
			"\"}";
		first = false;
	}

	if (hasAttachments) {
		if (!first) {
			content += ",";
		}

		content +=
			"{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/*\",\"data\":\"[omitted]\"}}";
	}

	content += "]";

	return "{\"role\":\"user\",\"content\":" +
		content +
		",\"timestamp\":" +
		std::to_string(timestampMs) +
		"}";
}

std::string BuildChatEventJson(
	const std::string& runId,
	const std::string& sessionKey,
	const std::string& state,
	const std::optional<std::string>& messageJson,
	const std::optional<std::string>& errorCode,
	const std::optional<std::string>& errorMessage,
	const std::optional<std::string>& contextJson,
	const bool approvalRequired,
	const std::optional<std::string>& approvalToken,
	const std::optional<std::uint64_t>& approvalTokenExpiresAtEpochMs,
	const std::optional<std::string>& approvalNextAction,
	const std::optional<std::string>& terminalReason,
	const std::uint64_t timestampMs) {
	std::string payload =
		"{\"runId\":\"" +
		EscapeJsonLocal(runId) +
		"\",\"sessionKey\":\"" +
		EscapeJsonLocal(sessionKey) +
		"\",\"state\":\"" +
		EscapeJsonLocal(state) +
		"\",\"timestamp\":" +
		std::to_string(timestampMs);

	if (messageJson.has_value()) {
		payload += ",\"message\":" + messageJson.value();
	}

	if (errorCode.has_value()) {
		payload +=
			",\"errorCode\":\"" +
			EscapeJsonLocal(errorCode.value()) +
			"\"";
	}

	if (errorMessage.has_value()) {
		payload +=
			",\"errorMessage\":\"" +
			EscapeJsonLocal(errorMessage.value()) +
			"\"";
	}

	if (contextJson.has_value()) {
		payload += ",\"context\":" + contextJson.value();
	}

	if (approvalRequired) {
		payload += ",\"approvalRequired\":true";
	}

	if (approvalToken.has_value()) {
		payload += ",\"approvalToken\":\"" + EscapeJsonLocal(approvalToken.value()) + "\"";
	}

	if (approvalTokenExpiresAtEpochMs.has_value()) {
		payload += ",\"approvalTokenExpiresAtEpochMs\":" +
			std::to_string(approvalTokenExpiresAtEpochMs.value());
	}

	if (approvalNextAction.has_value()) {
		payload += ",\"approvalNextAction\":\"" +
			EscapeJsonLocal(approvalNextAction.value()) +
			"\"";
	}

	if (terminalReason.has_value()) {
		payload += ",\"terminalReason\":\"" +
			EscapeJsonLocal(terminalReason.value()) +
			"\"";
	}

	payload += "}";
	return payload;
}

void EmitPushLifecycleEvent(
	GatewayWebSocketTransport& transport,
	const GatewayEventFanoutService& fanout,
	const GatewayEventFanoutService::ChatLifecycleEvent& event,
	std::uint64_t& eventSeq) {
	std::string queueError;
	const std::string frame = fanout.BuildChatLifecycleEventFrame(event, ++eventSeq);
	transport.BroadcastOutboundFrame(frame, queueError);
}

bool IsTerminalChatState(const std::string& state) {
	return state == "final" || state == "error" || state == "aborted" || state == "needs_approval";
}

std::string SerializeTaskDeltaEntryJson(
	const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
	return "{\"index\":" +
		std::to_string(delta.index) +
		",\"schemaVersion\":" +
		std::to_string(delta.schemaVersion) +
		",\"runId\":\"" +
		EscapeJsonLocal(delta.runId) +
		"\",\"sessionId\":\"" +
		EscapeJsonLocal(delta.sessionId) +
		"\",\"phase\":\"" +
		EscapeJsonLocal(delta.phase) +
		"\",\"toolName\":\"" +
		EscapeJsonLocal(delta.toolName) +
		"\",\"fallbackBackend\":\"" +
		EscapeJsonLocal(delta.fallbackBackend) +
		"\",\"fallbackAction\":\"" +
		EscapeJsonLocal(delta.fallbackAction) +
		"\",\"fallbackAttempt\":" +
		std::to_string(delta.fallbackAttempt) +
		",\"fallbackMaxAttempts\":" +
		std::to_string(delta.fallbackMaxAttempts) +
		",\"argsJson\":\"" +
		EscapeJsonLocal(delta.argsJson) +
		"\",\"resultJson\":\"" +
		EscapeJsonLocal(delta.resultJson) +
		"\",\"status\":\"" +
		EscapeJsonLocal(delta.status) +
		"\",\"errorCode\":\"" +
		EscapeJsonLocal(delta.errorCode) +
		"\",\"errorMessage\":\"" +
		EscapeJsonLocal(delta.errorMessage) +
		"\",\"startedAtMs\":" +
		std::to_string(delta.startedAtMs) +
		",\"completedAtMs\":" +
		std::to_string(delta.completedAtMs) +
		",\"latencyMs\":" +
		std::to_string(delta.latencyMs) +
		",\"modelTurnId\":\"" +
		EscapeJsonLocal(delta.modelTurnId) +
		"\",\"stepLabel\":\"" +
		EscapeJsonLocal(delta.stepLabel) +
		"\"}";
}

std::string ToLowerCopyLocal(const std::string& value) {
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

bool HasNonAsciiBytesLocal(const std::string& value) {
	for (const unsigned char ch : value) {
		if (ch > 0x7Fu) {
			return true;
		}
	}
	return false;
}

std::string TranslateChineseCityToAsciiLocal(const std::string& city) {
	const std::string trimmedCity = json::Trim(city);
	if (trimmedCity == Utf8LiteralLocal(u8"上海")) {
		return "Shanghai";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"武汉")) {
		return "Wuhan";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"北京")) {
		return "Beijing";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"深圳")) {
		return "Shenzhen";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"广州")) {
		return "Guangzhou";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"杭州")) {
		return "Hangzhou";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"南京")) {
		return "Nanjing";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"成都")) {
		return "Chengdu";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"重庆")) {
		return "Chongqing";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"天津")) {
		return "Tianjin";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"西安")) {
		return "Xi'an";
	}
	if (trimmedCity == Utf8LiteralLocal(u8"苏州")) {
		return "Suzhou";
	}
	return {};
}

std::string ResolveAsciiCityForDeliveryLocal(const std::string& city) {
	const std::string trimmedCity = json::Trim(city);
	if (trimmedCity.empty()) {
		return "the requested city";
	}
	if (!HasNonAsciiBytesLocal(trimmedCity)) {
		return trimmedCity;
	}

	const std::string translated = TranslateChineseCityToAsciiLocal(trimmedCity);
	if (!translated.empty()) {
		return translated;
	}

	return "the requested city";
}

std::string BuildSafeEmailSubjectLocal(const std::string& city) {
	const std::string deliveryCity = ResolveAsciiCityForDeliveryLocal(city);
	if (deliveryCity == "the requested city") {
		return "Weather report";
	}
	return deliveryCity + " weather report";
}

std::string Utf8LiteralLocal(const char* value) {
	return value == nullptr ? std::string{} : std::string(value);
}

#if defined(__cpp_char8_t)
std::string Utf8LiteralLocal(const char8_t* value) {
	if (value == nullptr) {
		return {};
	}

	return std::string(reinterpret_cast<const char*>(value));
}
#endif

bool IsLikelyChinesePromptLocal(const std::string& text) {
	if (text.empty()) {
		return false;
	}

	for (std::size_t i = 0; i < text.size();) {
		const unsigned char lead =
			static_cast<unsigned char>(text[i]);
		std::uint32_t codePoint = 0;
		std::size_t advance = 1;

		if ((lead & 0x80u) == 0) {
			codePoint = lead;
		}
		else if ((lead & 0xE0u) == 0xC0u && i + 1 < text.size()) {
			const unsigned char b1 =
				static_cast<unsigned char>(text[i + 1]);
			if ((b1 & 0xC0u) != 0x80u) {
				i += 1;
				continue;
			}

			codePoint =
				(static_cast<std::uint32_t>(lead & 0x1Fu) << 6) |
				static_cast<std::uint32_t>(b1 & 0x3Fu);
			advance = 2;
		}
		else if ((lead & 0xF0u) == 0xE0u && i + 2 < text.size()) {
			const unsigned char b1 =
				static_cast<unsigned char>(text[i + 1]);
			const unsigned char b2 =
				static_cast<unsigned char>(text[i + 2]);
			if ((b1 & 0xC0u) != 0x80u || (b2 & 0xC0u) != 0x80u) {
				i += 1;
				continue;
			}

			codePoint =
				(static_cast<std::uint32_t>(lead & 0x0Fu) << 12) |
				(static_cast<std::uint32_t>(b1 & 0x3Fu) << 6) |
				static_cast<std::uint32_t>(b2 & 0x3Fu);
			advance = 3;
		}
		else if ((lead & 0xF8u) == 0xF0u && i + 3 < text.size()) {
			const unsigned char b1 =
				static_cast<unsigned char>(text[i + 1]);
			const unsigned char b2 =
				static_cast<unsigned char>(text[i + 2]);
			const unsigned char b3 =
				static_cast<unsigned char>(text[i + 3]);
			if ((b1 & 0xC0u) != 0x80u ||
				(b2 & 0xC0u) != 0x80u ||
				(b3 & 0xC0u) != 0x80u) {
				i += 1;
				continue;
			}

			codePoint =
				(static_cast<std::uint32_t>(lead & 0x07u) << 18) |
				(static_cast<std::uint32_t>(b1 & 0x3Fu) << 12) |
				(static_cast<std::uint32_t>(b2 & 0x3Fu) << 6) |
				static_cast<std::uint32_t>(b3 & 0x3Fu);
			advance = 4;
		}

		const bool isCjkUnifiedIdeograph =
			(codePoint >= 0x4E00u && codePoint <= 0x9FFFu) ||
			(codePoint >= 0x3400u && codePoint <= 0x4DBFu);
		if (isCjkUnifiedIdeograph) {
			return true;
		}

		i += advance;
	}

	return false;
}

std::string SerializeStringArrayLocal(
	const std::vector<std::string>& values) {
	std::string json = "[";
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i > 0) {
			json += ",";
		}

		json += JsonString(values[i]);
	}

	json += "]";
	return json;
}

protocol::ErrorShape BuildRuntimeErrorShape(
	const std::string& code,
	const std::string& message,
	const std::string& runId,
	const std::string& sessionKey) {
	std::string details =
		"{\"runId\":" + JsonString(runId) +
		",\"sessionKey\":" + JsonString(sessionKey) + "}";

	protocol::ErrorShape shape{
		.code = code,
		.message = message,
		.detailsJson = details,
		.retryable = RuntimeTranscriptGuard::IsRetryableErrorCode(code),
		.retryAfterMs = RuntimeTranscriptGuard::SuggestedRetryAfterMs(code),
	};
	return shape;
}

std::string ResolveCurrentLocalTimeHHmm() {
	std::time_t now = std::time(nullptr);
	std::tm localTime = {};
#if defined(_WIN32)
	localtime_s(&localTime, &now);
#else
	localtime_r(&now, &localTime);
#endif

	std::ostringstream output;
	output << std::setw(2) << std::setfill('0') << localTime.tm_hour
		<< ":"
		<< std::setw(2) << std::setfill('0') << localTime.tm_min;
	return output.str();
}

std::optional<std::string> TryParsePromptSendAt(
	const std::string& message) {
	static const std::regex kTwelveHourRegex(
		R"((\b\d{1,2})(?::(\d{2}))?\s*(am|pm)\b)",
		std::regex_constants::icase);
	static const std::regex kTwentyFourHourRegex(
		R"((\b\d{1,2}):(\d{2})\b)");

	std::smatch twelveHourMatch;
	if (std::regex_search(message, twelveHourMatch, kTwelveHourRegex) &&
		twelveHourMatch.size() >= 4) {
		int hour = 0;
		int minute = 0;
		try {
			hour = std::stoi(twelveHourMatch[1].str());
			minute = twelveHourMatch[2].matched
				? std::stoi(twelveHourMatch[2].str())
				: 0;
		}
		catch (...) {
			return std::nullopt;
		}

		if (hour < 1 || hour > 12 || minute < 0 || minute > 59) {
			return std::nullopt;
		}

		std::string meridiem = twelveHourMatch[3].str();
		std::transform(
			meridiem.begin(),
			meridiem.end(),
			meridiem.begin(),
			[](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});

		if (meridiem == "am") {
			hour = hour == 12 ? 0 : hour;
		}
		else {
			hour = hour == 12 ? 12 : hour + 12;
		}

		std::ostringstream time;
		time << std::setw(2) << std::setfill('0') << hour
			<< ":"
			<< std::setw(2) << std::setfill('0') << minute;
		return time.str();
	}

	std::smatch twentyFourHourMatch;
	if (std::regex_search(message, twentyFourHourMatch, kTwentyFourHourRegex) &&
		twentyFourHourMatch.size() >= 3) {
		int hour = 0;
		int minute = 0;
		try {
			hour = std::stoi(twentyFourHourMatch[1].str());
			minute = std::stoi(twentyFourHourMatch[2].str());
		}
		catch (...) {
			return std::nullopt;
		}

		if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
			return std::nullopt;
		}

		std::ostringstream time;
		time << std::setw(2) << std::setfill('0') << hour
			<< ":"
			<< std::setw(2) << std::setfill('0') << minute;
		return time.str();
	}

	return std::nullopt;
}

PromptScheduleResolution ResolvePromptSchedule(
	const std::string& message,
	const std::string& loweredMessage) {
	PromptScheduleResolution schedule;

	const auto parsedTime = TryParsePromptSendAt(message);
	if (parsedTime.has_value()) {
		schedule.hasSchedule = true;
		schedule.sendAt = parsedTime.value();
		schedule.kind = "clock_time";
		return schedule;
	}

	const bool immediateKeyword =
		loweredMessage.find("right now") != std::string::npos ||
		loweredMessage.find("immediately") != std::string::npos ||
		loweredMessage.find(" as soon as possible") !=
		std::string::npos ||
		loweredMessage.find(" now") != std::string::npos ||
		loweredMessage.rfind("now", 0) == 0;
	if (immediateKeyword) {
		schedule.hasSchedule = true;
		schedule.immediate = true;
		schedule.sendAt = ResolveCurrentLocalTimeHHmm();
		schedule.kind = "immediate_keyword";
		return schedule;
	}

	schedule.hasSchedule = false;
	schedule.immediate = false;
	schedule.sendAt = "13:00";
	schedule.kind = "default_fallback";
	return schedule;
}

bool HasWeatherIntent(const std::string& loweredMessage) {
	return loweredMessage.find("weather") != std::string::npos;
}

bool HasEmailIntent(const std::string& loweredMessage) {
	return loweredMessage.find("email") != std::string::npos ||
		loweredMessage.find("mail") != std::string::npos;
}

bool HasReportIntent(const std::string& loweredMessage) {
	return loweredMessage.find("report") != std::string::npos ||
		loweredMessage.find("summary") != std::string::npos ||
		loweredMessage.find("write") != std::string::npos;
}

std::string ExtractFirstEmailAddress(const std::string& text) {
	static const std::regex kEmailRegex(
		R"(([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}))");

	std::smatch match;
	if (std::regex_search(text, match, kEmailRegex) && !match.empty()) {
		return match[1].str();
	}

	return {};
}

std::string ResolvePromptCity(const std::string& message) {
	const std::string lowered = [&message]() {
		std::string value = message;
		std::transform(
			value.begin(),
			value.end(),
			value.begin(),
			[](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});
		return value;
		}();

	if (lowered.find("wuhan") != std::string::npos) {
		return "Wuhan";
	}

	return "Wuhan";
}

std::string ResolvePromptDate(const std::string& message) {
	const std::string lowered = ToLowerCopyLocal(message);

	if (lowered.find("today") != std::string::npos) {
		return "today";
	}

	if (lowered.find("tomorrow") != std::string::npos) {
		return "tomorrow";
	}

	return "tomorrow";
}

std::string ResolvePromptSendAt(const std::string& message) {
	const auto schedule =
		ResolvePromptSchedule(message, ToLowerCopyLocal(message));
	return schedule.sendAt;
}

std::string BuildWeatherReportText(
	const std::string& city,
	const std::string& date,
	const std::string& condition,
	const int temperatureC,
	const std::string& wind,
	const int humidityPct,
	const bool preferChinese) {
	if (preferChinese) {
		return city + Utf8LiteralLocal(u8"\uFF08") + date +
			Utf8LiteralLocal(u8"\uFF09\u5929\u6C14\uFF1A") +
			condition + Utf8LiteralLocal(u8"\uFF0C\u6C14\u6E29\u7EA6 ") +
			std::to_string(temperatureC) +
			"C" + Utf8LiteralLocal(u8"\uFF0C\u98CE\u529B ") + wind +
			Utf8LiteralLocal(u8"\uFF0C\u6E7F\u5EA6 ") +
			std::to_string(humidityPct) + "%" +
			Utf8LiteralLocal(u8"\u3002");
	}

	return "Weather report for " + city + " (" + date + "): " +
		condition + ", around " + std::to_string(temperatureC) +
		"C, wind " + wind + ", humidity " +
		std::to_string(humidityPct) + "% .";
}

void ResolveFallbackProbeDiagnostic(
	const std::string& rawOutput,
	std::string& outCode,
	std::string& outMessage) {
	outCode.clear();
	outMessage.clear();

	if (rawOutput.empty()) {
		return;
	}

	try {
		const auto payload = nlohmann::json::parse(rawOutput);
		if (payload.is_object()) {
			if (payload.contains("error") && payload["error"].is_object()) {
				const auto& error = payload["error"];
				if (error.contains("code") && error["code"].is_string()) {
					outCode = error["code"].get<std::string>();
				}
				if (error.contains("message") && error["message"].is_string()) {
					outMessage = error["message"].get<std::string>();
				}
			}

			if (outCode.empty() &&
				payload.contains("code") &&
				payload["code"].is_string()) {
				outCode = payload["code"].get<std::string>();
			}

			if (outMessage.empty() &&
				payload.contains("message") &&
				payload["message"].is_string()) {
				outMessage = payload["message"].get<std::string>();
			}
		}
	}
	catch (...) {
	}

	if (!outCode.empty()) {
		if (outCode == "node_cli_missing" &&
			(outMessage.empty() ||
				ToLowerCopyLocal(json::Trim(outMessage)) == "node_cli_missing")) {
			outMessage = "node runtime not found";
		}

		return;
	}

	const std::string lowered = ToLowerCopyLocal(rawOutput);
	if (lowered.find("node_cli_missing") != std::string::npos) {
		outCode = "node_cli_missing";
		outMessage = "node runtime not found";
		return;
	}

	if (lowered.find("imap_smtp_skill_missing") != std::string::npos) {
		outCode = "imap_smtp_skill_missing";
		outMessage = "imap smtp skill scripts not found";
		return;
	}

	if (lowered.find("invalid_himalaya_account") != std::string::npos ||
		lowered.find("invalid_account") != std::string::npos) {
		outCode = "invalid_account";
		outMessage = "configured email account is invalid";
		return;
	}

	if (lowered.find("imap_smtp_send_failed") != std::string::npos) {
		outCode = "imap_smtp_send_failed";
		outMessage = "imap smtp send failed; check configuration/account credentials";
	}
}

ChatPromptOrchestrationResult TryOrchestrateWeatherEmailPrompt(
	GatewayToolRegistry& toolRegistry,
	const std::string& message) {
	ChatPromptOrchestrationResult result;
	try {
		const bool preferChinese = IsLikelyChinesePromptLocal(message);
		const auto intent =
			prompt::AnalyzeWeatherEmailPromptIntent(message);
		result.matched = intent.matched;
		result.missReasons = intent.missReasons;
		result.scheduleKind = intent.scheduleKind;
		result.city = intent.city;
		result.date = intent.date;
		result.recipient = intent.recipient;
		result.sendAt = intent.sendAt;
		result.decompositionSteps = intent.decompositionSteps;
		if (!result.matched) {
			return result;
		}

		const std::string city = intent.city;
		const std::string date = intent.date;
		const std::string sendAt = intent.sendAt;
		const std::string recipient = intent.recipient;
		result.city = city;
		result.date = date;
		result.recipient = recipient;
		result.sendAt = sendAt;
		result.scheduleKind = intent.scheduleKind;
		result.decompositionSteps = intent.decompositionSteps;

		if (recipient.empty()) {
			result.success = false;
			result.terminalStatus = "failed";
			result.terminalReason = "recipient_missing";
			result.errorCode = "orchestration_invalid_prompt";
			result.errorMessage = "recipient_email_required";
			return result;
		}

		nlohmann::json weatherArgs = {
			{ "city", city },
			{ "date", date },
		};
		const auto weatherExecution = toolRegistry.Execute(
			"weather.lookup",
			weatherArgs.dump());

		if (!weatherExecution.executed || weatherExecution.status != "ok") {
			result.success = false;
			result.terminalStatus = "failed";
			result.terminalReason = "weather_failed";
			result.errorCode = "orchestration_weather_failed";
			result.errorMessage = weatherExecution.output;
			return result;
		}

		std::string condition = "Cloudy";
		int temperatureC = 20;
		std::string wind = "NE 9 km/h";
		int humidityPct = 68;
		try {
			const auto weatherPayload =
				nlohmann::json::parse(weatherExecution.output);
			if (weatherPayload.contains("forecast") &&
				weatherPayload["forecast"].is_object()) {
				const auto& forecast = weatherPayload["forecast"];
				if (forecast.contains("condition") && forecast["condition"].is_string()) {
					condition = forecast["condition"].get<std::string>();
				}
				if (forecast.contains("temperatureC") && forecast["temperatureC"].is_number_integer()) {
					temperatureC = forecast["temperatureC"].get<int>();
				}
				if (forecast.contains("wind") && forecast["wind"].is_string()) {
					wind = forecast["wind"].get<std::string>();
				}
				if (forecast.contains("humidityPct") && forecast["humidityPct"].is_number_integer()) {
					humidityPct = forecast["humidityPct"].get<int>();
				}
			}
		}
		catch (...) {
		}

		const std::string report = BuildWeatherReportText(
			city,
			date,
			condition,
			temperatureC,
			wind,
			humidityPct,
			preferChinese);
		const std::string deliveryBody = preferChinese
			? report
			: BuildWeatherReportText(
				ResolveAsciiCityForDeliveryLocal(city),
				date,
				condition,
				temperatureC,
				wind,
				humidityPct,
				false);
		const std::string emailSubject = BuildSafeEmailSubjectLocal(city);

		nlohmann::json emailPrepareArgs = {
			{ "action", "prepare" },
			{ "to", recipient },
			{ "subject", emailSubject },
			{ "body", deliveryBody },
			{ "sendAt", sendAt },
		};

		const auto emailPrepareExecution = toolRegistry.Execute(
			"email.schedule",
			emailPrepareArgs.dump());

		if (!emailPrepareExecution.executed ||
			emailPrepareExecution.status != "needs_approval") {
			result.success = false;
			result.terminalStatus = "failed";
			result.terminalReason = "email_prepare_failed";
			result.errorCode = "orchestration_email_prepare_failed";
			result.errorMessage = emailPrepareExecution.output;
			return result;
		}

		std::string approvalToken;
		std::uint64_t approvalTokenExpiresAtEpochMs = 0;
		try {
			const auto emailPayload =
				nlohmann::json::parse(emailPrepareExecution.output);
			if (emailPayload.contains("requiresApproval") &&
				emailPayload["requiresApproval"].is_object()) {
				const auto& approval = emailPayload["requiresApproval"];
				if (approval.contains("approvalToken") &&
					approval["approvalToken"].is_string()) {
					approvalToken = approval["approvalToken"].get<std::string>();
				}
				if (approval.contains("approvalTokenExpiresAtEpochMs") &&
					approval["approvalTokenExpiresAtEpochMs"].is_number_unsigned()) {
					approvalTokenExpiresAtEpochMs =
						approval["approvalTokenExpiresAtEpochMs"].get<std::uint64_t>();
				}
			}
		}
		catch (...) {
		}

		if (approvalToken.empty()) {
			result.success = false;
			result.terminalStatus = "failed";
			result.terminalReason = "approval_token_missing";
			result.errorCode = "orchestration_email_missing_approval_token";
			result.errorMessage = "approval_token_missing";
			return result;
		}

		result.approvalToken = approvalToken;
		result.approvalTokenExpiresAtEpochMs = approvalTokenExpiresAtEpochMs;
		result.approvalNextAction = "email.schedule.approve";

		ToolExecuteResult emailApproveExecution;
		bool shouldAutoApprove =
			intent.scheduleKind == "immediate_keyword";
		bool autoApproveBackendMissing = false;
		std::string autoApproveBackend = "himalaya";
		std::string fallbackProbeCode;
		std::string fallbackProbeMessage;
		if (shouldAutoApprove) {
			nlohmann::json emailApproveArgs = {
				{ "action", "approve" },
				{ "approvalToken", approvalToken },
				{ "approve", true },
			};

			emailApproveExecution = toolRegistry.Execute(
				"email.schedule",
				emailApproveArgs.dump());
			if (emailApproveExecution.executed &&
				emailApproveExecution.status == "ok") {
				try {
					const auto approvePayload =
						nlohmann::json::parse(emailApproveExecution.output);
					if (approvePayload.contains("output") &&
						approvePayload["output"].is_array() &&
						!approvePayload["output"].empty() &&
						approvePayload["output"][0].is_object() &&
						approvePayload["output"][0].contains("summary") &&
						approvePayload["output"][0]["summary"].is_object() &&
						approvePayload["output"][0]["summary"].contains("engine") &&
						approvePayload["output"][0]["summary"]["engine"].is_string()) {
						autoApproveBackend =
							approvePayload["output"][0]["summary"]["engine"].get<std::string>();
					}
				}
				catch (...) {
				}
			}
			else {
				const std::string approveOutputLower =
					ToLowerCopyLocal(emailApproveExecution.output);
				ResolveFallbackProbeDiagnostic(
					emailApproveExecution.output,
					fallbackProbeCode,
					fallbackProbeMessage);
				autoApproveBackendMissing =
					emailApproveExecution.status == "error" &&
					(approveOutputLower.find("missing") != std::string::npos ||
						approveOutputLower.find("unavailable") != std::string::npos ||
						emailApproveExecution.output.find("email_delivery_backends_exhausted") != std::string::npos);
				if (!autoApproveBackendMissing) {
					result.success = false;
					result.terminalStatus = "failed";
					result.terminalReason = "email_approve_failed";
					result.errorCode = "orchestration_email_approve_failed";
					result.errorMessage = emailApproveExecution.output;
					return result;
				}

				shouldAutoApprove = false;
			}
		}

		result.success = true;
		result.requiresApproval = !shouldAutoApprove;
		result.assistantDeltas = {
			"orchestration.intent city=" + city +
			" date=" + date +
			" source=structural_orchestration_signals",
			"tools.execute.start tool=weather.lookup",
			"tools.execute.result tool=weather.lookup status=ok",
			"tools.execute.start tool=email.schedule action=prepare",
			"tools.execute.result tool=email.schedule status=needs_approval",
		};
		if (shouldAutoApprove) {
			result.assistantDeltas.push_back(
				"tools.execute.start tool=email.schedule action=approve");
			result.assistantDeltas.push_back(
				"tools.execute.result tool=email.schedule status=ok");
		}
		else if (autoApproveBackendMissing) {
			result.assistantDeltas.push_back(
				"tools.execute.start tool=email.schedule action=approve");
			std::string approveDelta =
				"tools.execute.result tool=email.schedule status=needs_approval backend_missing=himalaya";
			if (!fallbackProbeCode.empty()) {
				approveDelta += " probe=" + fallbackProbeCode;
			}
			result.assistantDeltas.push_back(approveDelta);
		}

		if (shouldAutoApprove) {
			result.terminalStatus = "completed";
			result.terminalReason = "auto_approved";
			if (preferChinese) {
				result.assistantText =
					report +
					Utf8LiteralLocal(u8"\u5DF2\u901A\u8FC7 ") + autoApproveBackend +
					Utf8LiteralLocal(u8"\u5728 ") + sendAt +
					Utf8LiteralLocal(u8"\u5411 ") + recipient +
					Utf8LiteralLocal(u8"\u53D1\u9001\u90AE\u4EF6\u3002");
			}
			else {
				result.assistantText =
					report +
					" Email sent to " + recipient +
					" at " + sendAt +
					" via " + autoApproveBackend + ".";
			}
		}
		else {
			result.terminalStatus = "needs_approval";
			result.terminalReason = autoApproveBackendMissing
				? "fallback_backend_unavailable"
				: "approval_required";
			result.fallbackBackend = autoApproveBackend;
			result.fallbackAction = "continue";
			result.fallbackAttempt = 1;
			result.fallbackMaxAttempts = 2;
			result.approvalPrompt = "Approve email schedule by calling email.schedule with action=approve and approvalToken.";
			if (preferChinese) {
				result.assistantText =
					report +
					Utf8LiteralLocal(u8"\u5411 ") + recipient +
					Utf8LiteralLocal(u8"\u5728 ") + sendAt +
					Utf8LiteralLocal(u8"\u53D1\u9001\u90AE\u4EF6\u7684\u8BA1\u5212\u7B49\u5F85\u5BA1\u6279\u3002approvalToken=") +
					approvalToken;
			}
			else {
				result.assistantText =
					report +
					" Email scheduling to " + recipient +
					" at " + sendAt +
					" is pending approval. approvalToken=" +
					approvalToken;
			}
			if (autoApproveBackendMissing) {
				result.assistantText += preferChinese
					? Utf8LiteralLocal(u8" \u90AE\u4EF6\u6295\u9012\u540E\u7AEF\u4E0D\u53EF\u7528\uFF08\u7F3A\u5C11 himalaya CLI\uFF09\u3002\u8BF7\u5B89\u88C5\u5E76\u914D\u7F6E himalaya \u540E\u91CD\u65B0\u5BA1\u6279\u8BE5\u4EE4\u724C\u3002")
					: " Delivery backend is unavailable (himalaya CLI missing). Install/configure himalaya and re-approve this token.";
				const std::string fallbackProbeLabel =
					!fallbackProbeMessage.empty()
					? fallbackProbeMessage
					: fallbackProbeCode;
				if (!fallbackProbeLabel.empty()) {
					result.assistantText += preferChinese
						? " fallbackProbe=" + fallbackProbeLabel + Utf8LiteralLocal(u8"\u3002")
						: " fallbackProbe=" + fallbackProbeLabel + ".";
				}
			}
			if (approvalTokenExpiresAtEpochMs > 0) {
				result.assistantText +=
					" expiresAtEpochMs=" +
					std::to_string(approvalTokenExpiresAtEpochMs);
			}
		}

		return result;
	}
	catch (...) {
		result.success = false;
		result.matched = false;
		result.terminalStatus = "failed";
		result.terminalReason = "orchestration_exception";
		result.errorCode = "orchestration_internal_exception";
		result.errorMessage = "weather_email_orchestration_exception";
		return result;
	}
}

bool IsDeepSeekDiagnosticsVerboseEnabled() {
	static const bool enabled = []() {
		char* raw = nullptr;
		std::size_t size = 0;
		if (_dupenv_s(
			&raw,
			&size,
			"BLAZECLAW_DEEPSEEK_DEBUG_TELEMETRY") != 0 ||
			raw == nullptr) {
			return false;
		}

		std::string value(raw);
		free(raw);
		std::transform(
			value.begin(),
			value.end(),
			value.begin(),
			[](unsigned char ch) {
				return static_cast<char>(std::tolower(ch));
			});

		return value == "1" ||
			value == "true" ||
			value == "yes" ||
			value == "on";
		}();

	return enabled;
}

void EmitDeepSeekGatewayDiagnostic(
	const char* stage,
	const std::string& detail,
	const bool verboseOnly) {
	if (verboseOnly && !IsDeepSeekDiagnosticsVerboseEnabled()) {
		return;
	}

	const std::string safeStage =
		(stage == nullptr || std::string(stage).empty())
		? "unknown"
		: std::string(stage);
	TRACE(
		"[DeepSeek][%s] %s\n",
		safeStage.c_str(),
		detail.c_str());
}

bool IsSilentReplyText(const std::string& text) {
	return json::Trim(text) == kSilentReplyToken;
}

bool IsSilentAssistantMessageJson(const std::string& messageJson) {
	std::string role;
	if (!json::FindStringField(messageJson, "role", role)) {
		return false;
	}

	if (role != "assistant") {
		return false;
	}

	return messageJson.find("\"text\":\"NO_REPLY\"") !=
		std::string::npos;
}

void PushHistoryMessageIfNew(
	std::vector<std::string>& history,
	const std::string& messageJson) {
	if (!history.empty() && history.back() == messageJson) {
		return;
	}

	history.push_back(messageJson);
	if (history.size() > kMaxChatHistoryEntriesPerSession) {
		const std::size_t overflow =
			history.size() - kMaxChatHistoryEntriesPerSession;
		history.erase(
			history.begin(),
			history.begin() + static_cast<std::ptrdiff_t>(overflow));
	}
}

bool ValidateAttachmentPayloadShape(
	const std::optional<std::string>& paramsJson,
	bool& hasAttachments,
	std::string& errorCode,
	std::string& errorMessage) {
	hasAttachments = false;
	errorCode.clear();
	errorMessage.clear();
	if (!paramsJson.has_value()) {
		return true;
	}

	std::string attachmentsRaw;
	if (!json::FindRawField(paramsJson.value(), "attachments", attachmentsRaw)) {
		return true;
	}

	const std::string attachmentsTrimmed = json::Trim(attachmentsRaw);
	if (attachmentsTrimmed.empty() || attachmentsTrimmed == "[]") {
		return true;
	}

	if (attachmentsTrimmed.front() != '[' || attachmentsTrimmed.back() != ']') {
		errorCode = "invalid_attachments";
		errorMessage = "attachments must be a JSON array.";
		return false;
	}

	hasAttachments = true;
	if (attachmentsTrimmed.find("\"type\":\"image\"") == std::string::npos ||
		attachmentsTrimmed.find("\"mimeType\":\"") == std::string::npos ||
		attachmentsTrimmed.find("\"content\":\"") == std::string::npos) {
		errorCode = "invalid_attachments";
		errorMessage =
			"attachments entries must include type=image, mimeType, and content.";
		return false;
	}

	return true;
}

std::vector<std::string> ExtractAttachmentMimeTypes(
	const std::optional<std::string>& paramsJson) {
	std::vector<std::string> mimeTypes;
	if (!paramsJson.has_value()) {
		return mimeTypes;
	}

	std::string attachmentsRaw;
	if (!json::FindRawField(
		paramsJson.value(),
		"attachments",
		attachmentsRaw)) {
		return mimeTypes;
	}

	const std::string key = "\"mimeType\":\"";
	std::size_t cursor = 0;
	while (cursor < attachmentsRaw.size()) {
		const auto keyPos = attachmentsRaw.find(key, cursor);
		if (keyPos == std::string::npos) {
			break;
		}

		const std::size_t valueStart = keyPos + key.size();
		if (valueStart >= attachmentsRaw.size()) {
			break;
		}

		std::size_t valueEnd = valueStart;
		bool escaped = false;
		while (valueEnd < attachmentsRaw.size()) {
			const char ch = attachmentsRaw[valueEnd];
			if (escaped) {
				escaped = false;
				++valueEnd;
				continue;
			}

			if (ch == '\\') {
				escaped = true;
				++valueEnd;
				continue;
			}

			if (ch == '"') {
				break;
			}

			++valueEnd;
		}

		if (valueEnd > valueStart) {
			mimeTypes.push_back(
				attachmentsRaw.substr(valueStart, valueEnd - valueStart));
		}

		cursor = valueEnd == std::string::npos
			? attachmentsRaw.size()
			: valueEnd + 1;
	}

	return mimeTypes;
}

std::vector<std::string> ParseJsonStringArrayLocal(
	const std::string& rawArray) {
	std::vector<std::string> values;
	const std::string trimmed = json::Trim(rawArray);
	if (trimmed.size() < 2 ||
		trimmed.front() != '[' ||
		trimmed.back() != ']') {
		return values;
	}

	std::string current;
	bool inString = false;
	bool escaping = false;
	for (std::size_t i = 1; i + 1 < trimmed.size(); ++i) {
		const char ch = trimmed[i];
		if (!inString) {
			if (ch == '"') {
				inString = true;
				current.clear();
			}
			continue;
		}

		if (escaping) {
			current.push_back(ch);
			escaping = false;
			continue;
		}

		if (ch == '\\') {
			escaping = true;
			continue;
		}

		if (ch == '"') {
			values.push_back(current);
			inString = false;
			continue;
		}

		current.push_back(ch);
	}

	return values;
}

std::string ResolvePreferredToolForNamespace(
	const std::string& normalizedNamespace,
	const std::vector<ToolCatalogEntry>& tools) {
	if (normalizedNamespace.empty()) {
		return {};
	}

	const std::string preferredSendId =
		normalizedNamespace + ".smtp.send";
	for (const auto& tool : tools) {
		if (ToLowerCopyLocal(tool.id) == preferredSendId) {
			return tool.id;
		}
	}

	const std::string preferredSearchId =
		normalizedNamespace + ".search.web";
	for (const auto& tool : tools) {
		if (ToLowerCopyLocal(tool.id) == preferredSearchId) {
			return tool.id;
		}
	}

	for (const auto& tool : tools) {
		const std::string toolIdLower = ToLowerCopyLocal(tool.id);
		if (toolIdLower.rfind(normalizedNamespace + ".", 0) == 0) {
			return tool.id;
		}
	}

	return {};
}

std::string NormalizeOrderedTargetToken(const std::string& token) {
	std::string normalized = json::Trim(token);
	while (!normalized.empty() &&
		(normalized.back() == '.' ||
			normalized.back() == ';' ||
			normalized.back() == ',' ||
			normalized.back() == ':' ||
			normalized.back() == ')' ||
			normalized.back() == '"')) {
		normalized.pop_back();
	}

	while (!normalized.empty() &&
		(normalized.front() == '(' ||
			normalized.front() == '"')) {
		normalized.erase(normalized.begin());
	}

	return ToLowerCopyLocal(normalized);
}

std::vector<std::string> ExtractOrderedTargetsFromPrompt(
	const std::string& message,
	std::vector<std::string>* explicitCallTargets) {
	std::vector<std::string> targets;
	std::vector<std::string> explicitTargets;

	auto addTarget = [&targets](const std::string& candidate) {
		const std::string normalized =
			NormalizeOrderedTargetToken(candidate);
		if (normalized.empty()) {
			return;
		}

		if (std::find(targets.begin(), targets.end(), normalized) !=
			targets.end()) {
			return;
		}

		targets.push_back(normalized);
		};

	auto addExplicitTarget =
		[&explicitTargets](const std::string& candidate) {
		const std::string normalized =
			NormalizeOrderedTargetToken(candidate);
		if (normalized.empty()) {
			return;
		}

		if (std::find(
			explicitTargets.begin(),
			explicitTargets.end(),
			normalized) != explicitTargets.end()) {
			return;
		}

		explicitTargets.push_back(normalized);
		};

	const std::regex backtickTargetRegex(
		R"(`([A-Za-z0-9._-]+)`)",
		std::regex_constants::icase);
	for (std::sregex_iterator it(message.begin(), message.end(), backtickTargetRegex), end;
		it != end;
		++it) {
		if (it->size() >= 2) {
			const std::string target = (*it)[1].str();
			addExplicitTarget(target);
			addTarget(target);
		}
	}

	const std::regex callTargetRegex(
		R"(\bcall\s+([A-Za-z0-9._-]+))",
		std::regex_constants::icase);
	for (std::sregex_iterator it(message.begin(), message.end(), callTargetRegex), end;
		it != end;
		++it) {
		if (it->size() >= 2) {
			const std::string target = (*it)[1].str();
			addExplicitTarget(target);
			addTarget(target);
		}
	}

	const std::regex numberedStepTargetRegex(
		R"((?:^|\n|\r|;|\xEF\xBC\x9B)\s*(?:step\s*)?\d+\s*[\)\.:\-]\s*([A-Za-z0-9._-]+))",
		std::regex_constants::icase);
	for (std::sregex_iterator it(message.begin(), message.end(), numberedStepTargetRegex), end;
		it != end;
		++it) {
		if (it->size() >= 2) {
			addTarget((*it)[1].str());
		}
	}

	if (message.find("->") != std::string::npos) {
		std::size_t cursor = 0;
		while (cursor < message.size()) {
			const std::size_t arrow = message.find("->", cursor);
			if (arrow == std::string::npos) {
				break;
			}

			const std::size_t leftBoundary =
				message.rfind(' ', arrow) == std::string::npos
				? 0
				: message.rfind(' ', arrow) + 1;
			const std::size_t rightBoundary =
				message.find_first_of(" \n\r\t", arrow + 2);
			const std::size_t rightEnd = rightBoundary == std::string::npos
				? message.size()
				: rightBoundary;

			if (arrow > leftBoundary) {
				addTarget(message.substr(leftBoundary, arrow - leftBoundary));
			}
			if (rightEnd > arrow + 2) {
				addTarget(message.substr(arrow + 2, rightEnd - (arrow + 2)));
			}

			cursor = arrow + 2;
		}
	}

	if (explicitCallTargets != nullptr) {
		*explicitCallTargets = explicitTargets;
	}

	return targets;
}

bool HasStructuralSequenceSignal(const std::string& message) {
	const std::regex backtickTargetRegex(
		R"(`([A-Za-z0-9._-]+)`)",
		std::regex_constants::icase);
	std::size_t backtickTargetCount = 0;
	for (std::sregex_iterator it(message.begin(), message.end(), backtickTargetRegex), end;
		it != end;
		++it) {
		++backtickTargetCount;
		if (backtickTargetCount >= 2) {
			return true;
		}
	}

	if (message.find("->") != std::string::npos) {
		return true;
	}

	const std::regex numberedStepRegex(
		R"((?:^|\n|\r|;|\xEF\xBC\x9B)\s*(?:step\s*)?\d+\s*[\)\.:\-])",
		std::regex_constants::icase);
	std::size_t numberedStepCount = 0;
	for (std::sregex_iterator it(message.begin(), message.end(), numberedStepRegex), end;
		it != end;
		++it) {
		++numberedStepCount;
		if (numberedStepCount >= 2) {
			return true;
		}
	}

	const std::regex callDirectiveRegex(
		R"(\bcall\s+[A-Za-z0-9._-]+)",
		std::regex_constants::icase);
	std::size_t callDirectiveCount = 0;
	for (std::sregex_iterator it(message.begin(), message.end(), callDirectiveRegex), end;
		it != end;
		++it) {
		++callDirectiveCount;
		if (callDirectiveCount >= 2) {
			return true;
		}
	}

	return false;
}

std::string ResolveOrderedTargetToToolId(
	const std::string& target,
	const std::vector<ToolCatalogEntry>& tools,
	const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries) {
	if (target.empty()) {
		return {};
	}

	const std::string normalizedTarget = ToLowerCopyLocal(target);
	std::string normalizedNamespace = normalizedTarget;
	std::replace(
		normalizedNamespace.begin(),
		normalizedNamespace.end(),
		'-',
		'_');

	for (const auto& tool : tools) {
		const std::string toolIdLower = ToLowerCopyLocal(tool.id);
		if (toolIdLower == normalizedTarget) {
			return tool.id;
		}

		if (toolIdLower == normalizedTarget + ".search.web") {
			return tool.id;
		}

		if (toolIdLower == normalizedTarget + ".smtp.send") {
			return tool.id;
		}
	}

	const std::string preferredByNamespace =
		ResolvePreferredToolForNamespace(normalizedNamespace, tools);
	if (!preferredByNamespace.empty()) {
		return preferredByNamespace;
	}

	for (const auto& entry : skillsCatalogEntries) {
		const std::string nameLower = ToLowerCopyLocal(entry.name);
		const std::string keyLower = ToLowerCopyLocal(entry.skillKey);
		const std::string commandLower = ToLowerCopyLocal(entry.commandName);
		const std::string commandToolLower = ToLowerCopyLocal(entry.commandToolName);

		if (nameLower == normalizedTarget ||
			keyLower == normalizedTarget ||
			commandLower == normalizedTarget ||
			commandToolLower == normalizedTarget) {
			if (!entry.commandToolName.empty()) {
				return entry.commandToolName;
			}

			std::string entryNamespace = keyLower.empty()
				? nameLower
				: keyLower;
			std::replace(
				entryNamespace.begin(),
				entryNamespace.end(),
				'-',
				'_');
			const std::string resolvedBySkill =
				ResolvePreferredToolForNamespace(entryNamespace, tools);
			if (!resolvedBySkill.empty()) {
				return resolvedBySkill;
			}

			const bool modelInvocationAllowed =
				entry.eligible &&
				!entry.disabled &&
				!entry.blockedByAllowlist &&
				!entry.disableModelInvocation;
			if (modelInvocationAllowed) {
				return std::string("model_skill.") +
					(entry.skillKey.empty() ? entry.name : entry.skillKey);
			}

			return {};
		}
	}

	return {};
}

OrderedSequencePreflight BuildOrderedSequencePreflight(
	const std::string& message,
	const std::vector<ToolCatalogEntry>& tools,
	const std::vector<SkillsCatalogGatewayEntry>& skillsCatalogEntries) {
	OrderedSequencePreflight preflight;
	std::vector<std::string> inferredTargets;
	preflight.explicitCallTargets.clear();
	inferredTargets = ExtractOrderedTargetsFromPrompt(
		message,
		&preflight.explicitCallTargets);

	if (!preflight.explicitCallTargets.empty()) {
		preflight.orderedTargets = preflight.explicitCallTargets;
		preflight.strictAllowlist = true;
		preflight.enforced = true;
	}
	else {
		if (!HasStructuralSequenceSignal(message)) {
			return preflight;
		}

		preflight.orderedTargets = std::move(inferredTargets);
		preflight.strictAllowlist = false;
		preflight.enforced = preflight.orderedTargets.size() >= 2;
	}

	preflight.resolvedToolTargets.reserve(preflight.orderedTargets.size());
	if (!preflight.enforced) {
		return preflight;
	}

	for (const auto& target : preflight.orderedTargets) {
		const std::string resolvedTool = ResolveOrderedTargetToToolId(
			target,
			tools,
			skillsCatalogEntries);
		preflight.resolvedToolTargets.push_back(resolvedTool);
		if (resolvedTool.empty()) {
			preflight.missingTargets.push_back(target);
			std::string normalizedTarget = NormalizeOrderedTargetToken(target);
			std::replace(normalizedTarget.begin(), normalizedTarget.end(), '-', '_');
			if (!normalizedTarget.empty()) {
				if (normalizedTarget.find('.') == std::string::npos) {
					preflight.missingResolvedToolTargets.push_back(normalizedTarget + ".generate");
					preflight.missingResolvedToolTargets.push_back(normalizedTarget + ".edit");
				}
				else {
					preflight.missingResolvedToolTargets.push_back(normalizedTarget);
				}
			}
		}
	}

	return preflight;
}

std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
BuildOrderedPreflightTaskDeltas(
	const std::string& runId,
	const std::string& sessionKey,
	const OrderedSequencePreflight& preflight,
	const bool terminalFailure,
	const std::string& terminalErrorCode,
	const std::string& terminalErrorMessage) {
	std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> taskDeltas;
	if (!preflight.enforced) {
		return taskDeltas;
	}

	const std::uint64_t baseMs = CurrentEpochMsLocal();
	taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
		.index = taskDeltas.size(),
		.runId = runId,
		.sessionId = sessionKey,
		.phase = "plan",
		.resultJson = SerializeStringArrayLocal(preflight.orderedTargets),
		.status = "ok",
		.startedAtMs = baseMs,
		.completedAtMs = baseMs,
		.latencyMs = 0,
		.stepLabel = "ordered_execution_plan",
		});

	for (std::size_t index = 0; index < preflight.orderedTargets.size(); ++index) {
		const std::string& target = preflight.orderedTargets[index];
		const std::string resolvedTarget =
			index < preflight.resolvedToolTargets.size() &&
			!preflight.resolvedToolTargets[index].empty()
			? preflight.resolvedToolTargets[index]
			: target;
		const bool missing = std::find(
			preflight.missingTargets.begin(),
			preflight.missingTargets.end(),
			target) != preflight.missingTargets.end();

		taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
			.index = taskDeltas.size(),
			.runId = runId,
			.sessionId = sessionKey,
			.phase = "preflight",
			.toolName = resolvedTarget,
			.argsJson = target,
			.status = missing ? "missing" : "ok",
			.errorCode = missing ? "step_target_unavailable" : std::string(),
			.startedAtMs = baseMs + index + 1,
			.completedAtMs = baseMs + index + 1,
			.latencyMs = 0,
			.stepLabel = "ordered_step_precheck",
			});
	}

	if (terminalFailure) {
		taskDeltas.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
			.index = taskDeltas.size(),
			.runId = runId,
			.sessionId = sessionKey,
			.phase = "final",
			.resultJson = terminalErrorMessage,
			.status = "failed",
			.errorCode = terminalErrorCode,
			.startedAtMs = baseMs + preflight.orderedTargets.size() + 1,
			.completedAtMs = baseMs + preflight.orderedTargets.size() + 1,
			.latencyMs = 0,
			.stepLabel = "run_terminal",
			});
	}

	return taskDeltas;
}

std::string JoinOrderedTargets(const std::vector<std::string>& targets) {
	std::string joined;
	for (std::size_t i = 0; i < targets.size(); ++i) {
		if (i > 0) {
			joined += ", ";
		}

		joined += targets[i];
	}

	return joined;
}

std::string BuildOrderedStepPreflightLabel(const std::string& resolvedTarget) {
	if (resolvedTarget.rfind("model_skill.", 0) == 0) {
		return "ordered_step_precheck_model";
	}

	return "ordered_step_precheck";
}

bool EndsWithLocal(
	const std::string& value,
	const std::string& suffix) {
	if (value.size() < suffix.size()) {
		return false;
	}

	return value.compare(
		value.size() - suffix.size(),
		suffix.size(),
		suffix) == 0;
}

bool IsResolvedRuntimeToolTarget(
	const std::string& resolvedToolId,
	const std::vector<ToolCatalogEntry>& runtimeTools) {
	if (resolvedToolId.empty()) {
		return false;
	}

	const std::string lowered = ToLowerCopyLocal(resolvedToolId);
	if (lowered.rfind("model_skill.", 0) == 0) {
		return false;
	}

	for (const auto& tool : runtimeTools) {
		if (!tool.enabled) {
			continue;
		}

		if (ToLowerCopyLocal(tool.id) == lowered) {
			return true;
		}
	}

	return false;
}

bool IsInvalidArgumentsResult(
	const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
	if (delta.phase != "tool_result") {
		return false;
	}

	const std::string status = ToLowerCopyLocal(delta.status);
	const std::string errorCode = ToLowerCopyLocal(delta.errorCode);
	return status == "invalid_arguments" ||
		status == "invalid_args" ||
		errorCode == "invalid_arguments" ||
		errorCode == "invalid_args";
}

std::string ExtractFirstHttpUrl(
	const std::string& text) {
	static const std::regex kHttpRegex(
		R"((https?://[^\s\)\]\>\"]+))",
		std::regex_constants::icase);
	std::smatch match;
	if (std::regex_search(text, match, kHttpRegex) && !match.empty()) {
		return match[1].str();
	}

	return {};
}

bool TryBuildRecoveredArgsJson(
	const std::string& toolId,
	const std::string& message,
	std::string& outArgsJson) {
	outArgsJson.clear();
	const std::string trimmedMessage = json::Trim(message);
	if (trimmedMessage.empty()) {
		return false;
	}

	const std::string lowerTool = ToLowerCopyLocal(toolId);
	if (EndsWithLocal(lowerTool, ".search.web")) {
		const auto compactQuery = DeriveCompactSearchQueryLocal(trimmedMessage);
		if (!compactQuery.has_value()) {
			return false;
		}

		outArgsJson =
			"{\"query\":" + JsonString(compactQuery.value()) +
			",\"count\":5}";
		return true;
	}

	if (EndsWithLocal(lowerTool, ".fetch.content")) {
		const std::string firstUrl = ExtractFirstHttpUrl(trimmedMessage);
		if (firstUrl.empty()) {
			return false;
		}

		outArgsJson = "{\"url\":" + JsonString(firstUrl) + "}";
		return true;
	}

	if (EndsWithLocal(lowerTool, ".smtp.send")) {
		const std::string recipient = ExtractFirstEmailAddress(trimmedMessage);
		if (recipient.empty()) {
			return false;
		}

		outArgsJson =
			"{\"to\":" + JsonString(recipient) +
			",\"subject\":\"Preview\",\"body\":" +
			JsonString(trimmedMessage) + "}";
		return true;
	}

	return false;
}

std::string SkillNamespaceOfToolId(const std::string& toolId) {
	const auto dot = toolId.find('.');
	if (dot == std::string::npos || dot == 0) {
		return {};
	}

	return toolId.substr(0, dot);
}

int IntentSimilarityScore(
	const std::string& loweredMessage,
	const ToolCatalogEntry& tool,
	const std::string& referenceCategory,
	const std::string& referenceNamespace) {
	int score = 0;
	const std::string toolIdLower = ToLowerCopyLocal(tool.id);
	const std::string toolLabelLower = ToLowerCopyLocal(tool.label);
	const std::string toolCategoryLower = ToLowerCopyLocal(tool.category);

	if (!referenceCategory.empty() &&
		toolCategoryLower == referenceCategory) {
		score += 2;
	}

	if (!referenceNamespace.empty() &&
		toolIdLower.rfind(referenceNamespace + ".", 0) == 0) {
		score += 3;
	}

	if (loweredMessage.find("search") != std::string::npos &&
		toolIdLower.find("search") != std::string::npos) {
		score += 2;
	}

	if ((loweredMessage.find("email") != std::string::npos ||
		loweredMessage.find("mail") != std::string::npos) &&
		(toolIdLower.find("smtp") != std::string::npos ||
			toolIdLower.find("imap") != std::string::npos ||
			toolLabelLower.find("email") != std::string::npos)) {
		score += 2;
	}

	if (loweredMessage.find("fetch") != std::string::npos &&
		toolIdLower.find("fetch") != std::string::npos) {
		score += 1;
	}

	return score;
}

std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>
ApplyInvalidArgumentsRecoveryPolicy(
	const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& source,
	const std::string& runId,
	const std::string& sessionKey,
	const std::string& message,
	GatewayToolRegistry& toolRegistry) {
	if (source.empty()) {
		return source;
	}

	auto recovered = source;
	auto invalidIt = std::find_if(
		recovered.begin(),
		recovered.end(),
		[](const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& delta) {
			return IsInvalidArgumentsResult(delta);
		});
	if (invalidIt == recovered.end() || invalidIt->toolName.empty()) {
		return recovered;
	}

	const auto allTools = toolRegistry.List();
	const std::string failedTool = invalidIt->toolName;
	const std::string failedToolLower = ToLowerCopyLocal(failedTool);
	const std::string failedNamespace = SkillNamespaceOfToolId(failedToolLower);
	std::string failedCategory;
	for (const auto& tool : allTools) {
		if (ToLowerCopyLocal(tool.id) == failedToolLower) {
			failedCategory = ToLowerCopyLocal(tool.category);
			break;
		}
	}

	auto appendAttempt =
		[&](const std::string& toolId,
			const std::string& action,
			const std::size_t attempt,
			const std::size_t maxAttempts,
			const std::string& argsJson,
			const ToolExecuteResult& execution) {
				const std::uint64_t nowMs = CurrentEpochMsLocal();
				recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = recovered.size(),
					.runId = runId,
					.sessionId = sessionKey,
					.phase = "tool_call",
					.toolName = toolId,
					.fallbackAction = action,
					.fallbackAttempt = attempt,
					.fallbackMaxAttempts = maxAttempts,
					.argsJson = argsJson,
					.status = "requested",
					.startedAtMs = nowMs,
					.completedAtMs = nowMs,
					.latencyMs = 0,
					.stepLabel = "tool_retry_request",
					});

				const std::string resultStatus = execution.status.empty()
					? (execution.executed ? "ok" : "error")
					: execution.status;
				const std::string resultErrorCode =
					ToLowerCopyLocal(resultStatus) == "ok"
					? std::string{}
					: (ToLowerCopyLocal(resultStatus) == "invalid_args"
						? std::string("invalid_arguments")
						: resultStatus);
				recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
					.index = recovered.size(),
					.runId = runId,
					.sessionId = sessionKey,
					.phase = "tool_result",
					.toolName = toolId,
					.fallbackAction = action,
					.fallbackAttempt = attempt,
					.fallbackMaxAttempts = maxAttempts,
					.argsJson = argsJson,
					.resultJson = execution.output,
					.status = resultStatus,
					.errorCode = resultErrorCode,
					.startedAtMs = nowMs,
					.completedAtMs = nowMs,
					.latencyMs = 0,
					.stepLabel = "tool_retry_result",
					});
		};

	auto tryExecuteRecovered =
		[&](const std::string& toolId,
			const std::string& action,
			const std::size_t attempt,
			const std::size_t maxAttempts) {
				std::string rebuiltArgsJson;
				if (!TryBuildRecoveredArgsJson(toolId, message, rebuiltArgsJson)) {
					return false;
				}

				const ToolExecuteResult execution = toolRegistry.Execute(
					toolId,
					rebuiltArgsJson);
				appendAttempt(
					toolId,
					action,
					attempt,
					maxAttempts,
					rebuiltArgsJson,
					execution);

				const std::string statusLower = ToLowerCopyLocal(execution.status);
				return execution.executed &&
					statusLower != "error" &&
					statusLower != "invalid_args" &&
					statusLower != "invalid_arguments";
		};

	if (tryExecuteRecovered(
		failedTool,
		"args_rebuild_retry",
		1,
		1)) {
		return recovered;
	}

	std::size_t sameSkillAttempt = 1;
	for (const auto& tool : allTools) {
		const std::string toolIdLower = ToLowerCopyLocal(tool.id);
		if (!failedNamespace.empty() &&
			toolIdLower.rfind(failedNamespace + ".", 0) != 0) {
			continue;
		}

		if (toolIdLower == failedToolLower || !tool.enabled) {
			continue;
		}

		if (tryExecuteRecovered(
			tool.id,
			"same_skill_candidate_retry",
			sameSkillAttempt,
			2)) {
			return recovered;
		}

		++sameSkillAttempt;
		if (sameSkillAttempt > 2) {
			break;
		}
	}

	const std::string loweredMessage = ToLowerCopyLocal(message);
	const ToolCatalogEntry* bestCandidate = nullptr;
	int bestScore = 0;
	for (const auto& tool : allTools) {
		const std::string toolIdLower = ToLowerCopyLocal(tool.id);
		if (!tool.enabled || toolIdLower == failedToolLower) {
			continue;
		}

		std::string candidateArgs;
		if (!TryBuildRecoveredArgsJson(tool.id, message, candidateArgs)) {
			continue;
		}

		const int score = IntentSimilarityScore(
			loweredMessage,
			tool,
			failedCategory,
			failedNamespace);
		if (score > bestScore) {
			bestScore = score;
			bestCandidate = &tool;
		}
	}

	if (bestCandidate != nullptr && bestScore > 0) {
		(void)tryExecuteRecovered(
			bestCandidate->id,
			"cross_skill_guarded_retry",
			1,
			1);
	}
	else {
		const std::uint64_t nowMs = CurrentEpochMsLocal();
		recovered.push_back(GatewayHost::ChatRuntimeResult::TaskDeltaEntry{
			.index = recovered.size(),
			.runId = runId,
			.sessionId = sessionKey,
			.phase = "fallback",
			.toolName = failedTool,
			.fallbackAction = "cross_skill_guarded_retry",
			.status = "skipped",
			.errorCode = "cross_skill_fallback_not_eligible",
			.startedAtMs = nowMs,
			.completedAtMs = nowMs,
			.latencyMs = 0,
			.stepLabel = "fallback_gate",
			});
	}

	return recovered;
}

std::string JoinOrderedResolution(
	const OrderedSequencePreflight& preflight) {
	std::string joined;
	for (std::size_t i = 0; i < preflight.orderedTargets.size(); ++i) {
		if (i > 0) {
			joined += ", ";
		}

		const std::string requested = preflight.orderedTargets[i];
		const std::string resolved =
			i < preflight.resolvedToolTargets.size()
			? preflight.resolvedToolTargets[i]
			: std::string{};
		if (!resolved.empty() && resolved != requested) {
			joined += requested + "=>" + resolved;
		}
		else {
			joined += requested;
		}
	}

	return joined;
}

std::string SerializeFloatArrayLocal(
	const std::vector<float>& values) {
	std::ostringstream output;
	output.setf(std::ios::fixed);
	output.precision(6);
	output << "[";
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i > 0) {
			output << ",";
		}

		output << values[i];
	}
	output << "]";
	return output.str();
}

std::string SerializeFloatMatrixLocal(
	const std::vector<std::vector<float>>& vectors) {
	std::string output = "[";
	for (std::size_t i = 0; i < vectors.size(); ++i) {
		if (i > 0) {
			output += ",";
		}

		output += SerializeFloatArrayLocal(vectors[i]);
	}

	output += "]";
	return output;
}
