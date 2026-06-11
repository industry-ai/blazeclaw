(function () {
    function createInstancesController(options) {
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
            throw new Error("instances-controller requires state and request");
        }

        function normalizePresenceEntries(payload) {
            if (!Array.isArray(payload)) {
                return [];
            }
            return payload.filter(function (entry) {
                return entry && typeof entry === "object";
            });
        }

        function loadPresenceStatusMessage(entries, payloadWasArray) {
            if (!payloadWasArray) {
                return "No presence payload.";
            }
            return entries.length === 0 ? "No instances yet." : null;
        }

        async function loadPresence(options) {
            const opts = options || {};
            const quiet = Boolean(opts.quiet);
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.presenceLoading) {
                return state.presenceEntries;
            }

            state.presenceLoading = true;
            if (!quiet) {
                state.presenceError = null;
                state.presenceStatus = null;
                state.lastError = null;
            }
            onStateUpdated();

            try {
                const res = await request("system-presence", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.presenceEntries;
                }

                const payload = res && Object.prototype.hasOwnProperty.call(res, "payload")
                    ? res.payload
                    : res;
                const entries = normalizePresenceEntries(payload);
                const payloadWasArray = Array.isArray(payload);
                state.presenceEntries = entries;
                state.presenceStatus = loadPresenceStatusMessage(entries, payloadWasArray);
                if (!quiet) {
                    state.presenceError = null;
                }
                return entries;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.presenceEntries;
                }

                if (!quiet) {
                    state.presenceEntries = [];
                    state.presenceStatus = null;
                    state.presenceError = resolveToolsErrorMessage(err, "instance presence");
                    state.lastError = state.presenceError;
                }
                return state.presenceEntries;
            } finally {
                state.presenceLoading = false;
                onStateUpdated();
            }
        }

        return {
            normalizePresenceEntries,
            loadPresenceStatusMessage,
            loadPresence,
        };
    }

    window.BlazeClawInstancesController = {
        createInstancesController,
    };
})();
