(function () {
    function resolveStructuredTranscriptRenderEnabled() {
        const search = new URLSearchParams(window.location.search || "");
        const queryValue = String(search.get("structuredTranscript") || "").trim().toLowerCase();
        if (queryValue === "1" || queryValue === "true") {
            return true;
        }
        if (queryValue === "0" || queryValue === "false") {
            return false;
        }

        try {
            if (window.localStorage) {
                const stored = String(
                    window.localStorage.getItem("blazeclaw.chat.structuredTranscript") || ""
                ).trim().toLowerCase();
                if (stored === "1" || stored === "true") {
                    return true;
                }
                if (stored === "0" || stored === "false") {
                    return false;
                }
            }
        } catch (_) {
        }

        return false;
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
        return isSpeechFinalRunId(source.finalRunId) ||
            isSpeechFinalRunId(source.runId);
    }

    function isRecordingSpeechStage(stage, runId) {
        const normalizedStage = String(stage || "").trim();
        const previewRun = isSpeechPreviewRunId(runId);
        if (normalizedStage === "recording" ||
            normalizedStage === "start_stream" ||
            normalizedStage === "streaming") {
            return previewRun;
        }
        return normalizedStage === "queued" && previewRun;
    }

    function createGuiModule(options) {
        const deps = options && typeof options === "object"
            ? options
            : {};
        const state = deps.state && typeof deps.state === "object"
            ? deps.state
            : {};

        const callbacks = {
            scanApprovalTokenFromText: typeof deps.scanApprovalTokenFromText === "function"
                ? deps.scanApprovalTokenFromText
                : function () { },
            harvestApprovalTokensFromText: typeof deps.harvestApprovalTokensFromText === "function"
                ? deps.harvestApprovalTokensFromText
                : function () { },
            resolveApprovalToken: typeof deps.resolveApprovalToken === "function"
                ? deps.resolveApprovalToken
                : async function () { },
            controllerProvider: typeof deps.controllerProvider === "function"
                ? deps.controllerProvider
                : function () { return null; },
        };

        const elements = {
            statusEl: document.getElementById("status"),
            speechStatusEl: document.getElementById("speechStatus"),
            assistantIdentityEl: document.getElementById("assistantIdentity"),
            approvalQueueEl: document.getElementById("approvalQueue"),
            messagesEl: document.getElementById("messages"),
            detachedNoticesEl: document.getElementById("detachedNotices"),
            speechLivePreviewEl: document.getElementById("speechLivePreview"),
        };
        elements.speechLivePreviewLabelEl = elements.speechLivePreviewEl
            ? elements.speechLivePreviewEl.querySelector(".speech-live-preview-label")
            : null;
        elements.speechLivePreviewTextEl = elements.speechLivePreviewEl
            ? elements.speechLivePreviewEl.querySelector(".speech-live-preview-text")
            : null;

        const structuredTranscriptRenderEnabled = resolveStructuredTranscriptRenderEnabled();

        function setStatus(text) {
            if (!elements.statusEl) {
                return;
            }
            elements.statusEl.textContent = text;
        }

        function renderSpeechStatus() {
            if (!elements.speechStatusEl) {
                return;
            }

            const capability = state.speechCapabilities && typeof state.speechCapabilities === "object"
                ? state.speechCapabilities
                : null;
            const sessionState = state.speechSessionState && typeof state.speechSessionState === "object"
                ? state.speechSessionState
                : null;

            if (!capability) {
                elements.speechStatusEl.textContent = "speech: unavailable";
                return;
            }

            const parts = [];
            if (capability.loaded !== true) {
                parts.push("loading");
            } else {
                parts.push(capability.sttReady
                    ? "stt ready"
                    : (capability.sttSupported ? "stt unavailable" : "stt unsupported"));
                if (capability.transcriptSupportsSegments) {
                    parts.push("segments");
                    parts.push(capability.transcriptSupportsInterim ? "interim" : "final-only");
                } else {
                    parts.push("final-only");
                }
                if (capability.streamingPreviewEnabled === false) {
                    parts.push("preview=off");
                }
                parts.push(capability.ttsSupported ? "tts available" : "tts off");
                const capabilityEffectiveProvider = String(
                    capability.effectiveExecutionProvider || ""
                ).trim();
                if (capabilityEffectiveProvider) {
                    parts.push(`provider=${capabilityEffectiveProvider}`);
                }
                const capabilityCudaReason = String(
                    capability.cudaExecutionProviderReason || ""
                ).trim();
                if (capabilityEffectiveProvider === "cuda") {
                    parts.push("cuda=active");
                } else if (capabilityCudaReason && capabilityCudaReason !== "none") {
                    parts.push(`cuda=${capabilityCudaReason}`);
                } else if (capability.cudaExecutionProviderAvailable === false &&
                    capability.cudaExecutionProviderEnabled === false) {
                    parts.push("cuda=unavailable");
                }
            }

            if (capability.error) {
                parts.push(`capErr=${String(capability.error)}`);
            }

            if (sessionState && sessionState.stage) {
                const stage = String(sessionState.stage).trim();
                if (stage) {
                    parts.push(`stage=${stage}`);
                }
                if (sessionState.segmentText) {
                    const stageImpliesFinal =
                        stage === "segment_finalized" ||
                        stage === "stopped" ||
                        stage === "completed" ||
                        stage === "failed" ||
                        stage === "cancelled";
                    const suffix =
                        (sessionState.segmentFinal || stageImpliesFinal) ? "final" : "interim";
                    parts.push(`segment=${suffix}`);
                } else if (sessionState.text) {
                    const textFinal =
                        stage === "segment_finalized" ||
                        stage === "stopped" ||
                        stage === "completed" ||
                        stage === "failed" ||
                        stage === "cancelled";
                    parts.push(`text=${textFinal ? "final" : "stream"}`);
                }
                if (sessionState.errorCode) {
                    parts.push(`err=${String(sessionState.errorCode)}`);
                }
            }

            elements.speechStatusEl.textContent = `speech: ${parts.join(" | ")}`;
        }

        function renderSpeechLivePreview() {
            if (!elements.speechLivePreviewEl ||
                !elements.speechLivePreviewLabelEl ||
                !elements.speechLivePreviewTextEl) {
                return;
            }

            const sessionState = state.speechSessionState && typeof state.speechSessionState === "object"
                ? state.speechSessionState
                : null;
            if (!sessionState) {
                elements.speechLivePreviewEl.hidden = true;
                elements.speechLivePreviewEl.className = "speech-live-preview";
                elements.speechLivePreviewLabelEl.textContent = "";
                elements.speechLivePreviewTextEl.textContent = "";
                return;
            }

            const capability = state.speechCapabilities && typeof state.speechCapabilities === "object"
                ? state.speechCapabilities
                : null;
            const stage = String(sessionState.stage || "").trim();
            const runId = String(sessionState.runId || "").trim();
            const text = String(sessionState.segmentText || sessionState.text || "").trim();
            const errorMessage = String(sessionState.errorMessage || "").trim();
            const previewDisabled = capability && capability.streamingPreviewEnabled === false;
            const finalAuthorityActive = hasSpeechFinalAuthority(sessionState);
            const recording = isRecordingSpeechStage(stage, runId);

            const liveStages = new Set(["recording", "start_stream", "streaming", "queued", "stopped", "transcribing"]);
            const finalizingStages = new Set(["segment_finalized", "completed", "failed", "cancelled"]);
            if (!recording && !finalizingStages.has(stage) && !liveStages.has(stage)) {
                elements.speechLivePreviewEl.hidden = true;
                elements.speechLivePreviewEl.className = "speech-live-preview";
                elements.speechLivePreviewLabelEl.textContent = "";
                elements.speechLivePreviewTextEl.textContent = "";
                return;
            }

            let label = "speech";
            let modeClass = "status";
            if (recording) {
                label = previewDisabled ? "speech (recording)" : "speech preview";
                modeClass = previewDisabled ? "status" : "preview";
            } else if (stage === "segment_finalized" || stage === "completed") {
                label = finalAuthorityActive ? "speech final" : "speech";
                modeClass = "final";
            } else if (stage === "failed") {
                label = "speech error";
                modeClass = "error";
            }

            const noSpeechTriage = sessionState.noSpeechTriage && typeof sessionState.noSpeechTriage === "object"
                ? sessionState.noSpeechTriage
                : null;
            const noSpeechDetected = String(sessionState.errorCode || "").trim() === "no_speech_detected";
            const finalNoSpeechFailed = stage === "failed" && noSpeechDetected;
            if (finalNoSpeechFailed && noSpeechTriage) {
                const energyAvg = Number.isFinite(Number(noSpeechTriage.sherpaChunkEnergyAvgPermille))
                    ? Number(noSpeechTriage.sherpaChunkEnergyAvgPermille)
                    : 0;
                const voiced = Number.isFinite(Number(noSpeechTriage.sherpaVoicedChunkCount))
                    ? Number(noSpeechTriage.sherpaVoicedChunkCount)
                    : 0;
                const nearZero = Number.isFinite(Number(noSpeechTriage.sherpaNearZeroSamplePermille))
                    ? Number(noSpeechTriage.sherpaNearZeroSamplePermille)
                    : 0;
                const health = Number.isFinite(Number(noSpeechTriage.sherpaInputHealthIndex))
                    ? Number(noSpeechTriage.sherpaInputHealthIndex)
                    : 0;
                const channelIndex = Number.isFinite(Number(noSpeechTriage.captureChannelIndex))
                    ? Number(noSpeechTriage.captureChannelIndex)
                    : 0;
                const channelEnergy = Number.isFinite(Number(noSpeechTriage.captureChannelEnergyPermille))
                    ? Number(noSpeechTriage.captureChannelEnergyPermille)
                    : 0;
                const triageText =
                    ` [triage: health=${health}, energyAvg=${energyAvg}, voiced=${voiced}, nearZero=${nearZero}, ch=${channelIndex}, chEnergy=${channelEnergy}]`;
                elements.speechLivePreviewEl.hidden = false;
                elements.speechLivePreviewEl.className = "speech-live-preview status";
                elements.speechLivePreviewLabelEl.textContent = "speech status";
                elements.speechLivePreviewTextEl.textContent =
                    (text || errorMessage || "No speech detected") + triageText;
                return;
            }

            elements.speechLivePreviewEl.hidden = false;
            elements.speechLivePreviewEl.className = `speech-live-preview ${modeClass}`;
            elements.speechLivePreviewLabelEl.textContent = label;
            elements.speechLivePreviewTextEl.textContent = previewDisabled &&
                (liveStages.has(stage) || finalizingStages.has(stage))
                ? (text || errorMessage || "Preview is off; final transcription will run after stop.")
                : (text || errorMessage || "Speak now");
        }

        function renderApprovalQueue() {
            if (!elements.approvalQueueEl) {
                return;
            }
            const queue = Array.isArray(state.approvalQueue) ? state.approvalQueue : [];
            if (!queue.length) {
                elements.approvalQueueEl.hidden = true;
                elements.approvalQueueEl.innerHTML = "";
                return;
            }
            elements.approvalQueueEl.hidden = false;
            elements.approvalQueueEl.innerHTML = "";

            for (const item of queue) {
                if (!item || typeof item !== "object") {
                    continue;
                }
                const card = document.createElement("div");
                card.className = `approval-card ${String(item.status || "pending")}`;
                const title = document.createElement("div");
                title.textContent = String(item.title || "Approval pending");
                card.appendChild(title);

                const meta = document.createElement("div");
                meta.className = "meta";
                meta.textContent =
                    `token=${String(item.token || "")} ?? status=${String(item.status || "pending")}`;
                card.appendChild(meta);

                if (String(item.status || "pending") === "pending") {
                    const actions = document.createElement("div");
                    actions.className = "actions";
                    const approveBtn = document.createElement("button");
                    approveBtn.textContent = "Approve";
                    approveBtn.disabled = Boolean(item.busy);
                    approveBtn.addEventListener("click", function () {
                        void callbacks.resolveApprovalToken(item.token, true);
                    });
                    const denyBtn = document.createElement("button");
                    denyBtn.textContent = "Deny";
                    denyBtn.disabled = Boolean(item.busy);
                    denyBtn.addEventListener("click", function () {
                        void callbacks.resolveApprovalToken(item.token, false);
                    });
                    actions.appendChild(approveBtn);
                    actions.appendChild(denyBtn);
                    card.appendChild(actions);
                } else if (String(item.status || "").toLowerCase() === "failed") {
                    if (item.errorMessage) {
                        const errorLine = document.createElement("div");
                        errorLine.className = "meta";
                        errorLine.textContent = `error=${String(item.errorMessage)}`;
                        card.appendChild(errorLine);
                    }
                    if (item.missingDependency ||
                        item.remediation ||
                        item.installHint ||
                        item.configHint ||
                        (item.readinessKnown && !item.readinessReady)) {
                        const guide = document.createElement("div");
                        guide.className = "meta";
                        const missingParts = [];
                        if (item.missingDependency) {
                            missingParts.push(String(item.missingDependency));
                        }
                        if (item.readinessKnown &&
                            !item.readinessReady &&
                            item.readinessMissingDependency) {
                            const readinessMissing = String(item.readinessMissingDependency);
                            if (!missingParts.includes(readinessMissing)) {
                                missingParts.push(readinessMissing);
                            }
                        }
                        const missingSummary = missingParts.length
                            ? `missing=${missingParts.join("|")} ?? `
                            : "";
                        let remediation = "Repair dependency/configuration and retry approval.";
                        if (item.remediation) {
                            remediation = String(item.remediation);
                        } else if (item.readinessRemediation) {
                            remediation = String(item.readinessRemediation);
                        }
                        const executionDetails = item.installHint || item.configHint
                            ? `${item.installHint ? ` install: ${String(item.installHint)}` : ""}${item.configHint ? ` config: ${String(item.configHint)}` : ""}`
                            : "";
                        const precheckDetails = (item.readinessKnown && !item.readinessReady)
                            ? ` precheck: ${item.readinessCode ? `code=${String(item.readinessCode)} ?? ` : ""}${item.readinessMessage ? String(item.readinessMessage) : "Email backend is not ready."}`
                            : "";
                        guide.textContent =
                            `[backend stack incomplete] ${missingSummary}${remediation}${executionDetails}${precheckDetails}`;
                        card.appendChild(guide);
                    }

                    const retryActions = document.createElement("div");
                    retryActions.className = "actions";
                    const retryApproveBtn = document.createElement("button");
                    retryApproveBtn.textContent = "Retry approve";
                    retryApproveBtn.disabled = Boolean(item.busy);
                    retryApproveBtn.addEventListener("click", function () {
                        item.status = "pending";
                        renderApprovalQueue();
                        void callbacks.resolveApprovalToken(item.token, true);
                    });
                    retryActions.appendChild(retryApproveBtn);
                    card.appendChild(retryActions);
                }

                elements.approvalQueueEl.appendChild(card);
            }
            elements.approvalQueueEl.scrollTop = elements.approvalQueueEl.scrollHeight;
        }

        function renderDetachedNotices() {
            if (!elements.detachedNoticesEl) {
                return;
            }

            const notices = Array.isArray(state.detachedNotices)
                ? state.detachedNotices
                : [];
            if (!notices.length) {
                elements.detachedNoticesEl.hidden = true;
                elements.detachedNoticesEl.innerHTML = "";
                return;
            }

            elements.detachedNoticesEl.hidden = false;
            elements.detachedNoticesEl.innerHTML = "";
            for (const notice of notices) {
                const row = document.createElement("div");
                const kind = String(notice && notice.kind || "sent");
                row.className = `detached-notice ${kind}`;
                row.textContent = String(notice && notice.text || "");
                elements.detachedNoticesEl.appendChild(row);
            }
            elements.detachedNoticesEl.scrollTop = elements.detachedNoticesEl.scrollHeight;
        }

        function renderAssistantIdentity() {
            if (!elements.assistantIdentityEl) {
                return;
            }

            const assistantName = String(state.assistantName || "").trim() || "Assistant";
            const rawAvatar = typeof state.assistantAvatar === "string"
                ? state.assistantAvatar.trim()
                : "";
            const assistantAvatar = rawAvatar || "A";
            const assistantAgentId = typeof state.assistantAgentId === "string"
                ? state.assistantAgentId.trim()
                : "";
            const suffix = assistantAgentId ? ` (${assistantAgentId})` : "";
            elements.assistantIdentityEl.textContent = `${assistantAvatar} ${assistantName}${suffix}`;
        }

        function scrollBottom() {
            if (!elements.messagesEl) {
                return;
            }
            elements.messagesEl.scrollTop = elements.messagesEl.scrollHeight;
        }

        function renderMessagesFromStructuredTranscript(streamTextOverride) {
            const controller = callbacks.controllerProvider();
            if (!structuredTranscriptRenderEnabled || !controller || !elements.messagesEl) {
                return;
            }

            const transcript = typeof controller.getStructuredTranscript === "function"
                ? controller.getStructuredTranscript()
                : [];
            const rows = Array.isArray(transcript) ? transcript : [];
            const streamText = String(streamTextOverride || "").trim();

            elements.messagesEl.innerHTML = "";
            for (const entry of rows) {
                if (!entry || typeof entry !== "object") {
                    continue;
                }
                const text = String(entry.text || "").trim();
                if (!text) {
                    continue;
                }

                const role = typeof entry.role === "string"
                    ? entry.role.trim().toLowerCase()
                    : "assistant";
                let kind = "peer";
                if (role === "user") {
                    kind = "self";
                } else if (role === "error") {
                    kind = "error";
                }
                const div = document.createElement("div");
                div.className = `msg ${kind}`;
                div.textContent = text;
                elements.messagesEl.appendChild(div);
                if (kind === "peer") {
                    callbacks.scanApprovalTokenFromText(text);
                    callbacks.harvestApprovalTokensFromText(text);
                }
            }

            if (streamText && !controller.isSilentReplyText(streamText)) {
                const streamDiv = document.createElement("div");
                streamDiv.id = "stream-msg";
                streamDiv.className = "msg peer";
                streamDiv.textContent = streamText;
                elements.messagesEl.appendChild(streamDiv);
            }

            scrollBottom();
        }

        function addMessage(text, kind) {
            const controller = callbacks.controllerProvider();
            if (structuredTranscriptRenderEnabled) {
                const stream = String(state.streamText || "").trim();
                renderMessagesFromStructuredTranscript(stream);
                return;
            }
            if (!elements.messagesEl) {
                return;
            }
            const div = document.createElement("div");
            div.className = `msg ${kind}`;
            div.textContent = text;
            elements.messagesEl.appendChild(div);
            if (kind === "peer") {
                callbacks.scanApprovalTokenFromText(text);
                callbacks.harvestApprovalTokensFromText(text);
            }
            scrollBottom();
        }

        function clearMessages() {
            if (structuredTranscriptRenderEnabled) {
                renderMessagesFromStructuredTranscript("");
                return;
            }
            if (!elements.messagesEl) {
                return;
            }
            elements.messagesEl.innerHTML = "";
        }

        function addOrReplaceStream(text) {
            const controller = callbacks.controllerProvider();
            if (!controller || !text || controller.isSilentReplyText(text)) {
                return;
            }
            callbacks.scanApprovalTokenFromText(text);
            callbacks.harvestApprovalTokensFromText(text);

            if (structuredTranscriptRenderEnabled) {
                renderMessagesFromStructuredTranscript(text);
                return;
            }
            if (!elements.messagesEl) {
                return;
            }

            const existing = document.getElementById("stream-msg");
            if (existing) {
                existing.textContent = text;
                scrollBottom();
                return;
            }
            const div = document.createElement("div");
            div.id = "stream-msg";
            div.className = "msg peer";
            div.textContent = text;
            elements.messagesEl.appendChild(div);
            scrollBottom();
        }

        function finalizeStream() {
            if (structuredTranscriptRenderEnabled) {
                renderMessagesFromStructuredTranscript("");
                return;
            }
            const existing = document.getElementById("stream-msg");
            if (existing) {
                existing.removeAttribute("id");
            }
        }

        function setInputValue(text) {
            if (state.inputEl) {
                state.inputEl.value = String(text || "");
            }
        }

        return {
            elements,
            structuredTranscriptRenderEnabled,
            setStatus,
            renderSpeechStatus,
            renderSpeechLivePreview,
            renderApprovalQueue,
            renderDetachedNotices,
            renderAssistantIdentity,
            scrollBottom,
            renderMessagesFromStructuredTranscript,
            addMessage,
            clearMessages,
            addOrReplaceStream,
            finalizeStream,
            setInputValue,
            isSpeechPreviewRunId,
            hasSpeechFinalAuthority,
            isRecordingSpeechStage,
        };
    }

    window.BlazeClawChatControllerGui = {
        createGuiModule,
    };
})();
