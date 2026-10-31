#!/usr/bin/env bash
# Regression: macOS /bin/bash 3.2 treats "${empty[@]}" as unbound under set -u.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo="$(cd -- "${root}/../.." && pwd)"
home="$(mktemp -d)/home"
mkdir -p "${home}"

save_flag_args=()
resolve_args=(--home "${home}" --non-interactive)

# resolve_save_for_build.sh must succeed with no save flags under set -u.
eval "$(bash "${root}/resolve_save_for_build.sh" "${resolve_args[@]}" -- ${save_flag_args[@]+"${save_flag_args[@]}"})"
[[ -n "${NECTAR_SAVE_DIR:-}" ]]

# build-app.sh and test-build.sh use the same eval pattern at startup.
build_app="${repo}/games/pikmin1/packaging/macos/build-app.sh"
test_build="${root}/test-build.sh"
for script in "${build_app}" "${test_build}"; do
    [[ -f "${script}" ]] || { printf 'missing %s\n' "${script}" >&2; exit 1; }
    bash -c 'set -euo pipefail
      source="$1"
      home="$2"
      root="$3"
      save_flag_args=()
      resolve_sh="${root}/resolve_save_for_build.sh"
      resolve_args=(--home "${home}" --non-interactive --build-output "${home}/out")
      eval "$(bash "${resolve_sh}" "${resolve_args[@]}" -- ${save_flag_args[@]+"${save_flag_args[@]}"})"
      [[ -n "${NECTAR_SAVE_DIR:-}" ]]
    ' _ "${script}" "${home}" "${root}"
done

printf 'test_bash_empty_save_arrays: ok\n'
