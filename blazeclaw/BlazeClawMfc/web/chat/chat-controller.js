(function () {
    function isSilentReplyText(text) {
        return typeof text === "string" && /^\s*NO_REPLY\s*$/i.test(text);
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

    function parseTextFromMessage(message) {
        if (typeof message === "string") {
            const trimmed = message.trim();
            if (!trimmed) {
                return "";
            }
            if (
                (trimmed.startsWith("{") && trimmed.endsWith("}")) ||
                (trimmed.startsWith("[") && trimmed.endsWith("]"))
            ) {
                try {
                    const parsed = JSON.parse(trimmed);
                    if (parsed && typeof parsed === "object") {
                        return stripChatTemplateMarkers(parseTextFromMessage(parsed));
                    }
                } catch (_) {
                    /* fall through to plain text */
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

    function isValidApprovalToken(token) {
        const normalized = String(token || "").trim();
        if (!normalized) {
            return false;
        }
        if (!/^[A-Za-z0-9:_\-]+$/.test(normalized)) {
            return false;
        }
        // Email schedule tokens currently use an email-approval-* prefix and include
        // multiple segments; this blocks streamed fragments such as "e" or "email-app".
        if (/^email-approval-\d{10,}-\d+$/.test(normalized)) {
            return true;
        }
        return normalized.length >= 24;
    }

    function parseApprovalTokenFromText(text) {
        const raw = String(text || "");
        if (!raw) {
            return null;
        }
        const tokenMatch = /approvalToken=([A-Za-z0-9:_\-]+)(?=\s|[，。！？；,.!?;]|$)/.exec(raw);
        if (!tokenMatch || !tokenMatch[1]) {
            // Fallback: tolerate missing delimiters around localized text.
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

    function parseToolErrorCodeFromOutput(output) {
        const raw = String(output || "").trim();
        if (!raw) {
            return "";
        }
        try {
            const parsed = JSON.parse(raw);
            if (parsed && parsed.error && typeof parsed.error.code === "string") {
                return parsed.error.code.trim();
            }
        } catch (_) {
            // Ignore non-JSON output and fall back to regex parsing.
        }
        const match = /"code"\s*:\s*"([^"]+)"/.exec(raw);
        if (match && match[1]) {
            return String(match[1]).trim();
        }
        return "";
    }

    function parseApprovalFailureHints(output) {
        const raw = String(output || "").trim();
        if (!raw) {
            return null;
        }

        try {
            const parsed = JSON.parse(raw);
            const error = parsed && parsed.error && typeof parsed.error === "object"
                ? parsed.error
                : null;
            if (!error) {
                return null;
            }
            return {
                code: typeof error.code === "string" ? error.code.trim() : "",
                message: typeof error.message === "string" ? error.message.trim() : "",
                remediation: typeof error.remediation === "string" ? error.remediation.trim() : "",
                missingDependency: typeof error.missingDependency === "string" ? error.missingDependency.trim() : "",
                installHint: typeof error.installHint === "string" ? error.installHint.trim() : "",
                configHint: typeof error.configHint === "string" ? error.configHint.trim() : "",
                bucket: typeof error.bucket === "string" ? error.bucket.trim() : "",
            };
        } catch (_) {
            return null;
        }
    }

    function resolveNormalizedErrorCode(responsePayload, output, parsedHints) {
        const topLevel = responsePayload && typeof responsePayload.errorCode === "string"
            ? responsePayload.errorCode.trim()
            : "";
        if (topLevel && topLevel !== "legacy_execution_failed") {
            return topLevel;
        }
        if (parsedHints && parsedHints.code) {
            return parsedHints.code;
        }
        if (topLevel) {
            return topLevel;
        }
        return parseToolErrorCodeFromOutput(output);
    }

    async function requestEmailBackendReadiness(requestImpl) {
        try {
            const response = await requestImpl("gateway.email.backend.readiness", {});
            const payload = response && response.payload && typeof response.payload === "object"
                ? response.payload
                : {};
            return {
                ready: Boolean(payload.ready),
                errorCode: typeof payload.errorCode === "string" ? payload.errorCode.trim() : "",
                remediation: typeof payload.remediation === "string" ? payload.remediation.trim() : "",
                missingDependency: typeof payload.missingDependency === "string" ? payload.missingDependency.trim() : "",
                installHint: typeof payload.installHint === "string" ? payload.installHint.trim() : "",
                configHint: typeof payload.configHint === "string" ? payload.configHint.trim() : "",
                bucket: typeof payload.bucket === "string" ? payload.bucket.trim() : "",
                message: typeof payload.message === "string" ? payload.message.trim() : "",
            };
        } catch (_) {
            return null;
        }
    }

    function dataUrlToBase64(dataUrl) {
        const match = /^data:([^;]+);base64,(.+)$/i.exec(String(dataUrl || ""));
        if (!match) {
            return null;
        }

        return {
            mimeType: match[1],
            content: match[2],
        };
    }

    const MAX_ASSISTANT_NAME = 50;
    const MAX_ASSISTANT_AVATAR = 200;
    const DEFAULT_ASSISTANT_NAME = "Assistant";
    const DEFAULT_CONTROL_UI_BOOTSTRAP_BASE_PATH = "";

    function coerceIdentityValue(value, maxLength) {
        if (typeof value !== "string") {
            return undefined;
        }

        const trimmed = value.trim();
        if (!trimmed) {
            return undefined;
        }

        if (trimmed.length <= maxLength) {
            return trimmed;
        }

        return trimmed.slice(0, maxLength);
    }

    function normalizeAssistantIdentity(input) {
        let source = {};
        if (input && typeof input === "object") {
            source = input;
        }

        let name = coerceIdentityValue(source.name, MAX_ASSISTANT_NAME);
        if (!name) {
            name = DEFAULT_ASSISTANT_NAME;
        }

        let avatar = coerceIdentityValue(source.avatar, MAX_ASSISTANT_AVATAR);
        if (!avatar) {
            avatar = null;
        }

        let agentId = null;
        if (typeof source.agentId === "string" && source.agentId.trim().length > 0) {
            agentId = source.agentId.trim();
        }

        return {
            name,
            avatar,
            agentId,
        };
    }

    function extractAssistantIdentity(response) {
        const payload =
            response && response.payload && typeof response.payload === "object"
                ? response.payload
                : response;
        if (!payload || typeof payload !== "object") {
            return {};
        }

        const nestedAgent = payload.agent && typeof payload.agent === "object"
            ? payload.agent
            : null;

        return {
            name: payload.name || (nestedAgent ? nestedAgent.name : undefined),
            avatar: payload.avatar || (nestedAgent ? nestedAgent.avatar : undefined),
            agentId: payload.agentId || (nestedAgent ? nestedAgent.id : undefined),
        };
    }

    function normalizeBootstrapBasePath(basePath) {
        if (typeof basePath !== "string") {
            return DEFAULT_CONTROL_UI_BOOTSTRAP_BASE_PATH;
        }

        const trimmed = basePath.trim();
        return trimmed;
    }

    function buildControlUiBootstrapConfigSnapshot(input) {
        let source = {};
        if (input && typeof input === "object") {
            source = input;
        }

        const normalizedIdentity = normalizeAssistantIdentity({
            name: source.assistantName,
            avatar: source.assistantAvatar,
            agentId: source.assistantAgentId,
        });

        return {
            basePath: normalizeBootstrapBasePath(source.basePath),
            assistantName: normalizedIdentity.name,
            assistantAvatar: normalizedIdentity.avatar,
        };
    }

    const MODEL_SELECTION_SCHEMA = {
        type: "object",
        properties: {
            model: { type: "string" },
        },
        additionalProperties: true,
    };

    function stableJsonNormalize(value) {
        const formUtils = window.BlazeClawConfigFormUtils;
        if (formUtils && typeof formUtils.cloneConfigObject === "function") {
            try {
                return formUtils.cloneConfigObject(value);
            } catch (_) {
            }
        }

        try {
            return JSON.parse(JSON.stringify(value));
        } catch (_) {
            return value;
        }
    }

    function coerceConfigMutationPayload(payload, schema) {
        const normalizedPayload = payload && typeof payload === "object"
            ? payload
            : {};

        const configCoerceEnabled = Boolean(window.BlazeClawConfigFormCoerce) &&
            typeof window.BlazeClawConfigFormCoerce.coerceFormValues === "function";
        if (!configCoerceEnabled) {
            return stableJsonNormalize(normalizedPayload);
        }

        const coerced = window.BlazeClawConfigFormCoerce.coerceFormValues(
            normalizedPayload,
            schema || { type: "object", additionalProperties: true });
        return stableJsonNormalize(coerced);
    }

    function createControllerLegacyImplementation(options) {
        const opts = options || {};
        const state = opts.state;
        if (!state) {
            throw new Error("chat-controller requires state");
        }

        const rawAddMessage = typeof opts.addMessage === "function" ? opts.addMessage : function () { };
        const MAX_STRUCTURED_TRANSCRIPT = 500;
        if (!Array.isArray(state.structuredTranscript)) {
            state.structuredTranscript = [];
        }

        function bubbleKindToTranscriptRole(kind) {
            if (kind === "self") {
                return "user";
            }
            if (kind === "error") {
                return "error";
            }
            return "assistant";
        }

        function toStructuredTranscriptEntry(input) {
            const source = input && typeof input === "object"
                ? input
                : {};
            const role = typeof source.role === "string"
                ? source.role.trim().toLowerCase()
                : "assistant";
            const text = String(source.text || "").trim();
            if (!text || isSilentReplyText(text)) {
                return null;
            }

            const runId = typeof source.runId === "string" ? source.runId.trim() : "";
            const entry = {
                id: typeof source.id === "string" && source.id.trim()
                    ? source.id.trim()
                    : `tx-${Date.now()}-${Math.floor(Math.random() * 10000)}`,
                role: role === "user" || role === "assistant" || role === "error"
                    ? role
                    : "assistant",
                text,
                ts: Number.isFinite(source.ts) ? Number(source.ts) : Date.now(),
                sessionKey: normalizeSessionKey(source.sessionKey || state.sessionKey),
                source: typeof source.source === "string" && source.source.trim()
                    ? source.source.trim()
                    : "ui",
            };

            if (runId) {
                entry.runId = runId;
            }
            if (typeof source.terminalState === "string" && source.terminalState.trim()) {
                entry.terminalState = source.terminalState.trim();
            }

            return entry;
        }

        function recordStructuredTranscript(input) {
            const entry = toStructuredTranscriptEntry(input);
            if (!entry) {
                return false;
            }
            state.structuredTranscript.push(entry);
            if (state.structuredTranscript.length > MAX_STRUCTURED_TRANSCRIPT) {
                state.structuredTranscript.splice(
                    0,
                    state.structuredTranscript.length - MAX_STRUCTURED_TRANSCRIPT);
            }
            return true;
        }

        function addMessage(text, kind, meta) {
            const sourceMeta = meta && typeof meta === "object"
                ? meta
                : {};

            recordStructuredTranscript({
                role: bubbleKindToTranscriptRole(kind),
                text,
                source: "ui",
                modelLabel: typeof sourceMeta.modelLabel === "string"
                    ? sourceMeta.modelLabel
                    : "",
            });
            rawAddMessage(text, kind, sourceMeta);
        }

        // Must be used for bridge / chat.event driven bubbles. The view's raw addMessage
        // (index.js) re-renders from structuredTranscript when that mode is on and does not
        // record new rows by itself, so calling it directly would drop assistant text.
        function appendChatBubble(text, kind, meta) {
            addMessage(text, kind, meta);
        }

        const addOrReplaceStream = opts.addOrReplaceStream || function () { };
        const finalizeStream = opts.finalizeStream || function () { };
        const updateComposerState = opts.updateComposerState || function () { };
        const clearMessages = opts.clearMessages || function () { };
        const setInputValue = opts.setInputValue || function () { };
        const onDetachedNotice = typeof opts.onDetachedNotice === "function"
            ? opts.onDetachedNotice
            : function () { };
        const onSessionControlStateChanged = typeof opts.onSessionControlStateChanged === "function"
            ? opts.onSessionControlStateChanged
            : function () { };
        const onCronSlashCommand = typeof opts.onCronSlashCommand === "function"
            ? opts.onCronSlashCommand
            : null;

        state.sessionOptions = Array.isArray(state.sessionOptions)
            ? state.sessionOptions
            : [];
        state.modelOptions = Array.isArray(state.modelOptions)
            ? state.modelOptions
            : [];
        state.thinkingOptions = ["low", "normal", "high"];
        state.selectedModel = state.selectedModel || "default";
        state.thinkingLevel = state.thinkingLevel || "normal";
        state.draftsBySession = state.draftsBySession || new Map();
        state.inputHistory = Array.isArray(state.inputHistory)
            ? state.inputHistory
            : [];
        state.inputHistoryIndex = Number.isInteger(state.inputHistoryIndex)
            ? state.inputHistoryIndex
            : -1;
        state.sendQueue = Array.isArray(state.sendQueue) ? state.sendQueue : [];
        state.slashCommands = Array.isArray(state.slashCommands)
            ? state.slashCommands
            : [];
        state.slashCommandsLoaded = Boolean(state.slashCommandsLoaded);
        state.terminalRunStates = state.terminalRunStates || new Map();
        state.reconcileTimer = state.reconcileTimer || null;
        state.reconcileFollowupTimers = Array.isArray(state.reconcileFollowupTimers)
            ? state.reconcileFollowupTimers
            : [];
        state.runWatchdogTimer = state.runWatchdogTimer || null;
        state.runWatchdogStartedAtMs = Number.isFinite(state.runWatchdogStartedAtMs)
            ? Number(state.runWatchdogStartedAtMs)
            : 0;
        state.runWatchdogLastInboundEventMs = Number.isFinite(state.runWatchdogLastInboundEventMs)
            ? Number(state.runWatchdogLastInboundEventMs)
            : 0;
        state.runWatchdogLastReconcileMs = Number.isFinite(state.runWatchdogLastReconcileMs)
            ? Number(state.runWatchdogLastReconcileMs)
            : 0;
        state.runWatchdogLastWarningMs = Number.isFinite(state.runWatchdogLastWarningMs)
            ? Number(state.runWatchdogLastWarningMs)
            : 0;
        state.operatorDiagnosticsCounters =
            state.operatorDiagnosticsCounters && typeof state.operatorDiagnosticsCounters === "object"
                ? state.operatorDiagnosticsCounters
                : {};
        state.operatorDiagnosticsLastEmitMs =
            state.operatorDiagnosticsLastEmitMs && typeof state.operatorDiagnosticsLastEmitMs === "object"
                ? state.operatorDiagnosticsLastEmitMs
                : {};
        state.sessionSubscribed = Boolean(state.sessionSubscribed);
        state.onPolledChatEvents = typeof state.onPolledChatEvents === "function"
            ? state.onPolledChatEvents
            : null;
        state.sessionCompactionItems = Array.isArray(state.sessionCompactionItems)
            ? state.sessionCompactionItems
            : [];
        state.sessionCompactionSelection = typeof state.sessionCompactionSelection === "string"
            ? state.sessionCompactionSelection
            : "";
        state.sessionCompactionStatus = typeof state.sessionCompactionStatus === "string"
            ? state.sessionCompactionStatus
            : "";
        state.assistantIdentityRequestSeq = Number.isInteger(state.assistantIdentityRequestSeq)
            ? state.assistantIdentityRequestSeq
            : 0;
        if (typeof state.assistantName !== "string" || !state.assistantName.trim()) {
            state.assistantName = DEFAULT_ASSISTANT_NAME;
        }
        if (typeof state.assistantAvatar !== "string" && state.assistantAvatar !== null) {
            state.assistantAvatar = "A";
        }
        if (typeof state.assistantAgentId !== "string" && state.assistantAgentId !== null) {
            state.assistantAgentId = null;
        }
        state.speechCapabilities = state.speechCapabilities && typeof state.speechCapabilities === "object"
            ? { ...state.speechCapabilities }
            : {
                sttSupported: false,
                sttReady: false,
                transcriptSupportsSegments: false,
                transcriptSupportsInterim: false,
                transcriptSupportsFinal: true,
                streamingPreviewEnabled: false,
                ttsSupported: false,
                ttsReady: false,
                lifecycle: [],
                loaded: false,
                error: "",
            };
        state.speechSessionState = state.speechSessionState && typeof state.speechSessionState === "object"
            ? { ...state.speechSessionState }
            : {
                stage: "idle",
                text: "",
                segmentText: "",
                segmentFinal: true,
                segmentSequence: 0,
                runId: "",
                sessionId: "",
                errorCode: "",
                errorMessage: "",
                errorClass: "status",
                retryable: false,
                retryStrategy: "immediate",
                retryGuidance: "",
                updatedAtMs: 0,
            };
        state.speechErrorPolicy = state.speechErrorPolicy && typeof state.speechErrorPolicy === "object"
            ? { ...state.speechErrorPolicy }
            : {
                loaded: false,
                defaultClass: "status",
                map: {},
                retry: {},
            };

        function nextId() {
            return `web-${Date.now()}-${Math.floor(Math.random() * 10000)}`;
        }

        function normalizeSessionKey(value) {
            const trimmed = String(value || "").trim();
            return trimmed || "main";
        }

        function currentInputText() {
            return state.inputEl ? String(state.inputEl.value || "") : "";
        }

        function setCurrentInputText(text) {
            if (state.inputEl) {
                state.inputEl.value = text;
            }
            setInputValue(text);
            updateComposerState();
        }

        function persistDraftForSession() {
            const key = normalizeSessionKey(state.sessionKey);
            state.draftsBySession.set(key, currentInputText());
        }

        function restoreDraftForSession() {
            const key = normalizeSessionKey(state.sessionKey);
            const draft = state.draftsBySession.get(key) || "";
            setCurrentInputText(draft);
        }

        function pushInputHistory(message) {
            const trimmed = String(message || "").trim();
            if (!trimmed || trimmed.startsWith("/")) {
                return;
            }

            const existingIndex = state.inputHistory.findIndex(function (item) {
                return item === trimmed;
            });
            if (existingIndex >= 0) {
                state.inputHistory.splice(existingIndex, 1);
            }

            state.inputHistory.unshift(trimmed);
            if (state.inputHistory.length > 40) {
                state.inputHistory.length = 40;
            }
            state.inputHistoryIndex = -1;
        }

        function recallInputHistory(direction) {
            if (!state.inputHistory.length) {
                return "";
            }

            const next = state.inputHistoryIndex + direction;
            if (next < 0) {
                state.inputHistoryIndex = -1;
                return "";
            }

            if (next >= state.inputHistory.length) {
                state.inputHistoryIndex = state.inputHistory.length - 1;
                return state.inputHistory[state.inputHistoryIndex] || "";
            }

            state.inputHistoryIndex = next;
            return state.inputHistory[state.inputHistoryIndex] || "";
        }

        function post(message) {
            if (!window.chrome || !window.chrome.webview) {
                state.bridgeQueue.push(message);
                return;
            }

            window.chrome.webview.postMessage(message);
        }

        function flushQueue() {
            while (state.bridgeQueue.length > 0) {
                post(state.bridgeQueue.shift());
            }
        }

        function request(method, params) {
            return new Promise((resolve, reject) => {
                const id = nextId();
                state.pending.set(id, {
                    resolve,
                    reject,
                    method: String(method || ""),
                });
                post({ channel: "blazeclaw.gateway.rpc", id, method, params });
            });
        }

        function requestWithOverride(method, params, overrideRequest) {
            if (typeof overrideRequest === "function") {
                return overrideRequest(method, params);
            }
            return request(method, params);
        }

        function normalizeSpeechCapabilitiesPayload(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const stt = source.stt && typeof source.stt === "object"
                ? source.stt
                : {};
            const transcript = source.transcript && typeof source.transcript === "object"
                ? source.transcript
                : {};
            const tts = source.tts && typeof source.tts === "object"
                ? source.tts
                : {};
            const lifecycle = Array.isArray(source.lifecycle)
                ? source.lifecycle
                    .map((value) => String(value || "").trim())
                    .filter((value) => value.length > 0)
                : [];

            return {
                sttSupported: Boolean(stt.supported),
                sttReady: Boolean(stt.ready),
                audioHandoffMode: String(stt.audioHandoffMode || "dual").trim() || "dual",
                streamingSupported: Boolean(stt.streamingSupported),
                streamingPreviewEnabled: stt.streamingPreviewEnabled === true,
                livePreviewToggleEnabled: stt.livePreviewToggleEnabled === true,
                livePreviewToggleSource: String(stt.livePreviewToggleSource || "").trim(),
                streamingConfigured: Boolean(stt.streamingConfigured),
                modelNativeVad: Boolean(stt.modelNativeVad),
                streamingChunkMs: Number.isFinite(Number(stt.streamingChunkMs))
                    ? Number(stt.streamingChunkMs)
                    : 0,
                streamingLookbackMs: Number.isFinite(Number(stt.streamingLookbackMs))
                    ? Number(stt.streamingLookbackMs)
                    : 0,
                provider: String(stt.provider || "").trim(),
                effectiveExecutionProvider: String(stt.effectiveExecutionProvider || "").trim(),
                cudaExecutionProviderAvailable: Boolean(stt.cudaExecutionProviderAvailable),
                cudaExecutionProviderEnabled: Boolean(stt.cudaExecutionProviderEnabled),
                cudaExecutionProviderReason: String(stt.cudaExecutionProviderReason || "").trim(),
                transcriptSupportsSegments: Boolean(transcript.supportsSegments),
                transcriptSupportsInterim: Boolean(transcript.supportsInterim),
                transcriptSupportsFinal: transcript.supportsFinal !== false,
                ttsSupported: Boolean(tts.supported),
                ttsReady: Boolean(tts.ready),
                lifecycle,
                loaded: true,
                error: "",
            };
        }

        function normalizeSpeechSessionPayload(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const speechSession = source.speechSession && typeof source.speechSession === "object"
                ? source.speechSession
                : source;
            const speechRuntime = speechSession.speechRuntime && typeof speechSession.speechRuntime === "object"
                ? speechSession.speechRuntime
                : source.speechRuntime && typeof source.speechRuntime === "object"
                    ? source.speechRuntime
                    : null;
            const firstTokenTiming = speechSession.firstTokenTiming && typeof speechSession.firstTokenTiming === "object"
                ? speechSession.firstTokenTiming
                : source.firstTokenTiming && typeof source.firstTokenTiming === "object"
                    ? source.firstTokenTiming
                    : null;
            const segment = speechSession.segment && typeof speechSession.segment === "object"
                ? speechSession.segment
                : null;
            const stage = String(speechSession.stage || "").trim() || "idle";
            const sessionText = String(speechSession.text || source.text || source.transcript || "").trim();
            const segmentText = segment
                ? String(segment.text || "").trim()
                : "";
            let segmentSequence = 0;
            if (segment && Number.isFinite(Number(segment.sequence))) {
                segmentSequence = Number(segment.sequence);
            }

            const runId = String(speechSession.runId || source.runId || "").trim();
            const previewRunActive = runId.startsWith("speech-preview-") && stage === "streaming";
            const stageImpliesSegmentFinal = !previewRunActive && (
                stage === "segment_finalized" ||
                stage === "stopped" ||
                stage === "completed" ||
                stage === "failed" ||
                stage === "cancelled");

            const segmentFinal = segment
                ? Boolean(segment.final)
                : stageImpliesSegmentFinal;

            return {
                stage,
                text: sessionText,
                segmentText,
                segmentFinal,
                segmentSequence,
                runId,
                sessionId: String(speechSession.sessionId || source.sessionId || "").trim(),
                audioPath: String(speechSession.audioPath || source.audioPath || "").trim(),
                audioArtifact: speechSession.audioArtifact && typeof speechSession.audioArtifact === "object"
                    ? { ...speechSession.audioArtifact }
                    : source.audioArtifact && typeof source.audioArtifact === "object"
                        ? { ...source.audioArtifact }
                        : null,
                language: String(speechSession.language || source.language || "").trim(),
                latencyMs: Number.isFinite(Number(speechSession.latencyMs || source.latencyMs))
                    ? Number(speechSession.latencyMs || source.latencyMs)
                    : 0,
                cancelled: Boolean(speechSession.cancelled || source.cancelled),
                errorCode: String(source.errorCode || "").trim(),
                errorMessage: String(source.errorMessage || "").trim(),
                errorClass: String(source.errorClass || "status").trim() || "status",
                retryable: Boolean(source.retry && source.retry.retryable),
                retryStrategy: String(source.retry && source.retry.strategy || "immediate").trim() || "immediate",
                retryGuidance: String(source.retry && source.retry.guidance || "").trim(),
                speechRuntime: speechRuntime ? { ...speechRuntime } : null,
                provider: String(speechRuntime && speechRuntime.provider || "").trim(),
                effectiveExecutionProvider: String(speechRuntime && speechRuntime.effectiveExecutionProvider || "").trim(),
                cudaExecutionProviderAvailable: Boolean(speechRuntime && speechRuntime.cudaExecutionProviderAvailable),
                cudaExecutionProviderEnabled: Boolean(speechRuntime && speechRuntime.cudaExecutionProviderEnabled),
                cudaExecutionProviderReason: String(speechRuntime && speechRuntime.cudaExecutionProviderReason || "").trim(),
                firstTokenTiming: firstTokenTiming ? { ...firstTokenTiming } : null,
                gatewayNativePayloadReadyOffsetMs: Number.isFinite(Number(source.gatewayNativePayloadReadyOffsetMs))
                    ? Number(source.gatewayNativePayloadReadyOffsetMs)
                    : 0,
                preflight: source.preflight && typeof source.preflight === "object"
                    ? { ...source.preflight }
                    : null,
                noSpeechTriage: source.noSpeechTriage && typeof source.noSpeechTriage === "object"
                    ? { ...source.noSpeechTriage }
                    : null,
                updatedAtMs: Date.now(),
            };
        }

        function normalizeSpeechErrorCode(rawCode) {
            return String(rawCode || "")
                .trim()
                .toLowerCase()
                .replace(/\s+/g, "_");
        }

        function inferNoSpeechSignal(sessionStateLike) {
            const source = sessionStateLike && typeof sessionStateLike === "object"
                ? sessionStateLike
                : {};
            const normalizedCode = normalizeSpeechErrorCode(source.errorCode);
            if (normalizedCode === "no_speech_detected") {
                return true;
            }

            const message = String(source.errorMessage || "").toLowerCase();
            if (message.includes("no_speech_detected")) {
                return true;
            }

            const debugInfo = source.debugInfo && typeof source.debugInfo === "object"
                ? source.debugInfo
                : null;
            if (debugInfo && String(debugInfo.sherpaFinalOutcome || "").trim() === "no_speech_detected") {
                return true;
            }

            const preflight = source.preflight && typeof source.preflight === "object"
                ? source.preflight
                : null;
            const healthIndex = Number(preflight && preflight.healthIndex);
            return Number.isFinite(healthIndex) && healthIndex > 0 && healthIndex < 55 &&
                (normalizedCode === "inference_failed" || message.includes("final transcript unavailable"));
        }

        function normalizeSpeechErrorPolicyPayload(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const errorMap = source.map && typeof source.map === "object"
                ? source.map
                : {};
            const retry = source.retry && typeof source.retry === "object"
                ? source.retry
                : {};
            return {
                loaded: true,
                defaultClass: String(source.defaultClass || "status").trim() || "status",
                map: { ...errorMap },
                retry: { ...retry },
            };
        }

        function isActiveSpeechPreviewStage(stage) {
            return stage === "recording" ||
                stage === "start_stream" ||
                stage === "streaming" ||
                stage === "queued";
        }

        function isSpeechPreviewRunId(runId) {
            return String(runId || "").trim().startsWith("speech-preview-");
        }

        function isSpeechFinalRunId(runId) {
            return String(runId || "").trim().startsWith("speech-final-");
        }

        function hasSpeechFinalAuthority(sessionState) {
            const source = sessionState && typeof sessionState === "object"
                ? sessionState
                : {};
            const finalRunId = String(source.finalRunId || "").trim();
            const runId = String(source.runId || "").trim();
            return isSpeechFinalRunId(finalRunId) || isSpeechFinalRunId(runId);
        }

        function extractFinalOwnedTranscriptText(payload, requestedRunId) {
            const result = describeFinalOwnedTranscript(payload, requestedRunId);
            return result.text;
        }

        function describeFinalOwnedTranscript(payload, requestedRunId) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const requested = String(requestedRunId || "").trim();
            const speechSession = source.speechSession && typeof source.speechSession === "object"
                ? source.speechSession
                : null;
            const segment = speechSession && speechSession.segment && typeof speechSession.segment === "object"
                ? speechSession.segment
                : source.segment && typeof source.segment === "object"
                    ? source.segment
                    : null;

            const ownerRunId = String(
                source.executionRunId ||
                source.runId ||
                speechSession && speechSession.runId ||
                "").trim();
            const finalOwned = !isSpeechPreviewRunId(ownerRunId) ||
                (requested && ownerRunId === requested);
            const topLevelText = String(source.text || source.transcript || "").trim();
            const nestedText = String(
                speechSession && (speechSession.text || speechSession.transcript) ||
                "").trim();
            const segmentText = String(segment && segment.text || "").trim();
            if (!finalOwned) {
                return {
                    text: "",
                    ownerRunId,
                    finalOwned,
                    hasTopLevelText: Boolean(topLevelText),
                    hasNestedText: Boolean(nestedText),
                    hasSegmentText: Boolean(segmentText),
                    source: "preview_owned",
                };
            }

            const text = topLevelText || nestedText || segmentText;
            return {
                text,
                ownerRunId,
                finalOwned,
                hasTopLevelText: Boolean(topLevelText),
                hasNestedText: Boolean(nestedText),
                hasSegmentText: Boolean(segmentText),
                source: topLevelText
                    ? "top_level"
                    : nestedText
                        ? "speech_session"
                        : segmentText
                            ? "segment"
                            : "none",
            };
        }

        function isActiveSpeechPreviewState(sessionState) {
            const source = sessionState && typeof sessionState === "object"
                ? sessionState
                : {};
            const stage = String(source.stage || "").trim();
            if (stage === "recording" || stage === "start_stream" || stage === "streaming") {
                return true;
            }

            return stage === "queued" && isSpeechPreviewRunId(source.runId);
        }

        function isSpeechPreviewUpdate(normalized) {
            const source = normalized && typeof normalized === "object"
                ? normalized
                : {};
            return isSpeechPreviewRunId(source.runId);
        }

        function isPreviewUpdateBlockedByFinalAuthority(normalized, previous) {
            if (!isSpeechPreviewUpdate(normalized)) {
                return false;
            }

            const current = previous && typeof previous === "object"
                ? previous
                : {};
            if (isSpeechFinalRunId(current.finalRunId) ||
                isSpeechFinalRunId(current.runId)) {
                return true;
            }

            return !isActiveSpeechPreviewState(previous);
        }

        function isStaleSpeechPreviewUpdate(normalized, previous) {
            const next = normalized && typeof normalized === "object"
                ? normalized
                : {};
            const current = previous && typeof previous === "object"
                ? previous
                : {};
            if (next.stage !== "streaming" || next.segmentFinal) {
                return false;
            }

            const previousStage = String(current.stage || "").trim();
            if (previousStage && !isActiveSpeechPreviewState(current)) {
                return true;
            }

            const nextRunId = String(next.runId || "").trim();
            const previousRunId = String(current.runId || "").trim();
            if (isSpeechPreviewRunId(nextRunId) &&
                isSpeechPreviewRunId(previousRunId) &&
                nextRunId !== previousRunId) {
                return true;
            }

            const nextSessionId = String(next.sessionId || "").trim();
            const previousSessionId = String(current.sessionId || "").trim();
            if (nextSessionId && previousSessionId && nextSessionId !== previousSessionId) {
                return true;
            }

            const nextSequence = Number(next.segmentSequence || 0);
            const previousSequence = Number(current.segmentSequence || 0);
            if (nextSequence > 0 && previousSequence > 0 && nextSequence < previousSequence) {
                return true;
            }

            const nextText = String(next.segmentText || next.text || "").trim();
            const previousText = String(current.segmentText || current.text || "").trim();
            return nextSequence > 0 &&
                previousSequence > 0 &&
                nextSequence === previousSequence &&
                nextText !== "" &&
                nextText === previousText;
        }

        function isEmptyTerminalPreviewUpdate(normalized, previous) {
            const next = normalized && typeof normalized === "object"
                ? normalized
                : {};
            const current = previous && typeof previous === "object"
                ? previous
                : {};
            const nextStage = String(next.stage || "").trim();
            if (nextStage !== "completed" && nextStage !== "segment_finalized") {
                return false;
            }

            if (!isSpeechPreviewRunId(next.runId)) {
                return false;
            }

            const previousStage = String(current.stage || "").trim();
            if (!isActiveSpeechPreviewState(current)) {
                return false;
            }

            return !String(next.segmentText || next.text || "").trim() &&
                !String(next.errorCode || "").trim();
        }

        function isPreviewTerminalUpdateWhileRecording(normalized, previous) {
            const next = normalized && typeof normalized === "object"
                ? normalized
                : {};
            const current = previous && typeof previous === "object"
                ? previous
                : {};
            const nextStage = String(next.stage || "").trim();
            if (nextStage !== "completed" && nextStage !== "segment_finalized") {
                return false;
            }
            if (!isSpeechPreviewRunId(next.runId)) {
                return false;
            }

            return isActiveSpeechPreviewState(current);
        }

        function classifySpeechError(errorCode, fallbackClass) {
            const normalizedCode = normalizeSpeechErrorCode(errorCode);
            const policy = state.speechErrorPolicy && typeof state.speechErrorPolicy === "object"
                ? state.speechErrorPolicy
                : null;
            const defaultClass = policy && typeof policy.defaultClass === "string" && policy.defaultClass.trim()
                ? policy.defaultClass.trim()
                : "status";
            let resolvedClass = String(fallbackClass || "").trim();
            if (!resolvedClass && policy && policy.map && typeof policy.map === "object") {
                const mapped = policy.map[normalizedCode] || policy.map[String(errorCode || "").trim()] || policy.map.none;
                if (typeof mapped === "string" && mapped.trim()) {
                    resolvedClass = mapped.trim();
                }
            }
            if (!resolvedClass) {
                resolvedClass = defaultClass;
            }
            const retryDescriptor = policy && policy.retry && typeof policy.retry === "object"
                ? policy.retry[normalizedCode]
                : null;
            return {
                code: normalizedCode,
                behaviorClass: resolvedClass,
                retryDescriptor: retryDescriptor && typeof retryDescriptor === "object"
                    ? retryDescriptor
                    : null,
            };
        }

        async function loadSpeechErrorPolicy(options) {
            const opts = options && typeof options === "object"
                ? options
                : {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : null;
            try {
                const response = await requestWithOverride(
                    "speech.errorPolicy.get",
                    {},
                    requestOverride);
                const payload = response && response.payload && typeof response.payload === "object"
                    ? response.payload
                    : {};
                state.speechErrorPolicy = normalizeSpeechErrorPolicyPayload(payload);
            } catch (_) {
                state.speechErrorPolicy = {
                    loaded: true,
                    defaultClass: "status",
                    map: {},
                    retry: {},
                };
            }
            return { ...state.speechErrorPolicy };
        }

        async function loadSpeechCapabilities(options) {
            const opts = options && typeof options === "object"
                ? options
                : {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : null;

            try {
                const response = await requestWithOverride(
                    "speech.capabilities.get",
                    {},
                    requestOverride);
                const payload = response && response.payload && typeof response.payload === "object"
                    ? response.payload
                    : {};
                state.speechCapabilities = normalizeSpeechCapabilitiesPayload(payload);
            } catch (error) {
                state.speechCapabilities = {
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
                    streamingPreviewEnabled: false,
                    ttsSupported: false,
                    ttsReady: false,
                    lifecycle: [],
                    loaded: true,
                    error: String(error || "speech capability request failed"),
                };
            }

            void loadSpeechErrorPolicy({
                requestOverride,
            }).catch(() => {
            });
            updateComposerState();
            return { ...state.speechCapabilities };
        }

        function getSpeechCapabilitiesSnapshot() {
            return state.speechCapabilities && typeof state.speechCapabilities === "object"
                ? { ...state.speechCapabilities }
                : {
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
                streamingPreviewEnabled: false,
                    ttsSupported: false,
                    ttsReady: false,
                    lifecycle: [],
                    loaded: false,
                    error: "",
                };
        }

        function getSpeechSessionStateSnapshot() {
            return state.speechSessionState && typeof state.speechSessionState === "object"
                ? { ...state.speechSessionState }
                : {
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
                    speechRuntime: null,
                    provider: "",
                    effectiveExecutionProvider: "",
                    cudaExecutionProviderAvailable: false,
                    cudaExecutionProviderEnabled: false,
                    cudaExecutionProviderReason: "",
                    firstTokenTiming: null,
                    gatewayNativePayloadReadyOffsetMs: 0,
                    updatedAtMs: 0,
                };
        }

        function emitSessionControlState() {
            onSessionControlStateChanged({
                sessionKey: normalizeSessionKey(state.sessionKey),
                subscribed: Boolean(state.sessionSubscribed),
                compactions: Array.isArray(state.sessionCompactionItems)
                    ? state.sessionCompactionItems.slice()
                    : [],
                selectedCompactionId: String(state.sessionCompactionSelection || ""),
                status: String(state.sessionCompactionStatus || ""),
            });
        }

        async function loadHistory() {
            try {
                state.streamText = "";
                state.runId = null;
                finalizeStream();
                clearMessages();
                state.structuredTranscript = [];
                state.streamTranscriptDraft = null;

                const response = await request("chat.history", {
                    sessionKey: state.sessionKey,
                    limit: 200,
                });

                if (response && response.payload) {
                    if (typeof response.payload.thinkingLevel === "string" &&
                        response.payload.thinkingLevel.trim().length > 0) {
                        state.thinkingLevel = response.payload.thinkingLevel.trim();
                    }
                }

                const messages = Array.isArray(response && response.payload && response.payload.messages)
                    ? response.payload.messages
                    : Array.isArray(response && response.messages)
                        ? response.messages
                        : [];

                for (const message of messages) {
                    const role = typeof message.role === "string" ? message.role : "assistant";
                    const text = parseTextFromMessage(message);
                    if (!text || isSilentReplyText(text)) {
                        continue;
                    }

                    recordStructuredTranscript({
                        id: typeof message.id === "string" ? message.id : undefined,
                        role,
                        text,
                        runId: typeof message.runId === "string" ? message.runId : undefined,
                        sessionKey: typeof message.sessionKey === "string"
                            ? message.sessionKey
                            : state.sessionKey,
                        ts: Number.isFinite(message.ts) ? Number(message.ts) : Date.now(),
                        source: "history",
                        terminalState: typeof message.state === "string" ? message.state : undefined,
                    });
                    rawAddMessage(text, role === "user" ? "self" : "peer");
                }
            } catch (error) {
                addMessage(`history error: ${String(error)}`, "error", { source: "history" });
            }
        }

        async function loadSessionOptions() {
            try {
                const response = await request("sessions.list", {
                    active: true,
                });

                const sessions = Array.isArray(response && response.payload && response.payload.sessions)
                    ? response.payload.sessions
                    : [];

                state.sessionOptions = sessions.length > 0
                    ? sessions.map((session) => ({
                        id: String(session.id || "main"),
                        scope: String(session.scope || "chat"),
                        active: Boolean(session.active),
                    }))
                    : [{ id: "main", scope: "chat", active: true }];

                const hasSelected = state.sessionOptions.some((item) => item.id === state.sessionKey);
                if (!hasSelected && state.sessionOptions.length > 0) {
                    state.sessionKey = state.sessionOptions[0].id;
                }
            } catch (_) {
                state.sessionOptions = [{ id: "main", scope: "chat", active: true }];
            }

            return state.sessionOptions;
        }

        async function subscribeSessionUpdates(options) {
            const opts = options && typeof options === "object"
                ? options
                : {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : null;
            try {
                await requestWithOverride("sessions.subscribe", {
                    sessionKey: state.sessionKey,
                }, requestOverride);
                state.sessionSubscribed = true;
                state.sessionCompactionStatus = `subscribed to ${normalizeSessionKey(state.sessionKey)}`;
                emitSessionControlState();
                updateComposerState();
                return true;
            } catch (error) {
                state.sessionSubscribed = false;
                state.sessionCompactionStatus = `subscribe error: ${String(error)}`;
                emitSessionControlState();
                updateComposerState();
                return false;
            }
        }

        async function unsubscribeSessionUpdates(options) {
            const opts = options && typeof options === "object"
                ? options
                : {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : null;
            try {
                await requestWithOverride("sessions.unsubscribe", {
                    sessionKey: state.sessionKey,
                }, requestOverride);
                state.sessionSubscribed = false;
                state.sessionCompactionStatus = `unsubscribed from ${normalizeSessionKey(state.sessionKey)}`;
                emitSessionControlState();
                updateComposerState();
                return true;
            } catch (error) {
                state.sessionCompactionStatus = `unsubscribe error: ${String(error)}`;
                emitSessionControlState();
                updateComposerState();
                return false;
            }
        }

        async function loadSessionCompactions(options) {
            const opts = options && typeof options === "object"
                ? options
                : {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : null;
            const quiet = opts.quiet === true;
            try {
                const response = await requestWithOverride("sessions.compaction.list", {
                    sessionKey: state.sessionKey,
                }, requestOverride);
                const rawItems = Array.isArray(response && response.payload && response.payload.compactions)
                    ? response.payload.compactions
                    : Array.isArray(response && response.compactions)
                        ? response.compactions
                        : [];
                state.sessionCompactionItems = rawItems.map((item, index) => {
                    const row = item && typeof item === "object" ? item : {};
                    const id = String(row.id || row.branchId || `compaction-${index + 1}`);
                    const label = String(row.label || row.summary || row.title || id);
                    return {
                        id,
                        label,
                    };
                });
                const selectedExists = state.sessionCompactionItems.some((item) => item.id === state.sessionCompactionSelection);
                if (!selectedExists) {
                    state.sessionCompactionSelection = state.sessionCompactionItems.length > 0
                        ? state.sessionCompactionItems[0].id
                        : "";
                }
                if (!quiet) {
                    state.sessionCompactionStatus = state.sessionCompactionItems.length > 0
                        ? `loaded ${state.sessionCompactionItems.length} compactions`
                        : "no compactions available";
                }
                emitSessionControlState();
                updateComposerState();
                return state.sessionCompactionItems.slice();
            } catch (error) {
                state.sessionCompactionItems = [];
                state.sessionCompactionSelection = "";
                if (!quiet) {
                    state.sessionCompactionStatus = `compaction load error: ${String(error)}`;
                }
                emitSessionControlState();
                updateComposerState();
                return [];
            }
        }

        async function refreshSessionControlState(options) {
            const opts = options && typeof options === "object"
                ? options
                : {};
            await loadSessionOptions();
            await loadSessionCompactions({
                quiet: opts.quiet === true,
            });
            emitSessionControlState();
            updateComposerState();
        }

        function selectSessionCompaction(compactionId) {
            const normalized = String(compactionId || "").trim();
            state.sessionCompactionSelection = normalized;
            emitSessionControlState();
            updateComposerState();
        }

        async function branchSessionCompaction(options) {
            const opts = options && typeof options === "object"
                ? options
                : {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : null;
            const compactionId = String(opts.compactionId || state.sessionCompactionSelection || "").trim();
            try {
                const response = await requestWithOverride("sessions.compaction.branch", {
                    sessionKey: state.sessionKey,
                    compactionId,
                }, requestOverride);
                const branchId = response && response.payload && typeof response.payload.branchId === "string"
                    ? response.payload.branchId
                    : (response && typeof response.branchId === "string" ? response.branchId : "");
                state.sessionCompactionStatus = branchId
                    ? `branched compaction: ${branchId}`
                    : "branched compaction";
                emitSessionControlState();
                updateComposerState();
                return true;
            } catch (error) {
                state.sessionCompactionStatus = `branch error: ${String(error)}`;
                emitSessionControlState();
                updateComposerState();
                return false;
            }
        }

        async function restoreSessionCompaction(options) {
            const opts = options && typeof options === "object"
                ? options
                : {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : null;
            const compactionId = String(opts.compactionId || state.sessionCompactionSelection || "").trim();
            try {
                await requestWithOverride("sessions.compaction.restore", {
                    sessionKey: state.sessionKey,
                    compactionId,
                }, requestOverride);
                state.sessionCompactionStatus = compactionId
                    ? `restored compaction: ${compactionId}`
                    : "restored compaction";
                emitSessionControlState();
                updateComposerState();
                await loadHistory();
                return true;
            } catch (error) {
                state.sessionCompactionStatus = `restore error: ${String(error)}`;
                emitSessionControlState();
                updateComposerState();
                return false;
            }
        }

        async function loadAssistantIdentity(opts) {
            if (!state.connected || !state.bridgeAvailable) {
                return;
            }

            const options = opts && typeof opts === "object"
                ? opts
                : {};
            const requestedSession = options.sessionKey;
            const sessionKey = normalizeSessionKey(
                requestedSession || state.sessionKey);
            const params = sessionKey ? { sessionKey } : {};
            const requestSeq = state.assistantIdentityRequestSeq + 1;
            state.assistantIdentityRequestSeq = requestSeq;

            try {
                const response = await requestWithOverride(
                    "agent.identity.get",
                    params,
                    options.requestOverride);
                if (!response) {
                    return;
                }

                if (requestSeq !== state.assistantIdentityRequestSeq) {
                    return;
                }

                const normalized = normalizeAssistantIdentity(
                    extractAssistantIdentity(response));
                state.assistantName = normalized.name;
                state.assistantAvatar = normalized.avatar;
                state.assistantAgentId = normalized.agentId;
            } catch (_) {
                // Keep last known assistant identity on errors.
            }
        }

        async function getControlUiBootstrapConfig(opts) {
            const options = opts && typeof opts === "object"
                ? opts
                : {};

            if (options.refreshIdentity === true) {
                await loadAssistantIdentity({
                    sessionKey: options.sessionKey,
                    requestOverride: options.requestOverride,
                });
            }

            return buildControlUiBootstrapConfigSnapshot({
                basePath: options.basePath,
                assistantName: state.assistantName,
                assistantAvatar: state.assistantAvatar,
                assistantAgentId: state.assistantAgentId,
            });
        }

        async function switchSession(sessionKey) {
            const nextSession = normalizeSessionKey(sessionKey);
            if (nextSession === state.sessionKey) {
                restoreDraftForSession();
                if (state.sessionSelect) {
                    state.sessionSelect.value = state.sessionKey;
                }
                if (state.connected && state.bridgeAvailable) {
                    void getControlUiBootstrapConfig({
                        refreshIdentity: true,
                        sessionKey: state.sessionKey,
                    });
                }
                return;
            }

            persistDraftForSession();
            state.sessionKey = nextSession;
            state.runId = null;
            state.streamText = "";
            finalizeStream();
            restoreDraftForSession();
            await loadHistory();
            if (state.sessionSubscribed) {
                await subscribeSessionUpdates();
            }
            await loadSessionCompactions();
            void getControlUiBootstrapConfig({
                refreshIdentity: true,
                sessionKey: state.sessionKey,
            });
            if (state.sessionSelect) {
                state.sessionSelect.value = state.sessionKey;
            }
            updateComposerState();
        }

        async function loadModelOptions() {
            try {
                const response = await request("models.list", {});
                const models = Array.isArray(response && response.payload && response.payload.models)
                    ? response.payload.models
                    : [];

                state.modelOptions = models.length > 0
                    ? models.map((model) => ({
                        id: String(model.id || "default"),
                        label: String(model.label || model.id || "default"),
                    }))
                    : [{ id: "default", label: "default" }];
            } catch (_) {
                state.modelOptions = [{ id: "default", label: "default" }];
            }

            try {
                const configResponse = await request("config.get", {});
                const model =
                    configResponse &&
                        configResponse.payload &&
                        configResponse.payload.agent &&
                        typeof configResponse.payload.agent.model === "string"
                        ? configResponse.payload.agent.model
                        : "";
                if (model) {
                    state.selectedModel = model;
                }
            } catch (_) {
            }

            const modelExists = state.modelOptions.some((item) => item.id === state.selectedModel);
            if (!modelExists && state.modelOptions.length > 0) {
                state.selectedModel = state.modelOptions[0].id;
            }

            return state.modelOptions;
        }

        function loadThinkingOptions() {
            return state.thinkingOptions.slice();
        }

        async function applyModelSelection(modelId, options) {
            const nextModel = String(modelId || "").trim();
            if (!nextModel) {
                return false;
            }

            const opts = options && typeof options === "object"
                ? options
                : {};

            state.selectedModel = nextModel;
            try {
                const payload = {
                    model: nextModel,
                };
                const shouldCoercePayload = Boolean(state.configCoerceEnabled);
                const params = shouldCoercePayload
                    ? coerceConfigMutationPayload(payload, MODEL_SELECTION_SCHEMA)
                    : stableJsonNormalize(payload);
                await requestWithOverride("config.set", params, opts.requestOverride);
            } catch (error) {
                addMessage(`model update error: ${String(error)}`, "error", { source: "model" });
                return false;
            }

            return true;
        }

        function applyThinkingLevel(level) {
            const normalized = String(level || "").trim().toLowerCase();
            if (!normalized) {
                return false;
            }

            state.thinkingLevel = normalized;
            return true;
        }

        async function ensureSlashCommandsLoaded() {
            if (state.slashCommandsLoaded) {
                return state.slashCommands;
            }

            const builtins = [
                { name: "help", description: "Show local slash commands" },
                { name: "clear", description: "Clear chat transcript in the current view" },
                { name: "new", description: "Create a new chat session" },
                { name: "session", description: "Switch active session" },
                { name: "model", description: "Switch active model" },
                { name: "thinking", description: "Set thinking level: low, normal, high" },
                { name: "btw", description: "Send detached side-channel message (no local user bubble)" },
                { name: "abort", description: "Abort active run" },
            ];
            if (state.cronCliEnabled) {
                builtins.push({
                    name: "cron",
                    description: "Cron jobs CLI (status, list, run, help)",
                });
            }

            let skillCommands = [];
            try {
                const response = await request("skills.commands", {});
                const commands = Array.isArray(response && response.payload && response.payload.commands)
                    ? response.payload.commands
                    : [];
                skillCommands = commands
                    .map((entry) => ({
                        name: String(entry.name || "").replace(/^\//, ""),
                        description: String(entry.description || "skill command"),
                    }))
                    .filter((entry) => entry.name.length > 0);
            } catch (_) {
            }

            const merged = [];
            const seen = new Set();
            for (const item of builtins.concat(skillCommands)) {
                const key = item.name.toLowerCase();
                if (seen.has(key)) {
                    continue;
                }
                seen.add(key);
                merged.push(item);
            }

            state.slashCommands = merged;
            state.slashCommandsLoaded = true;
            return state.slashCommands;
        }

        async function getSlashCommandHints(query) {
            await ensureSlashCommandsLoaded();
            const prefix = String(query || "").trim().toLowerCase();
            if (!prefix) {
                return state.slashCommands.slice(0, 12);
            }

            return state.slashCommands
                .filter((item) => item.name.toLowerCase().startsWith(prefix))
                .slice(0, 12);
        }

        async function createSession(optionalId) {
            const requestedId = String(optionalId || "").trim();
            try {
                const response = await request("sessions.create", {
                    sessionId: requestedId,
                    scope: "chat",
                    active: true,
                });
                const nextId =
                    response &&
                        response.payload &&
                        response.payload.session &&
                        typeof response.payload.session.id === "string"
                        ? response.payload.session.id
                        : requestedId || `session-${Date.now()}`;
                await loadSessionOptions();
                await switchSession(nextId);
                addMessage(`session switched: ${nextId}`, "peer", { source: "session" });
            } catch (error) {
                addMessage(`session create error: ${String(error)}`, "error", { source: "session" });
            }
        }

        async function handleLocalSlashCommand(rawCommandLine) {
            const line = String(rawCommandLine || "").trim();
            if (!line.startsWith("/")) {
                return { handled: false };
            }

            const tokens = line.slice(1).split(/\s+/).filter((token) => token.length > 0);
            const command = (tokens[0] || "").toLowerCase();
            const args = tokens.slice(1);

            if (command === "help") {
                await ensureSlashCommandsLoaded();
                const names = state.slashCommands.map((item) => `/${item.name}`).join(", ");
                addMessage(`slash commands: ${names}`, "peer", { source: "help" });
                return { handled: true };
            }

            if (command === "clear") {
                clearMessages();
                finalizeStream();
                addMessage("chat cleared", "peer", { source: "clear" });
                return { handled: true };
            }

            if (command === "new") {
                await createSession(args[0] || "");
                return { handled: true };
            }

            if (command === "session") {
                const target = args[0] || "";
                if (!target) {
                    addMessage("usage: /session <sessionId>", "error", { source: "session" });
                    return { handled: true };
                }
                await switchSession(target);
                addMessage(`session switched: ${normalizeSessionKey(target)}`, "peer", { source: "session" });
                return { handled: true };
            }

            if (command === "model") {
                const target = args[0] || "";
                if (!target) {
                    addMessage("usage: /model <modelId>", "error", { source: "model" });
                    return { handled: true };
                }
                const ok = await applyModelSelection(target);
                if (ok) {
                    addMessage(`model set: ${target}`, "peer", { source: "model" });
                }
                return { handled: true };
            }

            if (command === "thinking") {
                const target = args[0] || "";
                if (!target) {
                    addMessage("usage: /thinking <low|normal|high>", "error", { source: "thinking" });
                    return { handled: true };
                }
                if (!applyThinkingLevel(target)) {
                    addMessage("invalid thinking level", "error", { source: "thinking" });
                } else {
                    addMessage(`thinking level set: ${state.thinkingLevel}`, "peer", { source: "thinking" });
                }
                return { handled: true };
            }

            if (command === "btw") {
                const detachedMessage = args.join(" ").trim();
                if (!detachedMessage) {
                    addMessage("usage: /btw <message>", "error", { source: "btw" });
                    return { handled: true };
                }
                await sendDetachedMessage(detachedMessage);
                return { handled: true };
            }

            if (command === "abort") {
                await abort();
                return { handled: true };
            }

            if (command === "cron") {
                if (typeof onCronSlashCommand !== "function") {
                    addMessage("/cron is unavailable in this session.", "error", { source: "cron" });
                    return { handled: true };
                }

                const slashResult = await onCronSlashCommand(line);
                const result = slashResult && typeof slashResult === "object"
                    ? slashResult
                    : {};
                const messageText = String(result.message || "").trim();
                if (messageText) {
                    addMessage(messageText, result.kind === "error" ? "error" : "peer", { source: "cron" });
                }
                return {
                    handled: true,
                    pending: result.kind === "pending",
                };
            }

            return { handled: false };
        }

        function queuePendingSend(message, attachments, forceError, options) {
            const sendOptions = options && typeof options === "object"
                ? options
                : {};
            state.sendQueue.push({
                message,
                attachments,
                forceError,
                detached: sendOptions.detached === true,
            });
            addMessage(
                `waiting for terminal event; queued message (${state.sendQueue.length})`,
                "peer", { source: "queue" });
            if (sendOptions.detached === true) {
                onDetachedNotice({
                    kind: "queued",
                    text: String(message || "").trim(),
                    sessionKey: state.sessionKey,
                });
            }
        }

        function stopRunWatchdog() {
            if (state.runWatchdogTimer) {
                clearInterval(state.runWatchdogTimer);
                state.runWatchdogTimer = null;
            }
            state.runWatchdogStartedAtMs = 0;
            state.runWatchdogLastInboundEventMs = 0;
            state.runWatchdogLastReconcileMs = 0;
            state.runWatchdogLastWarningMs = 0;
        }

        function noteInboundChatEvent(eventState) {
            const normalizedState = String(eventState || "").trim().toLowerCase();
            if (!state.runId) {
                return;
            }
            const nowMs = Date.now();
            state.runWatchdogLastInboundEventMs = nowMs;
            if (normalizedState === "delta" || normalizedState === "queued" || normalizedState === "started") {
                state.runWatchdogLastWarningMs = 0;
            }
        }

        function startRunWatchdog() {
            stopRunWatchdog();
            if (!state.runId || !state.bridgeAvailable) {
                return;
            }

            const nowMs = Date.now();
            state.runWatchdogStartedAtMs = nowMs;
            state.runWatchdogLastInboundEventMs = nowMs;

            const staleThresholdMs = 4000;
            const reconcileCooldownMs = 2000;
            const warningCooldownMs = 10000;
            const tickMs = 1000;

            state.runWatchdogTimer = setInterval(() => {
                if (!state.runId || !state.bridgeAvailable) {
                    stopRunWatchdog();
                    return;
                }

                const now = Date.now();
                const lastInbound = Number(state.runWatchdogLastInboundEventMs || state.runWatchdogStartedAtMs || now);
                const staleForMs = now - lastInbound;
                if (staleForMs < staleThresholdMs) {
                    return;
                }
                emitOperatorDiagnostic("chat.queue.stale_run_detected", {
                    runId: String(state.runId || ""),
                    sessionKey: normalizeSessionKey(state.sessionKey),
                    staleForMs,
                    queuedMessages: state.sendQueue.length,
                }, {
                    minIntervalMs: 10000,
                });

                if ((now - Number(state.runWatchdogLastReconcileMs || 0)) >= reconcileCooldownMs) {
                    state.runWatchdogLastReconcileMs = now;
                    void request("chat.events.poll", {
                        sessionKey: state.sessionKey,
                        limit: 50,
                    })
                        .then(function (pollResult) {
                            const polledEvents = extractChatEventsFromPollResponse(pollResult);
                            if (!Array.isArray(polledEvents) || polledEvents.length === 0) {
                                return;
                            }
                            if (typeof state.onPolledChatEvents === "function") {
                                state.onPolledChatEvents(polledEvents);
                            }
                        })
                        .catch(function () {
                            // Ignore reconcile failures; watchdog retries on next stale interval.
                        });
                }

                if (state.sendQueue.length > 0 &&
                    (now - Number(state.runWatchdogLastWarningMs || 0)) >= warningCooldownMs) {
                    state.runWatchdogLastWarningMs = now;
                    addMessage(
                        `waiting for terminal event; reconciling stalled run (${state.sendQueue.length} queued)`,
                        "peer", { source: "queue" });
                }
            }, tickMs);
        }

        function extractRunIdFromSendResult(sendResult) {
            const result = sendResult && typeof sendResult === "object"
                ? sendResult
                : {};
            const payload = result.payload && typeof result.payload === "object"
                ? result.payload
                : {};
            const payloadData = payload.data && typeof payload.data === "object"
                ? payload.data
                : {};

            const candidates = [
                payload.runId,
                result.runId,
                payloadData.runId,
            ];
            for (const candidate of candidates) {
                if (typeof candidate === "string" && candidate.trim()) {
                    return candidate.trim();
                }
            }
            return "";
        }

        function extractAbortOutcome(abortResult) {
            const result = abortResult && typeof abortResult === "object"
                ? abortResult
                : {};
            const payload = result.payload && typeof result.payload === "object"
                ? result.payload
                : {};
            const payloadData = payload.data && typeof payload.data === "object"
                ? payload.data
                : {};
            const aborted = payload.aborted === true ||
                result.aborted === true ||
                payloadData.aborted === true;
            const resolvedRunId = typeof payload.runId === "string" && payload.runId.trim()
                ? payload.runId.trim()
                : (typeof result.runId === "string" && result.runId.trim()
                    ? result.runId.trim()
                    : (typeof payloadData.runId === "string" && payloadData.runId.trim()
                        ? payloadData.runId.trim()
                        : ""));
            return {
                aborted,
                runId: resolvedRunId,
            };
        }

        function extractChatEventsFromPollResponse(pollResult) {
            const result = pollResult && typeof pollResult === "object"
                ? pollResult
                : {};
            const payload = result.payload && typeof result.payload === "object"
                ? result.payload
                : {};
            if (Array.isArray(payload.events)) {
                return payload.events;
            }
            if (Array.isArray(result.events)) {
                return result.events;
            }
            return [];
        }

        function incrementOperatorDiagnosticCounter(counterName) {
            const key = String(counterName || "").trim();
            if (!key) {
                return 0;
            }
            const current = Number(state.operatorDiagnosticsCounters[key] || 0);
            const next = current + 1;
            state.operatorDiagnosticsCounters[key] = next;
            return next;
        }

        function emitOperatorDiagnostic(counterName, details, options) {
            const key = String(counterName || "").trim();
            if (!key) {
                return;
            }
            const opts = options && typeof options === "object"
                ? options
                : {};
            const nowMs = Date.now();
            const minIntervalMs = Number.isFinite(opts.minIntervalMs)
                ? Number(opts.minIntervalMs)
                : 5000;
            const lastEmit = Number(state.operatorDiagnosticsLastEmitMs[key] || 0);
            const count = incrementOperatorDiagnosticCounter(key);
            if (nowMs - lastEmit < minIntervalMs) {
                return;
            }
            state.operatorDiagnosticsLastEmitMs[key] = nowMs;
            if (window.console && typeof window.console.debug === "function") {
                window.console.debug("[chat-operator-diagnostic]", {
                    counter: key,
                    count,
                    details: details && typeof details === "object" ? details : {},
                });
            }
        }

        async function sendPayload(message, attachments, forceError, options) {
            const sendOptions = options && typeof options === "object"
                ? options
                : {};
            const detached = sendOptions.detached === true;
            const requestOverride = typeof sendOptions.requestOverride === "function"
                ? sendOptions.requestOverride
                : null;
            const speechContext = sendOptions.speechContext && typeof sendOptions.speechContext === "object"
                ? sendOptions.speechContext
                : null;
            const userMessage = String(message || "").trim();
            const payloadAttachments = Array.isArray(attachments) ? attachments : [];

            if ((!userMessage && payloadAttachments.length === 0) || !state.bridgeAvailable) {
                return;
            }

            if (userMessage && !detached) {
                pushInputHistory(userMessage);
                addMessage(userMessage, "self", { source: "user" });
            }

            if (payloadAttachments.length > 0 && !detached) {
                addMessage(
                    `[Attachment] ${payloadAttachments.map((item) => item.name).join(", ")}`,
                    "self", { source: "user" });
            }

            state.runId = nextId();
            state.streamText = "";
            startRunWatchdog();
            updateComposerState();

            const apiAttachments = payloadAttachments
                .map((item) => {
                    const parsed = dataUrlToBase64(item.dataUrl);
                    if (!parsed) {
                        return null;
                    }

                    return {
                        type: "image",
                        mimeType: parsed.mimeType || item.mimeType || "image/*",
                        content: parsed.content,
                    };
                })
                .filter((x) => x !== null);

            try {
                const chatParams = {
                    sessionKey: state.sessionKey,
                    message: userMessage,
                    deliver: detached,
                    detached,
                    idempotencyKey: state.runId,
                    forceError: Boolean(forceError),
                    model: state.selectedModel,
                    thinkingLevel: state.thinkingLevel,
                    bodyForCommands: userMessage,
                    bodyForAgent: userMessage,
                    clientMode: "webchat",
                    attachments: apiAttachments,
                };
                if (speechContext) {
                    if (speechContext.transcriptInjection && typeof speechContext.transcriptInjection === "object") {
                        chatParams.transcriptInjection = { ...speechContext.transcriptInjection };
                    }
                    if (speechContext.speechArtifact && typeof speechContext.speechArtifact === "object") {
                        chatParams.speechArtifact = { ...speechContext.speechArtifact };
                    }
                    if (typeof speechContext.sessionId === "string" && speechContext.sessionId.trim()) {
                        chatParams.sessionId = speechContext.sessionId.trim();
                    }
                    if (typeof speechContext.runId === "string" && speechContext.runId.trim()) {
                        chatParams.runId = speechContext.runId.trim();
                    }
                    if (typeof speechContext.audioPath === "string" && speechContext.audioPath.trim()) {
                        chatParams.audioPath = speechContext.audioPath.trim();
                    }
                    if (typeof speechContext.language === "string" && speechContext.language.trim()) {
                        chatParams.language = speechContext.language.trim();
                    }
                    if (Number.isFinite(Number(speechContext.latencyMs))) {
                        chatParams.latencyMs = Number(speechContext.latencyMs);
                    }
                    if (typeof speechContext.source === "string" && speechContext.source.trim()) {
                        chatParams.source = speechContext.source.trim();
                    }
                }
                const sendResult = await requestWithOverride("chat.send", chatParams, requestOverride);

                const serverRunId = extractRunIdFromSendResult(sendResult);

                if (serverRunId) {
                    const previousRunId = String(state.runId || "").trim();
                    state.runId = serverRunId;
                    if (previousRunId && previousRunId !== serverRunId) {
                        emitOperatorDiagnostic("chat.queue.run_id_remapped", {
                            previousRunId,
                            serverRunId,
                            sessionKey: normalizeSessionKey(state.sessionKey),
                        }, {
                            minIntervalMs: 1000,
                        });
                    }
                    startRunWatchdog();
                }
            } catch (error) {
                addMessage(`send error: ${String(error)}`, "error", { source: "user" });
                state.runId = null;
                stopRunWatchdog();
                void processSendQueue();
            }

            updateComposerState();
        }

        async function send(forceError) {
            const message = state.inputEl
                ? String(state.inputEl.value || "").trim()
                : "";
            const hasAttachments = state.attachments.length > 0;
            if ((!message && !hasAttachments) || !state.bridgeAvailable) {
                return;
            }

            const localCommand = await handleLocalSlashCommand(message);
            if (localCommand.handled) {
                if (state.inputEl) {
                    state.inputEl.value = "";
                }
                persistDraftForSession();
                updateComposerState();
                return;
            }

            const pendingAttachments = state.attachments.slice();
            if (state.inputEl) {
                state.inputEl.value = "";
            }
            state.attachments = [];
            persistDraftForSession();

            if (state.runId) {
                queuePendingSend(message, pendingAttachments, forceError, { detached: false });
                updateComposerState();
                return;
            }

            await sendPayload(message, pendingAttachments, forceError, { detached: false });
        }

        function applySpeechLifecycleUpdate(payload) {
            const normalized = normalizeSpeechSessionPayload(payload);
            const previous = state.speechSessionState && typeof state.speechSessionState === "object"
                ? state.speechSessionState
                : {};
            if (isPreviewUpdateBlockedByFinalAuthority(normalized, previous)) {
                emitSpeechRequestTrace("lifecycle_ignored", {
                    requestType: "preview",
                    reason: "final_authority_active",
                    previousStage: String(previous.stage || ""),
                    previousRunId: String(previous.runId || ""),
                    responseStage: String(normalized.stage || ""),
                    responseRunId: String(normalized.runId || ""),
                });
                return { ...previous };
            }
            if (isEmptyTerminalPreviewUpdate(normalized, previous)) {
                return { ...previous };
            }
            if (isStaleSpeechPreviewUpdate(normalized, previous)) {
                return { ...previous };
            }

            if (isPreviewTerminalUpdateWhileRecording(normalized, previous)) {
                normalized.stage = "streaming";
                normalized.segmentFinal = false;
            }

            const clearsPreviousTranscript =
                normalized.stage === "failed" &&
                String(normalized.errorCode || "").trim() === "missing_final_transcript";

            let resolvedSegmentText = normalized.segmentText;
            if (!resolvedSegmentText && normalized.stage === "segment_finalized") {
                resolvedSegmentText = normalized.text;
            }
            if (!clearsPreviousTranscript &&
                !resolvedSegmentText &&
                (normalized.stage === "queued" ||
                    normalized.stage === "stopped" ||
                    normalized.stage === "transcribing" ||
                    normalized.stage === "failed")) {
                resolvedSegmentText = String(previous.segmentText || "");
            }

            let resolvedText = normalized.text;
            if (!clearsPreviousTranscript &&
                !resolvedText &&
                (normalized.stage === "streaming" ||
                    normalized.stage === "queued" ||
                    normalized.stage === "stopped" ||
                    normalized.stage === "transcribing" ||
                    normalized.stage === "failed")) {
                resolvedText = String(previous.text || "");
            }

            state.speechSessionState = {
                ...previous,
                ...normalized,
                text: resolvedText,
                segmentText: resolvedSegmentText,
                updatedAtMs: Date.now(),
            };
            return { ...state.speechSessionState };
        }

        function sanitizeTranscriptText(text) {
            let value = String(text || "").trim();
            if (!value) {
                return "";
            }

            value = value.replace(/<\|[^|]*\|>/g, " ");
            value = value.replace(/\[assistant_response\]/gi, " ");
            value = value.replace(/assistant_response/gi, " ");
            value = value.replace(/\[user_message\]/gi, " ");
            value = value.replace(/^language\s+[^\s<]+\s*<asr_text>\s*/gi, "");
            value = value.replace(/<asr_text>/gi, " ");

            const lower = value.toLowerCase();
            const boundaries = ["\nuser\n", "\nassistant\n", "\r\nuser\r\n", "\r\nassistant\r\n"];
            let cutIndex = -1;
            for (const marker of boundaries) {
                const idx = lower.indexOf(marker);
                if (idx > 0 && (cutIndex < 0 || idx < cutIndex)) {
                    cutIndex = idx;
                }
            }
            if (cutIndex > 0) {
                value = value.slice(0, cutIndex);
            }

            value = value.replace(/\s+/g, " ").trim();
            return value;
        }

        function assessTranscriptQuality(text) {
            const value = sanitizeTranscriptText(text);
            if (!value) {
                return { accepted: false, reason: "empty transcript", cleanedText: "" };
            }

            const isCjkCharacter = function (ch) {
                return /[\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff]/.test(ch);
            };

            const findRepeatedCjkPhrase = function (compactText) {
                const chars = Array.from(String(compactText || ""));
                if (chars.length < 4) {
                    return null;
                }

                let best = null;
                for (let unitLength = 2; unitLength <= 6; unitLength += 1) {
                    if (chars.length < unitLength * 2) {
                        continue;
                    }
                    for (let start = 0; start + unitLength * 2 <= chars.length; start += 1) {
                        const unitChars = chars.slice(start, start + unitLength);
                        if (!unitChars.every(isCjkCharacter)) {
                            continue;
                        }
                        const unit = unitChars.join("");
                        let repeatCount = 1;
                        let cursor = start + unitLength;
                        while (cursor + unitLength <= chars.length &&
                            chars.slice(cursor, cursor + unitLength).join("") === unit) {
                            repeatCount += 1;
                            cursor += unitLength;
                        }
                        const coverage = (repeatCount * unitLength) / chars.length;
                        const degenerate =
                            (unitLength === 2 && repeatCount >= 5) ||
                            (unitLength >= 3 && unitLength <= 6 && repeatCount >= 4) ||
                            (repeatCount >= 3 && coverage >= 0.35);
                        if (!degenerate) {
                            continue;
                        }
                        if (!best ||
                            repeatCount > best.repeatCount ||
                            (repeatCount === best.repeatCount && coverage > best.coverage)) {
                            best = { unit, unitLength, repeatCount, coverage };
                        }
                    }
                }
                return best;
            };

            let longestRun = 1;
            let currentRun = 1;
            for (let i = 1; i < value.length; i += 1) {
                if (value[i] === value[i - 1]) {
                    currentRun += 1;
                    if (currentRun > longestRun) {
                        longestRun = currentRun;
                    }
                } else {
                    currentRun = 1;
                }
            }

            let cleaned = value;
            if (longestRun >= 16) {
                cleaned = cleaned.replace(/(.)\1{5,}/g, "$1$1$1");
                cleaned = cleaned.replace(/\s+/g, " ").trim();
            }

            const compact = cleaned.replace(/\s+/g, "");
            const compactUniqueChars = new Set(compact).size;
            const punctuationCount = (compact.match(/[.,!?，。！？；;:、]/g) || []).length;
            const cjkCount = (cleaned.match(/[\u4e00-\u9fff]/g) || []).length;
            const repeatedCjkPhrase = findRepeatedCjkPhrase(compact);
            if (repeatedCjkPhrase) {
                return { accepted: false, reason: "repetitive phrase transcript pattern detected", cleanedText: cleaned };
            }
            const shortCjkUtterance = cjkCount > 0 && compact.length <= 12 && compactUniqueChars >= 3;
            if (shortCjkUtterance) {
                return { accepted: true, reason: "", cleanedText: cleaned };
            }
            if (compact.length >= 4) {
                if (compactUniqueChars === 1) {
                    return { accepted: false, reason: "repetitive transcript pattern detected", cleanedText: cleaned };
                }
                if (compact.length <= 12 && compactUniqueChars <= 2 && longestRun >= 5 && cjkCount === 0) {
                    return { accepted: false, reason: "repetitive transcript pattern detected", cleanedText: cleaned };
                }
                if (compact.length <= 8 && punctuationCount >= 3 && punctuationCount / compact.length >= 0.5 && compactUniqueChars <= 3 && cjkCount === 0) {
                    return { accepted: false, reason: "punctuation-heavy transcript pattern detected", cleanedText: cleaned };
                }
            }

            const mojibakeHintCount = (cleaned.match(/[ÂÃå°ðĿ]/g) || []).length;
            if (cleaned.length >= 16 && mojibakeHintCount / cleaned.length > 0.35) {
                return { accepted: false, reason: "mojibake transcript pattern detected", cleanedText: cleaned };
            }

            const hasLanguageLikeContent = /[A-Za-z0-9\u4e00-\u9fff]/.test(cleaned);
            if (!hasLanguageLikeContent) {
                return { accepted: false, reason: "transcript has no language content", cleanedText: cleaned };
            }

            const alphaCount = (cleaned.match(/[A-Za-z]/g) || []).length;
            const upperCount = (cleaned.match(/[A-Z]/g) || []).length;
            const asciiWord = cleaned.match(/^[A-Za-z!?.\s]{3,24}$/) !== null;
            if (cjkCount === 0 &&
                asciiWord &&
                alphaCount >= 3 &&
                upperCount / alphaCount > 0.8 &&
                /\b(uster|user|assistant)\b/i.test(cleaned)) {
                return { accepted: false, reason: "placeholder transcript pattern detected", cleanedText: cleaned };
            }

            return { accepted: true, reason: "", cleanedText: cleaned };
        }

        function clearSpeechPreviewAfterDelay(expectedRunId, delayMs) {
            const timeoutMs = Number.isFinite(Number(delayMs)) && Number(delayMs) >= 0
                ? Number(delayMs)
                : 2500;
            window.setTimeout(() => {
                const current = state.speechSessionState && typeof state.speechSessionState === "object"
                    ? state.speechSessionState
                    : null;
                if (!current) {
                    return;
                }

                const currentRunId = String(current.runId || "").trim();
                if (expectedRunId && currentRunId && currentRunId !== expectedRunId) {
                    return;
                }

                const stage = String(current.stage || "").trim();
                if (stage !== "completed" && stage !== "segment_finalized") {
                    return;
                }

                state.speechSessionState = {
                    stage: "idle",
                    text: "",
                    segmentText: "",
                    segmentFinal: true,
                    segmentSequence: 0,
                    runId: "",
                    sessionId: state.sessionKey,
                    audioPath: "",
                    audioArtifact: null,
                    language: "",
                    latencyMs: 0,
                    cancelled: false,
                    errorCode: "",
                    errorMessage: "",
                    errorClass: "status",
                    retryable: false,
                    retryStrategy: "immediate",
                    retryGuidance: "",
                    updatedAtMs: Date.now(),
                };
                updateComposerState();
            }, timeoutMs);
        }

        function summarizeSpeechAudioArtifact(audioArtifact) {
            const artifact = audioArtifact && typeof audioArtifact === "object"
                ? audioArtifact
                : null;
            if (!artifact) {
                return null;
            }

            return {
                handoffMode: String(artifact.handoffMode || "").trim(),
                streamId: String(artifact.streamId || "").trim(),
                sequenceStart: Number.isFinite(Number(artifact.sequenceStart))
                    ? Number(artifact.sequenceStart)
                    : 0,
                sequenceEnd: Number.isFinite(Number(artifact.sequenceEnd))
                    ? Number(artifact.sequenceEnd)
                    : 0,
                sampleRate: Number.isFinite(Number(artifact.sampleRate))
                    ? Number(artifact.sampleRate)
                    : 0,
                channels: Number.isFinite(Number(artifact.channels))
                    ? Number(artifact.channels)
                    : 0,
                durationMs: Number.isFinite(Number(artifact.durationMs))
                    ? Number(artifact.durationMs)
                    : 0,
            };
        }

        function emitSpeechRequestTrace(eventName, details) {
            const current = state.speechSessionState && typeof state.speechSessionState === "object"
                ? state.speechSessionState
                : {};
            const trace = {
                event: String(eventName || ""),
                stage: String(current.stage || ""),
                sessionId: String(current.sessionId || state.sessionKey || ""),
                runId: String(current.runId || ""),
                details: details && typeof details === "object" ? details : {},
            };

            emitOperatorDiagnostic(
                `speech.request.${trace.event || "unknown"}`,
                trace,
                { minIntervalMs: 0 });

            if (window.console && typeof window.console.debug === "function") {
                window.console.debug("[speech-request-trace]", trace);
            }
        }

        async function transcribeSpeech(options) {
            const sendOptions = options && typeof options === "object"
                ? options
                : {};
            const livePreviewOnly = sendOptions.livePreviewOnly === true;
            if (!state.bridgeAvailable) {
                return;
            }

            const audioPath = String(sendOptions.audioPath || "").trim();
            const prompt = String(sendOptions.prompt || "").trim();
            const language = String(sendOptions.language || "").trim();
            const audioArtifact = sendOptions.audioArtifact && typeof sendOptions.audioArtifact === "object"
                ? sendOptions.audioArtifact
                : null;
            const requestOverride = typeof sendOptions.requestOverride === "function"
                ? sendOptions.requestOverride
                : null;
            const requestedRunId = String(sendOptions.runId || "").trim();
            const transcriptRequest = {
                sessionId: state.sessionKey,
                runId: requestedRunId || nextId(),
                prompt: prompt || String(state.inputEl && state.inputEl.value || "").trim(),
                livePreviewOnly,
            };
            const transcriptionTimeoutMs = Number.isFinite(Number(sendOptions.timeoutMs)) && Number(sendOptions.timeoutMs) > 0
                ? Number(sendOptions.timeoutMs)
                : 90000;
            if (audioPath) {
                transcriptRequest.audioPath = audioPath;
            }
            if (language) {
                transcriptRequest.language = language;
            }
            if (audioArtifact) {
                transcriptRequest.audioArtifact = audioArtifact;
            }

            emitSpeechRequestTrace("request_start", {
                requestType: livePreviewOnly ? "preview" : "final",
                livePreviewOnly,
                sessionId: transcriptRequest.sessionId,
                runId: transcriptRequest.runId,
                audioPath,
                audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                speechSessionStage: String(state.speechSessionState && state.speechSessionState.stage || ""),
            });

            if (!livePreviewOnly) {
                applySpeechLifecycleUpdate({
                    stage: "queued",
                    sessionId: transcriptRequest.sessionId,
                    runId: transcriptRequest.runId,
                    audioPath,
                    text: "",
                    errorCode: "",
                    errorMessage: "",
                    errorClass: "status",
                });
            }
            updateComposerState();

            let transcriptionTimeoutId = null;
            try {
                const transcriptionPromise = requestWithOverride(
                    "speech.transcribe",
                    transcriptRequest,
                    requestOverride);
                const timeoutPromise = new Promise((_, reject) => {
                    transcriptionTimeoutId = window.setTimeout(() => {
                        reject(new Error(`speech transcribe timed out after ${transcriptionTimeoutMs} ms`));
                    }, transcriptionTimeoutMs);
                });
                const response = await Promise.race([transcriptionPromise, timeoutPromise]);
                const payload = response && response.payload && typeof response.payload === "object"
                    ? response.payload
                    : {};
                const previousSpeechSessionState = state.speechSessionState && typeof state.speechSessionState === "object"
                    ? state.speechSessionState
                    : {};
                const normalizedSpeechSessionState = normalizeSpeechSessionPayload(payload);
                const previousStage = String(previousSpeechSessionState.stage || "").trim();
                const livePreviewStillActive = isActiveSpeechPreviewState(previousSpeechSessionState);
                const finalAuthorityActive = !livePreviewOnly && hasSpeechFinalAuthority(previousSpeechSessionState);
                if (livePreviewOnly && !livePreviewStillActive) {
                    emitSpeechRequestTrace("response_ignored", {
                        requestType: "preview",
                        reason: "preview_not_active",
                        livePreviewOnly,
                        sessionId: transcriptRequest.sessionId,
                        runId: transcriptRequest.runId,
                        previousStage,
                        responseStage: String(normalizedSpeechSessionState.stage || ""),
                        responseRunId: String(normalizedSpeechSessionState.runId || ""),
                        audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                    });
                    return;
                }
                if (!livePreviewOnly &&
                    finalAuthorityActive &&
                    isSpeechPreviewRunId(normalizedSpeechSessionState.runId)) {
                    emitSpeechRequestTrace("response_ignored", {
                        requestType: "final",
                        reason: "preview_state_blocked_by_final_authority",
                        livePreviewOnly,
                        sessionId: transcriptRequest.sessionId,
                        runId: transcriptRequest.runId,
                        previousStage,
                        previousRunId: String(previousSpeechSessionState.runId || ""),
                        previousFinalRunId: String(previousSpeechSessionState.finalRunId || ""),
                        responseStage: String(normalizedSpeechSessionState.stage || ""),
                        responseRunId: String(normalizedSpeechSessionState.runId || ""),
                        audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                    });
                    updateComposerState();
                    return;
                }
                if (livePreviewOnly && isStaleSpeechPreviewUpdate(normalizedSpeechSessionState, previousSpeechSessionState)) {
                    emitSpeechRequestTrace("response_ignored", {
                        requestType: "preview",
                        reason: "stale_preview_update",
                        livePreviewOnly,
                        sessionId: transcriptRequest.sessionId,
                        runId: transcriptRequest.runId,
                        previousStage,
                        previousRunId: String(previousSpeechSessionState.runId || ""),
                        responseStage: String(normalizedSpeechSessionState.stage || ""),
                        responseRunId: String(normalizedSpeechSessionState.runId || ""),
                        audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                    });
                    updateComposerState();
                    return;
                }
                const keepLiveStreamingState =
                    livePreviewOnly === true &&
                    livePreviewStillActive &&
                    normalizedSpeechSessionState.stage === "completed" &&
                    !normalizedSpeechSessionState.segmentText &&
                    !normalizedSpeechSessionState.text &&
                    !normalizedSpeechSessionState.errorCode;

                state.speechSessionState = keepLiveStreamingState
                    ? {
                        ...previousSpeechSessionState,
                        runId: String(normalizedSpeechSessionState.runId || previousSpeechSessionState.runId || "").trim(),
                        sessionId: String(normalizedSpeechSessionState.sessionId || previousSpeechSessionState.sessionId || "").trim(),
                        audioPath: String(normalizedSpeechSessionState.audioPath || previousSpeechSessionState.audioPath || "").trim(),
                        latencyMs: Number.isFinite(Number(normalizedSpeechSessionState.latencyMs))
                            ? Number(normalizedSpeechSessionState.latencyMs)
                            : Number(previousSpeechSessionState.latencyMs || 0),
                        updatedAtMs: Date.now(),
                    }
                    : normalizedSpeechSessionState;
                const finalTranscript = describeFinalOwnedTranscript(payload, transcriptRequest.runId);
                const finalPayloadText = finalTranscript.text;
                const previewPayloadText = String(
                    finalPayloadText ||
                    state.speechSessionState.segmentText ||
                    state.speechSessionState.text ||
                    previousSpeechSessionState.segmentText ||
                    previousSpeechSessionState.text ||
                    "").trim();
                const transcriptText = livePreviewOnly
                    ? previewPayloadText
                    : finalPayloadText;
                if (!livePreviewOnly && !transcriptText) {
                    const nativeEmptyFinalErrorCode = String(
                        normalizedSpeechSessionState.errorCode ||
                        payload.errorCode ||
                        "").trim();
                    const nativeEmptyFinalErrorMessage = String(
                        normalizedSpeechSessionState.errorMessage ||
                        payload.errorMessage ||
                        "").trim();
                    if (nativeEmptyFinalErrorCode) {
                        emitSpeechRequestTrace("response_ignored", {
                            requestType: "final",
                            reason: "native_final_error_no_transcript",
                            livePreviewOnly,
                            sessionId: transcriptRequest.sessionId,
                            runId: transcriptRequest.runId,
                            responseStage: String(normalizedSpeechSessionState.stage || ""),
                            responseRunId: String(normalizedSpeechSessionState.runId || ""),
                            nativeErrorCode: nativeEmptyFinalErrorCode,
                            nativeErrorMessageLength: nativeEmptyFinalErrorMessage.length,
                            finalTextSource: String(finalTranscript.source || ""),
                            finalTextOwnerRunId: String(finalTranscript.ownerRunId || ""),
                            finalTextOwned: Boolean(finalTranscript.finalOwned),
                            hasTopLevelText: Boolean(finalTranscript.hasTopLevelText),
                            hasNestedText: Boolean(finalTranscript.hasNestedText),
                            hasSegmentText: Boolean(finalTranscript.hasSegmentText),
                            audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                        });
                        if (String(state.speechSessionState.stage || "").trim() === "completed") {
                            applySpeechLifecycleUpdate({
                                stage: "failed",
                                sessionId: transcriptRequest.sessionId,
                                runId: transcriptRequest.runId,
                                audioPath,
                                audioArtifact,
                                text: "",
                                segmentText: "",
                                errorCode: nativeEmptyFinalErrorCode,
                                errorMessage: nativeEmptyFinalErrorMessage || "speech final response returned an explicit error without transcript text",
                                errorClass: "status",
                            });
                        }
                    } else {
                    emitSpeechRequestTrace("response_ignored", {
                        requestType: "final",
                        reason: "missing_final_transcript",
                        livePreviewOnly,
                        sessionId: transcriptRequest.sessionId,
                        runId: transcriptRequest.runId,
                        responseStage: String(normalizedSpeechSessionState.stage || ""),
                        responseRunId: String(normalizedSpeechSessionState.runId || ""),
                        finalTextSource: String(finalTranscript.source || ""),
                        finalTextOwnerRunId: String(finalTranscript.ownerRunId || ""),
                        finalTextOwned: Boolean(finalTranscript.finalOwned),
                        hasTopLevelText: Boolean(finalTranscript.hasTopLevelText),
                        hasNestedText: Boolean(finalTranscript.hasNestedText),
                        hasSegmentText: Boolean(finalTranscript.hasSegmentText),
                        preservedPreviewTextLength: String(
                            previousSpeechSessionState.segmentText ||
                            previousSpeechSessionState.text ||
                            "").trim().length,
                        audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                    });
                    applySpeechLifecycleUpdate({
                        stage: "failed",
                        sessionId: transcriptRequest.sessionId,
                        runId: transcriptRequest.runId,
                        audioPath,
                        audioArtifact,
                        text: "",
                        segmentText: "",
                        errorCode: "missing_final_transcript",
                        errorMessage: "speech final response did not contain transcript text",
                        errorClass: "status",
                    });
                    }
                }
                if (transcriptText) {
                    const quality = assessTranscriptQuality(transcriptText);
                    if (!quality.accepted) {
                        if (livePreviewOnly) {
                            emitSpeechRequestTrace("response_ignored", {
                                requestType: "preview",
                                reason: "quality_rejected",
                                livePreviewOnly,
                                sessionId: transcriptRequest.sessionId,
                                runId: transcriptRequest.runId,
                                responseStage: String(state.speechSessionState.stage || ""),
                                responseRunId: String(state.speechSessionState.runId || ""),
                                qualityReason: String(quality.reason || ""),
                                audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                            });
                            updateComposerState();
                            return;
                        }
                        state.speechSessionState = {
                            ...state.speechSessionState,
                            stage: "failed",
                            errorCode: "transcript_rejected",
                            errorMessage: `speech transcript blocked: ${quality.reason}`,
                            errorClass: "status",
                            text: "",
                            segmentText: "",
                            updatedAtMs: Date.now(),
                        };
                        addMessage(`speech transcript blocked: ${quality.reason}`, "error", { source: "speech" });
                        updateComposerState();
                        return;
                    }

                    const cleanedTranscriptText = String(quality.cleanedText || transcriptText).trim();
                    if (livePreviewOnly) {
                        applySpeechLifecycleUpdate({
                            stage: "streaming",
                            sessionId: transcriptRequest.sessionId,
                            runId: transcriptRequest.runId,
                            audioPath,
                            text: cleanedTranscriptText,
                            language: String(payload.language || state.speechSessionState.language || "").trim(),
                            latencyMs: Number.isFinite(Number(payload.latencyMs)) ? Number(payload.latencyMs) : 0,
                            errorCode: "",
                            errorMessage: "",
                            errorClass: "status",
                        });
                        emitSpeechRequestTrace("response_applied", {
                            requestType: "preview",
                            livePreviewOnly,
                            sessionId: transcriptRequest.sessionId,
                            runId: transcriptRequest.runId,
                            responseStage: String(state.speechSessionState.stage || ""),
                            responseRunId: String(state.speechSessionState.runId || ""),
                            responseTextLength: cleanedTranscriptText.length,
                            audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                        });
                        updateComposerState();
                        return;
                    }

                    await sendPayload(cleanedTranscriptText, [], false, {
                        detached: false,
                        requestOverride,
                        speechContext: {
                            source: "voice",
                            sessionId: String(payload.sessionId || state.sessionKey || "").trim(),
                            runId: String(payload.executionRunId || payload.runId || payload.speechSession && payload.speechSession.runId || "").trim(),
                            audioPath: String(payload.audioPath || payload.speechSession && payload.speechSession.audioPath || "").trim(),
                            audioArtifact: payload.audioArtifact && typeof payload.audioArtifact === "object"
                                ? payload.audioArtifact
                                : payload.speechSession && payload.speechSession.audioArtifact && typeof payload.speechSession.audioArtifact === "object"
                                    ? payload.speechSession.audioArtifact
                                    : null,
                            language: String(payload.language || payload.speechSession && payload.speechSession.language || "").trim(),
                            latencyMs: Number.isFinite(Number(payload.latencyMs)) ? Number(payload.latencyMs) : Number(payload.speechSession && payload.speechSession.latencyMs || 0),
                            transcriptInjection: payload.transcriptInjection && typeof payload.transcriptInjection === "object"
                                ? payload.transcriptInjection
                                : {
                                    source: "voice",
                                    ingestMethod: "speech.transcribe",
                                    sessionId: String(payload.sessionId || state.sessionKey || "").trim(),
                                    runId: String(payload.executionRunId || payload.runId || payload.speechSession && payload.speechSession.runId || "").trim(),
                                    requestCorrelationId: String(payload.requestCorrelationId || payload.id || "").trim(),
                                    orchestrationSurface: "chat.send",
                                },
                            speechArtifact: payload.speechArtifact && typeof payload.speechArtifact === "object"
                                ? payload.speechArtifact
                                : {
                                    type: "voice_transcript",
                                    source: "speech.transcribe",
                                    audioPath: String(payload.audioPath || payload.speechSession && payload.speechSession.audioPath || "").trim(),
                                    text: transcriptText,
                                    language: String(payload.language || payload.speechSession && payload.speechSession.language || "").trim(),
                                    latencyMs: Number.isFinite(Number(payload.latencyMs)) ? Number(payload.latencyMs) : Number(payload.speechSession && payload.speechSession.latencyMs || 0),
                                    stage: String(payload.stage || payload.speechSession && payload.speechSession.stage || "completed").trim(),
                                    hasSegment: Boolean(payload.speechSession && payload.speechSession.segment),
                                },
                        },
                    });
                    const responseRunId = String(
                        payload.executionRunId ||
                        payload.runId ||
                        payload.speechSession && payload.speechSession.runId ||
                        "").trim();
                    const finalRunId = responseRunId && !isSpeechPreviewRunId(responseRunId)
                        ? responseRunId
                        : transcriptRequest.runId;
                    applySpeechLifecycleUpdate({
                        stage: "completed",
                        sessionId: transcriptRequest.sessionId,
                        runId: finalRunId,
                        audioPath: String(payload.audioPath || payload.speechSession && payload.speechSession.audioPath || audioPath || "").trim(),
                        audioArtifact: payload.audioArtifact && typeof payload.audioArtifact === "object"
                            ? payload.audioArtifact
                            : payload.speechSession && payload.speechSession.audioArtifact && typeof payload.speechSession.audioArtifact === "object"
                                ? payload.speechSession.audioArtifact
                                : audioArtifact,
                        text: cleanedTranscriptText,
                        language: String(payload.language || payload.speechSession && payload.speechSession.language || "").trim(),
                        latencyMs: Number.isFinite(Number(payload.latencyMs)) ? Number(payload.latencyMs) : Number(payload.speechSession && payload.speechSession.latencyMs || 0),
                        errorCode: "",
                        errorMessage: "",
                        errorClass: "status",
                    });
                    emitSpeechRequestTrace("response_applied", {
                        requestType: "final",
                        livePreviewOnly,
                        sessionId: transcriptRequest.sessionId,
                        runId: transcriptRequest.runId,
                        responseStage: String(state.speechSessionState.stage || ""),
                        responseRunId: finalRunId,
                        responseTextLength: cleanedTranscriptText.length,
                        audioArtifact: summarizeSpeechAudioArtifact(
                            payload.audioArtifact && typeof payload.audioArtifact === "object"
                                ? payload.audioArtifact
                                : payload.speechSession && payload.speechSession.audioArtifact && typeof payload.speechSession.audioArtifact === "object"
                                    ? payload.speechSession.audioArtifact
                                    : audioArtifact),
                    });
                    emitOperatorDiagnostic("speech.final.replacement", {
                        runId: finalRunId,
                        sessionId: transcriptRequest.sessionId,
                        stage: "completed",
                        hasText: true,
                        transcriptLength: cleanedTranscriptText.length,
                    }, {
                        minIntervalMs: 0,
                    });
                    clearSpeechPreviewAfterDelay(finalRunId, 2500);
                    updateComposerState();
                    return;
                }

                const sessionErrorCode = String(state.speechSessionState.errorCode || "").trim();
                const forceNoSpeechSemantic = inferNoSpeechSignal(state.speechSessionState);
                if (forceNoSpeechSemantic && sessionErrorCode !== "no_speech_detected") {
                    state.speechSessionState.errorCode = "no_speech_detected";
                    if (!String(state.speechSessionState.errorMessage || "").trim() ||
                        /inference_failed/i.test(String(state.speechSessionState.errorMessage || ""))) {
                        state.speechSessionState.errorMessage = "final transcript unavailable: no_speech_detected";
                    }
                    state.speechSessionState.errorClass = "status";
                    if (!state.speechSessionState.retryGuidance) {
                        state.speechSessionState.retryGuidance = "No speech detected. Check microphone level/input channel and retry.";
                    }
                    state.speechSessionState.retryable = true;
                    if (!String(state.speechSessionState.retryStrategy || "").trim()) {
                        state.speechSessionState.retryStrategy = "immediate";
                    }
                }
                const effectiveSessionErrorCode = normalizeSpeechErrorCode(
                    state.speechSessionState.errorCode || "");
                const classified = classifySpeechError(
                    effectiveSessionErrorCode,
                    String(state.speechSessionState.errorClass || "").trim());
                if (classified.retryDescriptor && state.speechSessionState.retryGuidance === "") {
                    state.speechSessionState.retryGuidance = String(classified.retryDescriptor.guidance || "").trim();
                }
                if (classified.retryDescriptor && state.speechSessionState.retryStrategy === "immediate") {
                    const strategy = String(classified.retryDescriptor.strategy || "").trim();
                    if (strategy) {
                        state.speechSessionState.retryStrategy = strategy;
                    }
                }
                if (classified.retryDescriptor && !state.speechSessionState.retryable) {
                    state.speechSessionState.retryable = Boolean(classified.retryDescriptor.retryable);
                }
                state.speechSessionState.errorClass = classified.behaviorClass;

                if (!transcriptText && effectiveSessionErrorCode) {
                    const noSpeechTriage = state.speechSessionState.noSpeechTriage && typeof state.speechSessionState.noSpeechTriage === "object"
                        ? state.speechSessionState.noSpeechTriage
                        : null;
                    emitSpeechRequestTrace("response_applied", {
                        requestType: livePreviewOnly ? "preview" : "final",
                        livePreviewOnly,
                        sessionId: transcriptRequest.sessionId,
                        runId: transcriptRequest.runId,
                        responseStage: String(state.speechSessionState.stage || ""),
                        responseRunId: String(state.speechSessionState.runId || ""),
                        errorCode: effectiveSessionErrorCode,
                        errorClass: classified.behaviorClass,
                        noSpeechSemanticForced: forceNoSpeechSemantic,
                        noSpeechTriage,
                        audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                    });
                    emitOperatorDiagnostic("speech.final.error_classification", {
                        runId: String(state.speechSessionState.runId || transcriptRequest.runId || "").trim(),
                        sessionId: transcriptRequest.sessionId,
                        errorCode: effectiveSessionErrorCode,
                        errorClass: classified.behaviorClass,
                        noSpeechSemanticForced: forceNoSpeechSemantic,
                        noSpeechTriage,
                    }, {
                        minIntervalMs: 0,
                    });
                    if (classified.behaviorClass === "ignore") {
                        updateComposerState();
                        return;
                    }

                    const detail = String(state.speechSessionState.errorMessage || "").trim();
                    const guidance = state.speechSessionState.retryGuidance
                        ? ` (${state.speechSessionState.retryGuidance})`
                        : "";
                    const msg = detail
                        ? `speech transcribe ${effectiveSessionErrorCode}: ${detail}${guidance}`
                        : `speech transcribe ${effectiveSessionErrorCode}${guidance}`;
                    if (classified.behaviorClass === "status") {
                        state.speechSessionState.errorMessage = msg;
                    } else if (classified.behaviorClass === "toast") {
                        addMessage(msg, "error", { source: "speech" });
                    } else {
                        addMessage(`speech blocking error: ${msg}`, "error", { source: "speech" });
                    }
                }
                updateComposerState();
            } catch (error) {
                const errorMessage = String(error && error.message ? error.message : error || "speech transcribe failed");
                const isTimeout = /timed out/i.test(errorMessage);
                emitSpeechRequestTrace("request_error", {
                    requestType: livePreviewOnly ? "preview" : "final",
                    livePreviewOnly,
                    sessionId: transcriptRequest.sessionId,
                    runId: transcriptRequest.runId,
                    errorMessage,
                    errorCode: isTimeout ? "transcribe_timeout" : "transcribe_failed",
                    audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                });
                if (livePreviewOnly) {
                    updateComposerState();
                    return;
                }

                const classified = classifySpeechError(isTimeout ? "transcribe_timeout" : "transcribe_failed", "toast");
                state.speechSessionState = {
                    stage: "failed",
                    text: "",
                    segmentText: "",
                    segmentFinal: true,
                    segmentSequence: 0,
                    runId: transcriptRequest.runId,
                    sessionId: transcriptRequest.sessionId,
                    audioPath,
                    errorCode: isTimeout ? "transcribe_timeout" : "transcribe_failed",
                    errorMessage,
                    errorClass: classified.behaviorClass,
                    retryable: true,
                    retryStrategy: "immediate",
                    retryGuidance: "Retry after checking microphone/audio input and runtime readiness.",
                    updatedAtMs: Date.now(),
                };
                addMessage(`speech transcribe error: ${errorMessage}`, "error", { source: "speech" });
                updateComposerState();
            } finally {
                if (transcriptionTimeoutId !== null) {
                    window.clearTimeout(transcriptionTimeoutId);
                }
            }
        }

        async function processSendQueue() {
            if (state.runId || state.sendQueue.length === 0) {
                return;
            }

            const next = state.sendQueue.shift();
            if (!next) {
                return;
            }

            await sendPayload(next.message, next.attachments, next.forceError, {
                detached: next.detached === true,
            });
        }

        async function sendDetachedMessage(message, options) {
            const detachedMessage = String(message || "").trim();
            if (!detachedMessage || !state.bridgeAvailable) {
                return false;
            }

            const opts = options && typeof options === "object"
                ? options
                : {};
            if (state.runId) {
                queuePendingSend(detachedMessage, [], false, { detached: true });
                updateComposerState();
                return true;
            }

            await sendPayload(detachedMessage, [], false, {
                detached: true,
                requestOverride: opts.requestOverride,
            });
            onDetachedNotice({
                kind: "sent",
                text: detachedMessage,
                sessionKey: state.sessionKey,
            });
            return true;
        }

        async function abort(options) {
            if (!state.runId || !state.bridgeAvailable) {
                return;
            }

            const opts = options && typeof options === "object"
                ? options
                : {};
            const requestImpl = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            const targetRunId = String(state.runId || "").trim();
            let abortOutcome = {
                aborted: false,
                runId: "",
            };
            try {
                const abortResult = await requestImpl("chat.abort", {
                    sessionKey: state.sessionKey,
                    runId: targetRunId,
                });
                abortOutcome = extractAbortOutcome(abortResult);
            } catch (error) {
                addMessage(`abort error: ${String(error)}`, "error", { source: "chat" });
                updateComposerState();
                return;
            }

            if (abortOutcome.aborted) {
                const resolvedRunId = abortOutcome.runId || targetRunId;
                addMessage(`abort requested for run ${resolvedRunId}; awaiting terminal event`, "peer", { source: "chat" });
                updateComposerState();
                return;
            }

            let recoveredViaReconcile = false;
            try {
                const pollResult = await requestImpl("chat.events.poll", {
                    sessionKey: state.sessionKey,
                    limit: 50,
                });
                const polledEvents = extractChatEventsFromPollResponse(pollResult);
                const hasTerminalEvent = polledEvents.some((event) => {
                    if (!event || typeof event !== "object") {
                        return false;
                    }
                    const eventSession = String(event.sessionKey || "").trim();
                    if (eventSession && eventSession !== normalizeSessionKey(state.sessionKey)) {
                        return false;
                    }
                    const eventState = String(event.state || "").trim().toLowerCase();
                    return eventState === "final" || eventState === "aborted" || eventState === "error";
                });
                if (hasTerminalEvent) {
                    clearRunState();
                    recoveredViaReconcile = true;
                }
            } catch (_) {
                // Ignore reconcile RPC failures and leave explicit diagnostic message below.
            }

            if (recoveredViaReconcile) {
                emitOperatorDiagnostic("chat.abort.stale_run_reconcile", {
                    targetRunId,
                    sessionKey: normalizeSessionKey(state.sessionKey),
                    recoveredVia: "chat.events.poll",
                }, {
                    minIntervalMs: 1000,
                });
                addMessage(
                    `abort fallback reconciled stale run state (target=${targetRunId})`,
                    "peer", { source: "chat" });
            } else {
                addMessage(
                    `abort diagnostic: target=${targetRunId}; no confirmed terminal event yet; queue remains guarded`,
                    "error", { source: "chat" });
            }
            updateComposerState();
        }

        function resolveScopeErrorFeatureLabel(method) {
            const normalized = String(method || "").trim().toLowerCase();
            if (normalized.startsWith("sessions.")) {
                return "sessions";
            }
            if (normalized.startsWith("chat.")) {
                return "chat history";
            }
            if (normalized.startsWith("skills.")) {
                return "skills";
            }
            if (normalized.startsWith("agents.") || normalized.startsWith("agent.")) {
                return "agents";
            }
            if (normalized.startsWith("channels.")) {
                return "channels";
            }
            if (normalized.startsWith("cron.")) {
                return "cron";
            }
            if (normalized.startsWith("nodes.") || normalized.startsWith("node.")) {
                return "nodes";
            }
            if (normalized.startsWith("usage.") || normalized.startsWith("sessions.usage")) {
                return "usage";
            }
            return "this feature";
        }

        function formatRpcErrorForSurface(method, errorShape) {
            const scopeErrors = window.BlazeClawScopeErrors;
            if (scopeErrors &&
                typeof scopeErrors.isMissingOperatorReadScopeError === "function" &&
                typeof scopeErrors.formatMissingOperatorReadScopeMessage === "function" &&
                scopeErrors.isMissingOperatorReadScopeError(errorShape)) {
                return scopeErrors.formatMissingOperatorReadScopeMessage(
                    resolveScopeErrorFeatureLabel(method));
            }

            if (errorShape && typeof errorShape === "object") {
                const message = typeof errorShape.message === "string"
                    ? errorShape.message.trim()
                    : "";
                if (message) {
                    return message;
                }
                const code = typeof errorShape.code === "string"
                    ? errorShape.code.trim()
                    : "";
                if (code) {
                    return `request failed (${code})`;
                }
            }
            return "request failed";
        }

        function handleRpcResult(message) {
            const slot = state.pending.get(message.id);
            if (!slot) {
                return;
            }

            state.pending.delete(message.id);
            if (message.ok) {
                slot.resolve(message);
            } else {
                const method = slot.method || "";
                slot.reject(formatRpcErrorForSurface(method, message.error));
            }
        }

        function consumeTerminalText(message) {
            const parsed = parseTextFromMessage(message);
            if (parsed && !isSilentReplyText(parsed)) {
                return parsed;
            }

            if (state.streamText && !isSilentReplyText(state.streamText)) {
                return state.streamText;
            }

            return "";
        }

        function applyDeltaText(text) {
            if (!text || isSilentReplyText(text)) {
                return;
            }

            if (text.length >= state.streamText.length) {
                state.streamText = text;
                state.streamTranscriptDraft = {
                    role: "assistant",
                    text: state.streamText,
                    runId: typeof state.runId === "string" ? state.runId : "",
                    sessionKey: state.sessionKey,
                    source: "stream",
                    terminalState: "delta",
                };
                addOrReplaceStream(state.streamText);
            }
        }

        function commitStreamTranscriptFinal(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const text = String(source.text || "").trim();
            if (!text || isSilentReplyText(text)) {
                state.streamTranscriptDraft = null;
                return false;
            }

            const runId = typeof source.runId === "string" && source.runId.trim()
                ? source.runId.trim()
                : (state.streamTranscriptDraft && typeof state.streamTranscriptDraft.runId === "string"
                    ? state.streamTranscriptDraft.runId
                    : "");
            const terminalState = typeof source.terminalState === "string" && source.terminalState.trim()
                ? source.terminalState.trim()
                : "final";
            const committed = recordStructuredTranscript({
                role: "assistant",
                text,
                runId,
                sessionKey: state.sessionKey,
                source: "stream",
                terminalState,
            });
            state.streamTranscriptDraft = null;
            return committed;
        }

        function hasBufferedAssistantStream() {
            return typeof state.streamText === "string" && state.streamText.trim().length > 0;
        }

        function clearRunState() {
            state.streamText = "";
            state.runId = null;
            state.streamTranscriptDraft = null;
            if (state.reconcileTimer) {
                clearTimeout(state.reconcileTimer);
                state.reconcileTimer = null;
            }
            if (Array.isArray(state.reconcileFollowupTimers)) {
                for (const timerId of state.reconcileFollowupTimers) {
                    clearTimeout(timerId);
                }
                state.reconcileFollowupTimers = [];
            }
            stopRunWatchdog();
            if (state.abortBtn) {
                state.abortBtn.disabled = true;
            }
            void processSendQueue();
        }

        function markTerminalRun(runId, terminalState) {
            const normalizedRunId = String(runId || "").trim();
            if (!normalizedRunId) {
                return;
            }

            state.terminalRunStates.set(normalizedRunId, String(terminalState || "final"));
            if (state.terminalRunStates.size > 256) {
                const first = state.terminalRunStates.keys().next();
                if (!first.done) {
                    state.terminalRunStates.delete(first.value);
                }
            }
        }

        function hasTerminalRun(runId) {
            const normalizedRunId = String(runId || "").trim();
            if (!normalizedRunId) {
                return false;
            }

            return state.terminalRunStates.has(normalizedRunId);
        }

        function setPolledEventsHandler(handler) {
            state.onPolledChatEvents = typeof handler === "function" ? handler : null;
        }

        function scheduleHistoryReconcile() {
            if (state.reconcileTimer) {
                clearTimeout(state.reconcileTimer);
            }
            if (Array.isArray(state.reconcileFollowupTimers)) {
                for (const timerId of state.reconcileFollowupTimers) {
                    clearTimeout(timerId);
                }
            }
            state.reconcileFollowupTimers = [];

            state.reconcileTimer = setTimeout(() => {
                state.reconcileTimer = null;
                void loadHistory();
            }, 250);

            const followMs = [900, 2400];
            for (const delay of followMs) {
                state.reconcileFollowupTimers.push(setTimeout(() => {
                    void loadHistory();
                }, delay));
            }
        }

        function readFileAsDataUrl(file) {
            return new Promise((resolve, reject) => {
                const reader = new FileReader();
                reader.onload = () => resolve(String(reader.result || ""));
                reader.onerror = () => reject(new Error("file read failed"));
                reader.readAsDataURL(file);
            });
        }

        async function addAttachmentFiles(files) {
            const fileList = Array.from(files || []);
            if (fileList.length === 0) {
                return;
            }

            for (const file of fileList) {
                if (!file || !String(file.type || "").startsWith("image/")) {
                    addMessage(`attachment skipped: ${file ? file.name : "unknown"}`, "error", { source: "attachment" });
                    continue;
                }

                try {
                    const dataUrl = await readFileAsDataUrl(file);
                    state.attachments.push({
                        name: file.name,
                        mimeType: file.type || "image/*",
                        dataUrl,
                    });
                } catch (_) {
                    addMessage(`attachment read error: ${file.name}`, "error", { source: "attachment" });
                }
            }

            if (state.attachInput) {
                state.attachInput.value = "";
            }

            updateComposerState();
        }

        async function executeExecApprovalAction(approvalToken, approve, options) {
            const normalizedToken = String(approvalToken || "").trim();
            if (!normalizedToken || !isValidApprovalToken(normalizedToken)) {
                return {
                    ok: false,
                    status: "invalid",
                    message: "approval token is invalid",
                };
            }

            const opts = options && typeof options === "object" ? options : {};
            const requestImpl = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            const readiness = approve
                ? await requestEmailBackendReadiness(requestImpl)
                : null;
            const payload = await requestImpl("gateway.tools.call.execute", {
                tool: "email.schedule",
                args: {
                    action: "approve",
                    approvalToken: normalizedToken,
                    approve: Boolean(approve),
                },
            });
            const responsePayload = payload && payload.payload && typeof payload.payload === "object"
                ? payload.payload
                : {};
            const status = typeof responsePayload.status === "string"
                ? responsePayload.status.trim().toLowerCase()
                : "";
            const output = typeof responsePayload.output === "string"
                ? responsePayload.output
                : "";
            const parsedHints = parseApprovalFailureHints(output);
            const errorCode = resolveNormalizedErrorCode(responsePayload, output, parsedHints);
            const expired = output.toLowerCase().includes("expired") || errorCode === "approval_token_expired";

            let resolvedOk = status === "cancelled" || status === "ok";
            if (approve) {
                resolvedOk = status === "ok";
            }

            let normalizedStatus = "unknown";
            if (status) {
                normalizedStatus = status;
            }
            if (expired) {
                normalizedStatus = "expired";
            }

            let remediation = "";
            let missingDependency = "";
            let installHint = "";
            let configHint = "";
            let failureBucket = "";
            let errorMessage = "";
            if (parsedHints) {
                if (parsedHints.remediation) {
                    remediation = parsedHints.remediation;
                }
                if (parsedHints.missingDependency) {
                    missingDependency = parsedHints.missingDependency;
                }
                if (parsedHints.installHint) {
                    installHint = parsedHints.installHint;
                }
                if (parsedHints.configHint) {
                    configHint = parsedHints.configHint;
                }
                if (parsedHints.bucket) {
                    failureBucket = parsedHints.bucket;
                }
                if (parsedHints.message) {
                    errorMessage = parsedHints.message;
                }
            }

            let readinessReady = false;
            let readinessCode = "";
            let readinessMessage = "";
            let readinessRemediation = "";
            let readinessMissingDependency = "";
            let readinessInstallHint = "";
            let readinessConfigHint = "";
            let readinessBucket = "";
            if (readiness) {
                readinessReady = Boolean(readiness.ready);
                if (readiness.errorCode) {
                    readinessCode = readiness.errorCode;
                }
                if (readiness.message) {
                    readinessMessage = readiness.message;
                }
                if (readiness.remediation) {
                    readinessRemediation = readiness.remediation;
                }
                if (readiness.missingDependency) {
                    readinessMissingDependency = readiness.missingDependency;
                }
                if (readiness.installHint) {
                    readinessInstallHint = readiness.installHint;
                }
                if (readiness.configHint) {
                    readinessConfigHint = readiness.configHint;
                }
                if (readiness.bucket) {
                    readinessBucket = readiness.bucket;
                }
            }

            return {
                ok: resolvedOk && !expired,
                status: normalizedStatus,
                output,
                errorCode,
                remediation,
                missingDependency,
                installHint,
                configHint,
                failureBucket,
                errorMessage,
                readinessKnown: Boolean(readiness),
                readinessReady,
                readinessCode,
                readinessMessage,
                readinessRemediation,
                readinessMissingDependency,
                readinessInstallHint,
                readinessConfigHint,
                readinessBucket,
            };
        }

        return {
            nextId,
            post,
            flushQueue,
            request,
            loadHistory,
            send,
            abort,
            handleRpcResult,
            consumeTerminalText,
            applyDeltaText,
            commitStreamTranscriptFinal,
            clearRunState,
            parseTextFromMessage,
            isSilentReplyText,
            addAttachmentFiles,
            loadSessionOptions,
            subscribeSessionUpdates,
            unsubscribeSessionUpdates,
            loadSessionCompactions,
            refreshSessionControlState,
            selectSessionCompaction,
            branchSessionCompaction,
            restoreSessionCompaction,
            loadModelOptions,
            loadThinkingOptions,
            switchSession,
            applyModelSelection,
            applyThinkingLevel,
            loadAssistantIdentity,
            getControlUiBootstrapConfig,
            persistDraftForSession,
            restoreDraftForSession,
            recallInputHistory,
            getSlashCommandHints,
            processSendQueue,
            sendDetachedMessage,
            transcribeSpeech,
            applySpeechLifecycleUpdate,
            loadSpeechCapabilities,
            loadSpeechErrorPolicy,
            getSpeechCapabilitiesSnapshot,
            getSpeechSessionStateSnapshot,
            assessTranscriptQuality,
            parseApprovalTokenFromText,
            executeExecApprovalAction,
            noteInboundChatEvent,
            appendChatBubble,
            markTerminalRun,
            hasTerminalRun,
            hasBufferedAssistantStream,
            setPolledEventsHandler,
            scheduleHistoryReconcile,
            getStructuredTranscript: function () {
                return Array.isArray(state.structuredTranscript)
                    ? state.structuredTranscript.slice()
                    : [];
            },
            getOperatorDiagnosticsSnapshot: function () {
                return {
                    counters: { ...(state.operatorDiagnosticsCounters || {}) },
                    lastEmitMs: { ...(state.operatorDiagnosticsLastEmitMs || {}) },
                };
            },
        };
    }

    function createControllerCompatibilityFacade(implementation) {
        const controllerImpl = implementation && typeof implementation === "object"
            ? implementation
            : {};

        const legacyApiMethods = [
            "nextId",
            "post",
            "flushQueue",
            "request",
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
            "addAttachmentFiles",
            "loadSessionOptions",
            "subscribeSessionUpdates",
            "unsubscribeSessionUpdates",
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
            "loadAssistantIdentity",
            "getControlUiBootstrapConfig",
            "persistDraftForSession",
            "restoreDraftForSession",
            "recallInputHistory",
            "getSlashCommandHints",
            "processSendQueue",
            "sendDetachedMessage",
            "transcribeSpeech",
            "applySpeechLifecycleUpdate",
            "loadSpeechCapabilities",
            "loadSpeechErrorPolicy",
            "getSpeechCapabilitiesSnapshot",
            "getSpeechSessionStateSnapshot",
            "assessTranscriptQuality",
            "parseApprovalTokenFromText",
            "executeExecApprovalAction",
            "noteInboundChatEvent",
            "appendChatBubble",
            "markTerminalRun",
            "hasTerminalRun",
            "hasBufferedAssistantStream",
            "setPolledEventsHandler",
            "scheduleHistoryReconcile",
            "getStructuredTranscript",
            "getOperatorDiagnosticsSnapshot",
        ];

        const facade = {
            __compatFacade: true,
            __compatFacadeVersion: "step8.1",
        };

        for (const methodName of legacyApiMethods) {
            if (typeof controllerImpl[methodName] === "function") {
                facade[methodName] = function (...args) {
                    return controllerImpl[methodName](...args);
                };
            }
        }

        for (const key of Object.keys(controllerImpl)) {
            if (Object.prototype.hasOwnProperty.call(facade, key)) {
                continue;
            }

            const value = controllerImpl[key];
            if (typeof value === "function") {
                facade[key] = function (...args) {
                    return controllerImpl[key](...args);
                };
            } else {
                facade[key] = value;
            }
        }

        return facade;
    }

    function createController(options) {
        const legacyImplementation = createControllerLegacyImplementation(options);
        return createControllerCompatibilityFacade(legacyImplementation);
    }

    function createRegressionState() {
        return {
            connected: true,
            bridgeAvailable: true,
            sessionKey: "main",
            basePath: "",
            assistantName: "Assistant",
            assistantAvatar: "A",
            assistantAgentId: null,
            pending: new Map(),
            bridgeQueue: [],
            terminalRunStates: new Map(),
            reconcileTimer: null,
            reconcileFollowupTimers: [],
            runWatchdogTimer: null,
            runWatchdogStartedAtMs: 0,
            runWatchdogLastInboundEventMs: 0,
            runWatchdogLastReconcileMs: 0,
            runWatchdogLastWarningMs: 0,
            operatorDiagnosticsCounters: {},
            operatorDiagnosticsLastEmitMs: {},
            draftsBySession: new Map(),
            inputHistory: [],
            inputHistoryIndex: -1,
            sendQueue: [],
            slashCommands: [],
            slashCommandsLoaded: false,
            cronCliEnabled: false,
            sessionOptions: [],
            modelOptions: [],
            sessionSubscribed: false,
            sessionCompactionItems: [],
            sessionCompactionSelection: "",
            sessionCompactionStatus: "",
            selectedModel: "default",
            thinkingLevel: "normal",
            inputEl: { value: "" },
            attachInput: { value: "" },
            attachments: [],
            toolTimelineByRequest: new Map(),
            seenBridgeSeq: new Set(),
            seenBridgeIds: new Set(),
            abortBtn: { disabled: true },
            configCoerceEnabled: false,
            assistantIdentityRequestSeq: 0,
            speechCapabilities: {
                sttSupported: false,
                sttReady: false,
                transcriptSupportsSegments: false,
                transcriptSupportsInterim: false,
                transcriptSupportsFinal: true,
                ttsSupported: false,
                ttsReady: false,
                lifecycle: [],
                loaded: false,
                error: "",
            },
            speechSessionState: {
                stage: "idle",
                text: "",
                segmentText: "",
                segmentFinal: true,
                segmentSequence: 0,
                runId: "",
                sessionId: "",
                errorCode: "",
                errorMessage: "",
                errorClass: "status",
                retryable: false,
                retryStrategy: "immediate",
                retryGuidance: "",
                updatedAtMs: 0,
            },
            speechErrorPolicy: {
                loaded: false,
                defaultClass: "status",
                map: {},
                retry: {},
            },
            structuredTranscript: [],
            streamTranscriptDraft: null,
        };
    }

    function assertRegression(condition, message) {
        if (!condition) {
            throw new Error(message);
        }
    }

    async function runRegressionChecks() {
        const summary = [];

        {
            const baseSchema = {
                type: "object",
                properties: {
                    value: { type: "number" },
                    enabled: { type: "boolean" },
                    unionField: {
                        anyOf: [
                            { type: "null" },
                            { type: "number" },
                            { type: "boolean" },
                        ],
                    },
                    nested: {
                        type: "object",
                        additionalProperties: {
                            type: "array",
                            items: { type: "integer" },
                        },
                    },
                },
            };

            const payload = {
                value: "42.5",
                enabled: "true",
                unionField: "7",
                nested: {
                    listA: ["1", "2", "3"],
                    listB: ["4", "x"],
                },
                untouched: "keep",
            };

            const coerced = coerceConfigMutationPayload(payload, baseSchema);
            assertRegression(typeof coerced.value === "number" && coerced.value === 42.5,
                "config payload coercion should convert numeric string fields");
            assertRegression(typeof coerced.enabled === "boolean" && coerced.enabled === true,
                "config payload coercion should convert boolean string fields");
            assertRegression(typeof coerced.unionField === "number" && coerced.unionField === 7,
                "config payload coercion should apply anyOf numeric variant when matched");
            assertRegression(Array.isArray(coerced.nested.listA) &&
                coerced.nested.listA[0] === 1 &&
                coerced.nested.listA[2] === 3,
                "config payload coercion should recurse through nested arrays and integer items");
            assertRegression(coerced.nested.listB[1] === "x",
                "config payload coercion should preserve non-coercible values");
            assertRegression(coerced.untouched === "keep",
                "config payload coercion should preserve unknown additional properties");
            summary.push("config coercion parity fixtures");
        }

        {
            const formUtils = window.BlazeClawConfigFormUtils;
            assertRegression(Boolean(formUtils),
                "config form-utils module should be available");
            assertRegression(typeof formUtils.cloneConfigObject === "function",
                "config form-utils should expose cloneConfigObject");
            assertRegression(typeof formUtils.serializeConfigForm === "function",
                "config form-utils should expose serializeConfigForm");
            assertRegression(typeof formUtils.setPathValue === "function",
                "config form-utils should expose setPathValue");
            assertRegression(typeof formUtils.removePathValue === "function",
                "config form-utils should expose removePathValue");

            const source = {
                gateway: {
                    auth: {
                        token: "abc",
                    },
                    port: 18789,
                },
                list: [{ id: "a" }, { id: "b" }],
            };
            const cloned = formUtils.cloneConfigObject(source);
            formUtils.setPathValue(cloned, ["gateway", "auth", "token"], "xyz");
            formUtils.setPathValue(cloned, ["list", 1, "id"], "b2");
            formUtils.removePathValue(cloned, ["list", 0]);
            formUtils.setPathValue(cloned, ["__proto__", "polluted"], true);

            assertRegression(source.gateway.auth.token === "abc",
                "config form-utils clone should not mutate source object");
            assertRegression(cloned.gateway.auth.token === "xyz",
                "config form-utils setPathValue should update nested object paths");
            assertRegression(Array.isArray(cloned.list) && cloned.list.length === 1,
                "config form-utils removePathValue should splice array indexes");
            assertRegression(cloned.list[0].id === "b2",
                "config form-utils path updates should support mixed object-array traversal");
            assertRegression(({}).polluted === undefined,
                "config form-utils should reject forbidden prototype pollution path keys");

            const raw = formUtils.serializeConfigForm({
                gateway: {
                    port: 18789,
                },
            });
            assertRegression(raw.endsWith("\n"),
                "config form-utils serializeConfigForm should append trailing newline");

            const reparsed = JSON.parse(raw);
            assertRegression(reparsed.gateway.port === 18789,
                "config form-utils serializeConfigForm should preserve numeric values");

            summary.push("config form-utils parity fixtures");
        }

        {
            const state = createRegressionState();
            state.connected = false;
            const calls = [];
            const controller = createController({
                state,
            });

            await controller.loadAssistantIdentity({
                requestOverride: async (method, params) => {
                    calls.push({ method, params });
                    return {
                        payload: {
                            name: "ShouldNotLoad",
                        },
                    };
                },
            });

            assertRegression(calls.length === 0,
                "assistant identity loader should short-circuit when disconnected");
            summary.push("assistant guard disconnected");
        }

        {
            const parsed = parseApprovalTokenFromText("Email scheduling pending approval. approvalToken=email-approval-1775016776785-60 expiresAtEpochMs=1775016776785");
            assertRegression(Boolean(parsed) &&
                parsed.approvalToken === "email-approval-1775016776785-60" &&
                parsed.expiresAtEpochMs === 1775016776785,
                "approval parser should extract token and expiry from assistant text");
            assertRegression(parseApprovalTokenFromText("no token present") === null,
                "approval parser should return null when token marker is absent");
            assertRegression(parseApprovalTokenFromText("approvalToken=e expiresAtEpochMs=1775016776785") === null,
                "approval parser should reject truncated streamed token fragments");
            summary.push("exec approval token parser");
        }

        {
            const state = createRegressionState();
            const calls = [];
            const controller = createController({
                state,
                addMessage: function () { },
            });
            const approved = await controller.executeExecApprovalAction("email-token-1", true, {
                requestOverride: async (method, params) => {
                    calls.push({ method, params });
                    if (method === "gateway.email.backend.readiness") {
                        return {
                            payload: {
                                ready: true,
                                errorCode: "none",
                            },
                        };
                    }
                    return {
                        payload: {
                            status: "ok",
                            output: "{\"executed\":true}",
                        },
                    };
                },
            });
            assertRegression(calls.length === 2 &&
                calls[0].method === "gateway.email.backend.readiness" &&
                calls[1].method === "gateway.tools.call.execute",
                "exec approval action should probe backend readiness before gateway.tools.call.execute");
            assertRegression(calls[1].params &&
                calls[1].params.tool === "email.schedule" &&
                calls[1].params.args &&
                calls[1].params.args.action === "approve" &&
                calls[1].params.args.approvalToken === "email-token-1" &&
                calls[1].params.args.approve === true,
                "exec approval action should submit canonical email.schedule approval args");
            assertRegression(approved.ok === true && approved.status === "ok",
                "exec approval action should report successful approval resolution");
            assertRegression(approved.readinessKnown === true && approved.readinessReady === true,
                "exec approval action should expose readiness metadata for approve path");
            summary.push("exec approval approve action");
        }

        {
            const state = createRegressionState();
            const calls = [];
            const controller = createController({
                state,
                addMessage: function () { },
            });
            const invalid = await controller.executeExecApprovalAction("email-app", true, {
                requestOverride: async (method, params) => {
                    calls.push({ method, params });
                    return { payload: {} };
                },
            });
            assertRegression(invalid.ok === false && invalid.status === "invalid",
                "exec approval action should reject malformed approval tokens before RPC");
            assertRegression(calls.length === 0,
                "exec approval action should not call gateway for malformed tokens");
            summary.push("exec approval malformed-token guard");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });
            const denied = await controller.executeExecApprovalAction("email-token-2", false, {
                requestOverride: async () => ({
                    payload: {
                        status: "cancelled",
                        output: "{\"cancelled\":true}",
                    },
                }),
            });
            const expired = await controller.executeExecApprovalAction("email-token-3", true, {
                requestOverride: async (method, params) => {
                    if (method === "gateway.email.backend.readiness") {
                        return {
                            payload: {
                                ready: false,
                                errorCode: "imap_smtp_skill_missing",
                                message: "Email backend dependency is missing for approval execution.",
                                remediation: "Install required email skill backend dependencies and retry approve.",
                                missingDependency: "imap_smtp_email",
                                installHint: "Install Himalaya CLI and imap_smtp_email skill assets.",
                                configHint: "Verify backend scripts and account profile configuration.",
                                bucket: "missing_skill",
                            },
                        };
                    }
                    return {
                        payload: {
                            status: "error",
                            output: "approval_token_expired",
                        },
                    };
                },
            });
            const remapped = await controller.executeExecApprovalAction("email-token-4", true, {
                requestOverride: async (method) => {
                    if (method === "gateway.email.backend.readiness") {
                        return {
                            payload: {
                                ready: false,
                                errorCode: "imap_smtp_skill_missing",
                                message: "Email backend dependency is missing for approval execution.",
                                remediation: "Install required email skill backend dependencies and retry approve.",
                                missingDependency: "imap_smtp_email",
                                installHint: "Install Himalaya CLI and imap_smtp_email skill assets.",
                                configHint: "Verify backend scripts and account profile configuration.",
                                bucket: "missing_skill",
                            },
                        };
                    }
                    return {
                        payload: {
                            status: "error",
                            errorCode: "legacy_execution_failed",
                            output: "{\"ok\":false,\"error\":{\"code\":\"imap_smtp_skill_missing\",\"message\":\"imap_smtp_skill_missing\",\"remediation\":\"install backend\"}}",
                        },
                    };
                },
            });
            assertRegression(denied.ok === true && denied.status === "cancelled",
                "exec approval action should treat cancelled response as resolved deny path");
            assertRegression(expired.ok === false && expired.status === "expired",
                "exec approval action should classify token-expired responses");
            assertRegression(remapped.ok === false && remapped.errorCode === "imap_smtp_skill_missing",
                "exec approval action should prioritize structured mapped nested error code over legacy top-level code");
            assertRegression(remapped.readinessKnown === true && remapped.readinessReady === false &&
                remapped.readinessCode === "imap_smtp_skill_missing" &&
                remapped.readinessMissingDependency === "imap_smtp_email",
                "exec approval action should propagate backend readiness hints for pre-approve guardrail rendering");
            summary.push("exec approval deny + expired actions");
        }

        {
            const state = createRegressionState();
            const calls = [];
            const controller = createController({
                state,
            });

            await controller.loadAssistantIdentity({
                sessionKey: "  session-a ",
                requestOverride: async (method, params) => {
                    calls.push({ method, params });
                    return {
                        payload: {
                            name: "Alpha",
                            avatar: "⚡",
                            agentId: "agent-alpha",
                        },
                    };
                },
            });

            assertRegression(calls.length === 1,
                "assistant identity loader should issue exactly one request");
            assertRegression(calls[0].method === "agent.identity.get",
                "assistant identity loader should call agent.identity.get");
            assertRegression(calls[0].params && calls[0].params.sessionKey === "session-a",
                "assistant identity loader should pass trimmed session key param");
            assertRegression(state.assistantName === "Alpha" &&
                state.assistantAvatar === "⚡" &&
                state.assistantAgentId === "agent-alpha",
                "assistant identity loader should apply normalized identity values");
            summary.push("assistant session-key propagation");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
            });

            await controller.loadAssistantIdentity({
                requestOverride: async () => ({
                    payload: {
                        name: "",
                        avatar: "",
                        agentId: "",
                    },
                }),
            });

            assertRegression(state.assistantName === "Assistant" &&
                state.assistantAvatar === null &&
                state.assistantAgentId === null,
                "assistant identity loader should normalize empty payload values to defaults");
            summary.push("assistant normalization defaults");
        }

        {
            const state = createRegressionState();
            state.assistantName = "Persisted";
            state.assistantAvatar = "P";
            state.assistantAgentId = "agent-persisted";
            const controller = createController({
                state,
            });

            await controller.loadAssistantIdentity({
                requestOverride: async () => {
                    throw new Error("assistant identity unavailable");
                },
            });

            assertRegression(state.assistantName === "Persisted" &&
                state.assistantAvatar === "P" &&
                state.assistantAgentId === "agent-persisted",
                "assistant identity loader should retain last known values on request failure");
            summary.push("assistant error retention");
        }

        {
            const state = createRegressionState();
            state.basePath = " /openclaw ";
            const controller = createController({
                state,
            });

            const snapshot = await controller.getControlUiBootstrapConfig();
            assertRegression(snapshot.basePath === "" &&
                snapshot.assistantName === "Assistant" &&
                snapshot.assistantAvatar === "A",
                "bootstrap snapshot should normalize to control-ui field names and preserve default identity values");
            summary.push("control-ui bootstrap snapshot defaults");
        }

        {
            const state = createRegressionState();
            state.assistantName = "Persisted";
            state.assistantAvatar = "P";
            state.assistantAgentId = "agent-persisted";
            const calls = [];
            const controller = createController({
                state,
            });

            const snapshot = await controller.getControlUiBootstrapConfig({
                refreshIdentity: true,
                sessionKey: "  s-bootstrap ",
                basePath: " /openclaw ",
                requestOverride: async (method, params) => {
                    calls.push({ method, params });
                    throw new Error("bridge identity unavailable");
                },
            });

            assertRegression(calls.length === 1 &&
                calls[0].method === "agent.identity.get" &&
                calls[0].params && calls[0].params.sessionKey === "s-bootstrap",
                "bootstrap adapter refresh should delegate to session-aware assistant identity loader");
            assertRegression(snapshot.basePath === "/openclaw" &&
                snapshot.assistantName === "Persisted" &&
                snapshot.assistantAvatar === "P",
                "bootstrap adapter should retain last-known identity when refresh path fails");
            summary.push("control-ui bootstrap refresh retention");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
            });

            let resolveOlder;
            const olderPromise = new Promise((resolve) => {
                resolveOlder = resolve;
            });
            const newerPromise = Promise.resolve({
                payload: {
                    name: "Newer",
                    avatar: "N",
                    agentId: "agent-newer",
                },
            });
            let callIndex = 0;

            const first = controller.loadAssistantIdentity({
                requestOverride: async () => {
                    callIndex += 1;
                    if (callIndex === 1) {
                        return olderPromise;
                    }
                    return newerPromise;
                },
            });
            const second = controller.loadAssistantIdentity({
                requestOverride: async () => {
                    callIndex += 1;
                    if (callIndex === 1) {
                        return olderPromise;
                    }
                    return newerPromise;
                },
            });

            resolveOlder({
                payload: {
                    name: "Older",
                    avatar: "O",
                    agentId: "agent-older",
                },
            });
            await Promise.all([first, second]);

            assertRegression(state.assistantName === "Newer" &&
                state.assistantAvatar === "N" &&
                state.assistantAgentId === "agent-newer",
                "assistant identity loader should ignore stale out-of-order responses");
            summary.push("assistant stale-response suppression");
        }

        {
            const state = createRegressionState();
            state.configCoerceEnabled = true;
            const calls = [];
            const controller = createController({
                state,
                addMessage: function () { },
            });

            const ok = await controller.applyModelSelection("next-model", {
                requestOverride: async (method, params) => {
                    calls.push({ method, params });
                    return { payload: { updated: true } };
                },
            });
            assertRegression(ok === true,
                "config submit path should resolve success when config.set request succeeds");
            assertRegression(calls.length === 1 && calls[0].method === "config.set",
                "config submit path should call canonical config.set");
            assertRegression(calls[0].params && calls[0].params.model === "next-model",
                "config submit path should preserve model value through coercion pipeline");
            summary.push("config submit path smoke");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });
            assertRegression(controller.assessTranscriptQuality("写一首诗用它来描述春天的色彩").accepted === true,
                "speech transcript quality should accept clean CJK utterance");
            const repeatedQuality = controller.assessTranscriptQuality(
                "写一首诗用它来描述描述描述描述描述春天的色素色素色素色素色素色素色素色素色素色素色素色素色素色素色素色素色素彩");
            assertRegression(repeatedQuality.accepted === false &&
                repeatedQuality.reason === "repetitive phrase transcript pattern detected",
                "speech transcript quality should reject repeated CJK phrase transcript");
            summary.push("speech transcript CJK repeat quality gate");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });
            assertRegression(typeof controller.getStructuredTranscript === "function",
                "chat controller should expose structured transcript snapshot accessor");
            assertRegression(controller.getStructuredTranscript().length === 0,
                "structured transcript should start empty before history load");
            summary.push("structured transcript smoke");
        }

        {
            const parsed = parseTextFromMessage({
                content: [
                    { type: "text", text: "Assistant response" },
                    { type: "image", mimeType: "image/png" },
                    { type: "tool-call", tool: "search_docs" },
                    { type: "tool-result", tool: "search_docs" },
                ],
            });
            assertRegression(parsed.includes("Assistant response"),
                "message parser should preserve text blocks");
            assertRegression(parsed.includes("[Image attachment: image/png]"),
                "message parser should degrade image blocks to stable text markers");
            assertRegression(parsed.includes("[Tool call: search_docs]") &&
                parsed.includes("[Tool result: search_docs]"),
                "message parser should degrade tool blocks to stable text markers");
            summary.push("structured content degraded-text rendering");
        }

        {
            const state = createRegressionState();
            const messageRows = [];
            const sendCalls = [];
            const detachedNotices = [];
            const controller = createController({
                state,
                addMessage: (text, kind) => {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
                onDetachedNotice: (notice) => {
                    detachedNotices.push(notice);
                },
            });
            const sent = await controller.sendDetachedMessage("side channel note", {
                requestOverride: async (method, params) => {
                    sendCalls.push({ method, params });
                    return {
                        payload: {
                            runId: "detached-run-1",
                        },
                    };
                },
            });
            assertRegression(sent === true,
                "detached send helper should report success for non-empty messages");
            assertRegression(sendCalls.length === 1 &&
                sendCalls[0].method === "chat.send",
                "detached send helper should call chat.send");
            assertRegression(sendCalls[0].params &&
                sendCalls[0].params.detached === true &&
                sendCalls[0].params.deliver === true &&
                sendCalls[0].params.clientMode === "webchat",
                "detached send helper should set detached/deliver/clientMode request params");
            assertRegression(messageRows.every((row) => row.kind !== "self"),
                "detached send helper should avoid local self bubble rendering");
            assertRegression(detachedNotices.length === 1 &&
                detachedNotices[0].kind === "sent",
                "detached send helper should emit side-channel notice callback events");
            summary.push("detached send semantics");
        }

        {
            const state = createRegressionState();
            const calls = [];
            const sessionSnapshots = [];
            const controller = createController({
                state,
                addMessage: function () { },
                onSessionControlStateChanged: (snapshot) => {
                    sessionSnapshots.push(snapshot);
                },
            });
            const subscribed = await controller.subscribeSessionUpdates({
                requestOverride: async (method, params) => {
                    calls.push({ method, params });
                    return { payload: { subscribed: true } };
                },
            });
            assertRegression(subscribed === true,
                "session subscribe helper should resolve true on successful request");
            assertRegression(calls.length === 1 &&
                calls[0].method === "sessions.subscribe" &&
                calls[0].params &&
                calls[0].params.sessionKey === "main",
                "session subscribe helper should call sessions.subscribe with active session");

            const compactions = await controller.loadSessionCompactions({
                requestOverride: async (method) => {
                    calls.push({ method });
                    return {
                        payload: {
                            compactions: [
                                { id: "cmp-1", label: "checkpoint one" },
                                { branchId: "br-2", summary: "checkpoint two" },
                            ],
                        },
                    };
                },
            });
            assertRegression(Array.isArray(compactions) && compactions.length === 2 &&
                compactions[0].id === "cmp-1" && compactions[1].id === "br-2",
                "compaction loader should normalize compaction ids from payload");
            assertRegression(sessionSnapshots.length >= 2 &&
                sessionSnapshots[sessionSnapshots.length - 1].selectedCompactionId === "cmp-1",
                "session control callback should receive normalized compaction selection state");
            summary.push("session subscribe + compaction controls");
        }

        {
            const state = createRegressionState();
            const streamSnapshots = [];
            let streamFinalized = 0;
            const controller = createController({
                state,
                addOrReplaceStream: (text) => streamSnapshots.push(String(text || "")),
            });
            const eventsModule = window.BlazeClawChatEvents
                ? window.BlazeClawChatEvents.createEventsModule({
                    state,
                    controller,
                    addMessage: function () { },
                    addOrReplaceStream: (text) => streamSnapshots.push(String(text || "")),
                    finalizeStream: function () { streamFinalized += 1; },
                })
                : null;
            assertRegression(Boolean(eventsModule),
                "chat events module should be available for transcript parity checks");

            state.runId = "run-1";
            eventsModule.handleChatEvents([{
                sessionKey: "main",
                runId: "run-1",
                state: "delta",
                message: { text: "Hello wor" },
            }]);
            eventsModule.handleChatEvents([{
                sessionKey: "main",
                runId: "run-1",
                state: "final",
                message: { role: "assistant", text: "Hello world" },
            }]);

            const transcript = controller.getStructuredTranscript();
            const finalEntry = transcript[transcript.length - 1];
            assertRegression(Boolean(finalEntry) &&
                finalEntry.role === "assistant" &&
                finalEntry.text === "Hello world" &&
                finalEntry.runId === "run-1" &&
                finalEntry.terminalState === "final" &&
                finalEntry.source === "stream",
                "terminal final events should commit deterministic structured transcript entries");
            assertRegression(streamSnapshots.includes("Hello wor") &&
                streamSnapshots.includes("Hello world") &&
                streamFinalized === 1,
                "delta/final flow should still drive stream rendering transitions");
            summary.push("stream final transcript commit");
        }

        {
            const state = createRegressionState();
            const reconcileCalls = [];
            const messageRows = [];
            const controller = createController({
                state,
                addMessage: (text, kind) => {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
            });
            controller.scheduleHistoryReconcile = function () {
                reconcileCalls.push("scheduled");
            };
            const eventsModule = window.BlazeClawChatEvents
                ? window.BlazeClawChatEvents.createEventsModule({
                    state,
                    controller,
                    addMessage: function () { },
                    finalizeStream: function () { },
                    addOrReplaceStream: function () { },
                })
                : null;
            assertRegression(Boolean(eventsModule),
                "chat events module should be available for reconcile guard checks");

            state.runId = "run-2";
            eventsModule.handleChatEvents([{
                sessionKey: "main",
                runId: "run-2",
                state: "final",
                message: { role: "assistant", text: "done" },
            }]);
            eventsModule.handleChatEvents([{
                sessionKey: "main",
                runId: "run-3",
                state: "final",
                message: { role: "assistant", text: "" },
            }]);
            state.runId = "stale-run-active";
            eventsModule.handleChatEvents([{
                sessionKey: "main",
                runId: "canonical-run-final",
                state: "final",
                message: { role: "assistant", text: "mismatch terminal recovered" },
            }]);

            assertRegression(reconcileCalls.length === 1,
                "history reconcile should run only as repair when terminal text is unavailable");
            assertRegression(state.runId === null,
                "mismatched terminal events should clear stale active run state");
            assertRegression(messageRows.some((row) =>
                row.kind === "peer" && row.text === "mismatch terminal recovered"),
                "mismatched terminal recovery should preserve terminal assistant text visibility");
            state.runId = "stale-run-aborted";
            eventsModule.handleChatEvents([{
                sessionKey: "main",
                runId: "canonical-run-aborted",
                state: "aborted",
                message: { role: "assistant", text: "mismatch aborted recovered" },
            }]);
            assertRegression(state.runId === null,
                "mismatched aborted events should also clear stale active run state");
            assertRegression(messageRows.some((row) =>
                row.kind === "peer" && row.text === "mismatch aborted recovered"),
                "mismatched aborted recovery should preserve terminal assistant text visibility");
            summary.push("history reconcile repair-only");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });
            const pendingId = "rpc-scope-1";
            let rejectionText = "";
            state.pending.set(pendingId, {
                method: "sessions.list",
                resolve: function () { },
                reject: function (reason) {
                    rejectionText = String(reason || "");
                },
            });
            controller.handleRpcResult({
                id: pendingId,
                ok: false,
                error: {
                    code: "unauthorized",
                    message: "missing scope: operator.read",
                    detailCode: "AUTH_UNAUTHORIZED",
                },
            });
            assertRegression(rejectionText.toLowerCase().includes("operator.read"),
                "rpc scope failures should map to operator-readable scope guidance");
            summary.push("scope-aware rpc error formatting");
        }

        {
            const state = createRegressionState();
            state.runId = "stale-run-id";
            const messageRows = [];
            const controller = createController({
                state,
                addMessage: (text, kind) => {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
            });
            const calls = [];
            await controller.abort({
                requestOverride: async (method, params) => {
                    calls.push({ method, params });
                    if (method === "chat.abort") {
                        return {
                            payload: {
                                aborted: false,
                                runId: "different-run-id",
                            },
                        };
                    }
                    if (method === "chat.events.poll") {
                        return {
                            payload: {
                                events: [{
                                    sessionKey: "main",
                                    runId: "different-run-id",
                                    state: "final",
                                    message: { role: "assistant", text: "done" },
                                }],
                            },
                        };
                    }
                    return { payload: {} };
                },
            });
            assertRegression(calls.length >= 2 &&
                calls[0].method === "chat.abort" &&
                calls[1].method === "chat.events.poll",
                "abort fallback should trigger one reconcile poll when abort outcome is unresolved");
            assertRegression(state.runId === null,
                "abort fallback reconcile should clear stale active run state after terminal evidence");
            assertRegression(messageRows.some((row) =>
                row.kind === "peer" && row.text.includes("abort fallback reconciled stale run state")),
                "abort fallback should emit explicit recovery diagnostic message");
            assertRegression(Number(state.operatorDiagnosticsCounters["chat.abort.stale_run_reconcile"] || 0) >= 1,
                "abort fallback recovery should increment stale-run reconcile diagnostic counter");
            summary.push("abort fallback reconcile recovery");
        }

        {
            const state = createRegressionState();
            state.runId = "stale-run-id-no-terminal";
            const messageRows = [];
            const controller = createController({
                state,
                addMessage: (text, kind) => {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
            });
            await controller.abort({
                requestOverride: async (method) => {
                    if (method === "chat.abort") {
                        return {
                            payload: {
                                aborted: false,
                            },
                        };
                    }
                    if (method === "chat.events.poll") {
                        return {
                            payload: {
                                events: [{
                                    sessionKey: "main",
                                    runId: "still-running",
                                    state: "delta",
                                    message: { text: "still running" },
                                }],
                            },
                        };
                    }
                    return { payload: {} };
                },
            });
            assertRegression(state.runId === "stale-run-id-no-terminal",
                "abort fallback should keep run active when reconcile has no terminal evidence");
            assertRegression(messageRows.some((row) =>
                row.kind === "error" && row.text.includes("abort diagnostic: target=stale-run-id-no-terminal")),
                "abort fallback unresolved path should emit explicit diagnostic message");
            summary.push("abort fallback unresolved diagnostics");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });
            await controller.sendDetachedMessage("phase0 detached", {
                requestOverride: async () => ({
                    runId: "server-run-id-top-level",
                }),
            });
            assertRegression(state.runId === "server-run-id-top-level",
                "send path should accept top-level runId in rpc response envelope");
            assertRegression(Number(state.operatorDiagnosticsCounters["chat.queue.run_id_remapped"] || 0) >= 1,
                "run-id remap should increment operator diagnostic counter");
            summary.push("run-id extraction tolerant envelope compatibility");
        }

        {
            const state = createRegressionState();
            state.runId = "run-queue-1";
            state.inputEl.value = "Follow-up while previous run is active";
            const messageRows = [];
            const controller = createController({
                state,
                addMessage: (text, kind) => {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
            });

            await controller.send(false);
            assertRegression(state.sendQueue.length === 1,
                "active run should queue follow-up send payloads");
            assertRegression(messageRows.some((row) =>
                row.kind === "peer" &&
                row.text.includes("waiting for terminal event; queued message (1)")),
                "queued-send guardrail should surface explicit waiting-for-terminal status text");
            summary.push("queue waiting-status guardrail");
        }

        {
            const state = createRegressionState();
            state.inputEl.value = "/cron status";
            const captured = [];
            let cronCalls = 0;
            const controller = createController({
                state,
                addMessage: (text, kind) => {
                    captured.push({
                        text: String(text || ""),
                        kind: String(kind || ""),
                    });
                },
                onCronSlashCommand: async (line) => {
                    cronCalls += 1;
                    return {
                        handled: true,
                        ok: true,
                        kind: "peer",
                        message: JSON.stringify({
                            surface: "cron-cli",
                            ok: true,
                            code: "ok",
                            command: "status",
                            echo: line,
                        }),
                    };
                },
            });

            await controller.send(false);
            assertRegression(cronCalls === 1,
                "chat slash handler should route /cron command lines to injected execution bridge callback");
            assertRegression(captured.some((row) =>
                row.kind === "peer" &&
                row.text.indexOf("\"surface\":\"cron-cli\"") >= 0 &&
                row.text.indexOf("\"command\":\"status\"") >= 0),
                "chat slash handler should render deterministic /cron execution envelope output");
            summary.push("cron slash execution bridge callback");
        }

        {
            const stateDisabled = createRegressionState();
            stateDisabled.cronCliEnabled = false;
            const controllerDisabled = createController({
                state: stateDisabled,
            });
            const disabledHints = await controllerDisabled.getSlashCommandHints("");
            assertRegression(!disabledHints.some((item) => item.name === "cron"),
                "slash hints should omit /cron when cronCliEnabled is false");
            summary.push("cron slash hint hidden when disabled");
        }

        {
            const stateEnabled = createRegressionState();
            stateEnabled.cronCliEnabled = true;
            const controllerEnabled = createController({
                state: stateEnabled,
            });
            const enabledHints = await controllerEnabled.getSlashCommandHints("cron");
            assertRegression(enabledHints.some((item) => item.name === "cron"),
                "slash hints should include /cron when cronCliEnabled is true");
            summary.push("cron slash hint visible when enabled");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });
            const polledBatch = [{
                sessionKey: "main",
                runId: "run-watchdog-poll-1",
                state: "final",
                message: { role: "assistant", text: "done" },
            }];
            let forwarded = 0;
            controller.setPolledEventsHandler(function (events) {
                if (Array.isArray(events) && events.length === 1 && events[0].runId === "run-watchdog-poll-1") {
                    forwarded += 1;
                }
            });
            state.onPolledChatEvents(polledBatch);
            assertRegression(forwarded === 1,
                "setPolledEventsHandler should forward polled chat events to registered callback");
            controller.clearRunState();
            summary.push("watchdog poll event forwarding");
        }

        {
            const state = createRegressionState();
            const messageRows = [];
            const controller = createController({
                state,
                addMessage: (text, kind) => {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "recording",
                sessionId: "main",
                runId: "speech-preview-regression-1",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-1",
                segment: {
                    text: "interim hello",
                    final: false,
                    sequence: 2,
                },
            });
            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-old",
                segment: {
                    text: "stale text",
                    final: false,
                    sequence: 1,
                },
            });

            const snapshot = controller.getSpeechSessionStateSnapshot();
            assertRegression(snapshot.segmentText === "interim hello" && snapshot.segmentSequence === 2,
                "speech lifecycle should keep newer interim segment when stale preview update arrives");
            assertRegression(messageRows.length === 0,
                "speech lifecycle interim updates should not append chat messages");
            summary.push("speech lifecycle stale interim guard");
        }

        {
            const state = createRegressionState();
            const messageRows = [];
            const sendCalls = [];
            const controller = createController({
                state,
                addMessage: (text, kind) => {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "recording",
                sessionId: "main",
                runId: "speech-preview-regression-2",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-2",
                segment: {
                    text: "interim should not send",
                    final: false,
                    sequence: 3,
                },
            });
            controller.applySpeechLifecycleUpdate({
                stage: "stopped",
                sessionId: "main",
                runId: "speech-final-regression-2",
                text: "",
            });

            await controller.transcribeSpeech({
                audioPath: "final.wav",
                runId: "speech-final-regression-2",
                requestOverride: async (method, params) => {
                    if (method === "speech.transcribe") {
                        return {
                            payload: {
                                stage: "completed",
                                sessionId: "main",
                                runId: params.runId,
                                audioPath: "final.wav",
                                text: "final accepted transcript",
                                speechSession: {
                                    stage: "completed",
                                    sessionId: "main",
                                    runId: params.runId,
                                    audioPath: "final.wav",
                                    text: "final accepted transcript",
                                },
                            },
                        };
                    }
                    if (method === "chat.send") {
                        sendCalls.push(params);
                        return { payload: { runId: "chat-run-final-speech" } };
                    }
                    return { payload: {} };
                },
            });

            assertRegression(sendCalls.length === 1 && sendCalls[0].message === "final accepted transcript",
                "speech finalization should send only authoritative final transcript");
            assertRegression(sendCalls[0].message !== "interim should not send",
                "speech finalization should not send interim-only transcript text");
            assertRegression(Number(state.operatorDiagnosticsCounters["speech.final.replacement"] || 0) >= 1,
                "speech final replacement should increment operator diagnostic counter");
            assertRegression(messageRows.every((row) => row.text.indexOf("interim should not send") < 0),
                "speech finalization should not append interim transcript to message list");
            summary.push("speech final transcript authority");
        }

        {
            const state = createRegressionState();
            const sendCalls = [];
            const controller = createController({
                state,
                addMessage: function () { },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "recording",
                sessionId: "main",
                runId: "speech-preview-regression-3",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-3",
                text: "interim should not be reused",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "stopped",
                sessionId: "main",
                runId: "speech-preview-regression-3",
                text: "interim should not be reused",
            });

            await controller.transcribeSpeech({
                audioPath: "final.wav",
                runId: "speech-final-regression-3",
                requestOverride: async (method, params) => {
                    if (method === "speech.transcribe") {
                        return {
                            payload: {
                                stage: "completed",
                                sessionId: "main",
                                runId: "speech-preview-regression-3",
                                audioPath: "final.wav",
                                text: "authoritative final transcript",
                                speechSession: {
                                    stage: "completed",
                                    sessionId: "main",
                                    runId: "speech-preview-regression-3",
                                    audioPath: "final.wav",
                                    text: "authoritative final transcript",
                                },
                            },
                        };
                    }
                    if (method === "chat.send") {
                        sendCalls.push(params);
                        return { payload: { runId: "chat-run-final-speech-stale-preview" } };
                    }
                    return { payload: {} };
                },
            });

            const snapshot = controller.getSpeechSessionStateSnapshot();
            assertRegression(sendCalls.length === 0,
                "speech finalization should reject response text owned by stale preview run id");
            assertRegression(snapshot.stage === "failed" && snapshot.errorCode === "missing_final_transcript",
                "speech finalization should terminally fail when only preview-owned response text is available");
            summary.push("speech final stale preview response guard");
        }

        {
            const state = createRegressionState();
            const sendCalls = [];
            const controller = createController({
                state,
                addMessage: function () { },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "stopped",
                sessionId: "main",
                runId: "speech-final-regression-3a",
                text: "preview text should not win",
            });

            await controller.transcribeSpeech({
                audioPath: "final.wav",
                runId: "speech-final-regression-3a",
                requestOverride: async (method, params) => {
                    if (method === "speech.transcribe") {
                        return {
                            payload: {
                                stage: "completed",
                                sessionId: "main",
                                runId: params.runId,
                                audioPath: "final.wav",
                                speechSession: {
                                    stage: "completed",
                                    sessionId: "main",
                                    runId: params.runId,
                                    audioPath: "final.wav",
                                    segment: {
                                        text: "nested final transcript",
                                        final: true,
                                        sequence: 1,
                                    },
                                },
                            },
                        };
                    }
                    if (method === "chat.send") {
                        sendCalls.push(params);
                        return { payload: { runId: "chat-run-nested-final-speech" } };
                    }
                    return { payload: {} };
                },
            });

            assertRegression(sendCalls.length === 1 && sendCalls[0].message === "nested final transcript",
                "speech finalization should accept nested final-owned segment text");
            summary.push("speech final nested transcript authority");
        }

        {
            const state = createRegressionState();
            const sendCalls = [];
            const controller = createController({
                state,
                addMessage: function () { },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-3b",
                text: "preview must not be submitted",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "stopped",
                sessionId: "main",
                runId: "speech-final-regression-3b",
                text: "preview must not be submitted",
            });

            await controller.transcribeSpeech({
                audioPath: "final.wav",
                runId: "speech-final-regression-3b",
                requestOverride: async (method, params) => {
                    if (method === "speech.transcribe") {
                        return {
                            payload: {
                                stage: "completed",
                                sessionId: "main",
                                runId: "speech-final-regression-3b",
                                audioPath: "final.wav",
                            },
                        };
                    }
                    if (method === "chat.send") {
                        sendCalls.push(params);
                        return { payload: { runId: "chat-run-should-not-send" } };
                    }
                    return { payload: {} };
                },
            });

            assertRegression(sendCalls.length === 0,
                "speech finalization should not submit preserved preview text when final payload has no transcript");
            const snapshot = controller.getSpeechSessionStateSnapshot();
            assertRegression(snapshot.stage === "failed" && snapshot.errorCode === "missing_final_transcript",
                "speech finalization should leave terminal failed state when final payload has no transcript");
            assertRegression(!snapshot.text && !snapshot.segmentText,
                "speech finalization should clear preserved preview text when final payload has no transcript");
            assertRegression(Number(state.operatorDiagnosticsCounters["speech.request.response_ignored"] || 0) >= 1,
                "speech finalization should trace missing final transcript diagnostics");
            summary.push("speech final missing transcript does not reuse preview text");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "recording",
                sessionId: "main",
                runId: "speech-preview-regression-4",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-4",
                text: "live preview text",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "stopped",
                sessionId: "main",
                runId: "speech-final-regression-4",
                text: "live preview text",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "completed",
                sessionId: "main",
                runId: "speech-preview-regression-4",
                text: "late preview overwrite",
            });

            const snapshot = controller.getSpeechSessionStateSnapshot();
            assertRegression(snapshot.runId === "speech-final-regression-4",
                "speech final authority should keep final run id after late preview lifecycle update");
            assertRegression(snapshot.text === "live preview text",
                "speech final authority should reject late preview lifecycle text overwrite");
            assertRegression(Number(state.operatorDiagnosticsCounters["speech.request.lifecycle_ignored"] || 0) >= 1,
                "speech final authority should emit ignored preview lifecycle diagnostic");
            summary.push("speech final lifecycle preview invalidation");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "stopped",
                sessionId: "main",
                runId: "speech-final-regression-6",
                finalRunId: "speech-final-regression-6",
                text: "preview text before final",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-6",
                text: "late preview after final dispatch",
            });

            const snapshot = controller.getSpeechSessionStateSnapshot();
            assertRegression(snapshot.runId === "speech-final-regression-6",
                "speech final authority should keep final run id when late preview arrives after final dispatch");
            assertRegression(snapshot.text === "preview text before final",
                "speech final authority should reject late preview text after final dispatch");
            assertRegression(Number(state.operatorDiagnosticsCounters["speech.request.lifecycle_ignored"] || 0) >= 1,
                "speech final authority should trace ignored late preview after final dispatch");
            summary.push("speech final dispatch blocks late preview lifecycle");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "recording",
                sessionId: "main",
                runId: "speech-preview-regression-5",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "completed",
                sessionId: "main",
                runId: "speech-preview-regression-5",
                text: "preview terminal text",
            });

            const snapshot = controller.getSpeechSessionStateSnapshot();
            assertRegression(snapshot.stage === "streaming",
                "speech preview terminal lifecycle should remain streaming while recording is active");
            assertRegression(snapshot.runId === "speech-preview-regression-5",
                "speech preview terminal lifecycle should preserve preview run id");
            assertRegression(snapshot.text === "preview terminal text" || snapshot.segmentText === "preview terminal text",
                "speech preview terminal lifecycle should preserve interim text without ending recording");
            summary.push("speech preview terminal remains active");
        }

        {
            const state = createRegressionState();
            const controller = createController({
                state,
                addMessage: function () { },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "recording",
                sessionId: "main",
                runId: "speech-preview-regression-8",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-8",
                text: "partial preview",
            });

            await controller.transcribeSpeech({
                audioPath: "preview.wav",
                runId: "speech-preview-regression-8",
                livePreviewOnly: true,
                requestOverride: async (method, params) => {
                    if (method === "speech.transcribe") {
                        return {
                            payload: {
                                stage: "completed",
                                sessionId: "main",
                                runId: params.runId,
                                audioPath: "preview.wav",
                                text: "",
                                speechSession: {
                                    stage: "completed",
                                    sessionId: "main",
                                    runId: params.runId,
                                    audioPath: "preview.wav",
                                    text: "",
                                },
                            },
                        };
                    }
                    return { payload: {} };
                },
            });

            const snapshot = controller.getSpeechSessionStateSnapshot();
            assertRegression(snapshot.stage === "streaming",
                "speech preview empty completed response should remain non-terminal while recording");
            assertRegression(snapshot.runId === "speech-preview-regression-8",
                "speech preview empty completed response should preserve active preview run id");
            assertRegression(String(snapshot.errorCode || "").trim() === "",
                "speech preview empty completed response should not force preview failed error state");
            summary.push("speech preview empty response remains non-terminal");
        }

        {
            const state = createRegressionState();
            const sendCalls = [];
            const transcribeCalls = [];
            const addMessageCalls = [];
            const controller = createController({
                state,
                addMessage: function (text, kind) {
                    addMessageCalls.push({ text, kind });
                },
            });

            controller.applySpeechLifecycleUpdate({
                stage: "recording",
                sessionId: "main",
                runId: "speech-preview-regression-7",
            });
            controller.applySpeechLifecycleUpdate({
                stage: "streaming",
                sessionId: "main",
                runId: "speech-preview-regression-7",
                text: "preview in flight",
            });

            await controller.transcribeSpeech({
                audioPath: "preview.wav",
                runId: "speech-preview-regression-7",
                livePreviewOnly: true,
                requestOverride: async (method, params) => {
                    if (method === "speech.transcribe") {
                        transcribeCalls.push({ ...params });
                        return {
                            payload: {
                                stage: "completed",
                                sessionId: "main",
                                runId: params.runId,
                                audioPath: "preview.wav",
                            },
                        };
                    }
                    if (method === "chat.send") {
                        sendCalls.push(params);
                        return { payload: { runId: "unexpected-preview-chat-send" } };
                    }
                    return { payload: {} };
                },
            });

            await controller.transcribeSpeech({
                audioPath: "final.wav",
                runId: "speech-final-regression-7",
                requestOverride: async (method, params) => {
                    if (method === "speech.transcribe") {
                        transcribeCalls.push({ ...params });
                        return {
                            payload: {
                                stage: "completed",
                                sessionId: "main",
                                runId: params.runId,
                                audioPath: "final.wav",
                                errorCode: "inference_failed",
                                errorMessage: "final transcript unavailable: no_speech_detected",
                                noSpeechTriage: {
                                    sherpaChunkEnergyAvgPermille: 0,
                                    sherpaVoicedChunkCount: 0,
                                    sherpaNearZeroSamplePermille: 1000,
                                    sherpaInputHealthIndex: 45,
                                    captureChannelIndex: 1,
                                    captureChannelEnergyPermille: 0,
                                },
                                audioArtifact: {
                                    streamId: "voice_recorder",
                                    sequenceStart: 0,
                                    sequenceEnd: 56289,
                                },
                            },
                        };
                    }
                    if (method === "chat.send") {
                        sendCalls.push(params);
                        return { payload: { runId: "unexpected-final-chat-send" } };
                    }
                    return { payload: {} };
                },
            });

            assertRegression(transcribeCalls.length >= 2,
                "speech regression should capture preview and final transcribe dispatch payloads");
            assertRegression(transcribeCalls[0].livePreviewOnly === true,
                "speech preview transcribe payload should explicitly set livePreviewOnly=true");
            assertRegression(transcribeCalls[1].livePreviewOnly === false,
                "speech final transcribe payload should explicitly set livePreviewOnly=false");
            assertRegression(sendCalls.length === 0,
                "speech finalization should not submit chat message when native final error is returned without transcript");

            const snapshot = controller.getSpeechSessionStateSnapshot();
            assertRegression(snapshot.stage === "failed" && snapshot.errorCode === "no_speech_detected",
                "speech finalization should classify no-speech empty-final responses with no_speech_detected code");
            assertRegression(snapshot.errorClass === "status",
                "speech finalization should classify no_speech_detected as status guidance");
            assertRegression(String(snapshot.errorMessage || "").startsWith("speech transcribe no_speech_detected:"),
                "speech stale-label regression should render no_speech_detected as the final message prefix");
            assertRegression(snapshot.noSpeechTriage &&
                Number(snapshot.noSpeechTriage.sherpaInputHealthIndex) === 45 &&
                Number(snapshot.noSpeechTriage.captureChannelIndex) === 1,
                "speech final no-speech path should retain triage payload fields for diagnostics and status guidance");
            assertRegression(
                String(snapshot.retryGuidance || "").indexOf("selected recording device") >= 0,
                "speech final full-silence no-speech should provide device-selection-first retry guidance");
            assertRegression(addMessageCalls.length === 0,
                "speech final no_speech_detected should not emit toast/error chat messages");
            summary.push("speech transcribe no-speech classification and status guidance parity");
        }

        return {
            ok: true,
            checks: summary,
        };
    }

    window.BlazeClawChatController = {
        createController,
        runRegressionChecks,
    };
})();
