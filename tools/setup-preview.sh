#!/usr/bin/env bash
set -euo pipefail
# shellcheck source=tools/release-common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/release-common.sh"
iso=''
verify_only=false
json_events=''
download=()
experimental=()
edition=()
while (( $# )); do
  case "$1" in
    --iso-path) (( $# >= 2 )) || die 'Missing ISO path.'; iso="$2"; shift 2 ;;
    --verify-only) verify_only=true; shift ;;
    --download) download=(--download); shift ;;
    --experimental) experimental=(--experimental); shift ;;
    --dump-id) (( $# >= 2 )) || die 'Missing disc edition.'; edition=(--dump-id "$2"); shift 2 ;;
    --json-events) json_events=--json-events; shift ;;
    --help|-h) echo 'Usage: setup-preview.sh --iso-path FILE [--dump-id ID] [--experimental] [--verify-only] [--download] [--json-events]'; exit 0 ;;
    *) die "Unknown argument: $1" ;;
  esac
done
require_linux
[[ -n "$iso" ]] || die 'Provide --iso-path FILE.'
iso="$(realpath -- "$iso")"
[[ -f "$iso" ]] || die 'ISO file does not exist.'
ensure_game_stopped
logs="$(resolve_local_path .local/logs)"
mkdir -p "$logs"
error_path="$logs/setup-error.json"
rm -f -- "$error_path"
stage=verify
on_error() {
  local code=$?
  trap - ERR
  jq -n --arg stage "$stage" --argjson exit_code "$code" \
    '{stage:$stage,exit_code:$exit_code,message:"Setup failed; inspect the stage log in .local/logs."}' > "$error_path"
  echo "Setup failed at $stage (exit $code). Details: $error_path" >&2
  exit "$code"
}
trap on_error ERR
write_pinyon_event verify 2 'Verifying the disc image locally.' "$json_events"
verification="$(python3 "$PINYON_REPO_ROOT/tools/linux_support.py" verify --iso-path "$iso" "${experimental[@]}" "${edition[@]}")"
dump_id="$(jq -r '.dump_id' <<< "$verification")"
write_pinyon_event verify 15 'Exact disc hash verified.' "$json_events"
if $verify_only; then exit 0; fi
events=()
[[ -z "$json_events" ]] || events=("$json_events")
stage=tools
bash "$PINYON_REPO_ROOT/tools/provision-toolchain.sh" "${download[@]}" "${events[@]}"
bash "$PINYON_REPO_ROOT/tools/prepare-rexglue.sh" "${events[@]}"
stage=extract
game="$(resolve_local_path .local/game/base)"
if ! python3 "$PINYON_REPO_ROOT/tools/linux_support.py" verify --extracted-root "$game" "${experimental[@]}" > "$logs/extraction-verify.json" ||
   ! jq -e --arg expected "$dump_id" '.dump_id == $expected' "$logs/extraction-verify.json" >/dev/null; then
  # Never remove an existing game tree. Extract to a sibling, verify it, then
  # preserve the previous tree before installing the new one.
  mkdir -p "$(dirname "$game")"
  temporary="$(mktemp -d "$(dirname "$game")/extract.XXXXXXXX")"
  extractor="$PINYON_REPO_ROOT/$(toolchain_value '.extract_xiso.install_path')/$(toolchain_value '.extract_xiso.executable')"
  if [[ ! -x "$extractor" ]]; then extractor="$(command -v extract-xiso)"; fi
  write_pinyon_event extract 46 'Extracting your disc without modifying the ISO.' "$json_events"
  invoke_build_command "$logs/extract.log" 'Extraction failed' "$extractor" -q -s -x -d "$temporary" "$iso"
  python3 "$PINYON_REPO_ROOT/tools/linux_support.py" verify --extracted-root "$temporary" "${experimental[@]}" > "$logs/extraction-verify.json"
  jq -e --arg expected "$dump_id" '.dump_id == $expected' "$logs/extraction-verify.json" >/dev/null
  if [[ -e "$game" ]]; then mv -- "$game" "$temporary.previous"; fi
  mv -- "$temporary" "$game"
fi
stage=build
bash "$PINYON_REPO_ROOT/tools/build-preview.sh" "${events[@]}" "${experimental[@]}"
jq --arg iso_path "$iso" --arg completed_utc "$(date -u +%FT%TZ)" \
  '{schema_version:1,completed_utc:$completed_utc,dump_id:.dump_id,iso_path:$iso_path,iso_sha256:.iso_sha256,result:"ready"}' \
  <<< "$verification" > "$PINYON_REPO_ROOT/.local/setup-state.json"
write_pinyon_event play 100 'Ready. Run tools/launch-preview.sh.' "$json_events"
