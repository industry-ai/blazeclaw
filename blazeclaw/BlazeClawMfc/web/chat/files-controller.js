(function () {
    function createFilesController(options) {
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
            throw new Error("files-controller requires state and request");
        }

        function buildAgentFileContentRequestKey(params) {
            const resolvedAgentId = String(params && params.agentId || "").trim() || "main";
            const resolvedPath = String(params && params.path || "").trim();
            return resolvedAgentId + ":path=" + resolvedPath;
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
                state.agentFileSelectedPath =
                    String(savedFile && (savedFile.path || savedFile.name) || resolvedPath).trim() ||
                    resolvedPath;
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

        return {
            buildAgentFileContentRequestKey,
            loadAgentFiles,
            loadAgentFileContent,
            updateAgentFileDraft,
            selectAgentFile,
            saveAgentFileContent,
        };
    }

    window.BlazeClawFilesController = {
        createFilesController,
    };
})();
