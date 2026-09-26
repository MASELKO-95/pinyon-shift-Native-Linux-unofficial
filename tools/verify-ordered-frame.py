"""Summarize the consumed draw/resolve order of one captured race frame."""

import csv
import json
import math
import struct
import sys
from collections import Counter
from pathlib import Path

STATE_PAYLOAD_BYTES = 8 + 8 * 8 + (2048 + 64 + 192 + 40) * 4


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
    copy_inputs_ready = 0
    if "copy_info_valid" in rows[0]:
        for row in copies:
            get = lambda key: int(row[key])
            if not get("resolve_width") or not get("resolve_height"):
                continue  # Mip-skip events still have an ordinal.
            if (not get("copy_info_valid") or
                    not get("copy_source_available") or
                    not get("copy_physical_width") or
                    not get("copy_physical_height") or
                    get("copy_physical_x") + get("copy_physical_width") >
                    get("copy_source_resource_width") or
                    get("copy_physical_y") + get("copy_physical_height") >
                    get("copy_source_resource_height") or
                    get("copy_dest_x") + get("resolve_width") >
                    get("copy_dest_pitch") or
                    get("copy_dest_y") + get("resolve_height") >
                    get("copy_dest_height")):
                raise ValueError(f"incomplete copy source at ordinal {row['sequence']}")
            copy_inputs_ready += 1
    frame = path.stem.rsplit("-", 1)[-1]
    support_path = path.parent / f"ordered-native-support-{frame}.csv"
    support = Counter()
    if support_path.is_file():
        with support_path.open(newline="") as file:
            entries = list(csv.DictReader(file))
        if [int(row["sequence"]) for row in entries] != [
                int(row["sequence"]) for row in draws]:
            raise ValueError("native support manifest does not match draw order")
        support = Counter(int(row["family"]) for row in entries)
        if set(support) - set(range(-1, 6)):
            raise ValueError("unknown native draw family")
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
    requested_texture_versions = set()
    geometry_metadata_draws = 0
    geometry_snapshot_ready_draws = 0
    missing_index_snapshots = 0
    missing_vertex_snapshots = 0
    truncated_vertex_metadata = 0
    vertex_ranges = set()
    geometry_blob_refs = set()
    geometry_incomplete = 0
    geometry_payload_ready_draws = 0
    state_snapshot_ready_draws = 0
    state_manifest_present = False
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
            requested_texture_versions.update(tuple(key[1:9]) for key in keys
                                              if key[7] and key[8] and not key[9])
    pin_path = path.parent / f"ordered-texture-pins-{frame}.csv"
    pinned_texture_versions = set()
    if pin_path.is_file():
        with pin_path.open(newline="") as file:
            pins = list(csv.DictReader(file))
        for pin in pins:
            key = tuple(int(pin[f"word{i}"]) for i in range(6)) + (
                int(pin["allocation_id"]), int(pin["payload_generation"]))
            if key in pinned_texture_versions:
                raise ValueError("duplicate pinned texture version")
            if not int(pin["width"]) or not int(pin["height"]) or not int(pin["mips"]):
                raise ValueError("invalid pinned texture description")
            pinned_texture_versions.add(key)
        if not requested_texture_versions.issubset(pinned_texture_versions):
            raise ValueError("ordered texture versions missing from pinned manifest")
    if "vertex_inputs" in rows[0]:
        for row in draws:
            inputs = row["vertex_inputs"].split(";") if row["vertex_inputs"] else []
            vertices = [[int(value) for value in item.split(":")] for item in inputs]
            if any(len(vertex) != 7 or vertex[4] > 4 for vertex in vertices):
                raise ValueError(f"invalid vertex metadata at ordinal {row['sequence']}")
            index_status = int(row["index_snapshot_status"])
            if index_status > 4:
                raise ValueError(f"invalid index metadata at ordinal {row['sequence']}")
            index_ready = not int(row["index_type"]) or index_status == 1
            vertex_ready = (len(vertices) == int(row["vertex_fetches"])
                            and all(vertex[4] == 1 or
                                    (vertex[2] == 0 and vertex[4] == 3)
                                    for vertex in vertices))
            geometry_metadata_draws += 1
            geometry_snapshot_ready_draws += index_ready and vertex_ready
            missing_index_snapshots += not index_ready
            missing_vertex_snapshots += sum(
                vertex[4] != 1 and not (vertex[2] == 0 and vertex[4] == 3)
                for vertex in vertices)
            truncated_vertex_metadata += len(vertices) != int(row["vertex_fetches"])
            vertex_ranges.update((vertex[1], vertex[2]) for vertex in vertices)
    if "vertex_blobs" in rows[0]:
        flags = {int(row["geometry_incomplete"]) for row in rows}
        if len(flags) != 1:
            raise ValueError("inconsistent geometry completion")
        geometry_incomplete = flags.pop()
        for row in draws:
            vertices = [[int(value) for value in item.split(":")]
                        for item in row["vertex_inputs"].split(";")] if row["vertex_inputs"] else []
            blobs = [[int(value) for value in item.split(":")]
                     for item in row["vertex_blobs"].split(";")] if row["vertex_blobs"] else []
            if len(blobs) != len(vertices) or any(len(blob) != 2 for blob in blobs):
                raise ValueError(f"invalid geometry references at ordinal {row['sequence']}")
            payload_ready = len(vertices) == int(row["vertex_fetches"])
            for vertex, blob in zip(vertices, blobs):
                if blob[1]:
                    if vertex[4] != 1 or blob[1] != (vertex[5] or vertex[2]):
                        raise ValueError(f"invalid vertex blob at ordinal {row['sequence']}")
                    geometry_blob_refs.add(tuple(blob))
                else:
                    payload_ready = (payload_ready and vertex[2] == 0
                                     and vertex[4] == 3)
                    if vertex[4] == 1 and not geometry_incomplete:
                        raise ValueError(f"missing vertex blob at ordinal {row['sequence']}")
            index_length = int(row["index_blob_length"])
            if index_length:
                if (int(row["index_snapshot_status"]) != 1
                        or index_length != int(row["index_length"])):
                    raise ValueError(f"invalid index blob at ordinal {row['sequence']}")
                geometry_blob_refs.add((int(row["index_blob_hash"]), index_length))
            elif int(row["index_type"]):
                payload_ready = False
                if int(row["index_snapshot_status"]) == 1 and not geometry_incomplete:
                    raise ValueError(f"missing index blob at ordinal {row['sequence']}")
            geometry_payload_ready_draws += payload_ready
    if "state_snapshot_ready" in rows[0]:
        flags = {int(row["state_incomplete"]) for row in rows}
        if len(flags) != 1:
            raise ValueError("inconsistent ordered state completion")
        state_path = path.parent / f"ordered-state-{frame}.bin"
        data = state_path.read_bytes()
        if data[:8] != b"RAYSTA01" or len(data) < 20 or struct.unpack_from("<Q", data, 8)[0] != int(frame):
            raise ValueError("invalid ordered state artifact header")
        count = struct.unpack_from("<I", data, 16)[0]
        if count != len(draws):
            raise ValueError("ordered state draw count mismatch")
        offset = 20
        for row in draws:
            if offset + 12 > len(data):
                raise ValueError("truncated ordered state record")
            sequence, ready = struct.unpack_from("<QI", data, offset)
            offset += 12
            if sequence != int(row["sequence"]) or ready != int(row["state_snapshot_ready"]):
                raise ValueError("ordered state sequence or status mismatch")
            if ready:
                if offset + STATE_PAYLOAD_BYTES > len(data):
                    raise ValueError("truncated ordered draw state")
                offset += STATE_PAYLOAD_BYTES
                state_snapshot_ready_draws += 1
        if offset != len(data) or bool(flags.pop()) == (state_snapshot_ready_draws == len(draws)):
            raise ValueError("invalid ordered state completion")
        state_manifest_present = True
    geometry_blob_count = 0
    geometry_blob_bytes = 0
    unreferenced_geometry_blobs = 0
    if geometry_blob_refs:
        artifact = path.parent / f"ordered-geometry-{frame}.bin"
        data = artifact.read_bytes()
        if data[:8] != b"RAYGEO01" or len(data) < 20 or struct.unpack_from("<Q", data, 8)[0] != int(frame):
            raise ValueError("invalid geometry artifact header")
        count = struct.unpack_from("<I", data, 16)[0]
        offset = 20
        keys = set()
        for _ in range(count):
            if offset + 12 > len(data):
                raise ValueError("truncated geometry artifact")
            hash_value, length = struct.unpack_from("<QI", data, offset)
            offset += 12
            if offset + length > len(data):
                raise ValueError("truncated geometry blob")
            actual = 14695981039346656037
            for byte in data[offset:offset + length]:
                actual = ((actual ^ byte) * 1099511628211) & ((1 << 64) - 1)
            if actual != hash_value or (hash_value, length) in keys:
                raise ValueError("invalid geometry blob hash")
            keys.add((hash_value, length))
            geometry_blob_bytes += length
            offset += length
        if offset != len(data) or not geometry_blob_refs.issubset(keys):
            raise ValueError("geometry references do not match artifact")
        unreferenced_geometry_blobs = len(keys - geometry_blob_refs)
        geometry_blob_count = count
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
        "copy_inputs_present": "copy_info_valid" in rows[0],
        "copy_inputs_ready": copy_inputs_ready,
        "ui_draws": len(ui), "missing_final": len(missing_final),
        "native_support_manifest_present": support_path.is_file(),
        "native_support_draws": len(draws) - support[-1] if support else 0,
        "native_unsupported_draws": support[-1],
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
        "unique_texture_versions": len(requested_texture_versions),
        "texture_manifest_present": pin_path.is_file(),
        "pinned_texture_versions": len(pinned_texture_versions),
        "unreferenced_pinned_textures": len(pinned_texture_versions -
                                           requested_texture_versions),
        "geometry_metadata_draws": geometry_metadata_draws,
        "geometry_snapshot_ready_draws": geometry_snapshot_ready_draws,
        "missing_index_snapshots": missing_index_snapshots,
        "missing_vertex_snapshots": missing_vertex_snapshots,
        "truncated_vertex_metadata": truncated_vertex_metadata,
        "unique_vertex_ranges": len(vertex_ranges),
        "geometry_incomplete": geometry_incomplete,
        "geometry_blob_count": geometry_blob_count,
        "geometry_blob_bytes": geometry_blob_bytes,
        "unreferenced_geometry_blobs": unreferenced_geometry_blobs,
        "geometry_payload_ready_draws": geometry_payload_ready_draws,
        "state_manifest_present": state_manifest_present,
        "state_snapshot_ready_draws": state_snapshot_ready_draws,
    }


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and
                                     sys.argv[2] != "--require-owned-inputs"):
        raise SystemExit("usage: verify-ordered-frame.py ordered-frame-N.csv "
                         "[--require-owned-inputs]")
    result = verify(Path(sys.argv[1]))
    print(json.dumps(result, indent=2))
    if result["missing_final"] or result["failed_copies"]:
        raise SystemExit(1)
    if len(sys.argv) == 3 and (not result["texture_manifest_present"] or
                               result["pinned_texture_versions"] !=
                               result["unique_texture_versions"] or
                               result["outdated_texture_keys"] or
                               result["geometry_payload_ready_draws"] !=
                               result["draws"] or
                               not result["state_manifest_present"] or
                               result["state_snapshot_ready_draws"] !=
                               result["draws"]):
        raise SystemExit(1)
