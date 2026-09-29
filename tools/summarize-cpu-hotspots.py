#!/usr/bin/env python3
"""Rank sampled CPU and wait cost by source frame from normalized WPA exports."""

import argparse
import bisect
import csv
import json
import statistics
from collections import defaultdict
from pathlib import Path


def rows(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        yield from csv.DictReader(stream)


def percentile(values, fraction):
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int((len(ordered) - 1) * fraction))]


def summarize(markers_path, samples_path, waits_path=None, start_frame=None, end_frame=None,
              ready_path=None):
    markers = sorted(
        (float(row["timestamp_ms"]), int(row["source_frame"]))
        for row in rows(markers_path)
    )
    if not markers:
        raise ValueError("markers CSV contains no source frames")
    if start_frame is not None and end_frame is not None and start_frame > end_frame:
        raise ValueError("start frame is after end frame")
    times = [item[0] for item in markers]
    ids = [item[1] for item in markers]
    if any(next_id != frame + 1 for frame, next_id in zip(ids, ids[1:])):
        raise ValueError("source-frame markers contain a duplicate or gap")

    def frame_at(timestamp):
        index = bisect.bisect_right(times, timestamp) - 1
        if index < 0 or index + 1 == len(markers):
            return None
        frame = markers[index][1]
        if start_frame is not None and frame < start_frame:
            return None
        if end_frame is not None and frame > end_frame:
            return None
        return frame

    frames = defaultdict(lambda: {"cpu_ms": 0.0, "wait_ms": 0.0})
    for index, (_, frame) in enumerate(markers[:-1]):
        if (start_frame is None or frame >= start_frame) and (end_frame is None or frame <= end_frame):
            frames[frame]["interval_ms"] = times[index + 1] - times[index]
    functions = defaultdict(float)
    modules = defaultdict(float)
    project_callers = defaultdict(float)
    threads = defaultdict(float)
    wait_threads = defaultdict(float)
    waits = defaultdict(float)
    # Guest code (map-generated-lines.py), waking threads and scheduler delay.
    guest_functions = defaultdict(float)
    guest_addresses = defaultdict(float)
    wakers = defaultdict(float)
    ready_by_thread = defaultdict(list)
    unmatched = {"samples": 0, "waits": 0}

    for row in rows(samples_path):
        frame = frame_at(float(row["timestamp_ms"]))
        if frame is None:
            unmatched["samples"] += 1
            continue
        cost = float(row["cpu_ms"])
        frames[frame]["cpu_ms"] += cost
        modules[row["module"] or "<unknown>"] += cost
        functions[f"{row['module']}!{row['function']}"] += cost
        if row.get("project_caller"):
            project_callers[row["project_caller"]] += cost
        if row.get("thread_id"):
            threads[row["thread_id"]] += cost
        if row.get("guest_function"):
            guest_functions[row["guest_function"]] += cost
            guest_addresses[f"{row['guest_function']}+{row['guest_address']}"] += cost

    if waits_path:
        for row in rows(waits_path):
            start = float(row["timestamp_ms"])
            end = start + float(row["wait_ms"])
            if end <= start:
                unmatched["waits"] += 1
                continue
            index = max(0, bisect.bisect_right(times, start) - 1)
            attributed = False
            while index + 1 < len(markers) and times[index] < end:
                frame = frame_at(max(start, times[index]))
                cost = max(0.0, min(end, times[index + 1]) - max(start, times[index]))
                if frame is not None and cost:
                    frames[frame]["wait_ms"] += cost
                    waits[row["wait_reason"] or "<unknown>"] += cost
                    if row.get("thread_id"):
                        wait_threads[row["thread_id"]] += cost
                    if row.get("waker_thread_id"):
                        waker = row["waker_thread_id"]
                        if row.get("waker_in_process") == "0":
                            waker = f"{waker} (other process)"
                        wakers[waker] += cost
                    attributed = True
                index += 1
            if not attributed:
                unmatched["waits"] += 1

    if ready_path:
        for row in rows(ready_path):
            if frame_at(float(row["timestamp_ms"])) is None:
                continue
            ready_by_thread[row["thread_id"]].append(float(row["ready_ms"]))

    def ranked(values):
        return [
            {"name": name, "ms": round(cost, 3)}
            for name, cost in sorted(values.items(), key=lambda item: item[1], reverse=True)
        ]

    cpu = [value["cpu_ms"] for value in frames.values()]
    wait = [value["wait_ms"] for value in frames.values()]
    intervals = [value["interval_ms"] for value in frames.values()]
    per_frame = [
        {"source_frame": frame, "cpu_ms": round(value["cpu_ms"], 3),
         "wait_ms": round(value["wait_ms"], 3),
         "interval_ms": round(value["interval_ms"], 3)}
        for frame, value in sorted(frames.items())
    ]
    return {
        "schema": "pinyon-shift.cpu-hotspots.v1",
        "source_frames": len(frames),
        "unmatched_rows": unmatched,
        "per_frame_ms": {
            "source_median": round(statistics.median(intervals), 3) if intervals else 0.0,
            "source_p95": round(percentile(intervals, 0.95), 3),
            "cpu_median": round(statistics.median(cpu), 3) if cpu else 0.0,
            "cpu_p95": round(percentile(cpu, 0.95), 3),
            "wait_median": round(statistics.median(wait), 3) if wait else 0.0,
            "wait_p95": round(percentile(wait, 0.95), 3),
        },
        "top_functions": ranked(functions),
        "top_modules": ranked(modules),
        "top_project_callers": ranked(project_callers),
        "top_threads": ranked(threads),
        "top_wait_threads": ranked(wait_threads),
        "top_wait_reasons": ranked(waits),
        "top_wakers": ranked(wakers),
        "top_guest_functions": ranked(guest_functions),
        "top_guest_addresses": ranked(guest_addresses),
        # Scheduler delay (ready to running) per thread, by total.
        "ready_delay_threads": [
            {"name": thread, "ms": round(sum(delays), 3),
             "median_ms": round(statistics.median(delays), 4),
             "p95_ms": round(percentile(delays, 0.95), 4), "count": len(delays)}
            for thread, delays in sorted(ready_by_thread.items(),
                                         key=lambda item: sum(item[1]), reverse=True)
        ],
        "frames": per_frame,
    }


