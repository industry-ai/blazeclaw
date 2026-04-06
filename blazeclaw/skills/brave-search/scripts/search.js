#!/usr/bin/env node

import { Readability } from "@mozilla/readability";
import { JSDOM } from "jsdom";
import TurndownService from "turndown";
import { gfm } from "turndown-plugin-gfm";

const args = process.argv.slice(2);

const contentIndex = args.indexOf("--content");
const fetchContent = contentIndex !== -1;
if (fetchContent) args.splice(contentIndex, 1);

let numResults = 5;
const nIndex = args.indexOf("-n");
if (nIndex !== -1 && args[nIndex + 1]) {
    numResults = parseInt(args[nIndex + 1], 10);
    args.splice(nIndex, 2);
}

const query = args.join(" ");

function stringifyErrorCause(cause) {
    if (!cause) {
        return undefined;
    }

    if (typeof cause === "string") {
        return cause;
    }

    if (cause instanceof Error) {
        return cause.message;
    }

    try {
        return JSON.stringify(cause);
    } catch {
        return String(cause);
    }
}

function formatErrorDetails(error, context = {}) {
    const source = error?.cause && typeof error.cause === "object"
        ? error.cause
        : error;

    const parts = [];
    if (context.operation) {
        parts.push(`operation=${context.operation}`);
    }
    if (context.target) {
        parts.push(`target=${context.target}`);
    }

    parts.push(`message=${error?.message || "unknown_error"}`);

    const causeMessage = stringifyErrorCause(error?.cause);
    if (causeMessage) {
        parts.push(`cause=${causeMessage}`);
    }

    const code = source?.code || error?.code;
    if (code) {
        parts.push(`code=${code}`);
    }

    const errno = source?.errno || error?.errno;
    if (errno !== undefined) {
        parts.push(`errno=${errno}`);
    }

    const syscall = source?.syscall || error?.syscall;
    if (syscall) {
        parts.push(`syscall=${syscall}`);
    }

    const hostname = source?.hostname || error?.hostname;
    if (hostname) {
        parts.push(`hostname=${hostname}`);
    }

    const address = source?.address || error?.address;
    if (address) {
        parts.push(`address=${address}`);
    }

    const port = source?.port || error?.port;
    if (port) {
        parts.push(`port=${port}`);
    }

    const proxy = process.env.HTTPS_PROXY || process.env.https_proxy ||
        process.env.HTTP_PROXY || process.env.http_proxy;
    if (proxy) {
        parts.push(`proxyConfigured=true`);
    }

    if (code === "ENOTFOUND" || code === "EAI_AGAIN") {
        parts.push("hint=DNS lookup failed; check network and DNS settings");
    } else if (code === "ECONNREFUSED" || code === "ECONNRESET") {
        parts.push("hint=connection rejected/reset; check proxy, firewall, or outbound policy");
    } else if (code === "ETIMEDOUT") {
        parts.push("hint=network timeout; check connectivity/proxy latency");
    } else if (typeof code === "string" && code.startsWith("ERR_TLS")) {
        parts.push("hint=TLS handshake/certificate issue; inspect proxy CA and TLS policy");
    }

    return parts.join("; ");
}

if (!query) {
    console.log("Usage: scripts/search.js <query> [-n <num>] [--content]");
    console.log("\nOptions:");
    console.log("  -n <num>    Number of results (default: 5)");
    console.log("  --content   Fetch readable content as markdown");
    console.log("\nExamples:");
    console.log('  scripts/search.js "javascript async await"');
    console.log('  scripts/search.js "rust programming" -n 10');
    console.log('  scripts/search.js "climate change" --content');
    process.exit(1);
}

