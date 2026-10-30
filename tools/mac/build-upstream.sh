#!/usr/bin/env bash
# build-upstream.sh [--install] [--if-changed] [--no-fetch] [--ref REF] [--check]
#                   [--data-dir DIR] [--output-dir DIR] [--build-dir DIR] [--install-dir DIR]
#
# The one command for an upstream build. It fetches upstream main, checks it out
# in a fixed worktree, applies this fork's packaging and identity overlays, and
# builds "Open Nectar.app". The game code is upstream's: only the files listed in
# allowed_changes below are patched or added, and the build fails if the
# overlays touch anything else. No commits are cherry-picked.
#
#   tools/mac/build-upstream.sh --install        # fetch, build, install /Applications/Open Nectar.app
#   tools/mac/build-upstream.sh                  # fetch and build only
#   tools/mac/build-upstream.sh --check          # do the overlays still apply to upstream main?
#
# The hourly LaunchAgent (tools/mac/auto-install-upstream.sh) runs
# `build-upstream.sh --install --if-changed`, the same command.
#
# Defaults (all under ~/OpenNectar-builds.noindex, which Spotlight skips):
#   source worktree  upstream-source        (fixed, reused so the CMake cache stays valid)
#   build directory  upstream-build
#   output           upstream-out/<sha12>-XXXXXX (the newest 3 are kept)
#   data folder      ~/.local/share/pikmin-native-upstream (never the real one)
#   install folder   /Applications (with --install)
#
# --if-changed  skip the build when the installed app already has this upstream
#               commit and these overlays.
# --no-fetch    build the upstream/main already fetched.
# --ref REF     build REF instead; it must be an ancestor of upstream/main.
# --install-dir DIR implies --install.
#
# Exit status: 0 built, installed or up to date; 75 deferred (the game is
# running or another upstream build holds the lock); anything else failed.
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd -- "${script_dir}/../.." && pwd)"
upstream_url="https://github.com/SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port.git"
app_name="Open Nectar.app"
builds_root="${PIKMIN_BUILDS_ROOT:-${HOME}/OpenNectar-builds.noindex}"
ref="refs/remotes/upstream/main"
output="" build="" data="" install_dir="" check=0 install=0 if_changed=0 fetch=1
while [[ $# -gt 0 ]]; do
    case "$1" in
        --ref) ref="$2"; shift 2 ;;
        --output-dir) output="$2"; shift 2 ;;
        --build-dir) build="$2"; shift 2 ;;
        --data-dir) data="$2"; shift 2 ;;
        --install-dir) install_dir="$2"; install=1; shift 2 ;;
        --install) install=1; shift ;;
        --if-changed) if_changed=1; shift ;;
        --no-fetch) fetch=0; shift ;;
        --check) check=1; shift ;;
        -h|--help) sed -n '2,32p' "$0"; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done
