import json
import re
import sys
from html.parser import HTMLParser
from urllib import error as urllib_error
from urllib import parse as urllib_parse
from urllib import request as urllib_request


class _PlainTextExtractor(HTMLParser):
    def __init__(self):
        super().__init__()
        self._skip_depth = 0
        self._parts = []

    def handle_starttag(self, tag, attrs):
        lowered = tag.lower()
        if lowered in {"script", "style", "noscript"}:
            self._skip_depth += 1
            return

        if self._skip_depth > 0:
            return

        if lowered in {"p", "br", "div", "section", "article", "li", "h1", "h2", "h3", "h4"}:
            self._parts.append("\n")

    def handle_endtag(self, tag):
        lowered = tag.lower()
        if lowered in {"script", "style", "noscript"}:
            if self._skip_depth > 0:
                self._skip_depth -= 1
            return

        if self._skip_depth > 0:
            return

        if lowered in {"p", "div", "section", "article", "li"}:
            self._parts.append("\n")

    def handle_data(self, data):
        if self._skip_depth > 0:
            return

        text = data.strip()
        if text:
            self._parts.append(text)
            self._parts.append(" ")

    def get_text(self):
        raw = "".join(self._parts)
        raw = re.sub(r"[ \t]+", " ", raw)
        raw = re.sub(r"\n{3,}", "\n\n", raw)
        return raw.strip()


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


def parse_args(argv):
    if len(argv) < 2:
        raise ValueError("Usage: python fetch_content.py '<JSON>'")

    raw = argv[1]
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ValueError(f"JSON parse error: {exc}") from exc

    if not isinstance(parsed, dict):
        raise ValueError("request body must be a JSON object")

    url = parsed.get("url")
    if not isinstance(url, str) or not url.strip():
        raise ValueError("url must be present in request body")

    normalized = url.strip()
    lowered = normalized.lower()
    if not (lowered.startswith("http://") or lowered.startswith("https://")):
        raise ValueError("url must start with http:// or https://")

    return normalized


def extract_content(url: str):
    html = _http_get_text(url)

    parser = _PlainTextExtractor()
    parser.feed(html)
    text = parser.get_text()

    if not text:
        text = "Could not extract readable text from page."

    max_chars = 5000
    if len(text) > max_chars:
        text = text[:max_chars] + "\n\n[truncated]"

    return {
        "url": url,
        "content": text,
    }


def main(argv):
    url = parse_args(argv)
    result = extract_content(url)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    try:
        main(sys.argv)
    except Exception as exc:
        print(f"Error: {exc}")
        sys.exit(1)
