(function () {
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

    function getUtils() {
        return window.BlazeClawControllerUtils || {};
    }

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
        const utils = getUtils();
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };

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
        const utils = getUtils();
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };

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
        const utils = getUtils();
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };

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
        const utils = getUtils();
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };

        const safePath = String(path || "").trim() || "(unknown)";
        const start = Math.max(1, normalizeFiniteInt(startLine, 1));
        const end = Math.max(1, normalizeFiniteInt(endLine, start));
        return start === end
            ? safePath + ":" + String(start)
            : safePath + ":" + String(start) + "-" + String(end);
    }

    function currentDreamPhrase(state) {
        const utils = getUtils();
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };

        const now = Date.now();
        if (now - Number(state.dreamingPhraseLastSwapMs || 0) > DREAM_SWAP_MS) {
            state.dreamingPhraseLastSwapMs = now;
            state.dreamingPhraseIndex =
                (normalizeFiniteInt(state.dreamingPhraseIndex, 0) + 1) % DREAM_PHRASES.length;
        }
        const index = normalizeFiniteInt(state.dreamingPhraseIndex, 0) % DREAM_PHRASES.length;
        return DREAM_PHRASES[index] || DREAM_PHRASES[0];
    }

    function normalizeDreamingEntry(raw) {
        const utils = getUtils();
        const asRecord = utils.asRecord || function (value) {
            return value && typeof value === "object" && !Array.isArray(value) ? value : null;
        };
        const normalizeTrimmedString = utils.normalizeTrimmedString || function (value) {
            if (typeof value !== "string") {
                return undefined;
            }
            const trimmed = value.trim();
            return trimmed.length > 0 ? trimmed : undefined;
        };
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };

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

    function normalizeDreamingStatus(raw) {
        const utils = getUtils();
        const asRecord = utils.asRecord || function (value) {
            return value && typeof value === "object" && !Array.isArray(value) ? value : null;
        };
        const normalizeBooleanFlag = utils.normalizeBooleanFlag || function (value) {
            return value === true;
        };
        const normalizeTrimmedString = utils.normalizeTrimmedString || function (value) {
            if (typeof value !== "string") {
                return undefined;
            }
            const trimmed = value.trim();
            return trimmed.length > 0 ? trimmed : undefined;
        };
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };
        const normalizeFiniteScore = utils.normalizeFiniteScore || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.min(1, parsed)) : 0;
        };
        const normalizeStorageMode = utils.normalizeStorageMode || function () {
            return "inline";
        };

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
        const utils = getUtils();
        const asRecord = utils.asRecord || function (value) {
            return value && typeof value === "object" && !Array.isArray(value) ? value : null;
        };
        const normalizeTrimmedString = utils.normalizeTrimmedString || function (value) {
            if (typeof value !== "string") {
                return undefined;
            }
            const trimmed = value.trim();
            return trimmed.length > 0 ? trimmed : undefined;
        };

        const plugins = asRecord(configValue && configValue.plugins);
        const slots = asRecord(plugins && plugins.slots);
        const configuredSlot = normalizeTrimmedString(slots && slots.memory);
        if (configuredSlot && configuredSlot.toLowerCase() !== "none") {
            return configuredSlot;
        }
        return "memory-core";
    }

    function resolveConfiguredDreaming(configValue) {
        const utils = getUtils();
        const asRecord = utils.asRecord || function (value) {
            return value && typeof value === "object" && !Array.isArray(value) ? value : null;
        };
        const normalizeBooleanFlag = utils.normalizeBooleanFlag || function (value) {
            return value === true;
        };

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
        const utils = getUtils();
        const asRecord = utils.asRecord || function (entry) {
            return entry && typeof entry === "object" && !Array.isArray(entry) ? entry : null;
        };
        const normalizeTrimmedString = utils.normalizeTrimmedString || function (entry) {
            if (typeof entry !== "string") {
                return undefined;
            }
            const trimmed = entry.trim();
            return trimmed.length > 0 ? trimmed : undefined;
        };

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
        const utils = getUtils();
        const asRecord = utils.asRecord || function (entry) {
            return entry && typeof entry === "object" && !Array.isArray(entry) ? entry : null;
        };

        const lookup = asRecord(value);
        const schema = asRecord(lookup && lookup.schema);
        return schema && schema.additionalProperties === false;
    }

    function getDreamingUiModel(state) {
        const utils = getUtils();
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };

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
            phrase: currentDreamPhrase(state),
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
            flattenDiaryBody: flattenDreamDiaryBody,
            describeWaitingEntryOrigin,
            formatRange,
            formatCompactDateTime,
        };
    }

    function setDreamingSubTab(state, tab, onStateUpdated) {
        const normalized = String(tab || "").trim();
        if (normalized !== "scene" && normalized !== "diary" && normalized !== "advanced") {
            return;
        }
        state.dreamingUiSubTab = normalized;
        onStateUpdated();
    }

    function setDreamingAdvancedWaitingSort(state, sort, onStateUpdated) {
        const normalized = String(sort || "").trim();
        if (normalized !== "recent" && normalized !== "signals") {
            return;
        }
        state.dreamingAdvancedWaitingSort = normalized;
        onStateUpdated();
    }

    function setDreamDiaryPage(state, page, onStateUpdated) {
        const utils = getUtils();
        const normalizeFiniteInt = utils.normalizeFiniteInt || function (value) {
            const parsed = Number(value);
            return Number.isFinite(parsed) ? Math.max(0, Math.floor(parsed)) : 0;
        };

        const nav = Array.isArray(state.dreamDiaryNavigation)
            ? state.dreamDiaryNavigation
            : [];
        const maxPage = Math.max(0, nav.length - 1);
        state.dreamDiaryPage = Math.max(0, Math.min(normalizeFiniteInt(page, 0), maxPage));
        onStateUpdated();
    }

    window.BlazeClawDreamingController = {
        normalizeDreamingStatus,
        resolveDreamingPluginId,
        resolveConfiguredDreaming,
        lookupIncludesDreamingProperty,
        lookupDisallowsUnknownProperties,
        getDreamingUiModel,
        setDreamingSubTab,
        setDreamingAdvancedWaitingSort,
        setDreamDiaryPage,
    };
})();
