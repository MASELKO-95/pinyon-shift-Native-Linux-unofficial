#!/usr/bin/env python3
"""Summarize consumed-swap frame times between two render-test captures."""

import argparse
import csv
import json
import math
import statistics
from pathlib import Path


def summarize(perf_path, events_path, start_name, end_name):
    captures = {}
    for line in events_path.read_text(encoding="utf-8").splitlines():
        event = json.loads(line)
        if event.get("event") == "fh1.render_test.capture":
            captures[event["name"]] = event
    first, last = captures[start_name], captures[end_name]
    if first.get("vehicle_pose_valid") != "1" or last.get("vehicle_pose_valid") != "1":
        raise ValueError("capture has no vehicle pose")
    start, end = int(first["trigger_output_frame"]), int(last["trigger_output_frame"])
    if start >= end:
        raise ValueError("capture boundaries are out of order")

    frame = 0
    durations = []
    simulation_ns = 0
    simulation_ticks = 0
    with perf_path.open(encoding="utf-8-sig", newline="") as stream:
        for row in csv.DictReader(stream):
            frame += int(row["source_frame_count"])
            if start <= frame < end and int(row["frame_time_us"]) > 0:
                durations.append(int(row["frame_time_us"]))
                simulation_ns += int(row["simulation_time_ns"])
                simulation_ticks += int(row["simulation_tick_count"])
    if frame < end or len(durations) < 2:
        raise ValueError("performance CSV does not cover the capture interval")
    durations.sort()
    first_pose = [float(first[key]) for key in ("vehicle_x", "vehicle_y", "vehicle_z")]
    last_pose = [float(last[key]) for key in ("vehicle_x", "vehicle_y", "vehicle_z")]
    return {
        "start_output_frame": start,
        "end_output_frame": end,
        "samples": len(durations),
        "wall_seconds": round(sum(durations) / 1_000_000, 3),
        "simulation_seconds": round(simulation_ns / 1_000_000_000, 3),
        "simulation_ticks": simulation_ticks,
        "start_pose": first_pose,
        "end_pose": last_pose,
        "distance_m": round(math.dist(first_pose, last_pose), 1),
        "median_frame_time_us": statistics.median(durations),
        "p95_frame_time_us": durations[int((len(durations) - 1) * 0.95)],
        "p99_frame_time_us": durations[int((len(durations) - 1) * 0.99)],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("perf_csv", type=Path)
    parser.add_argument("events_jsonl", type=Path)
    parser.add_argument("--start", default="race-moving")
    parser.add_argument("--end", default="race-sustained")
    args = parser.parse_args()
    print(json.dumps(summarize(args.perf_csv, args.events_jsonl, args.start, args.end), indent=2))
