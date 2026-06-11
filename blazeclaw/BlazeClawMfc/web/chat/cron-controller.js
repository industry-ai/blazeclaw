(function () {
    function createCronController(options) {
        const opts = options || {};
        const state = opts.state;
        const request = typeof opts.request === "function" ? opts.request : null;
        const onStateUpdated = typeof opts.onStateUpdated === "function"
            ? opts.onStateUpdated
            : function () {
            };
        const hasSelectedAgentMismatch =
            typeof opts.hasSelectedAgentMismatch === "function"
                ? opts.hasSelectedAgentMismatch
                : function () {
                    return false;
                };
        const resolveToolsErrorMessage =
            typeof opts.resolveToolsErrorMessage === "function"
                ? opts.resolveToolsErrorMessage
                : function (err) {
                    return String(err);
                };

        const normalizeCronPaginationMeta =
            typeof opts.normalizeCronPaginationMeta === "function"
                ? opts.normalizeCronPaginationMeta
                : function (payload, entriesLength, fallbackLimit, fallbackOffset) {
                    const safePayload = payload && typeof payload === "object"
                        ? payload
                        : {};
                    const totalRaw = Number(safePayload.total);
                    const limitRaw = Number(safePayload.limit);
                    const offsetRaw = Number(safePayload.offset);
                    const hasMoreRaw = safePayload.hasMore;
                    const nextOffsetRaw = safePayload.nextOffset;

                    const total = Number.isFinite(totalRaw)
                        ? Math.max(0, Math.floor(totalRaw))
                        : entriesLength;
                    const limit = Number.isFinite(limitRaw) && limitRaw > 0
                        ? Math.floor(limitRaw)
                        : fallbackLimit;
                    const offset = Number.isFinite(offsetRaw) && offsetRaw >= 0
                        ? Math.floor(offsetRaw)
                        : fallbackOffset;
                    const hasMore = typeof hasMoreRaw === "boolean"
                        ? hasMoreRaw
                        : offset + entriesLength < Math.max(total, offset + entriesLength);
                    const nextOffset = Number.isFinite(Number(nextOffsetRaw))
                        ? Math.max(0, Math.floor(Number(nextOffsetRaw)))
                        : hasMore
                            ? offset + entriesLength
                            : null;

                    return {
                        total,
                        limit,
                        offset,
                        hasMore,
                        nextOffset,
                    };
                };
        const normalizeCronJob = typeof opts.normalizeCronJob === "function"
            ? opts.normalizeCronJob
            : function (entry) {
                return entry && typeof entry === "object"
                    ? entry
                    : null;
            };
        const normalizeCronFormState = typeof opts.normalizeCronFormState === "function"
            ? opts.normalizeCronFormState
            : function (form) {
                return form && typeof form === "object"
                    ? form
                    : {};
            };
        const validateCronForm = typeof opts.validateCronForm === "function"
            ? opts.validateCronForm
            : function () {
                return {};
            };
        const hasCronFormErrors = typeof opts.hasCronFormErrors === "function"
            ? opts.hasCronFormErrors
            : function () {
                return false;
            };
        const buildCronSchedule = typeof opts.buildCronSchedule === "function"
            ? opts.buildCronSchedule
            : function () {
                return {};
            };
        const buildCronPayloadWithToolParity =
            typeof opts.buildCronPayloadWithToolParity === "function"
                ? opts.buildCronPayloadWithToolParity
                : async function () {
                    return {};
                };
        const buildCronDelivery = typeof opts.buildCronDelivery === "function"
            ? opts.buildCronDelivery
            : function () {
                return {};
            };
        const buildCronFailureAlert = typeof opts.buildCronFailureAlert === "function"
            ? opts.buildCronFailureAlert
            : function () {
                return undefined;
            };
        const applyCronToolParityToMutationPayload =
            typeof opts.applyCronToolParityToMutationPayload === "function"
                ? opts.applyCronToolParityToMutationPayload
                : function (payload) {
                    return payload;
                };
        const recoverCronFlatJobShape = typeof opts.recoverCronFlatJobShape === "function"
            ? opts.recoverCronFlatJobShape
            : function () {
                return null;
            };
        const buildCronJobIdentityParams = typeof opts.buildCronJobIdentityParams === "function"
            ? opts.buildCronJobIdentityParams
            : function (jobId) {
                const resolved = String(jobId || "").trim();
                return resolved
                    ? { id: resolved, jobId: resolved }
                    : {};
            };
        const resetCronFormToDefaults = typeof opts.resetCronFormToDefaults === "function"
            ? opts.resetCronFormToDefaults
            : function () {
            };
        const jobToForm = typeof opts.jobToForm === "function"
            ? opts.jobToForm
            : function (job) {
                return job && typeof job === "object"
                    ? job
                    : {};
            };
        const buildCloneName = typeof opts.buildCloneName === "function"
            ? opts.buildCloneName
            : function (name) {
                return String(name || "Job") + " copy";
            };
        const normalizeLowercaseStringOrEmpty =
            typeof opts.normalizeLowercaseStringOrEmpty === "function"
                ? opts.normalizeLowercaseStringOrEmpty
                : function (value) {
                    return String(value || "").trim().toLowerCase();
                };

        if (!state || !request) {
            throw new Error("cron-controller requires state and request");
        }

        async function loadCronModelSuggestions(options) {
            const localOpts = options || {};
            const shouldIgnoreResponse = typeof localOpts.shouldIgnoreResponse === "function"
                ? localOpts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected) {
                state.agentCronModelSuggestions = [];
                return [];
            }

            try {
                const res = await request("models.list", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return [];
                }

                const payload = res && res.payload ? res.payload : res;
                const models = payload && Array.isArray(payload.models)
                    ? payload.models
                    : [];
                const unique = new Set();
                models.forEach(function (entry) {
                    const id = String(entry && entry.id || "").trim();
                    if (id) {
                        unique.add(id);
                    }
                });
                state.agentCronModelSuggestions = Array.from(unique).sort(function (left, right) {
                    return left.localeCompare(right);
                });
                return state.agentCronModelSuggestions;
            } catch (_) {
                state.agentCronModelSuggestions = [];
                return [];
            }
        }

        async function withCronBusy(run) {
            if (!request || !state.connected || state.agentCronBusy) {
                return false;
            }

            state.agentCronBusy = true;
            state.agentCronError = null;
            onStateUpdated();

            try {
                await run();
                return true;
            } catch (err) {
                state.agentCronError = resolveToolsErrorMessage(err, "cron mutation");
                return false;
            } finally {
                state.agentCronBusy = false;
                onStateUpdated();
            }
        }

        async function addOrUpdateCronJob() {
            return withCronBusy(async function () {
                const normalizedForm = normalizeCronFormState(state.agentCronForm);
                state.agentCronForm = normalizedForm;
                const fieldErrors = validateCronForm(normalizedForm);
                state.agentCronFieldErrors = fieldErrors;
                if (hasCronFormErrors(fieldErrors)) {
                    return;
                }

                const payload = {
                    name: String(normalizedForm.name || "").trim(),
                    enabled: normalizedForm.enabled !== false,
                    schedule: buildCronSchedule(normalizedForm),
                    payload: await buildCronPayloadWithToolParity(normalizedForm),
                    delivery: buildCronDelivery(normalizedForm),
                };

                const failureAlert = buildCronFailureAlert(normalizedForm);
                if (failureAlert !== undefined) {
                    payload.failureAlert = failureAlert;
                }

                const normalizedPayload = applyCronToolParityToMutationPayload(payload);

                if (state.agentCronEditingJobId) {
                    await request("cron.update", Object.assign(
                        buildCronJobIdentityParams(state.agentCronEditingJobId),
                        {
                            patch: recoverCronFlatJobShape(normalizedPayload) || normalizedPayload,
                        }
                    ));
                    state.agentCronEditingJobId = null;
                } else {
                    await request("cron.add", recoverCronFlatJobShape(normalizedPayload) || normalizedPayload);
                    resetCronFormToDefaults();
                }

                await loadCronJobsPage({ append: false });
                await loadCronStatus({});
                await loadCronRuns({ append: false });
            });
        }

        async function triggerCronWake(mode) {
            const resolvedMode = mode === "next-heartbeat"
                ? "next-heartbeat"
                : "now";

            return withCronBusy(async function () {
                await request("wake", {
                    mode: resolvedMode,
                });
                await loadCronStatus({});
                await loadCronRuns({ append: false });
            });
        }

        async function removeCronJob(jobId) {
            const resolvedJobId = String(jobId || "").trim();
            if (!resolvedJobId) {
                return false;
            }

            return withCronBusy(async function () {
                await request("cron.remove", buildCronJobIdentityParams(resolvedJobId));

                if (state.agentCronEditingJobId === resolvedJobId) {
                    resetCronFormToDefaults();
                }
                if (state.agentCronSelectedJobId === resolvedJobId) {
                    state.agentCronSelectedJobId = null;
                }

                await loadCronJobsPage({ append: false });
                await loadCronStatus({});
                await loadCronRuns({ append: false });
            });
        }

        async function runCronJobNow(jobId, mode) {
            const resolvedJobId = String(jobId || "").trim();
            if (!resolvedJobId) {
                return false;
            }

            const resolvedMode = mode === "due" ? "due" : "force";
            return withCronBusy(async function () {
                await request("cron.run", Object.assign(
                    buildCronJobIdentityParams(resolvedJobId),
                    { mode: resolvedMode }
                ));
                await loadCronRuns({ append: false });
            });
        }

        function startCronEdit(jobId) {
            const resolvedJobId = String(jobId || "").trim();
            const jobs = Array.isArray(state.agentCronJobs) ? state.agentCronJobs : [];
            const job = jobs.find(function (entry) {
                return String(entry && entry.id || "").trim() === resolvedJobId;
            });
            if (!job) {
                return false;
            }

            state.agentCronEditingJobId = resolvedJobId;
            state.agentCronSelectedJobId = resolvedJobId;
            state.agentCronForm = jobToForm(job, state.agentCronForm);
            state.agentCronFieldErrors = validateCronForm(state.agentCronForm);
            onStateUpdated();
            return true;
        }

        function startCronClone(jobId) {
            const resolvedJobId = String(jobId || "").trim();
            const jobs = Array.isArray(state.agentCronJobs) ? state.agentCronJobs : [];
            const job = jobs.find(function (entry) {
                return String(entry && entry.id || "").trim() === resolvedJobId;
            });
            if (!job) {
                return false;
            }

            const existingNames = new Set(
                jobs.map(function (entry) {
                    return normalizeLowercaseStringOrEmpty(entry && entry.name || "");
                })
            );
            const cloned = jobToForm(job, state.agentCronForm);
            cloned.name = buildCloneName(job.name, existingNames);
            state.agentCronEditingJobId = null;
            state.agentCronSelectedJobId = resolvedJobId;
            state.agentCronForm = cloned;
            state.agentCronFieldErrors = validateCronForm(state.agentCronForm);
            onStateUpdated();
            return true;
        }

        function cancelCronEdit() {
            resetCronFormToDefaults();
            onStateUpdated();
        }

        function updateCronFormField(field, value) {
            const allowedFields = {
                name: true,
                enabled: true,
                scheduleKind: true,
                scheduleAt: true,
                everyAmount: true,
                everyUnit: true,
                cronExpr: true,
                payloadKind: true,
                payloadText: true,
                payloadModel: true,
                payloadThinking: true,
                timeoutSeconds: true,
                contextMessages: true,
                deliveryMode: true,
                deliveryTo: true,
                failureAlertMode: true,
                failureAlertAfter: true,
                failureAlertCooldownSeconds: true,
            };
            const key = String(field || "").trim();
            if (!allowedFields[key]) {
                return;
            }

            const nextForm = Object.assign({}, state.agentCronForm);
            nextForm[key] = value;
            state.agentCronForm = normalizeCronFormState(nextForm);
            state.agentCronFieldErrors = validateCronForm(state.agentCronForm);
            onStateUpdated();
        }

        async function loadCronStatus(options) {
            const localOpts = options || {};
            const shouldIgnoreResponse = typeof localOpts.shouldIgnoreResponse === "function"
                ? localOpts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.agentCronStatusLoading) {
                return state.agentCronStatusResult;
            }

            state.agentCronStatusLoading = true;
            state.agentCronStatusError = null;
            onStateUpdated();

            try {
                const res = await request("cron.status", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                const payload = res && res.payload ? res.payload : res;
                const normalizedStatus = payload && typeof payload === "object"
                    ? {
                        enabled: Boolean(payload.enabled),
                        jobs: Number.isFinite(Number(payload.jobs))
                            ? Math.max(0, Math.floor(Number(payload.jobs)))
                            : 0,
                        nextWakeAtMs: Number.isFinite(Number(payload.nextWakeAtMs))
                            ? Math.floor(Number(payload.nextWakeAtMs))
                            : null,
                    }
                    : {
                        enabled: false,
                        jobs: 0,
                        nextWakeAtMs: null,
                    };
                state.agentCronStatusResult = normalizedStatus;
                return normalizedStatus;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                state.agentCronStatusError = resolveToolsErrorMessage(err, "cron status");
                return null;
            } finally {
                state.agentCronStatusLoading = false;
                onStateUpdated();
            }
        }

        async function loadCronJobsPage(options) {
            const localOpts = options || {};
            const shouldIgnoreResponse = typeof localOpts.shouldIgnoreResponse === "function"
                ? localOpts.shouldIgnoreResponse
                : null;
            const append = Boolean(localOpts.append);
            if (!request || !state.connected) {
                return;
            }
            if (!append && state.agentCronJobsLoading) {
                return;
            }
            if (append && state.agentCronJobsLoadingMore) {
                return;
            }
            if (append && !state.agentCronJobsHasMore) {
                return;
            }

            const offset = append
                ? Math.max(0, Number(state.agentCronJobsNextOffset || state.agentCronJobs.length || 0))
                : 0;
            if (append) {
                state.agentCronJobsLoadingMore = true;
            } else {
                state.agentCronJobsLoading = true;
            }
            state.agentCronJobsError = null;
            onStateUpdated();

            try {
                const res = await request("cron.list", {
                    includeDisabled: state.agentCronJobsEnabledFilter === "all",
                    enabled: state.agentCronJobsEnabledFilter,
                    limit: state.agentCronJobsLimit,
                    offset: offset,
                    query: String(state.agentCronJobsQuery || "").trim() || undefined,
                    sortBy: state.agentCronJobsSortBy,
                    sortDir: state.agentCronJobsSortDir,
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : res;
                const jobs = payload && Array.isArray(payload.jobs)
                    ? payload.jobs.map(normalizeCronJob).filter(function (job) {
                        return job && String(job.id || "").trim().length > 0;
                    })
                    : [];
                state.agentCronJobs = append
                    ? state.agentCronJobs.concat(jobs)
                    : jobs;

                const meta = normalizeCronPaginationMeta(
                    payload,
                    jobs.length,
                    state.agentCronJobsLimit,
                    offset
                );
                state.agentCronJobsTotal = Math.max(meta.total, state.agentCronJobs.length);
                state.agentCronJobsHasMore = meta.hasMore;
                state.agentCronJobsNextOffset = meta.nextOffset;
                if (state.agentCronEditingJobId && !state.agentCronJobs.some(function (job) {
                    return String(job.id || "").trim() === String(state.agentCronEditingJobId || "").trim();
                })) {
                    resetCronFormToDefaults();
                }
                if (state.agentCronSelectedJobId && !state.agentCronJobs.some(function (job) {
                    return String(job.id || "").trim() === String(state.agentCronSelectedJobId || "").trim();
                })) {
                    state.agentCronSelectedJobId = null;
                }
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                state.agentCronJobsError = resolveToolsErrorMessage(err, "cron jobs");
            } finally {
                if (append) {
                    state.agentCronJobsLoadingMore = false;
                } else {
                    state.agentCronJobsLoading = false;
                }
                onStateUpdated();
            }
        }

        async function loadCronRuns(options) {
            const localOpts = options || {};
            const shouldIgnoreResponse = typeof localOpts.shouldIgnoreResponse === "function"
                ? localOpts.shouldIgnoreResponse
                : null;
            const append = Boolean(localOpts.append);
            if (!request || !state.connected) {
                return;
            }
            if (!append && state.agentCronRunsLoading) {
                return;
            }
            if (append && state.agentCronRunsLoadingMore) {
                return;
            }
            if (append && !state.agentCronRunsHasMore) {
                return;
            }

            const offset = append
                ? Math.max(0, Number(state.agentCronRunsNextOffset || state.agentCronRuns.length || 0))
                : 0;
            if (append) {
                state.agentCronRunsLoadingMore = true;
            } else {
                state.agentCronRunsLoading = true;
            }
            state.agentCronRunsError = null;
            onStateUpdated();

            try {
                const requestedScope = state.agentCronRunsScope === "job"
                    ? "job"
                    : "all";
                const selectedJobId = String(state.agentCronSelectedJobId || "").trim();
                const scope = requestedScope === "job" && selectedJobId
                    ? "job"
                    : "all";
                const runsParams = Object.assign(
                    {
                        scope: scope,
                        limit: state.agentCronRunsLimit,
                        offset: offset,
                        status: state.agentCronRunsStatusFilter,
                        query: String(state.agentCronRunsQuery || "").trim() || undefined,
                        sortDir: state.agentCronRunsSortDir,
                    },
                    scope === "job" && selectedJobId
                        ? buildCronJobIdentityParams(selectedJobId)
                        : {}
                );
                const res = await request("cron.runs", runsParams);
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : res;
                const entries = payload && Array.isArray(payload.entries)
                    ? payload.entries
                    : [];
                const scopeIsJob = scope === "job" && selectedJobId;
                state.agentCronRuns = append && (!scopeIsJob || selectedJobId)
                    ? state.agentCronRuns.concat(entries)
                    : entries;

                const meta = normalizeCronPaginationMeta(
                    payload,
                    entries.length,
                    state.agentCronRunsLimit,
                    offset
                );
                state.agentCronRunsTotal = Math.max(meta.total, state.agentCronRuns.length);
                state.agentCronRunsHasMore = meta.hasMore;
                state.agentCronRunsNextOffset = meta.nextOffset;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                state.agentCronRunsError = resolveToolsErrorMessage(err, "cron runs");
            } finally {
                if (append) {
                    state.agentCronRunsLoadingMore = false;
                } else {
                    state.agentCronRunsLoading = false;
                }
                onStateUpdated();
            }
        }

        async function loadAgentCron(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!request || !state.connected) {
                return;
            }

            function shouldIgnoreResponse() {
                if (state.agentsPanel !== "cron") {
                    return true;
                }
                if (resolvedAgentId) {
                    return hasSelectedAgentMismatch(resolvedAgentId);
                }
                return false;
            }

            state.agentCronLoading = true;
            state.agentCronError = null;
            onStateUpdated();

            try {
                const cronStatus = await loadCronStatus({
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                await loadCronJobsPage({
                    append: false,
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                if (!state.agentCronSelectedJobId &&
                    Array.isArray(state.agentCronJobs) &&
                    state.agentCronJobs.length > 0) {
                    const firstJob = state.agentCronJobs[0] || {};
                    state.agentCronSelectedJobId = String(firstJob.id || "").trim() || null;
                }

                await loadCronRuns({
                    append: false,
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                await loadCronModelSuggestions({
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentCronResult = {
                    status: cronStatus,
                    jobs: state.agentCronJobs,
                    jobsTotal: state.agentCronJobsTotal,
                    jobsHasMore: state.agentCronJobsHasMore,
                    runs: state.agentCronRuns,
                    runsTotal: state.agentCronRunsTotal,
                    runsHasMore: state.agentCronRunsHasMore,
                    modelSuggestions: state.agentCronModelSuggestions,
                    capability: state.agentCronCapability,
                };
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentCronError = resolveToolsErrorMessage(err, "cron surface");
            } finally {
                state.agentCronLoading = false;
                onStateUpdated();
            }
        }

        function updateCronJobsFilter(patch) {
            const next = patch || {};
            if (typeof next.query === "string") {
                state.agentCronJobsQuery = next.query;
            }
            if (next.enabledFilter === "all" ||
                next.enabledFilter === "enabled" ||
                next.enabledFilter === "disabled") {
                state.agentCronJobsEnabledFilter = next.enabledFilter;
            }
            if (next.sortBy === "nextRunAtMs" ||
                next.sortBy === "updatedAtMs" ||
                next.sortBy === "name") {
                state.agentCronJobsSortBy = next.sortBy;
            }
            if (next.sortDir === "asc" || next.sortDir === "desc") {
                state.agentCronJobsSortDir = next.sortDir;
            }
            state.agentCronJobsNextOffset = null;
            state.agentCronJobsHasMore = false;
            onStateUpdated();
        }

        function updateCronRunsFilter(patch) {
            const next = patch || {};
            if (next.scope === "all" || next.scope === "job") {
                state.agentCronRunsScope = next.scope;
            }
            if (next.statusFilter === "all" ||
                next.statusFilter === "ok" ||
                next.statusFilter === "error" ||
                next.statusFilter === "skipped") {
                state.agentCronRunsStatusFilter = next.statusFilter;
            }
            if (typeof next.query === "string") {
                state.agentCronRunsQuery = next.query;
            }
            if (next.sortDir === "asc" || next.sortDir === "desc") {
                state.agentCronRunsSortDir = next.sortDir;
            }
            if (Object.prototype.hasOwnProperty.call(next, "selectedJobId")) {
                state.agentCronSelectedJobId = next.selectedJobId
                    ? String(next.selectedJobId).trim()
                    : null;
            }
            state.agentCronRunsNextOffset = null;
            state.agentCronRunsHasMore = false;
            onStateUpdated();
        }

        async function loadMoreCronJobs() {
            await loadCronJobsPage({ append: true });
        }

        async function loadMoreCronRuns() {
            await loadCronRuns({ append: true });
        }

        return {
            normalizeCronPaginationMeta,
            normalizeCronJob,
            loadCronModelSuggestions,
            withCronBusy,
            addOrUpdateCronJob,
            triggerCronWake,
            removeCronJob,
            runCronJobNow,
            startCronEdit,
            startCronClone,
            cancelCronEdit,
            updateCronFormField,
            loadCronStatus,
            loadCronJobsPage,
            loadCronRuns,
            loadAgentCron,
            updateCronJobsFilter,
            updateCronRunsFilter,
            loadMoreCronJobs,
            loadMoreCronRuns,
        };
    }

    window.BlazeClawCronController = {
        createCronController,
    };
})();
