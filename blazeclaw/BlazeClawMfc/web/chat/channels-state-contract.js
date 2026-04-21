(function () {
    /**
     * @typedef {Object} BlazeClawChannelsBaselineState
     * @property {boolean} connected
     * @property {boolean} channelsLoading
     * @property {object|null} channelsSnapshot
     * @property {string|null} channelsError
     * @property {number|null} channelsLastSuccess
     * @property {string|null} whatsappLoginMessage
     * @property {string|null} whatsappLoginQrDataUrl
     * @property {boolean|null} whatsappLoginConnected
     * @property {boolean} whatsappBusy
     */

    /**
     * @typedef {Object} BlazeClawChannelsExtensionState
     * @property {boolean} agentChannelsLoading
     * @property {string|null} agentChannelsError
     * @property {object|null} agentChannelsResult
     * @property {{ method: string, agentScoped: boolean, todo: string }} agentChannelsCapability
     */

    /**
     * Applies OpenClaw channels.types baseline defaults and BlazeClaw extension defaults.
     * @param {Record<string, any>} state
     */
    function ensureChannelsStateDefaults(state) {
        if (!state || typeof state !== "object") {
            return;
        }

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

    window.BlazeClawChannelsStateContract = {
        ensureChannelsStateDefaults,
    };
})();
