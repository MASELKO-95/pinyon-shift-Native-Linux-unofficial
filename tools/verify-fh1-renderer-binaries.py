#!/usr/bin/env python3
"""Verify that FH1's normal GPU plugin contains no shader compiler."""

import argparse
import pathlib


TRANSLATOR_MARKERS = (
    b"AnalyzeUcode",
    b"Unsupported host vertex shader type in StartVertexOrDomainShader",
    b"Unsupported host vertex shader type in WriteShaderCode",
)
RETIRED_RUNTIME_MARKERS = (
    b"D3D12 Storage writer",
    b"Resolve Clear 32bpp",
    b"D3D12 - ROV",
    b".rtv.d3d12.xpso",
    b"Isolated MSAA depth readback pipeline",
    b"Qualified native resolve preview",
    b"PinyonShift NR-04D native retained-pass publication",
    b"PinyonShift NR-05F native shadow-depth publication",
    b"Dump DXBC disassembly",
    b"Dump DXIL conversion disassembly",
    b"Failed to disassemble DXBC shader",
    b"Read data written by memory export in shaders on the CPU",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("runtime", type=pathlib.Path)
    parser.add_argument("producer", type=pathlib.Path)
    args = parser.parse_args()

    runtime = args.runtime.read_bytes()
    producer = args.producer.read_bytes()
    leaked = [
        marker.decode()
        for marker in (*TRANSLATOR_MARKERS, *RETIRED_RUNTIME_MARKERS)
        if marker in runtime
    ]
    missing = [marker.decode() for marker in TRANSLATOR_MARKERS if marker not in producer]
    if leaked:
        raise SystemExit(f"runtime contains shader analysis or translator code: {', '.join(leaked)}")
    if missing:
        raise SystemExit(f"producer lacks translator code: {', '.join(missing)}")
    if len(runtime) >= len(producer):
        raise SystemExit("runtime plugin is not smaller than the producer plugin")
    print(
        f"FH1 runtime excludes shader analysis and compiler ({len(runtime):,} bytes; "
        f"producer {len(producer):,} bytes)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
