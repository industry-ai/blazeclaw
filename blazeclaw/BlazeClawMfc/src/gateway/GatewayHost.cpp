#include "pch.h"
#include "GatewayHost.h"
#include "../core/diagnostics/DiagnosticsSnapshot.h"

#include "GatewayJsonUtils.h"
#include "GatewayJsonSerializers.h"
#include "GatewayJsonBuilder.h"
#include "GatewayRequestParams.h"
#include "GatewayHostCatalogHelpers.h"
#include "GatewayHostModelHelpers.h"
#include "GatewayHostProtocolHelpers.h"
#include "GatewaySkillRootResolver.h"
#include "GatewayHostHandlersToolsShared.h"
#include "GatewayPersistencePaths.h"
#include "GatewayProtocolCodec.h"
#include "GatewayProtocolSchemaValidator.h"
#include "PluginHostAdapter.h"
#include "GatewayHostEx.h"
#include "TaskDeltaRepository.h"
#include "TaskDeltaLegacyAdapter.h"
#include "TaskDeltaSchemaValidator.h"
#include "python/PythonRuntimeDispatcher.h"
#include "generated/GatewayHandlerCatalog.Generated.h"
#include "GatewayMethodSurfaceAudit.h"
#include "GatewayHttpAuthService.h"
#include "Telemetry.h"
#include "cron/CronOpsService.h"
#include "../app/MainFrame.h"
#include "../app/BlazeClawMfcApp.h"
#include "../app/ChatView.h"
#include "../app/VoiceRecorder.h"

