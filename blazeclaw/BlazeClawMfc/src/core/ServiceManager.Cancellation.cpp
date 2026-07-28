#include "pch.h"
#include "ServiceManager.h"
#include "GatewayHostBindingCoordinator.h"
#include "ServiceLifecycleStartupCoordinator.h"
#include "SkillsAgentCommandDescriptorPolicy.h"
#include "SkillsGatewayPublicationCoordinator.h"
#include "../app/CredentialStore.h"
#include "../app/BlazeClawMFCDoc.h"
#include "../app/BlazeClawMFCView.h"
#include "../app/MainFrame.h"

#include "../config/ConfigLoader.h"
#include "../gateway/GatewayProtocolModels.h"
#include "../gateway/GatewayJsonUtils.h"
#include "../gateway/Telemetry.h"
#include "../gateway/executors/EmailScheduleExecutor.h"
#include "diagnostics/DiagnosticsSnapshot.h"
#include "diagnostics/DiagnosticsRegressionComparator.h"
#include "bootstrap/StartupFixtureValidator.h"
#include "filesystem/SafeOpenSync.h"
#include "SkillsFrontmatterCompat.h"
#include "tools/ToolArgumentValidators.h"
#include "tools/ToolProcessRunner.h"
#include "runtime/SpeechRecognition/SpeechRecognitionRuntime.h"
#include "ServiceManagerTextHelpers.h"
#include "ServiceManagerSkillRootsHelpers.h"
#include "ServiceManagerRoutingIntentHelpers.h"
#include "ServiceManagerLifecycleHelpers.h"
#include "ServiceManagerLocalModelHelpers.h"
#include "ServiceManagerSpeechRuntimeHelpers.h"
#include "ServiceManagerTextToSpeechHelpers.h"
#include "ServiceManagerSnapshotHelpers.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <Windows.h>
#include <nlohmann/json.hpp>

namespace blazeclaw::core {
	void EmitDeepSeekDiagnostic(
		const char* stage,
		const std::string& detail) {
		const std::string safeStage =
			(stage == nullptr || std::string(stage).empty())
			? "unknown"
			: std::string(stage);
		TRACE(
			"[DeepSeek][%s] %s\n",
			safeStage.c_str(),
			detail.c_str());
	}

	bool ServiceManager::IsDeepSeekRunCancelled(const std::string& runId) const {
		std::scoped_lock lock(m_deepSeekCancelMutex);
		const auto it = m_deepSeekCancelledRuns.find(runId);
		return it != m_deepSeekCancelledRuns.end() && it->second;
	}

	void ServiceManager::MarkDeepSeekRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		EmitDeepSeekDiagnostic(
			"cancel",
			std::string("mark cancelled runId=") + runId);

		std::scoped_lock lock(m_deepSeekCancelMutex);
		m_deepSeekCancelledRuns.insert_or_assign(runId, true);
	}

	void ServiceManager::ClearDeepSeekRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_deepSeekCancelMutex);
		m_deepSeekCancelledRuns.erase(runId);
	}

	bool ServiceManager::IsEmbeddedRunCancelled(const std::string& runId) const {
		std::scoped_lock lock(m_embeddedCancelMutex);
		const auto it = m_embeddedCancelledRuns.find(runId);
		return it != m_embeddedCancelledRuns.end() && it->second;
	}

	void ServiceManager::MarkEmbeddedRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_embeddedCancelMutex);
		m_embeddedCancelledRuns.insert_or_assign(runId, true);
	}

	void ServiceManager::ClearEmbeddedRunCancelled(const std::string& runId) {
		if (runId.empty()) {
			return;
		}

		std::scoped_lock lock(m_embeddedCancelMutex);
		m_embeddedCancelledRuns.erase(runId);
	}

} // namespace blazeclaw::core

