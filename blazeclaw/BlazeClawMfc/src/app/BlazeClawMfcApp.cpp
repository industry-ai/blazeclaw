#include "pch.h"
#include "framework.h"
#include "afxwinappex.h"
#include "afxdialogex.h"
#include "BlazeClawMFCApp.h"

#include "MainFrame.h"

#include "ChildFrm.h"
#include "BlazeClawMFCDoc.h"
#include "BlazeClawMFCView.h"
#include "ChatView.h"
#include "BlazeClawMarkdownView.h"
#include "SharedTabsDocTemplate.h"
#include "SharedDocWebViewChildFrame.h"
#include "SharedDocMarkdownChildFrame.h"

#include "../core/runtime/LocalModel/TokenizerBridge.h"
#include "../core/runtime/SpeechRecognition/SpeechRecognitionRuntime.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <exception>
#include <set>
#include <unordered_set>
#include <vector>
#include <Windows.h>
#include <TlHelp32.h>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace {
	constexpr wchar_t kConfigPath[] = L"blazeclaw.conf";

	std::wstring ToWide(const std::string& value) {
		std::wstring output;
		output.reserve(value.size());

		for (const char ch : value) {
			output.push_back(static_cast<wchar_t>(
				static_cast<unsigned char>(ch)));
		}

		return output;
	}

	std::wstring JoinValues(const std::vector<std::wstring>& values) {
		if (values.empty()) {
			return L"none";
		}

		std::wstring output;
		for (const auto& value : values) {
			if (value.empty()) {
				continue;
			}
			if (!output.empty()) {
				output += L",";
			}
			output += value;
		}

		return output.empty() ? L"none" : output;
	}

	std::wstring JoinUtf8Values(const std::vector<std::string>& values) {
		if (values.empty()) {
			return L"none";
		}

		std::vector<std::wstring> converted;
		converted.reserve(values.size());
		for (const auto& value : values) {
			converted.push_back(ToWide(value));
		}
		return JoinValues(converted);
	}

	std::wstring Trim(const std::wstring& value) {
		const auto first = std::find_if_not(
			value.begin(),
			value.end(),
			[](const wchar_t ch) { return std::iswspace(ch) != 0; });
		const auto last = std::find_if_not(
			value.rbegin(),
			value.rend(),
			[](const wchar_t ch) { return std::iswspace(ch) != 0; }).base();
		if (first >= last) {
			return {};
		}

		return std::wstring(first, last);
	}

	bool HasCommandLineSwitch(const wchar_t* value) {
		if (value == nullptr || *value == L'\0') {
			return false;
		}

		for (int idx = 1; idx < __argc; ++idx) {
			if (_wcsicmp(__wargv[idx], value) == 0) {
				return true;
			}
		}

		return false;
	}

	std::vector<std::wstring> CollectCommandLineValuesWithPrefix(
		const std::wstring& prefix) {
		std::vector<std::wstring> values;
		for (int idx = 1; idx < __argc; ++idx) {
			const std::wstring arg = __wargv[idx] == nullptr
				? std::wstring()
				: std::wstring(__wargv[idx]);
			if (arg.rfind(prefix, 0) != 0) {
				continue;
			}

			const std::wstring value = Trim(arg.substr(prefix.size()));
			if (!value.empty()) {
				values.push_back(value);
			}
		}

		return values;
	}

	std::optional<int> TryRunOfflineSttOptimizationCommand(
		const blazeclaw::config::AppConfig& config) {
		if (!HasCommandLineSwitch(L"--stt-optimize-offline")) {
			return std::nullopt;
		}

		const auto explicitRoots = CollectCommandLineValuesWithPrefix(
			L"--stt-model-root=");
		const auto optimizationResult =
			blazeclaw::core::speechrecognition::OptimizeSpeechRecognitionModelsOffline(
				config.speechRecognition,
				explicitRoots);

		TRACE(
			"[Startup][speech.offline.optimize.summary] %S optimizedRoots=%S failedRoots=%S\n",
			optimizationResult.summary.c_str(),
			JoinUtf8Values(optimizationResult.optimizedRoots).c_str(),
			JoinUtf8Values(optimizationResult.failedRoots).c_str());

		if (!optimizationResult.success) {
			TRACE(
				"[Startup][speech.offline.optimize.failed] summary=%S\n",
				optimizationResult.summary.c_str());
			return 1;
		}

		TRACE(
			"[Startup][speech.offline.optimize.completed] summary=%S\n",
			optimizationResult.summary.c_str());
		return 0;
	}

	void UpsertConfigEntry(
		std::vector<std::wstring>& lines,
		const std::wstring& key,
		const std::wstring& value) {
		const std::wstring prefix = key + L"=";
		for (std::wstring& line : lines) {
			if (Trim(line).rfind(prefix, 0) == 0) {
				line = prefix + value;
				return;
			}
		}

		lines.push_back(prefix + value);
	}

	void PersistActiveChatConnection(
		const blazeclaw::core::ServiceManager& services) {
		std::vector<std::wstring> lines;
		{
			std::wifstream input(kConfigPath);
			std::wstring line;
			while (std::getline(input, line)) {
				lines.push_back(line);
			}
		}

		const std::wstring provider = ToWide(services.ActiveChatProvider());
		const std::wstring model = ToWide(services.ActiveChatModel());
		UpsertConfigEntry(
			lines,
			L"chat.activeProvider",
			provider.empty() ? L"local" : provider);
		UpsertConfigEntry(
			lines,
			L"chat.activeModel",
			model.empty() ? L"default" : model);

		std::wofstream output(kConfigPath, std::ios::trunc);
		if (!output.is_open()) {
			return;
		}

		for (const auto& line : lines) {
			output << line << L"\n";
		}
	}

	void AppendMainFrameStatusLine(const CString& line) {
		auto* mainFrame = dynamic_cast<CMainFrame*>(AfxGetMainWnd());
		if (mainFrame == nullptr) {
			return;
		}

		mainFrame->AddChatStatusLine(line);
	}

	std::wstring ToLowerInvariant(std::wstring value) {
		std::transform(
			value.begin(),
			value.end(),
			value.begin(),
			[](const wchar_t ch) {
				return static_cast<wchar_t>(std::towlower(ch));
			});
		return value;
	}

	bool IsTrackedSpeechCudaModule(const std::wstring& moduleNameLower) {
		static const std::unordered_set<std::wstring> kExactModules = {
			L"onnxruntime_providers_cuda.dll",
			L"cudnn64_9.dll",
			L"cudnn_graph64_9.dll",
			L"cudnn_engines_precompiled64_9.dll",
			L"cudnn_engines_runtime_compiled64_9.dll",
		};

		if (kExactModules.find(moduleNameLower) != kExactModules.end()) {
			return true;
		}

		return moduleNameLower.rfind(L"cublas64_", 0) == 0 ||
			moduleNameLower.rfind(L"cublaslt64_", 0) == 0 ||
			moduleNameLower.rfind(L"cufft64_", 0) == 0;
	}

	std::wstring ExtractDigitsAfterPrefix(
		const std::wstring& moduleNameLower,
		const std::wstring& prefix) {
		if (moduleNameLower.rfind(prefix, 0) != 0) {
			return {};
		}

		std::size_t cursor = prefix.size();
		const std::size_t start = cursor;
		while (cursor < moduleNameLower.size() &&
			std::iswdigit(moduleNameLower[cursor])) {
			++cursor;
		}

		if (cursor == start) {
			return {};
		}

		return moduleNameLower.substr(start, cursor - start);
	}

	std::wstring DetectVersionFamilyTag(const std::wstring& moduleNameLower) {
		const std::wstring cublas = ExtractDigitsAfterPrefix(moduleNameLower, L"cublas64_");
		if (!cublas.empty()) {
			return L"cublas:" + cublas;
		}

		const std::wstring cublasLt = ExtractDigitsAfterPrefix(moduleNameLower, L"cublaslt64_");
		if (!cublasLt.empty()) {
			return L"cublaslt:" + cublasLt;
		}

		const std::wstring cufft = ExtractDigitsAfterPrefix(moduleNameLower, L"cufft64_");
		if (!cufft.empty()) {
			return L"cufft:" + cufft;
		}

		const std::wstring cudnn = ExtractDigitsAfterPrefix(moduleNameLower, L"cudnn64_");
		if (!cudnn.empty()) {
			return L"cudnn:" + cudnn;
		}

		const std::wstring cudnnGraph = ExtractDigitsAfterPrefix(moduleNameLower, L"cudnn_graph64_");
		if (!cudnnGraph.empty()) {
			return L"cudnn_graph:" + cudnnGraph;
		}

		const std::wstring cudnnEnginePrecompiled =
			ExtractDigitsAfterPrefix(moduleNameLower, L"cudnn_engines_precompiled64_");
		if (!cudnnEnginePrecompiled.empty()) {
			return L"cudnn_engines_precompiled:" + cudnnEnginePrecompiled;
		}

		const std::wstring cudnnEngineRuntimeCompiled =
			ExtractDigitsAfterPrefix(moduleNameLower, L"cudnn_engines_runtime_compiled64_");
		if (!cudnnEngineRuntimeCompiled.empty()) {
			return L"cudnn_engines_runtime_compiled:" + cudnnEngineRuntimeCompiled;
		}

		return {};
	}

	std::wstring JoinValues(const std::set<std::wstring>& values) {
		if (values.empty()) {
			return L"none";
		}

		std::wstring output;
		for (const auto& value : values) {
			if (!output.empty()) {
				output += L",";
			}
			output += value;
		}

		return output;
	}

	void AppendSpeechCudaModuleInventoryStatus() {
		const HANDLE snapshot = CreateToolhelp32Snapshot(
			TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
			GetCurrentProcessId());
		if (snapshot == INVALID_HANDLE_VALUE) {
			AppendMainFrameStatusLine(
				L"[Speech] startup.runtime.cuda.modules - unavailable (module snapshot failed)");
			return;
		}

		MODULEENTRY32W moduleEntry{};
		moduleEntry.dwSize = sizeof(moduleEntry);
		std::vector<std::pair<std::wstring, std::wstring>> trackedModules;
		if (Module32FirstW(snapshot, &moduleEntry)) {
			do {
				const std::wstring moduleNameLower = ToLowerInvariant(moduleEntry.szModule);
				if (!IsTrackedSpeechCudaModule(moduleNameLower)) {
					continue;
				}

				trackedModules.emplace_back(moduleNameLower, moduleEntry.szExePath);
			} while (Module32NextW(snapshot, &moduleEntry));
		}

		CloseHandle(snapshot);

		if (trackedModules.empty()) {
			AppendMainFrameStatusLine(
				L"[Speech] startup.runtime.cuda.modules - none of the tracked CUDA/cuDNN runtime DLLs are currently loaded");
			return;
		}

		std::set<std::wstring> moduleRoots;
		std::set<std::wstring> versionFamilies;
		std::set<std::wstring> versionMajors;
		for (const auto& module : trackedModules) {
			const std::filesystem::path modulePath = module.second;
			const std::filesystem::path rootPath = modulePath.parent_path().lexically_normal();
			if (!rootPath.empty()) {
				moduleRoots.insert(ToLowerInvariant(rootPath.wstring()));
			}

			const std::wstring versionFamily = DetectVersionFamilyTag(module.first);
			if (!versionFamily.empty()) {
				versionFamilies.insert(versionFamily);
				const std::size_t separator = versionFamily.find(L':');
				if (separator != std::wstring::npos && separator + 1 < versionFamily.size()) {
					versionMajors.insert(versionFamily.substr(separator + 1));
				}
			}
		}

		std::sort(trackedModules.begin(), trackedModules.end());
		for (const auto& module : trackedModules) {
			CString moduleLine;
			moduleLine.Format(
				L"[Speech] startup.runtime.cuda.module - name=%s path=%s",
				module.first.c_str(),
				module.second.c_str());
			AppendMainFrameStatusLine(moduleLine);
		}

		const bool mixedRoots = moduleRoots.size() > 1;
		const bool mixedVersions = versionMajors.size() > 1;
		const wchar_t* alignmentStatus = (mixedRoots || mixedVersions)
			? L"mixed"
			: L"aligned";
		CString alignmentLine;
		alignmentLine.Format(
			L"[Speech] startup.runtime.cuda.alignment - status=%s moduleCount=%llu rootCount=%llu versionMajorCount=%llu",
			alignmentStatus,
			static_cast<unsigned long long>(trackedModules.size()),
			static_cast<unsigned long long>(moduleRoots.size()),
			static_cast<unsigned long long>(versionMajors.size()));
		AppendMainFrameStatusLine(alignmentLine);

		CString alignmentDetailsLine;
		alignmentDetailsLine.Format(
			L"[Speech] startup.runtime.cuda.alignment.details - roots=%s versionFamilies=%s versionMajors=%s",
			JoinValues(moduleRoots).c_str(),
			JoinValues(versionFamilies).c_str(),
			JoinValues(versionMajors).c_str());
		AppendMainFrameStatusLine(alignmentDetailsLine);

		if (mixedRoots || mixedVersions) {
			AppendMainFrameStatusLine(
				L"[Speech] startup.runtime.cuda.alignment.recommendation - mixed CUDA/cuDNN runtime stack detected; align DLL roots/versions before enabling speech CUDA in production");
		}
	}

	std::filesystem::path ResolveStartupTracePath() {
		wchar_t tempPath[MAX_PATH]{};
		const DWORD tempLength = GetTempPathW(MAX_PATH, tempPath);
		if (tempLength > 0 && tempLength < MAX_PATH) {
			return std::filesystem::path(tempPath) / L"BlazeClaw.startup.trace.log";
		}

		return std::filesystem::current_path() / L"BlazeClaw.startup.trace.log";
	}

	void AppendStartupCheckpoint(const std::wstring& stage) {
		std::wofstream output(ResolveStartupTracePath(), std::ios::app);
		if (!output.is_open()) {
			return;
		}

		output
			<< L"pid=" << static_cast<unsigned long>(GetCurrentProcessId())
			<< L" tick=" << static_cast<unsigned long long>(GetTickCount64())
			<< L" stage=" << stage
			<< L"\n";
	}

	void AppendStartupConfigStatus(const blazeclaw::config::AppConfig& config) {
		const auto absoluteConfigPath =
			std::filesystem::absolute(std::filesystem::path(kConfigPath));
		const auto repoRootConfigPath =
			std::filesystem::current_path() / L"blazeclaw.conf";

		wchar_t modulePath[MAX_PATH]{};
		const DWORD moduleLength = GetModuleFileNameW(
			nullptr,
			modulePath,
			MAX_PATH);
		const std::wstring executablePath = moduleLength > 0
			? std::wstring(modulePath, modulePath + moduleLength)
			: std::wstring(L"unknown");

		const std::wstring workingDirectory =
			std::filesystem::current_path().wstring();

		CString runtimeContextLine;
		runtimeContextLine.Format(
			L"[Chat] startup.runtime.context - exe=%s cwd=%s config=%s",
			executablePath.c_str(),
			workingDirectory.c_str(),
			absoluteConfigPath.c_str());
		AppendMainFrameStatusLine(runtimeContextLine);

		CString configPathLine;
		configPathLine.Format(
			L"[Chat] startup.config.path - %s",
			absoluteConfigPath.c_str());
		AppendMainFrameStatusLine(configPathLine);

		std::error_code configCompareEc;
		const bool sameConfigPath = std::filesystem::equivalent(
			absoluteConfigPath,
			repoRootConfigPath,
			configCompareEc);
		if (!sameConfigPath) {
			CString configMismatchLine;
			configMismatchLine.Format(
				L"[Chat] startup.config.path.warning - runtimeConfig=%s differsFromCwdDefault=%s (tooling should target runtime config path)",
				absoluteConfigPath.c_str(),
				repoRootConfigPath.c_str());
			AppendMainFrameStatusLine(configMismatchLine);
		}

		CString modeLine;
		modeLine.Format(
			L"[Chat] startup.config.mode - %s",
			config.chat.mode.c_str());
		AppendMainFrameStatusLine(modeLine);
	}

	void AppendStartupEmbeddingsStatus(
		const blazeclaw::config::AppConfig& config,
		const blazeclaw::core::ServiceManager& services) {
		if (!config.embeddings.enabled) {
			AppendMainFrameStatusLine(
				L"[Embeddings] startup.disabled - embeddings.enabled=false");
			return;
		}

		CString configLine;
		configLine.Format(
			L"[Embeddings] startup.config - provider=%s model=%s tokenizer=%s",
			config.embeddings.provider.c_str(),
			config.embeddings.modelPath.c_str(),
			config.embeddings.tokenizerPath.c_str());
		AppendMainFrameStatusLine(configLine);

		const std::string probeResult = services.InvokeGatewayMethod(
			"gateway.embeddings.generate",
			std::optional<std::string>(
				"{\"text\":\"startup-embedding-probe\"}"));

		if (probeResult.find("\"vector\":") != std::string::npos) {
			AppendMainFrameStatusLine(
				L"[Embeddings] startup.loaded - model probe succeeded");
			return;
		}

		const CString errorLine(
			(L"[Embeddings] startup.error - " + ToWide(probeResult)).c_str());
		AppendMainFrameStatusLine(errorLine);
	}

	void AppendStartupSpeechStatus(
		const blazeclaw::config::AppConfig& config,
		const blazeclaw::core::ServiceManager& services) {
		const auto runtime = services.SpeechRecognition();
		CString configLine;
		configLine.Format(
			L"[Speech] startup.config - enabled=%s cudaEnabled=%s provider=%s stage=%s storageRoot=%s model=%s modelVariant=%s language=%s allowedLanguages=%s enforceAllowedLanguages=%s sampleRate=%u chunkMs=%u overlapMs=%u streamingChunkMs=%u streamingLookbackMs=%u streamingLatencyProfile=%s streamingPreviewChunkMs=%u streamingPreviewLookbackMs=%u threads=%u mode=%s",
			config.speechRecognition.enabled ? L"true" : L"false",
			config.speechRecognition.cudaEnabled ? L"true" : L"false",
			config.speechRecognition.provider.c_str(),
			config.speechRecognition.rolloutStage.c_str(),
			config.speechRecognition.storageRoot.c_str(),
			config.speechRecognition.modelPath.c_str(),
			config.speechRecognition.modelVariant.c_str(),
			config.speechRecognition.language.c_str(),
			JoinValues(config.speechRecognition.allowedLanguages).c_str(),
			config.speechRecognition.enforceAllowedLanguages ? L"true" : L"false",
			config.speechRecognition.sampleRate,
			config.speechRecognition.chunkMs,
			config.speechRecognition.overlapMs,
			config.speechRecognition.streamingChunkMs,
			config.speechRecognition.streamingLookbackMs,
			config.speechRecognition.streamingLatencyProfile.c_str(),
			config.speechRecognition.streamingPreviewChunkMs,
			config.speechRecognition.streamingPreviewLookbackMs,
			config.speechRecognition.threads,
			config.speechRecognition.executionMode.c_str());
		AppendMainFrameStatusLine(configLine);

		CString runtimeLine;
		runtimeLine.Format(
			L"[Speech] startup.runtime - ready=%s status=%s provider=%s effectiveProvider=%s model=%s layout=%s variant=%s encoder=%s decoderInit=%s tokenizer=%s loadAttempts=%llu loadFailures=%llu transcribeCompleted=%llu",
			runtime.ready ? L"true" : L"false",
			ToWide(runtime.status).c_str(),
			ToWide(runtime.provider).c_str(),
			ToWide(runtime.effectiveExecutionProvider).c_str(),
			ToWide(runtime.modelPath).c_str(),
			ToWide(runtime.modelLayout).c_str(),
			ToWide(runtime.modelVariant).c_str(),
			ToWide(runtime.encoderModelPath).c_str(),
			ToWide(runtime.decoderInitModelPath).c_str(),
			ToWide(runtime.tokenizerPath).c_str(),
			static_cast<unsigned long long>(runtime.modelLoadAttempts),
			static_cast<unsigned long long>(runtime.modelLoadFailures),
			static_cast<unsigned long long>(runtime.transcribeRequestsCompleted));
		AppendMainFrameStatusLine(runtimeLine);

		CString runtimeLoadLine;
		runtimeLoadLine.Format(
			L"[Speech] startup.runtime.load - stage=%s latencyMs=%u",
			ToWide(runtime.lastModelLoadStage).c_str(),
			runtime.lastModelLoadLatencyMs);
		AppendMainFrameStatusLine(runtimeLoadLine);

		CString runtimeWarmupLine;
		runtimeWarmupLine.Format(
			L"[Speech] startup.runtime.warmup - enabled=%s completed=%s succeeded=%s runs=%u provider=%s stage=%s latencyMs=%u error=%s",
			runtime.runtimeHotWarmupEnabled ? L"true" : L"false",
			runtime.runtimeHotWarmupCompleted ? L"true" : L"false",
			runtime.runtimeHotWarmupSucceeded ? L"true" : L"false",
			runtime.runtimeHotWarmupRuns,
			ToWide(runtime.runtimeHotWarmupProvider).c_str(),
			ToWide(runtime.runtimeHotWarmupStage).c_str(),
			runtime.runtimeHotWarmupLatencyMs,
			ToWide(runtime.runtimeHotWarmupError).c_str());
		AppendMainFrameStatusLine(runtimeWarmupLine);

		const std::wstring cudaReason = runtime.cudaExecutionProviderReason.empty()
			? std::wstring(L"none")
			: ToWide(runtime.cudaExecutionProviderReason);
		CString cudaLine;
		cudaLine.Format(
			L"[Speech] startup.runtime.cuda - available=%s enabled=%s reason=%s",
			runtime.cudaExecutionProviderAvailable ? L"true" : L"false",
			runtime.cudaExecutionProviderEnabled ? L"true" : L"false",
			cudaReason.c_str());
		AppendMainFrameStatusLine(cudaLine);
		AppendMainFrameStatusLine(
			L"[Speech] startup.runtime.cuda.policy - cublasMajor=12 cublasLtMajor=12 cufftMajor=12 cudnnMajor=9 guard=enabled latchOnGuardFailure=true");
		AppendSpeechCudaModuleInventoryStatus();

		if (!config.speechRecognition.enabled) {
			AppendMainFrameStatusLine(
				L"[Speech] startup.disabled - speechRecognition.enabled=false");
		}

		if (runtime.error.has_value()) {
			CString errorLine;
			errorLine.Format(
				L"[Speech] startup.error - code=%s message=%s",
				ToWide(blazeclaw::core::speechrecognition::SpeechRecognitionErrorCodeToString(
					runtime.error->code)).c_str(),
				ToWide(runtime.error->message).c_str());
			AppendMainFrameStatusLine(errorLine);
		}
	}

	void AppendStartupLocalModelStatus(
		const blazeclaw::config::AppConfig& config,
		const blazeclaw::core::ServiceManager& services) {
		if (!config.localModel.enabled) {
			AppendMainFrameStatusLine(
				L"[Chat] startup.localModel.disabled - chat.localModel.enabled=false");
			return;
		}

		const auto runtime = services.LocalModelRuntime();

		CString localModelLine;
		localModelLine.Format(
			L"[Chat] startup.localModel.config - provider=%s stage=%s model=%s tokenizer=%s maxTokens=%u temperature=%.2f",
			ToWide(runtime.provider).c_str(),
			ToWide(runtime.rolloutStage).c_str(),
			ToWide(runtime.modelPath).c_str(),
			ToWide(runtime.tokenizerPath).c_str(),
			runtime.maxTokens,
			runtime.temperature);
		AppendMainFrameStatusLine(localModelLine);

		CString gatingLine;
		gatingLine.Format(
			L"[Chat] startup.localModel.gating - rolloutEligible=%s activationEnabled=%s reason=%s",
			services.LocalModelRolloutEligible() ? L"true" : L"false",
			services.LocalModelActivationEnabled() ? L"true" : L"false",
			ToWide(services.LocalModelActivationReason()).c_str());
		AppendMainFrameStatusLine(gatingLine);

		CString integrityLine;
		integrityLine.Format(
			L"[Chat] startup.localModel.integrity - runtimeDll=%s modelHashVerified=%s tokenizerHashVerified=%s",
			runtime.runtimeDllPresent ? L"true" : L"false",
			runtime.modelHashVerified ? L"true" : L"false",
			runtime.tokenizerHashVerified ? L"true" : L"false");
		AppendMainFrameStatusLine(integrityLine);

		const std::wstring runtimeDllPath =
			runtime.onnxRuntimeDllPath.empty()
			? L"unresolved"
			: ToWide(runtime.onnxRuntimeDllPath);
		CString runtimeDllPathLine;
		runtimeDllPathLine.Format(
			L"[Chat] startup.localModel.runtimeDll.path - %s",
			runtimeDllPath.c_str());
		AppendMainFrameStatusLine(runtimeDllPathLine);

		CString executionProviderLine;
		executionProviderLine.Format(
			L"[Chat] startup.localModel.executionProvider - effective=%s",
			ToWide(runtime.effectiveExecutionProvider).c_str());
		AppendMainFrameStatusLine(executionProviderLine);

		const bool activeLocalProvider =
			_wcsicmp(config.chat.activeProvider.c_str(), L"local") == 0;
		const bool activeLlamaModel =
			config.chat.activeModel.rfind(L"llama/", 0) == 0;
		if (activeLocalProvider && activeLlamaModel) {
			std::filesystem::path ggufPath(config.localModel.modelPath);
			if (ggufPath.is_relative()) {
				ggufPath =
					std::filesystem::path(config.localModel.storageRoot) / ggufPath;
			}

			std::error_code existsError;
			const bool ggufExists =
				std::filesystem::exists(ggufPath, existsError) && !existsError;
			if (!ggufExists) {
				const CString ggufMissingLine(
					(L"[Chat] startup.localModel.diagnostic - llama selected but gguf missing: " +
						ggufPath.wstring())
					.c_str());
				AppendMainFrameStatusLine(ggufMissingLine);
			}
		}

		CString cudaStatusLine;
		const std::wstring cudaReason =
			runtime.cudaExecutionProviderReason.empty()
			? L"none"
			: ToWide(runtime.cudaExecutionProviderReason);
		cudaStatusLine.Format(
			L"[Chat] startup.localModel.cuda - available=%s enabled=%s reason=%s",
			runtime.cudaExecutionProviderAvailable ? L"true" : L"false",
			runtime.cudaExecutionProviderEnabled ? L"true" : L"false",
			cudaReason.c_str());
		AppendMainFrameStatusLine(cudaStatusLine);

		if (!runtime.tokenizerPath.empty()) {
			blazeclaw::core::localmodel::TokenizerBridge tokenizer;
			std::string tokenizerLoadError;
			std::filesystem::path tokenizerPath(ToWide(runtime.tokenizerPath));
			if (tokenizerPath.is_relative()) {
				tokenizerPath =
					std::filesystem::path(config.localModel.storageRoot) / tokenizerPath;
			}
			if (!tokenizer.Load(tokenizerPath, tokenizerLoadError)) {
				const CString tokenizerErrorLine(
					(L"[Chat] startup.localModel.tokenizer.roundtrip - load_failed: " +
						ToWide(tokenizerLoadError)).c_str());
				AppendMainFrameStatusLine(tokenizerErrorLine);
			}
			else {
				blazeclaw::core::localmodel::TextGenerationError tokenizationError;
				const std::string probeText =
					"roundtrip probe: hello tokenizer 123";
				const auto ids = tokenizer.EncodeToIds(
					probeText,
					96,
					tokenizationError,
					true);

				if (ids.empty()) {
					const std::wstring reason = tokenizationError.message.empty()
						? L"unknown"
						: ToWide(tokenizationError.message);
					const CString tokenizerErrorLine(
						(L"[Chat] startup.localModel.tokenizer.roundtrip - encode_failed: " +
							reason).c_str());
					AppendMainFrameStatusLine(tokenizerErrorLine);
				}
				else {
					const std::string decoded = tokenizer.DecodeFromIds(ids);
					const bool matched = decoded == probeText;

					CString tokenizerLine;
					tokenizerLine.Format(
						L"[Chat] startup.localModel.tokenizer.roundtrip - ok=%s ids=%zu",
						matched ? L"true" : L"false",
						ids.size());
					AppendMainFrameStatusLine(tokenizerLine);

					if (!matched) {
						const CString mismatchLine(
							(L"[Chat] startup.localModel.tokenizer.roundtrip.mismatch - decoded=" +
								ToWide(decoded)).c_str());
						AppendMainFrameStatusLine(mismatchLine);
					}
				}
			}
		}

		if (runtime.ready) {
			const bool llamaRuntime =
				runtime.effectiveExecutionProvider == "llama.cpp" ||
				runtime.provider == "llama.cpp" ||
				runtime.modelPath.ends_with(".gguf");
			if (!llamaRuntime) {
				AppendMainFrameStatusLine(
					L"[Chat] startup.localModel.qwenContract - promptTemplate=qwen3-chat markers=<|im_start|>/<|im_end|> decodeStop=<|im_end|>");
			}
			if (services.LocalModelActivationEnabled()) {
				AppendMainFrameStatusLine(
					llamaRuntime
					? L"[Chat] startup.localModel.loaded - local llama.cpp GGUF runtime ready and active"
					: L"[Chat] startup.localModel.loaded - local ONNX runtime ready and active");
			}
			else {
				AppendMainFrameStatusLine(
					llamaRuntime
					? L"[Chat] startup.localModel.loaded - local llama.cpp GGUF runtime ready but fallback is active"
					: L"[Chat] startup.localModel.loaded - local ONNX runtime ready but fallback is active");
			}
			return;
		}

		const CString errorLine(
			(L"[Chat] startup.localModel.error - status=" +
				ToWide(runtime.status)).c_str());
		AppendMainFrameStatusLine(errorLine);
	}

	CRuntimeClass* ResolveChatRuntimeViewClass(
		const blazeclaw::config::AppConfig& config) {
		if (config.chat.mode == L"native") {
			return RUNTIME_CLASS(CChatView);
		}

		return RUNTIME_CLASS(CBlazeClawMFCView);
	}
}


