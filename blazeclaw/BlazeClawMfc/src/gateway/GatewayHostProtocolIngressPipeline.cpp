#include "pch.h"
#include "GatewayHostProtocolIngressPipeline.h"
#include "Telemetry.h"

#include <nlohmann/json.hpp>
#include <algorithm>

namespace blazeclaw::gateway::GatewayHostProtocolIngressPipeline {

	using Json = nlohmann::json;
	using protocol::RequestFrame;
	using protocol::ResponseFrame;
	using protocol::ErrorShape;
	using protocol::SchemaValidationIssue;

	std::optional<std::string> TryNormalizeCronParamsPreValidation(
		const std::string& method,
		const std::optional<std::string>& paramsJson) {
		if (!paramsJson.has_value()) {
			return std::nullopt;
		}

		if (method != "cron.add" &&
			method != "cron.update" &&
			method != "cron.remove" &&
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

		auto aliasCanonicalJobId = [&](Json& node) {
			if (node.contains("cronId") &&
				!node.contains("id") &&
				!node.contains("jobId")) {
				node["id"] = node["cronId"];
				node.erase("cronId");
				changed = true;
			}

			if (node.contains("jobId") &&
				!node.contains("id") &&
				node["jobId"].is_string()) {
				node["id"] = node["jobId"];
				changed = true;
			}
		};

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
				params.contains("deliveryTransportDispatch") ||
				params.contains("deliveryChannel") ||
				params.contains("deliveryAccountId") ||
				params.contains("deliveryBestEffort") ||
				params.contains("failureDestinationMode") ||
				params.contains("failureDestinationUrl") ||
				params.contains("failureDestinationTo") ||
				params.contains("failureDestinationTransportDispatch") ||
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
				moveFieldIfPresent(params, delivery, "deliveryTransportDispatch");
				if (delivery.contains("deliveryTransportDispatch") &&
					!delivery.contains("transportDispatch")) {
					delivery["transportDispatch"] = delivery["deliveryTransportDispatch"];
					delivery.erase("deliveryTransportDispatch");
					changed = true;
				}
				else if (delivery.contains("deliveryTransportDispatch")) {
					delivery.erase("deliveryTransportDispatch");
					changed = true;
				}

				if (params.contains("failureDestinationMode") ||
					params.contains("failureDestinationUrl") ||
					params.contains("failureDestinationTo") ||
					params.contains("failureDestinationTransportDispatch") ||
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
					moveFieldIfPresent(params, failureDestination, "failureDestinationTransportDispatch");
					if (failureDestination.contains("failureDestinationTransportDispatch") &&
						!failureDestination.contains("transportDispatch")) {
						failureDestination["transportDispatch"] =
							failureDestination["failureDestinationTransportDispatch"];
						failureDestination.erase("failureDestinationTransportDispatch");
						changed = true;
					}
					else if (failureDestination.contains("failureDestinationTransportDispatch")) {
						failureDestination.erase("failureDestinationTransportDispatch");
						changed = true;
					}
				}
			}

			if (params.contains("failureAlertAfter") ||
				params.contains("failureAlertCooldownMs") ||
				params.contains("failureAlertMode") ||
				params.contains("failureAlertUrl") ||
				params.contains("failureAlertTo") ||
				params.contains("failureAlertTransportDispatch") ||
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
				moveFieldIfPresent(params, failureAlert, "failureAlertTransportDispatch");
				if (failureAlert.contains("failureAlertTransportDispatch") &&
					!failureAlert.contains("transportDispatch")) {
					failureAlert["transportDispatch"] = failureAlert["failureAlertTransportDispatch"];
					failureAlert.erase("failureAlertTransportDispatch");
					changed = true;
				}
				else if (failureAlert.contains("failureAlertTransportDispatch")) {
					failureAlert.erase("failureAlertTransportDispatch");
					changed = true;
				}
			}
		}

		if (method == "cron.update") {
			aliasCanonicalJobId(params);

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
					patch.contains("deliveryTransportDispatch") ||
					patch.contains("deliveryChannel") ||
					patch.contains("deliveryAccountId") ||
					patch.contains("deliveryBestEffort") ||
					patch.contains("failureDestinationMode") ||
					patch.contains("failureDestinationUrl") ||
					patch.contains("failureDestinationTo") ||
					patch.contains("failureDestinationTransportDispatch") ||
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
					moveFieldIfPresent(patch, delivery, "deliveryTransportDispatch");
					if (delivery.contains("deliveryTransportDispatch") &&
						!delivery.contains("transportDispatch")) {
						delivery["transportDispatch"] = delivery["deliveryTransportDispatch"];
						delivery.erase("deliveryTransportDispatch");
						changed = true;
					}
					else if (delivery.contains("deliveryTransportDispatch")) {
						delivery.erase("deliveryTransportDispatch");
						changed = true;
					}

					if (patch.contains("failureDestinationMode") ||
						patch.contains("failureDestinationUrl") ||
						patch.contains("failureDestinationTo") ||
						patch.contains("failureDestinationTransportDispatch") ||
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
						moveFieldIfPresent(patch, failureDestination, "failureDestinationTransportDispatch");
						if (failureDestination.contains("failureDestinationTransportDispatch") &&
							!failureDestination.contains("transportDispatch")) {
							failureDestination["transportDispatch"] =
								failureDestination["failureDestinationTransportDispatch"];
							failureDestination.erase("failureDestinationTransportDispatch");
							changed = true;
						}
						else if (failureDestination.contains("failureDestinationTransportDispatch")) {
							failureDestination.erase("failureDestinationTransportDispatch");
							changed = true;
						}
					}
				}

