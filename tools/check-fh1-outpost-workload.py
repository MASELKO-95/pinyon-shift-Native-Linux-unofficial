"""Validate the fixed North Carson route's position, motion and visible HUD."""

import importlib.util
import json
import math
from pathlib import Path
import sys

spec = importlib.util.spec_from_file_location(
    'render_test', Path(__file__).with_name('run-fh1-render-test.py'))
render_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(render_test)


def check(output):
    meta = json.loads((output / 'process-samples.json').read_text(encoding='utf-8-sig'))
    session = Path(meta['session_file']).name
    events = render_test.load_events(output / session)
    captures = {e['name']: e for e in events if e.get('event') == 'fh1.render_test.capture'}

    def pose(name):
        e = captures[name]
        if e['vehicle_pose_valid'] != '1':
            raise ValueError('invalid vehicle pose: ' + name)
        return tuple(float(e['vehicle_' + axis]) for axis in 'xyz')

    ready, stationary, moving, stopped = map(pose, (
        'outpost-ready', 'outpost-stationary', 'outpost-moving', 'outpost-stopped'))
    checks = dict(
        arrived=math.dist(ready, (-4271.483398, -5.340042, 1186.721313)) < 5,
        stationary=math.dist(ready, stationary) < 1,
        reverse_motion=3 < math.dist(stationary, moving) < 100,
        stopped_nearby=math.dist(stationary, stopped) < 150)
    fractions = {}
    for name in ('outpost-ready', 'outpost-stationary', 'outpost-moving'):
        width, height, pixels = render_test.ppm_payload(output / (name + '.ppm'))
        pink = total = 0
        # Fixed route/speedometer region; this is not a general HUD detector.
        for y in range(int(height * .75), int(height * .97), 4):
            for x in range(int(width * .82), int(width * .99), 4):
                offset = (y * width + x) * 3
                red, green, blue = pixels[offset:offset + 3]
                pink += red > 120 and red > green * 1.5 and blue > green * 1.15
                total += 1
        fractions[name] = pink / total
        checks['hud_' + name] = fractions[name] > .01
    result = dict(session=session, checks=checks, hud_pink_fractions=fractions,
                  passed=all(checks.values()))
    (output / 'workload-check.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit('usage: check-fh1-outpost-workload.py RUN_DIRECTORY [...]')
    results = [check(Path(name)) for name in sys.argv[1:]]
    print(json.dumps(results, indent=2))
    raise SystemExit(0 if all(r['passed'] for r in results) else 1)
