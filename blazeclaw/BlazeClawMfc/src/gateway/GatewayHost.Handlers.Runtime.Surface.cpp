#include "pch.h"
#include "GatewayHost.h"
#include "GatewayHostHandlersRuntime.h"
#include "GatewayHostRuntimeLocalHelpers.h"
#include "GatewayJsonUtils.h"
#include "Telemetry.h"
#include "ChatRunStageContext.h"
#include "TaskDeltaRepository.h"
#include "TaskDeltaLegacyAdapter.h"
#include "TaskDeltaSchemaValidator.h"
#include "RuntimeSequencingPolicy.h"
#include "RuntimeToolCallNormalizer.h"
#include "RuntimeTranscriptGuard.h"
#include "RecoveryPolicyEngine.h"
#include "SendPolicyResolver.h"
#include "ToolPolicyPipeline.h"
#include "TranscriptPolicyResolver.h"
#include "GatewayLifecycleEventEmitter.h"
#include "RunSummaryBuilder.h"
#include "BranchDecisionDiagnostics.h"
#include "ChatTranscriptStore.h"
#include "ChatAbortCoordinator.h"
#include "ChatHistoryPolicy.h"
#include "ChatRoutePolicy.h"
#include "ChatOrchestrationPolicy.h"
#include "ToolEventRecipientPolicy.h"
#include "ChatControlPlaneService.h"
#include "GatewayEventFanoutService.h"
#include "executors/EmailScheduleExecutor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <iomanip>
#include <regex>
#include <sstream>
#include <nlohmann/json.hpp>

namespace blazeclaw::gateway {

	namespace handlers::runtime {

