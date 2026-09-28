"""Summarize a frame-time window; activity counts are not CPU/GPU cost attribution."""
import argparse
import csv
import json
from pathlib import Path

COUNTERS = ('draw_calls', 'command_buffer_stalls', 'texture_cache_hits',
            'texture_cache_misses', 'memexport_bytes', 'memexport_sync_fallbacks',
            'resolve_readback_requests', 'resolve_readback_bytes',
            'resolve_readback_full_waits', 'resolve_readback_wait_time_ns')

TEXTURE_COUNTERS = ('texture_request_cpu_time_ns', 'texture_request_timing_samples',
                    'texture_dirty_load_attempts')


def summarize(rows, start, end):
    if not 0 <= start < end:
        raise ValueError('invalid time window')
    selected = []
    elapsed = 0
    for index, row in enumerate(rows):
        duration = int(row['frame_time_us'])
        if duration < 0:
            raise ValueError('negative frame duration')
        elapsed += duration / 1e6
        if duration and start < elapsed <= end:
            extra = TEXTURE_COUNTERS if any(name in row for name in TEXTURE_COUNTERS) else ()
            if extra and not all(name in row for name in extra):
                raise ValueError('incomplete texture counter schema')
            values = {name: int(row[name]) for name in COUNTERS + extra}
            if any(value < 0 for value in values.values()):
                raise ValueError('negative activity counter')
            if extra and values['texture_request_cpu_time_ns'] and not values['texture_request_timing_samples']:
                raise ValueError('texture time without timing samples')
            selected.append((index, values))
    if not selected or elapsed < end:
        raise ValueError('capture does not cover the requested window')
    names = tuple(selected[0][1])
    if any(tuple(row) != names for _, row in selected):
        raise ValueError('mixed counter schemas')
    timed = [(index, row) for index, row in selected
             if row.get('texture_request_timing_samples', 0) > 0]
    timing = dict(sampled_frames=len(timed),
                  ms_per_sampled_frame=sum(row['texture_request_cpu_time_ns'] for _, row in timed)/len(timed)/1e6,
                  frame_ms={index: row['texture_request_cpu_time_ns']/1e6 for index, row in timed}) if timed else None
    return dict(texture_request_timing=timing, window_seconds=[start, end], frames=len(selected),
                first_row=selected[0][0], last_row=selected[-1][0],
                counters={name: dict(total=sum(row[name] for _, row in selected),
                    per_frame=sum(row[name] for _, row in selected)/len(selected),
                    active_frames=sum(row[name] > 0 for _, row in selected))
                    for name in names},
                scope=('Activity only; wait nanoseconds are not total resource cost. '
                       'D3D12 texture_cache_hits/misses count SRV descriptor lookups, not uploads.'))


def self_test():
    def row(duration, misses):
        return dict.fromkeys(COUNTERS, 0) | dict(frame_time_us=duration, texture_cache_misses=misses)
    rows = [row(1000000, 100), row(0, 500), row(1000000, 2), row(1000000, 4)]
    result = summarize(rows, 1, 3)
    assert result['frames'] == 2 and result['first_row'] == 2
    assert result['counters']['texture_cache_misses'] == dict(total=6, per_frame=3, active_frames=2)
    assert result['texture_request_timing'] is None
    modern = [row(1000000, 0) | dict.fromkeys(TEXTURE_COUNTERS, 0) for _ in range(3)]
    modern[1].update(texture_request_cpu_time_ns=2000000, texture_request_timing_samples=20)
    assert summarize(modern, 0, 3)['texture_request_timing'] == dict(sampled_frames=1, ms_per_sampled_frame=2, frame_ms={1:2})
    invalid_timing = [modern[0] | dict(texture_request_cpu_time_ns=1)]
    for data, start, end in ((invalid_timing, 0, 1), (rows, 3, 4), (rows, 2, 1), ([row(-1, 0)], 0, 1), ([row(1000000,-1)],0,1)):
        try:
            summarize(data, start, end)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid input accepted')
    print('resource-window checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('csv', type=Path, nargs='?')
    parser.add_argument('--start', type=float)
    parser.add_argument('--end', type=float)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if args.csv is None or args.start is None or args.end is None:
            parser.error('supply CSV, --start and --end')
        with args.csv.open(encoding='utf-8-sig', newline='') as stream:
            print(json.dumps(summarize(csv.DictReader(stream), args.start, args.end), indent=2))
