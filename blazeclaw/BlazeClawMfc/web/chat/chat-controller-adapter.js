(function () {
    const BRIDGE_BACKED_LOW_RISK_METHODS = [
        "post",
        "request",
        "flushQueue",
        "loadSessionOptions",
        "loadSessionCompactions",
        "refreshSessionControlState",
        "selectSessionCompaction",
        "branchSessionCompaction",
        "restoreSessionCompaction",
        "loadModelOptions",
        "loadThinkingOptions",
        "switchSession",
        "applyModelSelection",
        "applyThinkingLevel",
        "getControlUiBootstrapConfig",
        "loadSpeechCapabilities",
        "getSpeechSessionStateSnapshot",
        "parseApprovalTokenFromText",
        "executeExecApprovalAction",
        "getOperatorDiagnosticsSnapshot",
    ];

    const RUN_LOOP_TRANSCRIPT_PARITY_METHODS = [
        "loadHistory",
        "send",
        "abort",
        "handleRpcResult",
        "consumeTerminalText",
        "applyDeltaText",
        "commitStreamTranscriptFinal",
        "clearRunState",
        "parseTextFromMessage",
        "isSilentReplyText",
        "noteInboundChatEvent",
        "appendChatBubble",
        "markTerminalRun",
        "hasTerminalRun",
        "hasBufferedAssistantStream",
        "scheduleHistoryReconcile",
        "getStructuredTranscript",
    ];

    const SPEECH_APPROVAL_PARITY_METHODS = [
        "loadSpeechErrorPolicy",
        "loadSpeechCapabilities",
        "getSpeechCapabilitiesSnapshot",
        "getSpeechSessionStateSnapshot",
        "applySpeechLifecycleUpdate",
        "assessTranscriptQuality",
        "transcribeSpeech",
        "parseApprovalTokenFromText",
        "executeExecApprovalAction",
    ];

    function resolveLegacyControllerEnabled() {
        if (typeof window.__BLAZECLAW_CHAT_LEGACY_CONTROLLER_ENABLED__ === "boolean") {
            return window.__BLAZECLAW_CHAT_LEGACY_CONTROLLER_ENABLED__;
        }

        try {
            if (window.localStorage && typeof window.localStorage.getItem === "function") {
                const raw = String(window.localStorage.getItem("blazeclaw.chat.legacyControllerEnabled") || "")
                    .trim()
                    .toLowerCase();
                if (raw === "0" || raw === "false" || raw === "off" || raw === "disabled") {
                    return false;
                }
                if (raw === "1" || raw === "true" || raw === "on" || raw === "enabled") {
                    return true;
                }
            }
        } catch (_) {
        }

        return true;
    }

    function getLegacyControllerApi() {
        if (!resolveLegacyControllerEnabled()) {
            throw new Error("BlazeClawChatControllerLegacy disabled by kill-switch");
        }

        const api = window.BlazeClawChatControllerLegacy;
        if (!api || typeof api !== "object") {
            throw new Error("BlazeClawChatControllerLegacy unavailable");
        }
        return api;
    }

    function isPromiseLike(value) {
        return Boolean(value) && typeof value.then === "function";
    }

    function normalizeContentTextItem(item) {
        if (!item || typeof item !== "object") {
            return "";
        }

        const type = typeof item.type === "string"
            ? item.type.trim().toLowerCase()
            : "";
        if (type === "text" && typeof item.text === "string") {
            return item.text;
        }
        if (type === "image") {
            const mimeType = typeof item.mimeType === "string" && item.mimeType.trim()
                ? item.mimeType.trim()
                : "image/*";
            return `[Image attachment: ${mimeType}]`;
        }
        if (type === "tool-call") {
            const toolName = typeof item.tool === "string" && item.tool.trim()
                ? item.tool.trim()
                : "tool";
            return `[Tool call: ${toolName}]`;
        }
        if (type === "tool-result") {
            const toolName = typeof item.tool === "string" && item.tool.trim()
                ? item.tool.trim()
                : "tool";
            return `[Tool result: ${toolName}]`;
        }
        if (type) {
            return `[${type}]`;
        }
        return "";
    }

    function stripChatTemplateMarkers(text) {
        const raw = String(text || "");
        return raw
            .replace(/<\|[^>]*\|>/g, "")
            .replace(/\n{3,}/g, "\n\n")
            .trim();
    }

    function parseTextFromMessageFallback(message) {
        if (typeof message === "string") {
            const trimmed = message.trim();
            if (!trimmed) {
                return "";
            }
            if ((trimmed.startsWith("{") && trimmed.endsWith("}")) ||
                (trimmed.startsWith("[") && trimmed.endsWith("]"))) {
                try {
                    const parsed = JSON.parse(trimmed);
                    if (parsed && typeof parsed === "object") {
                        return stripChatTemplateMarkers(parseTextFromMessageFallback(parsed));
                    }
                } catch (_) {
                }
            }
            return stripChatTemplateMarkers(trimmed);
        }

        if (!message || typeof message !== "object") {
            return "";
        }

        if (typeof message.text === "string") {
            return stripChatTemplateMarkers(message.text);
        }

        if (Array.isArray(message.content)) {
            const lines = [];
            for (const item of message.content) {
                const line = normalizeContentTextItem(item);
                if (line) {
                    lines.push(line);
                }
            }
            return stripChatTemplateMarkers(lines.join("\n").trim());
        }

        return "";
    }

    function isSilentReplyTextFallback(text) {
        return typeof text === "string" && /^\s*NO_REPLY\s*$/i.test(text);
    }

    function buildDefaultSpeechErrorPolicy() {
        return {
            loaded: true,
            defaultClass: "status",
            map: {},
            retry: {},
        };
    }

    function buildDefaultSpeechCapabilities() {
        return {
            sttSupported: false,
            sttReady: false,
            audioHandoffMode: "dual",
            streamingSupported: false,
            streamingPreviewEnabled: false,
            streamingConfigured: false,
            modelNativeVad: false,
            streamingChunkMs: 0,
            streamingLookbackMs: 0,
            provider: "",
            effectiveExecutionProvider: "",
            cudaExecutionProviderAvailable: false,
            cudaExecutionProviderEnabled: false,
            cudaExecutionProviderReason: "",
            transcriptSupportsSegments: false,
            transcriptSupportsInterim: false,
            transcriptSupportsFinal: true,
            ttsSupported: false,
            ttsReady: false,
            lifecycle: [],
            loaded: false,
            error: "",
        };
    }

    function buildDefaultSpeechSessionState() {
        return {
            stage: "idle",
            text: "",
            segmentText: "",
            segmentFinal: true,
            segmentSequence: 0,
            runId: "",
            sessionId: "",
            audioPath: "",
            language: "",
            latencyMs: 0,
            cancelled: false,
            errorCode: "",
            errorMessage: "",
            errorClass: "status",
            retryable: false,
            retryStrategy: "immediate",
            retryGuidance: "",
            provider: "",
            effectiveExecutionProvider: "",
            cudaExecutionProviderAvailable: false,
            cudaExecutionProviderEnabled: false,
            cudaExecutionProviderReason: "",
            updatedAtMs: 0,
        };
    }

    function isValidApprovalToken(token) {
        const normalized = String(token || "").trim();
        if (!normalized) {
            return false;
        }
        if (!/^[A-Za-z0-9:_\-]+$/.test(normalized)) {
            return false;
        }
        if (/^email-approval-\d{10,}-\d+$/.test(normalized)) {
            return true;
        }
        return normalized.length >= 24;
    }

    function parseApprovalTokenFromTextFallback(text) {
        const raw = String(text || "");
        if (!raw) {
            return null;
        }
        const tokenMatch = /approvalToken=([A-Za-z0-9:_\-]+)(?=\s|[，。！？；,.!?;]|$)/.exec(raw);
        if (!tokenMatch || !tokenMatch[1]) {
            const fallbackMatch = /approvalToken=([A-Za-z0-9:_\-]+)/.exec(raw);
            if (!fallbackMatch || !fallbackMatch[1]) {
                return null;
            }
            const fallbackToken = String(fallbackMatch[1] || "").trim();
            if (!isValidApprovalToken(fallbackToken)) {
                return null;
            }
            const fallbackResult = {
                approvalToken: fallbackToken,
            };
            const fallbackExpires = /expiresAtEpochMs=(\d{8,})/.exec(raw);
            if (fallbackExpires && fallbackExpires[1]) {
                fallbackResult.expiresAtEpochMs = Number(fallbackExpires[1]);
            }
            return fallbackResult;
        }
        const parsedToken = String(tokenMatch[1] || "").trim();
        if (!isValidApprovalToken(parsedToken)) {
            return null;
        }
        const result = {
            approvalToken: parsedToken,
        };
        const expiresMatch = /expiresAtEpochMs=(\d{8,})/.exec(raw);
        if (expiresMatch && expiresMatch[1]) {
            result.expiresAtEpochMs = Number(expiresMatch[1]);
        }
        return result;
    }

    function normalizeSpeechErrorPolicySnapshot(value) {
        const source = value && typeof value === "object"
            ? value
            : {};
        return {
            loaded: source.loaded !== false,
            defaultClass: String(source.defaultClass || "status"),
            map: source.map && typeof source.map === "object" ? { ...source.map } : {},
            retry: source.retry && typeof source.retry === "object" ? { ...source.retry } : {},
        };
    }

    function normalizeSpeechCapabilitiesSnapshot(value) {
        const base = buildDefaultSpeechCapabilities();
        const source = value && typeof value === "object"
            ? value
            : {};
        return {
            ...base,
            ...source,
            sttSupported: Boolean(source.sttSupported),
            sttReady: Boolean(source.sttReady),
            transcriptSupportsSegments: Boolean(source.transcriptSupportsSegments),
            transcriptSupportsInterim: Boolean(source.transcriptSupportsInterim),
            transcriptSupportsFinal: source.transcriptSupportsFinal !== false,
            ttsSupported: Boolean(source.ttsSupported),
            ttsReady: Boolean(source.ttsReady),
            loaded: Boolean(source.loaded),
            lifecycle: Array.isArray(source.lifecycle) ? source.lifecycle.slice() : [],
            error: String(source.error || ""),
        };
    }

    function normalizeSpeechSessionStateSnapshot(value) {
        const base = buildDefaultSpeechSessionState();
        const source = value && typeof value === "object"
            ? value
            : {};
        return {
            ...base,
            ...source,
            stage: String(source.stage || base.stage),
            text: String(source.text || ""),
            segmentText: String(source.segmentText || ""),
            segmentFinal: source.segmentFinal !== false,
            runId: String(source.runId || ""),
            sessionId: String(source.sessionId || ""),
            errorCode: String(source.errorCode || ""),
            errorMessage: String(source.errorMessage || ""),
            errorClass: String(source.errorClass || base.errorClass),
            retryable: Boolean(source.retryable),
            retryStrategy: String(source.retryStrategy || base.retryStrategy),
            retryGuidance: String(source.retryGuidance || ""),
            updatedAtMs: Number.isFinite(Number(source.updatedAtMs))
                ? Number(source.updatedAtMs)
                : 0,
        };
    }

    function normalizeTranscriptQualityResult(value, originalText) {
        const source = value && typeof value === "object"
            ? value
            : {};
        return {
            accepted: Boolean(source.accepted),
            reason: String(source.reason || ""),
            cleanedText: String(source.cleanedText || originalText || ""),
        };
    }

    function normalizeApprovalActionResult(value) {
        const source = value && typeof value === "object"
            ? value
            : {};
        return {
            ok: Boolean(source.ok),
            status: String(source.status || (source.ok ? "ok" : "invalid")),
            errorCode: String(source.errorCode || ""),
            errorMessage: String(source.errorMessage || ""),
            remediation: String(source.remediation || ""),
            missingDependency: String(source.missingDependency || ""),
            installHint: String(source.installHint || ""),
            configHint: String(source.configHint || ""),
            failureBucket: String(source.failureBucket || ""),
            readinessKnown: Boolean(source.readinessKnown),
            readinessReady: Boolean(source.readinessReady),
            readinessCode: String(source.readinessCode || ""),
            readinessMessage: String(source.readinessMessage || ""),
            readinessRemediation: String(source.readinessRemediation || ""),
            readinessMissingDependency: String(source.readinessMissingDependency || ""),
        };
    }

    function fallbackRunLoopTranscriptMethod(methodName, args) {
        if (methodName === "isSilentReplyText") {
            return isSilentReplyTextFallback(args[0]);
        }

        if (methodName === "parseTextFromMessage" ||
            methodName === "consumeTerminalText" ||
            methodName === "applyDeltaText") {
            return parseTextFromMessageFallback(args[0]);
        }

        if (methodName === "getStructuredTranscript") {
            return [];
        }

        if (methodName === "hasTerminalRun" || methodName === "hasBufferedAssistantStream") {
            return false;
        }

        return undefined;
    }

    function fallbackSpeechApprovalMethod(methodName, args) {
        if (methodName === "parseApprovalTokenFromText") {
            return parseApprovalTokenFromTextFallback(args[0]);
        }

        if (methodName === "getSpeechCapabilitiesSnapshot" || methodName === "loadSpeechCapabilities") {
            return buildDefaultSpeechCapabilities();
        }

        if (methodName === "loadSpeechErrorPolicy") {
            return buildDefaultSpeechErrorPolicy();
        }

        if (methodName === "getSpeechSessionStateSnapshot" || methodName === "applySpeechLifecycleUpdate") {
            return buildDefaultSpeechSessionState();
        }

        if (methodName === "assessTranscriptQuality") {
            return {
                accepted: false,
                reason: "transcript quality unavailable",
                cleanedText: String(args[0] || ""),
            };
        }

        if (methodName === "executeExecApprovalAction") {
            return normalizeApprovalActionResult({
                ok: false,
                status: "invalid",
                errorCode: "adapter_unavailable",
                errorMessage: "approval workflow unavailable",
            });
        }

        if (methodName === "transcribeSpeech") {
            return undefined;
        }

        return undefined;
    }

    function normalizeRunLoopTranscriptResult(methodName, value, args) {
        if (methodName === "isSilentReplyText") {
            return Boolean(value);
        }

        if (methodName === "parseTextFromMessage" ||
            methodName === "consumeTerminalText" ||
            methodName === "applyDeltaText") {
            if (typeof value === "string") {
                return value;
            }
            return parseTextFromMessageFallback(args[0]);
        }

        if (methodName === "hasTerminalRun" || methodName === "hasBufferedAssistantStream") {
            return Boolean(value);
        }

        if (methodName === "getStructuredTranscript") {
            return Array.isArray(value) ? value.slice() : [];
        }

        return value;
    }

    function normalizeSpeechApprovalResult(methodName, value, args) {
        if (methodName === "parseApprovalTokenFromText") {
            if (!value || typeof value !== "object") {
                return parseApprovalTokenFromTextFallback(args[0]);
            }
            const parsedToken = String(value.approvalToken || "").trim();
            if (!isValidApprovalToken(parsedToken)) {
                return null;
            }
            const parsed = {
                approvalToken: parsedToken,
            };
            if (Number.isFinite(Number(value.expiresAtEpochMs))) {
                parsed.expiresAtEpochMs = Number(value.expiresAtEpochMs);
            }
            return parsed;
        }

        if (methodName === "getSpeechCapabilitiesSnapshot" || methodName === "loadSpeechCapabilities") {
            return normalizeSpeechCapabilitiesSnapshot(value);
        }

        if (methodName === "loadSpeechErrorPolicy") {
            return normalizeSpeechErrorPolicySnapshot(value);
        }

        if (methodName === "getSpeechSessionStateSnapshot" || methodName === "applySpeechLifecycleUpdate") {
            return normalizeSpeechSessionStateSnapshot(value);
        }

        if (methodName === "assessTranscriptQuality") {
            return normalizeTranscriptQualityResult(value, args[0]);
        }

        if (methodName === "executeExecApprovalAction") {
            return normalizeApprovalActionResult(value);
        }

        return value;
    }

    function invokeAdapterMethod(controllerImpl, methodName, args, mode) {
        const candidate = controllerImpl[methodName];
        if (typeof candidate !== "function") {
            if (mode === "run-loop-transcript") {
                return fallbackRunLoopTranscriptMethod(methodName, args);
            }
            if (mode === "speech-approval") {
                return fallbackSpeechApprovalMethod(methodName, args);
            }
            return undefined;
        }

        try {
            const result = candidate(...args);

            if (mode === "speech-approval" && isPromiseLike(result)) {
                return Promise.resolve(result)
                    .then((resolved) => normalizeSpeechApprovalResult(methodName, resolved, args))
                    .catch((error) => {
                        if (window.console && typeof window.console.warn === "function") {
                            window.console.warn(
                                "[chat-controller-adapter] speech/approval parity guard fallback:",
                                methodName,
                                error);
                        }
                        return fallbackSpeechApprovalMethod(methodName, args);
                    });
            }

            if (mode === "run-loop-transcript") {
                return normalizeRunLoopTranscriptResult(methodName, result, args);
            }
            if (mode === "speech-approval") {
                return normalizeSpeechApprovalResult(methodName, result, args);
            }
            return result;
        } catch (error) {
            if (mode === "run-loop-transcript") {
                if (window.console && typeof window.console.warn === "function") {
                    window.console.warn(
                        "[chat-controller-adapter] run-loop/transcript parity guard fallback:",
                        methodName,
                        error);
                }
                return fallbackRunLoopTranscriptMethod(methodName, args);
            }
            if (mode === "speech-approval") {
                if (window.console && typeof window.console.warn === "function") {
                    window.console.warn(
                        "[chat-controller-adapter] speech/approval parity guard fallback:",
                        methodName,
                        error);
                }
                return fallbackSpeechApprovalMethod(methodName, args);
            }
            throw error;
        }
    }

    function createAdapterControllerFacade(legacyController) {
        const controllerImpl = legacyController && typeof legacyController === "object"
            ? legacyController
            : {};
        const facade = {};

        for (const key of Object.keys(controllerImpl)) {
            const value = controllerImpl[key];
            if (typeof value === "function") {
                facade[key] = function (...args) {
                    return invokeAdapterMethod(controllerImpl, key, args, "default");
                };
                continue;
            }

            try {
                Object.defineProperty(facade, key, {
                    enumerable: true,
                    configurable: true,
                    get: function () {
                        return controllerImpl[key];
                    },
                    set: function (nextValue) {
                        controllerImpl[key] = nextValue;
                    },
                });
            } catch (_) {
                facade[key] = value;
            }
        }

        for (const methodName of BRIDGE_BACKED_LOW_RISK_METHODS) {
            if (typeof controllerImpl[methodName] === "function") {
                facade[methodName] = function (...args) {
                    return invokeAdapterMethod(controllerImpl, methodName, args, "bridge-backed-low-risk");
                };
            }
        }

        for (const methodName of RUN_LOOP_TRANSCRIPT_PARITY_METHODS) {
            facade[methodName] = function (...args) {
                return invokeAdapterMethod(controllerImpl, methodName, args, "run-loop-transcript");
            };
        }

        for (const methodName of SPEECH_APPROVAL_PARITY_METHODS) {
            facade[methodName] = function (...args) {
                return invokeAdapterMethod(controllerImpl, methodName, args, "speech-approval");
            };
        }

        if (typeof facade.getAdapterParitySnapshot !== "function") {
            facade.getAdapterParitySnapshot = function () {
                return {
                    adapterVersion: "step7.0",
                    adapterMode: "legacy-deprecated-killswitch+bridge-backed-low-risk+run-loop-transcript-guards+speech-approval-guards",
                    legacyControllerEnabled: resolveLegacyControllerEnabled(),
                    bridgeBackedLowRiskMethods: BRIDGE_BACKED_LOW_RISK_METHODS.slice(),
                    runLoopTranscriptMethods: RUN_LOOP_TRANSCRIPT_PARITY_METHODS.slice(),
                    speechApprovalMethods: SPEECH_APPROVAL_PARITY_METHODS.slice(),
                };
            };
        }

        facade.__adapterVersion = "step10.0";
        facade.__adapterMode = "legacy-deprecated-killswitch+bridge-backed-low-risk+run-loop-transcript-guards+speech-approval-guards";
        facade.__adapterLegacyControllerEnabled = resolveLegacyControllerEnabled();
        facade.__adapterBridgeBackedLowRiskMethods = BRIDGE_BACKED_LOW_RISK_METHODS.slice();
        facade.__adapterRunLoopTranscriptMethods = RUN_LOOP_TRANSCRIPT_PARITY_METHODS.slice();
        facade.__adapterSpeechApprovalMethods = SPEECH_APPROVAL_PARITY_METHODS.slice();

        return facade;
    }

    function createController(options) {
        const legacyApi = getLegacyControllerApi();
        if (typeof legacyApi.createController !== "function") {
            throw new Error("BlazeClawChatControllerLegacy.createController unavailable");
        }
        const legacyController = legacyApi.createController(options);
        return createAdapterControllerFacade(legacyController);
    }

    async function runRegressionChecks() {
        const legacyApi = getLegacyControllerApi();
        if (typeof legacyApi.runRegressionChecks !== "function") {
            throw new Error("BlazeClawChatControllerLegacy.runRegressionChecks unavailable");
        }
        return legacyApi.runRegressionChecks();
    }

    window.BlazeClawChatControllerAdapter = {
        createController,
        runRegressionChecks,
    };
})();
