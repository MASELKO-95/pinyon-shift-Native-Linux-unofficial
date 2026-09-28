"""Tabulate stencil use per depth surface from FH1 frame census records.

Input is the JSON Lines file written with `--fh1_frame_census=true
--fh1_frame_census_path=<file>`. For every depth surface (base tile, format,
MSAA and pitch) the report counts the draws that enable stencil and the draws
that may leave a nonzero stencil value, using the same rule as the native
executor's DrawMayWriteNonzeroStencil: a face writes when its write mask is
nonzero and a fail, depth-fail or pass op is neither KEEP, ZERO nor a REPLACE
of zero. Depth transfers can skip their eight stencil-bit passes only for
sources no such draw touched, so the per-window column shows how often a
surface stays provably zero.
"""

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path


SCHEMA = "pinyon-shift.fh1-frame-census.v1"
MSAA = {0: "1x", 1: "2x", 2: "4x"}
STENCIL_OPS = ("keep", "zero", "replace", "incr_sat", "decr_sat", "invert",
               "incr_wrap", "decr_wrap")
KEEP, ZERO, REPLACE = 0, 1, 2


def depth_surface(surface: dict) -> str | None:
    if not surface["bound"] & 1:
        return None
    info = int(surface["surface_info"], 16)
    depth = int(surface["depth_info"], 16)
    return (f"{'D24FS8' if (depth >> 16) & 1 else 'D24S8'}@{depth & 0xFFF}"
            f"/{MSAA.get((info >> 16) & 3, str((info >> 16) & 3))}"
            f"/pitch{info & 0x3FFF}")


def face_writes_nonzero(ref_mask: int, fail: int, zpass: int, zfail: int) -> bool:
    reference = ref_mask & 0xFF
    write_mask = (ref_mask >> 16) & 0xFF
    if not write_mask:
        return False
    reference &= write_mask
    for op in (fail, zpass, zfail):
        if op in (KEEP, ZERO):
            continue
        if op == REPLACE and not reference:
            continue
        return True
    return False


def classify(state: dict) -> dict:
    """Stencil use of one draw state; `writes` is None when unknown."""
    control = int(state.get("depth_control", "0"), 16)
    if not control & 1:
        return {"enabled": False, "writes": False, "key": None}
    front_mask = int(state.get("stencil_ref_mask", "0"), 16)
    front = ((control >> 11) & 7, (control >> 14) & 7, (control >> 17) & 7)
    writes = face_writes_nonzero(front_mask, *front)
    backface = bool((control >> 7) & 1)
    back = ((control >> 23) & 7, (control >> 26) & 7, (control >> 29) & 7)
    back_mask_text = state.get("stencil_ref_mask_bf")
    if backface and not writes:
        # Censuses recorded before the back-face mask was added cannot rule
        # the back face out.
        writes = (None if back_mask_text is None
                  else face_writes_nonzero(int(back_mask_text, 16), *back))
    key = (f"ref={front_mask & 0xFF:02X} write={(front_mask >> 16) & 0xFF:02X} "
           f"fail/pass/zfail={'/'.join(STENCIL_OPS[op] for op in front)}")
    if backface:
        key += f" bf={'/'.join(STENCIL_OPS[op] for op in back)}"
    return {"enabled": True, "writes": writes, "key": key}


def summarize(records, first_frame=None, last_frame=None) -> dict:
    surfaces = defaultdict(lambda: {
        "draws": 0, "stencil_draws": 0, "writing_draws": 0, "unknown_draws": 0,
        "windows": 0, "writing_windows": 0, "writing_states": defaultdict(int),
    })
    windows = 0
    for record in records:
        if first_frame is not None and record["last_frame"] < first_frame:
            continue
        if last_frame is not None and record["first_frame"] > last_frame:
            continue
        windows += 1
        ids = {surface["id"]: depth_surface(surface) for surface in record["surfaces"]}
        seen, written = set(), set()
        for state in record["draw_states"]:
            key = ids.get(state.get("surface"))
            if key is None:
                continue
            entry = surfaces[key]
            draws = state["draws"]
            use = classify(state)
            entry["draws"] += draws
            seen.add(key)
            if not use["enabled"]:
                continue
            entry["stencil_draws"] += draws
            if use["writes"] is None:
                entry["unknown_draws"] += draws
                written.add(key)
            elif use["writes"]:
                entry["writing_draws"] += draws
                entry["writing_states"][use["key"]] += draws
                written.add(key)
        for key in seen:
            surfaces[key]["windows"] += 1
            surfaces[key]["writing_windows"] += key in written
    rows = []
    for key, entry in sorted(surfaces.items(), key=lambda item: -item[1]["draws"]):
        states = sorted(entry["writing_states"].items(), key=lambda item: -item[1])
        rows.append({
            "depth_surface": key,
            **{name: entry[name] for name in ("draws", "stencil_draws", "writing_draws",
                                               "unknown_draws", "windows", "writing_windows")},
            "writing_states": [{"state": state, "draws": draws} for state, draws in states],
        })
    return {"windows": windows, "surfaces": rows}


def markdown(summary: dict, top_states: int) -> str:
    lines = [
        f"Stencil use per depth surface over {summary['windows']} census windows.",
        "",
        "| Depth surface | Draws | Stencil on | May write nonzero | Unknown | "
        "Windows writing / present |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in summary["surfaces"]:
        lines.append(
            f"| {row['depth_surface']} | {row['draws']} | {row['stencil_draws']} | "
            f"{row['writing_draws']} | {row['unknown_draws']} | "
            f"{row['writing_windows']} / {row['windows']} |")
    for row in summary["surfaces"]:
        if not row["writing_states"]:
            continue
        lines += ["", f"Writing states on {row['depth_surface']}:", ""]
        for state in row["writing_states"][:top_states]:
            lines.append(f"- {state['state']}: {state['draws']} draws")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("census", type=Path, nargs="+")
    parser.add_argument("--first-frame", type=int,
                        help="skip windows that end before this source frame")
    parser.add_argument("--last-frame", type=int,
                        help="skip windows that start after this source frame")
    parser.add_argument("--top-states", type=int, default=8)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    records = []
    for path in args.census:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                record = json.loads(line)
                if record.get("schema") != SCHEMA:
                    raise ValueError(f"unexpected census schema in {path}")
                records.append(record)
    summary = summarize(records, args.first_frame, args.last_frame)
    if args.json:
        args.json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    text = markdown(summary, args.top_states)
    if args.markdown:
        args.markdown.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
