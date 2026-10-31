#!/usr/bin/env bash
# copy-saves-to-upstream.sh [--data-dir DIR] [--replace] [--dry-run] | --self-test
#
# Opt-in legacy helper: copies real saves into a chosen folder (for example the
# old upstream-only path). New builds share the central save automatically via
# tools/mac/save_mode.py migrate. This script is kept for manual one-off copies.
#
# Copied (never moved, never changed):
#   Pikmin 1  ~/.local/share/pikmin-native/save/
#   Pikmin 2  <Pikmin 2 install>/save/, where the install folder is the one in
#             ~/.config/open-nectar/pikmin2_dir (skipped if that is not set)
# into <DIR>/save/ (default DIR: ~/.local/share/pikmin-native-upstream), which is
# the NECTAR_SAVE_DIR the upstream app uses for both games.
#
# It refuses while the game is running, refuses if both games have a file at the
# same path, and refuses if the upstream save folder already has files. With
# --replace, the existing upstream saves are first moved to
# ~/OpenNectar-archive.noindex/upstream-save-backups/<time>/ and then replaced.
# Progress made in the upstream app is separate from then on: nothing is ever
# copied back.

set -euo pipefail

game_running() {
    pgrep -x nectar >/dev/null 2>&1 || pgrep -x nectar-launcher >/dev/null 2>&1 || pgrep -x pikmin2 >/dev/null 2>&1
}

overlaps() {
    python3 - "$1" "$2" <<'PY'
import os, sys
a, b = (os.path.realpath(os.path.expanduser(p)) for p in sys.argv[1:3])
inside = lambda x, y: x == y or x.startswith(y.rstrip(os.sep) + os.sep)
sys.exit(0 if inside(a, b) or inside(b, a) else 1)
PY
}

# Lists files under DIR as relative paths.
relative_files() {
    (cd "$1" && find . \( -type f -o -type l \) | sed 's#^\./##' | LC_ALL=C sort)
}

