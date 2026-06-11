(function () {
    function createSkillsController(options) {
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
            throw new Error("skills-controller requires state and request");
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

        function normalizeSkillsSearchPayload(payload) {
            const source = payload && typeof payload === "object" ? payload : {};
            const entries = Array.isArray(source.skills)
                ? source.skills
                : [];
            return entries.map(function (entry, index) {
                const row = entry && typeof entry === "object" ? entry : {};
                const skill = String(row.skill || row.skillKey || row.name || "skill-" + String(index + 1)).trim();
                return {
                    skill: skill || "skill-" + String(index + 1),
                    name: String(row.name || row.skill || row.skillKey || skill || "").trim(),
                    description: String(row.description || row.summary || "").trim(),
                };
            });
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

        async function searchSkillsHub(options) {
            const localOptions = options || {};
            const query = String(localOptions.query != null ? localOptions.query : state.skillsHubQuery).trim();
            const requestOverride = typeof localOptions.requestOverride === "function"
                ? localOptions.requestOverride
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
            const localOptions = options || {};
            const requestOverride = typeof localOptions.requestOverride === "function"
                ? localOptions.requestOverride
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
            const localOptions = options || {};
            const requestOverride = typeof localOptions.requestOverride === "function"
                ? localOptions.requestOverride
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
            const localOptions = options || {};
            const requestOverride = typeof localOptions.requestOverride === "function"
                ? localOptions.requestOverride
                : request;
            const rawPayload = String(localOptions.payload != null ? localOptions.payload : state.skillsEditPayload || "").trim();
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
                state.skillsEditError = "Invalid JSON payload: " + String(parseErr.message || parseErr);
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

        return {
            isMethodNotFoundError,
            normalizeAgentSkillsReportPayload,
            normalizeSkillsSearchPayload,
            loadAgentSkills,
            searchSkillsHub,
            loadSkillDetail,
            installSkill,
            updateSkillConfig,
        };
    }

    window.BlazeClawSkillsController = {
        createSkillsController,
    };
})();
