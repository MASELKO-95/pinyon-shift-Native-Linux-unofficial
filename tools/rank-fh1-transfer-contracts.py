"""Rank ordinary transfer GPU intervals by complete audited contract, not first entry."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from fh1_runtime_log import session_lines


def rank(lines, session, frames):
    if not frames or any(type(f) is not int or f < 0 for f in frames):
        raise ValueError('nonnegative source frames required')
    frames = sorted(set(frames))
    contracts, records = {}, {}
    losses = None

    def uint(row, field, maximum=None):
        value = row.get(field)
        if type(value) is not int or value < 0 or maximum is not None and value > maximum:
            raise ValueError('invalid ' + field)
        return value

    def signature(row):
        value = row.get('signature')
        if not isinstance(value, str) or len(value) != 16 or any(c not in '0123456789ABCDEF' for c in value):
            raise ValueError('missing or invalid contract signature; use the contract audit log')
        return value

    for line in session_lines(lines, session):
        if 'FH1 timing loss reasons ' in line:
            current = json.loads(line.split('FH1 timing loss reasons ', 1)[1])
            for field in ('busy', 'capacity', 'interrupted', 'invalid'):
                if uint(current, field) < (losses[field] if losses else 0):
                    raise ValueError('loss counter decreased')
            losses = current
        if 'FH1 transfer contract ' in line:
            row = json.loads(line.split('FH1 transfer contract ', 1)[1])
            key = signature(row), uint(row, 'index')
            for field in ('dest', 'source', 'host_depth'):
                uint(row, field, 0xFFFFFFFF)
            if uint(row, 'start', 2047) >= uint(row, 'end', 2048):
                raise ValueError('invalid tile range')
            if key in contracts and contracts[key] != row:
                raise ValueError('conflicting contract entry')
            contracts[key] = row
        if 'FH1 render target transfer sample ' not in line:
            continue
        row = json.loads(line.split('FH1 render target transfer sample ', 1)[1])
        if uint(row, 'frame') not in frames:
            continue
        if type(row.get('resolve_clear')) is not bool:
            raise ValueError('invalid resolve_clear')
        if row['resolve_clear']:
            continue  # Old audit signatures omit clear values/rectangles.
        signature(row)
        for field in ('submission', 'record', 'transfers', 'total_ns'):
            uint(row, field)
        if not row['transfers']:
            raise ValueError('empty ordinary transfer sample')
        key = row['submission'], row['record']
        if key in records and records[key] != row:
            raise ValueError('conflicting timing sample')
        records[key] = row
    observed = {row['frame'] for row in records.values()}
    if observed != set(frames):
        raise ValueError('ordinary transfer samples missing for requested frames: ' + str(sorted(set(frames) - observed)))
    groups = {}
    for row in records.values():
        sig = row['signature']
        entries = [contracts.get((sig, i)) for i in range(row['transfers'])]
        if any(entry is None for entry in entries) or (sig, row['transfers']) in contracts:
            raise ValueError('incomplete or oversized contract mapping')
        group = groups.setdefault(sig, dict(signature=sig, entries=entries, calls=0, total_ns=0))
        if group['entries'] != entries:
            raise ValueError('contract length changed')
        group['calls'] += 1
        group['total_ns'] += row['total_ns']
    for group in groups.values():
        group['ms_per_sampled_frame'] = group['total_ns'] / len(frames) / 1e6
    return dict(session=session, frames=frames, records=len(records),
                reported_loss_events=losses,
                complete_reported_samples=losses is not None and not any(losses.values()),
                groups=sorted(groups.values(), key=lambda g: -g['total_ns']),
                scope='Verbose ordinary-transfer audit; full ordered lists, not per-entry attribution. Clear calls excluded. Logging may perturb timing; zero reported session-wide losses does not prove unsampled-frame coverage.')


def self_test():
    def line(kind, row):
        return kind + json.dumps(row)
    start = line('M2_EVENT ', dict(event='logging.ready', session='test'))
    entry = dict(signature='0123456789ABCDEF', index=0, dest=1, source=2, host_depth=0, start=0, end=2048)
    sample = dict(signature=entry['signature'], frame=60, submission=1, record=0, transfers=1, resolve_clear=False, total_ns=1000000)
    contract = lambda row: line('FH1 transfer contract ', row)
    timing = lambda row: line('FH1 render target transfer sample ', row)
    loss = lambda row: line('FH1 timing loss reasons ', row)
    zero = dict(busy=0, capacity=0, interrupted=0, invalid=0)
    lines = [start, contract(entry), timing(sample), timing(sample), loss(zero)]
    result = rank(lines, 'test', [60])
    assert result['records'] == 1 and result['groups'][0]['ms_per_sampled_frame'] == 1
    assert result['complete_reported_samples']
    two = [start, contract(entry), contract(dict(entry, index=1, source=7)),
           timing(dict(sample, transfers=2)), loss(zero)]
    grouped = rank(two, 'test', [60])['groups'][0]
    assert len(grouped['entries']) == 2 and grouped['ms_per_sampled_frame'] == 1
    other = line('M2_EVENT ', dict(event='logging.ready', session='other'))
    assert rank(lines + [other, timing(dict(sample, total_ns=99))], 'test', [60])['groups'][0]['total_ns'] == 1000000
    assert not rank(lines + [loss(dict(zero, capacity=1))], 'test', [60])['complete_reported_samples']
    assert rank(lines[:-1], 'test', [60])['reported_loss_events'] is None
    for bad, frames in [([start, timing(sample)], [60]), (lines, [60, 120]),
                        (lines + [contract(dict(entry, source=3))], [60]),
                        (lines + [timing(dict(sample, total_ns=2))], [60]),
                        (lines + [contract(dict(entry, index=1))], [60]),
                        (lines + [loss(dict(zero, capacity=1)), loss(zero)], [60]),
                        ([start, contract(dict(entry, start=2048)), timing(sample)], [60])]:
        try:
            rank(bad, 'test', frames)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid audit accepted')
    assert rank(lines + [timing(dict(sample, record=1, resolve_clear=True))], 'test', [60])['records'] == 1
    print('transfer contract completeness, deduplication, frame denominator and loss checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', nargs='?', type=Path)
    parser.add_argument('--session')
    parser.add_argument('--frames', nargs='+', type=int)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.log or not args.session or not args.frames:
            parser.error('log, --session and --frames are required')
        with args.log.open(encoding='utf-8-sig') as source:
            print(json.dumps(rank(source, args.session, args.frames), indent=2))
