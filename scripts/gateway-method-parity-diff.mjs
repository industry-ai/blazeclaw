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

function collectOpenClawMethods(openclawServerMethodsDir) {
  const files = walk(openclawServerMethodsDir).filter((f) => f.endsWith(".ts"));
  const methodSet = new Set();
  const byFile = {};

  const methodRegex = /["'`]([a-z0-9_.-]+)["'`]\s*:/g;
  for (const file of files) {
    const content = fs.readFileSync(file, "utf8");
    const local = new Set();
    let match;
    while ((match = methodRegex.exec(content)) !== null) {
      const method = match[1];
      if (method.includes(".") && !method.startsWith("http")) {
        methodSet.add(method);
        local.add(method);
      }
    }
    if (local.size > 0) {
      byFile[path.relative(process.cwd(), file).replace(/\\/g, "/")] = Array.from(local).sort();
    }
  }

  return { methods: Array.from(methodSet).sort(), byFile };
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

function buildDiff(openclawMethods, blazeclawMethods) {
  const openSet = new Set(openclawMethods);
  const blazeSet = new Set(blazeclawMethods);

  const missingInBlazeClaw = openclawMethods.filter((m) => !blazeSet.has(m));
  const blazeOnly = blazeclawMethods.filter((m) => !openSet.has(m));
  const overlap = openclawMethods.filter((m) => blazeSet.has(m));

  return {
    openclawCount: openclawMethods.length,
    blazeclawCount: blazeclawMethods.length,
    overlapCount: overlap.length,
    missingInBlazeClawCount: missingInBlazeClaw.length,
    blazeOnlyCount: blazeOnly.length,
    overlap,
    missingInBlazeClaw,
    blazeOnly,
  };
}

function main() {
  const workspaceRoot = process.cwd();
  const openclawDir = path.join(workspaceRoot, "openclaw", "src", "gateway", "server-methods");
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

  const openclaw = collectOpenClawMethods(openclawDir);
  const blazeclaw = collectBlazeClawMethods(blazeManifestPath);
  const diff = buildDiff(openclaw.methods, blazeclaw.methods);

  fs.mkdirSync(outputDir, { recursive: true });

  const payload = {
    generatedAt: new Date().toISOString(),
    inputs: {
      openclawServerMethodsDir: path.relative(workspaceRoot, openclawDir).replace(/\\/g, "/"),
      blazeclawHandlersManifest: path.relative(workspaceRoot, blazeManifestPath).replace(/\\/g, "/"),
      blazeclawManifestVersion: blazeclaw.manifestVersion,
    },
    summary: {
      openclawMethodCount: diff.openclawCount,
      blazeclawMethodCount: diff.blazeclawCount,
      overlapCount: diff.overlapCount,
      missingInBlazeclawCount: diff.missingInBlazeClawCount,
      blazeOnlyCount: diff.blazeOnlyCount,
    },
    overlap: diff.overlap,
    missingInBlazeClaw: diff.missingInBlazeClaw,
    blazeOnly: diff.blazeOnly,
    openclawMethodsByFile: openclaw.byFile,
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
    `- BlazeClaw handlers manifest: \`${payload.inputs.blazeclawHandlersManifest}\` (version ${payload.inputs.blazeclawManifestVersion})`,
    "",
    "## Summary",
    "",
    `- OpenClaw method count: **${payload.summary.openclawMethodCount}**`,
    `- BlazeClaw method count: **${payload.summary.blazeclawMethodCount}**`,
    `- Overlap: **${payload.summary.overlapCount}**`,
    `- Missing in BlazeClaw: **${payload.summary.missingInBlazeclawCount}**`,
    `- BlazeClaw-only: **${payload.summary.blazeOnlyCount}**`,
    "",
    "## Missing in BlazeClaw",
    "",
    ...payload.missingInBlazeClaw.map((m) => `- \`${m}\``),
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
