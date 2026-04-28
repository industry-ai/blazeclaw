import hashlib
import json
import os
import random
import re
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path
from typing import Any, Dict, Optional
from urllib import error as urllib_error
from urllib import request as urllib_request

try:
    import requests  # type: ignore
except Exception:
    requests = None


class HttpRequestError(RuntimeError):
    def __init__(self, status_code: int, message: str, headers: Optional[Dict[str, str]] = None):
        super().__init__(message)
        self.status_code = status_code
        self.headers = headers or {}


def _now_epoch_ms() -> int:
    return int(time.time() * 1000)


def _sha256_text(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def _header_map(headers_obj: Any) -> Dict[str, str]:
    if headers_obj is None:
        return {}

    try:
        if hasattr(headers_obj, "items"):
            return {str(key).lower(): str(value) for key, value in headers_obj.items()}
    except Exception:
        pass
    return {}


def _emit_diag(event: str, payload: Dict[str, Any]):
    if (os.getenv("BAIDU_SEARCH_DEBUG_DIAG") or "").strip() not in {"1", "true", "TRUE", "on", "ON"}:
        return

    row = {
        "scope": "baidu-search",
        "event": event,
        "ts": _now_epoch_ms(),
        "payload": payload,
    }
    log_path = (os.getenv("BAIDU_SEARCH_DIAG_LOG_PATH") or "").strip()
    if not log_path:
        return

    try:
        with open(log_path, "a", encoding="utf-8") as handle:
            handle.write(json.dumps(row, ensure_ascii=False) + "\n")
    except Exception:
        pass


def _get_env_int(name: str, default_value: int, min_value: int, max_value: int) -> int:
    raw = (os.getenv(name) or "").strip()
    if not raw:
        return default_value

    try:
        parsed = int(raw)
    except Exception:
        return default_value

    if parsed < min_value:
        return min_value
    if parsed > max_value:
        return max_value
    return parsed


def _extract_retry_after_seconds(headers: Dict[str, str]) -> Optional[int]:
    retry_after_raw = (headers.get("retry-after") or "").strip()
    if not retry_after_raw:
        return None

    try:
        parsed = int(retry_after_raw)
        if parsed < 0:
            return None
        return parsed
    except Exception:
        return None


def _http_post_json(
    url: str,
    headers: dict,
    request_body: dict,
    timeout: int = 30,
    simulate_429: bool = False,
):
    payload = json.dumps(request_body, ensure_ascii=False).encode("utf-8")

    if simulate_429:
        raise HttpRequestError(
            429,
            "synthetic 429 for deterministic retry validation",
            {
                "retry-after": "1",
                "x-request-id": "synthetic-429",
            },
        )

    if requests is not None:
        response = requests.post(url, data=payload, headers=headers, timeout=timeout)
        if response.status_code >= 400:
            raise HttpRequestError(
                response.status_code,
                response.text or response.reason,
                _header_map(response.headers),
            )
        return response.json(), _header_map(response.headers)

    req = urllib_request.Request(url, data=payload, headers=headers, method="POST")
    try:
        with urllib_request.urlopen(req, timeout=timeout) as response:
            charset = response.headers.get_content_charset() or "utf-8"
            text = response.read().decode(charset, errors="replace")
            return json.loads(text), _header_map(response.headers)
    except urllib_error.HTTPError as exc:
        charset = "utf-8"
        if exc.headers is not None:
            charset = exc.headers.get_content_charset() or "utf-8"
        body = exc.read().decode(charset, errors="replace") if exc.fp else ""
        raise HttpRequestError(
            exc.code,
            body or str(exc.reason),
            _header_map(exc.headers),
        ) from exc


def _cooldown_file_path() -> Path:
    base_dir = Path.home() / ".config" / "baidu-search"
    base_dir.mkdir(parents=True, exist_ok=True)
    return base_dir / "rate-limit-cooldowns.json"


def _load_cooldowns() -> dict:
    path = _cooldown_file_path()
    if not path.exists():
        return {}

    try:
        raw = path.read_text(encoding="utf-8")
        parsed = json.loads(raw)
        if isinstance(parsed, dict):
            return parsed
    except Exception:
        return {}
    return {}


def _save_cooldowns(rows: dict):
    try:
        _cooldown_file_path().write_text(
            json.dumps(rows, ensure_ascii=False, indent=2),
            encoding="utf-8",
        )
    except Exception:
        pass


def _cooldown_key(session_key: str, query_hash: str) -> str:
    return f"{session_key}:{query_hash}"


def _resolve_retry_policy() -> dict:
    return {
        "max_attempts": _get_env_int("BAIDU_SEARCH_RETRY_MAX_ATTEMPTS", 3, 1, 8),
        "base_delay_ms": _get_env_int("BAIDU_SEARCH_RETRY_BASE_DELAY_MS", 800, 100, 60000),
        "max_delay_ms": _get_env_int("BAIDU_SEARCH_RETRY_MAX_DELAY_MS", 8000, 200, 120000),
        "jitter_ms": _get_env_int("BAIDU_SEARCH_RETRY_JITTER_MS", 200, 0, 10000),
        "cooldown_seconds": _get_env_int("BAIDU_SEARCH_RATE_LIMIT_COOLDOWN_SECONDS", 30, 1, 3600),
    }


def baidu_search(api_key: str, parsed_args: dict):
    url = "https://qianfan.baidubce.com/v2/ai_search/web_search"
    headers = {
        "Authorization": f"Bearer {api_key}",
        "X-Appbuilder-From": "blazeclaw",
        "Content-Type": "application/json",
    }

    retry_policy = _resolve_retry_policy()
    request_body = {
        "messages": [{"content": parsed_args["query"], "role": "user"}],
        "search_source": "baidu_search_v2",
        "resource_type_filter": [{"type": "web", "top_k": parsed_args["count"]}],
        "search_filter": parsed_args["search_filter"],
    }

    query_hash = _sha256_text(parsed_args["query"])[:16]
    api_key_hash = _sha256_text(api_key)[-8:]
    session_key = parsed_args.get("sessionKey") or "main"
    simulate_429 = bool(parsed_args.get("simulate429", False))

    cooldown_rows = _load_cooldowns()
    cooldown_row = cooldown_rows.get(_cooldown_key(session_key, query_hash), {})
    cooldown_until = int(cooldown_row.get("untilEpochMs", 0) or 0)
    now_ms = _now_epoch_ms()
    if cooldown_until > now_ms:
        remaining_sec = max(1, int((cooldown_until - now_ms) / 1000))
        structured = {
            "errorType": "rate_limited_cooldown_active",
            "statusCode": 429,
            "sessionKey": session_key,
            "queryFingerprint": query_hash,
            "retryAfterSec": remaining_sec,
            "cooldownUntilEpochMs": cooldown_until,
            "remediation": "wait for cooldown or reduce immediate repeat calls",
        }
        raise RuntimeError("HTTP 429 cooldown_active: " + json.dumps(structured, ensure_ascii=False))

    retry_after_honored = False
    total_wait_ms = 0
    retry_attempt_total = 0

    for attempt in range(1, retry_policy["max_attempts"] + 1):
        _emit_diag(
            "request.start",
            {
                "attempt": attempt,
                "maxAttempts": retry_policy["max_attempts"],
                "sessionKey": session_key,
                "queryFingerprint": query_hash,
                "apiKeyFingerprint": api_key_hash,
                "simulate429": simulate_429,
            },
        )

        try:
            payload, response_headers = _http_post_json(
                url,
                headers,
                request_body,
                timeout=30,
                simulate_429=simulate_429,
            )
            if "code" in payload:
                raise RuntimeError(payload.get("message", "Baidu search request failed"))

            references = payload.get("references", [])
            for item in references:
                if "snippet" in item:
                    del item["snippet"]

            _emit_diag(
                "request.success",
                {
                    "attempt": attempt,
                    "sessionKey": session_key,
                    "queryFingerprint": query_hash,
                    "apiKeyFingerprint": api_key_hash,
                    "retryAttemptTotal": retry_attempt_total,
                    "totalWaitMs": total_wait_ms,
                    "responseHeaders": {
                        "retry-after": response_headers.get("retry-after", ""),
                        "x-request-id": response_headers.get("x-request-id", ""),
                    },
                },
            )
            return references
        except HttpRequestError as http_error:
            status_code = int(http_error.status_code)
            response_headers = _header_map(http_error.headers)
            retry_after_sec = _extract_retry_after_seconds(response_headers)
            is_retryable = status_code == 429 or 500 <= status_code <= 599

            _emit_diag(
                "request.http_error",
                {
                    "attempt": attempt,
                    "statusCode": status_code,
                    "sessionKey": session_key,
                    "queryFingerprint": query_hash,
                    "apiKeyFingerprint": api_key_hash,
                    "retryAfterSec": retry_after_sec,
                    "responseHeaders": {
                        "retry-after": response_headers.get("retry-after", ""),
                        "x-request-id": response_headers.get("x-request-id", ""),
                    },
                },
            )

            if status_code == 429:
                cooldown_sec = retry_after_sec or retry_policy["cooldown_seconds"]
                cooldown_until_epoch_ms = _now_epoch_ms() + (cooldown_sec * 1000)
                cooldown_rows[_cooldown_key(session_key, query_hash)] = {
                    "untilEpochMs": cooldown_until_epoch_ms,
                    "updatedAtEpochMs": _now_epoch_ms(),
                }
                _save_cooldowns(cooldown_rows)

            should_retry = is_retryable and attempt < retry_policy["max_attempts"]
            if should_retry:
                if retry_after_sec is not None:
                    sleep_ms = retry_after_sec * 1000
                    retry_after_honored = True
                else:
                    exponential_ms = retry_policy["base_delay_ms"] * (2 ** (attempt - 1))
                    sleep_ms = min(exponential_ms, retry_policy["max_delay_ms"])
                    if retry_policy["jitter_ms"] > 0:
                        sleep_ms += random.randint(0, retry_policy["jitter_ms"])

                total_wait_ms += sleep_ms
                retry_attempt_total += 1
                _emit_diag(
                    "request.retry_scheduled",
                    {
                        "attempt": attempt,
                        "nextAttempt": attempt + 1,
                        "statusCode": status_code,
                        "sleepMs": sleep_ms,
                        "retryAfterHonored": retry_after_sec is not None,
                    },
                )
                time.sleep(sleep_ms / 1000.0)
                continue

            final_error = {
                "errorType": "retry_exhausted",
                "statusCode": status_code,
                "sessionKey": session_key,
                "queryFingerprint": query_hash,
                "apiKeyFingerprint": api_key_hash,
                "attempt": attempt,
                "maxAttempts": retry_policy["max_attempts"],
                "retryAttemptTotal": retry_attempt_total,
                "totalWaitMs": total_wait_ms,
                "retryAfterHonored": retry_after_honored,
                "retryAfterSec": retry_after_sec,
                "requestId": response_headers.get("x-request-id", ""),
                "remediation": "retry later, lower request burst, or use fallback source",
            }
            raise RuntimeError(
                f"HTTP {status_code} retry_exhausted: "
                + json.dumps(final_error, ensure_ascii=False)
            ) from http_error


def parse_args(argv):
    if len(argv) < 2:
        raise ValueError("Usage: python search.py '<JSON>'")

    raw_arg = argv[1]

    try:
        parsed = json.loads(raw_arg)
    except json.JSONDecodeError as exc:
        raw = (raw_arg or "").strip()

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

    session_key = parsed.get("sessionKey")
    if session_key is None:
        session_key = "main"
    if not isinstance(session_key, str):
        session_key = str(session_key)

    simulate_429 = bool(parsed.get("simulate429", False))

    return {
        "query": query,
        "count": count,
        "search_filter": search_filter,
        "sessionKey": session_key,
        "simulate429": simulate_429,
    }


def main(argv):
    parsed = parse_args(argv)

    api_key = os.getenv("BAIDU_API_KEY")
    if not api_key:
        raise RuntimeError("BAIDU_API_KEY must be set in environment")

    results = baidu_search(api_key, parsed)
    print(json.dumps(results, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main(sys.argv)
    except Exception as exc:
        print(f"Error: {exc}")
        sys.exit(1)
