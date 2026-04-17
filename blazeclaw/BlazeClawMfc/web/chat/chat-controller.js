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

    function createController(options) {
        const opts = options || {};
        const state = opts.state;
        if (!state) {
            throw new Error("chat-controller requires state");
        }

        const addMessage = opts.addMessage || function () { };
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

        async function loadHistory() {
            try {
                state.streamText = "";
                state.runId = null;
                finalizeStream();
                clearMessages();

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
                const response = await request("gateway.session.list", {
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

        async function switchSession(sessionKey) {
            const nextSession = normalizeSessionKey(sessionKey);
            if (nextSession === state.sessionKey) {
                restoreDraftForSession();
                if (state.sessionSelect) {
                    state.sessionSelect.value = state.sessionKey;
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
            if (state.sessionSelect) {
                state.sessionSelect.value = state.sessionKey;
            }
            updateComposerState();
        }

        async function loadModelOptions() {
            try {
                const response = await request("gateway.models.list", {});
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
                const configResponse = await request("gateway.config.get", {});
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

        async function applyModelSelection(modelId) {
            const nextModel = String(modelId || "").trim();
            if (!nextModel) {
                return false;
            }

            state.selectedModel = nextModel;
            try {
                await request("gateway.config.set", {
                    model: nextModel,
                });
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
                const response = await request("gateway.skills.commands", {});
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
                const response = await request("gateway.sessions.create", {
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
            persistDraftForSession,
            restoreDraftForSession,
            recallInputHistory,
            getSlashCommandHints,
            processSendQueue,
            markTerminalRun,
            hasTerminalRun,
            scheduleHistoryReconcile,
        };
    }

    window.BlazeClawChatController = {
        createController,
    };
})();
