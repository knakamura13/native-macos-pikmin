#!/usr/bin/env bash
# install_log.sh: shared logging for the hourly auto-install LaunchAgents.
# Sourced by auto-install-kyle-develop.sh and auto-install-upstream.sh.
#
# The main log (~/Library/Logs/OpenNectar/auto-install*.log) gets a few
# timestamped summary lines per run. The full build output goes to a per-role
# file, last-build-<role>.log, overwritten every run. When a build fails, its
# last lines are copied into the main log so the failure is visible there.
#
# Callers set log_file and last_build_file before using these functions.
# last_build_final, if set, is the path named in failure messages (for a
# caller that writes to a temporary file and moves it there afterwards).

install_log_max_bytes="${INSTALL_LOG_MAX_BYTES:-5242880}"
install_log_keep_lines="${INSTALL_LOG_KEEP_LINES:-5000}"
install_log_tail_lines="${INSTALL_LOG_TAIL_LINES:-100}"

log() {
    local line
    line="$(date '+%Y-%m-%d %H:%M:%S') $*"
    mkdir -p "$(dirname -- "${log_file}")"
    printf '%s\n' "${line}" >> "${log_file}"
    if [[ -t 2 ]]; then
        printf '%s\n' "${line}" >&2
    fi
}

# Keep the main log small: past the size cap, keep only its last lines. The
# file is rewritten in place (same inode), so a LaunchAgent that has it open
# for StandardOutPath keeps appending to the trimmed file.
cap_log() {
    local file="${1:-${log_file}}" size tmp
    [[ -f "${file}" ]] || return 0
    size="$(wc -c < "${file}" | tr -d ' ')"
    if [[ "${size}" -le "${install_log_max_bytes}" ]]; then
        return 0
    fi
    tmp="$(mktemp "${file}.trim.XXXXXX")"
    tail -n "${install_log_keep_lines}" "${file}" > "${tmp}"
    cat "${tmp}" > "${file}"
    rm -f "${tmp}"
    log "trimmed log from ${size} bytes to its last ${install_log_keep_lines} lines"
}

# Start a fresh per-run output file.
start_build_output() {
    mkdir -p "$(dirname -- "${last_build_file}")"
    printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" > "${last_build_file}"
}

# run_quiet COMMAND...: append the command's output to last_build_file only.
run_quiet() {
    "$@" >> "${last_build_file}" 2>&1
}

# run_build_logged LABEL COMMAND...: run the build with its output in
# last_build_file, then log "LABEL ok" or "LABEL failed (exit N)" with the
# duration. On failure the last lines of the output go into the main log.
# Returns the command's exit status.
run_build_logged() {
    local label="$1" start rc=0 secs
    shift
    start="${SECONDS}"
    "$@" >> "${last_build_file}" 2>&1 || rc=$?
    secs=$(( SECONDS - start ))
    if [[ "${rc}" -eq 0 ]]; then
        log "${label} ok in $(format_duration "${secs}")"
    elif [[ "${rc}" -eq 75 ]]; then
        # 75 means deferred (the game is running or a lock is held), not broken.
        log "${label} stopped after $(format_duration "${secs}") (exit 75, deferred)"
    else
        log "${label} failed (exit ${rc}) after $(format_duration "${secs}"); full output: ${last_build_final:-${last_build_file}}"
        copy_output_tail
    fi
    return "${rc}"
}

copy_output_tail() {
    [[ -f "${last_build_file}" ]] || return 0
    {
        printf -- '---- last %s lines of %s ----\n' "${install_log_tail_lines}" "${last_build_file}"
        tail -n "${install_log_tail_lines}" "${last_build_file}"
        printf -- '---- end ----\n'
    } >> "${log_file}"
}

format_duration() {
    local s="$1"
    printf '%dm%02ds' $(( s / 60 )) $(( s % 60 ))
}

# Log build-app.sh's "Installed:" and "Previous app:" lines (and other summary
# lines matching PATTERN, an extended regex) from the build output.
log_summary_lines() {
    local pattern="${1:-^(Installed|Previous app): }" line
    [[ -f "${last_build_file}" ]] || return 0
    while IFS= read -r line; do
        log "${line}"
    done < <(grep -E "${pattern}" "${last_build_file}" || true)
}

install_log_self_test() {
    local tmp rc=0
    tmp="$(mktemp -d)"
    (
        set -euo pipefail
        log_file="${tmp}/main.log"
        last_build_file="${tmp}/last-build.log"
        install_log_max_bytes=1000
        install_log_keep_lines=5
        install_log_tail_lines=3

        # A passing build: one summary line in the main log, output elsewhere.
        start_build_output "test build"
        run_build_logged "build" sh -c 'echo noisy-line; echo "Installed: /Applications/X.app"; echo "Previous app: /tmp/backup/X.app"'
        log_summary_lines
        grep -q ' build ok in 0m0[0-9]s$' "${log_file}"
        grep -q ' Installed: /Applications/X.app$' "${log_file}"
        grep -q ' Previous app: /tmp/backup/X.app$' "${log_file}"
        if grep -q noisy-line "${log_file}"; then echo 'build output leaked into the main log' >&2; exit 1; fi
        grep -q noisy-line "${last_build_file}"

        # A failing build: status, duration and the output's tail.
        start_build_output "test build 2"
        if run_build_logged "build" sh -c 'for i in 1 2 3 4 5 6; do echo "out-$i"; done; exit 4'; then
            echo 'a failed build returned success' >&2; exit 1
        fi
        grep -q 'build failed (exit 4) after' "${log_file}"
        grep -q '^out-6$' "${log_file}"
        grep -q '^out-4$' "${log_file}"
        if grep -q '^out-3$' "${log_file}"; then echo 'copied more than the tail' >&2; exit 1; fi
        start_build_output "test build 3"
        run_build_logged "build" sh -c 'echo deferred-output; exit 75' || [[ $? -eq 75 ]]
        grep -q 'exit 75, deferred' "${log_file}"
        if grep -q '^deferred-output$' "${log_file}"; then echo 'a deferred build copied its output' >&2; exit 1; fi
        start_build_output "test build 2 again"
        # The per-run file is overwritten each run.
        if grep -q noisy-line "${last_build_file}"; then echo 'last-build file was not reset' >&2; exit 1; fi

        # The cap keeps the same inode and only the last lines.
        local inode_before inode_after
        for i in $(seq 1 200); do printf 'filler line %03d xxxxxxxxxxxxxxxxxxxx\n' "${i}" >> "${log_file}"; done
        inode_before="$(ls -i "${log_file}" | awk '{print $1}')"
        exec 9>> "${log_file}"
        cap_log
        printf 'appended through old fd\n' >&9
        exec 9>&-
        inode_after="$(ls -i "${log_file}" | awk '{print $1}')"
        [[ "${inode_before}" == "${inode_after}" ]]
        grep -q 'filler line 200' "${log_file}"
        if grep -q 'filler line 190' "${log_file}"; then echo 'cap kept too much' >&2; exit 1; fi
        grep -q 'trimmed log from' "${log_file}"
        grep -q 'appended through old fd' "${log_file}"
        [[ "$(wc -l < "${log_file}" | tr -d ' ')" -le 7 ]]
        # Under the cap nothing changes.
        local before
        before="$(cat "${log_file}")"
        cap_log
        [[ "$(cat "${log_file}")" == "${before}" ]]
        [[ "$(format_duration 125)" == "2m05s" ]]
    ) || rc=$?
    rm -rf "${tmp}"
    return "${rc}"
}
