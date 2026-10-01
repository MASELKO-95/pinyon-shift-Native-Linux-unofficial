"""Export BC3 import inputs from selected fixed-root layered RenderDoc draws.

Run with qrenderdoc --python. Set PINYON_SHIFT_RENDERDOC_CAPTURE,
PINYON_SHIFT_RENDERDOC_EXPORT_DIR (new directory), and
PINYON_SHIFT_RENDERDOC_EVENTS (comma-separated verified draw events).
The bounded raw snapshots are test inputs, not a runtime residency policy.
"""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import renderdoc as rd

cap = rd.OpenCaptureFile()
controller = None
created = False
report = {'textures': []}
try:
    out = Path(os.environ['PINYON_SHIFT_RENDERDOC_EXPORT_DIR'])
    out.mkdir(parents=True, exist_ok=False)
    created = True
    capture = os.environ['PINYON_SHIFT_RENDERDOC_CAPTURE']
    status = cap.OpenFile(capture, '', None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    names = {str(r.resourceId): r.name for r in controller.GetResources()}
    shared = [b.resourceId for b in controller.GetBuffers() if b.length == 1 << 29]
    if len(shared) != 1:
        raise RuntimeError('Expected one shared-memory source')
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    for event in map(int, os.environ['PINYON_SHIFT_RENDERDOC_EVENTS'].split(',')):
        controller.SetFrameEvent(event, True)
        pipeline = controller.GetPipelineState()
        if names[str(pipeline.GetGraphicsPipelineObject())] != 'VS 3BC346726C1C2535, PS 9584B309533EF6C9':
            raise RuntimeError('Unexpected shader pair')
        views = pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel)
        if len(views) != 2 or views[0].descriptor.format.Name() != 'BC3_UNORM':
            raise RuntimeError('Expected the fixed BC3 texture bindings')
        texture = textures[str(views[0].descriptor.resource)]
        if texture.arraysize != 1 or texture.msSamp != 1:
            raise RuntimeError('Expected a single non-MSAA texture')
        cb = controller.GetD3D12PipelineState().rootSignature.parameters[0].descriptor
        fetch = bytes(controller.GetBufferData(cb.resource, cb.byteOffset, 24))
        words = struct.unpack('<6I', fetch)
        directory = out / str(event)
        directory.mkdir()
        (directory / 'fetch.bin').write_bytes(fetch)
        for name, word in (('base', words[1]), ('mips', words[5])):
            address = word & 0x1FFFF000
            size = min(1 << 20, (1 << 29) - address) if address else 0
            data = bytes(controller.GetBufferData(shared[0], address, size)) if size else b''
            if len(data) != size:
                raise RuntimeError('Truncated shared-memory snapshot')
            (directory / (name + '.bin')).write_bytes(data)
        expected = bytearray()
        sizes = []
        for mip in range(texture.mips):
            sub = rd.Subresource()
            sub.mip = mip
            data = bytes(controller.GetTextureData(texture.resourceId, sub))
            sizes.append(len(data))
            expected.extend(data)
        (directory / 'expected.bc3').write_bytes(expected)
        report['textures'].append(dict(event=event, width=texture.width,
            height=texture.height, mip_bytes=sizes, bytes=len(expected),
            sha256=hashlib.sha256(expected).hexdigest()))
    if not report['textures']:
        raise RuntimeError('No texture events selected')
except Exception as error:
    report['error'] = str(error)
finally:
    if controller:
        controller.Shutdown()
    cap.Shutdown()
    if created:
        (out / 'textures.json').write_text(json.dumps(report, indent=2))
    sys.exit(1 if 'error' in report else 0)
