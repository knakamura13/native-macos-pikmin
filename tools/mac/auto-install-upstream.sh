#!/usr/bin/env bash
# auto-install-upstream.sh [REPO]
#
# Keeps /Applications/Open Nectar.app on the latest upstream main. A LaunchAgent
# (org.opennectar.upstream-autoinstall) runs this hourly. It is a thin wrapper:
# the work is the standard upstream command,
#
#   tools/mac/build-upstream.sh --install --if-changed
#
# run from REPO (default: the checkout this script is in). If the game is open
# or another upstream build is running, that command exits 75 and this run logs
# "deferred" and exits 0; the next run tries again.
# Logs go to ~/Library/Logs/OpenNectar/auto-install-upstream.log.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
log_file="${HOME:-/tmp}/Library/Logs/OpenNectar/auto-install-upstream.log"

log() {
    local line
    line="$(date '+%Y-%m-%d %H:%M:%S') $*"
    mkdir -p "$(dirname -- "${log_file}")"
    printf '%s\n' "${line}" >> "${log_file}"
    if [[ -t 2 ]]; then printf '%s\n' "${line}" >&2; fi
}

# run_build COMMAND... : runs the build, maps its status to this job's status.
run_build() {
    local rc=0
    "$@" >> "${log_file}" 2>&1 || rc=$?
    case "${rc}" in
        0) log "done"; return 0 ;;
        75) log "deferred: the game is running or another upstream build holds the lock"; return 0 ;;
        *) log "upstream build failed (exit ${rc})"; return 1 ;;
    esac
}

self_test() {
    local tmp
    tmp="$(mktemp -d)"
    log_file="${tmp}/log"
    run_build true
    grep -q ' done$' "${log_file}"
    run_build sh -c 'exit 75'
    grep -q 'deferred' "${log_file}"
    if run_build sh -c 'exit 3'; then printf 'a failed build was reported as success\n' >&2; return 1; fi
    grep -q 'failed (exit 3)' "${log_file}"
    rm -rf "${tmp}"
    printf 'auto-install-upstream self-test ok\n'
}

if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    exit 0
fi
if [[ "$(uname -s)" != "Darwin" ]]; then
    printf 'auto-install-upstream.sh installs the macOS app. Run it on the Mac.\n' >&2
    exit 1
fi
if [[ $# -gt 1 ]]; then
    printf 'usage: %s [REPO]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
fi
repo="${1:-$(cd -- "${script_dir}/../.." && pwd)}"
helper="${repo}/tools/mac/build-upstream.sh"
if [[ ! -x "${helper}" ]]; then
    log "missing ${helper}"
    exit 1
fi
log "running ${helper} --install --if-changed"
run_build "${helper}" --install --if-changed
