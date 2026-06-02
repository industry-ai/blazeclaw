(function () {
    const state = {
        sessionKey: "main",
        connected: false,
        runId: null,
        streamText: "",
        attachments: [],
        assistantName: "Assistant",
        assistantAvatar: "A",
        assistantAgentId: null,
        detachedNotices: [],
        approvalQueue: [],
        approvalInFlightTokens: new Set(),
        needsApprovalWatchTimers: new Map(),
        toolTimelineByRequest: new Map(),
        bridgeQueue: [],
        pending: new Map(),
        seenBridgeSeq: new Set(),
        seenBridgeIds: new Set(),
        bridgeAvailable: !!(window.chrome && window.chrome.webview),
    };

    const NEEDS_APPROVAL_QUEUE_TIMEOUT_MS = 1500;

    const statusEl = document.getElementById("status");
    const speechStatusEl = document.getElementById("speechStatus");
    const assistantIdentityEl = document.getElementById("assistantIdentity");
    const approvalQueueEl = document.getElementById("approvalQueue");
    const messagesEl = document.getElementById("messages");
    const detachedNoticesEl = document.getElementById("detachedNotices");
    const speechLivePreviewEl = document.getElementById("speechLivePreview");
    const speechLivePreviewLabelEl = speechLivePreviewEl
        ? speechLivePreviewEl.querySelector(".speech-live-preview-label")
        : null;
    const speechLivePreviewTextEl = speechLivePreviewEl
        ? speechLivePreviewEl.querySelector(".speech-live-preview-text")
        : null;
    const structuredTranscriptRenderEnabled = resolveStructuredTranscriptRenderEnabled();
    state.inputEl = document.getElementById("input");
    state.sendBtn = document.getElementById("sendBtn");
    state.sendErrBtn = document.getElementById("sendErrBtn");
    state.attachBtn = document.getElementById("attachBtn");
    state.attachInput = document.getElementById("attachInput");
    state.speechTranscribeBtn = document.getElementById("speechTranscribeBtn");
    state.abortBtn = document.getElementById("abortBtn");
    state.sessionSelect = document.getElementById("sessionSelect");
    state.modelSelect = document.getElementById("modelSelect");
    state.thinkingSelect = document.getElementById("thinkingSelect");
    state.sessionSubscribeBtn = document.getElementById("sessionSubscribeBtn");
    state.sessionUnsubscribeBtn = document.getElementById("sessionUnsubscribeBtn");
    state.sessionCompactionRefreshBtn = document.getElementById("sessionCompactionRefreshBtn");
    state.sessionCompactionSelect = document.getElementById("sessionCompactionSelect");
    state.sessionCompactionBranchBtn = document.getElementById("sessionCompactionBranchBtn");
    state.sessionCompactionRestoreBtn = document.getElementById("sessionCompactionRestoreBtn");
    state.sessionControlStatusEl = document.getElementById("sessionControlStatus");
    state.slashMenuEl = document.getElementById("slashMenu");
    state.agentsControlPlaneEl = document.getElementById("agentsControlPlane");
    state.agentsTabsEl = document.getElementById("agentsTabs");
    state.agentsSurfaceEl = document.getElementById("agentsSurface");

    function setStatus(text) {
        statusEl.textContent = text;
    }

    function renderSpeechStatus() {
        if (!speechStatusEl) {
            return;
        }

        const capability = state.speechCapabilities && typeof state.speechCapabilities === "object"
            ? state.speechCapabilities
            : null;
        const sessionState = state.speechSessionState && typeof state.speechSessionState === "object"
            ? state.speechSessionState
            : null;

        if (!capability) {
            speechStatusEl.textContent = "speech: unavailable";
            return;
        }

        const parts = [];
        if (capability.loaded !== true) {
            parts.push("loading");
        } else {
            parts.push(capability.sttReady ? "stt ready" : (capability.sttSupported ? "stt unavailable" : "stt unsupported"));
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
            const capabilityEffectiveProvider = String(capability.effectiveExecutionProvider || "").trim();
            if (capabilityEffectiveProvider) {
                parts.push(`provider=${capabilityEffectiveProvider}`);
            }
            const capabilityCudaReason = String(capability.cudaExecutionProviderReason || "").trim();
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
                const suffix = (sessionState.segmentFinal || stageImpliesFinal) ? "final" : "interim";
                parts.push(`segment=${suffix}`);
            } else if (sessionState.text) {
                const textFinal =
                    stage === "segment_finalized" ||
                    stage === "stopped" ||
                    stage === "completed" ||
                    stage === "failed" ||
                    stage === "cancelled";
                parts.push(`segment=${textFinal ? "final" : "interim"}`);
            }
            if (sessionState.errorCode) {
                parts.push(`err=${String(sessionState.errorCode)}`);
            }
            const sessionEffectiveProvider = String(sessionState.effectiveExecutionProvider || "").trim();
            if (sessionEffectiveProvider && sessionEffectiveProvider !== String(capability && capability.effectiveExecutionProvider || "").trim()) {
                parts.push(`sessionProvider=${sessionEffectiveProvider}`);
            }
            const sessionCudaReason = String(sessionState.cudaExecutionProviderReason || "").trim();
            if (sessionCudaReason && sessionCudaReason !== "none") {
                parts.push(`sessionCuda=${sessionCudaReason}`);
            }
        }

        speechStatusEl.textContent = `speech: ${parts.join(" | ")}`;
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
        return normalizedStage === "recording" ||
            normalizedStage === "start_stream" ||
            normalizedStage === "streaming" ||
            ((normalizedStage === "queued" ||
                normalizedStage === "completed" ||
                normalizedStage === "segment_finalized") && previewRun);
    }

    function renderSpeechLivePreview() {
        if (!speechLivePreviewEl || !speechLivePreviewLabelEl || !speechLivePreviewTextEl) {
            return;
        }

        const sessionState = state.speechSessionState && typeof state.speechSessionState === "object"
            ? state.speechSessionState
            : null;
        if (!sessionState) {
            speechLivePreviewEl.hidden = true;
            speechLivePreviewEl.className = "speech-live-preview";
            speechLivePreviewLabelEl.textContent = "";
            speechLivePreviewTextEl.textContent = "";
            return;
        }

        const stage = String(sessionState.stage || "").trim();
        const text = String(sessionState.segmentText || sessionState.text || "").trim();
        const errorMessage = String(sessionState.errorMessage || "").trim();
        const capability = state.speechCapabilities && typeof state.speechCapabilities === "object"
            ? state.speechCapabilities
            : null;
        const previewDisabled = !capability || capability.streamingPreviewEnabled !== true;
        const liveStages = new Set(["recording", "start_stream", "streaming"]);
        const finalizingStages = new Set(["queued", "stopped", "transcribing"]);
        const finalStages = new Set(["segment_finalized", "completed"]);
        const failedStages = new Set(["failed", "cancelled"]);
        const shouldShow =
            liveStages.has(stage) ||
            finalizingStages.has(stage) ||
            finalStages.has(stage) ||
            (failedStages.has(stage) && (text || errorMessage));

        if (!shouldShow) {
            speechLivePreviewEl.hidden = true;
            speechLivePreviewEl.className = "speech-live-preview";
            speechLivePreviewLabelEl.textContent = "";
            speechLivePreviewTextEl.textContent = "";
            return;
        }

        let label = "Listening...";
        let modeClass = "listening";
        if (previewDisabled && (liveStages.has(stage) || finalizingStages.has(stage))) {
            label = "Live preview disabled";
            modeClass = finalizingStages.has(stage) ? "finalizing" : "disabled";
        } else if (stage === "streaming" && text) {
            label = "Recognizing stream ...";
            modeClass = sessionState.segmentFinal ? "final" : "interim";
        } else if ((stage === "segment_finalized" || stage === "completed") && isSpeechPreviewRunId(sessionState.runId)) {
            label = "Recognizing segment ...";
            modeClass = "interim";
        } else if (stage === "queued" || stage === "stopped" || stage === "transcribing") {
            label = "Finalizing...";
            modeClass = "finalizing";
        } else if (stage === "segment_finalized" || stage === "completed") {
            label = "Recognized";
            modeClass = "final";
        } else if (stage === "failed") {
            label = "Recognition failed";
            modeClass = "error";
        } else if (stage === "cancelled") {
            label = "Recognition cancelled";
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
            speechLivePreviewTextEl.textContent =
                (text || errorMessage || "No speech detected") + triageText;
            return;
        }

        speechLivePreviewEl.hidden = false;
        speechLivePreviewEl.className = `speech-live-preview ${modeClass}`;
        speechLivePreviewLabelEl.textContent = label;
        speechLivePreviewTextEl.textContent = previewDisabled && (liveStages.has(stage) || finalizingStages.has(stage))
            ? (text || errorMessage || "Preview is off; final transcription will run after stop.")
            : (text || errorMessage || "Speak now");
    }

    function renderApprovalQueue() {
        if (!approvalQueueEl) {
            return;
        }
        const queue = Array.isArray(state.approvalQueue) ? state.approvalQueue : [];
        if (!queue.length) {
            approvalQueueEl.hidden = true;
            approvalQueueEl.innerHTML = "";
            return;
        }
        approvalQueueEl.hidden = false;
        approvalQueueEl.innerHTML = "";
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
            meta.textContent = `token=${String(item.token || "")} ， status=${String(item.status || "pending")}`;
            card.appendChild(meta);

            if (String(item.status || "pending") === "pending") {
                const actions = document.createElement("div");
                actions.className = "actions";
                const approveBtn = document.createElement("button");
                approveBtn.textContent = "Approve";
                approveBtn.disabled = Boolean(item.busy);
                approveBtn.addEventListener("click", function () {
                    void resolveApprovalToken(item.token, true);
                });
                const denyBtn = document.createElement("button");
                denyBtn.textContent = "Deny";
                denyBtn.disabled = Boolean(item.busy);
                denyBtn.addEventListener("click", function () {
                    void resolveApprovalToken(item.token, false);
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
                if (item.missingDependency || item.remediation || item.installHint || item.configHint || (item.readinessKnown && !item.readinessReady)) {
                    const guide = document.createElement("div");
                    guide.className = "meta";
                    const missingParts = [];
                    if (item.missingDependency) {
                        missingParts.push(String(item.missingDependency));
                    }
                    if (item.readinessKnown && !item.readinessReady && item.readinessMissingDependency) {
                        const readinessMissing = String(item.readinessMissingDependency);
                        if (!missingParts.includes(readinessMissing)) {
                            missingParts.push(readinessMissing);
                        }
                    }
                    const missingSummary = missingParts.length
                        ? `missing=${missingParts.join("|")} ， `
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
                        ? ` precheck: ${item.readinessCode ? `code=${String(item.readinessCode)} ， ` : ""}${item.readinessMessage ? String(item.readinessMessage) : "Email backend is not ready."}`
                        : "";
                    guide.textContent = `[backend stack incomplete] ${missingSummary}${remediation}${executionDetails}${precheckDetails}`;
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
                    void resolveApprovalToken(item.token, true);
                });
                retryActions.appendChild(retryApproveBtn);
                card.appendChild(retryActions);
            }

            approvalQueueEl.appendChild(card);
        }
        approvalQueueEl.scrollTop = approvalQueueEl.scrollHeight;
    }

    function isLikelyApprovalToken(token) {
        const normalizedToken = String(token || "").trim();
        if (!normalizedToken) {
            return false;
        }
        if (!/^[A-Za-z0-9:_\-]+$/.test(normalizedToken)) {
            return false;
        }
        if (/^email-approval-\d{10,}-\d+$/.test(normalizedToken)) {
            return true;
        }
        return normalizedToken.length >= 24;
    }

    function hasApprovalQueueToken(token) {
        const normalizedToken = String(token || "").trim();
        if (!normalizedToken || !Array.isArray(state.approvalQueue)) {
            return false;
        }

        return state.approvalQueue.some(function (item) {
            return String(item && item.token || "").trim() === normalizedToken;
        });
    }

    function scheduleNeedsApprovalQueueWatch(input) {
        const source = input && typeof input === "object"
            ? input
            : {};
        const token = String(source.approvalToken || "").trim();
        if (!token) {
            return;
        }

        if (!(state.needsApprovalWatchTimers instanceof Map)) {
            state.needsApprovalWatchTimers = new Map();
        }

        const existingTimer = state.needsApprovalWatchTimers.get(token);
        if (existingTimer) {
            window.clearTimeout(existingTimer);
        }

        const timer = window.setTimeout(function () {
            state.needsApprovalWatchTimers.delete(token);
            if (hasApprovalQueueToken(token)) {
                return;
            }

            const payload = {
                token,
                runId: String(source.runId || ""),
                sessionKey: String(source.sessionKey || state.sessionKey || ""),
                timeoutMs: NEEDS_APPROVAL_QUEUE_TIMEOUT_MS,
            };
            emitAgentsTelemetry("approval.queue_missing_after_needs_approval", payload);
            try {
                console.warn("[approval-watchdog] queue item missing after needs_approval", payload);
            } catch (_) {
            }
        }, NEEDS_APPROVAL_QUEUE_TIMEOUT_MS);

        state.needsApprovalWatchTimers.set(token, timer);
    }

    function upsertApprovalToken(token, title) {
        const normalizedToken = String(token || "").trim();
        if (!normalizedToken || !isLikelyApprovalToken(normalizedToken)) {
            return;
        }
        if (!Array.isArray(state.approvalQueue)) {
            state.approvalQueue = [];
        }
        state.approvalQueue = state.approvalQueue.filter(function (item) {
            const existingToken = String(item && item.token || "").trim();
            if (!existingToken || existingToken === normalizedToken) {
                return true;
            }
            // Drop stale streamed fragments once a longer canonical token appears.
            if (String(item.status || "pending") === "pending" &&
                normalizedToken.startsWith(existingToken) &&
                existingToken.length < normalizedToken.length) {
                return false;
            }
            return true;
        });
        const existing = state.approvalQueue.find((item) => item.token === normalizedToken);
        if (existing) {
            if (title && !existing.title) {
                existing.title = title;
            }
            renderApprovalQueue();
            return;
        }
        state.approvalQueue.push({
            token: normalizedToken,
            title: String(title || "Email scheduling approval required"),
            status: "pending",
            ts: Date.now(),
        });
        if (state.approvalQueue.length > 20) {
            state.approvalQueue.splice(0, state.approvalQueue.length - 20);
        }
        renderApprovalQueue();
    }

    function scanApprovalTokenFromText(text) {
        const raw = String(text || "");
        if (!raw) {
            return;
        }
        const parsed = controller && typeof controller.parseApprovalTokenFromText === "function"
            ? controller.parseApprovalTokenFromText(raw)
            : null;
        if (parsed && parsed.approvalToken) {
            upsertApprovalToken(parsed.approvalToken, "Email scheduling approval required");
            return;
        }

        // UI fallback parser: keep queue functional even if controller parsing
        // misses localized punctuation/spacing edge cases.
        const fallbackMatch = /approvalToken=([A-Za-z0-9:_\-]+)/.exec(raw);
        if (fallbackMatch && fallbackMatch[1]) {
            upsertApprovalToken(fallbackMatch[1], "Email scheduling approval required");
        }
    }

    function harvestApprovalTokensFromText(text) {
        const raw = String(text || "");
        if (!raw) {
            return;
        }
        const matches = raw.match(/approvalToken=([A-Za-z0-9:_\-]+)/g) || [];
        for (const match of matches) {
            const token = String(match || "").replace(/^approvalToken=/, "").trim();
            if (token) {
                upsertApprovalToken(token, "Email scheduling approval required");
            }
        }
    }

    async function resolveApprovalToken(token, approve) {
        const normalizedToken = String(token || "").trim();
        if (!normalizedToken) {
            return;
        }
        const queue = Array.isArray(state.approvalQueue) ? state.approvalQueue : [];
        const item = queue.find((entry) => entry.token === normalizedToken);
        if (!item) {
            return;
        }
        if (item.busy || item.status !== "pending") {
            return;
        }
        if (!isLikelyApprovalToken(normalizedToken)) {
            item.status = "denied";
            item.title = "Approval token invalid";
            renderApprovalQueue();
            return;
        }
        if (!state.approvalInFlightTokens) {
            state.approvalInFlightTokens = new Set();
        }
        if (state.approvalInFlightTokens.has(normalizedToken)) {
            return;
        }
        state.approvalInFlightTokens.add(normalizedToken);
        item.busy = true;
        renderApprovalQueue();

        try {
            const result = controller && typeof controller.executeExecApprovalAction === "function"
                ? await controller.executeExecApprovalAction(normalizedToken, approve)
                : { ok: false, status: "invalid" };
            item.status = result.ok
                ? (approve ? "approved" : "denied")
                : (approve ? "failed" : "denied");
            if (result.status === "expired") {
                item.status = "failed";
                item.title = "Approval token expired";
            } else {
                const errorCode = String(result && result.errorCode || "").trim();
                item.title = result.ok
                    ? (approve ? "Email approval executed" : "Email approval denied")
                    : (approve
                        ? (errorCode ? `Email approval failed (${errorCode})` : "Email approval failed")
                        : "Email approval denied");
            }
            item.errorMessage = String(result && result.errorMessage || "").trim();
            item.remediation = String(result && result.remediation || "").trim();
            item.missingDependency = String(result && result.missingDependency || "").trim();
            item.installHint = String(result && result.installHint || "").trim();
            item.configHint = String(result && result.configHint || "").trim();
            item.failureBucket = String(result && result.failureBucket || "").trim();
            item.readinessKnown = Boolean(result && result.readinessKnown);
            item.readinessReady = Boolean(result && result.readinessReady);
            item.readinessCode = String(result && result.readinessCode || "").trim();
            item.readinessMessage = String(result && result.readinessMessage || "").trim();
            item.readinessRemediation = String(result && result.readinessRemediation || "").trim();
            item.readinessMissingDependency = String(result && result.readinessMissingDependency || "").trim();
        } catch (error) {
            item.status = "denied";
            item.title = `Approval error: ${String(error)}`;
        }
        item.busy = false;
        state.approvalInFlightTokens.delete(normalizedToken);
        renderApprovalQueue();
    }

    function renderDetachedNotices() {
        if (!detachedNoticesEl) {
            return;
        }

        const notices = Array.isArray(state.detachedNotices)
            ? state.detachedNotices
            : [];
        if (!notices.length) {
            detachedNoticesEl.hidden = true;
            detachedNoticesEl.innerHTML = "";
            return;
        }

        detachedNoticesEl.hidden = false;
        detachedNoticesEl.innerHTML = "";
        for (const notice of notices) {
            const row = document.createElement("div");
            const kind = String(notice && notice.kind || "sent");
            row.className = `detached-notice ${kind}`;
            row.textContent = String(notice && notice.text || "");
            detachedNoticesEl.appendChild(row);
        }
        detachedNoticesEl.scrollTop = detachedNoticesEl.scrollHeight;
    }

    function appendDetachedNotice(input) {
        const source = input && typeof input === "object"
            ? input
            : {};
        const kind = String(source.kind || "sent").trim().toLowerCase();
        const text = String(source.text || "").trim();
        if (!text) {
            return;
        }

        if (!Array.isArray(state.detachedNotices)) {
            state.detachedNotices = [];
        }
        const prefix = kind === "queued" ? "[queued] " : "[sent] ";
        state.detachedNotices.push({
            kind: kind === "queued" ? "queued" : "sent",
            text: `${prefix}${text}`,
            ts: Date.now(),
        });
        if (state.detachedNotices.length > 20) {
            state.detachedNotices.splice(0, state.detachedNotices.length - 20);
        }
        renderDetachedNotices();
    }

    function renderAssistantIdentity() {
        if (!assistantIdentityEl) {
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
        assistantIdentityEl.textContent = `${assistantAvatar} ${assistantName}${suffix}`;
    }

    function scrollBottom() {
        messagesEl.scrollTop = messagesEl.scrollHeight;
    }

    function renderMessagesFromStructuredTranscript(streamTextOverride) {
        if (!structuredTranscriptRenderEnabled || !controller) {
            return;
        }

        const transcript = typeof controller.getStructuredTranscript === "function"
            ? controller.getStructuredTranscript()
            : [];
        const rows = Array.isArray(transcript) ? transcript : [];
        const streamText = String(streamTextOverride || "").trim();

        messagesEl.innerHTML = "";
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
            messagesEl.appendChild(div);
            if (kind === "peer") {
                scanApprovalTokenFromText(text);
                harvestApprovalTokensFromText(text);
            }
        }

        if (streamText && !controller.isSilentReplyText(streamText)) {
            const streamDiv = document.createElement("div");
            streamDiv.id = "stream-msg";
            streamDiv.className = "msg peer";
            streamDiv.textContent = streamText;
            messagesEl.appendChild(streamDiv);
        }

        scrollBottom();
    }

    function addMessage(text, kind) {
        if (structuredTranscriptRenderEnabled) {
            const stream = String(state.streamText || "").trim();
            renderMessagesFromStructuredTranscript(stream);
            return;
        }
        const div = document.createElement("div");
        div.className = `msg ${kind}`;
        div.textContent = text;
        messagesEl.appendChild(div);
        if (kind === "peer") {
            scanApprovalTokenFromText(text);
            harvestApprovalTokensFromText(text);
        }
        scrollBottom();
    }

    function clearMessages() {
        if (structuredTranscriptRenderEnabled) {
            renderMessagesFromStructuredTranscript("");
            return;
        }
        messagesEl.innerHTML = "";
    }

    function escapeHtml(value) {
        return String(value || "")
            .replace(/&/g, "&amp;")
            .replace(/</g, "&lt;")
            .replace(/>/g, "&gt;")
            .replace(/"/g, "&quot;")
            .replace(/'/g, "&#39;");
    }

    function setInputValue(text) {
        if (state.inputEl) {
            state.inputEl.value = String(text || "");
        }
    }

    function renderSessionControls() {
        const compactionItems = Array.isArray(state.sessionCompactionItems)
            ? state.sessionCompactionItems
            : [];
        const selectedCompaction = String(state.sessionCompactionSelection || "");
        if (state.sessionCompactionSelect) {
            state.sessionCompactionSelect.innerHTML = "";
            if (!compactionItems.length) {
                const optionEl = document.createElement("option");
                optionEl.value = "";
                optionEl.textContent = "no compactions";
                state.sessionCompactionSelect.appendChild(optionEl);
            } else {
                for (const item of compactionItems) {
                    const optionEl = document.createElement("option");
                    optionEl.value = String(item.id || "");
                    optionEl.textContent = String(item.label || item.id || "");
                    if (optionEl.value === selectedCompaction) {
                        optionEl.selected = true;
                    }
                    state.sessionCompactionSelect.appendChild(optionEl);
                }
            }
        }

        if (state.sessionControlStatusEl) {
            const subscription = state.sessionSubscribed
                ? "subscription: active"
                : "subscription: inactive";
            const status = String(state.sessionCompactionStatus || "").trim();
            state.sessionControlStatusEl.textContent = status
                ? `${subscription} ！ ${status}`
                : subscription;
        }
    }

    function ensureToolTimelineCard(requestId) {
        if (!requestId) {
            return null;
        }

        let card = document.getElementById(`tool-card-${requestId}`);
        if (card) {
            return card;
        }

        card = document.createElement("div");
        card.id = `tool-card-${requestId}`;
        card.className = "tool-timeline";

        const title = document.createElement("div");
        title.className = "title";
        title.textContent = `Tool timeline (${requestId})`;
        card.appendChild(title);

        const body = document.createElement("div");
        body.className = "tool-body";
        card.appendChild(body);

        messagesEl.appendChild(card);
        scrollBottom();
        return card;
    }

    function appendToolLifecycleRow(payload) {
        if (!payload || typeof payload !== "object") {
            return;
        }

        const requestId = String(payload.id || "");
        const card = ensureToolTimelineCard(requestId);
        if (!card) {
            return;
        }

        const body = card.querySelector(".tool-body");
        if (!body) {
            return;
        }

        const phase = String(payload.phase || "result");
        const tool = String(payload.tool || "unknown");
        const status = String(payload.status || "");
        const errMsg = typeof payload.errorMessage === "string" ? payload.errorMessage.trim() : "";
        const outputRaw = typeof payload.output === "string" ? payload.output : "";
        let nestedCode = "";
        if (outputRaw) {
            try {
                const parsedOutput = JSON.parse(outputRaw);
                if (parsedOutput && parsedOutput.error && typeof parsedOutput.error.code === "string") {
                    nestedCode = String(parsedOutput.error.code || "").trim();
                }
            } catch (_) {
            }
        }
        const topLevelCode = String(payload.code || "").trim();
        let code = nestedCode || topLevelCode;
        if (topLevelCode && topLevelCode !== "legacy_execution_failed") {
            code = topLevelCode;
        }

        const row = document.createElement("div");
        row.className = "tool-row";

        const phaseEl = document.createElement("span");
        phaseEl.className = "tool-phase";
        if (phase === "error") {
            phaseEl.classList.add("error");
        }
        if (phase === "approval-needed" || phase === "approval-resolved") {
            phaseEl.classList.add("approval");
        }
        phaseEl.textContent = phase;
        row.appendChild(phaseEl);

        const detailEl = document.createElement("span");
        let detailText = `tool=${tool}`;
        if (status) {
            detailText += ` status=${status}`;
        }
        if (code) {
            detailText += ` code=${code}`;
        }
        if (errMsg) {
            const max = 280;
            detailText += ` errorMessage=${errMsg.length > max ? errMsg.slice(0, max) + "..." : errMsg}`;
        }
        detailEl.textContent = detailText;
        row.appendChild(detailEl);

        const copyBtn = document.createElement("button");
        copyBtn.type = "button";
        copyBtn.className = "tool-copy-json";
        copyBtn.textContent = "Copy JSON";
        copyBtn.title = "Copy full tools.lifecycle payload as JSON";
        copyBtn.addEventListener("click", () => {
            try {
                const text = JSON.stringify(payload);
                if (navigator.clipboard && navigator.clipboard.writeText) {
                    void navigator.clipboard.writeText(text);
                } else {
                    const ta = document.createElement("textarea");
                    ta.value = text;
                    document.body.appendChild(ta);
                    ta.select();
                    document.execCommand("copy");
                    ta.remove();
                }
            } catch (_) {
                /* ignore */
            }
        });
        row.appendChild(copyBtn);

        body.appendChild(row);
        scrollBottom();

        state.toolTimelineByRequest.set(requestId, true);
    }

    function addOrReplaceStream(text) {
        if (!text || controller.isSilentReplyText(text)) {
            return;
        }
        scanApprovalTokenFromText(text);
        harvestApprovalTokensFromText(text);

        if (structuredTranscriptRenderEnabled) {
            renderMessagesFromStructuredTranscript(text);
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
        messagesEl.appendChild(div);
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

    function renderAgentsSurface() {
        if (!state.agentsSurfaceEl) {
            return;
        }

        if (!agentsController) {
            state.agentsSurfaceEl.innerHTML = "";
            return;
        }

        const selectedAgent = String(state.agentsSelectedId || "").trim();
        const panel = String(state.agentsPanel || "overview");
        const rows = [];

        rows.push(`<div class="agents-surface-title">Agents control-plane ??${panel}</div>`);

        if (state.agentsLoading) {
            rows.push("<div class=\"agents-row\">Loading agents...</div>");
        }
        if (state.agentsError) {
            rows.push(`<div class=\"agents-row error\">${state.agentsError}</div>`);
        }

        if (state.agentsList && Array.isArray(state.agentsList.agents) && state.agentsList.agents.length > 0) {
            const chips = state.agentsList.agents.map((entry) => {
                const id = String((entry && entry.id) || "");
                const active = selectedAgent && selectedAgent === id ? " active" : "";
                const label = id || "(unknown)";
                return `<button class=\"agents-chip${active}\" data-agent-id=\"${label.replace(/\"/g, "&quot;")}\">${label}</button>`;
            }).join("");
            rows.push(`<div class=\"agents-list\">${chips}</div>`);
        }

        if (panel === "overview") {
            const count = state.agentsList && Array.isArray(state.agentsList.agents)
                ? state.agentsList.agents.length
                : 0;
            const identity = selectedAgent && state.agentIdentityById
                ? state.agentIdentityById[selectedAgent] || null
                : null;
            const identityDisplayName = identity
                ? String(identity.displayName || identity.name || "").trim()
                : "";
            const identityRole = identity
                ? String(identity.role || identity.description || "").trim()
                : "";
            const identityStyle = identity
                ? String(identity.style || identity.tone || "").trim()
                : "";

            rows.push(`<div class=\"agents-row\">Selected agent: ${selectedAgent || "(none)"}</div>`);
            rows.push(`<div class=\"agents-row\">Agent count: ${count}</div>`);
            rows.push(`<div class=\"agents-row\">Session key: ${String(state.sessionKey || "")}</div>`);

            if (state.agentIdentityLoading) {
                rows.push("<div class=\"agents-row\">Loading agent identity...</div>");
            }
            if (state.agentIdentityError) {
                rows.push(`<div class=\"agents-row error\">${state.agentIdentityError}</div>`);
            }

            rows.push(`<div class=\"agents-row\">Identity cached: ${identity ? "yes" : "no"}</div>`);
            if (identity) {
                rows.push(`<div class=\"agents-row\">Identity name: ${identityDisplayName || "(n/a)"}</div>`);
                rows.push(`<div class=\"agents-row\">Identity role: ${identityRole || "(n/a)"}</div>`);
                rows.push(`<div class=\"agents-row\">Identity style: ${identityStyle || "(n/a)"}</div>`);
            }
        }

        if (panel === "tools") {
            if (state.toolsCatalogLoading || state.toolsEffectiveLoading) {
                rows.push("<div class=\"agents-row\">Loading tools surface...</div>");
            }
            if (state.toolsCatalogError) {
                rows.push(`<div class=\"agents-row error\">${state.toolsCatalogError}</div>`);
            }
            if (state.toolsEffectiveError) {
                rows.push(`<div class=\"agents-row error\">${state.toolsEffectiveError}</div>`);
            }

            const catalogTools = state.toolsCatalogResult && Array.isArray(state.toolsCatalogResult.tools)
                ? state.toolsCatalogResult.tools.length
                : 0;
            const effectiveTools = state.toolsEffectiveResult && Array.isArray(state.toolsEffectiveResult.tools)
                ? state.toolsEffectiveResult.tools.length
                : 0;

            rows.push(`<div class=\"agents-row\">Catalog tools: ${catalogTools}</div>`);
            rows.push(`<div class=\"agents-row\">Effective tools: ${effectiveTools}</div>`);
            rows.push(`<div class=\"agents-row\">Effective key: ${String(state.toolsEffectiveResultKey || "(none)")}</div>`);
        }

        if (panel === "files") {
            if (state.agentFilesLoading) {
                rows.push("<div class=\"agents-row\">Loading agent files...</div>");
            }
            if (state.agentFilesError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentFilesError)}</div>`);
            }

            const files = state.agentFilesResult && Array.isArray(state.agentFilesResult.files)
                ? state.agentFilesResult.files
                : [];
            rows.push(`<div class=\"agents-row\">Files count: ${files.length}</div>`);
            if (files.length > 0) {
                const selectedPath = String(state.agentFileSelectedPath || "").trim();
                const chips = files.slice(0, 12).map((entry) => {
                    const path = String(entry && (entry.path || entry.name) || "").trim();
                    if (!path) {
                        return "";
                    }
                    const active = selectedPath && selectedPath === path;
                    return `<button class=\"agents-chip\" data-agent-file-path=\"${escapeHtml(path)}\"${active ? " style=\"font-weight:700;\"" : ""}>${escapeHtml(path)}</button>`;
                }).filter(Boolean).join(" ");
                rows.push(`<div class=\"agents-row\">${chips}</div>`);
            }
            if (state.agentFileContentLoading) {
                rows.push("<div class=\"agents-row\">Loading file content...</div>");
            }
            if (state.agentFileContentError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentFileContentError)}</div>`);
            }
            if (state.agentFileSaveError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentFileSaveError)}</div>`);
            }
            if (state.agentFileSaveStatus) {
                rows.push(`<div class=\"agents-row\">Save status: ${escapeHtml(state.agentFileSaveStatus)}</div>`);
            }

            const contentResult = state.agentFileContentResult && typeof state.agentFileContentResult === "object"
                ? state.agentFileContentResult
                : null;
            const contentFile = contentResult && contentResult.file && typeof contentResult.file === "object"
                ? contentResult.file
                : null;
            const selectedPath = String(state.agentFileSelectedPath || contentFile && (contentFile.path || contentFile.name) || "").trim();
            const draft = String(state.agentFileEditDraft || "");
            const baseContent = String(state.agentFileEditBaseContent || "");
            const dirty = selectedPath.length > 0 && draft !== baseContent;

            rows.push("<div class=\"agents-row\">"
                + `<button class=\"agents-chip\" data-files-action=\"refresh\">Refresh</button> `
                + `<button class=\"agents-chip\" data-files-action=\"reload\"${selectedPath ? "" : " disabled"}${state.agentFileContentLoading ? " disabled" : ""}>Reload</button> `
                + `<button class=\"agents-chip\" data-files-action=\"save\"${selectedPath && dirty ? "" : " disabled"}${state.agentFileSaveBusy ? " disabled" : ""}>Save</button>`
                + "</div>");
            rows.push(`<div class=\"agents-row\">Selected file: ${escapeHtml(selectedPath || "(none)")}</div>`);
            rows.push("<div class=\"agents-row\">"
                + `<textarea data-files-field=\"content\" rows=\"8\" style=\"width:100%;\"${selectedPath ? "" : " disabled"}>${escapeHtml(draft)}</textarea>`
                + "</div>");
            if (contentFile && Object.prototype.hasOwnProperty.call(contentFile, "updatedAt")) {
                rows.push(`<div class=\"agents-row\">Updated at: ${escapeHtml(String(contentFile.updatedAt || "(unknown)"))}</div>`);
            }
        }

        if (panel === "skills") {
            if (state.agentSkillsLoading) {
                rows.push("<div class=\"agents-row\">Loading skills...</div>");
            }
            if (state.agentSkillsError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentSkillsError)}</div>`);
            }

            const skillsResult = state.agentSkillsResult || null;
            const report = state.agentSkillsReport || (skillsResult && skillsResult.report) || null;
            const commands = skillsResult && Array.isArray(skillsResult.commands)
                ? skillsResult.commands
                : [];
            const reportSkills = report && Array.isArray(report.skills)
                ? report.skills
                : [];

            rows.push(`<div class=\"agents-row\">Agent-scoped endpoint: ${skillsResult && skillsResult.agentScoped ? "yes" : "no (capability-gated)"}</div>`);
            rows.push(`<div class=\"agents-row\">Skills report agent: ${String(state.agentSkillsAgentId || "(none)")}</div>`);
            rows.push(`<div class=\"agents-row\">Skills report entries: ${reportSkills.length}</div>`);
            rows.push(`<div class=\"agents-row\">Skill commands (derived): ${commands.length}</div>`);
            rows.push("<div class=\"agents-row\">Hub/Install/Edit baseline: skills.search + skills.detail/gateway.skills.info + gateway.skills.install.execute + skills.update</div>");

            const hubQuery = String(state.skillsHubQuery || "");
            const hubResults = Array.isArray(state.skillsHubResults)
                ? state.skillsHubResults
                : [];
            const selectedSkill = state.skillsDetailResult && typeof state.skillsDetailResult === "object"
                ? String(state.skillsDetailResult.skill || state.skillsDetailResult.skillKey || "").trim()
                : "";
            rows.push("<div class=\"agents-row\">"
                + `<input type=\"text\" data-skills-field=\"query\" placeholder=\"Search skills (optional query)\" value=\"${escapeHtml(hubQuery)}\" style=\"min-width:260px;\"/> `
                + `<button class=\"agents-chip\" data-skills-action=\"search\"${state.skillsHubLoading ? " disabled" : ""}>Search</button> `
                + `<button class=\"agents-chip\" data-skills-action=\"refresh\">Refresh Status</button>`
                + "</div>");
            if (state.skillsHubLoading) {
                rows.push("<div class=\"agents-row\">Searching skills hub...</div>");
            }
            if (state.skillsHubError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.skillsHubError)}</div>`);
            }

            rows.push(`<div class=\"agents-row\">Hub matches: ${hubResults.length}</div>`);
            if (hubResults.length > 0) {
                const preview = hubResults.slice(0, 8).map((entry) => {
                    const skill = String(entry && entry.skill || entry && entry.name || "").trim();
                    const label = String(entry && entry.name || skill || "(unnamed)");
                    const description = String(entry && entry.description || "").trim();
                    return `<button class=\"agents-chip\" data-skills-action=\"detail\" data-skill-key=\"${escapeHtml(skill)}\" title=\"${escapeHtml(description)}\">${escapeHtml(label)}</button>`;
                }).join(" ");
                rows.push(`<div class=\"agents-row\">${preview}</div>`);
            }

            rows.push("<div class=\"agents-row\">"
                + `<button class=\"agents-chip\" data-skills-action=\"install\" data-skill-key=\"${escapeHtml(selectedSkill)}\"${selectedSkill ? "" : " disabled"}${state.skillsInstallBusy ? " disabled" : ""}>Install Selected</button> `
                + `<button class=\"agents-chip\" data-skills-action=\"edit\"${state.skillsEditBusy ? " disabled" : ""}>Apply JSON Update</button>`
                + "</div>");
            rows.push("<div class=\"agents-row\">"
                + `<textarea data-skills-field=\"editPayload\" rows=\"5\" style=\"width:100%;\" placeholder='{\"skill\":\"example\",\"enabled\":true}'>${escapeHtml(String(state.skillsEditPayload || "{}"))}</textarea>`
                + "</div>");
            if (state.skillsDetailLoading) {
                rows.push("<div class=\"agents-row\">Loading skill detail...</div>");
            }
            if (state.skillsDetailError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.skillsDetailError)}</div>`);
            }
            if (state.skillsDetailResult) {
                rows.push(`<div class=\"agents-row\"><pre style=\"white-space:pre-wrap;margin:0;\">${escapeHtml(JSON.stringify(state.skillsDetailResult, null, 2))}</pre></div>`);
            }
            if (state.skillsInstallStatus) {
                rows.push(`<div class=\"agents-row\"><pre style=\"white-space:pre-wrap;margin:0;\">${escapeHtml(String(state.skillsInstallStatus))}</pre></div>`);
            }
            if (state.skillsEditError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.skillsEditError)}</div>`);
            }
            if (state.skillsEditStatus) {
                rows.push(`<div class=\"agents-row\"><pre style=\"white-space:pre-wrap;margin:0;\">${escapeHtml(String(state.skillsEditStatus))}</pre></div>`);
            }
        }

        if (panel === "channels") {
            if (state.agentChannelsLoading) {
                rows.push("<div class=\"agents-row\">Loading channels...</div>");
            }
            if (state.agentChannelsError) {
                rows.push(`<div class=\"agents-row error\">${state.agentChannelsError}</div>`);
            }

            const channelResult = state.agentChannelsResult || null;
            const snapshot = channelResult && channelResult.snapshot
                ? channelResult.snapshot
                : null;
            const channels = channelResult && Array.isArray(channelResult.channels)
                ? channelResult.channels
                : [];
            const routes = channelResult && Array.isArray(channelResult.routes)
                ? channelResult.routes
                : [];
            const whatsappMessage = typeof state.whatsappLoginMessage === "string"
                ? state.whatsappLoginMessage
                : "(none)";
            const whatsappConnected = typeof state.whatsappLoginConnected === "boolean"
                ? (state.whatsappLoginConnected ? "yes" : "no")
                : "unknown";
            const whatsappQrDataUrl = typeof state.whatsappLoginQrDataUrl === "string"
                ? state.whatsappLoginQrDataUrl
                : "";

            rows.push(`<div class=\"agents-row\">Channels: ${channels.length}</div>`);
            rows.push(`<div class=\"agents-row\">Agent-related routes: ${routes.length}</div>`);
            rows.push(`<div class=\"agents-row\">Snapshot loaded: ${snapshot ? "yes" : "no"}</div>`);
            rows.push(`<div class=\"agents-row\">Last success: ${typeof state.channelsLastSuccess === "number" ? state.channelsLastSuccess : "(none)"}</div>`);
            rows.push(`<div class=\"agents-row\">Scope-aware status loader: ${state.agentChannelsCapability && state.agentChannelsCapability.method ? state.agentChannelsCapability.method : "channels.status"}</div>`);
            rows.push(`<div class=\"agents-row\">WhatsApp busy: ${state.whatsappBusy ? "yes" : "no"}</div>`);
            rows.push(`<div class=\"agents-row\">WhatsApp connected: ${whatsappConnected}</div>`);
            rows.push(`<div class=\"agents-row\">WhatsApp message: ${whatsappMessage}</div>`);
            if (whatsappQrDataUrl) {
                rows.push(`<div class=\"agents-row\">WhatsApp QR: <a href=\"${whatsappQrDataUrl}\" target=\"_blank\" rel=\"noopener noreferrer\">open</a></div>`);
            }

            const buttonsDisabled = state.whatsappBusy ? " disabled" : "";
            rows.push(`<div class=\"agents-row\">`
                + `<button class=\"agents-chip\" data-channels-action=\"refresh\">Refresh</button> `
                + `<button class=\"agents-chip\" data-channels-action=\"start\"${buttonsDisabled}>Start Login</button> `
                + `<button class=\"agents-chip\" data-channels-action=\"start-force\"${buttonsDisabled}>Force Start</button> `
                + `<button class=\"agents-chip\" data-channels-action=\"wait\"${buttonsDisabled}>Wait Login</button> `
                + `<button class=\"agents-chip\" data-channels-action=\"logout\"${buttonsDisabled}>Logout</button>`
                + `</div>`);

            if (channels.length > 0) {
                const firstChannel = channels[0] || {};
                const firstChannelAccounts = Array.isArray(firstChannel.accounts)
                    ? firstChannel.accounts
                    : [];
                rows.push(`<div class=\"agents-row\">First channel: ${String(firstChannel.label || firstChannel.id || "(n/a)")}</div>`);
                rows.push(`<div class=\"agents-row\">First channel accounts: ${firstChannelAccounts.length}</div>`);
                rows.push(`<div class=\"agents-row\">First channel default account: ${String(firstChannel.defaultAccountId || "(n/a)")}</div>`);
            }
            rows.push("<div class=\"agents-row\">Capability mode: canonical channels snapshot + shared route projection</div>");
            rows.push("<div class=\"agents-row\">Parity track: docs/compare/channels.ts/CHANNELS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md</div>");
        }

        if (panel === "cron") {
            if (state.agentCronLoading) {
                rows.push("<div class=\"agents-row\">Loading cron surface...</div>");
            }
            if (state.agentCronError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentCronError)}</div>`);
            }
            if (state.agentCronStatusError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentCronStatusError)}</div>`);
            }
            if (state.agentCronJobsError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentCronJobsError)}</div>`);
            }
            if (state.agentCronRunsError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentCronRunsError)}</div>`);
            }

            const cronStatus = state.agentCronStatusResult || null;
            const jobs = Array.isArray(state.agentCronJobs)
                ? state.agentCronJobs
                : [];
            const runs = Array.isArray(state.agentCronRuns)
                ? state.agentCronRuns
                : [];
            const selectedJobId = typeof state.agentCronSelectedJobId === "string"
                ? state.agentCronSelectedJobId
                : "";
            const selectedJob = jobs.find((entry) => {
                return String(entry && entry.id || "").trim() === selectedJobId;
            }) || null;
            const form = state.agentCronForm || {};
            const fieldErrors = state.agentCronFieldErrors || {};
            const editingLabel = state.agentCronEditingJobId
                ? `Editing job ${escapeHtml(state.agentCronEditingJobId)}`
                : "Creating new job";
            const cronBusy = state.agentCronBusy ? "yes" : "no";
            const modelSuggestions = Array.isArray(state.agentCronModelSuggestions)
                ? state.agentCronModelSuggestions
                : [];

            rows.push(`<div class=\"agents-row\">Cron enabled: ${cronStatus ? (cronStatus.enabled ? "yes" : "no") : "unknown"}</div>`);
            rows.push(`<div class=\"agents-row\">Cron jobs total: ${Number.isFinite(Number(state.agentCronJobsTotal)) ? state.agentCronJobsTotal : jobs.length}</div>`);
            rows.push(`<div class=\"agents-row\">Cron next wake: ${cronStatus && Number.isFinite(Number(cronStatus.nextWakeAtMs)) ? cronStatus.nextWakeAtMs : "(none)"}</div>`);
            rows.push(`<div class=\"agents-row\">Cron busy: ${cronBusy}</div>`);

            rows.push(`<div class=\"agents-row\">Jobs loaded: ${jobs.length}</div>`);
            rows.push(`<div class=\"agents-row\">Jobs has more: ${state.agentCronJobsHasMore ? "yes" : "no"}</div>`);
            rows.push(`<div class=\"agents-row\">Jobs filter: enabled=${state.agentCronJobsEnabledFilter}, sort=${state.agentCronJobsSortBy}/${state.agentCronJobsSortDir}</div>`);
            rows.push(`<div class=\"agents-row\">Runs loaded: ${runs.length}</div>`);
            rows.push(`<div class=\"agents-row\">Runs has more: ${state.agentCronRunsHasMore ? "yes" : "no"}</div>`);
            rows.push(`<div class=\"agents-row\">Runs filter: scope=${state.agentCronRunsScope}, status=${state.agentCronRunsStatusFilter}, sort=${state.agentCronRunsSortDir}</div>`);
            rows.push(`<div class=\"agents-row\">Selected cron job: ${selectedJobId || "(none)"}</div>`);
            rows.push(`<div class=\"agents-row\">Model suggestions: ${modelSuggestions.length}</div>`);

            const jobItems = jobs.slice(0, 8).map((job) => {
                const jobId = String(job && job.id || "").trim();
                const active = jobId === selectedJobId ? " active" : "";
                const label = String(job && job.name || jobId || "(unnamed)");
                const scheduleKind = String(job && job.schedule && job.schedule.kind || "n/a");
                return `<button class=\"agents-chip${active}\" data-cron-job-id=\"${escapeHtml(jobId)}\" title=\"schedule=${escapeHtml(scheduleKind)}\">${escapeHtml(label)}</button>`;
            });
            if (jobItems.length > 0) {
                rows.push(`<div class=\"agents-list\">${jobItems.join("")}</div>`);
            }

            rows.push(`<div class=\"agents-row\">${editingLabel}</div>`);
            rows.push(`<div class=\"agents-row\">`
                + `<label>Name <input data-cron-field=\"name\" value=\"${escapeHtml(form.name || "")}\" /></label> `
                + `<label>Enabled <input type=\"checkbox\" data-cron-field=\"enabled\"${form.enabled === false ? "" : " checked"} /></label> `
                + `<label>Schedule <select data-cron-field=\"scheduleKind\">`
                + `<option value=\"every\"${String(form.scheduleKind || "every") === "every" ? " selected" : ""}>every</option>`
                + `<option value=\"at\"${String(form.scheduleKind || "") === "at" ? " selected" : ""}>at</option>`
                + `<option value=\"cron\"${String(form.scheduleKind || "") === "cron" ? " selected" : ""}>cron</option>`
                + `</select></label>`
                + `</div>`);

            rows.push(`<div class=\"agents-row\">`
                + `<label>At <input data-cron-field=\"scheduleAt\" value=\"${escapeHtml(form.scheduleAt || "")}\" placeholder=\"2026-04-21T09:30\" /></label> `
                + `<label>Every <input data-cron-field=\"everyAmount\" value=\"${escapeHtml(form.everyAmount || "")}\" size=\"4\" /></label> `
                + `<label>Unit <select data-cron-field=\"everyUnit\">`
                + `<option value=\"minutes\"${String(form.everyUnit || "minutes") === "minutes" ? " selected" : ""}>minutes</option>`
                + `<option value=\"hours\"${String(form.everyUnit || "") === "hours" ? " selected" : ""}>hours</option>`
                + `<option value=\"days\"${String(form.everyUnit || "") === "days" ? " selected" : ""}>days</option>`
                + `</select></label>`
                + `</div>`);

            rows.push(`<div class=\"agents-row\">`
                + `<label>Cron expr <input data-cron-field=\"cronExpr\" value=\"${escapeHtml(form.cronExpr || "")}\" placeholder=\"*/10 * * * *\" /></label> `
                + `<label>Payload <select data-cron-field=\"payloadKind\">`
                + `<option value=\"agentTurn\"${String(form.payloadKind || "agentTurn") === "agentTurn" ? " selected" : ""}>agentTurn</option>`
                + `<option value=\"systemEvent\"${String(form.payloadKind || "") === "systemEvent" ? " selected" : ""}>systemEvent</option>`
                + `</select></label>`
                + `</div>`);

            rows.push(`<div class=\"agents-row\">`
                + `<label>Payload text <input data-cron-field=\"payloadText\" value=\"${escapeHtml(form.payloadText || "")}\" /></label> `
                + `<label>Model <input data-cron-field=\"payloadModel\" list=\"cronModelSuggestions\" value=\"${escapeHtml(form.payloadModel || "")}\" /></label> `
                + `<label>Thinking <input data-cron-field=\"payloadThinking\" value=\"${escapeHtml(form.payloadThinking || "")}\" /></label> `
                + `<label>Timeout(s) <input data-cron-field=\"timeoutSeconds\" value=\"${escapeHtml(form.timeoutSeconds || "")}\" size=\"6\" /></label>`
                + `</div>`);

            rows.push(`<div class=\"agents-row\">`
                + `<label>Delivery <select data-cron-field=\"deliveryMode\">`
                + `<option value=\"none\"${String(form.deliveryMode || "none") === "none" ? " selected" : ""}>none</option>`
                + `<option value=\"webhook\"${String(form.deliveryMode || "") === "webhook" ? " selected" : ""}>webhook</option>`
                + `<option value=\"announce\"${String(form.deliveryMode || "") === "announce" ? " selected" : ""}>announce</option>`
                + `</select></label> `
                + `<label>Delivery to <input data-cron-field=\"deliveryTo\" value=\"${escapeHtml(form.deliveryTo || "")}\" /></label> `
                + `<label>Failure alert <select data-cron-field=\"failureAlertMode\">`
                + `<option value=\"inherit\"${String(form.failureAlertMode || "inherit") === "inherit" ? " selected" : ""}>inherit</option>`
                + `<option value=\"disabled\"${String(form.failureAlertMode || "") === "disabled" ? " selected" : ""}>disabled</option>`
                + `<option value=\"custom\"${String(form.failureAlertMode || "") === "custom" ? " selected" : ""}>custom</option>`
                + `</select></label> `
                + `<label>Alert after <input data-cron-field=\"failureAlertAfter\" value=\"${escapeHtml(form.failureAlertAfter || "")}\" size=\"6\" /></label> `
                + `<label>Cooldown(s) <input data-cron-field=\"failureAlertCooldownSeconds\" value=\"${escapeHtml(form.failureAlertCooldownSeconds || "")}\" size=\"6\" /></label>`
                + `</div>`);

            if (Object.keys(fieldErrors).length > 0) {
                const errorText = Object.keys(fieldErrors).map((key) => {
                    return `${key}: ${fieldErrors[key]}`;
                }).join(" | ");
                rows.push(`<div class=\"agents-row error\">Field errors: ${escapeHtml(errorText)}</div>`);
            }

            if (selectedJob) {
                rows.push(`<div class=\"agents-row\">Selected schedule: ${escapeHtml(String(selectedJob.schedule && selectedJob.schedule.kind || "n/a"))}</div>`);
                rows.push(`<div class=\"agents-row\">Selected payload: ${escapeHtml(String(selectedJob.payload && selectedJob.payload.kind || "n/a"))}</div>`);
            }

            if (runs.length > 0) {
                const firstRun = runs[0] || {};
                rows.push(`<div class=\"agents-row\">First run job: ${escapeHtml(String(firstRun.jobId || "(n/a)"))}</div>`);
                rows.push(`<div class=\"agents-row\">First run status: ${escapeHtml(String(firstRun.status || "(n/a)"))}</div>`);
                rows.push(`<div class=\"agents-row\">First run ts: ${escapeHtml(String(firstRun.ts || "(n/a)"))}</div>`);
            }

            rows.push(`<datalist id=\"cronModelSuggestions\">${modelSuggestions.map((id) => `<option value=\"${escapeHtml(id)}\"></option>`).join("")}</datalist>`);

            rows.push(`<div class=\"agents-row\">`
                + `<button class=\"agents-chip\" data-cron-action=\"refresh\">Refresh</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"save\"${state.agentCronBusy ? " disabled" : ""}>${state.agentCronEditingJobId ? "Update Job" : "Add Job"}</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"cancel-edit\"${state.agentCronEditingJobId ? "" : " disabled"}>Cancel Edit</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"run-now\"${selectedJobId ? "" : " disabled"}>Run Now</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"remove\"${selectedJobId ? "" : " disabled"}>Remove</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"edit\"${selectedJobId ? "" : " disabled"}>Edit</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"clone\"${selectedJobId ? "" : " disabled"}>Clone</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"jobs-more\"${state.agentCronJobsHasMore ? "" : " disabled"}>Load More Jobs</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"runs-more\"${state.agentCronRunsHasMore ? "" : " disabled"}>Load More Runs</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"scope-all\">Scope All Runs</button> `
                + `<button class=\"agents-chip\" data-cron-action=\"scope-job\"${selectedJobId ? "" : " disabled"}>Scope Selected Job</button>`
                + `</div>`);

            rows.push("<div class=\"agents-row\">Capability mode: cron mutation + form parity baseline via cron.status/list/add/update/remove/run/runs</div>");
            rows.push("<div class=\"agents-row\">Parity track: docs/compare/cron.ts/CRON_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md</div>");
        }

        if (panel === "nodes") {
            if (state.nodesLoading) {
                rows.push("<div class=\"agents-row\">Loading nodes...</div>");
            }
            if (state.nodesError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.nodesError)}</div>`);
            }

            const nodes = Array.isArray(state.nodes)
                ? state.nodes
                : [];
            rows.push(`<div class=\"agents-row\">Nodes count: ${nodes.length}</div>`);
            if (nodes.length > 0) {
                const firstNode = nodes[0] || {};
                rows.push(`<div class=\"agents-row\">First node id: ${escapeHtml(String(firstNode.id || firstNode.nodeId || "(n/a)"))}</div>`);
                rows.push(`<div class=\"agents-row\">First node label: ${escapeHtml(String(firstNode.label || firstNode.name || "(n/a)"))}</div>`);
            }

            rows.push(`<div class=\"agents-row\">`
                + `<button class=\"agents-chip\" data-nodes-action=\"refresh\">Refresh</button>`
                + `</div>`);
            rows.push("<div class=\"agents-row\">Capability mode: OpenClaw-aligned node.list loading contract with quiet/state guards</div>");
            rows.push("<div class=\"agents-row\">Parity track: docs/compare/nodes.ts/NODES_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md</div>");
        }

        if (panel === "instances") {
            if (state.presenceLoading) {
                rows.push("<div class=\"agents-row\">Loading instances...</div>");
            }
            if (state.presenceError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.presenceError)}</div>`);
            }
            if (state.presenceStatus) {
                rows.push(`<div class=\"agents-row\">${escapeHtml(state.presenceStatus)}</div>`);
            }

            const entries = Array.isArray(state.presenceEntries)
                ? state.presenceEntries
                : [];
            rows.push(`<div class=\"agents-row\">Instances count: ${entries.length}</div>`);
            if (entries.length > 0) {
                const firstEntry = entries[0] || {};
                rows.push(`<div class=\"agents-row\">First instance id: ${escapeHtml(String(firstEntry.instanceId || "(n/a)"))}</div>`);
                rows.push(`<div class=\"agents-row\">First host: ${escapeHtml(String(firstEntry.host || "(n/a)"))}</div>`);
                rows.push(`<div class=\"agents-row\">First mode: ${escapeHtml(String(firstEntry.mode || "(n/a)"))}</div>`);
            }

            rows.push(`<div class=\"agents-row\">`
                + `<button class=\"agents-chip\" data-presence-action=\"refresh\">Refresh</button>`
                + `</div>`);
            rows.push("<div class=\"agents-row\">Capability mode: OpenClaw-aligned system-presence loading contract with status/error semantics</div>");
            rows.push("<div class=\"agents-row\">Parity track: docs/compare/presence.ts/PRESENCE_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md</div>");
        }

        if (panel === "usage") {
            if (state.usageLoading) {
                rows.push("<div class=\"agents-row\">Loading usage...</div>");
            }
            if (state.usageError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.usageError)}</div>`);
            }

            rows.push(`<div class=\"agents-row\">Date range: ${escapeHtml(String(state.usageStartDate || "(auto)"))} -> ${escapeHtml(String(state.usageEndDate || "(auto)"))}</div>`);
            rows.push(`<div class=\"agents-row\">Time zone: ${escapeHtml(String(state.usageTimeZone || "local"))}</div>`);
            rows.push(`<div class=\"agents-row\">`
                + `<label>Start <input class=\"agents-input\" data-usage-field=\"start-date\" type=\"date\" value=\"${escapeHtml(String(state.usageStartDate || ""))}\" /></label> `
                + `<label>End <input class=\"agents-input\" data-usage-field=\"end-date\" type=\"date\" value=\"${escapeHtml(String(state.usageEndDate || ""))}\" /></label>`
                + `</div>`);

            const usageSessions = state.usageResult && Array.isArray(state.usageResult.sessions)
                ? state.usageResult.sessions
                : [];
            rows.push(`<div class=\"agents-row\">Usage sessions: ${usageSessions.length}</div>`);

            const usageDaily = state.usageCostSummary && Array.isArray(state.usageCostSummary.daily)
                ? state.usageCostSummary.daily
                : [];
            rows.push(`<div class=\"agents-row\">Usage cost daily points: ${usageDaily.length}</div>`);

            if (usageSessions.length > 0) {
                const firstUsageSession = usageSessions[0] || {};
                const firstUsageSessionKey = String(firstUsageSession.key || "").trim();
                rows.push(`<div class=\"agents-row\">First session key: ${escapeHtml(firstUsageSessionKey || "(n/a)")}</div>`);
                rows.push(`<div class=\"agents-row\">`
                    + `<button class=\"agents-chip\" data-usage-action=\"details\" data-usage-session-key=\"${escapeHtml(firstUsageSessionKey)}\"${firstUsageSessionKey ? "" : " disabled"}>Load Details</button>`
                    + `</div>`);
            }

            const usagePoints = state.usageTimeSeries && Array.isArray(state.usageTimeSeries.points)
                ? state.usageTimeSeries.points
                : [];
            rows.push(`<div class=\"agents-row\">Timeseries points: ${usagePoints.length}</div>`);

            const usageLogs = Array.isArray(state.usageSessionLogs)
                ? state.usageSessionLogs
                : [];
            rows.push(`<div class=\"agents-row\">Session logs: ${usageLogs.length}</div>`);

            rows.push(`<div class=\"agents-row\">`
                + `<button class=\"agents-chip\" data-usage-action=\"refresh\">Refresh</button> `
                + `<button class=\"agents-chip\" data-usage-action=\"apply-date-range\">Apply Date Range</button> `
                + `<button class=\"agents-chip\" data-usage-action=\"timezone-local\">Timezone Local</button> `
                + `<button class=\"agents-chip\" data-usage-action=\"timezone-utc\">Timezone UTC</button>`
                + `</div>`);
            rows.push("<div class=\"agents-row\">Capability mode: OpenClaw-aligned usage baseline (`sessions.usage` + `usage.cost`) with optional detail loaders (`sessions.usage.timeseries`, `sessions.usage.logs`)</div>");
            rows.push("<div class=\"agents-row\">Parity track: docs/compare/usage.ts/USAGE_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md</div>");
        }

        if (panel === "observability") {
            if (!state.observabilityEnabled) {
                rows.push("<div class=\"agents-row\">Observability panel is disabled. Enable with `?observability=1` or localStorage `blazeclaw.observability.enabled=1`.</div>");
            } else {
                if (state.observabilityLoading) {
                    rows.push("<div class=\"agents-row\">Loading observability snapshot...</div>");
                }
                if (state.observabilityError) {
                    rows.push(`<div class=\"agents-row error\">${escapeHtml(state.observabilityError)}</div>`);
                }

                const health = state.observabilityHealth || null;
                const details = state.observabilityHealthDetails || null;
                const transport = state.observabilityTransportStatus || null;
                const heartbeat = state.observabilityHeartbeat || null;
                const models = Array.isArray(state.observabilityModels) ? state.observabilityModels : [];
                const logs = Array.isArray(state.observabilityLogs) ? state.observabilityLogs : [];
                const paused = Boolean(state.observabilityPaused);
                const lastUpdated = Number.isFinite(Number(state.observabilityLastUpdatedMs))
                    ? Number(state.observabilityLastUpdatedMs)
                    : 0;

                rows.push(`<div class=\"agents-row\">Health: ${escapeHtml(String(health && health.status || "(unknown)"))} ， running=${health && health.running ? "yes" : "no"}</div>`);
                rows.push(`<div class=\"agents-row\">Transport: ${transport && transport.running ? "running" : "idle"} ， endpoint=${escapeHtml(String(transport && transport.endpoint || "(n/a)"))} ， connections=${escapeHtml(String(transport && transport.connections || 0))}</div>`);
                rows.push(`<div class=\"agents-row\">Heartbeat: connected=${heartbeat && heartbeat.connected ? "yes" : "no"} ， lastHeartbeatMs=${escapeHtml(String(heartbeat && heartbeat.lastHeartbeatMs || 0))}</div>`);
                rows.push(`<div class=\"agents-row\">Health details endpoint: ${escapeHtml(String(details && details.transport && details.transport.endpoint || "(n/a)"))}</div>`);
                rows.push(`<div class=\"agents-row\">Models available: ${models.length}</div>`);
                rows.push(`<div class=\"agents-row\">Logs entries: ${logs.length} ， paused=${paused ? "yes" : "no"} ， lastUpdatedMs=${lastUpdated || "(none)"}</div>`);
                if (logs.length > 0) {
                    const firstLog = logs[0] || {};
                    rows.push(`<div class=\"agents-row\">Latest log: [${escapeHtml(String(firstLog.level || "info"))}] ${escapeHtml(String(firstLog.message || ""))}</div>`);
                }

                rows.push(`<div class=\"agents-row\">`
                    + `<label>Log level <select data-observability-field=\"logLevel\">`
                    + `<option value=\"all\"${String(state.observabilityLogLevel || "all") === "all" ? " selected" : ""}>all</option>`
                    + `<option value=\"info\"${String(state.observabilityLogLevel || "") === "info" ? " selected" : ""}>info</option>`
                    + `<option value=\"debug\"${String(state.observabilityLogLevel || "") === "debug" ? " selected" : ""}>debug</option>`
                    + `<option value=\"warn\"${String(state.observabilityLogLevel || "") === "warn" ? " selected" : ""}>warn</option>`
                    + `<option value=\"error\"${String(state.observabilityLogLevel || "") === "error" ? " selected" : ""}>error</option>`
                    + `</select></label> `
                    + `<label>Log limit <input class=\"agents-input\" data-observability-field=\"logLimit\" type=\"number\" min=\"1\" max=\"200\" value=\"${escapeHtml(String(state.observabilityLogLimit || 50))}\" /></label>`
                    + `</div>`);

                rows.push(`<div class=\"agents-row\">`
                    + `<label>Method <input class=\"agents-input\" data-observability-field=\"method\" value=\"${escapeHtml(String(state.observabilityMethod || ""))}\" /></label>`
                    + `</div>`);
                rows.push(`<div class=\"agents-row\">`
                    + `<label>Params JSON <input class=\"agents-input\" data-observability-field=\"params\" value=\"${escapeHtml(String(state.observabilityMethodParams || "{}"))}\" /></label>`
                    + `</div>`);
                rows.push(`<div class=\"agents-row\">`
                    + `<button class=\"agents-chip\" data-observability-action=\"refresh\">Refresh</button> `
                    + `<button class=\"agents-chip\" data-observability-action=\"toggle-pause\">${paused ? "Resume logs" : "Pause logs"}</button> `
                    + `<button class=\"agents-chip\" data-observability-action=\"invoke\"${state.observabilityMethodBusy ? " disabled" : ""}>Invoke Method</button> `
                    + `<button class=\"agents-chip\" data-observability-action=\"export\">Export Logs</button>`
                    + `</div>`);

                if (state.observabilityMethodError) {
                    rows.push(`<div class=\"agents-row error\">Method error: ${escapeHtml(String(state.observabilityMethodError || ""))}</div>`);
                }
                if (state.observabilityMethodResult) {
                    rows.push(`<div class=\"agents-row\">Method result:<pre>${escapeHtml(String(state.observabilityMethodResult || ""))}</pre></div>`);
                }
                if (state.observabilityExportText) {
                    rows.push(`<div class=\"agents-row\">Logs export:<pre>${escapeHtml(String(state.observabilityExportText || ""))}</pre></div>`);
                }
                rows.push("<div class=\"agents-row\">Capability mode: debug/logs/health baseline (gateway.health, gateway.health.details, gateway.transport.status, last-heartbeat, models.list, gateway.logs.tail)</div>");
            }
        }

        if (panel === "devices") {
            if (state.devicePairsLoading) {
                rows.push("<div class=\"agents-row\">Loading device pairs...</div>");
            }
            if (state.devicePairsError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(String(state.devicePairsError || ""))}</div>`);
            }
            if (state.devicePairActionStatus) {
                rows.push(`<div class=\"agents-row\">${escapeHtml(String(state.devicePairActionStatus || ""))}</div>`);
            }

            const pairs = Array.isArray(state.devicePairs) ? state.devicePairs : [];
            const selectedDeviceId = String(state.devicePairSelection || "").trim();
            rows.push(`<div class=\"agents-row\">Device pairs: ${pairs.length}</div>`);
            rows.push(`<div class=\"agents-row\">Selected device: ${escapeHtml(selectedDeviceId || "(none)")}</div>`);

            if (pairs.length > 0) {
                const chips = pairs.slice(0, 20).map((entry) => {
                    const deviceId = String(entry && entry.deviceId || "").trim();
                    const status = String(entry && entry.status || "pending").trim();
                    const label = String(entry && (entry.label || entry.deviceId) || "(unknown)").trim();
                    const active = deviceId === selectedDeviceId ? " active" : "";
                    return `<button class=\"agents-chip${active}\" data-devicepair-id=\"${escapeHtml(deviceId)}\">${escapeHtml(label)} (${escapeHtml(status)})</button>`;
                }).join("");
                rows.push(`<div class=\"agents-list\">${chips}</div>`);
            }

            let selectedDeviceDisabledAttr = " disabled";
            if (selectedDeviceId) {
                selectedDeviceDisabledAttr = "";
            }

            let devicePairsBusyDisabledAttr = "";
            if (state.devicePairsBusy) {
                devicePairsBusyDisabledAttr = " disabled";
            }

            rows.push(`<div class=\"agents-row\">`
                + `<button class=\"agents-chip\" data-devicepair-action=\"refresh\">Refresh</button> `
                + `<button class=\"agents-chip\" data-devicepair-action=\"approve\"${selectedDeviceDisabledAttr}${devicePairsBusyDisabledAttr}>Approve</button> `
                + `<button class=\"agents-chip\" data-devicepair-action=\"reject\"${selectedDeviceDisabledAttr}${devicePairsBusyDisabledAttr}>Reject</button> `
                + `<button class=\"agents-chip\" data-devicepair-action=\"remove\"${selectedDeviceDisabledAttr}${devicePairsBusyDisabledAttr}>Remove</button>`
                + `</div>`);
            rows.push("<div class=\"agents-row\">Capability mode: device pairing baseline via device.pair.list/approve/reject/remove</div>");
        }

        if (panel === "dreaming") {
            if (state.agentDreamingLoading || state.dreamingStatusLoading || state.dreamDiaryLoading) {
                rows.push("<div class=\"agents-row\">Loading dreaming surface...</div>");
            }

            let dreamingUi = null;
            if (agentsController && typeof agentsController.getDreamingUiModel === "function") {
                dreamingUi = agentsController.getDreamingUiModel();
            }

            let dreamingStatus = state.dreamingStatus || null;
            if (dreamingUi && dreamingUi.status) {
                dreamingStatus = dreamingUi.status;
            }

            let dreamDiaryPath = "(none)";
            if (typeof state.dreamDiaryPath === "string") {
                dreamDiaryPath = state.dreamDiaryPath;
            }
            if (dreamingUi && typeof dreamingUi.diaryPath === "string") {
                dreamDiaryPath = dreamingUi.diaryPath;
            }

            let shortTermEntries = [];
            if (dreamingStatus && Array.isArray(dreamingStatus.shortTermEntries)) {
                shortTermEntries = dreamingStatus.shortTermEntries;
            }
            if (dreamingUi && Array.isArray(dreamingUi.shortTermEntries)) {
                shortTermEntries = dreamingUi.shortTermEntries;
            }

            let groundedEntries = shortTermEntries.filter(function (entry) {
                return Number(entry && entry.groundedCount || 0) > 0;
            });
            if (dreamingUi && Array.isArray(dreamingUi.groundedEntries)) {
                groundedEntries = dreamingUi.groundedEntries;
            }

            let waitingEntries = shortTermEntries;
            if (dreamingUi && Array.isArray(dreamingUi.waitingEntries)) {
                waitingEntries = dreamingUi.waitingEntries;
            }

            let promotedEntries = [];
            if (dreamingStatus && Array.isArray(dreamingStatus.promotedEntries)) {
                promotedEntries = dreamingStatus.promotedEntries;
            }
            if (dreamingUi && Array.isArray(dreamingUi.promotedEntries)) {
                promotedEntries = dreamingUi.promotedEntries;
            }

            let subTab = String(state.dreamingUiSubTab || "scene");
            if (dreamingUi && typeof dreamingUi.subTab === "string") {
                subTab = dreamingUi.subTab;
            }

            let waitingSort = String(state.dreamingAdvancedWaitingSort || "recent");
            if (dreamingUi && typeof dreamingUi.waitingSort === "string") {
                waitingSort = dreamingUi.waitingSort;
            }

            let diaryPage = Number(state.dreamDiaryPage || 0);
            if (dreamingUi && Number.isFinite(Number(dreamingUi.diaryPage))) {
                diaryPage = Number(dreamingUi.diaryPage);
            }

            let diaryEntry = null;
            if (dreamingUi && dreamingUi.diaryEntry) {
                diaryEntry = dreamingUi.diaryEntry;
            }

            let diaryNavigation = [];
            if (Array.isArray(state.dreamDiaryNavigation)) {
                diaryNavigation = state.dreamDiaryNavigation;
            }

            let dreamPhrase = "Consolidating memories...";
            if (dreamingUi && typeof dreamingUi.phrase === "string") {
                dreamPhrase = dreamingUi.phrase;
            }
            let flattenDiaryBody = function (body) {
                return String(body || "").split("\n").map(function (line) {
                    return String(line || "").trim();
                }).filter(Boolean);
            };
            if (dreamingUi && typeof dreamingUi.flattenDiaryBody === "function") {
                flattenDiaryBody = dreamingUi.flattenDiaryBody;
            }

            let formatRange = function (path, startLine, endLine) {
                let start = 1;
                if (Number.isFinite(Number(startLine))) {
                    start = Number(startLine);
                }

                let end = start;
                if (Number.isFinite(Number(endLine))) {
                    end = Number(endLine);
                }

                if (start === end) {
                    return `${path}:${start}`;
                }
                return `${path}:${start}-${end}`;
            };
            if (dreamingUi && typeof dreamingUi.formatRange === "function") {
                formatRange = dreamingUi.formatRange;
            }

            let formatCompactDateTime = function (value) {
                return String(value || "");
            };
            if (dreamingUi && typeof dreamingUi.formatCompactDateTime === "function") {
                formatCompactDateTime = dreamingUi.formatCompactDateTime;
            }

            let describeOrigin = function () {
                return "Live";
            };
            if (dreamingUi && typeof dreamingUi.describeWaitingEntryOrigin === "function") {
                describeOrigin = dreamingUi.describeWaitingEntryOrigin;
            }

            let formatChipLabel = function (value) {
                return String(value || "");
            };
            if (dreamingUi && typeof dreamingUi.diaryChipLabel === "function") {
                formatChipLabel = dreamingUi.diaryChipLabel;
            }

            const sceneActiveClass = subTab === "scene" ? " active" : "";
            const diaryActiveClass = subTab === "diary" ? " active" : "";
            const advancedActiveClass = subTab === "advanced" ? " active" : "";
            rows.push("<div class=\"agents-row\">"
                + `<button class=\"agents-chip${sceneActiveClass}\" data-dreaming-subtab=\"scene\">Scene</button> `
                + `<button class=\"agents-chip${diaryActiveClass}\" data-dreaming-subtab=\"diary\">Diary</button> `
                + `<button class=\"agents-chip${advancedActiveClass}\" data-dreaming-subtab=\"advanced\">Advanced</button>`
                + "</div>");

            if (state.agentDreamingError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.agentDreamingError)}</div>`);
            }
            if (state.dreamingStatusError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.dreamingStatusError)}</div>`);
            }
            if (state.dreamDiaryError) {
                rows.push(`<div class=\"agents-row error\">${escapeHtml(state.dreamDiaryError)}</div>`);
            }

            const dreamingStateLabel = dreamingStatus && dreamingStatus.enabled ? "active" : "idle";
            rows.push(`<div class=\"agents-row\">Dreaming ${dreamingStateLabel} ， ${escapeHtml(dreamPhrase)}</div>`);
            rows.push(`<div class=\"agents-row\">Plugin id: ${escapeHtml(String(state.dreamingResolvedPluginId || "(unresolved)"))} ， Config hash: ${escapeHtml(String(state.dreamingConfigSnapshotHash || "(missing)"))}</div>`);
            const storageMode = String(dreamingStatus && dreamingStatus.storageMode || "inline");
            const timeZone = String(dreamingStatus && dreamingStatus.timezone || "(n/a)");

            let shortTermCount = 0;
            if (Number.isFinite(Number(dreamingStatus && dreamingStatus.shortTermCount))) {
                shortTermCount = Number(dreamingStatus.shortTermCount);
            }

            let promotedTodayCount = 0;
            if (Number.isFinite(Number(dreamingStatus && dreamingStatus.promotedToday))) {
                promotedTodayCount = Number(dreamingStatus.promotedToday);
            }

            rows.push(`<div class=\"agents-row\">Storage=${escapeHtml(storageMode)} ， TZ=${escapeHtml(timeZone)} ， Short-term=${shortTermCount} ， Promoted today=${promotedTodayCount}</div>`);

            if (subTab === "scene") {
                if (dreamingStatus && dreamingStatus.phases && dreamingStatus.phases.light) {
                    rows.push(`<div class=\"agents-row\">Light phase: ${dreamingStatus.phases.light.enabled ? "on" : "off"} ， cron=${escapeHtml(String(dreamingStatus.phases.light.cron || ""))}</div>`);
                }
                if (dreamingStatus && dreamingStatus.phases && dreamingStatus.phases.deep) {
                    rows.push(`<div class=\"agents-row\">Deep phase: ${dreamingStatus.phases.deep.enabled ? "on" : "off"} ， cron=${escapeHtml(String(dreamingStatus.phases.deep.cron || ""))}</div>`);
                }
                if (dreamingStatus && dreamingStatus.phases && dreamingStatus.phases.rem) {
                    rows.push(`<div class=\"agents-row\">REM phase: ${dreamingStatus.phases.rem.enabled ? "on" : "off"} ， cron=${escapeHtml(String(dreamingStatus.phases.rem.cron || ""))}</div>`);
                }
            }

            if (subTab === "diary") {
                rows.push(`<div class=\"agents-row\">Dream diary path: ${escapeHtml(dreamDiaryPath)}</div>`);
                if (diaryNavigation.length > 0) {
                    rows.push("<div class=\"agents-list\">"
                        + diaryNavigation.map(function (entry) {
                            const page = Number(entry && entry.page || 0);
                            let active = "";
                            if (page === diaryPage) {
                                active = " active";
                            }
                            const label = formatChipLabel(entry && entry.date || "");
                            return `<button class=\"agents-chip${active}\" data-dreaming-day-page=\"${page}\">${escapeHtml(label || String(page + 1))}</button>`;
                        }).join("")
                        + "</div>");
                }
                if (diaryEntry) {
                    const paragraphs = flattenDiaryBody(diaryEntry.body || "");
                    rows.push(`<div class=\"agents-row\">${escapeHtml(String(diaryEntry.date || "(undated)"))}</div>`);
                    rows.push("<div class=\"agents-row\">"
                        + paragraphs.map((para) => `<p>${escapeHtml(String(para || ""))}</p>`).join("")
                        + "</div>");
                } else {
                    rows.push("<div class=\"agents-row\">No dream diary entries yet.</div>");
                }
            }

            if (subTab === "advanced") {
                const waitingRecentActiveClass = waitingSort === "recent" ? " active" : "";
                const waitingSignalsActiveClass = waitingSort === "signals" ? " active" : "";
                rows.push("<div class=\"agents-row\">"
                    + `<button class=\"agents-chip${waitingRecentActiveClass}\" data-dreaming-sort=\"recent\">Sort recent</button> `
                    + `<button class=\"agents-chip${waitingSignalsActiveClass}\" data-dreaming-sort=\"signals\">Sort signals</button>`
                    + "</div>");
                rows.push(`<div class=\"agents-row\">Grounded staged: ${groundedEntries.length} ， Waiting short-term: ${waitingEntries.length} ， Promoted: ${promotedEntries.length}</div>`);

                const renderEntry = (entry, meta) => {
                    const snippet = escapeHtml(String(entry && entry.snippet || ""));
                    const source = escapeHtml(formatRange(entry && entry.path, entry && entry.startLine, entry && entry.endLine));
                    const metaText = escapeHtml(meta.filter((part) => String(part || "").trim().length > 0).join(" ， "));
                    return `<div class=\"agents-row\"><div>${snippet}</div><div>${source}</div><div>${metaText}</div></div>`;
                };

                groundedEntries.slice(0, 8).forEach((entry) => {
                    rows.push(renderEntry(entry, [
                        "Daily log",
                        `${Number(entry && entry.groundedCount || 0)} grounded`,
                        Number(entry && entry.recallCount || 0) > 0
                            ? `${Number(entry && entry.recallCount || 0)} recall`
                            : "",
                    ]));
                });
                waitingEntries.slice(0, 8).forEach((entry) => {
                    rows.push(renderEntry(entry, [
                        describeOrigin(entry),
                        `${Number(entry && entry.totalSignalCount || 0)} signals`,
                        Number(entry && entry.phaseHitCount || 0) > 0
                            ? `${Number(entry && entry.phaseHitCount || 0)} phase`
                            : "",
                    ]));
                });
                promotedEntries.slice(0, 8).forEach((entry) => {
                    rows.push(renderEntry(entry, [
                        describeOrigin(entry),
                        entry && entry.promotedAt
                            ? `updated ${formatCompactDateTime(entry.promotedAt)}`
                            : "",
                    ]));
                });
            }

            let dreamingModeDisabledAttr = "";
            if (state.dreamingModeSaving) {
                dreamingModeDisabledAttr = " disabled";
            }

            let dreamDiaryActionDisabledAttr = "";
            if (state.dreamDiaryActionLoading) {
                dreamDiaryActionDisabledAttr = " disabled";
            }

            rows.push(`<div class=\"agents-row\">`
                + `<button class=\"agents-chip\" data-dreaming-action=\"refresh\">Refresh</button> `
                + `<button class=\"agents-chip\" data-dreaming-action=\"refresh-diary\">Refresh Diary</button> `
                + `<button class=\"agents-chip\" data-dreaming-action=\"enable\"${dreamingModeDisabledAttr}>Enable</button> `
                + `<button class=\"agents-chip\" data-dreaming-action=\"disable\"${dreamingModeDisabledAttr}>Disable</button> `
                + `<button class=\"agents-chip\" data-dreaming-action=\"backfill\"${dreamDiaryActionDisabledAttr}>Backfill Diary</button> `
                + `<button class=\"agents-chip\" data-dreaming-action=\"reset-diary\"${dreamDiaryActionDisabledAttr}>Reset Diary</button> `
                + `<button class=\"agents-chip\" data-dreaming-action=\"reset-grounded\"${dreamDiaryActionDisabledAttr}>Reset Grounded</button>`
                + `</div>`);

            rows.push("<div class=\"agents-row\">Capability mode: OpenClaw-aligned scene/diary/advanced dreaming UX over doctor.memory.* + config.patch/config.schema.lookup</div>");
            rows.push("<div class=\"agents-row\">Parity track: docs/compare/dreaming.ts/DREAMING_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md</div>");
        }

        state.agentsSurfaceEl.innerHTML = rows.join("");

        state.agentsSurfaceEl.querySelectorAll("[data-agent-id]").forEach((el) => {
            el.addEventListener("click", () => {
                const agentId = String(el.getAttribute("data-agent-id") || "").trim();
                if (!agentId) {
                    return;
                }
                agentsController.setSelectedAgentId(agentId);
                persistAgentsSnapshot(agentsController.getPersistenceSnapshot());
                emitAgentsTelemetry("agent.selected", {
                    agentId,
                    panel: state.agentsPanel,
                });
                void agentsController.loadPanelDataForCurrentAgent();
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-agent-file-path]").forEach((el) => {
            el.addEventListener("click", () => {
                const path = String(el.getAttribute("data-agent-file-path") || "").trim();
                const agentId = String(state.agentsSelectedId || "").trim();
                if (!path || !agentId) {
                    return;
                }
                emitAgentsTelemetry("files.file.select", {
                    panel: state.agentsPanel,
                    path,
                });
                void agentsController.selectAgentFile({
                    agentId,
                    path,
                });
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-files-field]").forEach((el) => {
            const field = String(el.getAttribute("data-files-field") || "").trim();
            if (field !== "content") {
                return;
            }
            el.addEventListener("input", () => {
                agentsController.updateAgentFileDraft(String(el.value || ""));
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-files-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-files-action") || "").trim();
                if (!action) {
                    return;
                }
                const agentId = String(state.agentsSelectedId || "").trim();
                const path = String(state.agentFileSelectedPath || "").trim();
                emitAgentsTelemetry("files.action", {
                    panel: state.agentsPanel,
                    action,
                    path: path || null,
                });
                if (action === "refresh") {
                    void agentsController.loadPanelDataForCurrentAgent();
                    return;
                }
                if (action === "reload") {
                    if (!agentId || !path) {
                        return;
                    }
                    void agentsController.loadAgentFileContent({
                        agentId,
                        path,
                    });
                    return;
                }
                if (action === "save") {
                    if (!agentId || !path) {
                        return;
                    }
                    void agentsController.saveAgentFileContent({
                        agentId,
                        path,
                        content: String(state.agentFileEditDraft || ""),
                    });
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-skills-field]").forEach((el) => {
            const field = String(el.getAttribute("data-skills-field") || "").trim();
            if (!field) {
                return;
            }
            el.addEventListener("input", () => {
                const value = String(el.value || "");
                if (field === "query") {
                    state.skillsHubQuery = value;
                    return;
                }
                if (field === "editPayload") {
                    state.skillsEditPayload = value;
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-skills-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-skills-action") || "").trim();
                if (!action) {
                    return;
                }
                const skill = String(el.getAttribute("data-skill-key") || "").trim();
                emitAgentsTelemetry("skills.action", {
                    action,
                    panel: state.agentsPanel,
                    skill: skill || null,
                });

                if (action === "refresh") {
                    void agentsController.loadPanelDataForCurrentAgent();
                    return;
                }
                if (action === "search") {
                    void agentsController.searchSkillsHub({
                        query: String(state.skillsHubQuery || ""),
                    });
                    return;
                }
                if (action === "detail") {
                    if (!skill) {
                        return;
                    }
                    void agentsController.loadSkillDetail(skill);
                    return;
                }
                if (action === "install") {
                    if (!skill) {
                        return;
                    }
                    void agentsController.installSkill(skill);
                    return;
                }
                if (action === "edit") {
                    void agentsController.updateSkillConfig({
                        payload: String(state.skillsEditPayload || ""),
                    });
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-channels-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-channels-action") || "").trim();
                if (!action) {
                    return;
                }

                emitAgentsTelemetry("channels.action", {
                    action,
                    panel: state.agentsPanel,
                });

                if (action === "refresh") {
                    void agentsController.loadPanelDataForCurrentAgent();
                    return;
                }
                if (action === "start") {
                    void agentsController.startWhatsAppLogin({ force: false }).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                    return;
                }
                if (action === "start-force") {
                    void agentsController.startWhatsAppLogin({ force: true }).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                    return;
                }
                if (action === "wait") {
                    void agentsController.waitWhatsAppLogin({}).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                    return;
                }
                if (action === "logout") {
                    void agentsController.logoutWhatsApp({}).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-cron-job-id]").forEach((el) => {
            el.addEventListener("click", () => {
                const selectedJobId = String(el.getAttribute("data-cron-job-id") || "").trim();
                if (!selectedJobId) {
                    return;
                }

                agentsController.updateCronRunsFilter({
                    selectedJobId,
                });
                emitAgentsTelemetry("cron.job.selected", {
                    panel: state.agentsPanel,
                    selectedJobId,
                });
                void agentsController.loadCronRuns({ append: false });
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-cron-field]").forEach(function (el) {
            const field = String(el.getAttribute("data-cron-field") || "").trim();
            if (!field) {
                return;
            }

            const isCheckbox = el instanceof HTMLInputElement && el.type === "checkbox";
            let eventName = "input";
            if (isCheckbox) {
                eventName = "change";
            }

            el.addEventListener(eventName, function () {
                let value = String(el.value || "");
                if (isCheckbox) {
                    value = Boolean(el.checked);
                }
                agentsController.updateCronFormField(field, value);
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-cron-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-cron-action") || "").trim();
                if (!action) {
                    return;
                }

                emitAgentsTelemetry("cron.action", {
                    action,
                    panel: state.agentsPanel,
                    selectedJobId: String(state.agentCronSelectedJobId || "").trim() || null,
                    editingJobId: String(state.agentCronEditingJobId || "").trim() || null,
                });

                if (action === "refresh") {
                    void agentsController.loadPanelDataForCurrentAgent();
                    return;
                }
                if (action === "save") {
                    void agentsController.addOrUpdateCronJob();
                    return;
                }
                if (action === "cancel-edit") {
                    agentsController.cancelCronEdit();
                    return;
                }
                if (action === "run-now") {
                    const selectedJobId = String(state.agentCronSelectedJobId || "").trim();
                    if (!selectedJobId) {
                        return;
                    }
                    void agentsController.runCronJobNow(selectedJobId, "force");
                    return;
                }
                if (action === "remove") {
                    const selectedJobId = String(state.agentCronSelectedJobId || "").trim();
                    if (!selectedJobId) {
                        return;
                    }
                    void agentsController.removeCronJob(selectedJobId);
                    return;
                }
                if (action === "edit") {
                    const selectedJobId = String(state.agentCronSelectedJobId || "").trim();
                    if (!selectedJobId) {
                        return;
                    }
                    agentsController.startCronEdit(selectedJobId);
                    return;
                }
                if (action === "clone") {
                    const selectedJobId = String(state.agentCronSelectedJobId || "").trim();
                    if (!selectedJobId) {
                        return;
                    }
                    agentsController.startCronClone(selectedJobId);
                    return;
                }
                if (action === "jobs-more") {
                    void agentsController.loadMoreCronJobs();
                    return;
                }
                if (action === "runs-more") {
                    void agentsController.loadMoreCronRuns();
                    return;
                }
                if (action === "scope-all") {
                    agentsController.updateCronRunsFilter({
                        scope: "all",
                    });
                    void agentsController.loadCronRuns({ append: false });
                    return;
                }
                if (action === "scope-job") {
                    const selectedJobId = String(state.agentCronSelectedJobId || "").trim();
                    if (!selectedJobId) {
                        return;
                    }
                    agentsController.updateCronRunsFilter({
                        scope: "job",
                        selectedJobId,
                    });
                    void agentsController.loadCronRuns({ append: false });
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-nodes-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-nodes-action") || "").trim();
                if (!action) {
                    return;
                }

                emitAgentsTelemetry("nodes.action", {
                    action,
                    panel: state.agentsPanel,
                });

                if (action === "refresh") {
                    void agentsController.loadNodes({
                        quiet: false,
                    });
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-presence-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-presence-action") || "").trim();
                if (!action) {
                    return;
                }

                emitAgentsTelemetry("presence.action", {
                    action,
                    panel: state.agentsPanel,
                });

                if (action === "refresh") {
                    void agentsController.loadPresence({
                        quiet: false,
                    });
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-usage-field]").forEach((el) => {
            el.addEventListener("change", () => {
                const field = String(el.getAttribute("data-usage-field") || "").trim();
                const value = String(el.value || "").trim();
                if (!field) {
                    return;
                }

                if (field === "start-date") {
                    state.usageStartDate = value;
                } else if (field === "end-date") {
                    state.usageEndDate = value;
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-usage-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-usage-action") || "").trim();
                if (!action) {
                    return;
                }

                emitAgentsTelemetry("usage.action", {
                    action,
                    panel: state.agentsPanel,
                });

                if (action === "refresh") {
                    void agentsController.loadUsage({
                        quiet: false,
                    });
                    return;
                }

                if (action === "apply-date-range") {
                    if (state.usageStartDate && state.usageEndDate && state.usageStartDate > state.usageEndDate) {
                        state.usageError = "Start date must be on or before end date.";
                        return;
                    }
                    void agentsController.loadUsage({
                        quiet: false,
                    });
                    return;
                }

                if (action === "timezone-local" || action === "timezone-utc") {
                    state.usageTimeZone = "local";
                    if (action === "timezone-utc") {
                        state.usageTimeZone = "utc";
                    }

                    void agentsController.loadUsage({
                        quiet: false,
                    });
                    return;
                }

                if (action === "details") {
                    const sessionKey = String(el.getAttribute("data-usage-session-key") || "").trim();
                    if (!sessionKey) {
                        return;
                    }
                    state.usageSelectedSessions = [sessionKey];
                    void Promise.all([
                        agentsController.loadUsageTimeSeries(sessionKey, {
                            shouldIgnoreResponse: function () {
                                return String(state.agentsPanel || "") !== "usage";
                            },
                        }),
                        agentsController.loadUsageSessionLogs(sessionKey, {
                            shouldIgnoreResponse: function () {
                                return String(state.agentsPanel || "") !== "usage";
                            },
                        }),
                    ]);
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-observability-field]").forEach(function (el) {
            const field = String(el.getAttribute("data-observability-field") || "").trim();
            if (!field) {
                return;
            }

            let eventName = "input";
            if (field === "logLevel") {
                eventName = "change";
            }

            el.addEventListener(eventName, function () {
                const value = String(el.value || "");
                agentsController.updateObservabilityField(field, value);
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-observability-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-observability-action") || "").trim();
                if (!action) {
                    return;
                }

                emitAgentsTelemetry("observability.action", {
                    action,
                    panel: state.agentsPanel,
                });

                if (action === "refresh") {
                    void agentsController.loadObservability({
                        quiet: false,
                        shouldIgnoreResponse: function () {
                            return String(state.agentsPanel || "") !== "observability";
                        },
                    });
                    return;
                }

                if (action === "toggle-pause") {
                    agentsController.updateObservabilityField("paused", !state.observabilityPaused);
                    if (!state.observabilityPaused) {
                        void agentsController.loadObservability({
                            quiet: false,
                            shouldIgnoreResponse: function () {
                                return String(state.agentsPanel || "") !== "observability";
                            },
                        });
                    }
                    return;
                }

                if (action === "invoke") {
                    void agentsController.invokeObservabilityMethod();
                    return;
                }

                if (action === "export") {
                    agentsController.exportObservabilityLogs();
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-devicepair-id]").forEach((el) => {
            el.addEventListener("click", () => {
                const deviceId = String(el.getAttribute("data-devicepair-id") || "").trim();
                if (!deviceId) {
                    return;
                }
                agentsController.selectDevicePair(deviceId);
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-devicepair-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-devicepair-action") || "").trim();
                if (!action) {
                    return;
                }
                emitAgentsTelemetry("devices.action", {
                    action,
                    panel: state.agentsPanel,
                    deviceId: String(state.devicePairSelection || ""),
                });
                if (action === "refresh") {
                    void agentsController.loadDevicePairs({
                        quiet: false,
                        shouldIgnoreResponse: function () {
                            return String(state.agentsPanel || "") !== "devices";
                        },
                    });
                    return;
                }
                if (action === "approve" || action === "reject" || action === "remove") {
                    const deviceId = String(state.devicePairSelection || "").trim();
                    if (!deviceId) {
                        return;
                    }
                    void agentsController.resolveDevicePair(action, deviceId);
                }
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-dreaming-subtab]").forEach((el) => {
            el.addEventListener("click", () => {
                const tab = String(el.getAttribute("data-dreaming-subtab") || "").trim();
                if (!tab || !agentsController || typeof agentsController.setDreamingSubTab !== "function") {
                    return;
                }
                agentsController.setDreamingSubTab(tab);
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-dreaming-sort]").forEach((el) => {
            el.addEventListener("click", () => {
                const sort = String(el.getAttribute("data-dreaming-sort") || "").trim();
                if (!sort || !agentsController || typeof agentsController.setDreamingAdvancedWaitingSort !== "function") {
                    return;
                }
                agentsController.setDreamingAdvancedWaitingSort(sort);
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-dreaming-day-page]").forEach((el) => {
            el.addEventListener("click", () => {
                const pageText = String(el.getAttribute("data-dreaming-day-page") || "").trim();
                const page = Number(pageText);
                if (!Number.isFinite(page) ||
                    !agentsController ||
                    typeof agentsController.setDreamDiaryPage !== "function") {
                    return;
                }
                agentsController.setDreamDiaryPage(page);
            });
        });

        state.agentsSurfaceEl.querySelectorAll("[data-dreaming-action]").forEach((el) => {
            el.addEventListener("click", () => {
                const action = String(el.getAttribute("data-dreaming-action") || "").trim();
                if (!action) {
                    return;
                }

                emitAgentsTelemetry("dreaming.action", {
                    action,
                    panel: state.agentsPanel,
                    selectedAgentId: String(state.agentsSelectedId || "").trim() || null,
                });

                if (action === "refresh") {
                    void agentsController.loadPanelDataForCurrentAgent();
                    return;
                }
                if (action === "refresh-diary") {
                    void agentsController.loadDreamDiary({});
                    return;
                }
                if (action === "enable") {
                    void agentsController.updateDreamingEnabled(true).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                    return;
                }
                if (action === "disable") {
                    void agentsController.updateDreamingEnabled(false).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                    return;
                }
                if (action === "backfill") {
                    void agentsController.backfillDreamDiary({}).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                    return;
                }
                if (action === "reset-diary") {
                    void agentsController.resetDreamDiary({}).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                    return;
                }
                if (action === "reset-grounded") {
                    void agentsController.resetGroundedShortTerm({}).then(() => {
                        return agentsController.loadPanelDataForCurrentAgent();
                    });
                }
            });
        });
    }

    function renderAgentsTabs() {
        if (!state.agentsTabsEl) {
            return;
        }

        if (!agentsController) {
            state.agentsTabsEl.innerHTML = "";
            return;
        }

        const tabs = [
            { id: "overview", label: "Overview" },
            { id: "tools", label: "Tools" },
            { id: "files", label: "Files" },
            { id: "skills", label: "Skills" },
            { id: "channels", label: "Channels" },
            { id: "cron", label: "Cron" },
            { id: "dreaming", label: "Dreaming" },
            { id: "nodes", label: "Nodes" },
            { id: "instances", label: "Instances" },
            { id: "usage", label: "Usage" },
        ];
        if (state.observabilityEnabled) {
            tabs.push({ id: "observability", label: "Observability" });
        }
        tabs.push({ id: "devices", label: "Devices" });
        const current = String(state.agentsPanel || "overview");

        state.agentsTabsEl.innerHTML = tabs.map(function (tab) {
            let active = "";
            if (tab.id === current) {
                active = " active";
            }
            return `<button class=\"agents-tab${active}\" data-panel=\"${tab.id}\">${tab.label}</button>`;
        }).join("");

        state.agentsTabsEl.querySelectorAll("[data-panel]").forEach((el) => {
            el.addEventListener("click", () => {
                const panel = String(el.getAttribute("data-panel") || "").trim();
                agentsController.setAgentsPanel(panel);
                persistAgentsSnapshot(agentsController.getPersistenceSnapshot());
                emitAgentsTelemetry("tab.selected", {
                    panel,
                });
                void agentsController.loadPanelDataForCurrentAgent();
            });
        });
    }

    function updateComposerState() {
        const hasInput = state.inputEl.value.trim().length > 0;
        const hasAttachments = state.attachments.length > 0;
        const canSend = state.bridgeAvailable && (hasInput || hasAttachments);
        state.sendBtn.disabled = !canSend;
        state.sendErrBtn.disabled = !canSend;
        state.abortBtn.disabled = !state.bridgeAvailable || !state.runId;
        state.attachBtn.disabled = !state.bridgeAvailable;
        if (state.speechTranscribeBtn) {
            const speechCapabilities = state.speechCapabilities && typeof state.speechCapabilities === "object"
                ? state.speechCapabilities
                : null;
            const speechReady = speechCapabilities
                ? (speechCapabilities.loaded !== true || (speechCapabilities.sttSupported && speechCapabilities.sttReady))
                : true;
            const speechSessionState = state.speechSessionState && typeof state.speechSessionState === "object"
                ? state.speechSessionState
                : null;
            const speechStage = speechSessionState
                ? String(speechSessionState.stage || "").trim()
                : "";
            const speechRunId = speechSessionState
                ? String(speechSessionState.runId || "").trim()
                : "";
            const finalAuthorityActive = hasSpeechFinalAuthority(speechSessionState);
            const speechBusy = speechStage === "queued" ||
                speechStage === "recording" ||
                speechStage === "start_stream" ||
                speechStage === "streaming" ||
                speechStage === "stopped" ||
                speechStage === "transcribing";
            const previewStageBlockedByFinalAuthority =
                finalAuthorityActive &&
                isSpeechPreviewRunId(speechRunId);
            const recordingActive =
                !previewStageBlockedByFinalAuthority &&
                isRecordingSpeechStage(speechStage, speechRunId);
            state.speechTranscribeBtn.disabled = !state.bridgeAvailable || !speechReady || (speechBusy && !recordingActive);
            if (speechCapabilities && speechCapabilities.loaded === true && !speechCapabilities.sttSupported) {
                state.speechTranscribeBtn.disabled = true;
            }

            if (recordingActive) {
                state.speechTranscribeBtn.textContent = "Recording... (click to stop)";
            } else if (previewStageBlockedByFinalAuthority) {
                state.speechTranscribeBtn.textContent = "Finalizing...";
            } else if (speechStage === "queued") {
                state.speechTranscribeBtn.textContent = "Queued...";
            } else if (speechStage === "stopped") {
                state.speechTranscribeBtn.textContent = "Finalizing...";
            } else if (speechStage === "transcribing") {
                state.speechTranscribeBtn.textContent = "Transcribing...";
            } else if (speechStage === "failed" && String(speechSessionState && speechSessionState.errorCode || "").trim() === "transcript_rejected") {
                state.speechTranscribeBtn.textContent = "Transcribe (retry)";
            } else {
                state.speechTranscribeBtn.textContent = "Transcribe";
            }
        }
        if (state.sessionSelect) {
            state.sessionSelect.disabled = !state.bridgeAvailable;
        }
        if (state.modelSelect) {
            state.modelSelect.disabled = !state.bridgeAvailable;
        }
        if (state.thinkingSelect) {
            state.thinkingSelect.disabled = !state.bridgeAvailable;
        }
        if (state.sessionSubscribeBtn) {
            state.sessionSubscribeBtn.disabled = !state.bridgeAvailable;
        }
        if (state.sessionUnsubscribeBtn) {
            state.sessionUnsubscribeBtn.disabled = !state.bridgeAvailable;
        }
        if (state.sessionCompactionRefreshBtn) {
            state.sessionCompactionRefreshBtn.disabled = !state.bridgeAvailable;
        }
        if (state.sessionCompactionSelect) {
            state.sessionCompactionSelect.disabled = !state.bridgeAvailable;
        }
        if (state.sessionCompactionBranchBtn) {
            state.sessionCompactionBranchBtn.disabled = !state.bridgeAvailable;
        }
        if (state.sessionCompactionRestoreBtn) {
            state.sessionCompactionRestoreBtn.disabled = !state.bridgeAvailable;
        }
        state.attachBtn.textContent = state.attachments.length > 0
            ? `Attach(${state.attachments.length})`
            : "Attach";

        renderAssistantIdentity();
        renderSpeechStatus();
        renderSpeechLivePreview();
        renderSessionControls();
        if (state.agentsControlPlaneEl) {
            state.agentsControlPlaneEl.hidden = !agentsController;
        }
        renderAgentsTabs();
        renderAgentsSurface();
        syncNodesPolling();
        syncSessionControlsPolling();
        syncObservabilityPolling();
        renderApprovalQueue();
        renderDetachedNotices();
    }

    const controller = window.BlazeClawChatController.createController({
        state,
        addMessage,
        addOrReplaceStream,
        finalizeStream,
        updateComposerState,
        clearMessages,
        setInputValue,
        onDetachedNotice: appendDetachedNotice,
        onSessionControlStateChanged: (snapshot) => {
            const source = snapshot && typeof snapshot === "object"
                ? snapshot
                : {};
            state.sessionSubscribed = Boolean(source.subscribed);
            state.sessionCompactionItems = Array.isArray(source.compactions)
                ? source.compactions.slice()
                : [];
            state.sessionCompactionSelection = String(source.selectedCompactionId || "");
            state.sessionCompactionStatus = String(source.status || "");
            syncSessionControlsPolling();
            updateComposerState();
        },
        onCronSlashCommand: async (input) => {
            if (!agentsController || typeof agentsController.executeCronCliSlashCommand !== "function") {
                return {
                    handled: true,
                    ok: false,
                    kind: "error",
                    message: JSON.stringify({
                        surface: "cron-cli",
                        ok: false,
                        code: "unavailable",
                        message: "/cron is unavailable because agents control plane is disabled.",
                    }),
                };
            }
            return agentsController.executeCronCliSlashCommand(String(input || ""));
        },
    });

    if (state.speechTranscribeBtn) {
        let recordingBusy = false;
        let liveSpeechPollTimer = null;
        let liveSpeechPollBusy = false;
        let liveSpeechPollGeneration = 0;
        let liveSpeechPollInFlightRunId = "";
        let speechFirstTokenTrace = null;
        const liveSpeechPollIntervalMs = 150;
        const liveSpeechPollBusyRetryMs = 50;
        const liveSpeechPollTimeoutMs = 8000;
        const nowPerfMs = () => {
            if (window.performance && typeof window.performance.now === "function") {
                return window.performance.now();
            }
            return Date.now();
        };
        const emitSpeechPreviewDiagnostic = (counterName, details) => {
            if (typeof controller.getOperatorDiagnosticsSnapshot === "function" &&
                window.console &&
                typeof window.console.debug === "function") {
                window.console.debug("[speech-preview-diagnostic]", {
                    counter: String(counterName || ""),
                    details: details && typeof details === "object" ? details : {},
                });
            }
        };
        const summarizeSpeechAudioArtifact = (audioArtifact) => {
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
        };
        const stopLiveSpeechPoll = () => {
            liveSpeechPollGeneration += 1;
            if (liveSpeechPollTimer !== null) {
                window.clearInterval(liveSpeechPollTimer);
                liveSpeechPollTimer = null;
            }
            liveSpeechPollBusy = false;
            liveSpeechPollInFlightRunId = "";
        };
        const emitFirstTokenRenderDiagnostic = (runId, sessionState) => {
            const text = String(sessionState && (sessionState.segmentText || sessionState.text) || "").trim();
            if (!text || !speechFirstTokenTrace || speechFirstTokenTrace.firstRendered) {
                return;
            }

            const activeRunId = String(runId || sessionState.runId || "").trim();
            if (speechFirstTokenTrace.runId && activeRunId && speechFirstTokenTrace.runId !== activeRunId) {
                return;
            }

            speechFirstTokenTrace.firstRendered = true;
            speechFirstTokenTrace.firstRenderAtMs = nowPerfMs();
            emitSpeechPreviewDiagnostic("speech.first_token.rendered", {
                runId: activeRunId,
                sessionId: String(sessionState.sessionId || ""),
                clickToRenderMs: speechFirstTokenTrace.clickAtMs
                    ? Math.max(0, speechFirstTokenTrace.firstRenderAtMs - speechFirstTokenTrace.clickAtMs)
                    : 0,
                previewResponseToRenderMs: speechFirstTokenTrace.previewResponseAtMs
                    ? Math.max(0, speechFirstTokenTrace.firstRenderAtMs - speechFirstTokenTrace.previewResponseAtMs)
                    : 0,
                firstTokenTiming: sessionState.firstTokenTiming || null,
                gatewayNativePayloadReadyOffsetMs: Number(sessionState.gatewayNativePayloadReadyOffsetMs || 0),
                effectiveExecutionProvider: String(sessionState.effectiveExecutionProvider || ""),
                cudaExecutionProviderReason: String(sessionState.cudaExecutionProviderReason || ""),
            });
        };
        const startLiveSpeechPoll = (audioPath, audioArtifact, prompt, previewRunId) => {
            stopLiveSpeechPoll();
            const speechCapabilities = state.speechCapabilities && typeof state.speechCapabilities === "object"
                ? state.speechCapabilities
                : null;
            if (!speechCapabilities || speechCapabilities.streamingPreviewEnabled !== true) {
                emitSpeechPreviewDiagnostic("speech.preview.disabled", {
                    runId: String(previewRunId || ""),
                    reason: speechCapabilities ? "capability_toggle" : "capability_unloaded",
                    loaded: Boolean(speechCapabilities && speechCapabilities.loaded),
                    streamingPreviewEnabled: Boolean(speechCapabilities && speechCapabilities.streamingPreviewEnabled),
                    livePreviewToggleEnabled: Boolean(speechCapabilities && speechCapabilities.livePreviewToggleEnabled),
                    livePreviewToggleSource: String(speechCapabilities && speechCapabilities.livePreviewToggleSource || ""),
                });
                return;
            }
            if (!audioPath && !audioArtifact) {
                return;
            }

            const pollGeneration = liveSpeechPollGeneration;
            const stablePreviewRunId = String(previewRunId || `speech-preview-${Date.now()}`).trim();
            emitSpeechPreviewDiagnostic("speech.preview.poll_config", {
                runId: stablePreviewRunId,
                generation: pollGeneration,
                intervalMs: liveSpeechPollIntervalMs,
                busyRetryMs: liveSpeechPollBusyRetryMs,
                timeoutMs: liveSpeechPollTimeoutMs,
                mode: "recursive_timeout",
                hasAudioArtifact: Boolean(audioArtifact),
                hasAudioPath: Boolean(audioPath),
            });
            const scheduleNextPoll = (delayMs) => {
                if (pollGeneration !== liveSpeechPollGeneration) {
                    return;
                }

                const safeDelayMs = Math.max(0, Number(delayMs) || 0);
                liveSpeechPollTimer = window.setTimeout(pollOnce, safeDelayMs);
            };
            const pollOnce = async () => {
                if (liveSpeechPollTimer !== null) {
                    window.clearTimeout(liveSpeechPollTimer);
                    liveSpeechPollTimer = null;
                }
                if (pollGeneration !== liveSpeechPollGeneration) {
                    emitSpeechPreviewDiagnostic("speech.preview.skip_stale_generation", {
                        runId: stablePreviewRunId,
                        generation: pollGeneration,
                        currentGeneration: liveSpeechPollGeneration,
                    });
                    return;
                }
                if (liveSpeechPollBusy) {
                    emitSpeechPreviewDiagnostic("speech.preview.skip_busy", {
                        runId: liveSpeechPollInFlightRunId || stablePreviewRunId,
                        generation: pollGeneration,
                        retryMs: liveSpeechPollBusyRetryMs,
                    });
                    scheduleNextPoll(liveSpeechPollBusyRetryMs);
                    return;
                }
                const speechSnapshot = state.speechSessionState && typeof state.speechSessionState === "object"
                    ? state.speechSessionState
                    : null;
                const stage = String(speechSnapshot && speechSnapshot.stage || "").trim();
                const activeRunId = String(speechSnapshot && speechSnapshot.runId || stablePreviewRunId).trim();
                if (!isRecordingSpeechStage(stage, activeRunId)) {
                    emitSpeechPreviewDiagnostic("speech.preview.stop_inactive_stage", {
                        runId: stablePreviewRunId,
                        generation: pollGeneration,
                        stage,
                    });
                    stopLiveSpeechPoll();
                    return;
                }

                liveSpeechPollBusy = true;
                liveSpeechPollInFlightRunId = stablePreviewRunId;
                const previewRequestAtMs = nowPerfMs();
                emitSpeechPreviewDiagnostic("speech.preview.request_start", {
                    runId: stablePreviewRunId,
                    generation: pollGeneration,
                    stage,
                        intervalMs: liveSpeechPollIntervalMs,
                    hasAudioArtifact: Boolean(audioArtifact),
                    hasAudioPath: Boolean(audioPath),
                    clickToPreviewRequestMs: speechFirstTokenTrace && speechFirstTokenTrace.clickAtMs
                        ? Math.max(0, previewRequestAtMs - speechFirstTokenTrace.clickAtMs)
                        : 0,
                });
                try {
                    await controller.transcribeSpeech({
                        audioPath,
                        audioArtifact,
                        prompt,
                        runId: stablePreviewRunId,
                        timeoutMs: liveSpeechPollTimeoutMs,
                        livePreviewOnly: true,
                    });
                    if (pollGeneration !== liveSpeechPollGeneration) {
                        emitSpeechPreviewDiagnostic("speech.preview.response_stale_generation", {
                            runId: stablePreviewRunId,
                            generation: pollGeneration,
                            currentGeneration: liveSpeechPollGeneration,
                        });
                        return;
                    }
                    if (typeof controller.getSpeechSessionStateSnapshot === "function") {
                        state.speechSessionState = controller.getSpeechSessionStateSnapshot();
                        const previewResponseAtMs = nowPerfMs();
                        if (speechFirstTokenTrace && speechFirstTokenTrace.runId === stablePreviewRunId) {
                            speechFirstTokenTrace.previewResponseAtMs = previewResponseAtMs;
                        }
                        const updatedSequence = Number(state.speechSessionState.segmentSequence || 0);
                        emitSpeechPreviewDiagnostic("speech.preview.request_end", {
                            runId: stablePreviewRunId,
                            generation: pollGeneration,
                            stage: String(state.speechSessionState.stage || ""),
                            segmentSequence: updatedSequence,
                            hasText: Boolean(state.speechSessionState.segmentText || state.speechSessionState.text),
                            intervalMs: liveSpeechPollIntervalMs,
                            previewRequestToResponseMs: Math.max(0, previewResponseAtMs - previewRequestAtMs),
                            clickToPreviewResponseMs: speechFirstTokenTrace && speechFirstTokenTrace.clickAtMs
                                ? Math.max(0, previewResponseAtMs - speechFirstTokenTrace.clickAtMs)
                                : 0,
                            firstTokenTiming: state.speechSessionState.firstTokenTiming || null,
                            gatewayNativePayloadReadyOffsetMs: Number(state.speechSessionState.gatewayNativePayloadReadyOffsetMs || 0),
                            effectiveExecutionProvider: String(state.speechSessionState.effectiveExecutionProvider || ""),
                            cudaExecutionProviderAvailable: Boolean(state.speechSessionState.cudaExecutionProviderAvailable),
                            cudaExecutionProviderEnabled: Boolean(state.speechSessionState.cudaExecutionProviderEnabled),
                            cudaExecutionProviderReason: String(state.speechSessionState.cudaExecutionProviderReason || ""),
                        });
                        updateComposerState();
                        emitFirstTokenRenderDiagnostic(stablePreviewRunId, state.speechSessionState);
                    }
                } catch (_error) {
                    emitSpeechPreviewDiagnostic("speech.preview.request_error", {
                        runId: stablePreviewRunId,
                        generation: pollGeneration,
                    });
                    // Keep polling; final error will be surfaced on explicit stop transcribe.
                } finally {
                    if (pollGeneration === liveSpeechPollGeneration) {
                        liveSpeechPollBusy = false;
                        liveSpeechPollInFlightRunId = "";
                        scheduleNextPoll(liveSpeechPollIntervalMs);
                    } else if (liveSpeechPollInFlightRunId === stablePreviewRunId) {
                        liveSpeechPollInFlightRunId = "";
                    }
                }
            };

            void pollOnce();
        };
        state.speechTranscribeBtn.addEventListener("click", async () => {
            if (recordingBusy) {
                return;
            }

            const speechSessionState = state.speechSessionState && typeof state.speechSessionState === "object"
                ? state.speechSessionState
                : null;
            const speechStage = speechSessionState
                ? String(speechSessionState.stage || "").trim()
                : "";
            const speechRunId = speechSessionState
                ? String(speechSessionState.runId || "").trim()
                : "";
            const recordingActive = isRecordingSpeechStage(speechStage, speechRunId);

            recordingBusy = true;
            try {
                if (!recordingActive) {
                    const clickAtMs = nowPerfMs();
                    emitSpeechPreviewDiagnostic("speech.first_token.click", {
                        sessionId: state.sessionKey,
                    });
                    const startResponse = await controller.request("gateway.speech.startRecording", {
                        sessionId: state.sessionKey,
                    });
                    const startResponseAtMs = nowPerfMs();
                    const startPayload = startResponse && typeof startResponse.payload === "object"
                        ? startResponse.payload
                        : {};
                    const startAudioPath = String(startPayload.audioPath || "").trim();
                    const startAudioArtifact = startPayload.audioArtifact && typeof startPayload.audioArtifact === "object"
                        ? startPayload.audioArtifact
                        : null;
                    const previewRunId = `speech-preview-${Date.now()}`;
                    speechFirstTokenTrace = {
                        runId: previewRunId,
                        sessionId: state.sessionKey,
                        clickAtMs,
                        startResponseAtMs,
                        firstRendered: false,
                    };
                    emitSpeechPreviewDiagnostic("speech.first_token.start_recording_response", {
                        runId: previewRunId,
                        sessionId: state.sessionKey,
                        clickToStartResponseMs: Math.max(0, startResponseAtMs - clickAtMs),
                        firstTokenTiming: startPayload.firstTokenTiming || null,
                        effectiveExecutionProvider: String(startPayload.speechRuntime && startPayload.speechRuntime.effectiveExecutionProvider || ""),
                        cudaExecutionProviderReason: String(startPayload.speechRuntime && startPayload.speechRuntime.cudaExecutionProviderReason || ""),
                    });
                    if (typeof controller.applySpeechLifecycleUpdate === "function") {
                        controller.applySpeechLifecycleUpdate({
                            stage: "recording",
                            sessionId: state.sessionKey,
                            runId: previewRunId,
                            audioPath: startAudioPath,
                            audioArtifact: startAudioArtifact,
                            speechRuntime: startPayload.speechRuntime || null,
                            firstTokenTiming: startPayload.firstTokenTiming || null,
                            text: "",
                            errorCode: "",
                            errorMessage: "",
                            errorClass: "status",
                        });
                    }
                    const prompt = String(state.inputEl.value || "").trim();
                    startLiveSpeechPoll(startAudioPath, startAudioArtifact, prompt, previewRunId);
                    updateComposerState();
                    return;
                }

                stopLiveSpeechPoll();

                const finalizingText = String(
                    speechSessionState && (speechSessionState.segmentText || speechSessionState.text) ||
                    "").trim();
                if (typeof controller.applySpeechLifecycleUpdate === "function") {
                    controller.applySpeechLifecycleUpdate({
                        stage: "stopped",
                        sessionId: state.sessionKey,
                        runId: speechRunId,
                        audioPath: String(speechSessionState && speechSessionState.audioPath || "").trim(),
                        audioArtifact: speechSessionState && speechSessionState.audioArtifact &&
                            typeof speechSessionState.audioArtifact === "object"
                            ? speechSessionState.audioArtifact
                            : null,
                        text: finalizingText,
                        errorCode: "",
                        errorMessage: "",
                        errorClass: "status",
                    });
                    updateComposerState();
                }

                const stopResponse = await controller.request("gateway.speech.stopRecording", {
                    sessionId: state.sessionKey,
                });
                const payload = stopResponse && typeof stopResponse.payload === "object"
                    ? stopResponse.payload
                    : {};
                const audioPath = String(payload.audioPath || "").trim();
                const audioArtifact = payload.audioArtifact && typeof payload.audioArtifact === "object"
                    ? payload.audioArtifact
                    : null;

                const finalRunId = `speech-final-${Date.now()}`;
                emitSpeechPreviewDiagnostic("speech.final.request_start", {
                    livePreviewOnly: false,
                    sessionId: state.sessionKey,
                    runId: finalRunId,
                    previewRunId: speechRunId,
                    stage: String(state.speechSessionState && state.speechSessionState.stage || ""),
                    audioArtifact: summarizeSpeechAudioArtifact(audioArtifact),
                    audioPath,
                });

                if (typeof controller.applySpeechLifecycleUpdate === "function") {
                    controller.applySpeechLifecycleUpdate({
                        stage: "stopped",
                        sessionId: state.sessionKey,
                        runId: finalRunId,
                        previousRunId: speechRunId,
                        finalRunId,
                        audioPath,
                        audioArtifact,
                        text: finalizingText,
                        errorCode: "",
                        errorMessage: "",
                        errorClass: "status",
                    });
                }

                if (!audioPath) {
                    addMessage("speech recording failed: no audio path returned", "error");
                    updateComposerState();
                    return;
                }

                const prompt = String(state.inputEl.value || "").trim();
                await controller.transcribeSpeech({
                    audioPath,
                    audioArtifact,
                    prompt,
                    runId: finalRunId,
                });
                if (typeof controller.getSpeechSessionStateSnapshot === "function") {
                    state.speechSessionState = controller.getSpeechSessionStateSnapshot();
                    emitSpeechPreviewDiagnostic("speech.final.request_end", {
                        livePreviewOnly: false,
                        sessionId: state.sessionKey,
                        runId: finalRunId,
                        stage: String(state.speechSessionState.stage || ""),
                        responseRunId: String(state.speechSessionState.runId || ""),
                        hasText: Boolean(state.speechSessionState.text || state.speechSessionState.segmentText),
                        audioArtifact: summarizeSpeechAudioArtifact(
                            state.speechSessionState.audioArtifact || audioArtifact),
                    });
                }
                updateComposerState();
            } catch (e) {
                stopLiveSpeechPoll();
                const message = String(e || "speech recording failed");
                addMessage(`speech recording error: ${message}`, "error");
                if (typeof controller.applySpeechLifecycleUpdate === "function") {
                    controller.applySpeechLifecycleUpdate({
                        stage: "failed",
                        errorCode: "recording_failed",
                        errorMessage: message,
                        errorClass: "toast",
                    });
                }
            } finally {
                recordingBusy = false;
                updateComposerState();
            }
        });
    }
    if (state.sessionSubscribeBtn) {
        state.sessionSubscribeBtn.addEventListener("click", () => {
            void controller.subscribeSessionUpdates().then(() => {
                syncSessionControlsPolling();
            });
        });
    }
    if (state.sessionUnsubscribeBtn) {
        state.sessionUnsubscribeBtn.addEventListener("click", () => {
            void controller.unsubscribeSessionUpdates().then(() => {
                syncSessionControlsPolling();
            });
        });
    }
    if (state.sessionCompactionRefreshBtn) {
        state.sessionCompactionRefreshBtn.addEventListener("click", () => {
            void controller.loadSessionCompactions();
        });
    }
    if (state.sessionCompactionSelect) {
        state.sessionCompactionSelect.addEventListener("change", () => {
            controller.selectSessionCompaction(state.sessionCompactionSelect.value);
        });
    }
    if (state.sessionCompactionBranchBtn) {
        state.sessionCompactionBranchBtn.addEventListener("click", () => {
            void controller.branchSessionCompaction({
                compactionId: state.sessionCompactionSelection,
            });
        });
    }
    if (state.sessionCompactionRestoreBtn) {
        state.sessionCompactionRestoreBtn.addEventListener("click", () => {
            void controller.restoreSessionCompaction({
                compactionId: state.sessionCompactionSelection,
            });
        });
    }

    if (resolveAssistantRegressionChecksEnabled() &&
        typeof window.BlazeClawChatController.runRegressionChecks === "function") {
        window.BlazeClawChatController.runRegressionChecks()
            .then((result) => {
                if (result && result.ok) {
                    console.log("[assistant-regression] passed:", result.checks);
                }
            })
            .catch((err) => {
                console.error("[assistant-regression] failed:", err);
            });
    }

    if (resolveAssistantRegressionChecksEnabled() &&
        typeof window.BlazeClawChatEvents.runRegressionChecks === "function") {
        window.BlazeClawChatEvents.runRegressionChecks()
            .then((result) => {
                if (result && result.ok) {
                    console.log("[assistant-events-regression] passed:", result.checks);
                }
            })
            .catch((err) => {
                console.error("[assistant-events-regression] failed:", err);
            });
    }

    function resolveAgentsEnabled() {
        const search = new URLSearchParams(window.location.search || "");
        const queryToggle = search.get("agents");
        if (queryToggle === "0") {
            return false;
        }
        if (queryToggle === "1") {
            return true;
        }

        try {
            if (window.localStorage) {
                const storedToggle = window.localStorage.getItem("blazeclaw.agents.enabled");
                if (storedToggle === "0") {
                    return false;
                }
                if (storedToggle === "1") {
                    return true;
                }
            }
        } catch (_) {
        }

        return true;
    }

    function resolveObservabilityEnabled() {
        const search = new URLSearchParams(window.location.search || "");
        const queryToggle = search.get("observability");
        if (queryToggle === "0") {
            return false;
        }
        if (queryToggle === "1") {
            return true;
        }
        try {
            if (window.localStorage) {
                const storedToggle = window.localStorage.getItem("blazeclaw.observability.enabled");
                if (storedToggle === "0") {
                    return false;
                }
                if (storedToggle === "1") {
                    return true;
                }
            }
        } catch (_) {
        }
        return false;
    }

    function resolveAgentsRegressionChecksEnabled() {
        const search = new URLSearchParams(window.location.search || "");
        if (search.get("agentsRegression") === "1") {
            return true;
        }

        try {
            return Boolean(
                window.localStorage &&
                window.localStorage.getItem("blazeclaw.agents.regression") === "1"
            );
        } catch (_) {
            return false;
        }
    }

    function resolveAssistantRegressionChecksEnabled() {
        const search = new URLSearchParams(window.location.search || "");
        if (search.get("assistantRegression") === "1") {
            return true;
        }

        try {
            return Boolean(
                window.localStorage &&
                window.localStorage.getItem("blazeclaw.assistant.regression") === "1"
            );
        } catch (_) {
            return false;
        }
    }

    function resolveStructuredTranscriptRenderEnabled() {
        const search = new URLSearchParams(window.location.search || "");
        const queryToggle = search.get("structuredTranscript");
        if (queryToggle === "0") {
            return false;
        }
        if (queryToggle === "1") {
            return true;
        }

        try {
            return Boolean(
                window.localStorage &&
                window.localStorage.getItem("blazeclaw.chat.structuredTranscript") === "1"
            );
        } catch (_) {
            return false;
        }
    }

    function resolveConfigCoerceEnabled() {
        const search = new URLSearchParams(window.location.search || "");
        const queryToggle = search.get("configCoerce");
        if (queryToggle === "0") {
            return false;
        }
        if (queryToggle === "1") {
            return true;
        }

        try {
            if (window.localStorage) {
                const storedToggle = window.localStorage.getItem("blazeclaw.config.coerce.enabled");
                if (storedToggle === "0") {
                    return false;
                }
                if (storedToggle === "1") {
                    return true;
                }
            }
        } catch (_) {
        }

        return true;
    }

    function loadAgentsPersistenceSnapshot() {
        try {
            if (!window.localStorage) {
                return null;
            }

            const raw = window.localStorage.getItem("blazeclaw.agents.persistence");
            if (!raw) {
                return null;
            }

            const parsed = JSON.parse(raw);
            if (!parsed || typeof parsed !== "object") {
                return null;
            }

            return {
                panel: String(parsed.panel || "overview"),
                selectedAgentId: typeof parsed.selectedAgentId === "string"
                    ? parsed.selectedAgentId
                    : null,
            };
        } catch (_) {
            return null;
        }
    }

    function persistAgentsSnapshot(snapshot) {
        try {
            if (!window.localStorage) {
                return;
            }

            window.localStorage.setItem(
                "blazeclaw.agents.persistence",
                JSON.stringify(snapshot || {})
            );
        } catch (_) {
        }
    }

    function emitAgentsTelemetry(eventName, payload) {
        try {
            console.log("[agents-control-plane]", eventName, payload || {});
        } catch (_) {
        }
    }

    let nodesPollingTimer = null;
    let nodesPollingInFlight = false;
    const NODES_POLLING_MS = 5000;
    let observabilityPollingTimer = null;
    let observabilityPollingInFlight = false;
    const OBSERVABILITY_POLLING_MS = 5000;
    let sessionControlsPollingTimer = null;
    let sessionControlsPollingInFlight = false;
    const SESSION_CONTROLS_POLLING_MS = 5000;

    function stopNodesPolling() {
        if (nodesPollingTimer !== null) {
            window.clearInterval(nodesPollingTimer);
            nodesPollingTimer = null;
        }
    }

    function shouldRunNodesPolling() {
        return Boolean(
            agentsController &&
            state.connected &&
            String(state.agentsPanel || "") === "nodes"
        );
    }

    function triggerNodesRefresh(options) {
        if (!agentsController || typeof agentsController.loadNodes !== "function") {
            return;
        }
        if (nodesPollingInFlight) {
            return;
        }

        nodesPollingInFlight = true;
        const opts = options || {};
        void agentsController.loadNodes(opts)
            .catch(() => {
            })
            .finally(() => {
                nodesPollingInFlight = false;
            });
    }

    function syncNodesPolling() {
        if (!shouldRunNodesPolling()) {
            stopNodesPolling();
            return;
        }

        if (nodesPollingTimer !== null) {
            return;
        }

        nodesPollingTimer = window.setInterval(() => {
            if (!shouldRunNodesPolling()) {
                stopNodesPolling();
                return;
            }

            triggerNodesRefresh({
                quiet: true,
            });
        }, NODES_POLLING_MS);
    }

    function stopObservabilityPolling() {
        if (observabilityPollingTimer !== null) {
            window.clearInterval(observabilityPollingTimer);
            observabilityPollingTimer = null;
        }
    }

    function shouldRunObservabilityPolling() {
        return Boolean(
            agentsController &&
            state.connected &&
            state.observabilityEnabled &&
            String(state.agentsPanel || "") === "observability"
        );
    }

    function triggerObservabilityRefresh(options) {
        if (!agentsController || typeof agentsController.loadObservability !== "function") {
            return;
        }
        if (observabilityPollingInFlight) {
            return;
        }
        observabilityPollingInFlight = true;
        const opts = options || {};
        void agentsController.loadObservability({
            quiet: opts.quiet === true,
            shouldIgnoreResponse: function () {
                return String(state.agentsPanel || "") !== "observability";
            },
        })
            .catch(() => {
            })
            .finally(() => {
                observabilityPollingInFlight = false;
            });
    }

    function syncObservabilityPolling() {
        if (!shouldRunObservabilityPolling()) {
            stopObservabilityPolling();
            return;
        }

        if (observabilityPollingTimer !== null) {
            return;
        }

        observabilityPollingTimer = window.setInterval(() => {
            if (!shouldRunObservabilityPolling()) {
                stopObservabilityPolling();
                return;
            }
            if (state.observabilityPaused) {
                return;
            }
            triggerObservabilityRefresh({
                quiet: true,
            });
        }, OBSERVABILITY_POLLING_MS);
    }

    function stopSessionControlsPolling() {
        if (sessionControlsPollingTimer !== null) {
            window.clearInterval(sessionControlsPollingTimer);
            sessionControlsPollingTimer = null;
        }
    }

    function shouldRunSessionControlsPolling() {
        return Boolean(
            state.connected &&
            state.bridgeAvailable &&
            state.sessionSubscribed
        );
    }

    function triggerSessionControlsRefresh(options) {
        if (sessionControlsPollingInFlight) {
            return;
        }
        sessionControlsPollingInFlight = true;
        const opts = options || {};
        void controller.refreshSessionControlState({
            quiet: opts.quiet === true,
        })
            .catch(() => {
            })
            .finally(() => {
                sessionControlsPollingInFlight = false;
            });
    }

    function syncSessionControlsPolling() {
        if (!shouldRunSessionControlsPolling()) {
            stopSessionControlsPolling();
            return;
        }
        if (sessionControlsPollingTimer !== null) {
            return;
        }
        sessionControlsPollingTimer = window.setInterval(() => {
            if (!shouldRunSessionControlsPolling()) {
                stopSessionControlsPolling();
                return;
            }
            triggerSessionControlsRefresh({
                quiet: true,
            });
        }, SESSION_CONTROLS_POLLING_MS);
    }

    state.configCoerceEnabled = resolveConfigCoerceEnabled();
    state.observabilityEnabled = resolveObservabilityEnabled();

    const agentsEnabled = resolveAgentsEnabled();
    const agentsController = agentsEnabled && window.BlazeClawAgentsController
        ? window.BlazeClawAgentsController.createAgentsController({
            state,
            request: (method, params) => controller.request(method, params),
            onStateUpdated: () => updateComposerState(),
        })
        : null;

    if (agentsController) {
        state.agentsPanel = "overview";
        const persisted = loadAgentsPersistenceSnapshot();
        if (persisted) {
            agentsController.applyPersistenceSnapshot(persisted);
            emitAgentsTelemetry("persistence.restored", persisted);
        }

        state.onSessionChanged = () => {
            agentsController.syncSessionContext({
                sessionKey: state.sessionKey,
            });
            emitAgentsTelemetry("session.changed", {
                sessionKey: state.sessionKey,
            });
            void agentsController.loadPanelDataForCurrentAgent();
            syncNodesPolling();
            syncObservabilityPolling();
        };
        state.onModelChanged = () => {
            agentsController.syncSessionContext({
                sessionKey: state.sessionKey,
            });
            emitAgentsTelemetry("model.changed", {
                model: state.selectedModel,
            });
            void agentsController.refreshFromConfigSnapshot();
            syncNodesPolling();
            syncObservabilityPolling();
        };
        agentsController.syncSessionContext({
            sessionKey: state.sessionKey,
        });

        state.onGatewayLifecycleChanged = (lifecycle) => {
            if (!lifecycle || !lifecycle.connected) {
                syncNodesPolling();
                syncObservabilityPolling();
                return;
            }

            void controller.loadSpeechCapabilities()
                .then((snapshot) => {
                    state.speechCapabilities = snapshot && typeof snapshot === "object"
                        ? snapshot
                        : state.speechCapabilities;
                    updateComposerState();
                })
                .catch(() => {
                });

            const activePanel = String(state.agentsPanel || "");
            if (activePanel === "nodes") {
                emitAgentsTelemetry("nodes.lifecycle.refresh", {
                    state: lifecycle.state,
                    wasConnected: Boolean(lifecycle.wasConnected),
                });
                triggerNodesRefresh({
                    quiet: true,
                });
                syncNodesPolling();
                return;
            }

            if (activePanel === "instances") {
                emitAgentsTelemetry("presence.lifecycle.refresh", {
                    state: lifecycle.state,
                    wasConnected: Boolean(lifecycle.wasConnected),
                });
                void agentsController.loadPresence({
                    quiet: true,
                    shouldIgnoreResponse: function () {
                        return String(state.agentsPanel || "") !== "instances";
                    },
                });
            }

            if (activePanel === "observability") {
                emitAgentsTelemetry("observability.lifecycle.refresh", {
                    state: lifecycle.state,
                    wasConnected: Boolean(lifecycle.wasConnected),
                });
                triggerObservabilityRefresh({
                    quiet: true,
                });
            }

            syncNodesPolling();
            syncObservabilityPolling();
        };

        if (resolveAgentsRegressionChecksEnabled() &&
            typeof window.BlazeClawAgentsController.runRegressionChecks === "function") {
            window.BlazeClawAgentsController.runRegressionChecks()
                .then((result) => {
                    if (result && result.ok) {
                        console.log("[agents-regression] passed:", result.checks);
                        emitAgentsTelemetry("regression.passed", {
                            checks: result.checks,
                        });
                    }
                })
                .catch((err) => {
                    console.error("[agents-regression] failed:", err);
                    emitAgentsTelemetry("regression.failed", {
                        error: String(err || ""),
                    });
                });
        }
    }

    const eventsModule = window.BlazeClawChatEvents.createEventsModule({
        state,
        controller,
        addMessage(text, kind) {
            if (typeof controller.appendChatBubble === "function") {
                controller.appendChatBubble(text, kind);
                return;
            }
            addMessage(text, kind);
        },
        appendToolLifecycleRow,
        setStatus,
        updateComposerState,
        finalizeStream,
        addOrReplaceStream,
        upsertApprovalToken,
        onNeedsApprovalEvent: scheduleNeedsApprovalQueueWatch,
    });

    if (typeof controller.setPolledEventsHandler === "function") {
        controller.setPolledEventsHandler(function (events) {
            eventsModule.handleChatEvents(Array.isArray(events) ? events : []);
        });
    }

    const composerModule = window.BlazeClawChatComposer.createComposerModule({
        state,
        controller,
        updateComposerState,
    });

    composerModule.bind();
    void controller.loadSessionCompactions();
    syncSessionControlsPolling();
    if (agentsController) {
        void agentsController.loadAgents().then(() => {
            if (state.agentsSelectedId) {
                persistAgentsSnapshot(agentsController.getPersistenceSnapshot());
                emitAgentsTelemetry("initial.agent.ready", {
                    agentId: state.agentsSelectedId,
                    panel: state.agentsPanel,
                });
                return agentsController.loadPanelDataForCurrentAgent();
            }
            return undefined;
        });
    }

    if (state.bridgeAvailable) {
        window.chrome.webview.addEventListener("message", (event) => {
            eventsModule.handleInboundMessage(event && event.data);
        });

        controller.post({ channel: "blazeclaw.gateway.lifecycle.subscribe" });
        controller.flushQueue();
        void controller.loadSpeechCapabilities()
            .then((snapshot) => {
                state.speechCapabilities = snapshot && typeof snapshot === "object"
                    ? snapshot
                    : state.speechCapabilities;
            })
            .finally(() => {
                updateComposerState();
            });
        setStatus("gateway: subscribing...");
        updateComposerState();
    } else {
        setStatus("bridge unavailable");
        updateComposerState();
    }
})();