pikmin2_save_dir() {
    local file="${XDG_CONFIG_HOME:-${HOME}/.config}/open-nectar/pikmin2_dir" dir
    [[ -f "${file}" ]] || return 0
    dir="$(head -n 1 "${file}" | sed 's/[[:space:]]*$//')"
    if [[ "${dir}" == /* && -d "${dir}/save" ]]; then printf '%s\n' "${dir}/save"; fi
}

# copy_saves DEST REPLACE DRY_RUN ARCHIVE SOURCE...
copy_saves() {
    local dest="$1" replace="$2" dry_run="$3" archive="$4"
    shift 4
    local sources=() src real
    real="${HOME}/.local/share/pikmin-native"
    if overlaps "$(dirname -- "${dest}")" "${real}"; then
        printf 'Refusing: %s overlaps the real data folder %s.\n' "${dest}" "${real}" >&2
        return 2
    fi
    for src in "$@"; do
        [[ -n "${src}" && -d "${src}" ]] || continue
        if overlaps "${dest}" "${src}"; then
            printf 'Refusing: %s overlaps the source %s.\n' "${dest}" "${src}" >&2
            return 2
        fi
        sources+=("${src}")
    done
    if [[ "${#sources[@]}" -eq 0 ]]; then
        printf 'No saves found to copy.\n' >&2
        return 1
    fi
    if game_running; then
        printf 'Open Nectar is running; quit it first so the saves are not mid-write.\n' >&2
        return 75
    fi
    local dupes
    dupes="$(for src in "${sources[@]}"; do relative_files "${src}"; done | LC_ALL=C sort | uniq -d)"
    if [[ -n "${dupes}" ]]; then
        printf 'Refusing: both games have these save files:\n%s\n' "${dupes}" >&2
        return 1
    fi
    if [[ -d "${dest}" && -n "$(relative_files "${dest}")" ]]; then
        if [[ "${replace}" -ne 1 ]]; then
            printf 'Refusing: %s already has saves. Rerun with --replace to move them aside first.\n' "${dest}" >&2
            return 1
        fi
        if [[ "${dry_run}" -eq 1 ]]; then
            printf 'Would move %s to %s/<time>/save\n' "${dest}" "${archive}"
        else
            local backup
            mkdir -p "${archive}"
            backup="$(mktemp -d "${archive}/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")"
            mv "${dest}" "${backup}/save"
            printf 'Moved the old upstream saves to %s/save\n' "${backup}"
        fi
    fi
    for src in "${sources[@]}"; do
        if [[ "${dry_run}" -eq 1 ]]; then
            printf 'Would copy %s/ -> %s/\n' "${src}" "${dest}"
        else
            mkdir -p "${dest}"
            cp -Rp "${src}/." "${dest}/"
            printf 'Copied %s/ -> %s/\n' "${src}" "${dest}"
        fi
    done
}

self_test() {
    local tmp home dest p1 p2 rc
    tmp="$(mktemp -d)"
    home="${tmp}/home"
    HOME="${home}"
    unset XDG_CONFIG_HOME
    game_running() { return 1; }
    p1="${home}/.local/share/pikmin-native/save"
    p2="${tmp}/pikmin2/save"
    dest="${home}/.local/share/pikmin-native-upstream/save"
    mkdir -p "${p1}/card0" "${p2}/card0" "${home}/.config/open-nectar"
    printf 'one' > "${p1}/card0/01-GPIE.gci"
    printf 'two' > "${p2}/card0/01-GPVE.gci"
    printf '%s\n' "${tmp}/pikmin2" > "${home}/.config/open-nectar/pikmin2_dir"
    [[ "$(pikmin2_save_dir)" == "${p2}" ]]

    copy_saves "${dest}" 0 0 "${tmp}/archive" "${p1}" "$(pikmin2_save_dir)" >/dev/null
    [[ "$(cat "${dest}/card0/01-GPIE.gci")" == one && "$(cat "${dest}/card0/01-GPVE.gci")" == two ]]
    [[ "$(cat "${p1}/card0/01-GPIE.gci")" == one && "$(cat "${p2}/card0/01-GPVE.gci")" == two ]]

    rc=0; copy_saves "${dest}" 0 0 "${tmp}/archive" "${p1}" 2>/dev/null || rc=$?
    [[ "${rc}" -eq 1 ]]
    printf 'upstream progress' > "${dest}/card0/01-GPIE.gci"
    copy_saves "${dest}" 1 0 "${tmp}/archive" "${p1}" >/dev/null
    [[ "$(cat "${dest}/card0/01-GPIE.gci")" == one ]]
    [[ "$(cat "${tmp}"/archive/*/save/card0/01-GPIE.gci)" == "upstream progress" ]]

    rc=0; copy_saves "${home}/.local/share/pikmin-native/x/save" 0 0 "${tmp}/archive" "${p1}" 2>/dev/null || rc=$?
    [[ "${rc}" -eq 2 ]]
    ln -s "${home}/.local/share/pikmin-native" "${tmp}/sneaky"
    rc=0; copy_saves "${tmp}/sneaky/save" 0 0 "${tmp}/archive" "${p1}" 2>/dev/null || rc=$?
    [[ "${rc}" -eq 2 ]]

    printf 'clash' > "${p2}/card0/01-GPIE.gci"
    rc=0; copy_saves "${tmp}/fresh/save" 0 0 "${tmp}/archive" "${p1}" "${p2}" 2>/dev/null || rc=$?
    [[ "${rc}" -eq 1 && ! -e "${tmp}/fresh/save" ]]

    game_running() { return 0; }
    rc=0; copy_saves "${tmp}/fresh/save" 0 0 "${tmp}/archive" "${p1}" 2>/dev/null || rc=$?
    [[ "${rc}" -eq 75 && ! -e "${tmp}/fresh/save" ]]
    rm -rf "${tmp}"
    printf 'copy-saves-to-upstream self-test ok\n'
}

if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    exit 0
fi
data="${HOME}/.local/share/pikmin-native-upstream"
replace=0 dry_run=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --data-dir) data="$2"; shift 2 ;;
        --replace) replace=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
done
if [[ "${data}" != /* ]]; then
    printf '%s\n' '--data-dir must be absolute.' >&2
    exit 2
fi
copy_saves "${data}/save" "${replace}" "${dry_run}" "${HOME}/OpenNectar-archive.noindex/upstream-save-backups" \
    "${HOME}/.local/share/pikmin-native/save" "$(pikmin2_save_dir)"