async function fetchBraveResults(query, numResults) {
    const url = `https://search.brave.com/search?q=${encodeURIComponent(query)}`;

    const response = await fetch(url, {
        headers: {
            "User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36",
            "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8",
            "Accept-Language": "en-US,en;q=0.9",
            "sec-ch-ua": '"Chromium";v="142", "Google Chrome";v="142", "Not_A Brand";v="99"',
            "sec-ch-ua-mobile": "?0",
            "sec-ch-ua-platform": '"macOS"',
            "sec-fetch-dest": "document",
            "sec-fetch-mode": "navigate",
            "sec-fetch-site": "none",
            "sec-fetch-user": "?1",
        }
    });

    if (!response.ok) {
        throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }

    const html = await response.text();
    const dom = new JSDOM(html);
    const doc = dom.window.document;

    const results = [];

    // Find all search result snippets with data-type="web"
    const snippets = doc.querySelectorAll('div.snippet[data-type="web"]');

    for (const snippet of snippets) {
        if (results.length >= numResults) break;

        // Get the main link and title
        const titleLink = snippet.querySelector('a.svelte-14r20fy');
        if (!titleLink) continue;

        const link = titleLink.getAttribute('href');
        if (!link || link.includes('brave.com')) continue;

        const titleEl = titleLink.querySelector('.title');
        const title = titleEl?.textContent?.trim() || titleLink.textContent?.trim() || '';

        // Get the snippet/description
        const descEl = snippet.querySelector('.generic-snippet .content');
        let snippetText = descEl?.textContent?.trim() || '';
        // Remove date prefix if present
        snippetText = snippetText.replace(/^[A-Z][a-z]+ \d+, \d{4} -\s*/, '');

        if (title && link) {
            results.push({ title, link, snippet: snippetText });
        }
    }

    return results;
}

function htmlToMarkdown(html) {
    const turndown = new TurndownService({ headingStyle: "atx", codeBlockStyle: "fenced" });
    turndown.use(gfm);
    turndown.addRule("removeEmptyLinks", {
        filter: (node) => node.nodeName === "A" && !node.textContent?.trim(),
        replacement: () => "",
    });
    return turndown
        .turndown(html)
        .replace(/\[\\?\[\s*\\?\]\]\([^)]*\)/g, "")
        .replace(/ +/g, " ")
        .replace(/\s+,/g, ",")
        .replace(/\s+\./g, ".")
        .replace(/\n{3,}/g, "\n\n")
        .trim();
}

async function fetchPageContent(url) {
    try {
        const response = await fetch(url, {
            headers: {
                "User-Agent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36",
                "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            },
            signal: AbortSignal.timeout(10000),
        });

        if (!response.ok) {
            return `(HTTP ${response.status})`;
        }

        const html = await response.text();
        const dom = new JSDOM(html, { url });
        const reader = new Readability(dom.window.document);
        const article = reader.parse();

        if (article && article.content) {
            return htmlToMarkdown(article.content).substring(0, 5000);
        }

        // Fallback: try to get main content
        const fallbackDoc = new JSDOM(html, { url });
        const body = fallbackDoc.window.document;
        body.querySelectorAll("script, style, noscript, nav, header, footer, aside").forEach(el => el.remove());
        const main = body.querySelector("main, article, [role='main'], .content, #content") || body.body;
        const text = main?.textContent || "";

        if (text.trim().length > 100) {
            return text.trim().substring(0, 5000);
        }

        return "(Could not extract content)";
    } catch (e) {
        return `(Error: ${formatErrorDetails(e, {
            operation: "brave_search.fetch.content",
            target: url,
        })})`;
    }
}

// Main
try {
    const results = await fetchBraveResults(query, numResults);

    if (results.length === 0) {
        console.error("No results found.");
        process.exit(0);
    }

    if (fetchContent) {
        for (const result of results) {
            result.content = await fetchPageContent(result.link);
        }
    }

    for (let i = 0; i < results.length; i++) {
        const r = results[i];
        console.log(`--- Result ${i + 1} ---`);
        console.log(`Title: ${r.title}`);
        console.log(`Link: ${r.link}`);
        console.log(`Snippet: ${r.snippet}`);
        if (r.content) {
            console.log(`Content:\n${r.content}`);
        }
        console.log("");
    }
} catch (e) {
    console.error(formatErrorDetails(e, {
        operation: "brave_search.search.web",
        target: "https://search.brave.com",
    }));
    process.exit(1);
}
