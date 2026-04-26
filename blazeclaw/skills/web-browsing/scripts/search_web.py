import json
import re
import sys
from html.parser import HTMLParser
from urllib import error as urllib_error
from urllib import parse as urllib_parse
from urllib import request as urllib_request


class _AnchorExtractor(HTMLParser):
    def __init__(self):
        super().__init__()
        self._inside_anchor = False
        self._current_href = ""
        self._current_text = []
        self.results = []

    def handle_starttag(self, tag, attrs):
        if tag.lower() != "a":
            return

        href = ""
        for key, value in attrs:
            if key.lower() == "href":
                href = value or ""
                break

        if not href:
            return

        self._inside_anchor = True
        self._current_href = href.strip()
        self._current_text = []

    def handle_data(self, data):
        if self._inside_anchor and data:
            self._current_text.append(data)

    def handle_endtag(self, tag):
        if tag.lower() != "a" or not self._inside_anchor:
            return

        text = " ".join(part.strip() for part in self._current_text if part.strip())
        if self._current_href and text:
            self.results.append({
                "title": text,
                "link": self._current_href,
                "snippet": "",
            })

        self._inside_anchor = False
        self._current_href = ""
        self._current_text = []


def _normalize_url(base_url: str, href: str) -> str:
    if not href:
        return ""

    resolved = urllib_parse.urljoin(base_url, href)
    lowered = resolved.lower()
    if lowered.startswith("http://") or lowered.startswith("https://"):
        return resolved
    return ""


def _http_get_text(url: str, timeout_seconds: int = 20) -> str:
    request = urllib_request.Request(
        url,
        headers={
            "User-Agent": (
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                "AppleWebKit/537.36 (KHTML, like Gecko) "
                "Chrome/126.0.0.0 Safari/537.36"
            ),
            "Accept": "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
            "Accept-Language": "en-US,en;q=0.9",
        },
        method="GET",
    )

    try:
        with urllib_request.urlopen(request, timeout=timeout_seconds) as response:
            charset = response.headers.get_content_charset() or "utf-8"
            return response.read().decode(charset, errors="replace")
    except urllib_error.HTTPError as exc:
        charset = "utf-8"
        if exc.headers is not None:
            charset = exc.headers.get_content_charset() or "utf-8"
        body = exc.read().decode(charset, errors="replace") if exc.fp else ""
        raise RuntimeError(f"HTTP {exc.code}: {body or exc.reason}") from exc


def _extract_results_from_duckduckgo(html: str, max_count: int):
    parser = _AnchorExtractor()
    parser.feed(html)

    filtered = []
    seen_links = set()

    for item in parser.results:
        link = _normalize_url("https://duckduckgo.com", item["link"])
        if not link:
            continue

        lowered = link.lower()
        if "duckduckgo.com" in lowered:
            continue

        if link in seen_links:
            continue

        title = re.sub(r"\s+", " ", item["title"]).strip()
        if not title:
            continue

        seen_links.add(link)
        filtered.append({
            "title": title,
            "link": link,
            "snippet": "",
        })

        if len(filtered) >= max_count:
            break

    return filtered


def parse_args(argv):
    if len(argv) < 2:
        raise ValueError("Usage: python search_web.py '<JSON>'")

    raw = argv[1]
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ValueError(f"JSON parse error: {exc}") from exc

    if not isinstance(parsed, dict):
        raise ValueError("request body must be a JSON object")

    query = parsed.get("query")
    if not isinstance(query, str) or not query.strip():
        raise ValueError("query must be present in request body")

    count = parsed.get("count", 5)
    try:
        count = int(count)
    except Exception:
        count = 5

    if count <= 0:
        count = 5
    if count > 20:
        count = 20

    return {
        "query": query.strip(),
        "count": count,
    }


def run_search(query: str, count: int):
    encoded_query = urllib_parse.quote_plus(query)
    url = f"https://duckduckgo.com/html/?q={encoded_query}"
    html = _http_get_text(url)
    results = _extract_results_from_duckduckgo(html, count)

    if results:
        return results

    # Fallback: return at least one deterministic row to keep workflow visible.
    return [
        {
            "title": f"Search query: {query}",
            "link": f"https://duckduckgo.com/?q={encoded_query}",
            "snippet": "No parsed result rows from HTML endpoint.",
        }
    ]


def main(argv):
    args = parse_args(argv)
    rows = run_search(args["query"], args["count"])
    print(json.dumps(rows, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    try:
        main(sys.argv)
    except Exception as exc:
        print(f"Error: {exc}")
        sys.exit(1)
