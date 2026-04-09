#include "pch.h"
#include "HooksGovernanceEmitter.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace blazeclaw::core {

	namespace {

		std::wstring BuildGovernanceReportJson(
			const HookExecutionSnapshot& execution,
			const std::vector<std::wstring>& allowedPackages,
			const bool strictMode)
		{
			std::wstringstream builder;
			builder << L"{\"dispatchCount\":" << execution.diagnostics.dispatchCount
				<< L",\"successCount\":" << execution.diagnostics.successCount
				<< L",\"failureCount\":" << execution.diagnostics.failureCount
				<< L",\"skippedCount\":" << execution.diagnostics.skippedCount
				<< L",\"policyBlockedCount\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"driftDetectedCount\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"lastDriftReason\":\""
				<< execution.diagnostics.lastDriftReason
				<< L"\",\"strictMode\":" << (strictMode ? L"true" : L"false")
				<< L",\"allowedPackages\":[";

			for (std::size_t i = 0; i < allowedPackages.size(); ++i)
			{
				if (i > 0)
				{
					builder << L",";
				}

				builder << L"\"" << allowedPackages[i] << L"\"";
			}

			builder << L"]}";
			return builder.str();
		}

		bool WriteGovernanceReportFile(
			const std::filesystem::path& reportFile,
			const std::wstring& content)
		{
			std::ofstream output(reportFile, std::ios::binary | std::ios::trunc);
			if (!output.is_open())
			{
				return false;
			}

			std::string narrow;
			narrow.reserve(content.size());
			for (const auto ch : content)
			{
				narrow.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}

			output.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
			return output.good();
		}

		std::wstring BuildComplianceAttestationJson(
			const std::wstring& tenantId,
			const std::wstring& sloStatus,
			const HookExecutionSnapshot& execution,
			const std::wstring& telemetryPath,
			const std::wstring& auditPath)
		{
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"sloStatus\":\"" << sloStatus
				<< L"\",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"telemetryPath\":\"" << telemetryPath
				<< L"\",\"auditPath\":\"" << auditPath
				<< L"\"}";
			return builder.str();
		}

		std::wstring BuildCrossTenantAttestationAggregationJson(
			const std::wstring& tenantId,
			const std::wstring& policyId,
			const std::wstring& sloStatus,
			const std::wstring& attestationPath,
			const HookExecutionSnapshot& execution,
			const std::uint64_t aggregationCount)
		{
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"policyId\":\"" << policyId
				<< L"\",\"sloStatus\":\"" << sloStatus
				<< L"\",\"attestationPath\":\"" << attestationPath
				<< L"\",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"aggregationSequence\":"
				<< (aggregationCount + 1)
				<< L"}";
			return builder.str();
		}

		std::wstring BuildRemediationPlaybookJson(
			const std::wstring& tenantId,
			const HookExecutionSnapshot& execution,
			const std::wstring& reportPath,
			const std::uint32_t tokenMaxAgeMinutes)
		{
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"lastDriftReason\":\""
				<< execution.diagnostics.lastDriftReason
				<< L"\",\"sourceReportPath\":\""
				<< reportPath
				<< L"\",\"tokenMaxAgeMinutes\":"
				<< tokenMaxAgeMinutes
				<< L",\"recommendedSteps\":[\"validate_approval_token\",\"review_policy_drift\",\"execute_remediation_with_gate\"]}";
			return builder.str();
		}

		std::wstring BuildRemediationTelemetryJson(
			const std::wstring& tenantId,
			const std::wstring& playbookPath,
			const HookExecutionSnapshot& execution,
			const std::wstring& remediationStatus)
		{
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"playbookPath\":\"" << playbookPath
				<< L"\",\"status\":\"" << remediationStatus
				<< L"\",\"policyBlocked\":"
				<< execution.diagnostics.policyBlockedCount
				<< L",\"driftDetected\":"
				<< execution.diagnostics.driftDetectedCount
				<< L",\"lastDriftReason\":\""
				<< execution.diagnostics.lastDriftReason
				<< L"\"}";
			return builder.str();
		}

		std::wstring BuildRemediationAuditJson(
			const std::wstring& tenantId,
			const std::wstring& approvalToken,
			const std::wstring& remediationStatus,
			const std::wstring& reportPath,
			const std::wstring& playbookPath,
			const std::uint64_t tokenRotations)
		{
			std::wstringstream builder;
			builder << L"{\"tenantId\":\"" << tenantId
				<< L"\",\"approvalTokenConfigured\":"
				<< (!approvalToken.empty() ? L"true" : L"false")
				<< L",\"status\":\"" << remediationStatus
				<< L"\",\"reportPath\":\"" << reportPath
				<< L"\",\"playbookPath\":\"" << playbookPath
				<< L"\",\"tokenRotations\":" << tokenRotations
				<< L"}";
			return builder.str();
		}

		std::wstring WriteLifecycleArtifact(
			const std::filesystem::path& outputDir,
			const std::wstring& filePrefix,
			const std::wstring& tenantId,
			const std::wstring& content,
			const std::wstring& failureLabel,
			std::vector<std::wstring>& inOutWarnings)
		{
			std::error_code ec;
			auto fullDir = outputDir;
			if (fullDir.is_relative())
			{
				fullDir = std::filesystem::current_path(ec) / fullDir;
			}
			std::filesystem::create_directories(fullDir, ec);
			if (ec)
			{
				inOutWarnings.push_back(failureLabel + L": cannot create directory.");
				return {};
			}

			const auto nonce = std::to_wstring(
				static_cast<std::uint64_t>(
					std::chrono::system_clock::now().time_since_epoch().count()));
			const auto path = fullDir /
				(filePrefix + L"-" + tenantId + L"-" + nonce + L".json");
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			if (!output.is_open())
			{
				inOutWarnings.push_back(failureLabel + L": cannot write file.");
				return {};
			}

			std::string narrow;
			narrow.reserve(content.size());
			for (const auto ch : content)
			{
				narrow.push_back(static_cast<char>(ch <= 0x7F ? ch : '?'));
			}
			output.write(narrow.c_str(), static_cast<std::streamsize>(narrow.size()));
			if (!output.good())
			{
				inOutWarnings.push_back(failureLabel + L": cannot flush file.");
				return {};
			}

			return path.wstring();
		}

		std::wstring EvaluateRemediationSloStatus(
			const HookExecutionSnapshot& execution,
			const std::uint32_t maxDriftDetected,
			const std::uint32_t maxPolicyBlocked)
		{
			if (execution.diagnostics.driftDetectedCount > maxDriftDetected)
			{
				return L"breach_drift";
			}

			if (execution.diagnostics.policyBlockedCount > maxPolicyBlocked)
			{
				return L"breach_policy_blocked";
			}

			return L"healthy";
		}

		void EmitTenantRemediationPlaybookIfNeeded(
			const HookExecutionSnapshot& execution,
			const bool autoRemediationEnabled,
			const std::wstring& tenantId,
			const std::filesystem::path& playbookDir,
			const std::wstring& sourceReportPath,
			const std::uint32_t tokenMaxAgeMinutes,
			std::wstring& outPlaybookPath,
			std::vector<std::wstring>& inOutWarnings)
		{
			outPlaybookPath.clear();
			if (!autoRemediationEnabled)
			{
				return;
			}

			if (execution.diagnostics.policyBlockedCount == 0 &&
				execution.diagnostics.driftDetectedCount == 0)
			{
				return;
			}

			const auto content = BuildRemediationPlaybookJson(
				tenantId,
				execution,
				sourceReportPath,
				tokenMaxAgeMinutes);
			outPlaybookPath = WriteLifecycleArtifact(
				playbookDir,
				L"hooks-remediation",
				tenantId,
				content,
				L"hooks-remediation playbook generation failed",
				inOutWarnings);
		}

		void EmitRemediationTelemetryAndAuditIfNeeded(
			const HookExecutionSnapshot& execution,
			const bool telemetryEnabled,
			const std::filesystem::path& telemetryDir,
			const bool auditEnabled,
			const std::filesystem::path& auditDir,
			const std::wstring& tenantId,
			const std::wstring& approvalToken,
			const std::wstring& reportPath,
			const std::wstring& playbookPath,
			const std::wstring& remediationStatus,
			const std::uint64_t tokenRotations,
			std::wstring& outTelemetryPath,
			std::wstring& outAuditPath,
			std::vector<std::wstring>& inOutWarnings)
		{
			outTelemetryPath.clear();
			outAuditPath.clear();

			if (execution.diagnostics.policyBlockedCount == 0 &&
				execution.diagnostics.driftDetectedCount == 0)
			{
				return;
			}

			if (telemetryEnabled)
			{
				const auto telemetryContent = BuildRemediationTelemetryJson(
					tenantId,
					playbookPath,
					execution,
					remediationStatus);
				outTelemetryPath = WriteLifecycleArtifact(
					telemetryDir,
					L"hooks-remediation-telemetry",
					tenantId,
					telemetryContent,
					L"hooks-remediation telemetry emission failed",
					inOutWarnings);
			}

			if (auditEnabled)
			{
				const auto auditContent = BuildRemediationAuditJson(
					tenantId,
					approvalToken,
					remediationStatus,
					reportPath,
					playbookPath,
					tokenRotations);
				outAuditPath = WriteLifecycleArtifact(
					auditDir,
					L"hooks-remediation-audit",
					tenantId,
					auditContent,
					L"hooks-remediation audit emission failed",
					inOutWarnings);
			}
		}

		void EmitComplianceAttestationIfNeeded(
			const bool enabled,
			const std::filesystem::path& attestationDir,
			const std::wstring& tenantId,
			const std::wstring& sloStatus,
			const HookExecutionSnapshot& execution,
			const std::wstring& telemetryPath,
			const std::wstring& auditPath,
			std::wstring& outAttestationPath,
			std::vector<std::wstring>& inOutWarnings)
		{
			outAttestationPath.clear();
			if (!enabled)
			{
				return;
			}

			if (execution.diagnostics.policyBlockedCount == 0 &&
				execution.diagnostics.driftDetectedCount == 0)
			{
				return;
			}

			const auto content = BuildComplianceAttestationJson(
				tenantId,
				sloStatus,
				execution,
				telemetryPath,
				auditPath);
			outAttestationPath = WriteLifecycleArtifact(
				attestationDir,
				L"hooks-remediation-attestation",
				tenantId,
				content,
				L"hooks-remediation compliance attestation emission failed",
				inOutWarnings);
		}

		void EmitCrossTenantAttestationAggregationIfNeeded(
			const bool enabled,
			const std::filesystem::path& aggregationDir,
			const std::wstring& tenantId,
			const std::wstring& policyId,
			const std::wstring& sloStatus,
			const std::wstring& attestationPath,
			const HookExecutionSnapshot& execution,
			std::uint64_t& inOutAggregationCount,
			std::wstring& outAggregationStatus,
			std::wstring& outAggregationPath,
			std::vector<std::wstring>& inOutWarnings)
		{
			outAggregationPath.clear();
			if (!enabled)
			{
				outAggregationStatus = L"aggregation_disabled";
				return;
			}

			if (attestationPath.empty())
			{
				outAggregationStatus = L"attestation_missing";
				return;
			}

			const auto content = BuildCrossTenantAttestationAggregationJson(
				tenantId,
				policyId,
				sloStatus,
				attestationPath,
				execution,
				inOutAggregationCount);
			const auto path = WriteLifecycleArtifact(
				aggregationDir,
				L"hooks-attestation-aggregation",
				tenantId,
				content,
				L"hooks-remediation cross-tenant aggregation emission failed",
				inOutWarnings);
			if (path.empty())
			{
				outAggregationStatus = L"aggregation_emit_failed";
				return;
			}

			++inOutAggregationCount;
			outAggregationStatus = L"aggregation_emitted";
			outAggregationPath = path;
		}

	} // namespace

	void HooksGovernanceEmitter::EmitGovernanceReportIfNeeded(
		const GovernanceContext& context,
		std::vector<std::wstring>& inOutWarnings) const
	{
		if (!context.governanceReportingEnabled)
		{
			return;
		}

		if (context.execution.diagnostics.policyBlockedCount == 0 &&
			context.execution.diagnostics.driftDetectedCount == 0)
		{
			return;
		}

		std::error_code ec;
		auto fullDir = context.governanceReportDir;
		if (fullDir.is_relative())
		{
			fullDir = std::filesystem::current_path(ec) / fullDir;
		}

		std::filesystem::create_directories(fullDir, ec);
		if (ec)
		{
			inOutWarnings.push_back(
				L"hooks-governance report generation failed: cannot create report directory.");
			return;
		}

		const auto nonce = std::to_wstring(
			static_cast<std::uint64_t>(
				std::chrono::system_clock::now().time_since_epoch().count()));
		const auto reportFile = fullDir / (L"hooks-governance-" + nonce + L".json");
		const auto reportContent = BuildGovernanceReportJson(
			context.execution,
			context.allowedPackages,
			context.strictPolicyEnforcement);
		if (!WriteGovernanceReportFile(reportFile, reportContent))
		{
			inOutWarnings.push_back(
				L"hooks-governance report generation failed: cannot write report file.");
			return;
		}

		++context.governanceReportsGenerated;
		context.lastGovernanceReportPath = reportFile.wstring();
	}

	void HooksGovernanceEmitter::EmitRemediationLifecycleIfNeeded(
		const RemediationContext& context,
		std::vector<std::wstring>& inOutWarnings) const
	{
		EmitTenantRemediationPlaybookIfNeeded(
			context.execution,
			context.autoRemediationEnabled,
			context.autoRemediationTenantId,
			context.autoRemediationPlaybookDir,
			context.lastGovernanceReportPath,
			context.autoRemediationTokenMaxAgeMinutes,
			context.lastAutoRemediationPlaybookPath,
			inOutWarnings);

		if (!context.lastAutoRemediationPlaybookPath.empty())
		{
			context.lastAutoRemediationStatus = L"playbook_generated";
		}

		EmitRemediationTelemetryAndAuditIfNeeded(
			context.execution,
			context.remediationTelemetryEnabled,
			context.remediationTelemetryDir,
			context.remediationAuditEnabled,
			context.remediationAuditDir,
			context.autoRemediationTenantId,
			context.autoRemediationApprovalToken,
			context.lastGovernanceReportPath,
			context.lastAutoRemediationPlaybookPath,
			context.lastAutoRemediationStatus,
			context.autoRemediationTokenRotations,
			context.lastRemediationTelemetryPath,
			context.lastRemediationAuditPath,
			inOutWarnings);

		context.remediationSloStatus = EvaluateRemediationSloStatus(
			context.execution,
			context.remediationSloMaxDriftDetected,
			context.remediationSloMaxPolicyBlocked);

		EmitComplianceAttestationIfNeeded(
			context.complianceAttestationEnabled,
			context.complianceAttestationDir,
			context.autoRemediationTenantId,
			context.remediationSloStatus,
			context.execution,
			context.lastRemediationTelemetryPath,
			context.lastRemediationAuditPath,
			context.lastComplianceAttestationPath,
			inOutWarnings);

		EmitCrossTenantAttestationAggregationIfNeeded(
			context.crossTenantAttestationAggregationEnabled,
			context.crossTenantAttestationAggregationDir,
			context.autoRemediationTenantId,
			context.enterpriseSlaPolicyId,
			context.remediationSloStatus,
			context.lastComplianceAttestationPath,
			context.execution,
			context.crossTenantAttestationAggregationCount,
			context.crossTenantAttestationAggregationStatus,
			context.lastCrossTenantAttestationAggregationPath,
			inOutWarnings);
	}

} // namespace blazeclaw::core
