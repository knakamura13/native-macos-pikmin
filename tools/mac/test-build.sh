#!/usr/bin/env bash
# test-build.sh [--self-test]
#
# Build the checkout that contains this script (any branch or worktree) as
# "Open Nectar Test.app" and point ~/Desktop/Open Nectar at that build.
#
# Test builds stay in ~/OpenNectar-test-builds.noindex (Spotlight skips it), each
# with its own bundle id (org.opennectar.testbuild.<hash>). The newest 5 builds
# are kept; data/ is never removed. The apps in /Applications are left alone. Saves, settings, and launcher
# config for every test build go under ~/OpenNectar-test-builds.noindex/data,
# not ~/.local/share/pikmin-native. Extracted disc files are linked, not copied.
#
# Run it from any directory:
#   /path/to/checkout/tools/mac/test-build.sh

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd -- "${script_dir}/../.." && pwd)"

# macOS mv follows a symlink to a directory. ~/Desktop/Open Nectar points at
# an .app, which is a directory, so mv would drop the new link inside the app
# and leave the desktop shortcut unchanged. rename(2) replaces the symlink.
# The helper is a few lines of C compiled with cc, which the game build needs.
replace_symlink_entry() {
    local src="$1"
    local dest="$2"
    local work src_c bin
    if ! command -v cc >/dev/null 2>&1; then
        printf 'cc is required to update the Desktop shortcut.\n' >&2
        return 1
    fi
    work="$(mktemp -d)"
    src_c="${work}/rename-link.c"
    bin="${work}/rename-link"
    cat > "${src_c}" <<'EOF'
#include <stdio.h>
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    if (rename(argv[1], argv[2]) != 0) {
        perror("rename");
        return 1;
    }
    return 0;
}
EOF
    if ! cc -O2 -o "${bin}" "${src_c}"; then
        rm -rf "${work}"
        printf 'cc could not build the shortcut helper.\n' >&2
        return 1
    fi
    if ! "${bin}" "${src}" "${dest}"; then
        rm -rf "${work}"
        return 1
    fi
    rm -rf "${work}"
}

atomic_symlink() {
    local target="$1"
    local link="$2"
    local dir tmp
    if [[ -e "${link}" && ! -L "${link}" ]]; then
        printf 'Refusing to replace %s because it is not a symlink.\n' "${link}" >&2
        return 1
    fi
    dir="$(dirname -- "${link}")"
    mkdir -p "${dir}"
    tmp="${dir}/.open-nectar-link.$$"
    rm -f "${tmp}"
    ln -sfn "${target}" "${tmp}"
    if ! replace_symlink_entry "${tmp}" "${link}"; then
        rm -f "${tmp}"
        return 1
    fi
}

# Branch name, or the short sha when HEAD is detached. "data" is reserved.
sanitize_build_name() {
    local branch="$1"
    local short="$2"
    local name
    if [[ -z "${branch}" || "${branch}" == "HEAD" ]]; then
        printf '%s\n' "${short}"
        return 0
    fi
    name="$(printf '%s' "${branch}" | tr -c 'A-Za-z0-9._-' '-')"
    name="$(printf '%s' "${name}" | sed 's/^-*//; s/-*$//; s/--*/-/g')"
    name="$(printf '%s' "${name}" | cut -c1-80)"
    case "${name}" in
        ""|.|..|data) name="${short}" ;;
    esac
    printf '%s\n' "${name}"
}

build_identity_label() {
    if [[ -z "$1" || "$1" == "HEAD" ]]; then printf 'commit-%s\n' "$2"; else printf '%s\n' "$1"; fi
}

