#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
executable="$root/out/launcher/linux-x64/PinyonShiftLauncher"
if [[ ! -x "$executable" ]]; then
  echo 'Build the graphical launcher first: tools/build-launcher.sh --download' >&2
  exit 1
fi
exec "$executable" --repo-root "$root" "$@"