// CBlazeClawMFCApp

BEGIN_MESSAGE_MAP(CBlazeClawMFCApp, CWinAppEx)
	ON_COMMAND(ID_APP_ABOUT, &CBlazeClawMFCApp::OnAppAbout)
	ON_COMMAND(ID_FILE_NEW, &CBlazeClawMFCApp::OnFileNew)
	ON_COMMAND(ID_FILE_OPEN, &CWinAppEx::OnFileOpen)
	// Standard print setup command
	ON_COMMAND(ID_FILE_PRINT_SETUP, &CWinAppEx::OnFilePrintSetup)
END_MESSAGE_MAP()

// CBlazeClawMFCApp construction

CBlazeClawMFCApp::CBlazeClawMFCApp() noexcept
{
	m_bHiColorIcons = TRUE;


	m_nAppLook = 0;
	// support Restart Manager
	m_dwRestartManagerSupportFlags = AFX_RESTART_MANAGER_SUPPORT_ALL_ASPECTS;
#ifdef _MANAGED
	// If the application is built using Common Language Runtime support (/clr):
	//     1) This additional setting is needed for Restart Manager support to work properly.
	//     2) In your project, you must add a reference to System.Windows.Forms in order to build.
	System::Windows::Forms::Application::SetUnhandledExceptionMode(System::Windows::Forms::UnhandledExceptionMode::ThrowException);
#endif

	// TODO: replace application ID string below with unique ID string; recommended
	// format for string is CompanyName.ProductName.SubProduct.VersionInformation
	SetAppID(_T("BlazeClawMFC.AppID.NoVersion"));

	// TODO: add construction code here,
	// Place all significant initialization in InitInstance
}

