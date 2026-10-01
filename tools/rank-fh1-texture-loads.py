"""Rank successful texture-load trace messages, not bytes transferred or cost."""
import argparse
from collections import Counter
from datetime import datetime
import json
from pathlib import Path
import re

STAMP = re.compile(r'^\[([^]]+)\]')
LOAD = re.compile(r'\[gpu\] \[t\d+\] Loaded (from CPU )?(.+ texture with .+)$')


def summarize(lines, start, end):
    if not 0 <= start < end:
        raise ValueError('invalid elapsed wall-time window')
    origin = None
    latest = 0
    counts = Counter()
    for line in lines:
        stamp = STAMP.match(line)
        if not stamp:
            continue
        moment = datetime.fromisoformat(stamp[1])
        if origin is None:
            origin = moment
        elapsed = (moment - origin).total_seconds()
        latest = max(latest, elapsed)
        if start <= elapsed < end:
            load = LOAD.search(line.rstrip())
            if load:
                counts[('cpu' if load[1] else 'resident_memory', load[2])] += 1
    if latest < end:
        raise ValueError('log does not reach window end; check rotation/truncation')
    return {'window_wall_seconds': [start, end], 'origin': str(origin),
            'successful_load_messages': sum(counts.values()),
            'scaled_load_messages': sum(n for (_, key), n in counts.items()
                                        if key.startswith(('tiled scaled ', 'linear scaled '))),
            'textures': [{'path': path, 'identity': identity, 'loads': count}
                         for (path, identity), count in counts.most_common()]}


def self_test():
    def line(second, action):
        return f'[2026-09-07 12:00:{second:02}.000] [trace] [gpu] [t1] {action}'
    identity = 'tiled scaled 2x2x1 2D R8 texture with 1 unpacked mip level'
    result = summarize([line(0, 'Created ' + identity), line(1, 'Loaded ' + identity),
                        line(1, 'Loaded ' + identity),
                        line(2, 'Loaded from CPU ' + identity),
                        line(3, 'Loaded ' + identity)], 1, 3)
    assert result['successful_load_messages'] == result['scaled_load_messages'] == 3
    assert [row['loads'] for row in result['textures']] == [2, 1]
    assert result['textures'][1]['path'] == 'cpu'
    for start, end in [(2, 1), (0, 4)]:
        try:
            summarize([line(0, 'start'), line(3, 'end')], start, end)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid window accepted')
    print('texture-load ranking checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', nargs='?', type=Path)
    parser.add_argument('--start', type=float, default=26)
    parser.add_argument('--end', type=float, default=30)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if args.log is None:
            parser.error('log is required')
        with args.log.open(encoding='utf-8') as source:
            print(json.dumps(summarize(source, args.start, args.end), indent=2))
