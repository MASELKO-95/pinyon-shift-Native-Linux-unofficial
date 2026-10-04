#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
download=false
test=false
for argument in "$@"; do
  case "$argument" in
    --download) download=true ;;
    --test) test=true ;;
    --help|-h) echo 'Usage: build-launcher.sh [--download] [--test]
Builds a self-contained Linux x86_64 graphical launcher. Requires a .NET 10 SDK.
--download installs the pinned SDK into .local when no SDK is available.
--test also runs the launcher backend integration tests.'; exit 0 ;;
    *) echo "Unknown argument: $argument" >&2; exit 2 ;;
  esac
done
[[ "$(uname -sm)" == 'Linux x86_64' ]] || { echo 'Linux x86_64 is required.' >&2; exit 1; }
manifest="$root/config/launcher-toolchain-linux.json"
version="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["dotnet_sdk"]["version"])' "$manifest")"
sdk="$root/.local/toolchain/dotnet-$version"
dotnet="$sdk/dotnet"
if [[ ! -x "$dotnet" ]]; then
  if command -v dotnet >/dev/null && dotnet --list-sdks | grep -q '^10\.'; then
    dotnet="$(command -v dotnet)"
  elif $download; then
    mkdir -p "$root/.local/downloads" "$sdk"
    url="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["dotnet_sdk"]["url"])' "$manifest")"
    archive="$root/.local/downloads/dotnet-sdk-$version-linux-x64.tar.gz"
    curl -fL --retry 2 "$url" -o "$archive.part"
    python3 - "$manifest" "$archive.part" "$sdk" <<'PY'
import hashlib, json, sys, tarfile
from pathlib import Path
manifest, archive, destination = map(Path, sys.argv[1:])
expected = json.loads(manifest.read_text())['dotnet_sdk']['sha512']
with archive.open('rb') as stream:
    if hashlib.file_digest(stream, 'sha512').hexdigest() != expected:
        raise SystemExit('Downloaded .NET SDK checksum mismatch')
with tarfile.open(archive) as stream:
    stream.extractall(destination, filter='data')
archive.rename(archive.with_suffix(''))
PY
  else
    echo 'Install a .NET 10 SDK or run tools/build-launcher.sh --download.' >&2
    exit 1
  fi
fi
export DOTNET_CLI_HOME="$root/.local/dotnet-home"
export NUGET_PACKAGES="$root/.local/nuget/packages"
export DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_NOLOGO=1
export AVALONIA_TELEMETRY_OPTOUT=1
project="$root/launcher/PinyonShift.Launcher.Linux/PinyonShift.Launcher.Linux.csproj"
output="$root/out/launcher/linux-x64"
"$dotnet" restore "$project" --locked-mode
"$dotnet" publish "$project" -c Release --no-restore -o "$output"
if $test; then
  "$dotnet" run --project "$root/launcher/PinyonShift.Launcher.Linux.Tests" -c Release -- "$root"
fi
python3 - "$root" "$output" <<'PY'
from pathlib import Path
import json, os, shutil, sys
root, output = map(Path, sys.argv[1:])
shutil.copy2(root / 'LICENSE', output / 'LICENSE')
shutil.copy2(root / 'AUTHORS.md', output / 'AUTHORS.md')
shutil.copy2(root / 'THIRD_PARTY_NOTICES.md', output / 'THIRD_PARTY_NOTICES.md')
dependencies = json.loads((output / 'PinyonShiftLauncher.deps.json').read_text())['libraries']
packages = Path(os.environ['NUGET_PACKAGES'])
for name in dependencies:
    package, version = name.removeprefix('runtimepack.').lower().split('/')
    source = packages / package / version
    for path in source.glob('*'):
        if path.is_file() and any(token in path.name.upper() for token in ('LICENSE', 'NOTICE', 'COPYING')):
            target = output / 'Notices' / f'{package}-{version}' / path.name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
# Desktop Entry Exec has its own escaping rules, not shell quoting.
def quote(value):
    return '"' + str(value).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%') + '"'
entry = output / 'Pinyon Shift.desktop'
entry.write_text('[Desktop Entry]\nType=Application\nName=Pinyon Shift\n'
    'Comment=Unofficial native Linux port by MASELKO-95\n'
    f'Exec={quote(root / "tools/launch-launcher.sh")}\n'
    f'Icon={root / "launcher/PinyonShift.Launcher/Branding/mark-light.png"}\n'
    'Terminal=false\nCategories=Game;\nStartupWMClass=PinyonShiftLauncher\n')
entry.chmod(0o755)
PY
echo "Launcher ready: $output/PinyonShiftLauncher"
echo 'Start it with tools/launch-launcher.sh or the generated desktop shortcut.'
