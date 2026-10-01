#!/usr/bin/env python3
"""Attribute CPU samples in generated code to guest instructions (NP-3.0).

`profile-etl-export` writes each project sample's source file and line. For
the recompiled code those are lines of the generated `pinyon_shift_recomp.*.cpp`
files, where every guest instruction is one `// <disassembly>` comment
followed by its C++ and a function starts at `DEFINE_REX_FUNC(sub_<address>)`
and branch targets at `loc_<address>:`. This adds `guest_function` and
`guest_address` columns to `samples.csv` for samples in generated code.

The generated sources must be the ones the profiled build compiled: the
capture's `capture.json` records the codegen fingerprint, and a different
fingerprint is refused unless `--force` is given.
"""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path

FUNCTION = re.compile(r"^DEFINE_REX_FUNC\(sub_([0-9A-Fa-f]{8})\)")
LABEL = re.compile(r"^loc_([0-9A-Fa-f]{8}):")
INSTRUCTION = re.compile(r"^\t// \S")


def index_generated_file(path: Path) -> list[tuple[int, int, int]]:
    """Return (line, function, address) at every guest instruction comment."""
    entries = []
    function = address = None
    with path.open(encoding="utf-8", errors="replace") as stream:
        for number, line in enumerate(stream, 1):
            match = FUNCTION.match(line)
            if match:
                function = address = int(match.group(1), 16)
                continue
            match = LABEL.match(line)
            if match and function is not None:
                address = int(match.group(1), 16)
                continue
            if function is not None and INSTRUCTION.match(line):
                entries.append((number, function, address))
                address += 4
    return entries


def locate(entries: list[tuple[int, int, int]], line: int) -> tuple[int, int] | None:
    """The instruction a line of C++ belongs to: the last comment at or above it."""
    low, high = 0, len(entries)
    while low < high:
        middle = (low + high) // 2
        if entries[middle][0] <= line:
            low = middle + 1
        else:
            high = middle
    if low == 0:
        return None
    _, function, address = entries[low - 1]
    return function, address


def codegen_fingerprint(generated_dir: Path) -> str | None:
    stamp = generated_dir / "codegen.stamp"
    try:
        return json.loads(stamp.read_text(encoding="utf-8")).get("fingerprint")
    except (OSError, ValueError):
        return None


def map_samples(samples_path: Path, generated_dir: Path) -> dict[str, int]:
    indexes: dict[str, list[tuple[int, int, int]] | None] = {}
    with samples_path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        fields = list(reader.fieldnames or [])
        rows = list(reader)
    for column in ("guest_function", "guest_address"):
        if column not in fields:
            fields.append(column)
    counts = {"samples": len(rows), "generated": 0, "mapped": 0}
    for row in rows:
        row["guest_function"] = row["guest_address"] = ""
        source = row.get("source_file", "")
        name = source.replace("\\", "/").rsplit("/", 1)[-1]
        if not name.startswith("pinyon_shift_recomp.") or not row.get("source_line"):
            continue
        counts["generated"] += 1
        if name not in indexes:
            path = generated_dir / name
            indexes[name] = index_generated_file(path) if path.is_file() else None
        entries = indexes[name]
        found = locate(entries, int(row["source_line"])) if entries else None
        if found:
            row["guest_function"] = f"sub_{found[0]:08X}"
            row["guest_address"] = f"{found[1]:08X}"
            counts["mapped"] += 1
    temporary = samples_path.with_suffix(".csv.tmp")
    with temporary.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, quoting=csv.QUOTE_ALL)
        writer.writeheader()
        writer.writerows(rows)
    temporary.replace(samples_path)
    return counts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("capture", type=Path, help="capture directory with samples.csv")
    parser.add_argument("--generated-dir", type=Path, default=Path(".local/generated/default"))
    parser.add_argument("--force", action="store_true",
                        help="map even when the codegen fingerprint differs from the capture's")
    args = parser.parse_args(argv)
    manifest = args.capture / "capture.json"
    recorded = None
    if manifest.is_file():
        recorded = json.loads(manifest.read_text(encoding="utf-8-sig")).get("codegen_fingerprint")
    current = codegen_fingerprint(args.generated_dir)
    if not args.force and recorded != current:
        print(f"map-generated-lines: the capture's codegen fingerprint ({recorded}) is not the "
              f"generated sources' ({current}); regenerate them or pass --force",
              file=sys.stderr)
        return 2
    counts = map_samples(args.capture / "samples.csv", args.generated_dir)
    print(json.dumps(counts, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
