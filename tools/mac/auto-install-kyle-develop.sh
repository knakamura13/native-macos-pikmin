#!/usr/bin/env bash
# auto-install-kyle-develop.sh [REPO]
#
# Keep /Applications/Pikmin 1.app on origin/kyle-develop.
# A LaunchAgent runs this hourly. If the game is open, it exits 0 and the next
# run tries again. Logs go to ~/Library/Logs/OpenNectar/auto-install.log.
#
# REPO is a checkout that can fetch origin. The LaunchAgent passes the checkout
# that install-launchd.sh was run from. The build itself uses a worktree in
# ~/OpenNectar-builds.noindex, not that checkout's working tree.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
log_file="${HOME:-/tmp}/Library/Logs/OpenNectar/auto-install.log"
app_info="/Applications/Pikmin 1.app/Contents/Resources/build-info.json"
builds_root="${HOME:-}/OpenNectar-builds.noindex"
lock_dir=""

log() {
    local line
    line="$(date '+%Y-%m-%d %H:%M:%S') $*"
    mkdir -p "$(dirname -- "${log_file}")"
    printf '%s\n' "${line}" >> "${log_file}"
    if [[ -t 2 ]]; then
        printf '%s\n' "${line}" >&2
    fi
}

read_json_sha() {
    local file="$1"
    [[ -f "${file}" ]] || return 0
    # No pipe to head: with pipefail, head's early close can fail the read.
    sed -n 's/^[[:space:]]*"sha"[[:space:]]*:[[:space:]]*"\([0-9a-fA-F]\{7,\}\)".*/\1/p' "${file}"
}

read_state_sha() {
    local file="$1"
    local line
    [[ -f "${file}" ]] || return 0
    line="$(head -n 1 "${file}" 2>/dev/null || true)"
    case "${line}" in
        [0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F][0-9a-fA-F]*)
            printf '%s\n' "${line}"
            ;;
    esac
}

game_running() {
    pgrep -x nectar >/dev/null 2>&1 || pgrep -x nectar-launcher >/dev/null 2>&1
}

