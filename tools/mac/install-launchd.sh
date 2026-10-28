#!/usr/bin/env bash
# install-launchd.sh [--uninstall|--self-test]
#
# Install the hourly LaunchAgent that keeps /Applications/Pikmin 1.app on
# origin/kyle-develop. Run it from the checkout that should supply `git fetch`
# (a kyle-patches checkout is the usual one). --uninstall removes the agent.

set -euo pipefail

label="org.opennectar.kyle-develop-autoinstall"
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
template="${script_dir}/org.opennectar.kyle-develop-autoinstall.plist"
autoinstall="${script_dir}/auto-install-kyle-develop.sh"

xml_escape() {
    local text=$1
    text=${text//&/&amp;}
    text=${text//</&lt;}
    text=${text//>/&gt;}
    printf '%s' "${text}"
}

render_plist() {
    local dest="$1"
    local autoinstall_path="$2"
    local repo="$3"
    local home="$4"
    local path_value="$5"
    local log_path="$6"
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
    local dest="$1"
    local domain="gui/${UID}"
    launchctl bootout "${domain}" "${dest}" 2>/dev/null || true
    launchctl bootout "${domain}/${label}" 2>/dev/null || true
    rm -f "${dest}"
    printf 'Removed %s\n' "${dest}"
}

self_test() {
    local tmp dest repo home path_value log_path
    tmp="$(mktemp -d)"
    dest="${tmp}/agent.plist"
    repo="/tmp/checkout"
    home="/tmp/home"
    path_value="/opt/homebrew/bin:/usr/bin"
    log_path="${home}/Library/Logs/OpenNectar/auto-install.log"
    render_plist "${dest}" "/tmp/checkout/tools/mac/auto-install-kyle-develop.sh" \
        "${repo}" "${home}" "${path_value}" "${log_path}"
    if grep -q '__' "${dest}"; then
        printf 'plist still has placeholders\n' >&2
        return 1
    fi
    grep -q '<string>org.opennectar.kyle-develop-autoinstall</string>' "${dest}"
    grep -q '<integer>3600</integer>' "${dest}"
    grep -q '<key>RunAtLoad</key>' "${dest}"
    grep -q '<true/>' "${dest}"
    grep -q '<string>/tmp/checkout/tools/mac/auto-install-kyle-develop.sh</string>' "${dest}"
    grep -q '<string>/tmp/checkout</string>' "${dest}"
    grep -q '<string>/tmp/home</string>' "${dest}"
    grep -q 'auto-install.log</string>' "${dest}"
    rm -rf "${tmp}"
    printf 'install-launchd self-test ok\n'
}

if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    exit 0
fi

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

dest="${HOME}/Library/LaunchAgents/${label}.plist"
if [[ "${1:-}" == "--uninstall" ]]; then
    uninstall_agent "${dest}"
    exit 0
fi
if [[ $# -gt 0 ]]; then
    printf 'usage: %s [--uninstall]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
fi

repo="$(git -C "${script_dir}" rev-parse --show-toplevel)"
log_path="${HOME}/Library/Logs/OpenNectar/auto-install.log"
mkdir -p "${HOME}/Library/LaunchAgents" "$(dirname -- "${log_path}")"
render_plist "${dest}" "${autoinstall}" "${repo}" "${HOME}" "$(agent_path)" "${log_path}"
chmod 644 "${dest}"

domain="gui/${UID}"
launchctl bootout "${domain}" "${dest}" 2>/dev/null || true
launchctl bootstrap "${domain}" "${dest}"
printf 'Installed %s\n' "${dest}"
printf 'It runs now, then every hour. Log: %s\n' "${log_path}"
