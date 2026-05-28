#!/usr/bin/env python3
"""Compare BlazeClaw Sherpa baseline diagnostics with reference decoder output."""

from __future__ import annotations

import argparse
import difflib
import json
import sys
from pathlib import Path
from typing import Any, Iterable


BLAZECLAW_FIELD_ALIASES: dict[str, tuple[str, ...]] = {
    "decoded_text": ("decodedText", "sherpaBaselineDecodedText", "sherpaDecodedText", "text"),
    "expected_text": ("expectedText", "sherpaBaselineExpectedText"),
    "sample_rate": ("sampleRate", "sherpaBaselineSampleRate"),
    "chunk_samples": ("chunkSamples", "sherpaBaselineChunkSamples"),
    "fbank_frames": ("fbankFrameCount", "sherpaFbankFrameCount"),
    "encoder_frames": ("encoderFrameCount", "sherpaEncoderFrameCount"),
    "joiner_calls": ("joinerCallCount", "sherpaJoinerCallCount"),
    "blank_tokens": ("blankTokenCount", "sherpaBlankTokenCount"),
    "decoded_token_count": ("decodedTokenCount", "sherpaDecodedTokenCount"),
    "token_ids": ("tokenIds", "sherpaBaselineTokenIds"),
    "token_pieces": ("tokenPieces", "sherpaBaselineTokenPieces", "sherpaRawTokenPieces"),
    "sequence_start": ("sequenceStart", "sherpaBaselineInputStartSequence"),
    "sequence_end": ("sequenceEnd", "sherpaBaselineInputEndSequence", "sherpaFinalSequenceEnd"),
    "cursor_next": ("cursorNextSequence", "sherpaBaselineCursorNextSequence", "sherpaFinalCursorNext"),
    "final_remaining_samples": ("finalRemainingSamples", "sherpaFinalRemainingSamples"),
    "final_flush": ("finalFlush", "sherpaBaselineFinalFlush", "sherpaFinalFbankFlush"),
    "final_outcome": ("finalOutcome", "sherpaFinalOutcome"),
    "final_drain_complete": ("finalDrainComplete", "sherpaFinalDrainComplete"),
    "latency_ms": ("latencyMs", "finalMs", "final_ms"),
    "loop_count": ("loopCount", "sherpaLoopCount"),
    "max_loop_count": ("maxLoopCount", "sherpaMaxLoopCount"),
    "has_segment": ("hasSegment", "hasSegmentEffective"),
    "fallback_used": ("fallbackUsed", "sherpaFallbackUsed"),
    "rnnt_repeated_token_count": ("rnntRepeatedTokenCount", "sherpaRnntRepeatedTokenCount"),
    "rnnt_max_symbols_hit_count": ("rnntMaxSymbolsHitCount", "sherpaRnntMaxSymbolsHitCount"),
    "rnnt_multi_symbol_frame_count": ("rnntMultiSymbolFrameCount", "sherpaRnntMultiSymbolFrameCount"),
}

REFERENCE_FIELD_ALIASES: dict[str, tuple[str, ...]] = {
    "decoded_text": ("decodedText", "text", "transcript", "result", "sentence"),
    "sample_rate": ("sampleRate", "sample_rate"),
    "fbank_frames": ("fbankFrameCount", "fbank_frames", "numFrames", "num_frames"),
    "encoder_frames": ("encoderFrameCount", "encoder_frames"),
    "decoded_token_count": ("decodedTokenCount", "tokenCount", "token_count"),
    "token_ids": ("tokenIds", "tokens", "token_ids"),
    "token_pieces": ("tokenPieces", "pieces", "token_pieces"),
    "sequence_start": ("sequenceStart", "sequence_start", "inputStartSequence", "input_start_sequence"),
    "sequence_end": ("sequenceEnd", "sequence_end", "inputEndSequence", "input_end_sequence"),
    "cursor_next": ("cursorNextSequence", "cursor_next", "finalCursorNext", "final_cursor_next"),
    "final_remaining_samples": ("finalRemainingSamples", "final_remaining_samples"),
    "final_flush": ("finalFlush", "final_flush", "finalFbankFlush", "final_fbank_flush"),
    "final_outcome": ("finalOutcome", "final_outcome"),
    "final_drain_complete": ("finalDrainComplete", "final_drain_complete"),
    "latency_ms": ("finalMs", "final_ms", "latencyMs", "latency_ms"),
    "loop_count": ("loopCount", "loop_count"),
    "max_loop_count": ("maxLoopCount", "max_loop_count"),
}