// The one and only CBlazeClawMFCApp object
CBlazeClawMFCApp theApp;


// CBlazeClawMFCApp initialization
BOOL CBlazeClawMFCApp::InitInstance() try {
	AppendStartupCheckpoint(L"InitInstance.begin");
	// InitCommonControlsEx() is required on Windows XP if an application
	// manifest specifies use of ComCtl32.dll version 6 or later to enable
	// visual styles.  Otherwise, any window creation will fail.
	INITCOMMONCONTROLSEX InitCtrls;
	InitCtrls.dwSize = sizeof(InitCtrls);
	// Set this to include all the common control classes you want to use
	// in your application.
	InitCtrls.dwICC = ICC_WIN95_CLASSES;
	InitCommonControlsEx(&InitCtrls);

	CWinAppEx::InitInstance();

	if (!AfxSocketInit())
	{
		AppendStartupCheckpoint(L"InitInstance.fail.AfxSocketInit");
		AfxMessageBox(IDP_SOCKETS_INIT_FAILED);
		return FALSE;
	}

	// Initialize OLE libraries
	if (!AfxOleInit())
	{
		AppendStartupCheckpoint(L"InitInstance.fail.AfxOleInit");
		AfxMessageBox(IDP_OLE_INIT_FAILED);
		return FALSE;
	}

	AfxEnableControlContainer();

	EnableTaskbarInteraction();

	// AfxInitRichEdit2() is required to use RichEdit control
	// AfxInitRichEdit2();

	// Standard initialization
	// If you are not using these features and wish to reduce the size
	// of your final executable, you should remove from the following
	// the specific initialization routines you do not need
	// Change the registry key under which our settings are stored
	// TODO: You should modify this string to be something appropriate
	// such as the name of your company or organization
	SetRegistryKey(_T("BlazeClaw @ Wuhan, China"));
	LoadStdProfileSettings(16);  // Load standard INI file options (including MRU)


	InitContextMenuManager();
	InitShellManager();

	InitKeyboardManager();

	InitTooltipManager();
	CMFCToolTipInfo ttParams;
	ttParams.m_bVislManagerTheme = TRUE;
	theApp.GetTooltipManager()->SetTooltipParams(AFX_TOOLTIP_TYPE_ALL,
		RUNTIME_CLASS(CMFCToolTipCtrl), &ttParams);

	m_configLoader.LoadFromFile(kConfigPath, m_config);
	AppendStartupCheckpoint(L"InitInstance.config.loaded");
	if (const auto commandExitCode = TryRunOfflineSttOptimizationCommand(m_config);
		commandExitCode.has_value()) {
		const std::wstring checkpoint = *commandExitCode == 0
			? L"InitInstance.exit.speech.offline.optimize.success"
			: L"InitInstance.exit.speech.offline.optimize.failed";
		AppendStartupCheckpoint(checkpoint);
		::ExitProcess(static_cast<UINT>(*commandExitCode));
	}
	std::optional<std::wstring> startupServiceError;
	try {
		if (!m_serviceManager.Start(m_config)) {
			startupServiceError =
				L"service manager returned not running after startup.";
		}
	}
	catch (const std::exception& ex) {
		startupServiceError =
			L"service manager startup exception: " +
			ToWide(ex.what());
	}
	catch (...) {
		startupServiceError =
			L"service manager startup failed with unknown exception.";
	}
	AppendStartupCheckpoint(
		startupServiceError.has_value()
		? (L"InitInstance.service.start.warning=" + startupServiceError.value())
		: L"InitInstance.service.start.ok");
	if (m_serviceManager.IsRunning()) {
		StartGatewayPumpWorker();
		AppendStartupCheckpoint(L"InitInstance.gateway.pump.worker.started");
	}

	// Register the application's document templates.  Document templates
	//  serve as the connection between documents, frame windows and views
	CRuntimeClass* viewRuntimeClass = ResolveChatRuntimeViewClass(m_config);
	m_pChatDocTemplate = new CMultiDocTemplate(IDR_BlazeClawMFCTYPE,
		RUNTIME_CLASS(CBlazeClawMFCDoc),
		RUNTIME_CLASS(CChildFrame), // custom MDI child frame
		viewRuntimeClass);
	if (!m_pChatDocTemplate) {
		AppendStartupCheckpoint(L"InitInstance.fail.ChatDocTemplate");
		StopGatewayPumpWorker();
		AfxMessageBox(
			L"Startup failed: unable to create chat document template.",
			MB_OK | MB_ICONERROR);
		return FALSE;
	}
	AddDocTemplate(m_pChatDocTemplate);

	// New template: Two MDI tabs (WebView + Markdown) sharing the same document
	m_pWebViewMarkdownSharedDocTemplate = new CSharedTabsDocTemplate(
		IDR_BlazeClawMFCTYPE,
		RUNTIME_CLASS(CBlazeClawMFCDoc),
		RUNTIME_CLASS(CSharedDocWebViewChildFrame),
		RUNTIME_CLASS(CSharedDocMarkdownChildFrame));
	AddDocTemplate(m_pWebViewMarkdownSharedDocTemplate);

	//auto* frame = new CMainFrame();
	//m_pMainWnd = frame;

	// create main MDI Frame window
	CMainFrame* pMainFrame = new CMainFrame;
	if (!pMainFrame || !pMainFrame->LoadFrame(IDR_MAINFRAME))
	{
		AppendStartupCheckpoint(L"InitInstance.fail.MainFrame.LoadFrame");
		StopGatewayPumpWorker();
		AfxMessageBox(
			L"Startup failed: unable to create main window frame.",
			MB_OK | MB_ICONERROR);
		delete pMainFrame;
		return FALSE;
	}
	m_pMainWnd = pMainFrame;

	// call DragAcceptFiles only if there's a suffix
	//  In an MDI app, this should occur immediately after setting m_pMainWnd
	// Enable drag/drop open
	m_pMainWnd->DragAcceptFiles();

	// Parse command line for standard shell commands, DDE, file open
	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);
	const auto parsedShellCommand = cmdInfo.m_nShellCommand;

	// Enable DDE Execute open
	EnableShellOpen();
	RegisterShellFileTypes(TRUE);

	//frame->ShowWindow(SW_SHOW);
	//frame->UpdateWindow();

	// 禁用默认的文档创建
	cmdInfo.m_nShellCommand = CCommandLineInfo::FileNothing;
	std::optional<std::wstring> startupShellWarning;

	// Dispatch commands specified on the command line.  Will return FALSE if
	// app was launched with /RegServer, /Register, /Unregserver or /Unregister.
	if (!ProcessShellCommand(cmdInfo)) {
		if (parsedShellCommand == CCommandLineInfo::AppRegister ||
			parsedShellCommand == CCommandLineInfo::AppUnregister) {
			AppendStartupCheckpoint(L"InitInstance.exit.ProcessShellCommand.RegisterOperation");
			StopGatewayPumpWorker();
			return FALSE;
		}

		startupShellWarning =
			L"ProcessShellCommand returned false; continuing with fallback UI startup.";
	}
	AppendStartupCheckpoint(L"InitInstance.shell.command.processed");
	// The main window has been initialized, so show and update it
	pMainFrame->ShowWindow(SW_SHOWMAXIMIZED);
	pMainFrame->UpdateWindow();
	AppendStartupCheckpoint(L"InitInstance.mainframe.shown");

	pMainFrame->PostMessage(kMsgCreateMdiGroup);

	if (startupServiceError.has_value() && !startupServiceError->empty()) {
		const CString startupErrorLine(
			(L"[Startup] service.bootstrap.error - " +
				startupServiceError.value()).c_str());
		AppendMainFrameStatusLine(startupErrorLine);
	}
	if (startupShellWarning.has_value() && !startupShellWarning->empty()) {
		const CString startupShellWarningLine(
			(L"[Startup] shell.command.warning - " +
				startupShellWarning.value()).c_str());
		AppendMainFrameStatusLine(startupShellWarningLine);
	}

	AppendStartupConfigStatus(m_config);
	if (m_serviceManager.IsRunning()) {
		AppendStartupLocalModelStatus(m_config, m_serviceManager);
		AppendStartupEmbeddingsStatus(m_config, m_serviceManager);
		AppendStartupSpeechStatus(m_config, m_serviceManager);
	}
	else {
		AppendMainFrameStatusLine(
			L"[Startup] service.bootstrap.status - running=false, startup diagnostics limited.");
	}

	m_bStartupComplete = TRUE;
	AppendStartupCheckpoint(L"InitInstance.completed.true");

	return TRUE;
}
catch (const std::exception& ex) {
	StopGatewayPumpWorker();
	AppendStartupCheckpoint(
		L"InitInstance.exception.std=" + ToWide(ex.what()));
	const std::wstring message =
		L"Startup failed with an exception. See: " +
		ResolveStartupTracePath().wstring();
	AfxMessageBox(message.c_str(), MB_OK | MB_ICONERROR);
	return FALSE;
}
catch (...) {
	StopGatewayPumpWorker();
	AppendStartupCheckpoint(L"InitInstance.exception.unknown");
	const std::wstring message =
		L"Startup failed with an unknown exception. See: " +
		ResolveStartupTracePath().wstring();
	AfxMessageBox(message.c_str(), MB_OK | MB_ICONERROR);
	return FALSE;
}

