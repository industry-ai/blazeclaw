(function () {
    function createObservabilityController(options) {
        const opts = options || {};
        const state = opts.state;
        const request = typeof opts.request === "function" ? opts.request : null;
        const onStateUpdated = typeof opts.onStateUpdated === "function"
            ? opts.onStateUpdated
            : function () {
            };
        const resolveToolsErrorMessage =
            typeof opts.resolveToolsErrorMessage === "function"
                ? opts.resolveToolsErrorMessage
                : function (err) {
                    return String(err);
                };

        if (!state || !request) {
            throw new Error("observability-controller requires state and request");
        }

        function parseObservabilityMethodParams(raw) {
            const text = String(raw || "").trim();
            if (!text) {
                return {};
            }
            const parsed = JSON.parse(text);
            if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
                throw new Error("method params must be a JSON object");
            }
            return parsed;
        }

        function normalizeObservabilityLogs(entries, level) {
            const list = Array.isArray(entries) ? entries : [];
            const targetLevel = String(level || "all").trim().toLowerCase();
            return list.filter(function (entry) {
                if (!entry || typeof entry !== "object") {
                    return false;
                }
                if (targetLevel === "all") {
                    return true;
                }
                const entryLevel = String(entry.level || "").trim().toLowerCase();
                return entryLevel === targetLevel;
            });
        }

        async function loadObservability(options) {
            const opts = options || {};
            const quiet = Boolean(opts.quiet);
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.observabilityLoading || !state.observabilityEnabled) {
                return state.observabilityHealth;
            }

            state.observabilityLoading = true;
            if (!quiet) {
                state.observabilityError = null;
                state.lastError = null;
            }
            onStateUpdated();

            try {
                const logLimit = Math.max(1, Math.min(200, Number(state.observabilityLogLimit) || 50));
                const calls = await Promise.all([
                    request("gateway.health", {}),
                    request("gateway.health.details", {}),
                    request("gateway.transport.status", {}),
                    request("last-heartbeat", {}),
                    request("models.list", {}),
                    state.observabilityPaused
                        ? Promise.resolve({ payload: { entries: state.observabilityLogs || [] } })
                        : request("gateway.logs.tail", { limit: logLimit }),
                ]);

                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.observabilityHealth;
                }

                const healthPayload = calls[0] && calls[0].payload ? calls[0].payload : calls[0];
                const detailsPayload = calls[1] && calls[1].payload ? calls[1].payload : calls[1];
                const transportPayload = calls[2] && calls[2].payload ? calls[2].payload : calls[2];
                const heartbeatPayload = calls[3] && calls[3].payload ? calls[3].payload : calls[3];
                const modelsPayload = calls[4] && calls[4].payload ? calls[4].payload : calls[4];
                const logsPayload = calls[5] && calls[5].payload ? calls[5].payload : calls[5];
                const models = Array.isArray(modelsPayload && modelsPayload.models)
                    ? modelsPayload.models
                    : [];
                const entries = Array.isArray(logsPayload && logsPayload.entries)
                    ? logsPayload.entries
                    : [];

                state.observabilityHealth = healthPayload && typeof healthPayload === "object"
                    ? healthPayload
                    : null;
                state.observabilityHealthDetails = detailsPayload && typeof detailsPayload === "object"
                    ? detailsPayload
                    : null;
                state.observabilityTransportStatus = transportPayload && typeof transportPayload === "object"
                    ? transportPayload
                    : null;
                state.observabilityHeartbeat = heartbeatPayload && typeof heartbeatPayload === "object"
                    ? heartbeatPayload
                    : null;
                state.observabilityModels = models;
                state.observabilityLogs = normalizeObservabilityLogs(entries, state.observabilityLogLevel);
                state.observabilityLastUpdatedMs = Date.now();
                if (!quiet) {
                    state.observabilityError = null;
                }
                return state.observabilityHealth;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.observabilityHealth;
                }

                if (!quiet) {
                    state.observabilityError = resolveToolsErrorMessage(err, "observability");
                    state.lastError = state.observabilityError;
                }
                return state.observabilityHealth;
            } finally {
                state.observabilityLoading = false;
                onStateUpdated();
            }
        }

        function updateObservabilityField(field, value) {
            const key = String(field || "").trim();
            if (!key) {
                return;
            }

            if (key === "method") {
                state.observabilityMethod = String(value || "");
            } else if (key === "params") {
                state.observabilityMethodParams = String(value || "");
            } else if (key === "logLevel") {
                state.observabilityLogLevel = String(value || "all").trim().toLowerCase() || "all";
            } else if (key === "logLimit") {
                const nextLimit = Number(value);
                if (Number.isFinite(nextLimit) && nextLimit > 0) {
                    state.observabilityLogLimit = Math.max(1, Math.min(200, Math.floor(nextLimit)));
                }
            } else if (key === "paused") {
                state.observabilityPaused = Boolean(value);
            }
            onStateUpdated();
        }

        async function invokeObservabilityMethod(options) {
            const opts = options || {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            if (!requestOverride || state.observabilityMethodBusy || !state.observabilityEnabled) {
                return null;
            }

            const method = String(state.observabilityMethod || "").trim();
            if (!method) {
                state.observabilityMethodError = "method is required";
                state.observabilityMethodResult = null;
                onStateUpdated();
                return null;
            }

            state.observabilityMethodBusy = true;
            state.observabilityMethodError = null;
            onStateUpdated();
            try {
                const params = parseObservabilityMethodParams(state.observabilityMethodParams);
                const response = await requestOverride(method, params);
                state.observabilityMethodResult = JSON.stringify(response, null, 2);
                state.observabilityMethodError = null;
                return response;
            } catch (err) {
                state.observabilityMethodResult = null;
                state.observabilityMethodError = resolveToolsErrorMessage(err, "debug method invoke");
                return null;
            } finally {
                state.observabilityMethodBusy = false;
                onStateUpdated();
            }
        }

        function buildObservabilityLogsExportText() {
            const rows = Array.isArray(state.observabilityLogs) ? state.observabilityLogs : [];
            return rows.map(function (entry) {
                const ts = Number(entry && entry.ts || 0);
                const level = String(entry && entry.level || "info");
                const source = String(entry && entry.source || "gateway");
                const message = String(entry && entry.message || "");
                return ts + "\t" + level + "\t" + source + "\t" + message;
            }).join("\n");
        }

        function exportObservabilityLogs() {
            state.observabilityExportText = buildObservabilityLogsExportText();
            onStateUpdated();
            return state.observabilityExportText;
        }

        return {
            parseObservabilityMethodParams,
            normalizeObservabilityLogs,
            loadObservability,
            updateObservabilityField,
            invokeObservabilityMethod,
            exportObservabilityLogs,
        };
    }

    window.BlazeClawObservabilityController = {
        createObservabilityController,
    };
})();
