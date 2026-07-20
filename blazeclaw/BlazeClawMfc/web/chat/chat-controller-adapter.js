(function () {
    const BRIDGE_BACKED_LOW_RISK_METHODS = [
        "post",
        "request",
        "flushQueue",
        "loadSessionOptions",
        "loadSessionCompactions",
        "refreshSessionControlState",
        "selectSessionCompaction",
        "branchSessionCompaction",
        "restoreSessionCompaction",
        "loadModelOptions",
        "loadThinkingOptions",
        "switchSession",
        "applyModelSelection",
        "applyThinkingLevel",
        "getControlUiBootstrapConfig",
        "loadSpeechCapabilities",
        "getSpeechSessionStateSnapshot",
        "parseApprovalTokenFromText",
        "executeExecApprovalAction",
        "getOperatorDiagnosticsSnapshot",
    ];

    function getLegacyControllerApi() {
        const api = window.BlazeClawChatController;
        if (!api || typeof api !== "object") {
            throw new Error("BlazeClawChatController unavailable");
        }
        return api;
    }

    function createAdapterControllerFacade(legacyController) {
        const controllerImpl = legacyController && typeof legacyController === "object"
            ? legacyController
            : {};
        const facade = {};

        for (const key of Object.keys(controllerImpl)) {
            const value = controllerImpl[key];
            if (typeof value === "function") {
                facade[key] = function (...args) {
                    return controllerImpl[key](...args);
                };
                continue;
            }

            try {
                Object.defineProperty(facade, key, {
                    enumerable: true,
                    configurable: true,
                    get: function () {
                        return controllerImpl[key];
                    },
                    set: function (nextValue) {
                        controllerImpl[key] = nextValue;
                    },
                });
            } catch (_) {
                facade[key] = value;
            }
        }

        for (const methodName of BRIDGE_BACKED_LOW_RISK_METHODS) {
            if (typeof controllerImpl[methodName] === "function") {
                facade[methodName] = function (...args) {
                    return controllerImpl[methodName](...args);
                };
            }
        }

        facade.__adapterVersion = "step5.0";
        facade.__adapterMode = "legacy+bridge-backed-low-risk";
        facade.__adapterBridgeBackedLowRiskMethods = BRIDGE_BACKED_LOW_RISK_METHODS.slice();

        return facade;
    }

    function createController(options) {
        const legacyApi = getLegacyControllerApi();
        if (typeof legacyApi.createController !== "function") {
            throw new Error("BlazeClawChatController.createController unavailable");
        }
        const legacyController = legacyApi.createController(options);
        return createAdapterControllerFacade(legacyController);
    }

    async function runRegressionChecks() {
        const legacyApi = getLegacyControllerApi();
        if (typeof legacyApi.runRegressionChecks !== "function") {
            throw new Error("BlazeClawChatController.runRegressionChecks unavailable");
        }
        return legacyApi.runRegressionChecks();
    }

    window.BlazeClawChatControllerAdapter = {
        createController,
        runRegressionChecks,
    };
})();
