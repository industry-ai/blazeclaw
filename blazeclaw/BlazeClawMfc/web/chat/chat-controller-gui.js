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
        const groupedResponsesState = {
            promptOrder: [],
            promptGroups: new Map(),
            legacyStreamElement: null,
        };

        function normalizeTerminalState(value) {
            const normalized = String(value || "").trim().toLowerCase();
            if (normalized === "failed") {
                return "error";
            }
            return normalized;
        }

        function resolvePromptRunId(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const promptRunId = String(
                source.promptRunId ||
                source.parentRunId ||
                ""
            ).trim();
            if (promptRunId) {
                return promptRunId;
            }

            const runId = String(source.runId || source.responderRunId || "").trim();
            if (runId) {
                return `${runId}.prompt`;
            }

            return "";
        }

        function resolveResponderRunId(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const responderRunId = String(source.responderRunId || source.runId || "").trim();
            if (responderRunId) {
                return responderRunId;
            }

            const responderId = String(source.responderId || "").trim();
            if (responderId) {
                return `responder:${responderId}`;
            }

            return "";
        }

        function resolveResponderIdentity(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const responderId = String(source.responderId || source.responder || "").trim();
            const responderLabel = String(
                source.responderLabel ||
                source.label ||
                source.responderName ||
                responderId ||
                "Responder"
            ).trim() || "Responder";
            const orderNumber = Number(source.responderOrder);
            const responderOrder = Number.isFinite(orderNumber)
                ? orderNumber
                : Number.MAX_SAFE_INTEGER;
            return {
                responderId,
                responderLabel,
                responderOrder,
            };
        }

        function compareResponderCards(left, right) {
            const leftOrder = Number.isFinite(left && left.responderOrder)
                ? Number(left.responderOrder)
                : Number.MAX_SAFE_INTEGER;
            const rightOrder = Number.isFinite(right && right.responderOrder)
                ? Number(right.responderOrder)
                : Number.MAX_SAFE_INTEGER;
            if (leftOrder !== rightOrder) {
                return leftOrder - rightOrder;
            }

            const leftLabel = String(left && left.responderLabel || "").trim().toLowerCase();
            const rightLabel = String(right && right.responderLabel || "").trim().toLowerCase();
            if (leftLabel !== rightLabel) {
                return leftLabel.localeCompare(rightLabel);
            }

            const leftId = String(left && left.responderId || left && left.responderRunId || "").trim().toLowerCase();
            const rightId = String(right && right.responderId || right && right.responderRunId || "").trim().toLowerCase();
            return leftId.localeCompare(rightId);
        }

        function resolveResponderStatusClass(terminalState, hasDraft) {
            const stateName = normalizeTerminalState(terminalState);
            if (!stateName) {
                return hasDraft ? "streaming" : "pending";
            }
            if (stateName === "final" || stateName === "completed") {
                return "success";
            }
            if (stateName === "error" || stateName === "aborted") {
                return "failure";
            }
            if (stateName === "needs_approval") {
                return "approval";
            }
            return stateName;
        }

        function computeGroupOutcomeClass(group) {
            if (!group || !Array.isArray(group.cards) || group.cards.length === 0) {
                return "pending";
            }

            let success = 0;
            let failure = 0;
            let approval = 0;
            let pending = 0;
            for (const card of group.cards) {
                const statusClass = resolveResponderStatusClass(card.terminalState, card.hasDraft);
                if (statusClass === "success") {
                    success += 1;
                } else if (statusClass === "failure") {
                    failure += 1;
                } else if (statusClass === "approval") {
                    approval += 1;
                } else {
                    pending += 1;
                }
            }

            if ((success > 0 && (failure > 0 || approval > 0)) ||
                (failure > 0 && approval > 0)) {
                return "mixed";
            }
            if (failure > 0) {
                return "failure";
            }
            if (approval > 0) {
                return "approval";
            }
            if (pending > 0) {
                return "pending";
            }
            return success > 0 ? "success" : "pending";
        }

        function ensurePromptGroup(promptRunId, payload) {
            if (!elements.messagesEl || !promptRunId) {
                return null;
            }

            let group = groupedResponsesState.promptGroups.get(promptRunId);
            if (group) {
                return group;
            }

            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const responseMode = String(source.responseMode || "multi_active").trim() || "multi_active";

            const groupEl = document.createElement("div");
            groupEl.className = "multi-response-group pending";
            groupEl.setAttribute("data-prompt-run-id", promptRunId);

            const headerEl = document.createElement("div");
            headerEl.className = "multi-response-group-header";

            const titleEl = document.createElement("div");
            titleEl.className = "multi-response-group-title";
            titleEl.textContent = `Response group ${groupedResponsesState.promptOrder.length + 1}`;

            const badgeEl = document.createElement("span");
            badgeEl.className = "multi-response-group-badge";
            badgeEl.textContent = responseMode === "multi_active"
                ? "multi-active"
                : responseMode;

            const summaryEl = document.createElement("div");
            summaryEl.className = "multi-response-group-summary";
            summaryEl.textContent = "waiting responders";

            headerEl.appendChild(titleEl);
            headerEl.appendChild(badgeEl);
            headerEl.appendChild(summaryEl);

            const cardsEl = document.createElement("div");
            cardsEl.className = "multi-response-cards";

            groupEl.appendChild(headerEl);
            groupEl.appendChild(cardsEl);

            group = {
                promptRunId,
                responseMode,
                groupEl,
                summaryEl,
                cardsEl,
                cards: [],
                cardsByRunId: new Map(),
                completed: false,
                terminalState: "",
            };
            groupedResponsesState.promptGroups.set(promptRunId, group);
            groupedResponsesState.promptOrder.push(promptRunId);
            elements.messagesEl.appendChild(groupEl);
            scrollBottom();
            return group;
        }

        function syncResponderCardOrder(group) {
            if (!group || !group.cardsEl) {
                return;
            }
            group.cards.sort(compareResponderCards);
            for (const card of group.cards) {
                if (card && card.cardEl) {
                    group.cardsEl.appendChild(card.cardEl);
                }
            }
        }

        function updatePromptGroupSummary(group) {
            if (!group || !group.summaryEl) {
                return;
            }

            const total = group.cards.length;
            const completed = group.cards.reduce((sum, card) => sum + (card.completed ? 1 : 0), 0);
            const outcomeClass = computeGroupOutcomeClass(group);
            group.groupEl.className = `multi-response-group ${outcomeClass}`;
            if (group.completed) {
                const terminal = normalizeTerminalState(group.terminalState) || outcomeClass;
                group.summaryEl.textContent = `${completed}/${total} responders · group ${terminal}`;
                return;
            }
            group.summaryEl.textContent = `${completed}/${total} responders completed`;
        }

        function ensureResponderCard(group, payload) {
            if (!group) {
                return null;
            }

            const responderRunId = resolveResponderRunId(payload);
            if (!responderRunId) {
                return null;
            }

            let card = group.cardsByRunId.get(responderRunId);
            const identity = resolveResponderIdentity(payload);
            if (card) {
                if (identity.responderId) {
                    card.responderId = identity.responderId;
                }
                if (identity.responderLabel) {
                    card.responderLabel = identity.responderLabel;
                    if (card.labelEl) {
                        card.labelEl.textContent = identity.responderLabel;
                    }
                }
                if (Number.isFinite(identity.responderOrder) &&
                    identity.responderOrder !== Number.MAX_SAFE_INTEGER) {
                    card.responderOrder = identity.responderOrder;
                }
                syncResponderCardOrder(group);
                return card;
            }

            const cardEl = document.createElement("div");
            cardEl.className = "responder-card pending";
            cardEl.setAttribute("data-responder-run-id", responderRunId);

            const cardHeader = document.createElement("div");
            cardHeader.className = "responder-card-header";

            const labelEl = document.createElement("div");
            labelEl.className = "responder-card-label";
            labelEl.textContent = identity.responderLabel;

            const statusBadgeEl = document.createElement("span");
            statusBadgeEl.className = "responder-card-status pending";
            statusBadgeEl.textContent = "pending";

            cardHeader.appendChild(labelEl);
            cardHeader.appendChild(statusBadgeEl);

            const contentEl = document.createElement("div");
            contentEl.className = "responder-card-content";

            cardEl.appendChild(cardHeader);
            cardEl.appendChild(contentEl);

            card = {
                responderRunId,
                responderId: identity.responderId,
                responderLabel: identity.responderLabel,
                responderOrder: identity.responderOrder,
                terminalState: "",
                hasDraft: false,
                completed: false,
                cardEl,
                labelEl,
                statusBadgeEl,
                contentEl,
            };

            group.cardsByRunId.set(responderRunId, card);
            group.cards.push(card);
            syncResponderCardOrder(group);
            updatePromptGroupSummary(group);
            return card;
        }

        function updateResponderCardStatus(card, terminalState, hasDraft) {
            if (!card || !card.statusBadgeEl || !card.cardEl) {
                return;
            }

            const normalized = normalizeTerminalState(terminalState);
            const statusClass = resolveResponderStatusClass(normalized, hasDraft);
            const statusText = normalized || (hasDraft ? "streaming" : "pending");
            card.statusBadgeEl.className = `responder-card-status ${statusClass}`;
            card.statusBadgeEl.textContent = statusText;
            card.cardEl.className = `responder-card ${statusClass}`;
        }

        function addOrReplaceGroupedStream(text, metadata) {
            const source = metadata && typeof metadata === "object"
                ? metadata
                : {};
            const controller = callbacks.controllerProvider();
            const promptRunId = resolvePromptRunId(source);
            if (!promptRunId) {
                return false;
            }

            const group = ensurePromptGroup(promptRunId, source);
            const card = ensureResponderCard(group, source);
            if (!group || !card) {
                return false;
            }

            const nextText = String(text || "");
            const silentReply = controller && typeof controller.isSilentReplyText === "function"
                ? controller.isSilentReplyText(nextText)
                : false;
            if (nextText && !silentReply) {
                card.contentEl.textContent = nextText;
                callbacks.scanApprovalTokenFromText(nextText);
                callbacks.harvestApprovalTokensFromText(nextText);
            }
            card.hasDraft = true;
            card.completed = false;
            card.terminalState = "delta";
            updateResponderCardStatus(card, "delta", true);
            updatePromptGroupSummary(group);
            scrollBottom();
            return true;
        }

        function finalizeGroupedStream(metadata) {
            const source = metadata && typeof metadata === "object"
                ? metadata
                : {};
            const controller = callbacks.controllerProvider();
            const promptRunId = resolvePromptRunId(source);
            if (!promptRunId) {
                return false;
            }

            const group = ensurePromptGroup(promptRunId, source);
            const card = ensureResponderCard(group, source);
            if (!group || !card) {
                return false;
            }

            const terminalState = normalizeTerminalState(
                source.terminalState ||
                source.state ||
                "final"
            );
            const finalText = String(source.text || "");
            const silentReply = controller && typeof controller.isSilentReplyText === "function"
                ? controller.isSilentReplyText(finalText)
                : false;
            if (finalText && !silentReply) {
                card.contentEl.textContent = finalText;
                callbacks.scanApprovalTokenFromText(finalText);
                callbacks.harvestApprovalTokensFromText(finalText);
            }

            card.hasDraft = false;
            card.completed = true;
            card.terminalState = terminalState;
            updateResponderCardStatus(card, terminalState, false);
            updatePromptGroupSummary(group);
            scrollBottom();
            return true;
        }

        function completePromptGroup(metadata) {
            const source = metadata && typeof metadata === "object"
                ? metadata
                : {};
            const promptRunId = resolvePromptRunId(source);
            if (!promptRunId) {
                return;
            }
            const group = ensurePromptGroup(promptRunId, source);
            if (!group) {
                return;
            }

            group.completed = true;
            group.terminalState = normalizeTerminalState(source.terminalState || "completed");
            updatePromptGroupSummary(group);
        }

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
            groupedResponsesState.promptOrder = [];
            groupedResponsesState.promptGroups.clear();
            groupedResponsesState.legacyStreamElement = null;
        }

        function addOrReplaceStream(text, metadata) {
            const controller = callbacks.controllerProvider();
            if (!controller || !text || controller.isSilentReplyText(text)) {
                return;
            }

            if (!structuredTranscriptRenderEnabled && addOrReplaceGroupedStream(text, metadata)) {
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

            const existing = groupedResponsesState.legacyStreamElement || document.getElementById("stream-msg");
            if (existing) {
                existing.textContent = text;
                groupedResponsesState.legacyStreamElement = existing;
                scrollBottom();
                return;
            }
            const div = document.createElement("div");
            div.id = "stream-msg";
            div.className = "msg peer";
            div.textContent = text;
            elements.messagesEl.appendChild(div);
            groupedResponsesState.legacyStreamElement = div;
            scrollBottom();
        }

        function finalizeStream(metadata) {
            if (!structuredTranscriptRenderEnabled && finalizeGroupedStream(metadata)) {
                return;
            }

            if (structuredTranscriptRenderEnabled) {
                renderMessagesFromStructuredTranscript("");
                return;
            }
            const existing = groupedResponsesState.legacyStreamElement || document.getElementById("stream-msg");
            if (existing) {
                existing.removeAttribute("id");
                groupedResponsesState.legacyStreamElement = null;
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
            completePromptGroup,
            setInputValue,
        };
    }

    window.BlazeClawChatControllerGui = {
        createGuiModule,
    };
})();
