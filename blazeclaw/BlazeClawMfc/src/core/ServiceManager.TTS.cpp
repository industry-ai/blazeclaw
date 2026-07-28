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

	// (TTS facade forwarding implementations are defined later in this TU.)
	namespace {

		std::wstring Trim(const std::wstring& value) {
			const auto first = std::find_if_not(
				value.begin(),
				value.end(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				});
			const auto last = std::find_if_not(
				value.rbegin(),
				value.rend(),
				[](const wchar_t ch) {
					return std::iswspace(ch) != 0;
				}).base();

			if (first >= last) {
				return {};
			}

			return std::wstring(first, last);
		}

		std::wstring ToLower(const std::wstring& value) {
			std::wstring lowered = value;
			std::transform(
				lowered.begin(),
				lowered.end(),
				lowered.begin(),
				[](const wchar_t ch) {
					return static_cast<wchar_t>(std::towlower(ch));
				});
			return lowered;
		}

		std::wstring Utf8ToWideLocal(const std::string& value) {
			return servicemanager_text::Utf8ToWideLocal(value);
		}

		std::string WideToUtf8Local(const std::wstring& value) {
			if (value.empty()) {
				return {};
			}

			const int required = WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				nullptr,
				0,
				nullptr,
				nullptr);
			if (required <= 0) {
				return {};
			}

			std::string output(static_cast<std::size_t>(required), '\0');
			WideCharToMultiByte(
				CP_UTF8,
				0,
				value.c_str(),
				static_cast<int>(value.size()),
				output.data(),
				required,
				nullptr,
				nullptr);
			return output;
		}

	} // namespace

	// ServiceManager TTS facade forwarding implementations (placed after
	// anonymous helpers to avoid symbol/namespace collisions)

	bool ServiceManager::TextToSpeechEnabled() const noexcept {
		return servicemanager_tts::TextToSpeechEnabled();
	}

	std::string ServiceManager::StartTextToSpeech(
		const std::string& text,
		const std::string& provider,
		const std::string& model,
		const std::string& voice,
		const std::string& runId) {
		auto snapshot = servicemanager_tts::StartTextToSpeechState(
			m_running,
			provider,
			model,
			voice,
			runId);
		(void)snapshot;
		return servicemanager_tts::StartTextToSpeech(text, voice, model);
	}

	void ServiceManager::StopTextToSpeech(const std::string& utteranceId) {
		auto snapshot = servicemanager_tts::StopTextToSpeechState(utteranceId);
		(void)snapshot;
		servicemanager_tts::StopTextToSpeech(utteranceId);
	}

	texttospeech::TextToSpeechRuntimeSnapshot ServiceManager::CollectTextToSpeechSnapshot() const noexcept {
		return servicemanager_tts::CollectTextToSpeechSnapshot();
	}

	bool ServiceManager::ApplySpeechRecognitionConfigReload(
		const bool speechEnabled,
		const std::wstring& speechProvider,
		const std::wstring& speechStorageRoot,
		const std::wstring& speechActiveModelId,
		const std::wstring& speechModelPath,
		std::string* outStatusMessage) {

		// Forward orchestration into the speech runtime helper to shrink
		// ServiceManager.cpp while preserving original behavior.
		m_activeConfig.speechRecognition.enabled = speechEnabled;
		m_activeConfig.speechRecognition.provider = speechProvider;
		m_activeConfig.speechRecognition.storageRoot = speechStorageRoot;
		m_activeConfig.speechRecognition.activeModelId = speechActiveModelId;
		m_activeConfig.speechRecognition.modelPath = speechModelPath;

		return servicemanager_speech::ApplySpeechRecognitionConfigReload(
			m_running,
			m_activeConfig,
			m_speechTranscriptionCoordinator,
			m_speechRecognitionRuntime,
			m_speechRecognition,
			outStatusMessage);
	}

} // namespace blazeclaw::core

