(function () {
    function getLegacyControllerApi() {
        const api = window.BlazeClawChatController;
        if (!api || typeof api !== "object") {
            throw new Error("BlazeClawChatController unavailable");
        }
        return api;
    }

    function createController(options) {
        const legacyApi = getLegacyControllerApi();
        if (typeof legacyApi.createController !== "function") {
            throw new Error("BlazeClawChatController.createController unavailable");
        }
        return legacyApi.createController(options);
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
