(function () {
    const supportedPanels = [
        "overview",
        "tools",
        "files",
        "skills",
        "channels",
        "cron",
        "dreaming",
        "nodes",
        "instances",
        "usage",
        "observability",
        "devices",
    ];

    function normalizePanelId(value) {
        const normalized = String(value || "").trim().toLowerCase();
        if (supportedPanels.indexOf(normalized) >= 0) {
            return normalized;
        }
        return "";
    }

    function isDashboardHost() {
        if (window.__BLAZECLAW_DASHBOARD_HOST__ === true) {
            return true;
        }

        const search = new URLSearchParams(window.location.search || "");
        if (search.get("host") === "dashboard") {
            return true;
        }

        return /dashboard(?:_[a-z0-9_-]+)?\.html(?:$|[?#])/i
            .test(String(window.location.pathname || ""));
    }

    function resolvePanelHint() {
        if (!isDashboardHost()) {
            return "";
        }

        const explicitPanel = normalizePanelId(window.__BLAZECLAW_DASHBOARD_PANEL__);
        if (explicitPanel) {
            return explicitPanel;
        }

        const search = new URLSearchParams(window.location.search || "");
        return normalizePanelId(search.get("panel"));
    }

    function resolveFixedPanelId(panelHint) {
        if (!panelHint || !isDashboardHost()) {
            return "";
        }

        const search = new URLSearchParams(window.location.search || "");
        const fixedPanelQuery = String(search.get("fixedPanel") || "")
            .trim()
            .toLowerCase();
        const fixedPanelGlobal = window.__BLAZECLAW_DASHBOARD_PANEL_FIXED__;

        if (fixedPanelGlobal === false ||
            fixedPanelQuery === "0" ||
            fixedPanelQuery === "false") {
            return "";
        }

        return normalizePanelId(panelHint);
    }

    function applyHostLayout(state) {
        if (!isDashboardHost()) {
            return;
        }

        if (document.body) {
            document.body.classList.add("blazeclaw-dashboard-host");
        }

        const controlPlaneEl = state && state.agentsControlPlaneEl
            ? state.agentsControlPlaneEl
            : null;
        if (controlPlaneEl) {
            controlPlaneEl.hidden = false;
        }
    }

    function isPageVisible() {
        if (typeof document.hidden === "boolean") {
            return !document.hidden;
        }
        return true;
    }

    function runSmokeChecks(options) {
        const opts = options || {};
        if (!isDashboardHost()) {
            return {
                ok: true,
                checks: ["not-dashboard-host"],
            };
        }

        const checks = [];
        const panelHint = resolvePanelHint();
        const fixedPanelId = resolveFixedPanelId(panelHint);
        const expectedPanel = normalizePanelId(opts.expectedPanel || panelHint);
        const activePanel = normalizePanelId(opts.activePanel);

        if (expectedPanel) {
            checks.push("expected-panel-present");
            if (fixedPanelId === expectedPanel) {
                checks.push("fixed-panel-match");
            }
            if (!activePanel || activePanel === expectedPanel) {
                checks.push("active-panel-match");
            }
        }

        const hasControlPlane = Boolean(opts.hasControlPlane);
        if (hasControlPlane) {
            checks.push("control-plane-present");
        }

        return {
            ok: checks.length > 0,
            checks,
            panelHint,
            fixedPanelId,
        };
    }

    window.BlazeClawDashboardHostBootstrap = {
        normalizePanelId,
        isDashboardHost,
        resolvePanelHint,
        resolveFixedPanelId,
        applyHostLayout,
        isPageVisible,
        runSmokeChecks,
    };
})();
