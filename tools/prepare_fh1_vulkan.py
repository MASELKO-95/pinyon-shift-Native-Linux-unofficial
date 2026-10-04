"""Warm Vulkan pipelines in a private fresh profile without touching player saves."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def prepare(state, game, build, emit=print):
    state, game, build = map(Path, (state, game, build))
    cache = state / 'cache'
    if any((cache / 'shaders/shareable').glob('*.vk.xpso')):
        return True
    executable = (build / 'pinyon_shift').stat()
    fingerprint = f'{executable.st_size}:{executable.st_mtime_ns}'
    receipt = cache / 'fh1-vulkan-preparation-skipped.json'
    try:
        if json.loads(receipt.read_text()).get('build') == fingerprint:
            return False
    except (OSError, ValueError):
        pass
    parent = ROOT / '.local/native-renderer'
    parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='vulkan-preparation-', dir=parent))
    private = work / 'state'
    (private / 'config').mkdir(parents=True)
    config = state / 'config/pinyon_shift.toml'
    if config.is_file():
        shutil.copy2(config, private / 'config/pinyon_shift.toml')
    emit('Preparing Vulkan shaders in a private profile; the first start may take a few minutes.')
    command = [sys.executable, str(ROOT / 'tools/pinyon.py'), 'launch',
               '--state-root', str(private), '--game-root', str(game),
               '--build-directory', str(build), '--hidden', '--skip-shader-preparation',
               '--render-test-script', str(ROOT / 'config/render-tests/fh1-shader-preparation.fh1test'),
               '--render-test-output', str(work / 'output'), '--include-opening-movies',
               '--timeout', '600', '--json']
    with (work / 'preparation.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    complete = 0
    for path in (private / 'logs').glob('*.jsonl'):
        for line in path.read_text(errors='replace').splitlines():
            try:
                complete += json.loads(line).get('event') == 'fh1.render_test.complete'
            except (ValueError, AttributeError):
                pass
    if result.returncode == 0 and complete == 1:
        for relative, patterns in [('cache/shaders/shareable', ('*.xsh', '*.vk.xpso')),
                                   ('cache/shaders', ('*.vkpipelinecache.*.bin',))]:
            for pattern in patterns:
                for source in (private / relative).glob(pattern):
                    destination = state / relative / source.name
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    # Publish complete files exclusively; never replace existing storage.
                    fd, temporary = tempfile.mkstemp(dir=destination.parent)
                    os.close(fd)
                    try:
                        shutil.copyfile(source, temporary)
                        try:
                            os.link(temporary, destination)
                        except FileExistsError:
                            pass
                    finally:
                        Path(temporary).unlink()
        if any((cache / 'shaders/shareable').glob('*.vk.xpso')):
            shutil.rmtree(work)
            emit('Vulkan shaders are prepared.')
            return True
    cache.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps({'schema_version': 1, 'build': fingerprint}) + '\n')
    emit(f'Vulkan preparation was skipped; the game can still run. Details: {work}')
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--state-root', type=Path, default=ROOT / '.local/preview')
    parser.add_argument('--game-root', type=Path, default=ROOT / '.local/game/base')
    parser.add_argument('--build-directory', type=Path, default=ROOT / 'out/build/linux-amd64-release')
    parser.add_argument('--json-events', action='store_true')
    args = parser.parse_args()
    def emit(message):
        print('::pinyon::' + json.dumps(dict(stage='shaders', percent=98, message=message))
              if args.json_events else message, flush=True)
    prepare(args.state_root.resolve(), args.game_root.resolve(), args.build_directory.resolve(), emit)


if __name__ == '__main__':
    main()
