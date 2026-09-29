#!/usr/bin/env python3
"""Build the FH1 D3D12 startup-prewarm allowlist from a V4 corpus."""

import argparse
import json
import shutil
from pathlib import Path


SCHEMA = "pinyon-shift.fh1-gpu-prewarm.v3"
# The admitted velocity-dilate pass uses an owned native pipeline, not a
# PipelineCache descriptor. Keep its draw identity but do not request an XPSo.
NATIVE_PIPELINES = {"6E456C111D3FA84D"}


def read_pipeline_storage(legacy_cache: Path) -> bytes:
    data = (legacy_cache / "shaders/shareable/4D5309C9.rtv.d3d12.xpso").read_bytes()
    if data[:4] != b"XEPS" or len(data) < 12 or (len(data) - 12) % 72:
        raise ValueError(f"invalid legacy FH1 catalog: {legacy_cache}")
    return data


def stored_pipelines(legacy_cache: Path) -> set[str]:
    """Every pipeline the producer created while replaying its route."""
    data = read_pipeline_storage(legacy_cache)
    return {
        f"{int.from_bytes(data[offset:offset + 8], 'little'):016X}"
        for offset in range(12, len(data), 72)
    } - NATIVE_PIPELINES


def stage_native_catalog(legacy_cache: Path, manifest: Path, output_root: Path) -> None:
    shader_source = legacy_cache / "fh1-native-shaders-v2.bin"
    header = shader_source.read_bytes()[:16]
    if (len(header) != 16 or header[:4] != b"FHSA" or
            int.from_bytes(header[4:8], "little") != 1 or
            int.from_bytes(header[8:12], "little") == 0 or header[12:] != bytes(4)):
        raise ValueError(f"invalid FH1 analysis catalog: {shader_source}")
    shutil.copyfile(shader_source, output_root / "fh1-native-shaders-v2.bin")

    data = read_pipeline_storage(legacy_cache)
    allowed = {
        int(line[2:], 16)
        for line in manifest.read_text(encoding="ascii").splitlines()
        if line.startswith("P ")
    }
    records = {data[offset : offset + 72] for offset in range(12, len(data), 72)}
    selected = sorted(record for record in records if int.from_bytes(record[:8], "little") in allowed)
    if {int.from_bytes(record[:8], "little") for record in selected} != allowed:
        raise ValueError("FH1 prewarm manifest references missing pipeline descriptions")
    (output_root / "fh1-native-pipelines-v1.bin").write_bytes(
        data[:12] + b"".join(selected)
    )


def build(corpus: Path | list[Path], output: Path,
          extra_pipelines: set[str] = frozenset()) -> tuple[int, int, int]:
    sources = [corpus] if isinstance(corpus, Path) else corpus
    pipelines, draws, copies = set(extra_pipelines), set(), set()
    for source in sources:
        text = source.read_text(encoding="utf-8")
        if text.startswith(SCHEMA + "\n"):
            for line in text.splitlines()[1:]:
                if len(line) != 18 or line[:2] not in {"P ", "D ", "C "}:
                    raise ValueError("invalid FH1 pipeline prewarm manifest")
                try:
                    value = f"{int(line[2:], 16):016X}"
                except ValueError as error:
                    raise ValueError(
                        "invalid FH1 pipeline prewarm manifest"
                    ) from error
                if line.startswith("P ") and value in NATIVE_PIPELINES:
                    continue
                {"P ": pipelines, "D ": draws, "C ": copies}[line[:2]].add(
                    value
                )
            continue
        data = json.loads(text)
        if data.get("schema") not in {
            "pinyon-shift.fh1-gpu-corpus.v1",
            "pinyon-shift.fh1-gpu-corpus.v2",
            "pinyon-shift.fh1-gpu-corpus.v3",
        } or data.get("key_version") != 2:
            raise ValueError("unsupported FH1 GPU corpus")
        pipelines.update(
            entry["pipeline_state"].upper() for entry in data["entries"]
            if entry.get("kind") == 1 and entry.get("pipeline_state") != "0000000000000000"
            and entry.get("pipeline_state", "").upper() not in NATIVE_PIPELINES
        )
        draws.update(entry["identity"].upper() for entry in data["entries"]
                     if entry.get("kind") == 1)
        copies.update(entry["identity"].upper() for entry in data["entries"]
                      if entry.get("kind") == 2)
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.with_suffix(output.suffix + ".tmp")
    lines = ([SCHEMA] + [f"P {value}" for value in sorted(pipelines)] +
             [f"D {value}" for value in sorted(draws)] +
             [f"C {value}" for value in sorted(copies)])
    staging.write_text("\n".join(lines) + "\n", encoding="ascii")
    staging.replace(output)
    return len(pipelines), len(draws), len(copies)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("corpus", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--merge", type=Path, action="append", default=[])
    parser.add_argument("--legacy-cache", type=Path)
    # The native renderer records no per-draw execution keys, so the corpus
    # names no pipelines; prewarm everything the producer's route created.
    parser.add_argument("--all-stored-pipelines", action="store_true")
    args = parser.parse_args()
    if args.all_stored_pipelines and not args.legacy_cache:
        parser.error("--all-stored-pipelines requires --legacy-cache")
    extra = stored_pipelines(args.legacy_cache) if args.all_stored_pipelines else set()
    pipelines, draws, copies = build([args.corpus, *args.merge], args.output, extra)
    if args.legacy_cache:
        stage_native_catalog(args.legacy_cache, args.output, args.output.parent)
    print(f"pipelines={pipelines} draws={draws} copies={copies}")


if __name__ == "__main__":
    main()
