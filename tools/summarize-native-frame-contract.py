"""Summarize FH1 frame census records into the native frame contract.

Input is the JSON Lines file written with `--fh1_frame_census=true
--fh1_frame_census_path=<file>`. Windows are grouped into modes with
`--mode label:first-last` (source-frame ranges, inclusive); windows outside
every range fall into `unlabelled`. The report lists, per mode, every render
surface, primitive type, texture layout, resolve and front-buffer format the
native renderer must implement, plus memexport/occlusion use and overflow.
"""

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path


TEXTURE_FORMATS = {
    0: "1_REVERSE", 1: "1", 2: "8", 3: "1_5_5_5", 4: "5_6_5", 5: "6_5_5",
    6: "8_8_8_8", 7: "2_10_10_10", 8: "8_A", 9: "8_B", 10: "8_8",
    11: "Cr_Y1_Cb_Y0_REP", 12: "Y1_Cr_Y0_Cb_REP", 13: "16_16_EDRAM",
    14: "8_8_8_8_A", 15: "4_4_4_4", 16: "10_11_11", 17: "11_11_10", 18: "DXT1",
    19: "DXT2_3", 20: "DXT4_5", 21: "16_16_16_16_EDRAM", 22: "24_8",
    23: "24_8_FLOAT", 24: "16", 25: "16_16", 26: "16_16_16_16", 27: "16_EXPAND",
    28: "16_16_EXPAND", 29: "16_16_16_16_EXPAND", 30: "16_FLOAT",
    31: "16_16_FLOAT", 32: "16_16_16_16_FLOAT", 33: "32", 34: "32_32",
    35: "32_32_32_32", 36: "32_FLOAT", 37: "32_32_FLOAT", 38: "32_32_32_32_FLOAT",
    39: "32_AS_8", 40: "32_AS_8_8", 41: "16_MPEG", 42: "16_16_MPEG",
    43: "8_INTERLACED", 44: "32_AS_8_INTERLACED", 45: "32_AS_8_8_INTERLACED",
    46: "16_INTERLACED", 47: "16_MPEG_INTERLACED", 48: "16_16_MPEG_INTERLACED",
    49: "DXN", 50: "8_8_8_8_AS_16_16_16_16", 51: "DXT1_AS_16_16_16_16",
    52: "DXT2_3_AS_16_16_16_16", 53: "DXT4_5_AS_16_16_16_16",
    54: "2_10_10_10_AS_16_16_16_16", 55: "10_11_11_AS_16_16_16_16",
    56: "11_11_10_AS_16_16_16_16", 57: "32_32_32_FLOAT", 58: "DXT3A", 59: "DXT5A",
    60: "CTX1", 61: "DXT3A_AS_1_1_1_1", 62: "8_8_8_8_GAMMA_EDRAM",
    63: "2_10_10_10_FLOAT_EDRAM",
}
COLOR_TARGET_FORMATS = {
    0: "8_8_8_8", 1: "8_8_8_8_GAMMA", 2: "2_10_10_10", 3: "2_10_10_10_FLOAT",
    4: "16_16", 5: "16_16_16_16", 6: "16_16_FLOAT", 7: "16_16_16_16_FLOAT",
    10: "2_10_10_10_AS_10_10_10_10", 12: "2_10_10_10_FLOAT_AS_16_16_16_16",
    14: "32_FLOAT", 15: "32_32_FLOAT",
}
PRIMITIVES = {
    0: "none", 1: "point_list", 2: "line_list", 3: "line_strip", 4: "triangle_list",
    5: "triangle_fan", 6: "triangle_strip", 7: "triangle_with_w_flags",
    8: "rectangle_list", 12: "line_loop", 13: "quad_list", 14: "quad_strip",
    15: "polygon",
}
DIMENSIONS = {0: "1D", 1: "2D", 2: "3D", 3: "cube"}
MSAA = {0: "1x", 1: "2x", 2: "4x"}
SAMPLE_SELECT = {0: "sample0", 1: "sample1", 2: "sample2", 3: "sample3",
                 4: "average01", 5: "average23", 6: "average0123"}


def signed(value: int, bits: int) -> int:
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def surface_description(surface: dict) -> dict:
    info = int(surface["surface_info"], 16)
    bound = surface["bound"]
    colors = []
    for index, word in enumerate(surface["color_info"]):
        if bound & (1 << (1 + index)):
            value = int(word, 16)
            colors.append({
                "target": index,
                "base_tiles": value & 0xFFF,
                "format": COLOR_TARGET_FORMATS.get((value >> 16) & 0xF, str((value >> 16) & 0xF)),
                "exp_bias": signed((value >> 20) & 0x3F, 6),
            })
    depth = None
    if bound & 1:
        value = int(surface["depth_info"], 16)
        depth = {"base_tiles": value & 0xFFF,
                 "format": "D24FS8" if (value >> 16) & 1 else "D24S8"}
    offset = int(surface["window_offset"], 16)
    return {
        "pitch": info & 0x3FFF,
        "msaa": MSAA.get((info >> 16) & 3, str((info >> 16) & 3)),
        "colors": colors,
        "depth": depth,
        "window_offset": [signed(offset & 0x7FFF, 15), signed((offset >> 16) & 0x7FFF, 15)],
    }


