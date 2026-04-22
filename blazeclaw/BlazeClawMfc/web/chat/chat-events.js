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

        function normalizeInboundBridgeEvent(rawMessage) {
            const message = rawMessage && typeof rawMessage === "object" ? rawMessage : null;
            if (!message) {
                return null;
            }

            if (message.channel === "blazeclaw.transport.event.v1") {
                const topic = String(message.topic || "");
                const payload = message && typeof message.payload === "object" ? message.payload : {};
                const meta = message && typeof message.meta === "object" ? message.meta : {};
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

        function handleChatEvents(events) {
            if (!Array.isArray(events)) {
                return;
            }

            for (const event of events) {
                if (!event || event.sessionKey !== state.sessionKey) {
                    continue;
                }

                const runId = String(event.runId || "").trim();
                const eventState = String(event.state || "");
                const eventKey = `${runId}:${eventState}`;
                if (runId && (eventState === "final" || eventState === "aborted" || eventState === "error")) {
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
                    if (event.state === "final") {
                        const otherFinal = normalizeFinalAssistantMessage(event.message);
                        const text = controller.parseTextFromMessage(otherFinal);
                        if (otherFinal && text && !controller.isSilentReplyText(text)) {
                            addMessage(text, "peer");
                        }
                    }
                    continue;
                }

                if (event.state === "delta") {
                    const next = controller.parseTextFromMessage(event.message);
                    controller.applyDeltaText(next);
                    continue;
                }

                if (event.state === "final") {
                    const normalizedFinal = normalizeFinalAssistantMessage(event.message);
                    const text = controller.consumeTerminalText(normalizedFinal || event.message);
                    let shouldReconcile = false;
                    if (text) {
                        if (state.streamText) {
                            controller.commitStreamTranscriptFinal({
                                runId,
                                text,
                                terminalState: "final",
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
                        controller.markTerminalRun(runId, "final");
                    }
                    controller.clearRunState();
                    if (shouldReconcile) {
                        controller.scheduleHistoryReconcile();
                    }
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
                const provider = typeof message.provider === "string" ? message.provider : "";
                const model = typeof message.model === "string" ? message.model : "";
                const runtimeKind = typeof message.runtimeKind === "string" ? message.runtimeKind : "";

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
            const payload = message && typeof message === "object" ? message : {};
            const payloadSession = typeof payload.sessionId === "string"
                ? payload.sessionId
                : (payload.session && typeof payload.session.id === "string"
                    ? payload.session.id
                    : "");
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
                handleChatEvents(message.events);
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

    window.BlazeClawChatEvents = {
        createEventsModule,
    };
})();
