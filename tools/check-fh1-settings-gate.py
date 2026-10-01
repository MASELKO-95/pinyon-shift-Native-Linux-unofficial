#!/usr/bin/env python3
"""Check an fh1-settings-gate render-test run against the NP-1 gates.

The route opens and closes the host SETTINGS screen 100 times, navigates it
with only the keyboard, only the pad and only the mouse, then resumes free
roam and drives. The run passes when every open is matched by a release of
the drawer, input listener and guest input capture, each input method
reached the DISPLAY page, every recorded layout lies inside the title's
90 % safe area, no setting was saved, and the car moved after the menu
closed (the guest has its input back).
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path

CYCLES = 100
NAVIGATIONS = 3  # keyboard, pad, mouse
MINIMUM_DRIVE = 20.0  # world units between the paused and driving captures


def load_events(state: Path) -> list[dict]:
    logs = sorted((state / "logs").glob("*.jsonl"))
    if not logs:
        raise SystemExit(f"no event log under {state / 'logs'}")
    return [json.loads(line) for line in logs[-1].read_text(encoding="utf-8").splitlines() if line]


def check(state: Path) -> list[str]:
    events = load_events(state)
    failures = []
    names = [event.get("event") for event in events]
    opens, closes = names.count("hostui.open"), names.count("hostui.closed")
    expected = CYCLES + NAVIGATIONS
    if opens != expected or closes != expected:
        failures.append(f"expected {expected} opens and releases, saw {opens} and {closes}")
    displays = sum(1 for event in events
                   if event.get("event") == "hostui.screen" and event.get("screen") == "DISPLAY")
    if displays != NAVIGATIONS:
        failures.append(f"expected DISPLAY reached {NAVIGATIONS} times, saw {displays}")
    layouts = [event for event in events if event.get("event") == "hostui.layout"]
    if not layouts:
        failures.append("no hostui.layout event")
    for layout in layouts:
        if layout.get("inside") != "1":
            failures.append(f"{layout.get('screen')} drew {layout.get('content')} "
                            f"outside the safe area {layout.get('safe')}")
    captures = {event.get("name"): event for event in events
                if event.get("event") == "fh1.render_test.capture"}
    try:
        paused, driving = captures["paused"], captures["driving"]
        moved = math.dist([float(paused[f"vehicle_{axis}"]) for axis in "xyz"],
                          [float(driving[f"vehicle_{axis}"]) for axis in "xyz"])
        if moved < MINIMUM_DRIVE:
            failures.append(f"the car moved {moved:.1f} units after the menu closed")
    except (KeyError, ValueError):
        failures.append("paused and driving captures with vehicle poses are missing")
    started = next((event.get("utc") for event in events if event.get("event") == "process.start"),
                   None)
    if started:
        start = datetime.strptime(started, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        for backup in (state / "config" / "backups").glob("*.toml"):
            if backup.stat().st_mtime >= start.timestamp():
                failures.append(f"settings were saved during the run ({backup.name})")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("state", type=Path, help="the run's private state directory")
    failures = check(parser.parse_args().state)
    for failure in failures:
        print(f"FAIL: {failure}")
    if not failures:
        print("settings gate passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
