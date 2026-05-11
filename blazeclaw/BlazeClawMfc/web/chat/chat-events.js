(function () {
    function trackSeenWithLimit(container, key, maxSize) {
        if (!key) {
            return false;
        }

        if (container.has(key)) {
            return true;
        }

        container.add(key);
        if (container.size > maxSize) {
            const first = container.values().next();
            if (!first.done) {
                container.delete(first.value);
            }
        }

        return false;
    }

    function createEventsModule(options) {
        const opts = options || {};
        const state = opts.state;
        if (!state) {
            throw new Error("chat-events requires state");
        }

        const controller = opts.controller;
        if (!controller) {
            throw new Error("chat-events requires controller");
        }

        const addMessage = opts.addMessage || function () { };
        const appendToolLifecycleRow = opts.appendToolLifecycleRow || function () { };
        const setStatus = opts.setStatus || function () { };
        const updateComposerState = opts.updateComposerState || function () { };
        const finalizeStream = opts.finalizeStream || function () { };
        const addOrReplaceStream = opts.addOrReplaceStream || function () { };
        const upsertApprovalToken = opts.upsertApprovalToken || function () { };
        const onNeedsApprovalEvent = opts.onNeedsApprovalEvent || function () { };

        state.seenChatTerminalRuns = state.seenChatTerminalRuns || new Set();
        state.seenToolLifecycleKeys = state.seenToolLifecycleKeys || new Set();

        function normalizeFinalAssistantMessage(message) {
            if (!message || typeof message !== "object") {
                return null;
            }

            const candidate = message;
            if ("role" in candidate && typeof candidate.role === "string") {
                if (String(candidate.role).toLowerCase() !== "assistant") {
                    return null;
                }
            }

            if (!("content" in candidate) && !("text" in candidate)) {
                return null;
            }

            return candidate;
        }

        function normalizeAbortedAssistantMessage(message) {
            if (!message || typeof message !== "object") {
                return null;
            }

            if (!("role" in message) || String(message.role) !== "assistant") {
                return null;
            }

            if (!Array.isArray(message.content)) {
                return null;
            }

            return message;
        }

        function isTerminalEventState(eventState) {
            return eventState === "final" ||
                eventState === "completed" ||
                eventState === "aborted" ||
                eventState === "error" ||
                eventState === "needs_approval";
        }

        function normalizeInboundBridgeEvent(rawMessage) {
            let normalized = rawMessage;
            if (typeof normalized === "string") {
                try {
                    normalized = JSON.parse(normalized);
                } catch (_) {
                    return null;
                }
            }

            let message = null;
            if (normalized && typeof normalized === "object") {
                message = normalized;
            }
            if (!message) {
                return null;
            }

            if (message.channel === "blazeclaw.transport.event.v1") {
                const topic = String(message.topic || "");

                let payload = {};
                if (message && typeof message.payload === "object") {
                    payload = message.payload;
                }

                let meta = {};
                if (message && typeof message.meta === "object") {
                    meta = message.meta;
                }

                const seq = Number(meta.seq || 0);

                if (seq > 0 && trackSeenWithLimit(state.seenBridgeSeq, `seq:${seq}`, 512)) {
                    return null;
                }

                if (topic === "lifecycle") {
                    return { channel: "blazeclaw.gateway.lifecycle", ...payload };
                }

                if (topic === "rpc.result") {
                    return { channel: "blazeclaw.gateway.rpc.result", ...payload };
                }

                if (topic === "chat.events") {
                    return { channel: "blazeclaw.gateway.chat.events", ...payload };
                }

                if (topic === "tools.lifecycle") {
                    return { channel: "blazeclaw.gateway.tools.lifecycle", ...payload };
                }

                return null;
            }

            if (message.channel === "blazeclaw.gateway.rpc.result") {
                const id = String(message.id || "");
                if (trackSeenWithLimit(state.seenBridgeIds, `rpc:${id}`, 512)) {
                    return null;
                }
            }

            if (message.channel === "blazeclaw.gateway.tools.lifecycle") {
                const id = String(message.id || "");
                const phase = String(message.phase || "");
                if (trackSeenWithLimit(state.seenBridgeIds, `tool:${id}:${phase}`, 512)) {
                    return null;
                }
            }

            return message;
        }

        function notifyNeedsApprovalEvent(event) {
            onNeedsApprovalEvent({
                approvalToken: String(event && event.approvalToken || "").trim(),
                runId: String(event && event.runId || ""),
                sessionKey: String(event && event.sessionKey || state.sessionKey || ""),
            });
        }

        function upsertApprovalTokenFromEvent(event) {
            const approvalToken = String(event && event.approvalToken || "").trim();
            if (!approvalToken) {
                return;
            }

            upsertApprovalToken(approvalToken, "Email scheduling approval required");
        }

        function normalizeSessionKeyLocal(value) {
            const trimmed = String(value || "").trim();
            return trimmed || "main";
        }

        function handleChatEvents(events) {
            if (!Array.isArray(events)) {
                return;
            }

            const activeSession = normalizeSessionKeyLocal(state.sessionKey);

            for (const event of events) {
                if (!event || normalizeSessionKeyLocal(event.sessionKey) !== activeSession) {
                    continue;
                }

                const runId = String(event.runId || "").trim();
                const eventState = String(event.state || "");
                const eventKey = `${runId}:${eventState}`;
                if (runId && isTerminalEventState(eventState)) {
                    if (state.seenChatTerminalRuns.has(eventKey)) {
                        continue;
                    }

                    state.seenChatTerminalRuns.add(eventKey);
                    if (state.seenChatTerminalRuns.size > 512) {
                        const first = state.seenChatTerminalRuns.values().next();
                        if (!first.done) {
                            state.seenChatTerminalRuns.delete(first.value);
                        }
                    }
                }

                if (runId && controller.hasTerminalRun(runId) && eventState === "delta") {
                    continue;
                }

                if (event.state === "error" && event.runId && state.runId && event.runId !== state.runId) {
                    continue;
                }

                if (event.runId && state.runId && event.runId !== state.runId) {
                    const isTerminalMismatch = isTerminalEventState(event.state);
                    if (!isTerminalMismatch) {
                        continue;
                    }

                    let shouldReconcile = false;
                    if (event.state === "final" || event.state === "completed") {
                        const otherFinal = normalizeFinalAssistantMessage(event.message);
                        const text = controller.parseTextFromMessage(otherFinal);
                        if (otherFinal && text && !controller.isSilentReplyText(text)) {
                            addMessage(text, "peer");
                        } else {
                            shouldReconcile = true;
                        }
                    } else if (event.state === "needs_approval") {
                        notifyNeedsApprovalEvent(event);
                        upsertApprovalTokenFromEvent(event);
                        const approvalMessage = normalizeFinalAssistantMessage(event.message);
                        const text = controller.parseTextFromMessage(approvalMessage || event.message);
                        if (text && !controller.isSilentReplyText(text)) {
                            addMessage(text, "peer");
                        } else {
                            const approvalToken = String(event.approvalToken || "").trim();
                            const nextAction = String(event.approvalNextAction || "").trim();
                            let fallback = "Approval required before email send.";
                            if (approvalToken) {
                                fallback += ` approvalToken=${approvalToken}`;
                            }
                            if (nextAction) {
                                fallback += ` nextAction=${nextAction}`;
                            }
                            addMessage(fallback, "peer");
                        }
                    } else if (event.state === "aborted") {
                        const otherAborted = normalizeAbortedAssistantMessage(event.message);
                        const text = controller.parseTextFromMessage(otherAborted || event.message);
                        if (text && !controller.isSilentReplyText(text)) {
                            addMessage(text, "peer");
                        } else {
                            shouldReconcile = true;
                        }
                    } else {
                        addMessage(event.errorMessage || "chat error", "error");
                    }

                    controller.markTerminalRun(runId, event.state);
                    controller.clearRunState();
                    if (shouldReconcile) {
                        controller.scheduleHistoryReconcile();
                    }
                    continue;
                }

                if (typeof controller.noteInboundChatEvent === "function") {
                    controller.noteInboundChatEvent(event.state);
                }

                if (event.state === "delta") {
                    const next = controller.parseTextFromMessage(event.message);
                    controller.applyDeltaText(next);
                    continue;
                }

                if (event.state === "final" || event.state === "completed") {
                    const normalizedFinal = normalizeFinalAssistantMessage(event.message);
                    const text = controller.consumeTerminalText(normalizedFinal || event.message);
                    let shouldReconcile = false;
                    if (text) {
                        const streamedThisTurn =
                            (typeof controller.hasBufferedAssistantStream === "function" &&
                                controller.hasBufferedAssistantStream()) ||
                            Boolean(state.streamText);
                        if (streamedThisTurn) {
                            controller.commitStreamTranscriptFinal({
                                runId,
                                text,
                                terminalState: event.state === "completed" ? "completed" : "final",
                            });
                            addOrReplaceStream(text);
                            finalizeStream();
                        } else {
                            addMessage(text, "peer");
                        }
                    } else {
                        finalizeStream();
                        shouldReconcile = true;
                    }

                    if (runId) {
                        controller.markTerminalRun(runId, event.state === "completed" ? "completed" : "final");
                    }
                    controller.clearRunState();
                    if (shouldReconcile) {
                        controller.scheduleHistoryReconcile();
                    }
                    continue;
                }

                if (event.state === "needs_approval") {
                    notifyNeedsApprovalEvent(event);
                    upsertApprovalTokenFromEvent(event);
                    const normalizedApproval = normalizeFinalAssistantMessage(event.message);
                    let text = controller.consumeTerminalText(normalizedApproval || event.message);
                    if (!text) {
                        const approvalToken = String(event.approvalToken || "").trim();
                        const nextAction = String(event.approvalNextAction || "").trim();
                        text = "Approval required before email send.";
                        if (approvalToken) {
                            text += ` approvalToken=${approvalToken}`;
                        }
                        if (nextAction) {
                            text += ` nextAction=${nextAction}`;
                        }
                    }

                    if (text) {
                        controller.commitStreamTranscriptFinal({
                            runId,
                            text,
                            terminalState: "needs_approval",
                        });

                        const streamedThisTurnApproval =
                            (typeof controller.hasBufferedAssistantStream === "function" &&
                                controller.hasBufferedAssistantStream()) ||
                            Boolean(state.streamText);
                        if (streamedThisTurnApproval) {
                            addOrReplaceStream(text);
                            finalizeStream();
                        } else {
                            addMessage(text, "peer");
                        }
                    } else {
                        finalizeStream();
                        controller.scheduleHistoryReconcile();
                    }

                    if (runId) {
                        controller.markTerminalRun(runId, "needs_approval");
                    }
                    controller.clearRunState();
                    continue;
                }

                if (event.state === "aborted") {
                    const normalizedAborted = normalizeAbortedAssistantMessage(event.message);
                    const text = controller.consumeTerminalText(normalizedAborted || event.message);
                    let shouldReconcile = false;
                    if (text) {
                        controller.commitStreamTranscriptFinal({
                            runId,
                            text,
                            terminalState: "aborted",
                        });
                        addOrReplaceStream(text);
                        finalizeStream();
                    } else {
                        shouldReconcile = true;
                    }

                    if (runId) {
                        controller.markTerminalRun(runId, "aborted");
                    }
                    controller.clearRunState();
                    if (shouldReconcile) {
                        controller.scheduleHistoryReconcile();
                    }
                    continue;
                }

                if (event.state === "error") {
                    addMessage(event.errorMessage || "chat error", "error");
                    if (runId) {
                        controller.markTerminalRun(runId, "error");
                    }
                    controller.clearRunState();
                }
            }

            updateComposerState();
        }

        function handleLifecycle(message) {
            const wasConnected = state.connected;
            state.connected = message.state === "connected" || message.state === "reconnected";

            let status = `gateway: ${message.state}`;
            if (state.connected) {
                let provider = "";
                if (typeof message.provider === "string") {
                    provider = message.provider;
                }

                let model = "";
                if (typeof message.model === "string") {
                    model = message.model;
                }

                let runtimeKind = "";
                if (typeof message.runtimeKind === "string") {
                    runtimeKind = message.runtimeKind;
                }

                const details = [];
                if (runtimeKind) {
                    details.push(runtimeKind);
                }
                if (provider) {
                    details.push(provider);
                }
                if (model) {
                    details.push(model);
                }

                if (details.length > 0) {
                    status += ` (${details.join(" / ")})`;
                }
            }

            setStatus(status);
            if (state.connected && !wasConnected) {
                void controller.loadHistory();
                void controller.refreshSessionControlState({
                    quiet: true,
                });
            }
            if (state.connected) {
                void controller.getControlUiBootstrapConfig({
                    refreshIdentity: true,
                    sessionKey: state.sessionKey,
                });
            }
            if (typeof state.onGatewayLifecycleChanged === "function") {
                state.onGatewayLifecycleChanged({
                    connected: Boolean(state.connected),
                    state: String(message.state || ""),
                    wasConnected: Boolean(wasConnected),
                });
            }

            updateComposerState();
        }

        function handleSessionReset(message) {
            let payload = {};
            if (message && typeof message === "object") {
                payload = message;
            }

            let payloadSession = "";
            if (typeof payload.sessionId === "string") {
                payloadSession = payload.sessionId;
            } else if (payload.session && typeof payload.session.id === "string") {
                payloadSession = payload.session.id;
            }
            if (!payloadSession || payloadSession === state.sessionKey) {
                void controller.loadHistory();
            }
            void controller.refreshSessionControlState({
                quiet: true,
            });
        }

        function handleInboundMessage(rawMessage) {
            const message = normalizeInboundBridgeEvent(rawMessage);
            if (!message || typeof message !== "object") {
                return;
            }

            if (message.channel === "blazeclaw.gateway.lifecycle") {
                handleLifecycle(message);
                return;
            }

            if (message.channel === "blazeclaw.gateway.rpc.result") {
                controller.handleRpcResult(message);
                return;
            }

            if (message.channel === "blazeclaw.gateway.chat.events") {
                const batch = message.events;
                handleChatEvents(Array.isArray(batch) ? batch : []);
                return;
            }

            if (message.channel === "blazeclaw.gateway.tools.lifecycle") {
                const key = `${String(message.id || "")}:${String(message.phase || "")}:${String(message.status || "")}`;
                if (key !== "::" && state.seenToolLifecycleKeys.has(key)) {
                    return;
                }
                state.seenToolLifecycleKeys.add(key);
                if (state.seenToolLifecycleKeys.size > 512) {
                    const first = state.seenToolLifecycleKeys.values().next();
                    if (!first.done) {
                        state.seenToolLifecycleKeys.delete(first.value);
                    }
                }
                appendToolLifecycleRow(message);
                return;
            }

            if (message.channel === "gateway.session.reset" ||
                message.channel === "session.reset" ||
                message.event === "gateway.session.reset") {
                handleSessionReset(message);
            }
        }

        return {
            normalizeInboundBridgeEvent,
            handleChatEvents,
            handleInboundMessage,
        };
    }

    function createRegressionState() {
        return {
            sessionKey: "main",
            runId: null,
            streamText: "",
            seenChatTerminalRuns: new Set(),
            seenToolLifecycleKeys: new Set(),
            seenBridgeSeq: new Set(),
            seenBridgeIds: new Set(),
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
            const state = createRegressionState();
            state.streamText = "partial stream";
            const messageRows = [];
            const streamRows = [];
            const transcriptCommits = [];
            const approvalUpserts = [];
            let finalizeCount = 0;

            const module = createEventsModule({
                state,
                controller: {
                    hasTerminalRun: function () { return false; },
                    markTerminalRun: function () { },
                    clearRunState: function () { },
                    scheduleHistoryReconcile: function () { },
                    parseTextFromMessage: function (message) {
                        if (message && typeof message.text === "string") {
                            return message.text;
                        }
                        return "";
                    },
                    consumeTerminalText: function (message) {
                        if (message && typeof message.text === "string") {
                            return message.text;
                        }
                        return "";
                    },
                    isSilentReplyText: function (text) {
                        return typeof text === "string" && /^\s*NO_REPLY\s*$/i.test(text);
                    },
                    commitStreamTranscriptFinal: function (payload) {
                        transcriptCommits.push(payload);
                        return true;
                    },
                    noteInboundChatEvent: function () { },
                    applyDeltaText: function () { },
                },
                addMessage: function (text, kind) {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
                addOrReplaceStream: function (text) {
                    streamRows.push(String(text || ""));
                },
                finalizeStream: function () {
                    finalizeCount += 1;
                },
                upsertApprovalToken: function (token, title) {
                    approvalUpserts.push({ token: String(token || ""), title: String(title || "") });
                },
            });

            module.handleChatEvents([{
                sessionKey: "main",
                runId: "run-needs-approval-stream",
                state: "needs_approval",
                approvalToken: "email-approval-1234567890-1",
                message: {
                    role: "assistant",
                    text: "Approval required before email send. approvalToken=email-approval-1234567890-1",
                },
            }]);

            assertRegression(transcriptCommits.length === 1 &&
                transcriptCommits[0] &&
                transcriptCommits[0].terminalState === "needs_approval",
                "needs_approval (stream path) should commit transcript terminal state");
            assertRegression(streamRows.length === 1,
                "needs_approval (stream path) should render/update stream text");
            assertRegression(finalizeCount === 1,
                "needs_approval (stream path) should finalize stream");
            assertRegression(messageRows.length === 0,
                "needs_approval (stream path) should not append direct peer bubble");
            assertRegression(approvalUpserts.some(function (entry) {
                return entry.token === "email-approval-1234567890-1";
            }),
                "needs_approval (stream path) should upsert approval token directly");
            summary.push("needs_approval with stream text");
        }

        {
            const state = createRegressionState();
            state.streamText = "";
            const messageRows = [];
            const transcriptCommits = [];
            const approvalUpserts = [];

            const module = createEventsModule({
                state,
                controller: {
                    hasTerminalRun: function () { return false; },
                    markTerminalRun: function () { },
                    clearRunState: function () { },
                    scheduleHistoryReconcile: function () { },
                    parseTextFromMessage: function (message) {
                        if (message && typeof message.text === "string") {
                            return message.text;
                        }
                        return "";
                    },
                    consumeTerminalText: function (message) {
                        if (message && typeof message.text === "string") {
                            return message.text;
                        }
                        return "";
                    },
                    isSilentReplyText: function (text) {
                        return typeof text === "string" && /^\s*NO_REPLY\s*$/i.test(text);
                    },
                    commitStreamTranscriptFinal: function (payload) {
                        transcriptCommits.push(payload);
                        return true;
                    },
                    noteInboundChatEvent: function () { },
                    applyDeltaText: function () { },
                },
                addMessage: function (text, kind) {
                    messageRows.push({ text: String(text || ""), kind: String(kind || "") });
                },
                addOrReplaceStream: function () { },
                finalizeStream: function () { },
                upsertApprovalToken: function (token, title) {
                    approvalUpserts.push({ token: String(token || ""), title: String(title || "") });
                },
            });

            module.handleChatEvents([{
                sessionKey: "main",
                runId: "run-needs-approval-no-stream",
                state: "needs_approval",
                approvalToken: "email-approval-1234567890-2",
                message: {},
            }]);

            assertRegression(transcriptCommits.length === 1 &&
                transcriptCommits[0] &&
                transcriptCommits[0].terminalState === "needs_approval",
                "needs_approval (non-stream path) should still commit transcript terminal state");
            assertRegression(messageRows.some(function (row) {
                return row.kind === "peer" && row.text.indexOf("Approval required before email send.") >= 0;
            }),
                "needs_approval (non-stream path) should render peer assistant feedback");
            assertRegression(approvalUpserts.some(function (entry) {
                return entry.token === "email-approval-1234567890-2";
            }),
                "needs_approval (non-stream path) should upsert approval token directly");
            summary.push("needs_approval without stream text");
        }

        return {
            ok: true,
            checks: summary,
        };
    }

    window.BlazeClawChatEvents = {
        createEventsModule,
        runRegressionChecks,
    };
})();
