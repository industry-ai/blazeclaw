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

        function normalizeBooleanFlag(value, fallback) {
            if (typeof value === "boolean") {
                return value;
            }
            if (typeof fallback === "boolean") {
                return fallback;
            }
            return false;
        }

        function asRecord(value) {
            if (!value || typeof value !== "object" || Array.isArray(value)) {
                return null;
            }
            return value;
        }

        function normalizeTrimmedString(value) {
            if (typeof value !== "string") {
                return undefined;
            }
            const trimmed = value.trim();
            return trimmed.length > 0 ? trimmed : undefined;
        }

        function normalizeFiniteInt(value, fallback) {
            const parsed = Number(value);
            if (!Number.isFinite(parsed)) {
                return Number.isFinite(Number(fallback)) ? Math.max(0, Math.floor(Number(fallback))) : 0;
            }
            return Math.max(0, Math.floor(parsed));
        }

        function normalizeFiniteScore(value, fallback) {
            const parsed = Number(value);
            if (!Number.isFinite(parsed)) {
                return Number.isFinite(Number(fallback)) ? Math.max(0, Math.min(1, Number(fallback))) : 0;
            }
            return Math.max(0, Math.min(1, parsed));
        }

        function normalizeStorageMode(value) {
            const normalized = normalizeTrimmedString(value);
            if (!normalized) {
                return "inline";
            }
            const lowered = normalized.toLowerCase();
            if (lowered === "inline" || lowered === "separate" || lowered === "both") {
                return lowered;
            }
            return "inline";
        }

        function normalizeDreamingEntry(raw) {
            const record = asRecord(raw);
            const key = normalizeTrimmedString(record && record.key);
            const path = normalizeTrimmedString(record && record.path);
            const snippet = normalizeTrimmedString(record && record.snippet);
            if (!key || !path || !snippet) {
                return null;
            }

            const promotedAt = normalizeTrimmedString(record && record.promotedAt);
            const lastRecalledAt = normalizeTrimmedString(record && record.lastRecalledAt);
            const normalized = {
                key,
                path,
                startLine: Math.max(1, normalizeFiniteInt(record && record.startLine, 1)),
                endLine: Math.max(1, normalizeFiniteInt(record && record.endLine, 1)),
                snippet,
                recallCount: normalizeFiniteInt(record && record.recallCount, 0),
                dailyCount: normalizeFiniteInt(record && record.dailyCount, 0),
                groundedCount: normalizeFiniteInt(record && record.groundedCount, 0),
                totalSignalCount: normalizeFiniteInt(record && record.totalSignalCount, 0),
                lightHits: normalizeFiniteInt(record && record.lightHits, 0),
                remHits: normalizeFiniteInt(record && record.remHits, 0),
                phaseHitCount: normalizeFiniteInt(record && record.phaseHitCount, 0),
            };
            if (promotedAt) {
                normalized.promotedAt = promotedAt;
            }
            if (lastRecalledAt) {
                normalized.lastRecalledAt = lastRecalledAt;
            }
            return normalized;
        }

        function normalizeDreamingEntries(raw) {
            if (!Array.isArray(raw)) {
                return [];
            }
            return raw.map(normalizeDreamingEntry).filter(function (entry) {
                return Boolean(entry);
            });
        }

        const DREAM_PHRASES = [
            "Consolidating memories...",
            "Tidying the knowledge graph...",
            "Replaying conversations...",
            "Weaving short-term signals...",
            "Defragmenting the mind palace...",
            "Filing loose thoughts...",
            "Connecting distant dots...",
            "Composting stale context...",
            "Promoting durable insights...",
            "Forgetting noisy traces...",
        ];
        const DREAM_SWAP_MS = 6000;
        const DIARY_START_RE = /<!--\s*openclaw:dreaming:diary:start\s*-->/;
        const DIARY_END_RE = /<!--\s*openclaw:dreaming:diary:end\s*-->/;

        function normalizeTimestampMs(value) {
            const parsed = Number(value);
            return Number.isFinite(parsed)
                ? Math.floor(parsed)
                : Number.NEGATIVE_INFINITY;
        }

        function parseDiaryEntries(raw) {
            const text = typeof raw === "string" ? raw : "";
            if (!text.trim()) {
                return [];
            }

            let content = text;
            const startMatch = DIARY_START_RE.exec(text);
            const endMatch = DIARY_END_RE.exec(text);
            if (startMatch && endMatch && endMatch.index > startMatch.index) {
                content = text.slice(startMatch.index + startMatch[0].length, endMatch.index);
            }

            return content
                .split(/\n---\n/)
                .map(function (block) {
                    const lines = String(block || "")
                        .trim()
                        .split("\n");
                    let date = "";
                    const bodyLines = [];
                    lines.forEach(function (line) {
                        const trimmed = String(line || "").trim();
                        if (!trimmed) {
                            return;
                        }
                        if (!date &&
                            trimmed.startsWith("*") &&
                            trimmed.endsWith("*") &&
                            trimmed.length > 2) {
                            date = trimmed.slice(1, -1);
                            return;
                        }
                        if (trimmed.startsWith("#") || trimmed.startsWith("<!--")) {
                            return;
                        }
                        bodyLines.push(trimmed);
                    });

                    return bodyLines.length > 0
                        ? {
                            date,
                            body: bodyLines.join("\n"),
                        }
                        : null;
                })
                .filter(function (entry) {
                    return Boolean(entry);
                });
        }

        function parseDiaryTimestamp(date) {
            const parsed = Date.parse(String(date || ""));
            return Number.isFinite(parsed) ? parsed : null;
        }

        function formatDiaryChipLabel(date) {
            const parsed = parseDiaryTimestamp(date);
            if (parsed === null) {
                return String(date || "");
            }
            const value = new Date(parsed);
            return String(value.getMonth() + 1) + "/" + String(value.getDate());
        }

        function buildDreamDiaryNavigation(entries) {
            return entries.slice().reverse().map(function (entry, page) {
                return {
                    date: entry.date,
                    body: entry.body,
                    page,
                };
            });
        }

        function flattenDreamDiaryBody(body) {
            return String(body || "")
                .split("\n")
                .map(function (line) {
                    return String(line || "").trim();
                })
                .filter(function (line) {
                    return line.length > 0 &&
                        line !== "What Happened" &&
                        line !== "Reflections" &&
                        line !== "Candidates" &&
                        line !== "Possible Lasting Updates";
                })
                .map(function (line) {
                    return line.replace(/\s*\[memory\/[^[\]]+\]/g, "");
                })
                .map(function (line) {
                    return line
                        .replace(/^(?:\d+\.\s+|-\s+(?:\[[^\]]+\]\s+)?(?:[a-z_]+:\s+)?)/i, "")
                        .replace(/^(?:likely_durable|likely_situational|unclear):\s+/i, "")
                        .trim();
                })
                .filter(function (line) {
                    return line.length > 0;
                });
        }

        function formatCompactDateTime(value) {
            const parsed = Date.parse(String(value || ""));
            if (!Number.isFinite(parsed)) {
                return String(value || "");
            }
            return new Date(parsed).toLocaleString([], {
                month: "short",
                day: "numeric",
                hour: "numeric",
                minute: "2-digit",
            });
        }

        function compareWaitingEntryByRecency(a, b) {
            const aMs = normalizeTimestampMs(a && a.lastRecalledAt);
            const bMs = normalizeTimestampMs(b && b.lastRecalledAt);
            if (bMs !== aMs) {
                return bMs - aMs;
            }
            const aSignals = normalizeFiniteInt(a && a.totalSignalCount, 0);
            const bSignals = normalizeFiniteInt(b && b.totalSignalCount, 0);
            if (bSignals !== aSignals) {
                return bSignals - aSignals;
            }
            return String(a && a.path || "").localeCompare(String(b && b.path || ""));
        }

        function compareWaitingEntryBySignals(a, b) {
            const aSignals = normalizeFiniteInt(a && a.totalSignalCount, 0);
            const bSignals = normalizeFiniteInt(b && b.totalSignalCount, 0);
            if (bSignals !== aSignals) {
                return bSignals - aSignals;
            }
            const aPhaseHits = normalizeFiniteInt(a && a.phaseHitCount, 0);
            const bPhaseHits = normalizeFiniteInt(b && b.phaseHitCount, 0);
            if (bPhaseHits !== aPhaseHits) {
                return bPhaseHits - aPhaseHits;
            }
            return compareWaitingEntryByRecency(a, b);
        }

        function sortWaitingEntries(entries, sortMode) {
            const source = Array.isArray(entries) ? entries.slice() : [];
            return source.sort(sortMode === "signals"
                ? compareWaitingEntryBySignals
                : compareWaitingEntryByRecency);
        }

        function describeWaitingEntryOrigin(entry) {
            const grounded = normalizeFiniteInt(entry && entry.groundedCount, 0) > 0;
            const hasLiveSupport = normalizeFiniteInt(entry && entry.recallCount, 0) > 0 ||
                normalizeFiniteInt(entry && entry.dailyCount, 0) > 0;
            if (grounded && hasLiveSupport) {
                return "Mixed";
            }
            if (grounded) {
                return "Daily log";
            }
            return "Live";
        }

        function formatRange(path, startLine, endLine) {
            const safePath = String(path || "").trim() || "(unknown)";
            const start = Math.max(1, normalizeFiniteInt(startLine, 1));
            const end = Math.max(1, normalizeFiniteInt(endLine, start));
            return start === end
                ? safePath + ":" + String(start)
                : safePath + ":" + String(start) + "-" + String(end);
        }

        function currentDreamPhrase() {
            const now = Date.now();
            if (now - Number(state.dreamingPhraseLastSwapMs || 0) > DREAM_SWAP_MS) {
                state.dreamingPhraseLastSwapMs = now;
                state.dreamingPhraseIndex =
                    (normalizeFiniteInt(state.dreamingPhraseIndex, 0) + 1) % DREAM_PHRASES.length;
            }
            const index = normalizeFiniteInt(state.dreamingPhraseIndex, 0) % DREAM_PHRASES.length;
            return DREAM_PHRASES[index] || DREAM_PHRASES[0];
        }

        function getDreamingUiModel() {
            const dreamingStatus = state.dreamingStatus && typeof state.dreamingStatus === "object"
                ? state.dreamingStatus
                : null;
            const dreamDiaryContent = typeof state.dreamDiaryContent === "string"
                ? state.dreamDiaryContent
                : "";
            const parsedEntries = parseDiaryEntries(dreamDiaryContent);
            const navigation = buildDreamDiaryNavigation(parsedEntries);
            const entryCount = navigation.length;
            const page = entryCount > 0
                ? Math.max(0, Math.min(normalizeFiniteInt(state.dreamDiaryPage, 0), entryCount - 1))
                : 0;
            state.dreamDiaryParsedEntries = parsedEntries;
            state.dreamDiaryNavigation = navigation;
            state.dreamDiaryPage = page;

            const waitingSort = state.dreamingAdvancedWaitingSort === "signals"
                ? "signals"
                : "recent";
            const shortTermEntries = dreamingStatus && Array.isArray(dreamingStatus.shortTermEntries)
                ? dreamingStatus.shortTermEntries
                : [];
            const promotedEntries = dreamingStatus && Array.isArray(dreamingStatus.promotedEntries)
                ? dreamingStatus.promotedEntries
                : [];
            const groundedEntries = shortTermEntries.filter(function (entry) {
                return normalizeFiniteInt(entry && entry.groundedCount, 0) > 0;
            });

            return {
                subTab: state.dreamingUiSubTab === "diary" || state.dreamingUiSubTab === "advanced"
                    ? state.dreamingUiSubTab
                    : "scene",
                waitingSort,
                phrase: currentDreamPhrase(),
                entryCount,
                diaryPage: page,
                diaryEntry: navigation[page] || null,
                shortTermEntries,
                groundedEntries,
                waitingEntries: sortWaitingEntries(shortTermEntries, waitingSort),
                promotedEntries,
                status: dreamingStatus,
                diaryPath: state.dreamDiaryPath,
                diaryContent: dreamDiaryContent,
                diaryChipLabel: formatDiaryChipLabel,
                flattenDiaryBody,
                describeWaitingEntryOrigin,
                formatRange,
                formatCompactDateTime,
            };
        }

        function setDreamingSubTab(tab) {
            const normalized = String(tab || "").trim();
            if (normalized !== "scene" && normalized !== "diary" && normalized !== "advanced") {
                return;
            }
            state.dreamingUiSubTab = normalized;
            onStateUpdated();
        }

        function setDreamingAdvancedWaitingSort(sort) {
            const normalized = String(sort || "").trim();
            if (normalized !== "recent" && normalized !== "signals") {
                return;
            }
            state.dreamingAdvancedWaitingSort = normalized;
            onStateUpdated();
        }

        function setDreamDiaryPage(page) {
            const nav = Array.isArray(state.dreamDiaryNavigation)
                ? state.dreamDiaryNavigation
                : [];
            const maxPage = Math.max(0, nav.length - 1);
            state.dreamDiaryPage = Math.max(0, Math.min(normalizeFiniteInt(page, 0), maxPage));
            onStateUpdated();
        }

        function normalizeDreamingStatus(raw) {
            const record = asRecord(raw);
            if (!record) {
                return null;
            }

            const phasesRecord = asRecord(record.phases);
            const lightRecord = asRecord(phasesRecord && phasesRecord.light);
            const deepRecord = asRecord(phasesRecord && phasesRecord.deep);
            const remRecord = asRecord(phasesRecord && phasesRecord.rem);
            const normalized = {
                enabled: normalizeBooleanFlag(record.enabled, false),
                verboseLogging: normalizeBooleanFlag(record.verboseLogging, false),
                storageMode: normalizeStorageMode(record.storageMode),
                separateReports: normalizeBooleanFlag(record.separateReports, false),
                shortTermCount: normalizeFiniteInt(record.shortTermCount, 0),
                recallSignalCount: normalizeFiniteInt(record.recallSignalCount, 0),
                dailySignalCount: normalizeFiniteInt(record.dailySignalCount, 0),
                groundedSignalCount: normalizeFiniteInt(record.groundedSignalCount, 0),
                totalSignalCount: normalizeFiniteInt(record.totalSignalCount, 0),
                phaseSignalCount: normalizeFiniteInt(record.phaseSignalCount, 0),
                lightPhaseHitCount: normalizeFiniteInt(record.lightPhaseHitCount, 0),
                remPhaseHitCount: normalizeFiniteInt(record.remPhaseHitCount, 0),
                promotedTotal: normalizeFiniteInt(record.promotedTotal, 0),
                promotedToday: normalizeFiniteInt(record.promotedToday, 0),
                shortTermEntries: normalizeDreamingEntries(record.shortTermEntries),
                signalEntries: normalizeDreamingEntries(record.signalEntries),
                promotedEntries: normalizeDreamingEntries(record.promotedEntries),
            };

            const timezone = normalizeTrimmedString(record.timezone);
            const storePath = normalizeTrimmedString(record.storePath);
            const phaseSignalPath = normalizeTrimmedString(record.phaseSignalPath);
            const storeError = normalizeTrimmedString(record.storeError);
            const phaseSignalError = normalizeTrimmedString(record.phaseSignalError);
            if (timezone) {
                normalized.timezone = timezone;
            }
            if (storePath) {
                normalized.storePath = storePath;
            }
            if (phaseSignalPath) {
                normalized.phaseSignalPath = phaseSignalPath;
            }
            if (storeError) {
                normalized.storeError = storeError;
            }
            if (phaseSignalError) {
                normalized.phaseSignalError = phaseSignalError;
            }

            if (lightRecord && deepRecord && remRecord) {
                const phases = {
                    light: {
                        enabled: normalizeBooleanFlag(lightRecord.enabled, false),
                        cron: normalizeTrimmedString(lightRecord.cron) || "",
                        managedCronPresent: normalizeBooleanFlag(lightRecord.managedCronPresent, false),
                        lookbackDays: normalizeFiniteInt(lightRecord.lookbackDays, 0),
                        limit: normalizeFiniteInt(lightRecord.limit, 0),
                    },
                    deep: {
                        enabled: normalizeBooleanFlag(deepRecord.enabled, false),
                        cron: normalizeTrimmedString(deepRecord.cron) || "",
                        managedCronPresent: normalizeBooleanFlag(deepRecord.managedCronPresent, false),
                        limit: normalizeFiniteInt(deepRecord.limit, 0),
                        minScore: normalizeFiniteScore(deepRecord.minScore, 0),
                        minRecallCount: normalizeFiniteInt(deepRecord.minRecallCount, 0),
                        minUniqueQueries: normalizeFiniteInt(deepRecord.minUniqueQueries, 0),
                        recencyHalfLifeDays: normalizeFiniteInt(deepRecord.recencyHalfLifeDays, 0),
                    },
                    rem: {
                        enabled: normalizeBooleanFlag(remRecord.enabled, false),
                        cron: normalizeTrimmedString(remRecord.cron) || "",
                        managedCronPresent: normalizeBooleanFlag(remRecord.managedCronPresent, false),
                        lookbackDays: normalizeFiniteInt(remRecord.lookbackDays, 0),
                        limit: normalizeFiniteInt(remRecord.limit, 0),
                        minPatternStrength: normalizeFiniteScore(remRecord.minPatternStrength, 0),
                    },
                };
                if (Number.isFinite(Number(lightRecord.nextRunAtMs))) {
                    phases.light.nextRunAtMs = Math.floor(Number(lightRecord.nextRunAtMs));
                }
                if (Number.isFinite(Number(deepRecord.nextRunAtMs))) {
                    phases.deep.nextRunAtMs = Math.floor(Number(deepRecord.nextRunAtMs));
                }
                if (Number.isFinite(Number(remRecord.nextRunAtMs))) {
                    phases.rem.nextRunAtMs = Math.floor(Number(remRecord.nextRunAtMs));
                }
                if (Number.isFinite(Number(deepRecord.maxAgeDays))) {
                    phases.deep.maxAgeDays = normalizeFiniteInt(deepRecord.maxAgeDays, 0);
                }
                normalized.phases = phases;
            }

            return normalized;
        }

        function resolveDreamingPluginId(configValue) {
            const plugins = asRecord(configValue && configValue.plugins);
            const slots = asRecord(plugins && plugins.slots);
            const configuredSlot = normalizeTrimmedString(slots && slots.memory);
            if (configuredSlot && configuredSlot.toLowerCase() !== "none") {
                return configuredSlot;
            }
            return "memory-core";
        }

        function resolveConfiguredDreaming(configValue) {
            const pluginId = resolveDreamingPluginId(configValue);
            const plugins = asRecord(configValue && configValue.plugins);
            const entries = asRecord(plugins && plugins.entries);
            const pluginEntry = asRecord(entries && entries[pluginId]);
            const config = asRecord(pluginEntry && pluginEntry.config);
            const dreaming = asRecord(config && config.dreaming);
            return {
                pluginId,
                enabled: normalizeBooleanFlag(dreaming && dreaming.enabled, false),
            };
        }

        function lookupIncludesDreamingProperty(value) {
            const lookup = asRecord(value);
            const children = Array.isArray(lookup && lookup.children) ? lookup.children : [];
            for (let index = 0; index < children.length; index += 1) {
                const child = asRecord(children[index]);
                if (normalizeTrimmedString(child && child.key) === "dreaming") {
                    return true;
                }
            }
            return false;
        }

        function lookupDisallowsUnknownProperties(value) {
            const lookup = asRecord(value);
            const schema = asRecord(lookup && lookup.schema);
            return schema && schema.additionalProperties === false;
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

        const LEGACY_USAGE_DATE_PARAMS_MODE_RE = /unexpected property ['"]mode['"]/i;
        const LEGACY_USAGE_DATE_PARAMS_OFFSET_RE = /unexpected property ['"]utcoffset['"]/i;
        const LEGACY_USAGE_DATE_PARAMS_INVALID_RE = /invalid sessions\.usage params/i;

        function buildUsageDateBounds() {
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
        }

        function formatUtcOffset(timezoneOffsetMinutes) {
            const offsetFromUtcMinutes = -timezoneOffsetMinutes;
            const sign = offsetFromUtcMinutes >= 0 ? "+" : "-";
            const absMinutes = Math.abs(offsetFromUtcMinutes);
            const hours = Math.floor(absMinutes / 60);
            const minutes = absMinutes % 60;
            return minutes === 0
                ? "UTC" + sign + String(hours)
                : "UTC" + sign + String(hours) + ":" + String(minutes).padStart(2, "0");
        }

        function buildUsageDateInterpretationParams(timeZone) {
            if (timeZone === "utc") {
                return {
                    mode: "utc",
                };
            }
            return {
                mode: "specific",
                utcOffset: formatUtcOffset(new Date().getTimezoneOffset()),
            };
        }

        function isLegacyDateInterpretationUnsupportedError(err) {
            const message = String((err && err.message) || err || "");
            return LEGACY_USAGE_DATE_PARAMS_INVALID_RE.test(message) &&
                (LEGACY_USAGE_DATE_PARAMS_MODE_RE.test(message) || LEGACY_USAGE_DATE_PARAMS_OFFSET_RE.test(message));
        }

        function shouldIgnoreUsageDetailResponse(shouldIgnoreResponse, sessionKey) {
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
        }

        async function runOptionalUsageDetailRequest(loadingKey, run, shouldIgnoreResponse) {
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
        }

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
                const file = state.agentFileContentResult && state.agentFileContentResult.file &&
                    typeof state.agentFileContentResult.file === "object"
                    ? state.agentFileContentResult.file
                    : null;
                const resolvedFilePath = String(file && (file.path || file.name) || resolvedPath).trim();
                const resolvedContent = String(file && file.content || "");
                state.agentFileSelectedPath = resolvedFilePath || resolvedPath;
                state.agentFileEditDraft = resolvedContent;
                state.agentFileEditBaseContent = resolvedContent;
                state.agentFileSaveError = null;
                state.agentFileSaveStatus = null;
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

        function updateAgentFileDraft(content) {
            state.agentFileEditDraft = String(content || "");
            state.agentFileSaveError = null;
            state.agentFileSaveStatus = null;
            onStateUpdated();
        }

        async function selectAgentFile(params) {
            const resolvedAgentId = String(params && params.agentId || state.agentsSelectedId || "").trim();
            const resolvedPath = String(params && params.path || "").trim();
            if (!resolvedAgentId || !resolvedPath) {
                return;
            }
            await loadAgentFileContent({
                agentId: resolvedAgentId,
                path: resolvedPath,
            });
        }

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

        function isMethodNotFoundError(err) {
            if (!err) {
                return false;
            }

            if (typeof err === "object") {
                const candidate = err;
                const code = String(candidate.code || candidate.detailCode || "").toLowerCase();
                const message = String(candidate.message || "").toLowerCase();
                return code.indexOf("method") >= 0 && code.indexOf("not") >= 0 ||
                    message.indexOf("method_not_found") >= 0 ||
                    message.indexOf("unknown method") >= 0;
            }

            const text = String(err).toLowerCase();
            return text.indexOf("method_not_found") >= 0 || text.indexOf("unknown method") >= 0;
        }

        function normalizeAgentSkillsReportPayload(payload) {
            const candidate = payload && typeof payload === "object" ? payload : {};
            const skills = Array.isArray(candidate.skills)
                ? candidate.skills
                : [];

            const workspaceDir = String(candidate.workspaceDir || "").trim();
            const managedSkillsDir = String(candidate.managedSkillsDir || "").trim();

            return {
                workspaceDir,
                managedSkillsDir,
                skills,
            };
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
            onStateUpdated();

            try {
                let res = null;
                try {
                    res = await request("skills.status", {
                        agentId: resolvedAgentId,
                    });
                } catch (primaryError) {
                    if (!isMethodNotFoundError(primaryError)) {
                        throw primaryError;
                    }

                    res = await request("gateway.skills.status", {
                        agentId: resolvedAgentId,
                    });
                }

                if (shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : null;
                const report = normalizeAgentSkillsReportPayload(payload);
                const commandLikeEntries = Array.isArray(report.skills)
                    ? report.skills.map(function (entry) {
                        return {
                            name: String(entry && entry.name || "").trim(),
                            description: String(entry && entry.description || "").trim(),
                            skill: String(entry && entry.skillKey || entry && entry.name || "").trim(),
                        };
                    }).filter(function (entry) {
                        return entry.name.length > 0;
                    })
                    : [];

                state.agentSkillsReport = report;
                state.agentSkillsAgentId = resolvedAgentId;
                state.agentSkillsResult = {
                    commands: commandLikeEntries,
                    count: report.skills.length,
                    capability: "skills.status",
                    agentScoped: true,
                    report,
                };
                if (!state.skillsHubResults.length) {
                    state.skillsHubResults = report.skills.slice(0, 20).map(function (entry) {
                        const skillName = String(entry && (entry.skillKey || entry.name) || "").trim();
                        return {
                            skill: skillName,
                            name: String(entry && entry.name || skillName).trim(),
                            description: String(entry && entry.description || "").trim(),
                        };
                    }).filter(function (entry) {
                        return entry.skill.length > 0;
                    });
                }
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

        function normalizeSkillsSearchPayload(payload) {
            const source = payload && typeof payload === "object" ? payload : {};
            const entries = Array.isArray(source.skills)
                ? source.skills
                : [];
            return entries.map(function (entry, index) {
                const row = entry && typeof entry === "object" ? entry : {};
                const skill = String(row.skill || row.skillKey || row.name || `skill-${index + 1}`).trim();
                return {
                    skill: skill || `skill-${index + 1}`,
                    name: String(row.name || row.skill || row.skillKey || skill || "").trim(),
                    description: String(row.description || row.summary || "").trim(),
                };
            });
        }

        async function searchSkillsHub(options) {
            const opts = options || {};
            const query = String(opts.query != null ? opts.query : state.skillsHubQuery).trim();
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            if (!requestOverride || !state.connected || state.skillsHubLoading) {
                return state.skillsHubResults;
            }

            state.skillsHubLoading = true;
            state.skillsHubError = null;
            onStateUpdated();
            try {
                const res = await requestOverride("skills.search", {
                    query,
                });
                const payload = res && res.payload ? res.payload : res;
                state.skillsHubResults = normalizeSkillsSearchPayload(payload);
                state.skillsHubQuery = query;
                return state.skillsHubResults;
            } catch (err) {
                state.skillsHubError = resolveToolsErrorMessage(err, "skills search");
                state.lastError = state.skillsHubError;
                return state.skillsHubResults;
            } finally {
                state.skillsHubLoading = false;
                onStateUpdated();
            }
        }

        async function loadSkillDetail(skill, options) {
            const skillName = String(skill || "").trim();
            const opts = options || {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            if (!skillName || !requestOverride || !state.connected || state.skillsDetailLoading) {
                return state.skillsDetailResult;
            }

            state.skillsDetailLoading = true;
            state.skillsDetailError = null;
            onStateUpdated();
            try {
                let response;
                try {
                    response = await requestOverride("skills.detail", {
                        skill: skillName,
                    });
                } catch (detailErr) {
                    if (!isMethodNotFoundError(detailErr)) {
                        throw detailErr;
                    }
                    response = await requestOverride("gateway.skills.info", {
                        skill: skillName,
                    });
                }
                const payload = response && response.payload ? response.payload : response;
                state.skillsDetailResult = payload && typeof payload === "object"
                    ? payload
                    : null;
                return state.skillsDetailResult;
            } catch (err) {
                state.skillsDetailError = resolveToolsErrorMessage(err, "skill detail");
                state.lastError = state.skillsDetailError;
                return state.skillsDetailResult;
            } finally {
                state.skillsDetailLoading = false;
                onStateUpdated();
            }
        }

        async function installSkill(skill, options) {
            const skillName = String(skill || "").trim();
            const opts = options || {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            if (!skillName || !requestOverride || !state.connected || state.skillsInstallBusy) {
                return null;
            }

            state.skillsInstallBusy = true;
            state.skillsInstallStatus = null;
            state.skillsHubError = null;
            onStateUpdated();
            try {
                let response;
                try {
                    response = await requestOverride("gateway.skills.install.execute", {
                        skill: skillName,
                    });
                } catch (execErr) {
                    if (!isMethodNotFoundError(execErr)) {
                        throw execErr;
                    }
                    response = await requestOverride("skills.install", {
                        skill: skillName,
                    });
                }
                const payload = response && response.payload ? response.payload : response;
                state.skillsInstallStatus = JSON.stringify(payload || {}, null, 2);
                return payload;
            } catch (err) {
                state.skillsInstallStatus = null;
                state.skillsHubError = resolveToolsErrorMessage(err, "skills install");
                state.lastError = state.skillsHubError;
                return null;
            } finally {
                state.skillsInstallBusy = false;
                onStateUpdated();
            }
        }

        async function updateSkillConfig(options) {
            const opts = options || {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            const rawPayload = String(opts.payload != null ? opts.payload : state.skillsEditPayload || "").trim();
            if (!requestOverride || !state.connected || state.skillsEditBusy) {
                return null;
            }

            let parsedPayload = {};
            try {
                parsedPayload = rawPayload ? JSON.parse(rawPayload) : {};
                if (!parsedPayload || typeof parsedPayload !== "object" || Array.isArray(parsedPayload)) {
                    throw new Error("payload must be JSON object");
                }
            } catch (parseErr) {
                state.skillsEditError = `Invalid JSON payload: ${String(parseErr.message || parseErr)}`;
                state.skillsEditStatus = null;
                onStateUpdated();
                return null;
            }

            state.skillsEditBusy = true;
            state.skillsEditError = null;
            state.skillsEditStatus = null;
            onStateUpdated();
            try {
                const response = await requestOverride("skills.update", parsedPayload);
                const payload = response && response.payload ? response.payload : response;
                state.skillsEditStatus = JSON.stringify(payload || {}, null, 2);
                return payload;
            } catch (err) {
                state.skillsEditError = resolveToolsErrorMessage(err, "skills update");
                state.lastError = state.skillsEditError;
                return null;
            } finally {
                state.skillsEditBusy = false;
                onStateUpdated();
            }
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

        async function loadUsage(options) {
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
        }

        async function loadUsageTimeSeries(sessionKey, options) {
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
        }

        async function loadUsageSessionLogs(sessionKey, options) {
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
        }

        function parseObservabilityMethodParams(raw) {
            const text = String(raw || "").trim();
            if (!text) {
                return {};
            }
            const parsed = JSON.parse(text);
            if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) {
                throw new Error("method params must be a JSON object");
            }
            return parsed;
        }

        function normalizeObservabilityLogs(entries, level) {
            const list = Array.isArray(entries) ? entries : [];
            const targetLevel = String(level || "all").trim().toLowerCase();
            return list.filter((entry) => {
                if (!entry || typeof entry !== "object") {
                    return false;
                }
                if (targetLevel === "all") {
                    return true;
                }
                const entryLevel = String(entry.level || "").trim().toLowerCase();
                return entryLevel === targetLevel;
            });
        }

        async function loadObservability(options) {
            const opts = options || {};
            const quiet = Boolean(opts.quiet);
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.observabilityLoading || !state.observabilityEnabled) {
                return state.observabilityHealth;
            }

            state.observabilityLoading = true;
            if (!quiet) {
                state.observabilityError = null;
                state.lastError = null;
            }
            onStateUpdated();

            try {
                const logLimit = Math.max(1, Math.min(200, Number(state.observabilityLogLimit) || 50));
                const calls = await Promise.all([
                    request("gateway.health", {}),
                    request("gateway.health.details", {}),
                    request("gateway.transport.status", {}),
                    request("last-heartbeat", {}),
                    request("models.list", {}),
                    state.observabilityPaused
                        ? Promise.resolve({ payload: { entries: state.observabilityLogs || [] } })
                        : request("gateway.logs.tail", { limit: logLimit }),
                ]);

                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.observabilityHealth;
                }

                const healthPayload = calls[0] && calls[0].payload ? calls[0].payload : calls[0];
                const detailsPayload = calls[1] && calls[1].payload ? calls[1].payload : calls[1];
                const transportPayload = calls[2] && calls[2].payload ? calls[2].payload : calls[2];
                const heartbeatPayload = calls[3] && calls[3].payload ? calls[3].payload : calls[3];
                const modelsPayload = calls[4] && calls[4].payload ? calls[4].payload : calls[4];
                const logsPayload = calls[5] && calls[5].payload ? calls[5].payload : calls[5];
                const models = Array.isArray(modelsPayload && modelsPayload.models)
                    ? modelsPayload.models
                    : [];
                const entries = Array.isArray(logsPayload && logsPayload.entries)
                    ? logsPayload.entries
                    : [];

                state.observabilityHealth = healthPayload && typeof healthPayload === "object"
                    ? healthPayload
                    : null;
                state.observabilityHealthDetails = detailsPayload && typeof detailsPayload === "object"
                    ? detailsPayload
                    : null;
                state.observabilityTransportStatus = transportPayload && typeof transportPayload === "object"
                    ? transportPayload
                    : null;
                state.observabilityHeartbeat = heartbeatPayload && typeof heartbeatPayload === "object"
                    ? heartbeatPayload
                    : null;
                state.observabilityModels = models;
                state.observabilityLogs = normalizeObservabilityLogs(entries, state.observabilityLogLevel);
                state.observabilityLastUpdatedMs = Date.now();
                if (!quiet) {
                    state.observabilityError = null;
                }
                return state.observabilityHealth;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return state.observabilityHealth;
                }

                if (!quiet) {
                    state.observabilityError = resolveToolsErrorMessage(err, "observability");
                    state.lastError = state.observabilityError;
                }
                return state.observabilityHealth;
            } finally {
                state.observabilityLoading = false;
                onStateUpdated();
            }
        }

        function updateObservabilityField(field, value) {
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
        }

        async function invokeObservabilityMethod(options) {
            const opts = options || {};
            const requestOverride = typeof opts.requestOverride === "function"
                ? opts.requestOverride
                : request;
            if (!requestOverride || state.observabilityMethodBusy || !state.observabilityEnabled) {
                return null;
            }

            const method = String(state.observabilityMethod || "").trim();
            if (!method) {
                state.observabilityMethodError = "method is required";
                state.observabilityMethodResult = null;
                onStateUpdated();
                return null;
            }

            state.observabilityMethodBusy = true;
            state.observabilityMethodError = null;
            onStateUpdated();
            try {
                const params = parseObservabilityMethodParams(state.observabilityMethodParams);
                const response = await requestOverride(method, params);
                state.observabilityMethodResult = JSON.stringify(response, null, 2);
                state.observabilityMethodError = null;
                return response;
            } catch (err) {
                state.observabilityMethodResult = null;
                state.observabilityMethodError = resolveToolsErrorMessage(err, "debug method invoke");
                return null;
            } finally {
                state.observabilityMethodBusy = false;
                onStateUpdated();
            }
        }

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

        function exportObservabilityLogs() {
            state.observabilityExportText = buildObservabilityLogsExportText();
            onStateUpdated();
            return state.observabilityExportText;
        }

        function normalizeDevicePairs(payload) {
            const source = payload && typeof payload === "object" ? payload : {};
            const pairs = Array.isArray(source.pairs) ? source.pairs : [];
            return pairs.map((entry, index) => {
                const row = entry && typeof entry === "object" ? entry : {};
                const deviceId = String(
                    row.deviceId ||
                    row.id ||
                    row.requestId ||
                    `device-${index + 1}`
                ).trim();
                return {
                    deviceId: deviceId || `device-${index + 1}`,
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
                state.devicePairActionStatus = `${method} ok for ${targetDeviceId}`;
                if (action === "remove") {
                    state.devicePairs = (state.devicePairs || []).filter((entry) => String(entry && entry.deviceId || "") !== targetDeviceId);
                } else {
                    state.devicePairs = (state.devicePairs || []).map((entry) => {
                        if (String(entry && entry.deviceId || "") !== targetDeviceId) {
                            return entry;
                        }
                        const nextStatus = action === "approve" ? "approved" : "rejected";
                        return Object.assign({}, entry, { status: nextStatus, updatedAtMs: Date.now() });
                    });
                }
                return payload;
            } catch (err) {
                state.devicePairsError = resolveToolsErrorMessage(err, `device pair ${action}`);
                state.lastError = state.devicePairsError;
                return null;
            } finally {
                state.devicePairsBusy = false;
                onStateUpdated();
            }
        }

        async function loadChannels(options) {
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
        }

        async function waitWhatsAppLogin(options) {
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
        }

        async function logoutWhatsApp(options) {
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
        }

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

            return hasRecoverable
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

        async function loadCronModelSuggestions(options) {
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
                const models = payload && Array.isArray(payload.models)
                    ? payload.models
                    : [];
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
        }

        async function withCronBusy(run) {
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
        }

        async function addOrUpdateCronJob() {
            return withCronBusy(async function () {
                const normalizedForm = normalizeCronFormState(state.agentCronForm);
                state.agentCronForm = normalizedForm;
                const fieldErrors = validateCronForm(normalizedForm);
                state.agentCronFieldErrors = fieldErrors;
                if (hasCronFormErrors(fieldErrors)) {
                    return;
                }

                const payload = {
                    name: String(normalizedForm.name || "").trim(),
                    enabled: normalizedForm.enabled !== false,
                    schedule: buildCronSchedule(normalizedForm),
                    payload: buildCronPayload(normalizedForm),
                    delivery: buildCronDelivery(normalizedForm),
                };

                const failureAlert = buildCronFailureAlert(normalizedForm);
                if (failureAlert !== undefined) {
                    payload.failureAlert = failureAlert;
                }

                const normalizedPayload = applyCronToolParityToMutationPayload(payload);

                if (state.agentCronEditingJobId) {
                    await request("cron.update", {
                        id: state.agentCronEditingJobId,
                        patch: recoverCronFlatJobShape(normalizedPayload) || normalizedPayload,
                    });
                    state.agentCronEditingJobId = null;
                } else {
                    await request("cron.add", recoverCronFlatJobShape(normalizedPayload) || normalizedPayload);
                    resetCronFormToDefaults();
                }

                await loadCronJobsPage({ append: false });
                await loadCronStatus({});
                await loadCronRuns({ append: false });
            });
        }

        async function removeCronJob(jobId) {
            const resolvedJobId = String(jobId || "").trim();
            if (!resolvedJobId) {
                return false;
            }

            return withCronBusy(async function () {
                await request("cron.remove", {
                    id: resolvedJobId,
                });

                if (state.agentCronEditingJobId === resolvedJobId) {
                    resetCronFormToDefaults();
                }
                if (state.agentCronSelectedJobId === resolvedJobId) {
                    state.agentCronSelectedJobId = null;
                }

                await loadCronJobsPage({ append: false });
                await loadCronStatus({});
                await loadCronRuns({ append: false });
            });
        }

        async function runCronJobNow(jobId, mode) {
            const resolvedJobId = String(jobId || "").trim();
            if (!resolvedJobId) {
                return false;
            }

            const resolvedMode = mode === "due" ? "due" : "force";
            return withCronBusy(async function () {
                await request("cron.run", {
                    id: resolvedJobId,
                    mode: resolvedMode,
                });
                await loadCronRuns({ append: false });
            });
        }

        function startCronEdit(jobId) {
            const resolvedJobId = String(jobId || "").trim();
            const jobs = Array.isArray(state.agentCronJobs) ? state.agentCronJobs : [];
            const job = jobs.find(function (entry) {
                return String(entry && entry.id || "").trim() === resolvedJobId;
            });
            if (!job) {
                return false;
            }

            state.agentCronEditingJobId = resolvedJobId;
            state.agentCronSelectedJobId = resolvedJobId;
            state.agentCronForm = jobToForm(job, state.agentCronForm);
            state.agentCronFieldErrors = validateCronForm(state.agentCronForm);
            onStateUpdated();
            return true;
        }

        function startCronClone(jobId) {
            const resolvedJobId = String(jobId || "").trim();
            const jobs = Array.isArray(state.agentCronJobs) ? state.agentCronJobs : [];
            const job = jobs.find(function (entry) {
                return String(entry && entry.id || "").trim() === resolvedJobId;
            });
            if (!job) {
                return false;
            }

            const existingNames = new Set(
                jobs.map(function (entry) {
                    return normalizeLowercaseStringOrEmpty(entry && entry.name || "");
                })
            );
            const cloned = jobToForm(job, state.agentCronForm);
            cloned.name = buildCloneName(job.name, existingNames);
            state.agentCronEditingJobId = null;
            state.agentCronSelectedJobId = resolvedJobId;
            state.agentCronForm = cloned;
            state.agentCronFieldErrors = validateCronForm(state.agentCronForm);
            onStateUpdated();
            return true;
        }

        function cancelCronEdit() {
            resetCronFormToDefaults();
            onStateUpdated();
        }

        function updateCronFormField(field, value) {
            const allowedFields = {
                name: true,
                enabled: true,
                scheduleKind: true,
                scheduleAt: true,
                everyAmount: true,
                everyUnit: true,
                cronExpr: true,
                payloadKind: true,
                payloadText: true,
                payloadModel: true,
                payloadThinking: true,
                timeoutSeconds: true,
                deliveryMode: true,
                deliveryTo: true,
                failureAlertMode: true,
                failureAlertAfter: true,
                failureAlertCooldownSeconds: true,
            };
            const key = String(field || "").trim();
            if (!allowedFields[key]) {
                return;
            }

            const nextForm = Object.assign({}, state.agentCronForm);
            nextForm[key] = value;
            state.agentCronForm = normalizeCronFormState(nextForm);
            state.agentCronFieldErrors = validateCronForm(state.agentCronForm);
            onStateUpdated();
        }

        async function loadCronStatus(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            if (!request || !state.connected || state.agentCronStatusLoading) {
                return state.agentCronStatusResult;
            }

            state.agentCronStatusLoading = true;
            state.agentCronStatusError = null;
            onStateUpdated();

            try {
                const res = await request("cron.status", {});
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                const payload = res && res.payload ? res.payload : res;
                const normalizedStatus = payload && typeof payload === "object"
                    ? {
                        enabled: Boolean(payload.enabled),
                        jobs: Number.isFinite(Number(payload.jobs))
                            ? Math.max(0, Math.floor(Number(payload.jobs)))
                            : 0,
                        nextWakeAtMs: Number.isFinite(Number(payload.nextWakeAtMs))
                            ? Math.floor(Number(payload.nextWakeAtMs))
                            : null,
                    }
                    : {
                        enabled: false,
                        jobs: 0,
                        nextWakeAtMs: null,
                    };
                state.agentCronStatusResult = normalizedStatus;
                return normalizedStatus;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return null;
                }

                state.agentCronStatusError = resolveToolsErrorMessage(err, "cron status");
                return null;
            } finally {
                state.agentCronStatusLoading = false;
                onStateUpdated();
            }
        }

        async function loadCronJobsPage(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            const append = Boolean(opts.append);
            if (!request || !state.connected) {
                return;
            }
            if (!append && state.agentCronJobsLoading) {
                return;
            }
            if (append && state.agentCronJobsLoadingMore) {
                return;
            }
            if (append && !state.agentCronJobsHasMore) {
                return;
            }

            const offset = append
                ? Math.max(0, Number(state.agentCronJobsNextOffset || state.agentCronJobs.length || 0))
                : 0;
            if (append) {
                state.agentCronJobsLoadingMore = true;
            } else {
                state.agentCronJobsLoading = true;
            }
            state.agentCronJobsError = null;
            onStateUpdated();

            try {
                const res = await request("cron.list", {
                    includeDisabled: state.agentCronJobsEnabledFilter === "all",
                    enabled: state.agentCronJobsEnabledFilter,
                    limit: state.agentCronJobsLimit,
                    offset: offset,
                    query: String(state.agentCronJobsQuery || "").trim() || undefined,
                    sortBy: state.agentCronJobsSortBy,
                    sortDir: state.agentCronJobsSortDir,
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : res;
                const jobs = payload && Array.isArray(payload.jobs)
                    ? payload.jobs.map(normalizeCronJob).filter(function (job) {
                        return job && String(job.id || "").trim().length > 0;
                    })
                    : [];
                state.agentCronJobs = append
                    ? state.agentCronJobs.concat(jobs)
                    : jobs;

                const meta = normalizeCronPaginationMeta(
                    payload,
                    jobs.length,
                    state.agentCronJobsLimit,
                    offset
                );
                state.agentCronJobsTotal = Math.max(meta.total, state.agentCronJobs.length);
                state.agentCronJobsHasMore = meta.hasMore;
                state.agentCronJobsNextOffset = meta.nextOffset;
                if (state.agentCronEditingJobId && !state.agentCronJobs.some(function (job) {
                    return String(job.id || "").trim() === String(state.agentCronEditingJobId || "").trim();
                })) {
                    resetCronFormToDefaults();
                }
                if (state.agentCronSelectedJobId && !state.agentCronJobs.some(function (job) {
                    return String(job.id || "").trim() === String(state.agentCronSelectedJobId || "").trim();
                })) {
                    state.agentCronSelectedJobId = null;
                }
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                state.agentCronJobsError = resolveToolsErrorMessage(err, "cron jobs");
            } finally {
                if (append) {
                    state.agentCronJobsLoadingMore = false;
                } else {
                    state.agentCronJobsLoading = false;
                }
                onStateUpdated();
            }
        }

        async function loadCronRuns(options) {
            const opts = options || {};
            const shouldIgnoreResponse = typeof opts.shouldIgnoreResponse === "function"
                ? opts.shouldIgnoreResponse
                : null;
            const append = Boolean(opts.append);
            if (!request || !state.connected) {
                return;
            }
            if (!append && state.agentCronRunsLoading) {
                return;
            }
            if (append && state.agentCronRunsLoadingMore) {
                return;
            }
            if (append && !state.agentCronRunsHasMore) {
                return;
            }

            const offset = append
                ? Math.max(0, Number(state.agentCronRunsNextOffset || state.agentCronRuns.length || 0))
                : 0;
            if (append) {
                state.agentCronRunsLoadingMore = true;
            } else {
                state.agentCronRunsLoading = true;
            }
            state.agentCronRunsError = null;
            onStateUpdated();

            try {
                const requestedScope = state.agentCronRunsScope === "job"
                    ? "job"
                    : "all";
                const selectedJobId = String(state.agentCronSelectedJobId || "").trim();
                const scope = requestedScope === "job" && selectedJobId
                    ? "job"
                    : "all";
                const res = await request("cron.runs", {
                    scope: scope,
                    id: scope === "job" && selectedJobId ? selectedJobId : undefined,
                    limit: state.agentCronRunsLimit,
                    offset: offset,
                    status: state.agentCronRunsStatusFilter,
                    query: String(state.agentCronRunsQuery || "").trim() || undefined,
                    sortDir: state.agentCronRunsSortDir,
                });
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                const payload = res && res.payload ? res.payload : res;
                const entries = payload && Array.isArray(payload.entries)
                    ? payload.entries
                    : [];
                const scopeIsJob = scope === "job" && selectedJobId;
                state.agentCronRuns = append && (!scopeIsJob || selectedJobId)
                    ? state.agentCronRuns.concat(entries)
                    : entries;

                const meta = normalizeCronPaginationMeta(
                    payload,
                    entries.length,
                    state.agentCronRunsLimit,
                    offset
                );
                state.agentCronRunsTotal = Math.max(meta.total, state.agentCronRuns.length);
                state.agentCronRunsHasMore = meta.hasMore;
                state.agentCronRunsNextOffset = meta.nextOffset;
            } catch (err) {
                if (shouldIgnoreResponse && shouldIgnoreResponse()) {
                    return;
                }

                state.agentCronRunsError = resolveToolsErrorMessage(err, "cron runs");
            } finally {
                if (append) {
                    state.agentCronRunsLoadingMore = false;
                } else {
                    state.agentCronRunsLoading = false;
                }
                onStateUpdated();
            }
        }

        async function loadAgentCron(agentId) {
            const resolvedAgentId = String(agentId || "").trim();
            if (!resolvedAgentId || !request || !state.connected) {
                return;
            }

            function shouldIgnoreResponse() {
                return hasSelectedAgentMismatch(resolvedAgentId) || state.agentsPanel !== "cron";
            }

            state.agentCronLoading = true;
            state.agentCronError = null;
            onStateUpdated();

            try {
                const cronStatus = await loadCronStatus({
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                await loadCronJobsPage({
                    append: false,
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                if (!state.agentCronSelectedJobId && Array.isArray(state.agentCronJobs) && state.agentCronJobs.length > 0) {
                    const firstJob = state.agentCronJobs[0] || {};
                    state.agentCronSelectedJobId = String(firstJob.id || "").trim() || null;
                }

                await loadCronRuns({
                    append: false,
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                await loadCronModelSuggestions({
                    shouldIgnoreResponse: shouldIgnoreResponse,
                });
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentCronResult = {
                    status: cronStatus,
                    jobs: state.agentCronJobs,
                    jobsTotal: state.agentCronJobsTotal,
                    jobsHasMore: state.agentCronJobsHasMore,
                    runs: state.agentCronRuns,
                    runsTotal: state.agentCronRunsTotal,
                    runsHasMore: state.agentCronRunsHasMore,
                    modelSuggestions: state.agentCronModelSuggestions,
                    capability: state.agentCronCapability,
                };
            } catch (err) {
                if (shouldIgnoreResponse()) {
                    return;
                }

                state.agentCronError = resolveToolsErrorMessage(err, "cron surface");
            } finally {
                state.agentCronLoading = false;
                onStateUpdated();
            }
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

        function updateCronJobsFilter(patch) {
            const next = patch || {};
            if (typeof next.query === "string") {
                state.agentCronJobsQuery = next.query;
            }
            if (next.enabledFilter === "all" || next.enabledFilter === "enabled" || next.enabledFilter === "disabled") {
                state.agentCronJobsEnabledFilter = next.enabledFilter;
            }
            if (next.sortBy === "nextRunAtMs" || next.sortBy === "updatedAtMs" || next.sortBy === "name") {
                state.agentCronJobsSortBy = next.sortBy;
            }
            if (next.sortDir === "asc" || next.sortDir === "desc") {
                state.agentCronJobsSortDir = next.sortDir;
            }
            state.agentCronJobsNextOffset = null;
            state.agentCronJobsHasMore = false;
            onStateUpdated();
        }

        function updateCronRunsFilter(patch) {
            const next = patch || {};
            if (next.scope === "all" || next.scope === "job") {
                state.agentCronRunsScope = next.scope;
            }
            if (next.statusFilter === "all" || next.statusFilter === "ok" || next.statusFilter === "error" || next.statusFilter === "skipped") {
                state.agentCronRunsStatusFilter = next.statusFilter;
            }
            if (typeof next.query === "string") {
                state.agentCronRunsQuery = next.query;
            }
            if (next.sortDir === "asc" || next.sortDir === "desc") {
                state.agentCronRunsSortDir = next.sortDir;
            }
            if (Object.prototype.hasOwnProperty.call(next, "selectedJobId")) {
                state.agentCronSelectedJobId = next.selectedJobId ? String(next.selectedJobId).trim() : null;
            }
            state.agentCronRunsNextOffset = null;
            state.agentCronRunsHasMore = false;
            onStateUpdated();
        }

        async function loadMoreCronJobs() {
            await loadCronJobsPage({ append: true });
        }

        async function loadMoreCronRuns() {
            await loadCronRuns({ append: true });
        }

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
            addOrUpdateCronJob,
            removeCronJob,
            runCronJobNow,
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
            assertRegression(runCall.params && runCall.params.id === "cron-main" && runCall.params.mode === "due",
                "cron run-now should call cron.run with selected id and mode");
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

            const removePending = controller.removeCronJob("cron-main");
            const removeCall = harness.takeNextCall("cron.remove");
            assertRegression(removeCall.params && removeCall.params.id === "cron-main",
                "cron remove should call cron.remove with selected id");
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
            summary.push("cron mutation flow + validation baseline");
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
