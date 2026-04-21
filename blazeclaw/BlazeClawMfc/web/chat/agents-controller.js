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
                method: "gateway.channels.status",
                agentScoped: false,
                todo: "docs/compare/ui.controllers.md#2-mechanical-audit-openclawuisrcuicontrollers",
            };
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
                method: "(none)",
                agentScoped: false,
                todo: "docs/compare/ui.controllers.md#2-mechanical-audit-openclawuisrcuicontrollers",
            };
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
            state.agentSkillsResult = null;
            onStateUpdated();

            try {
                const res = await request("gateway.skills.commands", {
                    agentId: resolvedAgentId,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : null;
                const commands = payload && Array.isArray(payload.commands) ? payload.commands : [];
                state.agentSkillsResult = {
                    commands,
                    count: typeof payload.count === "number" ? payload.count : commands.length,
                    capability: "gateway.skills.commands",
                    agentScoped: false,
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
            state.agentChannelsResult = null;
            onStateUpdated();

            try {
                const statusRes = await request("gateway.channels.status", {});
                const routesRes = await request("gateway.channels.routes", {});
                if (shouldIgnoreResponse()) {
                    return;
                }

                const payload = statusRes && statusRes.payload ? statusRes.payload : null;
                const routePayload = routesRes && routesRes.payload ? routesRes.payload : null;
                const channels = payload && Array.isArray(payload.channels) ? payload.channels : [];
                const routes = routePayload && Array.isArray(routePayload.routes) ? routePayload.routes : [];
                const filteredRoutes = routes.filter(function (route) {
                    return route && (!route.agentId || route.agentId === resolvedAgentId);
                });

                state.agentChannelsResult = {
                    channels,
                    routes: filteredRoutes,
                    capability: state.agentChannelsCapability,
                };
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentChannelsError = resolveToolsErrorMessage(err, "agent channels");
            } finally {
                state.agentChannelsLoading = false;
                onStateUpdated();
            }
        }

        async function loadAgentCron(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!resolvedAgentId) {
                return;
            }

            state.agentCronLoading = true;
            state.agentCronError = null;
            state.agentCronResult = {
                jobs: [],
                capability: state.agentCronCapability,
                status: "not_available",
            };
            state.agentCronLoading = false;
            onStateUpdated();
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
            state.agentChannelsResult = null;
            state.agentCronResult = null;
            onStateUpdated();
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

        return {
            loadAgents,
            loadToolsCatalog,
            loadToolsEffective,
            loadAgentFiles,
            loadAgentFileContent,
            loadAgentSkills,
            loadAgentChannels,
            loadAgentCron,
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
            const secondCall = harness.takeNextCall("gateway.skills.commands");

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
                    commands: [],
                    count: 0,
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
            const channelsStatus = harness.takeNextCall("gateway.channels.status");
            const channelsRoutes = harness.takeNextCall("gateway.channels.routes");

            controller.setAgentsPanel("cron");
            await controller.loadPanelDataForCurrentAgent();

            channelsStatus.deferred.resolve({
                payload: {
                    channels: [
                        {
                            id: "wechat",
                        },
                    ],
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
