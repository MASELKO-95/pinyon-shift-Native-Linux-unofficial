#!/usr/bin/env bash
set -euo pipefail
# shellcheck source=tools/release-common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/release-common.sh"

download=false
json_events=''
for argument in "$@"; do
  case "$argument" in
    --download) download=true ;;
    --json-events) json_events=--json-events ;;
    --help|-h) echo 'Usage: provision-toolchain.sh [--download] [--json-events]
Checks system tools; --download installs missing CMake, LLVM and extract-xiso under .local.
System packages (Python, jq, Git, Ninja, pkg-config, C/C++ headers and window/audio development
libraries) must be installed through your distribution. No sudo commands are run.'; exit 0 ;;
    *) die "Unknown argument: $argument" ;;
  esac
done
require_linux
enter_build_environment
for tool in git ninja pkg-config python3 jq pgrep; do
  command -v "$tool" >/dev/null || die "Install the system dependency: $tool (see docs/LINUX_PORT.md)."
done

version_ok() {
  local executable="$1" minimum="$2" version
  version="$("$executable" --version | sed -n '1s/[^0-9]*\([0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\).*/\1/p')"
  python3 -c 'import sys; sys.exit(tuple(map(int,sys.argv[1].split("."))) < tuple(map(int,sys.argv[2].split("."))))' "$version" "$minimum"
}

for tool in cmake llvm extract_xiso; do
  case "$tool" in cmake) command_name=cmake ;; llvm) command_name=clang ;; *) command_name=extract-xiso ;; esac
  local_root="$(resolve_local_path "$(toolchain_value ".$tool.install_path")")"
  executable="$(toolchain_value ".$tool.executable")"
  candidate="$local_root/$executable"
  if [[ ! -x "$candidate" ]]; then candidate="$(command -v "$command_name" || true)"; fi
  minimum="$(toolchain_value ".$tool.minimum_version // empty")"
  if [[ -n "$candidate" && ( -z "$minimum" ) ]]; then continue; fi
  if [[ -n "$candidate" ]] && version_ok "$candidate" "$minimum"; then continue; fi
  $download || die "Missing/outdated $command_name. Install it or rerun with --download."
  command -v curl >/dev/null || die 'Install curl for downloads.'
  url="$(toolchain_value ".$tool.url")"
  archive="$(resolve_local_path ".local/downloads/${url##*/}")"
  write_pinyon_event tools 22 "Downloading verified $tool." "$json_events"
  invoke_download "$url" "$archive" "$(toolchain_value ".$tool.sha256")"
  python3 "$PINYON_REPO_ROOT/tools/linux_support.py" install-archive "$archive" "$local_root" "$executable"
done
enter_build_environment
command -v clang++ >/dev/null || die 'Install clang++.'
version_ok "$(command -v clang++)" '18.0.0' || die 'Clang++ 18+ is required.'
write_pinyon_event tools 30 'Toolchain ready. CMake will check native development libraries.' "$json_events"
