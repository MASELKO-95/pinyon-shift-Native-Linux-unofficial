#!/usr/bin/env python3
"""Check delivered test inputs and bound the wall-time route / CSV clock offset."""
import argparse
import csv
import json
from pathlib import Path


def check_clock(events, durations):
    configured = [e for e in events if e['event'] == 'fh1.render_test.configured']
    assert len(configured) == 1 and configured[0]['clock'] == 'wall_time', 'one wall-time route required'
    assert len({e['session'] for e in events}) == 1, 'mixed sessions'
    config = configured[0]
    captures = [e for e in events if e['event'] == 'fh1.render_test.capture']
    inputs = [e for e in events if e['event'] == 'fh1.render_test.input_step']
    failures = []
    expected_inputs = int(config['input_steps'])
    if sorted(int(e['index']) for e in inputs) != list(range(expected_inputs)):
        failures.append('not every scheduled input step was delivered to the input API')
    if any(int(e['skipped_steps']) or int(e['observed_frame']) < int(e['scheduled_frame'])
           for e in inputs):
        failures.append('input delivery contains skipped or premature steps')
    if len(captures) != int(config['captures']):
        failures.append('capture count differs from the configured route')
    assert durations and durations[0] == 0 and all(t >= 0 for t in durations), 'invalid or truncated CSV'
    times, total = [], 0
    for duration in durations:
        total += duration
        times.append(total)
    lower, upper = 0, None
    checked = []
    for event in captures:
        index = int(event['trigger_output_frame'])
        trigger = int(event['trigger_elapsed_us'])
        begin = int(event['capture_begin_elapsed_us'])
        end = int(event['capture_end_elapsed_us'])
        assert 0 <= index < len(times) - 1, 'trigger lacks bounding CSV rows'
        assert 0 <= trigger <= begin <= end, 'capture timing is out of order'
        assert trigger * int(config['clock_hz']) >= int(event['frame']) * 1_000_000, 'premature capture'
        # XE_SWAP writes CSV row i before invoking output callback i. The
        # callback finishes before row i+1. Each duration truncates <1 us.
        low = times[index] - trigger
        high = times[index + 1] + index + 1 - end
        lower = max(lower, low)
        upper = high if upper is None else min(upper, high)
        checked.append({'name': event['name'], 'trigger_output_frame': index,
                        'trigger_elapsed_us': trigger, 'capture_begin_elapsed_us': begin,
                        'capture_end_elapsed_us': end, 'offset_bounds_us': [low, high]})
    if upper is None or lower >= upper:
        failures.append('capture anchors do not share a valid route / CSV clock offset')
    return {'passed': not failures, 'failures': failures,
            'route_origin_after_csv_origin_us': [lower, upper], 'captures': checked,
            'input_steps_observed': len(inputs), 'input_steps_expected': expected_inputs,
            'scope': 'Bounds use synchronous D3D12 output callbacks and one CSV row per XE_SWAP. '
                     'Trigger IDs are not captured-image source IDs. Input delivery does not '
                     'prove menu acceptance, arrival, HUD correctness or matched race stage.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('events', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        events = [json.loads(line) for line in args.events.read_text(encoding='utf-8-sig').splitlines()]
        with args.events.with_suffix('.perf.csv').open(encoding='utf-8-sig', newline='') as stream:
            durations = [int(row['frame_time_us']) for row in csv.DictReader(stream)]
        result = check_clock(events, durations)
    except (AssertionError, KeyError, ValueError, OSError) as error:
        result = {'passed': False, 'failures': [f'invalid or unsupported evidence: {error!r}']}
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text, encoding='utf-8')
    print(text, end='')
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
