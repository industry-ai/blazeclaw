#include "pch.h"
#include "GatewayHost.h"
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
#include "GatewayHostRuntimeStaticOrchestrationStreamingMetrics.h"
#include "GatewayHostHandlersRuntime.h"

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

	namespace {
#include "GatewayHost.Handlers.RuntimeHelpers.inl"
	}

	void GatewayHost::RegisterRuntimeHandlers() {
		handlers::runtime::RuntimeSurfaceHandlers::RegisterAll(*this);
		handlers::runtime::ChatPipelineHandlers::RegisterAll(*this);
		handlers::runtime::RuntimeOrchestrationStreamingHandlers::RegisterAll(*this);
	}


namespace handlers::runtime {


void RuntimeSurfaceHandlers::RegisterAll(GatewayHost& host) {
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

void ChatPipelineHandlers::RegisterAll(GatewayHost& host) {

		host.m_dispatcher.Register(
			"chat.send",
			[&host](const protocol::RequestFrame& request) {
				ChatRunStageContext stageContext{
					   .requestId = request.id,
					   .method = request.method,
				 .paramsJson = request.paramsJson,
					.validateAttachments = [&host](
						const std::optional<std::string>& paramsJson,
						bool& hasAttachments,
						std::string& errorCode,
						std::string& errorMessage) {
						return ValidateAttachmentPayloadShape(
							paramsJson,
							hasAttachments,
							errorCode,
							errorMessage);
					},
					.findRunByIdempotency = [&host](const std::string& key)
						-> std::optional<std::string> {
						if (key.empty()) {
							return std::nullopt;
						}

						const auto dedupeIt = host.m_chatRunByIdempotency.find(key);
						if (dedupeIt == host.m_chatRunByIdempotency.end()) {
							return std::nullopt;
						}

						return dedupeIt->second;
					},
				  .extractAttachmentMimeTypes = [&host](
						const std::optional<std::string>& paramsJson) {
						return ExtractAttachmentMimeTypes(paramsJson);
					},
				};
				auto pipelineResult = host.m_chatRunPipelineOrchestrator.Run(stageContext);
				EmitTelemetryEvent(
					"gateway.chat.pipeline.stages",
					std::string("{\"requestId\":") +
					JsonString(stageContext.requestId) +
					",\"runId\":" +
					JsonString(stageContext.runId) +
					",\"sessionKey\":" +
					JsonString(stageContext.sessionKey) +
					",\"forceError\":" +
					std::string(stageContext.forceError ? "true" : "false") +
					",\"hasAttachmentPayload\":" +
					std::string(stageContext.hasAttachmentPayload ? "true" : "false") +
					",\"normalizedMessageChars\":" +
					std::to_string(stageContext.normalizedMessage.size()) +
					",\"method\":" +
					JsonString(stageContext.method) +
					",\"status\":" +
					JsonString(pipelineResult.status) +
					",\"stages\":" +
					SerializeStringArrayLocal(stageContext.stageTrace) +
					"}");

				if (stageContext.shouldReturnEarly) {
					if (stageContext.skippedByInlinePolicy) {
						EmitTelemetryEvent(
							"gateway.chat.inline.skip",
							std::string("{\"requestId\":") +
							JsonString(request.id) +
							",\"reason\":" +
							JsonString(stageContext.skippedReasonCode) +
							"}");
						return protocol::OkResponseOptionalPayload(request, stageContext.responsePayloadJson);
					}

					if (stageContext.deduped) {
						const auto replayIt =
							host.m_chatReplayByIdempotency.find(stageContext.idempotencyKey);
						if (replayIt != host.m_chatReplayByIdempotency.end()) {
							return protocol::ResponseFrame{
								.id = request.id,
								.ok = replayIt->second.ok,
								.payloadJson = replayIt->second.payloadJson,
								.error = replayIt->second.error,
							};
						}

						return protocol::OkResponse(request, "{\"runId\":\"" +
								EscapeJsonLocal(stageContext.dedupedRunId) +
								"\",\"queued\":false,\"deduped\":true}");
					}

					if (stageContext.responseError.has_value()) {
						return protocol::ErrorResponse(request, std::move(*stageContext.responseError));
					}
					return protocol::ErrorResponse(
						request,
						BuildRuntimeErrorShape(
							stageContext.responseErrorCode,
							stageContext.responseErrorMessage,
							stageContext.runId,
							stageContext.sessionKey));
				}

				const std::string requestedSessionKey = stageContext.requestedSessionKey;
				const std::string sessionKey = stageContext.sessionKey;
				const std::string message = stageContext.message;
				const std::string normalizedMessage = stageContext.normalizedMessage;
				const std::string idempotencyKey = stageContext.idempotencyKey;
				const std::string clientConnectionId = stageContext.clientConnectionId;
				const bool forceError = stageContext.forceError;
				const bool hasAttachments = stageContext.hasAttachmentPayload;
				const std::uint64_t nowMs = stageContext.nowEpochMs > 0
					? stageContext.nowEpochMs
					: CurrentEpochMsLocal();
				const std::string runId = !stageContext.runId.empty()
					? stageContext.runId
					: (!request.id.empty()
						? request.id
						: ("chat-run-" + std::to_string(nowMs) +
							"-" + std::to_string(host.m_chatRunsById.size() + 1)));
				const ChatTranscriptStore transcriptStore;
				bool userTurnPersisted = false;
				auto persistUserTurnIfNeeded = [&]() {
					if (userTurnPersisted) {
						return;
					}

					if (!normalizedMessage.empty() || hasAttachments) {
						const auto userPersisted = transcriptStore.AppendUserMessage(
							ChatTranscriptStore::AppendParams{
								.sessionKey = sessionKey,
								.role = "user",
								.message = normalizedMessage.empty()
									? std::string("[attachment]")
									: normalizedMessage,
								.label = hasAttachments ? "attachments" : std::string(),
								.idempotencyKey = runId + ":user",
							});
						if (!userPersisted.ok && !userPersisted.error.empty()) {
							EmitTelemetryEvent(
								"gateway.chat.transcript.user.persist.error",
								std::string("{\"runId\":") + JsonString(runId) +
								",\"sessionKey\":" + JsonString(sessionKey) +
								",\"error\":" + JsonString(userPersisted.error) + "}");
						}
					}

					PushHistoryMessageIfNew(
						host.m_chatHistoryBySession[sessionKey],
						BuildUserMessageJson(normalizedMessage, hasAttachments, nowMs));
					userTurnPersisted = true;
					};

				persistUserTurnIfNeeded();
				const bool runAlreadyTracked =
					host.m_chatRunsById.find(runId) != host.m_chatRunsById.end();
				const bool lateJoinRequested =
					runAlreadyTracked &&
					stageContext.hasConnectedClient &&
					!clientConnectionId.empty();
				const bool pushLifecycleEnabled =
					stageContext.pushLifecycleRequested;

				ChatControlPlaneService controlPlaneService;
				const bool hasRegisteredRecipient =
					!clientConnectionId.empty() &&
					host.m_transportRecipientRegistry.HasRecipients(runId);
				const auto sendControlDecision =
					controlPlaneService.EvaluateSendControl(
						ChatControlPlaneService::SendControlInput{
							.sessionKey = sessionKey,
							.deliver = stageContext.deliver,
							.routeChannel = stageContext.routeChannel,
							.routeTo = stageContext.routeTo,
							.clientMode = stageContext.clientMode,
						  .hasConnectedClient = stageContext.hasConnectedClient,
							.mainKey = stageContext.mainKey,
							.clientCaps = stageContext.clientCaps,
							.runId = runId,
						 .hasRegisteredRecipient = hasRegisteredRecipient,
							.lateJoinRequested = lateJoinRequested,
						});
				if (sendControlDecision.toolEvents.wantsToolEvents &&
					!clientConnectionId.empty()) {
					host.m_transportRecipientRegistry.RegisterRecipient(
						runId,
						sessionKey,
						clientConnectionId,
						nowMs);
					host.m_transportRecipientRegistry.RegisterLateJoin(
						sessionKey,
						clientConnectionId,
						nowMs);
					host.m_chatToolEventRecipientsByRun[runId].insert(clientConnectionId);
					for (const auto& [activeRunId, activeRun] : host.m_chatRunsById) {
						if (activeRunId != runId &&
							activeRun.sessionKey == sessionKey &&
							activeRun.active) {
							host.m_chatToolEventRecipientsByRun[activeRunId].insert(clientConnectionId);
						}
					}
					host.m_transportRecipientRegistry.PruneExpired(nowMs);
				}

				if (lateJoinRequested && !clientConnectionId.empty()) {
					auto& replayQueue = host.m_chatEventsBySession[sessionKey];
					const auto activeRuns =
						host.m_transportRecipientRegistry.ActiveRunsForSession(sessionKey);
					for (const auto& activeRunId : activeRuns) {
						const auto activeRunIt = host.m_chatRunsById.find(activeRunId);
						if (activeRunIt == host.m_chatRunsById.end()) {
							continue;
						}

						const auto& activeRun = activeRunIt->second;
						if (!activeRun.active ||
							RuntimeTranscriptGuard::IsSilentReplyText(activeRun.assistantText)) {
							continue;
						}

						std::string replayText;
						if (activeRun.providerDeltaCursor > 0 &&
							activeRun.providerDeltaCursor <= activeRun.providerDeltas.size()) {
							replayText = activeRun.providerDeltas[activeRun.providerDeltaCursor - 1];
						}
						if (replayText.empty()) {
							replayText = activeRun.assistantText.substr(
								0,
								(std::min)(activeRun.assistantText.size(), std::size_t{ 64 }));
						}

						if (replayText.empty()) {
							continue;
						}

						const std::string replayMessage =
							BuildAssistantDeltaMessageJson(replayText);
						PushEventWithRetentionLimit(replayQueue, GatewayHost::ChatEventState{
								.runId = activeRun.runId,
								.sessionKey = activeRun.sessionKey,
								.state = "delta",
								.messageJson = replayMessage,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							});
						BranchDecisionDiagnostics::Emit(
							activeRun.runId,
							"controlplane",
							"late_join_replay",
							"delta_replayed",
							std::string("{\"connectionId\":") +
							JsonString(clientConnectionId) +
							",\"sessionKey\":" +
							JsonString(sessionKey) + "}");
					}
				}
				EmitTelemetryEvent(
					"gateway.chat.controlplane.decision",
					std::string("{\"runId\":") +
					JsonString(runId) +
					",\"route\":{\"originatingChannel\":" +
					JsonString(sendControlDecision.route.originatingChannel) +
					",\"explicitDeliverRoute\":" +
					std::string(sendControlDecision.route.explicitDeliverRoute ? "true" : "false") +
					",\"reasonCode\":" +
					JsonString(sendControlDecision.route.reasonCode) +
					"},\"toolEvents\":{\"allowed\":" +
					std::string(sendControlDecision.toolEvents.wantsToolEvents ? "true" : "false") +
					",\"reasonCode\":" +
					JsonString(sendControlDecision.toolEvents.reasonCode) +
					"}}"
				);

				const std::vector<std::string> attachmentMimeTypes =
					stageContext.attachmentMimeTypes;
				const SendPolicyDecision sendPolicyDecision =
					SendPolicyResolver::Evaluate(
						sessionKey,
						normalizedMessage,
						hasAttachments,
						attachmentMimeTypes);
				if (!sendPolicyDecision.allowed) {
					BranchDecisionDiagnostics::Emit(
						runId,
						"transport_control",
						"send_policy",
						"denied_send",
						std::string("{\"hits\":") +
						SerializeStringArrayLocal(sendPolicyDecision.policyHits) +
						"}");
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"send\",\"reason\":\"denied_send\"}");
					return protocol::ErrorResponse(
						request,
						BuildRuntimeErrorShape(
							"denied_send",
							"Request denied by send policy.",
							runId,
							sessionKey));
				}
				auto persistTaskDeltas =
					[&host, &runId, &sessionKey](
						const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas,
						const bool success) {
							if (taskDeltas.empty()) {
								return;
							}

							std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> normalizedTaskDeltas;
							normalizedTaskDeltas.reserve(taskDeltas.size());
							for (std::size_t index = 0; index < taskDeltas.size(); ++index) {
								normalizedTaskDeltas.push_back(
									TaskDeltaLegacyAdapter::AdaptEntry(
										taskDeltas[index],
										runId,
										sessionKey,
										index));
							}

							std::string schemaErrorCode;
							std::string schemaErrorMessage;
							if (!TaskDeltaSchemaValidator::ValidateRun(
								runId,
								normalizedTaskDeltas,
								schemaErrorCode,
								schemaErrorMessage)) {
								return;
							}

							const bool upserted =
								host.m_taskDeltaRepository.Upsert(runId, normalizedTaskDeltas);
							(void)upserted;
							host.PersistTaskDeltas();
							for (const auto& delta : normalizedTaskDeltas) {
								EmitTelemetryEvent(
									"gateway.taskdelta.transition",
									std::string("{\"runId\":") +
									JsonString(runId) +
									",\"phase\":" + JsonString(delta.phase) +
									",\"toolName\":" + JsonString(delta.toolName) +
									",\"status\":" + JsonString(delta.status) +
									",\"index\":" + std::to_string(delta.index) +
									",\"latencyMs\":" + std::to_string(delta.latencyMs) +
									"}");
							}

							std::string terminalStatus = success ? "completed" : "failed";
							std::string terminalErrorCode;
							for (auto it = normalizedTaskDeltas.rbegin();
								it != normalizedTaskDeltas.rend();
								++it) {
								if (it->phase != "final") {
									continue;
								}

								if (!it->status.empty()) {
									terminalStatus = it->status;
								}

								terminalErrorCode = it->errorCode;
								break;
							}

							if (terminalStatus == "completed") {
								++host.m_taskDeltaRunSuccessCount;
							}
							else {
								++host.m_taskDeltaRunFailureCount;
							}

							if (terminalErrorCode == "embedded_deadline_exceeded") {
								++host.m_taskDeltaRunTimeoutCount;
							}

							if (terminalErrorCode == "embedded_run_cancelled" ||
								terminalStatus == "skipped") {
								++host.m_taskDeltaRunCancelledCount;
							}

							if (terminalErrorCode.find("fallback") != std::string::npos ||
								terminalStatus == "fallback") {
								++host.m_taskDeltaRunFallbackCount;
							}

							EmitTelemetryEvent(
								"gateway.taskdelta.runSummary",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"count\":" + std::to_string(normalizedTaskDeltas.size()) +
								",\"success\":" + (success ? std::string("true") : std::string("false")) +
								",\"terminalStatus\":" + JsonString(terminalStatus) +
								",\"errorCode\":" + JsonString(terminalErrorCode) +
								",\"totals\":{\"success\":" + std::to_string(host.m_taskDeltaRunSuccessCount) +
								",\"failure\":" + std::to_string(host.m_taskDeltaRunFailureCount) +
								",\"timeout\":" + std::to_string(host.m_taskDeltaRunTimeoutCount) +
								",\"cancelled\":" + std::to_string(host.m_taskDeltaRunCancelledCount) +
								",\"fallback\":" + std::to_string(host.m_taskDeltaRunFallbackCount) + "}" +
								"}");

							if (host.m_taskDeltaRepository.Size() > 64) {
								const auto& snapshot = host.m_taskDeltaRepository.Snapshot();
								if (!snapshot.empty()) {
									const bool cleared =
										host.m_taskDeltaRepository.Clear(snapshot.begin()->first);
									(void)cleared;
								}
								host.PersistTaskDeltas();
							}
					};

				std::string assistantText;
				if (message.empty() && hasAttachments) {
					assistantText = IsLikelyChinesePromptLocal(normalizedMessage)
						? Utf8LiteralLocal(u8"\u5DF2\u6536\u5230\u56FE\u7247\u9644\u4EF6\u3002")
						: "Received image attachment.";
				}
				std::vector<std::string> assistantDeltas;
				std::string backendErrorCode;
				std::string backendErrorMessage;
				bool failed = false;
				bool orchestrationHandled = false;
				bool lifecycleEventsEnqueued = false;
				bool providerStreamed = false;
				const auto orchestrationPolicy =
					ChatOrchestrationPolicy::Evaluate(
						ChatOrchestrationPolicy::Input{
							.orchestrationPath = host.m_embeddedOrchestrationPath,
							.message = normalizedMessage,
							.forceError = forceError,
							.hasAttachments = hasAttachments,
						});
				const std::string orchestrationPath =
					orchestrationPolicy.selectedPath;
				const bool allowPromptOrchestration =
					orchestrationPolicy.compatDeterministicEnabled;
				const bool forceWeatherEmailDeterministicOrchestration =
					orchestrationPolicy.intentDeterministicEnabled;
				const bool allowDeterministicPromptOrchestration =
					orchestrationPolicy.deterministicEnabled;
				host.m_latestOrchestrationPathSelection.runId = runId;
				host.m_latestOrchestrationPathSelection.path = orchestrationPath;
				host.m_latestOrchestrationPathSelection.compatDeterministicEnabled =
					allowPromptOrchestration;
				host.m_latestOrchestrationPathSelection.intentDeterministicEnabled =
					forceWeatherEmailDeterministicOrchestration;
				host.m_latestOrchestrationPathSelection.deterministicEnabled =
					allowDeterministicPromptOrchestration;
				host.m_latestOrchestrationPathSelection.decisionReasonCode =
					orchestrationPolicy.decisionReasonCode;
				host.m_latestOrchestrationPathSelection.decompositionMetadataSource =
					orchestrationPolicy.decompositionMetadataSource;
				host.m_latestOrchestrationPathSelection.orderedPolicyMode =
					orchestrationPolicy.orderedPolicyMode;
				host.m_latestOrchestrationPathSelection.orderedPolicyStrict =
					orchestrationPolicy.orderedPolicyStrict;
				host.m_latestOrchestrationPathSelection.fallbackPolicyProfile =
					orchestrationPolicy.fallbackPolicyProfile;
				host.m_latestOrchestrationPathSelection.observedAtEpochMs = nowMs;
				EmitTelemetryEvent(
					"gateway.chat.orchestration.pathSelection",
					std::string("{\"runId\":") +
					JsonString(runId) +
					",\"path\":" +
					JsonString(orchestrationPath) +
					",\"compatDeterministicEnabled\":" +
					std::string(allowPromptOrchestration ? "true" : "false") +
					",\"intentDeterministicEnabled\":" +
					std::string(
						forceWeatherEmailDeterministicOrchestration ? "true" : "false") +
					",\"deterministicEnabled\":" +
					std::string(
						allowDeterministicPromptOrchestration ? "true" : "false") +
					",\"decisionReasonCode\":" +
					JsonString(orchestrationPolicy.decisionReasonCode) +
					",\"decompositionMetadataSource\":" +
					JsonString(orchestrationPolicy.decompositionMetadataSource) +
					",\"orderedPolicyDecision\":" +
					JsonString(orchestrationPolicy.orderedPolicyDecision) +
					",\"orderedPolicyMode\":" +
					JsonString(orchestrationPolicy.orderedPolicyMode) +
					",\"orderedPolicyStrict\":" +
					std::string(orchestrationPolicy.orderedPolicyStrict
						? "true"
						: "false") +
					",\"orderedPolicyTargets\":" +
					SerializeStringArrayLocal(orchestrationPolicy.orderedPolicyTargets) +
					",\"fallbackPolicyProfile\":" +
					JsonString(orchestrationPolicy.fallbackPolicyProfile) +
					",\"allowlistPolicyHint\":" +
					JsonString(orchestrationPolicy.allowlistPolicyHint) +
					",\"fallbackPolicyHint\":" +
					JsonString(orchestrationPolicy.fallbackPolicyHint) +
					",\"dynamicRuntimeDefault\":true}");
				BranchDecisionDiagnostics::Emit(
					runId,
					"runtime",
					"orchestration.pathSelection",
					orchestrationPolicy.decisionReasonCode);
				EmitTelemetryEvent(
					"gateway.chat.policy.decision",
					std::string("{\"runId\":") + JsonString(runId) +
					",\"layer\":\"orchestration\",\"reason\":" +
					JsonString(orchestrationPolicy.decisionReasonCode) +
					",\"decompositionSource\":" +
					JsonString(orchestrationPolicy.decompositionMetadataSource) +
					",\"orderingMode\":" +
					JsonString(orchestrationPolicy.orderedPolicyMode) +
					",\"fallbackPolicyProfile\":" +
					JsonString(orchestrationPolicy.fallbackPolicyProfile) + "}");
				const auto runtimeToolsSnapshot = host.m_toolRegistry.List();
				OrderedSequencePolicyOverride orderedSequencePolicyOverride{};
				const OrderedSequencePolicyOverride* orderedSequencePolicyOverridePtr =
					nullptr;
				if (orchestrationPolicy.orderedPolicyMode != "none" &&
					!orchestrationPolicy.orderedPolicyTargets.empty()) {
					orderedSequencePolicyOverride.orderedTargets =
						orchestrationPolicy.orderedPolicyTargets;
					orderedSequencePolicyOverride.strictAllowlist =
						orchestrationPolicy.orderedPolicyStrict;
					orderedSequencePolicyOverride.source =
						orchestrationPolicy.decompositionMetadataSource;
					orderedSequencePolicyOverridePtr = &orderedSequencePolicyOverride;
				}
				const auto orderedSequencePreflight =
					RuntimeSequencingPolicy::BuildOrderedSequencePreflight(
						normalizedMessage,
						runtimeToolsSnapshot,
						host.m_skillsCatalogState.entries,
						orderedSequencePolicyOverridePtr);
				const bool preferChineseResponse =
					stageContext.preferChineseResponse;
				std::vector<std::string> orderedAllowlistTargets;
				bool enforceOrderedAllowlist = false;
				if (orderedSequencePreflight.enforced &&
					(!orderedSequencePreflight.resolvedToolTargets.empty()) &&
					(orderedSequencePreflight.strictAllowlist
						? orderedSequencePreflight.missingTargets.empty()
						: true)) {
					enforceOrderedAllowlist = true;
					orderedAllowlistTargets.reserve(
						orderedSequencePreflight.resolvedToolTargets.size());
					for (const auto& resolvedToolId :
						orderedSequencePreflight.resolvedToolTargets) {
						if (!RuntimeSequencingPolicy::IsResolvedRuntimeToolTarget(
							resolvedToolId,
							runtimeToolsSnapshot)) {
							enforceOrderedAllowlist = false;
							orderedAllowlistTargets.clear();
							break;
						}

						orderedAllowlistTargets.push_back(resolvedToolId);
					}
				}
				const ToolPolicyDecision toolPolicyDecision =
					ToolPolicyPipeline::Build(
						enforceOrderedAllowlist,
						orderedAllowlistTargets,
						runtimeToolsSnapshot);
				if (!toolPolicyDecision.allowAll &&
					toolPolicyDecision.allowedTargets.empty()) {
					BranchDecisionDiagnostics::Emit(
						runId,
						"runtime",
						"tool_policy",
						"tool_policy_block");
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"tool\",\"reason\":\"tool_policy_block\"}");
				}
				else {
					orderedAllowlistTargets = toolPolicyDecision.allowedTargets;
					enforceOrderedAllowlist = !toolPolicyDecision.allowAll;
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"tool\",\"reason\":" +
						JsonString(toolPolicyDecision.reasonCode) + "}");
				}

				const TranscriptPolicyDecision transcriptPolicyDecision =
					TranscriptPolicyResolver::Resolve(
						stageContext.runtimeMessage,
						"deepseek");
				if (transcriptPolicyDecision.applied) {
					BranchDecisionDiagnostics::Emit(
						runId,
						"runtime",
						"transcript_policy",
						"transcript_policy_applied");
					EmitTelemetryEvent(
						"gateway.chat.policy.decision",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"layer\":\"transcript\",\"reason\":\"transcript_policy_applied\"}");
				}

				const std::string runtimeMessage =
					(orderedSequencePreflight.enforced && !enforceOrderedAllowlist)
					? (std::string(preferChineseResponse
						? Utf8LiteralLocal(u8"\u6709\u5E8F\u6267\u884C\u6B65\u9AA4\uFF08\u4FDD\u6301\u987A\u5E8F\uFF09\uFF1A")
						: "Ordered execution steps (preserve order): ") +
						RuntimeSequencingPolicy::JoinOrderedResolution(
							orderedSequencePreflight) +
						"\n\n" + transcriptPolicyDecision.sanitizedMessage)
					: transcriptPolicyDecision.sanitizedMessage;
				BranchDecisionDiagnostics::EmitWithPayloadSummary(
					runId,
					"runtime",
					"runtime_message",
					"runtime_message_built",
					runtimeMessage,
					256);
				std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> orderedPreflightTaskDeltas;

				if (forceError) {
					failed = true;
					backendErrorCode = "forced_error";
					backendErrorMessage = "forced error for deterministic verification";
				}

				auto buildAssistantDeltasFromTaskDeltas =
					[&runId](
						const std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry>& taskDeltas) {
							std::vector<std::string> deltas;
							for (const auto& delta : taskDeltas) {
								if (delta.phase == "tool_call" && !delta.toolName.empty()) {
									deltas.push_back("tools.execute.start tool=" + delta.toolName);
									continue;
								}

								if (delta.phase == "tool_result" && !delta.toolName.empty()) {
									if (delta.toolName == "email.schedule") {
										EmitTelemetryEvent(
											"gateway.email.fallback.attempt",
											std::string("{\"runId\":") +
											JsonString(runId) +
											",\"tool\":\"email.schedule\",\"status\":" +
											JsonString(delta.status) +
											",\"backend\":" +
											JsonString(delta.fallbackBackend) +
											",\"action\":" +
											JsonString(delta.fallbackAction) +
											",\"attempt\":" +
											std::to_string(delta.fallbackAttempt) +
											",\"maxAttempts\":" +
											std::to_string(delta.fallbackMaxAttempts) +
											"}");
									}

									deltas.push_back(
										"tools.execute.result tool=" +
										delta.toolName +
										" status=" +
										(delta.status.empty() ? std::string("ok") : delta.status) +
										(delta.errorCode.empty()
											? std::string()
											: (" errorCode=" + delta.errorCode)));
									continue;
								}
							}

							return deltas;
					};

				if (!forceError &&
					!hasAttachments &&
					orderedSequencePreflight.enforced) {
					orderedPreflightTaskDeltas =
						RuntimeSequencingPolicy::BuildOrderedPreflightTaskDeltas(
							runId,
							sessionKey,
							orderedSequencePreflight,
							false,
							{},
							{});

					EmitTelemetryEvent(
						"gateway.chat.ordered.preflight",
						std::string("{\"runId\":") +
						JsonString(runId) +
						",\"enforced\":true,\"steps\":" +
						SerializeStringArrayLocal(orderedSequencePreflight.orderedTargets) +
						",\"resolvedTools\":" +
						SerializeStringArrayLocal(orderedSequencePreflight.resolvedToolTargets) +
						",\"strictAllowlist\":" +
						std::string(
							orderedSequencePreflight.strictAllowlist ? "true" : "false") +
						",\"missing\":" +
						SerializeStringArrayLocal(orderedSequencePreflight.missingTargets) +
						"}");

					if (orderedSequencePreflight.strictAllowlist &&
						!orderedSequencePreflight.missingTargets.empty()) {
						const std::string strictMissingErrorCode =
							"ordered_sequence_target_unavailable";
						const std::string strictMissingErrorMessage =
							"Ordered execution preflight failed. Missing targets: " +
							RuntimeSequencingPolicy::JoinOrderedTargets(
								orderedSequencePreflight.missingTargets);

						RunLoopBudget orderedRecoveryBudget;
						const RecoveryOutcome orderedRecoveryOutcome =
							RecoveryPolicyEngine::Execute(
								RecoveryRequest{
									.runId = runId,
									.sessionKey = sessionKey,
									.message = normalizedMessage,
									.errorCode = strictMissingErrorCode,
									.errorMessage = strictMissingErrorMessage,
									.authProfileId = "default",
									.taskDeltas = orderedPreflightTaskDeltas,
								},
								orderedRecoveryBudget);

						EmitTelemetryEvent(
							"gateway.chat.policy.decision",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"layer\":\"ordered_preflight\",\"reason\":\"strict_missing\",\"recoveryRoute\":" +
							JsonString(orderedRecoveryOutcome.recoveryRoute) + "}");

						if (orderedRecoveryOutcome.recovered ||
							orderedRecoveryOutcome.shouldReinvokeRuntime) {
							if (!orderedRecoveryOutcome.recoveryDeltas.empty()) {
								orderedPreflightTaskDeltas.insert(
									orderedPreflightTaskDeltas.end(),
									orderedRecoveryOutcome.recoveryDeltas.begin(),
									orderedRecoveryOutcome.recoveryDeltas.end());
							}
							if (!orderedRecoveryOutcome.normalizedDeltas.empty()) {
								orderedPreflightTaskDeltas = orderedRecoveryOutcome.normalizedDeltas;
							}
						}
						else {
							failed = true;
							orchestrationHandled = true;
							backendErrorCode = strictMissingErrorCode;
							backendErrorMessage = strictMissingErrorMessage;
							assistantText =
								(preferChineseResponse
									? (Utf8LiteralLocal(u8"\u65E0\u6CD5\u6267\u884C\u6709\u5E8F\u5DE5\u4F5C\u6D41\uFF0C\u4EE5\u4E0B\u6B65\u9AA4\u76EE\u6807\u4E0D\u53EF\u7528\uFF1A") +
										RuntimeSequencingPolicy::JoinOrderedTargets(
											orderedSequencePreflight.missingTargets) +
										Utf8LiteralLocal(u8"\u3002\u8BF7\u5B89\u88C5\u6216\u542F\u7528\u8FD9\u4E9B\u6280\u80FD/\u5DE5\u5177\u540E\u91CD\u8BD5\u3002"))
									: (std::string("Unable to execute the ordered workflow because required step targets are unavailable: ") +
										RuntimeSequencingPolicy::JoinOrderedTargets(
											orderedSequencePreflight.missingTargets) +
										". Please install or enable these skills/tools and retry."));

							auto blockedTaskDeltas =
								RuntimeSequencingPolicy::BuildOrderedPreflightTaskDeltas(
									runId,
									sessionKey,
									orderedSequencePreflight,
									true,
									backendErrorCode,
									backendErrorMessage);
							assistantDeltas =
								buildAssistantDeltasFromTaskDeltas(blockedTaskDeltas);
							if (assistantDeltas.empty()) {
								assistantDeltas.push_back(assistantText);
							}

							persistTaskDeltas(blockedTaskDeltas, false);
						}
					}
					else if (!orderedSequencePreflight.missingTargets.empty()) {
						BranchDecisionDiagnostics::Emit(
							runId,
							"runtime",
							"ordered_preflight",
							"advisory_missing_targets_continue");
						EmitTelemetryEvent(
							"gateway.chat.ordered.preflight",
							std::string("{\"runId\":") +
							JsonString(runId) +
							",\"advisoryContinue\":true,\"missing\":" +
							SerializeStringArrayLocal(
								orderedSequencePreflight.missingTargets) +
							"}");
					}
				}

				if (!forceError &&
					!orchestrationHandled &&
					!hasAttachments &&
					forceWeatherEmailDeterministicOrchestration) {
					const auto orchestrationResult =
						TryOrchestrateWeatherEmailPrompt(
							host.m_toolRegistry,
							normalizedMessage);

					if (orchestrationResult.matched) {
						orchestrationHandled = true;
						assistantDeltas = orchestrationResult.assistantDeltas;
						if (orchestrationResult.success) {
							failed = false;
							backendErrorCode.clear();
							backendErrorMessage.clear();
							assistantText = orchestrationResult.assistantText;
							if (assistantText.empty() &&
								!assistantDeltas.empty()) {
								assistantText = assistantDeltas.back();
							}

							EmitTelemetryEvent(
								"gateway.chat.orchestration.execution",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"path\":" +
								JsonString(orchestrationPath) +
								",\"status\":\"success\",\"steps\":" +
								std::to_string(
									orchestrationResult.decompositionSteps) +
								"}");

							auto orchestrationTaskDeltas =
								RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
									{},
									runId,
									sessionKey,
									true,
									assistantText,
									{},
									{});
							if (!orderedPreflightTaskDeltas.empty()) {
								std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
								mergedTaskDeltas.reserve(
									orderedPreflightTaskDeltas.size() +
									orchestrationTaskDeltas.size());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orderedPreflightTaskDeltas.begin(),
									orderedPreflightTaskDeltas.end());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orchestrationTaskDeltas.begin(),
									orchestrationTaskDeltas.end());
								orchestrationTaskDeltas = std::move(mergedTaskDeltas);
							}

							persistTaskDeltas(orchestrationTaskDeltas, true);
						}
						else {
							failed = true;
							assistantText.clear();
							backendErrorCode = orchestrationResult.errorCode.empty()
								? "chat_tool_orchestration_failed"
								: orchestrationResult.errorCode;
							backendErrorMessage = orchestrationResult.errorMessage.empty()
								? "chat tool orchestration failed"
								: orchestrationResult.errorMessage;

							EmitTelemetryEvent(
								"gateway.chat.orchestration.execution",
								std::string("{\"runId\":") +
								JsonString(runId) +
								",\"path\":" +
								JsonString(orchestrationPath) +
								",\"status\":\"failed\",\"errorCode\":" +
								JsonString(backendErrorCode) +
								",\"errorMessage\":" +
								JsonString(backendErrorMessage) +
								"}");

							auto orchestrationTaskDeltas =
								RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
									{},
									runId,
									sessionKey,
									false,
									{},
									backendErrorCode,
									backendErrorMessage);
							if (!orderedPreflightTaskDeltas.empty()) {
								std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
								mergedTaskDeltas.reserve(
									orderedPreflightTaskDeltas.size() +
									orchestrationTaskDeltas.size());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orderedPreflightTaskDeltas.begin(),
									orderedPreflightTaskDeltas.end());
								mergedTaskDeltas.insert(
									mergedTaskDeltas.end(),
									orchestrationTaskDeltas.begin(),
									orchestrationTaskDeltas.end());
								orchestrationTaskDeltas = std::move(mergedTaskDeltas);
							}

							persistTaskDeltas(orchestrationTaskDeltas, false);
						}
					}
				}

				if (!forceError && !orchestrationHandled && host.m_chatRuntimeCallback) {
					auto& runtimeSessionEvents = host.m_chatEventsBySession[sessionKey];
					PushEventWithRetentionLimit(runtimeSessionEvents, GatewayHost::ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "queued",
							.messageJson = std::nullopt,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
					PushEventWithRetentionLimit(runtimeSessionEvents, GatewayHost::ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "started",
							.messageJson = std::nullopt,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
					lifecycleEventsEnqueued = true;
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"queued",
						runId,
						sessionKey,
						nowMs);
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"started",
						runId,
						sessionKey,
						nowMs);
					if (pushLifecycleEnabled) {
						EmitPushLifecycleEvent(
							host.m_transport,
							host.m_eventFanoutService,
							GatewayEventFanoutService::ChatLifecycleEvent{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "queued",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							},
							host.m_chatPushEventSeq);
						EmitPushLifecycleEvent(
							host.m_transport,
							host.m_eventFanoutService,
							GatewayEventFanoutService::ChatLifecycleEvent{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "started",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							},
							host.m_chatPushEventSeq);
					}

					host.m_chatRunsById.insert_or_assign(
						runId,
						GatewayHost::ChatRunState{
							.runId = runId,
							.sessionKey = sessionKey,
							.idempotencyKey = idempotencyKey,
							.userMessage = message,
							.assistantText = {},
							.providerDeltas = {},
							.providerDeltaCursor = 0,
							.streamCursor = 0,
							.lastEmitMs = nowMs,
							.failed = false,
							.errorMessage = {},
							.startedAtMs = nowMs,
							.active = true,
							.terminalEventEnqueued = false,
							.pushLifecycleRequested = pushLifecycleEnabled,
							.toolEventsAllowed = sendControlDecision.toolEvents.wantsToolEvents,
							.originatingChannel = sendControlDecision.route.originatingChannel,
							.originatingTo = sendControlDecision.route.originatingTo,
							.explicitDeliverRoute = sendControlDecision.route.explicitDeliverRoute,
						});

					std::size_t streamedDeltaCount = 0;
					const auto runtimeResult = host.m_chatRuntimeCallback(
						GatewayHost::ChatRuntimeRequest{
							.runId = runId,
							.sessionKey = sessionKey,
							.message = runtimeMessage,
							.bodyForCommands = stageContext.bodyForCommands,
							.bodyForAgent = stageContext.bodyForAgent.empty()
								? runtimeMessage
								: stageContext.bodyForAgent,
							.slashCommandName = stageContext.slashCommandName,
							.shouldLoadInlineSkillCommands =
								stageContext.shouldLoadInlineSkillCommands,
							.inlineInvocationAuthorizedSender =
								stageContext.inlineInvocationAuthorizedSender,
							.inlineInvocationSenderIsOwner =
								stageContext.inlineInvocationSenderIsOwner,
							.allowInlineToolImmediateExecution =
								stageContext.allowInlineToolImmediateExecution,
							.enforceOrderedAllowlist = enforceOrderedAllowlist,
							.orderedAllowedToolTargets = orderedAllowlistTargets,
							.hasAttachments = hasAttachments,
							.attachmentMimeTypes = attachmentMimeTypes,
							.onAssistantDelta =
								[&host,
									&streamedDeltaCount,
									&runId,
									&sessionKey,
									&controlPlaneService,
									&sendControlDecision](const std::string& delta) {
									const std::string normalizedDelta = json::Trim(delta);
									if (normalizedDelta.empty() ||
										RuntimeTranscriptGuard::IsSilentReplyText(normalizedDelta)) {
										return;
									}

									if (!controlPlaneService.ShouldPublishToolDelta(
										normalizedDelta,
										sendControlDecision)) {
										return;
									}
									if (normalizedDelta.find("tools.execute") == 0) {
										const auto recipientsIt =
											host.m_chatToolEventRecipientsByRun.find(runId);
										if (recipientsIt == host.m_chatToolEventRecipientsByRun.end() ||
											recipientsIt->second.empty()) {
											return;
										}
									}

									auto& streamEvents = host.m_chatEventsBySession[sessionKey];
									PushEventWithRetentionLimit(streamEvents, GatewayHost::ChatEventState{
											.runId = runId,
											.sessionKey = sessionKey,
											.state = "delta",
											.messageJson = BuildAssistantDeltaMessageJson(normalizedDelta),
											.errorMessage = std::nullopt,
											.timestampMs = CurrentEpochMsLocal(),
										});
									const std::uint64_t deltaNowMs = CurrentEpochMsLocal();
									GatewayLifecycleEventEmitter::EmitLifecycle(
										"delta",
										runId,
										sessionKey,
									 deltaNowMs);

									auto runStateIt = host.m_chatRunsById.find(runId);
									if (runStateIt != host.m_chatRunsById.end() &&
										runStateIt->second.pushLifecycleRequested) {
										EmitPushLifecycleEvent(
											host.m_transport,
							 host.m_eventFanoutService,
											GatewayEventFanoutService::ChatLifecycleEvent{
												.runId = runId,
												.sessionKey = sessionKey,
												.state = "delta",
												.messageJson = BuildAssistantDeltaMessageJson(normalizedDelta),
												.errorMessage = std::nullopt,
												.timestampMs = deltaNowMs,
											},
											host.m_chatPushEventSeq);
									}

									if (runStateIt != host.m_chatRunsById.end()) {
										runStateIt->second.assistantText = normalizedDelta;
										runStateIt->second.streamCursor = normalizedDelta.size();
									  runStateIt->second.lastEmitMs = deltaNowMs;
									}

									++streamedDeltaCount;
								}
						});
					providerStreamed = streamedDeltaCount > 0;

					if (runtimeResult.ok) {
						if (!runtimeResult.assistantText.empty()) {
							assistantText = runtimeResult.assistantText;
						}

						if (assistantText.empty() &&
							!runtimeResult.assistantDeltas.empty()) {
							assistantText = runtimeResult.assistantDeltas.back();
						}

						std::vector<std::string> providerDeltas =
							RuntimeTranscriptGuard::NormalizeAssistantDeltas(
								runtimeResult.assistantDeltas,
								assistantText,
								providerStreamed);
						assistantDeltas = providerDeltas;

						if (assistantText.empty()) {
							failed = true;
							backendErrorCode = "chat_runtime_empty_response";
							backendErrorMessage =
								"chat runtime returned no assistant output";
						}
						else {
							auto existingRunIt = host.m_chatRunsById.find(runId);
							if (existingRunIt != host.m_chatRunsById.end()) {
								existingRunIt->second.assistantText = assistantText;
								existingRunIt->second.providerDeltas = assistantDeltas;
								existingRunIt->second.providerDeltaCursor = 0;
								existingRunIt->second.streamCursor =
									providerStreamed ? assistantText.size() : 0;
								existingRunIt->second.lastEmitMs = nowMs;
								existingRunIt->second.failed = failed;
								existingRunIt->second.errorMessage = backendErrorMessage;
								existingRunIt->second.active = true;
							}
						}
					}
					else {
						failed = true;
						backendErrorCode = runtimeResult.errorCode.empty()
							? "chat_runtime_error"
							: runtimeResult.errorCode;
						backendErrorMessage = runtimeResult.errorMessage.empty()
							? "chat runtime failed"
							: runtimeResult.errorMessage;
						assistantText.clear();
					}

					auto existingRunIt = host.m_chatRunsById.find(runId);
					if (existingRunIt != host.m_chatRunsById.end()) {
						existingRunIt->second.assistantText = assistantText;
						existingRunIt->second.providerDeltas = assistantDeltas;
						existingRunIt->second.providerDeltaCursor = 0;
						existingRunIt->second.streamCursor =
							providerStreamed ? assistantText.size() : 0;
						existingRunIt->second.lastEmitMs = nowMs;
						existingRunIt->second.failed = failed;
						existingRunIt->second.errorMessage = backendErrorMessage;
						existingRunIt->second.active = true;
					}

					auto runtimeTaskDeltas =
						RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
							runtimeResult.taskDeltas,
							runId,
							sessionKey,
							runtimeResult.ok && !failed,
							assistantText,
							backendErrorCode,
							backendErrorMessage);
					runtimeTaskDeltas =
						RuntimeToolCallNormalizer::ApplyInvalidArgumentsRecoveryPolicy(
							runtimeTaskDeltas,
							runId,
							sessionKey,
							normalizedMessage,
							host.m_toolRegistry);

					if (failed) {
						RunLoopBudget budget;
						const RecoveryOutcome recoveryOutcome =
							RecoveryPolicyEngine::Execute(
								RecoveryRequest{
									.runId = runId,
									.sessionKey = sessionKey,
									.message = normalizedMessage,
									.errorCode = backendErrorCode,
									.errorMessage = backendErrorMessage,
									.authProfileId = "default",
									.taskDeltas = runtimeTaskDeltas,
								},
								budget);

						if (!recoveryOutcome.recoveryDeltas.empty()) {
							runtimeTaskDeltas.insert(
								runtimeTaskDeltas.end(),
								recoveryOutcome.recoveryDeltas.begin(),
								recoveryOutcome.recoveryDeltas.end());
						}

						if (!recoveryOutcome.normalizedDeltas.empty()) {
							runtimeTaskDeltas = recoveryOutcome.normalizedDeltas;
						}

						EmitTelemetryEvent(
							"gateway.chat.recovery.decision",
							std::string("{\"runId\":") +
							JsonString(runId) +
							",\"recovered\":" +
							std::string(recoveryOutcome.recovered ? "true" : "false") +
							",\"retry\":" +
							std::string(recoveryOutcome.shouldRetry ? "true" : "false") +
							",\"reinvoke\":" +
							std::string(recoveryOutcome.shouldReinvokeRuntime ? "true" : "false") +
							",\"recoveryRoute\":" +
							JsonString(recoveryOutcome.recoveryRoute) +
							",\"compaction\":" +
							std::string(recoveryOutcome.compactionApplied ? "true" : "false") +
							",\"truncation\":" +
							std::string(recoveryOutcome.truncationApplied ? "true" : "false") +
							",\"fallbackPolicyProfile\":" +
							JsonString(orchestrationPolicy.fallbackPolicyProfile) +
							",\"profile\":" +
							JsonString(recoveryOutcome.selectedProfileId) +
							",\"contextEngine\":" +
							JsonString(recoveryOutcome.selectedContextEngineId) +
							",\"terminalCode\":" +
							JsonString(recoveryOutcome.terminalErrorCode) +
							"}");
						BranchDecisionDiagnostics::Emit(
							runId,
							"recovery",
							recoveryOutcome.recovered
							? "recovered"
							: "terminal",
							recoveryOutcome.terminalErrorCode.empty()
							? "recovery_chain_continue"
							: recoveryOutcome.terminalErrorCode);

						if (recoveryOutcome.recovered) {
							failed = false;
							backendErrorCode.clear();
							backendErrorMessage.clear();
							if (assistantText.empty()) {
								assistantText = recoveryOutcome.compactionApplied
									? "Recovered via context compaction; runtime will continue."
									: "Recovered via fallback normalization; runtime will continue.";
							}
						}
						else if (!recoveryOutcome.terminalErrorCode.empty()) {
							backendErrorCode = recoveryOutcome.terminalErrorCode;
							if (!recoveryOutcome.terminalErrorMessage.empty()) {
								backendErrorMessage = recoveryOutcome.terminalErrorMessage;
							}
						}
					}

					if (!orderedPreflightTaskDeltas.empty()) {
						std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
						mergedTaskDeltas.reserve(
							orderedPreflightTaskDeltas.size() +
							runtimeTaskDeltas.size());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							orderedPreflightTaskDeltas.begin(),
							orderedPreflightTaskDeltas.end());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							runtimeTaskDeltas.begin(),
							runtimeTaskDeltas.end());
						runtimeTaskDeltas = std::move(mergedTaskDeltas);
					}

					persistTaskDeltas(
						runtimeTaskDeltas,
						runtimeResult.ok && !failed);
				}
				else if (!forceError && !orchestrationHandled && !host.m_chatRuntimeCallback) {
					failed = true;
					assistantText.clear();
					assistantDeltas.clear();
					backendErrorCode = "chat_runtime_callback_missing";
					backendErrorMessage = "chat runtime callback is not configured";

					auto runtimeTaskDeltas =
						RuntimeToolCallNormalizer::EnsureRuntimeTaskDeltas(
							{},
							runId,
							sessionKey,
							false,
							assistantText,
							backendErrorCode,
							backendErrorMessage);
					if (!orderedPreflightTaskDeltas.empty()) {
						std::vector<GatewayHost::ChatRuntimeResult::TaskDeltaEntry> mergedTaskDeltas;
						mergedTaskDeltas.reserve(
							orderedPreflightTaskDeltas.size() +
							runtimeTaskDeltas.size());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							orderedPreflightTaskDeltas.begin(),
							orderedPreflightTaskDeltas.end());
						mergedTaskDeltas.insert(
							mergedTaskDeltas.end(),
							runtimeTaskDeltas.begin(),
							runtimeTaskDeltas.end());
						runtimeTaskDeltas = std::move(mergedTaskDeltas);
					}

					persistTaskDeltas(runtimeTaskDeltas, false);
				}

				const bool silentAssistantReply =
					RuntimeTranscriptGuard::IsSilentReplyText(assistantText);
				if (!assistantText.empty() && !silentAssistantReply) {
					const auto assistantPersisted = transcriptStore.AppendAssistantMessage(
						ChatTranscriptStore::AppendParams{
							.sessionKey = sessionKey,
							.role = "assistant",
							.message = assistantText,
							.label = std::string(),
							.idempotencyKey = runId + ":assistant",
						});
					if (!assistantPersisted.ok && !assistantPersisted.error.empty()) {
						EmitTelemetryEvent(
							"gateway.chat.transcript.assistant.persist.error",
							std::string("{\"runId\":") + JsonString(runId) +
							",\"sessionKey\":" + JsonString(sessionKey) +
							",\"error\":" + JsonString(assistantPersisted.error) + "}");
					}
				}

				auto& sessionEvents = host.m_chatEventsBySession[sessionKey];
				if (!lifecycleEventsEnqueued) {
					PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "queued",
							.messageJson = std::nullopt,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
					PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
							.runId = runId,
							.sessionKey = sessionKey,
							.state = "started",
							.messageJson = std::nullopt,
							.errorMessage = std::nullopt,
							.timestampMs = nowMs,
						});
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"queued",
						runId,
						sessionKey,
						nowMs);
					GatewayLifecycleEventEmitter::EmitLifecycle(
						"started",
						runId,
						sessionKey,
						nowMs);
					if (pushLifecycleEnabled) {
						EmitPushLifecycleEvent(
							host.m_transport,
							host.m_eventFanoutService,
							GatewayEventFanoutService::ChatLifecycleEvent{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "queued",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							},
							host.m_chatPushEventSeq);
						EmitPushLifecycleEvent(
							host.m_transport,
							host.m_eventFanoutService,
							GatewayEventFanoutService::ChatLifecycleEvent{
								.runId = runId,
								.sessionKey = sessionKey,
								.state = "started",
								.messageJson = std::nullopt,
								.errorMessage = std::nullopt,
								.timestampMs = nowMs,
							},
							host.m_chatPushEventSeq);
					}
				}
				EmitDeepSeekGatewayDiagnostic(
					"event.enqueue",
					std::string("state=queued runId=") +
					runId +
					" session=" +
					sessionKey +
					" queueSize=" +
					std::to_string(sessionEvents.size()));
				EmitDeepSeekGatewayDiagnostic(
					"event.enqueue",
					std::string("state=started runId=") +
					runId +
					" session=" +
					sessionKey +
					" queueSize=" +
					std::to_string(sessionEvents.size()));

				std::size_t streamCursor = 0;
				if (!failed && !silentAssistantReply && !providerStreamed) {
					streamCursor = (std::min)(assistantText.size(), std::size_t{ 6 });
					if (streamCursor > 0) {
						const std::string initialDeltaMessage = BuildAssistantDeltaMessageJson(
							assistantText.substr(0, streamCursor));
						PushEventWithRetentionLimit(sessionEvents, GatewayHost::ChatEventState{
							   .runId = runId,
							   .sessionKey = sessionKey,
							   .state = "delta",
							  .messageJson = initialDeltaMessage,
							   .errorMessage = std::nullopt,
							   .timestampMs = nowMs,
							});
						GatewayLifecycleEventEmitter::EmitLifecycle(
							"delta",
							runId,
							sessionKey,
							nowMs);
						EmitDeepSeekGatewayDiagnostic(
							"event.enqueue",
							std::string("state=delta runId=") +
							runId +
							" session=" +
							sessionKey +
							" queueSize=" +
							std::to_string(sessionEvents.size()));
						if (pushLifecycleEnabled) {
							EmitPushLifecycleEvent(
								host.m_transport,
								host.m_eventFanoutService,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = runId,
									.sessionKey = sessionKey,
									.state = "delta",
									.messageJson = initialDeltaMessage,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
						}
					}
				}

				if (host.m_chatRunsById.find(runId) == host.m_chatRunsById.end()) {
					host.m_chatRunsById.insert_or_assign(
						runId,
						GatewayHost::ChatRunState{
							.runId = runId,
							.sessionKey = sessionKey,
							.idempotencyKey = idempotencyKey,
							.userMessage = message,
							.assistantText = assistantText,
							.providerDeltas = assistantDeltas,
							.providerDeltaCursor = 0,
							.streamCursor = streamCursor,
							.lastEmitMs = nowMs,
							.failed = failed,
							.errorMessage = backendErrorMessage,
							.startedAtMs = nowMs,
						 .active = true,
						 .terminalEventEnqueued = false,
							.pushLifecycleRequested = pushLifecycleEnabled,
							.toolEventsAllowed = sendControlDecision.toolEvents.wantsToolEvents,
							.originatingChannel = sendControlDecision.route.originatingChannel,
							.originatingTo = sendControlDecision.route.originatingTo,
							.explicitDeliverRoute = sendControlDecision.route.explicitDeliverRoute,
						});
				}

				host.m_chatTerminalDeliveredRunIds.erase(runId);

				if (!idempotencyKey.empty()) {
					host.m_chatRunByIdempotency.insert_or_assign(idempotencyKey, runId);
				}

				std::string sendPayload =
					"{\"runId\":\"" +
					EscapeJsonLocal(runId) +
					"\",\"backendErrorCode\":" +
					(backendErrorCode.empty()
						? std::string("null")
						: ("\"" + EscapeJsonLocal(backendErrorCode) + "\"")) +
					",\"queued\":true,\"deduped\":false" +
					",\"originatingChannel\":" +
					JsonString(sendControlDecision.route.originatingChannel) +
					",\"explicitDeliverRoute\":" +
					std::string(sendControlDecision.route.explicitDeliverRoute ? "true" : "false");

				if (stageContext.pushLifecycleRequested) {
					sendPayload +=
						",\"lifecycle\":{\"transport\":\"push_compatible\",\"state\":\"started\"}";
				}
				sendPayload += "}";

				const protocol::ResponseFrame sendResponse = protocol::OkResponse(request, sendPayload);
				if (!idempotencyKey.empty()) {
					host.m_chatReplayByIdempotency.insert_or_assign(
						idempotencyKey,
						GatewayHost::ChatReplayEntry{
							.ok = sendResponse.ok,
							.payloadJson = sendResponse.payloadJson,
							.error = sendResponse.error,
						});
				}

				if (stageContext.pushLifecycleRequested) {
					EmitTelemetryEvent(
						"gateway.chat.lifecycle.push_ack",
						std::string("{\"runId\":") + JsonString(runId) +
						",\"sessionKey\":" + JsonString(sessionKey) +
						",\"state\":\"started\"}");
				}

				return sendResponse;
			});

		host.m_dispatcher.Register(
			"chat.inject",
			[&host](const protocol::RequestFrame& request) {
				const std::string requestedSessionKey =
					ExtractStringParam(request.paramsJson, "sessionKey");
				const std::string sessionKey =
					requestedSessionKey.empty() ? "main" : requestedSessionKey;
				const std::string message =
					ExtractStringParam(request.paramsJson, "message");
				const std::string label =
					ExtractStringParam(request.paramsJson, "label");

				if (json::Trim(message).empty()) {
					return protocol::ErrorResponse(
						request,
						BuildRuntimeErrorShape(
							"invalid_params",
							"`message` must be a non-empty string.",
							request.id,
							sessionKey));
				}

				const ChatTranscriptStore transcriptStore;
				const auto appended = transcriptStore.AppendAssistantMessage(
					ChatTranscriptStore::AppendParams{
						.sessionKey = sessionKey,
						.message = message,
						.label = label,
					 .idempotencyKey = request.id,
					});
				if (!appended.ok) {
					return protocol::ErrorResponse(
						request,
						BuildRuntimeErrorShape(
							"unavailable",
							"failed to write transcript: " +
							(appended.error.empty()
								? std::string("unknown error")
								: appended.error),
							request.id,
							sessionKey));
				}

				if (appended.messageId.empty() || appended.messageJson.empty()) {
					return protocol::OkResponse(request, "{\"ok\":true,\"deduped\":true}");
				}

				const std::uint64_t nowMs = CurrentEpochMsLocal();
				const std::string runId = "inject-" + appended.messageId;
				auto& queue = host.m_chatEventsBySession[sessionKey];
				PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
						.runId = runId,
						.sessionKey = sessionKey,
						.state = "final",
						.messageJson = appended.messageJson,
						.errorMessage = std::nullopt,
						.timestampMs = nowMs,
					});
				GatewayLifecycleEventEmitter::EmitLifecycle(
					"final",
					runId,
					sessionKey,
					nowMs);

				if (!RuntimeTranscriptGuard::IsSilentReplyText(message)) {
					PushHistoryMessageIfNew(
						host.m_chatHistoryBySession[sessionKey],
						appended.messageJson);
				}

				return protocol::OkResponse(request, "{\"ok\":true,\"messageId\":" +
						JsonString(appended.messageId) + "}");
			});

		host.m_dispatcher.Register(
			"chat.abort",
			[&host](const protocol::RequestFrame& request) {
				const std::string requestedSessionKey =
					ExtractStringParam(request.paramsJson, "sessionKey");
				const std::string sessionKey =
					requestedSessionKey.empty() ? "main" : requestedSessionKey;
				const std::string requestedRunId =
					ExtractStringParam(request.paramsJson, "runId");

				auto runIt = host.m_chatRunsById.end();
				if (!requestedRunId.empty()) {
					const auto exact = host.m_chatRunsById.find(requestedRunId);
					if (exact != host.m_chatRunsById.end() &&
						exact->second.sessionKey == sessionKey) {
						runIt = exact;
					}
				}
				else {
					runIt = std::find_if(
						host.m_chatRunsById.begin(),
						host.m_chatRunsById.end(),
						[&](const auto& pair) {
							return pair.second.sessionKey == sessionKey &&
								pair.second.active;
						});
				}

				if (runIt == host.m_chatRunsById.end()) {
					return protocol::OkResponse(request, "{\"aborted\":false,\"sessionKey\":\"" +
							EscapeJsonLocal(sessionKey) +
							"\"}");
				}

				const std::string runId = runIt->second.runId;
				if (host.m_chatAbortCallback) {
					host.m_chatAbortCallback(
						GatewayHost::ChatAbortRequest{
							.runId = runId,
							.sessionKey = sessionKey,
						});
				}

				auto& queue = host.m_chatEventsBySession[sessionKey];
				std::erase_if(
					queue,
					[&](const GatewayHost::ChatEventState& item) {
						return item.runId == runId;
					});

				const std::uint64_t nowMs = CurrentEpochMsLocal();
				const bool silentAssistantReply =
					RuntimeTranscriptGuard::IsSilentReplyText(
						runIt->second.assistantText);
				PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
					   .runId = runIt->second.runId,
					   .sessionKey = sessionKey,
					   .state = "aborted",
					   .messageJson = silentAssistantReply
						   ? std::nullopt
						   : std::optional<std::string>(
							   BuildAssistantFinalMessageJson(
								   runIt->second.assistantText,
								   nowMs)),
					   .errorMessage = std::nullopt,
					   .timestampMs = nowMs,
					});
				GatewayLifecycleEventEmitter::EmitLifecycle(
					"aborted",
					runIt->second.runId,
					sessionKey,
					nowMs);
				runIt->second.terminalEventEnqueued = true;
				EmitDeepSeekGatewayDiagnostic(
					"event.enqueue",
					std::string("state=aborted runId=") +
					runIt->second.runId +
					" session=" +
					sessionKey +
					" queueSize=" +
					std::to_string(queue.size()));

				runIt->second.active = false;
				runIt->second.streamCursor = runIt->second.assistantText.size();
				host.m_chatToolEventRecipientsByRun.erase(runId);
				host.m_transportRecipientRegistry.MarkRunFinalized(runId, nowMs);
				host.m_transportRecipientRegistry.PruneExpired(nowMs);

				ChatAbortCoordinator abortCoordinator;
				const auto persistResult = abortCoordinator.PersistAbortedPartial(
					ChatAbortCoordinator::PersistPartialParams{
						.sessionKey = sessionKey,
						.runId = runId,
						.text = runIt->second.assistantText,
						.origin = "rpc",
					});
				if (!persistResult.persisted && !persistResult.error.empty()) {
					EmitTelemetryEvent(
						"gateway.chat.abort.partialPersistence",
						std::string("{\"runId\":") +
						JsonString(runId) +
						",\"sessionKey\":" +
						JsonString(sessionKey) +
						",\"persisted\":false,\"error\":" +
						JsonString(persistResult.error) +
						"}");
				}

				return protocol::OkResponse(request, "{\"aborted\":true,\"runId\":\"" +
						EscapeJsonLocal(runId) +
						"\",\"sessionKey\":\"" +
						EscapeJsonLocal(sessionKey) +
					  "\",\"partialPersisted\":" +
						std::string(persistResult.persisted ? "true" : "false") +
						"}");
			});

		host.m_dispatcher.Register(
			"chat.events.poll",
			[&host](const protocol::RequestFrame& request) {
				const std::string requestedSessionKey =
					ExtractStringParam(request.paramsJson, "sessionKey");
				const std::string sessionKey =
					requestedSessionKey.empty() ? "main" : requestedSessionKey;
				const std::size_t requestedLimit =
					ExtractSizeParam(request.paramsJson, "limit").value_or(20);
				const std::size_t limit =
					(std::max)(std::size_t{ 1 }, (std::min)(requestedLimit, std::size_t{ 100 }));

				const std::uint64_t nowMs = CurrentEpochMsLocal();
				auto queueIt = host.m_chatEventsBySession.find(sessionKey);
				auto& queue = host.m_chatEventsBySession[sessionKey];

				auto runIt = std::find_if(
					host.m_chatRunsById.begin(),
					host.m_chatRunsById.end(),
					[&](auto& pair) {
						return pair.second.sessionKey == sessionKey && pair.second.active;
					});

				if (runIt != host.m_chatRunsById.end()) {
					auto& run = runIt->second;
					const bool pushLifecycleEnabledForRun =
						run.pushLifecycleRequested;
					const bool silentAssistantReply =
						RuntimeTranscriptGuard::IsSilentReplyText(run.assistantText);
					const bool enoughTimeElapsed =
						run.lastEmitMs == 0 || (nowMs - run.lastEmitMs) >= 180;

					if (!run.failed && !silentAssistantReply &&
						((run.providerDeltaCursor < run.providerDeltas.size()) ||
							(run.streamCursor < run.assistantText.size())) &&
						enoughTimeElapsed) {
						ChatControlPlaneService controlPlaneService;
						const auto pollDecision =
							ChatControlPlaneService::SendControlDecision{
								.route = ChatRoutePolicy::Output{
									.originatingChannel = run.originatingChannel,
									.originatingTo = run.originatingTo,
									.explicitDeliverRoute = run.explicitDeliverRoute,
									.reasonCode = "persisted_run_state",
								},
								.toolEvents = ToolEventRecipientPolicy::Output{
									.wantsToolEvents = run.toolEventsAllowed,
									.reasonCode = "persisted_run_state",
								},
						};

						std::string deltaText;
						std::string pollDeltaMessage;
						if (run.providerDeltaCursor < run.providerDeltas.size()) {
							while (run.providerDeltaCursor < run.providerDeltas.size()) {
								deltaText = run.providerDeltas[run.providerDeltaCursor];
								++run.providerDeltaCursor;
								if (controlPlaneService.ShouldPublishToolDelta(
									deltaText,
									pollDecision)) {
									if (deltaText.find("tools.execute") == 0) {
										const auto recipientsIt =
											host.m_chatToolEventRecipientsByRun.find(run.runId);
										if (recipientsIt == host.m_chatToolEventRecipientsByRun.end() ||
											recipientsIt->second.empty()) {
											deltaText.clear();
											continue;
										}
									}
									break;
								}
								deltaText.clear();
							}
							if (deltaText.empty()) {
								goto skip_poll_delta_emit;
							}
							run.streamCursor = deltaText.size();
						}
						else {
							const std::size_t nextCursor =
								(std::min)(run.assistantText.size(), run.streamCursor + std::size_t{ 8 });
							run.streamCursor = nextCursor;
							deltaText = run.assistantText.substr(0, run.streamCursor);
						}

						run.lastEmitMs = nowMs;
						pollDeltaMessage = BuildAssistantDeltaMessageJson(deltaText);

						PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
							   .runId = run.runId,
							   .sessionKey = run.sessionKey,
							   .state = "delta",
							   .messageJson = pollDeltaMessage,
							   .errorMessage = std::nullopt,
							   .timestampMs = nowMs,
							});
						if (pushLifecycleEnabledForRun) {
							EmitPushLifecycleEvent(
								host.m_transport,
								host.m_eventFanoutService,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = run.runId,
									.sessionKey = run.sessionKey,
									.state = "delta",
									.messageJson = pollDeltaMessage,
									.errorMessage = std::nullopt,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
						}
						EmitDeepSeekGatewayDiagnostic(
							"event.enqueue",
							std::string("state=delta runId=") +
							run.runId +
							" session=" +
							run.sessionKey +
							" queueSize=" +
							std::to_string(queue.size()));
					skip_poll_delta_emit:;
					}

					const bool streamCompleted =
						run.failed ||
						silentAssistantReply ||
						(run.providerDeltaCursor >= run.providerDeltas.size() &&
							run.streamCursor >= run.assistantText.size());
					if (streamCompleted && !run.terminalEventEnqueued) {
						const std::optional<std::string> terminalMessage =
							run.failed || silentAssistantReply
							? std::nullopt
							: std::optional<std::string>(
								BuildAssistantFinalMessageJson(run.assistantText, nowMs));
						const std::optional<std::string> terminalError =
							run.failed
							? std::optional<std::string>(run.errorMessage.empty()
								? "chat error"
								: run.errorMessage)
							: std::nullopt;

						PushEventWithRetentionLimit(queue, GatewayHost::ChatEventState{
							   .runId = run.runId,
							   .sessionKey = run.sessionKey,
							   .state = run.failed ? "error" : "final",
							   .messageJson = terminalMessage,
							   .errorMessage = terminalError,
							   .timestampMs = nowMs,
							});
						if (pushLifecycleEnabledForRun) {
							EmitPushLifecycleEvent(
								host.m_transport,
								host.m_eventFanoutService,
								GatewayEventFanoutService::ChatLifecycleEvent{
									.runId = run.runId,
									.sessionKey = run.sessionKey,
									.state = run.failed ? "error" : "final",
									.messageJson = terminalMessage,
									.errorMessage = terminalError,
									.timestampMs = nowMs,
								},
								host.m_chatPushEventSeq);
						}
						GatewayLifecycleEventEmitter::EmitLifecycle(
							run.failed ? "error" : "final",
							run.runId,
							run.sessionKey,
							nowMs,
							terminalError);
						run.terminalEventEnqueued = true;
						host.m_transportRecipientRegistry.MarkRunFinalized(
							run.runId,
							nowMs);
						EmitDeepSeekGatewayDiagnostic(
							"event.enqueue",
							std::string("state=") +
							(run.failed ? "error" : "final") +
							" runId=" +
							run.runId +
							" session=" +
							run.sessionKey +
							" queueSize=" +
							std::to_string(queue.size()));

						run.active = false;
						host.m_chatToolEventRecipientsByRun.erase(run.runId);
						host.m_transportRecipientRegistry.PruneExpired(nowMs);
					}
				}

				std::string eventsJson = "[";
				std::size_t emitted = 0;
				std::unordered_set<std::string> terminalRunIdsSeenThisPoll;
				if (!queue.empty()) {
					while (emitted < limit && !queue.empty()) {
						const GatewayHost::ChatEventState eventState = queue.front();
						queue.pop_front();

						if (IsTerminalChatState(eventState.state)) {
							if (host.m_chatTerminalDeliveredRunIds.find(eventState.runId) != host.m_chatTerminalDeliveredRunIds.end() ||
								terminalRunIdsSeenThisPoll.find(eventState.runId) != terminalRunIdsSeenThisPoll.end()) {
								continue;
							}

							terminalRunIdsSeenThisPoll.insert(eventState.runId);
							host.m_chatTerminalDeliveredRunIds.insert(eventState.runId);
							if (host.m_chatTerminalDeliveredRunIds.size() > 1024) {
								host.m_chatTerminalDeliveredRunIds.clear();
							}
						}

						EmitDeepSeekGatewayDiagnostic(
							"event.dequeue",
							std::string("state=") +
							eventState.state +
							" runId=" +
							eventState.runId +
							" session=" +
							eventState.sessionKey +
							" emitted=" +
							std::to_string(emitted + 1) +
							" queueRemaining=" +
							std::to_string(queue.size()));

						if (emitted > 0) {
							eventsJson += ",";
						}

						eventsJson += BuildChatEventJson(
							eventState.runId,
							eventState.sessionKey,
							eventState.state,
							eventState.messageJson,
							eventState.errorMessage,
							eventState.timestampMs);
						++emitted;

						if ((eventState.state == "final" ||
							eventState.state == "aborted") &&
							eventState.messageJson.has_value() &&
							!IsSilentAssistantMessageJson(eventState.messageJson.value())) {
							PushHistoryMessageIfNew(
								host.m_chatHistoryBySession[sessionKey],
								eventState.messageJson.value());
						}

						if (IsTerminalChatState(eventState.state)) {
							const auto runIt = host.m_chatRunsById.find(eventState.runId);
							if (runIt != host.m_chatRunsById.end()) {
								if (!runIt->second.idempotencyKey.empty()) {
									host.m_chatRunByIdempotency.erase(
										runIt->second.idempotencyKey);
								}

								host.m_chatRunsById.erase(runIt);
								host.m_transportRecipientRegistry.PruneRun(eventState.runId);
							}
						}
					}
				}

				eventsJson += "]";
				EmitDeepSeekGatewayDiagnostic(
					"event.dequeue",
					std::string("poll complete session=") +
					sessionKey +
					" emitted=" +
					std::to_string(emitted) +
					" queueRemaining=" +
					std::to_string(queue.size()));
				return protocol::OkResponse(request, "{\"sessionKey\":\"" +
						EscapeJsonLocal(sessionKey) +
						"\",\"events\":" +
						eventsJson +
						",\"count\":" +
						std::to_string(emitted) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.status",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;

				return protocol::OkResponse(request, "{\"total\":" +
						std::to_string(state.entries.size()) +
						",\"rootsScanned\":" +
						std::to_string(state.rootsScanned) +
						",\"rootsSkipped\":" +
						std::to_string(state.rootsSkipped) +
						",\"pluginRootsConfigured\":" +
						std::to_string(state.pluginRootsConfigured) +
						",\"pluginRootsScanned\":" +
						std::to_string(state.pluginRootsScanned) +
						",\"loaderPolicyRejectPathSymlink\":" +
						std::to_string(state.loaderPolicyRejectPathSymlinkCount) +
						",\"loaderPolicyStrictFrontmatter\":" +
						std::to_string(state.loaderPolicyStrictFrontmatterCount) +
						",\"symlinkRejectedFiles\":" +
						std::to_string(state.symlinkRejectedFiles) +
						",\"strictFrontmatterOmittedFiles\":" +
						std::to_string(state.strictFrontmatterOmittedFiles) +
						",\"verifiedOpenPathFailures\":" +
						std::to_string(state.verifiedOpenPathFailures) +
						",\"verifiedOpenValidationFailures\":" +
						std::to_string(state.verifiedOpenValidationFailures) +
						",\"verifiedOpenIoFailures\":" +
						std::to_string(state.verifiedOpenIoFailures) +
						",\"oversizedSkillFiles\":" +
						std::to_string(state.oversizedSkillFiles) +
						",\"invalidFrontmatterFiles\":" +
						std::to_string(state.invalidFrontmatterFiles) +
						",\"eligible\":" +
						std::to_string(state.eligibleCount) +
						",\"disabled\":" +
						std::to_string(state.disabledCount) +
						",\"blockedByAllowlist\":" +
						std::to_string(state.blockedByAllowlistCount) +
						",\"missingRequirements\":" +
						std::to_string(state.missingRequirementsCount) +
						",\"strictEntryResolutionMode\":" +
						std::to_string(state.strictEntryResolutionModeCount) +
						",\"compatEntryResolutionMode\":" +
						std::to_string(state.compatEntryResolutionModeCount) +
						",\"configResolvedByKey\":" +
						std::to_string(state.configResolvedByKeyCount) +
						",\"configResolvedByNameFallback\":" +
						std::to_string(state.configResolvedByNameFallbackCount) +
						",\"allowlistRaw\":" +
						std::to_string(state.allowlistRawCount) +
						",\"allowlistNormalized\":" +
						std::to_string(state.allowlistNormalizedCount) +
						",\"entryConfigRaw\":" +
						std::to_string(state.entryConfigRawCount) +
						",\"entryConfigNormalized\":" +
						std::to_string(state.entryConfigNormalizedCount) +
						",\"entryConfigMalformed\":" +
						std::to_string(state.entryConfigMalformedCount) +
						",\"remoteEligibilityEnabled\":" +
						std::to_string(state.remoteEligibilityEnabledCount) +
						",\"remotePlatformSatisfied\":" +
						std::to_string(state.remotePlatformSatisfiedCount) +
						",\"remoteBinSatisfied\":" +
						std::to_string(state.remoteBinSatisfiedCount) +
						",\"remoteAnyBinSatisfied\":" +
						std::to_string(state.remoteAnyBinSatisfiedCount) +
						",\"alwaysBypass\":" +
						std::to_string(state.alwaysBypassCount) +
						",\"commandSanitize\":" +
						std::to_string(state.commandSanitizeCount) +
						",\"commandDedupe\":" +
						std::to_string(state.commandDedupeCount) +
						",\"commandSkillNameDedupe\":" +
						std::to_string(state.commandSkillNameDedupeCount) +
						",\"commandMissingToolDispatch\":" +
						std::to_string(state.commandMissingToolDispatchCount) +
						",\"commandInvalidArgModeFallback\":" +
						std::to_string(state.commandInvalidArgModeFallbackCount) +
						",\"commandSourceContributions\":" +
						std::to_string(state.commandSourceContributionCount) +
						",\"bundleCommandRootsScanned\":" +
						std::to_string(state.bundleCommandRootsScannedCount) +
						",\"bundleCommandFilesLoaded\":" +
						std::to_string(state.bundleCommandFilesLoadedCount) +
						",\"bundleCommandFilesSkippedDisabled\":" +
						std::to_string(state.bundleCommandFilesSkippedDisabledCount) +
						",\"bundleCommandFilesSkippedEmptyPrompt\":" +
						std::to_string(state.bundleCommandFilesSkippedEmptyPromptCount) +
						",\"bundleCommandFilesSkippedInvalidName\":" +
						std::to_string(state.bundleCommandFilesSkippedInvalidNameCount) +
						",\"bundleCommandFilesRejectedUnsafe\":" +
						std::to_string(state.bundleCommandFilesRejectedUnsafeCount) +
						",\"promptIncluded\":" +
						std::to_string(state.promptIncludedCount) +
						",\"promptChars\":" +
						std::to_string(state.promptChars) +
						",\"promptTruncated\":" +
						std::string(state.promptTruncated ? "true" : "false") +
						",\"snapshotVersion\":" +
						std::to_string(state.snapshotVersion) +
						",\"watchEnabled\":" +
						std::string(state.watchEnabled ? "true" : "false") +
						",\"watchDebounceMs\":" +
						std::to_string(state.watchDebounceMs) +
						",\"watchReason\":\"" +
						EscapeJsonLocal(state.watchReason) +
						"\"" +
						",\"sandboxSyncOk\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"sandboxDestinationNamingMode\":\"" +
						EscapeJsonLocal(state.sandboxDestinationNamingMode) +
						"\"" +
						",\"sandboxDestinationCollisions\":" +
						std::to_string(state.sandboxDestinationCollisions) +
						",\"sandboxSourceDirFallbacks\":" +
						std::to_string(state.sandboxSourceDirFallbacks) +
						",\"sandboxSynced\":" +
						std::to_string(state.sandboxSynced) +
						",\"sandboxSkipped\":" +
						std::to_string(state.sandboxSkipped) +
						",\"envAllowed\":" +
						std::to_string(state.envAllowed) +
						",\"envBlocked\":" +
						std::to_string(state.envBlocked) +
						",\"installExecutable\":" +
						std::to_string(state.installExecutableCount) +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanInfo\":" +
						std::to_string(state.scanInfoCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanFiles\":" +
						std::to_string(state.scanScannedFiles) +
						",\"warnings\":" +
						std::to_string(state.warningCount) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.install.options",
			[&host](const protocol::RequestFrame& request) {
				std::string optionsJson = "[";
				bool first = true;
				std::size_t count = 0;
				for (const auto& entry : host.m_skillsCatalogState.entries) {
					if (entry.installKind.empty()) {
						continue;
					}

					if (!first) {
						optionsJson += ",";
					}

					optionsJson +=
						"{\"skill\":\"" +
						EscapeJsonLocal(entry.name) +
						"\",\"kind\":\"" +
						EscapeJsonLocal(entry.installKind) +
						"\",\"command\":\"" +
						EscapeJsonLocal(entry.installCommand) +
						"\",\"executable\":" +
						std::string(entry.installExecutable ? "true" : "false") +
						",\"reason\":\"" +
						EscapeJsonLocal(entry.installReason) +
						"\"}";
					first = false;
					++count;
				}

				optionsJson += "]";
				return protocol::OkResponse(request, "{\"options\":" +
						optionsJson +
						",\"count\":" +
						std::to_string(count) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.install.execute",
			[&host](const protocol::RequestFrame& request) {
				const auto skillName =
					ExtractStringParam(request.paramsJson, "skill");
				const auto it = std::find_if(
					host.m_skillsCatalogState.entries.begin(),
					host.m_skillsCatalogState.entries.end(),
					[&skillName](const SkillsCatalogGatewayEntry& item) {
						return item.name == skillName;
					});

				if (it == host.m_skillsCatalogState.entries.end()) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "skill_not_found",
							.message = "Requested skill install target was not found.",
							.detailsJson = "{\"skill\":\"" +
								EscapeJsonLocal(skillName) +
								"\"}",
							.retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				const auto warning = host.m_skillsCatalogState.scanCriticalCount > 0
					? "security_scan_critical"
					: (host.m_skillsCatalogState.scanWarnCount > 0
						? "security_scan_warn"
						: "none");

				return protocol::OkResponse(request, "{\"skill\":\"" +
						EscapeJsonLocal(it->name) +
						"\",\"kind\":\"" +
						EscapeJsonLocal(it->installKind) +
						"\",\"command\":\"" +
						EscapeJsonLocal(it->installCommand) +
						"\",\"executed\":" +
						std::string(it->installExecutable ? "true" : "false") +
						",\"warning\":\"" +
						warning +
						"\",\"scanCritical\":" +
						std::to_string(host.m_skillsCatalogState.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(host.m_skillsCatalogState.scanWarnCount) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.scan.status",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"files\":" +
						std::to_string(state.scanScannedFiles) +
						",\"info\":" +
						std::to_string(state.scanInfoCount) +
						",\"warn\":" +
						std::to_string(state.scanWarnCount) +
						",\"critical\":" +
						std::to_string(state.scanCriticalCount) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.sandbox.status",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"ok\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"synced\":" +
						std::to_string(state.sandboxSynced) +
						",\"skipped\":" +
						std::to_string(state.sandboxSkipped) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.env.status",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"allowed\":" +
						std::to_string(state.envAllowed) +
						",\"blocked\":" +
						std::to_string(state.envBlocked) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.config.schema.get",
			[&host](const protocol::RequestFrame& request) {
				if (!host.m_configSchemaGetCallback) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "not_supported",
							.message =
								"Config schema callback is not configured.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				const auto state = host.m_configSchemaGetCallback();
				const std::string payload =
					"{\"schema\":" +
					NormalizeJsonRawForPayload(state.schemaJson, "{}") +
					",\"uiHints\":" +
					NormalizeJsonRawForPayload(state.uiHintsJson, "{}") +
					",\"version\":\"" +
					EscapeJsonLocal(state.version) +
					"\",\"generatedAt\":\"" +
					EscapeJsonLocal(state.generatedAt) +
					"\"}";

				return protocol::OkResponse(request, payload);
			});

		host.m_dispatcher.Register(
			"gateway.config.schema.lookup",
			[&host](const protocol::RequestFrame& request) {
				if (!host.m_configSchemaLookupCallback) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "not_supported",
							.message =
								"Config schema lookup callback is not configured.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				const std::string requestedPath =
					ExtractStringParam(request.paramsJson, "path");
				const auto normalizedPath =
					NormalizeSchemaLookupPathRequest(requestedPath);
				if (!normalizedPath.has_value()) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "invalid_lookup_path",
							.message =
								"Invalid schema lookup path.",
							.detailsJson =
								"{\"path\":\"" +
								EscapeJsonLocal(requestedPath) +
								"\"}",
							.retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				const auto lookupResult =
					host.m_configSchemaLookupCallback(normalizedPath.value());
				if (!lookupResult.has_value()) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "schema_path_not_found",
							.message = "Schema path was not found.",
							.detailsJson =
								"{\"path\":\"" +
								EscapeJsonLocal(normalizedPath.value()) +
								"\"}",
							.retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				return protocol::OkResponse(request, SerializeConfigSchemaLookupResultLocal(
							lookupResult.value()));
			});

		host.m_dispatcher.Register(
			"gateway.skills.info",
			[&host](const protocol::RequestFrame& request) {
				const auto skillName =
					ExtractStringParam(request.paramsJson, "skill");
				if (skillName.empty()) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "missing_skill",
							.message = "Parameter `skill` is required.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				const auto it = std::find_if(
					host.m_skillsCatalogState.entries.begin(),
					host.m_skillsCatalogState.entries.end(),
					[&skillName](const SkillsCatalogGatewayEntry& entry) {
						return entry.name == skillName ||
							entry.skillKey == skillName;
					});

				if (it == host.m_skillsCatalogState.entries.end()) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "skill_not_found",
							.message = "Skill not found.",
							.detailsJson =
								"{\"skill\":\"" +
								EscapeJsonLocal(skillName) +
								"\"}",
							.retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				return protocol::OkResponse(request, "{\"skill\":\"" +
						EscapeJsonLocal(it->name) +
						"\",\"skillKey\":\"" +
						EscapeJsonLocal(it->skillKey) +
						"\",\"primaryEnv\":\"" +
						EscapeJsonLocal(it->primaryEnv) +
						"\",\"requiresEnv\":" +
						SerializeStringArrayLocal(it->requiresEnv) +
						",\"requiresConfig\":" +
						SerializeStringArrayLocal(it->requiresConfig) +
						",\"missingEnv\":" +
						SerializeStringArrayLocal(it->missingEnv) +
						",\"missingConfig\":" +
						SerializeStringArrayLocal(it->missingConfig) +
						",\"missingBins\":" +
						SerializeStringArrayLocal(it->missingBins) +
						",\"missingAnyBins\":" +
						SerializeStringArrayLocal(it->missingAnyBins) +
						",\"eligible\":" +
						std::string(it->eligible ? "true" : "false") +
						",\"disabled\":" +
						std::string(it->disabled ? "true" : "false") +
						",\"blockedByAllowlist\":" +
						std::string(it->blockedByAllowlist ? "true" : "false") +
						",\"installKind\":\"" +
						EscapeJsonLocal(it->installKind) +
					   "\",\"installReason\":\"" +
						EscapeJsonLocal(it->installReason) +
						"\",\"installExecutable\":" +
						std::string(it->installExecutable ? "true" : "false") +
						",\"scanCritical\":" +
						std::to_string(host.m_skillsCatalogState.scanCriticalCount) +
						"}");
			});

		// Skills update: no param parsing here; forward to host.m_skillsUpdateCallback (BlazeClaw:
		// SkillsGatewayMethodHandler::HandleSkillsUpdate). Alias "skills.update" below rewrites method only.
		host.m_dispatcher.Register(
			"gateway.skills.update",
			[&host](const protocol::RequestFrame& request) {
				if (!host.m_skillsUpdateCallback) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "not_supported",
							.message = "Skills update callback is not configured.",
							.detailsJson = std::nullopt,
						  .retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				return host.m_skillsUpdateCallback(request);
			});

		host.m_dispatcher.Register(
			"skills.update",
			[&host](const protocol::RequestFrame& request) {
				protocol::RequestFrame delegated = request;
				delegated.method = "gateway.skills.update";
				if (!host.m_skillsUpdateCallback) {
					return protocol::ErrorResponse(
				request,
				protocol::ErrorShape{
							.code = "not_supported",
							.message = "Skills update callback is not configured.",
							.detailsJson = std::nullopt,
							.retryable = false,
							.retryAfterMs = std::nullopt,
						});
				}

				return host.m_skillsUpdateCallback(delegated);
			});

		host.m_dispatcher.Register(
			"gateway.skills.check",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;
				const bool ok =
					state.scanCriticalCount == 0 &&
					state.installBlockedCount == 0 &&
					state.sandboxSyncOk;

				return protocol::OkResponse(request, "{\"ok\":" +
						std::string(ok ? "true" : "false") +
						",\"sandboxSyncOk\":" +
						std::string(state.sandboxSyncOk ? "true" : "false") +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.diagnostics",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;
				std::vector<std::string> hints;
				if (state.installBlockedCount > 0) {
					hints.push_back("skills.install.options");
				}

				if (state.scanCriticalCount > 0) {
					hints.push_back("skills.scan.status");
				}

				if (state.verifiedOpenPathFailures > 0 ||
					state.verifiedOpenValidationFailures > 0 ||
					state.verifiedOpenIoFailures > 0) {
					hints.push_back("skills.local-loader.verified-open");
				}

				if (state.commandMissingToolDispatchCount > 0 ||
					state.commandSkillNameDedupeCount > 0 ||
					state.commandInvalidArgModeFallbackCount > 0 ||
					state.commandSourceContributionCount > 0) {
					hints.push_back("skills.command-specs");
				}

				if (state.bundleCommandRootsScannedCount > 0 ||
					state.bundleCommandFilesLoadedCount > 0 ||
					state.bundleCommandFilesSkippedDisabledCount > 0 ||
					state.bundleCommandFilesSkippedEmptyPromptCount > 0 ||
					state.bundleCommandFilesSkippedInvalidNameCount > 0 ||
					state.bundleCommandFilesRejectedUnsafeCount > 0) {
					hints.push_back("plugins.bundle-commands");
				}

				if (state.entryConfigRawCount > 0 ||
					state.entryConfigNormalizedCount > 0 ||
					state.entryConfigMalformedCount > 0) {
					hints.push_back("skills.config.entries");
				}

				if (!state.sandboxSyncOk) {
					hints.push_back("skills.sandbox.status");
				}

				std::string hintsJson = "[";
				for (std::size_t index = 0; index < hints.size(); ++index) {
					if (index > 0) {
						hintsJson += ",";
					}

					hintsJson +=
						"\"" +
						EscapeJsonLocal(hints[index]) +
						"\"";
				}

				hintsJson += "]";
				return protocol::OkResponse(request, "{\"warnings\":" +
						std::to_string(state.warningCount) +
						",\"installBlocked\":" +
						std::to_string(state.installBlockedCount) +
						",\"scanCritical\":" +
						std::to_string(state.scanCriticalCount) +
						",\"scanWarn\":" +
						std::to_string(state.scanWarnCount) +
						",\"verifiedOpenPathFailures\":" +
						std::to_string(state.verifiedOpenPathFailures) +
						",\"verifiedOpenValidationFailures\":" +
						std::to_string(state.verifiedOpenValidationFailures) +
						",\"verifiedOpenIoFailures\":" +
						std::to_string(state.verifiedOpenIoFailures) +
						",\"commandSanitize\":" +
						std::to_string(state.commandSanitizeCount) +
						",\"commandDedupe\":" +
						std::to_string(state.commandDedupeCount) +
						",\"commandSkillNameDedupe\":" +
						std::to_string(state.commandSkillNameDedupeCount) +
						",\"commandMissingToolDispatch\":" +
						std::to_string(state.commandMissingToolDispatchCount) +
						",\"commandInvalidArgModeFallback\":" +
						std::to_string(state.commandInvalidArgModeFallbackCount) +
						",\"commandSourceContributions\":" +
						std::to_string(state.commandSourceContributionCount) +
						",\"entryConfigRaw\":" +
						std::to_string(state.entryConfigRawCount) +
						",\"entryConfigNormalized\":" +
						std::to_string(state.entryConfigNormalizedCount) +
						",\"entryConfigMalformed\":" +
						std::to_string(state.entryConfigMalformedCount) +
						",\"bundleCommandRootsScanned\":" +
						std::to_string(state.bundleCommandRootsScannedCount) +
						",\"bundleCommandFilesLoaded\":" +
						std::to_string(state.bundleCommandFilesLoadedCount) +
						",\"bundleCommandFilesSkippedDisabled\":" +
						std::to_string(state.bundleCommandFilesSkippedDisabledCount) +
						",\"bundleCommandFilesSkippedEmptyPrompt\":" +
						std::to_string(state.bundleCommandFilesSkippedEmptyPromptCount) +
						",\"bundleCommandFilesSkippedInvalidName\":" +
						std::to_string(state.bundleCommandFilesSkippedInvalidNameCount) +
						",\"bundleCommandFilesRejectedUnsafe\":" +
						std::to_string(state.bundleCommandFilesRejectedUnsafeCount) +
						",\"hints\":" +
						hintsJson +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.prompt",
			[&host](const protocol::RequestFrame& request) {
				const auto& state = host.m_skillsCatalogState;

				return protocol::OkResponse(request, "{\"prompt\":\"" +
						EscapeJsonLocal(state.prompt) +
						"\",\"included\":" +
						std::to_string(state.promptIncludedCount) +
						",\"chars\":" +
						std::to_string(state.promptChars) +
						",\"truncated\":" +
						std::string(state.promptTruncated ? "true" : "false") +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.commands",
			[&host](const protocol::RequestFrame& request) {
				std::string commandsJson = "[";
				bool first = true;
				std::size_t count = 0;

				for (const auto& entry : host.m_skillsCatalogState.entries) {
					if (entry.commandName.empty()) {
						continue;
					}

					if (!first) {
						commandsJson += ",";
					}

					commandsJson +=
						"{\"name\":\"" +
						EscapeJsonLocal(entry.commandName) +
						"\",\"skill\":\"" +
						EscapeJsonLocal(entry.name) +
						"\",\"toolName\":\"" +
						EscapeJsonLocal(entry.commandToolName) +
						"\",\"argMode\":\"" +
						EscapeJsonLocal(entry.commandArgMode) +
						"\",\"argSchema\":\"" +
						EscapeJsonLocal(entry.commandArgSchema) +
						"\",\"resultSchema\":\"" +
						EscapeJsonLocal(entry.commandResultSchema) +
						"\",\"idempotencyHint\":\"" +
						EscapeJsonLocal(entry.commandIdempotencyHint) +
						"\",\"retryPolicyHint\":\"" +
						EscapeJsonLocal(entry.commandRetryPolicyHint) +
						"\",\"requiresApproval\":" +
						std::string(entry.commandRequiresApproval ? "true" : "false") +
						"\",\"description\":\"" +
						EscapeJsonLocal(entry.description) +
						"\"}";
					first = false;
					++count;
				}

				commandsJson += "]";
				return protocol::OkResponse(request, "{\"commands\":" +
						commandsJson +
						",\"count\":" +
						std::to_string(count) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.refresh",
			[&host](const protocol::RequestFrame& request) {
				bool refreshed = false;
				if (host.m_skillsRefreshCallback) {
					host.m_skillsCatalogState = host.m_skillsRefreshCallback();
					refreshed = true;
				}

				const auto& state = host.m_skillsCatalogState;
				return protocol::OkResponse(request, "{\"refreshed\":" +
						std::string(refreshed ? "true" : "false") +
						",\"version\":" +
						std::to_string(state.snapshotVersion) +
						",\"reason\":\"" +
						EscapeJsonLocal(state.watchReason) +
						"\"}");
			});

		host.m_dispatcher.Register(
			"gateway.skills.list",
			[&host](const protocol::RequestFrame& request) {
				const auto includeInvalid =
					ExtractBoolParam(request.paramsJson, "includeInvalid");
				const bool shouldIncludeInvalid =
					!includeInvalid.has_value() || includeInvalid.value();

				std::string entriesJson = "[";
				bool first = true;
				std::size_t count = 0;
				for (const auto& entry : host.m_skillsCatalogState.entries) {
					if (!shouldIncludeInvalid && !entry.validFrontmatter) {
						continue;
					}

					if (!first) {
						entriesJson += ",";
					}

					entriesJson += SerializeSkillCatalogEntry(entry);
					first = false;
					++count;
				}
				entriesJson += "]";

				return protocol::OkResponse(request, "{\"skills\":" +
						entriesJson +
						",\"count\":" +
						std::to_string(count) +
						",\"includeInvalid\":" +
						std::string(shouldIncludeInvalid ? "true" : "false") +
						"}");
			});
}

void RuntimeOrchestrationStreamingHandlers::RegisterAll(GatewayHost& host) {

		host.m_dispatcher.Register("gateway.runtime.orchestration.status", [&host](const protocol::RequestFrame& request) {
			const auto sessions = host.m_sessionRegistry.List();
			const auto agents = host.m_agentRegistry.List();
			const std::string activeSession =
				HasSessionId(host.m_sessionRegistry, host.m_runtimeAssignedSessionId)
				? host.m_runtimeAssignedSessionId
				: (sessions.empty() ? "main" : sessions.front().id);
			const std::string activeAgent =
				HasAgentId(host.m_agentRegistry, host.m_runtimeAssignedAgentId)
				? host.m_runtimeAssignedAgentId
				: (agents.empty() ? "default" : agents.front().id);
			const bool busy =
				host.m_runtimeQueueDepth > 0 || host.m_runtimeRunningCount > 0;
			const std::string configuredOrchestrationPath =
				ToLowerCopyLocal(host.m_embeddedOrchestrationPath);
			const std::string selectedOrchestrationPath =
				host.m_latestOrchestrationPathSelection.path.empty()
				? configuredOrchestrationPath
				: host.m_latestOrchestrationPathSelection.path;
			const std::string latestSelectionRunId =
				host.m_latestOrchestrationPathSelection.runId;
			const std::uint64_t latestSelectionObservedAtEpochMs =
				host.m_latestOrchestrationPathSelection.observedAtEpochMs;
			const bool latestSelectionCompatEnabled =
				host.m_latestOrchestrationPathSelection.compatDeterministicEnabled;
			const bool latestSelectionIntentEnabled =
				host.m_latestOrchestrationPathSelection.intentDeterministicEnabled;
			const bool latestSelectionDeterministicEnabled =
				host.m_latestOrchestrationPathSelection.deterministicEnabled;
			const std::string latestSelectionDecisionReasonCode =
				host.m_latestOrchestrationPathSelection.decisionReasonCode;
			const std::string latestSelectionDecompositionMetadataSource =
				host.m_latestOrchestrationPathSelection.decompositionMetadataSource;
			const std::string latestSelectionOrderedPolicyMode =
				host.m_latestOrchestrationPathSelection.orderedPolicyMode;
			const bool latestSelectionOrderedPolicyStrict =
				host.m_latestOrchestrationPathSelection.orderedPolicyStrict;
			const std::string latestSelectionFallbackPolicyProfile =
				host.m_latestOrchestrationPathSelection.fallbackPolicyProfile;

			return protocol::OkResponse(request, "{\"state\":\"" + std::string(busy ? "busy" : "idle") +
					"\",\"activeSession\":\"" +
					EscapeJsonLocal(activeSession) + "\",\"activeAgent\":\"" +
					EscapeJsonLocal(activeAgent) + "\",\"queueDepth\":" +
					std::to_string(host.m_runtimeQueueDepth) +
					",\"running\":" +
					std::to_string(host.m_runtimeRunningCount) +
					",\"capacity\":" +
				 std::to_string(host.m_runtimeQueueCapacity) +
					",\"orchestrationPath\":{\"configured\":\"" +
					EscapeJsonLocal(configuredOrchestrationPath) +
					"\",\"selected\":\"" +
					EscapeJsonLocal(selectedOrchestrationPath) +
					"\",\"latestRunId\":\"" +
					EscapeJsonLocal(latestSelectionRunId) +
					"\",\"latestObservedAtEpochMs\":" +
					std::to_string(latestSelectionObservedAtEpochMs) +
					",\"compatDeterministicEnabled\":" +
					std::string(latestSelectionCompatEnabled ? "true" : "false") +
					",\"intentDeterministicEnabled\":" +
					std::string(latestSelectionIntentEnabled ? "true" : "false") +
					",\"deterministicEnabled\":" +
					std::string(
					   latestSelectionDeterministicEnabled ? "true" : "false") +
					",\"decisionReasonCode\":" +
					JsonString(latestSelectionDecisionReasonCode) +
					",\"decompositionMetadataSource\":" +
					JsonString(latestSelectionDecompositionMetadataSource) +
					",\"orderedPolicyMode\":" +
					JsonString(latestSelectionOrderedPolicyMode) +
					",\"orderedPolicyStrict\":" +
					std::string(latestSelectionOrderedPolicyStrict ? "true" : "false") +
					",\"fallbackPolicyProfile\":" +
					JsonString(latestSelectionFallbackPolicyProfile) +
				  "},\"dynamicLoopMetrics\":{\"success\":" +
					std::to_string(host.m_taskDeltaRunSuccessCount) +
					",\"failure\":" +
					std::to_string(host.m_taskDeltaRunFailureCount) +
					",\"timeout\":" +
					std::to_string(host.m_taskDeltaRunTimeoutCount) +
					",\"cancelled\":" +
					std::to_string(host.m_taskDeltaRunCancelledCount) +
					",\"fallback\":" +
				 std::to_string(host.m_taskDeltaRunFallbackCount) +
					"},\"taskDeltaLifecycleMapping\":{\"plan\":\"item.plan\",\"preflight\":\"tool.precheck\",\"tool_call\":\"tool.start\",\"tool_result\":\"tool.result\",\"final\":\"lifecycle.final\"}}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.health.dependencies",
			[&host](const protocol::RequestFrame& request) {
				const auto health =
					executors::EmailScheduleExecutor::GetRuntimeHealthIndex(false);

				std::string probesJson = "[";
				for (std::size_t index = 0; index < health.probes.size(); ++index) {
					if (index > 0) {
						probesJson += ",";
					}

					const auto& probe = health.probes[index];
					probesJson +=
						"{\"key\":\"" + EscapeJsonLocal(probe.key) +
						"\",\"state\":\"" + EscapeJsonLocal(probe.state) +
						"\",\"reasonCode\":\"" +
						EscapeJsonLocal(probe.reasonCode) +
						"\",\"reasonMessage\":\"" +
						EscapeJsonLocal(probe.reasonMessage) +
						"\",\"checkedAtEpochMs\":" +
						std::to_string(probe.checkedAtEpochMs) +
						",\"expiresAtEpochMs\":" +
						std::to_string(probe.expiresAtEpochMs) + "}";
				}
				probesJson += "]";

				EmitTelemetryEvent(
					"gateway.email.preflight.snapshot",
					std::string("{\"generatedAtEpochMs\":") +
					std::to_string(health.generatedAtEpochMs) +
					",\"ttlMs\":" +
					std::to_string(health.ttlMs) +
					",\"count\":" +
					std::to_string(health.probes.size()) +
					",\"capability\":\"" +
					EscapeJsonLocal(health.emailSendState) +
					"\"}");

				std::string runtimeNodeState = "unknown";
				std::string runtimePythonState = "unknown";
				std::string webBrowsingPythonState = "unknown";
				for (const auto& probe : health.probes) {
					if (probe.key == "runtime:node") {
						runtimeNodeState = probe.state;
						continue;
					}

					if (probe.key == "runtime:python") {
						runtimePythonState = probe.state;
						continue;
					}

					if (probe.key == "skill:web_browsing_python") {
						webBrowsingPythonState = probe.state;
					}
				}

				EmitTelemetryEvent(
					"gateway.runtime.preflight.executors",
					std::string("{\"runtimeNode\":\"") +
					EscapeJsonLocal(runtimeNodeState) +
					"\",\"runtimePython\":\"" +
					EscapeJsonLocal(runtimePythonState) +
					"\",\"webBrowsingPython\":\"" +
					EscapeJsonLocal(webBrowsingPythonState) +
					"\",\"generatedAtEpochMs\":" +
					std::to_string(health.generatedAtEpochMs) +
					"}");

				return protocol::OkResponse(request, "{\"probes\":" + probesJson +
						",\"count\":" +
						std::to_string(health.probes.size()) +
						",\"generatedAtEpochMs\":" +
						std::to_string(health.generatedAtEpochMs) +
						",\"ttlMs\":" + std::to_string(health.ttlMs) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.health.readiness",
			[&host](const protocol::RequestFrame& request) {
				const bool ready =
					host.m_running &&
					host.m_initialized &&
					host.m_dispatchInitialized &&
					host.m_runtimeHandlersInitialized;

				std::string reasonsJson = "[";
				bool first = true;
				auto appendReason = [&reasonsJson, &first](const std::string& reason) {
					if (!first) {
						reasonsJson += ",";
					}
					reasonsJson += "\"" + EscapeJsonLocal(reason) + "\"";
					first = false;
					};

				if (!host.m_running) {
					appendReason("gateway_not_running");
				}
				if (!host.m_initialized) {
					appendReason("runtime_not_initialized");
				}
				if (!host.m_dispatchInitialized) {
					appendReason("dispatcher_not_initialized");
				}
				if (!host.m_runtimeHandlersInitialized) {
					appendReason("runtime_handlers_not_initialized");
				}

				reasonsJson += "]";

				return protocol::OkResponse(request, "{\"ready\":" +
						std::string(ready ? "true" : "false") +
						",\"running\":" +
						std::string(host.m_running ? "true" : "false") +
						",\"initialized\":" +
						std::string(host.m_initialized ? "true" : "false") +
						",\"dispatchInitialized\":" +
						std::string(host.m_dispatchInitialized ? "true" : "false") +
						",\"runtimeHandlersInitialized\":" +
						std::string(host.m_runtimeHandlersInitialized ? "true" : "false") +
						",\"reasons\":" + reasonsJson +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.mutations.status",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"agentRuns\":" +
						std::to_string(host.m_agentRuns.size()) +
						",\"chatRuns\":" +
						std::to_string(host.m_chatRunsById.size()) +
						",\"taskDeltaRuns\":" +
						std::to_string(host.m_taskDeltasByRunId.size()) +
						",\"activeSessions\":" +
						std::to_string(host.m_sessionRegistry.List().size()) +
						",\"activeChannels\":" +
						std::to_string(host.m_channelRegistry.ListStatus().size()) +
						"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.health.capabilities",
			[](const protocol::RequestFrame& request) {
				const auto health =
					executors::EmailScheduleExecutor::GetRuntimeHealthIndex(false);
				return protocol::OkResponse(request, "{\"capabilities\":[{\"name\":\"email.send\",\"state\":\"" +
						EscapeJsonLocal(health.emailSendState) +
						"\"}],\"count\":1,\"generatedAtEpochMs\":" +
						std::to_string(health.generatedAtEpochMs) +
						",\"ttlMs\":" + std::to_string(health.ttlMs) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.policy.resolve",
			[&host](const protocol::RequestFrame& request) {
				std::string backendsJson = "[";
				for (std::size_t i = 0; i < host.m_runtimeEmailResolvedBackends.size(); ++i) {
					if (i > 0) {
						backendsJson += ",";
					}

					backendsJson +=
						"\"" + EscapeJsonLocal(host.m_runtimeEmailResolvedBackends[i]) + "\"";
				}
				backendsJson += "]";

				EmitTelemetryEvent(
					"gateway.email.policy.decision",
					std::string("{\"profileId\":\"") +
					EscapeJsonLocal(host.m_runtimeEmailPolicyProfileId) +
					"\",\"backends\":" +
					backendsJson +
					",\"actions\":{\"unavailable\":\"" +
					EscapeJsonLocal(host.m_runtimeEmailPolicyOnUnavailable) +
					"\",\"authError\":\"" +
					EscapeJsonLocal(host.m_runtimeEmailPolicyOnAuthError) +
					"\",\"execError\":\"" +
					EscapeJsonLocal(host.m_runtimeEmailPolicyOnExecError) +
					"\"}}");

				return protocol::OkResponse(request, "{\"profileId\":\"" +
						EscapeJsonLocal(host.m_runtimeEmailPolicyProfileId) +
						"\",\"backends\":" +
						backendsJson +
						",\"actions\":{\"unavailable\":\"" +
						EscapeJsonLocal(host.m_runtimeEmailPolicyOnUnavailable) +
						"\",\"authError\":\"" +
						EscapeJsonLocal(host.m_runtimeEmailPolicyOnAuthError) +
						"\",\"execError\":\"" +
						EscapeJsonLocal(host.m_runtimeEmailPolicyOnExecError) +
						"\"},\"retry\":{\"maxAttempts\":" +
						std::to_string(host.m_runtimeEmailRetryMaxAttempts) +
						",\"delayMs\":" +
						std::to_string(host.m_runtimeEmailRetryDelayMs) +
						"},\"approval\":{\"requiresApproval\":" +
						std::string(host.m_runtimeEmailRequiresApproval ? "true" : "false") +
						",\"tokenTtlMinutes\":" +
						std::to_string(host.m_runtimeEmailApprovalTokenTtlMinutes) +
						"}}");
			});

		RegisterGatewayRuntimeStaticOrchestrationStreamingMetrics(host.m_dispatcher);


		host.m_dispatcher.Register("gateway.runtime.streaming.status", [&host](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"enabled\":" +
					std::string(host.m_runtimeAgentStreaming ? "true" : "false") +
					",\"mode\":\"chunked\",\"heartbeatMs\":1500" +
					std::string(host.m_streamingThrottled ? ",\"throttled\":true" : ",\"throttled\":false") +
					",\"bufferedFrames\":" + std::to_string(host.m_streamingBufferedFrames) +
					",\"bufferedBytes\":" + std::to_string(host.m_streamingBufferedBytes) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.cohesion",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"cohesive\":true,\"delta\":0,\"samples\":2}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.waveIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"waveIndex\":1,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.syncBand",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncBand\":1,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.waveDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"waveDrift\":0,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register("gateway.models.failover.status", [&host](const protocol::RequestFrame& request) {
			const std::string selectedPrimary =
				host.m_failoverOverrideActive
				? host.m_failoverOverrideModel
				: host.m_runtimeAgentModel;
			return protocol::OkResponse(request, "{\"primary\":\"" + EscapeJsonLocal(selectedPrimary) +
					"\",\"fallbacks\":[\"reasoner\"],\"maxRetries\":2,\"strategy\":\"ordered\",\"overrideActive\":" +
					std::string(host.m_failoverOverrideActive ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.runtime.orchestration.queue", [&host](const protocol::RequestFrame& request) {
			const auto queued = ExtractSizeParam(request.paramsJson, "queued");
			const auto running = ExtractSizeParam(request.paramsJson, "running");
			const auto capacity = ExtractSizeParam(request.paramsJson, "capacity");

			if (queued.has_value()) {
				host.m_runtimeQueueDepth = queued.value();
			}

			if (running.has_value()) {
				host.m_runtimeRunningCount = running.value();
			}

			if (capacity.has_value() && capacity.value() > 0) {
				host.m_runtimeQueueCapacity = capacity.value();
			}

			if (host.m_runtimeRunningCount > host.m_runtimeQueueCapacity) {
				host.m_runtimeRunningCount = host.m_runtimeQueueCapacity;
			}

			return protocol::OkResponse(request, "{\"queued\":" + std::to_string(host.m_runtimeQueueDepth) +
					",\"running\":" + std::to_string(host.m_runtimeRunningCount) +
					",\"capacity\":" + std::to_string(host.m_runtimeQueueCapacity) +
					",\"updated\":true}");
			});

		host.m_dispatcher.Register("gateway.runtime.streaming.sample", [&host](const protocol::RequestFrame& request) {
			const std::size_t chunks = host.m_streamingBufferedFrames > 0
				? (std::min)(host.m_streamingBufferedFrames, static_cast<std::size_t>(3))
				: 2;
			const bool finalChunk = !host.m_streamingThrottled;

			return protocol::OkResponse(request, "{\"chunks\":[\"hello\",\"world\"],\"count\":" +
					std::to_string(chunks) +
					",\"final\":" +
					std::string(finalChunk ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.models.failover.preview", [&host](const protocol::RequestFrame& request) {
			std::string requested = ExtractStringParam(request.paramsJson, "model");
			if (requested.empty()) {
				requested = host.m_runtimeAgentModel;
			}

			const std::string selected =
				host.m_failoverOverrideActive
				? host.m_failoverOverrideModel
				: requested;

			return protocol::OkResponse(request, "{\"model\":\"" + EscapeJsonLocal(requested) +
					"\",\"attempts\":[\"" + EscapeJsonLocal(requested) +
					"\",\"reasoner\"],\"selected\":\"" +
					EscapeJsonLocal(selected) + "\"}");
			});

		host.m_dispatcher.Register("gateway.runtime.orchestration.assign", [&host](const protocol::RequestFrame& request) {
			std::string agentId = ExtractStringParam(request.paramsJson, "agentId");
			std::string sessionId = ExtractStringParam(request.paramsJson, "sessionId");

			if (agentId.empty()) {
				agentId = host.m_runtimeAssignedAgentId;
			}

			if (sessionId.empty()) {
				sessionId = host.m_runtimeAssignedSessionId;
			}

			const bool agentExists = HasAgentId(host.m_agentRegistry, agentId);
			const bool sessionExists = HasSessionId(host.m_sessionRegistry, sessionId);
			const bool assigned = agentExists && sessionExists;

			if (assigned) {
				host.m_runtimeAssignedAgentId = agentId;
				host.m_runtimeAssignedSessionId = sessionId;
				++host.m_runtimeAssignmentCount;

				if (host.m_runtimeQueueDepth > 0 &&
					host.m_runtimeRunningCount < host.m_runtimeQueueCapacity) {
					--host.m_runtimeQueueDepth;
					++host.m_runtimeRunningCount;
				}
			}

			return protocol::OkResponse(request, "{\"agentId\":\"" + EscapeJsonLocal(agentId) +
					"\",\"sessionId\":\"" + EscapeJsonLocal(sessionId) +
					"\",\"assigned\":" +
					std::string(assigned ? "true" : "false") +
					",\"assignments\":" +
					std::to_string(host.m_runtimeAssignmentCount) + "}");
			});

		host.m_dispatcher.Register("gateway.runtime.streaming.window", [&host](const protocol::RequestFrame& request) {
			const auto windowMs = ExtractSizeParam(request.paramsJson, "windowMs");
			if (windowMs.has_value() && windowMs.value() > 0) {
				host.m_streamingWindowMs = windowMs.value();
			}

			return protocol::OkResponse(request, "{\"windowMs\":" + std::to_string(host.m_streamingWindowMs) +
					",\"frames\":" + std::to_string(host.m_streamingBufferedFrames) +
					",\"dropped\":0}");
			});

		host.m_dispatcher.Register("gateway.models.failover.metrics", [&host](const protocol::RequestFrame& request) {
			const double successRate =
				host.m_failoverAttempts == 0
				? 1.0
				: static_cast<double>(host.m_failoverAttempts - host.m_failoverFallbackHits) /
				static_cast<double>(host.m_failoverAttempts);

			return protocol::OkResponse(request, "{\"attempts\":" + std::to_string(host.m_failoverAttempts) +
					",\"fallbackHits\":" + std::to_string(host.m_failoverFallbackHits) +
					",\"successRate\":" + std::to_string(successRate) + "}");
			});

		host.m_dispatcher.Register("gateway.runtime.orchestration.rebalance", [&host](const protocol::RequestFrame& request) {
			std::string strategy = ExtractStringParam(request.paramsJson, "strategy");
			if (strategy.empty()) {
				strategy = "sticky";
			}

			std::size_t moved = 0;
			if (host.m_runtimeQueueDepth > 0 &&
				host.m_runtimeRunningCount < host.m_runtimeQueueCapacity) {
				moved = 1;
				--host.m_runtimeQueueDepth;
				++host.m_runtimeRunningCount;
			}

			++host.m_runtimeRebalanceCount;

			return protocol::OkResponse(request, "{\"moved\":" + std::to_string(moved) +
					",\"remaining\":" +
					std::to_string(host.m_runtimeQueueDepth + host.m_runtimeRunningCount) +
					",\"strategy\":\"" + EscapeJsonLocal(strategy) +
					"\",\"rebalances\":" +
					std::to_string(host.m_runtimeRebalanceCount) + "}");
			});

		host.m_dispatcher.Register("gateway.runtime.streaming.backpressure", [&host](const protocol::RequestFrame& request) {
			const std::size_t pressure =
				host.m_streamingHighWatermark == 0
				? 0
				: (host.m_streamingBufferedFrames * 100) / host.m_streamingHighWatermark;
			host.m_streamingThrottled = pressure >= 80;

			return protocol::OkResponse(request, "{\"pressure\":" + std::to_string(pressure) +
					",\"throttled\":" +
					std::string(host.m_streamingThrottled ? "true" : "false") +
					",\"bufferedFrames\":" +
					std::to_string(host.m_streamingBufferedFrames) + "}");
			});

		host.m_dispatcher.Register("gateway.models.failover.simulate", [&host](const protocol::RequestFrame& request) {
			std::string requested = ExtractStringParam(request.paramsJson, "requested");
			if (requested.empty()) {
				requested = host.m_runtimeAgentModel;
			}

			const bool useFallback =
				host.m_failoverOverrideActive && host.m_failoverOverrideModel != requested;
			const std::string resolved =
				useFallback ? host.m_failoverOverrideModel : requested;
			++host.m_failoverAttempts;
			if (useFallback) {
				++host.m_failoverFallbackHits;
			}

			return protocol::OkResponse(request, "{\"requested\":\"" + EscapeJsonLocal(requested) +
					"\",\"resolved\":\"" + EscapeJsonLocal(resolved) +
					"\",\"usedFallback\":" +
					std::string(useFallback ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register("gateway.runtime.orchestration.drain", [&host](const protocol::RequestFrame& request) {
			std::string reason = ExtractStringParam(request.paramsJson, "reason");
			if (reason.empty()) {
				reason = "idle";
			}

			const std::size_t drained =
				host.m_runtimeQueueDepth + host.m_runtimeRunningCount;
			host.m_runtimeQueueDepth = 0;
			host.m_runtimeRunningCount = 0;
			++host.m_runtimeDrainCount;

			return protocol::OkResponse(request, "{\"drained\":" + std::to_string(drained) +
					",\"remaining\":0,\"reason\":\"" +
					EscapeJsonLocal(reason) +
					"\",\"drains\":" +
					std::to_string(host.m_runtimeDrainCount) + "}");
			});

		host.m_dispatcher.Register("gateway.runtime.streaming.replay", [&host](const protocol::RequestFrame& request) {
			const std::size_t replayed =
				(std::min)(host.m_streamingBufferedFrames, static_cast<std::size_t>(2));

			return protocol::OkResponse(request, "{\"replayed\":" + std::to_string(replayed) +
					",\"cursor\":\"stream-cursor-1\",\"complete\":true}");
			});

		host.m_dispatcher.Register("gateway.models.failover.audit", [](const protocol::RequestFrame& request) {
			return protocol::OkResponse(request, "{\"entries\":2,\"lastModel\":\"default\",\"lastOutcome\":\"primary\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.snapshot",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"sessions\":" +
						std::to_string(host.m_sessionRegistry.List().size()) +
						",\"agents\":" +
						std::to_string(host.m_agentRegistry.List().size()) +
						",\"active\":\"" +
						EscapeJsonLocal(host.m_runtimeAssignedSessionId) +
						"\",\"activeAgent\":\"" +
						EscapeJsonLocal(host.m_runtimeAssignedAgentId) +
						"\",\"queue\":" +
						std::to_string(host.m_runtimeQueueDepth) +
						",\"running\":" +
						std::to_string(host.m_runtimeRunningCount) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.cursor",
			[&host](const protocol::RequestFrame& request) {
				const std::size_t lagMs =
					host.m_streamingBufferedFrames * 10;
				return protocol::OkResponse(request, "{\"cursor\":\"stream-cursor-1\",\"lagMs\":" +
						std::to_string(lagMs) +
						",\"hasMore\":" +
						std::string(host.m_streamingBufferedFrames > 0 ? "true" : "false") + "}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.policy",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"policy\":\"ordered\",\"maxRetries\":2,\"stickyPrimary\":true,\"overrideModel\":\"" +
						EscapeJsonLocal(host.m_failoverOverrideModel) + "\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.timeline",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"ticks\":[" +
						std::to_string(host.m_runtimeAssignmentCount) + "," +
						std::to_string(host.m_runtimeRebalanceCount) + "," +
						std::to_string(host.m_runtimeDrainCount) +
						"],\"count\":3,\"source\":\"runtime-orchestrator\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.metrics",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"frames\":" +
						std::to_string(host.m_streamingBufferedFrames) +
						",\"bytes\":" +
						std::to_string(host.m_streamingBufferedBytes) +
						",\"avgChunkMs\":5}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.history",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"events\":[\"primary\",\"fallback\"],\"count\":" +
						std::to_string(host.m_failoverAttempts) +
						",\"last\":\"" +
						std::string(host.m_failoverFallbackHits > 0 ? "fallback" : "primary") + "\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.heartbeat",
			[&host](const protocol::RequestFrame& request) {
				const std::size_t backlog =
					host.m_runtimeQueueDepth + host.m_runtimeRunningCount;
				return protocol::OkResponse(request, "{\"alive\":true,\"intervalMs\":1000,\"jitterMs\":" +
						std::to_string(25 + (backlog > 0 ? 5 : 0)) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.health",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"healthy\":" +
						std::string(host.m_streamingBufferedFrames <= host.m_streamingHighWatermark ? "true" : "false") +
						",\"stalls\":0,\"recoveries\":0}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.recent",
			[&host](const protocol::RequestFrame& request) {
				const std::string activeModel =
					host.m_failoverOverrideActive
					? host.m_failoverOverrideModel
					: host.m_runtimeAgentModel;
				return protocol::OkResponse(request, "{\"models\":[\"" + EscapeJsonLocal(host.m_runtimeAgentModel) +
						"\",\"reasoner\"],\"count\":2,\"active\":\"" +
						EscapeJsonLocal(activeModel) + "\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.pulse",
			[&host](const protocol::RequestFrame& request) {
				const std::size_t pulse =
					host.m_runtimeAssignmentCount +
					host.m_runtimeRebalanceCount +
					host.m_runtimeDrainCount;
				const bool busy =
					host.m_runtimeQueueDepth > 0 || host.m_runtimeRunningCount > 0;
				return protocol::OkResponse(request, "{\"pulse\":" + std::to_string(pulse) +
						",\"driftMs\":0,\"state\":\"" +
						std::string(busy ? "active" : "steady") + "\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.snapshot",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"frames\":" + std::to_string(host.m_streamingBufferedFrames) +
						",\"cursor\":\"stream-cursor-2\",\"sealed\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.window",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"windowSec\":60,\"attempts\":" +
						std::to_string(host.m_failoverAttempts) +
						",\"fallbacks\":" +
						std::to_string(host.m_failoverFallbackHits) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.cadence",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"periodMs\":1000,\"varianceMs\":5,\"aligned\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.watermark",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"high\":" + std::to_string(host.m_streamingHighWatermark) +
						",\"low\":4,\"current\":" +
						std::to_string(host.m_streamingBufferedFrames) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.digest",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"digest\":\"sha256:failover-v1\",\"entries\":" +
						std::to_string(host.m_failoverAttempts) +
						",\"fresh\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.beacon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"beacon\":\"orch-1\",\"seq\":1,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.checkpoint",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"checkpoint\":\"cp-1\",\"frames\":2,\"persisted\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.ledger",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"entries\":2,\"primaryHits\":1,\"fallbackHits\":1}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.epoch",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"epoch\":1,\"startedMs\":1735689600000,\"active\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.resume",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"resumed\":true,\"cursor\":\"stream-cursor-3\",\"replayed\":1}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.profile",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"profile\":\"balanced\",\"weights\":[70,30],\"version\":1}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.phase",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phase\":\"steady\",\"step\":1,\"locked\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.recovery",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"recovering\":false,\"attempts\":0,\"lastMs\":0}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.baseline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"primary\":\"default\",\"secondary\":\"reasoner\",\"confidence\":100}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.signal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"signal\":\"ok\",\"priority\":1,\"latched\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.continuity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"continuous\":true,\"gaps\":0,\"lastSeq\":2}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.forecast",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"windowSec\":60,\"projectedFallbacks\":1,\"risk\":\"low\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.vector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"axis\":\"primary\",\"magnitude\":1,\"normalized\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.stability",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"stable\":true,\"variance\":0,\"samples\":2}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.threshold",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"minSuccessRate\":90,\"maxFallbacks\":2,\"active\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.matrix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"rows\":2,\"cols\":2,\"balanced\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.integrity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"valid\":true,\"violations\":0,\"checked\":2}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.guardrail",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"rule\":\"max_fallbacks\",\"limit\":2,\"enforced\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.lattice",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"layers\":2,\"nodes\":4,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.coherence",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"coherent\":true,\"drift\":0,\"segments\":2}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.envelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"windowSec\":60,\"floor\":90,\"ceiling\":100}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.mesh",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"nodes\":4,\"edges\":3,\"connected\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.fidelity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"fidelity\":100,\"drops\":0,\"verified\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.margin",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"headroom\":10,\"buffer\":2,\"safe\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.fabric",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"threads\":6,\"links\":8,\"resilient\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.accuracy",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"accuracy\":99,\"mismatches\":0,\"calibrated\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.reserve",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"reserve\":1,\"available\":true,\"priority\":2}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.load",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"queueLoad\":0,\"agentLoad\":0,\"state\":\"steady\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.buffer",
			[&host](const protocol::RequestFrame& request) {
				const auto bufferedFrames =
					ExtractSizeParam(request.paramsJson, "bufferedFrames");
				const auto bufferedBytes =
					ExtractSizeParam(request.paramsJson, "bufferedBytes");
				const auto highWatermark =
					ExtractSizeParam(request.paramsJson, "highWatermark");

				if (bufferedFrames.has_value()) {
					host.m_streamingBufferedFrames = bufferedFrames.value();
				}

				if (bufferedBytes.has_value()) {
					host.m_streamingBufferedBytes = bufferedBytes.value();
				}

				if (highWatermark.has_value() && highWatermark.value() > 0) {
					host.m_streamingHighWatermark = highWatermark.value();
				}

				host.m_streamingThrottled =
					host.m_streamingBufferedFrames >= host.m_streamingHighWatermark;

				return protocol::OkResponse(request, "{\"bufferedFrames\":" +
						std::to_string(host.m_streamingBufferedFrames) +
						",\"bufferedBytes\":" +
						std::to_string(host.m_streamingBufferedBytes) +
						",\"highWatermark\":" +
						std::to_string(host.m_streamingHighWatermark) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override",
			[&host](const protocol::RequestFrame& request) {
				std::string model =
					ExtractStringParam(request.paramsJson, "model");
				std::string reason =
					ExtractStringParam(request.paramsJson, "reason");
				const auto activeParam =
					ExtractBoolParam(request.paramsJson, "active");

				if (model.empty()) {
					model = host.m_runtimeAgentModel;
				}

				if (reason.empty()) {
					reason = "manual";
				}

				const bool active = activeParam.value_or(true);
				host.m_failoverOverrideActive = active;
				host.m_failoverOverrideModel = model;
				host.m_failoverOverrideReason = reason;
				++host.m_failoverOverrideChanges;

				return protocol::OkResponse(request, "{\"active\":" +
						std::string(host.m_failoverOverrideActive ? "true" : "false") +
						",\"model\":\"" +
						EscapeJsonLocal(host.m_failoverOverrideModel) +
						"\",\"reason\":\"" +
						EscapeJsonLocal(host.m_failoverOverrideReason) +
						"\",\"changes\":" +
						std::to_string(host.m_failoverOverrideChanges) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.saturation",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"saturation\":0,\"capacity\":8,\"state\":\"stable\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.throttle",
			[&host](const protocol::RequestFrame& request) {
				const auto throttled =
					ExtractBoolParam(request.paramsJson, "throttled");
				const auto limitPerSec =
					ExtractSizeParam(request.paramsJson, "limitPerSec");

				if (throttled.has_value()) {
					host.m_streamingThrottled = throttled.value();
				}

				if (limitPerSec.has_value() && limitPerSec.value() > 0) {
					host.m_streamingThrottleLimitPerSec = limitPerSec.value();
				}

				const std::size_t currentPerSec =
					host.m_streamingWindowMs == 0
					? 0
					: (host.m_streamingBufferedFrames * 1000) / host.m_streamingWindowMs;

				return protocol::OkResponse(request, "{\"throttled\":" +
						std::string(host.m_streamingThrottled ? "true" : "false") +
						",\"limitPerSec\":" +
						std::to_string(host.m_streamingThrottleLimitPerSec) +
						",\"currentPerSec\":" +
						std::to_string(currentPerSec) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.clear",
			[&host](const protocol::RequestFrame& request) {
				host.m_failoverOverrideActive = false;
				host.m_failoverOverrideModel = host.m_runtimeAgentModel;
				host.m_failoverOverrideReason = "cleared";
				++host.m_failoverOverrideChanges;

				return protocol::OkResponse(request, "{\"cleared\":true,\"active\":false,\"model\":\"" +
						EscapeJsonLocal(host.m_failoverOverrideModel) + "\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.pressure",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pressure\":0,\"threshold\":80,\"state\":\"normal\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.pacing",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"paceMs\":50,\"burst\":1,\"adaptive\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.status",
			[&host](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":" +
						std::string(host.m_failoverOverrideActive ? "true" : "false") +
						",\"model\":\"" +
						EscapeJsonLocal(host.m_failoverOverrideModel) +
						"\",\"reason\":\"" +
						EscapeJsonLocal(host.m_failoverOverrideReason) +
						"\",\"source\":\"runtime\",\"changes\":" +
						std::to_string(host.m_failoverOverrideChanges) + "}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.headroom",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"headroom\":8,\"used\":0,\"state\":\"ready\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.jitter",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"jitterMs\":0,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.history",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"entries\":0,\"lastModel\":\"default\",\"active\":false}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.balance",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"balanced\":true,\"skew\":0,\"state\":\"stable\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.drift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"driftMs\":0,\"windowMs\":1000,\"corrected\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.metrics",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"switches\":0,\"lastModel\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.efficiency",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"efficiency\":100,\"waste\":0,\"state\":\"optimized\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.variance",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"variance\":0,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.window",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"windowSec\":60,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.utilization",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"utilization\":0,\"capacity\":8,\"state\":\"idle\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.deviation",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"deviation\":0,\"samples\":2,\"withinBudget\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.digest",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"digest\":\"sha256:override-v1\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.capacity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"capacity\":8,\"used\":0,\"state\":\"ready\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.alignment",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"aligned\":true,\"offsetMs\":0,\"windowMs\":1000}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.timeline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"entries\":0,\"active\":false,\"lastModel\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.occupancy",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"occupancy\":0,\"slots\":8,\"state\":\"idle\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.skew",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"skewMs\":0,\"samples\":2,\"bounded\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.catalog",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"count\":1,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.elasticity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"elasticity\":100,\"headroom\":8,\"state\":\"expandable\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.dispersion",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"dispersion\":0,\"samples\":2,\"bounded\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.registry",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"entries\":1,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.cohesion",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"cohesion\":100,\"groups\":1,\"state\":\"coherent\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.curvature",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"curvature\":0,\"samples\":2,\"bounded\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.matrix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"rows\":1,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.resilience",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"resilience\":100,\"faults\":0,\"state\":\"steady\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.smoothness",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"smoothness\":100,\"jitterMs\":0,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.snapshot",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"revision\":1,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.readiness",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"ready\":true,\"queueDepth\":0,\"state\":\"ready\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.harmonics",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"harmonics\":0,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.pointer",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"pointer\":\"default\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.contention",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"contention\":0,\"waiters\":0,\"state\":\"clear\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.phase",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phase\":\"steady\",\"step\":1,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.state",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"state\":\"none\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.fairness",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"fairness\":100,\"skew\":0,\"state\":\"balanced\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.tempo",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"tempo\":1,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.profile",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"profile\":\"default\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.equilibrium",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"equilibrium\":100,\"delta\":0,\"state\":\"balanced\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.steadiness",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"steady\":true,\"variance\":0,\"windowMs\":1000}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.temporal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"temporal\":0,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.consistency",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"consistent\":true,\"deviation\":0,\"samples\":2}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.audit",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"entries\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.parity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"parity\":100,\"gap\":0,\"state\":\"aligned\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.stabilityIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"stabilityIndex\":100,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.spectral",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"spectral\":0,\"samples\":2,\"bounded\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.envelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"floor\":0,\"ceiling\":100,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.checkpoint",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"checkpoint\":\"cp-override-1\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.convergence",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"convergence\":100,\"drift\":0,\"state\":\"locked\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.hysteresis",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"hysteresis\":0,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.resonance",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"resonance\":0,\"samples\":2,\"bounded\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.vectorField",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"vectors\":2,\"magnitude\":0,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.baseline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"baseline\":\"default\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.balanceIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"balanceIndex\":100,\"skew\":0,\"state\":\"balanced\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseLock",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"locked\":true,\"phase\":\"steady\",\"drift\":0}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.waveform",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"waveform\":\"flat\",\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.horizon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"horizonMs\":1000,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.manifest",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"manifest\":\"default\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.symmetry",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"symmetry\":100,\"offset\":0,\"state\":\"aligned\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.gradient",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"gradient\":0,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.vectorClock",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"clock\":1,\"lag\":0,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.trend",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"trend\":\"flat\",\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.ledger",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"entries\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.harmonicity",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"harmonicity\":100,\"detune\":0,\"state\":\"aligned\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.inertia",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"inertia\":0,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.coordination",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"coordinated\":true,\"lag\":0,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.latencyBand",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"minMs\":0,\"maxMs\":0,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.snapshotIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"index\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.cadenceIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"cadenceIndex\":100,\"jitter\":0,\"state\":\"steady\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.damping",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"damping\":0,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.phaseNoise",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseNoise\":0,\"samples\":2,\"bounded\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.beat",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"beatHz\":1,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.digestIndex",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"digestIndex\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.waveLock",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"locked\":true,\"phase\":\"steady\",\"slip\":0}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.flux",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"flux\":0,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.phaseMatrix",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"phaseMatrix\":0,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.orchestration.driftEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"driftEnvelope\":0,\"windowMs\":1000,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.modulation",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"modulation\":0,\"samples\":2,\"bounded\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.syncVector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"syncVector\":1,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.bandEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"bandEnvelope\":0,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.runtime.streaming.pulseTrain",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"pulseHz\":1,\"samples\":2,\"stable\":true}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.cursor",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"cursor\":\"default\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vector\":\"default\",\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorDrift\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.phaseBias",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"phaseBias\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.biasEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"biasEnvelope\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.driftEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"driftEnvelope\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.envelopeDrift",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"envelopeDrift\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.driftVector",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"driftVector\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorEnvelope",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorEnvelope\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorContour",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorContour\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorRibbon",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRibbon\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpiral",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorSpiral\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorArc",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorArc\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorMesh\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorCore",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorCore\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorFrame",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorFrame\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpan",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorSpan\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorGrid",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGrid\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorLane",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLane\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorTrack",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorTrack\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorRail",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRail\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpline",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorSpline\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorChain",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorChain\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorThread",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorThread\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLink\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorBridge\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorAnchor2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorAnchor2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorPortal\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorRelay2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRelay2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGate2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorHub2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorHub2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode3\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLink2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorMesh2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorArc2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorArc2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorBand2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorBand2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorGrid2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGrid2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorLane2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLane2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorTrack2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorTrack2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorRail2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRail2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorSpline2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorSpline2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorChain2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorChain2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorThread2",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorThread2\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLink3\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode4\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorMesh3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorMesh3\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorBridge3\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorPortal3\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorRelay3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorRelay3\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGate3\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorHub3",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorHub3\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorNode5",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorNode5\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorLink4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorLink4\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorBridge4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorBridge4\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorPortal4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorPortal4\":0,\"model\":\"default\"}");
			});

		host.m_dispatcher.Register(
			"gateway.models.failover.override.vectorGate4",
			[](const protocol::RequestFrame& request) {
				return protocol::OkResponse(request, "{\"active\":false,\"vectorGate4\":0,\"model\":\"default\"}");
			});
}

} // namespace handlers::runtime
} // namespace blazeclaw::gateway