		void RuntimeSurfaceHandlers::RegisterAll(GatewayHost& host) {
			using namespace blazeclaw::gateway::runtime_local;
			// P1: Scheduler / cron contract stubs (OpenClaw-compatible envelope fields)
			host.m_dispatcher.Register("cron.list", [](const protocol::RequestFrame& request) {
				const auto requestedLimit =
					ExtractSizeParam(request.paramsJson, "limit").value_or(20);
				const auto requestedOffset =
					ExtractSizeParam(request.paramsJson, "offset").value_or(0);
				const std::size_t limit =
					(std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 200 }));
				const std::size_t offset = requestedOffset;

				return protocol::OkResponse(
					request,
					"{\"jobs\":[],\"total\":0,\"limit\":" + std::to_string(limit) +
					",\"offset\":" + std::to_string(offset) +
					",\"nextOffset\":null,\"hasMore\":false}");
				});
			host.m_dispatcher.Register("cron.status", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(
					request,
					"{\"enabled\":false,\"jobs\":0,\"nextWakeAtMs\":null}");
				});
			host.m_dispatcher.Register("cron.add", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"added\":true,\"cronId\":\"cron-1\"}");
				});
			host.m_dispatcher.Register("cron.update", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"updated\":true,\"cronId\":\"cron-1\"}");
				});
			host.m_dispatcher.Register("cron.remove", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"removed\":true,\"cronId\":\"cron-1\"}");
				});
			host.m_dispatcher.Register("cron.run", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"runId\":\"cron-run-1\",\"started\":true}");
				});
			host.m_dispatcher.Register("cron.runs", [](const protocol::RequestFrame& request) {
				const auto requestedLimit =
					ExtractSizeParam(request.paramsJson, "limit").value_or(20);
				const auto requestedOffset =
					ExtractSizeParam(request.paramsJson, "offset").value_or(0);
				const std::size_t limit =
					(std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 200 }));
				const std::size_t offset = requestedOffset;

				return protocol::OkResponse(
					request,
					"{\"entries\":[],\"total\":0,\"limit\":" + std::to_string(limit) +
					",\"offset\":" + std::to_string(offset) +
					",\"nextOffset\":null,\"hasMore\":false}");
				});
			host.m_dispatcher.Register("wizard.start", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"started\":true,\"wizardId\":\"wizard-1\"}");
				});
			host.m_dispatcher.Register("wizard.next", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"next\":true,\"wizardId\":\"wizard-1\"}");
				});
			host.m_dispatcher.Register("wizard.cancel", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"cancelled\":true,\"wizardId\":\"wizard-1\"}");
				});
			host.m_dispatcher.Register("wizard.status", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"status\":\"idle\",\"wizardId\":\"wizard-1\"}");
				});
			host.m_dispatcher.Register("talk.config", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"config\":{},\"updated\":true}");
				});
			host.m_dispatcher.Register("talk.speak", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"spoken\":true,\"text\":\"Hello!\"}");
				});
			host.m_dispatcher.Register("talk.mode", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"mode\":\"default\",\"updated\":true}");
				});
			host.m_dispatcher.Register("voicewake.get", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"enabled\":false,\"model\":\"default\"}");
				});
			host.m_dispatcher.Register("voicewake.set", [](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"enabled\":true,\"model\":\"default\",\"updated\":true}");
				});
			host.m_dispatcher.Register(
				"gateway.runtime.plugins.capabilities",
				[&host](const protocol::RequestFrame& request) {
					const auto contracts =
						host.m_pluginRuntimeState.ListCapabilityContracts();

					return protocol::OkResponse(request, "{\"capabilities\":" +
						SerializePluginRuntimeCapabilitiesJsonLocal(contracts) +
						",\"count\":" +
						std::to_string(contracts.size()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.state",
				[&host](const protocol::RequestFrame& request) {
					const auto snapshot = host.m_pluginRuntimeState.Snapshot();
					const auto importedPluginIds =
						host.m_pluginRuntimeState.ListImportedRuntimePluginIds();

					std::string importedJson = "[";
					for (std::size_t index = 0; index < importedPluginIds.size(); ++index) {
						if (index > 0) {
							importedJson += ",";
						}

						importedJson +=
							"\"" +
							EscapeJsonLocal(importedPluginIds[index]) +
							"\"";
					}
					importedJson += "]";

					const std::size_t activeRegistryCount =
						snapshot.activeRegistry == nullptr
						? 0
						: snapshot.activeRegistry->size();

					return protocol::OkResponse(request, "{\"activeVersion\":" +
						std::to_string(snapshot.activeVersion) +
						",\"httpRouteVersion\":" +
						std::to_string(host.m_pluginRuntimeState.GetHttpRouteVersion()) +
						",\"channelVersion\":" +
						std::to_string(host.m_pluginRuntimeState.GetChannelVersion()) +
						",\"activeRegistryCount\":" +
						std::to_string(activeRegistryCount) +
						",\"httpRoutePinned\":" +
						std::string(snapshot.httpRoute.pinned ? "true" : "false") +
						",\"channelPinned\":" +
						std::string(snapshot.channel.pinned ? "true" : "false") +
						",\"cacheKey\":\"" +
						EscapeJsonLocal(snapshot.cacheKey) +
						"\",\"workspaceDir\":\"" +
						EscapeJsonLocal(snapshot.workspaceDir) +
						"\",\"runtimeSubagentMode\":\"" +
						SerializePluginRuntimeSubagentModeLocal(
							snapshot.runtimeSubagentMode) +
						"\",\"importedPluginIds\":" +
						importedJson +
						",\"importedCount\":" +
						std::to_string(importedPluginIds.size()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.transitions",
				[&host](const protocol::RequestFrame& request) {
					const auto requestedLimit =
						ExtractSizeParam(request.paramsJson, "limit").value_or(20);
					const std::size_t limit =
						(std::max)(
							std::size_t{ 1 },
							(std::min)(requestedLimit, std::size_t{ 128 }));

					const auto transitions =
						host.m_pluginRuntimeState.GetTransitionHistory();
					const auto transitionPolicy =
						host.m_pluginRuntimeState.GetTransitionPolicySettings();

					return protocol::OkResponse(request, "{\"transitions\":" +
						SerializePluginRuntimeTransitionsJsonLocal(
							transitions,
							limit) +
						",\"count\":" +
						std::to_string((std::min)(transitions.size(), limit)) +
						",\"total\":" +
						std::to_string(transitions.size()) +
						",\"retention\":{\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}" +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.transitions.policy.get",
				[&host](const protocol::RequestFrame& request) {
					const auto transitionPolicy =
						host.m_pluginRuntimeState.GetTransitionPolicySettings();

					return protocol::OkResponse(request, "{\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.transitions.policy.set",
				[&host](const protocol::RequestFrame& request) {
					const auto historyLimit =
						ExtractSizeParam(request.paramsJson, "historyLimit").value_or(128);
					const auto exportEnabled =
						ExtractBoolParam(request.paramsJson, "exportEnabled").value_or(false);

					host.m_pluginRuntimeState.SetTransitionPolicySettings(
						PluginRuntimeStateService::TransitionPolicySettings{
							.historyLimit = historyLimit,
							.exportEnabled = exportEnabled,
						});

					const auto transitionPolicy =
						host.m_pluginRuntimeState.GetTransitionPolicySettings();
					return protocol::OkResponse(request, "{\"updated\":true,\"historyLimit\":" +
						std::to_string(transitionPolicy.historyLimit) +
						",\"exportEnabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.transitions.export",
				[&host](const protocol::RequestFrame& request) {
					const auto transitions =
						host.m_pluginRuntimeState.ExportTransitionHistory();
					const auto transitionPolicy =
						host.m_pluginRuntimeState.GetTransitionPolicySettings();

					return protocol::OkResponse(request, "{\"enabled\":" +
						std::string(transitionPolicy.exportEnabled ? "true" : "false") +
						",\"transitions\":" +
						SerializePluginRuntimeTransitionsJsonLocal(
							transitions,
							transitions.size()) +
						",\"count\":" +
						std::to_string(transitions.size()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.imported.list",
				[&host](const protocol::RequestFrame& request) {
					const auto importedPluginIds =
						host.m_pluginRuntimeState.ListImportedRuntimePluginIds();
					std::string importedJson = "[";
					for (std::size_t index = 0; index < importedPluginIds.size(); ++index) {
						if (index > 0) {
							importedJson += ",";
						}

						importedJson +=
							"\"" +
							EscapeJsonLocal(importedPluginIds[index]) +
							"\"";
					}
					importedJson += "]";

					return protocol::OkResponse(request, "{\"plugins\":" +
						importedJson +
						",\"count\":" +
						std::to_string(importedPluginIds.size()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.plugins.lifecycle.reset",
				[&host](const protocol::RequestFrame& request) {
					bool forTest = false;
					if (request.paramsJson.has_value()) {
						json::FindBoolField(
							request.paramsJson.value(),
							"forTest",
							forTest);
					}

					if (!forTest) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_params",
										.message = "Set forTest=true to reset plugin runtime lifecycle state.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					host.m_pluginRuntimeState.ResetForTest();
					const auto snapshot = host.m_pluginRuntimeState.Snapshot();

					return protocol::OkResponse(request, "{\"reset\":true,\"activeVersion\":" +
						std::to_string(snapshot.activeVersion) +
						",\"httpRouteVersion\":" +
						std::to_string(host.m_pluginRuntimeState.GetHttpRouteVersion()) +
						",\"channelVersion\":" +
						std::to_string(host.m_pluginRuntimeState.GetChannelVersion()) +
						"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.reportStatus",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"governanceReportingEnabled\":" +
						std::string(state.governanceReportingEnabled ? "true" : "false") +
						",\"remediationTelemetryPath\":\"" +
						EscapeJsonLocal(state.lastRemediationTelemetryPath) +
						"\",\"remediationAuditPath\":\"" +
						EscapeJsonLocal(state.lastRemediationAuditPath) +
						"\",\"autoRemediationTenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"autoRemediationTokenRotations\":" +
						std::to_string(state.autoRemediationTokenRotations) +
						",\"remediationSloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\",\"remediationSloMaxDriftDetected\":" +
						std::to_string(state.remediationSloMaxDriftDetected) +
						",\"remediationSloMaxPolicyBlocked\":" +
						std::to_string(state.remediationSloMaxPolicyBlocked) +
						",\"lastComplianceAttestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"complianceAttestationEnabled\":" +
						std::string(!state.lastComplianceAttestationPath.empty() ? "true" : "false") +
						",\"enterpriseSlaPolicyId\":\"" +
						EscapeJsonLocal(state.enterpriseSlaPolicyId) +
						"\",\"crossTenantAttestationAggregationEnabled\":" +
						std::string(state.crossTenantAttestationAggregationEnabled ? "true" : "false") +
						",\"crossTenantAttestationAggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"crossTenantAttestationAggregationCount\":" +
						std::to_string(state.crossTenantAttestationAggregationCount) +
						",\"lastCrossTenantAttestationAggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\"" +
						",\"governanceReportsGenerated\":" +
						std::to_string(state.governanceReportsGenerated) +
						",\"lastGovernanceReportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\",\"policyBlocked\":" +
						std::to_string(state.policyBlockedCount) +
						",\"driftDetected\":" +
						std::to_string(state.driftDetectedCount) +
						",\"lastDriftReason\":\"" +
						EscapeJsonLocal(state.lastDriftReason) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.attestationStatus",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"tenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"sloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\",\"maxDriftDetected\":" +
						std::to_string(state.remediationSloMaxDriftDetected) +
						",\"maxPolicyBlocked\":" +
						std::to_string(state.remediationSloMaxPolicyBlocked) +
						",\"attestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"aggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"aggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\",\"telemetryPath\":\"" +
						EscapeJsonLocal(state.lastRemediationTelemetryPath) +
						"\",\"auditPath\":\"" +
						EscapeJsonLocal(state.lastRemediationAuditPath) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.aggregationStatus",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					return protocol::OkResponse(request, "{\"tenantId\":\"" +
						EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"policyId\":\"" +
						EscapeJsonLocal(state.enterpriseSlaPolicyId) +
						"\",\"aggregationEnabled\":" +
						std::string(state.crossTenantAttestationAggregationEnabled ? "true" : "false") +
						",\"aggregationStatus\":\"" +
						EscapeJsonLocal(state.crossTenantAttestationAggregationStatus) +
						"\",\"aggregationCount\":" +
						std::to_string(state.crossTenantAttestationAggregationCount) +
						",\"aggregationPath\":\"" +
						EscapeJsonLocal(state.lastCrossTenantAttestationAggregationPath) +
						"\",\"attestationPath\":\"" +
						EscapeJsonLocal(state.lastComplianceAttestationPath) +
						"\",\"sloStatus\":\"" +
						EscapeJsonLocal(state.remediationSloStatus) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.remediationPlan",
				[&host](const protocol::RequestFrame& request) {
					(void)request;
					const auto& state = host.m_skillsCatalogState;
					std::string severity = "none";
					std::string recommendedAction = "monitor";
					if (state.driftDetectedCount > 0) {
						severity = "high";
						recommendedAction =
							"review drift report; enforce strict policy and investigate runtime divergence";
					}
					else if (state.policyBlockedCount > 0) {
						severity = "medium";
						recommendedAction =
							"review package allowlist policy and blocked package changes";
					}

					const std::uint64_t nowEpochMs = CurrentEpochMsLocal();
					const std::uint64_t ttlMinutes =
						state.autoRemediationTokenMaxAgeMinutes > 0
						? static_cast<std::uint64_t>(state.autoRemediationTokenMaxAgeMinutes)
						: std::uint64_t{ 60 };
					const std::uint64_t expiresAtEpochMs =
						nowEpochMs + (ttlMinutes * std::uint64_t{ 60000 });

					std::string issuedApprovalToken;
					if (state.autoRemediationRequiresApproval &&
						state.autoRemediationEnabled) {
						issuedApprovalToken =
							"remediation-approval-" + std::to_string(nowEpochMs) +
							"-" + std::to_string(state.driftDetectedCount + state.policyBlockedCount + 1);

						const std::string payload =
							"{\"tenantId\":\"" +
							EscapeJsonLocal(state.autoRemediationTenantId) +
							"\",\"recommendedAction\":\"" +
							EscapeJsonLocal(recommendedAction) +
							"\",\"reportPath\":\"" +
							EscapeJsonLocal(state.lastGovernanceReportPath) +
							"\"}";

						const ApprovalSessionRecord session{
							.token = issuedApprovalToken,
							.type = "governance.remediation",
							.payloadJson = payload,
							.createdAtEpochMs = nowEpochMs,
							.expiresAtEpochMs = expiresAtEpochMs,
						};

						if (!host.m_approvalStore.SaveSession(session)) {
							issuedApprovalToken.clear();
						}

						host.m_approvalStore.PruneExpired(nowEpochMs);
					}

					return protocol::OkResponse(request, "{\"severity\":\"" + EscapeJsonLocal(severity) +
						"\",\"recommendedAction\":\"" +
						EscapeJsonLocal(recommendedAction) +
						"\",\"policyBlocked\":" +
						std::to_string(state.policyBlockedCount) +
						",\"driftDetected\":" +
						std::to_string(state.driftDetectedCount) +
						",\"autoRemediationEnabled\":" +
						std::string(state.autoRemediationEnabled ? "true" : "false") +
						",\"autoRemediationRequiresApproval\":" +
						std::string(state.autoRemediationRequiresApproval ? "true" : "false") +
						",\"approvalToken\":\"" +
						EscapeJsonLocal(issuedApprovalToken) +
						"\",\"approvalTokenExpiresAtEpochMs\":" +
						std::to_string(expiresAtEpochMs) +
						",\"tokenMaxAgeMinutes\":" +
						std::to_string(ttlMinutes) +
						",\"reportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.governance.executeRemediation",
				[&host](const protocol::RequestFrame& request) {
					const auto& state = host.m_skillsCatalogState;
					if (!state.autoRemediationEnabled) {
						return protocol::OkResponse(request, "{\"executed\":false,\"status\":\"disabled\",\"approvalAccepted\":false}");
					}

					bool approvalAccepted = false;
					std::string approvalToken;
					if (request.paramsJson.has_value()) {
						json::FindStringField(
							request.paramsJson.value(),
							"approvalToken",
							approvalToken);
					}
					if (state.autoRemediationRequiresApproval) {
						bool approved = false;
						if (request.paramsJson.has_value()) {
							json::FindBoolField(request.paramsJson.value(), "approved", approved);
						}
						approvalAccepted = approved;
						const bool tokenAccepted = !approvalToken.empty();
						if (!approvalAccepted || !tokenAccepted) {
							return protocol::ErrorResponse(
								request,
								protocol::ErrorShape{
												.code = "approval_required",
												.message = "Auto-remediation execution requires explicit approval and token.",
												.detailsJson = std::nullopt,
												.retryable = false,
												.retryAfterMs = std::nullopt,
								});
						}

						const std::uint64_t nowEpochMs = CurrentEpochMsLocal();
						ApprovalSessionRecord approvalSession;
						if (!host.m_approvalStore.IsTokenValid(
							approvalToken,
							nowEpochMs,
							&approvalSession)) {
							const auto existing = host.m_approvalStore.LoadSession(approvalToken);
							return protocol::ErrorResponse(
								request,
								protocol::ErrorShape{
												.code = existing.has_value()
													? "approval_token_expired"
													: "approval_token_invalid",
												.message = existing.has_value()
													? "Approval token expired. Request a new remediation plan token."
													: "Approval token not found.",
												.detailsJson = std::nullopt,
												.retryable = false,
												.retryAfterMs = std::nullopt,
								});
						}

						if (approvalSession.type != "governance.remediation") {
							return protocol::ErrorResponse(
								request,
								protocol::ErrorShape{
												.code = "approval_token_orphaned",
												.message = "Approval token type mismatch for remediation execution.",
												.detailsJson = std::nullopt,
												.retryable = false,
												.retryAfterMs = std::nullopt,
								});
						}

						std::string tokenTenantId;
						json::FindStringField(
							approvalSession.payloadJson,
							"tenantId",
							tokenTenantId);
						if (!tokenTenantId.empty() &&
							tokenTenantId != state.autoRemediationTenantId) {
							return protocol::ErrorResponse(
								request,
								protocol::ErrorShape{
												.code = "approval_token_orphaned",
												.message = "Approval token tenant mismatch for remediation execution.",
												.detailsJson = std::nullopt,
												.retryable = false,
												.retryAfterMs = std::nullopt,
								});
						}
					}

					std::string action = "monitor";
					if (state.driftDetectedCount > 0) {
						action = "enable_strict_policy";
					}
					else if (state.policyBlockedCount > 0) {
						action = "refresh_allowlist_review";
					}

					if (state.autoRemediationRequiresApproval && !approvalToken.empty()) {
						host.m_approvalStore.RemoveToken(approvalToken);
					}

					return protocol::OkResponse(request, "{\"executed\":true,\"status\":\"applied\",\"approvalAccepted\":" +
						std::string(approvalAccepted ? "true" : "false") +
						",\"tenantId\":\"" + EscapeJsonLocal(state.autoRemediationTenantId) +
						"\",\"playbookPath\":\"" +
						EscapeJsonLocal(state.lastAutoRemediationPlaybookPath) +
						"\",\"tokenMaxAgeMinutes\":" +
						std::to_string(state.autoRemediationTokenMaxAgeMinutes) +
						",\"action\":\"" + EscapeJsonLocal(action) +
						"\",\"reportPath\":\"" +
						EscapeJsonLocal(state.lastGovernanceReportPath) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.embeddings.generate",
				[&host](const protocol::RequestFrame& request) {
					const std::string text =
						ExtractStringParam(request.paramsJson, "text");
					const std::optional<bool> normalize =
						ExtractBoolParam(request.paramsJson, "normalize");
					const std::string model =
						ExtractStringParam(request.paramsJson, "model");
					const std::string traceId =
						request.id.empty() ? "gateway.embeddings.generate" : request.id;

					if (text.empty()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_params",
										.message = "`text` must be a non-empty string.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					if (!host.m_embeddingsGenerateCallback) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "runtime_unavailable",
										.message = "Embeddings runtime callback is unavailable.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto result = host.m_embeddingsGenerateCallback(
						GatewayHost::EmbeddingsGenerateRequest{
							.text = text,
							.normalize = normalize,
							.model = model,
							.traceId = traceId,
						});

					if (!result.ok) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = result.errorCode.empty()
											? "embedding_failed"
											: result.errorCode,
										.message = result.errorMessage.empty()
											? "Embedding generation failed."
											: result.errorMessage,
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					return protocol::OkResponse(request, "{\"vector\":" + SerializeFloatArrayLocal(result.vector) +
						",\"dimension\":" + std::to_string(result.dimension) +
						",\"provider\":\"" + EscapeJsonLocal(result.provider) +
						"\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
						"\",\"latencyMs\":" + std::to_string(result.latencyMs) +
						",\"status\":\"" + EscapeJsonLocal(result.status) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.embeddings.batchGenerate",
				[&host](const protocol::RequestFrame& request) {
					std::string rawTexts;
					std::vector<std::string> texts;
					if (request.paramsJson.has_value() &&
						json::FindRawField(request.paramsJson.value(), "texts", rawTexts)) {
						texts = ParseJsonStringArrayLocal(rawTexts);
					}

					const std::optional<bool> normalize =
						ExtractBoolParam(request.paramsJson, "normalize");
					const std::string model =
						ExtractStringParam(request.paramsJson, "model");
					const std::string traceId =
						request.id.empty() ? "gateway.embeddings.batchGenerate" : request.id;

					if (texts.empty()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_params",
										.message = "`texts` must be a non-empty string array.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					if (texts.size() > 64) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "invalid_params",
										.message = "`texts` exceeds maximum batch size of 64.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					if (!host.m_embeddingsBatchCallback) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "runtime_unavailable",
										.message = "Embeddings runtime callback is unavailable.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto result = host.m_embeddingsBatchCallback(
						GatewayHost::EmbeddingsBatchRequest{
							.texts = texts,
							.normalize = normalize,
							.model = model,
							.traceId = traceId,
						});

					if (!result.ok) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = result.errorCode.empty()
											? "embedding_failed"
											: result.errorCode,
										.message = result.errorMessage.empty()
											? "Embedding batch generation failed."
											: result.errorMessage,
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					return protocol::OkResponse(request, "{\"vectors\":" + SerializeFloatMatrixLocal(result.vectors) +
						",\"count\":" + std::to_string(result.vectors.size()) +
						",\"dimension\":" + std::to_string(result.dimension) +
						",\"provider\":\"" + EscapeJsonLocal(result.provider) +
						"\",\"model\":\"" + EscapeJsonLocal(result.modelId) +
						"\",\"latencyMs\":" + std::to_string(result.latencyMs) +
						",\"status\":\"" + EscapeJsonLocal(result.status) +
						"\"}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.taskDeltas.get",
				[&host](const protocol::RequestFrame& request) {
					const std::string runId =
						ExtractStringParam(request.paramsJson, "runId");
					if (runId.empty()) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = "missing_run_id",
										.message = "runId is required.",
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					const auto storedDeltas = host.m_taskDeltaRepository.Get(runId);
					if (!storedDeltas.has_value()) {
						return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId) +
							"\",\"taskDeltas\":[],\"count\":0}");
					}

					auto orderedTaskDeltas = storedDeltas.value();
					std::sort(
						orderedTaskDeltas.begin(),
						orderedTaskDeltas.end(),
						[](const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& left,
							const GatewayHost::ChatRuntimeResult::TaskDeltaEntry& right) {
								return left.index < right.index;
						});

					orderedTaskDeltas = TaskDeltaLegacyAdapter::AdaptRun(
						runId,
						orderedTaskDeltas.empty() ? std::string("main") : orderedTaskDeltas.front().sessionId,
						orderedTaskDeltas);

					std::string schemaErrorCode;
					std::string schemaErrorMessage;
					if (!TaskDeltaSchemaValidator::ValidateRun(
						runId,
						orderedTaskDeltas,
						schemaErrorCode,
						schemaErrorMessage)) {
						return protocol::ErrorResponse(
							request,
							protocol::ErrorShape{
										.code = schemaErrorCode,
										.message = schemaErrorMessage,
										.detailsJson = std::nullopt,
										.retryable = false,
										.retryAfterMs = std::nullopt,
							});
					}

					std::string deltasJson = "[";
					for (std::size_t i = 0; i < orderedTaskDeltas.size(); ++i) {
						if (i > 0) {
							deltasJson += ",";
						}
						deltasJson += SerializeTaskDeltaEntryJson(orderedTaskDeltas[i]);
					}
					deltasJson += "]";

					return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId) +
						"\",\"taskDeltas\":" + deltasJson +
						",\"count\":" + std::to_string(orderedTaskDeltas.size()) + "}");
				});

			host.m_dispatcher.Register(
				"gateway.runtime.taskDeltas.clear",
				[&host](const protocol::RequestFrame& request) {
					const std::string runId =
						ExtractStringParam(request.paramsJson, "runId");
					std::size_t cleared = 0;
					if (runId.empty()) {
						cleared = host.m_taskDeltaRepository.Size();
						host.m_taskDeltaRepository.ClearAll();
					}
					else {
						cleared = host.m_taskDeltaRepository.Clear(runId) ? 1 : 0;
					}

					return protocol::OkResponse(request, "{\"runId\":\"" + EscapeJsonLocal(runId.empty() ? "*" : runId) +
						"\",\"cleared\":" + std::to_string(cleared) +
						",\"remaining\":" + std::to_string(host.m_taskDeltaRepository.Size()) + "}");
				});

			host.m_dispatcher.Register(
				"chat.history",
				[&host](const protocol::RequestFrame& request) {
					const std::string requestedSessionKey =
						ExtractStringParam(request.paramsJson, "sessionKey");
					const std::string sessionKey =
						requestedSessionKey.empty() ? "main" : requestedSessionKey;
					const std::size_t requestedLimit =
						ExtractSizeParam(request.paramsJson, "limit").value_or(200);
					const auto historyIt = host.m_chatHistoryBySession.find(sessionKey);
					ChatHistoryPolicy historyPolicy;
					ChatHistoryPolicy::BuildParams historyParams;
					historyParams.requestedLimit = requestedLimit;
					if (historyIt != host.m_chatHistoryBySession.end()) {
						historyParams.history = historyIt->second;
					}

					const auto historyResult = historyPolicy.Build(historyParams);
					if (historyResult.placeholderCount > 0) {
						EmitTelemetryEvent(
							"gateway.chat.history.placeholder",
							std::string("{\"sessionKey\":") +
							JsonString(sessionKey) +
							",\"placeholderCount\":" +
							std::to_string(historyResult.placeholderCount) +
							"}");
					}

					return protocol::OkResponse(request, "{\"messages\":" +
						historyResult.messagesJson +
						",\"thinkingLevel\":\"normal\"}");
				});
		}


	} // namespace handlers::runtime

} // namespace blazeclaw::gateway