int CBlazeClawMFCApp::ExitInstance() {
	StopGatewayPumpWorker();

	PersistActiveChatConnection(m_serviceManager);
	m_serviceManager.Stop();

	AfxOleTerm(FALSE);

	return CWinApp::ExitInstance();
}

BOOL CBlazeClawMFCApp::OnIdle(LONG lCount) {
	return CWinAppEx::OnIdle(lCount);
}

void CBlazeClawMFCApp::StartGatewayPumpWorker() {
	if (m_gatewayPumpWorker.joinable()) {
		return;
	}

	m_gatewayPumpWorkerStopRequested.store(false);
	m_gatewayPumpWorker = std::thread(
		[this]() {
			GatewayPumpWorkerLoop();
		});
}

void CBlazeClawMFCApp::StopGatewayPumpWorker() {
	if (!m_gatewayPumpWorker.joinable()) {
		return;
	}

	m_gatewayPumpWorkerStopRequested.store(true);
	m_gatewayPumpWorkerCv.notify_all();
	m_gatewayPumpWorker.join();
}

void CBlazeClawMFCApp::GatewayPumpWorkerLoop() {
	constexpr auto kPumpInterval = std::chrono::milliseconds(16);

	while (!m_gatewayPumpWorkerStopRequested.load()) {
		std::string pumpError;
		if (!m_serviceManager.PumpGatewayNetworkOnce(pumpError) &&
			!pumpError.empty()) {
			TRACE(
				"[Gateway][PumpNetworkOnce] %s\n",
				pumpError.c_str());
		}

		std::unique_lock<std::mutex> lock(m_gatewayPumpWorkerMutex);
		m_gatewayPumpWorkerCv.wait_for(
			lock,
			kPumpInterval,
			[this]() {
				return m_gatewayPumpWorkerStopRequested.load();
			});
	}
}