# Directory mkdir is the lock. A dead pid is stale and is taken over.
acquire_lock() {
    local lock="$1"
    local pid
    mkdir -p "$(dirname -- "${lock}")"
    if mkdir "${lock}" 2>/dev/null; then
        printf '%s\n' "$$" > "${lock}/pid"
        return 0
    fi
    pid="$(cat "${lock}/pid" 2>/dev/null || true)"
    if [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null; then
        return 1
    fi
    # Stale lock: remove it and take it. A live racer wins the mkdir.
    rm -rf "${lock}"
    if mkdir "${lock}" 2>/dev/null; then
        printf '%s\n' "$$" > "${lock}/pid"
        return 0
    fi
    return 1
}

release_lock() {
    if [[ -n "${lock_dir}" ]]; then
        rm -rf "${lock_dir}"
    fi
}

ensure_worktree() {
    local repo="$1"
    local worktree="$2"
    local sha="$3"
    if [[ -e "${worktree}" && ! -e "${worktree}/.git" ]]; then
        log "leaving ${worktree} alone; it is not a git worktree"
        return 1
    fi
    if [[ ! -e "${worktree}/.git" ]]; then
        git -C "${repo}" worktree prune
        git -C "${repo}" worktree add --detach "${worktree}" "${sha}"
    else
        git -C "${worktree}" checkout --detach --force "${sha}"
    fi
}

write_state() {
    local file="$1"
    local sha="$2"
    local tmp
    tmp="$(mktemp "${file}.XXXXXX")"
    printf '%s\n' "${sha}" > "${tmp}"
    mv -f "${tmp}" "${file}"
}

self_test() {
    local tmp home
    tmp="$(mktemp -d)"
    home="${tmp}/home"
    HOME="${home}"
    builds_root="${home}/OpenNectar-builds.noindex"
    log_file="${home}/Library/Logs/OpenNectar/auto-install.log"
    mkdir -p "${builds_root}"
    printf '{\n  "sha": "0123456789abcdef0123456789abcdef01234567",\n  "branch": "kyle-develop"\n}\n' \
        > "${tmp}/build-info.json"
    local sha
    sha="$(read_json_sha "${tmp}/build-info.json")"
    [[ "${sha}" == "0123456789abcdef0123456789abcdef01234567" ]]
    printf '{\n  "sha": "unknown",\n  "branch": ""\n}\n' > "${tmp}/unknown.json"
    [[ -z "$(read_json_sha "${tmp}/unknown.json")" ]]
    printf 'not-a-sha\n' > "${tmp}/state"
    [[ -z "$(read_state_sha "${tmp}/state")" ]]
    printf 'abcdef0123456789\n' > "${tmp}/state"
    [[ "$(read_state_sha "${tmp}/state")" == "abcdef0123456789" ]]

    local lock sleep_pid
    lock="${builds_root}/auto-install.lock"
    sleep 30 &
    sleep_pid=$!
    mkdir "${lock}"
    printf '%s\n' "${sleep_pid}" > "${lock}/pid"
    if acquire_lock "${lock}"; then
        kill "${sleep_pid}" || true
        printf 'took a lock that was still held\n' >&2
        return 1
    fi
    kill "${sleep_pid}" || true
    wait "${sleep_pid}" || true
    acquire_lock "${lock}"
    lock_dir="${lock}"
    release_lock
    lock_dir=""
    [[ ! -d "${lock}" ]]

    mkdir "${lock}"
    printf '999999\n' > "${lock}/pid"
    acquire_lock "${lock}"
    [[ "$(cat "${lock}/pid")" == "$$" ]]
    lock_dir="${lock}"
    release_lock
    printf 'auto-install self-test ok\n'
}

if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    exit 0
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
    printf 'auto-install-kyle-develop.sh installs the macOS app. Run it on the Mac.\n' >&2
    exit 1
fi
if [[ -z "${HOME:-}" ]]; then
    printf 'HOME is not set.\n' >&2
    exit 1
fi

if [[ $# -gt 1 ]]; then
    printf 'usage: %s [REPO]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
fi
repo="${1:-}"
if [[ -z "${repo}" ]]; then
    repo="$(cd -- "${script_dir}/../.." && pwd)"
fi
builds_root="${HOME}/OpenNectar-builds.noindex"
log_file="${HOME}/Library/Logs/OpenNectar/auto-install.log"
worktree="${builds_root}/kyle-develop"
build_dir="${builds_root}/kyle-develop-build"
state_file="${builds_root}/installed-sha"
lock_dir="${builds_root}/auto-install.lock"

if ! acquire_lock "${lock_dir}"; then
    log "another auto-install is running"
    exit 0
fi
trap release_lock EXIT INT TERM

if ! git -C "${repo}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    log "not a git checkout: ${repo}"
    exit 1
fi

log "fetching origin kyle-develop"
if ! git -C "${repo}" fetch origin kyle-develop >> "${log_file}" 2>&1; then
    log "fetch failed"
    exit 1
fi
sha="$(git -C "${repo}" rev-parse refs/remotes/origin/kyle-develop)"
installed="$(read_json_sha "${app_info}")"
if [[ -z "${installed}" ]]; then
    installed="$(read_state_sha "${state_file}")"
fi
installed="$(printf '%s' "${installed}" | tr 'A-F' 'a-f')"
sha="$(printf '%s' "${sha}" | tr 'A-F' 'a-f')"
if [[ -n "${installed}" && "${installed}" == "${sha}" ]]; then
    log "up to date ${sha}"
    exit 0
fi

if game_running; then
    log "Pikmin is running; leaving ${installed:-nothing} in place, want ${sha}"
    exit 0
fi

log "updating ${installed:-nothing} -> ${sha}"
if ! ensure_worktree "${repo}" "${worktree}" "${sha}"; then
    exit 1
fi
mkdir -p "${build_dir}"
build_app="${worktree}/games/pikmin1/packaging/macos/build-app.sh"
if [[ ! -f "${build_app}" ]]; then
    log "missing ${build_app}"
    exit 1
fi
if "${build_app}" --build-dir "${build_dir}" --install >> "${log_file}" 2>&1; then
    write_state "${state_file}" "${sha}"
    log "installed ${sha}"
    exit 0
fi
if game_running; then
    log "install deferred because the game is running"
    exit 0
fi
log "install failed for ${sha}"
exit 1
