#include "pch.h"
#include "GatewayProtocolSchemaValidator.h"
#include "generated/GatewaySchemaCatalog.Generated.h"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <unordered_set>

namespace blazeclaw::gateway::protocol {
	namespace {

		std::string Trim(const std::string& value) {
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

		bool IsJsonObjectShape(const std::string& value) {
			const std::string trimmed = Trim(value);
			return trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}';
		}

		bool IsHttpUrl(const std::string& value) {
			const std::string trimmed = Trim(value);
			if (trimmed.size() < 8) {
				return false;
			}

			std::string lowered = trimmed;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			return lowered.rfind("http://", 0) == 0 || lowered.rfind("https://", 0) == 0;
		}

		void SetIssue(SchemaValidationIssue& issue, const std::string& code, const std::string& message) {
			issue.code = code;
			issue.message = message;
		}

		bool ContainsFieldToken(const std::string& json, const std::string& fieldName, std::size_t& tokenPos) {
			const std::string token = "\"" + fieldName + "\"";
			tokenPos = json.find(token);
			return tokenPos != std::string::npos;
		}

		bool IsFieldValueType(const std::string& json, const std::string& fieldName, char expectedFirstChar) {
			std::size_t tokenPos = 0;
			if (!ContainsFieldToken(json, fieldName, tokenPos)) {
				return false;
			}

			std::size_t valuePos = json.find(':', tokenPos);
			if (valuePos == std::string::npos) {
				return false;
			}

			++valuePos;
			while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
				++valuePos;
			}

			if (valuePos >= json.size()) {
				return false;
			}

			return json[valuePos] == expectedFirstChar;
		}

		enum class JsonFieldKind {
			String,
			Number,
			Boolean,
			Object,
			Array,
			Null,
		};

		using ParsedObjectFieldKinds = std::unordered_map<std::string, JsonFieldKind>;