COMPARISON_KEYS = (
    "sample_rate",
    "fbank_frames",
    "encoder_frames",
    "decoded_token_count",
    "token_ids",
    "token_pieces",
    "decoded_text",
)

FINAL_TIMING_KEYS = (
    "latency_ms",
    "loop_count",
    "max_loop_count",
)

FINAL_DRAIN_KEYS = (
    "sequence_start",
    "sequence_end",
    "cursor_next",
    "final_remaining_samples",
    "final_flush",
    "final_drain_complete",
    "final_outcome",
)


MISSING = object()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        data = json.load(stream)
    if not isinstance(data, dict):
        raise ValueError(f"{path} must contain a JSON object")
    return data


def first_value(data: dict[str, Any], aliases: Iterable[str]) -> Any:
    for alias in aliases:
        if alias in data:
            return data[alias]
    return MISSING


def normalize_token_list(value: Any) -> list[str]:
    if value is MISSING or value is None:
        return []
    if isinstance(value, list):
        return [str(item) for item in value]
    if isinstance(value, str):
        if not value.strip():
            return []
        return [part for part in value.replace(",", " ").split() if part]
    return [str(value)]


def normalize_scalar(value: Any) -> Any:
    if value is MISSING:
        return None
    if isinstance(value, str):
        return value.strip()
    return value


def as_bool(value: Any) -> bool | None:
    if value is MISSING or value is None:
        return None
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        normalized = value.strip().lower()
        if normalized in {"true", "1", "yes", "y"}:
            return True
        if normalized in {"false", "0", "no", "n"}:
            return False
    if isinstance(value, (int, float)):
        return value != 0
    return None


def as_int(value: Any) -> int | None:
    if value is MISSING or value is None:
        return None
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    if isinstance(value, float):
        return int(value)
    if isinstance(value, str):
        stripped = value.strip()
        if not stripped:
            return None
        try:
            return int(stripped)
        except ValueError:
            return None
    return None


def extract_metrics(data: dict[str, Any], aliases: dict[str, tuple[str, ...]]) -> dict[str, Any]:
    metrics: dict[str, Any] = {}
    for key, field_aliases in aliases.items():
        value = first_value(data, field_aliases)
        if key in {"token_ids", "token_pieces"}:
            metrics[key] = normalize_token_list(value)
        else:
            metrics[key] = normalize_scalar(value)
    return metrics


def parse_telemetry_payload(line: str) -> tuple[str, dict[str, Any]] | None:
    marker = "[Telemetry]"
    if marker in line:
        line = line.split(marker, 1)[1].strip()
    if not line.startswith("{"):
        return None
    try:
        envelope = json.loads(line)
    except json.JSONDecodeError:
        return None
    if not isinstance(envelope, dict):
        return None
    payload = envelope.get("payload")
    if not isinstance(payload, dict):
        return None
    event = payload.get("event")
    nested_payload = payload.get("payload")
    if not isinstance(event, str) or not isinstance(nested_payload, dict):
        return None
    return event, nested_payload


def extract_gateway_metrics(debug_log: Path | None) -> dict[str, Any]:
    metrics: dict[str, Any] = {
        "has_segment": None,
        "fallback_used": False,
        "user_visible_transcript": "",
    }
    if debug_log is None:
        return metrics
    with debug_log.open("r", encoding="utf-8-sig", errors="replace") as stream:
        for line in stream:
            parsed = parse_telemetry_payload(line)
            if parsed is None:
                continue
            event, payload = parsed
            if event == "gateway.speech.handoff.fallback":
                metrics["fallback_used"] = True
            if event in {"gateway.speech.execution.update", "gateway.speech.lifecycle"}:
                has_segment = as_bool(payload.get("hasSegment"))
                if has_segment is not None:
                    metrics["has_segment"] = has_segment
            if event == "gateway.speech.debug.snapshot":
                has_segment = as_bool(payload.get("hasSegmentEffective"))
                if has_segment is not None:
                    metrics["has_segment"] = has_segment
                fallback_used = as_bool(payload.get("fallbackUsed"))
                if fallback_used is True:
                    metrics["fallback_used"] = True
                text = payload.get("sherpaDecodedText") or payload.get("sherpaBaselineDecodedText")
                if isinstance(text, str) and text.strip():
                    metrics["user_visible_transcript"] = text.strip()
    return metrics


