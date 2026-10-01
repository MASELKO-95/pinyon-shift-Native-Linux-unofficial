#!/usr/bin/env python3
"""Summarize opt-in CRITICAL_PATH events from a Pinyon Shift log."""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import statistics


MARKER = "CRITICAL_PATH "
SCHEMA = "pinyon-shift.critical-path-summary.v1"


def percentile(values: list[int], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return float(ordered[lower])
    return ordered[lower] * (upper - position) + ordered[upper] * (position - lower)


def read_events(path: pathlib.Path) -> list[dict[str, int | str]]:
    events = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        marker = line.find(MARKER)
        if marker < 0:
            continue
        try:
            event = json.loads(line[marker + len(MARKER) :])
        except json.JSONDecodeError as error:
            raise ValueError(f"line {line_number}: invalid critical-path JSON") from error
        required = {"event", "time_ns", "thread", "source_frame", "value0", "value1", "value2"}
        if set(event) != required:
            raise ValueError(f"line {line_number}: unexpected critical-path fields")
        events.append(event)
    if not events:
        raise ValueError("log has no CRITICAL_PATH events")
    return events


def read_event_logs(paths: list[pathlib.Path]) -> list[dict[str, int | str]]:
    events = [event for path in paths for event in read_events(path)]
    return sorted(events, key=lambda event: int(event["time_ns"]))


def summarize(events: list[dict[str, int | str]]) -> dict[str, object]:
    frames: dict[int, dict[str, list[dict[str, int | str]]]] = {}
    submissions: dict[int, dict[str, int]] = {}
    for event in events:
        frame = int(event["source_frame"])
        frames.setdefault(frame, {}).setdefault(str(event["event"]), []).append(event)
        if event["event"] in {"submission_begin", "submission_end"}:
            submissions.setdefault(int(event["value0"]), {})[str(event["event"])] = int(event["time_ns"])
        elif event["event"] == "gpu_completion":
            completed = int(event["value0"])
            for submission, record in submissions.items():
                if submission <= completed and "completion" not in record:
                    record["completion"] = int(event["time_ns"])

    source_times = sorted(
        (frame, int(items["source_frame"][0]["time_ns"]))
        for frame, items in frames.items()
        if items.get("source_frame")
    )
    frame_intervals = [later[1] - earlier[1] for earlier, later in zip(source_times, source_times[1:])]
    emitter_times = [
        sum(int(event["value0"]) for event in items.get("title_emitter", []))
        for items in frames.values()
        if items.get("title_emitter")
    ]
    tape_times = [
        sum(int(event["value0"]) for event in items.get("command_tape", []))
        for items in frames.values()
        if items.get("command_tape")
    ]
    vblank_lateness = [
        int(event["value0"])
        for event in events
        if event["event"] == "guest_vblank_deadline"
    ]
    dispatch_times = [
        int(event["value0"])
        for event in events
        if event["event"] == "guest_vblank_dispatch"
    ]
    submit_recording = [
        record["submission_end"] - record["submission_begin"]
        for record in submissions.values()
        if "submission_begin" in record and "submission_end" in record
    ]
    gpu_completion = [
        record["completion"] - record["submission_end"]
        for record in submissions.values()
        if "submission_end" in record and "completion" in record
    ]

    median_frame = statistics.median(frame_intervals) if frame_intervals else None
    median_emitter = statistics.median(emitter_times) if emitter_times else None
    title_threshold = max(2_000_000, int(median_frame * 0.08)) if median_frame else 2_000_000
    late_frames = sum(value >= 1_000_000 for value in vblank_lateness)
    return {
        "schema": SCHEMA,
        "events": len(events),
        "source_frames": len(source_times),
        "metrics_ns": {
            "frame_interval_median": median_frame,
            "frame_interval_p95": percentile(frame_intervals, 0.95),
            "title_emitter_median": median_emitter,
            "title_emitter_p95": percentile(emitter_times, 0.95),
            "command_tape_replay_median": statistics.median(tape_times) if tape_times else None,
            "command_tape_replay_p95": percentile(tape_times, 0.95),
            "submission_recording_median": statistics.median(submit_recording) if submit_recording else None,
            "gpu_completion_median": statistics.median(gpu_completion) if gpu_completion else None,
            "vblank_lateness_p95": percentile(vblank_lateness, 0.95),
            "vblank_dispatch_p95": percentile(dispatch_times, 0.95),
        },
        "gates": {
            "title_emitter": {
                "threshold_ns": title_threshold,
                "qualifies": median_emitter is not None and median_emitter >= title_threshold,
            },
            "command_tape": {
                "replay_upper_bound_only": True,
                "sampling_required": True,
            },
            "vblank": {
                "late_by_1ms_count": late_frames,
                "samples": len(vblank_lateness),
                "late_by_1ms_percent": (late_frames * 100.0 / len(vblank_lateness)) if vblank_lateness else None,
                "qualifies": bool(vblank_lateness) and late_frames * 100 >= len(vblank_lateness),
            },
            "vmx": {"sampling_required": True},
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=pathlib.Path, nargs="+")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    result = summarize(read_event_logs(args.log))
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
