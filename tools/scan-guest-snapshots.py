#!/usr/bin/env python3
"""Find guest values that change steadily between memory snapshots.

A render-test route's `snapshot <frame> <name>` writes the guest's 512 MB of
physical memory to `<output>/<name>.mem`. Given three or more snapshots taken
at equal frame spacing, this lists the aligned big-endian floats (or 32-bit
integers) that move by the same step between each pair: a clock such as the
time of day advances that way, while most memory holds still or jitters.
Candidates are then confirmed with the route's `poke` command.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np


def load(path: Path, kind: str) -> np.ndarray:
    data = np.fromfile(path, dtype=">f4" if kind == "float" else ">u4")
    with np.errstate(invalid="ignore"):
        return data.astype(np.float64)


def scan(snapshots: list[np.ndarray], minimum: float, maximum: float, tolerance: float,
         increasing: bool, min_step: float) -> np.ndarray:
    """Word indices whose value steps by the same nonzero amount each time."""
    first = snapshots[0]
    mask = np.isfinite(first) & (first >= minimum) & (first <= maximum)
    steps = []
    for previous, current in zip(snapshots, snapshots[1:]):
        mask &= np.isfinite(current) & (current >= minimum) & (current <= maximum)
        step = current - previous
        steps.append(step)
        mask &= np.abs(step) >= min_step
        if increasing:
            mask &= step > 0
    reference = steps[0]
    for step in steps[1:]:
        mask &= np.abs(step - reference) <= tolerance * np.abs(reference)
    return np.nonzero(mask)[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("snapshots", type=Path, nargs="+",
                        help="three or more .mem files at equal frame spacing, in order")
    parser.add_argument("--kind", choices=("float", "int"), default="float")
    parser.add_argument("--min", type=float, default=-1e9)
    parser.add_argument("--max", type=float, default=1e9)
    parser.add_argument("--tolerance", type=float, default=0.05,
                        help="allowed relative difference between steps (default 0.05)")
    parser.add_argument("--increasing", action="store_true", help="only rising values")
    parser.add_argument("--min-step", type=float, default=1e-6,
                        help="smallest step between snapshots (default 1e-6)")
    parser.add_argument("--limit", type=int, default=60)
    args = parser.parse_args()
    if len(args.snapshots) < 3:
        print("error: need at least three snapshots", file=sys.stderr)
        return 1
    snapshots = [load(path, args.kind) for path in args.snapshots]
    with np.errstate(invalid="ignore", over="ignore"):
        indices = scan(snapshots, args.min, args.max, args.tolerance, args.increasing,
                       args.min_step)
    print(f"{len(indices)} candidates")
    # Group by step so that one clock's copies sort together.
    step = snapshots[1][indices] - snapshots[0][indices]
    order = np.argsort(np.abs(step))
    for index in indices[order][: args.limit]:
        values = " ".join(f"{snapshot[index]:.6g}" for snapshot in snapshots)
        print(f"{index * 4:08X}  step {snapshots[1][index] - snapshots[0][index]:.6g}  {values}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