# Newest first. Never removes data/, and never removes current even if it is old.
prune_test_builds() {
    local root="$1"
    local keep="$2"
    local current="$3"
    local dir base count=0
    current="${current%/}"
    shopt -s nullglob
    local entries=("${root}"/*/)
    shopt -u nullglob
    if [[ "${#entries[@]}" -eq 0 ]]; then
        return 0
    fi
    # shellcheck disable=SC2012
    # ls -dt is the portable newest-first listing. macOS and GNU stat differ.
    while IFS= read -r dir; do
        [[ -z "${dir}" ]] && continue
        dir="${dir%/}"
        base="$(basename -- "${dir}")"
        if [[ "${base}" == "data" || "${base}" == .* ]]; then
            continue
        fi
        count=$((count + 1))
        if [[ "${count}" -gt "${keep}" && "${dir}" != "${current}" ]]; then
            rm -rf "${dir}"
        fi
    done < <(ls -1dt "${entries[@]}")
}

# Last step of a test build: mark DEST newest, point the Desktop shortcut at
# APP, then keep the newest 5 builds (DEST always stays, data/ is never removed).
finish_test_build() {
    local root="$1" dest="$2" app="$3" link="$4"
    touch "${dest}"
    atomic_symlink "${app}" "${link}"
    prune_test_builds "${root}" 5 "${dest}"
}

xml_escape() {
    local text=$1
    text=${text//&/&amp;}
    text=${text//</&lt;}
    text=${text//>/&gt;}
    printf '%s' "${text}"
}

json_escape() {
    local text=$1
    text=${text//\\/\\\\}
    text=${text//\"/\\\"}
    text=${text//$'\n'/}
    text=${text//$'\r'/}
    printf '%s' "${text}"
}

# The wrapper is what actually launches. NECTAR_SAVE_DIR has to be set:
# without it the game copies card0 out of ~/.local/share/pikmin-native/save.
# NECTAR_EXECUTABLE_PATH is the launcher's own path, so it must stay unset.
write_wrapper() {
    local file="$1"
    local data_home="$2"
    local config_home="$3"
    local cache_home="$4"
    local data_root="$5"
    local game_bin="$6"
    local launcher_bin="$7"
    local title="$8"
    local q_data_home q_config_home q_cache_home q_data_root q_game_bin q_launcher_bin q_save q_title
    q_data_home="$(printf '%q' "${data_home}")"
    q_config_home="$(printf '%q' "${config_home}")"
    q_cache_home="$(printf '%q' "${cache_home}")"
    q_data_root="$(printf '%q' "${data_root}")"
    q_game_bin="$(printf '%q' "${game_bin}")"
    q_launcher_bin="$(printf '%q' "${launcher_bin}")"
    q_save="$(printf '%q' "${data_root}/save")"
    q_title="$(printf '%q' "${title}")"
    cat > "${file}" <<EOF
#!/bin/bash
set -euo pipefail
export XDG_DATA_HOME=${q_data_home}
export XDG_CONFIG_HOME=${q_config_home}
export XDG_CACHE_HOME=${q_cache_home}
export NECTAR_PIKMIN1_DIR=${q_data_root}
export NECTAR_PIKMIN1_EXECUTABLE=${q_game_bin}
export NECTAR_SAVE_DIR=${q_save}
export PIKMIN_BUILD_INFO="\$(dirname -- "\$0")/../Resources/build-info.json"
export PIKMIN_BUNDLE_MANAGED=1
export PIKMIN_LAUNCHER_TITLE=${q_title}
unset NECTAR_EXECUTABLE_PATH PIKMIN_EXECUTABLE_PATH
data_root=\$NECTAR_PIKMIN1_DIR
mkdir -p "\$data_root/save" "\$XDG_CONFIG_HOME/open-nectar" "\$XDG_CACHE_HOME"
real_assets="\${HOME}/.local/share/pikmin-native/assets"
if [[ ! -e "\$data_root/assets" && ! -L "\$data_root/assets" && -f "\$real_assets/.pikmin-assets" ]]; then
    ln -s "\$real_assets" "\$data_root/assets"
fi
install_dir_file="\$XDG_CONFIG_HOME/open-nectar/install_dir"
if [[ ! -f "\$install_dir_file" ]]; then
    printf '%s\\n' "\$data_root" > "\$install_dir_file"
fi
exec ${q_launcher_bin} "\$@"
EOF
    chmod +x "${file}"
}

write_test_app() {
    local app="$1"
    local data_home="$2"
    local config_home="$3"
    local cache_home="$4"
    local data_root="$5"
    local game_bin="$6"
    local launcher_bin="$7"
    local sha="$8"
    local branch="$9"
    local snapshot="${10:-${sha:0:12}}"
    local macos="${app}/Contents/MacOS"
    local resources="${app}/Contents/Resources"
    local title="Open Nectar Test — ${branch} [${snapshot}]"
    local title_xml bundle_suffix
    title_xml="$(xml_escape "${title}")"
    bundle_suffix="$(printf '%s\n%s' "${branch}" "${snapshot}" | shasum -a 256 | cut -c1-16)"
    mkdir -p "${macos}" "${resources}"
    write_wrapper "${macos}/open-nectar" \
        "${data_home}" "${config_home}" "${cache_home}" \
        "${data_root}" "${game_bin}" "${launcher_bin}" "${title}"
    local data_home_xml config_home_xml cache_home_xml data_root_xml game_bin_xml save_xml
    local sha_json branch_json
    sha_json="$(json_escape "${sha}")"
    branch_json="$(json_escape "${branch}")"
    data_home_xml="$(xml_escape "${data_home}")"
    config_home_xml="$(xml_escape "${config_home}")"
    cache_home_xml="$(xml_escape "${cache_home}")"
    data_root_xml="$(xml_escape "${data_root}")"
    game_bin_xml="$(xml_escape "${game_bin}")"
    save_xml="$(xml_escape "${data_root}/save")"
    cat > "${app}/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>${title_xml}</string>
    <key>CFBundleDisplayName</key>
    <string>${title_xml}</string>
    <key>CFBundleIdentifier</key>
    <string>org.opennectar.testbuild.${bundle_suffix}</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleExecutable</key>
    <string>open-nectar</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSEnvironment</key>
    <dict>
        <key>XDG_DATA_HOME</key>
        <string>${data_home_xml}</string>
        <key>XDG_CONFIG_HOME</key>
        <string>${config_home_xml}</string>
        <key>XDG_CACHE_HOME</key>
        <string>${cache_home_xml}</string>
        <key>NECTAR_PIKMIN1_DIR</key>
        <string>${data_root_xml}</string>
        <key>NECTAR_PIKMIN1_EXECUTABLE</key>
        <string>${game_bin_xml}</string>
        <key>NECTAR_SAVE_DIR</key>
        <string>${save_xml}</string>
    </dict>
</dict>
</plist>
EOF
    printf '{\n  "sha": "%s",\n  "branch": "%s"\n}\n' "${sha_json}" "${branch_json}" \
        > "${resources}/build-info.json"
}

self_test() {
    local tmp home root desktop branch_name short name
    local data_home config_home cache_home data_root
    local app fake wrapper output link_target
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp}"' RETURN
    home="${tmp}/home"
    root="${home}/OpenNectar-test-builds.noindex"
    desktop="${home}/Desktop"
    mkdir -p "${root}/data" "${desktop}" "${home}/.local/share/pikmin-native/assets"
    printf 'disc\n' > "${home}/.local/share/pikmin-native/assets/.pikmin-assets"
    printf 'keep\n' > "${root}/data/marker"

    branch_name="$(sanitize_build_name 'fix/pad clamp' 'abc123')"
    [[ "${branch_name}" == "fix-pad-clamp" ]]
    branch_name="$(sanitize_build_name 'HEAD' 'abc123def456')"
    [[ "${branch_name}" == "abc123def456" ]]
    branch_name="$(sanitize_build_name 'data' 'abc123def456')"
    [[ "${branch_name}" == "abc123def456" ]]
    [[ "$(build_identity_label HEAD abc123)" == "commit-abc123" ]]
    [[ "$(build_identity_label HEAD def456)" == "commit-def456" ]]
    [[ "$(build_identity_label 'feature/foo' abc123)" == "feature/foo" ]]

    local i dir
    for i in 1 2 3 4 5 6; do
        dir="${root}/build-${i}"
        mkdir -p "${dir}"
        touch -t "20200101010${i}" "${dir}"
    done
    touch -t 201901010101 "${root}/data"
    mkdir -p "${root}/current"
    touch -t 201801010101 "${root}/current"
    prune_test_builds "${root}" 5 "${root}/current"
    [[ -f "${root}/data/marker" ]]
    [[ -d "${root}/current" ]]
    [[ -d "${root}/build-6" ]]
    [[ -d "${root}/build-2" ]]
    [[ ! -d "${root}/build-1" ]]

    mkdir -p "${tmp}/app1" "${tmp}/app2"
    atomic_symlink "${tmp}/app1" "${desktop}/Open Nectar"
    atomic_symlink "${tmp}/app2" "${desktop}/Open Nectar"
    link_target="$(readlink "${desktop}/Open Nectar")"
    [[ "${link_target}" == "${tmp}/app2" ]]
    if compgen -G "${tmp}/app1/.open-nectar-link.*" >/dev/null; then
        printf 'temp link was left inside the app\n' >&2
        return 1
    fi
    mkdir -p "${desktop}/real-folder"
    if atomic_symlink "${tmp}/app1" "${desktop}/real-folder" 2>/dev/null; then
        printf 'atomic_symlink replaced a real folder\n' >&2
        return 1
    fi

    data_home="${root}/data"
    config_home="${data_home}/config"
    cache_home="${data_home}/cache"
    data_root="${data_home}/pikmin-native"
    app="${root}/sample/Open Nectar Test.app"
    fake="${tmp}/fake-launcher"
    cat > "${fake}" <<'FAKE'
#!/bin/bash
printf 'XDG_DATA_HOME=%s\n' "${XDG_DATA_HOME}"
printf 'XDG_CONFIG_HOME=%s\n' "${XDG_CONFIG_HOME}"
printf 'NECTAR_PIKMIN1_DIR=%s\n' "${NECTAR_PIKMIN1_DIR}"
printf 'NECTAR_PIKMIN1_EXECUTABLE=%s\n' "${NECTAR_PIKMIN1_EXECUTABLE}"
printf 'NECTAR_SAVE_DIR=%s\n' "${NECTAR_SAVE_DIR}"
printf 'PIKMIN_LAUNCHER_TITLE=%s\n' "${PIKMIN_LAUNCHER_TITLE}"
if [[ -n "${NECTAR_EXECUTABLE_PATH+x}" ]]; then
    printf 'NECTAR_EXECUTABLE_PATH=set\n'
else
    printf 'NECTAR_EXECUTABLE_PATH=unset\n'
fi
FAKE
    chmod +x "${fake}"
    write_test_app "${app}" "${data_home}" "${config_home}" "${cache_home}" \
        "${data_root}" "${tmp}/bin/nectar" "${fake}" \
        "0123456789abcdef" "fixture"
    wrapper="${app}/Contents/MacOS/open-nectar"
    HOME="${home}" output="$("${wrapper}")"
    [[ "${output}" == *"XDG_DATA_HOME=${data_home}"* ]]
    [[ "${output}" == *"NECTAR_PIKMIN1_DIR=${data_root}"* ]]
    [[ "${output}" == *"NECTAR_SAVE_DIR=${data_root}/save"* ]]
    [[ "${output}" == *"NECTAR_PIKMIN1_EXECUTABLE=${tmp}/bin/nectar"* ]]
    [[ "${output}" == *"NECTAR_EXECUTABLE_PATH=unset"* ]]
    [[ "${output}" == *"PIKMIN_LAUNCHER_TITLE=Open Nectar Test — fixture [0123456789ab]"* ]]
    [[ -L "${data_root}/assets" ]]
    [[ "$(readlink "${data_root}/assets")" == "${home}/.local/share/pikmin-native/assets" ]]
    [[ "$(head -n 1 "${config_home}/open-nectar/install_dir")" == "${data_root}" ]]
    [[ -d "${data_root}/save" ]]
    grep -q '<string>org.opennectar.testbuild\.[0-9a-f]\{16\}</string>' "${app}/Contents/Info.plist"
    grep -q 'Open Nectar Test — fixture \[0123456789ab\]' "${app}/Contents/Info.plist"
    if grep -q 'Pikmin 1' "${app}/Contents/Info.plist"; then
        printf 'test app still says Pikmin 1\n' >&2
        return 1
    fi
    local other_app="${root}/other/Open Nectar Test.app" fixture_id other_id
    write_test_app "${other_app}" "${data_home}" "${config_home}" "${cache_home}" \
        "${data_root}" "${tmp}/bin/nectar" "${fake}" "0123456789abcdef" "fixture" "another-snapshot"
    fixture_id="$(sed -n '/<key>CFBundleIdentifier<\/key>/{n;s/.*<string>\(.*\)<\/string>.*/\1/p;}' "${app}/Contents/Info.plist")"
    other_id="$(sed -n '/<key>CFBundleIdentifier<\/key>/{n;s/.*<string>\(.*\)<\/string>.*/\1/p;}' "${other_app}/Contents/Info.plist")"
    [[ -n "${fixture_id}" && -n "${other_id}" && "${fixture_id}" != "${other_id}" ]]

    # The real flow's last step: link and prune to the newest 5, keeping data/
    # and the build just made.
    local finish_root="${tmp}/finish" newest
    mkdir -p "${finish_root}/data"
    printf 'keep\n' > "${finish_root}/data/marker"
    for i in 1 2 3 4 5 6 7; do
        mkdir -p "${finish_root}/old-${i}/Open Nectar Test.app"
        touch -t "20200101010${i}" "${finish_root}/old-${i}"
    done
    touch -t 201901010101 "${finish_root}/data"
    newest="${finish_root}/new-build"
    mkdir -p "${newest}/Open Nectar Test.app"
    finish_test_build "${finish_root}" "${newest}" "${newest}/Open Nectar Test.app" "${desktop}/Open Nectar"
    [[ "$(readlink "${desktop}/Open Nectar")" == "${newest}/Open Nectar Test.app" ]]
    [[ -f "${finish_root}/data/marker" && -d "${newest}" ]]
    [[ "$(find "${finish_root}" -mindepth 1 -maxdepth 1 -type d ! -name data | wc -l | tr -d ' ')" -eq 5 ]]
    [[ -d "${finish_root}/old-7" && -d "${finish_root}/old-4" && ! -d "${finish_root}/old-3" ]]
    printf 'test-build self-test ok\n'
}

