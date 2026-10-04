#!/usr/bin/env bash
set -euo pipefail
# shellcheck source=tools/release-common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/release-common.sh"
json_events=''
for argument in "$@"; do
  case "$argument" in
    --json-events) json_events=--json-events ;;
    --help|-h) echo 'Usage: prepare-rexglue.sh [--json-events]'; exit 0 ;;
    *) die "Unknown argument: $argument" ;;
  esac
done
require_linux
sdk="$(resolve_rexglue_root)"
log="$PINYON_REPO_ROOT/.local/logs/rexglue-prepare.log"
write_pinyon_event tools 34 'Preparing pinned ShiftGlue and its dependencies.' "$json_events"
if [[ -e "$PINYON_REPO_ROOT/.git" && -f "$PINYON_REPO_ROOT/.gitmodules" ]]; then
  if [[ ! -e "$sdk/.git" ]]; then
    invoke_build_command "$log" 'SDK initialization failed' git -C "$PINYON_REPO_ROOT" submodule update --init --recursive --jobs 8 -- "$(toolchain_value '.rexglue.submodule_path')"
  else
    # Respect developer checkouts; update only their pinned child dependencies.
    invoke_build_command "$log" 'SDK dependencies failed' git -C "$sdk" submodule update --init --recursive --jobs 8
  fi
else
  revision="$(toolchain_value '.rexglue.revision')"
  if [[ ! -e "$sdk" ]]; then
    invoke_build_command "$log" 'SDK clone failed' git clone --no-checkout "$(toolchain_value '.rexglue.repository')" "$sdk"
    invoke_build_command "$log" 'SDK checkout failed' git -C "$sdk" checkout --detach "$revision"
  fi
  [[ "$(git -C "$sdk" rev-parse HEAD)" == "$revision" ]] || die "SDK revision mismatch at $sdk; preserve/move that checkout before retrying."
  # The Linux patch intentionally leaves this checkout modified. The patch
  # helper checks applicability and preserves unrelated developer edits.
  invoke_build_command "$log" 'SDK dependencies failed' git -C "$sdk" submodule update --init --recursive --jobs 8
fi
python3 "$PINYON_REPO_ROOT/tools/apply-linux-sdk.py" --sdk-root "$sdk"
write_pinyon_event tools 39 "SDK ready at $(git -C "$sdk" rev-parse HEAD)." "$json_events"