def copy_description(copy: dict) -> dict:
    control = int(copy["copy_control"], 16)
    dest = int(copy["dest_info"], 16)
    source = control & 7
    source_info = int(copy["source_info"], 16)
    return {
        "source": "depth" if source == 4 else f"color{source}",
        "source_format": (
            "D24FS8" if (int(copy["depth_info"], 16) >> 16) & 1 else "D24S8"
        ) if source == 4 else COLOR_TARGET_FORMATS.get((source_info >> 16) & 0xF),
        "sample_select": SAMPLE_SELECT.get((control >> 4) & 7, str((control >> 4) & 7)),
        "clear_color": bool(control & (1 << 8)),
        "clear_depth": bool(control & (1 << 9)),
        "dest_format": TEXTURE_FORMATS.get((dest >> 7) & 0x3F, str((dest >> 7) & 0x3F)),
        "dest_exp_bias": signed((dest >> 16) & 0x3F, 6),
        "dest_array": bool(dest & (1 << 3)),
        "dest_pitch": int(copy["dest_pitch"], 16) & 0x3FFF,
        "dest_height": (int(copy["dest_pitch"], 16) >> 16) & 0x3FFF,
        "msaa": MSAA.get((int(copy["surface_info"], 16) >> 16) & 3),
        "succeeded": copy["succeeded"],
    }


def parse_modes(values):
    modes = []
    for value in values:
        label, _, span = value.partition(":")
        first, _, last = span.partition("-")
        modes.append((label, int(first), int(last)))
    return modes


def mode_for(window: dict, modes) -> str:
    for label, first, last in modes:
        if first <= window["first_frame"] <= last:
            return label
    return "unlabelled"


def summarize(records, modes) -> dict:
    groups = defaultdict(lambda: {
        "windows": 0, "frames": 0, "draws": 0,
        "surfaces": defaultdict(int), "primitives": defaultdict(int),
        "shader_pairs": set(), "draw_states": 0, "memexport_draws": 0,
        "occlusion_query_draws": 0, "textures": defaultdict(lambda: [0, 0]),
        "copies": defaultdict(lambda: [0, 0]), "optimized_clears": defaultdict(int),
        "swaps": defaultdict(int), "overflow": defaultdict(int), "zpd_events": 0,
        "cost_ns": 0,
    })
    for record in records:
        group = groups[mode_for(record, modes)]
        group["windows"] += 1
        group["frames"] += record["last_frame"] - record["first_frame"] + 1
        group["draws"] += record["draws"]
        surfaces = {}
        for surface in record["surfaces"]:
            key = json.dumps(surface_description(surface), sort_keys=True)
            surfaces[surface["id"]] = key
            group["surfaces"][key] += surface["draws"]
        for state in record["draw_states"]:
            group["primitives"][PRIMITIVES.get(state["guest_primitive"],
                                               str(state["guest_primitive"]))] += state["draws"]
            group["shader_pairs"].add((state["vs"], state["ps"]))
            group["draw_states"] += 1
            if state["memexport"]:
                group["memexport_draws"] += state["draws"]
            if state["occlusion_query"]:
                group["occlusion_query_draws"] += state["draws"]
        for texture in record["textures"]:
            key = json.dumps({
                "format": TEXTURE_FORMATS.get(texture["format"], str(texture["format"])),
                "dimension": DIMENSIONS.get(texture["dimension"], str(texture["dimension"])),
                "size": [texture["width"], texture["height"], texture["depth"]],
                "tiled": bool(texture["tiled"]),
                "packed_mips": bool(texture["packed_mips"]),
                "mips": [texture["mip_min"], texture["mip_max"]],
                "signs": texture["signs"],
            }, sort_keys=True)
            group["textures"][key][0] += texture["fetches"]
            group["textures"][key][1] += texture["from_resolve"]
        for copy in record["copies"]:
            key = json.dumps(copy_description(copy), sort_keys=True)
            group["copies"][key][0] += copy["copies"]
            group["copies"][key][1] += copy["bytes"]
        for mode, count in record["optimized_clears"].items():
            group["optimized_clears"][mode] += count
        for swap in record["swaps"]:
            group["swaps"][(TEXTURE_FORMATS.get(swap["format"], str(swap["format"])),
                            swap["width"], swap["height"])] += swap["swaps"]
        group["zpd_events"] += record.get("zpd_events", 0)
        group["cost_ns"] += record.get("cost_ns", 0)
        for name, count in record["overflow"].items():
            group["overflow"][name] += count

    result = {}
    for label, group in groups.items():
        frames = max(group["frames"], 1)
        result[label] = {
            "windows": group["windows"],
            "frames": group["frames"],
            "draws_per_frame": round(group["draws"] / frames, 1),
            "shader_pairs": len(group["shader_pairs"]),
            "draw_state_entries": group["draw_states"],
            "memexport_draws": group["memexport_draws"],
            "occlusion_query_draws": group["occlusion_query_draws"],
            "zpd_events_per_frame": round(group["zpd_events"] / frames, 2),
            "census_ms_per_frame": round(group["cost_ns"] / frames / 1e6, 3),
            "surfaces": sorted(
                ({**json.loads(key), "draws": draws} for key, draws in group["surfaces"].items()),
                key=lambda item: -item["draws"]),
            "primitives": dict(sorted(group["primitives"].items(), key=lambda item: -item[1])),
            "textures": sorted(
                ({**json.loads(key), "fetches": fetches, "from_resolve": resolved}
                 for key, (fetches, resolved) in group["textures"].items()),
                key=lambda item: -item["fetches"]),
            "copies": sorted(
                ({**json.loads(key), "copies": copies,
                  "bytes_per_frame": round(total / frames)}
                 for key, (copies, total) in group["copies"].items()),
                key=lambda item: -item["copies"]),
            "optimized_clears": dict(group["optimized_clears"]),
            "swaps": [{"format": f, "width": w, "height": h, "swaps": n}
                      for (f, w, h), n in sorted(group["swaps"].items())],
            "overflow": dict(group["overflow"]),
        }
    return result


