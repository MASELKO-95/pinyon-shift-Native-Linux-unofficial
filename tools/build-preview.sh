#!/usr/bin/env bash
set -euo pipefail
# shellcheck source=tools/release-common.sh
source "$(dirname -- "${BASH_SOURCE[0]}")/release-common.sh"
configuration=Release
parallel=0
baseline=sse4.1
json_events=''
experimental=()
while (( $# )); do
  case "$1" in
    --configuration|--parallel|--cpu-baseline)
      (( $# >= 2 )) || die "Missing value for $1"
      case "$1" in --configuration) configuration="$2" ;; --parallel) parallel="$2" ;; *) baseline="$2" ;; esac
      shift 2 ;;
    --json-events) json_events=--json-events; shift ;;
    --experimental) experimental=(--experimental); shift ;;
    --help|-h) echo 'Usage: build-preview.sh [--experimental] [--configuration Release|RelWithDebInfo] [--parallel N] [--cpu-baseline sse4.1|fma|auto] [--json-events]'; exit 0 ;;
    *) die "Unknown argument: $1" ;;
  esac
done
require_linux
ensure_game_stopped
enter_build_environment
[[ "$configuration" == Release || "$configuration" == RelWithDebInfo ]] || die 'Invalid configuration.'
[[ "$parallel" =~ ^[0-9]+$ && ${#parallel} -le 2 ]] || die 'Parallel must be 0..32.'
parallel=$((10#$parallel))
(( parallel <= 32 )) || die 'Parallel must be 0..32.'
if (( parallel == 0 )); then parallel="$(get_build_job_count)"; fi
if [[ "$baseline" == auto ]]; then
  baseline=sse4.1
  if grep -Eq '^flags[[:space:]]*:.*[[:space:]]fma[[:space:]]' /proc/cpuinfo; then baseline=fma; fi
fi
flags=-msse4.1
case "$baseline" in sse4.1) ;; fma) flags+=' -mfma -ffp-contract=off' ;; *) die 'Invalid CPU baseline.' ;; esac
cmake="$(get_cmake)"
sdk="$(resolve_rexglue_root)"
[[ -f "$sdk/CMakeLists.txt" ]] || die 'Run tools/prepare-rexglue.sh first.'
python3 "$PINYON_REPO_ROOT/tools/apply-linux-sdk.py" --sdk-root "$sdk" --check
python3 "$PINYON_REPO_ROOT/tools/linux_support.py" verify --extracted-root "$PINYON_REPO_ROOT/.local/game/base" "${experimental[@]}" >/dev/null
logs="$PINYON_REPO_ROOT/.local/logs"
export SOURCE_DATE_EPOCH=1784764800
write_pinyon_event build 62 "Building code generator with $parallel jobs." "$json_events"
cd "$sdk"
invoke_build_command "$logs/rexglue-configure.log" 'SDK configuration failed' "$cmake" --preset linux-amd64 -DREXGLUE_ENABLE_TRACY=OFF -DREXGLUE_USE_VULKAN=ON
invoke_build_command "$logs/rexglue-build.log" 'Code generator build failed' "$cmake" --build --preset linux-amd64-release --target rexglue --parallel "$parallel"
generator="$sdk/out/linux-amd64/Release/rexglue"
[[ -x "$generator" ]] || die "Generator missing: $generator"
cd "$PINYON_REPO_ROOT"
write_pinyon_event build 72 'Translating verified game executables.' "$json_events"
generated="$PINYON_REPO_ROOT/.local/generated"
bootstrap=false
for tree in default speech xmedia; do
  [[ -f "$generated/$tree/sources.cmake" ]] || bootstrap=true
done
[[ -f "$generated/default/codegen.build.stamp" ]] || bootstrap=true
invalidate_stamps() {
  local tree
  for tree in default speech xmedia; do rm -f -- "$generated/$tree/codegen.build.stamp"; done
}
if $bootstrap; then
  if ! invoke_build_command "$logs/codegen-command.log" 'Game translation failed' "$generator" --log-level info --log-file "$logs/codegen.log" codegen "$PINYON_REPO_ROOT/config/rexglue/pinyon_shift_manifest.toml" --ignore-stamp; then
    invalidate_stamps; exit 1
  fi
  if ! python3 tools/verify-codegen-log.py "$logs/codegen.log"; then invalidate_stamps; exit 1; fi
fi
preset="linux-amd64-${configuration,,}"
write_pinyon_event build 82 'Compiling the native Vulkan preview.' "$json_events"
invoke_build_command "$logs/preview-configure.log" 'Preview configuration failed' "$cmake" --preset "$preset" "-DREXSDK_DIR=$sdk" "-DPINYON_SHIFT_CPU_BASELINE=$baseline" "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" "-DPYTHON_EXECUTABLE=$(command -v python3)"
invoke_build_command "$logs/preview-build.log" 'Preview compilation failed' "$cmake" --build --preset "$preset" --parallel "$parallel"
if ! python3 tools/verify-codegen-log.py "$logs/codegen.log"; then invalidate_stamps; exit 1; fi
executable="$PINYON_REPO_ROOT/out/build/$preset/pinyon_shift"
[[ -x "$executable" ]] || die 'Build produced no game executable.'
python3 tools/linux_support.py build-manifest --sdk-root "$sdk" --executable "$executable" --configuration "$configuration" --cpu-baseline "$baseline"
write_pinyon_event build 96 'Native compilation completed.' "$json_events"
