(function () {
    function createUsageController(options) {
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
            throw new Error("usage-controller requires state and request");
        }

        const LEGACY_USAGE_DATE_PARAMS_MODE_RE = /unexpected property ['"]mode['"]/i;
        const LEGACY_USAGE_DATE_PARAMS_OFFSET_RE = /unexpected property ['"]utcoffset['"]/i;
        const LEGACY_USAGE_DATE_PARAMS_INVALID_RE = /invalid sessions\.usage params/i;

        function buildUsageDateBounds() {
            const now = new Date();
            const end = new Date(Date.UTC(
                now.getUTCFullYear(),
                now.getUTCMonth(),
                now.getUTCDate()
            ));
            const start = new Date(end.getTime() - 29 * 24 * 60 * 60 * 1000);
            function toIsoDate(value) {
                const year = value.getUTCFullYear();
                const month = String(value.getUTCMonth() + 1).padStart(2, "0");
                const day = String(value.getUTCDate()).padStart(2, "0");
                return year + "-" + month + "-" + day;
            }
            return {
                startDate: toIsoDate(start),
                endDate: toIsoDate(end),
            };
        }

        function formatUtcOffset(timezoneOffsetMinutes) {
            const offsetFromUtcMinutes = -timezoneOffsetMinutes;
            const sign = offsetFromUtcMinutes >= 0 ? "+" : "-";
            const absMinutes = Math.abs(offsetFromUtcMinutes);
            const hours = Math.floor(absMinutes / 60);
            const minutes = absMinutes % 60;
            return minutes === 0
                ? "UTC" + sign + String(hours)
                : "UTC" + sign + String(hours) + ":" + String(minutes).padStart(2, "0");
        }

        function buildUsageDateInterpretationParams(timeZone) {
            if (timeZone === "utc") {
                return {
                    mode: "utc",
                };
            }
            return {
                mode: "specific",
                utcOffset: formatUtcOffset(new Date().getTimezoneOffset()),
            };
        }

        function isLegacyDateInterpretationUnsupportedError(err) {
            const message = String((err && err.message) || err || "");
            return LEGACY_USAGE_DATE_PARAMS_INVALID_RE.test(message) &&
                (LEGACY_USAGE_DATE_PARAMS_MODE_RE.test(message) ||
                    LEGACY_USAGE_DATE_PARAMS_OFFSET_RE.test(message));
        }

        function shouldIgnoreUsageDetailResponse(shouldIgnoreResponse, sessionKey) {
            if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                return true;
            }
            const selectedPanel = String(state.agentsPanel || "");
            if (selectedPanel !== "usage") {
                return true;
            }
            const selectedSessions = Array.isArray(state.usageSelectedSessions)
                ? state.usageSelectedSessions
                : [];
            if (selectedSessions.length === 0) {
                return true;
            }
            return selectedSessions.indexOf(sessionKey) < 0;
        }

        async function runOptionalUsageDetailRequest(loadingKey, run, shouldIgnoreResponse) {
            if (!request || !state.connected || state[loadingKey]) {
                return;
            }

            state[loadingKey] = true;
            onStateUpdated();

            try {
                await run();
            } catch (_) {
                // Silently fail optional usage detail endpoints.
            } finally {
                state[loadingKey] = false;
                onStateUpdated();
            }
        }

        async function loadUsage(options) {
            const optsLoad = options || {};
            const quiet = Boolean(optsLoad.quiet);
            const shouldIgnoreResponse = typeof optsLoad.shouldIgnoreResponse === "function"
                ? optsLoad.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.usageLoading) {
                return state.usageResult;
            }

            state.usageLoading = true;
            if (!quiet) {
                state.usageError = null;
                state.lastError = null;
            }
            onStateUpdated();

            try {
                if (!state.usageStartDate || !state.usageEndDate) {
                    const bounds = buildUsageDateBounds();
                    state.usageStartDate = bounds.startDate;
                    state.usageEndDate = bounds.endDate;
                }

                const startDate = state.usageStartDate;
                const endDate = state.usageEndDate;
                async function runUsageRequests(includeDateInterpretation) {
                    const dateInterpretation = includeDateInterpretation
                        ? buildUsageDateInterpretationParams(state.usageTimeZone)
                        : undefined;
                    return Promise.all([
                        request("sessions.usage", {
                            startDate,
                            endDate,
                            limit: 1000,
                            includeContextWeight: true,
                            ...(dateInterpretation || {}),
                        }),
                        request("usage.cost", {
                            startDate,
                            endDate,
                            ...(dateInterpretation || {}),
                        }),
                    ]);
                }

                const includeDateInterpretation = true;
                let sessionsRes = null;
                let costRes = null;
                try {
                    const first = await runUsageRequests(includeDateInterpretation);
                    sessionsRes = first[0];
                    costRes = first[1];
                } catch (firstErr) {
                    if (includeDateInterpretation && isLegacyDateInterpretationUnsupportedError(firstErr)) {
                        const fallback = await runUsageRequests(false);
                        sessionsRes = fallback[0];
                        costRes = fallback[1];
                    } else {
                        throw firstErr;
                    }
                }

                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.usageResult;
                }

                state.usageResult = sessionsRes && sessionsRes.payload
                    ? sessionsRes.payload
                    : sessionsRes;
                state.usageCostSummary = costRes && costRes.payload
                    ? costRes.payload
                    : costRes;
                if (!quiet) {
                    state.usageError = null;
                }
                return state.usageResult;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.usageResult;
                }

                if (!quiet) {
                    state.usageResult = null;
                    state.usageCostSummary = null;
                    state.usageError = resolveToolsErrorMessage(err, "usage");
                    state.lastError = state.usageError;
                }
                return state.usageResult;
            } finally {
                state.usageLoading = false;
                onStateUpdated();
            }
        }

        async function loadUsageTimeSeries(sessionKey, options) {
            const key = String(sessionKey || "").trim();
            const optsLoad = options || {};
            const shouldIgnoreResponse = typeof optsLoad.shouldIgnoreResponse === "function"
                ? optsLoad.shouldIgnoreResponse
                : null;
            if (!key) {
                return;
            }

            await runOptionalUsageDetailRequest(
                "usageTimeSeriesLoading",
                async function () {
                    state.usageTimeSeries = null;
                    onStateUpdated();
                    const res = await request("sessions.usage.timeseries", {
                        key,
                    });
                    if (shouldIgnoreUsageDetailResponse(shouldIgnoreResponse, key)) {
                        return;
                    }

                    const payload = res && res.payload ? res.payload : res;
                    state.usageTimeSeries = payload && typeof payload === "object"
                        ? payload
                        : null;
                },
                shouldIgnoreResponse
            );
        }

        async function loadUsageSessionLogs(sessionKey, options) {
            const key = String(sessionKey || "").trim();
            const optsLoad = options || {};
            const shouldIgnoreResponse = typeof optsLoad.shouldIgnoreResponse === "function"
                ? optsLoad.shouldIgnoreResponse
                : null;
            if (!key) {
                return;
            }

            await runOptionalUsageDetailRequest(
                "usageSessionLogsLoading",
                async function () {
                    state.usageSessionLogs = null;
                    onStateUpdated();
                    const res = await request("sessions.usage.logs", {
                        key,
                        limit: 1000,
                    });
                    if (shouldIgnoreUsageDetailResponse(shouldIgnoreResponse, key)) {
                        return;
                    }

                    const payload = res && res.payload ? res.payload : res;
                    const logs = payload && payload.logs;
                    state.usageSessionLogs = Array.isArray(logs)
                        ? logs
                        : null;
                },
                shouldIgnoreResponse
            );
        }

        return {
            buildUsageDateBounds,
            formatUtcOffset,
            buildUsageDateInterpretationParams,
            isLegacyDateInterpretationUnsupportedError,
            shouldIgnoreUsageDetailResponse,
            runOptionalUsageDetailRequest,
            loadUsage,
            loadUsageTimeSeries,
            loadUsageSessionLogs,
        };
    }

    window.BlazeClawUsageController = {
        createUsageController,
    };
})();
