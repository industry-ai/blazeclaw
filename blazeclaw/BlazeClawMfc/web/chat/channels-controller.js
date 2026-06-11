(function () {
    function createChannelsController(options) {
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
        const isMissingOperatorReadScopeError =
            typeof opts.isMissingOperatorReadScopeError === "function"
                ? opts.isMissingOperatorReadScopeError
                : function () {
                    return false;
                };

        if (!state || !request) {
            throw new Error("channels-controller requires state and request");
        }

        const controllerUtils = window.BlazeClawControllerUtils || {};
        const normalizeBooleanFlag = typeof controllerUtils.normalizeBooleanFlag === "function"
            ? controllerUtils.normalizeBooleanFlag
            : function (value, fallback) {
                if (typeof value === "boolean") {
                    return value;
                }
                if (typeof fallback === "boolean") {
                    return fallback;
                }
                return false;
            };

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
                enabled: normalizeBooleanFlag(
                    entry && entry.enabled,
                    normalizeBooleanFlag(entry && entry.active, false)
                ),
                configured: normalizeBooleanFlag(entry && entry.configured, true),
                linked: normalizeBooleanFlag(
                    entry && entry.linked,
                    normalizeBooleanFlag(entry && entry.active, false)
                ),
                running: normalizeBooleanFlag(
                    entry && entry.running,
                    normalizeBooleanFlag(entry && entry.connected, false)
                ),
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
            const rawDefaultAccountMap =
                source.channelDefaultAccountId &&
                typeof source.channelDefaultAccountId === "object"
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
                const normalizedEntry = normalizeChannelStatusEntry(
                    entry,
                    channelId,
                    rawLabels[channelId]
                );
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

                const explicitDefaultAccountId =
                    String(rawDefaultAccountMap[resolvedChannelId] || "").trim();
                if (explicitDefaultAccountId) {
                    normalized.channelDefaultAccountId[resolvedChannelId] = explicitDefaultAccountId;
                } else if (normalizedAccounts.length > 0) {
                    normalized.channelDefaultAccountId[resolvedChannelId] =
                        normalizedAccounts[0].accountId;
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
                        label: String(
                            normalizedSnapshot.channelLabels &&
                            normalizedSnapshot.channelLabels[channelId] || channelId
                        ),
                        connected: false,
                        accounts: 0,
                    };
                const accountEntries =
                    normalizedSnapshot.channelAccounts &&
                    Array.isArray(normalizedSnapshot.channelAccounts[channelId])
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

        async function loadChannels(options) {
            const optsLoad = options || {};
            const probe = Boolean(optsLoad.probe);
            const shouldIgnoreResponse = typeof optsLoad.shouldIgnoreResponse === "function"
                ? optsLoad.shouldIgnoreResponse
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
            const optsLogin = options || {};
            const shouldIgnoreResponse = typeof optsLogin.shouldIgnoreResponse === "function"
                ? optsLogin.shouldIgnoreResponse
                : null;
            const force = Boolean(optsLogin.force);
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
            const optsWait = options || {};
            const shouldIgnoreResponse = typeof optsWait.shouldIgnoreResponse === "function"
                ? optsWait.shouldIgnoreResponse
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
            const optsLogout = options || {};
            const shouldIgnoreResponse = typeof optsLogout.shouldIgnoreResponse === "function"
                ? optsLogout.shouldIgnoreResponse
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

        return {
            normalizeChannelStatusEntry,
            normalizeChannelAccountEntry,
            normalizeChannelsSnapshot,
            buildAgentChannelsResult,
            loadChannels,
            loadAgentChannels,
            startWhatsAppLogin,
            waitWhatsAppLogin,
            logoutWhatsApp,
        };
    }

    window.BlazeClawChannelsController = {
        createChannelsController,
    };
})();