def markdown(report, limit=20):
    timing = report["per_frame_ms"]
    lines = [
        "# CPU hotspot report",
        "",
        f"Frames: {report['source_frames']}",
        f"Source-frame interval: median {timing['source_median']:.3f} ms, p95 {timing['source_p95']:.3f} ms",
        f"CPU/frame: median {timing['cpu_median']:.3f} ms, p95 {timing['cpu_p95']:.3f} ms",
        f"Wait/frame: median {timing['wait_median']:.3f} ms, p95 {timing['wait_p95']:.3f} ms",
        "Wait totals sum blocked time across all game threads; they are not frame latency.",
    ]
    for title, key in (
        ("Functions", "top_functions"),
        ("Modules", "top_modules"),
        ("Project callers (inclusive)", "top_project_callers"),
        ("Threads", "top_threads"),
        ("Blocked threads", "top_wait_threads"),
        ("Wait reasons", "top_wait_reasons"),
        ("Threads that woke waiters", "top_wakers"),
        ("Guest functions", "top_guest_functions"),
        ("Guest instructions", "top_guest_addresses"),
    ):
        lines += ["", f"## {title}", "", "| Rank | Name | Total ms |", "|---:|---|---:|"]
        lines += [
            f"| {index} | `{item['name']}` | {item['ms']:.3f} |"
            for index, item in enumerate(report[key][:limit], 1)
        ]
    if report["ready_delay_threads"]:
        lines += ["", "## Scheduler delay (ready to running)", "",
                  "| Rank | Thread | Total ms | Median ms | p95 ms | Count |",
                  "|---:|---|---:|---:|---:|---:|"]
        lines += [
            f"| {index} | `{item['name']}` | {item['ms']:.3f} | {item['median_ms']:.4f} | "
            f"{item['p95_ms']:.4f} | {item['count']} |"
            for index, item in enumerate(report["ready_delay_threads"][:limit], 1)
        ]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("markers", type=Path)
    parser.add_argument("samples", type=Path)
    parser.add_argument("--waits", type=Path)
    parser.add_argument("--ready", type=Path, help="ready.csv of profile-etl-export")
    parser.add_argument("--start-frame", type=int)
    parser.add_argument("--end-frame", type=int)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = summarize(args.markers, args.samples, args.waits, args.start_frame, args.end_frame,
                       args.ready)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.output.with_suffix(".md").write_text(markdown(report), encoding="utf-8")


if __name__ == "__main__":
    main()