if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    exit 0
fi
if [[ $# -gt 0 ]]; then
    printf 'usage: %s [--self-test]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
fi
if [[ "$(uname -s)" != "Darwin" ]]; then
    printf 'test-build.sh builds the macOS app. Run it on the Mac.\n' >&2
    exit 1
fi
if [[ -z "${HOME:-}" ]]; then
    printf 'HOME is not set.\n' >&2
    exit 1
fi

game_dir="${repo}/games/pikmin1"
if [[ ! -f "${game_dir}/CMakeLists.txt" ]]; then
    printf 'No Pikmin 1 checkout at %s\n' "${game_dir}" >&2
    exit 1
fi

branch="$(git -C "${repo}" rev-parse --abbrev-ref HEAD)"
sha="$(git -C "${repo}" rev-parse HEAD)"
short="$(git -C "${repo}" rev-parse --short=12 HEAD)"
name="$(sanitize_build_name "${branch}" "${short}")"
identity="$(build_identity_label "${branch}" "${short}")"
identity_hash="$(printf '%s' "${identity}" | shasum -a 256 | cut -c1-16)"

root="${HOME}/OpenNectar-test-builds.noindex"
mkdir -p "${root}"
dest="$(mktemp -d "${root}/${name}-${identity_hash}.XXXXXXXX")"
snapshot="${short}-${dest##*.}"
build_dir="${dest}/build"
data_home="${root}/data"
config_home="${data_home}/config"
cache_home="${data_home}/cache"
data_root="${data_home}/pikmin-native"
app="${dest}/Open Nectar Test.app"
mkdir -p "${build_dir}" "${data_home}"

cache_flags=()
if command -v ccache >/dev/null 2>&1; then
    cache_flags+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi
jobs="$(sysctl -n hw.ncpu 2>/dev/null || printf '4')"
# bash 3.2 with set -u rejects an empty array, so expand it the long way.
cmake -S "${game_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release \
    ${cache_flags[@]+"${cache_flags[@]}"}
cmake --build "${build_dir}" --target pikmin_pc pikmin_launcher -j "${jobs}"

game_source="${build_dir}/bin/nectar"
launcher_source="${build_dir}/bin/nectar-launcher"
if [[ ! -x "${game_source}" || ! -x "${launcher_source}" ]]; then
    printf 'Build did not produce nectar and nectar-launcher in %s\n' "${build_dir}/bin" >&2
    exit 1
fi
mkdir -p "${app}/Contents/MacOS"
game_bin="${app}/Contents/MacOS/nectar"
launcher_bin="${app}/Contents/MacOS/nectar-launcher"
cp "${game_source}" "${game_bin}"
cp "${launcher_source}" "${launcher_bin}"

write_test_app "${app}" "${data_home}" "${config_home}" "${cache_home}" \
    "${data_root}" "${game_bin}" "${launcher_bin}" "${sha}" "${identity}" "${snapshot}"
python3 "${script_dir}/build_identity.py" --repo "${repo}" \
    --output "${app}/Contents/Resources/build-info.json" --game "${game_bin}" \
    --launcher "${launcher_bin}" --cache "${build_dir}/CMakeCache.txt" --data "${data_root}" --role feature
finish_test_build "${root}" "${dest}" "${app}" "${HOME}/Desktop/Open Nectar"

printf 'branch: %s\n' "${branch}"
printf 'sha: %s\n' "${sha}"
printf 'path: %s\n' "${app}"
