(function () {
    function createAgentsController(options) {
        const opts = options || {};
        const state = opts.state;
        if (!state) {
            throw new Error("agents-controller requires state");
        }

        const request = typeof opts.request === "function"
            ? opts.request
            : null;
        if (!request) {
            throw new Error("agents-controller requires request function");
        }

        const onStateUpdated = typeof opts.onStateUpdated === "function"
            ? opts.onStateUpdated
            : function () { };

        if (typeof state.agentsLoading !== "boolean") {
            state.agentsLoading = false;
        }
        if (typeof state.agentsError !== "string" && state.agentsError !== null) {
            state.agentsError = null;
        }
        if (!state.agentsList) {
            state.agentsList = null;
        }
        if (typeof state.agentsSelectedId !== "string" && state.agentsSelectedId !== null) {
            state.agentsSelectedId = null;
        }

        if (typeof state.toolsCatalogLoading !== "boolean") {
            state.toolsCatalogLoading = false;
        }
        if (typeof state.toolsCatalogLoadingAgentId !== "string" && state.toolsCatalogLoadingAgentId !== null) {
            state.toolsCatalogLoadingAgentId = null;
        }
        if (typeof state.toolsCatalogError !== "string" && state.toolsCatalogError !== null) {
            state.toolsCatalogError = null;
        }
        if (!state.toolsCatalogResult) {
            state.toolsCatalogResult = null;
        }

        if (typeof state.toolsEffectiveLoading !== "boolean") {
            state.toolsEffectiveLoading = false;
        }
        if (typeof state.toolsEffectiveLoadingKey !== "string" && state.toolsEffectiveLoadingKey !== null) {
            state.toolsEffectiveLoadingKey = null;
        }
        if (typeof state.toolsEffectiveResultKey !== "string" && state.toolsEffectiveResultKey !== null) {
            state.toolsEffectiveResultKey = null;
        }
        if (typeof state.toolsEffectiveError !== "string" && state.toolsEffectiveError !== null) {
            state.toolsEffectiveError = null;
        }
        if (!state.toolsEffectiveResult) {
            state.toolsEffectiveResult = null;
        }

        const supportedPanels = ["overview", "tools", "files", "skills", "channels", "cron"];
        if (supportedPanels.indexOf(String(state.agentsPanel || "")) < 0) {
            state.agentsPanel = "overview";
        }

        if (typeof state.agentFilesLoading !== "boolean") {
            state.agentFilesLoading = false;
        }
        if (typeof state.agentFilesLoadingAgentId !== "string" && state.agentFilesLoadingAgentId !== null) {
            state.agentFilesLoadingAgentId = null;
        }
        if (typeof state.agentFilesError !== "string" && state.agentFilesError !== null) {
            state.agentFilesError = null;
        }
        if (!state.agentFilesResult) {
            state.agentFilesResult = null;
        }
        if (typeof state.agentFileContentLoading !== "boolean") {
            state.agentFileContentLoading = false;
        }
        if (typeof state.agentFileContentLoadingKey !== "string" && state.agentFileContentLoadingKey !== null) {
            state.agentFileContentLoadingKey = null;
        }
        if (typeof state.agentFileContentError !== "string" && state.agentFileContentError !== null) {
            state.agentFileContentError = null;
        }
        if (!state.agentFileContentResult) {
            state.agentFileContentResult = null;
        }

        if (typeof state.agentSkillsLoading !== "boolean") {
            state.agentSkillsLoading = false;
        }
        if (typeof state.agentSkillsError !== "string" && state.agentSkillsError !== null) {
            state.agentSkillsError = null;
        }
        if (!state.agentSkillsResult) {
            state.agentSkillsResult = null;
        }
        if (!state.agentSkillsReport) {
            state.agentSkillsReport = null;
        }
        if (typeof state.agentSkillsAgentId !== "string" && state.agentSkillsAgentId !== null) {
            state.agentSkillsAgentId = null;
        }

        const channelsStateContract = window.BlazeClawChannelsStateContract;
        if (channelsStateContract && typeof channelsStateContract.ensureChannelsStateDefaults === "function") {
            channelsStateContract.ensureChannelsStateDefaults(state);
        } else {
            if (typeof state.channelsLoading !== "boolean") {
                state.channelsLoading = false;
            }
            if (typeof state.channelsError !== "string" && state.channelsError !== null) {
                state.channelsError = null;
            }
            if (!state.channelsSnapshot) {
                state.channelsSnapshot = null;
            }
            if (typeof state.channelsLastSuccess !== "number" && state.channelsLastSuccess !== null) {
                state.channelsLastSuccess = null;
            }
            if (typeof state.whatsappBusy !== "boolean") {
                state.whatsappBusy = false;
            }
            if (typeof state.whatsappLoginMessage !== "string" && state.whatsappLoginMessage !== null) {
                state.whatsappLoginMessage = null;
            }
            if (typeof state.whatsappLoginQrDataUrl !== "string" && state.whatsappLoginQrDataUrl !== null) {
                state.whatsappLoginQrDataUrl = null;
            }
            if (typeof state.whatsappLoginConnected !== "boolean" && state.whatsappLoginConnected !== null) {
                state.whatsappLoginConnected = null;
            }

            if (typeof state.agentChannelsLoading !== "boolean") {
                state.agentChannelsLoading = false;
            }
            if (typeof state.agentChannelsError !== "string" && state.agentChannelsError !== null) {
                state.agentChannelsError = null;
            }
            if (!state.agentChannelsResult) {
                state.agentChannelsResult = null;
            }
            if (!state.agentChannelsCapability) {
                state.agentChannelsCapability = {
                    method: "channels.status",
                    agentScoped: false,
                    todo: "docs/compare/channels.ts/CHANNELS_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md",
                };
            }
        }

        if (typeof state.agentCronLoading !== "boolean") {
            state.agentCronLoading = false;
        }
        if (typeof state.agentCronError !== "string" && state.agentCronError !== null) {
            state.agentCronError = null;
        }
        if (!state.agentCronResult) {
            state.agentCronResult = null;
        }
        if (!state.agentCronCapability) {
            state.agentCronCapability = {
                method: "cron.status/list/runs",
                agentScoped: false,
                todo: "docs/compare/cron.ts/CRON_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md",
            };
        }
        if (typeof state.agentCronStatusLoading !== "boolean") {
            state.agentCronStatusLoading = false;
        }
        if (typeof state.agentCronStatusError !== "string" && state.agentCronStatusError !== null) {
            state.agentCronStatusError = null;
        }
        if (!state.agentCronStatusResult) {
            state.agentCronStatusResult = null;
        }
        if (typeof state.agentCronJobsLoading !== "boolean") {
            state.agentCronJobsLoading = false;
        }
        if (typeof state.agentCronJobsLoadingMore !== "boolean") {
            state.agentCronJobsLoadingMore = false;
        }
        if (typeof state.agentCronJobsError !== "string" && state.agentCronJobsError !== null) {
            state.agentCronJobsError = null;
        }
        if (!Array.isArray(state.agentCronJobs)) {
            state.agentCronJobs = [];
        }
        if (typeof state.agentCronJobsTotal !== "number") {
            state.agentCronJobsTotal = 0;
        }
        if (typeof state.agentCronJobsHasMore !== "boolean") {
            state.agentCronJobsHasMore = false;
        }
        if (typeof state.agentCronJobsNextOffset !== "number" && state.agentCronJobsNextOffset !== null) {
            state.agentCronJobsNextOffset = null;
        }
        if (typeof state.agentCronJobsLimit !== "number" || !Number.isFinite(state.agentCronJobsLimit) || state.agentCronJobsLimit <= 0) {
            state.agentCronJobsLimit = 20;
        }
        if (typeof state.agentCronJobsQuery !== "string") {
            state.agentCronJobsQuery = "";
        }
        if (state.agentCronJobsEnabledFilter !== "enabled" &&
            state.agentCronJobsEnabledFilter !== "disabled" &&
            state.agentCronJobsEnabledFilter !== "all") {
            state.agentCronJobsEnabledFilter = "all";
        }
        if (state.agentCronJobsSortBy !== "nextRunAtMs" &&
            state.agentCronJobsSortBy !== "updatedAtMs" &&
            state.agentCronJobsSortBy !== "name") {
            state.agentCronJobsSortBy = "updatedAtMs";
        }
        if (state.agentCronJobsSortDir !== "asc" && state.agentCronJobsSortDir !== "desc") {
            state.agentCronJobsSortDir = "desc";
        }
        if (typeof state.agentCronRunsLoading !== "boolean") {
            state.agentCronRunsLoading = false;
        }
        if (typeof state.agentCronRunsLoadingMore !== "boolean") {
            state.agentCronRunsLoadingMore = false;
        }
        if (typeof state.agentCronRunsError !== "string" && state.agentCronRunsError !== null) {
            state.agentCronRunsError = null;
        }
        if (!Array.isArray(state.agentCronRuns)) {
            state.agentCronRuns = [];
        }
        if (typeof state.agentCronRunsTotal !== "number") {
            state.agentCronRunsTotal = 0;
        }
        if (typeof state.agentCronRunsHasMore !== "boolean") {
            state.agentCronRunsHasMore = false;
        }
        if (typeof state.agentCronRunsNextOffset !== "number" && state.agentCronRunsNextOffset !== null) {
            state.agentCronRunsNextOffset = null;
        }
        if (typeof state.agentCronRunsLimit !== "number" || !Number.isFinite(state.agentCronRunsLimit) || state.agentCronRunsLimit <= 0) {
            state.agentCronRunsLimit = 20;
        }
        if (state.agentCronRunsScope !== "all" && state.agentCronRunsScope !== "job") {
            state.agentCronRunsScope = "all";
        }
        if (typeof state.agentCronRunsStatusFilter !== "string") {
            state.agentCronRunsStatusFilter = "all";
        }
        if (typeof state.agentCronRunsQuery !== "string") {
            state.agentCronRunsQuery = "";
        }
        if (state.agentCronRunsSortDir !== "asc" && state.agentCronRunsSortDir !== "desc") {
            state.agentCronRunsSortDir = "desc";
        }

        if (!state.agentsPersistence || typeof state.agentsPersistence !== "object") {
            state.agentsPersistence = {
                selectedAgentId: null,
                panel: "overview",
            };
        }

        if (typeof state.agentIdentityLoading !== "boolean") {
            state.agentIdentityLoading = false;
        }
        if (typeof state.agentIdentityError !== "string" && state.agentIdentityError !== null) {
            state.agentIdentityError = null;
        }
        if (!state.agentIdentityById || typeof state.agentIdentityById !== "object") {
            state.agentIdentityById = {};
        }

        if (!state.chatModelOverrides || typeof state.chatModelOverrides !== "object") {
            state.chatModelOverrides = {};
        }
        if (!Array.isArray(state.chatModelCatalog)) {
            state.chatModelCatalog = [];
        }

        function hasSelectedAgentMismatch(agentId) {
            return Boolean(state.agentsSelectedId && state.agentsSelectedId !== agentId);
        }

        function resolveToolsErrorMessage(err, target) {
            const scopeErrors = window.BlazeClawScopeErrors;
            if (scopeErrors && typeof scopeErrors.isMissingOperatorReadScopeError === "function") {
                if (scopeErrors.isMissingOperatorReadScopeError(err)) {
                    if (typeof scopeErrors.formatMissingOperatorReadScopeMessage === "function") {
                        return scopeErrors.formatMissingOperatorReadScopeMessage(target);
                    }
                }
            }

            if (err && typeof err === "object" && typeof err.message === "string") {
                return err.message;
            }
            return String(err);
        }

        function isMissingOperatorReadScopeError(err) {
            const scopeErrors = window.BlazeClawScopeErrors;
            return Boolean(
                scopeErrors &&
                typeof scopeErrors.isMissingOperatorReadScopeError === "function" &&
                scopeErrors.isMissingOperatorReadScopeError(err)
            );
        }

        function normalizeBooleanFlag(value, fallback) {
            if (typeof value === "boolean") {
                return value;
            }
            if (typeof fallback === "boolean") {
                return fallback;
            }
            return false;
        }

        function normalizeChannelStatusEntry(entry, channelId, labelFallback) {
            const resolvedId = String(channelId || entry && entry.id || "").trim();
            if (!resolvedId) {
                return null;
            }

            const resolvedLabel = String(entry && entry.label || labelFallback || resolvedId).trim() || resolvedId;
            const accountCount = Number(entry && entry.accounts);
            return {
                id: resolvedId,
                label: resolvedLabel,
                connected: normalizeBooleanFlag(entry && entry.connected, false),
                accounts: Number.isFinite(accountCount) ? accountCount : 0,
            };
        }

        function normalizeChannelAccountEntry(entry) {
            const resolvedAccountId = String(entry && entry.accountId || "").trim();
            if (!resolvedAccountId) {
                return null;
            }

            return {
                accountId: resolvedAccountId,
                name: String(entry && (entry.name || entry.label) || "").trim() || resolvedAccountId,
                enabled: normalizeBooleanFlag(entry && entry.enabled, normalizeBooleanFlag(entry && entry.active, false)),
                configured: normalizeBooleanFlag(entry && entry.configured, true),
                linked: normalizeBooleanFlag(entry && entry.linked, normalizeBooleanFlag(entry && entry.active, false)),
                running: normalizeBooleanFlag(entry && entry.running, normalizeBooleanFlag(entry && entry.connected, false)),
                connected: normalizeBooleanFlag(entry && entry.connected, false),
                lastError: entry && typeof entry.lastError === "string"
                    ? entry.lastError
                    : null,
                lastProbeAt: entry && typeof entry.lastProbeAt === "number"
                    ? entry.lastProbeAt
                    : null,
            };
        }

        function normalizeChannelsSnapshot(payload) {
            if (!payload || typeof payload !== "object") {
                return null;
            }

            const source = payload;
            const rawLabels = source.channelLabels && typeof source.channelLabels === "object"
                ? source.channelLabels
                : {};
            const rawAccountsMap = source.channelAccounts && typeof source.channelAccounts === "object"
                ? source.channelAccounts
                : {};
            const rawDefaultAccountMap = source.channelDefaultAccountId && typeof source.channelDefaultAccountId === "object"
                ? source.channelDefaultAccountId
                : {};
            const normalized = {
                ts: typeof source.ts === "number" ? source.ts : Date.now(),
                channelOrder: [],
                channelLabels: {},
                channels: {},
                channelAccounts: {},
                channelDefaultAccountId: {},
            };

            function assignChannel(channelId, entry) {
                const normalizedEntry = normalizeChannelStatusEntry(entry, channelId, rawLabels[channelId]);
                if (!normalizedEntry) {
                    return;
                }

                const resolvedChannelId = normalizedEntry.id;
                normalized.channelOrder.push(resolvedChannelId);
                normalized.channelLabels[resolvedChannelId] = normalizedEntry.label;
                normalized.channels[resolvedChannelId] = normalizedEntry;

                const rawAccounts = Array.isArray(rawAccountsMap[resolvedChannelId])
                    ? rawAccountsMap[resolvedChannelId]
                    : [];
                const normalizedAccounts = rawAccounts
                    .map(normalizeChannelAccountEntry)
                    .filter(function (account) {
                        return Boolean(account);
                    });
                normalized.channelAccounts[resolvedChannelId] = normalizedAccounts;

                const explicitDefaultAccountId = String(rawDefaultAccountMap[resolvedChannelId] || "").trim();
                if (explicitDefaultAccountId) {
                    normalized.channelDefaultAccountId[resolvedChannelId] = explicitDefaultAccountId;
                } else if (normalizedAccounts.length > 0) {
                    normalized.channelDefaultAccountId[resolvedChannelId] = normalizedAccounts[0].accountId;
                }
            }

            if (Array.isArray(source.channels)) {
                source.channels.forEach(function (entry) {
                    const resolvedId = String(entry && entry.id || "").trim();
                    assignChannel(resolvedId, entry);
                });
                return normalized;
            }

            const rawChannelMap = source.channels && typeof source.channels === "object"
                ? source.channels
                : {};
            const rawOrder = Array.isArray(source.channelOrder)
                ? source.channelOrder
                : Object.keys(rawChannelMap);
            rawOrder.forEach(function (channelId) {
                const resolvedId = String(channelId || "").trim();
                if (!resolvedId) {
                    return;
                }
                assignChannel(resolvedId, rawChannelMap[resolvedId]);
            });

            return normalized;
        }

        function buildAgentChannelsResult(snapshot, routes, agentId) {
            const normalizedSnapshot = snapshot || null;
            const resolvedAgentId = String(agentId || "").trim();
            const normalizedRoutes = Array.isArray(routes)
                ? routes.map(function (entry) {
                    return {
                        channel: String(entry && entry.channel || "").trim(),
                        accountId: String(entry && entry.accountId || "").trim(),
                        agentId: String(entry && entry.agentId || "").trim(),
                        sessionId: String(entry && entry.sessionId || "").trim(),
                    };
                }).filter(function (entry) {
                    return entry.channel.length > 0 &&
                        entry.accountId.length > 0 &&
                        (!resolvedAgentId || !entry.agentId || entry.agentId === resolvedAgentId);
                })
                : [];
            const channelOrder = normalizedSnapshot && Array.isArray(normalizedSnapshot.channelOrder)
                ? normalizedSnapshot.channelOrder
                : [];
            const channels = channelOrder.map(function (channelId) {
                const statusEntry = normalizedSnapshot.channels && normalizedSnapshot.channels[channelId]
                    ? normalizedSnapshot.channels[channelId]
                    : {
                        id: channelId,
                        label: String(normalizedSnapshot.channelLabels && normalizedSnapshot.channelLabels[channelId] || channelId),
                        connected: false,
                        accounts: 0,
                    };
                const accountEntries = normalizedSnapshot.channelAccounts && Array.isArray(normalizedSnapshot.channelAccounts[channelId])
                    ? normalizedSnapshot.channelAccounts[channelId]
                    : [];
                const defaultAccountId = normalizedSnapshot.channelDefaultAccountId &&
                    typeof normalizedSnapshot.channelDefaultAccountId[channelId] === "string"
                    ? normalizedSnapshot.channelDefaultAccountId[channelId]
                    : (accountEntries[0] && accountEntries[0].accountId) || "";
                return {
                    id: channelId,
                    label: String(statusEntry.label || channelId),
                    connected: Boolean(statusEntry.connected),
                    accountCount: accountEntries.length,
                    accounts: accountEntries,
                    defaultAccountId: defaultAccountId,
                };
            });

            return {
                snapshot: normalizedSnapshot,
                channels: channels,
                routes: normalizedRoutes,
                selectedAgentId: resolvedAgentId || null,
                capability: state.agentChannelsCapability,
            };
        }

        function resolvePreferredServerChatModelValue(model, modelProvider, catalog) {
            const normalizedCatalog = Array.isArray(catalog) ? catalog : [];
            const modelText = String(model || "").trim();
            const providerText = String(modelProvider || "").trim();

            if (modelText && providerText) {
                return providerText + ":" + modelText;
            }
            if (modelText) {
                return modelText;
            }
            return normalizedCatalog.length > 0
                ? String(normalizedCatalog[0].id || "")
                : "";
        }

        function normalizeChatModelOverrideValue(overrideValue, catalog) {
            if (typeof overrideValue === "string") {
                return overrideValue.trim();
            }

            if (overrideValue && typeof overrideValue === "object") {
                const candidate = overrideValue;
                if (typeof candidate.model === "string" && candidate.model.trim()) {
                    if (typeof candidate.modelProvider === "string" && candidate.modelProvider.trim()) {
                        return candidate.modelProvider.trim() + ":" + candidate.model.trim();
                    }
                    return candidate.model.trim();
                }
            }

            return resolvePreferredServerChatModelValue("", "", catalog);
        }

        function resolveAgentIdFromSessionKey(sessionKey) {
            const key = String(sessionKey || "").trim();
            if (!key) {
                return "main";
            }

            const normalized = key.toLowerCase();
            const parts = normalized.split(":").filter(function (part) {
                return part.length > 0;
            });

            if (parts.length >= 3 && parts[0] === "agent") {
                const parsedAgentId = String(parts[1] || "").trim();
                return parsedAgentId || "main";
            }

            return "main";
        }

        function resolveEffectiveToolsModelKey(sessionKey) {
            const resolvedSessionKey = String(sessionKey || "").trim();
            if (!resolvedSessionKey) {
                return "";
            }

            const catalog = Array.isArray(state.chatModelCatalog)
                ? state.chatModelCatalog
                : [];
            const cachedOverride = state.chatModelOverrides
                ? state.chatModelOverrides[resolvedSessionKey]
                : undefined;
            const defaults = state.sessionsResult && state.sessionsResult.defaults
                ? state.sessionsResult.defaults
                : null;
            const defaultModel = resolvePreferredServerChatModelValue(
                defaults ? defaults.model : "",
                defaults ? defaults.modelProvider : "",
                catalog);

            if (cachedOverride === null) {
                return defaultModel;
            }

            if (cachedOverride) {
                return normalizeChatModelOverrideValue(cachedOverride, catalog);
            }

            const sessions = state.sessionsResult && Array.isArray(state.sessionsResult.sessions)
                ? state.sessionsResult.sessions
                : [];
            const activeRow = sessions.find(function (row) {
                return row && row.key === resolvedSessionKey;
            });

            if (activeRow && activeRow.model) {
                return resolvePreferredServerChatModelValue(activeRow.model, activeRow.modelProvider, catalog);
            }

            return defaultModel;
        }

        function buildToolsEffectiveRequestKey(params) {
            const resolvedAgentId = String(params && params.agentId || "").trim() || "main";
            const resolvedSessionKey = String(params && params.sessionKey || "").trim();
            const modelKey = resolveEffectiveToolsModelKey(resolvedSessionKey);
            return resolvedAgentId + ":" + resolvedSessionKey + ":model=" + (modelKey || "(default)");
        }

        function buildAgentFileContentRequestKey(params) {
            const resolvedAgentId = String(params && params.agentId || "").trim() || "main";
            const resolvedPath = String(params && params.path || "").trim();
            return resolvedAgentId + ":path=" + resolvedPath;
        }

        async function loadAgents() {
            if (!request || !state.connected || state.agentsLoading) {
                return;
            }

            state.agentsLoading = true;
            state.agentsError = null;
            onStateUpdated();

            try {
                const res = await request("agents.list", {});
                if (res && res.payload) {
                    state.agentsList = res.payload;
                    const selected = state.agentsSelectedId;
                    const agents = Array.isArray(res.payload.agents) ? res.payload.agents : [];
                    const hasSelected = selected && agents.some(function (entry) {
                        return entry && entry.id === selected;
                    });

                    if (!hasSelected) {
                        state.agentsSelectedId = res.payload.defaultId || (agents[0] && agents[0].id) || null;
                    }

                    const agentIds = agents
                        .map(function (entry) {
                            return String(entry && entry.id || "").trim();
                        })
                        .filter(function (id) {
                            return id.length > 0;
                        });
                    void loadAgentIdentities(agentIds);

                    const selectedAgentId = String(state.agentsSelectedId || "").trim();
                    if (selectedAgentId) {
                        void loadAgentIdentity(selectedAgentId);
                    }
                }
            } catch (err) {
                const scopeErrors = window.BlazeClawScopeErrors;
                if (scopeErrors && typeof scopeErrors.isMissingOperatorReadScopeError === "function" &&
                    scopeErrors.isMissingOperatorReadScopeError(err)) {
                    state.agentsList = null;
                    state.agentsError = scopeErrors.formatMissingOperatorReadScopeMessage("agent list");
                } else {
                    state.agentsError = String(err);
                }
            } finally {
                state.agentsLoading = false;
                onStateUpdated();
            }
        }

        async function loadToolsCatalog(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!request ||
                !state.connected ||
                !resolvedAgentId ||
                (state.toolsCatalogLoading && state.toolsCatalogLoadingAgentId === resolvedAgentId)) {
                return;
            }

            function shouldIgnoreResponse() {
                return state.toolsCatalogLoadingAgentId !== resolvedAgentId ||
                    hasSelectedAgentMismatch(resolvedAgentId);
            }

            state.toolsCatalogLoading = true;
            state.toolsCatalogLoadingAgentId = resolvedAgentId;
            state.toolsCatalogError = null;
            state.toolsCatalogResult = null;
            onStateUpdated();

            try {
                const res = await request("tools.catalog", {
                    agentId: resolvedAgentId,
                    includePlugins: true,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.toolsCatalogResult = res && res.payload ? res.payload : null;
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.toolsCatalogError = resolveToolsErrorMessage(err, "tools catalog");
            } finally {
                if (state.toolsCatalogLoadingAgentId === resolvedAgentId) {
                    state.toolsCatalogLoadingAgentId = null;
                    state.toolsCatalogLoading = false;
                }
                onStateUpdated();
            }
        }

        async function loadToolsEffective(params) {
            const resolvedAgentId = String(params && params.agentId || "").trim();
            const resolvedSessionKey = String(params && params.sessionKey || "").trim();
            const requestKey = buildToolsEffectiveRequestKey({
                agentId: resolvedAgentId,
                sessionKey: resolvedSessionKey,
            });

            if (!request ||
                !state.connected ||
                !resolvedAgentId ||
                !resolvedSessionKey ||
                (state.toolsEffectiveLoading && state.toolsEffectiveLoadingKey === requestKey)) {
                return;
            }

            function shouldIgnoreResponse() {
                return state.toolsEffectiveLoadingKey !== requestKey ||
                    hasSelectedAgentMismatch(resolvedAgentId);
            }

            state.toolsEffectiveLoading = true;
            state.toolsEffectiveLoadingKey = requestKey;
            state.toolsEffectiveResultKey = null;
            state.toolsEffectiveError = null;
            state.toolsEffectiveResult = null;
            onStateUpdated();

            try {
                const res = await request("tools.effective", {
                    agentId: resolvedAgentId,
                    sessionKey: resolvedSessionKey,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.toolsEffectiveResultKey = requestKey;
                state.toolsEffectiveResult = res && res.payload ? res.payload : null;
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.toolsEffectiveError = resolveToolsErrorMessage(err, "effective tools");
            } finally {
                if (state.toolsEffectiveLoadingKey === requestKey) {
                    state.toolsEffectiveLoadingKey = null;
                    state.toolsEffectiveLoading = false;
                }
                onStateUpdated();
            }
        }

        function resetToolsEffectiveState() {
            state.toolsEffectiveResult = null;
            state.toolsEffectiveResultKey = null;
            state.toolsEffectiveError = null;
            state.toolsEffectiveLoading = false;
            state.toolsEffectiveLoadingKey = null;
            onStateUpdated();
        }

        function refreshVisibleToolsEffectiveForCurrentSession() {
            const resolvedSessionKey = String(state.sessionKey || "").trim();
            if (!resolvedSessionKey || state.agentsPanel !== "tools" || !state.agentsSelectedId) {
                return undefined;
            }

            const sessionAgentId = resolveAgentIdFromSessionKey(resolvedSessionKey);
            if (state.agentsSelectedId !== sessionAgentId) {
                return undefined;
            }

            return loadToolsEffective({
                agentId: sessionAgentId,
                sessionKey: resolvedSessionKey,
            });
        }

        async function loadAgentFiles(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!request ||
                !state.connected ||
                !resolvedAgentId ||
                (state.agentFilesLoading && state.agentFilesLoadingAgentId === resolvedAgentId)) {
                return;
            }

            function shouldIgnoreResponse() {
                return state.agentFilesLoadingAgentId !== resolvedAgentId ||
                    hasSelectedAgentMismatch(resolvedAgentId) ||
                    state.agentsPanel !== "files";
            }

            state.agentFilesLoading = true;
            state.agentFilesLoadingAgentId = resolvedAgentId;
            state.agentFilesError = null;
            state.agentFilesResult = null;
            onStateUpdated();

            try {
                const res = await request("gateway.agents.files.list", {
                    agentId: resolvedAgentId,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentFilesResult = res && res.payload ? res.payload : null;
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentFilesError = resolveToolsErrorMessage(err, "agent files");
            } finally {
                if (state.agentFilesLoadingAgentId === resolvedAgentId) {
                    state.agentFilesLoadingAgentId = null;
                    state.agentFilesLoading = false;
                }
                onStateUpdated();
            }
        }

        async function loadAgentFileContent(params) {
            const resolvedAgentId = String(params && params.agentId || "").trim();
            const resolvedPath = String(params && params.path || "").trim();
            const requestKey = buildAgentFileContentRequestKey({
                agentId: resolvedAgentId,
                path: resolvedPath,
            });

            if (!request ||
                !state.connected ||
                !resolvedAgentId ||
                !resolvedPath ||
                (state.agentFileContentLoading && state.agentFileContentLoadingKey === requestKey)) {
                return;
            }

            function shouldIgnoreResponse() {
                return state.agentFileContentLoadingKey !== requestKey ||
                    hasSelectedAgentMismatch(resolvedAgentId) ||
                    state.agentsPanel !== "files";
            }

            state.agentFileContentLoading = true;
            state.agentFileContentLoadingKey = requestKey;
            state.agentFileContentError = null;
            state.agentFileContentResult = null;
            onStateUpdated();

            try {
                const res = await request("gateway.agents.files.get", {
                    agentId: resolvedAgentId,
                    path: resolvedPath,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentFileContentResult = res && res.payload ? res.payload : null;
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentFileContentError = resolveToolsErrorMessage(err, "agent file content");
            } finally {
                if (state.agentFileContentLoadingKey === requestKey) {
                    state.agentFileContentLoadingKey = null;
                    state.agentFileContentLoading = false;
                }
                onStateUpdated();
            }
        }

        function isMethodNotFoundError(err) {
            if (!err) {
                return false;
            }

            if (typeof err === "object") {
                const candidate = err;
                const code = String(candidate.code || candidate.detailCode || "").toLowerCase();
                const message = String(candidate.message || "").toLowerCase();
                return code.indexOf("method") >= 0 && code.indexOf("not") >= 0 ||
                    message.indexOf("method_not_found") >= 0 ||
                    message.indexOf("unknown method") >= 0;
            }

            const text = String(err).toLowerCase();
            return text.indexOf("method_not_found") >= 0 || text.indexOf("unknown method") >= 0;
        }

        function normalizeAgentSkillsReportPayload(payload) {
            const candidate = payload && typeof payload === "object" ? payload : {};
            const skills = Array.isArray(candidate.skills)
                ? candidate.skills
                : [];

            const workspaceDir = String(candidate.workspaceDir || "").trim();
            const managedSkillsDir = String(candidate.managedSkillsDir || "").trim();

            return {
                workspaceDir,
                managedSkillsDir,
                skills,
            };
        }

        async function loadAgentSkills(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!request || !state.connected || !resolvedAgentId || state.agentSkillsLoading) {
                return;
            }

            function shouldIgnoreResponse() {
                return hasSelectedAgentMismatch(resolvedAgentId) || state.agentsPanel !== "skills";
            }

            state.agentSkillsLoading = true;
            state.agentSkillsError = null;
            onStateUpdated();

            try {
                let res = null;
                try {
                    res = await request("skills.status", {
                        agentId: resolvedAgentId,
                    });
                } catch (primaryError) {
                    if (!isMethodNotFoundError(primaryError)) {
                        throw primaryError;
                    }

                    res = await request("gateway.skills.status", {
                        agentId: resolvedAgentId,
                    });
                }

                if (shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : null;
                const report = normalizeAgentSkillsReportPayload(payload);
                const commandLikeEntries = Array.isArray(report.skills)
                    ? report.skills.map(function (entry) {
                        return {
                            name: String(entry && entry.name || "").trim(),
                            description: String(entry && entry.description || "").trim(),
                            skill: String(entry && entry.skillKey || entry && entry.name || "").trim(),
                        };
                    }).filter(function (entry) {
                        return entry.name.length > 0;
                    })
                    : [];

                state.agentSkillsReport = report;
                state.agentSkillsAgentId = resolvedAgentId;
                state.agentSkillsResult = {
                    commands: commandLikeEntries,
                    count: report.skills.length,
                    capability: "skills.status",
                    agentScoped: true,
                    report,
                };
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentSkillsError = resolveToolsErrorMessage(err, "agent skills");
            } finally {
                state.agentSkillsLoading = false;
                onStateUpdated();
            }
        }

        async function loadChannels(options) {
            const opts = options || {};
            const probe = Boolean(opts.probe);
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.channelsLoading) {
                return state.channelsSnapshot || null;
            }

            state.channelsLoading = true;
            state.channelsError = null;
            onStateUpdated();

            try {
                const res = await request("channels.status", {
                    probe: probe,
                    timeoutMs: 8000,
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                const payload = res && res.payload ? res.payload : res;
                const snapshot = normalizeChannelsSnapshot(payload);
                state.channelsSnapshot = snapshot;
                state.channelsLastSuccess = Date.now();
                return snapshot;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                if (isMissingOperatorReadScopeError(err)) {
                    state.channelsSnapshot = null;
                }
                state.channelsError = resolveToolsErrorMessage(err, "channel status");
                return null;
            } finally {
                state.channelsLoading = false;
                onStateUpdated();
            }
        }

        async function loadAgentChannels(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!request || !state.connected || !resolvedAgentId || state.agentChannelsLoading) {
                return;
            }

            function shouldIgnoreResponse() {
                return hasSelectedAgentMismatch(resolvedAgentId) || state.agentsPanel !== "channels";
            }

            state.agentChannelsLoading = true;
            state.agentChannelsError = null;
            onStateUpdated();

            try {
                const snapshot = await loadChannels({
                    probe: true,
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                if (!snapshot && state.channelsError) {
                    state.agentChannelsError = state.channelsError;
                    state.agentChannelsResult = null;
                    return;
                }

                const routesRes = await request("gateway.channels.routes", {});
                if (shouldIgnoreResponse()) {
                    return;
                }

                const routePayload = routesRes && routesRes.payload ? routesRes.payload : null;
                const routes = routePayload && Array.isArray(routePayload.routes)
                    ? routePayload.routes
                    : [];
                const effectiveSnapshot = snapshot || state.channelsSnapshot;
                state.agentChannelsResult = buildAgentChannelsResult(
                    effectiveSnapshot,
                    routes,
                    resolvedAgentId
                );
                state.agentChannelsError = null;
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentChannelsError = resolveToolsErrorMessage(err, "channel status");
            } finally {
                state.agentChannelsLoading = false;
                onStateUpdated();
            }
        }

        async function startWhatsAppLogin(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            const force = Boolean(opts.force);
            if (!request || !state.connected || state.whatsappBusy) {
                return false;
            }

            state.whatsappBusy = true;
            onStateUpdated();

            try {
                const res = await request("web.login.start", {
                    force: force,
                    timeoutMs: 30000,
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                const payload = res && res.payload ? res.payload : res;
                state.whatsappLoginMessage = payload && typeof payload.message === "string"
                    ? payload.message
                    : null;
                state.whatsappLoginQrDataUrl = payload && typeof payload.qrDataUrl === "string"
                    ? payload.qrDataUrl
                    : null;
                state.whatsappLoginConnected = null;
                return true;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                state.whatsappLoginMessage = String(err);
                state.whatsappLoginQrDataUrl = null;
                state.whatsappLoginConnected = null;
                return false;
            } finally {
                state.whatsappBusy = false;
                onStateUpdated();
            }
        }

        async function waitWhatsAppLogin(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.whatsappBusy) {
                return false;
            }

            state.whatsappBusy = true;
            onStateUpdated();

            try {
                const res = await request("web.login.wait", {
                    timeoutMs: 120000,
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                const payload = res && res.payload ? res.payload : res;
                state.whatsappLoginMessage = payload && typeof payload.message === "string"
                    ? payload.message
                    : null;
                state.whatsappLoginConnected = payload && typeof payload.connected === "boolean"
                    ? payload.connected
                    : null;
                if (state.whatsappLoginConnected) {
                    state.whatsappLoginQrDataUrl = null;
                    await loadChannels({
                        probe: true,
                        shouldIgnoreResponse: shouldIgnoreResponse,
                    });
                }
                return Boolean(state.whatsappLoginConnected);
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                state.whatsappLoginMessage = String(err);
                state.whatsappLoginConnected = null;
                return false;
            } finally {
                state.whatsappBusy = false;
                onStateUpdated();
            }
        }

        async function logoutWhatsApp(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.whatsappBusy) {
                return false;
            }

            state.whatsappBusy = true;
            onStateUpdated();

            try {
                await request("channels.logout", {
                    channel: "whatsapp",
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                state.whatsappLoginMessage = "Logged out.";
                state.whatsappLoginQrDataUrl = null;
                state.whatsappLoginConnected = null;
                await loadChannels({
                    probe: true,
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                return true;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                state.whatsappLoginMessage = String(err);
                return false;
            } finally {
                state.whatsappBusy = false;
                onStateUpdated();
            }
        }

        function normalizeCronPaginationMeta(payload, entriesLength, fallbackLimit, fallbackOffset) {
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
        }

        async function loadCronStatus(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
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
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            const append = Boolean(opts.append);
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
                    ? payload.jobs
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
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            const append = Boolean(opts.append);
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
                const scope = state.agentCronRunsScope === "job"
                    ? "job"
                    : "all";
                const selectedJobId = String(state.agentCronSelectedJobId || "").trim();
                const res = await request("cron.runs", {
                    scope: scope,
                    id: scope === "job" && selectedJobId ? selectedJobId : undefined,
                    limit: state.agentCronRunsLimit,
                    offset: offset,
                    status: state.agentCronRunsStatusFilter,
                    query: String(state.agentCronRunsQuery || "").trim() || undefined,
                    sortDir: state.agentCronRunsSortDir,
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : res;
                const entries = payload && Array.isArray(payload.entries)
                    ? payload.entries
                    : [];
                state.agentCronRuns = append
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
            if (!resolvedAgentId || !request || !state.connected) {
                return;
            }

            function shouldIgnoreResponse() {
                return hasSelectedAgentMismatch(resolvedAgentId) || state.agentsPanel !== "cron";
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

                if (!state.agentCronSelectedJobId && Array.isArray(state.agentCronJobs) && state.agentCronJobs.length > 0) {
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

                state.agentCronResult = {
                    status: cronStatus,
                    jobs: state.agentCronJobs,
                    jobsTotal: state.agentCronJobsTotal,
                    jobsHasMore: state.agentCronJobsHasMore,
                    runs: state.agentCronRuns,
                    runsTotal: state.agentCronRunsTotal,
                    runsHasMore: state.agentCronRunsHasMore,
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

        async function refreshFromConfigSnapshot() {
            if (!request || !state.connected) {
                return;
            }

            try {
                await request("gateway.config.get", {});
            } catch (_) {
            }

            await loadAgents();
            if (state.agentsPanel === "channels") {
                const selectedAgentId = String(state.agentsSelectedId || "").trim();
                if (selectedAgentId) {
                    await loadAgentChannels(selectedAgentId);
                    return;
                }
            }
            await loadPanelDataForCurrentAgent();
        }

        async function loadAgentIdentity(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!request || !state.connected || state.agentIdentityLoading || !resolvedAgentId) {
                return;
            }

            if (state.agentIdentityById[resolvedAgentId]) {
                return;
            }

            state.agentIdentityLoading = true;
            state.agentIdentityError = null;
            onStateUpdated();

            try {
                const res = await request("agent.identity.get", {
                    agentId: resolvedAgentId,
                });
                const payload = res && res.payload ? res.payload : null;
                const identity = payload && payload.agent ? payload.agent : payload;
                if (identity) {
                    state.agentIdentityById = Object.assign({}, state.agentIdentityById, {
                        [resolvedAgentId]: identity,
                    });
                }
            } catch (err) {
                state.agentIdentityError = String(err);
            } finally {
                state.agentIdentityLoading = false;
                onStateUpdated();
            }
        }

        async function loadAgentIdentities(agentIds) {
            if (!request || !state.connected || state.agentIdentityLoading) {
                return;
            }

            const normalizedIds = Array.isArray(agentIds)
                ? agentIds.map(function (id) { return String(id || "").trim(); }).filter(function (id) { return id.length > 0; })
                : [];
            const missing = normalizedIds.filter(function (id) {
                return !state.agentIdentityById[id];
            });
            if (missing.length === 0) {
                return;
            }

            state.agentIdentityLoading = true;
            state.agentIdentityError = null;
            onStateUpdated();

            try {
                for (const agentId of missing) {
                    const res = await request("agent.identity.get", {
                        agentId,
                    });
                    const payload = res && res.payload ? res.payload : null;
                    const identity = payload && payload.agent ? payload.agent : payload;
                    if (identity) {
                        state.agentIdentityById = Object.assign({}, state.agentIdentityById, {
                            [agentId]: identity,
                        });
                    }
                }
            } catch (err) {
                state.agentIdentityError = String(err);
            } finally {
                state.agentIdentityLoading = false;
                onStateUpdated();
            }
        }

        function setAgentsPanel(panel) {
            const panelValue = String(panel || "").trim();
            const normalized = ["overview", "tools", "files", "skills", "channels", "cron"].indexOf(panelValue) >= 0
                ? panelValue
                : "overview";
            state.agentsPanel = normalized;
            if (state.agentsPersistence && typeof state.agentsPersistence === "object") {
                state.agentsPersistence.panel = normalized;
            }
            onStateUpdated();
        }

        async function loadPanelDataForCurrentAgent() {
            const selectedAgentId = String(state.agentsSelectedId || "").trim();
            if (!selectedAgentId) {
                return;
            }

            if (state.agentsPanel === "tools") {
                await loadToolsCatalog(selectedAgentId);
                await refreshVisibleToolsEffectiveForCurrentSession();
                return;
            }

            if (state.agentsPanel === "files") {
                await loadAgentFiles(selectedAgentId);
                const files = state.agentFilesResult && Array.isArray(state.agentFilesResult.files)
                    ? state.agentFilesResult.files
                    : [];
                if (files.length > 0) {
                    const first = files[0] || {};
                    const path = String(first.path || first.name || "").trim();
                    if (path) {
                        await loadAgentFileContent({
                            agentId: selectedAgentId,
                            path,
                        });
                    }
                }
                return;
            }

            if (state.agentsPanel === "skills") {
                await loadAgentSkills(selectedAgentId);
                return;
            }

            if (state.agentsPanel === "channels") {
                await loadAgentChannels(selectedAgentId);
                return;
            }

            if (state.agentsPanel === "cron") {
                await loadAgentCron(selectedAgentId);
                return;
            }

            if (state.agentsPanel === "overview") {
                await loadAgentIdentity(selectedAgentId);
            }
        }

        async function saveAgentsConfig(saveConfigFn) {
            if (typeof saveConfigFn !== "function") {
                throw new Error("saveAgentsConfig requires saveConfig function");
            }

            const selectedBefore = state.agentsSelectedId;
            await saveConfigFn(state);
            await loadAgents();

            const agents = state.agentsList && Array.isArray(state.agentsList.agents)
                ? state.agentsList.agents
                : [];
            if (selectedBefore && agents.some(function (entry) { return entry && entry.id === selectedBefore; })) {
                state.agentsSelectedId = selectedBefore;
                onStateUpdated();
            }
        }

        function setSelectedAgentId(agentId) {
            const nextAgentId = String(agentId || "").trim() || null;
            state.agentsSelectedId = nextAgentId;
            if (state.agentsPersistence && typeof state.agentsPersistence === "object") {
                state.agentsPersistence.selectedAgentId = nextAgentId;
            }
            state.toolsCatalogResult = null;
            state.toolsEffectiveResult = null;
            state.toolsEffectiveResultKey = null;
            state.agentFilesResult = null;
            state.agentFileContentResult = null;
            state.agentSkillsResult = null;
            state.agentSkillsReport = null;
            state.agentSkillsAgentId = null;
            state.agentChannelsResult = null;
            state.agentCronResult = null;
            onStateUpdated();

            if (nextAgentId) {
                void loadAgentIdentity(nextAgentId);
            }
        }

        function syncSessionContext(params) {
            const normalizedSessionKey = String(params && params.sessionKey || state.sessionKey || "").trim();
            if (normalizedSessionKey) {
                state.sessionKey = normalizedSessionKey;
            }
            if (params && Object.prototype.hasOwnProperty.call(params, "sessionsResult")) {
                state.sessionsResult = params.sessionsResult || null;
            }
            if (params && params.chatModelOverrides && typeof params.chatModelOverrides === "object") {
                state.chatModelOverrides = params.chatModelOverrides;
            }
            if (params && Array.isArray(params.chatModelCatalog)) {
                state.chatModelCatalog = params.chatModelCatalog;
            }
            if (params && params.agentsPersistence && typeof params.agentsPersistence === "object") {
                state.agentsPersistence = params.agentsPersistence;
            }
            onStateUpdated();
        }

        function getPersistenceSnapshot() {
            return {
                panel: String(state.agentsPanel || "overview"),
                selectedAgentId: String(state.agentsSelectedId || "").trim() || null,
            };
        }

        function applyPersistenceSnapshot(snapshot) {
            if (!snapshot || typeof snapshot !== "object") {
                return;
            }

            if (snapshot.panel) {
                setAgentsPanel(snapshot.panel);
            }
            if (snapshot.selectedAgentId) {
                setSelectedAgentId(snapshot.selectedAgentId);
            }
        }

        function updateCronJobsFilter(patch) {
            const next = patch || {};
            if (typeof next.query === "string") {
                state.agentCronJobsQuery = next.query;
            }
            if (next.enabledFilter === "all" || next.enabledFilter === "enabled" || next.enabledFilter === "disabled") {
                state.agentCronJobsEnabledFilter = next.enabledFilter;
            }
            if (next.sortBy === "nextRunAtMs" || next.sortBy === "updatedAtMs" || next.sortBy === "name") {
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
            if (next.statusFilter === "all" || next.statusFilter === "ok" || next.statusFilter === "error" || next.statusFilter === "skipped") {
                state.agentCronRunsStatusFilter = next.statusFilter;
            }
            if (typeof next.query === "string") {
                state.agentCronRunsQuery = next.query;
            }
            if (next.sortDir === "asc" || next.sortDir === "desc") {
                state.agentCronRunsSortDir = next.sortDir;
            }
            if (Object.prototype.hasOwnProperty.call(next, "selectedJobId")) {
                state.agentCronSelectedJobId = next.selectedJobId ? String(next.selectedJobId).trim() : null;
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
            loadAgents,
            loadToolsCatalog,
            loadToolsEffective,
            loadAgentFiles,
            loadAgentFileContent,
            loadAgentSkills,
            loadChannels,
            startWhatsAppLogin,
            waitWhatsAppLogin,
            logoutWhatsApp,
            loadAgentChannels,
            loadAgentCron,
            loadCronStatus,
            loadCronJobsPage,
            loadCronRuns,
            loadMoreCronJobs,
            loadMoreCronRuns,
            updateCronJobsFilter,
            updateCronRunsFilter,
            loadAgentIdentity,
            loadAgentIdentities,
            loadPanelDataForCurrentAgent,
            refreshFromConfigSnapshot,
            resetToolsEffectiveState,
            buildToolsEffectiveRequestKey,
            buildAgentFileContentRequestKey,
            refreshVisibleToolsEffectiveForCurrentSession,
            saveAgentsConfig,
            setSelectedAgentId,
            setAgentsPanel,
            syncSessionContext,
            getPersistenceSnapshot,
            applyPersistenceSnapshot,
        };
    }

    function createDeferred() {
        let resolve;
        let reject;
        const promise = new Promise(function (res, rej) {
            resolve = res;
            reject = rej;
        });

        return {
            promise,
            resolve,
            reject,
        };
    }

    function createRegressionHarnessRequestStub() {
        const queue = {};

        return {
            request: function (method, params) {
                if (!queue[method]) {
                    queue[method] = [];
                }

                const call = {
                    method,
                    params,
                    deferred: createDeferred(),
                };
                queue[method].push(call);
                return call.deferred.promise;
            },
            takeNextCall: function (method) {
                const calls = queue[method] || [];
                if (!calls.length) {
                    throw new Error("No queued call for method: " + method);
                }
                return calls.shift();
            },
        };
    }

    function createRegressionState() {
        return {
            connected: true,
            sessionKey: "agent:main:default",
            agentsPanel: "tools",
            agentsSelectedId: "main",
            chatModelCatalog: [
                {
                    id: "openai:gpt-4.1-mini",
                },
            ],
            chatModelOverrides: {},
            agentsPersistence: {
                panel: "tools",
                selectedAgentId: "main",
            },
            sessionsResult: {
                defaults: {
                    model: "gpt-4.1-mini",
                    modelProvider: "openai",
                },
                sessions: [
                    {
                        key: "agent:main:default",
                        model: "gpt-4.1-mini",
                        modelProvider: "openai",
                    },
                ],
            },
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
            const state = {
                connected: true,
            };
            const harness = createRegressionHarnessRequestStub();
            createAgentsController({
                state,
                request: harness.request,
            });

            assertRegression(state.channelsLoading === false,
                "channels state contract should initialize channelsLoading=false");
            assertRegression(state.channelsSnapshot === null,
                "channels state contract should initialize channelsSnapshot=null");
            assertRegression(state.channelsError === null,
                "channels state contract should initialize channelsError=null");
            assertRegression(state.channelsLastSuccess === null,
                "channels state contract should initialize channelsLastSuccess=null");
            assertRegression(state.whatsappBusy === false,
                "channels state contract should initialize whatsappBusy=false");
            assertRegression(state.whatsappLoginMessage === null,
                "channels state contract should initialize whatsappLoginMessage=null");
            assertRegression(state.whatsappLoginQrDataUrl === null,
                "channels state contract should initialize whatsappLoginQrDataUrl=null");
            assertRegression(state.whatsappLoginConnected === null,
                "channels state contract should initialize whatsappLoginConnected=null");
            assertRegression(state.agentChannelsLoading === false,
                "channels extension contract should initialize agentChannelsLoading=false");
            assertRegression(state.agentChannelsError === null,
                "channels extension contract should initialize agentChannelsError=null");
            assertRegression(state.agentChannelsResult === null,
                "channels extension contract should initialize agentChannelsResult=null");
            assertRegression(Boolean(state.agentChannelsCapability) && state.agentChannelsCapability.method === "channels.status",
                "channels extension contract should initialize agentChannelsCapability");
            summary.push("channels state contract defaults");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const baselineKey = controller.buildToolsEffectiveRequestKey({
                agentId: "main",
                sessionKey: state.sessionKey,
            });

            state.chatModelOverrides[state.sessionKey] = {
                model: "claude-3-7-sonnet",
                modelProvider: "anthropic",
            };
            const overrideKey = controller.buildToolsEffectiveRequestKey({
                agentId: "main",
                sessionKey: state.sessionKey,
            });

            state.chatModelOverrides[state.sessionKey] = null;
            const fallbackKey = controller.buildToolsEffectiveRequestKey({
                agentId: "main",
                sessionKey: state.sessionKey,
            });

            assertRegression(baselineKey !== overrideKey,
                "request-key transition must change when model override changes");
            assertRegression(fallbackKey === baselineKey,
                "request-key transition must fallback to default model when override is null");
            summary.push("request-key transitions");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const first = controller.loadToolsEffective({
                agentId: "main",
                sessionKey: state.sessionKey,
            });
            const firstCall = harness.takeNextCall("tools.effective");

            state.chatModelOverrides[state.sessionKey] = {
                model: "claude-3-7-sonnet",
                modelProvider: "anthropic",
            };
            const second = controller.loadToolsEffective({
                agentId: "main",
                sessionKey: state.sessionKey,
            });
            const secondCall = harness.takeNextCall("tools.effective");

            firstCall.deferred.resolve({
                payload: {
                    source: "stale",
                },
            });
            await first;
            assertRegression(state.toolsEffectiveResult === null,
                "stale response should be suppressed when request key changes");

            secondCall.deferred.resolve({
                payload: {
                    source: "fresh",
                },
            });
            await second;
            assertRegression(Boolean(state.toolsEffectiveResult) &&
                state.toolsEffectiveResult.source === "fresh",
                "fresh response should win after request key transition");
            summary.push("stale-response suppression");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.loadToolsCatalog("main");
            const call = harness.takeNextCall("tools.catalog");
            call.deferred.reject({
                detailCode: "AUTH_UNAUTHORIZED",
                message: "missing scope: operator.read",
            });
            await pending;

            assertRegression(
                state.toolsCatalogError ===
                "This connection is missing operator.read, so tools catalog cannot be loaded yet.",
                "scope-error UX message should be normalized for tools catalog");
            summary.push("scope-error UX");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            controller.setAgentsPanel("files");
            const firstLoad = controller.loadPanelDataForCurrentAgent();
            const firstCall = harness.takeNextCall("gateway.agents.files.list");

            controller.setAgentsPanel("skills");
            const secondLoad = controller.loadPanelDataForCurrentAgent();
            const secondCall = harness.takeNextCall("skills.status");

            firstCall.deferred.resolve({
                payload: {
                    files: [
                        {
                            path: "README.md",
                        },
                    ],
                },
            });
            await firstLoad;
            assertRegression(state.agentFilesResult === null,
                "cross-tab stale file response should be suppressed after panel switch");

            secondCall.deferred.resolve({
                payload: {
                    workspaceDir: "E:/workspace",
                    managedSkillsDir: "E:/workspace/.skills",
                    skills: [],
                },
            });
            await secondLoad;
            assertRegression(Boolean(state.agentSkillsResult),
                "current tab response should remain after cross-tab request overlap");
            summary.push("cross-tab stale suppression");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            controller.setAgentsPanel("channels");
            const channelsLoad = controller.loadPanelDataForCurrentAgent();
            const channelsStatus = harness.takeNextCall("channels.status");
            const channelsRoutes = harness.takeNextCall("gateway.channels.routes");

            controller.setAgentsPanel("cron");
            await controller.loadPanelDataForCurrentAgent();

            channelsStatus.deferred.resolve({
                payload: {
                    ts: 123,
                    channelOrder: ["wechat"],
                    channelLabels: {
                        wechat: "WeChat",
                    },
                    channels: {
                        wechat: {
                            connected: true,
                            accounts: 1,
                        },
                    },
                    channelAccounts: {
                        wechat: [
                            {
                                accountId: "wechat.default",
                                name: "WeChat Default",
                                connected: true,
                            },
                        ],
                    },
                    channelDefaultAccountId: {
                        wechat: "wechat.default",
                    },
                },
            });
            channelsRoutes.deferred.resolve({
                payload: {
                    routes: [],
                },
            });
            await channelsLoad;

            assertRegression(state.agentChannelsResult === null,
                "stale channels response should be suppressed after switching away from channels tab");
            summary.push("channels stale suppression");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "channels";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.loadChannels({
                probe: true,
            });
            const channelsCall = harness.takeNextCall("channels.status");
            assertRegression(Boolean(channelsCall.params) && channelsCall.params.probe === true,
                "channels loader should forward probe flag to canonical channels.status method");
            assertRegression(channelsCall.params.timeoutMs === 8000,
                "channels loader should forward OpenClaw timeout semantics");

            channelsCall.deferred.resolve({
                payload: {
                    ts: 321,
                    channelOrder: ["whatsapp"],
                    channelLabels: {
                        whatsapp: "WhatsApp",
                    },
                    channels: {
                        whatsapp: {
                            connected: true,
                            accounts: 1,
                        },
                    },
                    channelAccounts: {
                        whatsapp: [
                            {
                                accountId: "whatsapp.default",
                                name: "WhatsApp Default",
                                active: true,
                                connected: true,
                            },
                        ],
                    },
                    channelDefaultAccountId: {
                        whatsapp: "whatsapp.default",
                    },
                },
            });
            const snapshot = await pending;

            assertRegression(state.channelsLoading === false,
                "channels loader should reset loading flag after success");
            assertRegression(state.channelsError === null,
                "channels loader should keep error cleared after success");
            assertRegression(Boolean(snapshot) && Array.isArray(snapshot.channelOrder) && snapshot.channelOrder[0] === "whatsapp",
                "channels loader should normalize canonical snapshot payload");
            assertRegression(typeof state.channelsLastSuccess === "number",
                "channels loader should record last success timestamp");
            summary.push("channels success binding");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "channels";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.loadChannels({
                probe: true,
            });
            const channelsCall = harness.takeNextCall("channels.status");
            channelsCall.deferred.reject({
                detailCode: "AUTH_UNAUTHORIZED",
                message: "missing scope: operator.read",
            });
            await pending;

            assertRegression(state.channelsSnapshot === null,
                "channels loader should clear snapshot for missing operator.read scope");
            assertRegression(
                state.channelsError ===
                "This connection is missing operator.read, so channel status cannot be loaded yet.",
                "channels loader should normalize missing-scope message for channel status");
            summary.push("channels scope-error UX");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const refresh = controller.refreshFromConfigSnapshot();
            const configCall = harness.takeNextCall("gateway.config.get");
            const agentsCall = harness.takeNextCall("agents.list");
            configCall.deferred.resolve({ payload: {} });
            agentsCall.deferred.resolve({
                payload: {
                    agents: [
                        { id: "main" },
                    ],
                    defaultId: "main",
                },
            });
            await refresh;
            summary.push("config-coupled refresh orchestration");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "channels";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.logoutWhatsApp({});
            const logoutCall = harness.takeNextCall("channels.logout");
            assertRegression(logoutCall.params && logoutCall.params.channel === "whatsapp",
                "logoutWhatsApp should target the canonical channels.logout method with whatsapp channel");
            logoutCall.deferred.resolve({ payload: { loggedOut: true } });
            const refreshCall = harness.takeNextCall("channels.status");
            assertRegression(refreshCall.params && refreshCall.params.probe === true,
                "logoutWhatsApp should refresh channels snapshot with probe=true after success");
            refreshCall.deferred.resolve({
                payload: {
                    ts: 777,
                    channelOrder: ["whatsapp"],
                    channelLabels: {
                        whatsapp: "WhatsApp",
                    },
                    channels: {
                        whatsapp: {
                            connected: false,
                            accounts: 1,
                        },
                    },
                    channelAccounts: {
                        whatsapp: [
                            {
                                accountId: "whatsapp.default",
                                name: "WhatsApp Default",
                                active: false,
                                connected: false,
                            },
                        ],
                    },
                    channelDefaultAccountId: {
                        whatsapp: "whatsapp.default",
                    },
                },
            });
            const result = await pending;

            assertRegression(result === true,
                "logoutWhatsApp should resolve true after successful logout and refresh");
            assertRegression(state.whatsappBusy === false,
                "logoutWhatsApp should always reset whatsappBusy in finally");
            assertRegression(state.whatsappLoginMessage === "Logged out.",
                "logoutWhatsApp should set the OpenClaw-style logout message");
            assertRegression(Boolean(state.channelsSnapshot) && state.channelsSnapshot.channels.whatsapp.connected === false,
                "logoutWhatsApp should refresh canonical channels snapshot after success");
            summary.push("channels logout refresh");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "channels";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const refresh = controller.refreshFromConfigSnapshot();
            const configCall = harness.takeNextCall("gateway.config.get");
            const agentsCall = harness.takeNextCall("agents.list");
            configCall.deferred.resolve({ payload: {} });
            agentsCall.deferred.resolve({
                payload: {
                    agents: [
                        { id: "main" },
                    ],
                    defaultId: "main",
                },
            });
            const channelsCall = harness.takeNextCall("channels.status");
            channelsCall.deferred.resolve({
                payload: {
                    ts: 888,
                    channelOrder: ["whatsapp"],
                    channelLabels: {
                        whatsapp: "WhatsApp",
                    },
                    channels: {
                        whatsapp: {
                            connected: false,
                            accounts: 1,
                        },
                    },
                    channelAccounts: {
                        whatsapp: [
                            {
                                accountId: "whatsapp.default",
                                name: "WhatsApp Default",
                                active: false,
                                connected: false,
                            },
                        ],
                    },
                    channelDefaultAccountId: {
                        whatsapp: "whatsapp.default",
                    },
                },
            });
            const routesCall = harness.takeNextCall("gateway.channels.routes");
            routesCall.deferred.resolve({
                payload: {
                    routes: [
                        {
                            channel: "whatsapp",
                            accountId: "whatsapp.default",
                            agentId: "main",
                            sessionId: "main",
                        },
                    ],
                },
            });
            await refresh;

            assertRegression(Boolean(state.agentChannelsResult) && state.agentChannelsResult.routes.length === 1,
                "config refresh should explicitly rehydrate the channels panel projection when channels tab is active");
            summary.push("channels config refresh");
        }

        {
            const state = createRegressionState();
            state.whatsappBusy = true;
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const result = await controller.startWhatsAppLogin({
                force: false,
            });

            let shortCircuited = false;
            try {
                harness.takeNextCall("web.login.start");
            } catch (_) {
                shortCircuited = true;
            }

            assertRegression(shortCircuited,
                "startWhatsAppLogin should short-circuit when whatsappBusy is already true");
            assertRegression(result === false,
                "startWhatsAppLogin should resolve false when it short-circuits");
            summary.push("channels login start busy guard");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "channels";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.startWhatsAppLogin({
                force: true,
            });
            const startCall = harness.takeNextCall("web.login.start");
            assertRegression(Boolean(startCall.params) && startCall.params.force === true,
                "startWhatsAppLogin should forward force flag to web.login.start");
            assertRegression(startCall.params.timeoutMs === 30000,
                "startWhatsAppLogin should forward OpenClaw timeout semantics");

            startCall.deferred.resolve({
                payload: {
                    started: true,
                    status: "pending_scan",
                    message: "Scan the QR code to connect WhatsApp.",
                    qrDataUrl: "data:image/png;base64,abc123",
                },
            });
            const result = await pending;

            assertRegression(result === true,
                "startWhatsAppLogin should resolve true after successful start response");
            assertRegression(state.whatsappBusy === false,
                "startWhatsAppLogin should reset whatsappBusy after completion");
            assertRegression(state.whatsappLoginMessage === "Scan the QR code to connect WhatsApp.",
                "startWhatsAppLogin should bind message from payload");
            assertRegression(state.whatsappLoginQrDataUrl === "data:image/png;base64,abc123",
                "startWhatsAppLogin should bind qrDataUrl from payload");
            assertRegression(state.whatsappLoginConnected === null,
                "startWhatsAppLogin should clear connected state until wait resolves");
            summary.push("channels login start success");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "channels";
            state.whatsappLoginQrDataUrl = "data:image/png;base64,abc123";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.waitWhatsAppLogin({});
            const waitCall = harness.takeNextCall("web.login.wait");
            assertRegression(waitCall.params && waitCall.params.timeoutMs === 120000,
                "waitWhatsAppLogin should forward OpenClaw timeout semantics");
            waitCall.deferred.resolve({
                payload: {
                    connected: true,
                    status: "connected",
                    message: "WhatsApp connected.",
                },
            });
            const refreshCall = harness.takeNextCall("channels.status");
            assertRegression(refreshCall.params && refreshCall.params.probe === true,
                "waitWhatsAppLogin should refresh channels snapshot after connected result");
            refreshCall.deferred.resolve({
                payload: {
                    ts: 901,
                    channelOrder: ["whatsapp"],
                    channelLabels: {
                        whatsapp: "WhatsApp",
                    },
                    channels: {
                        whatsapp: {
                            connected: true,
                            accounts: 1,
                        },
                    },
                    channelAccounts: {
                        whatsapp: [
                            {
                                accountId: "whatsapp.default",
                                name: "WhatsApp Default",
                                active: true,
                                connected: true,
                            },
                        ],
                    },
                    channelDefaultAccountId: {
                        whatsapp: "whatsapp.default",
                    },
                },
            });
            const result = await pending;

            assertRegression(result === true,
                "waitWhatsAppLogin should resolve true when connected becomes true");
            assertRegression(state.whatsappLoginConnected === true,
                "waitWhatsAppLogin should bind connected state from payload");
            assertRegression(state.whatsappLoginMessage === "WhatsApp connected.",
                "waitWhatsAppLogin should bind message from payload");
            assertRegression(state.whatsappLoginQrDataUrl === null,
                "waitWhatsAppLogin should clear qrDataUrl after successful connection");
            summary.push("channels login wait success");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.startWhatsAppLogin({
                force: false,
            });
            const startCall = harness.takeNextCall("web.login.start");
            startCall.deferred.reject(new Error("login unavailable"));
            const result = await pending;

            assertRegression(result === false,
                "startWhatsAppLogin should resolve false when start request fails");
            assertRegression(state.whatsappBusy === false,
                "startWhatsAppLogin should reset whatsappBusy after failure");
            assertRegression(String(state.whatsappLoginMessage || "").indexOf("login unavailable") >= 0,
                "startWhatsAppLogin should surface stringified failure message");
            assertRegression(state.whatsappLoginQrDataUrl === null,
                "startWhatsAppLogin should clear qrDataUrl on failure");
            summary.push("channels login start error lifecycle");
        }

        {
            const state = createRegressionState();
            state.whatsappBusy = true;
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const result = await controller.waitWhatsAppLogin({});

            let shortCircuited = false;
            try {
                harness.takeNextCall("web.login.wait");
            } catch (_) {
                shortCircuited = true;
            }

            assertRegression(shortCircuited,
                "waitWhatsAppLogin should short-circuit when whatsappBusy is already true");
            assertRegression(result === false,
                "waitWhatsAppLogin should resolve false when it short-circuits");
            summary.push("channels login wait busy guard");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "channels";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const started = controller.startWhatsAppLogin({
                shouldIgnoreResponse: function () {
                    return true;
                },
                force: false,
            });
            const startCall = harness.takeNextCall("web.login.start");
            startCall.deferred.resolve({
                payload: {
                    started: true,
                    message: "ignored",
                    qrDataUrl: "data:image/png;base64,ignored",
                },
            });
            const startResult = await started;

            assertRegression(startResult === false,
                "startWhatsAppLogin should resolve false when response is ignored");

            const waited = controller.waitWhatsAppLogin({
                shouldIgnoreResponse: function () {
                    return true;
                },
            });
            const waitCall = harness.takeNextCall("web.login.wait");
            waitCall.deferred.resolve({
                payload: {
                    connected: true,
                    message: "ignored",
                },
            });
            const waitResult = await waited;

            assertRegression(waitResult === false,
                "waitWhatsAppLogin should resolve false when response is ignored");

            let refreshQueued = true;
            try {
                harness.takeNextCall("channels.status");
            } catch (_) {
                refreshQueued = false;
            }

            assertRegression(refreshQueued === false,
                "waitWhatsAppLogin should not queue channels refresh when response is ignored");
            summary.push("channels login response-ignore guard");
        }

        {
            const state = createRegressionState();
            state.whatsappBusy = true;
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const result = await controller.logoutWhatsApp({});

            let shortCircuited = false;
            try {
                harness.takeNextCall("channels.logout");
            } catch (_) {
                shortCircuited = true;
            }

            assertRegression(shortCircuited,
                "logoutWhatsApp should short-circuit when whatsappBusy is already true");
            assertRegression(result === false,
                "logoutWhatsApp should resolve false when it short-circuits");
            summary.push("channels logout busy guard");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.logoutWhatsApp({});
            const logoutCall = harness.takeNextCall("channels.logout");
            logoutCall.deferred.reject(new Error("logout unavailable"));
            const result = await pending;

            assertRegression(result === false,
                "logoutWhatsApp should resolve false when logout request fails");
            assertRegression(state.whatsappBusy === false,
                "logoutWhatsApp should reset whatsappBusy after logout failure");
            assertRegression(String(state.whatsappLoginMessage || "").indexOf("logout unavailable") >= 0,
                "logoutWhatsApp should surface stringified failure message");
            summary.push("channels logout error lifecycle");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            controller.setAgentsPanel("channels");
            controller.setSelectedAgentId("reviewer");
            const snapshot = controller.getPersistenceSnapshot();

            assertRegression(snapshot.panel === "channels",
                "persistence snapshot should retain selected control-plane tab");
            assertRegression(snapshot.selectedAgentId === "reviewer",
                "persistence snapshot should retain selected agent id");

            controller.applyPersistenceSnapshot({
                panel: "cron",
                selectedAgentId: "main",
            });
            assertRegression(state.agentsPanel === "cron" && state.agentsSelectedId === "main",
                "persistence restore should deterministically apply panel + selected agent");
            summary.push("persistence restore");
        }

        {
            const state = createRegressionState();
            state.agentIdentityById = {
                main: {
                    id: "main",
                    displayName: "Main Agent",
                },
            };
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            await controller.loadAgentIdentity("main");

            let cacheShortCircuited = false;
            try {
                harness.takeNextCall("agent.identity.get");
            } catch (_) {
                cacheShortCircuited = true;
            }

            assertRegression(cacheShortCircuited,
                "identity cache short-circuit should avoid rpc call when cached");
            assertRegression(Boolean(state.agentIdentityById.main),
                "identity cache short-circuit should preserve existing identity");
            summary.push("identity cache short-circuit");
        }

        {
            const state = createRegressionState();
            state.agentIdentityById = {
                main: {
                    id: "main",
                },
            };
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.loadAgentIdentities(["main", "", "reviewer"]);
            const identityCall = harness.takeNextCall("agent.identity.get");
            assertRegression(identityCall.params && identityCall.params.agentId === "reviewer",
                "batch identity loading should request only missing normalized ids");

            identityCall.deferred.resolve({
                payload: {
                    agent: {
                        id: "reviewer",
                        displayName: "Reviewer",
                    },
                },
            });
            await pending;

            assertRegression(Boolean(state.agentIdentityById.main) && Boolean(state.agentIdentityById.reviewer),
                "batch identity loading should merge newly fetched identities into cache");
            summary.push("identity missing-id filtering");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.loadAgentIdentity("main");
            const identityCall = harness.takeNextCall("agent.identity.get");
            identityCall.deferred.reject(new Error("identity unavailable"));
            await pending;

            assertRegression(state.agentIdentityLoading === false,
                "identity loader should always reset loading flag in finally");
            assertRegression(String(state.agentIdentityError || "").indexOf("identity unavailable") >= 0,
                "identity loader should capture stringified error semantics");
            summary.push("identity error lifecycle");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "skills";
            state.agentSkillsLoading = true;
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            await controller.loadAgentSkills("main");

            let skippedDueToLoading = false;
            try {
                harness.takeNextCall("skills.status");
            } catch (_) {
                skippedDueToLoading = true;
            }

            assertRegression(skippedDueToLoading,
                "agent-skills loader should skip request when loading flag is already true");
            summary.push("agent-skills loading guard");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "skills";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.loadAgentSkills("main");
            const skillsCall = harness.takeNextCall("skills.status");
            skillsCall.deferred.resolve({
                payload: {
                    workspaceDir: "E:/workspace",
                    managedSkillsDir: "E:/workspace/.skills",
                    skills: [
                        {
                            name: "search",
                            description: "Search docs",
                            skillKey: "search",
                        },
                    ],
                },
            });
            await pending;

            assertRegression(state.agentSkillsLoading === false,
                "agent-skills loader should reset loading flag after success");
            assertRegression(state.agentSkillsError === null,
                "agent-skills loader should keep error cleared after success");
            assertRegression(state.agentSkillsAgentId === "main",
                "agent-skills loader should bind report to requested agent id");
            assertRegression(Boolean(state.agentSkillsReport) &&
                Array.isArray(state.agentSkillsReport.skills) &&
                state.agentSkillsReport.skills.length === 1,
                "agent-skills loader should populate OpenClaw-style report payload");
            summary.push("agent-skills success binding");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "skills";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.loadAgentSkills("main");
            const skillsCall = harness.takeNextCall("skills.status");
            controller.setAgentsPanel("cron");

            skillsCall.deferred.resolve({
                payload: {
                    workspaceDir: "E:/workspace",
                    managedSkillsDir: "E:/workspace/.skills",
                    skills: [
                        {
                            name: "ignored",
                            description: "stale",
                            skillKey: "ignored",
                        },
                    ],
                },
            });
            await pending;

            assertRegression(state.agentSkillsReport === null && state.agentSkillsAgentId === null,
                "agent-skills stale response should be suppressed after panel switch");
            summary.push("agent-skills stale suppression");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "skills";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const pending = controller.loadAgentSkills("main");
            const skillsCall = harness.takeNextCall("skills.status");
            skillsCall.deferred.reject(new Error("skills unavailable"));
            await pending;

            assertRegression(state.agentSkillsLoading === false,
                "agent-skills loader should always reset loading flag in finally");
            assertRegression(String(state.agentSkillsError || "").indexOf("skills unavailable") >= 0,
                "agent-skills loader should capture stringified error semantics");
            summary.push("agent-skills error lifecycle");
        }

        return {
            ok: true,
            checks: summary,
        };
    }

    window.BlazeClawAgentsController = {
        createAgentsController,
        runRegressionChecks,
    };
})();
