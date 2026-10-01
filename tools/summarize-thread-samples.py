#!/usr/bin/env python3
"""Summarize pinyon_shift_thread_sampler output for a time window.

Every sample is one wall-clock tick of its thread, so the shares are of the
thread's wall time; samples whose leaf is a kernel wait are reported as
waiting. `--weight cycles` weighs each sample by the CPU cycles the thread
used since its previous sample instead, which suits threads that are busy
throughout but misattributes a bursty thread's work to the wait it is found
in. The report ranks functions by exclusive (leaf) and inclusive (anywhere on
the stack) share, by source line too when the sampler recorded leaf lines, and
lists modules by exclusive share. Without a window it
prints a per-second timeline of each thread's cycles to choose one.
"""

from __future__ import annotations

import argparse
import collections
import csv
import json
import sys
from pathlib import Path


def load(directory: Path):
    threads = {}
    with (directory / "threads.csv").open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            threads[row["thread"]] = row["name"]
    stacks = {}
    with (directory / "stacks.csv").open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            stacks[row["stack_id"]] = row["stack"].split(";")
    samples = []
    with (directory / "samples.csv").open(encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            samples.append((float(row["t_ms"]), row["thread"], int(row["cycles"]), row["stack_id"]))
    return threads, stacks, samples


WAIT_LEAVES = (
    "ZwWaitForAlertByThreadId",
    "NtWaitForAlertByThreadId",
    "NtDelayExecution",
    "ZwDelayExecution",
    "NtWaitForSingleObject",
    "ZwWaitForSingleObject",
    "NtWaitForMultipleObjects",
    "ZwWaitForMultipleObjects",
    "NtYieldExecution",
    "ZwYieldExecution",
    "NtSignalAndWaitForSingleObject",
    "ZwSignalAndWaitForSingleObject",
    "NtRemoveIoCompletion",
    "ZwRemoveIoCompletion",
    "NtWaitForWorkViaWorkerFactory",
    "ZwWaitForWorkViaWorkerFactory",
    "NtWaitForKeyedEvent",
    "ZwWaitForKeyedEvent",
)


def function(frame: str) -> str:
    """The frame without the source line the sampler's --lines adds to leaves."""
    return frame.split(" @", 1)[0]


def is_wait(frame: str) -> bool:
    return function(frame).split("!", 1)[-1] in WAIT_LEAVES


def summarize(threads, stacks, samples, start_s, end_s, top, focus=None, weight="samples"):
    report = {"window_s": [start_s, end_s], "threads": {}}
    for thread_id, name in threads.items():
        exclusive = collections.Counter()
        exclusive_lines = collections.Counter()
        inclusive = collections.Counter()
        modules = collections.Counter()
        total = 0
        count = 0
        waiting = 0
        for t_ms, thread, cycles, stack_id in samples:
            if thread != thread_id or not start_s * 1000 <= t_ms < end_s * 1000:
                continue
            frames = stacks[stack_id]
            if focus and not any(focus in frame for frame in frames):
                continue
            value = cycles if weight == "cycles" else 1
            total += value
            count += 1
            if is_wait(frames[-1]):
                waiting += value
            exclusive[function(frames[-1])] += value
            exclusive_lines[frames[-1]] += value
            modules[frames[-1].split("!", 1)[0]] += value
            for frame in {function(frame) for frame in frames}:
                inclusive[frame] += value
        if not total:
            continue

        def ranked(counter):
            return [
                {"name": name_, "percent": round(100.0 * value / total, 2)}
                for name_, value in counter.most_common(top)
            ]

        report["threads"][name] = {
            "thread_id": thread_id,
            "samples": count,
            "weight": weight,
            "waiting_percent": round(100.0 * waiting / total, 2),
            "exclusive": ranked(exclusive),
            "exclusive_lines": ranked(exclusive_lines),
            "inclusive": ranked(inclusive),
            "modules": ranked(modules),
        }
    return report


def timeline(threads, samples):
    per_second = collections.defaultdict(collections.Counter)
    for t_ms, thread, cycles, _ in samples:
        per_second[int(t_ms // 1000)][thread] += cycles
    names = list(threads)
    print("second," + ",".join(threads[t] for t in names) + " (Gcycles)")
    for second in sorted(per_second):
        print(f"{second}," + ",".join(f"{per_second[second][t] / 1e9:.2f}" for t in names))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--from-s", type=float)
    parser.add_argument("--to-s", type=float)
    parser.add_argument("--top", type=int, default=40)
    parser.add_argument("--focus", help="only samples with a frame containing this text")
    parser.add_argument("--weight", choices=("samples", "cycles"), default="samples")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    threads, stacks, samples = load(args.directory)
    if args.from_s is None or args.to_s is None:
        timeline(threads, samples)
        return 0
    report = summarize(
        threads, stacks, samples, args.from_s, args.to_s, args.top, args.focus, args.weight
    )
    text = json.dumps(report, indent=2)
    if args.output:
        args.output.write_text(text + "\n", encoding="utf-8")
    for name, data in report["threads"].items():
        print(f"== {name} ({data['samples']} samples, {data['waiting_percent']}% waiting)")
        kinds = ["inclusive", "exclusive", "modules"]
        if data["exclusive_lines"] != data["exclusive"]:
            kinds.insert(2, "exclusive_lines")
        for kind in kinds:
            print(f"-- {kind}")
            for entry in data[kind]:
                print(f"  {entry['percent']:6.2f}  {entry['name'][:160]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