def evaluate_step8_regression(
    blazeclaw: dict[str, Any],
    gateway: dict[str, Any] | None = None,
) -> dict[str, Any]:
    gateway = gateway or {}
    decoded_text = blazeclaw.get("decoded_text")
    decoded_token_count = as_int(blazeclaw.get("decoded_token_count"))
    blank_tokens = as_int(blazeclaw.get("blank_tokens"))
    joiner_calls = as_int(blazeclaw.get("joiner_calls"))
    final_outcome = blazeclaw.get("final_outcome")

    has_segment = as_bool(blazeclaw.get("has_segment"))
    if has_segment is None:
        has_segment = as_bool(gateway.get("has_segment"))

    fallback_used = as_bool(blazeclaw.get("fallback_used"))
    if fallback_used is None:
        fallback_used = as_bool(gateway.get("fallback_used"))
    if fallback_used is None:
        fallback_used = False

    user_visible_transcript = gateway.get("user_visible_transcript")
    if not isinstance(user_visible_transcript, str) or not user_visible_transcript.strip():
        user_visible_transcript = decoded_text if isinstance(decoded_text, str) else ""

    checks = [
        {
            "name": "decoded_tokens_present",
            "pass": decoded_token_count is not None and decoded_token_count > 0,
            "details": {"sherpaDecodedTokenCount": decoded_token_count},
        },
        {
            "name": "not_all_joiner_calls_blank",
            "pass": blank_tokens is not None
            and joiner_calls is not None
            and blank_tokens < joiner_calls,
            "details": {
                "sherpaBlankTokenCount": blank_tokens,
                "sherpaJoinerCallCount": joiner_calls,
            },
        },
        {
            "name": "final_outcome_transcript",
            "pass": final_outcome == "final_transcript",
            "details": {"sherpaFinalOutcome": final_outcome},
        },
        {
            "name": "decoded_text_present",
            "pass": isinstance(decoded_text, str) and bool(decoded_text.strip()),
            "details": {"sherpaDecodedTextLength": len(decoded_text or "")},
        },
        {
            "name": "gateway_has_segment",
            "pass": has_segment is True,
            "details": {"hasSegment": has_segment},
        },
        {
            "name": "native_transcript_without_fallback",
            "pass": bool(str(user_visible_transcript).strip()) and fallback_used is False,
            "details": {
                "userVisibleTranscriptLength": len(str(user_visible_transcript).strip()),
                "fallbackUsed": fallback_used,
            },
        },
    ]
    return {
        "status": "passed" if all(check["pass"] for check in checks) else "failed",
        "checks": checks,
    }


def text_similarity(left: str | None, right: str | None) -> float | None:
    if left is None or right is None:
        return None
    return difflib.SequenceMatcher(a=left, b=right).ratio()


def is_cjk_char(value: str) -> bool:
    return any(
        start <= ord(value) <= end
        for start, end in (
            (0x3400, 0x4DBF),
            (0x4E00, 0x9FFF),
            (0xF900, 0xFAFF),
            (0x20000, 0x2A6DF),
            (0x2A700, 0x2B73F),
            (0x2B740, 0x2B81F),
            (0x2B820, 0x2CEAF),
        )
    )


