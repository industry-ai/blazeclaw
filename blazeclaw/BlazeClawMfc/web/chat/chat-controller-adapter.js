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

    function getLegacyControllerApi() {
        const api = window.BlazeClawChatController;
        if (!api || typeof api !== "object") {
            throw new Error("BlazeClawChatController unavailable");
        }
        return api;
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
                    // fall through to plain text
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

        if (methodName === "hasTerminalRun" ||
            methodName === "hasBufferedAssistantStream") {
            return false;
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

        if (methodName === "hasTerminalRun" ||
            methodName === "hasBufferedAssistantStream") {
            return Boolean(value);
        }

        if (methodName === "getStructuredTranscript") {
            return Array.isArray(value) ? value.slice() : [];
        }

        return value;
    }

    function invokeAdapterMethod(controllerImpl, methodName, args, mode) {
        const candidate = controllerImpl[methodName];
        if (typeof candidate !== "function") {
            if (mode === "run-loop-transcript") {
                return fallbackRunLoopTranscriptMethod(methodName, args);
            }
            return undefined;
        }

        try {
            const result = candidate(...args);
            if (mode === "run-loop-transcript") {
                return normalizeRunLoopTranscriptResult(methodName, result, args);
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

        if (typeof facade.getAdapterParitySnapshot !== "function") {
            facade.getAdapterParitySnapshot = function () {
                return {
                    adapterVersion: "step6.0",
                    adapterMode: "legacy+bridge-backed-low-risk+run-loop-transcript-guards",
                    bridgeBackedLowRiskMethods: BRIDGE_BACKED_LOW_RISK_METHODS.slice(),
                    runLoopTranscriptMethods: RUN_LOOP_TRANSCRIPT_PARITY_METHODS.slice(),
                };
            };
        }

        facade.__adapterVersion = "step6.0";
        facade.__adapterMode = "legacy+bridge-backed-low-risk+run-loop-transcript-guards";
        facade.__adapterBridgeBackedLowRiskMethods = BRIDGE_BACKED_LOW_RISK_METHODS.slice();
        facade.__adapterRunLoopTranscriptMethods = RUN_LOOP_TRANSCRIPT_PARITY_METHODS.slice();

        return facade;
    }

    function createController(options) {
        const legacyApi = getLegacyControllerApi();
        if (typeof legacyApi.createController !== "function") {
            throw new Error("BlazeClawChatController.createController unavailable");
        }
        const legacyController = legacyApi.createController(options);
        return createAdapterControllerFacade(legacyController);
    }

    async function runRegressionChecks() {
        const legacyApi = getLegacyControllerApi();
        if (typeof legacyApi.runRegressionChecks !== "function") {
            throw new Error("BlazeClawChatController.runRegressionChecks unavailable");
        }
        return legacyApi.runRegressionChecks();
    }

    window.BlazeClawChatControllerAdapter = {
        createController,
        runRegressionChecks,
    };
})();
