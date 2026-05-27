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
    "final_flush": ("finalFlush", "sherpaBaselineFinalFlush", "sherpaFinalFbankFlush"),
    "final_outcome": ("finalOutcome", "sherpaFinalOutcome"),
}

REFERENCE_FIELD_ALIASES: dict[str, tuple[str, ...]] = {
    "decoded_text": ("decodedText", "text", "transcript", "result", "sentence"),
    "sample_rate": ("sampleRate", "sample_rate"),
    "fbank_frames": ("fbankFrameCount", "fbank_frames", "numFrames", "num_frames"),
    "encoder_frames": ("encoderFrameCount", "encoder_frames"),
    "decoded_token_count": ("decodedTokenCount", "tokenCount", "token_count"),
    "token_ids": ("tokenIds", "tokens", "token_ids"),
    "token_pieces": ("tokenPieces", "pieces", "token_pieces"),
    "final_ms": ("finalMs", "final_ms", "latencyMs", "latency_ms"),
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


def extract_metrics(data: dict[str, Any], aliases: dict[str, tuple[str, ...]]) -> dict[str, Any]:
    metrics: dict[str, Any] = {}
    for key, field_aliases in aliases.items():
        value = first_value(data, field_aliases)
        if key in {"token_ids", "token_pieces"}:
            metrics[key] = normalize_token_list(value)
        else:
            metrics[key] = normalize_scalar(value)
    return metrics


def text_similarity(left: str | None, right: str | None) -> float | None:
    if left is None or right is None:
        return None
    return difflib.SequenceMatcher(a=left, b=right).ratio()


def compare_metrics(blazeclaw: dict[str, Any], reference: dict[str, Any] | None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "status": "reference_missing" if reference is None else "compared",
        "blazeclaw": blazeclaw,
    }
    if reference is None:
        result["message"] = "No reference JSON was supplied; BlazeClaw baseline metrics were extracted only."
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

    result["status"] = "matched" if matched else "different"
    result["deltas"] = deltas
    return result


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
    elif comparison.get("message"):
        print(comparison["message"])


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Compare a BlazeClaw Sherpa baseline JSON file with optional reference decoder JSON output."
    )
    parser.add_argument("--baseline", required=True, type=Path, help="Path to *.sherpa-baseline.json")
    parser.add_argument("--reference", type=Path, help="Optional known-good Sherpa/FunASR reference JSON")
    parser.add_argument("--output", type=Path, help="Optional path for comparison JSON output")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        baseline_data = load_json(args.baseline)
        blazeclaw_metrics = extract_metrics(baseline_data, BLAZECLAW_FIELD_ALIASES)
        reference_metrics = None
        if args.reference is not None:
            reference_metrics = extract_metrics(load_json(args.reference), REFERENCE_FIELD_ALIASES)
        comparison = compare_metrics(blazeclaw_metrics, reference_metrics)
        if args.output is not None:
            write_json(args.output, comparison)
        print_summary(comparison)
        return 0
    except Exception as ex:
        print(f"error: {ex}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
