#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"

namespace blazeclaw::gateway {

	namespace {

		std::string TrimCopy(const std::string& value) {
			std::size_t start = 0;
			std::size_t end = value.size();
			while (start < end && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
				++start;
			}
			while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
				--end;
			}
			return value.substr(start, end - start);
		}

		std::vector<std::string> ParseStringArrayField(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return {};
			}

			std::string raw;
			if (!json::FindRawField(paramsJson.value(), fieldName, raw)) {
				return {};
			}

			const std::string trimmed = json::Trim(raw);
			if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']') {
				return {};
			}

			std::vector<std::string> values;
			std::size_t index = 1;
			while (index < trimmed.size() - 1) {
				index = json::SkipWhitespace(trimmed, index);
				if (index >= trimmed.size() - 1) {
					break;
				}
				if (trimmed[index] == ',') {
					++index;
					continue;
				}

				std::string value;
				if (!json::ParseJsonStringAt(trimmed, index, value)) {
					break;
				}
				if (!TrimCopy(value).empty()) {
					values.push_back(value);
				}

				index = json::SkipWhitespace(trimmed, index);
				if (index < trimmed.size() && trimmed[index] == ',') {
					++index;
				}
			}

			return values;
		}

		std::unordered_map<std::string, bool> ParseBooleanMapField(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return {};
			}

			std::string raw;
			if (!json::FindRawField(paramsJson.value(), fieldName, raw)) {
				return {};
			}

			const std::string trimmed = json::Trim(raw);
			if (!json::IsJsonObjectShape(trimmed)) {
				return {};
			}

			std::unordered_map<std::string, bool> values;
			std::size_t index = 1;
			while (index < trimmed.size() - 1) {
				index = json::SkipWhitespace(trimmed, index);
				if (index >= trimmed.size() - 1 || trimmed[index] == '}') {
					break;
				}
				if (trimmed[index] == ',') {
					++index;
					continue;
				}

				std::string key;
				if (!json::ParseJsonStringAt(trimmed, index, key)) {
					break;
				}

				index = json::SkipWhitespace(trimmed, index);
				if (index >= trimmed.size() || trimmed[index] != ':') {
					break;
				}
				++index;
				index = json::SkipWhitespace(trimmed, index);
				if (index >= trimmed.size()) {
					break;
				}

				bool parsed = false;
				if (trimmed.compare(index, 4, "true") == 0) {
					values.insert_or_assign(key, true);
					index += 4;
					parsed = true;
				}
				else if (trimmed.compare(index, 5, "false") == 0) {
					values.insert_or_assign(key, false);
					index += 5;
					parsed = true;
				}
				if (!parsed) {
					break;
				}

				index = json::SkipWhitespace(trimmed, index);
				if (index < trimmed.size() && trimmed[index] == ',') {
					++index;
				}
			}

			return values;
		}

		std::string SerializeStringArrayJson(const std::vector<std::string>& values) {
			std::vector<std::string> rows;
			rows.reserve(values.size());
			for (const auto& value : values) {
				rows.push_back(JsonString(value));
			}
			return JsonArray(rows);
		}

		std::string SerializePermissionsJson(const std::unordered_map<std::string, bool>& permissions) {
			std::vector<std::string> keys;
			keys.reserve(permissions.size());
			for (const auto& [key, _] : permissions) {
				keys.push_back(key);
			}
			std::sort(keys.begin(), keys.end());

			std::vector<std::pair<const char*, std::string>> fields;
			std::string out = "{";
			bool first = true;
			for (const auto& key : keys) {
				const auto it = permissions.find(key);
				if (it == permissions.end()) {
					continue;
				}
				if (!first) {
					out += ",";
				}
				first = false;
				out += JsonString(key);
				out += ":";
				out += JsonBool(it->second);
			}
			out += "}";
			return out;
		}

		NodePairingDeclaredSurface BuildDeclaredSurfaceFromRequest(const protocol::RequestFrame& request) {
			RequestParamsView params(request.paramsJson);
			NodePairingDeclaredSurface declared;
			declared.nodeId = TrimCopy(params.GetString("nodeId"));
			declared.displayName = TrimCopy(params.GetString("displayName"));
			declared.platform = TrimCopy(params.GetString("platform"));
			declared.version = TrimCopy(params.GetString("version"));
			declared.coreVersion = TrimCopy(params.GetString("coreVersion"));
			declared.uiVersion = TrimCopy(params.GetString("uiVersion"));
			declared.deviceFamily = TrimCopy(params.GetString("deviceFamily"));
			declared.modelIdentifier = TrimCopy(params.GetString("modelIdentifier"));
			declared.remoteIp = TrimCopy(params.GetString("remoteIp"));
			declared.caps = ParseStringArrayField(request.paramsJson, "caps");
			declared.commands = ParseStringArrayField(request.paramsJson, "commands");
			declared.permissions = ParseBooleanMapField(request.paramsJson, "permissions");
			return declared;
		}

		std::string SerializeDeclaredSurfaceJson(const NodePairingDeclaredSurface& declared) {
			return JsonObject({
				{"nodeId", JsonString(declared.nodeId)},
				{"displayName", JsonString(declared.displayName)},
				{"platform", JsonString(declared.platform)},
				{"version", JsonString(declared.version)},
				{"coreVersion", JsonString(declared.coreVersion)},
				{"uiVersion", JsonString(declared.uiVersion)},
				{"deviceFamily", JsonString(declared.deviceFamily)},
				{"modelIdentifier", JsonString(declared.modelIdentifier)},
				{"caps", SerializeStringArrayJson(declared.caps)},
				{"commands", SerializeStringArrayJson(declared.commands)},
				{"permissions", SerializePermissionsJson(declared.permissions)},
				{"remoteIp", JsonString(declared.remoteIp)},
				});
		}

		std::string SerializePendingRequestJson(const NodePairingPendingRequest& request) {
			return JsonObject({
				{"requestId", JsonString(request.requestId)},
				{"nodeId", JsonString(request.declared.nodeId)},
				{"displayName", JsonString(request.declared.displayName)},
				{"platform", JsonString(request.declared.platform)},
				{"version", JsonString(request.declared.version)},
				{"coreVersion", JsonString(request.declared.coreVersion)},
				{"uiVersion", JsonString(request.declared.uiVersion)},
				{"deviceFamily", JsonString(request.declared.deviceFamily)},
				{"modelIdentifier", JsonString(request.declared.modelIdentifier)},
				{"caps", SerializeStringArrayJson(request.declared.caps)},
				{"commands", SerializeStringArrayJson(request.declared.commands)},
				{"permissions", SerializePermissionsJson(request.declared.permissions)},
				{"remoteIp", JsonString(request.declared.remoteIp)},
				{"silent", JsonBool(request.silent)},
				{"ts", JsonNumber(request.tsMs)},
				{"requiredApproveScopes", SerializeStringArrayJson(request.requiredApproveScopes)},
				});
		}

		std::string SerializePairedNodeJson(const NodePairingPairedNode& node) {
			return JsonObject({
				{"nodeId", JsonString(node.declared.nodeId)},
				{"token", JsonString(node.token)},
				{"displayName", JsonString(node.declared.displayName)},
				{"platform", JsonString(node.declared.platform)},
				{"version", JsonString(node.declared.version)},
				{"coreVersion", JsonString(node.declared.coreVersion)},
				{"uiVersion", JsonString(node.declared.uiVersion)},
				{"deviceFamily", JsonString(node.declared.deviceFamily)},
				{"modelIdentifier", JsonString(node.declared.modelIdentifier)},
				{"caps", SerializeStringArrayJson(node.declared.caps)},
				{"commands", SerializeStringArrayJson(node.declared.commands)},
				{"permissions", SerializePermissionsJson(node.declared.permissions)},
				{"remoteIp", JsonString(node.declared.remoteIp)},
				{"createdAtMs", JsonNumber(node.createdAtMs)},
				{"approvedAtMs", JsonNumber(node.approvedAtMs)},
				});
		}

		std::string SerializeKnownNodeJson(const KnownNodeSnapshot& node) {
			return JsonObject({
				{"nodeId", JsonString(node.nodeId)},
				{"displayName", JsonString(node.displayName)},
				{"platform", JsonString(node.platform)},
				{"version", JsonString(node.version)},
				{"coreVersion", JsonString(node.coreVersion)},
				{"uiVersion", JsonString(node.uiVersion)},
				{"deviceFamily", JsonString(node.deviceFamily)},
				{"modelIdentifier", JsonString(node.modelIdentifier)},
				{"caps", SerializeStringArrayJson(node.caps)},
				{"commands", SerializeStringArrayJson(node.commands)},
				{"pairedVia", SerializeStringArrayJson(node.pairedVia)},
				{"connected", JsonBool(node.connected)},
				});
		}

		std::vector<std::string> CollectCallerScopes(const protocol::RequestFrame& request) {
			auto scopes = ParseStringArrayField(request.paramsJson, "callerScopes");
			if (!scopes.empty()) {
				return scopes;
			}
			return ParseStringArrayField(request.paramsJson, "scopes");
		}

		bool IsForbiddenBrowserProxyMutation(const protocol::RequestFrame& request) {
			std::string method;
			std::string path;
			if (request.paramsJson.has_value()) {
				json::FindStringField(request.paramsJson.value(), "method", method);
				json::FindStringField(request.paramsJson.value(), "path", path);
			}

			method = TrimCopy(method);
			path = TrimCopy(path);
			std::transform(
				method.begin(),
				method.end(),
				method.begin(),
				[](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

			if (!path.empty() && path.front() != '/') {
				path = "/" + path;
			}
			while (path.size() > 1 && path.back() == '/') {
				path.pop_back();
			}

			if (method == "POST" && (path == "/profiles/create" || path == "/reset-profile")) {
				return true;
			}

			if (method == "DELETE") {
				const std::string prefix = "/profiles/";
				if (path.rfind(prefix, 0) == 0 && path.size() > prefix.size()) {
					return path.find('/', prefix.size()) == std::string::npos;
				}
			}

			return false;
		}

		std::vector<std::string> ResolveDeclaredCommandsFromRequest(const protocol::RequestFrame& request) {
			const std::vector<std::string> commands = ParseStringArrayField(request.paramsJson, "declaredCommands");
			if (!commands.empty()) {
				return commands;
			}
			return ParseStringArrayField(request.paramsJson, "commands");
		}

		std::string ResolveOptionalObjectJson(
			const std::optional<std::string>& paramsJson,
			const std::string& fieldName) {
			if (!paramsJson.has_value()) {
				return "{}";
			}
			std::string raw;
			if (!json::FindRawField(paramsJson.value(), fieldName, raw)) {
				return "{}";
			}
			if (!json::IsJsonObjectShape(raw)) {
				return "{}";
			}
			return json::Trim(raw);
		}

		bool IsForegroundRestrictedIosCommand(const std::string& command) {
			return command == "canvas.present" ||
				command == "canvas.navigate" ||
				command.rfind("canvas.", 0) == 0 ||
				command.rfind("camera.", 0) == 0 ||
				command.rfind("screen.", 0) == 0 ||
				command.rfind("talk.", 0) == 0;
		}

		bool ShouldQueueAsPendingForegroundAction(
			const std::string& platform,
			const std::string& command,
			const std::string& errorCode,
			const std::string& errorMessage) {
			std::string normalizedPlatform = platform;
			std::transform(
				normalizedPlatform.begin(),
				normalizedPlatform.end(),
				normalizedPlatform.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			if (normalizedPlatform.rfind("ios", 0) != 0 && normalizedPlatform.rfind("ipados", 0) != 0) {
				return false;
			}
			if (!IsForegroundRestrictedIosCommand(command)) {
				return false;
			}

			std::string normalizedCode = errorCode;
			std::transform(
				normalizedCode.begin(),
				normalizedCode.end(),
				normalizedCode.begin(),
				[](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
			std::string normalizedMessage = errorMessage;
			std::transform(
				normalizedMessage.begin(),
				normalizedMessage.end(),
				normalizedMessage.begin(),
				[](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });

			return normalizedCode == "NODE_BACKGROUND_UNAVAILABLE" ||
				normalizedMessage.find("BACKGROUND_UNAVAILABLE") != std::string::npos;
		}

		void EmitBestEffortEvent(
			GatewayHost& host,
			const std::string& eventName,
			const std::string& payloadJson) {
			(void)host;
			EmitTelemetryEvent(
				"gateway.node.event." + eventName,
				JsonObject({
					{"event", JsonString(eventName)},
					{"payload", payloadJson},
					}));
		}

		void RegisterMethodAlias(
			GatewayMethodDispatcher& dispatcher,
			const std::string& aliasMethod,
			const std::string& targetMethod)
		{
			dispatcher.Register(
				aliasMethod,
				[&dispatcher, targetMethod](const protocol::RequestFrame& request) {
					auto forwarded = request;
					forwarded.method = targetMethod;
					return dispatcher.Dispatch(forwarded);
				});
		}

		void RegisterStaticMethod(
			GatewayMethodDispatcher& dispatcher,
			const std::string& method,
			const std::string& payloadJson)
		{
			dispatcher.Register(
				method,
				[payloadJson](const protocol::RequestFrame& request) {
					return protocol::OkResponse(request, payloadJson);
				});
		}

	} // namespace

	void GatewayHost::RegisterSecurityOpsHandlers() {
		m_dispatcher.Register("gateway.nodes.voice.capabilities", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"wakeWord\":true,\"pushToTalk\":true,\"handsFree\":false,\"languages\":[\"en-US\"],\"count\":1}");
			});

		m_dispatcher.Register(
			"gateway.nodes.voice.streamScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId\":\"voice.streamScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.voice.sequenceScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId\":\"voice.sequenceScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.camera.streamScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId\":\"camera.streamScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.voice.pointerScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pointerScopeId\":\"voice.pointerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.notifications.streamScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId\":\"notifications.streamScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.camera.sequenceScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId\":\"camera.sequenceScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.templateScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"templateScopeId2\":\"logging.templateScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.revisionScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"revisionScopeId2\":\"logging.revisionScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.historyScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"historyScopeId2\":\"logging.historyScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.snapshotScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"snapshotScopeId2\":\"logging.snapshotScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.indexScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"indexScopeId2\":\"logging.indexScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.windowScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"windowScopeId2\":\"logging.windowScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.capabilities", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"still\":true,\"video\":false,\"maxWidth\":1920,\"maxHeight\":1080}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.templateScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"templateScopeId2\":\"diagnostics.templateScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.revisionScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"revisionScopeId2\":\"diagnostics.revisionScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.historyScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"historyScopeId2\":\"diagnostics.historyScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.snapshotScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"snapshotScopeId2\":\"diagnostics.snapshotScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.indexScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"indexScopeId2\":\"diagnostics.indexScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.windowScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"windowScopeId2\":\"diagnostics.windowScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.notifications.sequenceScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId\":\"notifications.sequenceScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.canvas.streamScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId\":\"canvas.streamScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.camera.pointerScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pointerScopeId\":\"camera.pointerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.platform.cli.streamScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId\":\"cli.streamScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.sequenceScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId3\":\"logging.sequenceScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.platform.web.streamScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId\":\"web.streamScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.streamScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId3\":\"logging.streamScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.bundleScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bundleScopeId3\":\"logging.bundleScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.packageScopeId4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"packageScopeId4\":\"logging.packageScopeId4.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.archiveScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"archiveScopeId3\":\"logging.archiveScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.manifestScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"manifestScopeId2\":\"logging.manifestScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.channels", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"channels\":[\"desktop\"],\"locationAware\":false,\"count\":1}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.sequenceScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId3\":\"diagnostics.sequenceScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.streamScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId3\":\"diagnostics.streamScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.bundleScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bundleScopeId3\":\"diagnostics.bundleScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.packageScopeId4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"packageScopeId4\":\"diagnostics.packageScopeId4.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.archiveScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"archiveScopeId3\":\"diagnostics.archiveScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.manifestScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"manifestScopeId2\":\"diagnostics.manifestScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.notifications.pointerScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pointerScopeId\":\"notifications.pointerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.canvas.sequenceScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId\":\"canvas.sequenceScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.access.entries", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"entries\":[],\"count\":0,\"mode\":\"allowlist\",\"source\":\"runtime\"}");
			});

		m_dispatcher.Register(
			"gateway.platform.cli.sequenceScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId\":\"cli.sequenceScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.pointerScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pointerScopeId2\":\"logging.pointerScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.platform.web.sequenceScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId\":\"web.sequenceScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.tokenScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tokenScopeId2\":\"logging.tokenScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.sequenceScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId2\":\"logging.sequenceScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.streamScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId2\":\"logging.streamScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.bundleScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bundleScopeId2\":\"logging.bundleScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.packageScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"packageScopeId3\":\"logging.packageScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.ops.doctor.run.preview", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"runId\":\"doctor-preview-1\",\"checks\":[\"transport\",\"session\",\"routing\"],\"count\":3,\"preview\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.pointerScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pointerScopeId2\":\"diagnostics.pointerScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.tokenScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tokenScopeId2\":\"diagnostics.tokenScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.sequenceScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sequenceScopeId2\":\"diagnostics.sequenceScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.streamScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"streamScopeId2\":\"diagnostics.streamScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.bundleScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bundleScopeId2\":\"diagnostics.bundleScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.packageScopeId3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"packageScopeId3\":\"diagnostics.packageScopeId3.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":false,\"wakeWord\":\"blaze\",\"talkMode\":\"push_to_talk\"}");
			});

		m_dispatcher.Register(
			"gateway.nodes.canvas.pointerScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pointerScopeId\":\"canvas.pointerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.devices", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"devices\":[\"default-mic\"],\"activeDevice\":\"default-mic\",\"count\":1}");
			});

		m_dispatcher.Register(
			"gateway.platform.cli.pointerScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pointerScopeId\":\"cli.pointerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.permissions", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"microphone\":false,\"hotword\":false,\"granted\":false}");
			});

		m_dispatcher.Register(
			"gateway.platform.web.pointerScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pointerScopeId\":\"web.pointerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.routing", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"route\":\"local\",\"fallback\":\"push_to_talk\",\"priority\":1}");
			});

		m_dispatcher.Register("gateway.nodes.voice.latency", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
			});

		m_dispatcher.Register("gateway.nodes.voice.health", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}");
			});

		m_dispatcher.Register("gateway.nodes.voice.metrics", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}");
			});

		m_dispatcher.Register("gateway.nodes.voice.profile", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profile\":\"default\",\"mode\":\"push_to_talk\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"voice.window.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"voice.token.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"voice.scope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"voice.state.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"voice.windowScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"voice.cursorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"voice.tokenScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"voice.windowScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.voice.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"voice.cursorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.voice.anchorScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"anchorScopeId\":\"voice.anchorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.voice.tokenScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tokenScopeId\":\"voice.tokenScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"available\":false,\"captureMode\":\"still\",\"lastCaptureMs\":0}");
			});

		m_dispatcher.Register("gateway.nodes.camera.devices", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"devices\":[\"default-camera\"],\"activeDevice\":\"default-camera\",\"count\":1}");
			});

		m_dispatcher.Register("gateway.nodes.camera.permissions", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"camera\":false,\"capture\":false,\"granted\":false}");
			});

		m_dispatcher.Register("gateway.nodes.camera.routing", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"route\":\"local\",\"fallback\":\"still\",\"priority\":1}");
			});

		m_dispatcher.Register("gateway.nodes.camera.latency", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
			});

		m_dispatcher.Register("gateway.nodes.camera.health", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}");
			});

		m_dispatcher.Register("gateway.nodes.camera.metrics", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}");
			});

		m_dispatcher.Register("gateway.nodes.camera.profile", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profile\":\"default\",\"mode\":\"still\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"camera.window.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"camera.token.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"camera.scope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"camera.state.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"camera.windowScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"camera.cursorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"camera.tokenScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"camera.windowScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.camera.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"camera.cursorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.camera.anchorScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"anchorScopeId\":\"camera.anchorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.camera.tokenScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tokenScopeId\":\"camera.tokenScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":false,\"locationHooked\":false,\"providers\":[\"desktop\"],\"count\":1}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.providers", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"providers\":[\"desktop\"],\"defaultProvider\":\"desktop\",\"count\":1}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.permissions", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"notifications\":false,\"location\":false,\"granted\":false}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.routing", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"route\":\"desktop\",\"fallback\":\"none\",\"priority\":1}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.latency", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.health", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.metrics", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.profile", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profile\":\"default\",\"mode\":\"desktop\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"notifications.window.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"notifications.token.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"notifications.scope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"notifications.state.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"notifications.windowScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"notifications.cursorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"notifications.tokenScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"notifications.windowScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.notifications.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"notifications.cursorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.notifications.anchorScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"anchorScopeId\":\"notifications.anchorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.notifications.tokenScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tokenScopeId\":\"notifications.tokenScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.historyScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"historyScopeId\":\"logging.historyScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.snapshotScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"snapshotScopeId\":\"logging.snapshotScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.indexScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"indexScopeId\":\"logging.indexScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.markerScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"markerScopeId\":\"logging.markerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.packageScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"packageScopeId2\":\"logging.packageScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.archiveScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"archiveScopeId2\":\"logging.archiveScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.access.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"mode\":\"allowlist\",\"enabled\":false,\"entries\":0}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.historyScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"historyScopeId\":\"diagnostics.historyScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.snapshotScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"snapshotScopeId\":\"diagnostics.snapshotScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.indexScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"indexScopeId\":\"diagnostics.indexScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.markerScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"markerScopeId\":\"diagnostics.markerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.packageScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"packageScopeId2\":\"diagnostics.packageScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.archiveScopeId2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"archiveScopeId2\":\"diagnostics.archiveScopeId2.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.dmPairing.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":false,\"policy\":\"manual\",\"pending\":0}");
			});

		m_dispatcher.Register("gateway.security.dmPairing.entries", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"entries\":[],\"count\":0,\"policy\":\"manual\"}");
			});

		m_dispatcher.Register("gateway.security.allowlists.entries", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"entries\":[],\"count\":0,\"source\":\"runtime\"}");
			});

		m_dispatcher.Register("gateway.security.allowlists.count", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"count\":0,\"mode\":\"allowlist\"}");
			});

		m_dispatcher.Register("gateway.security.logging.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":true,\"level\":\"info\",\"diagnostics\":\"seeded\"}");
			});

		m_dispatcher.Register("gateway.security.logging.levels", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"levels\":[\"debug\",\"info\",\"warn\",\"error\"],\"count\":4}");
			});

		m_dispatcher.Register("gateway.security.logging.targets", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"targets\":[\"memory\"],\"defaultTarget\":\"memory\",\"count\":1}");
			});

		m_dispatcher.Register("gateway.security.logging.retention", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":true,\"days\":7,\"maxEntries\":1000}");
			});

		m_dispatcher.Register("gateway.security.logging.filters", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"filters\":[\"level>=info\"],\"count\":1}");
			});

		m_dispatcher.Register("gateway.security.logging.format", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"format\":\"json\",\"timestamp\":\"iso8601\"}");
			});

		m_dispatcher.Register("gateway.security.logging.pipeline", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pipeline\":\"seeded\",\"stages\":3,\"enabled\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.schemaVersion", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"schemaVersion\":\"1.0\",\"compatible\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.snapshot", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"snapshotId\":\"log-snapshot-1\",\"entries\":0,\"captured\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.window", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowMs\":60000,\"entries\":0,\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.sample", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sample\":[],\"count\":0,\"source\":\"memory\"}");
			});

		m_dispatcher.Register("gateway.security.logging.recent", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"recent\":[],\"count\":0,\"windowMs\":60000}");
			});

		m_dispatcher.Register("gateway.security.logging.metrics", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"throughput\":0}");
			});

		m_dispatcher.Register("gateway.security.logging.catalog", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"catalog\":[\"status\",\"levels\",\"targets\"],\"count\":3}");
			});

		m_dispatcher.Register("gateway.security.logging.profile", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profile\":\"default\",\"level\":\"info\",\"enabled\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"logging.window.default\",\"windowMs\":60000}");
			});

		m_dispatcher.Register("gateway.security.logging.cursorKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorKey\":\"logging.cursor.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.anchorKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorKey\":\"logging.anchor.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.offsetKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetKey\":\"logging.offset.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.markerKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"markerKey\":\"logging.marker.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.pointerKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerKey\":\"logging.pointer.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"logging.token.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.sequenceKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceKey\":\"logging.sequence.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.streamKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamKey\":\"logging.stream.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.bundleKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleKey\":\"logging.bundle.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.packageKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"packageKey\":\"logging.package.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.archiveKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"archiveKey\":\"logging.archive.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"logging.scope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.contextKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"contextKey\":\"logging.context.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.channelKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"channelKey\":\"logging.channel.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.routeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"routeKey\":\"logging.route.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.accountKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"accountKey\":\"logging.account.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.agentKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"agentKey\":\"logging.agent.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"logging.state.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.healthKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthKey\":\"logging.health.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.logKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"logKey\":\"logging.log.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.metricKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"metricKey\":\"logging.metric.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.traceKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"traceKey\":\"logging.trace.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.debugKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"debugKey\":\"logging.debug.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.cacheKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cacheKey\":\"logging.cache.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.queueKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"queueKey\":\"logging.queue.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"logging.windowScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"logging.cursorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.anchorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorScopeKey\":\"logging.anchorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.offsetScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetScopeKey\":\"logging.offsetScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.markerScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"markerScopeKey\":\"logging.markerScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.pointerScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerScopeKey\":\"logging.pointerScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"logging.tokenScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.streamScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamScopeKey\":\"logging.streamScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.sequenceScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceScopeKey\":\"logging.sequenceScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.bundleScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleScopeKey\":\"logging.bundleScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.packageScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"packageScopeKey\":\"logging.packageScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.archiveScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"archiveScopeKey\":\"logging.archiveScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.manifestScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"manifestScopeKey\":\"logging.manifestScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.profileScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profileScopeKey\":\"logging.profileScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.templateScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"templateScopeKey\":\"logging.templateScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.revisionScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"revisionScopeKey\":\"logging.revisionScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.historyScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"historyScopeKey\":\"logging.historyScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.snapshotScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"snapshotScopeKey\":\"logging.snapshotScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.indexScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"indexScopeKey\":\"logging.indexScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"logging.windowScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"logging.cursorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.anchorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorScopeId\":\"logging.anchorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.offsetScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetScopeId\":\"logging.offsetScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.pointerScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerScopeId\":\"logging.pointerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.tokenScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeId\":\"logging.tokenScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.sequenceScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceScopeId\":\"logging.sequenceScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.streamScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamScopeId\":\"logging.streamScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.logging.bundleScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleScopeId\":\"logging.bundleScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.packageScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"packageScopeId\":\"logging.packageScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.archiveScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"archiveScopeId\":\"logging.archiveScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.manifestScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"manifestScopeId\":\"logging.manifestScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.profileScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"profileScopeId\":\"logging.profileScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.templateScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"templateScopeId\":\"logging.templateScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.logging.revisionScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"revisionScopeId\":\"logging.revisionScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":true,\"sinks\":[\"memory\"],\"count\":1}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.sinks", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sinks\":[\"memory\"],\"defaultSink\":\"memory\",\"count\":1}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.events", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"events\":[\"gateway.health\",\"gateway.shutdown\"],\"count\":2}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.export", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"format\":\"json\",\"supported\":true,\"destinations\":[\"file\"]}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.retention", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":true,\"days\":3,\"maxEvents\":500}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.channels", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"channels\":[\"memory\"],\"count\":1}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.pipeline", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pipeline\":\"seeded\",\"stages\":2,\"enabled\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.schemaVersion", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"schemaVersion\":\"1.0\",\"compatible\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.snapshotExport", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"exportId\":\"diag-snapshot-1\",\"format\":\"json\",\"supported\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.windowExport", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"exportId\":\"diag-window-1\",\"windowMs\":60000,\"supported\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.sample", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sample\":[],\"count\":0,\"source\":\"memory\"}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.recent", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"recent\":[],\"count\":0,\"windowMs\":60000}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.metrics", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"throughput\":0}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.catalog", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"catalog\":[\"status\",\"sinks\",\"events\"],\"count\":3}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.profile", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profile\":\"default\",\"sink\":\"memory\",\"enabled\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"diagnostics.window.default\",\"windowMs\":60000}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.cursorKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorKey\":\"diagnostics.cursor.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.anchorKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorKey\":\"diagnostics.anchor.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.offsetKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetKey\":\"diagnostics.offset.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.markerKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"markerKey\":\"diagnostics.marker.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.pointerKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerKey\":\"diagnostics.pointer.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"diagnostics.token.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.sequenceKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceKey\":\"diagnostics.sequence.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.streamKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamKey\":\"diagnostics.stream.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.bundleKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleKey\":\"diagnostics.bundle.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.packageKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"packageKey\":\"diagnostics.package.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.archiveKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"archiveKey\":\"diagnostics.archive.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"diagnostics.scope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.contextKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"contextKey\":\"diagnostics.context.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.channelKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"channelKey\":\"diagnostics.channel.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.routeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"routeKey\":\"diagnostics.route.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.accountKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"accountKey\":\"diagnostics.account.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.agentKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"agentKey\":\"diagnostics.agent.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"diagnostics.state.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.healthKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthKey\":\"diagnostics.health.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.logKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"logKey\":\"diagnostics.log.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.metricKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"metricKey\":\"diagnostics.metric.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.traceKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"traceKey\":\"diagnostics.trace.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.debugKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"debugKey\":\"diagnostics.debug.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.cacheKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cacheKey\":\"diagnostics.cache.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.queueKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"queueKey\":\"diagnostics.queue.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"diagnostics.windowScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"diagnostics.cursorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.anchorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorScopeKey\":\"diagnostics.anchorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.offsetScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetScopeKey\":\"diagnostics.offsetScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.markerScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"markerScopeKey\":\"diagnostics.markerScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.pointerScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerScopeKey\":\"diagnostics.pointerScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"diagnostics.tokenScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.streamScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamScopeKey\":\"diagnostics.streamScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.sequenceScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceScopeKey\":\"diagnostics.sequenceScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.bundleScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleScopeKey\":\"diagnostics.bundleScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.packageScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"packageScopeKey\":\"diagnostics.packageScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.archiveScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"archiveScopeKey\":\"diagnostics.archiveScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.manifestScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"manifestScopeKey\":\"diagnostics.manifestScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.profileScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profileScopeKey\":\"diagnostics.profileScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.templateScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"templateScopeKey\":\"diagnostics.templateScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.revisionScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"revisionScopeKey\":\"diagnostics.revisionScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.historyScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"historyScopeKey\":\"diagnostics.historyScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.snapshotScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"snapshotScopeKey\":\"diagnostics.snapshotScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.indexScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"indexScopeKey\":\"diagnostics.indexScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"diagnostics.windowScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"diagnostics.cursorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.anchorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"anchorScopeId\":\"diagnostics.anchorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.offsetScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"offsetScopeId\":\"diagnostics.offsetScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.pointerScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"pointerScopeId\":\"diagnostics.pointerScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.tokenScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeId\":\"diagnostics.tokenScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.sequenceScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"sequenceScopeId\":\"diagnostics.sequenceScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.streamScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"streamScopeId\":\"diagnostics.streamScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.security.diagnostics.bundleScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"bundleScopeId\":\"diagnostics.bundleScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.packageScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"packageScopeId\":\"diagnostics.packageScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.archiveScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"archiveScopeId\":\"diagnostics.archiveScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.manifestScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"manifestScopeId\":\"diagnostics.manifestScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.profileScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"profileScopeId\":\"diagnostics.profileScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.templateScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"templateScopeId\":\"diagnostics.templateScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.security.diagnostics.revisionScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"revisionScopeId\":\"diagnostics.revisionScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.ops.doctor.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthy\":true,\"checks\":3,\"doctorAvailable\":true}");
			});

		m_dispatcher.Register("gateway.ops.doctor.run.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"running\":false,\"lastRunId\":\"doctor-preview-1\",\"lastStatus\":\"ok\"}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"host\":\"a2ui\",\"available\":false,\"session\":\"none\"}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.capabilities", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"host\":\"a2ui\",\"layers\":true,\"annotations\":true,\"maxSurfaces\":1}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.session", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"session\":\"none\",\"attached\":false,\"surfaces\":0}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.permissions", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"draw\":false,\"annotate\":false,\"granted\":false}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.routing", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"route\":\"local\",\"fallback\":\"none\",\"priority\":1}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.latency", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.health", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthy\":true,\"state\":\"idle\",\"issues\":0}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.metrics", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"uptimeMs\":0}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.profile", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profile\":\"default\",\"host\":\"a2ui\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"canvas.window.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"canvas.token.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"canvas.scope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"canvas.state.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"canvas.windowScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"canvas.cursorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"canvas.tokenScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"canvas.windowScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.nodes.canvas.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"canvas.cursorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.canvas.anchorScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"anchorScopeId\":\"canvas.anchorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.nodes.canvas.tokenScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tokenScopeId\":\"canvas.tokenScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"desktopActions\":true,\"commandSurface\":\"seeded\",\"coverage\":0}");
			});

		m_dispatcher.Register("gateway.platform.cli.commands", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"commands\":[\"gateway.ping\",\"gateway.health\"],\"count\":2}");
			});

		m_dispatcher.Register("gateway.platform.cli.shortcuts", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"shortcuts\":[\"Ctrl+L\",\"Ctrl+R\"],\"count\":2}");
			});

		m_dispatcher.Register("gateway.platform.cli.aliases", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"aliases\":[\"ping\",\"health\"],\"count\":2}");
			});

		m_dispatcher.Register("gateway.platform.cli.profile", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profile\":\"default\",\"interactive\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.context", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"workspace\":\"default\",\"scope\":\"desktop\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.latency", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
			});

		m_dispatcher.Register("gateway.platform.cli.health", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthy\":true,\"status\":\"ok\",\"checks\":2}");
			});

		m_dispatcher.Register("gateway.platform.cli.metrics", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"throughput\":0}");
			});

		m_dispatcher.Register("gateway.platform.cli.catalog", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"catalog\":[\"commands\",\"shortcuts\",\"aliases\"],\"count\":3}");
			});

		m_dispatcher.Register("gateway.platform.cli.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"cli.window.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"cli.token.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"cli.scope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"cli.state.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"cli.windowScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"cli.cursorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"cli.tokenScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"cli.windowScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.cli.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"cli.cursorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.platform.cli.anchorScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"anchorScopeId\":\"cli.anchorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.platform.cli.tokenScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tokenScopeId\":\"cli.tokenScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.status", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"hosting\":false,\"endpoint\":\"\",\"surface\":\"control\"}");
			});

		m_dispatcher.Register("gateway.platform.web.endpoint", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":false,\"url\":\"\",\"surface\":\"control\"}");
			});

		m_dispatcher.Register("gateway.platform.web.routes", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"routes\":[\"/\",\"/health\"],\"count\":2}");
			});

		m_dispatcher.Register("gateway.platform.web.health", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"healthy\":true,\"status\":\"ok\",\"checks\":2}");
			});

		m_dispatcher.Register("gateway.platform.web.originPolicy", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"mode\":\"local-only\",\"allowed\":[\"http://localhost\"],\"count\":1}");
			});

		m_dispatcher.Register("gateway.platform.web.csrf", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":true,\"mode\":\"token\",\"sameSite\":\"strict\"}");
			});

		m_dispatcher.Register("gateway.platform.web.latency", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"latencyMs\":0,\"samples\":1,\"windowMs\":1000}");
			});

		m_dispatcher.Register("gateway.platform.web.profile", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"profile\":\"default\",\"hosting\":false,\"surface\":\"control\"}");
			});

		m_dispatcher.Register("gateway.platform.web.metrics", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"samples\":1,\"errorRate\":0,\"throughput\":0}");
			});

		m_dispatcher.Register("gateway.platform.web.catalog", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"catalog\":[\"routes\",\"health\",\"profile\"],\"count\":3}");
			});

		m_dispatcher.Register("gateway.platform.web.windowKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowKey\":\"web.window.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.tokenKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenKey\":\"web.token.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.scopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"scopeKey\":\"web.scope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.stateKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"stateKey\":\"web.state.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.windowScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeKey\":\"web.windowScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.cursorScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeKey\":\"web.cursorScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.tokenScopeKey", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"tokenScopeKey\":\"web.tokenScope.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.windowScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"windowScopeId\":\"web.windowScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register("gateway.platform.web.cursorScopeId", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"cursorScopeId\":\"web.cursorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.platform.web.anchorScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"anchorScopeId\":\"web.anchorScopeId.default\",\"active\":true}");
			});

		m_dispatcher.Register(
			"gateway.platform.web.tokenScopeId",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tokenScopeId\":\"web.tokenScopeId.default\",\"active\":true}");
			});

		// OpenClaw P0 security parity baseline: stateful approvals + policy modes.
		struct ApprovalRequestRecord {
			std::string requestId;
			std::string family;
			std::string status;
			std::string subject;
			std::string reason;
			std::string requestedBy;
			std::uint64_t requestedAtMs = 0;
			std::uint64_t resolvedAtMs = 0;
		};
		struct ApprovalStateStore {
			std::string globalMode = "manual";
			std::unordered_map<std::string, std::string> nodeModes;
			std::unordered_map<std::string, ApprovalRequestRecord> execRequests;
			std::unordered_map<std::string, ApprovalRequestRecord> pluginRequests;
			std::unordered_map<std::string, std::string> deviceTokenByNodeId;
			std::unordered_map<std::string, std::uint64_t> deviceTokenIssuedAtByNodeId;
			std::uint64_t requestSequence = 1;
		};
		auto approvalState = std::make_shared<ApprovalStateStore>();

		auto normalizeApprovalMode = [](const std::string& rawMode) {
			std::string mode = TrimCopy(rawMode);
			std::transform(
				mode.begin(),
				mode.end(),
				mode.begin(),
				[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
			if (mode == "auto" || mode == "manual" || mode == "deny") {
				return mode;
			}
			return std::string("manual");
			};

		auto buildApprovalRecordJson = [](const ApprovalRequestRecord& record) {
			return JsonObject({
				{"requestId", JsonString(record.requestId)},
				{"family", JsonString(record.family)},
				{"status", JsonString(record.status)},
				{"subject", JsonString(record.subject)},
				{"reason", JsonString(record.reason)},
				{"requestedBy", JsonString(record.requestedBy)},
				{"requestedAtMs", JsonNumber(record.requestedAtMs)},
				{"resolvedAtMs", JsonNumber(record.resolvedAtMs)},
				{"resolved", JsonBool(record.status != "pending")},
				{"found", JsonBool(true)},
				});
			};

		auto buildApprovalListJson = [&buildApprovalRecordJson](const std::unordered_map<std::string, ApprovalRequestRecord>& entries) {
			std::vector<const ApprovalRequestRecord*> ordered;
			ordered.reserve(entries.size());
			for (const auto& [_, value] : entries) {
				ordered.push_back(&value);
			}
			std::sort(
				ordered.begin(),
				ordered.end(),
				[](const ApprovalRequestRecord* left, const ApprovalRequestRecord* right) {
					if (left->requestedAtMs == right->requestedAtMs) {
						return left->requestId < right->requestId;
					}
					return left->requestedAtMs > right->requestedAtMs;
				});

			std::vector<std::string> itemRows;
			itemRows.reserve(ordered.size());
			for (const auto* record : ordered) {
				itemRows.push_back(buildApprovalRecordJson(*record));
			}
			return JsonObject({
				{"items", JsonArray(itemRows)},
				{"count", JsonNumber(static_cast<std::uint64_t>(itemRows.size()))},
				});
			};

		m_dispatcher.Register("exec.approvals.get", [approvalState](const protocol::RequestFrame& request) {
			return protocol::OkResponse(
				request,
				JsonObject({
					{"scope", JsonString("global")},
					{"defaultMode", JsonString(approvalState->globalMode)},
					{"updated", JsonBool(false)},
					{"stateful", JsonBool(true)},
					}));
			});
		m_dispatcher.Register("exec.approvals.set", [approvalState, normalizeApprovalMode](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			approvalState->globalMode = normalizeApprovalMode(params.GetString("defaultMode"));
			return protocol::OkResponse(
				request,
				JsonObject({
					{"scope", JsonString("global")},
					{"defaultMode", JsonString(approvalState->globalMode)},
					{"updated", JsonBool(true)},
					{"stateful", JsonBool(true)},
					}));
			});
		m_dispatcher.Register("exec.approvals.node.get", [approvalState](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string nodeId = TrimCopy(params.GetString("nodeId"));
			if (nodeId.empty()) {
				return protocol::ErrorResponse(request, "invalid_request", "nodeId required");
			}
			auto it = approvalState->nodeModes.find(nodeId);
			const std::string mode = it != approvalState->nodeModes.end()
				? it->second
				: approvalState->globalMode;
			return protocol::OkResponse(
				request,
				JsonObject({
					{"scope", JsonString("node")},
					{"nodeId", JsonString(nodeId)},
					{"defaultMode", JsonString(mode)},
					{"updated", JsonBool(false)},
					{"stateful", JsonBool(true)},
					}));
			});
		m_dispatcher.Register("exec.approvals.node.set", [approvalState, normalizeApprovalMode](const protocol::RequestFrame& request) {
			const RequestParamsView params(request.paramsJson);
			const std::string nodeId = TrimCopy(params.GetString("nodeId"));
			if (nodeId.empty()) {
				return protocol::ErrorResponse(request, "invalid_request", "nodeId required");
			}
			const std::string mode = normalizeApprovalMode(params.GetString("defaultMode"));
			approvalState->nodeModes.insert_or_assign(nodeId, mode);
			return protocol::OkResponse(
				request,
				JsonObject({
					{"scope", JsonString("node")},
					{"nodeId", JsonString(nodeId)},
					{"defaultMode", JsonString(mode)},
					{"updated", JsonBool(true)},
					{"stateful", JsonBool(true)},
					}));
			});

		auto registerApprovalLifecycle = [
			this,
			approvalState,
			buildApprovalRecordJson,
			buildApprovalListJson](
				const std::string& family,
				const std::string& listMethod,
				const std::string& requestMethod,
				const std::string& waitMethod,
				const std::string& resolveMethod,
				bool supportsGetMethod,
				const std::string& getMethod,
				const std::string& idPrefix,
				std::unordered_map<std::string, ApprovalRequestRecord>& bucket) {
					m_dispatcher.Register(listMethod, [approvalState, &bucket, buildApprovalListJson](const protocol::RequestFrame& request) {
						(void)approvalState;
						return protocol::OkResponse(request, buildApprovalListJson(bucket));
						});

					m_dispatcher.Register(requestMethod, [approvalState, &bucket, buildApprovalRecordJson, family, idPrefix](const protocol::RequestFrame& request) {
						const RequestParamsView params(request.paramsJson);
						std::string requestId = TrimCopy(params.GetString("requestId"));
						if (requestId.empty()) {
							requestId = idPrefix + std::to_string(approvalState->requestSequence++);
						}

						auto existing = bucket.find(requestId);
						if (existing != bucket.end()) {
							return protocol::OkResponse(
								request,
								JsonObject({
									{"requestId", JsonString(existing->second.requestId)},
									{"status", JsonString(existing->second.status)},
									{"queued", JsonBool(false)},
									{"approval", buildApprovalRecordJson(existing->second)},
									}));
						}

						ApprovalRequestRecord record;
						record.requestId = requestId;
						record.family = family;
						record.status = "pending";
						record.subject = TrimCopy(params.GetString("command"));
						if (record.subject.empty()) {
							record.subject = TrimCopy(params.GetString("pluginId"));
						}
						if (record.subject.empty()) {
							record.subject = TrimCopy(params.GetString("action"));
						}
						if (record.subject.empty()) {
							record.subject = family + ".request";
						}
						record.reason = TrimCopy(params.GetString("reason"));
						record.requestedBy = TrimCopy(params.GetString("requestedBy"));
						if (record.requestedBy.empty()) {
							record.requestedBy = "operator";
						}
						record.requestedAtMs = GatewayEpochMilliseconds();
						bucket.insert_or_assign(record.requestId, record);

						return protocol::OkResponse(
							request,
							JsonObject({
								{"requestId", JsonString(record.requestId)},
								{"status", JsonString(record.status)},
								{"queued", JsonBool(true)},
								{"approval", buildApprovalRecordJson(record)},
								}));
						});

					if (supportsGetMethod) {
						m_dispatcher.Register(getMethod, [&bucket, buildApprovalRecordJson](const protocol::RequestFrame& request) {
							const RequestParamsView params(request.paramsJson);
							const std::string requestId = TrimCopy(params.GetString("requestId"));
							if (requestId.empty()) {
								return protocol::ErrorResponse(request, "invalid_request", "requestId required");
							}
							auto it = bucket.find(requestId);
							if (it == bucket.end()) {
								return protocol::OkResponse(
									request,
									JsonObject({
										{"requestId", JsonString(requestId)},
										{"status", JsonString("missing")},
										{"found", JsonBool(false)},
										}));
							}
							return protocol::OkResponse(request, buildApprovalRecordJson(it->second));
							});
					}

					m_dispatcher.Register(waitMethod, [&bucket, buildApprovalRecordJson](const protocol::RequestFrame& request) {
						const RequestParamsView params(request.paramsJson);
						const std::string requestId = TrimCopy(params.GetString("requestId"));
						if (requestId.empty()) {
							return protocol::ErrorResponse(request, "invalid_request", "requestId required");
						}
						auto it = bucket.find(requestId);
						if (it == bucket.end()) {
							return protocol::OkResponse(
								request,
								JsonObject({
									{"requestId", JsonString(requestId)},
									{"status", JsonString("missing")},
									{"resolved", JsonBool(false)},
									{"found", JsonBool(false)},
									}));
						}
						return protocol::OkResponse(
							request,
							JsonObject({
								{"requestId", JsonString(it->second.requestId)},
								{"status", JsonString(it->second.status)},
								{"resolved", JsonBool(it->second.status != "pending")},
								{"found", JsonBool(true)},
								{"approval", buildApprovalRecordJson(it->second)},
								}));
						});

					m_dispatcher.Register(resolveMethod, [&bucket, buildApprovalRecordJson](const protocol::RequestFrame& request) {
						const RequestParamsView params(request.paramsJson);
						const std::string requestId = TrimCopy(params.GetString("requestId"));
						if (requestId.empty()) {
							return protocol::ErrorResponse(request, "invalid_request", "requestId required");
						}
						auto it = bucket.find(requestId);
						if (it == bucket.end()) {
							return protocol::ErrorResponse(request, "invalid_request", "requestId not found");
						}

						std::string decision = TrimCopy(params.GetString("decision"));
						std::transform(
							decision.begin(),
							decision.end(),
							decision.begin(),
							[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
						if (decision != "approve" && decision != "approved" && decision != "reject" && decision != "rejected") {
							decision = "approve";
						}
						it->second.status = (decision == "reject" || decision == "rejected")
							? "rejected"
							: "approved";
						it->second.resolvedAtMs = GatewayEpochMilliseconds();
						if (it->second.reason.empty()) {
							it->second.reason = TrimCopy(params.GetString("reason"));
						}

						return protocol::OkResponse(
							request,
							JsonObject({
								{"requestId", JsonString(it->second.requestId)},
								{"status", JsonString(it->second.status)},
								{"resolved", JsonBool(true)},
								{"approval", buildApprovalRecordJson(it->second)},
								}));
						});
			};

		registerApprovalLifecycle(
			"exec.approval",
			"exec.approval.list",
			"exec.approval.request",
			"exec.approval.waitDecision",
			"exec.approval.resolve",
			true,
			"exec.approval.get",
			"exec-approval-",
			approvalState->execRequests);
		registerApprovalLifecycle(
			"plugin.approval",
			"plugin.approval.list",
			"plugin.approval.request",
			"plugin.approval.waitDecision",
			"plugin.approval.resolve",
			false,
			"",
			"plugin-approval-",
			approvalState->pluginRequests);

		m_dispatcher.Register(
			"node.pair.request",
			[this](const protocol::RequestFrame& request) {
				const NodePairingDeclaredSurface declared =
					BuildDeclaredSurfaceFromRequest(request);
				if (declared.nodeId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId required");
				}
				const RequestParamsView params(request.paramsJson);
				const bool silent = params.GetBool("silent").value_or(false);
				const NodePairingRequestResult result =
					m_nodePairingService.RequestPairing(declared, silent);
				if (result.request.requestId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId required");
				}
				if (result.created && !result.request.silent) {
					EmitBestEffortEvent(
						*this,
						"node.pair.requested",
						SerializePendingRequestJson(result.request));
				}
				return protocol::OkResponse(
					request,
					JsonObject({
						{"status", JsonString("pending")},
						{"created", JsonBool(result.created)},
						{"request", SerializePendingRequestJson(result.request)},
						}));
			});
		m_dispatcher.Register(
			"node.pair.list",
			[this](const protocol::RequestFrame& request) {
				const NodePairingListResult list = m_nodePairingService.ListPairing();
				std::vector<std::string> pendingRows;
				pendingRows.reserve(list.pending.size());
				for (const auto& pending : list.pending) {
					pendingRows.push_back(SerializePendingRequestJson(pending));
				}
				std::vector<std::string> pairedRows;
				pairedRows.reserve(list.paired.size());
				for (const auto& paired : list.paired) {
					pairedRows.push_back(SerializePairedNodeJson(paired));
				}
				return protocol::OkResponse(
					request,
					JsonObject({
						{"pending", JsonArray(pendingRows)},
						{"paired", JsonArray(pairedRows)},
						{"pendingCount", JsonNumber(static_cast<std::uint64_t>(pendingRows.size()))},
						{"pairedCount", JsonNumber(static_cast<std::uint64_t>(pairedRows.size()))},
						}));
			});
		m_dispatcher.Register(
			"node.pair.approve",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string requestId = TrimCopy(params.GetString("requestId"));
				if (requestId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"requestId required");
				}
				const std::vector<std::string> callerScopes = CollectCallerScopes(request);
				const NodePairingApproveResult approved =
					m_nodePairingService.ApprovePairing(requestId, callerScopes);
				if (approved.kind == NodePairingApproveResult::Kind::NotFound) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"unknown requestId");
				}
				if (approved.kind == NodePairingApproveResult::Kind::Forbidden) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"missing scope: " + approved.missingScope);
				}
				EmitBestEffortEvent(
					*this,
					"node.pair.resolved",
					JsonObject({
						{"requestId", JsonString(approved.requestId)},
						{"nodeId", JsonString(approved.node.declared.nodeId)},
						{"decision", JsonString("approved")},
						{"ts", JsonNumber(GatewayEpochMilliseconds())},
						}));
				return protocol::OkResponse(
					request,
					JsonObject({
						{"requestId", JsonString(approved.requestId)},
						{"node", SerializePairedNodeJson(approved.node)},
						}));
			});
		m_dispatcher.Register(
			"node.pair.reject",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string requestId = TrimCopy(params.GetString("requestId"));
				if (requestId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"requestId required");
				}
				const NodePairingRejectResult rejected =
					m_nodePairingService.RejectPairing(requestId);
				if (!rejected.found) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"unknown requestId");
				}
				EmitBestEffortEvent(
					*this,
					"node.pair.resolved",
					JsonObject({
						{"requestId", JsonString(rejected.requestId)},
						{"nodeId", JsonString(rejected.nodeId)},
						{"decision", JsonString("rejected")},
						{"ts", JsonNumber(GatewayEpochMilliseconds())},
						}));
				return protocol::OkResponse(
					request,
					JsonObject({
						{"requestId", JsonString(rejected.requestId)},
						{"nodeId", JsonString(rejected.nodeId)},
						{"rejected", JsonBool(true)},
						}));
			});
		m_dispatcher.Register(
			"node.pair.verify",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				const std::string token = TrimCopy(params.GetString("token"));
				if (nodeId.empty() || token.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId and token required");
				}
				NodePairingPairedNode matched;
				const bool verified = m_nodePairingService.VerifyNodeToken(nodeId, token, &matched);
				if (!verified) {
					return protocol::OkResponse(
						request,
						JsonObject({
							{"ok", JsonBool(false)},
							}));
				}
				return protocol::OkResponse(
					request,
					JsonObject({
						{"ok", JsonBool(true)},
						{"node", SerializePairedNodeJson(matched)},
						}));
			});
		m_dispatcher.Register(
			"node.rename",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				const std::string displayName = TrimCopy(params.GetString("displayName"));
				if (nodeId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId required");
				}
				if (displayName.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"displayName required");
				}
				const auto renamed = m_nodePairingService.RenamePairedNode(nodeId, displayName);
				if (!renamed.has_value()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"unknown nodeId");
				}
				return protocol::OkResponse(
					request,
					JsonObject({
						{"nodeId", JsonString(renamed->declared.nodeId)},
						{"displayName", JsonString(renamed->declared.displayName)},
						}));
			});
		m_dispatcher.Register(
			"node.list",
			[this](const protocol::RequestFrame& request) {
				const std::vector<NodePairingPairedNode> paired = m_nodePairingService.ListPairedNodes();
				const std::vector<KnownNodeSnapshot> nodes =
					m_nodeCatalogService.BuildKnownNodes(paired);
				std::vector<std::string> rows;
				rows.reserve(nodes.size());
				for (const auto& node : nodes) {
					rows.push_back(SerializeKnownNodeJson(node));
				}
				return protocol::OkResponse(
					request,
					JsonObject({
						{"ts", JsonNumber(GatewayEpochMilliseconds())},
						{"nodes", JsonArray(rows)},
						}));
			});
		m_dispatcher.Register(
			"node.describe",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				if (nodeId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId required");
				}
				const std::vector<NodePairingPairedNode> paired = m_nodePairingService.ListPairedNodes();
				const std::vector<KnownNodeSnapshot> nodes =
					m_nodeCatalogService.BuildKnownNodes(paired);
				const auto known = m_nodeCatalogService.FindKnownNode(nodes, nodeId);
				if (!known.has_value()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"unknown nodeId");
				}
				std::string payload = SerializeKnownNodeJson(known.value());
				payload.pop_back();
				payload += ",\"ts\":" + JsonNumber(GatewayEpochMilliseconds()) + "}";
				return protocol::OkResponse(request, payload);
			});
		m_dispatcher.Register(
			"node.pending.drain",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				if (nodeId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId required");
				}

				const std::vector<PendingNodeAction> drained =
					m_nodePendingActionQueue.PullAllowed(
						nodeId,
						{},
						GatewayEpochMilliseconds());
				const std::vector<std::string> drainedIds = ParseStringArrayField(request.paramsJson, "drainedIds");
				const std::vector<PendingNodeAction> remaining =
					m_nodePendingActionQueue.Ack(
						nodeId,
						drainedIds,
						GatewayEpochMilliseconds());
				return protocol::OkResponse(
					request,
					JsonObject({
						{"nodeId", JsonString(nodeId)},
						{"drained", JsonNumber(static_cast<std::uint64_t>(drainedIds.empty() ? drained.size() : drainedIds.size()))},
						{"remaining", JsonNumber(static_cast<std::uint64_t>(remaining.size()))},
						}));
			});
		m_dispatcher.Register(
			"node.pending.enqueue",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				const std::string command = TrimCopy(params.GetString("command"));
				const std::string idempotencyKey = TrimCopy(params.GetString("idempotencyKey"));
				const std::string rawParams = ResolveOptionalObjectJson(request.paramsJson, "params");
				if (nodeId.empty() || command.empty() || idempotencyKey.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId, command, and idempotencyKey required");
				}
				const PendingNodeAction queued = m_nodePendingActionQueue.Enqueue(
					nodeId,
					command,
					rawParams,
					idempotencyKey,
					GatewayEpochMilliseconds());
				if (queued.id.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"node.pending.enqueue could not enqueue action");
				}
				const std::vector<PendingNodeAction> current =
					m_nodePendingActionQueue.List(nodeId, GatewayEpochMilliseconds());
				return protocol::OkResponse(
					request,
					JsonObject({
						{"accepted", JsonBool(true)},
						{"queuedActionId", JsonString(queued.id)},
						{"queueDepth", JsonNumber(static_cast<std::uint64_t>(current.size()))},
						}));
			});
		m_dispatcher.Register(
			"node.pending.pull",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				if (nodeId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId required");
				}
				const std::vector<std::string> declaredCommands = ResolveDeclaredCommandsFromRequest(request);
				const std::vector<PendingNodeAction> actions =
					m_nodePendingActionQueue.PullAllowed(
						nodeId,
						declaredCommands,
						GatewayEpochMilliseconds());

				std::vector<std::string> actionRows;
				actionRows.reserve(actions.size());
				for (const auto& action : actions) {
					actionRows.push_back(JsonObject({
						{"id", JsonString(action.id)},
						{"command", JsonString(action.command)},
						{"paramsJSON", action.paramsJson.empty() ? "null" : action.paramsJson},
						{"enqueuedAtMs", JsonNumber(action.enqueuedAtMs)},
						}));
				}

				return protocol::OkResponse(
					request,
					JsonObject({
						{"nodeId", JsonString(nodeId)},
						{"actions", JsonArray(actionRows)},
						}));
			});
		m_dispatcher.Register(
			"node.pending.ack",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				if (nodeId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId required");
				}
				const std::vector<std::string> ackedIds = ParseStringArrayField(request.paramsJson, "ids");
				const std::vector<PendingNodeAction> remaining =
					m_nodePendingActionQueue.Ack(
						nodeId,
						ackedIds,
						GatewayEpochMilliseconds());
				return protocol::OkResponse(
					request,
					JsonObject({
						{"nodeId", JsonString(nodeId)},
						{"ackedIds", SerializeStringArrayJson(ackedIds)},
						{"remainingCount", JsonNumber(static_cast<std::uint64_t>(remaining.size()))},
						}));
			});
		m_dispatcher.Register(
			"node.invoke",
			[this](const protocol::RequestFrame& request) {
				if (!m_runtimeNodeParityEnabled || m_runtimeNodeParityRolloutMode == "legacy") {
					return protocol::OkResponse(
						request,
						JsonObject({
							{"ok", JsonBool(true)},
							{"queued", JsonBool(true)},
							{"reason", JsonString("legacy_node_parity_mode")},
							}));
				}

				if (m_runtimeNodeParityDiagnosticsEnabled) {
					++m_nodeInvokeTotalCount;
				}
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				const std::string command = TrimCopy(params.GetString("command"));
				const std::string idempotencyKey = TrimCopy(params.GetString("idempotencyKey"));
				if (nodeId.empty() || command.empty() || idempotencyKey.empty()) {
					if (m_runtimeNodeParityDiagnosticsEnabled) {
						++m_nodeInvokePolicyRejectCount;
					}
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"nodeId, command, and idempotencyKey required");
				}

				if (command == "system.execApprovals.get" || command == "system.execApprovals.set") {
					if (m_runtimeNodeParityDiagnosticsEnabled) {
						++m_nodeInvokePolicyRejectCount;
					}
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"node.invoke does not allow system.execApprovals.*; use exec.approvals.node.*",
						std::optional<std::string>(JsonObject({ {"command", JsonString(command)} })));
				}

				if (command == "browser.proxy" && IsForbiddenBrowserProxyMutation(request)) {
					if (m_runtimeNodeParityDiagnosticsEnabled) {
						++m_nodeInvokePolicyRejectCount;
					}
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"node.invoke cannot mutate persistent browser profiles via browser.proxy",
						std::optional<std::string>(JsonObject({ {"command", JsonString(command)} })));
				}

				const std::vector<std::string> declaredCommands = ResolveDeclaredCommandsFromRequest(request);
				if (!declaredCommands.empty()) {
					bool declared = false;
					for (const auto& declaredCommand : declaredCommands) {
						if (declaredCommand == command) {
							declared = true;
							break;
						}
					}
					if (!declared) {
						if (m_runtimeNodeParityDiagnosticsEnabled) {
							++m_nodeInvokePolicyRejectCount;
						}
						return protocol::ErrorResponse(
							request,
							"invalid_request",
							"node command not allowed: command not declared by node",
							std::optional<std::string>(JsonObject({
								{"reason", JsonString("command not declared by node")},
								{"command", JsonString(command)},
								})));
					}
				}

				const std::vector<std::string> allowlist = ParseStringArrayField(request.paramsJson, "allowlist");
				if (!allowlist.empty()) {
					bool allowlisted = false;
					for (const auto& allowed : allowlist) {
						if (allowed == command) {
							allowlisted = true;
							break;
						}
					}
					if (!allowlisted) {
						if (m_runtimeNodeParityDiagnosticsEnabled) {
							++m_nodeInvokePolicyRejectCount;
						}
						return protocol::ErrorResponse(
							request,
							"invalid_request",
							"node command not allowed: command not allowlisted",
							std::optional<std::string>(JsonObject({
								{"reason", JsonString("command not allowlisted")},
								{"command", JsonString(command)},
								})));
					}
				}

				const std::uint64_t nowMs = GatewayEpochMilliseconds();
				const std::string platform = TrimCopy(params.GetString("platform"));
				const std::string nodeErrorCode = TrimCopy(params.GetString("nodeErrorCode"));
				const std::string nodeErrorMessage = TrimCopy(params.GetString("nodeErrorMessage"));
				const bool foregroundDeferredEligible = ShouldQueueAsPendingForegroundAction(
					platform,
					command,
					nodeErrorCode,
					nodeErrorMessage);

				const NodeWakeAttempt wake1 = m_nodeWakeService.MaybeWakeNode(
					nodeId,
					false,
					"node.invoke",
					nowMs);
				if (m_runtimeNodeParityDiagnosticsEnabled) {
					++m_nodeWakeAttemptCount;
				}
				const bool reconnected1 = wake1.available &&
					m_nodeWakeService.WaitForNodeReconnect(
						nodeId,
						GatewayNodeWakeService::ReconnectWaitMs(),
						GatewayNodeWakeService::ReconnectPollMs(),
						nowMs + 50);
				const NodeWakeAttempt wake2 = !reconnected1 && wake1.available
					? m_nodeWakeService.MaybeWakeNode(nodeId, true, "node.invoke.retry", nowMs + 60)
					: NodeWakeAttempt{};
				if (!wake2.path.empty() && m_runtimeNodeParityDiagnosticsEnabled) {
					++m_nodeWakeAttemptCount;
				}
				const bool reconnected2 = wake2.available &&
					m_nodeWakeService.WaitForNodeReconnect(
						nodeId,
						GatewayNodeWakeService::ReconnectRetryWaitMs(),
						GatewayNodeWakeService::ReconnectPollMs(),
						nowMs + 90);

				if (!reconnected1 && !reconnected2) {
					const std::string rawParams = ResolveOptionalObjectJson(request.paramsJson, "params");
					const PendingNodeAction queued = m_nodePendingActionQueue.Enqueue(
						nodeId,
						command,
						rawParams,
						idempotencyKey,
						nowMs);
					if (m_runtimeNodeParityDiagnosticsEnabled) {
						++m_nodePendingQueueEnqueueCount;
					}
					const NodeWakeNudgeAttempt nudge =
						m_nodeWakeService.MaybeSendWakeNudge(nodeId, nowMs + 120);
					if (nudge.sent && m_runtimeNodeParityDiagnosticsEnabled) {
						++m_nodeWakeNudgeCount;
					}

					const std::string unavailableCode = foregroundDeferredEligible
						? "QUEUED_UNTIL_FOREGROUND"
						: "NOT_CONNECTED";
					const std::string unavailableMessage = foregroundDeferredEligible
						? "node command queued until iOS returns to foreground"
						: "node not connected";

					return protocol::ErrorResponse(
						request,
						"unavailable",
						unavailableMessage,
						std::optional<std::string>(JsonObject({
							{"retryable", JsonBool(true)},
							{"code", JsonString(unavailableCode)},
							{"queuedActionId", JsonString(queued.id)},
							{"nodeId", JsonString(nodeId)},
							{"command", JsonString(command)},
							{"wakePath", JsonString(!wake2.path.empty() ? wake2.path : wake1.path)},
							{"wakeAvailable", JsonBool(wake1.available || wake2.available)},
							{"wakeThrottled", JsonBool(wake1.throttled || wake2.throttled)},
							{"nudgeReason", JsonString(nudge.reason)},
							{"nodeErrorCode", JsonString(nodeErrorCode)},
							{"nodeErrorMessage", JsonString(nodeErrorMessage)},
							})),
							true,
							std::nullopt);
				}

				return protocol::OkResponse(
					request,
					JsonObject({
						{"ok", JsonBool(true)},
						{"nodeId", JsonString(nodeId)},
						{"command", JsonString(command)},
						{"payload", JsonObject({
							{"status", JsonString("executed")},
							{"note", JsonString("gateway parity shim")},
						})},
						{"payloadJSON", JsonObject({
							{"status", JsonString("executed")},
							{"note", JsonString("gateway parity shim")},
						})},
						}));
			});
		m_dispatcher.Register(
			"node.invoke.result",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string runId = TrimCopy(params.GetString("runId"));
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				if (runId.empty() || nodeId.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"runId and nodeId required");
				}

				const std::string payloadJson = request.paramsJson.has_value()
					? json::Trim(request.paramsJson.value())
					: "{}";
				EmitBestEffortEvent(
					*this,
					"node.invoke.result",
					JsonObject({
						{"runId", JsonString(runId)},
						{"nodeId", JsonString(nodeId)},
						{"payloadJSON", payloadJson},
						}));

				return protocol::OkResponse(
					request,
					JsonObject({
						{"runId", JsonString(runId)},
						{"nodeId", JsonString(nodeId)},
						{"accepted", JsonBool(true)},
						}));
			});
		m_dispatcher.Register(
			"node.event",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string eventName = TrimCopy(params.GetString("event"));
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				if (eventName.empty()) {
					return protocol::ErrorResponse(
						request,
						"invalid_request",
						"event required");
				}

				std::string payloadJson;
				if (request.paramsJson.has_value()) {
					json::FindRawField(request.paramsJson.value(), "payloadJSON", payloadJson);
				}
				if (payloadJson.empty()) {
					payloadJson = ResolveOptionalObjectJson(request.paramsJson, "payload");
				}
				if (payloadJson.empty() || !json::IsJsonObjectShape(payloadJson)) {
					payloadJson = "{}";
				}

				EmitBestEffortEvent(
					*this,
					"node.event",
					JsonObject({
						{"event", JsonString(eventName)},
						{"nodeId", JsonString(nodeId.empty() ? "node" : nodeId)},
						{"payloadJSON", payloadJson},
						}));

				return protocol::OkResponse(
					request,
					JsonObject({
						{"ok", JsonBool(true)},
						{"accepted", JsonBool(true)},
						}));
			});
		m_dispatcher.Register(
			"node.canvas.capability.refresh",
			[this](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string sessionKey = TrimCopy(params.GetString("sessionKey"));
				const std::string canvasHostUrl = TrimCopy(params.GetString("canvasHostUrl"));
				const NodeCanvasCapabilityRefreshResult refreshed =
					m_nodeCanvasCapabilityService.RefreshCapability(sessionKey, canvasHostUrl);
				if (!refreshed.ok) {
					return protocol::ErrorResponse(
						request,
						"unavailable",
						refreshed.error.empty()
						? "canvas host unavailable for this node session"
						: refreshed.error);
				}
				return protocol::OkResponse(
					request,
					JsonObject({
						{"canvasCapability", JsonString(refreshed.canvasCapability)},
						{"canvasCapabilityExpiresAtMs", JsonNumber(refreshed.canvasCapabilityExpiresAtMs)},
						{"canvasHostUrl", JsonString(refreshed.canvasHostUrl)},
						}));
			});

		auto registerUnsupportedDeviceMethod = [this](const std::string& methodName) {
			m_dispatcher.Register(
				methodName,
				[methodName](const protocol::RequestFrame& request) {
					return protocol::ErrorResponse(
						request,
						"unavailable",
						"Method `" + methodName + "` is not supported in BlazeClaw yet.");
				});
			};
		registerUnsupportedDeviceMethod("device.pair.list");
		registerUnsupportedDeviceMethod("device.pair.approve");
		registerUnsupportedDeviceMethod("device.pair.reject");
		registerUnsupportedDeviceMethod("device.pair.remove");
		m_dispatcher.Register(
			"device.token.rotate",
			[approvalState](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				if (nodeId.empty()) {
					return protocol::ErrorResponse(request, "invalid_request", "nodeId required");
				}
				const std::string token = "device-token-" + std::to_string(approvalState->requestSequence++);
				const std::uint64_t issuedAtMs = GatewayEpochMilliseconds();
				approvalState->deviceTokenByNodeId.insert_or_assign(nodeId, token);
				approvalState->deviceTokenIssuedAtByNodeId.insert_or_assign(nodeId, issuedAtMs);
				return protocol::OkResponse(
					request,
					JsonObject({
						{"nodeId", JsonString(nodeId)},
						{"token", JsonString(token)},
						{"rotated", JsonBool(true)},
						{"issuedAtMs", JsonNumber(issuedAtMs)},
						}));
			});
		m_dispatcher.Register(
			"device.token.revoke",
			[approvalState](const protocol::RequestFrame& request) {
				const RequestParamsView params(request.paramsJson);
				const std::string nodeId = TrimCopy(params.GetString("nodeId"));
				if (nodeId.empty()) {
					return protocol::ErrorResponse(request, "invalid_request", "nodeId required");
				}
				auto tokenIt = approvalState->deviceTokenByNodeId.find(nodeId);
				if (tokenIt == approvalState->deviceTokenByNodeId.end()) {
					return protocol::OkResponse(
						request,
						JsonObject({
							{"nodeId", JsonString(nodeId)},
							{"revoked", JsonBool(false)},
							{"found", JsonBool(false)},
							}));
				}
				approvalState->deviceTokenByNodeId.erase(tokenIt);
				approvalState->deviceTokenIssuedAtByNodeId.erase(nodeId);
				return protocol::OkResponse(
					request,
					JsonObject({
						{"nodeId", JsonString(nodeId)},
						{"revoked", JsonBool(true)},
						{"found", JsonBool(true)},
						}));
			});
	}

} // namespace blazeclaw::gateway
