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
# Each run writes a few summary lines to
# ~/Library/Logs/OpenNectar/auto-install-upstream.log (capped at about 5 MB).
# The full output of a run that builds goes to last-build-upstream.log next to
# it; an up-to-date run leaves that file alone.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
log_file="${HOME:-/tmp}/Library/Logs/OpenNectar/auto-install-upstream.log"
last_build_final="${HOME:-/tmp}/Library/Logs/OpenNectar/last-build-upstream.log"
last_build_file=""

# log, cap_log, start_build_output, run_build_logged, log_summary_lines.
# shellcheck source=install_log.sh
source "${script_dir}/install_log.sh"

summary_pattern='^(Up to date|Installed|Previous app|Upstream game source): '

# run_build COMMAND... : runs the build with its output in a temporary file,
# logs the summary lines, and maps its status to this job's status. The output
# replaces last-build-upstream.log unless the run was only "Up to date".
run_build() {
    local rc=0
    mkdir -p "$(dirname -- "${last_build_final}")"
    last_build_file="$(mktemp "${last_build_final}.XXXXXX")"
    start_build_output "$*"
    run_build_logged "build" "$@" || rc=$?
    log_summary_lines "${summary_pattern}"
    if [[ "${rc}" -eq 0 ]] && grep -q '^Up to date: ' "${last_build_file}"; then
        rm -f "${last_build_file}"
    else
        mv -f "${last_build_file}" "${last_build_final}"
    fi
    last_build_file=""
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
    last_build_final="${tmp}/last-build-upstream.log"
    run_build true
    grep -q ' done$' "${log_file}"
    # A build's output stays out of the main log; its summary lines do not.
    run_build sh -c 'echo compiler-noise; echo "Installed: /Applications/Open Nectar.app"; echo "Upstream game source: abc"'
    if grep -q compiler-noise "${log_file}"; then printf 'build output leaked into the main log\n' >&2; return 1; fi
    grep -q 'compiler-noise' "${last_build_final}"
    grep -q ' Installed: /Applications/Open Nectar.app$' "${log_file}"
    grep -q ' Upstream game source: abc$' "${log_file}"
    # An up-to-date run logs its line and keeps the last build's output.
    run_build sh -c 'echo "Up to date: /Applications/Open Nectar.app has upstream abc."'
    grep -q ' Up to date: /Applications/Open Nectar.app has upstream abc.$' "${log_file}"
    grep -q 'compiler-noise' "${last_build_final}"
    run_build sh -c 'exit 75'
    grep -q 'deferred' "${log_file}"
    if run_build sh -c 'exit 3'; then printf 'a failed build was reported as success\n' >&2; return 1; fi
    grep -q 'failed (exit 3)' "${log_file}"
    grep -q "full output: ${last_build_final}" "${log_file}"
    [[ -z "$(find "${tmp}" -name 'last-build-upstream.log.*')" ]]
    rm -rf "${tmp}"
    install_log_self_test
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
cap_log
log "start: ${helper} --install --if-changed"
run_build "${helper}" --install --if-changed
