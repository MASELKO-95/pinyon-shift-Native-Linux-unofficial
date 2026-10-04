#!/usr/bin/env python3
"""Reproduce the Linux SDK patch and Vulkan pins without resetting local edits."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def git(root, *args, check=True):
    return subprocess.run(['git', '-c', 'core.fsmonitor=false', '-C', str(root),
                           *args], check=check, capture_output=True, text=True)


def prepare(sdk, manifest, check_only=False):
    if git(sdk, 'rev-parse', 'HEAD').stdout.strip() != manifest['sdk_revision']:
        raise ValueError('SDK base revision differs from the Linux port manifest')
    patch = ROOT / manifest['sdk_patch']
    if hashlib.sha256(patch.read_bytes()).hexdigest() != manifest['sdk_patch_sha256']:
        raise ValueError('Linux SDK patch checksum mismatch')
    applied = git(sdk, 'apply', '--reverse', '--check', str(patch), check=False).returncode == 0
    if not applied:
        if check_only:
            raise ValueError('Linux SDK patch missing; run tools/prepare-rexglue.sh')
        git(sdk, 'apply', '--check', str(patch))
    # Check every dependency before making changes. Never discard local files.
    pending = []
    for name, dependency in manifest['vulkan'].items():
        root = sdk / dependency['path']
        if git(root, 'rev-parse', 'HEAD').stdout.strip() == dependency['revision']:
            continue
        if check_only:
            raise ValueError(f'{name} pin differs; run tools/prepare-rexglue.sh')
        if git(root, 'status', '--porcelain').stdout.strip():
            raise ValueError(f'{name} contains local edits; refusing to change its revision')
        pending.append((root, dependency['revision']))
    for root, revision in pending:
        if git(root, 'cat-file', '-e', revision + '^{commit}', check=False).returncode:
            git(root, 'fetch', 'origin', revision)
    if not applied:
        git(sdk, 'apply', str(patch))
    for root, revision in pending:
        git(root, 'checkout', '--detach', revision)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', required=True, type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        manifest = json.loads((ROOT / 'config/linux-port.json').read_text())
        prepare(args.sdk_root.resolve(), manifest, args.check)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Linux SDK preparation failed: {error}\n'
                    + (getattr(error, 'stderr', '') or ''))
    print('Linux SDK patch and Vulkan pins verified.')


if __name__ == '__main__':
    main()
