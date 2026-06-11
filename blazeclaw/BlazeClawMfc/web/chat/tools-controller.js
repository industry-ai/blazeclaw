(function () {
    function createToolsController(options) {
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

        if (!state || !request) {
            throw new Error("tools-controller requires state and request");
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
                catalog
            );

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
                return resolvePreferredServerChatModelValue(
                    activeRow.model,
                    activeRow.modelProvider,
                    catalog
                );
            }

            return defaultModel;
        }

        function buildToolsEffectiveRequestKey(params) {
            const resolvedAgentId = String(params && params.agentId || "").trim() || "main";
            const resolvedSessionKey = String(params && params.sessionKey || "").trim();
            const modelKey = resolveEffectiveToolsModelKey(resolvedSessionKey);
            return resolvedAgentId + ":" + resolvedSessionKey + ":model=" + (modelKey || "(default)");
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

        return {
            resolvePreferredServerChatModelValue,
            normalizeChatModelOverrideValue,
            resolveAgentIdFromSessionKey,
            resolveEffectiveToolsModelKey,
            buildToolsEffectiveRequestKey,
            loadToolsCatalog,
            loadToolsEffective,
            resetToolsEffectiveState,
            refreshVisibleToolsEffectiveForCurrentSession,
        };
    }

    window.BlazeClawToolsController = {
        createToolsController,
    };
})();
