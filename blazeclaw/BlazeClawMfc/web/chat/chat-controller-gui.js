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

            const statusText = String(state.speechStatusText || "").trim();
            elements.speechStatusEl.textContent = statusText || "speech: unavailable";
        }

        function renderSpeechLivePreview() {
            if (!elements.speechLivePreviewEl ||
                !elements.speechLivePreviewLabelEl ||
                !elements.speechLivePreviewTextEl) {
                return;
            }

            const previewView =
                state.speechLivePreviewView && typeof state.speechLivePreviewView === "object"
                    ? state.speechLivePreviewView
                    : null;
            if (!previewView || previewView.visible !== true) {
                elements.speechLivePreviewEl.hidden = true;
                elements.speechLivePreviewEl.className = "speech-live-preview";
                elements.speechLivePreviewLabelEl.textContent = "";
                elements.speechLivePreviewTextEl.textContent = "";
                return;
            }

            const modeClass = String(previewView.modeClass || "status").trim() || "status";
            elements.speechLivePreviewEl.hidden = false;
            elements.speechLivePreviewEl.className = `speech-live-preview ${modeClass}`;
            elements.speechLivePreviewLabelEl.textContent = String(previewView.label || "speech");
            elements.speechLivePreviewTextEl.textContent = String(previewView.text || "Speak now");
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
                    if (item.failureGuideText) {
                        const guide = document.createElement("div");
                        guide.className = "meta";
                        guide.textContent = String(item.failureGuideText);
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
