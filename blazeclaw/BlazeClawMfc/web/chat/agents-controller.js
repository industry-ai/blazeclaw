(function () {
    function createAgentsController(options) {
        const opts = options || {};
        const state = opts.state;
        if (!state) {
            throw new Error("agents-controller requires state");
        }

        let request = null;
        if (typeof opts.request === "function") {
            request = opts.request;
        }
        if (!request) {
            throw new Error("agents-controller requires request function");
        }

        let onStateUpdated = function () { };
        if (typeof opts.onStateUpdated === "function") {
            onStateUpdated = opts.onStateUpdated;
        }

        let onCronCliExecutionSettled = function () { };
        if (typeof opts.onCronCliExecutionSettled === "function") {
            onCronCliExecutionSettled = opts.onCronCliExecutionSettled;
        }

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

        const supportedPanels = ["overview", "tools", "files", "skills", "channels", "cron", "dreaming", "nodes", "instances", "usage", "observability", "devices"];
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
        if (typeof state.agentFileSelectedPath !== "string" && state.agentFileSelectedPath !== null) {
            state.agentFileSelectedPath = null;
        }
        if (typeof state.agentFileEditDraft !== "string") {
            state.agentFileEditDraft = "";
        }
        if (typeof state.agentFileEditBaseContent !== "string") {
            state.agentFileEditBaseContent = "";
        }
        if (typeof state.agentFileSaveBusy !== "boolean") {
            state.agentFileSaveBusy = false;
        }
        if (typeof state.agentFileSaveError !== "string" && state.agentFileSaveError !== null) {
            state.agentFileSaveError = null;
        }
        if (typeof state.agentFileSaveStatus !== "string" && state.agentFileSaveStatus !== null) {
            state.agentFileSaveStatus = null;
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
        if (typeof state.skillsHubLoading !== "boolean") {
            state.skillsHubLoading = false;
        }
        if (typeof state.skillsHubError !== "string" && state.skillsHubError !== null) {
            state.skillsHubError = null;
        }
        if (!Array.isArray(state.skillsHubResults)) {
            state.skillsHubResults = [];
        }
        if (typeof state.skillsHubQuery !== "string") {
            state.skillsHubQuery = "";
        }
        if (typeof state.skillsDetailLoading !== "boolean") {
            state.skillsDetailLoading = false;
        }
        if (typeof state.skillsDetailError !== "string" && state.skillsDetailError !== null) {
            state.skillsDetailError = null;
        }
        if (!state.skillsDetailResult || typeof state.skillsDetailResult !== "object") {
            state.skillsDetailResult = null;
        }
        if (typeof state.skillsInstallBusy !== "boolean") {
            state.skillsInstallBusy = false;
        }
        if (typeof state.skillsInstallStatus !== "string" && state.skillsInstallStatus !== null) {
            state.skillsInstallStatus = null;
        }
        if (typeof state.skillsEditBusy !== "boolean") {
            state.skillsEditBusy = false;
        }
        if (typeof state.skillsEditError !== "string" && state.skillsEditError !== null) {
            state.skillsEditError = null;
        }
        if (typeof state.skillsEditStatus !== "string" && state.skillsEditStatus !== null) {
            state.skillsEditStatus = null;
        }
        if (typeof state.skillsEditPayload !== "string") {
            state.skillsEditPayload = "{}";
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
        if (typeof state.agentCronBusy !== "boolean") {
            state.agentCronBusy = false;
        }
        if (typeof state.agentCronEditingJobId !== "string" && state.agentCronEditingJobId !== null) {
            state.agentCronEditingJobId = null;
        }
        if (!state.agentCronFieldErrors || typeof state.agentCronFieldErrors !== "object") {
            state.agentCronFieldErrors = {};
        }
        if (!state.agentCronForm || typeof state.agentCronForm !== "object") {
            state.agentCronForm = {
                name: "",
                enabled: true,
                scheduleKind: "every",
                scheduleAt: "",
                everyAmount: "30",
                everyUnit: "minutes",
                cronExpr: "",
                payloadKind: "agentTurn",
                payloadText: "",
                payloadModel: "",
                payloadThinking: "",
                timeoutSeconds: "",
                contextMessages: "",
                deliveryMode: "none",
                deliveryTo: "",
                failureAlertMode: "inherit",
                failureAlertAfter: "",
                failureAlertCooldownSeconds: "",
            };
        }
        if (!Array.isArray(state.agentCronModelSuggestions)) {
            state.agentCronModelSuggestions = [];
        }
        if (typeof state.agentCronCliParseError !== "string" && state.agentCronCliParseError !== null) {
            state.agentCronCliParseError = null;
        }
        if (!state.agentCronCliLastParsed || typeof state.agentCronCliLastParsed !== "object") {
            state.agentCronCliLastParsed = null;
        }
        if (typeof state.agentCronCliExecutionSeq !== "number" || !Number.isFinite(state.agentCronCliExecutionSeq)) {
            state.agentCronCliExecutionSeq = 0;
        }
        if (state.agentCronCliActiveExecution !== null &&
            (typeof state.agentCronCliActiveExecution !== "object" || !state.agentCronCliActiveExecution)) {
            state.agentCronCliActiveExecution = null;
        }
        if (!Object.prototype.hasOwnProperty.call(state, "agentCronCliActiveExecution")) {
            state.agentCronCliActiveExecution = null;
        }

        if (typeof state.dreamingStatusLoading !== "boolean") {
            state.dreamingStatusLoading = false;
        }
        if (typeof state.dreamingStatusError !== "string" && state.dreamingStatusError !== null) {
            state.dreamingStatusError = null;
        }
        if (!state.dreamingStatus || typeof state.dreamingStatus !== "object") {
            state.dreamingStatus = null;
        }
        if (typeof state.dreamingModeSaving !== "boolean") {
            state.dreamingModeSaving = false;
        }
        if (typeof state.dreamDiaryLoading !== "boolean") {
            state.dreamDiaryLoading = false;
        }
        if (typeof state.dreamDiaryActionLoading !== "boolean") {
            state.dreamDiaryActionLoading = false;
        }
        if (typeof state.dreamDiaryError !== "string" && state.dreamDiaryError !== null) {
            state.dreamDiaryError = null;
        }
        if (typeof state.dreamDiaryPath !== "string" && state.dreamDiaryPath !== null) {
            state.dreamDiaryPath = null;
        }
        if (typeof state.dreamDiaryContent !== "string" && state.dreamDiaryContent !== null) {
            state.dreamDiaryContent = null;
        }
        if (state.dreamingUiSubTab !== "scene" &&
            state.dreamingUiSubTab !== "diary" &&
            state.dreamingUiSubTab !== "advanced") {
            state.dreamingUiSubTab = "scene";
        }
        if (state.dreamingAdvancedWaitingSort !== "recent" &&
            state.dreamingAdvancedWaitingSort !== "signals") {
            state.dreamingAdvancedWaitingSort = "recent";
        }
        if (!Array.isArray(state.dreamDiaryParsedEntries)) {
            state.dreamDiaryParsedEntries = [];
        }
        if (!Array.isArray(state.dreamDiaryNavigation)) {
            state.dreamDiaryNavigation = [];
        }
        if (typeof state.dreamDiaryPage !== "number" || !Number.isFinite(state.dreamDiaryPage)) {
            state.dreamDiaryPage = 0;
        }
        if (typeof state.dreamingPhraseIndex !== "number" || !Number.isFinite(state.dreamingPhraseIndex)) {
            state.dreamingPhraseIndex = 0;
        }
        if (typeof state.dreamingPhraseLastSwapMs !== "number" || !Number.isFinite(state.dreamingPhraseLastSwapMs)) {
            state.dreamingPhraseLastSwapMs = 0;
        }
        if (typeof state.dreamingConfigSnapshotHash !== "string" && state.dreamingConfigSnapshotHash !== null) {
            state.dreamingConfigSnapshotHash = null;
        }
        if (!state.dreamingConfigSnapshot || typeof state.dreamingConfigSnapshot !== "object") {
            state.dreamingConfigSnapshot = null;
        }
        if (typeof state.dreamingResolvedPluginId !== "string" && state.dreamingResolvedPluginId !== null) {
            state.dreamingResolvedPluginId = null;
        }
        if (typeof state.lastError !== "string" && state.lastError !== null) {
            state.lastError = null;
        }

        if (typeof state.nodesLoading !== "boolean") {
            state.nodesLoading = false;
        }
        if (!Array.isArray(state.nodes)) {
            state.nodes = [];
        }
        if (typeof state.nodesError !== "string" && state.nodesError !== null) {
            state.nodesError = null;
        }

        if (typeof state.presenceLoading !== "boolean") {
            state.presenceLoading = false;
        }
        if (!Array.isArray(state.presenceEntries)) {
            state.presenceEntries = [];
        }
        if (typeof state.presenceError !== "string" && state.presenceError !== null) {
            state.presenceError = null;
        }
        if (typeof state.presenceStatus !== "string" && state.presenceStatus !== null) {
            state.presenceStatus = null;
        }

        if (typeof state.usageLoading !== "boolean") {
            state.usageLoading = false;
        }
        if (!state.usageResult || typeof state.usageResult !== "object") {
            state.usageResult = null;
        }
        if (!state.usageCostSummary || typeof state.usageCostSummary !== "object") {
            state.usageCostSummary = null;
        }
        if (typeof state.usageError !== "string" && state.usageError !== null) {
            state.usageError = null;
        }
        if (typeof state.usageStartDate !== "string") {
            state.usageStartDate = "";
        }
        if (typeof state.usageEndDate !== "string") {
            state.usageEndDate = "";
        }
        if (!Array.isArray(state.usageSelectedSessions)) {
            state.usageSelectedSessions = [];
        }
        if (!Array.isArray(state.usageSelectedDays)) {
            state.usageSelectedDays = [];
        }
        if (!state.usageTimeSeries || typeof state.usageTimeSeries !== "object") {
            state.usageTimeSeries = null;
        }
        if (typeof state.usageTimeSeriesLoading !== "boolean") {
            state.usageTimeSeriesLoading = false;
        }
        if (typeof state.usageTimeSeriesCursorStart !== "number" && state.usageTimeSeriesCursorStart !== null) {
            state.usageTimeSeriesCursorStart = null;
        }
        if (typeof state.usageTimeSeriesCursorEnd !== "number" && state.usageTimeSeriesCursorEnd !== null) {
            state.usageTimeSeriesCursorEnd = null;
        }
        if (!Array.isArray(state.usageSessionLogs) && state.usageSessionLogs !== null) {
            state.usageSessionLogs = null;
        }
        if (typeof state.usageSessionLogsLoading !== "boolean") {
            state.usageSessionLogsLoading = false;
        }
        if (state.usageTimeZone !== "local" && state.usageTimeZone !== "utc") {
            state.usageTimeZone = "local";
        }
        state.observabilityEnabled = Boolean(state.observabilityEnabled);
        if (typeof state.observabilityLoading !== "boolean") {
            state.observabilityLoading = false;
        }
        if (typeof state.observabilityError !== "string" && state.observabilityError !== null) {
            state.observabilityError = null;
        }
        if (!state.observabilityHealth || typeof state.observabilityHealth !== "object") {
            state.observabilityHealth = null;
        }
        if (!state.observabilityHealthDetails || typeof state.observabilityHealthDetails !== "object") {
            state.observabilityHealthDetails = null;
        }
        if (!state.observabilityTransportStatus || typeof state.observabilityTransportStatus !== "object") {
            state.observabilityTransportStatus = null;
        }
        if (!state.observabilityHeartbeat || typeof state.observabilityHeartbeat !== "object") {
            state.observabilityHeartbeat = null;
        }
        if (!Array.isArray(state.observabilityModels)) {
            state.observabilityModels = [];
        }
        if (!Array.isArray(state.observabilityLogs)) {
            state.observabilityLogs = [];
        }
        if (typeof state.observabilityLogLevel !== "string") {
            state.observabilityLogLevel = "all";
        }
        if (!Number.isFinite(Number(state.observabilityLogLimit)) || Number(state.observabilityLogLimit) <= 0) {
            state.observabilityLogLimit = 50;
        }
        if (typeof state.observabilityPaused !== "boolean") {
            state.observabilityPaused = false;
        }
        if (typeof state.observabilityMethod !== "string" || !state.observabilityMethod.trim()) {
            state.observabilityMethod = "gateway.health";
        }
        if (typeof state.observabilityMethodParams !== "string") {
            state.observabilityMethodParams = "{}";
        }
        if (typeof state.observabilityMethodBusy !== "boolean") {
            state.observabilityMethodBusy = false;
        }
        if (typeof state.observabilityMethodError !== "string" && state.observabilityMethodError !== null) {
            state.observabilityMethodError = null;
        }
        if (typeof state.observabilityMethodResult !== "string" && state.observabilityMethodResult !== null) {
            state.observabilityMethodResult = null;
        }
        if (typeof state.observabilityExportText !== "string" && state.observabilityExportText !== null) {
            state.observabilityExportText = null;
        }
        if (typeof state.observabilityLastUpdatedMs !== "number" && state.observabilityLastUpdatedMs !== null) {
            state.observabilityLastUpdatedMs = null;
        }
        if (typeof state.devicePairsLoading !== "boolean") {
            state.devicePairsLoading = false;
        }
        if (typeof state.devicePairsBusy !== "boolean") {
            state.devicePairsBusy = false;
        }
        if (typeof state.devicePairsError !== "string" && state.devicePairsError !== null) {
            state.devicePairsError = null;
        }
        if (!Array.isArray(state.devicePairs)) {
            state.devicePairs = [];
        }
        if (typeof state.devicePairSelection !== "string") {
            state.devicePairSelection = "";
        }
        if (typeof state.devicePairActionStatus !== "string" && state.devicePairActionStatus !== null) {
            state.devicePairActionStatus = null;
        }

        if (typeof state.agentDreamingLoading !== "boolean") {
            state.agentDreamingLoading = false;
        }
        if (typeof state.agentDreamingError !== "string" && state.agentDreamingError !== null) {
            state.agentDreamingError = null;
        }
        if (!state.agentDreamingResult || typeof state.agentDreamingResult !== "object") {
            state.agentDreamingResult = null;
        }
        if (!state.agentDreamingCapability || typeof state.agentDreamingCapability !== "object") {
            state.agentDreamingCapability = {
                methods: "doctor.memory.* + config.patch + config.schema.lookup",
                agentScoped: false,
                todo: "docs/compare/dreaming.ts/DREAMING_TS_CAPABILITY_PARITY_GAP_ANALYSIS_AND_PORTING_PLAN.md",
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

        function isMissingOperatorReadScopeError(err) {
            const scopeErrors = window.BlazeClawScopeErrors;
            return Boolean(
                scopeErrors &&
                typeof scopeErrors.isMissingOperatorReadScopeError === "function" &&
                scopeErrors.isMissingOperatorReadScopeError(err)
            );
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
        const asRecord = typeof controllerUtils.asRecord === "function"
            ? controllerUtils.asRecord
            : function (value) {
                if (!value || typeof value !== "object" || Array.isArray(value)) {
                    return null;
                }
                return value;
            };
        const normalizeTrimmedString =
            typeof controllerUtils.normalizeTrimmedString === "function"
                ? controllerUtils.normalizeTrimmedString
                : function (value) {
                    if (typeof value !== "string") {
                        return undefined;
                    }
                    const trimmed = value.trim();
                    return trimmed.length > 0 ? trimmed : undefined;
                };
        const normalizeFiniteInt = typeof controllerUtils.normalizeFiniteInt === "function"
            ? controllerUtils.normalizeFiniteInt
            : function (value, fallback) {
                const parsed = Number(value);
                if (!Number.isFinite(parsed)) {
                    return Number.isFinite(Number(fallback))
                        ? Math.max(0, Math.floor(Number(fallback)))
                        : 0;
                }
                return Math.max(0, Math.floor(parsed));
            };
        const normalizeFiniteScore =
            typeof controllerUtils.normalizeFiniteScore === "function"
                ? controllerUtils.normalizeFiniteScore
                : function (value, fallback) {
                    const parsed = Number(value);
                    if (!Number.isFinite(parsed)) {
                        return Number.isFinite(Number(fallback))
                            ? Math.max(0, Math.min(1, Number(fallback)))
                            : 0;
                    }
                    return Math.max(0, Math.min(1, parsed));
                };
        const normalizeStorageMode =
            typeof controllerUtils.normalizeStorageMode === "function"
                ? controllerUtils.normalizeStorageMode
                : function (value) {
                    const normalized = normalizeTrimmedString(value);
                    if (!normalized) {
                        return "inline";
                    }
                    const lowered = normalized.toLowerCase();
                    if (lowered === "inline" || lowered === "separate" || lowered === "both") {
                        return lowered;
                    }
                    return "inline";
                };

        const dreamingController = window.BlazeClawDreamingController || {};
        const normalizeDreamingStatus =
            typeof dreamingController.normalizeDreamingStatus === "function"
                ? dreamingController.normalizeDreamingStatus
                : function () {
                    return null;
                };
        const resolveDreamingPluginId =
            typeof dreamingController.resolveDreamingPluginId === "function"
                ? dreamingController.resolveDreamingPluginId
                : function () {
                    return "memory-core";
                };
        const resolveConfiguredDreaming =
            typeof dreamingController.resolveConfiguredDreaming === "function"
                ? dreamingController.resolveConfiguredDreaming
                : function () {
                    return {
                        pluginId: resolveDreamingPluginId(null),
                        enabled: false,
                    };
                };
        const lookupIncludesDreamingProperty =
            typeof dreamingController.lookupIncludesDreamingProperty === "function"
                ? dreamingController.lookupIncludesDreamingProperty
                : function () {
                    return false;
                };
        const lookupDisallowsUnknownProperties =
            typeof dreamingController.lookupDisallowsUnknownProperties === "function"
                ? dreamingController.lookupDisallowsUnknownProperties
                : function () {
                    return false;
                };
        const getDreamingUiModel = typeof dreamingController.getDreamingUiModel === "function"
            ? function () {
                return dreamingController.getDreamingUiModel(state);
            }
            : function () {
                return {
                    subTab: "scene",
                    waitingSort: "recent",
                    phrase: "",
                    entryCount: 0,
                    diaryPage: 0,
                    diaryEntry: null,
                    shortTermEntries: [],
                    groundedEntries: [],
                    waitingEntries: [],
                    promotedEntries: [],
                    status: null,
                    diaryPath: state.dreamDiaryPath,
                    diaryContent: state.dreamDiaryContent,
                    diaryChipLabel: function (value) {
                        return String(value || "");
                    },
                    flattenDiaryBody: function () {
                        return [];
                    },
                    describeWaitingEntryOrigin: function () {
                        return "Live";
                    },
                    formatRange: function (path, startLine, endLine) {
                        const safePath = String(path || "").trim() || "(unknown)";
                        return safePath + ":" + String(startLine || 1) + "-" + String(endLine || 1);
                    },
                    formatCompactDateTime: function (value) {
                        return String(value || "");
                    },
                };
            };
        const setDreamingSubTab = typeof dreamingController.setDreamingSubTab === "function"
            ? function (tab) {
                dreamingController.setDreamingSubTab(state, tab, onStateUpdated);
            }
            : function () {
            };
        const setDreamingAdvancedWaitingSort =
            typeof dreamingController.setDreamingAdvancedWaitingSort === "function"
                ? function (sort) {
                    dreamingController.setDreamingAdvancedWaitingSort(
                        state,
                        sort,
                        onStateUpdated
                    );
                }
                : function () {
                };
        const setDreamDiaryPage = typeof dreamingController.setDreamDiaryPage === "function"
            ? function (page) {
                dreamingController.setDreamDiaryPage(state, page, onStateUpdated);
            }
            : function () {
            };

        const channelsControllerModule = window.BlazeClawChannelsController || {};
        const channelsRuntime =
            typeof channelsControllerModule.createChannelsController === "function"
                ? channelsControllerModule.createChannelsController({
                    state,
                    request,
                    onStateUpdated,
                    hasSelectedAgentMismatch,
                    resolveToolsErrorMessage,
                    isMissingOperatorReadScopeError,
                })
                : null;

        const usageControllerModule = window.BlazeClawUsageController || {};
        const usageRuntime =
            typeof usageControllerModule.createUsageController === "function"
                ? usageControllerModule.createUsageController({
                    state,
                    request,
                    onStateUpdated,
                    resolveToolsErrorMessage,
                })
                : null;

        const cronControllerModule = window.BlazeClawCronController || {};
        const cronRuntime =
            typeof cronControllerModule.createCronController === "function"
                ? cronControllerModule.createCronController({
                    state,
                    request,
                    onStateUpdated,
                    hasSelectedAgentMismatch,
                    resolveToolsErrorMessage,
                    normalizeCronPaginationMeta,
                    normalizeCronJob,
                    normalizeCronFormState,
                    validateCronForm,
                    hasCronFormErrors,
                    buildCronSchedule,
                    buildCronPayloadWithToolParity,
                    buildCronDelivery,
                    buildCronFailureAlert,
                    applyCronToolParityToMutationPayload,
                    recoverCronFlatJobShape,
                    buildCronJobIdentityParams,
                    resetCronFormToDefaults,
                    jobToForm,
                    buildCloneName,
                    normalizeLowercaseStringOrEmpty,
                })
                : null;

        const filesControllerModule = window.BlazeClawFilesController || {};
        const filesRuntime =
            typeof filesControllerModule.createFilesController === "function"
                ? filesControllerModule.createFilesController({
                    state,
                    request,
                    onStateUpdated,
                    hasSelectedAgentMismatch,
                    resolveToolsErrorMessage,
                })
                : null;

        const skillsControllerModule = window.BlazeClawSkillsController || {};
        const skillsRuntime =
            typeof skillsControllerModule.createSkillsController === "function"
                ? skillsControllerModule.createSkillsController({
                    state,
                    request,
                    onStateUpdated,
                    hasSelectedAgentMismatch,
                    resolveToolsErrorMessage,
                })
                : null;

        const toolsControllerModule = window.BlazeClawToolsController || {};
        const toolsRuntime =
            typeof toolsControllerModule.createToolsController === "function"
                ? toolsControllerModule.createToolsController({
                    state,
                    request,
                    onStateUpdated,
                    hasSelectedAgentMismatch,
                    resolveToolsErrorMessage,
                })
                : null;

        const nodesControllerModule = window.BlazeClawNodesController || {};
        const nodesRuntime =
            typeof nodesControllerModule.createNodesController === "function"
                ? nodesControllerModule.createNodesController({
                    state,
                    request,
                    onStateUpdated,
                })
                : null;

        const instancesControllerModule = window.BlazeClawInstancesController || {};
        const instancesRuntime =
            typeof instancesControllerModule.createInstancesController === "function"
                ? instancesControllerModule.createInstancesController({
                    state,
                    request,
                    onStateUpdated,
                    resolveToolsErrorMessage,
                })
                : null;

        const observabilityControllerModule = window.BlazeClawObservabilityController || {};
        const observabilityRuntime =
            typeof observabilityControllerModule.createObservabilityController === "function"
                ? observabilityControllerModule.createObservabilityController({
                    state,
                    request,
                    onStateUpdated,
                    resolveToolsErrorMessage,
                })
                : null;

        const devicesControllerModule = window.BlazeClawDevicesController || {};
        const devicesRuntime =
            typeof devicesControllerModule.createDevicesController === "function"
                ? devicesControllerModule.createDevicesController({
                    state,
                    request,
                    onStateUpdated,
                    resolveToolsErrorMessage,
                })
                : null;

        const normalizeNodeListPayload =
            nodesRuntime && typeof nodesRuntime.normalizeNodeListPayload === "function"
                ? nodesRuntime.normalizeNodeListPayload
                : function (payload) {
                    const source = payload && typeof payload === "object" ? payload : {};
                    return Array.isArray(source.nodes) ? source.nodes : [];
                };

        const normalizePresenceEntries =
            instancesRuntime && typeof instancesRuntime.normalizePresenceEntries === "function"
                ? instancesRuntime.normalizePresenceEntries
                : function (payload) {
                    return Array.isArray(payload) ? payload : [];
                };

        const loadPresenceStatusMessage =
            instancesRuntime && typeof instancesRuntime.loadPresenceStatusMessage === "function"
                ? instancesRuntime.loadPresenceStatusMessage
                : function (entries, payloadWasArray) {
                    return !payloadWasArray ? "No presence payload." :
                        (Array.isArray(entries) && entries.length === 0 ? "No instances yet." : null);
                };

        const buildUsageDateBounds =
            usageRuntime && typeof usageRuntime.buildUsageDateBounds === "function"
                ? usageRuntime.buildUsageDateBounds
                : function () {
                    const now = new Date();
                    const end = new Date(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate()));
                    const start = new Date(end.getTime() - 29 * 24 * 60 * 60 * 1000);
                    function toIsoDate(value) {
                        const year = value.getUTCFullYear();
                        const month = String(value.getUTCMonth() + 1).padStart(2, "0");
                        const day = String(value.getUTCDate()).padStart(2, "0");
                        return year + "-" + month + "-" + day;
                    }
                    return {
                        startDate: toIsoDate(start),
                        endDate: toIsoDate(end),
                    };
                };

        const formatUtcOffset =
            usageRuntime && typeof usageRuntime.formatUtcOffset === "function"
                ? usageRuntime.formatUtcOffset
                : function (timezoneOffsetMinutes) {
                    const offsetFromUtcMinutes = -timezoneOffsetMinutes;
                    const sign = offsetFromUtcMinutes >= 0 ? "+" : "-";
                    const absMinutes = Math.abs(offsetFromUtcMinutes);
                    const hours = Math.floor(absMinutes / 60);
                    const minutes = absMinutes % 60;
                    return minutes === 0
                        ? "UTC" + sign + String(hours)
                        : "UTC" + sign + String(hours) + ":" + String(minutes).padStart(2, "0");
                };

        const buildUsageDateInterpretationParams =
            usageRuntime && typeof usageRuntime.buildUsageDateInterpretationParams === "function"
                ? usageRuntime.buildUsageDateInterpretationParams
                : function (timeZone) {
                    if (timeZone === "utc") {
                        return {
                            mode: "utc",
                        };
                    }
                    return {
                        mode: "specific",
                        utcOffset: formatUtcOffset(new Date().getTimezoneOffset()),
                    };
                };

        const isLegacyDateInterpretationUnsupportedError =
            usageRuntime && typeof usageRuntime.isLegacyDateInterpretationUnsupportedError === "function"
                ? usageRuntime.isLegacyDateInterpretationUnsupportedError
                : function (err) {
                    const legacyMode = /unexpected property ['"]mode['"]/i;
                    const legacyOffset = /unexpected property ['"]utcoffset['"]/i;
                    const legacyInvalid = /invalid sessions\.usage params/i;
                    const message = String((err && err.message) || err || "");
                    return legacyInvalid.test(message) &&
                        (legacyMode.test(message) || legacyOffset.test(message));
                };

        const shouldIgnoreUsageDetailResponse =
            usageRuntime && typeof usageRuntime.shouldIgnoreUsageDetailResponse === "function"
                ? usageRuntime.shouldIgnoreUsageDetailResponse
                : function (shouldIgnoreResponse, sessionKey) {
                    if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                        return true;
                    }
                    const selectedPanel = String(state.agentsPanel || "");
                    if (selectedPanel !== "usage") {
                        return true;
                    }
                    const selectedSessions = Array.isArray(state.usageSelectedSessions)
                        ? state.usageSelectedSessions
                        : [];
                    if (selectedSessions.length === 0) {
                        return true;
                    }
                    return selectedSessions.indexOf(sessionKey) < 0;
                };

        const runOptionalUsageDetailRequest =
            usageRuntime && typeof usageRuntime.runOptionalUsageDetailRequest === "function"
                ? usageRuntime.runOptionalUsageDetailRequest
                : async function (loadingKey, run) {
                    if (!request || !state.connected || state[loadingKey]) {
                        return;
                    }

                    state[loadingKey] = true;
                    onStateUpdated();

                    try {
                        await run();
                    } catch (_) {
                        // Silently fail optional usage detail endpoints.
                    } finally {
                        state[loadingKey] = false;
                        onStateUpdated();
                    }
                };

        const normalizeChannelStatusEntry =
            channelsRuntime && typeof channelsRuntime.normalizeChannelStatusEntry === "function"
                ? channelsRuntime.normalizeChannelStatusEntry
                : function (entry, channelId, labelFallback) {
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
                };

        const normalizeChannelAccountEntry =
            channelsRuntime && typeof channelsRuntime.normalizeChannelAccountEntry === "function"
                ? channelsRuntime.normalizeChannelAccountEntry
                : function (entry) {
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
                };

        const normalizeChannelsSnapshot =
            channelsRuntime && typeof channelsRuntime.normalizeChannelsSnapshot === "function"
                ? channelsRuntime.normalizeChannelsSnapshot
                : function (payload) {
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
                };

        const buildAgentChannelsResult =
            channelsRuntime && typeof channelsRuntime.buildAgentChannelsResult === "function"
                ? channelsRuntime.buildAgentChannelsResult
                : function (snapshot, routes, agentId) {
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
                };

        const resolvePreferredServerChatModelValue =
            toolsRuntime && typeof toolsRuntime.resolvePreferredServerChatModelValue === "function"
                ? toolsRuntime.resolvePreferredServerChatModelValue
                : function (model, modelProvider, catalog) {
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
                };

        const normalizeChatModelOverrideValue =
            toolsRuntime && typeof toolsRuntime.normalizeChatModelOverrideValue === "function"
                ? toolsRuntime.normalizeChatModelOverrideValue
                : function (overrideValue, catalog) {
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
                };

        const resolveAgentIdFromSessionKey =
            toolsRuntime && typeof toolsRuntime.resolveAgentIdFromSessionKey === "function"
                ? toolsRuntime.resolveAgentIdFromSessionKey
                : function (sessionKey) {
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
                };

        const resolveEffectiveToolsModelKey =
            toolsRuntime && typeof toolsRuntime.resolveEffectiveToolsModelKey === "function"
                ? toolsRuntime.resolveEffectiveToolsModelKey
                : function (sessionKey) {
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
                        return resolvePreferredServerChatModelValue(activeRow.model, activeRow.modelProvider, catalog);
                    }

                    return defaultModel;
                };

        const buildToolsEffectiveRequestKey =
            toolsRuntime && typeof toolsRuntime.buildToolsEffectiveRequestKey === "function"
                ? toolsRuntime.buildToolsEffectiveRequestKey
                : function (params) {
                    const resolvedAgentId = String(params && params.agentId || "").trim() || "main";
                    const resolvedSessionKey = String(params && params.sessionKey || "").trim();
                    const modelKey = resolveEffectiveToolsModelKey(resolvedSessionKey);
                    return resolvedAgentId + ":" + resolvedSessionKey + ":model=" + (modelKey || "(default)");
                };

        const buildAgentFileContentRequestKey =
            filesRuntime && typeof filesRuntime.buildAgentFileContentRequestKey === "function"
                ? filesRuntime.buildAgentFileContentRequestKey
                : function (params) {
                    const resolvedAgentId = String(params && params.agentId || "").trim() || "main";
                    const resolvedPath = String(params && params.path || "").trim();
                    return resolvedAgentId + ":path=" + resolvedPath;
                };

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

        const loadToolsCatalog =
            toolsRuntime && typeof toolsRuntime.loadToolsCatalog === "function"
                ? toolsRuntime.loadToolsCatalog
                : async function (agentId) {
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
                };

        const loadToolsEffective =
            toolsRuntime && typeof toolsRuntime.loadToolsEffective === "function"
                ? toolsRuntime.loadToolsEffective
                : async function (params) {
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
                };

        const resetToolsEffectiveState =
            toolsRuntime && typeof toolsRuntime.resetToolsEffectiveState === "function"
                ? toolsRuntime.resetToolsEffectiveState
                : function () {
                    state.toolsEffectiveResult = null;
                    state.toolsEffectiveResultKey = null;
                    state.toolsEffectiveError = null;
                    state.toolsEffectiveLoading = false;
                    state.toolsEffectiveLoadingKey = null;
                    onStateUpdated();
                };

        const refreshVisibleToolsEffectiveForCurrentSession =
            toolsRuntime && typeof toolsRuntime.refreshVisibleToolsEffectiveForCurrentSession === "function"
                ? toolsRuntime.refreshVisibleToolsEffectiveForCurrentSession
                : function () {
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
                };

        const loadAgentFiles =
            filesRuntime && typeof filesRuntime.loadAgentFiles === "function"
                ? filesRuntime.loadAgentFiles
                : async function () {
                };

        const loadAgentFileContent =
            filesRuntime && typeof filesRuntime.loadAgentFileContent === "function"
                ? filesRuntime.loadAgentFileContent
                : async function () {
                };

        const updateAgentFileDraft =
            filesRuntime && typeof filesRuntime.updateAgentFileDraft === "function"
                ? filesRuntime.updateAgentFileDraft
                : function () {
                };

        const selectAgentFile =
            filesRuntime && typeof filesRuntime.selectAgentFile === "function"
                ? filesRuntime.selectAgentFile
                : async function () {
                };

        async function saveAgentFileContent(params) {
            const resolvedAgentId = String(params && params.agentId || state.agentsSelectedId || "").trim();
            const resolvedPath = String(params && params.path || state.agentFileSelectedPath || "").trim();
            const nextContent = String(params && Object.prototype.hasOwnProperty.call(params, "content")
                ? params.content
                : state.agentFileEditDraft || "");
            if (!request ||
                !state.connected ||
                !resolvedAgentId ||
                !resolvedPath ||
                state.agentsPanel !== "files" ||
                state.agentFileSaveBusy) {
                return null;
            }

            const previousResult = state.agentFileContentResult && typeof state.agentFileContentResult === "object"
                ? JSON.parse(JSON.stringify(state.agentFileContentResult))
                : null;
            const previousBaseContent = String(state.agentFileEditBaseContent || "");

            state.agentFileSaveBusy = true;
            state.agentFileSaveError = null;
            state.agentFileSaveStatus = "Saving...";
            if (state.agentFileContentResult &&
                state.agentFileContentResult.file &&
                typeof state.agentFileContentResult.file === "object") {
                state.agentFileContentResult.file.content = nextContent;
            }
            onStateUpdated();

            try {
                const response = await request("gateway.agents.files.set", {
                    agentId: resolvedAgentId,
                    path: resolvedPath,
                    content: nextContent,
                });
                const payload = response && response.payload ? response.payload : null;
                state.agentFileContentResult = payload;
                const savedFile = payload && payload.file && typeof payload.file === "object"
                    ? payload.file
                    : null;
                const savedContent = String(savedFile && savedFile.content || nextContent);
                state.agentFileEditBaseContent = savedContent;
                state.agentFileEditDraft = savedContent;
                state.agentFileSelectedPath = String(savedFile && (savedFile.path || savedFile.name) || resolvedPath).trim() || resolvedPath;
                state.agentFileSaveStatus = "Saved";
                return payload;
            } catch (err) {
                state.agentFileContentResult = previousResult;
                state.agentFileEditBaseContent = previousBaseContent;
                state.agentFileSaveError = resolveToolsErrorMessage(err, "agent file save");
                state.lastError = state.agentFileSaveError;
                state.agentFileSaveStatus = null;
                return null;
            } finally {
                state.agentFileSaveBusy = false;
                onStateUpdated();
            }
        }

        const isMethodNotFoundError =
            skillsRuntime && typeof skillsRuntime.isMethodNotFoundError === "function"
                ? skillsRuntime.isMethodNotFoundError
                : function (err) {
                    if (!err) {
                        return false;
                    }
                    const text = String((err && err.message) || err).toLowerCase();
                    return text.indexOf("method_not_found") >= 0 || text.indexOf("unknown method") >= 0;
                };

        const normalizeAgentSkillsReportPayload =
            skillsRuntime && typeof skillsRuntime.normalizeAgentSkillsReportPayload === "function"
                ? skillsRuntime.normalizeAgentSkillsReportPayload
                : function (payload) {
                    const candidate = payload && typeof payload === "object" ? payload : {};
                    return {
                        workspaceDir: String(candidate.workspaceDir || "").trim(),
                        managedSkillsDir: String(candidate.managedSkillsDir || "").trim(),
                        skills: Array.isArray(candidate.skills) ? candidate.skills : [],
                    };
                };

        const loadAgentSkills =
            skillsRuntime && typeof skillsRuntime.loadAgentSkills === "function"
                ? skillsRuntime.loadAgentSkills
                : async function () {
                };

        const normalizeSkillsSearchPayload =
            skillsRuntime && typeof skillsRuntime.normalizeSkillsSearchPayload === "function"
                ? skillsRuntime.normalizeSkillsSearchPayload
                : function (payload) {
                    const source = payload && typeof payload === "object" ? payload : {};
                    const entries = Array.isArray(source.skills) ? source.skills : [];
                    return entries.map(function (entry, index) {
                        const row = entry && typeof entry === "object" ? entry : {};
                        const skill = String(row.skill || row.skillKey || row.name || "skill-" + String(index + 1)).trim();
                        return {
                            skill: skill || "skill-" + String(index + 1),
                            name: String(row.name || row.skill || row.skillKey || skill || "").trim(),
                            description: String(row.description || row.summary || "").trim(),
                        };
                    });
                };

        const searchSkillsHub =
            skillsRuntime && typeof skillsRuntime.searchSkillsHub === "function"
                ? skillsRuntime.searchSkillsHub
                : async function () {
                    return state.skillsHubResults;
                };

        const loadSkillDetail =
            skillsRuntime && typeof skillsRuntime.loadSkillDetail === "function"
                ? skillsRuntime.loadSkillDetail
                : async function () {
                    return state.skillsDetailResult;
                };

        const installSkill =
            skillsRuntime && typeof skillsRuntime.installSkill === "function"
                ? skillsRuntime.installSkill
                : async function () {
                    return null;
                };

        const updateSkillConfig =
            skillsRuntime && typeof skillsRuntime.updateSkillConfig === "function"
                ? skillsRuntime.updateSkillConfig
                : async function () {
                    return null;
                };

        const loadNodes =
            nodesRuntime && typeof nodesRuntime.loadNodes === "function"
                ? nodesRuntime.loadNodes
                : async function () {
                    return state.nodes;
                };

        const loadPresence =
            instancesRuntime && typeof instancesRuntime.loadPresence === "function"
                ? instancesRuntime.loadPresence
                : async function () {
                    return state.presenceEntries;
                };

        const loadUsage =
            usageRuntime && typeof usageRuntime.loadUsage === "function"
                ? usageRuntime.loadUsage
                : async function (options) {
                    const opts = options || {};
                    const quiet = Boolean(opts.quiet);
                    const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                        ? opts.shouldIgnoreResponse
                        : null;
                    if (!request || !state.connected || state.usageLoading) {
                        return state.usageResult;
                    }

                    state.usageLoading = true;
                    if (!quiet) {
                        state.usageError = null;
                        state.lastError = null;
                    }
                    onStateUpdated();

                    try {
                        if (!state.usageStartDate || !state.usageEndDate) {
                            const bounds = buildUsageDateBounds();
                            state.usageStartDate = bounds.startDate;
                            state.usageEndDate = bounds.endDate;
                        }

                        const startDate = state.usageStartDate;
                        const endDate = state.usageEndDate;
                        async function runUsageRequests(includeDateInterpretation) {
                            const dateInterpretation = includeDateInterpretation
                                ? buildUsageDateInterpretationParams(state.usageTimeZone)
                                : undefined;
                            return Promise.all([
                                request("sessions.usage", {
                                    startDate,
                                    endDate,
                                    limit: 1000,
                                    includeContextWeight: true,
                                    ...(dateInterpretation || {}),
                                }),
                                request("usage.cost", {
                                    startDate,
                                    endDate,
                                    ...(dateInterpretation || {}),
                                }),
                            ]);
                        }

                        const includeDateInterpretation = true;
                        let sessionsRes = null;
                        let costRes = null;
                        try {
                            const first = await runUsageRequests(includeDateInterpretation);
                            sessionsRes = first[0];
                            costRes = first[1];
                        } catch (firstErr) {
                            if (includeDateInterpretation && isLegacyDateInterpretationUnsupportedError(firstErr)) {
                                const fallback = await runUsageRequests(false);
                                sessionsRes = fallback[0];
                                costRes = fallback[1];
                            } else {
                                throw firstErr;
                            }
                        }

                        if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                            return state.usageResult;
                        }

                        state.usageResult = sessionsRes && sessionsRes.payload
                            ? sessionsRes.payload
                            : sessionsRes;
                        state.usageCostSummary = costRes && costRes.payload
                            ? costRes.payload
                            : costRes;
                        if (!quiet) {
                            state.usageError = null;
                        }
                        return state.usageResult;
                    } catch (err) {
                        if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                            return state.usageResult;
                        }

                        if (!quiet) {
                            state.usageResult = null;
                            state.usageCostSummary = null;
                            state.usageError = resolveToolsErrorMessage(err, "usage");
                            state.lastError = state.usageError;
                        }
                        return state.usageResult;
                    } finally {
                        state.usageLoading = false;
                        onStateUpdated();
                    }
                };

        const loadUsageTimeSeries =
            usageRuntime && typeof usageRuntime.loadUsageTimeSeries === "function"
                ? usageRuntime.loadUsageTimeSeries
                : async function (sessionKey, options) {
                    const key = String(sessionKey || "").trim();
                    const opts = options || {};
                    const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                        ? opts.shouldIgnoreResponse
                        : null;
                    if (!key) {
                        return;
                    }

                    await runOptionalUsageDetailRequest("usageTimeSeriesLoading", async function () {
                        state.usageTimeSeries = null;
                        onStateUpdated();
                        const res = await request("sessions.usage.timeseries", {
                            key,
                        });
                        if (shouldIgnoreUsageDetailResponse(shouldIgnoreResponse, key)) {
                            return;
                        }

                        const payload = res && res.payload ? res.payload : res;
                        state.usageTimeSeries = payload && typeof payload === "object"
                            ? payload
                            : null;
                    }, shouldIgnoreResponse);
                };

        const loadUsageSessionLogs =
            usageRuntime && typeof usageRuntime.loadUsageSessionLogs === "function"
                ? usageRuntime.loadUsageSessionLogs
                : async function (sessionKey, options) {
                    const key = String(sessionKey || "").trim();
                    const opts = options || {};
                    const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                        ? opts.shouldIgnoreResponse
                        : null;
                    if (!key) {
                        return;
                    }

                    await runOptionalUsageDetailRequest("usageSessionLogsLoading", async function () {
                        state.usageSessionLogs = null;
                        onStateUpdated();
                        const res = await request("sessions.usage.logs", {
                            key,
                            limit: 1000,
                        });
                        if (shouldIgnoreUsageDetailResponse(shouldIgnoreResponse, key)) {
                            return;
                        }

                        const payload = res && res.payload ? res.payload : res;
                        const logs = payload && payload.logs;
                        state.usageSessionLogs = Array.isArray(logs)
                            ? logs
                            : null;
                    }, shouldIgnoreResponse);
                };

        const parseObservabilityMethodParams =
            observabilityRuntime && typeof observabilityRuntime.parseObservabilityMethodParams === "function"
                ? observabilityRuntime.parseObservabilityMethodParams
                : function () {
                    return {};
                };

        const normalizeObservabilityLogs =
            observabilityRuntime && typeof observabilityRuntime.normalizeObservabilityLogs === "function"
                ? observabilityRuntime.normalizeObservabilityLogs
                : function (entries) {
                    return Array.isArray(entries) ? entries : [];
                };

        const loadObservability =
            observabilityRuntime && typeof observabilityRuntime.loadObservability === "function"
                ? observabilityRuntime.loadObservability
                : async function () {
                    return state.observabilityHealth;
                };

        const updateObservabilityField =
            observabilityRuntime && typeof observabilityRuntime.updateObservabilityField === "function"
                ? observabilityRuntime.updateObservabilityField
                : function (field, value) {
                    const key = String(field || "").trim();
                    if (!key) {
                        return;
                    }

                    if (key === "method") {
                        state.observabilityMethod = String(value || "");
                    } else if (key === "params") {
                        state.observabilityMethodParams = String(value || "");
                    } else if (key === "logLevel") {
                        state.observabilityLogLevel = String(value || "all").trim().toLowerCase() || "all";
                    } else if (key === "logLimit") {
                        const nextLimit = Number(value);
                        if (Number.isFinite(nextLimit) && nextLimit > 0) {
                            state.observabilityLogLimit = Math.max(1, Math.min(200, Math.floor(nextLimit)));
                        }
                    } else if (key === "paused") {
                        state.observabilityPaused = Boolean(value);
                    }
                    onStateUpdated();
                };

        const invokeObservabilityMethod =
            observabilityRuntime && typeof observabilityRuntime.invokeObservabilityMethod === "function"
                ? observabilityRuntime.invokeObservabilityMethod
                : async function () {
                    return null;
                };

        function buildObservabilityLogsExportText() {
            const rows = Array.isArray(state.observabilityLogs) ? state.observabilityLogs : [];
            return rows.map((entry) => {
                const ts = Number(entry && entry.ts || 0);
                const level = String(entry && entry.level || "info");
                const source = String(entry && entry.source || "gateway");
                const message = String(entry && entry.message || "");
                return `${ts}\t${level}\t${source}\t${message}`;
            }).join("\n");
        }

        const exportObservabilityLogs =
            observabilityRuntime && typeof observabilityRuntime.exportObservabilityLogs === "function"
                ? observabilityRuntime.exportObservabilityLogs
                : function () {
                    state.observabilityExportText = null;
                    onStateUpdated();
                    return state.observabilityExportText;
                };

        const normalizeDevicePairs =
            devicesRuntime && typeof devicesRuntime.normalizeDevicePairs === "function"
                ? devicesRuntime.normalizeDevicePairs
                : function () {
                    return [];
                };

        const loadDevicePairs =
            devicesRuntime && typeof devicesRuntime.loadDevicePairs === "function"
                ? devicesRuntime.loadDevicePairs
                : async function () {
                    return state.devicePairs;
                };

        const selectDevicePair =
            devicesRuntime && typeof devicesRuntime.selectDevicePair === "function"
                ? devicesRuntime.selectDevicePair
                : function (deviceId) {
                    state.devicePairSelection = String(deviceId || "").trim();
                    onStateUpdated();
                };

        const resolveDevicePair =
            devicesRuntime && typeof devicesRuntime.resolveDevicePair === "function"
                ? devicesRuntime.resolveDevicePair
                : async function () {
                    return null;
                };

        const loadChannels =
            channelsRuntime && typeof channelsRuntime.loadChannels === "function"
                ? channelsRuntime.loadChannels
                : async function (options) {
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
                };

        const loadAgentChannels =
            channelsRuntime && typeof channelsRuntime.loadAgentChannels === "function"
                ? channelsRuntime.loadAgentChannels
                : async function (agentId) {
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
                };

        const startWhatsAppLogin =
            channelsRuntime && typeof channelsRuntime.startWhatsAppLogin === "function"
                ? channelsRuntime.startWhatsAppLogin
                : async function (options) {
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
                };

        const waitWhatsAppLogin =
            channelsRuntime && typeof channelsRuntime.waitWhatsAppLogin === "function"
                ? channelsRuntime.waitWhatsAppLogin
                : async function (options) {
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
                };

        const logoutWhatsApp =
            channelsRuntime && typeof channelsRuntime.logoutWhatsApp === "function"
                ? channelsRuntime.logoutWhatsApp
                : async function (options) {
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
                };

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

        function normalizeCronJob(entry) {
            const source = entry && typeof entry === "object"
                ? entry
                : {};
            const payload = source.payload && typeof source.payload === "object"
                ? source.payload
                : {};
            const schedule = source.schedule && typeof source.schedule === "object"
                ? source.schedule
                : {};
            const normalizedId = String(source.id || source.cronId || "").trim();
            const normalizedName = String(source.name || "").trim();

            return {
                id: normalizedId,
                name: normalizedName,
                enabled: source.enabled !== false,
                schedule: {
                    kind: String(schedule.kind || "every").trim() || "every",
                    at: typeof schedule.at === "string" ? schedule.at : "",
                    everyMs: Number.isFinite(Number(schedule.everyMs))
                        ? Number(schedule.everyMs)
                        : null,
                    expr: typeof schedule.expr === "string" ? schedule.expr : "",
                    tz: typeof schedule.tz === "string" ? schedule.tz : "",
                    staggerMs: Number.isFinite(Number(schedule.staggerMs))
                        ? Number(schedule.staggerMs)
                        : null,
                },
                payload: {
                    kind: String(payload.kind || "agentTurn").trim() || "agentTurn",
                    message: typeof payload.message === "string"
                        ? payload.message
                        : "",
                    text: typeof payload.text === "string"
                        ? payload.text
                        : "",
                    model: typeof payload.model === "string"
                        ? payload.model
                        : "",
                    thinking: typeof payload.thinking === "string"
                        ? payload.thinking
                        : "",
                    timeoutSeconds: Number.isFinite(Number(payload.timeoutSeconds))
                        ? Number(payload.timeoutSeconds)
                        : null,
                },
                state: source.state && typeof source.state === "object"
                    ? source.state
                    : null,
            };
        }

        function toNumber(value, fallback) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? parsed : fallback;
        }

        const CRON_CLI_SLASH_PREFIX = "/cron";
        const CRON_CLI_COMMAND_ALIASES = {
            help: ["help", "-h", "--help"],
            status: ["status"],
            list: ["list", "ls"],
            add: ["add", "create"],
            edit: ["edit", "update"],
            remove: ["rm", "remove", "delete"],
            run: ["run"],
            runs: ["runs"],
            wake: ["wake"],
        };
        const CRON_CLI_ALLOWED_OPTIONS = {
            help: {
                json: true,
            },
            status: {
                json: true,
            },
            list: {
                all: true,
                json: true,
                includeDisabled: true,
                enabled: true,
                limit: true,
                offset: true,
                query: true,
                sortBy: true,
                sortDir: true,
            },
            add: {
                name: true,
                description: true,
                disabled: true,
                deleteAfterRun: true,
                keepAfterRun: true,
                agent: true,
                session: true,
                sessionKey: true,
                wake: true,
                at: true,
                every: true,
                cron: true,
                tz: true,
                stagger: true,
                exact: true,
                systemEvent: true,
                message: true,
                thinking: true,
                model: true,
                timeoutSeconds: true,
                lightContext: true,
                tools: true,
                announce: true,
                deliver: true,
                noDeliver: true,
                channel: true,
                to: true,
                account: true,
                bestEffortDeliver: true,
                json: true,
            },
            edit: {
                name: true,
                description: true,
                enable: true,
                disable: true,
                deleteAfterRun: true,
                keepAfterRun: true,
                session: true,
                agent: true,
                clearAgent: true,
                sessionKey: true,
                clearSessionKey: true,
                wake: true,
                at: true,
                every: true,
                cron: true,
                tz: true,
                stagger: true,
                exact: true,
                systemEvent: true,
                message: true,
                thinking: true,
                model: true,
                timeoutSeconds: true,
                lightContext: true,
                noLightContext: true,
                tools: true,
                clearTools: true,
                announce: true,
                deliver: true,
                noDeliver: true,
                channel: true,
                to: true,
                account: true,
                bestEffortDeliver: true,
                noBestEffortDeliver: true,
                failureAlert: true,
                noFailureAlert: true,
                failureAlertAfter: true,
                failureAlertChannel: true,
                failureAlertTo: true,
                failureAlertCooldown: true,
                failureAlertMode: true,
                failureAlertAccountId: true,
                json: true,
            },
            remove: {
                json: true,
                id: true,
                jobId: true,
            },
            run: {
                due: true,
                mode: true,
                json: true,
                id: true,
                jobId: true,
            },
            runs: {
                id: true,
                jobId: true,
                limit: true,
                offset: true,
                scope: true,
                status: true,
                statuses: true,
                deliveryStatus: true,
                deliveryStatuses: true,
                query: true,
                sortDir: true,
                json: true,
            },
            wake: {
                mode: true,
                json: true,
            },
        };
        const CRON_CLI_USAGE = {
            root: "/cron <command> [options]",
            help: "/cron help [command]",
            status: "/cron status [--json]",
            list: "/cron list [--all|--include-disabled] [--limit <n>] [--offset <n>] [--query <text>]",
            add: "/cron add --name <name> (--at <when>|--every <duration>|--cron <expr>) (--system-event <text>|--message <text>)",
            edit: "/cron edit <id> [patch options]",
            remove: "/cron remove <id>",
            run: "/cron run <id> [--due|--mode <force|due>]",
            runs: "/cron runs --id <id> [--limit <n>]",
            wake: "/cron wake [--mode <now|next-heartbeat>]",
        };
        const CRON_CLI_COMMAND_HELP_ORDER = [
            "help",
            "status",
            "list",
            "add",
            "edit",
            "remove",
            "run",
            "runs",
            "wake",
        ];

        function toCronCliOptionKey(rawKey) {
            return String(rawKey || "")
                .trim()
                .replace(/^--?/, "")
                .replace(/-([a-z])/g, function (_, letter) {
                    return String(letter || "").toUpperCase();
                });
        }

        function tokenizeCronCliInput(rawInput) {
            const source = String(rawInput || "");
            const tokens = [];
            let current = "";
            let inQuote = false;
            let quote = "";
            let escaping = false;

            function pushToken() {
                if (current.length > 0) {
                    tokens.push(current);
                    current = "";
                }
            }

            for (let index = 0; index < source.length; index += 1) {
                const ch = source.charAt(index);

                if (escaping) {
                    current += ch;
                    escaping = false;
                    continue;
                }

                if (ch === "\\") {
                    escaping = true;
                    continue;
                }

                if (inQuote) {
                    if (ch === quote) {
                        inQuote = false;
                        quote = "";
                    } else {
                        current += ch;
                    }
                    continue;
                }

                if (ch === '"' || ch === "'") {
                    inQuote = true;
                    quote = ch;
                    continue;
                }

                if (/\s/.test(ch)) {
                    pushToken();
                    continue;
                }

                current += ch;
            }

            if (escaping) {
                current += "\\";
            }
            if (inQuote) {
                return {
                    ok: false,
                    errorCode: "unclosed_quote",
                    error: "Unclosed quote in /cron command.",
                    tokens: tokens,
                };
            }

            pushToken();
            return {
                ok: true,
                tokens: tokens,
            };
        }

        function resolveCronCliCanonicalCommand(rawCommand) {
            const normalized = normalizeLowercaseStringOrEmpty(rawCommand);
            if (!normalized) {
                return "help";
            }

            const canonicalCommands = Object.keys(CRON_CLI_COMMAND_ALIASES);
            for (let index = 0; index < canonicalCommands.length; index += 1) {
                const candidate = canonicalCommands[index];
                const aliases = CRON_CLI_COMMAND_ALIASES[candidate] || [];
                if (aliases.indexOf(normalized) >= 0) {
                    return candidate;
                }
            }

            return "";
        }

        function coerceCronCliOptionValue(value) {
            if (value === true) {
                return true;
            }

            const raw = String(value || "").trim();
            if (!raw) {
                return "";
            }

            const lowered = normalizeLowercaseStringOrEmpty(raw);
            if (lowered === "true") {
                return true;
            }
            if (lowered === "false") {
                return false;
            }
            if (/^-?\d+$/.test(raw)) {
                const parsedInt = Number(raw);
                if (Number.isFinite(parsedInt)) {
                    return parsedInt;
                }
            }
            if (/^-?\d+\.\d+$/.test(raw)) {
                const parsedFloat = Number(raw);
                if (Number.isFinite(parsedFloat)) {
                    return parsedFloat;
                }
            }

            return raw;
        }

        function parseCronCliSlashCommand(input) {
            const rawInput = String(input || "");
            const trimmedInput = rawInput.trim();
            if (!trimmedInput) {
                return {
                    handled: false,
                    reason: "empty",
                };
            }

            if (trimmedInput.slice(0, CRON_CLI_SLASH_PREFIX.length).toLowerCase() !== CRON_CLI_SLASH_PREFIX) {
                return {
                    handled: false,
                    reason: "not-cron",
                };
            }

            const tokenized = tokenizeCronCliInput(trimmedInput);
            if (!tokenized.ok) {
                return {
                    handled: true,
                    ok: false,
                    errorCode: tokenized.errorCode || "parse_error",
                    error: tokenized.error,
                    rawInput: rawInput,
                    tokens: tokenized.tokens || [],
                };
            }

            const tokens = tokenized.tokens || [];
            const firstToken = normalizeLowercaseStringOrEmpty(tokens[0]).replace(/^\//, "");
            if (firstToken !== CRON_CLI_SLASH_PREFIX.slice(1)) {
                return {
                    handled: false,
                    reason: "not-cron",
                };
            }

            const rawCommand = tokens.length > 1
                ? String(tokens[1] || "")
                : "help";
            const canonicalCommand = resolveCronCliCanonicalCommand(rawCommand);
            if (!canonicalCommand) {
                return {
                    handled: true,
                    ok: false,
                    errorCode: "unknown_command",
                    error: "Unknown /cron command: " + String(rawCommand || "").trim(),
                    rawInput: rawInput,
                    tokens: tokens,
                };
            }

            const allowedOptions = CRON_CLI_ALLOWED_OPTIONS[canonicalCommand] || {};
            const args = {
                positionals: [],
                options: {},
            };

            let index = 2;
            while (index < tokens.length) {
                const token = String(tokens[index] || "");
                if (!token) {
                    index += 1;
                    continue;
                }

                if (token.indexOf("--") === 0) {
                    const equalIndex = token.indexOf("=");
                    const rawOption = equalIndex >= 0
                        ? token.slice(0, equalIndex)
                        : token;
                    const optionKey = toCronCliOptionKey(rawOption);
                    if (!allowedOptions[optionKey]) {
                        return {
                            handled: true,
                            ok: false,
                            errorCode: "unknown_option",
                            error: "Unknown option for /cron " + canonicalCommand + ": " + rawOption,
                            rawInput: rawInput,
                            tokens: tokens,
                        };
                    }

                    let value = true;
                    if (equalIndex >= 0) {
                        value = token.slice(equalIndex + 1);
                    } else {
                        const nextToken = tokens[index + 1];
                        if (typeof nextToken === "string" && nextToken.indexOf("--") !== 0) {
                            value = nextToken;
                            index += 1;
                        }
                    }

                    const coercedValue = coerceCronCliOptionValue(value);
                    if (Object.prototype.hasOwnProperty.call(args.options, optionKey)) {
                        const existing = args.options[optionKey];
                        if (Array.isArray(existing)) {
                            existing.push(coercedValue);
                        } else {
                            args.options[optionKey] = [existing, coercedValue];
                        }
                    } else {
                        args.options[optionKey] = coercedValue;
                    }
                    index += 1;
                    continue;
                }

                if (token.charAt(0) === "-") {
                    return {
                        handled: true,
                        ok: false,
                        errorCode: "unsupported_short_option",
                        error: "Unsupported short option in /cron command: " + token,
                        rawInput: rawInput,
                        tokens: tokens,
                    };
                }

                args.positionals.push(token);
                index += 1;
            }

            return {
                handled: true,
                ok: true,
                kind: "cron-cli",
                command: canonicalCommand,
                rawCommand: String(rawCommand || "").trim(),
                args: args,
                rawInput: rawInput,
                tokens: tokens,
            };
        }

        function tryParseCronCliSlashCommand(input) {
            const parsed = parseCronCliSlashCommand(input);
            if (parsed && parsed.handled) {
                state.agentCronCliLastParsed = parsed;
                state.agentCronCliParseError = parsed.ok === false
                    ? String(parsed.error || "Invalid /cron command")
                    : null;
                onStateUpdated();
            }
            return parsed;
        }

        function formatCronCliSupportedCommands() {
            return CRON_CLI_COMMAND_HELP_ORDER.slice();
        }

        function buildCronCliHelpContract(command) {
            const resolved = resolveCronCliCanonicalCommand(command || "") || "help";
            const usage = CRON_CLI_USAGE[resolved] || CRON_CLI_USAGE.root;
            return {
                ok: true,
                code: "help",
                command: resolved,
                usage: usage,
                commands: formatCronCliSupportedCommands(),
                rootUsage: CRON_CLI_USAGE.root,
            };
        }

        function buildCronCliErrorContract(code, message, details, command) {
            return {
                ok: false,
                code: String(code || "validation_error"),
                message: String(message || "Invalid /cron command"),
                details: details || null,
                command: command || null,
                rootUsage: CRON_CLI_USAGE.root,
                commands: formatCronCliSupportedCommands(),
            };
        }

        function resolveCronCliJobId(args) {
            const parsedArgs = args && typeof args === "object"
                ? args
                : {};
            const options = parsedArgs.options && typeof parsedArgs.options === "object"
                ? parsedArgs.options
                : {};
            const positionals = Array.isArray(parsedArgs.positionals)
                ? parsedArgs.positionals
                : [];

            const optionId = String(options.id || options.jobId || "").trim();
            if (optionId) {
                return optionId;
            }

            const positionalId = String(positionals[0] || "").trim();
            return positionalId;
        }

        function parseCronCliIntegerOption(value, fieldName) {
            if (value === undefined || value === null || value === "") {
                return {
                    ok: true,
                    value: null,
                };
            }

            const parsed = Number(value);
            if (!Number.isFinite(parsed) || Math.floor(parsed) !== parsed || parsed < 0) {
                return {
                    ok: false,
                    error: fieldName + " must be a non-negative integer.",
                };
            }

            return {
                ok: true,
                value: parsed,
            };
        }

        function resolveCronCliWakeMode(value) {
            const normalized = normalizeLowercaseStringOrEmpty(value);
            if (!normalized) {
                return "now";
            }
            if (normalized === "now") {
                return "now";
            }
            if (normalized === "next-heartbeat" ||
                normalized === "nextheartbeat" ||
                normalized === "next_heartbeat") {
                return "next-heartbeat";
            }
            return "";
        }

        function buildCronCliCommandPlan(parsed) {
            if (!parsed || parsed.handled !== true) {
                return {
                    handled: false,
                };
            }

            if (parsed.ok !== true) {
                return {
                    handled: true,
                    command: parsed.command || null,
                    ux: buildCronCliErrorContract(
                        parsed.errorCode || "parse_error",
                        parsed.error || "Invalid /cron command",
                        {
                            rawInput: parsed.rawInput || "",
                        },
                        parsed.command || null
                    ),
                };
            }

            const command = String(parsed.command || "").trim();
            const args = parsed.args && typeof parsed.args === "object"
                ? parsed.args
                : {
                    positionals: [],
                    options: {},
                };
            const options = args.options && typeof args.options === "object"
                ? args.options
                : {};

            if (command === "help") {
                const helpTarget = String((args.positionals && args.positionals[0]) || "").trim();
                const resolvedTarget = resolveCronCliCanonicalCommand(helpTarget || "") || "help";
                return {
                    handled: true,
                    command: "help",
                    method: null,
                    params: {},
                    ux: buildCronCliHelpContract(resolvedTarget),
                };
            }

            if (command === "status") {
                return {
                    handled: true,
                    command: "status",
                    method: "cron.status",
                    params: {},
                    ux: {
                        ok: true,
                        code: "planned",
                        command: "status",
                        usage: CRON_CLI_USAGE.status,
                    },
                };
            }

            if (command === "list") {
                const normalized = {
                    includeDisabled: options.all === true || options.includeDisabled === true,
                    enabled: typeof options.enabled === "string"
                        ? String(options.enabled).trim()
                        : undefined,
                    query: typeof options.query === "string"
                        ? String(options.query).trim()
                        : undefined,
                    sortBy: typeof options.sortBy === "string"
                        ? String(options.sortBy).trim()
                        : undefined,
                    sortDir: typeof options.sortDir === "string"
                        ? String(options.sortDir).trim()
                        : undefined,
                };

                const limitResult = parseCronCliIntegerOption(options.limit, "limit");
                if (!limitResult.ok) {
                    return {
                        handled: true,
                        command: "list",
                        ux: buildCronCliErrorContract("invalid_range", limitResult.error, null, "list"),
                    };
                }
                const offsetResult = parseCronCliIntegerOption(options.offset, "offset");
                if (!offsetResult.ok) {
                    return {
                        handled: true,
                        command: "list",
                        ux: buildCronCliErrorContract("invalid_range", offsetResult.error, null, "list"),
                    };
                }

                if (limitResult.value !== null) {
                    normalized.limit = limitResult.value;
                }
                if (offsetResult.value !== null) {
                    normalized.offset = offsetResult.value;
                }

                return {
                    handled: true,
                    command: "list",
                    method: "cron.list",
                    params: normalized,
                    ux: {
                        ok: true,
                        code: "planned",
                        command: "list",
                        usage: CRON_CLI_USAGE.list,
                    },
                };
            }

            if (command === "remove") {
                const jobId = resolveCronCliJobId(args);
                if (!jobId) {
                    return {
                        handled: true,
                        command: "remove",
                        ux: buildCronCliErrorContract(
                            "missing_required",
                            "Missing required job id for /cron remove.",
                            {
                                usage: CRON_CLI_USAGE.remove,
                            },
                            "remove"
                        ),
                    };
                }

                return {
                    handled: true,
                    command: "remove",
                    method: "cron.remove",
                    params: buildCronJobIdentityParams(jobId),
                    ux: {
                        ok: true,
                        code: "planned",
                        command: "remove",
                        usage: CRON_CLI_USAGE.remove,
                    },
                };
            }

            if (command === "run") {
                const jobId = resolveCronCliJobId(args);
                if (!jobId) {
                    return {
                        handled: true,
                        command: "run",
                        ux: buildCronCliErrorContract(
                            "missing_required",
                            "Missing required job id for /cron run.",
                            {
                                usage: CRON_CLI_USAGE.run,
                            },
                            "run"
                        ),
                    };
                }

                let mode = "force";
                if (options.due === true) {
                    mode = "due";
                }
                if (typeof options.mode === "string" && String(options.mode).trim()) {
                    const requestedMode = normalizeLowercaseStringOrEmpty(options.mode);
                    if (requestedMode !== "due" && requestedMode !== "force") {
                        return {
                            handled: true,
                            command: "run",
                            ux: buildCronCliErrorContract(
                                "invalid_enum",
                                "run mode must be due or force.",
                                null,
                                "run"
                            ),
                        };
                    }
                    mode = requestedMode;
                }

                return {
                    handled: true,
                    command: "run",
                    method: "cron.run",
                    params: Object.assign({}, buildCronJobIdentityParams(jobId), {
                        mode: mode,
                    }),
                    ux: {
                        ok: true,
                        code: "planned",
                        command: "run",
                        usage: CRON_CLI_USAGE.run,
                    },
                };
            }

            if (command === "runs") {
                const jobId = resolveCronCliJobId(args);
                if (!jobId) {
                    return {
                        handled: true,
                        command: "runs",
                        ux: buildCronCliErrorContract(
                            "missing_required",
                            "Missing required --id for /cron runs.",
                            {
                                usage: CRON_CLI_USAGE.runs,
                            },
                            "runs"
                        ),
                    };
                }

                const limitResult = parseCronCliIntegerOption(options.limit, "limit");
                if (!limitResult.ok) {
                    return {
                        handled: true,
                        command: "runs",
                        ux: buildCronCliErrorContract("invalid_range", limitResult.error, null, "runs"),
                    };
                }

                const params = Object.assign({}, buildCronJobIdentityParams(jobId), {
                    scope: "job",
                    limit: limitResult.value !== null
                        ? limitResult.value
                        : 50,
                });

                return {
                    handled: true,
                    command: "runs",
                    method: "cron.runs",
                    params: params,
                    ux: {
                        ok: true,
                        code: "planned",
                        command: "runs",
                        usage: CRON_CLI_USAGE.runs,
                    },
                };
            }

            if (command === "wake") {
                const wakeMode = resolveCronCliWakeMode(options.mode);
                if (!wakeMode) {
                    return {
                        handled: true,
                        command: "wake",
                        ux: buildCronCliErrorContract(
                            "invalid_enum",
                            "wake mode must be now or next-heartbeat.",
                            null,
                            "wake"
                        ),
                    };
                }

                return {
                    handled: true,
                    command: "wake",
                    method: "wake",
                    params: {
                        mode: wakeMode,
                    },
                    ux: {
                        ok: true,
                        code: "planned",
                        command: "wake",
                        usage: CRON_CLI_USAGE.wake,
                    },
                };
            }

            if (command === "add" || command === "edit") {
                const scheduleSignals = [
                    options.at !== undefined,
                    options.every !== undefined,
                    options.cron !== undefined,
                ].filter(Boolean).length;
                if (command === "add" && scheduleSignals !== 1) {
                    return {
                        handled: true,
                        command: command,
                        ux: buildCronCliErrorContract(
                            "missing_required",
                            "add requires exactly one schedule: --at, --every, or --cron.",
                            {
                                usage: CRON_CLI_USAGE.add,
                            },
                            command
                        ),
                    };
                }
                if (command === "edit" && scheduleSignals > 1) {
                    return {
                        handled: true,
                        command: command,
                        ux: buildCronCliErrorContract(
                            "invalid_combination",
                            "edit accepts at most one schedule change: --at, --every, or --cron.",
                            {
                                usage: CRON_CLI_USAGE.edit,
                            },
                            command
                        ),
                    };
                }

                const payloadSignals = [
                    options.systemEvent !== undefined,
                    options.message !== undefined,
                ].filter(Boolean).length;
                if (command === "add" && payloadSignals !== 1) {
                    return {
                        handled: true,
                        command: command,
                        ux: buildCronCliErrorContract(
                            "missing_required",
                            "add requires exactly one payload: --system-event or --message.",
                            {
                                usage: CRON_CLI_USAGE.add,
                            },
                            command
                        ),
                    };
                }
                if (command === "edit" && payloadSignals > 1) {
                    return {
                        handled: true,
                        command: command,
                        ux: buildCronCliErrorContract(
                            "invalid_combination",
                            "edit accepts at most one payload change: --system-event or --message.",
                            {
                                usage: CRON_CLI_USAGE.edit,
                            },
                            command
                        ),
                    };
                }

                let method = "cron.add";
                const params = {
                    options: Object.assign({}, options),
                    positionals: Array.isArray(args.positionals)
                        ? args.positionals.slice()
                        : [],
                };
                if (command === "add") {
                    const name = String(options.name || "").trim();
                    if (!name) {
                        return {
                            handled: true,
                            command: "add",
                            ux: buildCronCliErrorContract(
                                "missing_required",
                                "Missing required --name for /cron add.",
                                {
                                    usage: CRON_CLI_USAGE.add,
                                },
                                "add"
                            ),
                        };
                    }
                    params.options.name = name;
                }
                if (command === "edit") {
                    method = "cron.update";
                    const editId = resolveCronCliJobId(args);
                    if (!editId) {
                        return {
                            handled: true,
                            command: "edit",
                            ux: buildCronCliErrorContract(
                                "missing_required",
                                "Missing required job id for /cron edit.",
                                {
                                    usage: CRON_CLI_USAGE.edit,
                                },
                                "edit"
                            ),
                        };
                    }
                    params.job = buildCronJobIdentityParams(editId);
                }

                return {
                    handled: true,
                    command: command,
                    method: method,
                    params: params,
                    ux: {
                        ok: true,
                        code: "planned",
                        command: command,
                        usage: command === "add"
                            ? CRON_CLI_USAGE.add
                            : CRON_CLI_USAGE.edit,
                    },
                };
            }

            return {
                handled: true,
                command: command,
                ux: buildCronCliErrorContract(
                    "not_implemented",
                    "Command is not yet mapped in slash-command planner: " + command,
                    null,
                    command
                ),
            };
        }

        function planCronCliSlashCommand(input) {
            const parsed = parseCronCliSlashCommand(input);
            const plan = buildCronCliCommandPlan(parsed);
            if (plan && plan.handled) {
                state.agentCronCliLastParsed = parsed;
                state.agentCronCliParseError = plan.ux && plan.ux.ok === false
                    ? String(plan.ux.message || "Invalid /cron command")
                    : null;
                onStateUpdated();
            }
            return plan;
        }

        function hasCronCliOption(options, key) {
            const source = options && typeof options === "object"
                ? options
                : {};
            return Object.prototype.hasOwnProperty.call(source, key);
        }

        function parseCronCliEveryDurationToMs(rawValue) {
            const value = String(rawValue || "").trim();
            if (!value) {
                return {
                    ok: false,
                    error: "every schedule requires a duration value.",
                };
            }

            const match = /^(\d+)(ms|s|m|h|d)?$/i.exec(value);
            if (!match) {
                return {
                    ok: false,
                    error: "every schedule must use <number>[ms|s|m|h|d], for example 30m.",
                };
            }

            const amount = Number(match[1]);
            if (!Number.isFinite(amount) || amount <= 0) {
                return {
                    ok: false,
                    error: "every schedule duration must be greater than 0.",
                };
            }

            const unit = normalizeLowercaseStringOrEmpty(match[2] || "m");
            const multiplier = unit === "ms"
                ? 1
                : unit === "s"
                    ? 1000
                    : unit === "m"
                        ? 60000
                        : unit === "h"
                            ? 3600000
                            : 86400000;

            return {
                ok: true,
                value: Math.floor(amount * multiplier),
            };
        }

        function buildCronCliScheduleFromOptions(command, options) {
            const hasAt = hasCronCliOption(options, "at");
            const hasEvery = hasCronCliOption(options, "every");
            const hasCron = hasCronCliOption(options, "cron");
            const scheduleSignals = [hasAt, hasEvery, hasCron].filter(Boolean).length;

            if (command === "add" && scheduleSignals !== 1) {
                return {
                    ok: false,
                    ux: buildCronCliErrorContract(
                        "missing_required",
                        "add requires exactly one schedule: --at, --every, or --cron.",
                        {
                            usage: CRON_CLI_USAGE.add,
                        },
                        "add"
                    ),
                };
            }

            if (command === "edit" && scheduleSignals > 1) {
                return {
                    ok: false,
                    ux: buildCronCliErrorContract(
                        "invalid_combination",
                        "edit accepts at most one schedule change: --at, --every, or --cron.",
                        {
                            usage: CRON_CLI_USAGE.edit,
                        },
                        "edit"
                    ),
                };
            }

            if (scheduleSignals === 0) {
                return {
                    ok: true,
                    value: null,
                };
            }

            let schedule = null;
            if (hasAt) {
                const atValue = String(options.at || "").trim();
                if (!atValue) {
                    return {
                        ok: false,
                        ux: buildCronCliErrorContract("missing_required", "--at requires a value.", null, command),
                    };
                }
                schedule = {
                    kind: "at",
                    at: atValue,
                };
            } else if (hasEvery) {
                const everyResult = parseCronCliEveryDurationToMs(options.every);
                if (!everyResult.ok) {
                    return {
                        ok: false,
                        ux: buildCronCliErrorContract("invalid_range", everyResult.error, null, command),
                    };
                }
                schedule = {
                    kind: "every",
                    everyMs: everyResult.value,
                };
            } else {
                const cronExpr = String(options.cron || "").trim();
                if (!cronExpr) {
                    return {
                        ok: false,
                        ux: buildCronCliErrorContract("missing_required", "--cron requires an expression.", null, command),
                    };
                }
                schedule = {
                    kind: "cron",
                    expr: cronExpr,
                };
            }

            if (hasCronCliOption(options, "tz")) {
                const tz = String(options.tz || "").trim();
                if (tz) {
                    schedule.tz = tz;
                }
            }
            if (hasCronCliOption(options, "stagger")) {
                schedule.stagger = Boolean(options.stagger);
            }
            if (hasCronCliOption(options, "exact")) {
                schedule.exact = Boolean(options.exact);
            }

            return {
                ok: true,
                value: schedule,
            };
        }

        function buildCronCliDeliveryFromOptions(command, options) {
            const hasNoDeliver = hasCronCliOption(options, "noDeliver") && options.noDeliver === true;
            const hasDeliverSignal = hasCronCliOption(options, "deliver") ||
                hasCronCliOption(options, "announce") ||
                hasCronCliOption(options, "to") ||
                hasCronCliOption(options, "channel") ||
                hasCronCliOption(options, "account");

            if (hasNoDeliver && hasDeliverSignal) {
                return {
                    ok: false,
                    ux: buildCronCliErrorContract(
                        "invalid_combination",
                        "delivery options cannot combine --no-deliver with delivery targets.",
                        null,
                        command
                    ),
                };
            }

            if (!hasDeliverSignal && !hasNoDeliver) {
                return {
                    ok: true,
                    value: null,
                };
            }

            if (hasNoDeliver) {
                return {
                    ok: true,
                    value: {
                        mode: "none",
                    },
                };
            }

            const toValue = String(options.to || "").trim();
            const channelValue = String(options.channel || "").trim();
            const accountValue = String(options.account || "").trim();

            let mode = "announce";
            if (toValue && /^https?:\/\//i.test(toValue)) {
                mode = "webhook";
            }

            const delivery = {
                mode: mode,
            };
            if (toValue) {
                delivery.to = toValue;
            }
            if (mode === "announce" && channelValue) {
                delivery.channel = channelValue;
            }
            if (mode === "announce" && accountValue) {
                delivery.accountId = accountValue;
            }
            if (hasCronCliOption(options, "bestEffortDeliver")) {
                delivery.bestEffort = Boolean(options.bestEffortDeliver);
            }

            return {
                ok: true,
                value: delivery,
            };
        }

        function buildCronCliFailureAlertFromOptions(command, options) {
            const disable = hasCronCliOption(options, "noFailureAlert") && options.noFailureAlert === true;
            const configure = hasCronCliOption(options, "failureAlert") ||
                hasCronCliOption(options, "failureAlertAfter") ||
                hasCronCliOption(options, "failureAlertChannel") ||
                hasCronCliOption(options, "failureAlertTo") ||
                hasCronCliOption(options, "failureAlertCooldown") ||
                hasCronCliOption(options, "failureAlertMode") ||
                hasCronCliOption(options, "failureAlertAccountId");

            if (!disable && !configure) {
                return {
                    ok: true,
                    value: null,
                };
            }

            if (disable && configure) {
                return {
                    ok: false,
                    ux: buildCronCliErrorContract(
                        "invalid_combination",
                        "failure-alert options cannot combine --no-failure-alert with explicit failure-alert fields.",
                        null,
                        command
                    ),
                };
            }

            if (disable) {
                return {
                    ok: true,
                    value: false,
                };
            }

            const alert = {};
            if (hasCronCliOption(options, "failureAlertAfter")) {
                const afterResult = parseCronCliIntegerOption(options.failureAlertAfter, "failureAlertAfter");
                if (!afterResult.ok || afterResult.value === null || afterResult.value <= 0) {
                    return {
                        ok: false,
                        ux: buildCronCliErrorContract(
                            "invalid_range",
                            "failureAlertAfter must be a positive integer.",
                            null,
                            command
                        ),
                    };
                }
                alert.after = afterResult.value;
            }

            if (hasCronCliOption(options, "failureAlertCooldown")) {
                const cooldownResult = parseCronCliIntegerOption(options.failureAlertCooldown, "failureAlertCooldown");
                if (!cooldownResult.ok || cooldownResult.value === null) {
                    return {
                        ok: false,
                        ux: buildCronCliErrorContract(
                            "invalid_range",
                            "failureAlertCooldown must be a non-negative integer.",
                            null,
                            command
                        ),
                    };
                }
                alert.cooldownMs = cooldownResult.value * 1000;
            }

            if (hasCronCliOption(options, "failureAlertMode")) {
                const modeValue = String(options.failureAlertMode || "").trim();
                if (modeValue) {
                    alert.mode = modeValue;
                }
            }
            if (hasCronCliOption(options, "failureAlertChannel")) {
                const channelValue = String(options.failureAlertChannel || "").trim();
                if (channelValue) {
                    alert.channel = channelValue;
                }
            }
            if (hasCronCliOption(options, "failureAlertTo")) {
                const toValue = String(options.failureAlertTo || "").trim();
                if (toValue) {
                    alert.to = toValue;
                }
            }
            if (hasCronCliOption(options, "failureAlertAccountId")) {
                const accountValue = String(options.failureAlertAccountId || "").trim();
                if (accountValue) {
                    alert.accountId = accountValue;
                }
            }

            if (Object.keys(alert).length === 0) {
                return {
                    ok: true,
                    value: true,
                };
            }

            return {
                ok: true,
                value: alert,
            };
        }

        async function buildCronCliMutationPayload(command, plan) {
            const params = plan && plan.params && typeof plan.params === "object"
                ? plan.params
                : {};
            const options = params.options && typeof params.options === "object"
                ? params.options
                : {};

            const mutationPayload = {};
            if (command === "add") {
                mutationPayload.name = String(options.name || "").trim();
                if (!mutationPayload.name) {
                    return {
                        ok: false,
                        ux: buildCronCliErrorContract("missing_required", "Missing required --name for /cron add.", null, "add"),
                    };
                }
                mutationPayload.enabled = options.disabled === true
                    ? false
                    : true;
            }

            if (hasCronCliOption(options, "description")) {
                mutationPayload.description = String(options.description || "").trim();
            }
            if (hasCronCliOption(options, "agent")) {
                const agentId = String(options.agent || "").trim();
                if (agentId) {
                    mutationPayload.agentId = agentId;
                }
            }
            if (hasCronCliOption(options, "sessionKey") || hasCronCliOption(options, "session")) {
                const sessionKey = String(options.sessionKey || options.session || "").trim();
                if (sessionKey) {
                    mutationPayload.sessionKey = sessionKey;
                }
            }

            if (command === "edit") {
                const enable = hasCronCliOption(options, "enable") && options.enable === true;
                const disable = hasCronCliOption(options, "disable") && options.disable === true;
                if (enable && disable) {
                    return {
                        ok: false,
                        ux: buildCronCliErrorContract(
                            "invalid_combination",
                            "edit cannot combine --enable and --disable.",
                            null,
                            "edit"
                        ),
                    };
                }
                if (enable) {
                    mutationPayload.enabled = true;
                }
                if (disable) {
                    mutationPayload.enabled = false;
                }
                if (hasCronCliOption(options, "clearSessionKey") && options.clearSessionKey === true) {
                    mutationPayload.sessionKey = "";
                }
                if (hasCronCliOption(options, "clearAgent") && options.clearAgent === true) {
                    mutationPayload.agentId = "";
                }
            }

            if (hasCronCliOption(options, "deleteAfterRun") || hasCronCliOption(options, "keepAfterRun")) {
                mutationPayload.deleteAfterRun = hasCronCliOption(options, "keepAfterRun") && options.keepAfterRun === true
                    ? false
                    : Boolean(options.deleteAfterRun);
            }

            const scheduleResult = buildCronCliScheduleFromOptions(command, options);
            if (!scheduleResult.ok) {
                return scheduleResult;
            }
            if (scheduleResult.value) {
                mutationPayload.schedule = scheduleResult.value;
            }

            const hasSystemEvent = hasCronCliOption(options, "systemEvent");
            const hasMessage = hasCronCliOption(options, "message");
            if (command === "edit" && hasSystemEvent && hasMessage) {
                return {
                    ok: false,
                    ux: buildCronCliErrorContract(
                        "invalid_combination",
                        "edit accepts at most one payload change: --system-event or --message.",
                        {
                            usage: CRON_CLI_USAGE.edit,
                        },
                        "edit"
                    ),
                };
            }

            if (hasSystemEvent || hasMessage || command === "add") {
                const payloadKind = hasSystemEvent
                    ? "systemEvent"
                    : "agentTurn";
                const payloadText = hasSystemEvent
                    ? String(options.systemEvent || "").trim()
                    : String(options.message || "").trim();
                if (!payloadText) {
                    return {
                        ok: false,
                        ux: buildCronCliErrorContract(
                            "missing_required",
                            "payload text is required for /cron " + command + ".",
                            null,
                            command
                        ),
                    };
                }

                const payload = await buildCronPayloadWithToolParity({
                    payloadKind: payloadKind,
                    payloadText: payloadText,
                    payloadModel: hasCronCliOption(options, "model")
                        ? String(options.model || "").trim()
                        : "",
                    payloadThinking: hasCronCliOption(options, "thinking")
                        ? String(options.thinking || "").trim()
                        : "",
                    timeoutSeconds: hasCronCliOption(options, "timeoutSeconds")
                        ? String(options.timeoutSeconds || "").trim()
                        : "",
                    contextMessages: "",
                });
                mutationPayload.payload = payload;
            }

            if (hasCronCliOption(options, "lightContext")) {
                mutationPayload.lightContext = Boolean(options.lightContext);
            }
            if (hasCronCliOption(options, "tools")) {
                const rawTools = String(options.tools || "").trim();
                if (rawTools) {
                    mutationPayload.toolsAllow = rawTools
                        .split(",")
                        .map(function (item) {
                            return String(item || "").trim();
                        })
                        .filter(function (item) {
                            return item.length > 0;
                        });
                }
            }

            const deliveryResult = buildCronCliDeliveryFromOptions(command, options);
            if (!deliveryResult.ok) {
                return deliveryResult;
            }
            if (deliveryResult.value) {
                mutationPayload.delivery = deliveryResult.value;
            }

            if (command === "edit") {
                if (hasCronCliOption(options, "noBestEffortDeliver") &&
                    mutationPayload.delivery &&
                    mutationPayload.delivery.mode === "announce") {
                    mutationPayload.delivery.bestEffort = false;
                }
                if (hasCronCliOption(options, "failureAlert") && options.failureAlert === false) {
                    mutationPayload.failureAlert = false;
                } else {
                    const failureAlertResult = buildCronCliFailureAlertFromOptions(command, options);
                    if (!failureAlertResult.ok) {
                        return failureAlertResult;
                    }
                    if (failureAlertResult.value !== null) {
                        mutationPayload.failureAlert = failureAlertResult.value;
                    }
                }
            }

            if (Object.keys(mutationPayload).length === 0) {
                return {
                    ok: false,
                    ux: buildCronCliErrorContract(
                        "missing_required",
                        "No patch fields provided for /cron edit.",
                        {
                            usage: CRON_CLI_USAGE.edit,
                        },
                        "edit"
                    ),
                };
            }

            return {
                ok: true,
                value: applyCronToolParityToMutationPayload(mutationPayload),
            };
        }

        function formatCronCliExecutionEnvelope(envelope) {
            const payload = envelope && typeof envelope === "object"
                ? envelope
                : {};
            return JSON.stringify(payload);
        }

        function nextCronCliExecutionToken(plan) {
            state.agentCronCliExecutionSeq = (state.agentCronCliExecutionSeq || 0) + 1;
            const sequence = state.agentCronCliExecutionSeq;
            const requestId = "cron-cli-" + String(sequence) + "-" + String(Date.now());
            const command = String(plan && plan.command || "").trim();
            const method = String(plan && plan.method || "").trim();
            state.agentCronCliActiveExecution = {
                sequence,
                requestId,
                command,
                method,
            };
            return state.agentCronCliActiveExecution;
        }

        function isStaleCronCliExecution(executionToken) {
            const active = state.agentCronCliActiveExecution;
            const token = executionToken && typeof executionToken === "object"
                ? executionToken
                : null;
            if (!active || !token) {
                return true;
            }
            return active.sequence !== token.sequence;
        }

        function buildCronCliPendingResult(plan, executionToken) {
            const command = String(plan.command || "").trim();
            const method = String(plan.method || "").trim();
            const pendingEnvelope = {
                surface: "cron-cli",
                ok: true,
                code: "pending",
                state: "pending",
                requestId: executionToken.requestId,
                sequence: executionToken.sequence,
                command: command,
                method: method,
            };
            return {
                handled: true,
                ok: true,
                kind: "pending",
                envelope: pendingEnvelope,
                message: formatCronCliExecutionEnvelope(pendingEnvelope),
            };
        }

        function settleCronCliExecution(executionToken, result) {
            if (isStaleCronCliExecution(executionToken)) {
                return;
            }
            onCronCliExecutionSettled(result);
        }

        async function executeCronCliPlanInternal(plan, refreshViews) {
            const command = String(plan.command || "").trim();
            const method = String(plan.method || "").trim();

            if (command === "status") {
                const status = await loadCronStatus({});
                refreshViews.push("status");
                return status;
            }

            if (command === "list") {
                const response = await request("cron.list", plan.params || {});
                refreshViews.push("list");
                return response && response.payload
                    ? response.payload
                    : response;
            }

            if (command === "runs") {
                const response = await request("cron.runs", plan.params || {});
                refreshViews.push("runs");
                return response && response.payload
                    ? response.payload
                    : response;
            }

            if (command === "add" || command === "edit") {
                const mutationResult = await buildCronCliMutationPayload(command, plan);
                if (!mutationResult.ok) {
                    return {
                        __cronCliUxError: mutationResult.ux,
                    };
                }

                const normalizedPayload = mutationResult.value;
                if (command === "add") {
                    await request("cron.add", recoverCronFlatJobShape(normalizedPayload) || normalizedPayload);
                } else {
                    await request("cron.update", Object.assign(
                        {},
                        plan.params && plan.params.job ? plan.params.job : {},
                        {
                            patch: recoverCronFlatJobShape(normalizedPayload) || normalizedPayload,
                        }
                    ));
                }
                await loadCronJobsPage({ append: false });
                await loadCronStatus({});
                await loadCronRuns({ append: false });
                refreshViews.push("list", "status", "runs");
                return {
                    method: method,
                };
            }

            const response = await request(method, plan.params || {});

            if (command === "remove") {
                await loadCronJobsPage({ append: false });
                await loadCronStatus({});
                await loadCronRuns({ append: false });
                refreshViews.push("list", "status", "runs");
            } else if (command === "run") {
                await loadCronRuns({ append: false });
                await loadCronStatus({});
                refreshViews.push("runs", "status");
            } else if (command === "wake") {
                await loadCronStatus({});
                await loadCronRuns({ append: false });
                refreshViews.push("status", "runs");
            }

            return response && response.payload
                ? response.payload
                : response;
        }

        async function runCronCliExecutionAsync(plan, executionToken) {
            const command = String(plan.command || "").trim();
            const method = String(plan.method || "").trim();
            const refreshViews = [];

            if (!request || !state.connected) {
                const failureEnvelope = {
                    surface: "cron-cli",
                    ok: false,
                    code: "gateway_error",
                    command: command,
                    method: method,
                    message: state.agentCronError || "cron slash command failed",
                    usage: CRON_CLI_USAGE[command] || CRON_CLI_USAGE.root,
                    requestId: executionToken.requestId,
                    sequence: executionToken.sequence,
                };
                settleCronCliExecution(executionToken, {
                    handled: true,
                    ok: false,
                    kind: "error",
                    envelope: failureEnvelope,
                    message: formatCronCliExecutionEnvelope(failureEnvelope),
                });
                return;
            }

            try {
                const payload = await executeCronCliPlanInternal(plan, refreshViews);

                if (payload && payload.__cronCliUxError) {
                    const structuredError = payload.__cronCliUxError;
                    const errorEnvelope = {
                        surface: "cron-cli",
                        ok: false,
                        code: structuredError.code || "invalid_params",
                        command: command,
                        method: method,
                        message: structuredError.message || "Invalid /cron command payload",
                        usage: structuredError.usage || CRON_CLI_USAGE[command] || CRON_CLI_USAGE.root,
                        requestId: executionToken.requestId,
                        sequence: executionToken.sequence,
                    };
                    settleCronCliExecution(executionToken, {
                        handled: true,
                        ok: false,
                        kind: "error",
                        envelope: errorEnvelope,
                        message: formatCronCliExecutionEnvelope(errorEnvelope),
                    });
                    return;
                }

                const successEnvelope = {
                    surface: "cron-cli",
                    ok: true,
                    code: "ok",
                    command: command,
                    method: method,
                    requestId: executionToken.requestId,
                    sequence: executionToken.sequence,
                    refreshViews: refreshViews,
                    payload: payload,
                };
                settleCronCliExecution(executionToken, {
                    handled: true,
                    ok: true,
                    kind: "peer",
                    envelope: successEnvelope,
                    message: formatCronCliExecutionEnvelope(successEnvelope),
                });
            } catch (error) {
                const message = resolveToolsErrorMessage(error, "cron slash command");
                const failureEnvelope = {
                    surface: "cron-cli",
                    ok: false,
                    code: "gateway_error",
                    command: command,
                    method: method,
                    message: message,
                    usage: CRON_CLI_USAGE[command] || CRON_CLI_USAGE.root,
                    requestId: executionToken.requestId,
                    sequence: executionToken.sequence,
                };
                settleCronCliExecution(executionToken, {
                    handled: true,
                    ok: false,
                    kind: "error",
                    envelope: failureEnvelope,
                    message: formatCronCliExecutionEnvelope(failureEnvelope),
                });
            }
        }

        async function executeCronCliSlashCommand(input) {
            const plan = typeof input === "string"
                ? planCronCliSlashCommand(input)
                : (input && typeof input === "object" ? input : null);
            if (!plan || plan.handled !== true) {
                return {
                    handled: false,
                };
            }

            const ux = plan.ux && typeof plan.ux === "object"
                ? plan.ux
                : null;
            if (ux && ux.ok === false) {
                const errorEnvelope = {
                    surface: "cron-cli",
                    ok: false,
                    code: ux.code || "parse_error",
                    command: plan.command || null,
                    message: ux.message || "Invalid /cron command",
                    usage: ux.usage || CRON_CLI_USAGE.root,
                    commands: ux.commands || formatCronCliSupportedCommands(),
                };
                return {
                    handled: true,
                    ok: false,
                    kind: "error",
                    envelope: errorEnvelope,
                    message: formatCronCliExecutionEnvelope(errorEnvelope),
                };
            }

            if (!plan.method) {
                const helpEnvelope = {
                    surface: "cron-cli",
                    ok: true,
                    code: "help",
                    command: plan.command || "help",
                    usage: ux && ux.usage ? ux.usage : CRON_CLI_USAGE.help,
                    rootUsage: ux && ux.rootUsage ? ux.rootUsage : CRON_CLI_USAGE.root,
                    commands: ux && ux.commands ? ux.commands : formatCronCliSupportedCommands(),
                };
                return {
                    handled: true,
                    ok: true,
                    kind: "peer",
                    envelope: helpEnvelope,
                    message: formatCronCliExecutionEnvelope(helpEnvelope),
                };
            }

            const executionToken = nextCronCliExecutionToken(plan);
            const pendingResult = buildCronCliPendingResult(plan, executionToken);
            void runCronCliExecutionAsync(plan, executionToken);
            return pendingResult;
        }

        function normalizeLowercaseStringOrEmpty(value) {
            return String(value || "").trim().toLowerCase();
        }

        function supportsAnnounceDelivery(form) {
            const sessionTarget = String(form && form.sessionTarget || "main").trim();
            const payloadKind = String(form && form.payloadKind || "agentTurn").trim();
            return sessionTarget !== "main" && payloadKind === "agentTurn";
        }

        function normalizeCronFormState(form) {
            const source = form && typeof form === "object" ? form : {};
            const normalized = Object.assign({}, source);
            if (normalized.deliveryMode !== "announce") {
                return normalized;
            }
            if (supportsAnnounceDelivery(normalized)) {
                return normalized;
            }
            normalized.deliveryMode = "none";
            return normalized;
        }

        function validateCronForm(form) {
            const errors = {};
            const source = form && typeof form === "object" ? form : {};
            if (!String(source.name || "").trim()) {
                errors.name = "Name is required.";
            }

            const scheduleKind = String(source.scheduleKind || "every").trim();
            if (scheduleKind === "at") {
                const atValue = String(source.scheduleAt || "").trim();
                const parsedAt = Date.parse(atValue);
                if (!Number.isFinite(parsedAt)) {
                    errors.scheduleAt = "Run time must be a valid date/time.";
                }
            } else if (scheduleKind === "every") {
                const amount = toNumber(String(source.everyAmount || "").trim(), 0);
                if (amount <= 0) {
                    errors.everyAmount = "Interval amount must be greater than 0.";
                }
            } else {
                const cronExpr = String(source.cronExpr || "").trim();
                if (!cronExpr) {
                    errors.cronExpr = "Cron expression is required.";
                }
            }

            const payloadKind = String(source.payloadKind || "agentTurn").trim();
            const payloadText = String(source.payloadText || "").trim();
            if (!payloadText) {
                errors.payloadText = payloadKind === "systemEvent"
                    ? "System event text is required."
                    : "Agent message is required.";
            }

            const timeoutText = String(source.timeoutSeconds || "").trim();
            if (payloadKind === "agentTurn" && timeoutText) {
                const timeout = toNumber(timeoutText, 0);
                if (timeout <= 0) {
                    errors.timeoutSeconds = "Timeout must be greater than 0.";
                }
            }

            const contextMessagesText = String(source.contextMessages || "").trim();
            if (contextMessagesText) {
                const parsedContextMessages = Number(contextMessagesText);
                const integerContextMessages = Math.floor(parsedContextMessages);
                if (!Number.isFinite(parsedContextMessages) ||
                    integerContextMessages.toString() !== contextMessagesText ||
                    integerContextMessages < 0 ||
                    integerContextMessages > CRON_REMINDER_CONTEXT_MESSAGES_MAX) {
                    errors.contextMessages =
                        "Context messages must be an integer between 0 and 10.";
                }
            }

            if (String(source.deliveryMode || "none").trim() === "webhook") {
                const deliveryTo = String(source.deliveryTo || "").trim();
                if (!deliveryTo) {
                    errors.deliveryTo = "Webhook URL is required.";
                } else if (!/^https?:\/\//i.test(deliveryTo)) {
                    errors.deliveryTo = "Webhook URL must start with http:// or https://.";
                }
            }

            if (String(source.failureAlertMode || "inherit").trim() === "custom") {
                const afterText = String(source.failureAlertAfter || "").trim();
                if (afterText) {
                    const after = toNumber(afterText, 0);
                    if (!Number.isFinite(after) || after <= 0) {
                        errors.failureAlertAfter = "Failure alert threshold must be greater than 0.";
                    }
                }

                const cooldownText = String(source.failureAlertCooldownSeconds || "").trim();
                if (cooldownText) {
                    const cooldown = toNumber(cooldownText, -1);
                    if (!Number.isFinite(cooldown) || cooldown < 0) {
                        errors.failureAlertCooldownSeconds = "Cooldown must be 0 or greater.";
                    }
                }
            }

            return errors;
        }

        function hasCronFormErrors(errors) {
            return errors && typeof errors === "object" && Object.keys(errors).length > 0;
        }

        function buildCronSchedule(form) {
            const scheduleKind = String(form && form.scheduleKind || "every").trim();
            if (scheduleKind === "at") {
                const parsed = Date.parse(String(form && form.scheduleAt || "").trim());
                if (!Number.isFinite(parsed)) {
                    throw new Error("Invalid run time.");
                }
                return {
                    kind: "at",
                    at: new Date(parsed).toISOString(),
                };
            }

            if (scheduleKind === "every") {
                const amount = toNumber(String(form && form.everyAmount || "").trim(), 0);
                if (amount <= 0) {
                    throw new Error("Invalid interval amount.");
                }
                const unit = String(form && form.everyUnit || "minutes").trim();
                const multiplier = unit === "hours"
                    ? 3600000
                    : unit === "days"
                        ? 86400000
                        : 60000;
                return {
                    kind: "every",
                    everyMs: amount * multiplier,
                };
            }

            const expr = String(form && form.cronExpr || "").trim();
            if (!expr) {
                throw new Error("Cron expression is required.");
            }

            return {
                kind: "cron",
                expr: expr,
            };
        }

        const CRON_REMINDER_CONTEXT_MESSAGES_MAX = 10;
        const CRON_REMINDER_CONTEXT_PER_MESSAGE_MAX = 220;
        const CRON_REMINDER_CONTEXT_TOTAL_MAX = 700;
        const CRON_REMINDER_CONTEXT_MARKER = "\n\nRecent context:\n";

        function truncateCronReminderText(input, maxLen) {
            const value = String(input || "");
            if (value.length <= maxLen) {
                return value;
            }

            const safeMax = Math.max(0, Number(maxLen) - 3);
            return value.slice(0, safeMax).trimEnd() + "...";
        }

        function stripExistingCronReminderContext(text) {
            const source = String(text || "");
            const markerIndex = source.indexOf(CRON_REMINDER_CONTEXT_MARKER);
            if (markerIndex < 0) {
                return source;
            }
            return source.slice(0, markerIndex).trim();
        }

        function extractCronReminderMessageText(message) {
            if (!message || typeof message !== "object") {
                return "";
            }

            if (typeof message.text === "string") {
                return String(message.text || "").trim();
            }

            if (Array.isArray(message.content)) {
                const lines = [];
                message.content.forEach(function (item) {
                    if (typeof item === "string") {
                        const plainLine = String(item || "").trim();
                        if (plainLine) {
                            lines.push(plainLine);
                        }
                        return;
                    }

                    if (!item || typeof item !== "object") {
                        return;
                    }
                    const type = normalizeLowercaseStringOrEmpty(item.type);
                    if (type !== "text" && type !== "output_text") {
                        return;
                    }

                    let resolvedText = "";
                    if (typeof item.text === "string") {
                        resolvedText = item.text;
                    } else if (item.text && typeof item.text === "object") {
                        if (typeof item.text.value === "string") {
                            resolvedText = item.text.value;
                        }
                    }

                    if (!resolvedText && typeof item.value === "string") {
                        resolvedText = item.value;
                    }

                    if (resolvedText) {
                        const line = String(resolvedText || "").trim();
                        if (line) {
                            lines.push(line);
                        }
                    }
                });
                return lines.join("\n").trim();
            }

            return "";
        }

        function resolveCronReminderContextMessages(form) {
            const raw = String(form && form.contextMessages || "").trim();
            if (!raw) {
                return 0;
            }

            const parsed = Number(raw);
            if (!Number.isFinite(parsed)) {
                return 0;
            }

            const integerValue = Math.floor(parsed);
            if (integerValue <= 0) {
                return 0;
            }

            return Math.min(CRON_REMINDER_CONTEXT_MESSAGES_MAX, integerValue);
        }

        async function buildCronReminderContextLines(contextMessages) {
            const maxMessages = Math.min(
                CRON_REMINDER_CONTEXT_MESSAGES_MAX,
                Math.max(0, Math.floor(Number(contextMessages) || 0))
            );
            if (maxMessages <= 0 || !request) {
                return [];
            }

            const resolvedSessionKey = String(state.sessionKey || "").trim();
            if (!resolvedSessionKey) {
                return [];
            }

            try {
                const response = await request("chat.history", {
                    sessionKey: resolvedSessionKey,
                    limit: maxMessages,
                });
                const payload = response && response.payload
                    ? response.payload
                    : response;
                const messages = payload && Array.isArray(payload.messages)
                    ? payload.messages
                    : Array.isArray(response && response.messages)
                        ? response.messages
                        : [];
                const parsed = messages
                    .map(function (message) {
                        const role = normalizeLowercaseStringOrEmpty(message && message.role);
                        if (role !== "user" && role !== "assistant") {
                            return null;
                        }
                        const text = extractCronReminderMessageText(message);
                        if (!text) {
                            return null;
                        }
                        return {
                            role,
                            text,
                        };
                    })
                    .filter(function (entry) {
                        return Boolean(entry);
                    });
                const recent = parsed.slice(-maxMessages);
                if (!recent.length) {
                    return [];
                }

                const lines = [];
                let total = 0;
                recent.forEach(function (entry) {
                    const label = entry.role === "user"
                        ? "User"
                        : "Assistant";
                    const text = truncateCronReminderText(
                        entry.text,
                        CRON_REMINDER_CONTEXT_PER_MESSAGE_MAX
                    );
                    const line = "- " + label + ": " + text;
                    total += line.length;
                    if (total > CRON_REMINDER_CONTEXT_TOTAL_MAX) {
                        return;
                    }
                    lines.push(line);
                });

                return lines;
            } catch (_) {
                return [];
            }
        }

        function buildCronPayload(form) {
            const payloadKind = String(form && form.payloadKind || "agentTurn").trim();
            if (payloadKind === "systemEvent") {
                const text = String(form && form.payloadText || "").trim();
                if (!text) {
                    throw new Error("System event text is required.");
                }
                return {
                    kind: "systemEvent",
                    text,
                };
            }

            const message = String(form && form.payloadText || "").trim();
            if (!message) {
                throw new Error("Agent message is required.");
            }

            const payload = {
                kind: "agentTurn",
                message,
            };
            const model = String(form && form.payloadModel || "").trim();
            if (model) {
                payload.model = model;
            }
            const thinking = String(form && form.payloadThinking || "").trim();
            if (thinking) {
                payload.thinking = thinking;
            }
            const timeout = toNumber(String(form && form.timeoutSeconds || "").trim(), 0);
            if (timeout > 0) {
                payload.timeoutSeconds = timeout;
            }
            return payload;
        }

        async function buildCronPayloadWithToolParity(form) {
            const payload = buildCronPayload(form);
            if (!payload || (payload.kind !== "systemEvent" && payload.kind !== "agentTurn")) {
                return payload;
            }

            const contextMessages = resolveCronReminderContextMessages(form);
            if (contextMessages <= 0) {
                return payload;
            }

            const contextField = payload.kind === "systemEvent"
                ? "text"
                : "message";

            const baseText = typeof payload[contextField] === "string"
                ? stripExistingCronReminderContext(payload[contextField])
                : "";
            if (!baseText.trim()) {
                return payload;
            }

            const contextLines = await buildCronReminderContextLines(contextMessages);
            if (!contextLines.length) {
                payload[contextField] = baseText;
                return payload;
            }

            payload[contextField] =
                baseText + CRON_REMINDER_CONTEXT_MARKER + contextLines.join("\n");
            return payload;
        }

        function buildCronDelivery(form) {
            const mode = String(form && form.deliveryMode || "none").trim();
            if (mode === "none") {
                return { mode: "none" };
            }

            const delivery = {
                mode: mode === "webhook" ? "webhook" : "announce",
            };

            let to = String(form && form.deliveryTo || "").trim();
            if (delivery.mode === "webhook") {
                to = to.replace(/^https?:\/\//i, function (prefix) {
                    return prefix.toLowerCase();
                });
            }
            if (to) {
                delivery.to = to;
            }

            return delivery;
        }

        function stripThreadSuffixFromSessionKey(sessionKey) {
            const normalized = normalizeLowercaseStringOrEmpty(sessionKey);
            const marker = ":thread:";
            const markerIndex = normalized.lastIndexOf(marker);
            if (markerIndex <= 0) {
                return String(sessionKey || "").trim();
            }

            const rawKey = String(sessionKey || "").trim();
            const parent = rawKey.slice(0, markerIndex).trim();
            return parent || rawKey;
        }

        function inferCronAnnounceDeliveryFromSessionKey(sessionKey) {
            const rawSessionKey = String(sessionKey || "").trim();
            if (!rawSessionKey) {
                return null;
            }

            const keyWithoutThread = stripThreadSuffixFromSessionKey(rawSessionKey);
            const segments = keyWithoutThread.split(":").filter(function (part) {
                return String(part || "").trim().length > 0;
            });
            if (segments.length < 4) {
                return null;
            }

            const prefix = normalizeLowercaseStringOrEmpty(segments[0]);
            if (prefix !== "agent") {
                return null;
            }

            const rest = segments.slice(2);
            const markerIndex = rest.findIndex(function (part) {
                const normalizedPart = normalizeLowercaseStringOrEmpty(part);
                return normalizedPart === "direct" ||
                    normalizedPart === "dm" ||
                    normalizedPart === "group" ||
                    normalizedPart === "channel";
            });
            if (markerIndex < 0) {
                return null;
            }

            const peerParts = rest.slice(markerIndex + 1);
            const peerId = peerParts.join(":").trim();
            if (!peerId) {
                return null;
            }

            const inferred = {
                mode: "announce",
                to: peerId,
            };

            if (markerIndex >= 1) {
                const normalizedChannel = normalizeLowercaseStringOrEmpty(rest[0]);
                if (normalizedChannel &&
                    normalizedChannel !== "main" &&
                    normalizedChannel !== "subagent" &&
                    normalizedChannel !== "acp") {
                    inferred.channel = normalizedChannel;
                }
            }

            return inferred;
        }

        function applyCronToolParityToMutationPayload(payload) {
            const source = payload && typeof payload === "object"
                ? payload
                : {};
            const normalized = Object.assign({}, source);
            const payloadObject = normalized.payload && typeof normalized.payload === "object"
                ? normalized.payload
                : null;
            const deliveryObject = normalized.delivery && typeof normalized.delivery === "object"
                ? Object.assign({}, normalized.delivery)
                : null;

            if (!payloadObject || String(payloadObject.kind || "").trim() !== "agentTurn") {
                return normalized;
            }

            if (!deliveryObject) {
                return normalized;
            }

            const mode = normalizeLowercaseStringOrEmpty(deliveryObject.mode || "none");
            if (mode === "webhook") {
                const webhookTo = String(deliveryObject.to || "").trim();
                if (webhookTo) {
                    deliveryObject.to = webhookTo.replace(/^https?:\/\//i, function (prefix) {
                        return prefix.toLowerCase();
                    });
                    normalized.delivery = deliveryObject;
                }
                return normalized;
            }

            if (mode !== "announce") {
                return normalized;
            }

            const hasTarget =
                String(deliveryObject.channel || "").trim().length > 0 ||
                String(deliveryObject.to || "").trim().length > 0;
            if (hasTarget) {
                normalized.delivery = deliveryObject;
                return normalized;
            }

            const inferred = inferCronAnnounceDeliveryFromSessionKey(state.sessionKey);
            if (!inferred) {
                normalized.delivery = deliveryObject;
                return normalized;
            }

            normalized.delivery = Object.assign({}, deliveryObject, inferred);
            return normalized;
        }

        function buildCronJobIdentityParams(jobId) {
            const resolved = String(jobId || "").trim();
            if (!resolved) {
                return {};
            }

            return {
                id: resolved,
                jobId: resolved,
            };
        }

        function recoverCronFlatJobShape(input) {
            const source = input && typeof input === "object"
                ? input
                : {};
            const recovered = {};
            let hasRecoverable = false;
            const recoverableKeys = {
                name: true,
                schedule: true,
                sessionTarget: true,
                wakeMode: true,
                payload: true,
                delivery: true,
                enabled: true,
                description: true,
                deleteAfterRun: true,
                agentId: true,
                sessionKey: true,
                failureAlert: true,
                message: true,
                text: true,
                model: true,
                fallbacks: true,
                toolsAllow: true,
                thinking: true,
                timeoutSeconds: true,
                lightContext: true,
                allowUnsafeExternalContent: true,
            };

            Object.keys(source).forEach(function (key) {
                if (!recoverableKeys[key]) {
                    return;
                }
                if (source[key] === undefined) {
                    return;
                }

                recovered[key] = source[key];
                hasRecoverable = true;
            });

            const hasMinimumSignal =
                recovered.schedule !== undefined ||
                recovered.payload !== undefined ||
                recovered.message !== undefined ||
                recovered.text !== undefined;

            return hasRecoverable && hasMinimumSignal
                ? recovered
                : null;
        }

        function buildCronFailureAlert(form) {
            const mode = String(form && form.failureAlertMode || "inherit").trim();
            if (mode === "disabled") {
                return false;
            }
            if (mode !== "custom") {
                return undefined;
            }

            const afterText = String(form && form.failureAlertAfter || "").trim();
            const cooldownText = String(form && form.failureAlertCooldownSeconds || "").trim();
            const failureAlert = {};
            if (afterText) {
                const after = toNumber(afterText, 0);
                if (after > 0) {
                    failureAlert.after = Math.floor(after);
                }
            }
            if (cooldownText) {
                const cooldownSeconds = toNumber(cooldownText, -1);
                if (cooldownSeconds >= 0) {
                    failureAlert.cooldownMs = Math.floor(cooldownSeconds * 1000);
                }
            }

            return Object.keys(failureAlert).length > 0
                ? failureAlert
                : undefined;
        }

        function resetCronFormToDefaults() {
            state.agentCronEditingJobId = null;
            state.agentCronForm = {
                name: "",
                enabled: true,
                scheduleKind: "every",
                scheduleAt: "",
                everyAmount: "30",
                everyUnit: "minutes",
                cronExpr: "",
                payloadKind: "agentTurn",
                payloadText: "",
                payloadModel: "",
                payloadThinking: "",
                timeoutSeconds: "",
                contextMessages: "",
                deliveryMode: "none",
                deliveryTo: "",
                failureAlertMode: "inherit",
                failureAlertAfter: "",
                failureAlertCooldownSeconds: "",
            };
            state.agentCronFieldErrors = validateCronForm(state.agentCronForm);
        }

        function buildCloneName(name, existingNames) {
            const base = String(name || "").trim() || "Job";
            const first = base + " copy";
            if (!existingNames.has(normalizeLowercaseStringOrEmpty(first))) {
                return first;
            }

            let index = 2;
            while (index < 1000) {
                const next = base + " copy " + index;
                if (!existingNames.has(normalizeLowercaseStringOrEmpty(next))) {
                    return next;
                }
                index += 1;
            }

            return base + " copy " + Date.now();
        }

        function jobToForm(job, previousForm) {
            const source = job && typeof job === "object" ? job : {};
            const schedule = source.schedule && typeof source.schedule === "object"
                ? source.schedule
                : {};
            const payload = source.payload && typeof source.payload === "object"
                ? source.payload
                : {};
            const fallback = previousForm && typeof previousForm === "object"
                ? previousForm
                : state.agentCronForm;

            const next = {
                name: String(source.name || "").trim(),
                enabled: source.enabled !== false,
                scheduleKind: String(schedule.kind || "every").trim() || "every",
                scheduleAt: typeof schedule.at === "string" ? schedule.at : "",
                everyAmount: fallback && fallback.everyAmount ? fallback.everyAmount : "30",
                everyUnit: fallback && fallback.everyUnit ? fallback.everyUnit : "minutes",
                cronExpr: typeof schedule.expr === "string" ? schedule.expr : "",
                payloadKind: String(payload.kind || "agentTurn").trim() || "agentTurn",
                payloadText: payload.kind === "systemEvent"
                    ? String(payload.text || "")
                    : String(payload.message || ""),
                payloadModel: String(payload.model || ""),
                payloadThinking: String(payload.thinking || ""),
                timeoutSeconds: Number.isFinite(Number(payload.timeoutSeconds))
                    ? String(Math.floor(Number(payload.timeoutSeconds)))
                    : "",
                contextMessages: fallback && typeof fallback.contextMessages === "string"
                    ? fallback.contextMessages
                    : "",
                deliveryMode: "none",
                deliveryTo: "",
                failureAlertMode: "inherit",
                failureAlertAfter: "",
                failureAlertCooldownSeconds: "",
            };

            if (next.scheduleKind === "every") {
                const everyMs = Number(schedule.everyMs);
                if (Number.isFinite(everyMs) && everyMs > 0) {
                    if (everyMs % 86400000 === 0) {
                        next.everyAmount = String(Math.max(1, Math.floor(everyMs / 86400000)));
                        next.everyUnit = "days";
                    } else if (everyMs % 3600000 === 0) {
                        next.everyAmount = String(Math.max(1, Math.floor(everyMs / 3600000)));
                        next.everyUnit = "hours";
                    } else {
                        next.everyAmount = String(Math.max(1, Math.floor(everyMs / 60000)));
                        next.everyUnit = "minutes";
                    }
                }
            }

            return normalizeCronFormState(next);
        }

        const loadCronModelSuggestions =
            cronRuntime && typeof cronRuntime.loadCronModelSuggestions === "function"
                ? cronRuntime.loadCronModelSuggestions
                : async function (options) {
                    const opts = options || {};
                    const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                        ? opts.shouldIgnoreResponse
                        : null;
                    if (!request || !state.connected) {
                        state.agentCronModelSuggestions = [];
                        return [];
                    }
                    try {
                        const res = await request("models.list", {});
                        if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                            return [];
                        }
                        const payload = res && res.payload ? res.payload : res;
                        const models = payload && Array.isArray(payload.models) ? payload.models : [];
                        const unique = new Set();
                        models.forEach(function (entry) {
                            const id = String(entry && entry.id || "").trim();
                            if (id) {
                                unique.add(id);
                            }
                        });
                        state.agentCronModelSuggestions = Array.from(unique).sort(function (left, right) {
                            return left.localeCompare(right);
                        });
                        return state.agentCronModelSuggestions;
                    } catch (_) {
                        state.agentCronModelSuggestions = [];
                        return [];
                    }
                };

        const withCronBusy =
            cronRuntime && typeof cronRuntime.withCronBusy === "function"
                ? cronRuntime.withCronBusy
                : async function (run) {
                    if (!request || !state.connected || state.agentCronBusy) {
                        return false;
                    }
                    state.agentCronBusy = true;
                    state.agentCronError = null;
                    onStateUpdated();
                    try {
                        await run();
                        return true;
                    } catch (err) {
                        state.agentCronError = resolveToolsErrorMessage(err, "cron mutation");
                        return false;
                    } finally {
                        state.agentCronBusy = false;
                        onStateUpdated();
                    }
                };

        const addOrUpdateCronJob =
            cronRuntime && typeof cronRuntime.addOrUpdateCronJob === "function"
                ? cronRuntime.addOrUpdateCronJob
                : async function () {
                    return false;
                };

        const triggerCronWake =
            cronRuntime && typeof cronRuntime.triggerCronWake === "function"
                ? cronRuntime.triggerCronWake
                : async function () {
                    return false;
                };

        const removeCronJob =
            cronRuntime && typeof cronRuntime.removeCronJob === "function"
                ? cronRuntime.removeCronJob
                : async function () {
                    return false;
                };

        const runCronJobNow =
            cronRuntime && typeof cronRuntime.runCronJobNow === "function"
                ? cronRuntime.runCronJobNow
                : async function () {
                    return false;
                };

        const startCronEdit =
            cronRuntime && typeof cronRuntime.startCronEdit === "function"
                ? cronRuntime.startCronEdit
                : function () {
                    return false;
                };

        const startCronClone =
            cronRuntime && typeof cronRuntime.startCronClone === "function"
                ? cronRuntime.startCronClone
                : function () {
                    return false;
                };

        const cancelCronEdit =
            cronRuntime && typeof cronRuntime.cancelCronEdit === "function"
                ? cronRuntime.cancelCronEdit
                : function () {
                };

        const updateCronFormField =
            cronRuntime && typeof cronRuntime.updateCronFormField === "function"
                ? cronRuntime.updateCronFormField
                : function () {
                };

        const loadCronStatus =
            cronRuntime && typeof cronRuntime.loadCronStatus === "function"
                ? cronRuntime.loadCronStatus
                : async function () {
                    return state.agentCronStatusResult;
                };

        const loadCronJobsPage =
            cronRuntime && typeof cronRuntime.loadCronJobsPage === "function"
                ? cronRuntime.loadCronJobsPage
                : async function () {
                };

        const loadCronRuns =
            cronRuntime && typeof cronRuntime.loadCronRuns === "function"
                ? cronRuntime.loadCronRuns
                : async function () {
                };

        const loadAgentCron =
            cronRuntime && typeof cronRuntime.loadAgentCron === "function"
                ? cronRuntime.loadAgentCron
                : async function () {
                };

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

        async function loadDreamingStatus(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.dreamingStatusLoading) {
                return state.dreamingStatus;
            }

            state.dreamingStatusLoading = true;
            state.dreamingStatusError = null;
            onStateUpdated();

            try {
                const res = await request("doctor.memory.status", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                const payload = res && res.payload ? res.payload : res;
                const source = payload && typeof payload === "object" && payload.dreaming
                    ? payload.dreaming
                    : payload;
                state.dreamingStatus = normalizeDreamingStatus(source);
                return state.dreamingStatus;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                state.dreamingStatusError = String(err);
                state.lastError = String(err);
                return null;
            } finally {
                state.dreamingStatusLoading = false;
                onStateUpdated();
            }
        }

        async function loadDreamDiary(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.dreamDiaryLoading) {
                return;
            }

            state.dreamDiaryLoading = true;
            state.dreamDiaryError = null;
            onStateUpdated();

            try {
                const res = await request("doctor.memory.dreamDiary", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : res;
                const path = normalizeTrimmedString(payload && payload.path) || "DREAMS.md";
                const found = payload && payload.found === true;
                state.dreamDiaryPath = path;
                state.dreamDiaryContent = found
                    ? (typeof payload.content === "string" ? payload.content : "")
                    : null;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                state.dreamDiaryError = String(err);
                state.lastError = String(err);
            } finally {
                state.dreamDiaryLoading = false;
                onStateUpdated();
            }
        }

        async function runDreamDiaryAction(method, options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            const reloadDiary = opts.reloadDiary !== false;

            if (!request || !state.connected || state.dreamDiaryActionLoading) {
                return false;
            }

            state.dreamDiaryActionLoading = true;
            state.dreamingStatusError = null;
            state.dreamDiaryError = null;
            onStateUpdated();

            try {
                await request(method, {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                if (reloadDiary) {
                    await loadDreamDiary({
                        shouldIgnoreResponse,
                    });
                }
                await loadDreamingStatus({
                    shouldIgnoreResponse,
                });
                return true;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                const message = String(err);
                state.dreamingStatusError = message;
                state.lastError = message;
                return false;
            } finally {
                state.dreamDiaryActionLoading = false;
                onStateUpdated();
            }
        }

        function normalizeDreamingConfigSnapshot(payload) {
            const responsePayload = payload && typeof payload === "object" ? payload : {};
            const hash = normalizeTrimmedString(responsePayload.hash) || null;
            const config = asRecord(responsePayload.config);
            return {
                hash,
                config,
            };
        }

        async function loadDreamingConfigSnapshot(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected) {
                return state.dreamingConfigSnapshot;
            }

            try {
                const response = await request("config.get", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                const payload = response && response.payload ? response.payload : response;
                const snapshot = normalizeDreamingConfigSnapshot(payload);
                state.dreamingConfigSnapshot = snapshot;
                state.dreamingConfigSnapshotHash = snapshot.hash;
                const configured = resolveConfiguredDreaming(snapshot.config || null);
                state.dreamingResolvedPluginId = configured.pluginId;
                return snapshot;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                state.dreamingStatusError = String(err);
                state.lastError = String(err);
                return null;
            }
        }

        async function ensureDreamingPathSupported(pluginId, options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected) {
                return true;
            }

            try {
                const response = await request("config.schema.lookup", {
                    path: "plugins.entries." + pluginId + ".config",
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                const payload = response && response.payload ? response.payload : response;
                if (lookupIncludesDreamingProperty(payload)) {
                    return true;
                }
                if (lookupDisallowsUnknownProperties(payload)) {
                    const message = "Selected memory plugin \"" + pluginId + "\" does not support dreaming settings.";
                    state.dreamingStatusError = message;
                    state.lastError = message;
                    return false;
                }
            } catch (_) {
                return true;
            }

            return true;
        }

        async function updateDreamingEnabled(enabled, options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.dreamingModeSaving) {
                return false;
            }

            const snapshot = await loadDreamingConfigSnapshot({
                shouldIgnoreResponse,
            });
            if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                return false;
            }

            const configSnapshot = snapshot || state.dreamingConfigSnapshot;
            const baseHash = configSnapshot && typeof configSnapshot.hash === "string"
                ? configSnapshot.hash
                : null;
            if (!baseHash) {
                state.dreamingStatusError = "Config hash missing; refresh and retry.";
                return false;
            }

            const configured = resolveConfiguredDreaming(configSnapshot && configSnapshot.config ? configSnapshot.config : null);
            const pluginId = configured.pluginId;
            state.dreamingResolvedPluginId = pluginId;

            const pathSupported = await ensureDreamingPathSupported(pluginId, {
                shouldIgnoreResponse,
            });
            if (!pathSupported) {
                return false;
            }

            state.dreamingModeSaving = true;
            state.dreamingStatusError = null;
            onStateUpdated();

            try {
                await request("config.patch", {
                    baseHash,
                    raw: JSON.stringify({
                        plugins: {
                            entries: {
                                [pluginId]: {
                                    config: {
                                        dreaming: {
                                            enabled: Boolean(enabled),
                                        },
                                    },
                                },
                            },
                        },
                    }),
                    sessionKey: String(state.sessionKey || "main"),
                    note: "Dreaming settings updated from the Dreaming tab.",
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                if (state.dreamingStatus && typeof state.dreamingStatus === "object") {
                    state.dreamingStatus = Object.assign({}, state.dreamingStatus, {
                        enabled: Boolean(enabled),
                    });
                }
                return true;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return false;
                }

                const message = String(err);
                state.dreamingStatusError = message;
                state.lastError = message;
                return false;
            } finally {
                state.dreamingModeSaving = false;
                onStateUpdated();
            }
        }

        async function backfillDreamDiary(options) {
            return runDreamDiaryAction("doctor.memory.backfillDreamDiary", options);
        }

        async function resetDreamDiary(options) {
            return runDreamDiaryAction("doctor.memory.resetDreamDiary", options);
        }

        async function resetGroundedShortTerm(options) {
            const opts = options || {};
            return runDreamDiaryAction("doctor.memory.resetGroundedShortTerm", Object.assign({}, opts, {
                reloadDiary: false,
            }));
        }

        async function loadAgentDreaming(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!resolvedAgentId || !request || !state.connected || state.agentDreamingLoading) {
                return;
            }

            function shouldIgnoreResponse() {
                return hasSelectedAgentMismatch(resolvedAgentId) || state.agentsPanel !== "dreaming";
            }

            state.agentDreamingLoading = true;
            state.agentDreamingError = null;
            onStateUpdated();

            try {
                await loadDreamingConfigSnapshot({
                    shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                await loadDreamingStatus({
                    shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                await loadDreamDiary({
                    shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentDreamingResult = {
                    status: state.dreamingStatus,
                    diaryPath: state.dreamDiaryPath,
                    diaryContent: state.dreamDiaryContent,
                    configHash: state.dreamingConfigSnapshotHash,
                    pluginId: state.dreamingResolvedPluginId,
                    capability: state.agentDreamingCapability,
                };
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentDreamingError = String(err);
            } finally {
                state.agentDreamingLoading = false;
                onStateUpdated();
            }
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
            const normalized = ["overview", "tools", "files", "skills", "channels", "cron", "dreaming", "nodes", "instances", "usage", "observability", "devices"].indexOf(panelValue) >= 0
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
            if (state.agentsPanel === "cron") {
                await loadAgentCron(selectedAgentId);
                return;
            }

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

            if (state.agentsPanel === "dreaming") {
                await loadAgentDreaming(selectedAgentId);
                return;
            }

            if (state.agentsPanel === "nodes") {
                await loadNodes({
                    shouldIgnoreResponse: function () {
                        return state.agentsPanel !== "nodes";
                    },
                });
                return;
            }

            if (state.agentsPanel === "instances") {
                await loadPresence({
                    shouldIgnoreResponse: function () {
                        return state.agentsPanel !== "instances";
                    },
                });
                return;
            }

            if (state.agentsPanel === "usage") {
                await loadUsage({
                    shouldIgnoreResponse: function () {
                        return state.agentsPanel !== "usage";
                    },
                });
                return;
            }

            if (state.agentsPanel === "observability") {
                await loadObservability({
                    shouldIgnoreResponse: function () {
                        return state.agentsPanel !== "observability";
                    },
                });
                return;
            }

            if (state.agentsPanel === "devices") {
                await loadDevicePairs({
                    shouldIgnoreResponse: function () {
                        return state.agentsPanel !== "devices";
                    },
                });
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
            state.agentFileSelectedPath = null;
            state.agentFileEditDraft = "";
            state.agentFileEditBaseContent = "";
            state.agentFileSaveError = null;
            state.agentFileSaveStatus = null;
            state.agentSkillsResult = null;
            state.agentSkillsReport = null;
            state.agentSkillsAgentId = null;
            state.skillsHubResults = [];
            state.skillsHubError = null;
            state.skillsDetailResult = null;
            state.skillsDetailError = null;
            state.skillsInstallStatus = null;
            state.skillsEditError = null;
            state.skillsEditStatus = null;
            state.agentChannelsResult = null;
            state.agentCronResult = null;
            state.agentDreamingResult = null;
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

        const updateCronJobsFilter =
            cronRuntime && typeof cronRuntime.updateCronJobsFilter === "function"
                ? cronRuntime.updateCronJobsFilter
                : function () {
                };

        const updateCronRunsFilter =
            cronRuntime && typeof cronRuntime.updateCronRunsFilter === "function"
                ? cronRuntime.updateCronRunsFilter
                : function () {
                };

        const loadMoreCronJobs =
            cronRuntime && typeof cronRuntime.loadMoreCronJobs === "function"
                ? cronRuntime.loadMoreCronJobs
                : async function () {
                };

        const loadMoreCronRuns =
            cronRuntime && typeof cronRuntime.loadMoreCronRuns === "function"
                ? cronRuntime.loadMoreCronRuns
                : async function () {
                };

        return {
            loadAgents,
            loadToolsCatalog,
            loadToolsEffective,
            loadAgentFiles,
            loadAgentFileContent,
            selectAgentFile,
            updateAgentFileDraft,
            saveAgentFileContent,
            loadAgentSkills,
            searchSkillsHub,
            loadSkillDetail,
            installSkill,
            updateSkillConfig,
            loadNodes,
            loadPresence,
            loadUsage,
            loadUsageTimeSeries,
            loadUsageSessionLogs,
            loadObservability,
            updateObservabilityField,
            invokeObservabilityMethod,
            exportObservabilityLogs,
            loadDevicePairs,
            selectDevicePair,
            resolveDevicePair,
            loadChannels,
            startWhatsAppLogin,
            waitWhatsAppLogin,
            logoutWhatsApp,
            loadAgentChannels,
            loadAgentCron,
            loadAgentDreaming,
            loadDreamingStatus,
            loadDreamDiary,
            getDreamingUiModel,
            setDreamingSubTab,
            setDreamingAdvancedWaitingSort,
            setDreamDiaryPage,
            loadDreamingConfigSnapshot,
            updateDreamingEnabled,
            backfillDreamDiary,
            resetDreamDiary,
            resetGroundedShortTerm,
            loadCronStatus,
            loadCronJobsPage,
            loadCronRuns,
            loadMoreCronJobs,
            loadMoreCronRuns,
            updateCronJobsFilter,
            updateCronRunsFilter,
            updateCronFormField,
            parseCronCliSlashCommand,
            tryParseCronCliSlashCommand,
            buildCronCliCommandPlan,
            planCronCliSlashCommand,
            buildCronCliHelpContract,
            executeCronCliSlashCommand,
            addOrUpdateCronJob,
            removeCronJob,
            runCronJobNow,
            triggerCronWake,
            startCronEdit,
            startCronClone,
            cancelCronEdit,
            loadCronModelSuggestions,
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

    async function awaitCronCliRegressionSettlements(settlements, expectedCount) {
        const target = Number.isFinite(Number(expectedCount))
            ? Math.max(1, Math.floor(Number(expectedCount)))
            : 1;
        for (let attempt = 0; attempt < 200 && settlements.length < target; attempt += 1) {
            await new Promise(function (resolve) {
                setTimeout(resolve, 0);
            });
        }
        if (settlements.length < target) {
            throw new Error("Timed out waiting for cron-cli settlements; expected " +
                String(target) + ", got " + String(settlements.length));
        }
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
                agentsPanel: "cron",
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
            assertRegression(state.agentCronBusy === false,
                "cron state contract should initialize agentCronBusy=false");
            assertRegression(state.agentCronEditingJobId === null,
                "cron state contract should initialize agentCronEditingJobId=null");
            assertRegression(Boolean(state.agentCronForm) && typeof state.agentCronForm.name === "string",
                "cron state contract should initialize agentCronForm default shape");
            assertRegression(Boolean(state.agentCronFieldErrors) && typeof state.agentCronFieldErrors === "object",
                "cron state contract should initialize agentCronFieldErrors object");
            assertRegression(state.dreamingStatusLoading === false,
                "dreaming state contract should initialize dreamingStatusLoading=false");
            assertRegression(state.dreamingStatusError === null,
                "dreaming state contract should initialize dreamingStatusError=null");
            assertRegression(state.dreamingStatus === null,
                "dreaming state contract should initialize dreamingStatus=null");
            assertRegression(state.dreamDiaryLoading === false,
                "dreaming state contract should initialize dreamDiaryLoading=false");
            assertRegression(state.dreamDiaryActionLoading === false,
                "dreaming state contract should initialize dreamDiaryActionLoading=false");
            assertRegression(state.dreamDiaryPath === null,
                "dreaming state contract should initialize dreamDiaryPath=null");
            assertRegression(state.dreamDiaryContent === null,
                "dreaming state contract should initialize dreamDiaryContent=null");
            assertRegression(state.agentDreamingLoading === false,
                "dreaming extension contract should initialize agentDreamingLoading=false");
            assertRegression(state.agentDreamingError === null,
                "dreaming extension contract should initialize agentDreamingError=null");
            assertRegression(state.agentDreamingResult === null,
                "dreaming extension contract should initialize agentDreamingResult=null");
            assertRegression(Boolean(state.agentDreamingCapability) && typeof state.agentDreamingCapability.methods === "string",
                "dreaming extension contract should initialize agentDreamingCapability");
            assertRegression(state.dreamingUiSubTab === "scene",
                "dreaming UI state contract should initialize dreamingUiSubTab=scene");
            assertRegression(state.dreamingAdvancedWaitingSort === "recent",
                "dreaming UI state contract should initialize dreamingAdvancedWaitingSort=recent");
            assertRegression(Array.isArray(state.dreamDiaryParsedEntries) && state.dreamDiaryParsedEntries.length === 0,
                "dreaming UI state contract should initialize dreamDiaryParsedEntries=[]");
            assertRegression(Array.isArray(state.dreamDiaryNavigation) && state.dreamDiaryNavigation.length === 0,
                "dreaming UI state contract should initialize dreamDiaryNavigation=[]");
            assertRegression(state.dreamDiaryPage === 0,
                "dreaming UI state contract should initialize dreamDiaryPage=0");
            assertRegression(state.nodesLoading === false,
                "nodes state contract should initialize nodesLoading=false");
            assertRegression(Array.isArray(state.nodes) && state.nodes.length === 0,
                "nodes state contract should initialize nodes=[]");
            assertRegression(state.nodesError === null,
                "nodes state contract should initialize nodesError=null");
            assertRegression(state.presenceLoading === false,
                "presence state contract should initialize presenceLoading=false");
            assertRegression(Array.isArray(state.presenceEntries) && state.presenceEntries.length === 0,
                "presence state contract should initialize presenceEntries=[]");
            assertRegression(state.presenceError === null,
                "presence state contract should initialize presenceError=null");
            assertRegression(state.presenceStatus === null,
                "presence state contract should initialize presenceStatus=null");
            assertRegression(state.usageLoading === false,
                "usage state contract should initialize usageLoading=false");
            assertRegression(state.usageResult === null,
                "usage state contract should initialize usageResult=null");
            assertRegression(state.usageCostSummary === null,
                "usage state contract should initialize usageCostSummary=null");
            assertRegression(state.usageError === null,
                "usage state contract should initialize usageError=null");
            assertRegression(Array.isArray(state.usageSelectedSessions) && state.usageSelectedSessions.length === 0,
                "usage state contract should initialize usageSelectedSessions=[]");
            assertRegression(Array.isArray(state.usageSelectedDays) && state.usageSelectedDays.length === 0,
                "usage state contract should initialize usageSelectedDays=[]");
            assertRegression(state.usageTimeSeries === null,
                "usage state contract should initialize usageTimeSeries=null");
            assertRegression(state.usageTimeSeriesLoading === false,
                "usage state contract should initialize usageTimeSeriesLoading=false");
            assertRegression(state.usageSessionLogs === null,
                "usage state contract should initialize usageSessionLogs=null");
            assertRegression(state.usageSessionLogsLoading === false,
                "usage state contract should initialize usageSessionLogsLoading=false");
            assertRegression(state.usageTimeZone === "local",
                "usage state contract should initialize usageTimeZone=local");
            assertRegression(state.observabilityEnabled === false,
                "observability state contract should initialize observabilityEnabled=false");
            assertRegression(state.observabilityLoading === false &&
                state.observabilityError === null &&
                state.observabilityHealth === null &&
                Array.isArray(state.observabilityLogs),
                "observability state contract should initialize health/logs defaults");
            assertRegression(state.devicePairsLoading === false &&
                state.devicePairsBusy === false &&
                Array.isArray(state.devicePairs),
                "devices state contract should initialize pairing defaults");
            summary.push("channels + cron + dreaming + nodes + presence + usage state/UI contract defaults");
        }

        {
            const state = createRegressionState();
            state.observabilityEnabled = true;
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            controller.setAgentsPanel("observability");
            const panelLoad = controller.loadPanelDataForCurrentAgent();
            const healthCall = harness.takeNextCall("gateway.health");
            const detailsCall = harness.takeNextCall("gateway.health.details");
            const transportCall = harness.takeNextCall("gateway.transport.status");
            const heartbeatCall = harness.takeNextCall("last-heartbeat");
            const modelsCall = harness.takeNextCall("models.list");
            const logsCall = harness.takeNextCall("gateway.logs.tail");
            logsCall.deferred.resolve({
                payload: {
                    entries: [
                        { ts: 1, level: "info", source: "gateway", message: "started" },
                    ],
                },
            });
            healthCall.deferred.resolve({ payload: { status: "ok", running: true } });
            detailsCall.deferred.resolve({ payload: { status: "ok" } });
            transportCall.deferred.resolve({ payload: { running: true, endpoint: "ws://127.0.0.1:18789" } });
            heartbeatCall.deferred.resolve({ payload: { connected: true, lastHeartbeatMs: 11 } });
            modelsCall.deferred.resolve({ payload: { models: [{ id: "default" }] } });
            await panelLoad;
            assertRegression(state.observabilityHealth && state.observabilityHealth.status === "ok",
                "observability panel should bind gateway.health payload");
            assertRegression(Array.isArray(state.observabilityLogs) && state.observabilityLogs.length === 1,
                "observability panel should bind logs.tail payload");

            controller.updateObservabilityField("method", "gateway.health");
            controller.updateObservabilityField("params", "{}");
            const invokePending = controller.invokeObservabilityMethod();
            const invokeCall = harness.takeNextCall("gateway.health");
            invokeCall.deferred.resolve({ payload: { status: "ok" } });
            await invokePending;
            assertRegression(typeof state.observabilityMethodResult === "string" &&
                state.observabilityMethodResult.indexOf("\"status\": \"ok\"") >= 0,
                "observability method invoke should capture JSON response text");
            summary.push("observability panel load + invoke");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });
            controller.setAgentsPanel("devices");
            const listPending = controller.loadPanelDataForCurrentAgent();
            const listCall = harness.takeNextCall("device.pair.list");
            listCall.deferred.resolve({
                payload: {
                    pairs: [
                        { deviceId: "device-a", status: "pending" },
                        { deviceId: "device-b", status: "approved" },
                    ],
                },
            });
            await listPending;
            assertRegression(Array.isArray(state.devicePairs) && state.devicePairs.length === 2,
                "devices panel should bind device.pair.list rows");
            assertRegression(state.devicePairSelection === "device-a",
                "devices panel should auto-select first pair");

            const approvePending = controller.resolveDevicePair("approve", "device-a");
            const approveCall = harness.takeNextCall("device.pair.approve");
            approveCall.deferred.resolve({ payload: { approved: true } });
            await approvePending;
            assertRegression(Array.isArray(state.devicePairs) &&
                state.devicePairs.some((entry) => entry.deviceId === "device-a" && entry.status === "approved"),
            "device approve action should update local status");

            const rejectPending = controller.resolveDevicePair("reject", "device-a");
            const rejectCall = harness.takeNextCall("device.pair.reject");
            rejectCall.deferred.resolve({ payload: { rejected: true } });
            await rejectPending;
            assertRegression(Array.isArray(state.devicePairs) &&
                state.devicePairs.some((entry) => entry.deviceId === "device-a" && entry.status === "rejected"),
            "device reject action should update local status");
            summary.push("devices list + approve/reject actions");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const firstLoad = controller.loadPresence({
                quiet: false,
            });
            const firstCall = harness.takeNextCall("system-presence");
            firstCall.deferred.resolve({
                payload: [
                    {
                        instanceId: "instance-main",
                        host: "blazeclaw.local",
                    },
                ],
            });
            await firstLoad;

            assertRegression(Array.isArray(state.presenceEntries) && state.presenceEntries.length === 1,
                "presence loader should bind array payload entries");
            assertRegression(state.presenceStatus === null,
                "presence loader should set null status when entries exist");
            assertRegression(state.presenceError === null,
                "presence loader should clear presenceError on success");

            const noPayloadLoad = controller.loadPresence({
                quiet: false,
            });
            const noPayloadCall = harness.takeNextCall("system-presence");
            noPayloadCall.deferred.resolve({
                payload: {
                    running: true,
                },
            });
            await noPayloadLoad;

            assertRegression(Array.isArray(state.presenceEntries) && state.presenceEntries.length === 0,
                "presence loader should clear entries for non-array payload");
            assertRegression(state.presenceStatus === "No presence payload.",
                "presence loader should set no-payload status for non-array payload");

            const errorLoad = controller.loadPresence({
                quiet: false,
            });
            const errorCall = harness.takeNextCall("system-presence");
            errorCall.deferred.reject(new Error("presence failure"));
            await errorLoad;

            assertRegression(typeof state.presenceError === "string" && state.presenceError.indexOf("presence failure") >= 0,
                "presence loader should set error message on failure");

            state.presenceError = "existing-presence-error";
            const quietErrorLoad = controller.loadPresence({
                quiet: true,
            });
            const quietErrorCall = harness.takeNextCall("system-presence");
            quietErrorCall.deferred.reject(new Error("quiet-presence-failure"));
            await quietErrorLoad;

            assertRegression(state.presenceError === "existing-presence-error",
                "quiet presence failure should retain existing presenceError");
            summary.push("presence load + payload fallback + error + quiet semantics");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            controller.setAgentsPanel("instances");
            const panelLoad = controller.loadPanelDataForCurrentAgent();
            const panelCall = harness.takeNextCall("system-presence");
            controller.setAgentsPanel("overview");
            panelCall.deferred.resolve({
                payload: [
                    {
                        instanceId: "stale-instance",
                        host: "stale-host",
                    },
                ],
            });
            await panelLoad;

            assertRegression(!Array.isArray(state.presenceEntries) || state.presenceEntries.every(function (entry) {
                return String(entry && entry.instanceId || "") !== "stale-instance";
            }),
                "presence panel stale response should be ignored after panel switch");
            summary.push("presence stale-panel suppression");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            state.usageStartDate = "2026-01-01";
            state.usageEndDate = "2026-01-30";
            const usageLoad = controller.loadUsage({
                quiet: false,
            });
            const sessionsUsageCall = harness.takeNextCall("sessions.usage");
            const usageCostCall = harness.takeNextCall("usage.cost");
            sessionsUsageCall.deferred.resolve({
                payload: {
                    sessions: [
                        {
                            key: "main",
                        },
                    ],
                },
            });
            usageCostCall.deferred.resolve({
                payload: {
                    daily: [],
                },
            });
            await usageLoad;

            assertRegression(Boolean(state.usageResult) && Array.isArray(state.usageResult.sessions) && state.usageResult.sessions.length === 1,
                "usage loader should bind sessions.usage payload");
            assertRegression(Boolean(state.usageCostSummary) && Array.isArray(state.usageCostSummary.daily),
                "usage loader should bind usage.cost payload");
            assertRegression(state.usageError === null,
                "usage loader should clear usageError on success");

            const failedUsageLoad = controller.loadUsage({
                quiet: false,
            });
            const failedSessionsUsageCall = harness.takeNextCall("sessions.usage");
            const failedUsageCostCall = harness.takeNextCall("usage.cost");
            failedSessionsUsageCall.deferred.reject(new Error("usage unavailable"));
            failedUsageCostCall.deferred.resolve({
                payload: {
                    daily: [],
                },
            });
            await failedUsageLoad;

            assertRegression(typeof state.usageError === "string" && state.usageError.indexOf("usage unavailable") >= 0,
                "usage loader should capture usage error semantics");

            const timeSeriesLoad = controller.loadUsageTimeSeries("main");
            const timeSeriesCall = harness.takeNextCall("sessions.usage.timeseries");
            timeSeriesCall.deferred.resolve({
                payload: {
                    points: [],
                },
            });
            await timeSeriesLoad;
            assertRegression(Boolean(state.usageTimeSeries) && Array.isArray(state.usageTimeSeries.points),
                "usage timeseries loader should bind payload");

            const logsLoad = controller.loadUsageSessionLogs("main");
            const logsCall = harness.takeNextCall("sessions.usage.logs");
            logsCall.deferred.resolve({
                payload: {
                    logs: [
                        {
                            role: "assistant",
                            text: "hello",
                        },
                    ],
                },
            });
            await logsLoad;
            assertRegression(Array.isArray(state.usageSessionLogs) && state.usageSessionLogs.length === 1,
                "usage logs loader should bind payload logs array");

            state.usageSelectedSessions = ["main"];
            controller.setAgentsPanel("usage");
            const staleDetailsLoad = controller.loadUsageSessionLogs("main", {
                shouldIgnoreResponse: function () {
                    return state.agentsPanel !== "usage";
                },
            });
            const staleDetailsCall = harness.takeNextCall("sessions.usage.logs");
            controller.setAgentsPanel("overview");
            staleDetailsCall.deferred.resolve({
                payload: {
                    logs: [
                        {
                            role: "assistant",
                            text: "stale-log",
                        },
                    ],
                },
            });
            await staleDetailsLoad;
            assertRegression(!Array.isArray(state.usageSessionLogs) || state.usageSessionLogs.every(function (entry) {
                return String(entry && entry.text || "") !== "stale-log";
            }),
                "usage detail loader should ignore stale response after panel switch");

            const fallbackUsageLoad = controller.loadUsage({
                quiet: false,
            });
            const fallbackSessionsCall = harness.takeNextCall("sessions.usage");
            const fallbackCostCall = harness.takeNextCall("usage.cost");
            fallbackSessionsCall.deferred.reject({
                message: "invalid sessions.usage params: unexpected property 'mode'",
            });
            const retrySessionsCall = harness.takeNextCall("sessions.usage");
            const retryCostCall = harness.takeNextCall("usage.cost");
            fallbackCostCall.deferred.resolve({
                payload: {
                    daily: [],
                },
            });
            retrySessionsCall.deferred.resolve({
                payload: {
                    sessions: [
                        {
                            key: "retry-main",
                        },
                    ],
                },
            });
            retryCostCall.deferred.resolve({
                payload: {
                    daily: [],
                },
            });
            await fallbackUsageLoad;
            assertRegression(Boolean(state.usageResult) && Array.isArray(state.usageResult.sessions) && state.usageResult.sessions.length === 1,
                "usage loader should retry without date interpretation after legacy rejection");

            controller.setAgentsPanel("usage");
            const panelLoad = controller.loadPanelDataForCurrentAgent();
            const panelSessionsCall = harness.takeNextCall("sessions.usage");
            const panelCostCall = harness.takeNextCall("usage.cost");
            controller.setAgentsPanel("overview");
            panelSessionsCall.deferred.resolve({
                payload: {
                    sessions: [
                        {
                            key: "stale-usage",
                        },
                    ],
                },
            });
            panelCostCall.deferred.resolve({
                payload: {
                    daily: [],
                },
            });
            await panelLoad;

            assertRegression(!state.usageResult || !Array.isArray(state.usageResult.sessions) || state.usageResult.sessions.every(function (entry) {
                return String(entry && entry.key || "") !== "stale-usage";
            }),
                "usage panel stale response should be ignored after panel switch");
            summary.push("usage load + details + stale-panel suppression");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const firstLoad = controller.loadNodes({
                quiet: true,
            });
            const firstCall = harness.takeNextCall("node.list");
            firstCall.deferred.resolve({
                payload: {
                    nodes: [
                        {
                            id: "node-a",
                            kind: "worker",
                        },
                    ],
                },
            });
            await firstLoad;

            assertRegression(Array.isArray(state.nodes) && state.nodes.length === 1,
                "nodes loader should bind normalized node.list payload");
            assertRegression(state.nodesError === null,
                "quiet nodes load should not set nodesError");

            state.nodesError = "existing-error";
            const quietFailure = controller.loadNodes({
                quiet: true,
            });
            const quietFailureCall = harness.takeNextCall("node.list");
            quietFailureCall.deferred.reject(new Error("quiet-failure"));
            await quietFailure;

            assertRegression(state.nodesError === "existing-error",
                "quiet nodes failure should retain existing nodesError");

            const noisyFailure = controller.loadNodes({
                quiet: false,
            });
            const noisyFailureCall = harness.takeNextCall("node.list");
            noisyFailureCall.deferred.reject(new Error("visible nodes failure"));
            await noisyFailure;

            assertRegression(typeof state.nodesError === "string" && state.nodesError.indexOf("visible nodes failure") >= 0,
                "non-quiet nodes failure should set nodesError");

            controller.setAgentsPanel("nodes");
            const panelLoad = controller.loadPanelDataForCurrentAgent();
            const panelCall = harness.takeNextCall("node.list");
            controller.setAgentsPanel("overview");
            panelCall.deferred.resolve({
                payload: {
                    nodes: [
                        {
                            id: "stale-node",
                        },
                    ],
                },
            });
            await panelLoad;

            assertRegression(!Array.isArray(state.nodes) || state.nodes.every(function (entry) {
                return String(entry && entry.id || "") !== "stale-node";
            }),
                "nodes panel stale response should be ignored after panel switch");
            summary.push("nodes load + quiet-error + stale-panel suppression");
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
            state.agentsPanel = "cron";
            state.agentsSelectedId = null;
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const panelLoad = controller.loadPanelDataForCurrentAgent();
            const statusCall = harness.takeNextCall("cron.status");
            statusCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 1,
                    nextWakeAtMs: 321,
                },
            });
            const jobsCall = harness.takeNextCall("cron.list");
            jobsCall.deferred.resolve({
                payload: {
                    jobs: [
                        {
                            id: "cron-global-a",
                            name: "Global Cron A",
                            enabled: true,
                            schedule: {
                                kind: "every",
                                everyMs: 60000,
                            },
                            payload: {
                                kind: "agentTurn",
                                message: "Ping",
                            },
                        },
                    ],
                    total: 1,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const runsCall = harness.takeNextCall("cron.runs");
            runsCall.deferred.resolve({
                payload: {
                    entries: [
                        {
                            id: "run-global-a",
                            jobId: "cron-global-a",
                            status: "ok",
                        },
                    ],
                    total: 1,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const modelsCall = harness.takeNextCall("models.list");
            modelsCall.deferred.resolve({
                payload: {
                    models: [
                        {
                            id: "openai:gpt-4.1-mini",
                        },
                    ],
                },
            });
            await panelLoad;

            assertRegression(Boolean(state.agentCronStatusResult) &&
                state.agentCronStatusResult.enabled === true,
            "cron panel should load status even when no agent is selected");
            assertRegression(Array.isArray(state.agentCronJobs) && state.agentCronJobs.length === 1,
                "cron panel should load jobs even when no agent is selected");
            assertRegression(Array.isArray(state.agentCronRuns) && state.agentCronRuns.length === 1,
                "cron panel should load runs even when no agent is selected");
            summary.push("cron global panel hydration without selected agent");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "cron";
            state.agentsSelectedId = "main";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const panelLoad = controller.loadPanelDataForCurrentAgent();
            const statusCall = harness.takeNextCall("cron.status");
            controller.setSelectedAgentId("reviewer");
            statusCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 99,
                    nextWakeAtMs: 999,
                },
            });
            await panelLoad;

            assertRegression(state.agentCronStatusResult === null,
                "cron panel should ignore stale status response after agent switch");
            assertRegression(state.agentCronLoading === false,
                "cron panel should always reset loading flag after stale suppression");
            summary.push("cron stale-response suppression on selected-agent switch");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "files";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const loadPending = controller.loadPanelDataForCurrentAgent();
            const listCall = harness.takeNextCall("gateway.agents.files.list");
            listCall.deferred.resolve({
                payload: {
                    files: [
                        {
                            path: "README.md",
                        },
                    ],
                },
            });
            const getCall = harness.takeNextCall("gateway.agents.files.get");
            getCall.deferred.resolve({
                payload: {
                    file: {
                        path: "README.md",
                        content: "old content",
                    },
                },
            });
            await loadPending;
            controller.updateAgentFileDraft("new content");

            const savePending = controller.saveAgentFileContent({
                agentId: "main",
                path: "README.md",
                content: "new content",
            });
            const setCall = harness.takeNextCall("gateway.agents.files.set");
            setCall.deferred.resolve({
                payload: {
                    file: {
                        path: "README.md",
                        content: "new content",
                    },
                    saved: true,
                },
            });
            await savePending;

            controller.updateCronFormField("name", "Morning digest");
            controller.updateCronFormField("scheduleKind", "every");
            controller.updateCronFormField("everyAmount", "30");
            controller.updateCronFormField("payloadKind", "systemEvent");
            controller.updateCronFormField("payloadText", "Summarize updates");
            controller.updateCronFormField("contextMessages", "2");
            const contextSavePending = controller.addOrUpdateCronJob();
            const historyCall = harness.takeNextCall("chat.history");
            assertRegression(historyCall.params && historyCall.params.sessionKey === "agent:main:default" && historyCall.params.limit === 2,
                "cron add should request chat.history with current session key and contextMessages limit");
            historyCall.deferred.resolve({
                payload: {
                    messages: [
                        {
                            role: "user",
                            text: "Need a morning digest about open issues.",
                        },
                        {
                            role: "assistant",
                            content: [
                                {
                                    type: "text",
                                    text: "I will prepare the summary each run.",
                                },
                            ],
                        },
                    ],
                },
            });
            const contextAddCall = harness.takeNextCall("cron.add");
            assertRegression(
                contextAddCall.params &&
                contextAddCall.params.payload &&
                contextAddCall.params.payload.kind === "systemEvent" &&
                typeof contextAddCall.params.payload.text === "string" &&
                contextAddCall.params.payload.text.indexOf("Recent context:") >= 0 &&
                contextAddCall.params.payload.text.indexOf("- User: Need a morning digest about open issues.") >= 0 &&
                contextAddCall.params.payload.text.indexOf("- Assistant: I will prepare the summary each run.") >= 0,
                "cron add should append bounded recent context lines to systemEvent payload text"
            );
            contextAddCall.deferred.resolve({
                payload: {
                    added: true,
                    cronId: "cron-context",
                },
            });
            const listAfterContextCall = harness.takeNextCall("cron.list");
            listAfterContextCall.deferred.resolve({
                payload: {
                    jobs: [
                        {
                            id: "cron-main",
                            name: "Main cron",
                            enabled: true,
                            schedule: {
                                kind: "every",
                                everyMs: 60000,
                            },
                            payload: {
                                kind: "agentTurn",
                                message: "Ping",
                            },
                        },
                    ],
                    total: 1,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const statusAfterContextCall = harness.takeNextCall("cron.status");
            statusAfterContextCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 1,
                    nextWakeAtMs: 321,
                },
            });
            const runsAfterContextCall = harness.takeNextCall("cron.runs");
            runsAfterContextCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await contextSavePending;

            controller.updateCronFormField("name", "Agent context parity");
            controller.updateCronFormField("payloadKind", "agentTurn");
            controller.updateCronFormField("payloadText", "Summarize for agent");
            controller.updateCronFormField("contextMessages", "2");
            const agentContextSavePending = controller.addOrUpdateCronJob();
            const agentHistoryCall = harness.takeNextCall("chat.history");
            assertRegression(
                agentHistoryCall.params &&
                agentHistoryCall.params.sessionKey === "agent:main:default" &&
                agentHistoryCall.params.limit === 2,
                "cron add should request chat.history for agentTurn payload context parity"
            );
            agentHistoryCall.deferred.resolve({
                payload: {
                    messages: [
                        {
                            role: "user",
                            content: [
                                {
                                    type: "output_text",
                                    text: {
                                        value: "What changed since yesterday?",
                                    },
                                },
                            ],
                        },
                        {
                            role: "assistant",
                            content: [
                                {
                                    type: "output_text",
                                    value: "I tracked the latest merged fixes.",
                                },
                            ],
                        },
                    ],
                },
            });
            const agentContextAddCall = harness.takeNextCall("cron.add");
            assertRegression(
                agentContextAddCall.params &&
                agentContextAddCall.params.payload &&
                agentContextAddCall.params.payload.kind === "agentTurn" &&
                typeof agentContextAddCall.params.payload.message === "string" &&
                agentContextAddCall.params.payload.message.indexOf("Recent context:") >= 0 &&
                agentContextAddCall.params.payload.message.indexOf("- User: What changed since yesterday?") >= 0 &&
                agentContextAddCall.params.payload.message.indexOf("- Assistant: I tracked the latest merged fixes.") >= 0,
                "cron add should append bounded recent context lines to agentTurn payload message"
            );
            agentContextAddCall.deferred.resolve({
                payload: {
                    added: true,
                    cronId: "cron-agent-context",
                },
            });
            const listAfterAgentContextCall = harness.takeNextCall("cron.list");
            listAfterAgentContextCall.deferred.resolve({
                payload: {
                    jobs: [
                        {
                            id: "cron-main",
                            name: "Main cron",
                            enabled: true,
                            schedule: {
                                kind: "every",
                                everyMs: 60000,
                            },
                            payload: {
                                kind: "agentTurn",
                                message: "Ping",
                            },
                        },
                    ],
                    total: 1,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const statusAfterAgentContextCall = harness.takeNextCall("cron.status");
            statusAfterAgentContextCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 1,
                    nextWakeAtMs: 432,
                },
            });
            const runsAfterAgentContextCall = harness.takeNextCall("cron.runs");
            runsAfterAgentContextCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await agentContextSavePending;

            controller.updateCronFormField("name", "No context");
            controller.updateCronFormField("payloadKind", "systemEvent");
            controller.updateCronFormField("payloadText", "Do not inject context");
            controller.updateCronFormField("contextMessages", "");
            const noContextSavePending = controller.addOrUpdateCronJob();
            const noContextAddCall = harness.takeNextCall("cron.add");
            assertRegression(
                noContextAddCall.params &&
                noContextAddCall.params.payload &&
                noContextAddCall.params.payload.kind === "systemEvent" &&
                String(noContextAddCall.params.payload.text || "") === "Do not inject context",
                "cron add should preserve systemEvent payload text when contextMessages is not set"
            );
            noContextAddCall.deferred.resolve({
                payload: {
                    added: true,
                    cronId: "cron-no-context",
                },
            });
            const listAfterNoContextCall = harness.takeNextCall("cron.list");
            listAfterNoContextCall.deferred.resolve({
                payload: {
                    jobs: [
                        {
                            id: "cron-main",
                            name: "Main cron",
                            enabled: true,
                            schedule: {
                                kind: "every",
                                everyMs: 60000,
                            },
                            payload: {
                                kind: "agentTurn",
                                message: "Ping",
                            },
                        },
                    ],
                    total: 1,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const statusAfterNoContextCall = harness.takeNextCall("cron.status");
            statusAfterNoContextCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 1,
                    nextWakeAtMs: 654,
                },
            });
            const runsAfterNoContextCall = harness.takeNextCall("cron.runs");
            runsAfterNoContextCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await noContextSavePending;

            assertRegression(state.agentFileSaveBusy === false &&
                state.agentFileSaveStatus === "Saved" &&
                state.agentFileEditBaseContent === "new content" &&
                state.agentFileEditDraft === "new content",
            "agent file save should update draft/base content after success");
            summary.push("agent-files save success");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "files";
            state.agentFileSelectedPath = "README.md";
            state.agentFileEditDraft = "next content";
            state.agentFileEditBaseContent = "stable content";
            state.agentFileContentResult = {
                file: {
                    path: "README.md",
                    content: "stable content",
                },
            };
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const savePending = controller.saveAgentFileContent({
                agentId: "main",
                path: "README.md",
                content: "next content",
            });
            const setCall = harness.takeNextCall("gateway.agents.files.set");
            setCall.deferred.reject(new Error("conflict detected"));
            await savePending;

            assertRegression(state.agentFileSaveBusy === false &&
                String(state.agentFileSaveError || "").indexOf("conflict detected") >= 0 &&
                state.agentFileContentResult &&
                state.agentFileContentResult.file &&
                state.agentFileContentResult.file.content === "stable content",
            "agent file save should rollback optimistic content on error");
            summary.push("agent-files save rollback");
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

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });
            state.skillsHubQuery = "weather";
            const pending = controller.searchSkillsHub();
            const searchCall = harness.takeNextCall("skills.search");
            searchCall.deferred.resolve({
                payload: {
                    skills: [],
                    count: 0,
                },
            });
            await pending;

            assertRegression(Array.isArray(state.skillsHubResults) && state.skillsHubResults.length === 0,
                "skills search should preserve explicit empty payload result");
            summary.push("skills search empty");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });
            const pending = controller.installSkill("weather");
            const installCall = harness.takeNextCall("gateway.skills.install.execute");
            installCall.deferred.resolve({
                payload: {
                    skill: "weather",
                    executed: true,
                    warning: "none",
                },
            });
            await pending;

            assertRegression(state.skillsInstallBusy === false && String(state.skillsInstallStatus || "").indexOf("\"skill\": \"weather\"") >= 0,
                "skills install success should capture payload status");
            summary.push("skills install success");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });
            const pending = controller.installSkill("weather");
            const installCall = harness.takeNextCall("gateway.skills.install.execute");
            installCall.deferred.reject(new Error("install denied"));
            await pending;

            assertRegression(state.skillsInstallBusy === false && String(state.skillsHubError || "").indexOf("install denied") >= 0,
                "skills install failure should bind error text");
            summary.push("skills install failure");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });
            state.skillsEditPayload = "{\"skill\":\"weather\",\"enabled\":true}";
            const pending = controller.updateSkillConfig();
            const updateCall = harness.takeNextCall("skills.update");
            updateCall.deferred.resolve({
                payload: {
                    updated: true,
                    hash: "skills-1",
                },
            });
            await pending;

            assertRegression(state.skillsEditBusy === false && String(state.skillsEditStatus || "").indexOf("\"updated\": true") >= 0,
                "skills edit should store round-trip response payload");
            summary.push("skills edit roundtrip");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "dreaming";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            const loadPending = controller.loadPanelDataForCurrentAgent();
            const configCall = harness.takeNextCall("config.get");
            configCall.deferred.resolve({
                payload: {
                    hash: "hash-1",
                    config: {
                        plugins: {
                            slots: {
                                memory: "memory-core",
                            },
                            entries: {
                                "memory-core": {
                                    config: {
                                        dreaming: {
                                            enabled: true,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            });
            const statusCall = harness.takeNextCall("doctor.memory.status");
            statusCall.deferred.resolve({
                payload: {
                    ok: true,
                    status: "healthy",
                    dreaming: {
                        enabled: true,
                        timezone: "UTC",
                        verboseLogging: false,
                        storageMode: "inline",
                        separateReports: false,
                        shortTermCount: 1,
                        recallSignalCount: 2,
                        dailySignalCount: 3,
                        groundedSignalCount: 4,
                        totalSignalCount: 5,
                        phaseSignalCount: 6,
                        lightPhaseHitCount: 7,
                        remPhaseHitCount: 8,
                        promotedTotal: 9,
                        promotedToday: 1,
                        shortTermEntries: [],
                        signalEntries: [],
                        promotedEntries: [],
                    },
                },
            });
            const diaryCall = harness.takeNextCall("doctor.memory.dreamDiary");
            diaryCall.deferred.resolve({
                payload: {
                    entries: [],
                    count: 0,
                    source: "memory",
                    found: true,
                    path: "DREAMS.md",
                    content: "Dream diary content",
                },
            });
            await loadPending;

            assertRegression(Boolean(state.agentDreamingResult),
                "dreaming panel loader should produce dreaming projection result");
            assertRegression(Boolean(state.dreamingStatus) && state.dreamingStatus.enabled === true,
                "dreaming panel loader should bind normalized dreaming status payload");
            assertRegression(state.dreamDiaryPath === "DREAMS.md" && state.dreamDiaryContent === "Dream diary content",
                "dreaming panel loader should bind dream diary found/path/content payload");
            assertRegression(state.dreamingResolvedPluginId === "memory-core",
                "dreaming panel loader should resolve configured memory plugin id");

            const enablePending = controller.updateDreamingEnabled(false);
            const configGetForEnable = harness.takeNextCall("config.get");
            configGetForEnable.deferred.resolve({
                payload: {
                    hash: "hash-1",
                    config: {
                        plugins: {
                            slots: {
                                memory: "memory-core",
                            },
                            entries: {
                                "memory-core": {
                                    config: {
                                        dreaming: {
                                            enabled: true,
                                        },
                                    },
                                },
                            },
                        },
                    },
                },
            });
            const schemaLookupCall = harness.takeNextCall("config.schema.lookup");
            schemaLookupCall.deferred.resolve({
                payload: {
                    path: "plugins.entries.memory-core.config",
                    schema: {
                        additionalProperties: false,
                    },
                    children: [
                        {
                            key: "dreaming",
                        },
                    ],
                },
            });
            const configPatchCall = harness.takeNextCall("config.patch");
            assertRegression(configPatchCall.params && configPatchCall.params.baseHash === "hash-1",
                "updateDreamingEnabled should include config hash when patching dreaming state");
            assertRegression(String(configPatchCall.params && configPatchCall.params.sessionKey || "") === state.sessionKey,
                "updateDreamingEnabled should include current sessionKey in config.patch");
            configPatchCall.deferred.resolve({
                payload: {
                    patched: true,
                    updated: true,
                    hash: "hash-2",
                    dreamingEnabled: false,
                },
            });
            const enableResult = await enablePending;
            assertRegression(enableResult === true,
                "updateDreamingEnabled should resolve true after successful config.patch");
            assertRegression(Boolean(state.dreamingStatus) && state.dreamingStatus.enabled === false,
                "updateDreamingEnabled should update local dreaming enabled projection");
            assertRegression(state.dreamingConfigSnapshotHash === "hash-1",
                "updateDreamingEnabled should keep current snapshot hash until refresh reload");

            const backfillPending = controller.backfillDreamDiary({});
            const backfillCall = harness.takeNextCall("doctor.memory.backfillDreamDiary");
            backfillCall.deferred.resolve({
                payload: {
                    queued: true,
                    status: "scheduled",
                },
            });
            const diaryReloadCall = harness.takeNextCall("doctor.memory.dreamDiary");
            diaryReloadCall.deferred.resolve({
                payload: {
                    entries: [],
                    count: 0,
                    source: "memory",
                    found: false,
                    path: "DREAMS.md",
                    content: null,
                },
            });
            const statusReloadCall = harness.takeNextCall("doctor.memory.status");
            statusReloadCall.deferred.resolve({
                payload: {
                    ok: true,
                    status: "healthy",
                    dreaming: {
                        enabled: false,
                        verboseLogging: false,
                        storageMode: "inline",
                        separateReports: false,
                        shortTermCount: 0,
                        recallSignalCount: 0,
                        dailySignalCount: 0,
                        groundedSignalCount: 0,
                        totalSignalCount: 0,
                        phaseSignalCount: 0,
                        lightPhaseHitCount: 0,
                        remPhaseHitCount: 0,
                        promotedTotal: 0,
                        promotedToday: 0,
                        shortTermEntries: [],
                        signalEntries: [],
                        promotedEntries: [],
                    },
                },
            });
            const backfillResult = await backfillPending;
            assertRegression(backfillResult === true,
                "backfillDreamDiary should resolve true after successful action+reload flow");

            const resetGroundedPending = controller.resetGroundedShortTerm({});
            const resetGroundedCall = harness.takeNextCall("doctor.memory.resetGroundedShortTerm");
            resetGroundedCall.deferred.resolve({
                payload: {
                    reset: true,
                    target: "groundedShortTerm",
                },
            });
            const statusAfterResetCall = harness.takeNextCall("doctor.memory.status");
            statusAfterResetCall.deferred.resolve({
                payload: {
                    ok: true,
                    status: "healthy",
                    dreaming: {
                        enabled: false,
                        verboseLogging: false,
                        storageMode: "inline",
                        separateReports: false,
                        shortTermCount: 0,
                        recallSignalCount: 0,
                        dailySignalCount: 0,
                        groundedSignalCount: 0,
                        totalSignalCount: 0,
                        phaseSignalCount: 0,
                        lightPhaseHitCount: 0,
                        remPhaseHitCount: 0,
                        promotedTotal: 0,
                        promotedToday: 0,
                        shortTermEntries: [],
                        signalEntries: [],
                        promotedEntries: [],
                    },
                },
            });
            const resetGroundedResult = await resetGroundedPending;
            assertRegression(resetGroundedResult === true,
                "resetGroundedShortTerm should resolve true after successful action flow");

            let noDiaryReloadQueued = false;
            try {
                harness.takeNextCall("doctor.memory.dreamDiary");
            } catch (_) {
                noDiaryReloadQueued = true;
            }
            assertRegression(noDiaryReloadQueued,
                "resetGroundedShortTerm should not reload dream diary when reloadDiary=false");

            controller.setDreamingSubTab("advanced");
            controller.setDreamingAdvancedWaitingSort("signals");
            controller.setDreamDiaryPage(2);
            const uiModel = controller.getDreamingUiModel();
            assertRegression(uiModel.subTab === "advanced",
                "dreaming ui model should expose selected sub-tab");
            assertRegression(uiModel.waitingSort === "signals",
                "dreaming ui model should expose selected waiting sort");
            assertRegression(Array.isArray(uiModel.waitingEntries),
                "dreaming ui model should include waitingEntries array");
            assertRegression(typeof uiModel.phrase === "string" && uiModel.phrase.length > 0,
                "dreaming ui model should include rotating phrase text");

            summary.push("dreaming controller parity baseline + ui model");
        }

        {
            const state = createRegressionState();
            const harness = createRegressionHarnessRequestStub();
            const settlements = [];
            const controller = createAgentsController({
                state,
                request: harness.request,
                onCronCliExecutionSettled: function (result) {
                    settlements.push(result);
                },
            });

            const notCron = controller.parseCronCliSlashCommand("hello world");
            assertRegression(notCron && notCron.handled === false,
                "cron slash parser should ignore non-/cron input");

            const listParsed = controller.parseCronCliSlashCommand("/cron list --all --limit 20 --query \"nightly report\"");
            assertRegression(listParsed && listParsed.ok === true && listParsed.command === "list",
                "cron slash parser should resolve canonical list command");
            assertRegression(listParsed && listParsed.args && listParsed.args.options &&
                listParsed.args.options.all === true &&
                listParsed.args.options.limit === 20 &&
                listParsed.args.options.query === "nightly report",
            "cron slash parser should coerce list options deterministically");

            const aliasParsed = controller.parseCronCliSlashCommand("/cron create --name \"Nightly report\" --every 30m --message \"ship it\"");
            assertRegression(aliasParsed && aliasParsed.ok === true && aliasParsed.command === "add",
                "cron slash parser should resolve create alias to add command");
            assertRegression(aliasParsed && aliasParsed.args && aliasParsed.args.options &&
                aliasParsed.args.options.name === "Nightly report" &&
                aliasParsed.args.options.every === "30m" &&
                aliasParsed.args.options.message === "ship it",
            "cron slash parser should preserve quoted option values");

            const unknownOption = controller.parseCronCliSlashCommand("/cron status --bogus");
            assertRegression(unknownOption && unknownOption.ok === false &&
                String(unknownOption.error || "").indexOf("Unknown option") >= 0,
            "cron slash parser should reject unknown options by command surface");

            const unclosedQuote = controller.parseCronCliSlashCommand("/cron add --name \"Nightly");
            assertRegression(unclosedQuote && unclosedQuote.ok === false &&
                String(unclosedQuote.error || "").indexOf("Unclosed quote") >= 0,
            "cron slash parser should reject unclosed quoted input");

            const shortOption = controller.parseCronCliSlashCommand("/cron list -a");
            assertRegression(shortOption && shortOption.ok === false &&
                String(shortOption.error || "").indexOf("Unsupported short option") >= 0,
            "cron slash parser should reject unsupported short options");

            const trackedError = controller.tryParseCronCliSlashCommand("/cron wake --unknown");
            assertRegression(trackedError && trackedError.ok === false &&
                typeof state.agentCronCliParseError === "string" &&
                state.agentCronCliParseError.indexOf("Unknown option") >= 0,
            "cron slash parser should persist parse error state through tracked parser helper");

            const trackedSuccess = controller.tryParseCronCliSlashCommand("/cron wake --mode next-heartbeat");
            assertRegression(trackedSuccess && trackedSuccess.ok === true &&
                state.agentCronCliParseError === null &&
                state.agentCronCliLastParsed &&
                state.agentCronCliLastParsed.command === "wake",
            "cron slash parser should persist last parsed command and clear prior parse error on success");

            const listPlan = controller.planCronCliSlashCommand("/cron list --all --limit 20 --offset 3 --query nightly --sort-dir desc");
            assertRegression(listPlan && listPlan.handled === true && listPlan.method === "cron.list",
                "cron slash planner should map list command to cron.list");
            assertRegression(listPlan && listPlan.params && listPlan.params.includeDisabled === true &&
                listPlan.params.limit === 20 && listPlan.params.offset === 3,
            "cron slash planner should normalize list defaults and numeric options");

            const removePlan = controller.planCronCliSlashCommand("/cron delete cron-main");
            assertRegression(removePlan && removePlan.method === "cron.remove" &&
                removePlan.params && removePlan.params.id === "cron-main" &&
                removePlan.params.jobId === "cron-main",
            "cron slash planner should normalize remove aliases to cron.remove id/jobId payload");

            const runDuePlan = controller.planCronCliSlashCommand("/cron run cron-main --due");
            assertRegression(runDuePlan && runDuePlan.method === "cron.run" &&
                runDuePlan.params && runDuePlan.params.mode === "due",
            "cron slash planner should normalize run due-mode alias behavior");

            const wakePlan = controller.planCronCliSlashCommand("/cron wake --mode next_heartbeat");
            assertRegression(wakePlan && wakePlan.method === "wake" &&
                wakePlan.params && wakePlan.params.mode === "next-heartbeat",
            "cron slash planner should normalize wake mode aliases to strict taxonomy");

            const addMissingNamePlan = controller.planCronCliSlashCommand("/cron add --every 30m --message hi");
            assertRegression(addMissingNamePlan && addMissingNamePlan.ux && addMissingNamePlan.ux.ok === false &&
                addMissingNamePlan.ux.code === "missing_required" &&
                String(addMissingNamePlan.ux.message || "").indexOf("--name") >= 0,
            "cron slash planner should return structured missing-required UX for add without --name");

            const addInvalidPayloadPlan = controller.planCronCliSlashCommand("/cron add --name Nightly --every 30m --message hi --system-event hey");
            assertRegression(addInvalidPayloadPlan && addInvalidPayloadPlan.ux && addInvalidPayloadPlan.ux.ok === false &&
                addInvalidPayloadPlan.ux.code === "missing_required",
            "cron slash planner should return structured payload contract error for conflicting add payload options");

            const editMissingIdPlan = controller.planCronCliSlashCommand("/cron update --message hi");
            assertRegression(editMissingIdPlan && editMissingIdPlan.ux && editMissingIdPlan.ux.ok === false &&
                editMissingIdPlan.ux.code === "missing_required" &&
                String(editMissingIdPlan.ux.message || "").indexOf("job id") >= 0,
            "cron slash planner should enforce required edit job id contract");

            const helpPlan = controller.planCronCliSlashCommand("/cron help runs");
            assertRegression(helpPlan && helpPlan.ux && helpPlan.ux.ok === true &&
                helpPlan.ux.code === "help" &&
                helpPlan.ux.command === "runs" &&
                typeof helpPlan.ux.usage === "string" &&
                helpPlan.ux.usage.indexOf("/cron runs") === 0,
            "cron slash planner should provide structured help contract for subcommands");

            settlements.length = 0;
            const executeStatusPending = await controller.executeCronCliSlashCommand("/cron status");
            assertRegression(executeStatusPending && executeStatusPending.kind === "pending" &&
                executeStatusPending.envelope && executeStatusPending.envelope.code === "pending",
            "cron slash execution bridge should return pending envelope before gateway settlement");
            const statusExecCall = harness.takeNextCall("cron.status");
            statusExecCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 2,
                    nextWakeAtMs: 123,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            const executeStatusResult = settlements[0];
            assertRegression(executeStatusResult && executeStatusResult.ok === true &&
                executeStatusResult.envelope &&
                executeStatusResult.envelope.method === "cron.status" &&
                Array.isArray(executeStatusResult.envelope.refreshViews) &&
                executeStatusResult.envelope.refreshViews.indexOf("status") >= 0,
            "cron slash execution bridge should route status to cron.status with deterministic success envelope");

            settlements.length = 0;
            const executeListPending = await controller.executeCronCliSlashCommand("/cron list --all --limit 5 --offset 1 --query nightly");
            assertRegression(executeListPending && executeListPending.kind === "pending",
                "cron slash execution bridge should keep list dispatch non-blocking");
            const listExecCall = harness.takeNextCall("cron.list");
            assertRegression(listExecCall.params &&
                listExecCall.params.includeDisabled === true &&
                listExecCall.params.limit === 5 &&
                listExecCall.params.offset === 1,
            "cron slash execution bridge should map list payload options to cron.list params");
            listExecCall.deferred.resolve({
                payload: {
                    jobs: [],
                    total: 0,
                    offset: 1,
                    limit: 5,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            const executeListResult = settlements[0];
            assertRegression(executeListResult && executeListResult.ok === true &&
                executeListResult.envelope &&
                executeListResult.envelope.method === "cron.list",
            "cron slash execution bridge should return deterministic list execution envelope");

            settlements.length = 0;
            const executeRunsPending = await controller.executeCronCliSlashCommand("/cron runs --id cron-main --limit 10");
            assertRegression(executeRunsPending && executeRunsPending.kind === "pending",
                "cron slash execution bridge should keep runs dispatch non-blocking");
            const runsExecCall = harness.takeNextCall("cron.runs");
            assertRegression(runsExecCall.params &&
                runsExecCall.params.id === "cron-main" &&
                runsExecCall.params.jobId === "cron-main" &&
                runsExecCall.params.scope === "job" &&
                runsExecCall.params.limit === 10,
            "cron slash execution bridge should map runs command to job-scoped cron.runs payload");
            runsExecCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 10,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            const executeRunsResult = settlements[0];
            assertRegression(executeRunsResult && executeRunsResult.ok === true &&
                executeRunsResult.envelope &&
                executeRunsResult.envelope.method === "cron.runs",
            "cron slash execution bridge should return deterministic runs execution envelope");

            settlements.length = 0;
            const executeWakePending = await controller.executeCronCliSlashCommand("/cron wake --mode next_heartbeat");
            assertRegression(executeWakePending && executeWakePending.kind === "pending",
                "cron slash execution bridge should keep wake dispatch non-blocking");
            const wakeExecCall = harness.takeNextCall("wake");
            assertRegression(wakeExecCall.params && wakeExecCall.params.mode === "next-heartbeat",
                "cron slash execution bridge should normalize wake mode aliases for wake routing");
            wakeExecCall.deferred.resolve({
                payload: {
                    ok: true,
                    mode: "next-heartbeat",
                },
            });
            const wakeStatusRefreshCall = harness.takeNextCall("cron.status");
            wakeStatusRefreshCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 2,
                    nextWakeAtMs: 234,
                },
            });
            const wakeRunsRefreshCall = harness.takeNextCall("cron.runs");
            wakeRunsRefreshCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            const executeWakeResult = settlements[0];
            assertRegression(executeWakeResult && executeWakeResult.ok === true &&
                executeWakeResult.envelope &&
                Array.isArray(executeWakeResult.envelope.refreshViews) &&
                executeWakeResult.envelope.refreshViews.indexOf("status") >= 0 &&
                executeWakeResult.envelope.refreshViews.indexOf("runs") >= 0,
            "cron slash execution bridge should apply wake post-action refresh policy for status/runs views");

            settlements.length = 0;
            const executeRunPending = await controller.executeCronCliSlashCommand("/cron run cron-main --due");
            assertRegression(executeRunPending && executeRunPending.kind === "pending",
                "cron slash execution bridge should keep run dispatch non-blocking");
            const runExecCall = harness.takeNextCall("cron.run");
            assertRegression(runExecCall.params && runExecCall.params.mode === "due",
                "cron slash execution bridge should map run due alias to cron.run due mode");
            runExecCall.deferred.resolve({
                payload: {
                    runId: "run-1",
                    started: true,
                },
            });
            const runRefreshRunsCall = harness.takeNextCall("cron.runs");
            runRefreshRunsCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const runRefreshStatusCall = harness.takeNextCall("cron.status");
            runRefreshStatusCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 2,
                    nextWakeAtMs: 345,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            const executeRunResult = settlements[0];
            assertRegression(executeRunResult && executeRunResult.ok === true &&
                executeRunResult.envelope &&
                executeRunResult.envelope.method === "cron.run" &&
                Array.isArray(executeRunResult.envelope.refreshViews) &&
                executeRunResult.envelope.refreshViews.indexOf("runs") >= 0,
            "cron slash execution bridge should apply run post-action refresh policy");

            settlements.length = 0;
            const executeRemovePending = await controller.executeCronCliSlashCommand("/cron remove cron-main");
            assertRegression(executeRemovePending && executeRemovePending.kind === "pending",
                "cron slash execution bridge should keep remove dispatch non-blocking");
            const removeExecCall = harness.takeNextCall("cron.remove");
            assertRegression(removeExecCall.params &&
                removeExecCall.params.id === "cron-main" &&
                removeExecCall.params.jobId === "cron-main",
            "cron slash execution bridge should map remove identity aliases to cron.remove payload");
            removeExecCall.deferred.resolve({
                payload: {
                    removed: true,
                },
            });
            const removeRefreshListCall = harness.takeNextCall("cron.list");
            removeRefreshListCall.deferred.resolve({
                payload: {
                    jobs: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const removeRefreshStatusCall = harness.takeNextCall("cron.status");
            removeRefreshStatusCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 1,
                    nextWakeAtMs: null,
                },
            });
            const removeRefreshRunsCall = harness.takeNextCall("cron.runs");
            removeRefreshRunsCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            const executeRemoveResult = settlements[0];
            assertRegression(executeRemoveResult && executeRemoveResult.ok === true &&
                executeRemoveResult.envelope &&
                Array.isArray(executeRemoveResult.envelope.refreshViews) &&
                executeRemoveResult.envelope.refreshViews.indexOf("list") >= 0,
            "cron slash execution bridge should apply remove post-action refresh policy");

            settlements.length = 0;
            const executeAddPending = await controller.executeCronCliSlashCommand(
                "/cron add --name nightly --every 30m --message hello --to https://example.test/hook"
            );
            assertRegression(executeAddPending && executeAddPending.kind === "pending",
                "cron slash execution bridge should keep add dispatch non-blocking");
            const addExecCall = harness.takeNextCall("cron.add");
            assertRegression(addExecCall.params &&
                addExecCall.params.name === "nightly" &&
                addExecCall.params.schedule &&
                addExecCall.params.schedule.kind === "every" &&
                addExecCall.params.schedule.everyMs === 1800000 &&
                addExecCall.params.payload &&
                addExecCall.params.payload.kind === "agentTurn" &&
                addExecCall.params.payload.message === "hello" &&
                addExecCall.params.delivery &&
                addExecCall.params.delivery.mode === "webhook" &&
                addExecCall.params.delivery.to === "https://example.test/hook",
            "cron slash execution bridge should coerce add schedule/delivery payloads and route cron.add");
            addExecCall.deferred.resolve({
                payload: {
                    added: true,
                    cronId: "cron-new",
                },
            });
            const addRefreshListCall = harness.takeNextCall("cron.list");
            addRefreshListCall.deferred.resolve({
                payload: {
                    jobs: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const addRefreshStatusCall = harness.takeNextCall("cron.status");
            addRefreshStatusCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 2,
                    nextWakeAtMs: 456,
                },
            });
            const addRefreshRunsCall = harness.takeNextCall("cron.runs");
            addRefreshRunsCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            const executeAddResult = settlements[0];
            assertRegression(executeAddResult && executeAddResult.ok === true &&
                executeAddResult.envelope &&
                executeAddResult.envelope.method === "cron.add" &&
                Array.isArray(executeAddResult.envelope.refreshViews) &&
                executeAddResult.envelope.refreshViews.indexOf("list") >= 0,
            "cron slash execution bridge should apply add post-action refresh policy");

            settlements.length = 0;
            const executeEditPending = await controller.executeCronCliSlashCommand(
                "/cron edit cron-main --message patched --failure-alert-after 2 --failure-alert-cooldown 10"
            );
            assertRegression(executeEditPending && executeEditPending.kind === "pending",
                "cron slash execution bridge should keep edit dispatch non-blocking");
            const editExecCall = harness.takeNextCall("cron.update");
            assertRegression(editExecCall.params &&
                editExecCall.params.id === "cron-main" &&
                editExecCall.params.patch &&
                editExecCall.params.patch.payload &&
                editExecCall.params.patch.payload.kind === "agentTurn" &&
                editExecCall.params.patch.payload.message === "patched" &&
                editExecCall.params.patch.failureAlert &&
                editExecCall.params.patch.failureAlert.after === 2 &&
                editExecCall.params.patch.failureAlert.cooldownMs === 10000,
            "cron slash execution bridge should route edit payload through cron.update patch parity shaping");
            editExecCall.deferred.resolve({
                payload: {
                    updated: true,
                },
            });
            const editRefreshListCall = harness.takeNextCall("cron.list");
            editRefreshListCall.deferred.resolve({
                payload: {
                    jobs: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const editRefreshStatusCall = harness.takeNextCall("cron.status");
            editRefreshStatusCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 2,
                    nextWakeAtMs: 567,
                },
            });
            const editRefreshRunsCall = harness.takeNextCall("cron.runs");
            editRefreshRunsCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            const executeEditResult = settlements[0];
            assertRegression(executeEditResult && executeEditResult.ok === true &&
                executeEditResult.envelope &&
                executeEditResult.envelope.method === "cron.update",
            "cron slash execution bridge should apply edit post-action refresh policy and deterministic envelope");

            settlements.length = 0;
            const staleFirstPending = await controller.executeCronCliSlashCommand("/cron status");
            const staleSecondPending = await controller.executeCronCliSlashCommand("/cron list --limit 1");
            assertRegression(staleFirstPending && staleFirstPending.envelope &&
                staleSecondPending && staleSecondPending.envelope &&
                staleSecondPending.envelope.sequence > staleFirstPending.envelope.sequence,
            "cron slash execution bridge should assign monotonic sequence tokens for overlapping commands");
            const staleListCall = harness.takeNextCall("cron.list");
            staleListCall.deferred.resolve({
                payload: {
                    jobs: [],
                    total: 0,
                    offset: 0,
                    limit: 1,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            assertRegression(settlements.length === 1 &&
                settlements[0].envelope &&
                settlements[0].envelope.command === "list",
            "cron slash execution bridge should settle only the latest overlapping command");
            const staleStatusCall = harness.takeNextCall("cron.status");
            staleStatusCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 99,
                    nextWakeAtMs: 999,
                },
            });
            await awaitCronCliRegressionSettlements(settlements, 1);
            assertRegression(settlements.length === 1 &&
                settlements[0].envelope &&
                settlements[0].envelope.command === "list",
            "cron slash execution bridge should discard stale out-of-order completion envelopes");

            const parseErrorResult = await controller.executeCronCliSlashCommand("/cron list --bogus");
            assertRegression(parseErrorResult && parseErrorResult.ok === false &&
                parseErrorResult.envelope &&
                parseErrorResult.envelope.code === "unknown_option",
            "cron slash execution bridge should project parser errors into deterministic error envelopes");

            const payloadErrorResult = await controller.executeCronCliSlashCommand("/cron add --name nightly --message hi --system-event ping --every 30m");
            assertRegression(payloadErrorResult && payloadErrorResult.ok === false &&
                payloadErrorResult.envelope &&
                payloadErrorResult.envelope.code === "missing_required",
            "cron slash execution bridge should project payload validation errors into deterministic error envelopes");

            summary.push("cron slash parser baseline determinism");
            summary.push("cron slash non-blocking execution bridge");
        }

        {
            const state = createRegressionState();
            state.agentsPanel = "cron";
            state.agentCronJobs = [
                {
                    id: "cron-main",
                    name: "Main cron",
                    enabled: true,
                    schedule: {
                        kind: "every",
                        everyMs: 60000,
                    },
                    payload: {
                        kind: "agentTurn",
                        message: "Ping",
                    },
                },
            ];
            state.agentCronSelectedJobId = "cron-main";
            const harness = createRegressionHarnessRequestStub();
            const controller = createAgentsController({
                state,
                request: harness.request,
            });

            controller.updateCronFormField("name", "");
            controller.updateCronFormField("payloadText", "hello");
            const invalid = await controller.addOrUpdateCronJob();
            assertRegression(invalid === true,
                "cron save should complete without transport call when validation fails");
            assertRegression(Boolean(state.agentCronFieldErrors.name),
                "cron save should populate field error for missing name");

            controller.updateCronFormField("name", "Nightly");
            controller.updateCronFormField("scheduleKind", "every");
            controller.updateCronFormField("everyAmount", "15");
            controller.updateCronFormField("payloadKind", "agentTurn");
            controller.updateCronFormField("payloadText", "Hello agent");
            const savePending = controller.addOrUpdateCronJob();
            const addCall = harness.takeNextCall("cron.add");
            assertRegression(addCall.params && addCall.params.name === "Nightly",
                "cron add should forward normalized job payload name");
            assertRegression(addCall.params && addCall.params.delivery && addCall.params.delivery.mode === "none",
                "cron add should include explicit delivery mode payload");
            addCall.deferred.resolve({
                payload: {
                    added: true,
                    cronId: "cron-new",
                },
            });
            const listCall = harness.takeNextCall("cron.list");
            listCall.deferred.resolve({
                payload: {
                    jobs: [
                        {
                            id: "cron-main",
                            name: "Main cron",
                            enabled: true,
                            schedule: {
                                kind: "every",
                                everyMs: 60000,
                            },
                            payload: {
                                kind: "agentTurn",
                                message: "Ping",
                            },
                        },
                    ],
                    total: 1,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const statusCall = harness.takeNextCall("cron.status");
            statusCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 1,
                    nextWakeAtMs: 123,
                },
            });
            const runsCall = harness.takeNextCall("cron.runs");
            runsCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await savePending;

            assertRegression(state.agentCronBusy === false,
                "cron add should always reset busy state");
            assertRegression(state.agentCronFieldErrors && Object.keys(state.agentCronFieldErrors).length === 0,
                "cron add should clear field errors after valid save");

            const runPending = controller.runCronJobNow("cron-main", "due");
            const runCall = harness.takeNextCall("cron.run");
            assertRegression(runCall.params && runCall.params.id === "cron-main" && runCall.params.jobId === "cron-main" && runCall.params.mode === "due",
                "cron run-now should call cron.run with selected id/jobId aliases and mode");
            runCall.deferred.resolve({
                payload: {
                    runId: "cron-run-1",
                    started: true,
                },
            });
            const runsRefreshCall = harness.takeNextCall("cron.runs");
            runsRefreshCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await runPending;

            const wakePending = controller.triggerCronWake("next-heartbeat");
            const wakeCall = harness.takeNextCall("wake");
            assertRegression(wakeCall.params && wakeCall.params.mode === "next-heartbeat",
                "cron wake should call wake with explicit next-heartbeat mode");
            wakeCall.deferred.resolve({
                payload: {
                    ok: true,
                    mode: "next-heartbeat",
                },
            });
            const statusAfterWakeCall = harness.takeNextCall("cron.status");
            statusAfterWakeCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 1,
                    nextWakeAtMs: 456,
                },
            });
            const runsAfterWakeCall = harness.takeNextCall("cron.runs");
            runsAfterWakeCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await wakePending;

            const removePending = controller.removeCronJob("cron-main");
            const removeCall = harness.takeNextCall("cron.remove");
            assertRegression(removeCall.params && removeCall.params.id === "cron-main" && removeCall.params.jobId === "cron-main",
                "cron remove should call cron.remove with selected id/jobId aliases");
            removeCall.deferred.resolve({
                payload: {
                    removed: true,
                },
            });
            const listAfterRemoveCall = harness.takeNextCall("cron.list");
            listAfterRemoveCall.deferred.resolve({
                payload: {
                    jobs: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            const statusAfterRemoveCall = harness.takeNextCall("cron.status");
            statusAfterRemoveCall.deferred.resolve({
                payload: {
                    enabled: true,
                    jobs: 0,
                    nextWakeAtMs: null,
                },
            });
            const runsAfterRemoveCall = harness.takeNextCall("cron.runs");
            runsAfterRemoveCall.deferred.resolve({
                payload: {
                    entries: [],
                    total: 0,
                    offset: 0,
                    limit: 20,
                    hasMore: false,
                    nextOffset: null,
                },
            });
            await removePending;

            assertRegression(state.agentCronSelectedJobId === null,
                "cron remove should clear selected job when removed");
            summary.push("cron mutation flow + wake action baseline");
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
