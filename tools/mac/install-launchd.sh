#!/usr/bin/env bash
# install-launchd.sh [--only develop|upstream] [--uninstall] | --self-test
#
# Installs the two hourly LaunchAgents (RunAtLoad, then every hour):
#
#   org.opennectar.kyle-develop-autoinstall
#       auto-install-kyle-develop.sh: keeps /Applications/Open Nectar Develop.app
#       on origin/kyle-develop. Log: ~/Library/Logs/OpenNectar/auto-install.log
#   org.opennectar.upstream-autoinstall
#       auto-install-upstream.sh: runs `build-upstream.sh --install --if-changed`
#       to keep /Applications/Open Nectar.app on upstream main.
#       Log: ~/Library/Logs/OpenNectar/auto-install-upstream.log
#
# Run it from the checkout that should supply `git fetch` and the packaging
# scripts (a kyle-patches checkout is the usual one). --only limits it to one
# agent. --uninstall removes the agents instead.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
roles=(develop upstream)

agent_label() {
    case "$1" in
        develop) printf '%s\n' "org.opennectar.kyle-develop-autoinstall" ;;
        upstream) printf '%s\n' "org.opennectar.upstream-autoinstall" ;;
    esac
}
agent_script() {
    case "$1" in
        develop) printf '%s\n' "${script_dir}/auto-install-kyle-develop.sh" ;;
        upstream) printf '%s\n' "${script_dir}/auto-install-upstream.sh" ;;
    esac
}
agent_log() {
    case "$1" in
        develop) printf '%s\n' "$2/Library/Logs/OpenNectar/auto-install.log" ;;
        upstream) printf '%s\n' "$2/Library/Logs/OpenNectar/auto-install-upstream.log" ;;
    esac
}

xml_escape() {
    local text=$1
    text=${text//&/&amp;}
    text=${text//</&lt;}
    text=${text//>/&gt;}
    printf '%s' "${text}"
}

render_plist() {
    local template="$1" dest="$2" autoinstall_path="$3" repo="$4" home="$5" path_value="$6" log_path="$7"
    local text
    text="$(cat "${template}")"
    text="${text//__AUTOINSTALL__/$(xml_escape "${autoinstall_path}")}"
    text="${text//__REPO__/$(xml_escape "${repo}")}"
    text="${text//__HOME__/$(xml_escape "${home}")}"
    text="${text//__PATH__/$(xml_escape "${path_value}")}"
    text="${text//__LOG__/$(xml_escape "${log_path}")}"
    printf '%s\n' "${text}" > "${dest}"
}

agent_path() {
    local base="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
    if [[ -n "${PATH:-}" ]]; then
        printf '%s\n' "${base}:${PATH}"
    else
        printf '%s\n' "${base}"
    fi
}

uninstall_agent() {
    local label="$1" dest="$2"
    local domain="gui/${UID}"
    launchctl bootout "${domain}" "${dest}" 2>/dev/null || true
    launchctl bootout "${domain}/${label}" 2>/dev/null || true
    rm -f "${dest}"
    printf 'Removed %s\n' "${dest}"
}

self_test() {
    local tmp dest role label script log_path
    tmp="$(mktemp -d)"
    for role in "${roles[@]}"; do
        label="$(agent_label "${role}")"
        dest="${tmp}/${label}.plist"
        script="/tmp/checkout/tools/mac/$(basename -- "$(agent_script "${role}")")"
        log_path="$(agent_log "${role}" /tmp/home)"
        render_plist "${script_dir}/${label}.plist" "${dest}" "${script}" /tmp/checkout /tmp/home \
            "/opt/homebrew/bin:/usr/bin" "${log_path}"
        if grep -q '__' "${dest}"; then
            printf 'plist still has placeholders\n' >&2
            return 1
        fi
        grep -q "<string>${label}</string>" "${dest}"
        grep -q '<integer>3600</integer>' "${dest}"
        grep -q '<key>RunAtLoad</key>' "${dest}"
        grep -q '<true/>' "${dest}"
        grep -q "<string>${script}</string>" "${dest}"
        grep -q '<string>/tmp/checkout</string>' "${dest}"
        grep -q '<string>/tmp/home</string>' "${dest}"
        grep -q "<string>${log_path}</string>" "${dest}"
        [[ -x "$(agent_script "${role}")" ]]
    done
    grep -q 'auto-install-upstream.log' "${tmp}/org.opennectar.upstream-autoinstall.plist"
    if grep -q 'auto-install-upstream' "${tmp}/org.opennectar.kyle-develop-autoinstall.plist"; then
        printf 'develop agent points at the upstream job\n' >&2
        return 1
    fi
    rm -rf "${tmp}"
    printf 'install-launchd self-test ok\n'
}

if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    exit 0
fi

uninstall=0
selected=("${roles[@]}")
while [[ $# -gt 0 ]]; do
    case "$1" in
        --uninstall) uninstall=1; shift ;;
        --only)
            case "${2:-}" in
                develop|upstream) selected=("$2") ;;
                *) printf '%s\n' '--only takes develop or upstream' >&2; exit 2 ;;
            esac
            shift 2
            ;;
        *) printf 'usage: %s [--only develop|upstream] [--uninstall]\n' "${BASH_SOURCE[0]}" >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    printf 'install-launchd.sh changes a macOS LaunchAgent. Run it on the Mac.\n' >&2
    exit 1
fi
if [[ "${UID}" -eq 0 ]]; then
    printf 'Run install-launchd.sh as your user, not as root.\n' >&2
    exit 1
fi
if [[ -z "${HOME:-}" ]]; then
    printf 'HOME is not set.\n' >&2
    exit 1
fi

repo="$(git -C "${script_dir}" rev-parse --show-toplevel)"
domain="gui/${UID}"
for role in "${selected[@]}"; do
    label="$(agent_label "${role}")"
    dest="${HOME}/Library/LaunchAgents/${label}.plist"
    if [[ "${uninstall}" -eq 1 ]]; then
        uninstall_agent "${label}" "${dest}"
        continue
    fi
    log_path="$(agent_log "${role}" "${HOME}")"
    mkdir -p "${HOME}/Library/LaunchAgents" "$(dirname -- "${log_path}")"
    render_plist "${script_dir}/${label}.plist" "${dest}" "$(agent_script "${role}")" "${repo}" "${HOME}" \
        "$(agent_path)" "${log_path}"
    chmod 644 "${dest}"
    launchctl bootout "${domain}" "${dest}" 2>/dev/null || true
    launchctl bootstrap "${domain}" "${dest}"
    printf 'Installed %s\n' "${dest}"
    printf 'It runs now, then every hour. Log: %s\n' "${log_path}"
done
