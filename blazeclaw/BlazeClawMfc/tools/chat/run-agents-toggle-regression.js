"use strict";

const fs = require("fs");
const path = require("path");
const vm = require("vm");
const { URLSearchParams } = require("url");

const mfcRoot = path.resolve(__dirname, "..", "..");
const agentsTogglePath = path.join(mfcRoot, "web", "chat", "agents-toggle.js");

if (!fs.existsSync(agentsTogglePath)) {
    console.error("[agents-toggle-regression] missing file:", agentsTogglePath);
    process.exit(1);
}

const source = fs.readFileSync(agentsTogglePath, "utf8");
const sandbox = {
    window: {
        location: {
            search: "",
            pathname: "/index.html",
        },
    },
    URLSearchParams,
    console,
};
sandbox.globalThis = sandbox.window;
vm.createContext(sandbox);
vm.runInContext(source, sandbox, { filename: agentsTogglePath });

const api = sandbox.window.BlazeClawAgentsToggle;
if (!api || typeof api.runRegressionChecks !== "function") {
    console.error("[agents-toggle-regression] BlazeClawAgentsToggle.runRegressionChecks missing");
    process.exit(1);
}

api.runRegressionChecks()
    .then((result) => {
        if (!result || !result.ok) {
            console.error("[agents-toggle-regression] failed:", result);
            process.exit(1);
        }
        console.log("[agents-toggle-regression] passed:", result.checks.join(", "));
        process.exit(0);
    })
    .catch((error) => {
        console.error("[agents-toggle-regression] failed:", error);
        process.exit(1);
    });
