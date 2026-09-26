"""Summarize the consumed draw/resolve order of one captured race frame."""

import csv
import json
import math
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
    state_draws = 0
    texture_keys = 0
    outdated_texture_keys = 0
    if "texture_versions" in rows[0]:
        for row in draws:
            viewport = [float(value) for value in row["viewport"].split(":")]
            scissor = [int(value) for value in row["scissor"].split(":")]
            if len(viewport) != 6 or not all(map(math.isfinite, viewport)) or len(scissor) != 4:
                raise ValueError(f"invalid draw state at ordinal {row['sequence']}")
            versions = row["texture_versions"].split(";") if row["texture_versions"] else []
            keys = [[int(value) for value in version.split(":")] for version in versions]
            if (any(len(key) != 10 for key in keys) or len(keys) > 32
                    or len(keys) != int(row["texture_fetches"])):
                raise ValueError(f"invalid texture keys at ordinal {row['sequence']}")
            if sum(key[7] != 0 and key[8] != 0 for key in keys) != int(row["versioned_textures"]):
                raise ValueError(f"texture count mismatch at ordinal {row['sequence']}")
            state_draws += 1
            texture_keys += len(keys)
            outdated_texture_keys += sum(key[9] != 0 for key in keys)
    ui = [row for row in draws
          if int(row["surface"]) == 0x14000500
          and int(row["color"]) == 0xA0000
          and int(row["target_bits"]) == 2]
    ui_replay_source = 0
    if "ui_replay_source_frame" in rows[0]:
        sources = {int(row["ui_replay_source_frame"]) for row in rows}
        if len(sources) != 1:
            raise ValueError("inconsistent retained UI source")
        ui_replay_source = sources.pop()
        if ui_replay_source and not (path.parent / f"ordered-ui-{ui_replay_source}.bin").is_file():
            raise ValueError(f"missing retained UI fixture for frame {ui_replay_source}")
    gaps = [(left + 1, right - 1) for left, right in zip(sequences, sequences[1:])
            if right > left + 1]
    targets = Counter((row["surface"], row["color"], row["depth"],
                       row["target_bits"]) for row in draws)
    return {
        "events": len(rows), "draws": len(draws), "copies": len(copies),
        "clears": len(clears),
        "ui_draws": len(ui), "missing_final": len(missing_final),
        "ui_replay_source_frame": ui_replay_source,
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
        "state_draws": state_draws, "texture_keys": texture_keys,
        "outdated_texture_keys": outdated_texture_keys,
    }


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify-ordered-frame.py ordered-frame-N.csv")
    result = verify(Path(sys.argv[1]))
    print(json.dumps(result, indent=2))
    if result["missing_final"] or result["failed_copies"]:
        raise SystemExit(1)
