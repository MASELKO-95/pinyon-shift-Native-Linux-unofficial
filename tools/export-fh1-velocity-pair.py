"""Replay a paired velocity capture with qrenderdoc --python.

Set PINYON_SHIFT_RENDERDOC_CAPTURE and PINYON_SHIFT_RENDERDOC_EXPORT_DIR.
The output directory must not exist. Exports local RGBA8 payloads and metadata
for compare-fh1-velocity-pair.py; requires the paired diagnostic draw markers.
"""
import hashlib
import json
import os
from pathlib import Path
import sys

import renderdoc as rd


def flatten(actions):
    for action in actions:
        yield action
        yield from flatten(action.children)


def main():
    capture_path = Path(os.environ['PINYON_SHIFT_RENDERDOC_CAPTURE'])
    output_dir = Path(os.environ['PINYON_SHIFT_RENDERDOC_EXPORT_DIR'])
    output_dir.mkdir(parents=True, exist_ok=False)
    report = {'capture': str(capture_path), 'events': []}
    capture = rd.OpenCaptureFile()
    controller = None
    try:
        with capture_path.open('rb') as source_file:
            digest = hashlib.sha256()
            for block in iter(lambda: source_file.read(1024 * 1024), b''):
                digest.update(block)
        report['capture_sha256'] = digest.hexdigest()
        status = capture.OpenFile(str(capture_path), '', None)
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError(str(status))
        status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
        if status != rd.ResultCode.Succeeded:
            raise RuntimeError(str(status))
        actions = list(flatten(controller.GetRootActions()))
        textures = {str(t.resourceId): t for t in controller.GetTextures()}
        for label, marker in (
                ('native', 'PinyonShift V5 native FH1 velocity dilation'),
                ('guest', 'FH1 velocity parity guest draw')):
            markers = [a for a in actions if a.customName == marker]
            if len(markers) != 1:
                raise ValueError(f'{label}: expected one marker, got {len(markers)}')
            draws = [a for a in flatten(markers[0].children)
                     if a.flags & rd.ActionFlags.Drawcall]
            if len(draws) != 1:
                raise ValueError(f'{label}: expected one draw')
            event = draws[0].eventId
            controller.SetFrameEvent(event, True)
            pipeline = controller.GetPipelineState()
            outputs = [o for o in pipeline.GetOutputTargets()
                       if o.resource != rd.ResourceId.Null()]
            if len(outputs) != 1:
                raise ValueError('expected one output')
            target = outputs[0]
            texture = textures[str(target.resource)]
            if texture.format.Name() != 'R8G8B8A8_UNORM' or texture.msSamp != 1:
                raise ValueError('expected single-sample RGBA8 target')
            sub = rd.Subresource()
            sub.mip, sub.slice = target.firstMip, target.firstSlice
            width = max(1, texture.width >> sub.mip)
            height = max(1, texture.height >> sub.mip)
            viewport, scissor = pipeline.GetViewport(0), pipeline.GetScissor(0)
            if label == 'native':
                inputs = [x.descriptor for x in pipeline.GetReadOnlyResources(rd.ShaderStage.Pixel)
                          if str(x.descriptor.resource) in textures]
                if len(inputs) != 1:
                    raise ValueError('expected one native source')
                source = inputs[0]
                source_sub = rd.Subresource()
                source_sub.mip, source_sub.slice = source.firstMip, source.firstSlice
                controller.SetFrameEvent(event - 1, True)
                (output_dir / 'before.rgba').write_bytes(
                    bytes(controller.GetTextureData(target.resource, sub)))
                controller.SetFrameEvent(event, True)
            data = bytes(controller.GetTextureData(target.resource, sub))
            if len(data) != width * height * 4:
                raise ValueError('unexpected packed output byte count')
            (output_dir / (label + '.rgba')).write_bytes(data)
            source_data = bytes(controller.GetTextureData(source.resource, source_sub))
            report['events'].append(dict(
                label=label, event_id=event, target=str(target.resource),
                width=width, height=height, mip=sub.mip, slice=sub.slice,
                viewport=[viewport.x, viewport.y, viewport.width, viewport.height],
                scissor=[scissor.x, scissor.y, scissor.width, scissor.height],
                source=str(source.resource),
                source_sha256=hashlib.sha256(source_data).hexdigest(),
                output_sha256=hashlib.sha256(data).hexdigest()))
        native, guest = report['events']
        report['intervening_draws'] = [a.eventId for a in actions
            if a.flags & rd.ActionFlags.Drawcall
            and native['event_id'] < a.eventId < guest['event_id']]
    except Exception as error:
        report['error'] = str(error)
    finally:
        if controller is not None:
            controller.Shutdown()
        capture.Shutdown()
        (output_dir / 'paired-output.json').write_text(json.dumps(report, indent=2))
    return 1 if 'error' in report else 0


try:
    sys.exit(main())
except Exception as error:
    sys.stderr.write(str(error) + '\n')
    sys.exit(1)
