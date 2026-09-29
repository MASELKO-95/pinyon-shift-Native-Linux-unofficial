#!/usr/bin/env python3
"""Build and verify deterministic Pinyon Shift native shader packs."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import re
import struct
import sys
import tempfile
from dataclasses import dataclass


SCHEMA = "pinyon-shift.native-shader-pack.v2"
MAGIC = b"PNYNSHPK"
VERSION = 2
HEADER = struct.Struct("<8sIIIIQQQ32sIIIIIIII")
ENTRY = struct.Struct("<IIQQQQIIII32s")
TEXTURE_BINDING = struct.Struct("<IIII")
SAMPLER_BINDING = struct.Struct("<IIIIII")
STAGES = {"vertex": 1, "pixel": 2}
STAGE_NAMES = {value: key for key, value in STAGES.items()}
BYTECODE_FORMAT_DXIL = 1
MAX_ENTRY_COUNT = 65_535
MAX_BYTECODE_SIZE = 16 * 1024 * 1024
MAX_PACK_SIZE = 512 * 1024 * 1024
MAX_BINDINGS = 255
HEX_64 = re.compile(r"^[0-9A-Fa-f]{16}$")
HEX_32 = re.compile(r"^[0-9A-Fa-f]{8}$")
HEX_256 = re.compile(r"^[0-9A-Fa-f]{64}$")

FLAG_BINDLESS_RESOURCES = 1 << 0
FLAG_EDRAM_ROV = 1 << 1
FLAG_GAMMA_RENDER_TARGET_AS_UNORM8 = 1 << 2
FLAG_MSAA_2X = 1 << 3
KNOWN_FLAGS = (
    FLAG_BINDLESS_RESOURCES
    | FLAG_EDRAM_ROV
    | FLAG_GAMMA_RENDER_TARGET_AS_UNORM8
    | FLAG_MSAA_2X
)


class PackError(ValueError):
    """Raised when a shader manifest or pack violates the format contract."""


@dataclass(frozen=True, order=True)
class ShaderIdentity:
    stage: int
    guest_hash: int
    specialization_mask: int


@dataclass(frozen=True)
class ShaderEntry:
    identity: ShaderIdentity
    bytecode: bytes
    bytecode_sha256: bytes
    texture_bindings: tuple[tuple[int, int, int, int], ...]
    sampler_bindings: tuple[tuple[int, int, int, int, int, int], ...]
    used_texture_mask: int


@dataclass(frozen=True)
class TranslationConfig:
    translator_version: int
    vendor_id: int
    flags: int
    draw_resolution_scale_x: int
    draw_resolution_scale_y: int


@dataclass(frozen=True)
class PackInput:
    config: TranslationConfig
    entries: tuple[ShaderEntry, ...]


def _parse_hex64(value: object, field: str) -> int:
    if not isinstance(value, str) or not HEX_64.fullmatch(value):
        raise PackError(f"{field} must contain exactly 16 hexadecimal digits")
    return int(value, 16)


def _parse_hex32(value: object, field: str) -> int:
    if not isinstance(value, str) or not HEX_32.fullmatch(value):
        raise PackError(f"{field} must contain exactly 8 hexadecimal digits")
    return int(value, 16)


def _parse_uint32(value: object, field: str, maximum: int = 0xFFFFFFFF) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= maximum:
        raise PackError(f"{field} must be an integer from 0 through {maximum}")
    return value


def _load_translation_config(document: dict) -> TranslationConfig:
    raw = document.get("translation")
    if not isinstance(raw, dict):
        raise PackError("translation must be a JSON object")
    flags = 0
    for name, flag in (
        ("bindless_resources", FLAG_BINDLESS_RESOURCES),
        ("edram_rov", FLAG_EDRAM_ROV),
        ("gamma_render_target_as_unorm8", FLAG_GAMMA_RENDER_TARGET_AS_UNORM8),
        ("msaa_2x", FLAG_MSAA_2X),
    ):
        value = raw.get(name)
        if not isinstance(value, bool):
            raise PackError(f"translation.{name} must be boolean")
        if value:
            flags |= flag
    scale_x = _parse_uint32(
        raw.get("draw_resolution_scale_x"), "translation.draw_resolution_scale_x", 4
    )
    scale_y = _parse_uint32(
        raw.get("draw_resolution_scale_y"), "translation.draw_resolution_scale_y", 4
    )
    if scale_x == 0 or scale_y == 0:
        raise PackError("translation draw resolution scales must be non-zero")
    return TranslationConfig(
        translator_version=_parse_hex32(
            raw.get("translator_version"), "translation.translator_version"
        ),
        vendor_id=_parse_uint32(raw.get("vendor_id"), "translation.vendor_id", 0xFFFF),
        flags=flags,
        draw_resolution_scale_x=scale_x,
        draw_resolution_scale_y=scale_y,
    )


def _load_binding_list(
    raw: object, field: str, names: tuple[str, ...], limits: tuple[int, ...]
) -> tuple[tuple[int, ...], ...]:
    if not isinstance(raw, list) or len(raw) > MAX_BINDINGS:
        raise PackError(f"{field} must be an array of at most {MAX_BINDINGS} bindings")
    result = []
    for index, binding in enumerate(raw):
        if not isinstance(binding, dict) or set(binding) != set(names):
            raise PackError(f"{field}[{index}] has invalid fields")
        result.append(
            tuple(
                _parse_uint32(binding[name], f"{field}[{index}].{name}", limit)
                for name, limit in zip(names, limits, strict=True)
            )
        )
    return tuple(result)


def _parse_sha256(value: object, field: str) -> bytes:
    if not isinstance(value, str) or not HEX_256.fullmatch(value):
        raise PackError(f"{field} must contain exactly 64 hexadecimal digits")
    return bytes.fromhex(value)


def _checked_local_file(root: pathlib.Path, relative: object) -> pathlib.Path:
    if not isinstance(relative, str) or not relative:
        raise PackError("bytecode must be a non-empty relative path")
    candidate = pathlib.Path(relative)
    if candidate.is_absolute():
        raise PackError("bytecode paths must be relative to the manifest")
    try:
        resolved = (root / candidate).resolve(strict=True)
    except OSError as error:
        raise PackError(f"bytecode file is unavailable: {relative}") from error
    try:
        resolved.relative_to(root)
    except ValueError as error:
        raise PackError("bytecode path escapes the manifest directory") from error
    if not resolved.is_file():
        raise PackError(f"bytecode path is not a regular file: {relative}")
    return resolved


def load_manifest(path: pathlib.Path) -> PackInput:
    manifest_path = path.resolve(strict=True)
    try:
        document = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PackError(f"unable to read shader manifest: {error}") from error
    if not isinstance(document, dict):
        raise PackError("shader manifest must be a JSON object")
    if document.get("schema") != SCHEMA:
        raise PackError(f"shader manifest schema must be {SCHEMA}")
    if document.get("backend") != "d3d12":
        raise PackError("shader manifest backend must be d3d12")
    config = _load_translation_config(document)
    raw_entries = document.get("entries")
    if not isinstance(raw_entries, list) or not raw_entries:
        raise PackError("shader manifest entries must be a non-empty array")
    if len(raw_entries) > MAX_ENTRY_COUNT:
        raise PackError(f"shader manifest exceeds {MAX_ENTRY_COUNT} entries")

    root = manifest_path.parent.resolve(strict=True)
    entries: list[ShaderEntry] = []
    identities: set[ShaderIdentity] = set()
    # Captures append every bytecode to one file and address it by offset and
    # size; one file per shader is still accepted.
    shared_files: dict[pathlib.Path, bytes] = {}
    for index, raw in enumerate(raw_entries):
        if not isinstance(raw, dict):
            raise PackError(f"entries[{index}] must be a JSON object")
        stage_name = raw.get("stage")
        if stage_name not in STAGES:
            raise PackError(f"entries[{index}].stage must be vertex or pixel")
        identity = ShaderIdentity(
            stage=STAGES[stage_name],
            guest_hash=_parse_hex64(raw.get("guest_hash"), f"entries[{index}].guest_hash"),
            specialization_mask=_parse_hex64(
                raw.get("specialization_mask"),
                f"entries[{index}].specialization_mask",
            ),
        )
        if identity in identities:
            raise PackError(f"entries[{index}] duplicates a shader identity")
        identities.add(identity)

        bytecode_path = _checked_local_file(root, raw.get("bytecode"))
        if "bytecode_offset" in raw or "bytecode_size" in raw:
            offset = _parse_uint32(raw.get("bytecode_offset"), f"entries[{index}].bytecode_offset")
            size = _parse_uint32(raw.get("bytecode_size"), f"entries[{index}].bytecode_size")
            if bytecode_path not in shared_files:
                shared_files[bytecode_path] = bytecode_path.read_bytes()
            shared = shared_files[bytecode_path]
            if offset + size > len(shared):
                raise PackError(f"entries[{index}] bytecode range exceeds {raw['bytecode']}")
            bytecode = shared[offset:offset + size]
        else:
            bytecode = bytecode_path.read_bytes()
        if not bytecode:
            raise PackError(f"entries[{index}] bytecode is empty")
        if len(bytecode) > MAX_BYTECODE_SIZE:
            raise PackError(
                f"entries[{index}] bytecode exceeds {MAX_BYTECODE_SIZE} bytes"
            )
        if not bytecode.startswith(b"DXBC"):
            raise PackError(f"entries[{index}] is not a DXIL container")
        actual_digest = hashlib.sha256(bytecode).digest()
        expected_digest = _parse_sha256(
            raw.get("sha256"), f"entries[{index}].sha256"
        )
        if actual_digest != expected_digest:
            raise PackError(f"entries[{index}] bytecode SHA-256 does not match")
        texture_bindings = _load_binding_list(
            raw.get("texture_bindings"),
            f"entries[{index}].texture_bindings",
            ("bindless_descriptor_index", "fetch_constant", "dimension", "is_signed"),
            (0xFFFFFFFF, 31, 3, 1),
        )
        sampler_bindings = _load_binding_list(
            raw.get("sampler_bindings"),
            f"entries[{index}].sampler_bindings",
            (
                "bindless_descriptor_index",
                "fetch_constant",
                "mag_filter",
                "min_filter",
                "mip_filter",
                "aniso_filter",
            ),
            (0xFFFFFFFF, 31, 3, 3, 3, 7),
        )
        used_texture_mask = _parse_uint32(
            raw.get("used_texture_mask"), f"entries[{index}].used_texture_mask"
        )
        expected_mask = 0
        for binding in texture_bindings:
            expected_mask |= 1 << binding[1]
        if used_texture_mask != expected_mask:
            raise PackError(f"entries[{index}].used_texture_mask does not match bindings")
        entries.append(
            ShaderEntry(
                identity,
                bytecode,
                actual_digest,
                texture_bindings,
                sampler_bindings,
                used_texture_mask,
            )
        )

    return PackInput(config, tuple(sorted(entries, key=lambda entry: entry.identity)))


def serialize(pack_input: PackInput) -> bytes:
    entries = pack_input.entries
    if not entries or len(entries) > MAX_ENTRY_COUNT:
        raise PackError("shader pack must contain a bounded non-empty entry set")
    index_offset = HEADER.size
    data_offset = index_offset + ENTRY.size * len(entries)
    index = bytearray()
    data = bytearray()
    for shader in entries:
        alignment = (-len(data)) % 16
        data.extend(b"\0" * alignment)
        entry_data_offset = len(data)
        data.extend(shader.bytecode)
        for binding in shader.texture_bindings:
            data.extend(TEXTURE_BINDING.pack(*binding))
        for binding in shader.sampler_bindings:
            data.extend(SAMPLER_BINDING.pack(*binding))
        index.extend(
            ENTRY.pack(
                shader.identity.stage,
                BYTECODE_FORMAT_DXIL,
                shader.identity.guest_hash,
                shader.identity.specialization_mask,
                entry_data_offset,
                len(shader.bytecode),
                len(shader.texture_bindings),
                len(shader.sampler_bindings),
                shader.used_texture_mask,
                0,
                shader.bytecode_sha256,
            )
        )
    content = bytes(index + data)
    total_size = HEADER.size + len(content)
    if total_size > MAX_PACK_SIZE:
        raise PackError(f"shader pack exceeds {MAX_PACK_SIZE} bytes")
    return HEADER.pack(
        MAGIC,
        VERSION,
        HEADER.size,
        ENTRY.size,
        len(entries),
        index_offset,
        data_offset,
        len(data),
        hashlib.sha256(content).digest(),
        pack_input.config.translator_version,
        pack_input.config.vendor_id,
        pack_input.config.flags,
        pack_input.config.draw_resolution_scale_x,
        pack_input.config.draw_resolution_scale_y,
        0,
        0,
        0,
    ) + content


def verify_pack(data: bytes) -> dict[str, object]:
    if len(data) < HEADER.size or len(data) > MAX_PACK_SIZE:
        raise PackError("shader pack size is outside the supported range")
    (
        magic,
        version,
        header_size,
        entry_size,
        entry_count,
        index_offset,
        data_offset,
        data_size,
        content_digest,
        translator_version,
        vendor_id,
        flags,
        scale_x,
        scale_y,
        reserved_0,
        reserved_1,
        reserved_2,
    ) = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION:
        raise PackError("shader pack magic or version is unsupported")
    if header_size != HEADER.size or entry_size != ENTRY.size:
        raise PackError("shader pack layout size is invalid")
    if not 0 < entry_count <= MAX_ENTRY_COUNT:
        raise PackError("shader pack entry count is invalid")
    expected_data_offset = index_offset + entry_count * entry_size
    if index_offset != header_size or data_offset != expected_data_offset:
        raise PackError("shader pack index offsets are invalid")
    if data_size > MAX_PACK_SIZE or data_offset + data_size != len(data):
        raise PackError("shader pack payload range is invalid")
    if hashlib.sha256(data[index_offset:]).digest() != content_digest:
        raise PackError("shader pack content SHA-256 does not match")
    if (
        not translator_version
        or vendor_id > 0xFFFF
        or flags & ~KNOWN_FLAGS
        or not 0 < scale_x <= 4
        or not 0 < scale_y <= 4
        or reserved_0
        or reserved_1
        or reserved_2
    ):
        raise PackError("shader pack translation configuration is invalid")

    identities: list[ShaderIdentity] = []
    previous_payload_end = 0
    for index in range(entry_count):
        offset = index_offset + index * entry_size
        (
            stage,
            bytecode_format,
            guest_hash,
            specialization_mask,
            entry_data_offset,
            bytecode_size,
            texture_binding_count,
            sampler_binding_count,
            used_texture_mask,
            reserved,
            bytecode_digest,
        ) = ENTRY.unpack_from(data, offset)
        if stage not in STAGE_NAMES or bytecode_format != BYTECODE_FORMAT_DXIL:
            raise PackError(f"shader pack entry {index} has unsupported identity data")
        if (
            reserved != 0
            or not 0 < bytecode_size <= MAX_BYTECODE_SIZE
            or texture_binding_count > MAX_BINDINGS
            or sampler_binding_count > MAX_BINDINGS
            or entry_data_offset % 16 != 0
            or entry_data_offset < previous_payload_end
        ):
            raise PackError(f"shader pack entry {index} has invalid bounds")
        bytecode_start = data_offset + entry_data_offset
        bytecode_end = bytecode_start + bytecode_size
        entry_end = (
            bytecode_end
            + texture_binding_count * TEXTURE_BINDING.size
            + sampler_binding_count * SAMPLER_BINDING.size
        )
        if bytecode_start < data_offset or entry_end > len(data):
            raise PackError(f"shader pack entry {index} escapes the payload")
        bytecode = data[bytecode_start:bytecode_end]
        if not bytecode.startswith(b"DXBC"):
            raise PackError(f"shader pack entry {index} is not a DXIL container")
        if hashlib.sha256(bytecode).digest() != bytecode_digest:
            raise PackError(f"shader pack entry {index} SHA-256 does not match")
        binding_offset = bytecode_end
        actual_used_texture_mask = 0
        for binding_index in range(texture_binding_count):
            binding = TEXTURE_BINDING.unpack_from(
                data, binding_offset + binding_index * TEXTURE_BINDING.size
            )
            if binding[1] > 31 or binding[2] > 3 or binding[3] > 1:
                raise PackError(f"shader pack entry {index} has invalid texture bindings")
            actual_used_texture_mask |= 1 << binding[1]
        if actual_used_texture_mask != used_texture_mask:
            raise PackError(f"shader pack entry {index} texture mask does not match bindings")
        binding_offset += texture_binding_count * TEXTURE_BINDING.size
        for binding_index in range(sampler_binding_count):
            binding = SAMPLER_BINDING.unpack_from(
                data, binding_offset + binding_index * SAMPLER_BINDING.size
            )
            if binding[1] > 31 or any(value > 3 for value in binding[2:5]) or binding[5] > 7:
                raise PackError(f"shader pack entry {index} has invalid sampler bindings")
        padding = data[
            data_offset + previous_payload_end : data_offset + entry_data_offset
        ]
        if any(padding):
            raise PackError(f"shader pack entry {index} has non-zero padding")
        identity = ShaderIdentity(stage, guest_hash, specialization_mask)
        if identities and identity <= identities[-1]:
            raise PackError("shader pack identities are duplicated or not sorted")
        identities.append(identity)
        previous_payload_end = entry_end - data_offset

    if previous_payload_end != data_size:
        raise PackError("shader pack payload contains unreferenced trailing data")

    return {
        "schema": SCHEMA,
        "backend": "d3d12",
        "entry_count": entry_count,
        "translator_version": f"{translator_version:08X}",
        "vendor_id": vendor_id,
        "flags": flags,
        "draw_resolution_scale_x": scale_x,
        "draw_resolution_scale_y": scale_y,
        "content_sha256": content_digest.hex().upper(),
        "pack_sha256": hashlib.sha256(data).hexdigest().upper(),
        "size_bytes": len(data),
    }


def _write_atomic(path: pathlib.Path, data: bytes) -> None:
    output = path.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=output.parent, prefix=f".{output.name}.", suffix=".tmp", delete=False
        ) as temporary:
            temporary_name = temporary.name
            temporary.write(data)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.replace(temporary_name, output)
    finally:
        if temporary_name:
            pathlib.Path(temporary_name).unlink(missing_ok=True)


def _build(arguments: argparse.Namespace) -> dict[str, object]:
    manifests = [load_manifest(path) for path in arguments.manifest]
    config = manifests[0].config
    merged: dict[ShaderIdentity, ShaderEntry] = {}
    for manifest in manifests:
        if manifest.config != config:
            raise PackError("shader manifests use different translation configurations")
        for entry in manifest.entries:
            existing = merged.get(entry.identity)
            if existing is not None and existing != entry:
                raise PackError("shader manifests disagree on a duplicate identity")
            merged[entry.identity] = entry
    pack_input = PackInput(config, tuple(sorted(merged.values(), key=lambda entry: entry.identity)))
    data = serialize(pack_input)
    verify = verify_pack(data)
    _write_atomic(arguments.output, data)
    return {"operation": "build", "output": str(arguments.output), **verify}


def _verify(arguments: argparse.Namespace) -> dict[str, object]:
    try:
        data = arguments.pack.read_bytes()
    except OSError as error:
        raise PackError(f"unable to read shader pack: {error}") from error
    return {"operation": "verify", "pack": str(arguments.pack), **verify_pack(data)}


def _stage(arguments: argparse.Namespace) -> dict[str, object]:
    try:
        data = arguments.pack.read_bytes()
    except OSError as error:
        raise PackError(f"unable to read shader pack: {error}") from error
    metadata = verify_pack(data)
    scale = metadata["draw_resolution_scale_x"]
    if metadata["draw_resolution_scale_y"] != scale:
        raise PackError("FH1 shader packs require equal X and Y scales")
    if arguments.scale is not None and scale != arguments.scale:
        raise PackError(f"shader pack does not match the requested {arguments.scale}x scale")
    name = (
        f"4D5309C9.fh1-native-v2.{metadata['vendor_id']:04X}."
        f"{metadata['flags']:02X}.{scale}x{scale}.pnsp"
    )
    destination = arguments.state_root.resolve() / "cache/shaders/shareable" / name
    unchanged = destination.is_file() and destination.stat().st_size == len(data)
    if unchanged:
        with destination.open("rb") as stream:
            unchanged = (
                hashlib.file_digest(stream, "sha256").hexdigest().upper()
                == metadata["pack_sha256"]
            )
    changed = not unchanged
    if changed:
        _write_atomic(destination, data)
    return {
        "operation": "stage",
        "destination": str(destination),
        "changed": changed,
        **metadata,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Build or verify deterministic native D3D12 shader packs."
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    build_parser = subparsers.add_parser("build", help="build a pack from a manifest")
    build_parser.add_argument("manifest", type=pathlib.Path, nargs="+")
    build_parser.add_argument("--output", required=True, type=pathlib.Path)
    build_parser.set_defaults(handler=_build)
    verify_parser = subparsers.add_parser("verify", help="verify a completed pack")
    verify_parser.add_argument("pack", type=pathlib.Path)
    verify_parser.set_defaults(handler=_verify)
    stage_parser = subparsers.add_parser("stage", help="stage an FH1 pack for runtime use")
    stage_parser.add_argument("pack", type=pathlib.Path)
    stage_parser.add_argument("--state-root", required=True, type=pathlib.Path)
    stage_parser.add_argument("--scale", type=int, choices=range(1, 5))
    stage_parser.set_defaults(handler=_stage)
    arguments = parser.parse_args(argv)
    try:
        result = arguments.handler(arguments)
    except (PackError, OSError) as error:
        print(f"native-shader-pack: {error}", file=sys.stderr)
        return 2
    print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
