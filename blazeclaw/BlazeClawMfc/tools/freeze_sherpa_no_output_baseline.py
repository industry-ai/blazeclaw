#!/usr/bin/env python3
"""Validate and archive the Sherpa no-output regression baseline."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


MISSING = object()

FIELD_ALIASES: dict[str, tuple[str, ...]] = {
    "run_id": ("runId", "run_id"),
    "session_id": ("sessionId", "session_id"),
    "stream_id": ("streamId", "stream_id"),
    "expected_text": ("expectedText", "sherpaBaselineExpectedText"),
    "decoded_text": ("decodedText", "sherpaBaselineDecodedText", "sherpaDecodedText", "text"),
    "sample_rate": ("sampleRate", "sherpaBaselineSampleRate"),
    "chunk_samples": ("chunkSamples", "sherpaBaselineChunkSamples"),
    "sequence_start": ("sequenceStart", "sherpaBaselineInputStartSequence"),
    "sequence_end": ("sequenceEnd", "sherpaBaselineInputEndSequence", "sherpaFinalSequenceEnd"),
    "cursor_next": ("cursorNextSequence", "sherpaBaselineCursorNextSequence", "sherpaFinalCursorNext"),
    "final_flush": ("finalFlush", "sherpaBaselineFinalFlush", "sherpaFinalFbankFlush"),
    "speech_active": ("speechActive", "sherpaSpeechActive"),
    "fbank_frames": ("fbankFrameCount", "sherpaFbankFrameCount"),
    "encoder_frames": ("encoderFrameCount", "sherpaEncoderFrameCount"),
    "joiner_calls": ("joinerCallCount", "sherpaJoinerCallCount"),
    "blank_tokens": ("blankTokenCount", "sherpaBlankTokenCount"),
    "decoded_token_count": ("decodedTokenCount", "sherpaDecodedTokenCount"),
    "emitted_token_count": ("emittedTokenCount", "sherpaEmittedTokenCount"),
    "final_outcome": ("finalOutcome", "sherpaFinalOutcome"),
    "token_ids": ("tokenIds", "sherpaBaselineTokenIds"),
    "token_pieces": ("tokenPieces", "sherpaBaselineTokenPieces", "sherpaRawTokenPieces"),
}


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


def as_text(value: Any) -> str:
    if value is MISSING or value is None:
        return ""
    if isinstance(value, str):
        return value.strip()
    return str(value).strip()


def extract_metrics(data: dict[str, Any]) -> dict[str, Any]:
    raw = {key: first_value(data, aliases) for key, aliases in FIELD_ALIASES.items()}
    return {
        "run_id": as_text(raw["run_id"]),
        "session_id": as_text(raw["session_id"]),
        "stream_id": as_text(raw["stream_id"]),
        "expected_text": as_text(raw["expected_text"]),
        "decoded_text": as_text(raw["decoded_text"]),
        "sample_rate": as_int(raw["sample_rate"]),
        "chunk_samples": as_int(raw["chunk_samples"]),
        "sequence_start": as_int(raw["sequence_start"]),
        "sequence_end": as_int(raw["sequence_end"]),
        "cursor_next": as_int(raw["cursor_next"]),
        "final_flush": as_bool(raw["final_flush"]),
        "speech_active": as_bool(raw["speech_active"]),
        "fbank_frames": as_int(raw["fbank_frames"]),
        "encoder_frames": as_int(raw["encoder_frames"]),
        "joiner_calls": as_int(raw["joiner_calls"]),
        "blank_tokens": as_int(raw["blank_tokens"]),
        "decoded_token_count": as_int(raw["decoded_token_count"]),
        "emitted_token_count": as_int(raw["emitted_token_count"]),
        "final_outcome": as_text(raw["final_outcome"]),
        "token_ids": as_text(raw["token_ids"]),
        "token_pieces": as_text(raw["token_pieces"]),
    }


def evaluate_signature(metrics: dict[str, Any], require_speech_active: bool) -> dict[str, Any]:
    checks = [
        {
            "name": "final_stream_drained",
            "pass": metrics["sequence_end"] is not None
            and metrics["cursor_next"] is not None
            and metrics["sequence_end"] > 0
            and metrics["cursor_next"] >= metrics["sequence_end"],
            "details": {
                "sequenceEnd": metrics["sequence_end"],
                "cursorNextSequence": metrics["cursor_next"],
            },
        },
        {
            "name": "final_fbank_flush",
            "pass": metrics["final_flush"] is True,
            "details": {"finalFlush": metrics["final_flush"]},
        },
        {
            "name": "speech_active",
            "pass": (metrics["speech_active"] is True) if require_speech_active else metrics["speech_active"] is not False,
            "details": {"speechActive": metrics["speech_active"], "required": require_speech_active},
        },
        {
            "name": "encoder_frames_present",
            "pass": metrics["encoder_frames"] is not None and metrics["encoder_frames"] > 0,
            "details": {"encoderFrameCount": metrics["encoder_frames"]},
        },
        {
            "name": "joiner_calls_present",
            "pass": metrics["joiner_calls"] is not None and metrics["joiner_calls"] > 0,
            "details": {"joinerCallCount": metrics["joiner_calls"]},
        },
        {
            "name": "no_decoded_tokens",
            "pass": metrics["decoded_token_count"] == 0,
            "details": {"decodedTokenCount": metrics["decoded_token_count"]},
        },
        {
            "name": "all_joiner_calls_blank",
            "pass": metrics["blank_tokens"] is not None
            and metrics["joiner_calls"] is not None
            and metrics["blank_tokens"] == metrics["joiner_calls"],
            "details": {
                "blankTokenCount": metrics["blank_tokens"],
                "joinerCallCount": metrics["joiner_calls"],
            },
        },
    ]
    return {
        "status": "frozen" if all(check["pass"] for check in checks) else "signature_mismatch",
        "checks": checks,
    }


def safe_stem(path: Path, metrics: dict[str, Any]) -> str:
    raw = metrics.get("run_id") or metrics.get("session_id") or path.stem
    return "".join(ch if ch.isalnum() or ch in {"-", "_"} else "_" for ch in raw) or "sherpa-no-output-baseline"


def copy_if_present(source: Path | None, destination: Path) -> str | None:
    if source is None:
        return None
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    return str(destination)


def write_json(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        json.dump(data, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate and archive a BlazeClaw Sherpa all-blank no-output regression baseline."
    )
    parser.add_argument("--baseline", required=True, type=Path, help="Path to *.sherpa-baseline.json")
    parser.add_argument("--debug-log", type=Path, help="Optional Visual Studio debug trace exported to a text file")
    parser.add_argument("--archive-dir", type=Path, help="Optional directory where baseline artifacts are copied")
    parser.add_argument("--output", type=Path, help="Optional summary JSON path")
    parser.add_argument(
        "--allow-missing-speech-active",
        action="store_true",
        help="Do not fail solely because the baseline JSON lacks speechActive telemetry",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        if not args.baseline.is_file():
            raise FileNotFoundError(f"baseline JSON not found: {args.baseline}")
        if args.debug_log is not None and not args.debug_log.is_file():
            raise FileNotFoundError(f"debug log not found: {args.debug_log}")

        baseline_data = load_json(args.baseline)
        metrics = extract_metrics(baseline_data)
        signature = evaluate_signature(metrics, require_speech_active=not args.allow_missing_speech_active)

        archive: dict[str, Any] = {}
        if args.archive_dir is not None:
            stem = safe_stem(args.baseline, metrics)
            archive_root = args.archive_dir / stem
            archive["directory"] = str(archive_root)
            archive["baselineJson"] = copy_if_present(args.baseline, archive_root / args.baseline.name)
            if args.debug_log is not None:
                archive["debugLog"] = copy_if_present(args.debug_log, archive_root / args.debug_log.name)

        summary = {
            "status": signature["status"],
            "createdAtUtc": datetime.now(timezone.utc).isoformat(),
            "sourceBaseline": str(args.baseline),
            "sourceDebugLog": str(args.debug_log) if args.debug_log else None,
            "metrics": metrics,
            "signature": signature,
            "archive": archive,
        }

        if args.output is not None:
            write_json(args.output, summary)
        elif args.archive_dir is not None:
            write_json(Path(archive["directory"]) / "freeze-summary.json", summary)

        print(f"status: {summary['status']}")
        print(f"decoded text: {metrics['decoded_text']}")
        print(f"final flush: {metrics['final_flush']}")
        print(f"speech active: {metrics['speech_active']}")
        print(f"encoder frames: {metrics['encoder_frames']}")
        print(f"joiner calls: {metrics['joiner_calls']}")
        print(f"blank tokens: {metrics['blank_tokens']}")
        print(f"decoded tokens: {metrics['decoded_token_count']}")
        if archive.get("directory"):
            print(f"archive: {archive['directory']}")

        return 0 if summary["status"] == "frozen" else 1
    except Exception as ex:
        print(f"error: {ex}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
