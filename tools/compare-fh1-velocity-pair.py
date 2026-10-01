"""Check same-frame native/guest velocity outputs exported from RenderDoc."""
import argparse
import hashlib
import json
from pathlib import Path


def compare(metadata, before, native, guest):
    if metadata.get('error'):
        raise ValueError('export failed: ' + str(metadata['error']))
    events = metadata['events']
    if len(events) != 2 or [e['label'] for e in events] != ['native', 'guest']:
        raise ValueError('expected one ordered native/guest pair')
    a, b = events
    for key in ('target', 'width', 'height', 'mip', 'slice', 'viewport',
                'scissor', 'source', 'source_sha256'):
        if a[key] != b[key]:
            raise ValueError('pair differs in ' + key)
    if a['event_id'] >= b['event_id'] or metadata['intervening_draws']:
        raise ValueError('draws are not an uninterrupted ordered pair')
    expected = a['width'] * a['height'] * 4
    if expected <= 0 or any(len(data) != expected for data in (before, native, guest)):
        raise ValueError('invalid tightly packed RGBA8 output length')
    for event, data in ((a, native), (b, guest)):
        if event.get('output_sha256') != hashlib.sha256(data).hexdigest():
            raise ValueError('output payload does not match exported hash')
    changed = sum(x != y for x, y in zip(before, native))
    equal = native == guest
    return {'native_event': a['event_id'], 'guest_event': b['event_id'],
            'compared_bytes': expected, 'native_changed_bytes': changed,
            'outputs_identical': equal, 'verified': equal and changed > 0,
            'scope': 'one captured velocity draw pair; not whole-renderer parity'}


def self_test():
    a = dict(label='native', event_id=1, target='rt', width=1, height=1,
             mip=0, slice=0, viewport=[0, 0, 1, 1], scissor=[0, 0, 1, 1],
             source='texture', source_sha256='a' * 64,
             output_sha256=hashlib.sha256(b'1234').hexdigest())
    b = dict(a, label='guest', event_id=2)
    metadata = dict(events=[a, b], intervening_draws=[])
    assert compare(metadata, b'0000', b'1234', b'1234')['verified']
    assert not compare(metadata, b'1234', b'1234', b'1234')['verified']
    different = dict(metadata, events=[a, dict(
        b, output_sha256=hashlib.sha256(b'1235').hexdigest())])
    assert not compare(different, b'0000', b'1234', b'1235')['verified']
    for bad in (dict(metadata, intervening_draws=[1]),
                dict(metadata, error='missing native marker'),
                dict(metadata, events=[]),
                dict(metadata, events=[a, dict(b, output_sha256='0' * 64)]),
                dict(metadata, events=[a, dict(b, source_sha256='b' * 64)])):
        try:
            compare(bad, b'0000', b'1234', b'1234')
        except ValueError:
            pass
        else:
            raise AssertionError('invalid pair accepted')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path, nargs='?')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if args.directory is None:
            parser.error('an exported pair directory is required')
        root = args.directory
        result = compare(json.loads((root / 'paired-output.json').read_text()),
                         *(root.joinpath(name + '.rgba').read_bytes()
                           for name in ('before', 'native', 'guest')))
        print(json.dumps(result, indent=2))
        raise SystemExit(0 if result['verified'] else 1)
