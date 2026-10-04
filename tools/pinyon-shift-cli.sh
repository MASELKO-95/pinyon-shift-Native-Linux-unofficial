#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if (( $# )); then
  command="$1"; shift
  case "$command" in
    --version|version)
      python3 - "$script_dir/../config/linux-port.json" <<'PY'
import json, sys
port = json.load(open(sys.argv[1]))
print(f"Pinyon Shift {port['version']} / Linux x86_64 / Vulkan {port['vulkan']['headers']['version']}")
PY
      exit 0 ;;
    setup) exec bash "$script_dir/setup-preview.sh" "$@" ;;
    launch|play) exec bash "$script_dir/launch-preview.sh" "$@" ;;
    settings) exec bash "$script_dir/set-graphics-experiment.sh" "$@" ;;
    verify) exec bash "$script_dir/verify-game.sh" "$@" ;;
    build) exec bash "$script_dir/build-preview.sh" "$@" ;;
    --help|-h) echo 'Usage: pinyon-shift-cli.sh [setup|launch|settings|verify|build|version] [options]
Run with no arguments for the interactive menu. Each command accepts --help.'; exit 0 ;;
    *) echo "Unknown command: $command" >&2; exit 2 ;;
  esac
fi
[[ -t 0 ]] || { echo 'Select a command; interactive mode requires a terminal.' >&2; exit 2; }
while true; do
  printf '\nPinyon Shift — Linux / Vulkan\n1) Set up from ISO\n2) Play\n3) Show settings\n4) Set resolution scale\n5) Exit\n'
  read -r -p '> ' choice || exit 0
  case "$choice" in
    1) read -r -p 'Path to your ISO: ' iso || exit 0
       bash "$script_dir/setup-preview.sh" --iso-path "$iso" || echo 'Setup failed; see .local/logs.' ;;
    2) bash "$script_dir/launch-preview.sh" || echo 'Launch failed; see the output above.' ;;
    3) bash "$script_dir/set-graphics-experiment.sh" || true ;;
    4) read -r -p 'Resolution scale (1–4): ' scale || exit 0
       bash "$script_dir/set-graphics-experiment.sh" --action apply --resolution-scale "$scale" || true ;;
    5) exit 0 ;;
    *) echo 'Choose 1–5.' ;;
  esac
done