		std::size_t SkipWhitespace(const std::string& json, std::size_t position) {
			while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position])) != 0) {
				++position;
			}

			return position;
		}

		bool TryConsumeJsonString(const std::string& json, std::size_t& position, std::string& value) {
			if (position >= json.size() || json[position] != '"') {
				return false;
			}

			++position;
			value.clear();
			while (position < json.size()) {
				const char ch = json[position++];
				if (ch == '\\') {
					if (position >= json.size()) {
						return false;
					}

					value.push_back(json[position++]);
					continue;
				}

				if (ch == '"') {
					return true;
				}

				value.push_back(ch);
			}

			return false;
		}

		bool TryReadTopLevelStringField(
			const std::string& json,
			const std::string& fieldName,
			std::string& valueOut) {
			const std::string token = "\"" + fieldName + "\"";
			std::size_t tokenPos = json.find(token);
			if (tokenPos == std::string::npos) {
				return false;
			}

			std::size_t valuePos = json.find(':', tokenPos);
			if (valuePos == std::string::npos) {
				return false;
			}

			valuePos = SkipWhitespace(json, valuePos + 1);
			if (valuePos >= json.size() || json[valuePos] != '"') {
				return false;
			}

			std::string parsed;
			if (!TryConsumeJsonString(json, valuePos, parsed)) {
				return false;
			}

			valueOut = parsed;
			return true;
		}

		bool TryReadTopLevelNumberField(
			const std::string& json,
			const std::string& fieldName,
			double& valueOut) {
			std::size_t tokenPos = 0;
			if (!ContainsFieldToken(json, fieldName, tokenPos)) {
				return false;
			}

			std::size_t valuePos = json.find(':', tokenPos);
			if (valuePos == std::string::npos) {
				return false;
			}

			valuePos = SkipWhitespace(json, valuePos + 1);
			if (valuePos >= json.size()) {
				return false;
			}

			std::size_t endPos = valuePos;
			while (endPos < json.size()) {
				const char ch = json[endPos];
				if ((ch >= '0' && ch <= '9') ||
					ch == '-' ||
					ch == '+' ||
					ch == '.' ||
					ch == 'e' ||
					ch == 'E') {
					++endPos;
					continue;
				}
				break;
			}

			if (endPos <= valuePos) {
				return false;
			}

			const std::string token = json.substr(valuePos, endPos - valuePos);
			char* parsedEnd = nullptr;
			const double parsed = std::strtod(token.c_str(), &parsedEnd);
			if (parsedEnd == token.c_str() || *parsedEnd != '\0' || !std::isfinite(parsed)) {
				return false;
			}

			valueOut = parsed;
			return true;
		}

		bool ValidateCronEnumStringField(
			const RequestFrame& request,
			const ParsedObjectFieldKinds& fieldKinds,
			const char* methodName,
			const char* fieldName,
			std::initializer_list<const char*> allowedValues,
			SchemaValidationIssue& issue) {
			if (fieldKinds.find(fieldName) == fieldKinds.end()) {
				return true;
			}
			if (!request.paramsJson.has_value()) {
				return true;
			}

			std::string value;
			if (!TryReadTopLevelStringField(request.paramsJson.value(), fieldName, value)) {
				return true;
			}

			for (const char* allowed : allowedValues) {
				if (value == allowed) {
					return true;
				}
			}

			std::string expected;
			bool first = true;
			for (const char* allowed : allowedValues) {
				if (!first) {
					expected += ", ";
				}
				expected += "`";
				expected += allowed;
				expected += "`";
				first = false;
			}

			SetIssue(
				issue,
				"schema_invalid_value",
				"Method `" + std::string(methodName) + "` requires `params." + fieldName +
				"` to be one of: " + expected + ".");
			return false;
		}

		bool TryConsumeBalancedComposite(
			const std::string& json,
			std::size_t& position,
			char openChar,
			char closeChar) {
			if (position >= json.size() || json[position] != openChar) {
				return false;
			}

			int depth = 0;
			bool inString = false;
			bool escaped = false;
			while (position < json.size()) {
				const char ch = json[position++];
				if (inString) {
					if (escaped) {
						escaped = false;
					}
					else if (ch == '\\') {
						escaped = true;
					}
					else if (ch == '"') {
						inString = false;
					}

					continue;
				}

				if (ch == '"') {
					inString = true;
					continue;
				}

				if (ch == openChar) {
					++depth;
				}
				else if (ch == closeChar) {
					--depth;
					if (depth == 0) {
						return true;
					}

				}
			}

			return false;
		}

		bool TryConsumePrimitive(const std::string& json, std::size_t& position, const std::string& token) {
			if (json.compare(position, token.size(), token) != 0) {
				return false;
			}

			position += token.size();
			return true;
		}

		bool TryConsumeJsonValue(const std::string& json, std::size_t& position, JsonFieldKind& kind) {
			position = SkipWhitespace(json, position);
			if (position >= json.size()) {
				return false;
			}

			const char ch = json[position];
			if (ch == '"') {
				std::string ignored;
				if (!TryConsumeJsonString(json, position, ignored)) {
					return false;
				}

				kind = JsonFieldKind::String;
				return true;
			}

			if (ch == '{') {
				if (!TryConsumeBalancedComposite(json, position, '{', '}')) {
					return false;
				}

				kind = JsonFieldKind::Object;
				return true;
			}

			if (ch == '[') {
				if (!TryConsumeBalancedComposite(json, position, '[', ']')) {
					return false;
				}

				kind = JsonFieldKind::Array;
				return true;
			}

			if (TryConsumePrimitive(json, position, "true") || TryConsumePrimitive(json, position, "false")) {
				kind = JsonFieldKind::Boolean;
				return true;
			}

			if (TryConsumePrimitive(json, position, "null")) {
				kind = JsonFieldKind::Null;
				return true;
			}

			if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)) != 0) {
				++position;
				while (position < json.size()) {
					const char numberCh = json[position];
					const bool isNumberChar =
						std::isdigit(static_cast<unsigned char>(numberCh)) != 0 ||
						numberCh == '.' || numberCh == 'e' || numberCh == 'E' || numberCh == '+' || numberCh == '-';
					if (!isNumberChar) {
						break;
					}

					++position;
				}

				kind = JsonFieldKind::Number;
				return true;
			}

			return false;
		}

		bool TryParseTopLevelObjectFieldKinds(const std::string& json, ParsedObjectFieldKinds& kinds) {
			kinds.clear();
			const std::string trimmed = Trim(json);
			if (!IsJsonObjectShape(trimmed)) {
				return false;
			}

			std::size_t position = 1;
			while (true) {
				position = SkipWhitespace(trimmed, position);
				if (position >= trimmed.size()) {
					return false;
				}

				if (trimmed[position] == '}') {
					return true;
				}

				std::string key;
				if (!TryConsumeJsonString(trimmed, position, key)) {
					return false;
				}

				position = SkipWhitespace(trimmed, position);
				if (position >= trimmed.size() || trimmed[position] != ':') {
					return false;
				}

				++position;
				JsonFieldKind kind{};
				if (!TryConsumeJsonValue(trimmed, position, kind)) {
					return false;
				}

				kinds.insert_or_assign(key, kind);
				position = SkipWhitespace(trimmed, position);
				if (position >= trimmed.size()) {
					return false;
				}

				if (trimmed[position] == ',') {
					++position;
					continue;
				}

				if (trimmed[position] == '}') {
					return true;
				}

				return false;
			}
		}

		bool IsFieldBoolean(const std::string& json, const std::string& fieldName);
		bool IsFieldNumber(const std::string& json, const std::string& fieldName);

		bool TryParseRequestParamsObject(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName,
			ParsedObjectFieldKinds& fieldKinds) {
			if (!request.paramsJson.has_value()) {
				fieldKinds.clear();
				return true;
			}

			if (!TryParseTopLevelObjectFieldKinds(request.paramsJson.value(), fieldKinds)) {
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` expects `params` to be a JSON object when provided.");
				return false;
			}

			return true;
		}

		bool RequireFieldKindIfPresent(
			const ParsedObjectFieldKinds& fieldKinds,
			const std::string& fieldName,
			JsonFieldKind kind,
			SchemaValidationIssue& issue,
			const std::string& methodName,
			const std::string& typeLabel) {
			const auto it = fieldKinds.find(fieldName);
			if (it == fieldKinds.end()) {
				return true;
			}

			if (it->second == kind) {
				return true;
			}

			SetIssue(
				issue,
				"schema_invalid_params",
				"Method `" + methodName + "` requires `params." + fieldName + "` to be " + typeLabel + ".");
			return false;
		}

		bool ContainsFieldName(std::initializer_list<const char*> allowedFieldNames, const std::string& fieldName) {
			for (const char* allowed : allowedFieldNames) {
				if (fieldName == allowed) {
					return true;
				}
			}

			return false;
		}

		bool ValidateOptionalSessionListParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			if (!request.paramsJson.has_value()) {
				return true;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseTopLevelObjectFieldKinds(request.paramsJson.value(), fieldKinds)) {
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` expects `params` to be a JSON object when provided.");
				return false;
			}

			auto requireFieldKind = [&](const std::string& field, JsonFieldKind kind, const char* typeLabel) {
				const auto it = fieldKinds.find(field);
				if (it == fieldKinds.end()) {
					return true;
				}

				if (it->second == kind) {
					return true;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` requires `params." + field + "` to be " + typeLabel + ".");
				return false;
				};

			if (!requireFieldKind("active", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("scope", JsonFieldKind::String, "a string") ||
				!requireFieldKind("limit", JsonFieldKind::Number, "numeric") ||
				!requireFieldKind("activeMinutes", JsonFieldKind::Number, "numeric") ||
				!requireFieldKind("includeGlobal", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("includeUnknown", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("includeDerivedTitles", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("includeLastMessage", JsonFieldKind::Boolean, "boolean") ||
				!requireFieldKind("label", JsonFieldKind::String, "a string") ||
				!requireFieldKind("spawnedBy", JsonFieldKind::String, "a string") ||
				!requireFieldKind("agentId", JsonFieldKind::String, "a string") ||
				!requireFieldKind("search", JsonFieldKind::String, "a string")) {
				return false;
			}

			if (request.paramsJson.has_value() &&
				fieldKinds.find("delivery") != fieldKinds.end()) {
				const std::string& json = request.paramsJson.value();
				std::size_t deliveryTokenPos = 0;
				if (ContainsFieldToken(json, "delivery", deliveryTokenPos)) {
					std::size_t deliveryValuePos = json.find(':', deliveryTokenPos);
					if (deliveryValuePos != std::string::npos) {
						deliveryValuePos = SkipWhitespace(json, deliveryValuePos + 1);
						if (deliveryValuePos < json.size() && json[deliveryValuePos] == '{') {
							std::size_t deliveryCursor = deliveryValuePos;
							if (!TryConsumeBalancedComposite(json, deliveryCursor, '{', '}')) {
								SetIssue(
									issue,
									"schema_invalid_params",
									"Method `cron.add` has invalid `params.delivery` JSON shape.");
								return false;
							}

							const std::string deliveryJson =
								json.substr(deliveryValuePos, deliveryCursor - deliveryValuePos);
							ParsedObjectFieldKinds deliveryKinds;
							if (!TryParseTopLevelObjectFieldKinds(deliveryJson, deliveryKinds)) {
								SetIssue(
									issue,
									"schema_invalid_params",
									"Method `cron.add` has invalid `params.delivery` object shape.");
								return false;
							}

							auto requireDeliveryFieldKind = [&](const char* fieldName,
								JsonFieldKind expected,
								const char* typeLabel) {
								const auto it = deliveryKinds.find(fieldName);
								if (it == deliveryKinds.end()) {
									return true;
								}
								if (it->second == expected) {
									return true;
								}

								SetIssue(
									issue,
									"schema_invalid_type",
									"Method `cron.add` expects `params.delivery." +
									std::string(fieldName) + "` to be " + typeLabel + ".");
								return false;
							};

							if (!requireDeliveryFieldKind("mode", JsonFieldKind::String, "a string") ||
								!requireDeliveryFieldKind("channel", JsonFieldKind::String, "a string") ||
								!requireDeliveryFieldKind("to", JsonFieldKind::String, "a string") ||
								!requireDeliveryFieldKind("accountId", JsonFieldKind::String, "a string") ||
								!requireDeliveryFieldKind("bestEffort", JsonFieldKind::Boolean, "boolean") ||
								!requireDeliveryFieldKind("failureDestination", JsonFieldKind::Object, "an object")) {
								return false;
							}

							if (deliveryKinds.find("mode") != deliveryKinds.end()) {
								std::string mode;
								if (TryReadTopLevelStringField(deliveryJson, "mode", mode)) {
									const std::string normalizedMode = Trim(mode);
									if (normalizedMode != "none" &&
										normalizedMode != "announce" &&
										normalizedMode != "webhook") {
										SetIssue(
											issue,
											"schema_invalid_value",
											"Method `cron.add` requires `params.delivery.mode` to be one of: `none`, `announce`, `webhook`.");
										return false;
									}

									if (normalizedMode == "webhook") {
										std::string toValue;
										if (!TryReadTopLevelStringField(deliveryJson, "to", toValue) ||
											Trim(toValue).empty()) {
											SetIssue(
												issue,
												"schema_invalid_value",
												"Method `cron.add` requires `params.delivery.to` to be a non-empty string when `params.delivery.mode` is `webhook`.");
											return false;
										}
										if (!IsHttpUrl(toValue)) {
											SetIssue(
												issue,
												"schema_invalid_value",
												"Method `cron.add` requires `params.delivery.to` to start with `http://` or `https://` when `params.delivery.mode` is `webhook`.");
											return false;
										}
									}
								}
							}

							if (deliveryKinds.find("failureDestination") != deliveryKinds.end()) {
								std::size_t failureDestinationTokenPos = 0;
								if (ContainsFieldToken(deliveryJson, "failureDestination", failureDestinationTokenPos)) {
									std::size_t failureDestinationValuePos =
										deliveryJson.find(':', failureDestinationTokenPos);
									if (failureDestinationValuePos != std::string::npos) {
										failureDestinationValuePos =
											SkipWhitespace(deliveryJson, failureDestinationValuePos + 1);
										if (failureDestinationValuePos < deliveryJson.size() &&
											deliveryJson[failureDestinationValuePos] == '{') {
											std::size_t failureDestinationCursor = failureDestinationValuePos;
											if (!TryConsumeBalancedComposite(
												deliveryJson,
												failureDestinationCursor,
												'{',
												'}')) {
												SetIssue(
													issue,
													"schema_invalid_params",
													"Method `cron.add` has invalid `params.delivery.failureDestination` JSON shape.");
												return false;
											}

											const std::string failureDestinationJson =
												deliveryJson.substr(
													failureDestinationValuePos,
													failureDestinationCursor - failureDestinationValuePos);
											ParsedObjectFieldKinds failureDestinationKinds;
											if (!TryParseTopLevelObjectFieldKinds(
												failureDestinationJson,
												failureDestinationKinds)) {
												SetIssue(
													issue,
													"schema_invalid_params",
													"Method `cron.add` has invalid `params.delivery.failureDestination` object shape.");
												return false;
											}

											auto requireFailureDestinationFieldKind = [&](const char* fieldName,
												JsonFieldKind expected,
												const char* typeLabel) {
												const auto it = failureDestinationKinds.find(fieldName);
												if (it == failureDestinationKinds.end()) {
													return true;
												}
												if (it->second == expected) {
													return true;
												}

												SetIssue(
													issue,
													"schema_invalid_type",
													"Method `cron.add` expects `params.delivery.failureDestination." +
													std::string(fieldName) + "` to be " + typeLabel + ".");
												return false;
											};

											if (!requireFailureDestinationFieldKind("mode", JsonFieldKind::String, "a string") ||
												!requireFailureDestinationFieldKind("channel", JsonFieldKind::String, "a string") ||
												!requireFailureDestinationFieldKind("to", JsonFieldKind::String, "a string") ||
												!requireFailureDestinationFieldKind("accountId", JsonFieldKind::String, "a string")) {
												return false;
											}

											if (failureDestinationKinds.find("mode") != failureDestinationKinds.end()) {
												std::string failureDestinationMode;
												if (TryReadTopLevelStringField(
													failureDestinationJson,
													"mode",
													failureDestinationMode)) {
													const std::string normalizedFailureDestinationMode =
														Trim(failureDestinationMode);
													if (normalizedFailureDestinationMode != "announce" &&
														normalizedFailureDestinationMode != "webhook") {
														SetIssue(
															issue,
															"schema_invalid_value",
															"Method `cron.add` requires `params.delivery.failureDestination.mode` to be one of: `announce`, `webhook`.");
														return false;
													}

													if (normalizedFailureDestinationMode == "webhook") {
														std::string failureDestinationTo;
														if (!TryReadTopLevelStringField(
															failureDestinationJson,
															"to",
															failureDestinationTo) ||
															Trim(failureDestinationTo).empty()) {
															SetIssue(
																issue,
																"schema_invalid_value",
																"Method `cron.add` requires `params.delivery.failureDestination.to` to be a non-empty string when `params.delivery.failureDestination.mode` is `webhook`.");
															return false;
														}
														if (!IsHttpUrl(failureDestinationTo)) {
															SetIssue(
																issue,
																"schema_invalid_value",
																"Method `cron.add` requires `params.delivery.failureDestination.to` to start with `http://` or `https://` when `params.delivery.failureDestination.mode` is `webhook`.");
															return false;
														}
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName(
					{ "active",
					 "scope",
					 "limit",
					 "activeMinutes",
					 "includeGlobal",
					 "includeUnknown",
					 "includeDerivedTitles",
					 "includeLastMessage",
					 "label",
					 "spawnedBy",
					 "agentId",
					 "search" },
					field)) {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateCronStatusParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "cron.status", fieldKinds)) {
				return false;
			}
			if (!fieldKinds.empty()) {
				const std::string field = fieldKinds.begin()->first;
				SetIssue(issue, "schema_invalid_params", "Method `cron.status` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateCronListParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "cron.list", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "limit", JsonFieldKind::Number, issue, "cron.list", "numeric") ||
				!RequireFieldKindIfPresent(fieldKinds, "offset", JsonFieldKind::Number, issue, "cron.list", "numeric") ||
				!RequireFieldKindIfPresent(fieldKinds, "includeDisabled", JsonFieldKind::Boolean, issue, "cron.list", "boolean") ||
				!RequireFieldKindIfPresent(fieldKinds, "enabled", JsonFieldKind::String, issue, "cron.list", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "query", JsonFieldKind::String, issue, "cron.list", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "sortBy", JsonFieldKind::String, issue, "cron.list", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "sortDir", JsonFieldKind::String, issue, "cron.list", "a string")) {
				return false;
			}

			if (!ValidateCronEnumStringField(request, fieldKinds, "cron.list", "enabled", { "all", "enabled", "disabled" }, issue) ||
				!ValidateCronEnumStringField(request, fieldKinds, "cron.list", "sortBy", { "nextRunAtMs", "updatedAtMs", "name" }, issue) ||
				!ValidateCronEnumStringField(request, fieldKinds, "cron.list", "sortDir", { "asc", "desc" }, issue)) {
				return false;
			}

			if (request.paramsJson.has_value()) {
				auto validateIntegralRange = [&](const char* fieldName, double minValue, double maxValue, bool boundedMax) {
					double value = 0.0;
					if (!TryReadTopLevelNumberField(request.paramsJson.value(), fieldName, value)) {
						return true;
					}

					if (std::floor(value) != value) {
						SetIssue(
							issue,
							"schema_invalid_value",
							"Method `cron.list` requires `params." + std::string(fieldName) + "` to be an integer.");
						return false;
					}

					if (value < minValue || (boundedMax && value > maxValue)) {
						std::string message =
							"Method `cron.list` requires `params." + std::string(fieldName) + "` to be ";
						if (boundedMax) {
							message += "between " + std::to_string(static_cast<int>(minValue)) +
								" and " + std::to_string(static_cast<int>(maxValue)) + ".";
						}
						else {
							message += "greater than or equal to " +
								std::to_string(static_cast<int>(minValue)) + ".";
						}

						SetIssue(issue, "schema_invalid_value", message);
						return false;
					}

					return true;
				};

				if (!validateIntegralRange("limit", 1.0, 200.0, true) ||
					!validateIntegralRange("offset", 0.0, 0.0, false)) {
					return false;
				}
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName(
					{ "limit", "offset", "includeDisabled", "enabled", "query", "sortBy", "sortDir" },
					field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `cron.list` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateCronAddParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "cron.add", fieldKinds)) {
				return false;
			}

			if (fieldKinds.find("name") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `cron.add` requires `params.name`.");
				return false;
			}
			if (fieldKinds.find("schedule") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `cron.add` requires `params.schedule`.");
				return false;
			}
			if (fieldKinds.find("payload") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `cron.add` requires `params.payload`.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "name", JsonFieldKind::String, issue, "cron.add", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "description", JsonFieldKind::String, issue, "cron.add", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "enabled", JsonFieldKind::Boolean, issue, "cron.add", "boolean") ||
				!RequireFieldKindIfPresent(fieldKinds, "schedule", JsonFieldKind::Object, issue, "cron.add", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "payload", JsonFieldKind::Object, issue, "cron.add", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "wakeMode", JsonFieldKind::String, issue, "cron.add", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "sessionTarget", JsonFieldKind::String, issue, "cron.add", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "deleteAfterRun", JsonFieldKind::Boolean, issue, "cron.add", "boolean") ||
				!RequireFieldKindIfPresent(fieldKinds, "delivery", JsonFieldKind::Object, issue, "cron.add", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "agentId", JsonFieldKind::String, issue, "cron.add", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "sessionKey", JsonFieldKind::String, issue, "cron.add", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "retry", JsonFieldKind::Object, issue, "cron.add", "an object")) {
				return false;
			}

			if (request.paramsJson.has_value() &&
				fieldKinds.find("delivery") != fieldKinds.end()) {
				const std::string& json = request.paramsJson.value();
				std::size_t deliveryTokenPos = 0;
				if (ContainsFieldToken(json, "delivery", deliveryTokenPos)) {
					std::size_t deliveryValuePos = json.find(':', deliveryTokenPos);
					if (deliveryValuePos != std::string::npos) {
						deliveryValuePos = SkipWhitespace(json, deliveryValuePos + 1);
						if (deliveryValuePos < json.size() && json[deliveryValuePos] == '{') {
							std::size_t deliveryCursor = deliveryValuePos;
							if (!TryConsumeBalancedComposite(json, deliveryCursor, '{', '}')) {
								SetIssue(
									issue,
									"schema_invalid_params",
									"Method `cron.add` has invalid `params.delivery` JSON shape.");
								return false;
							}

							const std::string deliveryJson =
								json.substr(deliveryValuePos, deliveryCursor - deliveryValuePos);
							ParsedObjectFieldKinds deliveryKinds;
							if (!TryParseTopLevelObjectFieldKinds(deliveryJson, deliveryKinds)) {
								SetIssue(
									issue,
									"schema_invalid_params",
									"Method `cron.add` has invalid `params.delivery` object shape.");
								return false;
							}

							auto requireDeliveryFieldKind = [&](const char* fieldName,
								JsonFieldKind expected,
								const char* typeLabel) {
								const auto it = deliveryKinds.find(fieldName);
								if (it == deliveryKinds.end()) {
									return true;
								}
								if (it->second == expected) {
									return true;
								}

								SetIssue(
									issue,
									"schema_invalid_type",
									"Method `cron.add` expects `params.delivery." +
									std::string(fieldName) + "` to be " + typeLabel + ".");
								return false;
							};

							if (!requireDeliveryFieldKind("mode", JsonFieldKind::String, "a string") ||
								!requireDeliveryFieldKind("channel", JsonFieldKind::String, "a string") ||
								!requireDeliveryFieldKind("to", JsonFieldKind::String, "a string") ||
								!requireDeliveryFieldKind("accountId", JsonFieldKind::String, "a string") ||
								!requireDeliveryFieldKind("bestEffort", JsonFieldKind::Boolean, "boolean") ||
								!requireDeliveryFieldKind("failureDestination", JsonFieldKind::Object, "an object")) {
								return false;
							}

							if (deliveryKinds.find("mode") != deliveryKinds.end()) {
								std::string mode;
								if (TryReadTopLevelStringField(deliveryJson, "mode", mode)) {
									const std::string normalizedMode = Trim(mode);
									if (normalizedMode != "none" &&
										normalizedMode != "announce" &&
										normalizedMode != "webhook") {
										SetIssue(
											issue,
											"schema_invalid_value",
											"Method `cron.add` requires `params.delivery.mode` to be one of: `none`, `announce`, `webhook`.");
										return false;
									}

									if (normalizedMode == "webhook") {
										std::string toValue;
										if (!TryReadTopLevelStringField(deliveryJson, "to", toValue) ||
											Trim(toValue).empty()) {
											SetIssue(
												issue,
												"schema_invalid_value",
												"Method `cron.add` requires `params.delivery.to` to be a non-empty string when `params.delivery.mode` is `webhook`.");
											return false;
										}
										if (!IsHttpUrl(toValue)) {
											SetIssue(
												issue,
												"schema_invalid_value",
												"Method `cron.add` requires `params.delivery.to` to start with `http://` or `https://` when `params.delivery.mode` is `webhook`.");
											return false;
										}
									}
								}
							}

							if (deliveryKinds.find("failureDestination") != deliveryKinds.end()) {
								std::size_t failureDestinationTokenPos = 0;
								if (ContainsFieldToken(deliveryJson, "failureDestination", failureDestinationTokenPos)) {
									std::size_t failureDestinationValuePos =
										deliveryJson.find(':', failureDestinationTokenPos);
									if (failureDestinationValuePos != std::string::npos) {
										failureDestinationValuePos =
											SkipWhitespace(deliveryJson, failureDestinationValuePos + 1);
										if (failureDestinationValuePos < deliveryJson.size() &&
											deliveryJson[failureDestinationValuePos] == '{') {
											std::size_t failureDestinationCursor = failureDestinationValuePos;
											if (!TryConsumeBalancedComposite(
												deliveryJson,
												failureDestinationCursor,
												'{',
												'}')) {
												SetIssue(
													issue,
													"schema_invalid_params",
													"Method `cron.add` has invalid `params.delivery.failureDestination` JSON shape.");
												return false;
											}

											const std::string failureDestinationJson =
												deliveryJson.substr(
													failureDestinationValuePos,
													failureDestinationCursor - failureDestinationValuePos);
											ParsedObjectFieldKinds failureDestinationKinds;
											if (!TryParseTopLevelObjectFieldKinds(
												failureDestinationJson,
												failureDestinationKinds)) {
												SetIssue(
													issue,
													"schema_invalid_params",
													"Method `cron.add` has invalid `params.delivery.failureDestination` object shape.");
												return false;
											}

											auto requireFailureDestinationFieldKind = [&](const char* fieldName,
												JsonFieldKind expected,
												const char* typeLabel) {
												const auto it = failureDestinationKinds.find(fieldName);
												if (it == failureDestinationKinds.end()) {
													return true;
												}
												if (it->second == expected) {
													return true;
												}

												SetIssue(
													issue,
													"schema_invalid_type",
													"Method `cron.add` expects `params.delivery.failureDestination." +
													std::string(fieldName) + "` to be " + typeLabel + ".");
												return false;
											};

											if (!requireFailureDestinationFieldKind("mode", JsonFieldKind::String, "a string") ||
												!requireFailureDestinationFieldKind("channel", JsonFieldKind::String, "a string") ||
												!requireFailureDestinationFieldKind("to", JsonFieldKind::String, "a string") ||
												!requireFailureDestinationFieldKind("accountId", JsonFieldKind::String, "a string")) {
												return false;
											}

											if (failureDestinationKinds.find("mode") != failureDestinationKinds.end()) {
												std::string failureDestinationMode;
												if (TryReadTopLevelStringField(
													failureDestinationJson,
													"mode",
													failureDestinationMode)) {
													const std::string normalizedFailureDestinationMode =
														Trim(failureDestinationMode);
													if (normalizedFailureDestinationMode != "announce" &&
														normalizedFailureDestinationMode != "webhook") {
														SetIssue(
															issue,
															"schema_invalid_value",
															"Method `cron.add` requires `params.delivery.failureDestination.mode` to be one of: `announce`, `webhook`.");
														return false;
													}

													if (normalizedFailureDestinationMode == "webhook") {
														std::string failureDestinationTo;
														if (!TryReadTopLevelStringField(
															failureDestinationJson,
															"to",
															failureDestinationTo) ||
															Trim(failureDestinationTo).empty()) {
															SetIssue(
																issue,
																"schema_invalid_value",
																"Method `cron.add` requires `params.delivery.failureDestination.to` to be a non-empty string when `params.delivery.failureDestination.mode` is `webhook`.");
															return false;
														}
														if (!IsHttpUrl(failureDestinationTo)) {
															SetIssue(
																issue,
																"schema_invalid_value",
																"Method `cron.add` requires `params.delivery.failureDestination.to` to start with `http://` or `https://` when `params.delivery.failureDestination.mode` is `webhook`.");
															return false;
														}
													}
												}
											}
										}
									}
								}
							}
						}
					}
				}
			}

			const auto failureAlertIt = fieldKinds.find("failureAlert");
			if (failureAlertIt != fieldKinds.end() &&
				failureAlertIt->second != JsonFieldKind::Object &&
				failureAlertIt->second != JsonFieldKind::Boolean) {
				SetIssue(
					issue,
					"schema_invalid_type",
					"Method `cron.add` expects `params.failureAlert` to be an object or boolean.");
				return false;
			}

			if (request.paramsJson.has_value() &&
				failureAlertIt != fieldKinds.end() &&
				failureAlertIt->second == JsonFieldKind::Object) {
				const std::string& json = request.paramsJson.value();
				std::size_t failureAlertTokenPos = 0;
				if (ContainsFieldToken(json, "failureAlert", failureAlertTokenPos)) {
					std::size_t failureAlertValuePos = json.find(':', failureAlertTokenPos);
					if (failureAlertValuePos != std::string::npos) {
						failureAlertValuePos = SkipWhitespace(json, failureAlertValuePos + 1);
						if (failureAlertValuePos < json.size() && json[failureAlertValuePos] == '{') {
							std::size_t failureAlertCursor = failureAlertValuePos;
							if (!TryConsumeBalancedComposite(json, failureAlertCursor, '{', '}')) {
								SetIssue(
									issue,
									"schema_invalid_params",
									"Method `cron.add` has invalid `params.failureAlert` JSON shape.");
								return false;
							}

							const std::string failureAlertJson =
								json.substr(failureAlertValuePos, failureAlertCursor - failureAlertValuePos);
							ParsedObjectFieldKinds failureAlertKinds;
							if (!TryParseTopLevelObjectFieldKinds(failureAlertJson, failureAlertKinds)) {
								SetIssue(
									issue,
									"schema_invalid_params",
									"Method `cron.add` has invalid `params.failureAlert` object shape.");
								return false;
							}

							auto requireFailureAlertFieldKind = [&](const char* fieldName,
								JsonFieldKind expected,
								const char* typeLabel) {
								const auto it = failureAlertKinds.find(fieldName);
								if (it == failureAlertKinds.end()) {
									return true;
								}
								if (it->second == expected) {
									return true;
								}

								SetIssue(
									issue,
									"schema_invalid_type",
									"Method `cron.add` expects `params.failureAlert." +
									std::string(fieldName) + "` to be " + typeLabel + ".");
								return false;
							};

							if (!requireFailureAlertFieldKind("after", JsonFieldKind::Number, "numeric") ||
								!requireFailureAlertFieldKind("cooldownMs", JsonFieldKind::Number, "numeric") ||
								!requireFailureAlertFieldKind("mode", JsonFieldKind::String, "a string") ||
								!requireFailureAlertFieldKind("channel", JsonFieldKind::String, "a string") ||
								!requireFailureAlertFieldKind("to", JsonFieldKind::String, "a string") ||
								!requireFailureAlertFieldKind("accountId", JsonFieldKind::String, "a string")) {
								return false;
							}

							auto validateIntegralRange = [&](const char* fieldName, double minValue) {
								double value = 0.0;
								if (!TryReadTopLevelNumberField(failureAlertJson, fieldName, value)) {
									return true;
								}

								if (std::floor(value) != value) {
									SetIssue(
										issue,
										"schema_invalid_value",
										"Method `cron.add` requires `params.failureAlert." +
										std::string(fieldName) + "` to be an integer.");
									return false;
								}

								if (value < minValue) {
									SetIssue(
										issue,
										"schema_invalid_value",
										"Method `cron.add` requires `params.failureAlert." +
										std::string(fieldName) + "` to be greater than or equal to " +
										std::to_string(static_cast<int>(minValue)) + ".");
									return false;
								}

								return true;
							};

							if (!validateIntegralRange("after", 1.0) ||
								!validateIntegralRange("cooldownMs", 0.0)) {
								return false;
							}

							if (failureAlertKinds.find("mode") != failureAlertKinds.end()) {
								std::string mode;
								if (TryReadTopLevelStringField(failureAlertJson, "mode", mode)) {
									const std::string normalizedMode = Trim(mode);
									if (normalizedMode != "announce" && normalizedMode != "webhook") {
										SetIssue(
											issue,
											"schema_invalid_value",
											"Method `cron.add` requires `params.failureAlert.mode` to be one of: `announce`, `webhook`.");
										return false;
									}
								}
							}

							for (const auto& [field, _] : failureAlertKinds) {
								if (ContainsFieldName(
									{ "after", "channel", "to", "cooldownMs", "mode", "accountId" },
									field)) {
									continue;
								}

								SetIssue(
									issue,
									"schema_invalid_params",
									"Method `cron.add` does not allow `params.failureAlert." +
									field + "`.");
								return false;
							}
						}
					}
				}
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName(
					{ "name", "description", "enabled", "schedule", "payload", "wakeMode", "sessionTarget", "deleteAfterRun", "delivery", "agentId", "sessionKey", "retry", "failureAlert" },
					field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `cron.add` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateCronUpdateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "cron.update", fieldKinds)) {
				return false;
			}

			if (fieldKinds.find("patch") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `cron.update` requires `params.patch`.");
				return false;
			}
			if (fieldKinds.find("id") == fieldKinds.end() && fieldKinds.find("jobId") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `cron.update` requires `params.id` or `params.jobId`.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "id", JsonFieldKind::String, issue, "cron.update", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "jobId", JsonFieldKind::String, issue, "cron.update", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "patch", JsonFieldKind::Object, issue, "cron.update", "an object")) {
				return false;
			}

			if (request.paramsJson.has_value()) {
				std::string id;
				if (TryReadTopLevelStringField(request.paramsJson.value(), "id", id) && Trim(id).empty()) {
					SetIssue(issue, "schema_invalid_value", "Method `cron.update` requires `params.id` to be a non-empty string.");
					return false;
				}

				std::string jobId;
				if (TryReadTopLevelStringField(request.paramsJson.value(), "jobId", jobId) && Trim(jobId).empty()) {
					SetIssue(issue, "schema_invalid_value", "Method `cron.update` requires `params.jobId` to be a non-empty string.");
					return false;
				}

				std::size_t patchTokenPos = 0;
				if (ContainsFieldToken(request.paramsJson.value(), "patch", patchTokenPos)) {
					std::size_t patchValuePos = request.paramsJson.value().find(':', patchTokenPos);
					if (patchValuePos != std::string::npos) {
						patchValuePos = SkipWhitespace(request.paramsJson.value(), patchValuePos + 1);
						if (patchValuePos < request.paramsJson.value().size() &&
							request.paramsJson.value()[patchValuePos] == '{') {
							std::size_t patchCursor = patchValuePos;
							if (!TryConsumeBalancedComposite(request.paramsJson.value(), patchCursor, '{', '}')) {
								SetIssue(issue, "schema_invalid_params", "Method `cron.update` has invalid `params.patch` JSON shape.");
								return false;
							}

							const std::string patchJson =
								request.paramsJson.value().substr(patchValuePos, patchCursor - patchValuePos);
							ParsedObjectFieldKinds patchKinds;
							if (!TryParseTopLevelObjectFieldKinds(patchJson, patchKinds)) {
								SetIssue(issue, "schema_invalid_params", "Method `cron.update` has invalid `params.patch` object shape.");
								return false;
							}

							const auto patchFailureAlertIt = patchKinds.find("failureAlert");
							if (patchFailureAlertIt != patchKinds.end() &&
								patchFailureAlertIt->second != JsonFieldKind::Object &&
								patchFailureAlertIt->second != JsonFieldKind::Boolean) {
								SetIssue(
									issue,
									"schema_invalid_type",
									"Method `cron.update` expects `params.patch.failureAlert` to be an object or boolean.");
								return false;
							}

							if (patchFailureAlertIt != patchKinds.end() &&
								patchFailureAlertIt->second == JsonFieldKind::Object) {
								std::size_t failureAlertTokenPos = 0;
								if (ContainsFieldToken(patchJson, "failureAlert", failureAlertTokenPos)) {
									std::size_t failureAlertValuePos = patchJson.find(':', failureAlertTokenPos);
									if (failureAlertValuePos != std::string::npos) {
										failureAlertValuePos = SkipWhitespace(patchJson, failureAlertValuePos + 1);
										if (failureAlertValuePos < patchJson.size() && patchJson[failureAlertValuePos] == '{') {
											std::size_t failureAlertCursor = failureAlertValuePos;
											if (!TryConsumeBalancedComposite(patchJson, failureAlertCursor, '{', '}')) {
												SetIssue(issue, "schema_invalid_params", "Method `cron.update` has invalid `params.patch.failureAlert` JSON shape.");
												return false;
											}

											const std::string failureAlertJson =
												patchJson.substr(failureAlertValuePos, failureAlertCursor - failureAlertValuePos);
											ParsedObjectFieldKinds failureAlertKinds;
											if (!TryParseTopLevelObjectFieldKinds(failureAlertJson, failureAlertKinds)) {
												SetIssue(issue, "schema_invalid_params", "Method `cron.update` has invalid `params.patch.failureAlert` object shape.");
												return false;
											}

											auto requireFailureAlertFieldKind = [&](const char* fieldName, JsonFieldKind expected, const char* typeLabel) {
												const auto it = failureAlertKinds.find(fieldName);
												if (it == failureAlertKinds.end()) {
													return true;
												}
												if (it->second == expected) {
													return true;
												}
												SetIssue(
													issue,
													"schema_invalid_type",
													"Method `cron.update` expects `params.patch.failureAlert." + std::string(fieldName) + "` to be " + typeLabel + ".");
												return false;
											};

											if (!requireFailureAlertFieldKind("after", JsonFieldKind::Number, "numeric") ||
												!requireFailureAlertFieldKind("cooldownMs", JsonFieldKind::Number, "numeric") ||
												!requireFailureAlertFieldKind("mode", JsonFieldKind::String, "a string") ||
												!requireFailureAlertFieldKind("channel", JsonFieldKind::String, "a string") ||
												!requireFailureAlertFieldKind("to", JsonFieldKind::String, "a string") ||
												!requireFailureAlertFieldKind("accountId", JsonFieldKind::String, "a string")) {
												return false;
											}

											auto validateIntegralRange = [&](const char* fieldName, double minValue) {
												double value = 0.0;
												if (!TryReadTopLevelNumberField(failureAlertJson, fieldName, value)) {
													return true;
												}
												if (std::floor(value) != value) {
													SetIssue(
														issue,
														"schema_invalid_value",
														"Method `cron.update` requires `params.patch.failureAlert." + std::string(fieldName) + "` to be an integer.");
													return false;
												}
												if (value < minValue) {
													SetIssue(
														issue,
														"schema_invalid_value",
														"Method `cron.update` requires `params.patch.failureAlert." + std::string(fieldName) + "` to be greater than or equal to " + std::to_string(static_cast<int>(minValue)) + ".");
													return false;
												}
												return true;
											};

											if (!validateIntegralRange("after", 1.0) ||
												!validateIntegralRange("cooldownMs", 0.0)) {
												return false;
											}

											if (failureAlertKinds.find("mode") != failureAlertKinds.end()) {
												std::string mode;
												if (TryReadTopLevelStringField(failureAlertJson, "mode", mode)) {
													const std::string normalizedMode = Trim(mode);
													if (normalizedMode != "announce" && normalizedMode != "webhook") {
														SetIssue(
															issue,
															"schema_invalid_value",
															"Method `cron.update` requires `params.patch.failureAlert.mode` to be one of: `announce`, `webhook`.");
														return false;
													}
												}
											}

											for (const auto& [field, _] : failureAlertKinds) {
												if (ContainsFieldName({ "after", "channel", "to", "cooldownMs", "mode", "accountId" }, field)) {
													continue;
												}
												SetIssue(
													issue,
													"schema_invalid_params",
													"Method `cron.update` does not allow `params.patch.failureAlert." + field + "`.");
												return false;
											}
										}
									}
								}
							}
						}
					}
				}
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "id", "jobId", "patch" }, field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `cron.update` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateCronRemoveParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "cron.remove", fieldKinds)) {
				return false;
			}

			if (fieldKinds.find("id") == fieldKinds.end() && fieldKinds.find("jobId") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `cron.remove` requires `params.id` or `params.jobId`.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "id", JsonFieldKind::String, issue, "cron.remove", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "jobId", JsonFieldKind::String, issue, "cron.remove", "a string")) {
				return false;
			}

			if (request.paramsJson.has_value()) {
				std::string id;
				if (TryReadTopLevelStringField(request.paramsJson.value(), "id", id) && Trim(id).empty()) {
					SetIssue(issue, "schema_invalid_value", "Method `cron.remove` requires `params.id` to be a non-empty string.");
					return false;
				}

				std::string jobId;
				if (TryReadTopLevelStringField(request.paramsJson.value(), "jobId", jobId) && Trim(jobId).empty()) {
					SetIssue(issue, "schema_invalid_value", "Method `cron.remove` requires `params.jobId` to be a non-empty string.");
					return false;
				}
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "id", "jobId" }, field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `cron.remove` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateCronRunParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "cron.run", fieldKinds)) {
				return false;
			}

			if (fieldKinds.find("id") == fieldKinds.end() && fieldKinds.find("jobId") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `cron.run` requires `params.id` or `params.jobId`.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "id", JsonFieldKind::String, issue, "cron.run", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "jobId", JsonFieldKind::String, issue, "cron.run", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "mode", JsonFieldKind::String, issue, "cron.run", "a string")) {
				return false;
			}

			if (!ValidateCronEnumStringField(request, fieldKinds, "cron.run", "mode", { "due", "force" }, issue)) {
				return false;
			}

			if (request.paramsJson.has_value()) {
				std::string id;
				if (TryReadTopLevelStringField(request.paramsJson.value(), "id", id) && Trim(id).empty()) {
					SetIssue(issue, "schema_invalid_value", "Method `cron.run` requires `params.id` to be a non-empty string.");
					return false;
				}

				std::string jobId;
				if (TryReadTopLevelStringField(request.paramsJson.value(), "jobId", jobId) && Trim(jobId).empty()) {
					SetIssue(issue, "schema_invalid_value", "Method `cron.run` requires `params.jobId` to be a non-empty string.");
					return false;
				}
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "id", "jobId", "mode" }, field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `cron.run` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateCronRunsParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "cron.runs", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "limit", JsonFieldKind::Number, issue, "cron.runs", "numeric") ||
				!RequireFieldKindIfPresent(fieldKinds, "offset", JsonFieldKind::Number, issue, "cron.runs", "numeric") ||
				!RequireFieldKindIfPresent(fieldKinds, "scope", JsonFieldKind::String, issue, "cron.runs", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "id", JsonFieldKind::String, issue, "cron.runs", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "jobId", JsonFieldKind::String, issue, "cron.runs", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "statuses", JsonFieldKind::Array, issue, "cron.runs", "an array") ||
				!RequireFieldKindIfPresent(fieldKinds, "status", JsonFieldKind::String, issue, "cron.runs", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "deliveryStatuses", JsonFieldKind::Array, issue, "cron.runs", "an array") ||
				!RequireFieldKindIfPresent(fieldKinds, "deliveryStatus", JsonFieldKind::String, issue, "cron.runs", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "query", JsonFieldKind::String, issue, "cron.runs", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "sortDir", JsonFieldKind::String, issue, "cron.runs", "a string")) {
				return false;
			}

			if (!ValidateCronEnumStringField(request, fieldKinds, "cron.runs", "scope", { "job", "all" }, issue) ||
				!ValidateCronEnumStringField(request, fieldKinds, "cron.runs", "status", { "all", "ok", "error", "skipped" }, issue) ||
				!ValidateCronEnumStringField(request, fieldKinds, "cron.runs", "deliveryStatus", { "not-requested", "delivered", "not-delivered", "suppressed" }, issue) ||
				!ValidateCronEnumStringField(request, fieldKinds, "cron.runs", "sortDir", { "asc", "desc" }, issue)) {
				return false;
			}

			if (request.paramsJson.has_value()) {
				auto validateIntegralRange = [&](const char* fieldName, double minValue, double maxValue, bool boundedMax) {
					double value = 0.0;
					if (!TryReadTopLevelNumberField(request.paramsJson.value(), fieldName, value)) {
						return true;
					}

					if (std::floor(value) != value) {
						SetIssue(
							issue,
							"schema_invalid_value",
							"Method `cron.runs` requires `params." + std::string(fieldName) + "` to be an integer.");
						return false;
					}

					if (value < minValue || (boundedMax && value > maxValue)) {
						std::string message =
							"Method `cron.runs` requires `params." + std::string(fieldName) + "` to be ";
						if (boundedMax) {
							message += "between " + std::to_string(static_cast<int>(minValue)) +
								" and " + std::to_string(static_cast<int>(maxValue)) + ".";
						}
						else {
							message += "greater than or equal to " +
								std::to_string(static_cast<int>(minValue)) + ".";
						}

						SetIssue(issue, "schema_invalid_value", message);
						return false;
					}

					return true;
				};

				if (!validateIntegralRange("limit", 1.0, 200.0, true) ||
					!validateIntegralRange("offset", 0.0, 0.0, false)) {
					return false;
				}

				std::string scope;
				if (TryReadTopLevelStringField(request.paramsJson.value(), "scope", scope) && scope == "job") {
					std::string id;
					std::string jobId;
					const bool hasId = TryReadTopLevelStringField(request.paramsJson.value(), "id", id) && !Trim(id).empty();
					const bool hasJobId = TryReadTopLevelStringField(request.paramsJson.value(), "jobId", jobId) && !Trim(jobId).empty();
					if (!hasId && !hasJobId) {
						SetIssue(
							issue,
							"schema_missing_field",
							"Method `cron.runs` requires `params.id` or `params.jobId` when `params.scope` is `job`.");
						return false;
					}
				}
			}

			if (request.paramsJson.has_value()) {
				const std::string& json = request.paramsJson.value();
				auto validateCronRunsArrayValues = [&](const char* fieldName,
					std::initializer_list<const char*> allowedValues) {
					const auto it = fieldKinds.find(fieldName);
					if (it == fieldKinds.end()) {
						return true;
					}
					if (it->second != JsonFieldKind::Array) {
						return true;
					}

					std::size_t tokenPos = 0;
					if (!ContainsFieldToken(json, fieldName, tokenPos)) {
						return true;
					}

					std::size_t valuePos = json.find(':', tokenPos);
					if (valuePos == std::string::npos) {
						return true;
					}
					valuePos = SkipWhitespace(json, valuePos + 1);
					if (valuePos >= json.size() || json[valuePos] != '[') {
						return true;
					}

					std::size_t cursor = valuePos + 1;
					bool hasValue = false;
					while (true) {
						cursor = SkipWhitespace(json, cursor);
						if (cursor >= json.size()) {
							break;
						}
						if (json[cursor] == ']') {
							break;
						}

						hasValue = true;
						if (json[cursor] != '"') {
							SetIssue(
								issue,
								"schema_invalid_params",
								"Method `cron.runs` requires `params." + std::string(fieldName) +
								"` to contain only string values.");
							return false;
						}

						std::string item;
						if (!TryConsumeJsonString(json, cursor, item)) {
							SetIssue(
								issue,
								"schema_invalid_params",
								"Method `cron.runs` has invalid `params." + std::string(fieldName) + "` JSON shape.");
							return false;
						}

						bool allowed = false;
						for (const char* candidate : allowedValues) {
							if (item == candidate) {
								allowed = true;
								break;
							}
						}
						if (!allowed) {
							SetIssue(
								issue,
								"schema_invalid_value",
								"Method `cron.runs` has unsupported `params." +
								std::string(fieldName) + "` value `" + item + "`.");
							return false;
						}

						cursor = SkipWhitespace(json, cursor);
						if (cursor < json.size() && json[cursor] == ',') {
							++cursor;
							continue;
						}
						if (cursor < json.size() && json[cursor] == ']') {
							break;
						}
					}

					if (!hasValue) {
						SetIssue(
							issue,
							"schema_invalid_params",
							"Method `cron.runs` requires `params." + std::string(fieldName) +
							"` to contain at least one value.");
						return false;
					}

					return true;
				};

				if (!validateCronRunsArrayValues("statuses", { "ok", "error", "skipped" }) ||
					!validateCronRunsArrayValues(
						"deliveryStatuses",
						{ "not-requested", "delivered", "not-delivered", "suppressed" })) {
					return false;
				}
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "limit", "offset", "scope", "id", "jobId", "statuses", "status", "deliveryStatuses", "deliveryStatus", "query", "sortDir" }, field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `cron.runs` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateWakeParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "wake", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "mode", JsonFieldKind::String, issue, "wake", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "text", JsonFieldKind::String, issue, "wake", "a string")) {
				return false;
			}

			if (!ValidateCronEnumStringField(request, fieldKinds, "wake", "mode", { "now", "next-heartbeat" }, issue)) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "mode", "text" }, field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `wake` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteResolveParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				return true;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseTopLevelObjectFieldKinds(request.paramsJson.value(), fieldKinds)) {
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.channels.route.resolve` expects `params` to be a JSON object when provided.");
				return false;
			}

			auto requireFieldKind = [&](const char* field, JsonFieldKind kind, const char* typeLabel) {
				const auto it = fieldKinds.find(field);
				if (it == fieldKinds.end()) {
					return true;
				}

				if (it->second == kind) {
					return true;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					std::string("Method `gateway.channels.route.resolve` requires `params.") + field + "` to be " + typeLabel + ".");
				return false;
				};

			if (!requireFieldKind("channel", JsonFieldKind::String, "a string") ||
				!requireFieldKind("accountId", JsonFieldKind::String, "a string") ||
				!requireFieldKind("sessionId", JsonFieldKind::String, "a string") ||
				!requireFieldKind("agentId", JsonFieldKind::String, "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "channel", "accountId", "sessionId", "agentId" }, field)) {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.resolve` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteSetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.set", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.set",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.set",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"agentId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.set",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"sessionId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.set",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId" || field == "agentId" || field == "sessionId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.set` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteDeleteParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.delete", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.delete",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.delete",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.delete` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteExistsParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.exists", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.exists",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.exists",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.exists` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteGetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.get", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.get",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.get",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.get` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRouteRestoreParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.restore", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.route.restore",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.route.restore",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.restore` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutesClearParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.routes.clear", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.routes.clear",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.routes.clear` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutesRestoreParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.routes.restore", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.routes.restore",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.routes.restore` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutesResetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.routes.reset", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.routes.reset", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.routes.reset` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutesCountParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.routes.count", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.routes.count", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.routes.count` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsRoutePatchParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.route.patch", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.route.patch", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "accountId", JsonFieldKind::String, issue, "gateway.channels.route.patch", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "agentId", JsonFieldKind::String, issue, "gateway.channels.route.patch", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "sessionId", JsonFieldKind::String, issue, "gateway.channels.route.patch", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId" || field == "agentId" || field == "sessionId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.route.patch` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsActivateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.activate", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.activate",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.activate",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.activate` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsDeactivateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.deactivate", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.deactivate",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.deactivate",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.deactivate` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsClearParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.clear", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.clear",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.clear` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsRestoreParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.restore", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.accounts.restore", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.restore` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsCountParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.count", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "channel", JsonFieldKind::String, issue, "gateway.channels.accounts.count", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.count` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsExistsParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.exists", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.exists",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.exists",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.exists` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsUpdateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.update", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.update",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.update",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"label",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.update",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"active",
					JsonFieldKind::Boolean,
					issue,
					"gateway.channels.accounts.update",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"connected",
					JsonFieldKind::Boolean,
					issue,
					"gateway.channels.accounts.update",
					"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId" || field == "label" || field == "active" || field == "connected") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.update` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsGetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.get", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.get",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.get",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.get` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsCreateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.create", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.create",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.create",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"label",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.create",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"active",
					JsonFieldKind::Boolean,
					issue,
					"gateway.channels.accounts.create",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"connected",
					JsonFieldKind::Boolean,
					issue,
					"gateway.channels.accounts.create",
					"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId" || field == "label" || field == "active" || field == "connected") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.create` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsAccountsDeleteParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.accounts.delete", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.accounts.delete",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.accounts.delete",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.channels.accounts.delete` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateOptionalChannelParam(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				methodName,
				"a string");
		}

		bool PayloadContainsAllStringValues(const std::string& json, std::initializer_list<const char*> values) {
			for (const char* value : values) {
				const std::string token = "\"" + std::string(value) + "\"";
				if (json.find(token) == std::string::npos) {
					return false;
				}
			}

			return true;
		}

		bool ValidateToolsCallPreviewParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.tools.call.preview", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"tool",
				JsonFieldKind::String,
				issue,
				"gateway.tools.call.preview",
				"a string")) {
				return false;
			}

			const auto argsIt = fieldKinds.find("args");
			if (argsIt != fieldKinds.end()) {
				if (argsIt->second != JsonFieldKind::Object &&
					argsIt->second != JsonFieldKind::String &&
					argsIt->second != JsonFieldKind::Array) {
					SetIssue(
						issue,
						"schema_invalid_params",
						"Method `gateway.tools.call.preview` requires `params.args` to be a JSON object, "
						"a JSON array, or a JSON string whose decoded value is JSON.");
					return false;
				}
			}

			return true;
		}

		bool ValidateToolsCallExecuteParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.tools.call.execute", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"tool",
				JsonFieldKind::String,
				issue,
				"gateway.tools.call.execute",
				"a string")) {
				return false;
			}

			const std::array<const char*, 6> argsFieldAliases = {
				"args",
				"arguments",
				"parameters",
				"tool_arguments",
				"toolArguments",
				"payload",
			};
			for (const char* fieldName : argsFieldAliases) {
				const auto argsIt = fieldKinds.find(fieldName);
				if (argsIt == fieldKinds.end()) {
					continue;
				}

				if (argsIt->second != JsonFieldKind::Object &&
					argsIt->second != JsonFieldKind::String &&
					argsIt->second != JsonFieldKind::Array) {
					SetIssue(
						issue,
						"schema_invalid_params",
						"Method `gateway.tools.call.execute` requires `params." +
						std::string(fieldName) +
						"` to be a JSON object, a JSON array, or a JSON string whose decoded value is JSON.");
					return false;
				}
			}

			return true;
		}

		bool IsFieldBoolean(const std::string& json, const std::string& fieldName) {
			std::size_t tokenPos = 0;
			if (!ContainsFieldToken(json, fieldName, tokenPos)) {
				return false;
			}

			std::size_t valuePos = json.find(':', tokenPos);
			if (valuePos == std::string::npos) {
				return false;
			}

			++valuePos;
			while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
				++valuePos;
			}

			return json.compare(valuePos, 4, "true") == 0 || json.compare(valuePos, 5, "false") == 0;
		}

		bool ValidateOptionalActiveParam(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"active",
				JsonFieldKind::Boolean,
				issue,
				methodName,
				"boolean");
		}

		bool ValidateTaskDeltasGetParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(
				request,
				issue,
				"gateway.runtime.taskDeltas.get",
				fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"runId",
				JsonFieldKind::String,
				issue,
				"gateway.runtime.taskDeltas.get",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "runId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.runtime.taskDeltas.get` does not allow `params." +
					field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateTaskDeltasClearParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(
				request,
				issue,
				"gateway.runtime.taskDeltas.clear",
				fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"runId",
				JsonFieldKind::String,
				issue,
				"gateway.runtime.taskDeltas.clear",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "runId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.runtime.taskDeltas.clear` does not allow `params." +
					field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChatSendParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "chat.send", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"sessionKey",
				JsonFieldKind::String,
				issue,
				"chat.send",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"message",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"bodyForCommands",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"bodyForAgent",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"idempotencyKey",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"forceError",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"attachments",
					JsonFieldKind::Array,
					issue,
					"chat.send",
					"an array") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"deliver",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"detached",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"originatingChannel",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"originatingTo",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"clientMode",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"clientConnectionId",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"hasConnectedClient",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"mainKey",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"clientCaps",
					JsonFieldKind::Array,
					issue,
					"chat.send",
					"an array") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"pushLifecycle",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"inlineInvocationAuthorizedSender",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"inlineInvocationSenderIsOwner",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"allowInlineToolImmediateExecution",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"channelId",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"from",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"to",
					JsonFieldKind::String,
					issue,
					"chat.send",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"skipWhenConfigEmpty",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"configEmpty",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"isStopLikeInbound",
					JsonFieldKind::Boolean,
					issue,
					"chat.send",
					"boolean") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"abortCutoffTimestampMs",
					JsonFieldKind::Number,
					issue,
					"chat.send",
					"number") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"inboundTimestampMs",
					JsonFieldKind::Number,
					issue,
					"chat.send",
					"number")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "sessionKey" ||
					field == "message" ||
					field == "bodyForCommands" ||
					field == "bodyForAgent" ||
					field == "idempotencyKey" ||
					field == "forceError" ||
					field == "attachments" ||
					field == "deliver" ||
					field == "detached" ||
					field == "originatingChannel" ||
					field == "originatingTo" ||
					field == "clientMode" ||
					field == "clientConnectionId" ||
					field == "hasConnectedClient" ||
					field == "mainKey" ||
					field == "clientCaps" ||
					field == "pushLifecycle" ||
					field == "inlineInvocationAuthorizedSender" ||
					field == "inlineInvocationSenderIsOwner" ||
					field == "allowInlineToolImmediateExecution" ||
					field == "channelId" ||
					field == "from" ||
					field == "to" ||
					field == "skipWhenConfigEmpty" ||
					field == "configEmpty" ||
					field == "isStopLikeInbound" ||
					field == "abortCutoffTimestampMs" ||
					field == "inboundTimestampMs") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `chat.send` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChatAbortParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "chat.abort", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"sessionKey",
				JsonFieldKind::String,
				issue,
				"chat.abort",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"runId",
					JsonFieldKind::String,
					issue,
					"chat.abort",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "sessionKey" || field == "runId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `chat.abort` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChatInjectParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "chat.inject", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"sessionKey",
				JsonFieldKind::String,
				issue,
				"chat.inject",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"message",
					JsonFieldKind::String,
					issue,
					"chat.inject",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"label",
					JsonFieldKind::String,
					issue,
					"chat.inject",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "sessionKey" || field == "message" || field == "label") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `chat.inject` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChatEventsPollParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(
				request,
				issue,
				"chat.events.poll",
				fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"sessionKey",
				JsonFieldKind::String,
				issue,
				"chat.events.poll",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"limit",
					JsonFieldKind::Number,
					issue,
					"chat.events.poll",
					"numeric")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "sessionKey" || field == "limit") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `chat.events.poll` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateSessionsCountParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.sessions.count", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "scope", JsonFieldKind::String, issue, "gateway.sessions.count", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "active", JsonFieldKind::Boolean, issue, "gateway.sessions.count", "boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "scope" || field == "active") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.sessions.count` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateSessionMutationParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"sessionId",
				JsonFieldKind::String,
				issue,
				methodName,
				"a string") &&
				RequireFieldKindIfPresent(
					fieldKinds,
					"scope",
					JsonFieldKind::String,
					issue,
					methodName,
					"a string") &&
				RequireFieldKindIfPresent(
					fieldKinds,
					"active",
					JsonFieldKind::Boolean,
					issue,
					methodName,
					"boolean");
		}

		bool IsFieldNumber(const std::string& json, const std::string& fieldName) {
			std::size_t tokenPos = 0;
			if (!ContainsFieldToken(json, fieldName, tokenPos)) {
				return false;
			}

			std::size_t valuePos = json.find(':', tokenPos);
			if (valuePos == std::string::npos) {
				return false;
			}

			++valuePos;
			while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
				++valuePos;
			}

			if (valuePos >= json.size()) {
				return false;
			}

			const char ch = json[valuePos];
			return std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '-';
		}

		bool IsArrayFieldExplicitlyEmpty(const std::string& json, const std::string& fieldName) {
			std::size_t tokenPos = 0;
			if (!ContainsFieldToken(json, fieldName, tokenPos)) {
				return false;
			}

			std::size_t valuePos = json.find(':', tokenPos);
			if (valuePos == std::string::npos) {
				return false;
			}

			++valuePos;
			while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
				++valuePos;
			}

			if (valuePos >= json.size() || json[valuePos] != '[') {
				return false;
			}

			++valuePos;
			while (valuePos < json.size() && std::isspace(static_cast<unsigned char>(json[valuePos])) != 0) {
				++valuePos;
			}

			return valuePos < json.size() && json[valuePos] == ']';
		}

		bool PayloadContainsAllFieldTokens(const std::string& json, std::initializer_list<const char*> fieldNames) {
			for (const char* fieldName : fieldNames) {
				std::size_t tokenPos = 0;
				if (!ContainsFieldToken(json, fieldName, tokenPos)) {
					return false;
				}
			}

			return true;
		}

		bool ValidateNoParamsAllowed(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			if (!request.paramsJson.has_value()) {
				return true;
			}

			SetIssue(issue, "schema_invalid_params", "Method `" + methodName + "` does not accept `params`.");
			return false;
		}

		bool ValidateStringIdParam(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName,
			const std::string& fieldName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				fieldName,
				JsonFieldKind::String,
				issue,
				methodName,
				"a string");
		}

		bool ValidateNodePairRequestParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `node.pair.request` requires `params.nodeId` string.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.pair.request", fieldKinds)) {
				return false;
			}

			const auto nodeIdIt = fieldKinds.find("nodeId");
			if (nodeIdIt == fieldKinds.end() || nodeIdIt->second != JsonFieldKind::String) {
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `node.pair.request` requires `params.nodeId` to be a string.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "displayName", JsonFieldKind::String, issue, "node.pair.request", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "platform", JsonFieldKind::String, issue, "node.pair.request", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "version", JsonFieldKind::String, issue, "node.pair.request", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "coreVersion", JsonFieldKind::String, issue, "node.pair.request", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "uiVersion", JsonFieldKind::String, issue, "node.pair.request", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "deviceFamily", JsonFieldKind::String, issue, "node.pair.request", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "modelIdentifier", JsonFieldKind::String, issue, "node.pair.request", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "caps", JsonFieldKind::Array, issue, "node.pair.request", "an array") ||
				!RequireFieldKindIfPresent(fieldKinds, "commands", JsonFieldKind::Array, issue, "node.pair.request", "an array") ||
				!RequireFieldKindIfPresent(fieldKinds, "permissions", JsonFieldKind::Object, issue, "node.pair.request", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "remoteIp", JsonFieldKind::String, issue, "node.pair.request", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "silent", JsonFieldKind::Boolean, issue, "node.pair.request", "boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName(
					{ "nodeId", "displayName", "platform", "version", "coreVersion", "uiVersion", "deviceFamily", "modelIdentifier", "caps", "commands", "permissions", "remoteIp", "silent" },
					field)) {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.request` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodePairApproveParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.approve` requires `params.requestId` string.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.pair.approve", fieldKinds)) {
				return false;
			}

			const auto requestIdIt = fieldKinds.find("requestId");
			if (requestIdIt == fieldKinds.end() || requestIdIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.approve` requires `params.requestId` to be a string.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "callerScopes", JsonFieldKind::Array, issue, "node.pair.approve", "an array") ||
				!RequireFieldKindIfPresent(fieldKinds, "scopes", JsonFieldKind::Array, issue, "node.pair.approve", "an array")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "requestId", "callerScopes", "scopes" }, field)) {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.approve` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodePairRejectParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.reject` requires `params.requestId` string.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.pair.reject", fieldKinds)) {
				return false;
			}

			const auto requestIdIt = fieldKinds.find("requestId");
			if (requestIdIt == fieldKinds.end() || requestIdIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.reject` requires `params.requestId` to be a string.");
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "requestId") {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.reject` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodePairVerifyParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.verify` requires `params.nodeId` and `params.token` strings.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.pair.verify", fieldKinds)) {
				return false;
			}

			const auto nodeIdIt = fieldKinds.find("nodeId");
			const auto tokenIt = fieldKinds.find("token");
			if (nodeIdIt == fieldKinds.end() || nodeIdIt->second != JsonFieldKind::String ||
				tokenIt == fieldKinds.end() || tokenIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.verify` requires `params.nodeId` and `params.token` to be strings.");
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "nodeId" || field == "token") {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.pair.verify` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodeRenameParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.rename` requires `params.nodeId` and `params.displayName` strings.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.rename", fieldKinds)) {
				return false;
			}

			const auto nodeIdIt = fieldKinds.find("nodeId");
			const auto displayNameIt = fieldKinds.find("displayName");
			if (nodeIdIt == fieldKinds.end() || nodeIdIt->second != JsonFieldKind::String ||
				displayNameIt == fieldKinds.end() || displayNameIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.rename` requires `params.nodeId` and `params.displayName` to be strings.");
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "nodeId" || field == "displayName") {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.rename` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodeDescribeParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.describe` requires `params.nodeId` string.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.describe", fieldKinds)) {
				return false;
			}

			const auto nodeIdIt = fieldKinds.find("nodeId");
			if (nodeIdIt == fieldKinds.end() || nodeIdIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.describe` requires `params.nodeId` to be a string.");
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "nodeId") {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.describe` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodeCanvasCapabilityRefreshParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.canvas.capability.refresh` requires `params.sessionKey` and `params.canvasHostUrl` strings.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.canvas.capability.refresh", fieldKinds)) {
				return false;
			}

			const auto sessionKeyIt = fieldKinds.find("sessionKey");
			const auto canvasHostUrlIt = fieldKinds.find("canvasHostUrl");
			if (sessionKeyIt == fieldKinds.end() || sessionKeyIt->second != JsonFieldKind::String ||
				canvasHostUrlIt == fieldKinds.end() || canvasHostUrlIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.canvas.capability.refresh` requires `params.sessionKey` and `params.canvasHostUrl` to be strings.");
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "sessionKey" || field == "canvasHostUrl") {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.canvas.capability.refresh` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodePendingPullParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pending.pull` requires `params.nodeId` string.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.pending.pull", fieldKinds)) {
				return false;
			}

			const auto nodeIdIt = fieldKinds.find("nodeId");
			if (nodeIdIt == fieldKinds.end() || nodeIdIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pending.pull` requires `params.nodeId` to be a string.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "declaredCommands", JsonFieldKind::Array, issue, "node.pending.pull", "an array") ||
				!RequireFieldKindIfPresent(fieldKinds, "commands", JsonFieldKind::Array, issue, "node.pending.pull", "an array")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "nodeId" || field == "declaredCommands" || field == "commands") {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.pending.pull` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodePendingAckParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pending.ack` requires `params.nodeId` string and optional `params.ids` array.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.pending.ack", fieldKinds)) {
				return false;
			}

			const auto nodeIdIt = fieldKinds.find("nodeId");
			if (nodeIdIt == fieldKinds.end() || nodeIdIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.pending.ack` requires `params.nodeId` to be a string.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "ids", JsonFieldKind::Array, issue, "node.pending.ack", "an array")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "nodeId" || field == "ids") {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.pending.ack` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodeInvokeParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.invoke` requires `params.nodeId`, `params.command`, and `params.idempotencyKey` strings.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.invoke", fieldKinds)) {
				return false;
			}

			const auto nodeIdIt = fieldKinds.find("nodeId");
			const auto commandIt = fieldKinds.find("command");
			const auto idempotencyKeyIt = fieldKinds.find("idempotencyKey");
			if (nodeIdIt == fieldKinds.end() || nodeIdIt->second != JsonFieldKind::String ||
				commandIt == fieldKinds.end() || commandIt->second != JsonFieldKind::String ||
				idempotencyKeyIt == fieldKinds.end() || idempotencyKeyIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.invoke` requires `params.nodeId`, `params.command`, and `params.idempotencyKey` to be strings.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "params", JsonFieldKind::Object, issue, "node.invoke", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "timeoutMs", JsonFieldKind::Number, issue, "node.invoke", "numeric") ||
				!RequireFieldKindIfPresent(fieldKinds, "allowlist", JsonFieldKind::Array, issue, "node.invoke", "an array") ||
				!RequireFieldKindIfPresent(fieldKinds, "declaredCommands", JsonFieldKind::Array, issue, "node.invoke", "an array") ||
				!RequireFieldKindIfPresent(fieldKinds, "commands", JsonFieldKind::Array, issue, "node.invoke", "an array")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "nodeId", "command", "params", "timeoutMs", "idempotencyKey", "allowlist", "declaredCommands", "commands" }, field)) {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.invoke` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodeInvokeResultParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.invoke.result` requires `params.runId` and `params.nodeId` strings.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.invoke.result", fieldKinds)) {
				return false;
			}

			const auto runIdIt = fieldKinds.find("runId");
			const auto nodeIdIt = fieldKinds.find("nodeId");
			if (runIdIt == fieldKinds.end() || runIdIt->second != JsonFieldKind::String ||
				nodeIdIt == fieldKinds.end() || nodeIdIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.invoke.result` requires `params.runId` and `params.nodeId` to be strings.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "payload", JsonFieldKind::Object, issue, "node.invoke.result", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "payloadJSON", JsonFieldKind::Object, issue, "node.invoke.result", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "status", JsonFieldKind::String, issue, "node.invoke.result", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "error", JsonFieldKind::String, issue, "node.invoke.result", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "errorCode", JsonFieldKind::String, issue, "node.invoke.result", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "errorMessage", JsonFieldKind::String, issue, "node.invoke.result", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "command", JsonFieldKind::String, issue, "node.invoke.result", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "idempotencyKey", JsonFieldKind::String, issue, "node.invoke.result", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "ts", JsonFieldKind::Number, issue, "node.invoke.result", "numeric")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "runId", "nodeId", "payload", "payloadJSON", "status", "error", "errorCode", "errorMessage", "command", "idempotencyKey", "ts" }, field)) {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.invoke.result` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateNodeEventParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			if (!request.paramsJson.has_value()) {
				SetIssue(issue, "schema_invalid_params", "Method `node.event` requires `params.event` string.");
				return false;
			}

			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "node.event", fieldKinds)) {
				return false;
			}

			const auto eventIt = fieldKinds.find("event");
			if (eventIt == fieldKinds.end() || eventIt->second != JsonFieldKind::String) {
				SetIssue(issue, "schema_invalid_params", "Method `node.event` requires `params.event` to be a string.");
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "nodeId", JsonFieldKind::String, issue, "node.event", "a string") ||
				!RequireFieldKindIfPresent(fieldKinds, "payload", JsonFieldKind::Object, issue, "node.event", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "payloadJSON", JsonFieldKind::Object, issue, "node.event", "an object") ||
				!RequireFieldKindIfPresent(fieldKinds, "ts", JsonFieldKind::Number, issue, "node.event", "numeric") ||
				!RequireFieldKindIfPresent(fieldKinds, "runId", JsonFieldKind::String, issue, "node.event", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (ContainsFieldName({ "event", "nodeId", "payload", "payloadJSON", "ts", "runId" }, field)) {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `node.event` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateLogsTailParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.logs.tail", fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"limit",
				JsonFieldKind::Number,
				issue,
				"gateway.logs.tail",
				"numeric");
		}

		bool ValidateLogsCountParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.logs.count", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(fieldKinds, "level", JsonFieldKind::String, issue, "gateway.logs.count", "a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "level") {
					continue;
				}

				SetIssue(issue, "schema_invalid_params", "Method `gateway.logs.count` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateSessionsCompactParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.sessions.compact", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"dryRun",
				JsonFieldKind::Boolean,
				issue,
				"gateway.sessions.compact",
				"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "dryRun") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.sessions.compact` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateSessionsPreviewParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.sessions.preview", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"sessionId",
				JsonFieldKind::String,
				issue,
				"gateway.sessions.preview",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "sessionId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.sessions.preview` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsCreateParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.create", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.create",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"name",
					JsonFieldKind::String,
					issue,
					"gateway.agents.create",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"active",
					JsonFieldKind::Boolean,
					issue,
					"gateway.agents.create",
					"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "name" || field == "active") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.create` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateChannelsLogoutParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.channels.logout", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"channel",
				JsonFieldKind::String,
				issue,
				"gateway.channels.logout",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"accountId",
					JsonFieldKind::String,
					issue,
					"gateway.channels.logout",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "channel" || field == "accountId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.channels.logout` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateConfigSetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.config.set", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"bind",
				JsonFieldKind::String,
				issue,
				"gateway.config.set",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"port",
					JsonFieldKind::Number,
					issue,
					"gateway.config.set",
					"numeric") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"model",
					JsonFieldKind::String,
					issue,
					"gateway.config.set",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"streaming",
					JsonFieldKind::Boolean,
					issue,
					"gateway.config.set",
					"boolean")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "bind" || field == "port" || field == "model" || field == "streaming") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.config.set` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateConfigSchemaLookupParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(
				request,
				issue,
				"gateway.config.schema.lookup",
				fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"path",
				JsonFieldKind::String,
				issue,
				"gateway.config.schema.lookup",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "path") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.config.schema.lookup` does not allow `params." +
					field +
					"`.");
				return false;
			}

			return true;
		}

		bool ValidateConfigApplyPatchParams(
			const RequestFrame& request,
			SchemaValidationIssue& issue,
			const std::string& methodName) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, methodName, fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"baseHash",
				JsonFieldKind::String,
				issue,
				methodName,
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"raw",
					JsonFieldKind::String,
					issue,
					methodName,
					"a JSON string payload")) {
				return false;
			}
			if (fieldKinds.find("raw") == fieldKinds.end()) {
				SetIssue(
					issue,
					"schema_missing_field",
					"Method `" + methodName + "` requires `params.raw`."
				);
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "baseHash" || field == "raw") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `" + methodName + "` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateToolsEffectiveParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "tools.effective", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"sessionId",
				JsonFieldKind::String,
				issue,
				"tools.effective",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"agentId",
					JsonFieldKind::String,
					issue,
					"tools.effective",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"category",
					JsonFieldKind::String,
					issue,
					"tools.effective",
					"a string")) {
				return false;
			}
			if (fieldKinds.find("sessionId") == fieldKinds.end() ||
				fieldKinds.find("agentId") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `tools.effective` requires `params.sessionId` and `params.agentId`.");
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "sessionId" || field == "agentId" || field == "category") {
					continue;
				}
				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `tools.effective` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateDevicePairRemoveParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "device.pair.remove", fieldKinds)) {
				return false;
			}
			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"nodeId",
				JsonFieldKind::String,
				issue,
				"device.pair.remove",
				"a string")) {
				return false;
			}
			if (fieldKinds.find("nodeId") == fieldKinds.end()) {
				SetIssue(issue, "schema_missing_field", "Method `device.pair.remove` requires `params.nodeId`.");
				return false;
			}
			for (const auto& [field, _] : fieldKinds) {
				if (field == "nodeId") {
					continue;
				}
				SetIssue(issue, "schema_invalid_params", "Method `device.pair.remove` does not allow `params." + field + "`.");
				return false;
			}
			return true;
		}

		bool ValidateAgentsFilesListParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.list", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.list",
				"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.list` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsFilesGetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.get", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.get",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"path",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.get",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "path") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.get` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsFilesSetParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.set", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.set",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"path",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.set",
					"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"content",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.set",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "path" || field == "content") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.set` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsFilesDeleteParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.delete", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.delete",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"path",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.delete",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "path") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.delete` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidateAgentsFilesExistsParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.agents.files.exists", fieldKinds)) {
				return false;
			}

			if (!RequireFieldKindIfPresent(
				fieldKinds,
				"agentId",
				JsonFieldKind::String,
				issue,
				"gateway.agents.files.exists",
				"a string") ||
				!RequireFieldKindIfPresent(
					fieldKinds,
					"path",
					JsonFieldKind::String,
					issue,
					"gateway.agents.files.exists",
					"a string")) {
				return false;
			}

			for (const auto& [field, _] : fieldKinds) {
				if (field == "agentId" || field == "path") {
					continue;
				}

				SetIssue(
					issue,
					"schema_invalid_params",
					"Method `gateway.agents.files.exists` does not allow `params." + field + "`.");
				return false;
			}

			return true;
		}

		bool ValidatePingParams(const RequestFrame& request, SchemaValidationIssue& issue) {
			ParsedObjectFieldKinds fieldKinds;
			if (!TryParseRequestParamsObject(request, issue, "gateway.ping", fieldKinds)) {
				return false;
			}

			return RequireFieldKindIfPresent(
				fieldKinds,
				"echo",
				JsonFieldKind::String,
				issue,
				"gateway.ping",
				"a string");
		}

		bool ValidateResponseEnvelope(const ResponseFrame& response, SchemaValidationIssue& issue) {
			if (response.id.empty()) {
				SetIssue(issue, "schema_invalid_response", "Response `id` is required.");
				return false;
			}

			if (!response.ok) {
				SetIssue(issue, "schema_invalid_response", "Expected success response (`ok=true`) for method fixture validation.");
				return false;
			}

			if (!response.payloadJson.has_value() || !IsJsonObjectShape(response.payloadJson.value())) {
				SetIssue(issue, "schema_invalid_response", "Response `payload` must be a JSON object.");
				return false;
			}

			return true;
		}

		bool MethodMatchesPattern(
			const std::string& method,
			const char* pattern) {
			if (pattern == nullptr) {
				return false;
			}

			const std::string_view patternView(pattern);
			if (patternView.size() >= 2 &&
				patternView.substr(patternView.size() - 2) == ".*") {
				const std::string_view prefix =
					patternView.substr(0, patternView.size() - 1);
				const std::string_view methodView(method);
				return methodView.size() >= prefix.size() &&
					methodView.compare(0, prefix.size(), prefix) == 0;
			}

			return method == pattern;
		}

		std::string_view ResolveGeneratedRequestPolicyType(
			const std::string& method) {
			for (const auto& rule : generated::GetSchemaMethodRules()) {
				if (method == rule.name) {
					return rule.requestPolicyType;
				}
			}

			for (const auto& patternRule : generated::GetSchemaMethodPatternRules()) {
				if (MethodMatchesPattern(method, patternRule.pattern)) {
					return patternRule.requestPolicyType;
				}
			}

			return {};
		}

		const char* ResolveGeneratedStringIdField(
			const std::string& method) {
			for (const auto& rule : generated::GetSchemaMethodRules()) {
				if (method == rule.name) {
					return rule.stringIdField;
				}
			}

			return nullptr;
		}

	} // namespace

	bool GatewayProtocolSchemaValidator::ValidateRequest(const RequestFrame& request, SchemaValidationIssue& issue) {
		issue = {};

		if (request.id.empty()) {
			SetIssue(issue, "schema_missing_field", "Request `id` is required.");
			return false;
		}

		if (request.method.empty()) {
			SetIssue(issue, "schema_missing_field", "Request `method` is required.");
			return false;
		}

		using RequestValidator = std::function<bool(const RequestFrame&, SchemaValidationIssue&)>;

		static const std::unordered_map<std::string, RequestValidator> directValidators = {
			{ "gateway.ping", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidatePingParams(r, i); } },
			{ "cron.status", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateCronStatusParams(r, i); } },
			{ "cron.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateCronListParams(r, i); } },
			{ "cron.add", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateCronAddParams(r, i); } },
			{ "cron.update", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateCronUpdateParams(r, i); } },
			{ "cron.remove", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateCronRemoveParams(r, i); } },
			{ "cron.run", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateCronRunParams(r, i); } },
			{ "cron.runs", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateCronRunsParams(r, i); } },
			{ "wake", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateWakeParams(r, i); } },
			{ "chat.send", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChatSendParams(r, i); } },
			{ "chat.abort", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChatAbortParams(r, i); } },
			{ "chat.inject", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChatInjectParams(r, i); } },
			{ "chat.events.poll", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChatEventsPollParams(r, i); } },
			{ "gateway.runtime.taskDeltas.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateTaskDeltasGetParams(r, i); } },
			{ "gateway.runtime.taskDeltas.clear", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateTaskDeltasClearParams(r, i); } },
			{ "gateway.runtime.health.dependencies", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.runtime.health.readiness", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.runtime.health.capabilities", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.runtime.mutations.status", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.runtime.policy.resolve", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.runtime.plugins.transitions.policy.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.runtime.plugins.transitions.policy.set", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.runtime.plugins.transitions.export", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.config.schema.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "config.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "config.schema", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "doctor.memory.status", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "doctor.memory.dreamDiary", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "doctor.memory.backfillDreamDiary", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "doctor.memory.resetDreamDiary", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "doctor.memory.resetGroundedShortTerm", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.config.schema.lookup", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateConfigSchemaLookupParams(r, i); } },
			{ "config.schema.lookup", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateConfigSchemaLookupParams(r, i); } },
			{ "gateway.logs.tail", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateLogsTailParams(r, i); } },
			{ "gateway.logs.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateLogsCountParams(r, i); } },
			{ "gateway.sessions.compact", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionsCompactParams(r, i); } },
			{ "gateway.sessions.preview", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionsPreviewParams(r, i); } },
			{ "gateway.sessions.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionsCountParams(r, i); } },
			{ "gateway.sessions.patch", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionMutationParams(r, i, r.method); } },
			{ "gateway.sessions.create", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionMutationParams(r, i, r.method); } },
			{ "gateway.sessions.reset", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionMutationParams(r, i, r.method); } },
			{ "gateway.channels.route.resolve", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteResolveParams(r, i); } },
			{ "gateway.channels.route.set", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteSetParams(r, i); } },
			{ "gateway.channels.route.delete", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteDeleteParams(r, i); } },
			{ "gateway.channels.route.exists", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteExistsParams(r, i); } },
			{ "gateway.channels.route.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteGetParams(r, i); } },
			{ "gateway.channels.route.restore", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteRestoreParams(r, i); } },
			{ "gateway.channels.route.reset", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRouteRestoreParams(r, i); } },
			{ "gateway.channels.route.patch", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutePatchParams(r, i); } },
			{ "gateway.channels.routes.clear", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutesClearParams(r, i); } },
			{ "gateway.channels.routes.restore", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutesRestoreParams(r, i); } },
			{ "gateway.channels.routes.reset", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutesResetParams(r, i); } },
			{ "gateway.channels.routes.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsRoutesCountParams(r, i); } },
			{ "gateway.channels.accounts.activate", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsActivateParams(r, i); } },
			{ "gateway.channels.accounts.deactivate", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsDeactivateParams(r, i); } },
			{ "gateway.channels.accounts.exists", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsExistsParams(r, i); } },
			{ "gateway.channels.accounts.update", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsUpdateParams(r, i); } },
			{ "gateway.channels.accounts.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsGetParams(r, i); } },
			{ "gateway.channels.accounts.create", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsCreateParams(r, i); } },
			{ "gateway.channels.accounts.delete", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsDeleteParams(r, i); } },
			{ "gateway.channels.accounts.clear", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsClearParams(r, i); } },
			{ "gateway.channels.accounts.restore", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsRestoreParams(r, i); } },
			{ "gateway.channels.accounts.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsAccountsCountParams(r, i); } },
			{ "gateway.channels.accounts.reset", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.status.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.status.exists", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.status.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.status", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.routes", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "gateway.channels.accounts", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalChannelParam(r, i, r.method); } },
			{ "node.pair.request", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodePairRequestParams(r, i); } },
			{ "node.pair.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "node.pair.approve", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodePairApproveParams(r, i); } },
			{ "node.pair.reject", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodePairRejectParams(r, i); } },
			{ "node.pair.verify", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodePairVerifyParams(r, i); } },
			{ "device.pair.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "device.pair.approve", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodePairApproveParams(r, i); } },
			{ "device.pair.reject", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodePairRejectParams(r, i); } },
			{ "device.pair.remove", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateDevicePairRemoveParams(r, i); } },
			{ "node.rename", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodeRenameParams(r, i); } },
			{ "node.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "node.describe", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodeDescribeParams(r, i); } },
			{ "node.canvas.capability.refresh", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodeCanvasCapabilityRefreshParams(r, i); } },
			{ "node.pending.pull", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodePendingPullParams(r, i); } },
			{ "node.pending.ack", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodePendingAckParams(r, i); } },
			{ "node.invoke", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodeInvokeParams(r, i); } },
			{ "node.invoke.result", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodeInvokeResultParams(r, i); } },
			{ "node.event", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNodeEventParams(r, i); } },
			{ "gateway.tools.call.preview", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateToolsCallPreviewParams(r, i); } },
			{ "gateway.tools.call.execute", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateToolsCallExecuteParams(r, i); } },
			{ "gateway.agents.create", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsCreateParams(r, i); } },
			{ "gateway.agents.update", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsCreateParams(r, i); } },
			{ "gateway.agents.files.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesListParams(r, i); } },
			{ "gateway.agents.files.get", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesGetParams(r, i); } },
			{ "gateway.agents.files.set", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesSetParams(r, i); } },
			{ "gateway.agents.files.delete", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesDeleteParams(r, i); } },
			{ "gateway.agents.files.exists", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateAgentsFilesExistsParams(r, i); } },
			{ "gateway.channels.logout", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateChannelsLogoutParams(r, i); } },
			{ "gateway.config.set", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateConfigSetParams(r, i); } },
			{ "config.set", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateConfigSetParams(r, i); } },
			{ "config.apply", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateConfigApplyPatchParams(r, i, r.method); } },
			{ "config.patch", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateConfigApplyPatchParams(r, i, r.method); } },
			{ "gateway.tools.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalActiveParam(r, i, r.method); } },
			{ "tools.catalog", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "tools.effective", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateToolsEffectiveParams(r, i); } },
			{ "gateway.agents.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalActiveParam(r, i, r.method); } },
			{ "agents.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalActiveParam(r, i, r.method); } },
			{ "gateway.agents.count", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalActiveParam(r, i, r.method); } },
			{ "gateway.session.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalSessionListParams(r, i, r.method); } },
			{ "sessions.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateOptionalSessionListParams(r, i, r.method); } },
			{ "sessions.create", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateSessionMutationParams(r, i, r.method); } },
			{ "models.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.models.list", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "skills.commands", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
			{ "gateway.skills.commands", [](const RequestFrame& r, SchemaValidationIssue& i) { return ValidateNoParamsAllowed(r, i, r.method); } },
		};

		if (const auto it = directValidators.find(request.method); it != directValidators.end()) {
			return it->second(request, issue);
		}

		if (const char* generatedStringIdField = ResolveGeneratedStringIdField(request.method);
			generatedStringIdField != nullptr && generatedStringIdField[0] != '\0') {
			return ValidateStringIdParam(
				request,
				issue,
				request.method,
				generatedStringIdField);
		}


		const std::string_view generatedPolicyType =
			ResolveGeneratedRequestPolicyType(request.method);
		if (!generatedPolicyType.empty()) {
			if (generatedPolicyType == "none") {
				return ValidateNoParamsAllowed(request, issue, request.method);
			}

			return true;
		}

		return true;
	}

} // namespace blazeclaw::gateway::protocol
