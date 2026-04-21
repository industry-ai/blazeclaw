(function () {
    function normalizeDetailCode(err) {
        if (!err || typeof err !== "object") {
            return "";
        }

        const candidate = err;
        if (typeof candidate.detailCode === "string") {
            return candidate.detailCode.trim().toUpperCase();
        }

        if (candidate.details && typeof candidate.details === "object") {
            const details = candidate.details;
            if (typeof details.detailCode === "string") {
                return details.detailCode.trim().toUpperCase();
            }
            if (typeof details.code === "string") {
                return details.code.trim().toUpperCase();
            }
        }

        return "";
    }

    function normalizeErrorMessage(err) {
        if (typeof err === "string") {
            return err;
        }

        if (err && typeof err === "object" && typeof err.message === "string") {
            return err.message;
        }

        return String(err || "");
    }

    function isMissingOperatorReadScopeError(err) {
        const detailCode = normalizeDetailCode(err);
        if (detailCode === "AUTH_UNAUTHORIZED") {
            return true;
        }

        const message = normalizeErrorMessage(err).toLowerCase();
        return message.includes("missing scope: operator.read");
    }

    function formatMissingOperatorReadScopeMessage(feature) {
        const normalizedFeature = String(feature || "this feature").trim() || "this feature";
        return "This connection is missing operator.read, so " + normalizedFeature + " cannot be loaded yet.";
    }

    window.BlazeClawScopeErrors = {
        isMissingOperatorReadScopeError,
        formatMissingOperatorReadScopeMessage,
    };
})();
