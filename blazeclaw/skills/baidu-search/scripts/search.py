import json
import os
import re
import sys
from datetime import datetime, timedelta
from urllib import error as urllib_error
from urllib import request as urllib_request

try:
    import requests  # type: ignore
except Exception:
    requests = None


def _http_post_json(url: str, headers: dict, request_body: dict, timeout: int = 30):
    payload = json.dumps(request_body, ensure_ascii=False).encode("utf-8")

    if requests is not None:
        response = requests.post(url, data=payload, headers=headers, timeout=timeout)
        response.raise_for_status()
        return response.json()

    req = urllib_request.Request(url, data=payload, headers=headers, method="POST")
    try:
        with urllib_request.urlopen(req, timeout=timeout) as response:
            charset = response.headers.get_content_charset() or "utf-8"
            text = response.read().decode(charset, errors="replace")
            return json.loads(text)
    except urllib_error.HTTPError as exc:
        charset = "utf-8"
        if exc.headers is not None:
            charset = exc.headers.get_content_charset() or "utf-8"
        body = exc.read().decode(charset, errors="replace") if exc.fp else ""
        raise RuntimeError(f"HTTP {exc.code}: {body or exc.reason}") from exc


def baidu_search(api_key: str, request_body: dict):
    url = "https://qianfan.baidubce.com/v2/ai_search/web_search"
    headers = {
        "Authorization": f"Bearer {api_key}",
        "X-Appbuilder-From": "blazeclaw",
        "Content-Type": "application/json",
    }

    payload = _http_post_json(url, headers, request_body, timeout=30)

    if "code" in payload:
        raise RuntimeError(payload.get("message", "Baidu search request failed"))

    references = payload.get("references", [])
    for item in references:
        if "snippet" in item:
            del item["snippet"]
    return references


def parse_args(argv):
    if len(argv) < 2:
        raise ValueError("Usage: python search.py '<JSON>'")

    raw_arg = argv[1]

    try:
        parsed = json.loads(raw_arg)
    except json.JSONDecodeError as exc:
        raw = (raw_arg or "").strip()

        # PowerShell can pass malformed object-like payloads such as {query:test}
        # by stripping JSON quotes. Accept this as a compatibility fallback.
        loose_query_match = re.match(r"^\{\s*query\s*:\s*(.*?)\s*\}$", raw, re.IGNORECASE)
        if loose_query_match is not None:
            fallback_query = loose_query_match.group(1).strip().strip('"').strip("'")
            parsed = {"query": fallback_query}
        else:
            raise ValueError(f"JSON parse error: {exc}") from exc

    if not isinstance(parsed, dict):
        raise ValueError("request body must be a JSON object")

    query = parsed.get("query")
    if not isinstance(query, str) or not query.strip():
        raise ValueError("query must be present in request body")

    count = 10
    if "count" in parsed:
        count = int(parsed["count"])
        if count <= 0:
            count = 10
        elif count > 50:
            count = 50

    search_filter = {}
    freshness = parsed.get("freshness")
    if freshness is not None:
        now = datetime.now()
        end_date = (now + timedelta(days=1)).strftime("%Y-%m-%d")
        pattern = r"\d{4}-\d{2}-\d{2}to\d{4}-\d{2}-\d{2}"

        if freshness in ["pd", "pw", "pm", "py"]:
            delta_days = {
                "pd": 1,
                "pw": 6,
                "pm": 30,
                "py": 364,
            }[freshness]
            start_date = (now - timedelta(days=delta_days)).strftime("%Y-%m-%d")
            search_filter = {
                "range": {
                    "page_time": {
                        "gte": start_date,
                        "lt": end_date,
                    }
                }
            }
        elif re.match(pattern, freshness):
            start_date, end_date = freshness.split("to")
            search_filter = {
                "range": {
                    "page_time": {
                        "gte": start_date,
                        "lt": end_date,
                    }
                }
            }
        else:
            raise ValueError(
                "freshness must be pd/pw/pm/py or YYYY-MM-DDtoYYYY-MM-DD"
            )

    return {
        "query": query,
        "count": count,
        "search_filter": search_filter,
    }


def main(argv):
    parsed = parse_args(argv)

    api_key = os.getenv("BAIDU_API_KEY")
    if not api_key:
        raise RuntimeError("BAIDU_API_KEY must be set in environment")

    request_body = {
        "messages": [{"content": parsed["query"], "role": "user"}],
        "search_source": "baidu_search_v2",
        "resource_type_filter": [{"type": "web", "top_k": parsed["count"]}],
        "search_filter": parsed["search_filter"],
    }

    results = baidu_search(api_key, request_body)
    print(json.dumps(results, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main(sys.argv)
    except Exception as exc:
        print(f"Error: {exc}")
        sys.exit(1)