default_output=0
[[ -n "${build}" ]] || build="${builds_root}/upstream-build"
[[ -n "${data}" ]] || data="${HOME}/.local/share/pikmin-native-upstream"
[[ -n "${output}" ]] || default_output=1
if [[ "${install}" -eq 1 && -z "${install_dir}" ]]; then install_dir="/Applications"; fi
for path in "${build}" "${data}" ${output:+"${output}"} ${install_dir:+"${install_dir}"}; do
    if [[ "${path}" != /* ]]; then
        printf 'Paths must be absolute: %s\n' "${path}" >&2
        exit 2
    fi
done
if [[ "${if_changed}" -eq 1 && "${install}" -eq 0 ]]; then
    printf '%s\n' '--if-changed needs --install.' >&2
    exit 2
fi

# The upstream app never touches the real saves: refuse the real data folder,
# anything inside it and anything containing it, after resolving symlinks.
overlaps() {
    python3 - "$1" "$2" <<'PY'
import os, sys
a, b = (os.path.realpath(os.path.expanduser(p)) for p in sys.argv[1:3])
inside = lambda x, y: x == y or x.startswith(y.rstrip(os.sep) + os.sep)
sys.exit(0 if inside(a, b) or inside(b, a) else 1)
PY
}
for real in "${HOME}/.local/share/pikmin-native" ${XDG_DATA_HOME:+"${XDG_DATA_HOME}/pikmin-native"}; do
    if overlaps "${data}" "${real}"; then
        printf 'Refusing --data-dir %s: it overlaps the real data folder %s.\n' "${data}" "${real}" >&2
        exit 2
    fi
done

game_running() {
    pgrep -x nectar >/dev/null 2>&1 || pgrep -x nectar-launcher >/dev/null 2>&1 || pgrep -x pikmin2 >/dev/null 2>&1
}

# Directory mkdir is the lock. A dead pid is stale and is taken over.
lock_dir=""
acquire_lock() {
    local lock="$1" pid
    mkdir -p "$(dirname -- "${lock}")"
    if mkdir "${lock}" 2>/dev/null; then printf '%s\n' "$$" > "${lock}/pid"; return 0; fi
    pid="$(cat "${lock}/pid" 2>/dev/null || true)"
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then return 1; fi
    rm -rf "${lock}"
    if mkdir "${lock}" 2>/dev/null; then printf '%s\n' "$$" > "${lock}/pid"; return 0; fi
    return 1
}

# Files the overlays patch and the packaging files copied in. Anything else
# changed in the worktree marks the build dirty.
patched_files=(
    games/pikmin1/pc_port/launcher/launcher_gui.cpp
    games/pikmin1/pc_port/launcher/launcher_main.cpp
    games/pikmin1/pc_port/pc_window.cpp
)
copied_files=(
    games/pikmin1/packaging/macos/build-app.sh
    games/pikmin1/packaging/macos/controls.conf
    tools/mac/build_identity.py
    games/pikmin1/pc_port/pc_build_window_title.h
)
overlay="${script_dir}/upstream-launcher.patch"
window_overlay="${script_dir}/upstream-window-title.patch"

# Fingerprint of everything this fork adds, so --if-changed rebuilds when the
# packaging changes even if upstream did not.
packaging_fingerprint() {
    local file
    for file in "${overlay}" "${window_overlay}" "${copied_files[@]/#/${repo}/}"; do
        shasum -a 256 "${file}" 2>/dev/null || sha256sum "${file}"
    done | awk '{print $1}' | { shasum -a 256 2>/dev/null || sha256sum; } | awk '{print $1}'
}

if [[ "${check}" -eq 0 ]]; then
    lock_dir="${builds_root}/upstream.lock"
    if ! acquire_lock "${lock_dir}"; then
        printf 'Another upstream build is running (%s).\n' "${lock_dir}" >&2
        exit 75
    fi
fi
check_parent=""
cleanup() {
    if [[ -n "${check_parent}" ]]; then
        git -C "${repo}" worktree remove --force "${check_parent}/source" >/dev/null 2>&1 || true
        rmdir "${check_parent}" 2>/dev/null || true
    fi
    if [[ -n "${lock_dir}" ]]; then rm -rf "${lock_dir}"; fi
}
trap cleanup EXIT

if [[ "${fetch}" -eq 1 ]]; then
    if ! git -C "${repo}" remote get-url upstream >/dev/null 2>&1; then
        git -C "${repo}" remote add upstream "${upstream_url}"
    fi
    git -C "${repo}" fetch --quiet upstream main
fi
sha="$(git -C "${repo}" rev-parse --verify "${ref}^{commit}")"
if ! git -C "${repo}" merge-base --is-ancestor "${sha}" refs/remotes/upstream/main; then
    printf 'Ref is not an ancestor of the fetched upstream/main: %s\n' "${ref}" >&2
    exit 1
fi
fingerprint="$(packaging_fingerprint)"

if [[ "${if_changed}" -eq 1 ]]; then
    manifest="${install_dir}/${app_name}/Contents/Resources/build-info.json"
    if [[ -f "${manifest}" ]] && python3 - "${manifest}" "${sha}" "${fingerprint}" <<'PY'
import json, sys
m = json.load(open(sys.argv[1]))
sys.exit(0 if m.get('role') == 'upstream' and m.get('sha') == sys.argv[2]
         and m.get('packaging_fingerprint') == sys.argv[3] else 1)
PY
    then
        printf 'Up to date: %s has upstream %s.\n' "${install_dir}/${app_name}" "${sha}"
        exit 0
    fi
fi
if [[ "${install}" -eq 1 ]] && game_running; then
    printf 'Open Nectar is running; not building upstream %s now.\n' "${sha}" >&2
    exit 75
fi

if [[ "${check}" -eq 1 ]]; then
    check_parent="$(mktemp -d "${TMPDIR:-/tmp}/pikmin-upstream.XXXXXXXX")"
    source_dir="${check_parent}/source"
    git -C "${repo}" worktree add --quiet --detach "${source_dir}" "${sha}"
else
    source_dir="${builds_root}/upstream-source"
    # Nothing that must survive may live in the worktree: it is reset every run.
    for candidate in "${build}" "${data}" ${output:+"${output}"} ${install_dir:+"${install_dir}"}; do
        if overlaps "${candidate}" "${source_dir}"; then
            printf 'Path must be outside the upstream source worktree %s: %s\n' "${source_dir}" "${candidate}" >&2
            exit 2
        fi
    done
    if [[ -e "${source_dir}" || -L "${source_dir}" ]]; then
        common="$(git -C "${source_dir}" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
        mine="$(git -C "${repo}" rev-parse --path-format=absolute --git-common-dir)"
        if [[ -z "${common}" || "$(cd "${common}" && pwd -P)" != "$(cd "${mine}" && pwd -P)" ]]; then
            printf 'Leaving %s alone: it is not a worktree of %s.\n' "${source_dir}" "${repo}" >&2
            exit 1
        fi
        git -C "${source_dir}" checkout --quiet --force --detach "${sha}"
        git -C "${source_dir}" clean -fdxq
    else
        mkdir -p "${builds_root}"
        git -C "${repo}" worktree prune
        git -C "${repo}" worktree add --quiet --detach "${source_dir}" "${sha}"
    fi
fi

git -C "${source_dir}" apply --check "${overlay}"
git -C "${source_dir}" apply --check "${window_overlay}"
if [[ "${check}" -eq 1 ]]; then
    printf 'Upstream %s: identity overlays apply cleanly.\n' "${sha}"
    exit 0
fi
git -C "${source_dir}" apply "${overlay}"
git -C "${source_dir}" apply "${window_overlay}"
changed="$(git -C "${source_dir}" diff --name-only)"
expected="$(printf '%s\n' "${patched_files[@]}" | LC_ALL=C sort)"
if [[ "$(printf '%s\n' "${changed}" | LC_ALL=C sort)" != "${expected}" ]]; then
    printf 'Identity overlays changed unexpected source files:\n%s\n' "${changed}" >&2
    exit 1
fi
for file in "${copied_files[@]}"; do
    mkdir -p "$(dirname -- "${source_dir}/${file}")"
    cp "${repo}/${file}" "${source_dir}/${file}"
done

if [[ "${default_output}" -eq 1 ]]; then
    mkdir -p "${builds_root}/upstream-out"
    output="$(mktemp -d "${builds_root}/upstream-out/$(date -u +%Y%m%dT%H%M%SZ)-${sha:0:12}-XXXXXX")"
    rmdir "${output}"
fi
mkdir -p "${build}"
install_args=()
if [[ "${install}" -eq 1 ]]; then install_args+=(--install "${install_dir}"); fi
allowed="$(printf '%s\n' "${patched_files[@]}" "${copied_files[@]}")"
rc=0
PIKMIN_BUILD_ROLE=upstream PIKMIN_DATA_ROOT="${data}" PIKMIN_LAUNCHER_OVERLAY="${overlay}" \
    PIKMIN_WINDOW_OVERLAY="${window_overlay}" PIKMIN_ALLOWED_CHANGES="${allowed}" \
    PIKMIN_PACKAGING_FINGERPRINT="${fingerprint}" \
    "${source_dir}/games/pikmin1/packaging/macos/build-app.sh" --build-dir "${build}" --output-dir "${output}" \
    ${install_args[@]+"${install_args[@]}"} || rc=$?

if [[ "${default_output}" -eq 1 ]]; then
    # Keep the newest three default outputs; names start with the UTC time.
    find "${builds_root}/upstream-out" -mindepth 1 -maxdepth 1 -type d -name '20*' | LC_ALL=C sort -r \
        | tail -n +4 | while IFS= read -r old; do rm -rf -- "${old}"; done
fi
if [[ "${rc}" -ne 0 ]]; then exit "${rc}"; fi
printf 'Upstream game source: %s\nPackaging fingerprint: %s\n' "${sha}" "${fingerprint}"
