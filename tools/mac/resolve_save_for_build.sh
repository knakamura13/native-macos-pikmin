#!/usr/bin/env bash
# resolve_save_for_build.sh --home DIR [--build-output DIR] [--cmake-cache FILE] [--non-interactive] [--] [save flags]
# Prints export lines for eval. Exits non-zero when save_mode.py fails.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
home=""
build_output=""
cache=""
non_interactive=0
save_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --home) home="$2"; shift 2 ;;
        --build-output) build_output="$2"; shift 2 ;;
        --cmake-cache) cache="$2"; shift 2 ;;
        --non-interactive) non_interactive=1; shift ;;
        --) shift; save_args=("$@"); break ;;
        *) save_args+=("$1"); shift ;;
    esac
done
[[ -n "${home}" ]] || { printf 'resolve_save_for_build.sh: --home is required\n' >&2; exit 2; }
cmd=(python3 "${script_dir}/save_mode.py" resolve --home "${home}")
[[ -n "${build_output}" ]] && cmd+=(--build-output "${build_output}")
[[ -n "${cache}" ]] && cmd+=(--cmake-cache "${cache}")
[[ "${non_interactive}" -eq 1 ]] && cmd+=(--non-interactive)
cmd+=(${save_args[@]+"${save_args[@]}"})
"${cmd[@]}"
