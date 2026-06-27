(function (global) {
    "use strict";

    function isDashboardHost() {
        const bootstrap = global.BlazeClawDashboardHostBootstrap;
        if (bootstrap && typeof bootstrap.isDashboardHost === "function") {
            return bootstrap.isDashboardHost();
        }

        if (global.__BLAZECLAW_DASHBOARD_HOST__ === true) {
            return true;
        }

        const search = new URLSearchParams(global.location && global.location.search || "");
        if (search.get("host") === "dashboard") {
            return true;
        }

        return /dashboard(?:_[a-z0-9_-]+)?\.html(?:$|[?#])/i.test(
            String(global.location && global.location.pathname || "")
        );
    }

    function readConfigAgentsEnabled() {
        const runtime = global.__BLAZECLAW_RUNTIME_CONFIG__;
        if (!runtime || !runtime.agents || typeof runtime.agents !== "object") {
            return null;
        }

        const value = runtime.agents.enabled;
        if (value === true || value === false) {
            return value;
        }

        return null;
    }

    function readQueryAgentsEnabled() {
        const search = new URLSearchParams(global.location && global.location.search || "");
        const queryToggle = search.get("agents");
        if (queryToggle === "0") {
            return false;
        }
        if (queryToggle === "1") {
            return true;
        }

        return null;
    }

    function readLocalStorageAgentsEnabled() {
        try {
            if (!global.localStorage) {
                return null;
            }

            const storedToggle = global.localStorage.getItem("blazeclaw.agents.enabled");
            if (storedToggle === "0") {
                return false;
            }
            if (storedToggle === "1") {
                return true;
            }
        } catch (_) {
        }

        return null;
    }

    function resolveAgentsEnabled() {
        const trace = {
            dashboardHost: isDashboardHost(),
            config: readConfigAgentsEnabled(),
            query: readQueryAgentsEnabled(),
            localStorage: readLocalStorageAgentsEnabled(),
            resolved: false,
            source: "default",
        };

        if (trace.dashboardHost) {
            trace.resolved = true;
            trace.source = "dashboard-host";
            return trace;
        }

        if (trace.config === true || trace.config === false) {
            trace.resolved = trace.config;
            trace.source = "config";
            return trace;
        }

        if (trace.query === true || trace.query === false) {
            trace.resolved = trace.query;
            trace.source = "query";
            return trace;
        }

        if (trace.localStorage === true || trace.localStorage === false) {
            trace.resolved = trace.localStorage;
            trace.source = "localStorage";
            return trace;
        }

        trace.resolved = false;
        trace.source = "default";
        return trace;
    }

    function postAgentsToggleTraceToNative(trace, options) {
        const opts = options && typeof options === "object" ? options : {};
        const payload = {
            channel: "blazeclaw.agents.toggle.trace",
            level: String(opts.level || "info"),
            reason: String(opts.reason || ""),
            resolved: trace.resolved === true,
            source: String(trace.source || "unknown"),
            modulePresent: opts.modulePresent !== false,
        };

        if (trace.config === true || trace.config === false) {
            payload.config = trace.config;
        } else {
            payload.config = null;
        }

        try {
            if (global.chrome &&
                global.chrome.webview &&
                typeof global.chrome.webview.postMessage === "function") {
                global.chrome.webview.postMessage(payload);
            }
        } catch (_) {
        }
    }

    function emitAgentsToggleTrace(trace, options) {
        if (global.console && typeof global.console.info === "function") {
            global.console.info("[agents-toggle]", trace);
        } else if (global.console && typeof global.console.debug === "function") {
            global.console.debug("[agents-toggle]", trace);
        }

        postAgentsToggleTraceToNative(trace, options);
    }

    function emitMissingAgentsToggleModule() {
        const trace = {
            dashboardHost: isDashboardHost(),
            config: readConfigAgentsEnabled(),
            query: readQueryAgentsEnabled(),
            localStorage: readLocalStorageAgentsEnabled(),
            resolved: false,
            source: "missing-module",
        };
        emitAgentsToggleTrace(trace, {
            level: "warn",
            reason: "agents-toggle.js not loaded",
            modulePresent: false,
        });
        return trace;
    }

    function assertRegression(condition, message) {
        if (!condition) {
            throw new Error(message);
        }
    }

    async function runRegressionChecks() {
        const summary = [];
        const originalRuntime = global.__BLAZECLAW_RUNTIME_CONFIG__;
        const originalDashboardHost = global.__BLAZECLAW_DASHBOARD_HOST__;
        const originalSearch = global.location && global.location.search;
        const originalPathname = global.location && global.location.pathname;
        const originalLocalStorage = global.localStorage;
        const originalPostMessage = global.chrome &&
            global.chrome.webview &&
            global.chrome.webview.postMessage;

        function restore() {
            global.__BLAZECLAW_RUNTIME_CONFIG__ = originalRuntime;
            global.__BLAZECLAW_DASHBOARD_HOST__ = originalDashboardHost;
            if (global.location) {
                try {
                    Object.defineProperty(global.location, "search", {
                        configurable: true,
                        value: originalSearch || "",
                    });
                    Object.defineProperty(global.location, "pathname", {
                        configurable: true,
                        value: originalPathname || "",
                    });
                } catch (_) {
                }
            }
            if (originalLocalStorage) {
                global.localStorage = originalLocalStorage;
            }
            if (global.chrome && global.chrome.webview) {
                global.chrome.webview.postMessage = originalPostMessage;
            }
        }

        try {
            global.__BLAZECLAW_DASHBOARD_HOST__ = false;
            global.__BLAZECLAW_RUNTIME_CONFIG__ = { agents: { enabled: null } };
            global.location = global.location || {};
            global.location.search = "";
            global.location.pathname = "/index.html";
            global.localStorage = {
                _values: {},
                getItem(key) {
                    return Object.prototype.hasOwnProperty.call(this._values, key)
                        ? this._values[key]
                        : null;
                },
                setItem(key, value) {
                    this._values[key] = String(value);
                },
            };

            let trace = resolveAgentsEnabled();
            assertRegression(trace.resolved === false && trace.source === "default",
                "agents toggle should default to disabled in chat mode");
            summary.push("default-disabled");

            delete global.__BLAZECLAW_RUNTIME_CONFIG__;
            trace = resolveAgentsEnabled();
            assertRegression(trace.resolved === false && trace.source === "default",
                "missing __BLAZECLAW_RUNTIME_CONFIG__ should stay disabled in chat mode");
            summary.push("missing-runtime-config-disabled");

            global.__BLAZECLAW_RUNTIME_CONFIG__ = { agents: { enabled: true } };
            global.location.search = "?agents=0";
            global.localStorage.setItem("blazeclaw.agents.enabled", "0");
            trace = resolveAgentsEnabled();
            assertRegression(trace.resolved === true && trace.source === "config",
                "blazeclaw.conf agents.enabled should override query/localStorage");
            summary.push("config-precedence");

            global.__BLAZECLAW_RUNTIME_CONFIG__ = { agents: { enabled: null } };
            global.location.search = "?agents=1";
            trace = resolveAgentsEnabled();
            assertRegression(trace.resolved === true && trace.source === "query",
                "query agents=1 should enable when config unset");
            summary.push("query-enable");

            global.location.search = "";
            global.localStorage.setItem("blazeclaw.agents.enabled", "1");
            trace = resolveAgentsEnabled();
            assertRegression(trace.resolved === true && trace.source === "localStorage",
                "localStorage agents.enabled should enable when config unset");
            summary.push("localStorage-enable");

            global.__BLAZECLAW_RUNTIME_CONFIG__ = { agents: { enabled: true } };
            trace = resolveAgentsEnabled();
            assertRegression(trace.resolved === true && trace.source === "config",
                "runtime config enabled=true should resolve config source");
            summary.push("runtime-config-enabled");

            global.__BLAZECLAW_DASHBOARD_HOST__ = true;
            global.__BLAZECLAW_RUNTIME_CONFIG__ = { agents: { enabled: false } };
            trace = resolveAgentsEnabled();
            assertRegression(trace.resolved === true && trace.source === "dashboard-host",
                "dashboard host should always enable agents control plane");
            summary.push("dashboard-host");

            const posted = [];
            global.chrome = global.chrome || {};
            global.chrome.webview = global.chrome.webview || {};
            global.chrome.webview.postMessage = function (payload) {
                posted.push(payload);
            };
            emitAgentsToggleTrace(resolveAgentsEnabled(), { level: "info" });
            assertRegression(posted.length === 1 &&
                posted[0].channel === "blazeclaw.agents.toggle.trace",
                "agents toggle trace should post blazeclaw.agents.toggle.trace to native bridge");
            summary.push("native-trace-channel");

            return { ok: true, checks: summary };
        } finally {
            restore();
        }
    }

    global.BlazeClawAgentsToggle = {
        isDashboardHost,
        resolveAgentsEnabled,
        emitAgentsToggleTrace,
        emitMissingAgentsToggleModule,
        runRegressionChecks,
    };
})(typeof window !== "undefined" ? window : globalThis);
