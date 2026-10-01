"""Rank texture GPU samples from one explicit session, including merged runtime logs."""
import argparse
import json
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fh1_runtime_log import session_lines


def rank(lines, session, frames):
    frames = sorted(set(frames))
    if not frames or any(type(f) is not int or f < 0 for f in frames):
        raise ValueError('invalid source frames')
    records = {}
    losses = None
    for line in session_lines(lines, session):
        if 'FH1 timing loss reasons ' in line:
            current = json.loads(line.split('FH1 timing loss reasons ', 1)[1])
            for field in ('busy', 'capacity', 'interrupted', 'invalid'):
                if type(current.get(field)) is not int or current[field] < 0:
                    raise ValueError('invalid timing loss count')
                if losses is not None and current[field] < losses[field]:
                    raise ValueError('timing loss counts decreased within session')
            losses = current
        if 'FH1 texture load sample ' not in line:
            continue
        row = json.loads(line.split('FH1 texture load sample ', 1)[1])
        if row['frame'] not in frames:
            continue
        for field in ('frame', 'submission', 'record', 'width', 'height', 'format',
                      'pitch', 'packed', 'endian', 'signed', 'conversion_ns', 'copy_ns'):
            if type(row[field]) is not int or row[field] < 0:
                raise ValueError('invalid sample')
        key = row['submission'], row['record']
        if key in records and records[key] != row:
            raise ValueError('conflicting sample')
        records[key] = row
    if not records:
        raise ValueError('no samples in selected session/frames')
    groups = {}
    identity = ('base', 'width', 'height', 'format', 'pitch', 'packed', 'endian', 'signed')
    for row in records.values():
        key = tuple(row[x] for x in identity)
        group = groups.setdefault(key, dict(zip(identity, key), loads=0, conversion_ns=0, copy_ns=0))
        group['loads'] += 1
        for field in ('conversion_ns', 'copy_ns'):
            group[field] += row[field]
    for group in groups.values():
        group['gpu_ms_per_sampled_frame'] = (group['conversion_ns'] + group['copy_ns']) / len(frames) / 1e6
    per_frame = {f: dict(loads=sum(r['frame'] == f for r in records.values()),
                        conversion_ms=sum(r['conversion_ns'] for r in records.values() if r['frame'] == f)/1e6,
                        copy_ms=sum(r['copy_ns'] for r in records.values() if r['frame'] == f)/1e6)
                 for f in frames}
    return dict(session=session, frames=per_frame, records=len(records),
                reported_loss_events=losses,
                loss_scope='Latest session-wide cumulative report; not localized to selected frames; null means unavailable; events may count one sample more than once',
                groups=sorted(groups.values(), key=lambda r: -r['gpu_ms_per_sampled_frame']),
                scope='Observed base-only scaled 2D loads; excludes dropped samples and setup before conversion; overlaps pass spans')


def self_test():
    def start(session):
        return 'M2_EVENT ' + json.dumps(dict(event='logging.ready', session=session))
    def sample(frame=60, ns=1000000):
        return 'FH1 texture load sample ' + json.dumps(dict(frame=frame, submission=frame,
            record=0, base='1000', width=32, height=32, format=6, pitch=32,
            packed=0, endian=0, signed=0, conversion_ns=ns, copy_ns=1000000))
    lines = [start('a'), sample(), start('b'), sample(ns=2000000), sample(ns=2000000)]
    assert rank(lines, 'a', [60])['groups'][0]['gpu_ms_per_sampled_frame'] == 2
    r = rank(lines, 'b', [60, 120])
    assert r['records'] == 1 and r['frames'][120]['loads'] == 0
    assert r['groups'][0]['gpu_ms_per_sampled_frame'] == 1.5
    assert r['reported_loss_events'] is None
    loss = 'FH1 timing loss reasons '
    zero = dict(busy=0, capacity=0, interrupted=0, invalid=0)
    assert rank(lines + [loss + json.dumps(zero)], 'b', [60])['reported_loss_events'] == zero
    partial = dict(zero, capacity=3)
    assert rank(lines + [loss + json.dumps(partial)], 'b', [60])['reported_loss_events'] == partial
    for bad, session in [(lines, 'missing'), (lines + [sample(ns=1)], 'b'),
                         (lines + [loss + json.dumps(dict(zero, invalid=-1))], 'b'),
                         (lines + [loss + json.dumps(partial), loss + json.dumps(zero)], 'b'),
                         ([start('a'), sample(ns=-1)], 'a')]:
        try:
            rank(bad, session, [60])
        except ValueError:
            pass
        else:
            raise AssertionError('invalid input accepted')
    print('texture GPU ranking session, deduplication and denominator checks pass')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', nargs='?', type=Path)
    parser.add_argument('--session')
    parser.add_argument('--frames', type=int, nargs='+')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.log or not args.session or not args.frames:
            parser.error('log, --session and --frames are required')
        with args.log.open(encoding='utf-8-sig') as source:
            print(json.dumps(rank(source, args.session, args.frames), indent=2))
