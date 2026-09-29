#!/usr/bin/env python3
"""Catalog FH1 UI archives, fonts and textures without putting game-derived data in the repo."""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import os
import re
import struct
import subprocess
import tempfile
import zlib
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from zipfile import BadZipFile, ZipFile
from xml.etree import ElementTree


LOCAL_HEADER = struct.Struct("<IHHHHHIIIHH")
LOCAL_HEADER_SIGNATURE = 0x04034B50
SUPPORTED_METHODS = {0, 21}
DEFAULT_ARCHIVES = (
    "media/UI.zip",
    "media/ui/Fonts.zip",
    "media/ui/Textures.zip",
    "media/ui/textures/Horizon.zip",
)
ASSET_SUFFIXES = (".xds", ".dt", ".dt.log.xml")
LOOSE_FONT_DIRECTORY = "media/ui/fonts"
SCENE_PATH = re.compile(r"(?:^|/)Scenes/ui4/([^/]+)\.(bgf|bsg|fbf)$", re.IGNORECASE)
ASSET_REFERENCE = re.compile(
    rb"(?i)(?:game|update):\\[^\x00\r\n]{1,240}|"
    rb"[A-Za-z0-9_ ./\\-]{2,180}\.(?:xds|tga|bgf|bsg|fbf|dt|xml|lua)"
)
FONT_ELEMENT_KEYS = ("lang", "font", "threshold", "scale_fc", "gap")
CAFF_MAGIC = b"CAFF"
VFONT_DATA_OFFSET = 0x190
VFONT_GLYPH = struct.Struct(">IfIIIIIfff")
XDS_HEADER_SIZE = 0x34
XDS_FETCH = struct.Struct(">6I")
# Texture formats present in the FH1 UI archives, by Xenos format number:
# (name, block edge in texels, bytes per block).
TEXTURE_FORMATS = {
    6: ("k_8_8_8_8", 1, 4),
    18: ("k_DXT1", 4, 8),
    19: ("k_DXT2_3", 4, 16),
    20: ("k_DXT4_5", 4, 16),
    50: ("k_8_8_8_8_AS_16_16_16_16", 1, 4),
    51: ("k_DXT1_AS_16_16_16_16", 4, 8),
    52: ("k_DXT2_3_AS_16_16_16_16", 4, 16),
    53: ("k_DXT4_5_AS_16_16_16_16", 4, 16),
    58: ("k_DXT3A", 4, 8),
    59: ("k_DXT5A", 4, 8),
}
# Byte permutations for none, 8-in-16, 8-in-32 and 16-in-32 guest endianness.
ENDIAN_MASKS = (0, 1, 3, 2)
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class UiInspectionError(ValueError):
    """A malformed archive or unsupported requested extraction."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_range(archive: Path, offset: int, size: int) -> bytes:
    if offset < 0 or size < 0:
        raise UiInspectionError("archive range is negative")
    file_size = archive.stat().st_size
    if offset > file_size or size > file_size - offset:
        raise UiInspectionError(
            f"archive range is outside {archive}: offset={offset} size={size}"
        )
    with archive.open("rb") as stream:
        stream.seek(offset)
        data = stream.read(size)
    if len(data) != size:
        raise UiInspectionError(f"truncated archive range in {archive}")
    return data


def payload_offset(archive: Path, offset: int, mode: str = "auto") -> int:
    """Resolve a local-header or already-resolved payload offset.

    The native XMem helper consumes compressed payload bytes. Some callers have
    a ZipInfo local-header offset, while extraction manifests may already store
    the payload offset, so support both forms explicitly.
    """
    if mode not in {"auto", "header", "payload"}:
        raise UiInspectionError(f"invalid payload offset mode: {mode}")
    if mode == "payload":
        return offset
    signature = _read_range(archive, offset, 4)
    if struct.unpack("<I", signature)[0] != LOCAL_HEADER_SIGNATURE:
        if mode == "header":
            raise UiInspectionError(f"missing ZIP local header at offset {offset}")
        return offset
    fields = LOCAL_HEADER.unpack(_read_range(archive, offset, LOCAL_HEADER.size))
    name_size, extra_size = fields[9], fields[10]
    resolved = offset + LOCAL_HEADER.size + name_size + extra_size
    _read_range(archive, resolved, 0)
    return resolved


def scene_family(name: str) -> str | None:
    match = SCENE_PATH.search(name.replace("\\", "/"))
    return match.group(1) if match else None


def _decode_reference(raw: bytes) -> str:
    return raw.decode("utf-8", errors="replace").rstrip("\x00")


def asset_references(data: bytes, limit: int = 256) -> list[str]:
    values = {_decode_reference(match) for match in ASSET_REFERENCE.findall(data)}
    return sorted(values, key=str.casefold)[:limit]


def extract_entry(
    archive: Path,
    info,
    helper: Path | None,
    offset_mode: str,
    maximum_size: int,
) -> bytes:
    if info.file_size > maximum_size:
        raise UiInspectionError(
            f"entry is larger than the extraction limit: {info.filename}"
        )
    if info.compress_type == 0:
        with ZipFile(archive) as zipped:
            data = zipped.read(info)
    elif info.compress_type == 21:
        if helper is None:
            raise UiInspectionError(
                f"method-21 entry requires --archive-extractor: {info.filename}"
            )
        if not helper.is_file():
            raise UiInspectionError(f"archive extractor does not exist: {helper}")
        with tempfile.TemporaryDirectory(prefix="fh1-ui-") as temporary:
            output = Path(temporary) / "entry.bin"
            offset = payload_offset(archive, info.header_offset, offset_mode)
            command = [
                str(helper),
                str(archive),
                str(offset),
                str(info.compress_size),
                str(info.file_size),
                str(output),
            ]
            completed = subprocess.run(command, capture_output=True, text=True)
            if completed.returncode:
                detail = completed.stderr.strip() or completed.stdout.strip()
                raise UiInspectionError(
                    f"failed to extract {info.filename}: {detail or 'helper failed'}"
                )
            try:
                data = output.read_bytes()
            except OSError as error:
                raise UiInspectionError(
                    f"archive extractor produced no output for {info.filename}"
                ) from error
    else:
        raise UiInspectionError(
            f"unsupported compression method {info.compress_type} for {info.filename}"
        )
    if len(data) != info.file_size:
        raise UiInspectionError(
            f"size mismatch for {info.filename}: got {len(data)}, expected {info.file_size}"
        )
    if (zlib.crc32(data) & 0xFFFFFFFF) != info.CRC:
        raise UiInspectionError(f"CRC mismatch for {info.filename}")
    return data


def parse_fontmap(data: bytes) -> dict[str, object]:
    try:
        root = ElementTree.fromstring(data)
    except ElementTree.ParseError as error:
        raise UiInspectionError(f"fontmap.xml is not valid XML: {error}") from error
    mappings = [
        {"fontname": item.attrib["fontname"], "target": item.attrib["target"]}
        for item in root.findall("mapping")
        if "fontname" in item.attrib and "target" in item.attrib
    ]
    fonts = [dict(item.attrib) for item in root.findall("font") if item.attrib.get("target")]

    def adjusters(tag: str) -> list[dict[str, object]]:
        found = []
        for item in root.findall(tag):
            if not (item.attrib.get("lang") and item.attrib.get("font")):
                continue
            record: dict[str, object] = {
                key: item.attrib[key] for key in FONT_ELEMENT_KEYS if key in item.attrib
            }
            overrides = [dict(child.attrib) for child in item.findall("override")]
            if overrides:
                record["overrides"] = overrides
            found.append(record)
        return found

    return {
        "mappings": mappings,
        "fonts": fonts,
        "fallback_adjusters": adjusters("fallback_adjuster"),
        # The _sd variants apply to standard-definition output.
        "fallback_adjusters_sd": adjusters("fallback_adjuster_sd"),
    }


def _caff_sections(data: bytes) -> tuple[int, int]:
    """Return the .data and .gpu section offsets of a FontCompiler CAFF file."""
    if data[:4] != CAFF_MAGIC or len(data) < VFONT_DATA_OFFSET + 0x400:
        raise UiInspectionError("not a CAFF asset")
    data_size = struct.unpack_from(">I", data, 0x44)[0]
    gpu = VFONT_DATA_OFFSET + struct.unpack_from(">I", data, 0x60)[0]
    if data_size > len(data) - VFONT_DATA_OFFSET or gpu > len(data):
        raise UiInspectionError("CAFF sections are outside the asset")
    return VFONT_DATA_OFFSET, gpu


def parse_vfont(data: bytes) -> dict[str, object]:
    """Summarise a FontCompiler vector font (.dt, asset type "vfont").

    Glyphs are triangle meshes in em units with half-float positions and a
    per-vertex coverage parameter; the title's VectorFont shader decides
    coverage, so only metrics and mesh sizes are catalogued here.
    """
    base, gpu = _caff_sections(data)
    if data[base:base + 6] != b"vfont\x00":
        raise UiInspectionError("CAFF asset is not a vector font")
    version = data[base + 6:base + 20].split(b"\x00", 1)[0].decode("ascii", "replace")
    glyph_count, = struct.unpack_from(">I", data, base + 0x34C)
    hash_offset, hash_size = struct.unpack_from(">II", data, base + 0x35C)
    after_hash = base + hash_offset + 4 * hash_size
    if after_hash + 8 > gpu:
        raise UiInspectionError("vector font hash table is truncated")
    metrics_offset, glyphs_offset = struct.unpack_from(">II", data, after_hash)
    metrics = base + metrics_offset
    vertex_bytes, _, index_count = struct.unpack_from(">III", data, metrics + 8)
    scale, design_size, line_factor = struct.unpack_from(">fff", data, metrics + 0x14)
    ascent, descent = struct.unpack_from(">ii", data, metrics + 0x24)
    if gpu + vertex_bytes + 2 * index_count > len(data):
        raise UiInspectionError("vector font meshes are outside the asset")
    glyphs = []
    for index in range(glyph_count):
        record = base + glyphs_offset + index * VFONT_GLYPH.size
        if record + VFONT_GLYPH.size > gpu:
            raise UiInspectionError("vector font glyph table is truncated")
        (code, advance, _, vertex_offset, vertex_count, index_offset, triangles,
         offset_x, _, _) = VFONT_GLYPH.unpack_from(data, record)
        if (vertex_offset + 8 * vertex_count > vertex_bytes
                or index_offset + 3 * triangles > index_count):
            raise UiInspectionError(f"glyph U+{code:04X} mesh is outside the font")
        glyphs.append({
            "code_point": code,
            "advance": round(advance, 6),
            "offset_x": round(offset_x, 6),
            "vertices": vertex_count,
            "triangles": triangles,
        })
    return {
        "version": version,
        "design_size": design_size,
        "scale": round(scale, 6),
        "line_factor": round(line_factor, 6),
        "ascent": ascent,
        "descent": descent,
        "glyph_count": glyph_count,
        "vertex_bytes": vertex_bytes,
        "index_count": index_count,
        "code_point_ranges": _code_point_ranges(glyph["code_point"] for glyph in glyphs),
        "glyphs": glyphs,
    }


def _code_point_ranges(code_points) -> list[str]:
    ranges: list[list[int]] = []
    for code in sorted(set(code_points)):
        if ranges and code == ranges[-1][1] + 1:
            ranges[-1][1] = code
        else:
            ranges.append([code, code])
    return [f"U+{low:04X}" if low == high else f"U+{low:04X}-U+{high:04X}" for low, high in ranges]


def _log2_ceil(value: int) -> int:
    return max(value - 1, 0).bit_length()


def tiled_offset_2d(x: int, y: int, pitch: int, bytes_per_block_log2: int) -> int:
    """Byte offset of block (x, y) in a Xenos 2D tiled surface (SDK GetTiledOffset2D)."""
    pitch = (pitch + 31) & ~31
    macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (bytes_per_block_log2 + 7)
    micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2
    offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4)
    return (((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2)
            + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F))


def parse_xds(data: bytes) -> dict[str, object]:
    """Decode the D3D texture header that prefixes an FH1 .xds texture."""
    if len(data) < XDS_HEADER_SIZE or struct.unpack_from(">I", data, 0)[0] != 3:
        raise UiInspectionError("not an .xds texture")
    fetch = XDS_FETCH.unpack_from(data, 0x1C)
    if fetch[0] & 3 != 2:
        raise UiInspectionError("texture fetch constant has the wrong type")
    format_number = fetch[1] & 0x3F
    dimension = (fetch[5] >> 9) & 3
    if dimension != 1:
        raise UiInspectionError(f"unsupported texture dimension {dimension}")
    swizzle = (fetch[3] >> 1) & 0xFFF
    name, block, _ = TEXTURE_FORMATS.get(format_number, (f"format_{format_number}", 0, 0))
    return {
        "format": name,
        "format_number": format_number,
        "width": (fetch[2] & 0x1FFF) + 1,
        "height": ((fetch[2] >> 13) & 0x1FFF) + 1,
        "pitch": ((fetch[0] >> 22) & 0x1FF) * 32,
        "tiled": bool(fetch[0] >> 31),
        "endianness": (fetch[1] >> 6) & 3,
        "gamma": all((fetch[0] >> shift) & 3 == 3 for shift in (2, 4, 6)),
        "swizzle": "".join("XYZW01??"[(swizzle >> (3 * component)) & 7] for component in range(4)),
        "mip_levels": ((fetch[4] >> 6) & 0xF) + 1,
        "packed_mips": bool((fetch[5] >> 11) & 1),
        "data_bytes": len(data) - XDS_HEADER_SIZE,
        "decodable": bool(block),
    }


def _base_level_blocks(header: dict[str, object]):
    """Yield (block x, block y, byte offset) for every base-level block."""
    _, block, block_bytes = TEXTURE_FORMATS[header["format_number"]]
    width, height = header["width"], header["height"]
    columns, rows = (width + block - 1) // block, (height + block - 1) // block
    offset_x = offset_y = 0
    if header["packed_mips"] and min(_log2_ceil(width), _log2_ceil(height)) <= 4:
        # A base level no larger than 16 texels sits in the packed mip tail.
        if _log2_ceil(width) > _log2_ceil(height):
            offset_y = 16 // block
        else:
            offset_x = 16 // block
    pitch = max(header["pitch"] // block, 1)
    log2 = block_bytes.bit_length() - 1
    for y in range(rows):
        for x in range(columns):
            if header["tiled"]:
                offset = tiled_offset_2d(x + offset_x, y + offset_y, pitch, log2)
            else:
                offset = (y + offset_y) * pitch * block_bytes + (x + offset_x) * block_bytes
            yield x, y, offset


def _unswap(data: bytes, offset: int, size: int, endianness: int) -> bytes:
    mask = ENDIAN_MASKS[endianness]
    return bytes(data[offset + (index ^ mask)] for index in range(size))


def _bc1_colors(block: bytes, allow_transparent: bool) -> list[tuple[int, int, int, int]]:
    color0, color1 = struct.unpack_from("<HH", block, 0)

    def expand(color: int) -> tuple[int, int, int]:
        red, green, blue = color >> 11, (color >> 5) & 0x3F, color & 0x1F
        return (red << 3 | red >> 2, green << 2 | green >> 4, blue << 3 | blue >> 2)

    first, second = expand(color0), expand(color1)
    if color0 > color1 or not allow_transparent:
        third = tuple((2 * a + b + 1) // 3 for a, b in zip(first, second))
        fourth = tuple((a + 2 * b + 1) // 3 for a, b in zip(first, second))
        return [(*first, 255), (*second, 255), (*third, 255), (*fourth, 255)]
    middle = tuple((a + b + 1) // 2 for a, b in zip(first, second))
    return [(*first, 255), (*second, 255), (*middle, 255), (0, 0, 0, 0)]


def _bc_alpha(block: bytes) -> list[int]:
    alpha0, alpha1 = block[0], block[1]
    if alpha0 > alpha1:
        palette = [alpha0, alpha1] + [((7 - i) * alpha0 + i * alpha1 + 3) // 7 for i in range(1, 7)]
    else:
        palette = ([alpha0, alpha1] + [((5 - i) * alpha0 + i * alpha1 + 2) // 5 for i in range(1, 5)]
                   + [0, 255])
    bits = int.from_bytes(block[2:8], "little")
    return [palette[(bits >> (3 * texel)) & 7] for texel in range(16)]


def _explicit_alpha(block: bytes) -> list[int]:
    bits = int.from_bytes(block[:8], "little")
    return [((bits >> (4 * texel)) & 0xF) * 17 for texel in range(16)]


def _decode_block(format_number: int, block: bytes) -> list[tuple[int, int, int, int]]:
    """Decode one block to XYZW texels (16 for BCn, 1 for 32-bit formats)."""
    if format_number in (6, 50):
        return [tuple(block[:4])]
    if format_number in (58, 59):
        # One-channel formats read the same value in every component, as
        # the SDK's RRRR host swizzle for these formats does.
        values = _explicit_alpha(block) if format_number == 58 else _bc_alpha(block)
        return [(value, value, value, value) for value in values]
    colors_at = 8 if format_number in (19, 20, 52, 53) else 0
    palette = _bc1_colors(block[colors_at:colors_at + 8], colors_at == 0)
    indices = struct.unpack_from("<I", block, colors_at + 4)[0]
    texels = [palette[(indices >> (2 * texel)) & 3] for texel in range(16)]
    if colors_at:
        alpha = _explicit_alpha(block) if format_number in (19, 52) else _bc_alpha(block)
        texels = [(r, g, b, alpha[texel]) for texel, (r, g, b, _) in enumerate(texels)]
    return texels


def decode_xds(data: bytes) -> tuple[dict[str, object], bytes]:
    """Return the header and the base level as swizzled RGBA8 rows."""
    header = parse_xds(data)
    if not header["decodable"]:
        raise UiInspectionError(f"unsupported texture format {header['format']}")
    _, block, block_bytes = TEXTURE_FORMATS[header["format_number"]]
    width, height = header["width"], header["height"]
    payload = data[XDS_HEADER_SIZE:]
    # Selector 4 is constant 0 and 5 is constant 1; 6 and 7 are unused.
    selectors = ["XYZW01??".index(character) for character in header["swizzle"]]
    output = bytearray(width * height * 4)
    seen: set[int] = set()
    for x, y, offset in _base_level_blocks(header):
        if offset + block_bytes > len(payload):
            raise UiInspectionError("texture block is outside the stored data")
        if offset in seen:
            raise UiInspectionError("two texture blocks share storage")
        seen.add(offset)
        texels = _decode_block(header["format_number"],
                               _unswap(payload, offset, block_bytes, header["endianness"]))
        for index, texel in enumerate(texels):
            texel_x, texel_y = x * block + index % block, y * block + index // block
            if texel_x >= width or texel_y >= height:
                continue
            source = (*texel, 0, 255, 0, 0)
            at = 4 * (texel_y * width + texel_x)
            output[at:at + 4] = bytes(source[selector] for selector in selectors)
    return header, bytes(output)


def encode_png(width: int, height: int, rgba: bytes) -> bytes:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return (struct.pack(">I", len(body)) + kind + body
                + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF))

    stride = 4 * width
    raw = b"".join(b"\x00" + rgba[row * stride:(row + 1) * stride] for row in range(height))
    return (PNG_SIGNATURE + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def decode_png(data: bytes) -> tuple[int, int, bytes]:
    """Read an 8-bit RGBA, non-interlaced PNG such as encode_png writes."""
    if not data.startswith(PNG_SIGNATURE):
        raise UiInspectionError("not a PNG file")
    at, header, compressed = len(PNG_SIGNATURE), None, b""
    while at + 8 <= len(data):
        size, kind = struct.unpack_from(">I4s", data, at)
        body = data[at + 8:at + 8 + size]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            compressed += body
        at += 12 + size
    if header is None or header[2:] != (8, 6, 0, 0, 0):
        raise UiInspectionError("only 8-bit RGBA non-interlaced PNG files are supported")
    width, height = header[:2]
    raw, stride = zlib.decompress(compressed), 4 * width
    rows, previous = [], bytearray(stride)
    for row in range(height):
        line = raw[row * (stride + 1):(row + 1) * (stride + 1)]
        kind, current = line[0], bytearray(line[1:])
        for index in range(stride):
            left = current[index - 4] if index >= 4 else 0
            up, corner = previous[index], previous[index - 4] if index >= 4 else 0
            if kind == 1:
                current[index] = (current[index] + left) & 0xFF
            elif kind == 2:
                current[index] = (current[index] + up) & 0xFF
            elif kind == 3:
                current[index] = (current[index] + (left + up) // 2) & 0xFF
            elif kind == 4:
                estimate = left + up - corner
                distances = (abs(estimate - left), abs(estimate - up), abs(estimate - corner))
                predictor = (left, up, corner)[distances.index(min(distances))]
                current[index] = (current[index] + predictor) & 0xFF
        rows.append(bytes(current))
        previous = current
    return width, height, b"".join(rows)


def reencode_xds(data: bytes, rgba: bytes) -> bytes:
    """Write RGBA8 pixels back into an uncompressed 32-bit texture's storage.

    Components that the swizzle does not read keep their stored values.
    """
    header = parse_xds(data)
    if header["format_number"] not in (6, 50):
        raise UiInspectionError(f"re-encoding {header['format']} is not supported")
    width, height = header["width"], header["height"]
    if len(rgba) != 4 * width * height:
        raise UiInspectionError("pixel data does not match the texture size")
    selectors = ["XYZW01??".index(character) for character in header["swizzle"]]
    mask = ENDIAN_MASKS[header["endianness"]]
    output = bytearray(data)
    for x, y, offset in _base_level_blocks(header):
        at = XDS_HEADER_SIZE + offset
        texel = 4 * (y * width + x)
        for channel, selector in enumerate(selectors):
            if selector < 4:
                output[at + (selector ^ mask)] = rgba[texel + channel]
    return bytes(output)


def texture_round_trip(data: bytes, png: bytes) -> str:
    """Compare a written PNG with the stored texture.

    Returns "bytes" when the PNG re-encodes to the stored bytes (uncompressed
    32-bit textures, which proves the address mapping both ways), "pixels"
    when it matches a fresh decode (block-compressed textures) and
    "mismatch" otherwise.
    """
    header, rgba = decode_xds(data)
    width, height, decoded = decode_png(png)
    if (width, height, decoded) != (header["width"], header["height"], rgba):
        return "mismatch"
    if header["format_number"] not in (6, 50):
        return "pixels"
    return "bytes" if reencode_xds(data, decoded) == data else "mismatch"


def _matches(name: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(name, pattern) for pattern in patterns)


def _entry_record(archive: Path, info, offset_mode: str) -> dict[str, object]:
    offset = payload_offset(archive, info.header_offset, offset_mode)
    compressed = _read_range(archive, offset, info.compress_size)
    return {
        "name": info.filename,
        "method": info.compress_type,
        "compressed_size": info.compress_size,
        "uncompressed_size": info.file_size,
        "crc32": f"{info.CRC:08x}",
        "compressed_sha256": sha256_bytes(compressed),
        "header_offset": info.header_offset,
        "payload_offset": offset,
        "scene_family": scene_family(info.filename),
    }


def _require_local(path: Path) -> Path:
    resolved = path.resolve()
    if ".local" not in {part.casefold() for part in resolved.parts}:
        raise UiInspectionError("derived output must be below a .local directory")
    return resolved


def _font_compiler_face(data: bytes) -> str | None:
    match = re.search(rb"name='face' value='([^']*)'", data)
    return match.group(1).decode("utf-8", "replace") if match else None


def _decode_asset(job: tuple) -> dict[str, object]:
    """Extract one member and decode it; runs in a worker process."""
    archive, name, helper, offset_mode, maximum_size, png_path = job
    with ZipFile(archive) as zipped:
        info = zipped.getinfo(name)
    data = extract_entry(archive, info, helper, offset_mode, maximum_size)
    record: dict[str, object] = {"sha256": sha256_bytes(data), "extracted": True}
    lowered = name.casefold()
    try:
        if lowered.endswith(".xds"):
            header = parse_xds(data)
            record["texture"] = header
            if png_path is not None and header["decodable"]:
                _, rgba = decode_xds(data)
                png = encode_png(header["width"], header["height"], rgba)
                png_path = Path(png_path)
                png_path.parent.mkdir(parents=True, exist_ok=True)
                png_path.write_bytes(png)
                header["png"] = png_path.as_posix()
                header["round_trip"] = texture_round_trip(data, png)
        elif lowered.endswith(".dt"):
            record["vector_font"] = parse_vfont(data)
        elif lowered.endswith(".dt.log.xml"):
            record["font_compiler_face"] = _font_compiler_face(data)
        else:
            record["asset_references"] = asset_references(data)
            if lowered == "fontmap.xml":
                record["fontmap"] = parse_fontmap(data)
    except (UiInspectionError, struct.error) as error:
        record["decode_error"] = str(error)
    return record


def _run_jobs(jobs: list[tuple], workers: int) -> list[dict[str, object]]:
    if workers <= 1 or len(jobs) < 2:
        return [_decode_asset(job) for job in jobs]
    # Decoding is pure Python and CPU bound, so use processes; each one also
    # waits on the extractor helper for its member.
    with ProcessPoolExecutor(workers) as pool:
        return list(pool.map(_decode_asset, jobs, chunksize=8))


def _loose_fonts(game_root: Path) -> list[dict[str, object]]:
    directory = game_root / LOOSE_FONT_DIRECTORY
    if not directory.is_dir():
        return []
    return [
        {"name": path.name, "size": path.stat().st_size, "sha256": sha256_file(path)}
        for path in sorted(directory.iterdir(), key=lambda item: item.name.casefold())
        if path.is_file()
    ]


def inspect(
    game_root: Path,
    output: Path,
    archives: list[Path],
    helper: Path | None = None,
    patterns: list[str] | None = None,
    offset_mode: str = "auto",
    maximum_size: int = 32 * 1024 * 1024,
    decode_assets: bool = False,
    png_directory: Path | None = None,
    png_patterns: list[str] | None = None,
    workers: int = 1,
) -> dict[str, object]:
    patterns = list(patterns or [])
    png_patterns = list(png_patterns or ["*.xds"])
    resolved_output = _require_local(output)
    if png_directory is not None:
        png_directory = _require_local(png_directory)
    resolved_output.parent.mkdir(parents=True, exist_ok=True)
    records: list[dict[str, object]] = []
    fontmap: dict[str, object] | None = None
    warnings: list[str] = []
    for archive in archives:
        archive = archive.resolve()
        if not archive.is_file():
            warnings.append(f"missing archive: {archive}")
            continue
        archive_record: dict[str, object] = {
            "path": archive.as_posix(),
            "size": archive.stat().st_size,
            "sha256": sha256_file(archive),
            "entries": [],
        }
        jobs: list[tuple] = []
        pending: list[dict[str, object]] = []
        try:
            with ZipFile(archive) as zipped:
                infos = sorted(zipped.infolist(), key=lambda item: item.filename.casefold())
            for info in infos:
                record = _entry_record(archive, info, offset_mode)
                archive_record["entries"].append(record)
                record["extracted"] = False
                if info.is_dir():
                    continue
                lowered = info.filename.casefold()
                requested = _matches(info.filename, patterns)
                decodable = decode_assets and lowered.endswith(ASSET_SUFFIXES)
                if not (requested or decodable or lowered == "fontmap.xml"):
                    continue
                if info.compress_type != 0 and helper is None:
                    if requested or decodable:
                        raise UiInspectionError(
                            f"compression method {info.compress_type} requires --archive-extractor for {info.filename}"
                        )
                    warnings.append(
                        f"skipped automatic fontmap extraction without --archive-extractor: {archive.name}"
                    )
                    continue
                png_path = None
                if (png_directory is not None and lowered.endswith(".xds")
                        and _matches(info.filename, png_patterns)):
                    png_path = str(png_directory / archive.stem / (info.filename + ".png"))
                jobs.append((archive, info.filename, helper, offset_mode, maximum_size, png_path))
                pending.append(record)
            for record, decoded in zip(pending, _run_jobs(jobs, workers)):
                record.update(decoded)
                if "fontmap" in decoded:
                    fontmap = decoded.pop("fontmap")
                    record.pop("fontmap", None)
                if "decode_error" in decoded:
                    warnings.append(f"{archive.name}!/{record['name']}: {decoded['decode_error']}")
        except (BadZipFile, OSError, UiInspectionError) as error:
            raise UiInspectionError(f"{archive}: {error}") from error
        records.append(archive_record)
    textures = [entry["texture"] for archive in records for entry in archive["entries"]
                if "texture" in entry]
    result = {
        "schema_version": 2,
        "game_root": game_root.resolve().as_posix(),
        "archives": records,
        "fontmap": fontmap or {"mappings": [], "fonts": [], "fallback_adjusters": [],
                               "fallback_adjusters_sd": []},
        "loose_fonts": _loose_fonts(game_root),
        "texture_summary": {
            "count": len(textures),
            "formats": dict(sorted(Counter(texture["format"] for texture in textures).items())),
            "png_written": sum(1 for texture in textures if "png" in texture),
            "round_trip": dict(sorted(Counter(texture["round_trip"] for texture in textures
                                              if "round_trip" in texture).items())),
        },
        "extract_patterns": patterns,
        "warnings": warnings,
    }
    resolved_output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return result


def _default_archives(game_root: Path) -> list[Path]:
    return [game_root / relative for relative in DEFAULT_ARCHIVES if (game_root / relative).is_file()]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--archive", action="append", type=Path, help="archive relative to --game-root; repeatable")
    parser.add_argument("--archive-extractor", type=Path)
    parser.add_argument("--extract-pattern", action="append", default=[], help="member glob to extract and inspect; repeatable")
    parser.add_argument("--offset-mode", choices=("auto", "header", "payload"), default="auto")
    parser.add_argument("--maximum-entry-size", type=int, default=32 * 1024 * 1024)
    parser.add_argument("--decode-assets", action="store_true",
                        help="extract and decode every .xds texture and .dt vector font")
    parser.add_argument("--png-dir", type=Path, help="write decoded textures as PNG files below this .local directory")
    parser.add_argument("--png-pattern", action="append", default=[],
                        help="member glob of textures to write as PNG (default: every .xds); repeatable")
    parser.add_argument("--workers", type=int, default=os.cpu_count() or 1)
    args = parser.parse_args()
    if args.png_dir and not args.decode_assets:
        parser.error("--png-dir requires --decode-assets")
    try:
        game_root = args.game_root.resolve()
        archives = [
            path if path.is_absolute() else game_root / path
            for path in (args.archive or _default_archives(game_root))
        ]
        result = inspect(
            game_root,
            args.output,
            archives,
            args.archive_extractor,
            args.extract_pattern,
            args.offset_mode,
            args.maximum_entry_size,
            args.decode_assets,
            args.png_dir,
            args.png_pattern,
            max(args.workers, 1),
        )
    except (OSError, UiInspectionError, ValueError) as error:
        parser.error(str(error))
    summary = result["texture_summary"]
    print(
        f"Inspected {len(result['archives'])} archive(s), {summary['count']} texture(s), "
        f"{summary['png_written']} PNG(s); wrote {args.output.resolve()}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
