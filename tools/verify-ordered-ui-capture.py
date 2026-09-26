"""Check a one-frame RAYUI001 capture before using it for offline replay."""

import json
import struct
import sys
from collections import Counter
from pathlib import Path


def verify(path: Path, allow_incomplete: bool = False) -> dict:
    data = memoryview(path.read_bytes())
    position = 0

    def take(size: int) -> memoryview:
        nonlocal position
        if size < 0 or position + size > len(data):
            raise ValueError(f"truncated capture at byte {position}")
        result = data[position : position + size]
        position += size
        return result

    def unpack(spec: str):
        return struct.unpack("<" + spec, take(struct.calcsize("<" + spec)))

    def fnv64(bytes_: memoryview) -> int:
        value = 14695981039346656037
        for byte in bytes_:
            value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
        return value

    if bytes(take(8)) != b"RAYUI001":
        raise ValueError("unexpected capture schema")
    frame, = unpack("Q")
    draw_count, complete = unpack("II")
    if not 0 < draw_count <= 256 or (complete != 1 and not allow_incomplete):
        raise ValueError(f"capture incomplete: draws={draw_count} complete={complete}")
    shaders = Counter()
    pair_index_types = {}
    pair_texture_fetches = {}
    pair_primitives = {}
    pair_endianness = {}
    pair_vertex_fetches = {}
    sequences = []
    vertex_bytes = index_bytes = texture_versions = missing_final = 0
    replay_eligible = 0
    for _ in range(draw_count):
        sequence, vertex_shader, pixel_shader, vertex_spec, pixel_spec = unpack("5Q")
        (primitive, index_type, index_count, index_base, index_length,
         index_endianness, surface, color, depth, target_bits,
         depth_control, color_mask) = unpack("12I")
        take(2048 * 4 + 8 * 8)  # Float constants and shader register maps.
        vertex_count, = unpack("I")
        if vertex_count > 8:
            raise ValueError(f"draw {sequence}: invalid vertex fetch count")
        vertices = []
        for _ in range(vertex_count):
            constant, stride, base, length, type_ = unpack("5I")
            expected_hash, = unpack("Q")
            payload = take(length)
            if not stride or fnv64(payload) != expected_hash:
                raise ValueError(f"draw {sequence}: vertex snapshot mismatch")
            vertex_bytes += length
            vertices.append((constant, stride, base, length))
        expected_hash, = unpack("Q")
        indices = memoryview(b"")
        if index_type:
            indices = take(index_length)
            if fnv64(indices) != expected_hash:
                raise ValueError(f"draw {sequence}: index snapshot mismatch")
            index_bytes += index_length
        texture_fetches, = unpack("I")
        if texture_fetches > 32:
            raise ValueError(f"draw {sequence}: invalid texture fetch count")
        take(texture_fetches * 36)
        final_seen, raster, clip, final_depth = unpack("4I")
        if final_seen != 1 and not allow_incomplete:
            raise ValueError(f"draw {sequence}: missing final state")
        missing_final += final_seen != 1
        take(64 * 4)
        fetch_constants = take(192 * 4)
        viewport = unpack("6f")
        scissor = unpack("4i")
        if (vertex_shader, pixel_shader, vertex_spec, pixel_spec) == (
            0xED90DA6EFF5C6BCA, 0x57B9400F6B398736, 3, 3
        ) and vertex_count == 1 and index_type == 1 and index_endianness == 1 \
                and index_count > 0 and index_count * 2 <= index_length \
                and vertices[0][0] < 96:
            constant, stride, base, length = vertices[0]
            address, fetch_length = struct.unpack_from("<II", fetch_constants, constant * 8)
            highest = max((indices[i] << 8) | indices[i + 1]
                          for i in range(0, index_count * 2, 2))
            if ((address & 0x1FFFFFFC) == (base & 0x1FFFFFFC)
                    and (fetch_length & 0x03FFFFFC) == length
                    and (highest + 1) * stride * 4 <= length
                    and viewport[0:3] == (0, 0, 1280)
                    and 0 < viewport[3] <= 720
                    and 0 <= scissor[0] <= scissor[2] <= 1280
                    and 0 <= scissor[1] <= scissor[3] <= 720):
                replay_eligible += 1
        textures, = unpack("I")
        if textures > 32:
            raise ValueError(f"draw {sequence}: invalid texture identity count")
        for _ in range(textures):
            words = unpack("7I4xQQI4x")
            texture_versions += words[7] != 0 and words[8] != 0
        pair = (vertex_shader, pixel_shader, vertex_spec, pixel_spec)
        shaders[pair] += 1
        pair_index_types.setdefault(pair, set()).add(index_type)
        pair_texture_fetches.setdefault(pair, set()).add(texture_fetches)
        pair_primitives.setdefault(pair, set()).add(primitive)
        pair_endianness.setdefault(pair, set()).add(index_endianness)
        pair_vertex_fetches.setdefault(pair, set()).add(vertex_count)
        sequences.append(sequence)
    if position != len(data) or sequences != sorted(set(sequences)):
        raise ValueError("trailing bytes or unordered/duplicate draws")
    missing = [value for left, right in zip(sequences, sequences[1:])
               for value in range(left + 1, right)]
    return {
        "schema": "RAYUI001", "frame": frame, "draws": draw_count,
        "complete": bool(complete), "missing_final": missing_final,
        "first_sequence": sequences[0], "last_sequence": sequences[-1],
        "sequence_gaps": sequences[-1] - sequences[0] + 1 - draw_count,
        "missing_sequences": missing[:32],
        "shader_pairs": len(shaders), "vertex_bytes": vertex_bytes,
        "top_shader_pairs": [
            {"vertex": f"{vertex:016X}", "pixel": f"{pixel:016X}",
             "vertex_spec": f"{vertex_spec:X}",
             "pixel_spec": f"{pixel_spec:X}", "draws": count,
             "index_types": sorted(pair_index_types[(vertex, pixel, vertex_spec, pixel_spec)]),
             "texture_fetch_counts": sorted(pair_texture_fetches[(vertex, pixel, vertex_spec, pixel_spec)]),
             "primitives": sorted(pair_primitives[(vertex, pixel, vertex_spec, pixel_spec)]),
             "index_endianness": sorted(pair_endianness[(vertex, pixel, vertex_spec, pixel_spec)]),
             "vertex_fetch_counts": sorted(pair_vertex_fetches[(vertex, pixel, vertex_spec, pixel_spec)])}
            for (vertex, pixel, vertex_spec, pixel_spec), count
            in shaders.most_common(8)],
        "index_bytes": index_bytes, "versioned_textures": texture_versions,
        "untextured_replay_eligible": replay_eligible,
        "file_bytes": len(data),
    }


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--allow-incomplete"):
        raise SystemExit("usage: verify-ordered-ui-capture.py CAPTURE.bin [--allow-incomplete]")
    print(json.dumps(verify(Path(sys.argv[1]), len(sys.argv) == 3), indent=2))
