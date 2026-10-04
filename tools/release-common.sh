#!/usr/bin/env bash
# release-common.sh — shared library sourced by all Pinyon Shift Linux build
# scripts.  This is the bash equivalent of tools/release-common.ps1.
#
# Usage: source tools/release-common.sh

set -euo pipefail

# ---------------------------------------------------------------------------
#  Paths
# ---------------------------------------------------------------------------

get_repo_root() {
  local script_dir
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "$script_dir/.." && pwd
}

PINYON_REPO_ROOT="$(get_repo_root)"
readonly PINYON_REPO_ROOT

die() { echo "error: $*" >&2; exit 1; }

require_linux() {
  [[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || die 'This pipeline requires Linux x86_64.'
  command -v python3 >/dev/null || die 'Install Python 3.11 or newer.'
  python3 -c 'import sys; sys.exit(sys.version_info < (3, 11))' || die 'Python 3.11 or newer is required.'
  command -v jq >/dev/null || die 'Install jq.'
}

enter_build_environment() {
  local tool bin_dir
  for tool in llvm cmake; do
    bin_dir="$PINYON_REPO_ROOT/$(toolchain_value ".$tool.install_path")/bin"
    if [[ -d "$bin_dir" ]]; then export PATH="$bin_dir:$PATH"; fi
  done
}

ensure_game_stopped() {
  command -v pgrep >/dev/null || die 'Install procps (pgrep).'
  if pgrep -x pinyon_shift >/dev/null; then die 'Close Pinyon Shift before building or editing settings.'; fi
}

get_release_toolchain_path() {
  local linux_config="$PINYON_REPO_ROOT/config/release-toolchain-linux.json"
  local generic_config="$PINYON_REPO_ROOT/config/release-toolchain.json"
  if [[ -f "$linux_config" ]]; then
    echo "$linux_config"
  else
    echo "$generic_config"
  fi
}

# Read a top-level key from the release toolchain JSON.
# Usage: toolchain_value '.cmake.executable'
toolchain_value() {
  jq -r "$1" "$(get_release_toolchain_path)"
}

resolve_local_path() {
  local relative="$1"
  local full
  full="$(cd "$PINYON_REPO_ROOT" && realpath -m "$relative")"
  local local_prefix
  local_prefix="$(cd "$PINYON_REPO_ROOT" && realpath -m ".local")"
  if [[ "$full" != "$local_prefix"/* ]]; then
    echo "error: refusing to use a release-work path outside $local_prefix" >&2
    return 1
  fi
  echo "$full"
}

# ---------------------------------------------------------------------------
#  Event reporting (launcher JSON protocol)
# ---------------------------------------------------------------------------

# Usage: write_pinyon_event <stage> <percent> <message> [--json-events]
write_pinyon_event() {
  local stage="$1"
  local percent="$2"
  local message="$3"
  local json_events="${4:-}"

  if [[ "$json_events" == "--json-events" ]]; then
    printf '::pinyon::%s\n' "$(jq -cn --arg stage "$stage" --argjson percent "$percent" \
      --arg message "$message" '{stage:$stage,percent:$percent,message:$message}')"
  else
    local upper_stage
    upper_stage="$(echo "$stage" | tr '[:lower:]' '[:upper:]')"
    echo "[$upper_stage] $message"
  fi
}

# ---------------------------------------------------------------------------
#  Python
# ---------------------------------------------------------------------------

get_python() {
  # Prefer the toolchain-local Python, fall back to system python3.
  if command -v python3 &>/dev/null; then
    echo "python3"
    return
  fi
  echo "error: python3 is not installed" >&2
  return 1
}

# ---------------------------------------------------------------------------
#  Git
# ---------------------------------------------------------------------------

get_git() {
  if command -v git &>/dev/null; then
    command -v git
    return
  fi
  echo "error: git is not installed" >&2
  return 1
}

# ---------------------------------------------------------------------------
#  ReXGlue SDK root
# ---------------------------------------------------------------------------

resolve_rexglue_root() {
  local submodule_path
  submodule_path="$(toolchain_value '.rexglue.submodule_path')"
  if [[ -e "$PINYON_REPO_ROOT/.git" && -f "$PINYON_REPO_ROOT/.gitmodules" ]]; then
    realpath "$PINYON_REPO_ROOT/$submodule_path"
    return
  fi
  local fallback
  fallback="$(toolchain_value '.rexglue.fallback_path')"
  resolve_local_path "$fallback"
}

# ---------------------------------------------------------------------------
#  Downloads with SHA-256 verification
# ---------------------------------------------------------------------------

invoke_download() {
  local url="$1"
  local destination="$2"
  local expected_sha256="$3"

  expected_sha256="${expected_sha256^^}"
  [[ "$expected_sha256" =~ ^[A-F0-9]{64}$ ]] || { echo "error: missing SHA-256" >&2; return 1; }
  mkdir -p "$(dirname "$destination")"

  if [[ -f "$destination" ]]; then
    local actual
    actual="$(sha256sum "$destination" | awk '{print toupper($1)}')"
    if [[ "$actual" == "$expected_sha256" ]]; then
      return 0
    fi
    rm -f "$destination"
  fi

  local partial="${destination}.partial"
  rm -f "$partial"

  curl --fail --location --retry 3 --output "$partial" "$url"

  local actual
  actual="$(sha256sum "$partial" | awk '{print toupper($1)}')"
  if [[ "$actual" != "$expected_sha256" ]]; then
    rm -f "$partial"
    echo "error: downloaded file failed SHA-256 verification." >&2
    echo "  expected: $expected_sha256" >&2
    echo "  got:      $actual" >&2
    return 1
  fi

  mv "$partial" "$destination"
}

# ---------------------------------------------------------------------------
#  Logging & command execution
# ---------------------------------------------------------------------------

format_command_line() {
  local parts=()
  for arg in "$@"; do
    if [[ "$arg" =~ [[:space:]] || -z "$arg" ]]; then
      parts+=("\"$arg\"")
    else
      parts+=("$arg")
    fi
  done
  echo "${parts[*]}"
}

# Run a command, stream output, and log to a file.
# Usage: invoke_logged_command <log_path> [--append] <command> [args...]
invoke_logged_command() {
  local log_path="$1"; shift
  local append=false
  if [[ "${1:-}" == "--append" ]]; then
    append=true; shift
  fi

  mkdir -p "$(dirname "$log_path")"

  local cmd_line
  cmd_line="$(format_command_line "$@")"

  if $append; then
    echo "> $cmd_line" >> "$log_path"
  else
    echo "> $cmd_line" > "$log_path"
  fi

  local exit_code=0
  "$@" 2>&1 | tee -a "$log_path" || exit_code=$?

  echo "> exit code $exit_code" >> "$log_path"
  return $exit_code
}

# Run a build command; abort with a message on failure.
# Usage: invoke_build_command <log_path> <failure_message> <command> [args...]
invoke_build_command() {
  local log_path="$1"; shift
  local failure_message="$1"; shift

  local exit_code=0
  invoke_logged_command "$log_path" "$@" || exit_code=$?
  if (( exit_code != 0 )); then
    echo "error: $failure_message (exit code $exit_code)" >&2
    echo "  full log: $log_path" >&2
    local hint
    hint="$(get_failure_hint "$log_path")"
    if [[ -n "$hint" ]]; then
      echo "  hint: $hint" >&2
    fi
    return $exit_code
  fi
}

# ---------------------------------------------------------------------------
#  System info
# ---------------------------------------------------------------------------

get_total_memory_bytes() {
  awk '/^MemTotal:/ {printf "%.0f\n", $2 * 1024}' /proc/meminfo 2>/dev/null || echo 0
}

# Optimal parallel job count: same algorithm as the PS1 version.
# max(1, min(min(16, nproc-1), floor((mem_gb - 2) / 1.5)))
get_build_job_count() {
  local logical_processors
  logical_processors="$(nproc 2>/dev/null || echo 4)"
  local memory_bytes
  memory_bytes="$(get_total_memory_bytes)"

  local jobs=$(( logical_processors - 1 ))
  (( jobs < 1 )) && jobs=1
  (( jobs > 16 )) && jobs=16

  if (( memory_bytes > 0 )); then
    local mem_gb=$(( memory_bytes / 1073741824 ))
    local by_memory=$(( (mem_gb - 2) * 10 / 15 ))  # floor((mem_gb-2)/1.5)
    (( by_memory < 1 )) && by_memory=1
    (( jobs > by_memory )) && jobs=$by_memory
  fi

  echo "$jobs"
}

get_system_summary() {
  local root="${1:-$PINYON_REPO_ROOT}"
  local kernel
  kernel="$(uname -r)"
  local logical_processors
  logical_processors="$(nproc 2>/dev/null || echo 0)"
  local memory_bytes
  memory_bytes="$(get_total_memory_bytes)"
  local mem_gb=""
  if (( memory_bytes > 0 )); then
    mem_gb="$(awk "BEGIN {printf \"%.1f\", $memory_bytes / 1073741824}")"
  fi
  local free_disk_gb=""
  local drive_path
  drive_path="$(realpath "$root" 2>/dev/null || echo "$root")"
  free_disk_gb="$(df -BG "$drive_path" 2>/dev/null | awk 'NR==2 {gsub("G",""); print $4}')"

  jq -n \
    --arg kernel "$kernel" \
    --argjson cpus "$logical_processors" \
    --arg mem_gb "${mem_gb:-null}" \
    --arg free_disk_gb "${free_disk_gb:-null}" \
    '{
      kernel: $kernel,
      logical_processors: $cpus,
      memory_gb: (if $mem_gb != "null" then ($mem_gb | tonumber) else null end),
      free_disk_gb: (if $free_disk_gb != "null" then ($free_disk_gb | tonumber) else null end)
    }'
}

# ---------------------------------------------------------------------------
#  CMake
# ---------------------------------------------------------------------------

get_cmake() {
  local candidate=""

  # Try local toolchain first.
  local install_path
  install_path="$(toolchain_value '.cmake.install_path // empty')"
  if [[ -n "$install_path" ]]; then
    local exe
    exe="$(toolchain_value '.cmake.executable // "bin/cmake"')"
    local local_cmake="$PINYON_REPO_ROOT/$install_path/$exe"
    if [[ -x "$local_cmake" ]]; then
      candidate="$local_cmake"
    fi
  fi

  # Fall back to system cmake.
  if [[ -z "$candidate" ]] && command -v cmake &>/dev/null; then
    candidate="$(command -v cmake)"
  fi

  if [[ -z "$candidate" ]]; then
    echo "error: cmake is not installed. Install cmake >= 3.25." >&2
    return 1
  fi

  # Verify version.
  local version_line
  version_line="$("$candidate" --version | head -1)"
  local version
  version="$(echo "$version_line" | grep -oP '\d+\.\d+\.\d+' | head -1)"
  if [[ -z "$version" ]]; then
    echo "error: cannot determine cmake version" >&2
    return 1
  fi

  # Compare versions: require >= 3.25.0
  local major minor
  major="$(echo "$version" | cut -d. -f1)"
  minor="$(echo "$version" | cut -d. -f2)"
  if (( major < 3 || (major == 3 && minor < 25) )); then
    echo "error: cmake $version is too old; 3.25 or newer is required." >&2
    return 1
  fi

  echo "$candidate"
}

# Detect and reset a CMake build tree that has been moved.
reset_relocated_cmake_cache() {
  local build_dir="$1"
  local cache="$build_dir/CMakeCache.txt"
  [[ -f "$cache" ]] || return 1

  local recorded
  recorded="$(grep '^CMAKE_CACHEFILE_DIR:INTERNAL=' "$cache" 2>/dev/null | head -1 | cut -d= -f2-)"
  [[ -n "$recorded" ]] || return 1

  recorded="$(realpath -m "$recorded")"
  local current
  current="$(realpath -m "$build_dir")"

  if [[ "$recorded" == "$current" ]]; then
    return 1  # not relocated
  fi

  rm -f "$cache"
  rm -rf "$build_dir/CMakeFiles"
  return 0
}

# ---------------------------------------------------------------------------
#  Source provenance
# ---------------------------------------------------------------------------

get_source_provenance() {
  local root="$1"
  local git_cmd
  git_cmd="$(get_git)"

  local commit=""
  local dirty="false"

  if [[ -e "$root/.git" ]]; then
    commit="$("$git_cmd" -C "$root" rev-parse HEAD 2>/dev/null || true)"
    if [[ "$commit" =~ ^[0-9a-fA-F]{40}$ ]]; then
      commit="$(echo "$commit" | tr '[:upper:]' '[:lower:]')"
      if [[ -n "$("$git_cmd" -C "$root" status --porcelain 2>/dev/null)" ]]; then
        dirty="true"
      fi
    else
      commit=""
    fi
  fi

  if [[ -z "$commit" ]]; then
    local provenance_file="$root/config/source-provenance.json"
    if [[ -f "$provenance_file" ]]; then
      commit="$(jq -r '.commit // empty' "$provenance_file")"
      dirty="$(jq -r '.dirty // false' "$provenance_file")"
      commit="$(echo "$commit" | tr '[:upper:]' '[:lower:]')"
    fi
  fi

  if [[ -z "$commit" ]]; then
    echo "error: build provenance requires an exact Pinyon Shift commit." >&2
    return 1
  fi

  jq -n --arg commit "$commit" --arg dirty "$dirty" \
    '{"commit": $commit, "dirty": ($dirty == "true")}'
}

# ---------------------------------------------------------------------------
#  Failure analysis
# ---------------------------------------------------------------------------

get_failure_hint() {
  local log_path="$1"
  [[ -f "$log_path" ]] || return 0

  local text
  text="$(cat "$log_path")"

  if echo "$text" | grep -qiE 'No space left on device|ENOSPC|disk.*(is )?full'; then
    echo "The drive ran out of free space. Free at least 30 GB, then run setup again."
    return
  fi
  if echo "$text" | grep -qiE 'out of memory|bad_alloc|not enough memory|Allocation failed'; then
    echo "The compiler ran out of memory. Close other programs, then run setup again."
    return
  fi
  if echo "$text" | grep -qiE 'Permission denied|Access is denied'; then
    echo "A file could not be written. Check directory permissions, then run setup again."
    return
  fi
  if echo "$text" | grep -qiE 'PLEASE submit a bug report|clang.*crashed|Stack dump:'; then
    echo "The compiler crashed. This is most often caused by running out of memory. Close other programs and run setup again."
    return
  fi
  if echo "$text" | grep -qiE 'No CMAKE_(C|CXX)_COMPILER could be found|compiler identification is unknown'; then
    echo "The C++ compiler is missing. Install clang and clang++ (>= 17), then run setup again."
    return
  fi
}
