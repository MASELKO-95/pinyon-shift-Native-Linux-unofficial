"""List and install the user's original FH1 STFS DLC packages without touching saves."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
TITLE = '4D5309C9'


def content_root(state):
    return state / 'user/0000000000000000' / TITLE


def list_content(state):
    root = content_root(state) / '00000002'
    return [{'id': path.name, 'path': str(path)} for path in sorted(root.glob('*'))
            if path.is_dir() and not path.is_symlink()]


def install(package, state, extractor):
    from pinyon import game_running
    if game_running():
        raise ValueError('Close Pinyon Shift before installing DLC.')
    if not package.is_file():
        raise ValueError('Select an original Xbox 360 STFS DLC package file.')
    if not extractor.is_file():
        raise ValueError('Rebuild this version first: the DLC extraction tool is missing.')
    with package.open('rb') as stream:
        signature = stream.read(4)
        if signature not in (b'LIVE', b'PIRS', b'CON '):
            raise ValueError('Expected an Xbox 360 LIVE/PIRS/CON package, not a ZIP or directory.')
        stream.seek(0)
        package_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
    # A stable filename fits the runtime's 42-byte content-name field.
    name = package_hash[:40].upper()
    root = content_root(state)
    destination = root / '00000002' / name
    header = root / 'Headers/00000002' / (name + '.header')
    if destination.exists() or header.exists():
        raise ValueError('This DLC is already installed, or its destination exists; no files were replaced.')
    cache = state / 'cache/dlc-import'
    cache.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='package-', dir=cache) as temporary:
        stage = Path(temporary) / 'extracted'
        subprocess.run([str(extractor), str(package), str(stage), name], check=True)
        if not (stage / 'payload').is_dir() or not (stage / 'package.header').is_file():
            raise ValueError('DLC extraction did not produce a complete package.')
        # Detect replacement or modification of the input during extraction.
        with package.open('rb') as stream:
            if hashlib.file_digest(stream, 'sha256').hexdigest() != package_hash:
                raise ValueError('The package changed during extraction; retry with a stable file.')
        destination.parent.mkdir(parents=True, exist_ok=True)
        header.parent.mkdir(parents=True, exist_ok=True)
        # Link the metadata without clobbering an existing file. Publish the
        # complete directory only after extraction succeeds; no partial DLC is
        # visible to the game's content enumerator.
        os.link(stage / 'package.header', header)
        try:
            if destination.exists():
                raise ValueError('DLC destination appeared during installation.')
            (stage / 'payload').rename(destination)
        except BaseException:
            header.unlink()
            raise
    return {'installed': name, 'path': str(destination), 'sha256': package_hash}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('list', 'install'))
    parser.add_argument('--state-root', type=Path, default=ROOT / '.local/preview')
    parser.add_argument('--package', type=Path)
    parser.add_argument('--extractor', type=Path, default=ROOT / 'out/build/linux-amd64-release/pinyon_shift_dlc_extract')
    args = parser.parse_args()
    try:
        if args.action == 'install':
            if args.package is None:
                parser.error('install requires --package')
            result = install(args.package.resolve(), args.state_root.resolve(), args.extractor.resolve())
        else:
            result = {'packages': list_content(args.state_root.resolve())}
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