def classify_repeated_token_ngrams(tokens: list[str]) -> dict[str, Any]:
    best: dict[str, Any] = {
        "detected": False,
        "ngramLength": 0,
        "repeatCount": 0,
        "unit": [],
        "startIndex": None,
        "coverage": 0.0,
    }
    if len(tokens) < 2:
        return best

    max_ngram = min(8, len(tokens) // 2)
    for ngram_length in range(1, max_ngram + 1):
        for start in range(0, len(tokens) - (ngram_length * 2) + 1):
            unit = tokens[start:start + ngram_length]
            if not unit:
                continue
            repeat_count = 1
            cursor = start + ngram_length
            while cursor + ngram_length <= len(tokens) and tokens[cursor:cursor + ngram_length] == unit:
                repeat_count += 1
                cursor += ngram_length
            coverage = (repeat_count * ngram_length) / len(tokens)
            best_coverage = float(best.get("coverage") or 0.0)
            if repeat_count > int(best.get("repeatCount") or 0) or (
                repeat_count == int(best.get("repeatCount") or 0) and coverage > best_coverage
            ):
                best = {
                    "detected": repeat_count >= 2,
                    "ngramLength": ngram_length,
                    "repeatCount": repeat_count,
                    "unit": unit,
                    "startIndex": start,
                    "coverage": coverage,
                }
    best["degenerate"] = (
        best["detected"]
        and int(best["repeatCount"]) >= 3
        and int(best["ngramLength"]) <= 8
    )
    return best


def classify_repeated_text_units(text: str | None) -> dict[str, Any]:
    value = "" if text is None else str(text)
    compact = "".join(ch for ch in value if not ch.isspace())
    longest_char_run = 0
    longest_char = ""
    current_char = ""
    current_count = 0
    for ch in compact:
        if ch == current_char:
            current_count += 1
        else:
            current_char = ch
            current_count = 1
        if current_count > longest_char_run:
            longest_char_run = current_count
            longest_char = ch

    best_phrase: dict[str, Any] = {
        "detected": False,
        "unit": "",
        "unitLength": 0,
        "repeatCount": 0,
        "startIndex": None,
        "coverage": 0.0,
    }
    for unit_length in range(2, 7):
        if len(compact) < unit_length * 2:
            continue
        for start in range(0, len(compact) - (unit_length * 2) + 1):
            unit = compact[start:start + unit_length]
            if not unit or not all(is_cjk_char(ch) for ch in unit):
                continue
            repeat_count = 1
            cursor = start + unit_length
            while cursor + unit_length <= len(compact) and compact[cursor:cursor + unit_length] == unit:
                repeat_count += 1
                cursor += unit_length
            coverage = (repeat_count * unit_length) / len(compact) if compact else 0.0
            best_coverage = float(best_phrase.get("coverage") or 0.0)
            if repeat_count > int(best_phrase.get("repeatCount") or 0) or (
                repeat_count == int(best_phrase.get("repeatCount") or 0) and coverage > best_coverage
            ):
                best_phrase = {
                    "detected": repeat_count >= 2,
                    "unit": unit,
                    "unitLength": unit_length,
                    "repeatCount": repeat_count,
                    "startIndex": start,
                    "coverage": coverage,
                }

    phrase_repeat_count = int(best_phrase.get("repeatCount") or 0)
    phrase_unit_length = int(best_phrase.get("unitLength") or 0)
    phrase_coverage = float(best_phrase.get("coverage") or 0.0)
    phrase_degenerate = (
        (phrase_unit_length == 2 and phrase_repeat_count >= 5)
        or (3 <= phrase_unit_length <= 6 and phrase_repeat_count >= 4)
        or (phrase_repeat_count >= 3 and phrase_coverage >= 0.35)
    )
    char_degenerate = longest_char_run >= 4 and bool(longest_char) and is_cjk_char(longest_char)
    return {
        "textLength": len(value),
        "compactLength": len(compact),
        "longestRepeatedChar": longest_char,
        "longestRepeatedCharRun": longest_char_run,
        "repeatedCjkPhrase": best_phrase,
        "degenerate": bool(phrase_degenerate or char_degenerate),
    }


def classify_repeat_patterns(metrics: dict[str, Any]) -> dict[str, Any]:
    token_classification = classify_repeated_token_ngrams(metrics.get("token_ids") or [])
    text_classification = classify_repeated_text_units(metrics.get("decoded_text"))
    rnnt_repeated = as_int(metrics.get("rnnt_repeated_token_count")) or 0
    rnnt_max_symbols = as_int(metrics.get("rnnt_max_symbols_hit_count")) or 0
    rnnt_multi_symbol = as_int(metrics.get("rnnt_multi_symbol_frame_count")) or 0
    degenerate = bool(
        token_classification.get("degenerate")
        or text_classification.get("degenerate")
    )
    return {
        "status": "degenerate_repeat_detected" if degenerate else "ok",
        "degenerate": degenerate,
        "tokenNgram": token_classification,
        "decodedText": text_classification,
        "rnntCounters": {
            "repeatedTokenCount": rnnt_repeated,
            "maxSymbolsHitCount": rnnt_max_symbols,
            "multiSymbolFrameCount": rnnt_multi_symbol,
        },
    }


def compare_metrics(
    blazeclaw: dict[str, Any],
    reference: dict[str, Any] | None,
    gateway: dict[str, Any] | None = None,
    require_step8_pass: bool = False,
    require_no_repeat: bool = False,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "status": "reference_missing" if reference is None else "compared",
        "blazeclaw": blazeclaw,
    }
    repeat_classification = classify_repeat_patterns(blazeclaw)
    result["repeatClassification"] = repeat_classification
    if gateway:
        result["gateway"] = gateway
    if require_no_repeat and repeat_classification.get("degenerate"):
        result["status"] = "repeat_failed"
        return result
    if require_step8_pass:
        step8 = evaluate_step8_regression(blazeclaw, gateway)
        result["step8"] = step8
        if step8["status"] != "passed":
            result["status"] = "step8_failed"
            return result
    if reference is None:
        result["message"] = "No reference JSON was supplied; BlazeClaw baseline metrics were extracted only."
        if require_step8_pass:
            result["status"] = "step8_passed"
        return result

    result["reference"] = reference
    deltas: dict[str, Any] = {}
    matched = True
    for key in COMPARISON_KEYS:
        left = blazeclaw.get(key)
        right = reference.get(key)
        if key in {"token_ids", "token_pieces"}:
            equal = left == right
            deltas[key] = {
                "match": equal,
                "blazeclawCount": len(left or []),
                "referenceCount": len(right or []),
                "firstDifference": first_difference(left or [], right or []),
            }
        elif isinstance(left, (int, float)) and isinstance(right, (int, float)):
            equal = left == right
            deltas[key] = {"match": equal, "delta": left - right}
        else:
            equal = left == right
            deltas[key] = {"match": equal}
        matched = matched and equal

    similarity = text_similarity(blazeclaw.get("decoded_text"), reference.get("decoded_text"))
    if similarity is not None:
        deltas["decoded_text"]["similarity"] = similarity

    final_timing = compare_named_fields(blazeclaw, reference, FINAL_TIMING_KEYS)
    final_drain = compare_named_fields(blazeclaw, reference, FINAL_DRAIN_KEYS)
    result["finalTiming"] = final_timing
    result["finalDrainState"] = final_drain
    matched = matched and final_timing["matched"] and final_drain["matched"]

    result["status"] = "matched" if matched else "different"
    result["deltas"] = deltas
    return result


def compare_named_fields(
    blazeclaw: dict[str, Any],
    reference: dict[str, Any],
    keys: Iterable[str],
) -> dict[str, Any]:
    fields: dict[str, Any] = {}
    matched = True
    comparable_count = 0
    for key in keys:
        left = blazeclaw.get(key)
        right = reference.get(key)
        both_missing = left is None and right is None
        if both_missing:
            fields[key] = {
                "match": True,
                "blazeclaw": left,
                "reference": right,
                "available": False,
            }
            continue

        comparable_count += 1
        equal = left == right
        field: dict[str, Any] = {
            "match": equal,
            "blazeclaw": left,
            "reference": right,
            "available": True,
        }
        if isinstance(left, (int, float)) and isinstance(right, (int, float)):
            field["delta"] = left - right
        fields[key] = field
        matched = matched and equal

    return {
        "matched": matched,
        "comparableCount": comparable_count,
        "fields": fields,
    }


def first_difference(left: list[str], right: list[str]) -> dict[str, Any] | None:
    max_len = max(len(left), len(right))
    for index in range(max_len):
        left_value = left[index] if index < len(left) else None
        right_value = right[index] if index < len(right) else None
        if left_value != right_value:
            return {"index": index, "blazeclaw": left_value, "reference": right_value}
    return None


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(data, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def print_summary(comparison: dict[str, Any]) -> None:
    blazeclaw = comparison["blazeclaw"]
    print(f"status: {comparison['status']}")
    print(f"blazeclaw decoded text: {blazeclaw.get('decoded_text') or ''}")
    expected_text = blazeclaw.get("expected_text")
    if expected_text:
        print(f"expected text: {expected_text}")
    print(f"blazeclaw token count: {len(blazeclaw.get('token_ids') or [])}")
    print(f"blazeclaw fbank frames: {blazeclaw.get('fbank_frames')}")
    print(f"blazeclaw final flush: {blazeclaw.get('final_flush')}")
    final_outcome = blazeclaw.get("final_outcome")
    if final_outcome:
        print(f"blazeclaw final outcome: {final_outcome}")

    step8 = comparison.get("step8")
    if isinstance(step8, dict):
        print(f"step8 status: {step8.get('status')}")
        for check in step8.get("checks", []):
            if isinstance(check, dict):
                state = "pass" if check.get("pass") else "fail"
                print(f"step8 {check.get('name')}: {state}")

    repeat_classification = comparison.get("repeatClassification")
    if isinstance(repeat_classification, dict):
        print(f"repeat classification: {repeat_classification.get('status')}")
        token_ngram = repeat_classification.get("tokenNgram")
        if isinstance(token_ngram, dict) and token_ngram.get("detected"):
            print(
                "token repeat: "
                f"len={token_ngram.get('ngramLength')} "
                f"count={token_ngram.get('repeatCount')} "
                f"unit={token_ngram.get('unit')}"
            )
        decoded_text = repeat_classification.get("decodedText")
        if isinstance(decoded_text, dict):
            phrase = decoded_text.get("repeatedCjkPhrase")
            if isinstance(phrase, dict) and phrase.get("detected"):
                print(
                    "decoded CJK repeat: "
                    f"unit={phrase.get('unit')} "
                    f"count={phrase.get('repeatCount')} "
                    f"coverage={float(phrase.get('coverage') or 0.0):.3f}"
                )

    reference = comparison.get("reference")
    if reference:
        print(f"reference decoded text: {reference.get('decoded_text') or ''}")
        deltas = comparison.get("deltas", {})
        text_delta = deltas.get("decoded_text", {})
        if "similarity" in text_delta:
            print(f"decoded text similarity: {text_delta['similarity']:.3f}")
        token_delta = deltas.get("token_ids", {})
        if token_delta.get("firstDifference"):
            diff = token_delta["firstDifference"]
            print(
                "first token difference: "
                f"index={diff['index']} blazeclaw={diff['blazeclaw']} reference={diff['reference']}"
            )
        final_timing = comparison.get("finalTiming")
        if isinstance(final_timing, dict):
            print(
                "final timing matched: "
                f"{str(final_timing.get('matched')).lower()} "
                f"({final_timing.get('comparableCount')} comparable fields)"
            )
        final_drain = comparison.get("finalDrainState")
        if isinstance(final_drain, dict):
            print(
                "final drain matched: "
                f"{str(final_drain.get('matched')).lower()} "
                f"({final_drain.get('comparableCount')} comparable fields)"
            )
    elif comparison.get("message"):
        print(comparison["message"])


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare a BlazeClaw Sherpa baseline JSON file with optional reference decoder JSON output."
    )
    parser.add_argument("--baseline", required=True, type=Path, help="Path to *.sherpa-baseline.json")
    parser.add_argument("--reference", type=Path, help="Optional known-good Sherpa/FunASR reference JSON")
    parser.add_argument("--debug-log", type=Path, help="Optional debug log containing gateway speech telemetry")
    parser.add_argument("--output", type=Path, help="Optional path for comparison JSON output")
    parser.add_argument(
        "--require-step8-pass",
        action="store_true",
        help="Fail unless the baseline and optional debug log satisfy Step 8 non-blank regression criteria.",
    )
    parser.add_argument(
        "--require-no-repeat",
        action="store_true",
        help="Fail if decoded text or token IDs contain a degenerate repeated CJK/token pattern.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        baseline_data = load_json(args.baseline)
        blazeclaw_metrics = extract_metrics(baseline_data, BLAZECLAW_FIELD_ALIASES)
        reference_metrics = None
        if args.reference is not None:
            reference_metrics = extract_metrics(load_json(args.reference), REFERENCE_FIELD_ALIASES)
        gateway_metrics = extract_gateway_metrics(args.debug_log)
        comparison = compare_metrics(
            blazeclaw_metrics,
            reference_metrics,
            gateway_metrics,
            args.require_step8_pass,
            args.require_no_repeat,
        )
        if args.output is not None:
            write_json(args.output, comparison)
        print_summary(comparison)
        if args.require_step8_pass and comparison.get("step8", {}).get("status") != "passed":
            return 1
        if args.require_no_repeat and comparison.get("repeatClassification", {}).get("degenerate"):
            return 1
        return 0
    except Exception as ex:
        print(f"error: {ex}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
