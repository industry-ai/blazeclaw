(function () {
    function createNodesController(options) {
        const opts = options || {};
        const state = opts.state;
        const request = typeof opts.request === "function" ? opts.request : null;
        const onStateUpdated = typeof opts.onStateUpdated === "function"
            ? opts.onStateUpdated
            : function () {
            };

        if (!state || !request) {
            throw new Error("nodes-controller requires state and request");
        }

        function normalizeNodeListPayload(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const nodes = Array.isArray(source.nodes)
                ? source.nodes.filter(function (entry) {
                    return entry && typeof entry === "object";
                })
                : [];
            return nodes;
        }

        async function loadNodes(options) {
            const opts = options || {};
            const quiet = Boolean(opts.quiet);
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.nodesLoading) {
                return state.nodes;
            }

            state.nodesLoading = true;
            if (!quiet) {
                state.nodesError = null;
                state.lastError = null;
            }
            onStateUpdated();

            try {
                const res = await request("node.list", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.nodes;
                }

                const payload = res && res.payload
                    ? res.payload
                    : res;
                state.nodes = normalizeNodeListPayload(payload);
                return state.nodes;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.nodes;
                }

                if (!quiet) {
                    const message = String(err);
                    state.nodesError = message;
                    state.lastError = message;
                }
                return state.nodes;
            } finally {
                state.nodesLoading = false;
                onStateUpdated();
            }
        }

        return {
            normalizeNodeListPayload,
            loadNodes,
        };
    }

    window.BlazeClawNodesController = {
        createNodesController,
    };
})();