				if (patch.contains("failureAlertAfter") ||
					patch.contains("failureAlertCooldownMs") ||
					patch.contains("failureAlertMode") ||
					patch.contains("failureAlertUrl") ||
					patch.contains("failureAlertTo") ||
					patch.contains("failureAlertTransportDispatch") ||
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
					moveFieldIfPresent(patch, failureAlert, "failureAlertTransportDispatch");
					if (failureAlert.contains("failureAlertTransportDispatch") &&
						!failureAlert.contains("transportDispatch")) {
						failureAlert["transportDispatch"] =
							failureAlert["failureAlertTransportDispatch"];
						failureAlert.erase("failureAlertTransportDispatch");
						changed = true;
					}
					else if (failureAlert.contains("failureAlertTransportDispatch")) {
						failureAlert.erase("failureAlertTransportDispatch");
						changed = true;
					}
				}
			}
		}

		if (method == "cron.run") {
			aliasCanonicalJobId(params);
		}

		if (method == "cron.remove") {
			aliasCanonicalJobId(params);
		}

		if (method == "cron.runs") {
			aliasCanonicalJobId(params);

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

	std::string ExecuteInboundTextPipeline(
		const std::string& inboundJson,
		const PipelineContext& context) {

		// Stage 1: Decode
		RequestFrame request;
		std::string decodeError;
		if (!protocol::TryDecodeRequestFrame(inboundJson, request, decodeError)) {
			const ResponseFrame errorResponse = protocol::ErrorResponse(
				RequestFrame{},
				ErrorShape{
					.code = "invalid_frame",
					.message = decodeError,
					.detailsJson = std::nullopt,
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});

			return protocol::EncodeResponseFrame(errorResponse);
		}

		// Stage 2: Normalize (method-specific pre-validation)
		const bool isToolsCallExecute = request.method == "gateway.tools.call.execute";
		if (const auto normalizedParams =
			TryNormalizeCronParamsPreValidation(request.method, request.paramsJson);
			normalizedParams.has_value()) {
			request.paramsJson = normalizedParams.value();
		}

		// Tool call attempt telemetry (pre-validation)
		if (isToolsCallExecute) {
			const std::string attemptPayload =
				"{\"method\":" + JsonString(request.method) +
				",\"requestId\":" + JsonString(request.id) +
				",\"paramsPresent\":" + std::string(request.paramsJson.has_value() ? "true" : "false") +
				"}";
			context.emitTelemetry("tool_call_attempted", attemptPayload);
		}

		// Stage 3: Schema Validate
		SchemaValidationIssue validationIssue;
		if (!protocol::GatewayProtocolSchemaValidator::ValidateRequest(request, validationIssue)) {
			if (isToolsCallExecute) {
				const std::string rejectionPayload =
					"{\"method\":" + JsonString(request.method) +
					",\"requestId\":" + JsonString(request.id) +
					",\"stage\":\"schema_validation\""
					",\"code\":" + JsonString(validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code) +
					",\"message\":" + JsonString(validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message) +
					"}";
				context.emitTelemetry("tool_call_rejected", rejectionPayload);
			}

			const ResponseFrame schemaErrorResponse = protocol::ErrorResponse(
				request,
				ErrorShape{
					.code = validationIssue.code.empty() ? "schema_validation_failed" : validationIssue.code,
					.message = validationIssue.message.empty() ? "Request failed schema validation." : validationIssue.message,
					.detailsJson = "{\"method\":\"" + request.method + "\"}",
					.retryable = false,
					.retryAfterMs = std::nullopt,
				});

			return protocol::EncodeResponseFrame(schemaErrorResponse);
		}

		// Stage 4: Policy Guard
		const auto policyError = context.requestPolicyGuard->Evaluate(
			request,
			GatewayRequestPolicyGuard::Context{
				.dispatchInitialized = context.dispatchInitialized,
				.hostRunning = context.hostRunning,
			});
		if (policyError.has_value()) {
			const ResponseFrame policyErrorResponse =
				protocol::ErrorResponse(request, std::move(policyError.value()));

			return protocol::EncodeResponseFrame(policyErrorResponse);
		}

		// Stage 5: Dispatch
		const ResponseFrame routedResponse = context.routeRequest(request);

		// Stage 6: Response Validate
		if (!protocol::GatewayProtocolSchemaValidator::ValidateResponseForMethod(
			request.method,
			routedResponse,
			validationIssue)) {
			const ResponseFrame schemaErrorResponse = protocol::ErrorResponse(
				request,
				ErrorShape{
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

} // namespace blazeclaw::gateway::GatewayHostProtocolIngressPipeline
