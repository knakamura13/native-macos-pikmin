#!/usr/bin/env bash
# Build upstream sources with build-information and game-window title overlays.
# Output, build, and data paths are explicit. Installation is optional and
# refuses to replace an existing app.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd -- "${script_dir}/../.." && pwd)"
ref="refs/remotes/upstream/main"
output="" build="" data="" install_dir="" check=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ref) ref="$2"; shift 2 ;;
        --output-dir) output="$2"; shift 2 ;;
        --build-dir) build="$2"; shift 2 ;;
        --data-dir) data="$2"; shift 2 ;;
        --install-dir) install_dir="$2"; shift 2 ;;
        --check) check=1; shift ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done
if [[ "${check}" -eq 0 && ( "${output}" != /* || "${build}" != /* || "${data}" != /* ) ]]; then
    printf 'Provide absolute --output-dir, --build-dir, and --data-dir paths.\n' >&2
    exit 2
fi
if [[ -n "${install_dir}" && "${install_dir}" != /* ]]; then
    printf '%s\n' '--install-dir must be an absolute path.' >&2
    exit 2
fi
if [[ -n "${install_dir}" && ( -e "${install_dir}/Pikmin 1.app" || -L "${install_dir}/Pikmin 1.app" ) ]]; then
    printf 'Refusing to replace an existing app: %s/Pikmin 1.app\n' "${install_dir}" >&2
    exit 1
fi
sha="$(git -C "${repo}" rev-parse --verify "${ref}^{commit}")"
if ! git -C "${repo}" merge-base --is-ancestor "${sha}" refs/remotes/upstream/main; then
    printf 'Ref is not an ancestor of the fetched upstream/main: %s\n' "${ref}" >&2
    exit 1
fi
if [[ "${check}" -eq 1 ]]; then
    parent="$(mktemp -d "${TMPDIR:-/tmp}/pikmin-upstream.XXXXXXXX")"
    source_dir="${parent}/source"
else
    mkdir -p "${build}"
    parent=""
    source_dir="${build}/kyl59-source"
    if [[ -e "${source_dir}" || -L "${source_dir}" ]]; then
        printf 'Refusing to replace existing source directory: %s\n' "${source_dir}" >&2
        exit 1
    fi
    # The temporary worktree is removed after the build. Never put artifacts,
    # saves, or an installation inside it (including via symlink or .. paths).
    for candidate in "${output}" "${data}" ${install_dir:+"${install_dir}"}; do
        if python3 -c 'import os,sys; s=os.path.realpath(sys.argv[1]); p=os.path.realpath(sys.argv[2]); sys.exit(0 if os.path.commonpath([s,p]) == s else 1)' "${source_dir}" "${candidate}"; then
            printf 'Path must be outside the temporary source worktree: %s\n' "${candidate}" >&2
            exit 2
        fi
    done
    if [[ -n "${install_dir}" ]] && python3 -c 'import os,sys; sys.exit(0 if os.path.realpath(sys.argv[1]) == os.path.realpath(sys.argv[2]) else 1)' "${output}" "${install_dir}"; then
        printf '%s\n' 'Output and installation directories must differ.' >&2
        exit 2
    fi
fi
created=0
cleanup() {
    if [[ "${created}" -eq 1 ]]; then
        git -C "${repo}" worktree remove --force "${source_dir}" >/dev/null 2>&1 || true
    fi
    if [[ -n "${parent}" ]]; then rmdir "${parent}" 2>/dev/null || true; fi
}
trap cleanup EXIT
git -C "${repo}" worktree add --quiet --detach "${source_dir}" "${sha}"
created=1
overlay="${script_dir}/upstream-launcher.patch"
window_overlay="${script_dir}/upstream-window-title.patch"
git -C "${source_dir}" apply --check "${overlay}"
git -C "${source_dir}" apply --check "${window_overlay}"
if [[ "${check}" -eq 1 ]]; then
    printf 'Upstream %s: identity overlays apply cleanly.\n' "${sha}"
    exit 0
fi
git -C "${source_dir}" apply "${overlay}"
git -C "${source_dir}" apply "${window_overlay}"
changed="$(git -C "${source_dir}" diff --name-only)"
expected=$'games/pikmin1/pc_port/launcher/launcher_gui.cpp\ngames/pikmin1/pc_port/launcher/launcher_main.cpp\ngames/pikmin1/pc_port/pc_window.cpp'
if [[ "${changed}" != "${expected}" ]]; then
    printf 'Identity overlays changed unexpected source files.\n' >&2
    exit 1
fi
mkdir -p "${source_dir}/games/pikmin1/packaging/macos" "${source_dir}/tools/mac"
cp "${repo}/games/pikmin1/packaging/macos/build-app.sh" "${source_dir}/games/pikmin1/packaging/macos/build-app.sh"
cp "${repo}/games/pikmin1/packaging/macos/controls.conf" "${source_dir}/games/pikmin1/packaging/macos/controls.conf"
cp "${script_dir}/build_identity.py" "${source_dir}/tools/mac/build_identity.py"
cp "${repo}/games/pikmin1/pc_port/pc_build_window_title.h" "${source_dir}/games/pikmin1/pc_port/pc_build_window_title.h"
install_args=()
if [[ -n "${install_dir}" ]]; then install_args+=(--install "${install_dir}"); fi
PIKMIN_BUILD_ROLE=upstream PIKMIN_DATA_ROOT="${data}" PIKMIN_LAUNCHER_OVERLAY="${overlay}" PIKMIN_WINDOW_OVERLAY="${window_overlay}" PIKMIN_INSTALL_NEW_ONLY=1 \
    "${source_dir}/games/pikmin1/packaging/macos/build-app.sh" --build-dir "${build}" --output-dir "${output}" \
    ${install_args[@]+"${install_args[@]}"}
printf 'Upstream game source: %s\nLauncher overlay SHA-256: %s\n' "${sha}" "$(shasum -a 256 "${overlay}" | cut -d' ' -f1)"
