(function () {
    function createDevicesController(options) {
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
            throw new Error("devices-controller requires state and request");
        }

        function normalizeDevicePairs(payload) {
            const source = payload && typeof payload === "object" ? payload : {};
            const pairs = Array.isArray(source.pairs) ? source.pairs : [];
            return pairs.map(function (entry, index) {
                const row = entry && typeof entry === "object" ? entry : {};
                const deviceId = String(
                    row.deviceId ||
                    row.id ||
                    row.requestId ||
                    "device-" + String(index + 1)
                ).trim();
                return {
                    deviceId: deviceId || "device-" + String(index + 1),
                    label: String(row.label || row.name || row.deviceName || deviceId || "").trim(),
                    status: String(row.status || (row.connected ? "connected" : "pending")).trim() || "pending",
                    updatedAtMs: Number(row.updatedAtMs || row.ts || 0) || 0,
                };
            });
        }

        async function loadDevicePairs(options) {
            const opts = options || {};
            const quiet = Boolean(opts.quiet);
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.devicePairsLoading) {
                return state.devicePairs;
            }

            state.devicePairsLoading = true;
            if (!quiet) {
                state.devicePairsError = null;
            }
            onStateUpdated();
            try {
                const res = await request("device.pair.list", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.devicePairs;
                }
                const payload = res && res.payload ? res.payload : res;
                const pairs = normalizeDevicePairs(payload);
                state.devicePairs = pairs;
                if (!state.devicePairSelection && pairs.length > 0) {
                    state.devicePairSelection = String(pairs[0].deviceId || "");
                }
                if (!quiet) {
                    state.devicePairsError = null;
                }
                return pairs;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.devicePairs;
                }
                if (!quiet) {
                    state.devicePairsError = resolveToolsErrorMessage(err, "device pair list");
                    state.lastError = state.devicePairsError;
                }
                return state.devicePairs;
            } finally {
                state.devicePairsLoading = false;
                onStateUpdated();
            }
        }

        function selectDevicePair(deviceId) {
            state.devicePairSelection = String(deviceId || "").trim();
            onStateUpdated();
        }

        async function resolveDevicePair(action, deviceId, options) {
            const methodByAction = {
                approve: "device.pair.approve",
                reject: "device.pair.reject",
                remove: "device.pair.remove",
            };
            const method = methodByAction[String(action || "").trim()] || "";
            const targetDeviceId = String(deviceId || state.devicePairSelection || "").trim();
            const opts = options || {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            if (!method || !requestOverride || !targetDeviceId || state.devicePairsBusy) {
                return null;
            }

            state.devicePairsBusy = true;
            state.devicePairActionStatus = null;
            state.devicePairsError = null;
            onStateUpdated();
            try {
                const response = await requestOverride(method, {
                    deviceId: targetDeviceId,
                });
                const payload = response && response.payload ? response.payload : response;
                state.devicePairActionStatus = method + " ok for " + targetDeviceId;
                if (action === "remove") {
                    state.devicePairs = (state.devicePairs || []).filter(function (entry) {
                        return String(entry && entry.deviceId || "") !== targetDeviceId;
                    });
                } else {
                    state.devicePairs = (state.devicePairs || []).map(function (entry) {
                        if (String(entry && entry.deviceId || "") !== targetDeviceId) {
                            return entry;
                        }
                        const nextStatus = action === "approve" ? "approved" : "rejected";
                        return Object.assign({}, entry, {
                            status: nextStatus,
                            updatedAtMs: Date.now(),
                        });
                    });
                }
                return payload;
            } catch (err) {
                state.devicePairsError = resolveToolsErrorMessage(err, "device pair " + String(action || ""));
                state.lastError = state.devicePairsError;
                return null;
            } finally {
                state.devicePairsBusy = false;
                onStateUpdated();
            }
        }

        return {
            normalizeDevicePairs,
            loadDevicePairs,
            selectDevicePair,
            resolveDevicePair,
        };
    }

    window.BlazeClawDevicesController = {
        createDevicesController,
    };
})();
