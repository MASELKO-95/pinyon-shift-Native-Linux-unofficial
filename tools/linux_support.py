#!/usr/bin/env python3
"""Data operations shared by the Linux shell tools (Python 3.11+, no pip packages)."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest().upper()


def atomic_write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', dir=path.parent)
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='') as stream:
            stream.write(text)
        os.replace(temporary, path)
    finally:
        Path(temporary).unlink(missing_ok=True)


def write_json(path: Path, value: dict) -> None:
    atomic_write(path, json.dumps(value, indent=2) + '\n')


def executable_results(dump: dict, root: Path) -> list[dict]:
    results = []
    for entry in dump['executables']:
        path = root / entry['guest_path']
        exists = path.is_file()
        size = path.stat().st_size if exists else None
        sha = digest(path) if exists else None
        results.append(dict(guest_path=entry['guest_path'], role=entry['role'], exists=exists,
                            size_bytes=size, sha256=sha,
                            matches=size == entry['size_bytes'] and sha == entry['sha256'].upper()))
    return results


def verify(iso: Path | None, extracted: Path | None, manifest: Path | None = None,
           experimental_manifest: Path | None = None, dump_id: str | None = None) -> dict:
    dumps = json.loads((manifest or ROOT / 'config/supported-dumps.json').read_text())['dumps']
    if experimental_manifest is not None:
        dumps += json.loads(experimental_manifest.read_text())['dumps']
    if dump_id is not None:
        dumps = [dump for dump in dumps if dump['id'] == dump_id]
        if not dumps:
            raise ValueError('Selected disc edition is unavailable; experimental editions require --experimental.')
    if iso is not None:
        size, sha = iso.stat().st_size, digest(iso)
        matched = next((d for d in dumps if d['iso']['size_bytes'] == size and
                        d['iso']['sha256'].upper() == sha), None)
        if matched is None:
            return dict(recognized=False, size_bytes=size, sha256=sha,
                        reason='No exact size and SHA-256 match for the selected supported disc edition(s).')
    else:
        if extracted is None:
            raise ValueError('provide --iso-path or --extracted-root')
        matched = next((d for d in dumps if all(e['matches'] for e in
                       executable_results(d, extracted))), None)
        if matched is None:
            return dict(recognized=False, reason='Extracted executables do not match a supported dump.')
        size, sha = None, None
    results = executable_results(matched, extracted) if extracted else []
    return dict(recognized=True, dump_id=matched['id'], serial=matched['serial'],
                title_id=matched['title_id'], status=matched['status'],
                iso_size_bytes=size, iso_sha256=sha, executables=results,
                extracted_executables_checked=len(results),
                extracted_executables_match=all(e['matches'] for e in results))


def install_archive(archive: Path, destination: Path, executable: str) -> None:
    """Extract only verified archives; reject escaping members and links."""
    if destination.exists():
        raise ValueError(f'incomplete toolchain exists at {destination}; move it aside and retry')
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
        stage = Path(temporary)
        if zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as source:
                for entry in source.infolist():
                    if not (stage / entry.filename).resolve().is_relative_to(stage.resolve()):
                        raise ValueError('archive member escapes its destination')
                    if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                        raise ValueError('ZIP symlinks are not supported')
                source.extractall(stage)
        else:
            # The data filter also checks symlink and hardlink targets.
            with tarfile.open(archive) as source:
                if not hasattr(tarfile, 'data_filter'):
                    raise ValueError('update Python to 3.11.8+ for safe tar extraction')
                source.extractall(stage, filter='data')
        payload = stage
        if not (payload / executable).is_file():
            children = list(stage.iterdir())
            if len(children) == 1 and children[0].is_dir():
                payload = children[0]
        binary = payload / executable
        if not binary.is_file():
            raise ValueError(f'archive does not contain {executable}')
        binary.chmod(binary.stat().st_mode | 0o111)
        # TemporaryDirectory must retain its root for cleanup.
        target = stage / '.install' if payload == stage else payload
        if payload == stage:
            target.mkdir()
            for child in list(stage.iterdir()):
                if child != target:
                    child.rename(target / child.name)
        target.rename(destination)


def toml_set(text: str, name: str, value: str) -> str:
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', name) or '\n' in value or '\r' in value:
        raise ValueError('expected a simple setting name and single-line TOML value')
    pattern = r'(?m)^[ \t]*' + re.escape(name) + r'[ \t]*=[^\r\n]*'
    line = f'{name} = {value}'
    if re.search(pattern, text):
        return re.sub(pattern, lambda _: line, text, count=1)
    newline = '\r\n' if '\r\n' in text else '\n'
    return (text.rstrip('\r\n') + newline if text.rstrip('\r\n') else '') + line + newline


def toml_get(text: str, name: str, default: str = '') -> str:
    found = re.search(r'(?m)^[ \t]*' + re.escape(name) + r'[ \t]*=([^#\r\n]*)', text)
    return (found[1].strip().strip('"') or default) if found else default


def read_text(path: Path) -> str:
    return path.read_bytes().decode('utf-8-sig') if path.exists() else ''


def backup_config(path: Path) -> Path | None:
    if not path.is_file():
        return None
    target = path.parent / 'backups' / ('pinyon_shift-' +
        datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + '.toml')
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(path, target)
    return target


def repair_trainer_literal(text: str) -> str:
    # Older trainer menus wrote this string cvar without TOML quotes. Repair
    # only the known generated forms, never guess how to fix unrelated edits.
    pattern = r'(?m)^([ \t]*cheat_set_profile_fields[ \t]*=[ \t]*)([^\r\n]*)'
    def replace(match):
        value, separator, comment = match[2].partition('#')
        value = value.strip()
        if not value or re.fullmatch(r'Main/WristbandLevel=[0-9]+', value):
            return match[1] + json.dumps(value) + (' #' + comment if separator else '')
        return match[0]
    return re.sub(pattern, replace, text)


def prepare_launch_config(state: Path) -> None:
    path = state / 'config/pinyon_shift.toml'
    if not path.is_file():
        return
    original = read_text(path)
    repaired = repair_trainer_literal(original)
    # Upgrade only the old PC preset's default arrow bindings. Custom layouts
    # and controller-only setups must retain their existing assignments.
    if (toml_get(repaired, 'mnk_mode') == 'true' and
            toml_get(repaired, 'keybind_right_trigger') == 'W' and
            toml_get(repaired, 'keybind_left_trigger') == 'S'):
        for direction in ('up', 'down', 'left', 'right'):
            key = direction.capitalize()
            dpad, stick = f'keybind_dpad_{direction}', f'keybind_rstick_{direction}'
            if (toml_get(repaired, dpad, 'Shift+' + key) == 'Shift+' + key and
                    toml_get(repaired, stick, key) == key):
                repaired = toml_set(repaired, dpad, json.dumps(key))
                repaired = toml_set(repaired, stick, json.dumps('Shift+' + key))
    # A failed SDK parse otherwise silently falls back to English and defaults.
    try:
        tomllib.loads(repaired)
    except tomllib.TOMLDecodeError as error:
        raise ValueError(f'Invalid configuration in {path}: {error}') from error
    if repaired != original:
        backup_config(path)
        atomic_write(path, repaired)


def graphics(args: argparse.Namespace) -> dict:
    path = args.state_root / 'config/pinyon_shift.toml'
    text = read_text(path)
    backup = None
    if args.action != 'get':
        # Keep old schemas for the game's own migration. Never mark a partial
        # launcher edit as a completed migration to the current schema.
        schema = int(toml_get(text, 'pinyon_shift_config_schema', '0'))
        if schema > 27:
            raise ValueError('configuration is newer than this launcher')
        if args.action == 'restore':
            choices = sorted((path.parent / 'backups').glob('pinyon_shift-*.toml'))
            if not choices:
                raise ValueError('no configuration backup available')
            restored = read_text(choices[-1])
            backup = backup_config(path)
            text = restored
        elif args.action == 'reset':
            backup = backup_config(path)
            text = ''  # The game supplies defaults and performs schema migration.
        else:
            backup = backup_config(path)
            settings = {'gpu_backend': '"vulkan"', 'gpu_record_thread': 'true'}
            for argument, key in [('output_scaling', 'present_effect'),
                                  ('post_effect', 'swap_post_effect')]:
                if getattr(args, argument) is not None:
                    settings[key] = json.dumps(getattr(args, argument))
            for argument, key in [('presentation_fps', 'host_present_fps_limit'),
                                  ('render_fps', 'pinyon_shift_fh1_render_fps_limit'),
                                  ('disable_motion_blur', 'disable_motion_blur'),
                                  ('disable_depth_of_field', 'disable_depth_of_field'),
                                  ('treasure_map', 'pinyon_shift_dlc_treasure_map')]:
                if getattr(args, argument) is not None:
                    settings[key] = str(getattr(args, argument))
            if args.resolution_scale is not None:
                settings.update(draw_resolution_scale_x=str(args.resolution_scale),
                                draw_resolution_scale_y=str(args.resolution_scale))
            if args.anisotropy is not None:
                settings['anisotropic_override'] = str({4: 3, 8: 4, 16: 5}[args.anisotropy])
            if getattr(args, 'game_language', None):
                languages = json.loads((ROOT / 'config/linux-game-languages.json').read_text())['languages']
                language = next((item for item in languages if item['id'] == args.game_language), None)
                if language is None:
                    raise ValueError('Unknown game language')
                settings.update(user_language=str(language['user_language']),
                                user_country=str(language['user_country']))
            controls = getattr(args, 'controls', None)
            if controls is not None:
                settings['mnk_mode'] = 'false' if controls == 'controller' else 'true'
                settings['mnk_mouse'] = 'true' if controls == 'pc-camera' else 'false'
                settings['mnk_mouse_steering'] = 'true' if controls == 'pc-steering' else 'false'
                if controls != 'controller':
                    # Explicit opt-in replaces keyboard bindings; the configuration
                    # backup lets players recover their previous custom layout.
                    bindings = dict(keybind_right_trigger='W', keybind_left_trigger='S',
                        keybind_lstick_left='A', keybind_lstick_right='D',
                        keybind_lstick_up='', keybind_lstick_down='',
                        keybind_a='Space,Return', keybind_b='E,Backspace',
                        keybind_x='Q', keybind_y='R', keybind_start='Escape',
                        keybind_back='Tab', keybind_left_shoulder='C',
                        keybind_right_shoulder='V', keybind_lstick_press='H',
                        keybind_rstick_press='F',
                        keybind_dpad_up='Up', keybind_dpad_down='Down',
                        keybind_dpad_left='Left', keybind_dpad_right='Right',
                        keybind_rstick_up='Shift+Up', keybind_rstick_down='Shift+Down',
                        keybind_rstick_left='Shift+Left', keybind_rstick_right='Shift+Right')
                    settings.update({key: json.dumps(value) for key, value in bindings.items()})
            if getattr(args, 'trainer', None) is not None:
                settings['pinyon_shift_cheats'] = args.trainer
            for key, value in settings.items():
                text = toml_set(text, key, value)
        text = repair_trainer_literal(text)
        tomllib.loads(text)
        atomic_write(path, text.rstrip('\r\n') + ('\r\n' if '\r\n' in text else '\n'))
    return dict(operation=args.action, config_path=str(path), backup_path=str(backup) if backup else None,
                restart_required=args.action != 'get', config=text)


def crash_report(state: Path, executable: Path, started: datetime, pid: int, exit_code: int) -> dict:
    """Local minimal report: no arbitrary log contents, saves, paths or dumps in ZIP."""
    crash_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ') + f'-p{pid}'
    directory = state / 'reports'
    directory.mkdir(parents=True, exist_ok=True)
    bundle = directory / f'{crash_id}.zip'
    report = dict(schema_version=1, crash_id=crash_id, process_id=pid, exit_code=exit_code,
                  started_utc=started.isoformat(), system=platform.system(),
                  kernel=platform.release(), machine=platform.machine(),
                  executable_sha256=digest(executable) if executable.is_file() else None)
    # POSIX reporter files include addresses and backtrace paths. Keep only
    # known scalar fields from the matching process and session.
    signals = []
    for path in (state / 'crashes').glob(f'*-p{pid}-*.txt'):
        if path.stat().st_mtime < started.timestamp():
            continue
        fields = {}
        for line in path.read_text(errors='replace')[:65536].splitlines():
            if re.fullmatch(r'(signal=sig[a-z]+|(?:pc|fault_address)=0x[0-9A-Fa-f]+)', line):
                key, value = line.split('=', 1)
                fields[key] = value
        signals.append(fields)
    report['signals'] = signals
    with zipfile.ZipFile(bundle, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('report.json', json.dumps(report, indent=2))
    result = dict(crash_id=crash_id, bundle=str(bundle))
    write_json(directory / 'pending-report.json', result)
    return result


def provenance(root: Path) -> dict:
    if not (root / '.git').exists():
        return json.loads((root / 'config/source-provenance.json').read_text())
    def git(*arguments: str) -> str:
        return subprocess.check_output(['git', '-c', 'core.fsmonitor=false', '-C', str(root),
                                        *arguments], text=True).strip()
    return dict(commit=git('rev-parse', 'HEAD'), dirty=bool(git('status', '--porcelain')))


def build_manifest(args: argparse.Namespace) -> dict:
    source, sdk = provenance(ROOT), provenance(args.sdk_root)
    port = json.loads((ROOT / 'config/linux-port.json').read_text())
    result = dict(schema_version=3, configuration=args.configuration, cpu_baseline=args.cpu_baseline,
        created_utc=datetime.now(timezone.utc).isoformat(),
        executable=str(args.executable.relative_to(ROOT)), executable_sha256=digest(args.executable),
        generated_locally=True, pinyon_shift_commit=source['commit'],
        pinyon_shift_dirty=str(source['dirty']).lower(), rexglue_commit=sdk['commit'],
        rexglue_dirty=str(sdk['dirty']).lower(), pinyon_shift_source_payload_sha256='',
        guest_executable_sha256=digest(ROOT / '.local/game/base/default.xex'),
        guest_codegen_patch_profile='fh1-retail-base-post-processing-v1',
        guest_codegen_patch_set_sha256=digest(ROOT / 'config/rexglue/analysis/fh1-post-processing.toml'))
    result['linux_port_version'] = port['version']
    result['upstream_release'] = port['upstream_version']
    result['upstream_release_commit'] = port['upstream_revision']
    result['linux_sdk_patch_sha256'] = digest(ROOT / port['sdk_patch'])
    result['vulkan_dependencies'] = {
        name: provenance(args.sdk_root / dependency['path'])
        for name, dependency in port['vulkan'].items()
    }
    write_json(ROOT / '.local' / ('build.json' if args.configuration == 'Release' else 'build-profile.json'), result)
    write_json(args.executable.parent / 'pinyon_shift_build.json', result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    check = commands.add_parser('verify', help='verify an ISO and/or all extracted executables')
    check.add_argument('--iso-path', type=Path)
    check.add_argument('--extracted-root', type=Path)
    check.add_argument('--json', action='store_true')
    check.add_argument('--experimental', action='store_true',
                       help='also accept exact hashes from experimental-dumps.json')
    check.add_argument('--dump-id', help='require this exact edition from the enabled manifests')
    install = commands.add_parser('install-archive')
    install.add_argument('archive', type=Path)
    install.add_argument('destination', type=Path)
    install.add_argument('executable')
    config = commands.add_parser('host-config', help='get/set one literal TOML value')
    config.add_argument('action', choices=('get', 'set'))
    config.add_argument('path', type=Path)
    config.add_argument('name')
    config.add_argument('value', nargs='?', default='')
    gfx = commands.add_parser('graphics', help='edit graphics settings; old schemas migrate at game startup')
    gfx.add_argument('--action', choices=('get', 'apply', 'reset', 'restore'), default='get')
    gfx.add_argument('--state-root', type=Path, default=ROOT / '.local/preview')
    gfx.add_argument('--resolution-scale', type=int, choices=(1, 2, 3, 4))
    gfx.add_argument('--anisotropy', type=int, choices=(4, 8, 16))
    gfx.add_argument('--output-scaling', choices=('bilinear', 'cas', 'fsr'))
    gfx.add_argument('--post-effect', choices=('none', 'fxaa', 'fxaa_extreme'))
    gfx.add_argument('--presentation-fps', type=int, choices=(0, 30, 60, 120, 240))
    gfx.add_argument('--render-fps', type=int, choices=range(241), metavar='0..240')
    gfx.add_argument('--game-language', help='language ID from config/linux-game-languages.json')
    gfx.add_argument('--controls', choices=('controller', 'pc-camera', 'pc-steering'))
    gfx.add_argument('--trainer', choices=('true', 'false'))
    for name in ('disable-motion-blur', 'disable-depth-of-field', 'treasure-map'):
        gfx.add_argument('--' + name, choices=('true', 'false'))
    gfx.add_argument('--json', action='store_true')
    report = commands.add_parser('crash-report')
    report.add_argument('--state-root', type=Path, required=True)
    report.add_argument('--executable', type=Path, required=True)
    report.add_argument('--started-utc', type=lambda s: datetime.fromisoformat(s.replace('Z', '+00:00')), required=True)
    report.add_argument('--process-id', type=int, required=True)
    report.add_argument('--exit-code', type=int, required=True)
    report.add_argument('--json', action='store_true')
    manifest = commands.add_parser('build-manifest')
    manifest.add_argument('--sdk-root', type=Path, required=True)
    manifest.add_argument('--executable', type=Path, required=True)
    manifest.add_argument('--configuration', required=True)
    manifest.add_argument('--cpu-baseline', required=True)
    args = parser.parse_args()
    try:
        if args.command == 'verify':
            result = verify(args.iso_path, args.extracted_root,
                            experimental_manifest=ROOT / 'config/experimental-dumps.json'
                            if args.experimental else None, dump_id=args.dump_id)
            print(json.dumps(result, indent=2))
            return 0 if result['recognized'] and result['extracted_executables_match'] else 1
        if args.command == 'install-archive':
            install_archive(args.archive, args.destination, args.executable)
            return 0
        if args.command == 'host-config':
            text = read_text(args.path)
            if args.action == 'set':
                backup_config(args.path)
                text = toml_set(text, args.name, args.value)
                atomic_write(args.path, text.rstrip('\r\n') + ('\r\n' if '\r\n' in text else '\n'))
            print(toml_get(text, args.name))
            return 0
        if args.command == 'graphics':
            result = graphics(args)
        elif args.command == 'build-manifest':
            result = build_manifest(args)
        else:
            result = crash_report(args.state_root, args.executable, args.started_utc,
                                  args.process_id, args.exit_code)
        print(json.dumps(result, indent=2))
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        print(f'error: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
