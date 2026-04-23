#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

function walk(dir, files = []) {
  if (!fs.existsSync(dir)) {
    return files;
  }
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      walk(full, files);
    } else {
      files.push(full);
    }
  }
  return files;
}

function relativePosix(workspaceRoot, filePath) {
  return path.relative(workspaceRoot, filePath).replace(/\\/g, "/");
}

function isIgnoredOpenClawTsFile(filePath) {
  const base = path.basename(filePath);
  if (!filePath.endsWith(".ts")) {
    return true;
  }
  if (base.endsWith(".test.ts")) {
    return true;
  }
  if (base.endsWith(".test-helpers.ts")) {
    return true;
  }
  if (base.endsWith(".runtime.ts")) {
    return true;
  }
  if (base === "types.ts" || base === "validation.ts") {
    return true;
  }
  return false;
}

function extractArrayLiteralStrings(content, constName) {
  const startRegex = new RegExp(`const\\s+${constName}\\s*=\\s*\\[`, "m");
  const startMatch = startRegex.exec(content);
  if (!startMatch) {
    return [];
  }

  const startIndex = startMatch.index + startMatch[0].length;
  let depth = 1;
  let idx = startIndex;
  while (idx < content.length && depth > 0) {
    const ch = content[idx];
    if (ch === "[") {
      depth += 1;
    } else if (ch === "]") {
      depth -= 1;
    }
    idx += 1;
  }
  if (depth !== 0) {
    return [];
  }

  const arrayBody = content.slice(startIndex, idx - 1);
  const literalRegex = /["'`]([a-z0-9_.-]+)["'`]/g;
  const values = new Set();
  let match;
  while ((match = literalRegex.exec(arrayBody)) !== null) {
    values.add(match[1]);
  }
  return Array.from(values);
}

const KNOWN_SINGLE_TOKEN_METHODS = new Set([
  "agent",
  "send",
  "wake",
  "status",
  "health",
  "connect",
]);

function isLikelyGatewayMethodName(value) {
  if (!value || value.length > 160) {
    return false;
  }
  if (!/^[a-z0-9.-]+$/.test(value)) {
    return false;
  }
  if (value.startsWith(".") || value.endsWith(".")) {
    return false;
  }
  if (value.includes("..")) {
    return false;
  }
  if (value.includes(".")) {
    return true;
  }
  return KNOWN_SINGLE_TOKEN_METHODS.has(value) || value.includes("-");
}

function extractHandlerObjectMethodKeys(content) {
  // Extract keys from patterns like:
  // export const xyzHandlers: GatewayRequestHandlers = { "chat.send": async (...) => ... }
  const blockRegex = /(?:const|let|var)\s+\w+Handlers\s*[:=][\s\S]*?\{([\s\S]*?)\n\};/g;
  const keyRegex = /["'`]([a-z0-9_.-]+)["'`]\s*:/g;
  const values = new Set();
  let block;
  while ((block = blockRegex.exec(content)) !== null) {
    let keyMatch;
    while ((keyMatch = keyRegex.exec(block[1])) !== null) {
      const method = keyMatch[1];
      if (isLikelyGatewayMethodName(method)) {
        values.add(method);
      }
    }
  }
  return Array.from(values);
}

function collectOpenClawMethods(workspaceRoot, openclawServerMethodsDir, openclawMethodsListPath) {
  const methodSet = new Set();
  const byFile = {};

  // 1) Canonical method inventory from BASE_METHODS in server-methods-list.ts
  if (fs.existsSync(openclawMethodsListPath)) {
    const content = fs.readFileSync(openclawMethodsListPath, "utf8");
    const methods = extractArrayLiteralStrings(content, "BASE_METHODS");
    const rel = relativePosix(workspaceRoot, openclawMethodsListPath);
    byFile[rel] = methods.slice().sort();
    for (const method of methods) {
      methodSet.add(method);
    }
  }

  // 2) Defensive secondary extraction from handler object keys in server-methods/*.ts
  const handlerFiles = walk(openclawServerMethodsDir).filter((f) => !isIgnoredOpenClawTsFile(f));
  for (const file of handlerFiles) {
    const content = fs.readFileSync(file, "utf8");
    const local = new Set(extractHandlerObjectMethodKeys(content));
    if (local.size === 0) {
      continue;
    }
    const rel = relativePosix(workspaceRoot, file);
    byFile[rel] = Array.from(local).sort();
    for (const method of local) {
      methodSet.add(method);
    }
  }

  return {
    methods: Array.from(methodSet).sort(),
    byFile,
  };
}

function collectBlazeClawMethods(handlersManifestPath) {
  const manifest = readJson(handlersManifestPath);
  const methods = new Set();

  for (const entry of manifest.methods ?? []) {
    if (entry?.name) {
      methods.add(entry.name);
    }
  }

  return { methods: Array.from(methods).sort(), manifestVersion: manifest.version ?? 1 };
}

function collectBlazeClawRegisteredMethods(workspaceRoot, blazeGatewayDir) {
  const files = walk(blazeGatewayDir).filter((f) => f.endsWith(".cpp") || f.endsWith(".h"));
  const methods = new Set();
  const byFile = {};
  const registerRegex = /Register\(\s*"([^"]+)"/g;
  const helperRegistrationCallRegex = /\b(?:Register|register)[A-Za-z0-9_]*\s*\(([\s\S]{0,800}?)\);/g;
  const methodLiteralRegex = /"([a-z0-9_.-]+)"/g;

  for (const file of files) {
    const content = fs.readFileSync(file, "utf8");
    const local = new Set();
    let match;
    while ((match = registerRegex.exec(content)) !== null) {
      local.add(match[1]);
      methods.add(match[1]);
    }
    while ((match = helperRegistrationCallRegex.exec(content)) !== null) {
      let literalMatch;
      while ((literalMatch = methodLiteralRegex.exec(match[1])) !== null) {
        const method = literalMatch[1];
        if (!isLikelyGatewayMethodName(method)) {
          continue;
        }
        local.add(method);
        methods.add(method);
      }
    }
    if (local.size > 0) {
      byFile[relativePosix(workspaceRoot, file)] = Array.from(local).sort();
    }
  }

  return { methods: Array.from(methods).sort(), byFile };
}

function normalizeBlazeMethod(method, openSet) {
  // Keep gateway.identity.get canonical; do not blindly strip gateway prefix.
  if (method === "gateway.session.list" && openSet.has("sessions.list")) {
    return "sessions.list";
  }
  if (method.startsWith("gateway.")) {
    const stripped = method.slice("gateway.".length);
    if (openSet.has(stripped)) {
      return stripped;
    }
  }
  return method;
}

function buildDiff(openclawMethods, blazeclawMethodsRaw) {
  const openSet = new Set(openclawMethods);
  const blazeSet = new Set(
    blazeclawMethodsRaw.map((m) => normalizeBlazeMethod(m, openSet)),
  );
  const blazeRawSet = new Set(blazeclawMethodsRaw);

  const missingInBlazeClaw = openclawMethods.filter((m) => !blazeSet.has(m));
  const blazeOnly = Array.from(blazeSet).filter((m) => !openSet.has(m));
  const overlap = openclawMethods.filter((m) => blazeSet.has(m));

  return {
    openclawCount: openclawMethods.length,
    blazeclawCount: blazeSet.size,
    blazeclawRawCount: blazeRawSet.size,
    overlapCount: overlap.length,
    missingInBlazeClawCount: missingInBlazeClaw.length,
    blazeOnlyCount: blazeOnly.length,
    overlap,
    missingInBlazeClaw,
    blazeOnly,
  };
}

const EXPECTED_MISSING_BY_DESIGN_RULES = [
  {
    type: "exact",
    value: "push.test",
    classification: "out_of_scope",
    reason: "OpenClaw push test helper is not in current BlazeClaw product scope.",
  },
  {
    type: "prefix",
    value: "device.pair.",
    classification: "explicitly_unsupported",
    reason: "Device pairing family is intentionally unsupported in current BlazeClaw runtime.",
  },
];

function classifyMissingMethods(missingMethods) {
  const expected = [];
  const actionable = [];

  for (const method of missingMethods) {
    const matchedRule = EXPECTED_MISSING_BY_DESIGN_RULES.find((rule) => {
      if (rule.type === "exact") {
        return method === rule.value;
      }
      if (rule.type === "prefix") {
        return method.startsWith(rule.value);
      }
      return false;
    });

    if (matchedRule) {
      expected.push({
        method,
        classification: matchedRule.classification,
        reason: matchedRule.reason,
      });
    } else {
      actionable.push(method);
    }
  }

  return { expected, actionable };
}

function main() {
  const workspaceRoot = process.cwd();
  const openclawDir = path.join(workspaceRoot, "openclaw", "src", "gateway", "server-methods");
  const openclawMethodsListPath = path.join(
    workspaceRoot,
    "openclaw",
    "src",
    "gateway",
    "server-methods-list.ts",
  );
  const blazeGatewayDir = path.join(workspaceRoot, "blazeclaw", "BlazeClawMfc", "src", "gateway");
  const blazeManifestPath = path.join(
    workspaceRoot,
    "blazeclaw",
    "BlazeClawMfc",
    "src",
    "gateway",
    "GatewayHandlers.manifest.json",
  );
  const outputDir = path.join(workspaceRoot, "artifacts", "gateway-parity");
  const outputJsonPath = path.join(outputDir, "openclaw-vs-blazeclaw-method-diff.json");
  const outputMdPath = path.join(outputDir, "openclaw-vs-blazeclaw-method-diff.md");

  const openclaw = collectOpenClawMethods(workspaceRoot, openclawDir, openclawMethodsListPath);
  const blazeManifest = collectBlazeClawMethods(blazeManifestPath);
  const blazeRegistered = collectBlazeClawRegisteredMethods(workspaceRoot, blazeGatewayDir);
  const blazeUnion = Array.from(new Set([...blazeManifest.methods, ...blazeRegistered.methods])).sort();
  const diff = buildDiff(openclaw.methods, blazeUnion);
  const missingClassification = classifyMissingMethods(diff.missingInBlazeClaw);

  fs.mkdirSync(outputDir, { recursive: true });

  const payload = {
    generatedAt: new Date().toISOString(),
    lifecycleMethodSurfaceCheckpoints: {
      phase: "S4",
      description:
        "BlazeClaw records dispatcher method-surface snapshots around deferred extension-catalog reload (managed config `embedded.extensionSurfaceApplyEpoch`) and at runtime finalize enforces three subset checks on the dispatcher: generated handler-catalog methods, static channel-handler RPC names, and the deduped union of optional per-extension `gatewayRpcMethods` strings from each `blazeclaw.extension.json` (parsed in `ExtensionLifecycleManager::LoadCatalog`; violation `plugin_rpc_surface_not_registered`). See `GatewayHost::PerformDeferredExtensionCatalogReloadWithMethodSurfaceTelemetry`, `GatewayMethodSurfaceAudit`, `GatewayHost::VerifyRuntimeMethodSurfaceInvariants`, and parity contract schema v4 fields `extensionSurfaceReloadCount` / `lastAppliedExtensionSurfaceEpoch` / `lastExtensionSurfaceMethodDeltaJson`.",
    },
    inputs: {
      openclawServerMethodsDir: path.relative(workspaceRoot, openclawDir).replace(/\\/g, "/"),
      openclawMethodsList: path.relative(workspaceRoot, openclawMethodsListPath).replace(/\\/g, "/"),
      blazeclawHandlersManifest: path.relative(workspaceRoot, blazeManifestPath).replace(/\\/g, "/"),
      blazeclawGatewaySourceDir: path.relative(workspaceRoot, blazeGatewayDir).replace(/\\/g, "/"),
      blazeclawManifestVersion: blazeManifest.manifestVersion,
      extraction: {
        openclaw: "BASE_METHODS + handler object keys (non-test files)",
        blazeclaw: "manifest methods + Register(\"...\") scan + helper registration call scan in gateway sources",
        normalization: "gateway.* stripped only when canonical OpenClaw method exists; gateway.session.list -> sessions.list alias",
      },
    },
    summary: {
      openclawMethodCount: diff.openclawCount,
      blazeclawMethodCount: diff.blazeclawCount,
      blazeclawRawMethodCount: diff.blazeclawRawCount,
      overlapCount: diff.overlapCount,
      missingInBlazeclawCount: diff.missingInBlazeClawCount,
      expectedMissingByDesignCount: missingClassification.expected.length,
      actionableMissingCount: missingClassification.actionable.length,
      blazeOnlyCount: diff.blazeOnlyCount,
    },
    overlap: diff.overlap,
    missingInBlazeClaw: diff.missingInBlazeClaw,
    missingInBlazeClawExpectedByDesign: missingClassification.expected,
    missingInBlazeClawActionable: missingClassification.actionable,
    blazeOnly: diff.blazeOnly,
    openclawMethodsByFile: openclaw.byFile,
    blazeclawMethodsByFile: blazeRegistered.byFile,
    blazeclawManifestMethods: blazeManifest.methods,
  };

  fs.writeFileSync(outputJsonPath, JSON.stringify(payload, null, 2), "utf8");

  const md = [
    "# OpenClaw vs BlazeClaw Gateway Method Diff",
    "",
    `Generated: ${payload.generatedAt}`,
    "",
    "## Inputs",
    "",
    `- OpenClaw methods: \`${payload.inputs.openclawServerMethodsDir}\``,
    `- OpenClaw canonical list source: \`${payload.inputs.openclawMethodsList}\``,
    `- BlazeClaw handlers manifest: \`${payload.inputs.blazeclawHandlersManifest}\` (version ${payload.inputs.blazeclawManifestVersion})`,
    `- BlazeClaw registration scan source: \`${payload.inputs.blazeclawGatewaySourceDir}\``,
    "",
    "## Lifecycle method-surface checkpoints (S4)",
    "",
    "See JSON root `lifecycleMethodSurfaceCheckpoints` for the BlazeClaw contract reference (deferred extension reload + subset invariant checks: generated catalog, channel surface, manifest `gatewayRpcMethods` union).",
    "",
    "## Summary",
    "",
    `- OpenClaw method count: **${payload.summary.openclawMethodCount}**`,
    `- BlazeClaw method count: **${payload.summary.blazeclawMethodCount}**`,
    `- BlazeClaw raw method count (before normalization): **${payload.summary.blazeclawRawMethodCount}**`,
    `- Overlap: **${payload.summary.overlapCount}**`,
    `- Missing in BlazeClaw: **${payload.summary.missingInBlazeclawCount}**`,
    `  - Expected missing by design: **${payload.summary.expectedMissingByDesignCount}**`,
    `  - Actionable missing: **${payload.summary.actionableMissingCount}**`,
    `- BlazeClaw-only: **${payload.summary.blazeOnlyCount}**`,
    "",
    "## Missing in BlazeClaw (Actionable)",
    "",
    ...(payload.missingInBlazeClawActionable.length > 0
      ? payload.missingInBlazeClawActionable.map((m) => `- \`${m}\``)
      : ["- _None_"]),
    "",
    "## Missing in BlazeClaw (Expected by Design)",
    "",
    ...(payload.missingInBlazeClawExpectedByDesign.length > 0
      ? payload.missingInBlazeClawExpectedByDesign.map(
          (entry) => `- \`${entry.method}\` — ${entry.classification}: ${entry.reason}`,
        )
      : ["- _None_"]),
    "",
    "## BlazeClaw-only",
    "",
    ...payload.blazeOnly.map((m) => `- \`${m}\``),
    "",
  ].join("\n");

  fs.writeFileSync(outputMdPath, md, "utf8");

  console.log(`Wrote ${path.relative(workspaceRoot, outputJsonPath).replace(/\\/g, "/")}`);
  console.log(`Wrote ${path.relative(workspaceRoot, outputMdPath).replace(/\\/g, "/")}`);
  console.log(`Missing in BlazeClaw: ${payload.summary.missingInBlazeclawCount}`);
}

main();
