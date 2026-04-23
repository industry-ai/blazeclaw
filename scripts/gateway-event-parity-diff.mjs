#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";
import process from "node:process";

function readText(filePath) {
  return fs.readFileSync(filePath, "utf8");
}

function extractArrayLiteralStrings(content, constName) {
  const startRegex = new RegExp(`const\\s+${constName}\\s*=\\s*\\[`, "m");
  const startMatch = startRegex.exec(content);
  if (!startMatch) {
    return [];
  }

  const startIndex = startMatch.index + startMatch[0].length;
  let depth = 1;
  let index = startIndex;
  while (index < content.length && depth > 0) {
    const ch = content[index];
    if (ch === "[") {
      depth += 1;
    } else if (ch === "]") {
      depth -= 1;
    }
    index += 1;
  }
  if (depth !== 0) {
    return [];
  }

  const body = content.slice(startIndex, index - 1);
  const literalRegex = /["'`]([a-z0-9_.-]+)["'`]/g;
  const values = new Set();
  let match;
  while ((match = literalRegex.exec(body)) !== null) {
    values.add(match[1]);
  }
  return Array.from(values);
}

function relativePosix(root, filePath) {
  return path.relative(root, filePath).replace(/\\/g, "/");
}

function collectLiteralMentions(workspaceRoot, files, eventNames) {
  const mentions = new Map();
  for (const eventName of eventNames) {
    mentions.set(eventName, []);
  }

  for (const filePath of files) {
    if (!fs.existsSync(filePath)) {
      continue;
    }
    const content = readText(filePath);
    const rel = relativePosix(workspaceRoot, filePath);
    for (const eventName of eventNames) {
      if (!content.includes(eventName)) {
        continue;
      }
      mentions.get(eventName).push(rel);
    }
  }
  return mentions;
}

function main() {
  const workspaceRoot = process.cwd();
  const openclawMethodsListPath = path.join(
    workspaceRoot,
    "openclaw",
    "src",
    "gateway",
    "server-methods-list.ts",
  );
  const blazeGatewayDir = path.join(workspaceRoot, "blazeclaw", "BlazeClawMfc", "src", "gateway");
  const blazeFiles = [
    "GatewayWebSocketTransport.cpp",
    "GatewayHost.cpp",
    "GatewayHost.Handlers.SecurityOps.cpp",
    "GatewayHost.Handlers.Runtime.ChatPipeline.cpp",
    "GatewayHost.Handlers.Runtime.Surface.cpp",
    "GatewayHost.Handlers.EventCatalogQuery.cpp",
    "GatewayHost.Handlers.Events.cpp",
    "GatewayHost.Handlers.Transport.cpp",
    "generated/GatewayHandlerCatalog.Generated.cpp",
    "GatewayHandlers.manifest.json",
  ].map((rel) => path.join(blazeGatewayDir, rel));

  const outputDir = path.join(workspaceRoot, "artifacts", "gateway-parity");
  const outputJsonPath = path.join(outputDir, "openclaw-vs-blazeclaw-event-diff.json");
  const outputMdPath = path.join(outputDir, "openclaw-vs-blazeclaw-event-diff.md");

  const source = readText(openclawMethodsListPath);
  const openclawEvents = extractArrayLiteralStrings(source, "GATEWAY_EVENTS");
  if (source.includes("GATEWAY_EVENT_UPDATE_AVAILABLE")) {
    openclawEvents.push("update.available");
  }
  const openclawResolvedEvents = Array.from(
    new Set(
      openclawEvents.map((value) =>
        value === "GATEWAY_EVENT_UPDATE_AVAILABLE" ? "update.available" : value,
      ),
    ),
  ).sort();
  const mentions = collectLiteralMentions(workspaceRoot, blazeFiles, openclawResolvedEvents);

  const rows = [];
  for (const eventName of openclawResolvedEvents) {
    const evidenceFiles = mentions.get(eventName) ?? [];
    rows.push({
      event: eventName,
      status: evidenceFiles.length > 0 ? "implemented" : "deferred",
      evidenceFiles,
      notes:
        evidenceFiles.length > 0
          ? "Literal event string found in BlazeClaw transport/handler/catalog surfaces."
          : "No direct literal match in scanned BlazeClaw files; requires alias/runtime-emitter follow-up.",
    });
  }

  const implemented = rows.filter((row) => row.status === "implemented").length;
  const deferred = rows.filter((row) => row.status === "deferred").length;
  const coveragePct = openclawResolvedEvents.length === 0
    ? 0
    : Math.round((implemented / openclawResolvedEvents.length) * 10000) / 100;

  const payload = {
    generatedAt: new Date().toISOString(),
    phase: "S2",
    inputs: {
      openclawEventSource: relativePosix(workspaceRoot, openclawMethodsListPath),
      blazeclawScannedFiles: blazeFiles.map((filePath) => relativePosix(workspaceRoot, filePath)),
      extraction: "OpenClaw GATEWAY_EVENTS array literals; BlazeClaw literal event-string evidence scan.",
    },
    summary: {
      openclawEventCount: openclawResolvedEvents.length,
      implementedCount: implemented,
      deferredCount: deferred,
      coveragePercent: coveragePct,
    },
    rows,
  };

  fs.mkdirSync(outputDir, { recursive: true });
  fs.writeFileSync(outputJsonPath, JSON.stringify(payload, null, 2), "utf8");

  const mdLines = [
    "# OpenClaw vs BlazeClaw Gateway Event Diff",
    "",
    `Generated: ${payload.generatedAt}`,
    "",
    "## Inputs",
    "",
    `- OpenClaw event list source: \`${payload.inputs.openclawEventSource}\``,
    `- BlazeClaw scanned files: **${payload.inputs.blazeclawScannedFiles.length}**`,
    `- Extraction: ${payload.inputs.extraction}`,
    "",
    "## Summary",
    "",
    `- OpenClaw event count: **${payload.summary.openclawEventCount}**`,
    `- Implemented evidence count: **${payload.summary.implementedCount}**`,
    `- Deferred/unmatched count: **${payload.summary.deferredCount}**`,
    `- Literal evidence coverage: **${payload.summary.coveragePercent}%**`,
    "",
    "## Event Mapping",
    "",
  ];

  for (const row of rows) {
    mdLines.push(`- \`${row.event}\` — **${row.status}**`);
    if (row.evidenceFiles.length > 0) {
      mdLines.push(`  - Evidence: ${row.evidenceFiles.map((file) => `\`${file}\``).join(", ")}`);
    } else {
      mdLines.push("  - Evidence: _none in scanned files_");
    }
  }
  mdLines.push("");

  fs.writeFileSync(outputMdPath, mdLines.join("\n"), "utf8");

  console.log(`Wrote ${relativePosix(workspaceRoot, outputJsonPath)}`);
  console.log(`Wrote ${relativePosix(workspaceRoot, outputMdPath)}`);
  console.log(`OpenClaw events: ${payload.summary.openclawEventCount}`);
  console.log(`Deferred events: ${payload.summary.deferredCount}`);
}

main();