blazeclaw::core::ServiceManager& CBlazeClawMFCApp::Services() noexcept {
	return m_serviceManager;
}

bool CBlazeClawMFCApp::EnsureServiceRunning(std::string* outError) {
	if (m_serviceManager.IsRunning()) {
		if (outError != nullptr) {
			outError->clear();
		}

		return true;
	}

	std::lock_guard<std::mutex> guard(m_serviceRecoveryMutex);
	if (m_serviceManager.IsRunning()) {
		if (outError != nullptr) {
			outError->clear();
		}

		return true;
	}

	std::string error;
	try {
		if (!m_serviceManager.Start(m_config) || !m_serviceManager.IsRunning()) {
			error = "service startup returned not running";
		}
	}
	catch (const std::exception& ex) {
		error = std::string("service startup exception: ") + ex.what();
	}
	catch (...) {
		error = "service startup unknown exception";
	}

	if (error.empty()) {
		StartGatewayPumpWorker();
	}

	if (outError != nullptr) {
		*outError = error;
	}

	return error.empty();
}

blazeclaw::gateway::protocol::ResponseFrame CBlazeClawMFCApp::RouteGatewayRequest(
	const blazeclaw::gateway::protocol::RequestFrame& request) {
	std::string startupError;
	if (!EnsureServiceRunning(&startupError)) {
		const std::string message = startupError.empty()
			? "Service manager is not running."
			: "Service manager is not running. " + startupError;
		return blazeclaw::gateway::protocol::ResponseFrame{
			.id = request.id,
			.ok = false,
			.payloadJson = std::nullopt,
			.error = blazeclaw::gateway::protocol::ErrorShape{
				.code = "service_not_running",
				.message = message,
				.detailsJson = std::nullopt,
				.retryable = false,
				.retryAfterMs = std::nullopt,
			},
		};
	}

	auto response = m_serviceManager.RouteGatewayRequest(request);
	if (response.ok || !response.error.has_value()) {
		return response;
	}

	if (response.error->code != "service_not_running") {
		return response;
	}

	if (!EnsureServiceRunning(&startupError)) {
		return response;
	}

	return m_serviceManager.RouteGatewayRequest(request);
}

