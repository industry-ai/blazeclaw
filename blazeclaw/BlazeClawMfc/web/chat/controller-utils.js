(function () {
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
            return Number.isFinite(Number(fallback))
                ? Math.max(0, Math.floor(Number(fallback)))
                : 0;
        }
        return Math.max(0, Math.floor(parsed));
    }

    function normalizeFiniteScore(value, fallback) {
        const parsed = Number(value);
        if (!Number.isFinite(parsed)) {
            return Number.isFinite(Number(fallback))
                ? Math.max(0, Math.min(1, Number(fallback)))
                : 0;
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

    window.BlazeClawControllerUtils = {
        normalizeBooleanFlag,
        asRecord,
        normalizeTrimmedString,
        normalizeFiniteInt,
        normalizeFiniteScore,
        normalizeStorageMode,
    };
})();