def markdown(summary: dict) -> str:
    lines = []
    for label, mode in summary.items():
        lines.append(f"## {label}\n")
        lines.append(
            f"{mode['frames']} frames, {mode['draws_per_frame']} draws/frame, "
            f"{mode['shader_pairs']} shader pairs, memexport draws "
            f"{mode['memexport_draws']}, occlusion-query draws "
            f"{mode['occlusion_query_draws']}, ZPD events/frame "
            f"{mode['zpd_events_per_frame']}, census cost "
            f"{mode['census_ms_per_frame']} ms/frame, overflow {mode['overflow'] or 'none'}.\n")
        lines.append("| Pitch | MSAA | Color targets | Depth | Window offset | Draws |")
        lines.append("| ---: | --- | --- | --- | --- | ---: |")
        for surface in mode["surfaces"]:
            colors = ", ".join(
                f"{c['target']}:{c['format']}@{c['base_tiles']}"
                + (f" bias {c['exp_bias']}" if c["exp_bias"] else "")
                for c in surface["colors"]) or "none"
            depth = (f"{surface['depth']['format']}@{surface['depth']['base_tiles']}"
                     if surface["depth"] else "none")
            lines.append(f"| {surface['pitch']} | {surface['msaa']} | {colors} | {depth} | "
                         f"{surface['window_offset']} | {surface['draws']} |")
        lines.append("")
        lines.append("Primitives: " + ", ".join(
            f"{name} {count}" for name, count in mode["primitives"].items()) + "\n")
        lines.append("| Texture format | Dim | Size | Tiled | Mips | Fetches | From resolve |")
        lines.append("| --- | --- | --- | --- | --- | ---: | ---: |")
        for texture in mode["textures"][:60]:
            size = "x".join(str(v) for v in texture["size"] if v)
            lines.append(f"| {texture['format']} | {texture['dimension']} | {size} | "
                         f"{texture['tiled']} | {texture['mips'][0]}-{texture['mips'][1]} | "
                         f"{texture['fetches']} | {texture['from_resolve']} |")
        if len(mode["textures"]) > 60:
            lines.append(f"| … {len(mode['textures']) - 60} more layouts | | | | | | |")
        lines.append("")
        lines.append("| Resolve source | Samples | Destination | Bias | Size | Clear | Copies | Bytes/frame |")
        lines.append("| --- | --- | --- | ---: | --- | --- | ---: | ---: |")
        for copy in mode["copies"]:
            clear = "+".join(n for n, v in (("color", copy["clear_color"]),
                                            ("depth", copy["clear_depth"])) if v) or "no"
            lines.append(f"| {copy['source']} {copy['source_format']} {copy['msaa']} | "
                         f"{copy['sample_select']} | {copy['dest_format']} | "
                         f"{copy['dest_exp_bias']} | {copy['dest_pitch']}x{copy['dest_height']} | "
                         f"{clear} | {copy['copies']} | {copy['bytes_per_frame']} |")
        lines.append("")
        lines.append("Swaps: " + ", ".join(
            f"{s['format']} {s['width']}x{s['height']} x{s['swaps']}" for s in mode["swaps"])
            + f". Optimized clears: {mode['optimized_clears'] or 'none'}.\n")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("census", type=Path, nargs="+")
    parser.add_argument("--mode", action="append", default=[],
                        help="label:first-last source-frame range")
    parser.add_argument("--json", type=Path)
    parser.add_argument("--markdown", type=Path)
    args = parser.parse_args()
    records = []
    for path in args.census:
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.strip():
                record = json.loads(line)
                if record.get("schema") != "pinyon-shift.fh1-frame-census.v1":
                    raise ValueError(f"unexpected census schema in {path}")
                records.append(record)
    summary = summarize(records, parse_modes(args.mode))
    if args.json:
        args.json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    text = markdown(summary)
    if args.markdown:
        args.markdown.write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