CRuntimeClass* CBlazeClawMFCApp::GetWebViewMarkdownLeftViewClass() const {
	return RUNTIME_CLASS(CBlazeClawMFCView);
}

CRuntimeClass* CBlazeClawMFCApp::GetWebViewMarkdownRightViewClass() const {
	return RUNTIME_CLASS(CBlazeClawMarkdownView);
}

void CBlazeClawMFCApp::OnFileNew()
{
	// Do nothing until InitInstance finishes. Early calls (e.g. shell / framework during load)
	// would otherwise open WebView+Chat here while CreateTwoTabbedGroups also runs → three tabs.
	if (!m_bStartupComplete)
		return;

	// With an active MDI child, MFC routes ID_FILE_NEW to CWinApp::OnCmdMsg before the main frame.
	CMainFrame* pMain = DYNAMIC_DOWNCAST(CMainFrame, m_pMainWnd);
	if (pMain != nullptr)
		pMain->OpenNewTabWithChoiceDialog();
}

const blazeclaw::core::ServiceManager& CBlazeClawMFCApp::Services() const noexcept {
	return m_serviceManager;
}

// CBlazeClawMFCApp message handlers


// CAboutDlg dialog used for App About

class CAboutDlg : public CDialogEx
{
public:
	CAboutDlg() noexcept;

	// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_ABOUTBOX };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	// Implementation
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() noexcept : CDialogEx(IDD_ABOUTBOX)
{}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()

// App command to run the dialog
void CBlazeClawMFCApp::OnAppAbout()
{
	CAboutDlg aboutDlg;
	aboutDlg.DoModal();
}

// CBlazeClawMFCApp customization load/save methods

void CBlazeClawMFCApp::PreLoadState()
{
	BOOL bNameValid;
	CString strName;
	bNameValid = strName.LoadString(IDS_EDIT_MENU);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EDIT);
	bNameValid = strName.LoadString(IDS_EXPLORER);
	ASSERT(bNameValid);
	GetContextMenuManager()->AddMenu(strName, IDR_POPUP_EXPLORER);
}

void CBlazeClawMFCApp::LoadCustomState()
{}

void CBlazeClawMFCApp::SaveCustomState()
{}