#include <algorithm>
#include <iterator>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <windows.h>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	std::vector<std::string> GatewayHost::ListReservedChatSlashCommandNames() {
		return std::vector<std::string>{
			"help",
				"skill",
				"agent",
				"agents",
				"session",
				"sessions",
				"channel",
				"channels",
				"tool",
				"tools",
		};
	}

	namespace {
		using Json = nlohmann::json;

		std::string EscapeJson(const std::string& value) {
			return EscapeJsonString(value);
		}

		std::optional<std::string> TryNormalizeCronParamsPreValidation(
			const std::string& method,
			const std::optional<std::string>& paramsJson) {
			if (!paramsJson.has_value()) {
				return std::nullopt;
			}

			if (method != "cron.add" &&
				method != "cron.update" &&
				method != "cron.run" &&
				method != "cron.runs" &&
				method != "wake") {
				return std::nullopt;
			}

			Json params;
			try {
				params = Json::parse(paramsJson.value());
			}
			catch (...) {
				return std::nullopt;
			}

			if (!params.is_object()) {
				return std::nullopt;
			}

			bool changed = false;

			auto moveFieldIfPresent = [&](Json& from, Json& to, const char* key) {
				const auto it = from.find(key);
				if (it == from.end()) {
					return;
				}
				to[key] = *it;
				from.erase(it);
				changed = true;
			};

			auto ensureObjectField = [&](Json& node, const char* key) -> Json& {
				if (!node.contains(key) || !node[key].is_object()) {
					node[key] = Json::object();
					changed = true;
				}
				return node[key];
			};

			auto splitCsvValues = [](const std::string& raw) {
				std::vector<std::string> values;
				std::size_t start = 0;
				while (start <= raw.size()) {
					const std::size_t end = raw.find(',', start);
					std::string token =
						end == std::string::npos
						? raw.substr(start)
						: raw.substr(start, end - start);

					token.erase(
						token.begin(),
						std::find_if(
							token.begin(),
							token.end(),
							[](unsigned char ch) {
								return !std::isspace(ch);
							}));
					token.erase(
						std::find_if(
							token.rbegin(),
							token.rend(),
							[](unsigned char ch) {
								return !std::isspace(ch);
							}).base(),
						token.end());

					if (!token.empty()) {
						values.push_back(token);
					}

					if (end == std::string::npos) {
						break;
					}

					start = end + 1;
				}

				return values;
			};

			if (method == "cron.add") {
				if (params.contains("kind") ||
					params.contains("everyMs") ||
					params.contains("at") ||
					params.contains("atMs") ||
					params.contains("expr") ||
					params.contains("tz") ||
					params.contains("staggerMs") ||
					params.contains("anchorMs")) {
					Json& schedule = ensureObjectField(params, "schedule");
					moveFieldIfPresent(params, schedule, "kind");
					moveFieldIfPresent(params, schedule, "everyMs");
					moveFieldIfPresent(params, schedule, "at");
					moveFieldIfPresent(params, schedule, "atMs");
					moveFieldIfPresent(params, schedule, "expr");
					moveFieldIfPresent(params, schedule, "tz");
					moveFieldIfPresent(params, schedule, "staggerMs");
					moveFieldIfPresent(params, schedule, "anchorMs");
				}

				if (!params.contains("payload") &&
					(params.contains("text") || params.contains("message"))) {
					Json payload = Json::object();
					if (params.contains("message")) {
						payload["kind"] = "agentTurn";
						moveFieldIfPresent(params, payload, "message");
					}
					else {
						payload["kind"] = "systemEvent";
						moveFieldIfPresent(params, payload, "text");
					}
					moveFieldIfPresent(params, payload, "model");
					moveFieldIfPresent(params, payload, "fallbacks");
					moveFieldIfPresent(params, payload, "toolsAllow");
					moveFieldIfPresent(params, payload, "thinking");
					moveFieldIfPresent(params, payload, "timeoutSeconds");
					moveFieldIfPresent(params, payload, "lightContext");
					moveFieldIfPresent(params, payload, "allowUnsafeExternalContent");
					params["payload"] = std::move(payload);
					changed = true;
				}

				if (params.contains("deliveryMode") ||
					params.contains("deliveryUrl") ||
					params.contains("deliveryTo") ||
					params.contains("deliveryChannel") ||
					params.contains("deliveryAccountId") ||
					params.contains("deliveryBestEffort") ||
					params.contains("failureDestinationMode") ||
					params.contains("failureDestinationUrl") ||
					params.contains("failureDestinationTo") ||
					params.contains("failureDestinationChannel") ||
					params.contains("failureDestinationAccountId")) {
					Json& delivery = ensureObjectField(params, "delivery");
					moveFieldIfPresent(params, delivery, "deliveryMode");
					if (delivery.contains("deliveryMode") && !delivery.contains("mode")) {
						delivery["mode"] = delivery["deliveryMode"];
						delivery.erase("deliveryMode");
						changed = true;
					}
					moveFieldIfPresent(params, delivery, "deliveryTo");
					if (delivery.contains("deliveryTo") && !delivery.contains("to")) {
						delivery["to"] = delivery["deliveryTo"];
						delivery.erase("deliveryTo");
						changed = true;
					}
					moveFieldIfPresent(params, delivery, "deliveryUrl");
					if (delivery.contains("deliveryUrl") && !delivery.contains("to")) {
						delivery["to"] = delivery["deliveryUrl"];
						delivery.erase("deliveryUrl");
						changed = true;
					}
					else if (delivery.contains("deliveryUrl")) {
						delivery.erase("deliveryUrl");
						changed = true;
					}
					moveFieldIfPresent(params, delivery, "deliveryChannel");
					if (delivery.contains("deliveryChannel") && !delivery.contains("channel")) {
						delivery["channel"] = delivery["deliveryChannel"];
						delivery.erase("deliveryChannel");
						changed = true;
					}
					moveFieldIfPresent(params, delivery, "deliveryAccountId");
					if (delivery.contains("deliveryAccountId") && !delivery.contains("accountId")) {
						delivery["accountId"] = delivery["deliveryAccountId"];
						delivery.erase("deliveryAccountId");
						changed = true;
					}
					moveFieldIfPresent(params, delivery, "deliveryBestEffort");
					if (delivery.contains("deliveryBestEffort") && !delivery.contains("bestEffort")) {
						delivery["bestEffort"] = delivery["deliveryBestEffort"];
						delivery.erase("deliveryBestEffort");
						changed = true;
					}

					if (params.contains("failureDestinationMode") ||
						params.contains("failureDestinationUrl") ||
						params.contains("failureDestinationTo") ||
						params.contains("failureDestinationChannel") ||
						params.contains("failureDestinationAccountId")) {
						Json& failureDestination = ensureObjectField(delivery, "failureDestination");
						moveFieldIfPresent(params, failureDestination, "failureDestinationMode");
						if (failureDestination.contains("failureDestinationMode") &&
							!failureDestination.contains("mode")) {
							failureDestination["mode"] = failureDestination["failureDestinationMode"];
							failureDestination.erase("failureDestinationMode");
							changed = true;
						}
						moveFieldIfPresent(params, failureDestination, "failureDestinationTo");
						if (failureDestination.contains("failureDestinationTo") &&
							!failureDestination.contains("to")) {
							failureDestination["to"] = failureDestination["failureDestinationTo"];
							failureDestination.erase("failureDestinationTo");
							changed = true;
						}
						moveFieldIfPresent(params, failureDestination, "failureDestinationUrl");
						if (failureDestination.contains("failureDestinationUrl") &&
							!failureDestination.contains("to")) {
							failureDestination["to"] = failureDestination["failureDestinationUrl"];
							failureDestination.erase("failureDestinationUrl");
							changed = true;
						}
						else if (failureDestination.contains("failureDestinationUrl")) {
							failureDestination.erase("failureDestinationUrl");
							changed = true;
						}
						moveFieldIfPresent(params, failureDestination, "failureDestinationChannel");
						if (failureDestination.contains("failureDestinationChannel") &&
							!failureDestination.contains("channel")) {
							failureDestination["channel"] = failureDestination["failureDestinationChannel"];
							failureDestination.erase("failureDestinationChannel");
							changed = true;
						}
						moveFieldIfPresent(params, failureDestination, "failureDestinationAccountId");
						if (failureDestination.contains("failureDestinationAccountId") &&
							!failureDestination.contains("accountId")) {
							failureDestination["accountId"] = failureDestination["failureDestinationAccountId"];
							failureDestination.erase("failureDestinationAccountId");
							changed = true;
						}
					}
				}

				if (params.contains("failureAlertAfter") ||
					params.contains("failureAlertCooldownMs") ||
					params.contains("failureAlertMode") ||
					params.contains("failureAlertUrl") ||
					params.contains("failureAlertTo") ||
					params.contains("failureAlertChannel") ||
					params.contains("failureAlertAccountId")) {
					Json& failureAlert = ensureObjectField(params, "failureAlert");
					moveFieldIfPresent(params, failureAlert, "failureAlertAfter");
					if (failureAlert.contains("failureAlertAfter") &&
						!failureAlert.contains("after")) {
						failureAlert["after"] = failureAlert["failureAlertAfter"];
						failureAlert.erase("failureAlertAfter");
						changed = true;
					}
					moveFieldIfPresent(params, failureAlert, "failureAlertCooldownMs");
					if (failureAlert.contains("failureAlertCooldownMs") &&
						!failureAlert.contains("cooldownMs")) {
						failureAlert["cooldownMs"] = failureAlert["failureAlertCooldownMs"];
						failureAlert.erase("failureAlertCooldownMs");
						changed = true;
					}
					moveFieldIfPresent(params, failureAlert, "failureAlertMode");
					if (failureAlert.contains("failureAlertMode") &&
						!failureAlert.contains("mode")) {
						failureAlert["mode"] = failureAlert["failureAlertMode"];
						failureAlert.erase("failureAlertMode");
						changed = true;
					}
					moveFieldIfPresent(params, failureAlert, "failureAlertTo");
					if (failureAlert.contains("failureAlertTo") &&
						!failureAlert.contains("to")) {
						failureAlert["to"] = failureAlert["failureAlertTo"];
						failureAlert.erase("failureAlertTo");
						changed = true;
					}
					moveFieldIfPresent(params, failureAlert, "failureAlertUrl");
					if (failureAlert.contains("failureAlertUrl") &&
						!failureAlert.contains("to")) {
						failureAlert["to"] = failureAlert["failureAlertUrl"];
						failureAlert.erase("failureAlertUrl");
						changed = true;
					}
					else if (failureAlert.contains("failureAlertUrl")) {
						failureAlert.erase("failureAlertUrl");
						changed = true;
					}
					moveFieldIfPresent(params, failureAlert, "failureAlertChannel");
					if (failureAlert.contains("failureAlertChannel") &&
						!failureAlert.contains("channel")) {
						failureAlert["channel"] = failureAlert["failureAlertChannel"];
						failureAlert.erase("failureAlertChannel");
						changed = true;
					}
					moveFieldIfPresent(params, failureAlert, "failureAlertAccountId");
					if (failureAlert.contains("failureAlertAccountId") &&
						!failureAlert.contains("accountId")) {
						failureAlert["accountId"] = failureAlert["failureAlertAccountId"];
						failureAlert.erase("failureAlertAccountId");
						changed = true;
					}
				}
			}

			if (method == "cron.update") {
				if (params.contains("cronId") && !params.contains("id") && !params.contains("jobId")) {
					params["id"] = params["cronId"];
					params.erase("cronId");
					changed = true;
				}

				if (!params.contains("patch")) {
					Json patch = Json::object();
					for (auto it = params.begin(); it != params.end();) {
						if (it.key() == "id" || it.key() == "jobId") {
							++it;
							continue;
						}

						patch[it.key()] = it.value();
						it = params.erase(it);
						changed = true;
					}

					if (!patch.empty()) {
						params["patch"] = std::move(patch);
						changed = true;
					}
				}

				if (params.contains("patch") && params["patch"].is_object()) {
					Json& patch = params["patch"];

					if (patch.contains("kind") ||
						patch.contains("everyMs") ||
						patch.contains("at") ||
						patch.contains("atMs") ||
						patch.contains("expr") ||
						patch.contains("tz") ||
						patch.contains("staggerMs") ||
						patch.contains("anchorMs")) {
						Json& schedule = ensureObjectField(patch, "schedule");
						moveFieldIfPresent(patch, schedule, "kind");
						moveFieldIfPresent(patch, schedule, "everyMs");
						moveFieldIfPresent(patch, schedule, "at");
						moveFieldIfPresent(patch, schedule, "atMs");
						moveFieldIfPresent(patch, schedule, "expr");
						moveFieldIfPresent(patch, schedule, "tz");
						moveFieldIfPresent(patch, schedule, "staggerMs");
						moveFieldIfPresent(patch, schedule, "anchorMs");
					}

					if (!patch.contains("payload") &&
						(patch.contains("text") || patch.contains("message"))) {
						Json payload = Json::object();
						if (patch.contains("message")) {
							payload["kind"] = "agentTurn";
							moveFieldIfPresent(patch, payload, "message");
						}
						else {
							payload["kind"] = "systemEvent";
							moveFieldIfPresent(patch, payload, "text");
						}
						moveFieldIfPresent(patch, payload, "model");
						moveFieldIfPresent(patch, payload, "fallbacks");
						moveFieldIfPresent(patch, payload, "toolsAllow");
						moveFieldIfPresent(patch, payload, "thinking");
						moveFieldIfPresent(patch, payload, "timeoutSeconds");
						moveFieldIfPresent(patch, payload, "lightContext");
						moveFieldIfPresent(patch, payload, "allowUnsafeExternalContent");
						patch["payload"] = std::move(payload);
						changed = true;
					}

					if (patch.contains("deliveryMode") ||
						patch.contains("deliveryUrl") ||
						patch.contains("deliveryTo") ||
						patch.contains("deliveryChannel") ||
						patch.contains("deliveryAccountId") ||
						patch.contains("deliveryBestEffort") ||
						patch.contains("failureDestinationMode") ||
						patch.contains("failureDestinationUrl") ||
						patch.contains("failureDestinationTo") ||
						patch.contains("failureDestinationChannel") ||
						patch.contains("failureDestinationAccountId")) {
						Json& delivery = ensureObjectField(patch, "delivery");
						moveFieldIfPresent(patch, delivery, "deliveryMode");
						if (delivery.contains("deliveryMode") && !delivery.contains("mode")) {
							delivery["mode"] = delivery["deliveryMode"];
							delivery.erase("deliveryMode");
							changed = true;
						}
						moveFieldIfPresent(patch, delivery, "deliveryTo");
						if (delivery.contains("deliveryTo") && !delivery.contains("to")) {
							delivery["to"] = delivery["deliveryTo"];
							delivery.erase("deliveryTo");
							changed = true;
						}
						moveFieldIfPresent(patch, delivery, "deliveryUrl");
						if (delivery.contains("deliveryUrl") && !delivery.contains("to")) {
							delivery["to"] = delivery["deliveryUrl"];
							delivery.erase("deliveryUrl");
							changed = true;
						}
						else if (delivery.contains("deliveryUrl")) {
							delivery.erase("deliveryUrl");
							changed = true;
						}
						moveFieldIfPresent(patch, delivery, "deliveryChannel");
						if (delivery.contains("deliveryChannel") && !delivery.contains("channel")) {
							delivery["channel"] = delivery["deliveryChannel"];
							delivery.erase("deliveryChannel");
							changed = true;
						}
						moveFieldIfPresent(patch, delivery, "deliveryAccountId");
						if (delivery.contains("deliveryAccountId") && !delivery.contains("accountId")) {
							delivery["accountId"] = delivery["deliveryAccountId"];
							delivery.erase("deliveryAccountId");
							changed = true;
						}
						moveFieldIfPresent(patch, delivery, "deliveryBestEffort");
						if (delivery.contains("deliveryBestEffort") && !delivery.contains("bestEffort")) {
							delivery["bestEffort"] = delivery["deliveryBestEffort"];
							delivery.erase("deliveryBestEffort");
							changed = true;
						}

						if (patch.contains("failureDestinationMode") ||
							patch.contains("failureDestinationUrl") ||
							patch.contains("failureDestinationTo") ||
							patch.contains("failureDestinationChannel") ||
							patch.contains("failureDestinationAccountId")) {
							Json& failureDestination = ensureObjectField(delivery, "failureDestination");
							moveFieldIfPresent(patch, failureDestination, "failureDestinationMode");
							if (failureDestination.contains("failureDestinationMode") &&
								!failureDestination.contains("mode")) {
								failureDestination["mode"] = failureDestination["failureDestinationMode"];
								failureDestination.erase("failureDestinationMode");
								changed = true;
							}
							moveFieldIfPresent(patch, failureDestination, "failureDestinationTo");
							if (failureDestination.contains("failureDestinationTo") &&
								!failureDestination.contains("to")) {
								failureDestination["to"] = failureDestination["failureDestinationTo"];
								failureDestination.erase("failureDestinationTo");
								changed = true;
							}
							moveFieldIfPresent(patch, failureDestination, "failureDestinationUrl");
							if (failureDestination.contains("failureDestinationUrl") &&
								!failureDestination.contains("to")) {
								failureDestination["to"] = failureDestination["failureDestinationUrl"];
								failureDestination.erase("failureDestinationUrl");
								changed = true;
							}
							else if (failureDestination.contains("failureDestinationUrl")) {
								failureDestination.erase("failureDestinationUrl");
								changed = true;
							}
							moveFieldIfPresent(patch, failureDestination, "failureDestinationChannel");
							if (failureDestination.contains("failureDestinationChannel") &&
								!failureDestination.contains("channel")) {
								failureDestination["channel"] = failureDestination["failureDestinationChannel"];
								failureDestination.erase("failureDestinationChannel");
								changed = true;
							}
							moveFieldIfPresent(patch, failureDestination, "failureDestinationAccountId");
							if (failureDestination.contains("failureDestinationAccountId") &&
								!failureDestination.contains("accountId")) {
								failureDestination["accountId"] = failureDestination["failureDestinationAccountId"];
								failureDestination.erase("failureDestinationAccountId");
								changed = true;
							}
						}
					}

					if (patch.contains("failureAlertAfter") ||
						patch.contains("failureAlertCooldownMs") ||
						patch.contains("failureAlertMode") ||
						patch.contains("failureAlertUrl") ||
						patch.contains("failureAlertTo") ||
						patch.contains("failureAlertChannel") ||
						patch.contains("failureAlertAccountId")) {
						Json& failureAlert = ensureObjectField(patch, "failureAlert");
						moveFieldIfPresent(patch, failureAlert, "failureAlertAfter");
						if (failureAlert.contains("failureAlertAfter") &&
							!failureAlert.contains("after")) {
							failureAlert["after"] = failureAlert["failureAlertAfter"];
							failureAlert.erase("failureAlertAfter");
							changed = true;
						}
						moveFieldIfPresent(patch, failureAlert, "failureAlertCooldownMs");
						if (failureAlert.contains("failureAlertCooldownMs") &&
							!failureAlert.contains("cooldownMs")) {
							failureAlert["cooldownMs"] = failureAlert["failureAlertCooldownMs"];
							failureAlert.erase("failureAlertCooldownMs");
							changed = true;
						}
						moveFieldIfPresent(patch, failureAlert, "failureAlertMode");
						if (failureAlert.contains("failureAlertMode") &&
							!failureAlert.contains("mode")) {
							failureAlert["mode"] = failureAlert["failureAlertMode"];
							failureAlert.erase("failureAlertMode");
							changed = true;
						}
						moveFieldIfPresent(patch, failureAlert, "failureAlertTo");
						if (failureAlert.contains("failureAlertTo") &&
							!failureAlert.contains("to")) {
							failureAlert["to"] = failureAlert["failureAlertTo"];
							failureAlert.erase("failureAlertTo");
							changed = true;
						}
						moveFieldIfPresent(patch, failureAlert, "failureAlertUrl");
						if (failureAlert.contains("failureAlertUrl") &&
							!failureAlert.contains("to")) {
							failureAlert["to"] = failureAlert["failureAlertUrl"];
							failureAlert.erase("failureAlertUrl");
							changed = true;
						}
						else if (failureAlert.contains("failureAlertUrl")) {
							failureAlert.erase("failureAlertUrl");
							changed = true;
						}
						moveFieldIfPresent(patch, failureAlert, "failureAlertChannel");
						if (failureAlert.contains("failureAlertChannel") &&
							!failureAlert.contains("channel")) {
							failureAlert["channel"] = failureAlert["failureAlertChannel"];
							failureAlert.erase("failureAlertChannel");
							changed = true;
						}
						moveFieldIfPresent(patch, failureAlert, "failureAlertAccountId");
						if (failureAlert.contains("failureAlertAccountId") &&
							!failureAlert.contains("accountId")) {
							failureAlert["accountId"] = failureAlert["failureAlertAccountId"];
							failureAlert.erase("failureAlertAccountId");
							changed = true;
						}
					}
				}
			}

			if (method == "cron.run") {
				if (params.contains("cronId") && !params.contains("id") && !params.contains("jobId")) {
					params["id"] = params["cronId"];
					params.erase("cronId");
					changed = true;
				}
			}

			if (method == "cron.runs") {
				if (params.contains("cronId") && !params.contains("id") && !params.contains("jobId")) {
					params["id"] = params["cronId"];
					params.erase("cronId");
					changed = true;
				}

				if (params.contains("statuses") && params["statuses"].is_string() &&
					!params.contains("status")) {
					const std::string rawStatuses = params["statuses"].get<std::string>();
					const std::vector<std::string> parsedStatuses = splitCsvValues(rawStatuses);
					if (parsedStatuses.size() > 1) {
						params["statuses"] = Json::array();
						for (const std::string& value : parsedStatuses) {
							params["statuses"].push_back(value);
						}
						changed = true;
					}
					else if (parsedStatuses.size() == 1) {
						params["status"] = parsedStatuses.front();
						params.erase("statuses");
						changed = true;
					}
				}

				if (params.contains("deliveryStatuses") &&
					params["deliveryStatuses"].is_string() &&
					!params.contains("deliveryStatus")) {
					const std::string rawDeliveryStatuses =
						params["deliveryStatuses"].get<std::string>();
					const std::vector<std::string> parsedDeliveryStatuses =
						splitCsvValues(rawDeliveryStatuses);
					if (parsedDeliveryStatuses.size() > 1) {
						params["deliveryStatuses"] = Json::array();
						for (const std::string& value : parsedDeliveryStatuses) {
							params["deliveryStatuses"].push_back(value);
						}
						changed = true;
					}
					else if (parsedDeliveryStatuses.size() == 1) {
						params["deliveryStatus"] = parsedDeliveryStatuses.front();
						params.erase("deliveryStatuses");
						changed = true;
					}
				}

				if (params.contains("status") &&
					params["status"].is_string() &&
					!params.contains("statuses")) {
					const std::string rawStatus = params["status"].get<std::string>();
					const std::vector<std::string> parsedStatuses =
						splitCsvValues(rawStatus);
					if (parsedStatuses.size() > 1) {
						params["statuses"] = Json::array();
						for (const std::string& value : parsedStatuses) {
							params["statuses"].push_back(value);
						}
						params.erase("status");
						changed = true;
					}
					else if (parsedStatuses.size() == 1 &&
						parsedStatuses.front() != rawStatus) {
						params["status"] = parsedStatuses.front();
						changed = true;
					}
				}

				if (params.contains("deliveryStatus") &&
					params["deliveryStatus"].is_string() &&
					!params.contains("deliveryStatuses")) {
					const std::string rawDeliveryStatus =
						params["deliveryStatus"].get<std::string>();
					const std::vector<std::string> parsedDeliveryStatuses =
						splitCsvValues(rawDeliveryStatus);
					if (parsedDeliveryStatuses.size() > 1) {
						params["deliveryStatuses"] = Json::array();
						for (const std::string& value : parsedDeliveryStatuses) {
							params["deliveryStatuses"].push_back(value);
						}
						params.erase("deliveryStatus");
						changed = true;
					}
					else if (parsedDeliveryStatuses.size() == 1 &&
						parsedDeliveryStatuses.front() != rawDeliveryStatus) {
						params["deliveryStatus"] = parsedDeliveryStatuses.front();
						changed = true;
					}
				}

				if (params.value("scope", std::string()) == "job" &&
					!params.contains("id") &&
					!params.contains("jobId")) {
					params["scope"] = "all";
					changed = true;
				}
			}

			if (method == "wake" && params.contains("wakeMode") && !params.contains("mode")) {
				params["mode"] = params["wakeMode"];
				params.erase("wakeMode");
				changed = true;
			}

			if (method == "wake" &&
				params.contains("mode") &&
				params["mode"].is_string()) {
				const std::string originalMode = params["mode"].get<std::string>();
				std::string modeRaw = params["mode"].get<std::string>();
				modeRaw.erase(
					modeRaw.begin(),
					std::find_if(
						modeRaw.begin(),
						modeRaw.end(),
						[](unsigned char ch) {
							return !std::isspace(ch);
						}));
				modeRaw.erase(
					std::find_if(
						modeRaw.rbegin(),
						modeRaw.rend(),
						[](unsigned char ch) {
							return !std::isspace(ch);
						}).base(),
					modeRaw.end());
				std::transform(
					modeRaw.begin(),
					modeRaw.end(),
					modeRaw.begin(),
					[](unsigned char ch) {
						return static_cast<char>(std::tolower(ch));
					});

				std::string canonicalMode;
				if (modeRaw == "now") {
					canonicalMode = "now";
				}
				else if (modeRaw == "next-heartbeat" ||
					modeRaw == "next_heartbeat" ||
					modeRaw == "next heartbeat" ||
					modeRaw == "nextheartbeat") {
					canonicalMode = "next-heartbeat";
				}

				if (!canonicalMode.empty() && canonicalMode != originalMode) {
					params["mode"] = canonicalMode;
					changed = true;
				}
			}

			if (!changed) {
				return std::nullopt;
			}

			return params.dump();
		}

		void EnsureOpsToolsRuntimeRegistered(GatewayToolRegistry& registry) {
			const ToolPreviewResult weatherPreview = registry.Preview("weather.lookup");
			const ToolPreviewResult emailPreview = registry.Preview("email.schedule");
			if (weatherPreview.allowed && emailPreview.allowed) {
				return;
			}

			const auto loadResult = PluginHostAdapter::LoadExtensionRuntime("ops-tools");
			if (!loadResult.ok) {
				return;
			}

			const auto weatherResolve =
				PluginHostAdapter::ResolveExecutor("ops-tools", "weather.lookup", "");
			if (weatherResolve.resolved && weatherResolve.executor) {
				registry.RegisterRuntimeTool(
					ToolCatalogEntry{
						.id = "weather.lookup",
						.label = "Weather Lookup",
						.category = "data",
						.enabled = true,
					},
					weatherResolve.executor);
			}

			const auto emailResolve =
				PluginHostAdapter::ResolveExecutor("ops-tools", "email.schedule", "");
			if (emailResolve.resolved && emailResolve.executor) {
				registry.RegisterRuntimeTool(
					ToolCatalogEntry{
						.id = "email.schedule",
						.label = "Email Schedule",
						.category = "communication",
						.enabled = true,
					},
					emailResolve.executor);
			}
		}

		void EnsurePdfGeneratorRuntimeRegistered(GatewayToolRegistry& registry) {
			registry.RegisterRuntimeTool(
				ToolCatalogEntry{
					.id = "pdf_generator.generate",
					.label = "PDF Generator",
					.category = "document",
					.enabled = true,
				},
				python::PythonRuntimeDispatcher::CreateExecutor());
		}

		std::string ResolveExtensionsCatalogPath() {
			const std::filesystem::path preferred =
				std::filesystem::path("blazeclaw") /
				"extensions" /
				"extensions.catalog.json";
			if (std::filesystem::exists(preferred)) {
				return preferred.string();
			}

			const std::filesystem::path fallback =
				std::filesystem::path("extensions") /
				"extensions.catalog.json";
			if (std::filesystem::exists(fallback)) {
				return fallback.string();
			}

			char modulePathBuffer[MAX_PATH] = {};
			const DWORD moduleChars =
				GetModuleFileNameA(nullptr, modulePathBuffer, MAX_PATH);
			if (moduleChars == 0 || moduleChars >= MAX_PATH) {
				return preferred.string();
			}

			const std::filesystem::path exeDir =
				std::filesystem::path(modulePathBuffer).parent_path();

			const std::vector<std::filesystem::path> relativeCandidates = {
				std::filesystem::path("..") / ".." / "extensions" / "extensions.catalog.json",
				std::filesystem::path("..") / ".." / ".." / "extensions" / "extensions.catalog.json",
				std::filesystem::path("..") / "blazeclaw" / "extensions" / "extensions.catalog.json",
			};

			for (const auto& relativeCandidate : relativeCandidates) {
				const std::filesystem::path candidate =
					std::filesystem::weakly_canonical(exeDir / relativeCandidate);
				if (std::filesystem::exists(candidate)) {
					return candidate.string();
				}
			}

			return preferred.string();
		}

		std::string ReadEnvironmentVariable(const char* name) {
			if (name == nullptr) {
				return {};
			}

			char* raw = nullptr;
			size_t size = 0;
			if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
				return {};
			}

			std::string value(raw);
			free(raw);
			return value;
		}

		std::string ToNarrow(const std::wstring& value) {
			std::string result;
			result.reserve(value.size());

			for (const wchar_t ch : value) {
				result.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			return result;
		}

		struct FallbackRecordingState {
			CVoiceRecorder recorder;
			CStringW lastFilePath;
			bool initialized = false;
		};

		FallbackRecordingState& GetFallbackRecordingState() {
			static FallbackRecordingState state;
			return state;
		}

		CStringW BuildRecordingFilePath() {
			WCHAR exePath[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePath, MAX_PATH);
			LPWSTR p = wcsrchr(exePath, L'\\');
			if (p != nullptr) {
				*p = L'\0';
			}
			CStringW recordingsDir;
			recordingsDir.Format(L"%s\\BlazeClawRecordings", exePath);
			CreateDirectoryW(recordingsDir.GetString(), nullptr);

			SYSTEMTIME st;
			GetLocalTime(&st);
			CStringW fileName;
			fileName.Format(L"recording_%04d%02d%02d_%02d%02d%02d.wav",
				st.wYear, st.wMonth, st.wDay,
				st.wHour, st.wMinute, st.wSecond);
			return recordingsDir + L"\\" + fileName;
		}

		GatewayHost::ChatRuntimeResult::TaskDeltaEntry NormalizePersistedTaskDelta(
			const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& source,
			const std::string& runId,
			const std::string& sessionId,
			const std::size_t stableIndex) {
			GatewayHost::ChatRuntimeResult::TaskDeltaEntry normalized = source;
			normalized.index = stableIndex;
			if (normalized.runId.empty()) {
				normalized.runId = runId;
			}

			if (normalized.sessionId.empty()) {
				normalized.sessionId = sessionId.empty() ? "main" : sessionId;
			}

			if (normalized.phase.empty()) {
				normalized.phase = stableIndex + 1 == 1 ? "plan" : "unknown";
			}

			if (normalized.phase == "final") {
				if (normalized.status.empty()) {
					normalized.status = "completed";
				}
				if (normalized.stepLabel.empty()) {
					normalized.stepLabel = "run_terminal";
				}
			}
			else {
				if (normalized.status.empty()) {
					normalized.status = "ok";
				}
				if (normalized.stepLabel.empty()) {
					normalized.stepLabel = normalized.phase;
				}
			}

			if (normalized.startedAtMs == 0 && normalized.completedAtMs > 0) {
				normalized.startedAtMs = normalized.completedAtMs;
			}
			if (normalized.completedAtMs == 0 && normalized.startedAtMs > 0) {
				normalized.completedAtMs = normalized.startedAtMs;
			}
			if (normalized.completedAtMs < normalized.startedAtMs) {
				normalized.completedAtMs = normalized.startedAtMs;
			}
			normalized.latencyMs =
				normalized.completedAtMs >= normalized.startedAtMs
				? (normalized.completedAtMs - normalized.startedAtMs)
				: 0;

			return normalized;
		}

		std::string ToLowerCopy(std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return value;
		}

		std::string TruncateForMatch(const std::string& text, std::size_t maxChars) {
			if (text.size() <= maxChars) {
				return text;
			}

			return text.substr(0, maxChars) + "...";
		}

		std::string BuildMemorySearchEnvelope(
			const std::string& sessionKey,
			const std::vector<std::string>& matches) {
			std::vector<std::string> rows;
			rows.reserve(matches.size());
			for (const auto& text : matches) {
				rows.push_back(JsonObject({ {"text", JsonString(text)} }));
			}

			return JsonObject({
				{"sessionKey", JsonString(sessionKey)},
				{"matches", JsonArray(rows)},
				{"count", JsonNumber(static_cast<std::uint64_t>(matches.size()))},
				});
		}

	} // namespace

	GatewayHost::NativeRecordingResult GatewayHost::StartNativeRecording()
	{
		NativeRecordingResult result;
		// Try to locate main frame and chat view to reuse existing recorder
		auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
		if (app == nullptr) {
			result.ok = false;
			result.errorMessage = "app unavailable";
			return result;
		}

		CMainFrame* main = dynamic_cast<CMainFrame*>(app->GetMainWnd());
		if (main == nullptr) {
			result.ok = false;
			result.errorMessage = "main frame unavailable";
			return result;
		}

		CStringW filePathW = BuildRecordingFilePath();
		CChatView* chat = main->GetActiveChatView();
		if (chat != nullptr) {
			if (!chat->StartRecordingToPath(filePathW)) {
				result.ok = false;
				result.errorMessage = "failed to start recording";
				return result;
			}

			result.ok = true;
			return result;
		}

		auto& fallback = GetFallbackRecordingState();
		if (!fallback.initialized) {
			if (!fallback.recorder.Initialize(main->GetSafeHwnd())) {
				result.ok = false;
				result.errorMessage = "failed to initialize fallback recorder";
				return result;
			}
			fallback.initialized = true;
		}

		fallback.lastFilePath = filePathW;
		if (!fallback.recorder.StartRecording(filePathW.GetString())) {
			result.ok = false;
			result.errorMessage = "failed to start recording";
			return result;
		}

		result.ok = true;
		return result;
	}

	GatewayHost::NativeRecordingResult GatewayHost::StopNativeRecording()
	{
		NativeRecordingResult result;
		auto* app = dynamic_cast<CBlazeClawMFCApp*>(AfxGetApp());
		if (app == nullptr) {
			result.ok = false;
			result.errorMessage = "app unavailable";
			return result;
		}

		CMainFrame* main = dynamic_cast<CMainFrame*>(app->GetMainWnd());
		if (main == nullptr) {
			result.ok = false;
			result.errorMessage = "main frame unavailable";
			return result;
		}

		CStringW lastPath;
		CChatView* chat = main->GetActiveChatView();
		if (chat != nullptr) {
			lastPath = chat->StopRecordingAndGetPath();
		}
		else {
			auto& fallback = GetFallbackRecordingState();
			if (fallback.recorder.GetState() == VoiceRecorderState::Recording) {
				fallback.recorder.StopRecording();
			}
			lastPath = fallback.lastFilePath;
		}

		if (lastPath.IsEmpty()) {
			result.ok = false;
			result.errorMessage = "no recording available";
			return result;
		}

		// Convert to UTF-8 narrow path
		const std::wstring lastPathWide(lastPath.GetString());
		std::string pathUtf8 = ToNarrow(lastPathWide);
		result.ok = true;
		result.audioPath = pathUtf8;
		return result;
	}

	bool GatewayHost::StartLocalDispatchOnly() {
		if (m_dispatchInitialized) {
			m_running = true;
			return true;
		}

		PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);
		EnsurePdfGeneratorRuntimeRegistered(m_toolRegistry);
		RegisterDefaultHandlers();
		m_dispatchInitialized = true;
		m_runtimeHandlersInitialized = true;
		m_running = true;
		return true;
	}

	bool GatewayHost::BootstrapCreateRuntimeState(
		const blazeclaw::config::GatewayConfig& config) {
		if (!m_dispatchInitialized) {
			RegisterDefaultHandlers();
			m_dispatchInitialized = true;
		}

		if (m_initialized) {
			return true;
		}

		return CreateRuntimeState(config);
	}

	bool GatewayHost::BootstrapStartRuntimeServices() {
		if (m_initialized) {
			return true;
		}

		return StartRuntimeServices();
	}

	bool GatewayHost::BootstrapAttachTransportHandlers() {
		if (m_initialized) {
			return true;
		}

		return AttachTransportRuntime();
	}

	bool GatewayHost::BootstrapStartRuntimeSubscriptions() {
		if (m_initialized) {
			return true;
		}

		return StartRuntimeSubscriptions();
	}

	bool GatewayHost::BootstrapFinalizeRuntimeInitialization() {
		if (m_initialized) {
			return true;
		}

		return FinalizeRuntimeInitialization();
	}

	bool GatewayHost::StartLocalRuntimeDispatchOnly() {
		PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);
		EnsurePdfGeneratorRuntimeRegistered(m_toolRegistry);
		m_runtimeResolvedSkillDirectories = ResolveAbsoluteSkillDirectories({
			"blazeclaw/skills",
			"skills",
		});
		EmitSkillRootDiagnostics("start_local_runtime_dispatch_only", m_runtimeResolvedSkillDirectories);
		for (const auto& directory : m_runtimeResolvedSkillDirectories) {
			const auto loadStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			const auto loadedCount = m_toolRegistry.LoadSkillToolsFromDirectory(directory);
			const auto loadEndMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			EmitTelemetryEvent(
				"gateway.skills.root.load",
				std::string("{\"stage\":\"start_local_runtime_dispatch_only\",\"root\":") + JsonString(directory) +
				",\"loadedCount\":" + std::to_string(loadedCount) +
				",\"elapsedMs\":" + std::to_string(loadEndMs >= loadStartMs ? (loadEndMs - loadStartMs) : 0) +
				"}");
		}
		handlers::tools_shared::ToolsSharedHandlers::RegisterToolsList(m_dispatcher, m_toolRegistry);
		handlers::tools_shared::ToolsSharedHandlers::RegisterToolsCatalog(m_dispatcher, m_toolRegistry);
		RegisterGatewayRegistryIntrospectionHandlers();
		if (!m_runtimeHandlersInitialized) {
			RegisterRuntimeHandlers();
			m_runtimeHandlersInitialized = true;
		}
		m_dispatchInitialized = true;

		m_running = true;
		return true;
	}

	bool GatewayHost::CreateRuntimeState(
		const blazeclaw::config::GatewayConfig& config) {
		if (config.bindAddress.empty() || config.port == 0) {
			m_lastWarning = "Invalid gateway bind configuration.";
			return false;
		}

		m_bindAddress = ToNarrow(config.bindAddress);
		m_port = config.port;
		m_runtimeGatewayBind = m_bindAddress;
		m_runtimeGatewayPort = m_port;

		if (!m_stageRuntimeHost) {
			m_stageRuntimeHost = std::make_unique<GatewayHostEx>(
				GatewayHostExDependencies{
					.routeLegacyRequest = [this](const protocol::RequestFrame& request) {
						return RouteRequestLegacy(request);
					},
					.isLegacyHealthy = [this]() {
						return IsHealthy();
					},
					.stagePipeline = &m_chatRunPipelineOrchestrator,
				});
		}

		return true;
	}

	bool GatewayHost::StartRuntimeServices() {
		PluginHostAdapter::EnsureDefaultAdaptersRegistered();
		m_runtimeDeepSeekApiKey = ReadEnvironmentVariable("DEEPSEEK_API_KEY");
		const std::string deepSeekBaseUrl =
			ReadEnvironmentVariable("DEEPSEEK_BASE_URL");
		if (!deepSeekBaseUrl.empty()) {
			m_runtimeDeepSeekBaseUrl = deepSeekBaseUrl;
		}

		if (!m_runtimeDeepSeekApiKey.empty() &&
			(m_runtimeAgentModel == GatewayModel::kDefaultModelId ||
				m_runtimeAgentModel == GatewayModel::kReasonerModelId)) {
			m_runtimeAgentModel = m_runtimeDeepSeekDefaultModel;
		}

		const std::string catalogPath = ResolveExtensionsCatalogPath();
		const auto extensionCatalogLoadStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		m_toolRegistry.LoadExtensionToolsFromCatalog(catalogPath);
		const auto extensionCatalogLoadEndMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		EmitTelemetryEvent(
			"gateway.startup.extensions.catalog_load",
			std::string("{\"catalogPath\":") + JsonString(catalogPath) +
			",\"elapsedMs\":" +
			std::to_string(extensionCatalogLoadEndMs >= extensionCatalogLoadStartMs
				? (extensionCatalogLoadEndMs - extensionCatalogLoadStartMs)
				: 0) +
			"}");
		m_runtimeResolvedSkillDirectories = ResolveAbsoluteSkillDirectories({
			"blazeclaw/skills-bundled",
			"blazeclaw/skills",
			"blazeclaw/skills-openclaw-original",
			"skills",
			"skills-openclaw-original",
		});
		EmitSkillRootDiagnostics("start_runtime_services", m_runtimeResolvedSkillDirectories);
		const auto startupSkillLoadStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		std::size_t startupSkillLoadedTotal = 0;
		for (const auto& directory : m_runtimeResolvedSkillDirectories) {
			const auto loadStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			const auto loadedCount = m_toolRegistry.LoadSkillToolsFromDirectory(directory);
			startupSkillLoadedTotal += loadedCount;
			const auto loadEndMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			EmitTelemetryEvent(
				"gateway.skills.root.load",
				std::string("{\"stage\":\"start_runtime_services\",\"root\":") + JsonString(directory) +
				",\"loadedCount\":" + std::to_string(loadedCount) +
				",\"elapsedMs\":" + std::to_string(loadEndMs >= loadStartMs ? (loadEndMs - loadStartMs) : 0) +
				"}");
		}
		const auto startupSkillLoadEndMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		EmitTelemetryEvent(
			"gateway.startup.skills.aggregate_load",
			std::string("{\"roots\":") + std::to_string(m_runtimeResolvedSkillDirectories.size()) +
			",\"loadedTotal\":" + std::to_string(startupSkillLoadedTotal) +
			",\"elapsedMs\":" +
			std::to_string(startupSkillLoadEndMs >= startupSkillLoadStartMs
				? (startupSkillLoadEndMs - startupSkillLoadStartMs)
				: 0) +
			"}");

		const auto extensionActivationStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		m_extensionLifecycle.LoadCatalog(catalogPath);
		m_extensionLifecycle.ActivateAll(m_toolRegistry);
		const auto extensionActivationEndMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count();
		EmitTelemetryEvent(
			"gateway.startup.extensions.activation",
			std::string("{\"catalogPath\":") + JsonString(catalogPath) +
			",\"elapsedMs\":" +
			std::to_string(extensionActivationEndMs >= extensionActivationStartMs
				? (extensionActivationEndMs - extensionActivationStartMs)
				: 0) +
			"}");
		const auto* extensionRegistry =
			m_pluginRuntimeState.RequireActiveRegistry(
				&m_extensionLifecycle.GetExtensions(),
				catalogPath,
				std::filesystem::current_path().string(),
				PluginRuntimeSubagentMode::GatewayBindable);
		for (const auto& extension : *extensionRegistry) {
			m_pluginRuntimeState.RecordImportedPluginId(extension.id);
		}
		m_pluginRuntimeState.ActivateRuntimeRegistry(
			extensionRegistry,
			catalogPath,
			std::filesystem::current_path().string(),
			PluginRuntimeSubagentMode::GatewayBindable,
			true,
			true);
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);
		EnsurePdfGeneratorRuntimeRegistered(m_toolRegistry);
		m_approvalStore.Initialize(ResolveGatewayStateFilePath("approvals.json").string());
		LoadPersistedTaskDeltas();

		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "python.script.run",
				.label = "Python Script Run",
				.category = "runtime",
				.enabled = true,
			},
			python::PythonRuntimeDispatcher::CreateExecutor());
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "python.runtime.health",
				.label = "Python Runtime Health",
				.category = "runtime",
				.enabled = true,
			},
			python::PythonRuntimeDispatcher::CreateDiagnosticsExecutor());
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "pdf_generator.generate",
				.label = "PDF Generator",
				.category = "document",
				.enabled = true,
			},
			python::PythonRuntimeDispatcher::CreateExecutor());

		WireCronProductionIntegration();
		cron::GetCronOpsService().StartBackgroundScheduler();

		return true;
	}

	bool GatewayHost::AttachTransportRuntime() {
		m_transport.SetHttpAuthCallbacks(
			GatewayHttpAuthPolicyCallbacks{
				.authorizeBearer = [this](
					const std::string& token,
					const GatewayHttpAuthRequestContext& context,
					std::string& failureReasonOut) {
						if (context.browserOriginPolicy == "origin_rejected") {
							failureReasonOut = "unauthorized";
							return false;
						}

						if (token.empty()) {
							failureReasonOut = "unauthorized";
							return false;
						}

						const std::vector<NodePairingPairedNode> paired =
							m_nodePairingService.ListPairedNodes();
						for (const auto& node : paired) {
							if (m_nodePairingService.VerifyNodeToken(
								node.declared.nodeId,
								token,
								nullptr)) {
								return true;
							}
						}

						failureReasonOut = "unauthorized";
						return false;
					},
				.checkRateLimit = [this](
					const GatewayHttpAuthRequestContext& context,
					std::string& failureReasonOut) {
						const std::string remoteKey =
							context.remoteIp.empty() ? "__unknown_remote_ip__" : context.remoteIp;
						auto& attempts = m_httpAuthBearerAttemptsByRemoteIp[remoteKey];
						const std::uint64_t nowMs = GatewayEpochMilliseconds();
						while (!attempts.empty() &&
							(nowMs - attempts.front()) > m_httpAuthRateLimitWindowMs) {
							attempts.pop_front();
						}

						if (attempts.size() >= m_httpAuthRateLimitMaxAttemptsPerWindow) {
							failureReasonOut = "rate_limited";
							return false;
						}

						attempts.push_back(nowMs);
						return true;
					},
				.authorizeCanvasCapability = [this](
					const std::string& capability,
					const GatewayHttpAuthRequestContext&,
					std::string& failureReasonOut) {
						if (m_nodeCanvasCapabilityService.VerifyCapabilityAndRefreshTtl(capability)) {
							return true;
						}
						failureReasonOut = "unauthorized";
						return false;
					},
				.observeDecision = nullptr,
			});
		return true;
	}

	bool GatewayHost::StartRuntimeSubscriptions() {
		return true;
	}

	bool GatewayHost::FinalizeRuntimeInitialization() {
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "chat.send",
				.label = "Chat Send",
				.category = "messaging",
				.enabled = true,
			},
			[this](
				const std::string& requestedTool,
				const std::optional<std::string>& argsJson) {
					const protocol::RequestFrame runtimeRequest{
						.id = "tool-runtime-" + std::to_string(GatewayEpochMilliseconds()),
						.method = requestedTool,
						.paramsJson = argsJson,
					};

					const protocol::ResponseFrame runtimeResponse =
						RouteRequest(runtimeRequest);
					if (!runtimeResponse.ok) {
						const std::string detail = runtimeResponse.error.has_value()
							? (runtimeResponse.error->code +
								":" +
								runtimeResponse.error->message)
							: "runtime_execution_failed";
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = false,
							.status = "error",
							.output = detail,
						};
					}

					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = runtimeResponse.payloadJson.value_or("{}"),
					};
			});
		m_toolRegistry.RegisterRuntimeTool(
			ToolCatalogEntry{
				.id = "memory.search",
				.label = "Memory Search",
				.category = "knowledge",
				.enabled = true,
			},
			[this](
				const std::string& requestedTool,
				const std::optional<std::string>& argsJson) {
					if (!argsJson.has_value()) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = false,
							.status = "invalid_args",
							.output = "missing_args",
						};
					}

					std::string query;
					std::string sessionKey;
					json::FindStringField(argsJson.value(), "query", query);
					json::FindStringField(argsJson.value(), "sessionKey", sessionKey);
					if (json::Trim(query).empty()) {
						return ToolExecuteResult{
							.tool = requestedTool,
							.executed = false,
							.status = "invalid_args",
							.output = "query_required",
						};
					}

					std::uint64_t requestedLimit = 3;
					json::FindUInt64Field(argsJson.value(), "limit", requestedLimit);
					const std::size_t limit = static_cast<std::size_t>((std::max)(
						std::uint64_t{ 1 },
						(std::min)(requestedLimit, std::uint64_t{ 10 })));

					const std::string normalizedSession =
						json::Trim(sessionKey).empty() ? "main" : json::Trim(sessionKey);
					const std::string loweredQuery = ToLowerCopy(query);

					std::vector<std::string> matches;
					const auto historyIt = m_chatHistoryBySession.find(normalizedSession);
					if (historyIt != m_chatHistoryBySession.end()) {
						for (const auto& messageJson : historyIt->second) {
							if (ToLowerCopy(messageJson).find(loweredQuery) ==
								std::string::npos) {
								continue;
							}

							matches.push_back(TruncateForMatch(messageJson, 180));
							if (matches.size() >= limit) {
								break;
							}
						}
					}

					return ToolExecuteResult{
						.tool = requestedTool,
						.executed = true,
						.status = "ok",
						.output = BuildMemorySearchEnvelope(normalizedSession, matches),
					};
			});

		m_fixtureParityValidated = false;
		m_stageRuntimeHost.reset();

		// Emit telemetry about startup configuration (mask sensitive values)
		{
			const std::string payload =
				"{\"bind\":" +
				JsonString(m_runtimeGatewayBind) +
				",\"port\":" +
				std::to_string(m_runtimeGatewayPort) +
				",\"agentModel\":" +
				JsonString(m_runtimeAgentModel) +
				"}";
			EmitTelemetryEvent("gateway.startup.config", payload);
		}

		std::string methodSurfaceViolation;
		if (!VerifyRuntimeMethodSurfaceInvariants(methodSurfaceViolation)) {
			EmitTelemetryEvent(
				"gateway.method_surface.invariant_failed",
				std::string("{\"reason\":") + JsonString(methodSurfaceViolation) + "}");
		}

		m_initialized = true;
		return true;
	}

	bool GatewayHost::InitializeRuntime(const blazeclaw::config::GatewayConfig& config) {
		if (!BootstrapCreateRuntimeState(config)) {
			return false;
		}
		if (!BootstrapStartRuntimeServices()) {
			return false;
		}
		if (!BootstrapAttachTransportHandlers()) {
			return false;
		}
		if (!BootstrapStartRuntimeSubscriptions()) {
			return false;
		}
		return BootstrapFinalizeRuntimeInitialization();
	}

	bool GatewayHost::Start(const blazeclaw::config::GatewayConfig& config) {
		if (m_running) {
			return true;
		}
		if (!InitializeRuntime(config)) {
			return false;
		}

		std::string transportError;
		if (!m_transport.Start(
			m_bindAddress,
			m_port,
			[this](const std::string& inboundFrame) {
				return HandleInboundText(inboundFrame);
			},
			transportError)) {
			m_lastWarning = transportError;
			return false;
		}

		m_running = true;
		return true;
	}

	bool GatewayHost::StartLocalOnly(const blazeclaw::config::GatewayConfig& config) {
		if (!InitializeRuntime(config)) {
			return false;
		}

		m_running = true;
		return true;
	}

	void GatewayHost::NotifyPluginGlobalStopPrelude() {
		PluginHostAdapter::UnloadAllLoadedExtensionRuntimes();
		m_pluginRuntimeState.RecordGlobalStopPreludeTransition();
	}

	void GatewayHost::Stop() {
		cron::GetCronOpsService().StopBackgroundScheduler();
		{
			std::lock_guard<std::mutex> lock(m_cronProductionMutex);
			cron::GetCronOpsService().SetRuntimeExecutionAdapters({});
			cron::GetCronOpsService().SetTaskLedgerHooks({});
			m_cronProductionWired = false;
		}
		m_transport.Stop();
		// Deactivate registered extension tools and clear approval state
		m_extensionLifecycle.DeactivateAll(m_toolRegistry);
		m_pluginRuntimeState.DeactivateRuntimeRegistry();
		PersistTaskDeltas();
		m_running = false;
		m_initialized = false;
		m_dispatchInitialized = false;
		m_runtimeHandlersInitialized = false;
		m_fixtureParityValidated = false;
		m_runtimeContext = {};
		m_bindAddress.clear();
		m_port = 0;
	}

	void GatewayHost::SetSkillsCatalogState(SkillsCatalogGatewayState state) {
		std::vector<ToolCatalogEntry> catalogSkillTools;
		catalogSkillTools.reserve(state.entries.size());
		std::unordered_set<std::string> expectedDispatchToolIds;
		for (const auto& entry : state.entries) {
			if (entry.commandToolName.empty()) {
				continue;
			}

			const std::string toolId = json::Trim(entry.commandToolName);
			if (toolId.empty()) {
				continue;
			}
			expectedDispatchToolIds.insert(toolId);

			catalogSkillTools.push_back(ToolCatalogEntry{
				.id = toolId,
				.label = entry.commandName.empty() ? toolId : entry.commandName,
				.category = "skill",
				.skillKey = entry.skillKey.empty() ? entry.name : entry.skillKey,
				.installKind = "skill",
				.source = "skills.catalog",
				.enabled = !entry.disabled,
				});
		}

		const std::vector<std::string> resolvedDirectories =
			ResolveAbsoluteSkillDirectories(
				std::vector<std::string>{
					"blazeclaw/skills-bundled",
					"blazeclaw/skills",
					"blazeclaw/skills-openclaw-original",
					"skills",
					"skills-openclaw-original",
				},
				m_preferredSkillRootDirectories.empty());
		EmitSkillRootDiagnostics("set_skills_catalog_state", resolvedDirectories);
		m_toolRegistry.SyncSkillToolsManifestFirst(
			resolvedDirectories,
			catalogSkillTools,
			true);

		const auto runtimeTools = m_toolRegistry.List();
		std::unordered_set<std::string> runtimeSkillToolIds;
		runtimeSkillToolIds.reserve(runtimeTools.size());
		for (const auto& tool : runtimeTools) {
			if (tool.category != "skill") {
				continue;
			}

			runtimeSkillToolIds.insert(tool.id);
		}

		state.projectedToolDispatchCount = expectedDispatchToolIds.size();
		state.runtimeRegisteredSkillToolCount = runtimeSkillToolIds.size();
		state.executionReadinessMismatchCount = 0;
		state.executionReadinessMismatchSample.clear();
		for (const auto& expectedToolId : expectedDispatchToolIds) {
			if (runtimeSkillToolIds.find(expectedToolId) != runtimeSkillToolIds.end()) {
				continue;
			}

			++state.executionReadinessMismatchCount;
			if (state.executionReadinessMismatchSample.size() < 10) {
				state.executionReadinessMismatchSample.push_back(expectedToolId);
			}
		}
		for (const auto& runtimeToolId : runtimeSkillToolIds) {
			if (expectedDispatchToolIds.find(runtimeToolId) != expectedDispatchToolIds.end()) {
				continue;
			}

			++state.executionReadinessMismatchCount;
			if (state.executionReadinessMismatchSample.size() < 10) {
				state.executionReadinessMismatchSample.push_back(runtimeToolId);
			}
		}
		state.effectiveSkillRootCount = resolvedDirectories.size();
		state.effectiveSkillRoots = resolvedDirectories;

		m_skillsCatalogState = std::move(state);
	}

	void GatewayHost::SetSkillsRefreshCallback(
		SkillsRefreshCallback callback) {
		m_skillsRefreshCallback = std::move(callback);
	}

	void GatewayHost::SetSkillsUpdateCallback(
		SkillsUpdateCallback callback) {
		m_skillsUpdateCallback = std::move(callback);
	}

	void GatewayHost::SetConfigSchemaGetCallback(
		ConfigSchemaGetCallback callback) {
		m_configSchemaGetCallback = std::move(callback);
	}

	void GatewayHost::SetConfigSchemaLookupCallback(
		ConfigSchemaLookupCallback callback) {
		m_configSchemaLookupCallback = std::move(callback);
	}

	void GatewayHost::SetEmbeddedOrchestrationPath(
		const std::string& path) {
		const std::string normalized = ToLowerCopy(json::Trim(path));
		m_stagePipelineFeatureEnabled = true;
		m_stagePipelineRolloutCohort = "stage_pipeline_full";
		if (normalized == "runtime_orchestration") {
			m_embeddedOrchestrationPath = normalized;
			m_stagePipelineFeatureEnabled = false;
			m_stagePipelineRolloutCohort = "compat_runtime_orchestration";
			return;
		}
		if (normalized == "legacy_only" ||
			normalized == "stage_pipeline_off") {
			m_embeddedOrchestrationPath = "dynamic_task_delta";
			m_stagePipelineFeatureEnabled = false;
			m_stagePipelineRolloutCohort = "stage_pipeline_off";
			return;
		}
		if (normalized == "stage_pipeline_canary") {
			m_embeddedOrchestrationPath = "dynamic_task_delta";
			m_stagePipelineFeatureEnabled = true;
			m_stagePipelineRolloutCohort = "canary";
			return;
		}
		if (normalized == "stage_pipeline_full") {
			m_embeddedOrchestrationPath = "dynamic_task_delta";
			m_stagePipelineFeatureEnabled = true;
			m_stagePipelineRolloutCohort = "stage_pipeline_full";
			return;
		}

		m_embeddedOrchestrationPath = "dynamic_task_delta";
	}

	void GatewayHost::SetEmailFallbackRuntimeFlags(
		bool preflightEnabled,
		bool policyProfilesEnabled,
		bool policyProfilesEnforce) {
		m_runtimeEmailPreflightEnabled = preflightEnabled;
		m_runtimeEmailPolicyProfilesEnabled = policyProfilesEnabled;
		m_runtimeEmailPolicyProfilesEnforce = policyProfilesEnforce;
	}

	void GatewayHost::SetEmailFallbackResolvedPolicy(
		const std::vector<std::string>& backends,
		const std::string& onUnavailable,
		const std::string& onAuthError,
		const std::string& onExecError,
		std::uint32_t retryMaxAttempts,
		std::uint32_t retryDelayMs,
		bool requiresApproval,
		std::uint32_t approvalTokenTtlMinutes,
		const std::string& profileId) {
		m_runtimeEmailResolvedBackends = backends;
		m_runtimeEmailPolicyOnUnavailable = onUnavailable;
		m_runtimeEmailPolicyOnAuthError = onAuthError;
		m_runtimeEmailPolicyOnExecError = onExecError;
		m_runtimeEmailRetryMaxAttempts = retryMaxAttempts;
		m_runtimeEmailRetryDelayMs = retryDelayMs;
		m_runtimeEmailRequiresApproval = requiresApproval;
		m_runtimeEmailApprovalTokenTtlMinutes = approvalTokenTtlMinutes;
		m_runtimeEmailPolicyProfileId = profileId;

		if (!m_runtimeEmailResolvedBackends.empty()) {
			std::string joined;
			for (std::size_t i = 0; i < m_runtimeEmailResolvedBackends.size(); ++i) {
				if (i > 0) {
					joined += ",";
				}
				joined += m_runtimeEmailResolvedBackends[i];
			}
			_putenv_s("BLAZECLAW_EMAIL_POLICY_BACKENDS", joined.c_str());
		}
		else {
			_putenv_s("BLAZECLAW_EMAIL_POLICY_BACKENDS", "");
		}

		_putenv_s("BLAZECLAW_EMAIL_POLICY_ACTION_UNAVAILABLE", m_runtimeEmailPolicyOnUnavailable.c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_ACTION_AUTH_ERROR", m_runtimeEmailPolicyOnAuthError.c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_ACTION_EXEC_ERROR", m_runtimeEmailPolicyOnExecError.c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_RETRY_MAX_ATTEMPTS", std::to_string(m_runtimeEmailRetryMaxAttempts).c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_RETRY_DELAY_MS", std::to_string(m_runtimeEmailRetryDelayMs).c_str());
		_putenv_s("BLAZECLAW_EMAIL_APPROVAL_TOKEN_TTL_MINUTES", std::to_string(m_runtimeEmailApprovalTokenTtlMinutes).c_str());
		_putenv_s("BLAZECLAW_EMAIL_POLICY_PROFILE_ID", m_runtimeEmailPolicyProfileId.c_str());
	}

	void GatewayHost::SetNodeParityRuntimeFlags(
		bool enabled,
		bool diagnosticsEnabled,
		const std::string& rolloutMode) {
		m_runtimeNodeParityEnabled = enabled;
		m_runtimeNodeParityDiagnosticsEnabled = diagnosticsEnabled;
		m_runtimeNodeParityRolloutMode = rolloutMode.empty() ? "monitor" : rolloutMode;
	}

	void GatewayHost::LoadPersistedTaskDeltas() {
		const std::filesystem::path persistencePath =
			ResolveGatewayStateFilePath("taskdeltas.state");
		std::ifstream input(persistencePath, std::ios::in | std::ios::binary);
		if (!input.is_open()) {
			return;
		}

		std::ostringstream buffer;
		buffer << input.rdbuf();
		const std::string jsonState = buffer.str();
		if (jsonState.empty()) {
			return;
		}

		if (jsonState.size() > m_taskDeltasMaxPayloadBytes) {
			return;
		}

		nlohmann::json root;
		try {
			root = nlohmann::json::parse(jsonState);
		}
		catch (...) {
			return;
		}

		if (!root.is_object() || !root.contains("runs") || !root["runs"].is_array()) {
			return;
		}

		m_taskDeltaRepository.ClearAll();
		for (const auto& runNode : root["runs"]) {
			if (!runNode.is_object() ||
				!runNode.contains("runId") ||
				!runNode["runId"].is_string() ||
				!runNode.contains("taskDeltas") ||
				!runNode["taskDeltas"].is_array()) {
				continue;
			}

			const std::string runId = runNode["runId"].get<std::string>();
			if (runId.empty()) {
				continue;
			}

			std::vector<ChatRuntimeResult::TaskDeltaEntry> parsedDeltas;
			for (const auto& deltaNode : runNode["taskDeltas"]) {
				if (!deltaNode.is_object()) {
					continue;
				}

				ChatRuntimeResult::TaskDeltaEntry delta;
				delta.index = deltaNode.value("index", std::size_t{ 0 });
				delta.schemaVersion = deltaNode.value("schemaVersion", std::uint32_t{ 1 });
				delta.runId = deltaNode.value("runId", runId);
				delta.sessionId = deltaNode.value("sessionId", std::string{});
				delta.phase = deltaNode.value("phase", std::string{});
				delta.toolName = deltaNode.value("toolName", std::string{});
				delta.fallbackBackend = deltaNode.value("fallbackBackend", std::string{});
				delta.fallbackAction = deltaNode.value("fallbackAction", std::string{});
				delta.fallbackAttempt = deltaNode.value("fallbackAttempt", std::size_t{ 0 });
				delta.fallbackMaxAttempts = deltaNode.value("fallbackMaxAttempts", std::size_t{ 0 });
				delta.argsJson = deltaNode.value("argsJson", std::string{});
				delta.resultJson = deltaNode.value("resultJson", std::string{});
				delta.status = deltaNode.value("status", std::string{});
				delta.errorCode = deltaNode.value("errorCode", std::string{});
				delta.errorMessage = deltaNode.value("errorMessage", std::string{});
				delta.startedAtMs = deltaNode.value("startedAtMs", std::uint64_t{ 0 });
				delta.completedAtMs = deltaNode.value("completedAtMs", std::uint64_t{ 0 });
				delta.latencyMs = deltaNode.value("latencyMs", std::uint64_t{ 0 });
				delta.modelTurnId = deltaNode.value("modelTurnId", std::string{});
				delta.stepLabel = deltaNode.value("stepLabel", std::string{});
				parsedDeltas.push_back(std::move(delta));
			}

			std::sort(
				parsedDeltas.begin(),
				parsedDeltas.end(),
				[](const ChatRuntimeResult::TaskDeltaEntry& left,
					const ChatRuntimeResult::TaskDeltaEntry& right) {
						return left.index < right.index;
				});

			std::string fallbackSessionId;
			for (const auto& delta : parsedDeltas) {
				if (!delta.sessionId.empty()) {
					fallbackSessionId = delta.sessionId;
					break;
				}
			}

			auto normalizedDeltas = TaskDeltaLegacyAdapter::AdaptRun(
				runId,
				fallbackSessionId,
				parsedDeltas);

			const bool hasPlanPhase = std::any_of(
				normalizedDeltas.begin(),
				normalizedDeltas.end(),
				[](const ChatRuntimeResult::TaskDeltaEntry& delta) {
					return delta.phase == "plan";
				});
			if (!hasPlanPhase) {
				normalizedDeltas.insert(
					normalizedDeltas.begin(),
					ChatRuntimeResult::TaskDeltaEntry{
						.index = 0,
						.runId = runId,
						.sessionId = fallbackSessionId.empty() ? "main" : fallbackSessionId,
						.phase = "plan",
						.resultJson = "[]",
						.status = "ok",
						.errorCode = {},
						.startedAtMs = 0,
						.completedAtMs = 0,
						.latencyMs = 0,
						.stepLabel = "execution_plan",
					});
			}

			const bool hasFinalPhase = std::any_of(
				normalizedDeltas.begin(),
				normalizedDeltas.end(),
				[](const ChatRuntimeResult::TaskDeltaEntry& delta) {
					return delta.phase == "final";
				});
			if (!hasFinalPhase) {
				normalizedDeltas.push_back(
					ChatRuntimeResult::TaskDeltaEntry{
						.index = normalizedDeltas.size(),
						.runId = runId,
						.sessionId = fallbackSessionId.empty() ? "main" : fallbackSessionId,
						.phase = "final",
						.resultJson = {},
						.status = "completed",
						.errorCode = {},
						.startedAtMs = 0,
						.completedAtMs = 0,
						.latencyMs = 0,
						.stepLabel = "run_terminal",
					});
			}

			for (std::size_t i = 0; i < normalizedDeltas.size(); ++i) {
				normalizedDeltas[i] = TaskDeltaLegacyAdapter::AdaptEntry(
					normalizedDeltas[i],
					runId,
					fallbackSessionId,
					i);
			}

			std::string schemaErrorCode;
			std::string schemaErrorMessage;
			if (!TaskDeltaSchemaValidator::ValidateRun(
				runId,
				normalizedDeltas,
				schemaErrorCode,
				schemaErrorMessage)) {
				continue;
			}

			std::uint64_t loadActivity = 0;
			for (const auto& d : normalizedDeltas) {
				loadActivity = (std::max)(loadActivity, d.startedAtMs);
				loadActivity = (std::max)(loadActivity, d.completedAtMs);
			}
			if (loadActivity == 0) {
				loadActivity = 1;
			}

			const bool upserted = m_taskDeltaRepository.Upsert(
				runId,
				normalizedDeltas,
				loadActivity);
			(void)upserted;
		}

		const std::size_t evictedFromLoad =
			m_taskDeltaRepository.EnforceRetentionLimit(m_taskDeltasRetentionLimit);
		if (evictedFromLoad > 0) {
			EmitTelemetryEvent(
				"gateway.taskdelta.retention.evicted",
				std::string("{\"evictedRuns\":") + std::to_string(evictedFromLoad) +
				",\"remainingRuns\":" + std::to_string(m_taskDeltaRepository.Size()) +
				",\"reason\":\"persistence_load\"}");
		}

		PersistTaskDeltas();
	}

	void GatewayHost::PersistTaskDeltas() const {
		const std::filesystem::path persistencePath =
			ResolveGatewayStateFilePath("taskdeltas.state");
		std::error_code ec;
		std::filesystem::create_directories(persistencePath.parent_path(), ec);

		const auto& taskDeltaState = m_taskDeltaRepository.Snapshot();
		const std::string jsonState = SerializeTaskDeltaState(taskDeltaState);
		if (jsonState.size() > m_taskDeltasMaxPayloadBytes) {
			return;
		}

		std::ofstream output(
			persistencePath,
			std::ios::out | std::ios::trunc | std::ios::binary);
		if (!output.is_open()) {
			return;
		}

		output.write(jsonState.data(), static_cast<std::streamsize>(jsonState.size()));
	}

	void GatewayHost::SetChatRuntimeCallback(
		ChatRuntimeCallback callback) {
		m_chatRuntimeCallback = std::move(callback);
	}

	void GatewayHost::SetChatAbortCallback(
		ChatAbortCallback callback) {
		m_chatAbortCallback = std::move(callback);
	}

	void GatewayHost::SetEmbeddingsGenerateCallback(
		EmbeddingsGenerateCallback callback) {
		m_embeddingsGenerateCallback = std::move(callback);
	}

	void GatewayHost::SetEmbeddingsBatchCallback(
		EmbeddingsBatchCallback callback) {
		m_embeddingsBatchCallback = std::move(callback);
	}

	void GatewayHost::SetSpeechExecutionUpdateCallback(
		SpeechExecutionUpdateCallback callback) {
		m_speechExecutionUpdateCallback = std::move(callback);
	}

	void GatewayHost::SetSpeechTranscribeAcceptedCallback(
		SpeechTranscribeAcceptedCallback callback) {
		m_speechTranscribeAcceptedCallback = std::move(callback);
	}

	GatewayHost::SpeechExecutionAccepted GatewayHost::AcceptSpeechTranscription(
		const SpeechExecutionRequest& request) const {
		if (!m_speechTranscribeAcceptedCallback) {
			SpeechExecutionAccepted accepted;
			accepted.accepted = false;
			accepted.executionState.sessionId = request.sessionId;
			accepted.executionState.runId = request.runId;
			accepted.executionState.audioPath = request.audioPath;
			accepted.executionState.audioArtifact = request.audioArtifact;
			accepted.executionState.language = request.language;
			accepted.executionState.prompt = request.prompt;
			accepted.executionState.stage = blazeclaw::core::speechrecognition::SpeechExecutionStage::Failed;
			accepted.errorCode = "speech_runtime_unavailable";
			accepted.errorMessage = "speech transcription coordinator callback is not configured";
			return accepted;
		}

		return m_speechTranscribeAcceptedCallback(request);
	}

	void GatewayHost::SetSpeechExecutionStatusCallback(
		SpeechExecutionStatusCallback callback) {
		m_speechExecutionStatusCallback = std::move(callback);
	}

	GatewayHost::SpeechExecutionStatus GatewayHost::GetSpeechExecutionStatus(
		const std::string& runId) const {
		if (!m_speechExecutionStatusCallback) {
			SpeechExecutionStatus status;
			status.found = false;
			status.errorCode = "speech_runtime_unavailable";
			status.errorMessage = "speech transcription status callback is not configured";
			return status;
		}

		return m_speechExecutionStatusCallback(runId);
	}

	void GatewayHost::SetSpeechCancelCallback(
		SpeechCancelCallback callback) {
		m_speechCancelCallback = std::move(callback);
	}

	bool GatewayHost::CancelSpeechTranscription(const std::string& runId) const {
		if (!m_speechCancelCallback) {
			return false;
		}

		return m_speechCancelCallback(runId);
	}

	void GatewayHost::SetSpeechTranscribeCallback(
		SpeechTranscribeCallback callback) {
		m_speechTranscribeCallback = std::move(callback);
	}

	GatewayHost::SpeechTranscribeResult GatewayHost::TranscribeSpeech(
		const SpeechTranscribeRequest& request) const {
		if (!m_speechTranscribeCallback) {
			return SpeechTranscribeResult{
				.ok = false,
				.cancelled = false,
				.text = {},
				.language = {},
				.latencyMs = 0,
				.sessionState = blazeclaw::core::speechrecognition::SpeechSessionState{
					.sessionId = request.sessionId,
					.runId = request.runId,
					.stage = blazeclaw::core::speechrecognition::SpeechSessionStage::Failed,
					.audioPath = request.audioPath,
				},
				.errorCode = "speech_runtime_unavailable",
				.errorMessage = "speech transcription runtime callback is not configured",
			};
		}

		return m_speechTranscribeCallback(request);
	}

	void GatewayHost::SetSpeechSpeakCallback(
		SpeechSpeakCallback callback) {
		m_speechSpeakCallback = std::move(callback);
	}

	GatewayHost::SpeechSpeakResult GatewayHost::SpeakSpeech(
		const SpeechSpeakRequest& request) const {
		if (!m_speechSpeakCallback) {
			return SpeechSpeakResult{
				.ok = false,
				.cancelled = false,
				.speaking = false,
				.utteranceId = {},
				.normalizedText = request.text,
				.audioPath = {},
				.voice = request.voice,
				.provider = request.provider,
				.model = request.model,
				.latencyMs = 0,
				.status = "unavailable",
				.errorCode = "tts_runtime_unavailable",
				.errorMessage = "speech synthesis runtime callback is not configured",
			};
		}

		return m_speechSpeakCallback(request);
	}

	void GatewayHost::SetSpeechStopCallback(
		SpeechStopCallback callback) {
		m_speechStopCallback = std::move(callback);
	}

	GatewayHost::SpeechStopResult GatewayHost::StopSpeech(
		const SpeechStopRequest& request) const {
		if (!m_speechStopCallback) {
			return SpeechStopResult{
				.ok = false,
				.stopped = false,
				.utteranceId = request.utteranceId,
				.status = "unavailable",
				.errorCode = "tts_runtime_unavailable",
				.errorMessage = "speech synthesis runtime callback is not configured",
			};
		}

		return m_speechStopCallback(request);
	}

	void GatewayHost::SetSpeechStatusCallback(
		SpeechStatusCallback callback) {
		m_speechStatusCallback = std::move(callback);
	}

	GatewayHost::SpeechStatusResult GatewayHost::GetSpeechStatus() const {
		if (!m_speechStatusCallback) {
			return SpeechStatusResult{
				.supported = true,
				.ready = false,
				.speaking = false,
				.utteranceId = {},
				.provider = "default",
				.model = "default",
				.voice = "default",
				.status = "unavailable",
				.errorCode = "tts_runtime_unavailable",
				.errorMessage = "speech synthesis runtime callback is not configured",
			};
		}

		return m_speechStatusCallback();
	}

	void GatewayHost::SetSpeechRecognitionRuntimeStatusCallback(
		SpeechRecognitionRuntimeStatusCallback callback) {
		m_speechRecognitionRuntimeStatusCallback = std::move(callback);
	}

	GatewayHost::SpeechRecognitionRuntimeStatus GatewayHost::GetSpeechRecognitionRuntimeStatus() const {
		if (!m_speechRecognitionRuntimeStatusCallback) {
			return SpeechRecognitionRuntimeStatus{
				.enabled = false,
				.ready = false,
				.status = "unavailable",
				.provider = "onnx",
				.modelPath = {},
				.runtimeHotMode = "always_online",
				.runtimeHotLifecycleState = "cold",
				.runtimeHotWarmupEnabled = false,
				.runtimeHotWarmupRuns = 0,
				.runtimeHotIdleTimeoutMs = 0,
				.effectiveExecutionProvider = "cpu",
			};
		}

		return m_speechRecognitionRuntimeStatusCallback();
	}

	void GatewayHost::SetParityLifecycleExportCallback(
		ParityLifecycleExportCallback callback) {
		m_parityLifecycleExport = std::move(callback);
	}

	std::string GatewayHost::ExportParityLifecycleTraceJson() const {
		if (!m_parityLifecycleExport) {
			return
				"{\"ok\":false,\"error\":"
				"\"gateway.parity.lifecycle_export_unconfigured\""
				",\"schemaVersion\":" +
				std::to_string(
					blazeclaw::core::GatewayParityLifecycleContract::kSchemaVersion) +
				"}";
		}
		return std::string("{\"ok\":true,\"schemaVersion\":") +
			std::to_string(
				blazeclaw::core::GatewayParityLifecycleContract::kSchemaVersion) +
			",\"contract\":" +
			m_parityLifecycleExport() +
			"}";
	}

	std::vector<ToolCatalogEntry> GatewayHost::ListRuntimeTools() const {
		return m_toolRegistry.List();
	}

	ToolExecuteResult GatewayHost::ExecuteRuntimeTool(
		const std::string& tool,
		const std::optional<std::string>& argsJson) {
		return m_toolRegistry.Execute(tool, argsJson);
	}

	ToolExecuteResultV2 GatewayHost::ExecuteRuntimeToolV2(
		const ToolExecuteRequestV2& request) {
#ifdef _DEBUG
		const std::string payload =
			std::string("{\"tool\":") + JsonString(request.tool) +
			",\"argsJson\":" + (request.argsJson.has_value() ? JsonString(request.argsJson.value()) : "null") +
			"}";
		//EmitTelemetryEvent("gateway.tool.execute.v2.request", payload);
		//EmitTelemetryEvent("gateway.skills.execute.v2.request", payload);
		const std::string findLine =
			std::string("[skills.execute.v2.request] tool=") + request.tool +
			", payload=" + payload;
		if (auto* app = AfxGetApp(); app != nullptr && app->m_pMainWnd != nullptr) {
			auto* line = new CString(CA2W(findLine.c_str(), CP_UTF8));
			if (!app->m_pMainWnd->PostMessage(kMsgAppendToolStatusLine, 0, reinterpret_cast<LPARAM>(line))) {
				delete line;
			}
		}

		TRACE(L"【GatewayHost::ExecuteRuntimeToolV2】 tool=%s, argsJson=%s",
			request.tool.c_str(),
			request.argsJson.has_value() ? request.argsJson->c_str() : "null");
#endif

		return m_toolRegistry.ExecuteV2(request);
	}

	void GatewayHost::RegisterRuntimeTool(
		const ToolCatalogEntry& tool,
		GatewayToolRegistry::RuntimeToolExecutor executor) {
		m_toolRegistry.RegisterRuntimeTool(tool, std::move(executor));
	}

	void GatewayHost::RegisterRuntimeToolV2(
		const ToolCatalogEntry& tool,
		GatewayToolRegistry::RuntimeToolExecutorV2 executor) {
		m_toolRegistry.RegisterRuntimeToolV2(tool, std::move(executor));
	}

	void GatewayHost::ReloadSkillToolsFromDirectories(
		const std::vector<std::string>& directories,
		const bool emitCatalogUpdateEvent) {
		const std::vector<std::string> resolvedDirectories =
			ResolveAbsoluteSkillDirectories(directories, false);
		EmitSkillRootDiagnostics("reload_skill_tools_from_directories", resolvedDirectories);
		for (const auto& directory : resolvedDirectories) {
			const auto loadStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			const auto loadedCount = m_toolRegistry.LoadSkillToolsFromDirectory(directory);
			const auto loadEndMs = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()).count();
			EmitTelemetryEvent(
				"gateway.skills.root.load",
				std::string("{\"stage\":\"reload_skill_tools_from_directories\",\"root\":") + JsonString(directory) +
				",\"loadedCount\":" + std::to_string(loadedCount) +
				",\"elapsedMs\":" + std::to_string(loadEndMs >= loadStartMs ? (loadEndMs - loadStartMs) : 0) +
				"}");
		}

		if (!emitCatalogUpdateEvent) {
			return;
		}

		std::string broadcastError;
		m_transport.BroadcastOutboundFrame(
			BuildToolsCatalogUpdateEventFrame(++m_chatPushEventSeq),
			broadcastError);
		EmitTelemetryEvent(
			"gateway.tools.catalog.update",
			std::string("{\"source\":\"openclaw-original\",\"broadcastError\":") +
			JsonString(broadcastError) +
			"}");
	}

	void GatewayHost::SetPreferredSkillRootDirectories(std::vector<std::string> directories) {
		m_preferredSkillRootDirectories.clear();
		m_preferredSkillRootDirectories.reserve(directories.size());
		for (auto& directory : directories) {
			if (!directory.empty()) {
				m_preferredSkillRootDirectories.push_back(std::move(directory));
			}
		}
	}

	std::vector<std::string> GatewayHost::ResolveAbsoluteSkillDirectories(
		const std::vector<std::string>& hintDirectories,
		const bool includeDefaultRoots) const {
		wchar_t modulePathBuffer[MAX_PATH] = {};
		std::filesystem::path moduleDir;
		if (GetModuleFileNameW(nullptr, modulePathBuffer, MAX_PATH) > 0) {
			moduleDir = std::filesystem::path(modulePathBuffer).parent_path();
		}

		const std::filesystem::path currentDir = std::filesystem::current_path();
		std::set<std::string> seen;
		std::vector<std::string> resolved;

		const auto pushUnique = [&seen, &resolved](const std::filesystem::path& input) {
			if (input.empty()) {
				return;
			}

			std::error_code ec;
			const auto canonical = std::filesystem::weakly_canonical(input, ec);
			const auto normalized = ec ? input.lexically_normal() : canonical.lexically_normal();
			if (normalized.empty()) {
				return;
			}

			const std::string value = normalized.string();
			if (seen.insert(value).second) {
				resolved.push_back(value);
			}
		};

		const auto readEnvOverride = [](const char* name) -> std::optional<std::string> {
			const std::string value = ReadEnvironmentVariable(name);
			if (value.empty()) {
				return std::nullopt;
			}
			return value;
		};

		if (includeDefaultRoots) {
			const std::optional<std::string> genericOverride =
				readEnvOverride("BLAZECLAW_SKILLS_ROOT");
			const std::optional<std::string> bundledOverride =
				readEnvOverride("BLAZECLAW_SKILLS_BUNDLED_ROOT");
			const std::optional<std::string> openClawOverride =
				readEnvOverride("BLAZECLAW_SKILLS_OPENCLAW_ROOT");

			for (const auto& path : skills::ResolveSkillRoots(
				skills::SkillRootKind::Bundled,
				moduleDir,
				currentDir,
				genericOverride,
				bundledOverride).resolvedRoots) {
				pushUnique(path);
			}
			for (const auto& path : skills::ResolveSkillRoots(
				skills::SkillRootKind::Core,
				moduleDir,
				currentDir,
				genericOverride,
				std::nullopt).resolvedRoots) {
				pushUnique(path);
			}
			for (const auto& path : skills::ResolveSkillRoots(
				skills::SkillRootKind::OpenClawOriginal,
				moduleDir,
				currentDir,
				genericOverride,
				openClawOverride).resolvedRoots) {
				pushUnique(path);
			}
		}

		for (const auto& preferred : m_preferredSkillRootDirectories) {
			if (!preferred.empty()) {
				pushUnique(preferred);
			}
		}

		for (const auto& hint : hintDirectories) {
			if (hint.empty()) {
				continue;
			}
			pushUnique(hint);
		}

		return resolved;
	}

	void GatewayHost::EmitSkillRootDiagnostics(
		const char* stage,
		const std::vector<std::string>& resolvedDirectories) const {
		if (stage == nullptr) {
			return;
		}

		std::string roots = "[]";
		if (!resolvedDirectories.empty()) {
			roots = "[";
			for (std::size_t i = 0; i < resolvedDirectories.size(); ++i) {
				if (i > 0) {
					roots += ",";
				}
				roots += JsonString(resolvedDirectories[i]);
			}
			roots += "]";
		}

		bool imapSmtpSkillDiscovered = false;
		bool imapSmtpManifestDiscovered = false;
		for (const auto& directory : resolvedDirectories) {
			std::error_code ec;
			if (!std::filesystem::exists(directory, ec) ||
				!std::filesystem::is_directory(directory, ec)) {
				continue;
			}

			for (const auto& child : std::filesystem::directory_iterator(directory, ec)) {
				if (ec) {
					break;
				}
				if (!child.is_directory()) {
					continue;
				}
				const std::string name = child.path().filename().string();
				if (name != "imap-smtp-email" && name != "imap_smtp_email") {
					continue;
				}
				imapSmtpSkillDiscovered = true;
				if (std::filesystem::exists(child.path() / "tool-manifest.json", ec)) {
					imapSmtpManifestDiscovered = true;
				}
			}
		}

		EmitTelemetryEvent(
			"gateway.skills.roots",
			std::string("{\"stage\":") + JsonString(stage) +
			",\"count\":" + std::to_string(resolvedDirectories.size()) +
			",\"roots\":" + roots +
			",\"imapSmtpSkillDiscovered\":" + std::string(imapSmtpSkillDiscovered ? "true" : "false") +
			",\"imapSmtpManifestDiscovered\":" + std::string(imapSmtpManifestDiscovered ? "true" : "false") +
			"}");
	}

	bool GatewayHost::IsRunning() const noexcept {
		return m_running;
	}

	std::string GatewayHost::LastWarning() const {
		auto* mutableThis = const_cast<GatewayHost*>(this);
		mutableThis->EnsureFixtureParityValidated();
		return m_lastWarning;
	}

	void GatewayHost::EnsureFixtureParityValidated() {
		if (m_fixtureParityValidated) {
			return;
		}

		m_fixtureParityValidated = true;
		if (!m_lastWarning.empty()) {
			return;
		}

		std::string fixtureError;
		if (!protocol::GatewayProtocolContract::ValidateFixtureParity(
			"blazeclaw/fixtures/gateway",
			fixtureError)) {
			m_lastWarning = fixtureError;
		}
	}

	bool GatewayHost::AcceptConnection(const std::string& connectionId, std::string& error) {
		return m_transport.AcceptConnection(connectionId, error);
	}

	bool GatewayHost::PumpInboundFrame(
		const std::string& connectionId,
		const std::string& inboundFrame,
		std::string& error) {
		return m_transport.ProcessInboundFrame(connectionId, inboundFrame, error);
	}

	std::vector<std::string> GatewayHost::DrainOutboundFrames(
		const std::string& connectionId,
		std::string& error) {
		m_transportRecipientRegistry.PruneDisconnected(connectionId);
		return m_transport.DrainOutboundFrames(connectionId, error);
	}

	bool GatewayHost::PumpNetworkOnce(std::string& error) {
		if (!m_transport.IsRunning()) {
			error.clear();
			return true;
		}

		return m_transport.PumpNetworkOnce(error);
	}

	std::string GatewayHost::BuildTickEventFrame(std::uint64_t timestampMs, std::uint64_t seq) const {
		return protocol::EncodeValidatedEvent(
			"gateway.tick",
			"{\"ts\":" + std::to_string(timestampMs) +
			",\"running\":" + std::string(IsRunning() ? "true" : "false") +
			",\"connections\":" + std::to_string(m_transport.ConnectionCount()) + "}",
			seq,
			"tick");
	}

	std::string GatewayHost::BuildChannelsAccountsUpdateEventFrame(std::uint64_t seq) const {
		const auto accounts = m_channelRegistry.ListAccounts();
		std::string accountsJson = "[";
		for (std::size_t i = 0; i < accounts.size(); ++i) {
			if (i > 0) {
				accountsJson += ",";
			}

			accountsJson += SerializeChannelAccount(accounts[i]);
		}

		accountsJson += "]";

		return protocol::EncodeValidatedEvent(
			"gateway.channels.accounts.update",
			"{\"accounts\":" + accountsJson + "}",
			seq,
			"channels.accounts.update");
	}

	std::string GatewayHost::BuildAgentUpdateEventFrame(const std::string& agentId, std::uint64_t seq) const {
		const AgentEntry agent = m_agentRegistry.Get(agentId);

		return protocol::EncodeValidatedEvent(
			"gateway.agent.update",
			"{\"agentId\":\"" + agent.id + "\",\"agent\":" + SerializeAgent(agent) + "}",
			seq,
			"agent.update");
	}

	std::string GatewayHost::BuildToolsCatalogUpdateEventFrame(std::uint64_t seq) const {
		const auto tools = m_toolRegistry.List();
		const auto sourceDiagnostics = m_toolRegistry.GetSkillToolSourceDiagnostics();
		std::string toolsJson = "[";
		for (std::size_t i = 0; i < tools.size(); ++i) {
			if (i > 0) {
				toolsJson += ",";
			}

			toolsJson += SerializeTool(tools[i]);
		}

		toolsJson += "]";

		std::string mismatchSampleJson = "[";
		for (std::size_t i = 0; i < m_skillsCatalogState.executionReadinessMismatchSample.size(); ++i) {
			if (i > 0) {
				mismatchSampleJson += ",";
			}
			mismatchSampleJson += JsonString(m_skillsCatalogState.executionReadinessMismatchSample[i]);
		}
		mismatchSampleJson += "]";

		std::string effectiveRootsJson = "[";
		for (std::size_t i = 0; i < m_skillsCatalogState.effectiveSkillRoots.size(); ++i) {
			if (i > 0) {
				effectiveRootsJson += ",";
			}
			effectiveRootsJson += JsonString(m_skillsCatalogState.effectiveSkillRoots[i]);
		}
		effectiveRootsJson += "]";

		const std::string diagnosticsJson =
			"{\"catalogRegistered\":" + std::to_string(sourceDiagnostics.catalogRegistered) +
			",\"manifestRegistered\":" + std::to_string(sourceDiagnostics.manifestRegistered) +
			",\"catalogRejected\":" + std::to_string(sourceDiagnostics.catalogRejected) +
			",\"manifestRejected\":" + std::to_string(sourceDiagnostics.manifestRejected) +
			",\"manifestsGenerated\":" + std::to_string(sourceDiagnostics.manifestsGenerated) +
			",\"manifestGenerationFailed\":" + std::to_string(sourceDiagnostics.manifestGenerationFailed) +
			",\"projectedToolDispatchCount\":" + std::to_string(m_skillsCatalogState.projectedToolDispatchCount) +
			",\"runtimeRegisteredSkillToolCount\":" + std::to_string(m_skillsCatalogState.runtimeRegisteredSkillToolCount) +
			",\"executionReadinessMismatchCount\":" + std::to_string(m_skillsCatalogState.executionReadinessMismatchCount) +
			",\"executionReadinessMismatchSample\":" + mismatchSampleJson +
			",\"effectiveSkillRootCount\":" + std::to_string(m_skillsCatalogState.effectiveSkillRootCount) +
			",\"effectiveSkillRoots\":" + effectiveRootsJson + "}";

		return protocol::EncodeValidatedEvent(
			"gateway.tools.catalog.update",
			"{\"tools\":" + toolsJson + ",\"skillToolSources\":" + diagnosticsJson + "}",
			seq,
			"tools.catalog.update");
	}

	std::string GatewayHost::BuildChannelsUpdateEventFrame(std::uint64_t seq) const {
		const auto channels = m_channelRegistry.ListStatus();
		std::string channelsJson = "[";
		for (std::size_t i = 0; i < channels.size(); ++i) {
			if (i > 0) {
				channelsJson += ",";
			}

			channelsJson += SerializeChannelStatus(channels[i]);
		}

		channelsJson += "]";

		return protocol::EncodeValidatedEvent(
			"gateway.channels.update",
			"{\"channels\":" + channelsJson + "}",
			seq,
			"channels.update");
	}

	std::string GatewayHost::BuildSessionResetEventFrame(const std::string& sessionId, std::uint64_t seq) const {
		const SessionEntry session = m_sessionRegistry.Resolve(sessionId);

		return protocol::EncodeValidatedEvent(
			"gateway.session.reset",
			"{\"sessionId\":\"" + session.id + "\",\"session\":" + SerializeSession(session) + "}",
			seq,
			"session.reset");
	}

	std::string GatewayHost::BuildHealthEventFrame(std::uint64_t seq) const {
		return protocol::EncodeValidatedEvent(
			"gateway.health",
			"{\"status\":\"ok\",\"running\":true}",
			seq,
			"health");
	}

	std::string GatewayHost::BuildShutdownEventFrame(const std::string& reason, std::uint64_t seq) const {
		return protocol::EncodeValidatedEvent(
			"gateway.shutdown",
			"{\"reason\":\"" + reason + "\",\"graceful\":true,\"seq\":" + std::to_string(seq) + "}",
			seq,
			"shutdown");
	}

	std::string GatewayHost::HandleInboundText(const std::string& inboundJson) const {
		protocol::RequestFrame request;
		std::string decodeError;
		if (!protocol::TryDecodeRequestFrame(inboundJson, request, decodeError)) {
			const protocol::ResponseFrame errorResponse = protocol::ErrorResponse(
				protocol::RequestFrame{},
				protocol::ErrorShape{
					.code = "invalid_frame",
					.message = decodeError,
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});

			return protocol::EncodeResponseFrame(errorResponse);
		}

		const bool isToolsCallExecute = request.method == "gateway.tools.call.execute";
		if (const auto normalizedParams =
			TryNormalizeCronParamsPreValidation(request.method, request.paramsJson);
			normalizedParams.has_value()) {
			request.paramsJson = normalizedParams.value();
		}

		if (isToolsCallExecute) {
			const std::string attemptPayload =
				"{\"method\":" + JsonString(request.method) +
				",\"requestId\":" + JsonString(request.id) +
				",\"paramsPresent\":" + std::string(request.paramsJson.has_value() ? "true" : "false") +
				"}";
			EmitTelemetryEvent("tool_call_attempted", attemptPayload);
		}

		protocol::SchemaValidationIssue validationIssue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateRequest(request, validationIssue)) {
			if (isToolsCallExecute) {
				const std::string rejectionPayload =
					"{\"method\":" + JsonString(request.method) +
					",\"requestId\":" + JsonString(request.id) +
					",\"stage\":\"schema_validation\""
					",\"code\":" + JsonString(validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code) +
					",\"message\":" + JsonString(validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message) +
					"}";
				EmitTelemetryEvent("tool_call_rejected", rejectionPayload);
			}

			const protocol::ResponseFrame schemaErrorResponse = protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
					.code = validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code,
					.message = validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});

			return protocol::EncodeResponseFrame(schemaErrorResponse);
		}

		const auto policyError = m_requestPolicyGuard.Evaluate(
			request,
			GatewayRequestPolicyGuard::Context{
				.dispatchInitialized = m_dispatchInitialized,
				.hostRunning = m_running,
			});
		if (policyError.has_value()) {
			const protocol::ResponseFrame policyErrorResponse =
				protocol::ErrorResponse(request, std::move(policyError.value()));

			return protocol::EncodeResponseFrame(policyErrorResponse);
		}

		const protocol::ResponseFrame routedResponse = RouteRequest(request);
		if (!protocol::GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			request.method,
			routedResponse,
			validationIssue)) {
			const protocol::ResponseFrame schemaErrorResponse = protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
					.code = validationIssue.code.empty() ? "schema_invalid_response" : validationIssue.code,
					.message = validationIssue.message.empty()
						? "Handler response failed schema validation."
						: validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});

			return protocol::EncodeResponseFrame(schemaErrorResponse);
		}

		return protocol::EncodeResponseFrame(routedResponse);
	}

	protocol::ResponseFrame GatewayHost::RouteRequest(const protocol::RequestFrame& request) const {
		if (request.method != "chat.send") {
			return RouteRequestLegacy(request);
		}

		auto* mutableThis = const_cast<GatewayHost*>(this);
		if (mutableThis->m_stageRuntimeHost == nullptr) {
			mutableThis->m_stageRuntimeHost = std::make_unique<GatewayHostEx>(
				GatewayHostExDependencies{
					.routeLegacyRequest = [this](const protocol::RequestFrame& routedRequest) {
						return RouteRequestLegacy(routedRequest);
					},
					.isLegacyHealthy = [this]() {
						return IsHealthy();
					},
					.stagePipeline = &m_chatRunPipelineOrchestrator,
				});
		}

		const bool stageHealthy =
			mutableThis->m_stageRuntimeHost != nullptr &&
			mutableThis->m_stageRuntimeHost->IsHealthy();
		const GatewayHostRouteRequest routeRequest{
		   .requestId = request.id,
			  .method = request.method,
			  .orchestrationPath = m_embeddedOrchestrationPath,
			  .stageHostHealthy = stageHealthy,
			  .runtimeOrchestrationCompatEnabled =
				  m_embeddedOrchestrationPath == "runtime_orchestration",
			.stagePipelineFeatureEnabled = m_stagePipelineFeatureEnabled,
			  .rolloutCohort = m_stagePipelineRolloutCohort,
		};
		const GatewayHostRouteDecision routeDecision =
			m_hostRouter.Decide(routeRequest);
		EmitTelemetryEvent(
			"gateway.host.route.decision",
			BuildGatewayHostRouteDecisionPayload(
				request.method,
				routeDecision.target == GatewayHostRouteTarget::StagePipeline
				? "stage_pipeline"
				: "legacy",
				routeDecision.reasonCode,
				routeDecision.selectedCohort,
				routeDecision.fallback));

		if (routeDecision.target == GatewayHostRouteTarget::StagePipeline &&
			mutableThis->m_stageRuntimeHost != nullptr) {
			const protocol::ResponseFrame stageResponse =
				mutableThis->m_stageRuntimeHost->RouteRequest(request);
			if (!stageResponse.ok &&
				stageResponse.error.has_value() &&
				stageResponse.error->code == "stage_host_unavailable") {
				EmitTelemetryEvent(
					"gateway.host.route.decision",
					BuildGatewayHostRouteDecisionPayload(
						request.method,
						"legacy",
						"fallback_stage_host_runtime_unavailable",
						routeDecision.selectedCohort,
						true));
				return RouteRequestLegacy(request);
			}

			return stageResponse;
		}

		return RouteRequestLegacy(request);
	}

	protocol::ResponseFrame GatewayHost::RouteRequestLegacy(
		const protocol::RequestFrame& request) const {
		return m_dispatcher.Dispatch(request);
	}

	bool GatewayHost::IsHealthy() const noexcept {
		return m_dispatchInitialized;
	}

	void GatewayHost::BindRuntimeContext() noexcept {
		m_runtimeContext.dispatcher = &m_dispatcher;
		m_runtimeContext.transport = &m_transport;
		m_runtimeContext.agentRegistry = &m_agentRegistry;
		m_runtimeContext.channelRegistry = &m_channelRegistry;
		m_runtimeContext.sessionRegistry = &m_sessionRegistry;
		m_runtimeContext.toolRegistry = &m_toolRegistry;
		m_runtimeContext.transportRecipientRegistry = &m_transportRecipientRegistry;
		m_runtimeContext.chatRunPipeline = &m_chatRunPipelineOrchestrator;
		m_runtimeContext.eventFanout = &m_eventFanoutService;
		m_runtimeContext.nodePairing = &m_nodePairingService;
		m_runtimeContext.nodeCatalog = &m_nodeCatalogService;
		m_runtimeContext.nodeCanvas = &m_nodeCanvasCapabilityService;
		m_runtimeContext.nodePending = &m_nodePendingActionQueue;
		m_runtimeContext.nodeWake = &m_nodeWakeService;
	}

	void GatewayHost::RegisterDefaultHandlers() {
		BindRuntimeContext();
		GatewayHostRegistration::RegisterDefaultHandlerSequence(*this);
	}

	bool GatewayHost::VerifyRuntimeMethodSurfaceInvariants(std::string& violationOut) const {
		std::unordered_set<std::string> pluginRpcDeduped;
		for (const auto& manifest : m_extensionLifecycle.GetExtensions()) {
			for (const std::string& method : manifest.gatewayRpcMethods) {
				if (!method.empty()) {
					pluginRpcDeduped.insert(method);
				}
			}
		}

		const std::vector<std::string> pluginRpcUnion(
			pluginRpcDeduped.begin(),
			pluginRpcDeduped.end());

		return GatewayRuntimeMethodSurfaceInvariantsHold(
			m_dispatcher,
			pluginRpcUnion,
			violationOut);
	}

	bool GatewayHost::PerformDeferredExtensionCatalogReloadWithMethodSurfaceTelemetry(
		std::string& outDeltaJson) {
		if (!m_initialized) {
			outDeltaJson = R"({"ok":false,"reason":"runtime_not_initialized"})";
			return false;
		}

		const auto sortNames = [](const GatewayMethodDispatcher& dispatcher) {
			auto names = dispatcher.RegisteredMethods();
			std::sort(names.begin(), names.end());
			return names;
			};

		const std::vector<std::string> before = sortNames(m_dispatcher);
		const std::string catalogPath = ResolveExtensionsCatalogPath();

		m_extensionLifecycle.DeactivateAll(m_toolRegistry);
		m_pluginRuntimeState.DeactivateRuntimeRegistry();
		m_toolRegistry.LoadExtensionToolsFromCatalog(catalogPath);
		m_extensionLifecycle.LoadCatalog(catalogPath);
		m_extensionLifecycle.ActivateAll(m_toolRegistry);
		const auto* extensionRegistry = m_pluginRuntimeState.RequireActiveRegistry(
			&m_extensionLifecycle.GetExtensions(),
			catalogPath,
			std::filesystem::current_path().string(),
			PluginRuntimeSubagentMode::GatewayBindable);
		for (const auto& extension : *extensionRegistry) {
			m_pluginRuntimeState.RecordImportedPluginId(extension.id);
		}
		m_pluginRuntimeState.ActivateRuntimeRegistry(
			extensionRegistry,
			catalogPath,
			std::filesystem::current_path().string(),
			PluginRuntimeSubagentMode::GatewayBindable,
			true,
			true);
		EnsureOpsToolsRuntimeRegistered(m_toolRegistry);
		EnsurePdfGeneratorRuntimeRegistered(m_toolRegistry);

		const std::vector<std::string> after = sortNames(m_dispatcher);
		std::vector<std::string> added;
		std::vector<std::string> removed;
		added.reserve(after.size());
		removed.reserve(before.size());
		std::set_difference(
			after.begin(),
			after.end(),
			before.begin(),
			before.end(),
			std::back_inserter(added));
		std::set_difference(
			before.begin(),
			before.end(),
			after.begin(),
			after.end(),
			std::back_inserter(removed));

		std::string addedJson = "[";
		for (std::size_t i = 0; i < added.size() && i < 12; ++i) {
			if (i > 0) {
				addedJson += ",";
			}
			addedJson += JsonString(added[i]);
		}
		addedJson += "]";
		std::string removedJson = "[";
		for (std::size_t i = 0; i < removed.size() && i < 12; ++i) {
			if (i > 0) {
				removedJson += ",";
			}
			removedJson += JsonString(removed[i]);
		}
		removedJson += "]";

		outDeltaJson =
			std::string("{\"ok\":true,\"phase\":\"deferred_extension_catalog_reload\"") +
			",\"beforeCount\":" + std::to_string(before.size()) +
			",\"afterCount\":" + std::to_string(after.size()) +
			",\"addedCount\":" + std::to_string(added.size()) +
			",\"removedCount\":" + std::to_string(removed.size()) +
			",\"addedSample\":" + std::move(addedJson) +
			",\"removedSample\":" + std::move(removedJson) + "}";

		EmitTelemetryEvent("gateway.method_surface.extension_reload", outDeltaJson);
		return true;
	}

	namespace cron_production {
		using CronJson = cron::CronJson;

		std::string ReadStringField(const CronJson& value, const char* key) {
			if (!value.contains(key) || !value[key].is_string()) {
				return {};
			}
			return cron::TrimCopy(value[key].get<std::string>());
		}

		std::string ResolveCronChatSessionKey(const CronJson& job) {
			std::string sessionKey = ReadStringField(job, "sessionKey");
			if (!sessionKey.empty()) {
				return sessionKey;
			}

			if (job.contains("payload") && job["payload"].is_object()) {
				sessionKey = ReadStringField(job["payload"], "sessionKey");
				if (!sessionKey.empty()) {
					return sessionKey;
				}
			}

			const std::string sessionTargetRaw =
				cron::TrimCopy(job.value("sessionTarget", std::string("main")));
			const std::string sessionTarget = cron::ToLowerCopy(sessionTargetRaw);
			if (sessionTarget.rfind("session:", 0) == 0 &&
				sessionTargetRaw.size() > std::string("session:").size()) {
				return cron::TrimCopy(sessionTargetRaw.substr(std::string("session:").size()));
			}

			const std::string agentId = ReadStringField(job, "agentId");
			if (!agentId.empty()) {
				return agentId;
			}

			return "main";
		}

		std::string BuildCronAdapterRunId(const CronJson& job, const std::int64_t nowMs) {
			const std::string jobId = job.value("id", std::string());
			if (!jobId.empty()) {
				return "cron-run-" + jobId + "-" + std::to_string(nowMs);
			}
			return cron::BuildCronRunId(nowMs);
		}

		bool IsCronOwnedRunId(const std::string& runId) {
			return runId.rfind("cron-run-", 0) == 0 || runId.rfind("cron-", 0) == 0;
		}

		using ChatRuntimeResult = GatewayHost::ChatRuntimeResult;

		std::optional<CronJson> BuildCronRuntimeUnavailableResult(
			const std::string& reason,
			const std::int64_t nowMs) {
			return CronJson{
				{ "handled", true },
				{ "status", "error" },
				{ "error", reason },
				{ "errorCategory", "runtime_unavailable" },
				{ "summary", reason },
				{ "retryable", true },
				{ "observedAtMs", nowMs }
			};
		}

		std::optional<CronJson> MapChatRuntimeResultToCron(const ChatRuntimeResult& result) {
			CronJson response = CronJson::object();
			response["handled"] = true;
			response["observedAtMs"] = cron::UtcNowMs();

			if (result.ok) {
				response["status"] = "ok";
				std::string summary = cron::TrimCopy(result.assistantText);
				if (summary.size() > 512) {
					summary.resize(512);
				}
				if (summary.empty()) {
					summary = "Cron runtime completed";
				}
				response["summary"] = summary;
			}
			else {
				const std::string errorMessage = result.errorMessage.empty()
					? "Cron runtime execution failed"
					: result.errorMessage;
				const std::string errorCode = result.errorCode.empty()
					? "runtime_error"
					: result.errorCode;
				response["status"] = "error";
				response["error"] = errorMessage;
				response["errorCategory"] = errorCode;
				response["summary"] = errorMessage;
				response["retryable"] =
					errorCode == "timeout" ||
					errorCode == "network" ||
					errorCode == "rate_limit";
				if (errorCode == "timeout") {
					response["timedOut"] = true;
				}
				if (errorCode == "aborted" || errorCode == "cancelled") {
					response["aborted"] = true;
				}
			}

			if (!result.modelId.empty()) {
				response["model"] = result.modelId;
			}

			const std::string assistantText = cron::TrimCopy(result.assistantText);
			if (!assistantText.empty()) {
				const std::int64_t promptTokens =
					(std::max)(static_cast<std::int64_t>(1),
						static_cast<std::int64_t>(assistantText.size() / 4));
				response["usage"] = CronJson{
					{ "promptTokens", promptTokens },
					{ "completionTokens", promptTokens },
					{ "totalTokens", promptTokens * 2 }
				};
			}

			if (!result.taskDeltas.empty()) {
				response["taskDeltaCount"] = result.taskDeltas.size();
			}

			return response;
		}

		ChatRuntimeResult::TaskDeltaEntry BuildCronTaskDeltaEntry(
			const CronJson& payload,
			const std::size_t index,
			const bool terminal) {
			const std::uint64_t nowMs = static_cast<std::uint64_t>(cron::UtcNowMs());
			ChatRuntimeResult::TaskDeltaEntry entry;
			entry.index = index;
			entry.runId = ReadStringField(payload, "runId");
			entry.sessionId = ReadStringField(payload, "sessionId");
			if (entry.sessionId.empty()) {
				entry.sessionId = ReadStringField(payload, "sessionKey");
			}
			if (entry.sessionId.empty()) {
				entry.sessionId = "cron";
			}
			entry.phase = ReadStringField(payload, "phase");
			if (entry.phase.empty()) {
				entry.phase = ReadStringField(payload, "taskLedgerPhase");
			}
			if (entry.phase.empty()) {
				entry.phase = terminal ? "terminal" : "active";
			}
			entry.status = ReadStringField(payload, "taskLedgerStatus");
			if (entry.status.empty()) {
				entry.status = ReadStringField(payload, "status");
			}
			if (entry.status.empty()) {
				entry.status = terminal ? "ok" : "running";
			}
			entry.stepLabel = "cron:" + ReadStringField(payload, "jobId");
			if (entry.stepLabel == "cron:") {
				entry.stepLabel = "cron";
			}
			entry.resultJson = ReadStringField(payload, "summary");
			entry.errorCode = ReadStringField(payload, "errorCategory");
			entry.errorMessage = ReadStringField(payload, "error");
			entry.startedAtMs = nowMs;
			entry.completedAtMs = terminal ? nowMs : 0;
			entry.latencyMs = terminal ? 0 : 0;
			return entry;
		}

	} // namespace cron_production

	void GatewayHost::WireCronProductionIntegration() {
		if (m_cronProductionWired) {
			return;
		}

		cron::CronRuntimeExecutionAdapters adapters;
		adapters.mainSession =
			[this](const cron::CronJson& job, const std::int64_t nowMs)
			-> std::optional<cron::CronJson> {
				return ExecuteCronMainSessionRuntime(job, nowMs);
			};
		adapters.isolatedSession =
			[this](const cron::CronJson& job, const std::int64_t nowMs)
			-> std::optional<cron::CronJson> {
				return ExecuteCronIsolatedSessionRuntime(job, nowMs);
			};
		adapters.preferRuntimeExecution = true;
		cron::GetCronOpsService().SetRuntimeExecutionAdapters(std::move(adapters));

		cron::CronOpsService::TaskLedgerHooks hooks;
		hooks.createRunningTaskRun =
			[this](const cron::CronJson& payload) {
				HandleCronTaskLedgerCreateRunning(payload);
			};
		hooks.completeTaskRunByRunId =
			[this](const cron::CronJson& payload) {
				HandleCronTaskLedgerComplete(payload);
			};
		hooks.failTaskRunByRunId =
			[this](const cron::CronJson& payload) {
				HandleCronTaskLedgerFail(payload);
			};
		cron::GetCronOpsService().SetTaskLedgerHooks(std::move(hooks));

		cron::CronOpsService::ScheduleNotificationHooks scheduleHooks;
		scheduleHooks.enqueueSystemEvent =
			[this](const cron::CronScheduleNotificationEvent& event) {
				DispatchCronScheduleAutoDisableNotification(event);
			};
		scheduleHooks.requestHeartbeatNow =
			[](const cron::CronScheduleNotificationEvent& event) {
				cron::CronJson wakeParams = {
					{ "mode", cron::kWakeModeNextHeartbeat }
				};
				if (!event.heartbeatWakeReason.empty()) {
					wakeParams["text"] = event.heartbeatWakeReason;
				}
				cron::GetCronOpsService().EnqueueDeferredWakeRequest(wakeParams);
			};
		cron::GetCronOpsService().SetScheduleNotificationHooks(std::move(scheduleHooks));

		cron::CronSchedulerConfig schedulerConfig;
		schedulerConfig.maxConcurrentRuns = 1;
		schedulerConfig.missedJobStaggerMs = cron::kCronDefaultMissedJobStaggerMs;
		schedulerConfig.maxMissedJobsPerRestart = cron::kCronDefaultMaxMissedJobsPerRestart;
		cron::GetCronOpsService().SetSchedulerConfig(schedulerConfig);

		cron::CronOpsService::CronRealtimeEventHooks realtimeHooks;
		realtimeHooks.onEvent =
			[this](const cron::CronRealtimeEvent& event) {
				BroadcastCronRealtimeEvent(event);
			};
		cron::GetCronOpsService().SetRealtimeEventHooks(std::move(realtimeHooks));

		m_cronProductionWired = true;
		EmitTelemetryEvent(
			"gateway.cron.production_integration.wired",
			"{\"runtimeAdapters\":true,\"taskLedgerHooks\":true,\"scheduleNotificationHooks\":true,\"schedulerHardening\":true,\"realtimeEvents\":true}");
	}

	bool GatewayHost::IsCronChatSessionBusy(const std::string& sessionKey) const {
		if (sessionKey.empty()) {
			return false;
		}

		for (const auto& [runId, run] : m_chatRunsById) {
			if (run.sessionKey != sessionKey || !run.active) {
				continue;
			}
			if (cron_production::IsCronOwnedRunId(runId)) {
				continue;
			}
			return true;
		}

		for (const auto& runId :
			m_transportRecipientRegistry.ActiveRunsForSession(sessionKey)) {
			if (cron_production::IsCronOwnedRunId(runId)) {
				continue;
			}
			return true;
		}

		return false;
	}

	std::optional<nlohmann::json> GatewayHost::ExecuteCronMainSessionRuntime(
		const nlohmann::json& job,
		const std::int64_t nowMs) {
		using CronJson = cron::CronJson;
		const CronJson& cronJob = job;
		if (!cronJob.contains("payload") || !cronJob["payload"].is_object()) {
			return std::nullopt;
		}

		const CronJson& payload = cronJob["payload"];
		const std::string payloadKind =
			cron::ToLowerCopy(cron::TrimCopy(payload.value("kind", std::string())));
		if (payloadKind != "systemevent") {
			return std::nullopt;
		}

		const std::string text = cron::TrimCopy(payload.value("text", std::string()));
		if (text.empty()) {
			return std::nullopt;
		}

		const std::string sessionKey = cron_production::ResolveCronChatSessionKey(cronJob);
		const std::string wakeMode =
			cron::NormalizeWakeMode(cronJob.value("wakeMode", std::string(cron::kWakeModeNow)));
		const bool wakeNow = wakeMode == cron::kWakeModeNow;
		const std::string scheduleKind = cronJob.contains("schedule") &&
			cronJob["schedule"].is_object()
			? cron::ToLowerCopy(
				cron::TrimCopy(cronJob["schedule"].value("kind", std::string())))
			: std::string();
		const bool isRecurringJob = scheduleKind != "at";

		if (wakeNow) {
			const std::int64_t maxWaitMs = (std::max)(
				static_cast<std::int64_t>(1),
				cron::TryReadInt64Field(payload, "wakeNowHeartbeatBusyMaxWaitMs")
					.value_or(cron::kCronWakeNowBusyMaxWaitMs));
			const std::int64_t retryDelayMs = (std::max)(
				static_cast<std::int64_t>(1),
				cron::TryReadInt64Field(payload, "wakeNowHeartbeatBusyRetryDelayMs")
					.value_or(cron::kCronWakeNowBusyRetryDelayMs));
			const std::int64_t waitStartedAtMs = nowMs;

			for (;;) {
				bool sessionBusy = false;
				{
					std::lock_guard<std::mutex> lock(m_cronProductionMutex);
					sessionBusy = IsCronChatSessionBusy(sessionKey);
				}
				if (!sessionBusy) {
					break;
				}

				if (isRecurringJob) {
					return CronJson{
						{ "handled", true },
						{ "busy", true },
						{ "status", "ok" },
						{ "summary", text },
						{ "errorCategory", "heartbeat_busy_fallback" },
						{ "sessionKey", sessionKey },
						{ "sessionId", "main" },
						{ "heartbeatFallbackWakeRequested", true },
						{ "heartbeatFallbackWakeRequestedAtMs", nowMs },
						{ "observedAtMs", nowMs }
					};
				}

				if (cron::UtcNowMs() - waitStartedAtMs > maxWaitMs) {
					return CronJson{
						{ "handled", true },
						{ "busy", true },
						{ "status", "error" },
						{ "error", "main heartbeat busy fallback wake requested" },
						{ "errorCategory", "heartbeat_busy_fallback" },
						{ "summary",
							"Main heartbeat busy after wake-now wait; fallback wake requested" },
						{ "sessionKey", sessionKey },
						{ "sessionId", "main" },
						{ "heartbeatFallbackWakeRequested", true },
						{ "heartbeatFallbackWakeRequestedAtMs", nowMs },
						{ "observedAtMs", nowMs }
					};
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
			}
		}
		else {
			std::lock_guard<std::mutex> lock(m_cronProductionMutex);
			if (IsCronChatSessionBusy(sessionKey)) {
				return CronJson{
					{ "handled", true },
					{ "busy", true },
					{ "status", "ok" },
					{ "summary", text },
					{ "errorCategory", "heartbeat_busy_fallback" },
					{ "sessionKey", sessionKey },
					{ "sessionId", "main" },
					{ "observedAtMs", nowMs }
				};
			}
		}

		if (!m_chatRuntimeCallback) {
			return cron_production::BuildCronRuntimeUnavailableResult(
				"chat runtime callback is not configured",
				nowMs);
		}

		const std::string runId = cron_production::BuildCronAdapterRunId(cronJob, nowMs);
		GatewayHost::ChatRuntimeRequest request;
		request.runId = runId;
		request.sessionKey = sessionKey;
		request.message = "[cron] " + text;
		request.bodyForCommands = text;
		request.bodyForAgent = request.message;
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = false;

		ChatRuntimeResult runtimeResult;
		{
			std::lock_guard<std::mutex> lock(m_cronProductionMutex);
			runtimeResult = m_chatRuntimeCallback(request);
		}

		auto mapped = cron_production::MapChatRuntimeResultToCron(runtimeResult);
		if (!mapped.has_value()) {
			return std::nullopt;
		}

		mapped.value()["sessionKey"] = sessionKey;
		mapped.value()["sessionId"] = "main";
		return mapped;
	}

	std::optional<nlohmann::json> GatewayHost::ExecuteCronIsolatedSessionRuntime(
		const nlohmann::json& job,
		const std::int64_t nowMs) {
		using CronJson = cron::CronJson;
		const CronJson& cronJob = job;
		if (!cronJob.contains("payload") || !cronJob["payload"].is_object()) {
			return std::nullopt;
		}

		const CronJson& payload = cronJob["payload"];
		const std::string payloadKind =
			cron::ToLowerCopy(cron::TrimCopy(payload.value("kind", std::string())));
		if (payloadKind != "agentturn") {
			return std::nullopt;
		}

		const std::string message = cron::TrimCopy(payload.value("message", std::string()));
		if (message.empty()) {
			return std::nullopt;
		}

		if (!m_chatRuntimeCallback) {
			return cron_production::BuildCronRuntimeUnavailableResult(
				"chat runtime callback is not configured",
				nowMs);
		}

		const std::string sessionKey = cron_production::ResolveCronChatSessionKey(cronJob);
		const std::string runId = cron_production::BuildCronAdapterRunId(cronJob, nowMs);
		GatewayHost::ChatRuntimeRequest request;
		request.runId = runId;
		request.sessionKey = sessionKey;
		request.message = message;
		request.bodyForCommands = message;
		request.bodyForAgent = message;
		if (payload.contains("model") && payload["model"].is_string()) {
			request.modelIdOverride = cron::TrimCopy(payload["model"].get<std::string>());
		}
		if (payload.contains("provider") && payload["provider"].is_string()) {
			request.providerOverride = cron::TrimCopy(payload["provider"].get<std::string>());
		}
		if (cronJob.contains("model") && cronJob["model"].is_string() &&
			request.modelIdOverride.empty()) {
			request.modelIdOverride = cron::TrimCopy(cronJob["model"].get<std::string>());
		}
		if (cronJob.contains("provider") && cronJob["provider"].is_string() &&
			request.providerOverride.empty()) {
			request.providerOverride = cron::TrimCopy(cronJob["provider"].get<std::string>());
		}
		const auto timeoutSeconds = cron::TryReadInt64Field(payload, "timeoutSeconds");
		if (timeoutSeconds.has_value() && timeoutSeconds.value() > 0) {
			request.timeoutSeconds = timeoutSeconds.value();
			if (timeoutSeconds.value() <= 1) {
				return CronJson{
					{ "handled", true },
					{ "status", "error" },
					{ "error", "cron: job execution timed out" },
					{ "errorCategory", "timeout" },
					{ "summary", "Agent turn timed out" },
					{ "retryable", true },
					{ "timedOut", true },
					{ "sessionKey", sessionKey },
					{ "sessionId", sessionKey.empty() ? "isolated" : sessionKey },
					{ "observedAtMs", nowMs }
				};
			}
		}
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = true;

		ChatRuntimeResult runtimeResult;
		{
			std::lock_guard<std::mutex> lock(m_cronProductionMutex);
			runtimeResult = m_chatRuntimeCallback(request);
		}

		auto mapped = cron_production::MapChatRuntimeResultToCron(runtimeResult);
		if (!mapped.has_value()) {
			return std::nullopt;
		}

		mapped.value()["sessionKey"] = sessionKey;
		mapped.value()["sessionId"] = sessionKey.empty() ? "isolated" : sessionKey;
		if (payload.contains("model") && payload["model"].is_string()) {
			mapped.value()["model"] = cron::TrimCopy(payload["model"].get<std::string>());
		}
		if (payload.contains("provider") && payload["provider"].is_string()) {
			mapped.value()["provider"] = cron::TrimCopy(payload["provider"].get<std::string>());
		}
		return mapped;
	}

	void GatewayHost::UpsertCronTaskLedgerEntry(
		const nlohmann::json& payload,
		const bool terminal) {
		const cron::CronJson& cronPayload = payload;
		const std::string runId = cron_production::ReadStringField(cronPayload, "runId");
		if (runId.empty()) {
			return;
		}

		std::vector<ChatRuntimeResult::TaskDeltaEntry> entries;
		if (const auto existing = m_taskDeltaRepository.Get(runId, false)) {
			entries = existing.value();
		}

		entries.push_back(
			cron_production::BuildCronTaskDeltaEntry(cronPayload, entries.size(), terminal));
		if (!m_taskDeltaRepository.Upsert(runId, entries)) {
			return;
		}

		if (terminal) {
			DispatchCronFailureAlertNotification(payload);
		}
	}

	void GatewayHost::BroadcastCronRealtimeEvent(const cron::CronRealtimeEvent& event) {
		if (!m_transport.IsRunning()) {
			return;
		}

		const cron::CronJson payload = cron::CronRealtimeEventToJson(event);
		std::string broadcastError;
		m_transport.BroadcastOutboundFrame(
			m_eventFanoutService.BuildCronEventFrame(payload.dump(), ++m_cronPushEventSeq),
			broadcastError);
		if (!broadcastError.empty()) {
			EmitTelemetryEvent(
				"gateway.cron.realtime.broadcast_error",
				std::string("{\"error\":") + JsonString(broadcastError) + "}");
		}
	}

	void GatewayHost::DispatchCronScheduleAutoDisableNotification(
		const cron::CronScheduleNotificationEvent& event) {
		if (event.text.empty() || !m_chatRuntimeCallback) {
			return;
		}

		std::string sessionKey = cron::TrimCopy(event.sessionKey);
		if (sessionKey.empty() && !event.agentId.empty()) {
			sessionKey = event.agentId;
		}
		if (sessionKey.empty()) {
			sessionKey = "main";
		}

		ChatRuntimeRequest request;
		request.runId =
			"cron-schedule-auto-disable-" + std::to_string(cron::UtcNowMs());
		request.sessionKey = sessionKey;
		request.message = "[cron] " + event.text;
		request.bodyForCommands = event.text;
		request.bodyForAgent = request.message;
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = false;

		{
			std::lock_guard<std::mutex> lock(m_cronProductionMutex);
			(void)m_chatRuntimeCallback(request);
		}
	}

	void GatewayHost::DispatchCronFailureAlertNotification(
		const nlohmann::json& payload) {
		const cron::CronJson& cronPayload = payload;
		if (!m_chatRuntimeCallback) {
			return;
		}
		if (!cronPayload.contains("failureAlertTriggered") ||
			!cronPayload["failureAlertTriggered"].is_boolean() ||
			!cronPayload["failureAlertTriggered"].get<bool>()) {
			return;
		}

		std::string sessionKey = cron::TrimCopy(
			cron_production::ReadStringField(cronPayload, "sessionKey"));
		if (sessionKey.empty()) {
			sessionKey = "main";
		}

		const std::string jobId =
			cron_production::ReadStringField(cronPayload, "jobId");
		const std::string mode =
			cron_production::ReadStringField(cronPayload, "failureAlertMode");
		const std::string target =
			cron_production::ReadStringField(cronPayload, "failureAlertTarget");
		const std::string summary =
			cron_production::ReadStringField(cronPayload, "summary");
		const std::string error =
			cron_production::ReadStringField(cronPayload, "error");

		std::string text = "[cron][failure-alert]";
		if (!jobId.empty()) {
			text += " job=" + jobId;
		}
		if (!mode.empty()) {
			text += " mode=" + mode;
		}
		if (!target.empty()) {
			text += " target=" + target;
		}
		if (!summary.empty()) {
			text += " summary=" + summary;
		}
		if (!error.empty()) {
			text += " error=" + error;
		}

		ChatRuntimeRequest request;
		request.runId = "cron-failure-alert-" + std::to_string(cron::UtcNowMs());
		request.sessionKey = sessionKey;
		request.message = text;
		request.bodyForCommands = text;
		request.bodyForAgent = text;
		request.shouldLoadInlineSkillCommands = false;
		request.allowInlineToolImmediateExecution = false;

		try {
			(void)m_chatRuntimeCallback(request);
		}
		catch (...) {
		}
	}

	void GatewayHost::HandleCronTaskLedgerCreateRunning(const nlohmann::json& payload) {
		std::lock_guard<std::mutex> lock(m_cronProductionMutex);
		UpsertCronTaskLedgerEntry(payload, false);
	}

	void GatewayHost::HandleCronTaskLedgerComplete(const nlohmann::json& payload) {
		std::lock_guard<std::mutex> lock(m_cronProductionMutex);
		UpsertCronTaskLedgerEntry(payload, true);
	}

	void GatewayHost::HandleCronTaskLedgerFail(const nlohmann::json& payload) {
		std::lock_guard<std::mutex> lock(m_cronProductionMutex);
		UpsertCronTaskLedgerEntry(payload, true);
	}

} // namespace blazeclaw::gateway
