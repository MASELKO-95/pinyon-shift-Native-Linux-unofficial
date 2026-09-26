"""Summarize the consumed draw/resolve order of one captured race frame."""

import csv
import json
import sys
from collections import Counter
from pathlib import Path


def verify(path: Path) -> dict:
    with path.open(newline="") as file:
        rows = list(csv.DictReader(file))
    if not rows or len(rows) > 8192:
        raise ValueError("missing or oversized frame stream")
    if any(int(row["capture_rejected"]) for row in rows):
        raise ValueError("frame collector rejected one or more events")
    sequences = [int(row["sequence"]) for row in rows]
    if sequences != sorted(set(sequences)):
        raise ValueError("duplicate or unordered command ordinals")
    kinds = Counter(row["kind"] for row in rows)
    if set(kinds) - {"D", "C", "K"}:
        raise ValueError("unknown frame event")
    draws = [row for row in rows if row["kind"] == "D"]
    copies = [row for row in rows if row["kind"] == "C"]
    clears = [row for row in rows if row["kind"] == "K"]
    for row in clears:
        count = int(row["rectangle_count"])
        if count not in (1, 2) or int(row["clear_mode"]) > 2 or not int(row["clear_flags"]) & 7:
            raise ValueError(f"invalid clear at ordinal {row['sequence']}")
        for index in range(count):
            bounds = [int(value) for value in row[f"bounds{index}"].split(":")]
            color = [float(value) for value in row[f"color{index}"].split(":")]
            if len(bounds) != 4 or len(color) != 4:
                raise ValueError(f"invalid clear rectangle at ordinal {row['sequence']}")
            float(row[f"depth{index}"])
    missing_final = [int(row["sequence"]) for row in draws
                     if int(row["final_seen"]) != 1]
    ui = [row for row in draws
          if int(row["surface"]) == 0x14000500
          and int(row["color"]) == 0xA0000
          and int(row["target_bits"]) == 2]
    gaps = [(left + 1, right - 1) for left, right in zip(sequences, sequences[1:])
            if right > left + 1]
    targets = Counter((row["surface"], row["color"], row["depth"],
                       row["target_bits"]) for row in draws)
    return {
        "events": len(rows), "draws": len(draws), "copies": len(copies),
        "clears": len(clears),
        "ui_draws": len(ui), "missing_final": len(missing_final),
        "missing_final_ordinals": missing_final[:32],
        "failed_copies": sum(int(row["succeeded"]) != 1 for row in copies),
        "first_ordinal": sequences[0], "last_ordinal": sequences[-1],
        "gap_count": sum(right - left + 1 for left, right in gaps),
        "gap_ranges": gaps[:32], "target_count": len(targets),
        "top_targets": [dict(surface=int(target[0]), color=int(target[1]),
                             depth=int(target[2]), bits=int(target[3]), draws=count)
                        for target, count in targets.most_common(8)],
        "versioned_texture_bindings": sum(int(row["versioned_textures"])
                                           for row in draws),
    }


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify-ordered-frame.py ordered-frame-N.csv")
    result = verify(Path(sys.argv[1]))
    print(json.dumps(result, indent=2))
    if result["missing_final"] or result["failed_copies"]:
        raise SystemExit(1)
