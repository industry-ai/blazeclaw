(function () {
    function normalizeDetailCode(err) {
        if (!err || typeof err !== "object") {
            return "";
        }

        if (typeof err.detailCode === "string") {
            return err.detailCode.trim().toUpperCase();
        }

        if (err.details && typeof err.details === "object") {
            const details = err.details;
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
        if (normalizeDetailCode(err) === "AUTH_UNAUTHORIZED") {
            return true;
        }

        return normalizeErrorMessage(err)
            .toLowerCase()
            .includes("missing scope: operator.read");
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
