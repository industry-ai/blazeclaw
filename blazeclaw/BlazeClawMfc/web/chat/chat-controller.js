(function () {
    function isSilentReplyText(text) {
        return typeof text === "string" && /^\s*NO_REPLY\s*$/i.test(text);
    }

    function parseTextFromMessage(message) {
        if (!message || typeof message !== "object") {
            return "";
        }

        if (typeof message.text === "string") {
            return message.text;
        }

        if (Array.isArray(message.content)) {
            const item = message.content.find(
                (x) => x && x.type === "text" && typeof x.text === "string");
            return item ? item.text : "";
        }

        return "";
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
        const source = input && typeof input === "object"
            ? input
            : {};

        const name =
            coerceIdentityValue(source.name, MAX_ASSISTANT_NAME) ||
            DEFAULT_ASSISTANT_NAME;
        const avatar =
            coerceIdentityValue(source.avatar, MAX_ASSISTANT_AVATAR) ||
            null;
        const agentId =
            typeof source.agentId === "string" && source.agentId.trim().length > 0
                ? source.agentId.trim()
                : null;

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
        const source = input && typeof input === "object"
            ? input
            : {};
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

    function createController(options) {
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

        function recordStructuredTranscript(role, text) {
            const body = String(text || "").trim();
            if (!body || isSilentReplyText(body)) {
                return;
            }
            state.structuredTranscript.push({
                role,
                text: body,
                ts: Date.now(),
            });
            if (state.structuredTranscript.length > MAX_STRUCTURED_TRANSCRIPT) {
                state.structuredTranscript.splice(
                    0,
                    state.structuredTranscript.length - MAX_STRUCTURED_TRANSCRIPT);
            }
        }

        function addMessage(text, kind) {
            recordStructuredTranscript(bubbleKindToTranscriptRole(kind), text);
            rawAddMessage(text, kind);
        }

        const addOrReplaceStream = opts.addOrReplaceStream || function () { };
        const finalizeStream = opts.finalizeStream || function () { };
        const updateComposerState = opts.updateComposerState || function () { };
        const clearMessages = opts.clearMessages || function () { };
        const setInputValue = opts.setInputValue || function () { };

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

            const existingIndex = state.inputHistory.findIndex((item) => item === trimmed);
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
                state.pending.set(id, { resolve, reject });
                post({ channel: "blazeclaw.gateway.rpc", id, method, params });
            });
        }

        function requestWithOverride(method, params, overrideRequest) {
            if (typeof overrideRequest === "function") {
                return overrideRequest(method, params);
            }
            return request(method, params);
        }

        async function loadHistory() {
            try {
                state.streamText = "";
                state.runId = null;
                finalizeStream();
                clearMessages();
                state.structuredTranscript = [];

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

                    addMessage(text, role === "user" ? "self" : "peer");
                }
            } catch (error) {
                addMessage(`history error: ${String(error)}`, "error");
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
                addMessage(`model update error: ${String(error)}`, "error");
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
                { name: "abort", description: "Abort active run" },
            ];

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
                addMessage(`session switched: ${nextId}`, "peer");
            } catch (error) {
                addMessage(`session create error: ${String(error)}`, "error");
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
                addMessage(`slash commands: ${names}`, "peer");
                return { handled: true };
            }

            if (command === "clear") {
                clearMessages();
                finalizeStream();
                addMessage("chat cleared", "peer");
                return { handled: true };
            }

            if (command === "new") {
                await createSession(args[0] || "");
                return { handled: true };
            }

            if (command === "session") {
                const target = args[0] || "";
                if (!target) {
                    addMessage("usage: /session <sessionId>", "error");
                    return { handled: true };
                }
                await switchSession(target);
                addMessage(`session switched: ${normalizeSessionKey(target)}`, "peer");
                return { handled: true };
            }

            if (command === "model") {
                const target = args[0] || "";
                if (!target) {
                    addMessage("usage: /model <modelId>", "error");
                    return { handled: true };
                }
                const ok = await applyModelSelection(target);
                if (ok) {
                    addMessage(`model set: ${target}`, "peer");
                }
                return { handled: true };
            }

            if (command === "thinking") {
                const target = args[0] || "";
                if (!target) {
                    addMessage("usage: /thinking <low|normal|high>", "error");
                    return { handled: true };
                }
                if (!applyThinkingLevel(target)) {
                    addMessage("invalid thinking level", "error");
                } else {
                    addMessage(`thinking level set: ${state.thinkingLevel}`, "peer");
                }
                return { handled: true };
            }

            if (command === "abort") {
                await abort();
                return { handled: true };
            }

            return { handled: false };
        }

        function queuePendingSend(message, attachments, forceError) {
            state.sendQueue.push({
                message,
                attachments,
                forceError,
            });
            addMessage(`queued message (${state.sendQueue.length})`, "peer");
        }

        async function sendPayload(message, attachments, forceError) {
            const userMessage = String(message || "").trim();
            const payloadAttachments = Array.isArray(attachments) ? attachments : [];

            if ((!userMessage && payloadAttachments.length === 0) || !state.bridgeAvailable) {
                return;
            }

            if (userMessage) {
                pushInputHistory(userMessage);
                addMessage(userMessage, "self");
            }

            if (payloadAttachments.length > 0) {
                addMessage(
                    `[Attachment] ${payloadAttachments.map((item) => item.name).join(", ")}`,
                    "self");
            }

            state.runId = nextId();
            state.streamText = "";
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
                const sendResult = await request("chat.send", {
                    sessionKey: state.sessionKey,
                    message: userMessage,
                    deliver: false,
                    idempotencyKey: state.runId,
                    forceError: Boolean(forceError),
                    model: state.selectedModel,
                    thinkingLevel: state.thinkingLevel,
                    attachments: apiAttachments,
                });

                const serverRunId =
                    sendResult &&
                        sendResult.payload &&
                        typeof sendResult.payload.runId === "string"
                        ? sendResult.payload.runId
                        : "";

                if (serverRunId) {
                    state.runId = serverRunId;
                }
            } catch (error) {
                addMessage(`send error: ${String(error)}`, "error");
                state.runId = null;
                void processSendQueue();
            }

            updateComposerState();
        }

        async function send(forceError) {
            const message = String(state.inputEl.value || "").trim();
            const hasAttachments = state.attachments.length > 0;
            if ((!message && !hasAttachments) || !state.bridgeAvailable) {
                return;
            }

            const localCommand = await handleLocalSlashCommand(message);
            if (localCommand.handled) {
                state.inputEl.value = "";
                persistDraftForSession();
                updateComposerState();
                return;
            }

            const pendingAttachments = state.attachments.slice();
            state.inputEl.value = "";
            state.attachments = [];
            persistDraftForSession();

            if (state.runId) {
                queuePendingSend(message, pendingAttachments, forceError);
                updateComposerState();
                return;
            }

            await sendPayload(message, pendingAttachments, forceError);
        }

        async function processSendQueue() {
            if (state.runId || state.sendQueue.length === 0) {
                return;
            }

            const next = state.sendQueue.shift();
            if (!next) {
                return;
            }

            await sendPayload(next.message, next.attachments, next.forceError);
        }

        async function abort() {
            if (!state.runId || !state.bridgeAvailable) {
                return;
            }

            try {
                await request("chat.abort", {
                    sessionKey: state.sessionKey,
                    runId: state.runId,
                });
            } catch (error) {
                addMessage(`abort error: ${String(error)}`, "error");
            }

            updateComposerState();
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
                slot.reject(
                    message.error ? message.error.message || "request failed" : "request failed");
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
                addOrReplaceStream(state.streamText);
            }
        }

        function clearRunState() {
            state.streamText = "";
            state.runId = null;
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

        function scheduleHistoryReconcile() {
            if (state.reconcileTimer) {
                clearTimeout(state.reconcileTimer);
            }

            state.reconcileTimer = setTimeout(() => {
                state.reconcileTimer = null;
                void loadHistory();
            }, 120);
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
                    addMessage(`attachment skipped: ${file ? file.name : "unknown"}`, "error");
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
                    addMessage(`attachment read error: ${file.name}`, "error");
                }
            }

            if (state.attachInput) {
                state.attachInput.value = "";
            }

            updateComposerState();
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
            clearRunState,
            parseTextFromMessage,
            isSilentReplyText,
            addAttachmentFiles,
            loadSessionOptions,
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
            markTerminalRun,
            hasTerminalRun,
            scheduleHistoryReconcile,
            getStructuredTranscript: function () {
                return Array.isArray(state.structuredTranscript)
                    ? state.structuredTranscript.slice()
                    : [];
            },
        };
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
            draftsBySession: new Map(),
            inputHistory: [],
            inputHistoryIndex: -1,
            sendQueue: [],
            slashCommands: [],
            slashCommandsLoaded: false,
            sessionOptions: [],
            modelOptions: [],
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
            structuredTranscript: [],
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
            assertRegression(typeof controller.getStructuredTranscript === "function",
                "chat controller should expose structured transcript snapshot accessor");
            assertRegression(controller.getStructuredTranscript().length === 0,
                "structured transcript should start empty before history load");
            summary.push("structured transcript smoke");
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
