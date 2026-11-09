#!/usr/bin/env bash
# test-build.sh [--self-test] [--save-self-test] [save-mode flags]
#
# Build the checkout that contains this script (any branch or worktree) as
# "Open Nectar Test.app" and point ~/Desktop/Open Nectar at that build.
#
# Test builds stay in ~/OpenNectar-test-builds.noindex (Spotlight skips it), each
# with its own bundle id (org.opennectar.testbuild.<hash>). The newest 5 builds
# are kept; data/ is never removed. By default every test build shares the
# central save under ~/Library/Application Support/Open Nectar/save.
#
# Run it from any directory:
#   /path/to/checkout/tools/mac/test-build.sh

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd -- "${script_dir}/../.." && pwd)"
resolve_save_sh="${script_dir}/resolve_save_for_build.sh"
save_mode_py="${script_dir}/save_mode.py"

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

# LaunchServices refuses a shell-script CFBundleExecutable (error -54, launchd
# "Codesigning issue"). The default entry is nectar-launcher itself, with the
# environment in Info.plist. Fresh saves need a new directory on every launch,
# so that mode compiles a Mach-O stub that creates one and execs the launcher.
compile_fresh_stub() {
    local file="$1" launcher_bin="$2" title="$3"
    local work src
    if ! command -v cc >/dev/null 2>&1; then
        printf 'cc is required to build the fresh-save launcher stub.\n' >&2
        return 1
    fi
    work="$(mktemp -d)"
    src="${work}/fresh-stub.c"
    python3 - "$src" "$launcher_bin" "$title" <<'PY'
import sys
path, launcher, title = sys.argv[1:]

def c_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'

program = r"""#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
static const char *launcher = __LAUNCHER__;
static const char *configured_title = __TITLE__;
int main(int argc, char **argv) {
    const char *tmp = getenv("TMPDIR");
    char tmpl[1024];
    char card[1024];
    if (!tmp || !*tmp) tmp = "/tmp";
    if (snprintf(tmpl, sizeof tmpl, "%s/open-nectar-fresh.XXXXXX", tmp) >= (int)sizeof tmpl) return 1;
    if (!mkdtemp(tmpl)) { perror("mkdtemp"); return 1; }
    if (snprintf(card, sizeof card, "%s/card0", tmpl) >= (int)sizeof card) return 1;
    if (mkdir(card, 0700) != 0) { perror("mkdir"); return 1; }
    setenv("NECTAR_SAVE_DIR", tmpl, 1);
    setenv("PIKMIN_LAUNCHER_TITLE", configured_title, 1);
    argv[0] = (char *)launcher;
    execv(launcher, argv);
    perror("execv");
    return 1;
}
"""
program = program.replace("__LAUNCHER__", c_string(launcher)).replace("__TITLE__", c_string(title))
open(path, "w", encoding="utf-8").write(program)
PY
    if ! cc -O2 -o "${file}" "${src}"; then
        rm -rf "${work}"
        printf 'cc could not build the fresh-save launcher stub.\n' >&2
        return 1
    fi
    rm -rf "${work}"
    chmod +x "${file}"
}

prepare_test_runtime() {
    local data_root="$1" config_home="$2" cache_home="$3"
    mkdir -p "${data_root}/save" "${config_home}/open-nectar" "${cache_home}"
    local real_assets="${HOME}/.local/share/pikmin-native/assets"
    if [[ ! -e "${data_root}/assets" && ! -L "${data_root}/assets" && -f "${real_assets}/.pikmin-assets" ]]; then
        ln -s "${real_assets}" "${data_root}/assets"
    fi
    local install_dir_file="${config_home}/open-nectar/install_dir"
    if [[ ! -f "${install_dir_file}" ]]; then
        printf '%s\n' "${data_root}" > "${install_dir_file}"
    fi
}

adhoc_sign_app() {
    local app="$1"
    [[ "$(uname -s)" == "Darwin" ]] || return 0
    if ! command -v codesign >/dev/null 2>&1; then
        printf 'codesign is required to sign %s\n' "${app}" >&2
        return 1
    fi
    codesign --force --deep --sign - "${app}"
}

write_test_app() {
    local app="$1"
    local data_home="$2"
    local config_home="$3"
    local cache_home="$4"
    local data_root="$5"
    local save_dir="$6"
    local game_bin="$7"
    local launcher_bin="$8"
    local sha="$9"
    local branch="${10}"
    local snapshot="${11:-${sha:0:12}}"
    local save_read_only="${12:-0}"
    local macos="${app}/Contents/MacOS"
    local resources="${app}/Contents/Resources"
    local title="Open Nectar Test — ${branch} [${snapshot}]"
    if [[ -n "${PIKMIN_SAVE_MODE_LABEL:-}" ]]; then
        title+=", ${PIKMIN_SAVE_MODE_LABEL}"
    fi
    local title_xml bundle_suffix executable="nectar-launcher"
    title_xml="$(xml_escape "${title}")"
    bundle_suffix="$(printf '%s\n%s' "${branch}" "${snapshot}" | shasum -a 256 | cut -c1-16)"
    mkdir -p "${macos}" "${resources}"
    cp "${save_mode_py}" "${resources}/save_mode.py"
    if [[ "${launcher_bin}" != "${macos}/nectar-launcher" ]]; then
        cp "${launcher_bin}" "${macos}/nectar-launcher"
    fi
    chmod +x "${macos}/nectar-launcher"
    if [[ "${PIKMIN_SAVE_MODE:-central}" == "fresh" ]]; then
        executable="open-nectar"
        compile_fresh_stub "${macos}/open-nectar" "${macos}/nectar-launcher" "${title}"
    fi
    local data_home_xml config_home_xml cache_home_xml data_root_xml game_bin_xml save_xml save_ro_xml="" mode_xml
    local sha_json branch_json build_info_xml
    sha_json="$(json_escape "${sha}")"
    branch_json="$(json_escape "${branch}")"
    data_home_xml="$(xml_escape "${data_home}")"
    config_home_xml="$(xml_escape "${config_home}")"
    cache_home_xml="$(xml_escape "${cache_home}")"
    data_root_xml="$(xml_escape "${data_root}")"
    game_bin_xml="$(xml_escape "${game_bin}")"
    save_xml="$(xml_escape "${save_dir}")"
    build_info_xml="$(xml_escape "${resources}/build-info.json")"
    mode_xml="$(xml_escape "${PIKMIN_SAVE_MODE:-central}")"
    if [[ "${save_read_only}" == "1" ]]; then
        save_ro_xml="
        <key>NECTAR_SAVE_READ_ONLY</key>
        <string>1</string>"
    fi
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
    <string>${executable}</string>
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
        <string>${save_xml}</string>${save_ro_xml}
        <key>PIKMIN_SAVE_MODE</key>
        <string>${mode_xml}</string>
        <key>PIKMIN_BUNDLE_MANAGED</key>
        <string>1</string>
        <key>PIKMIN_LAUNCHER_TITLE</key>
        <string>${title_xml}</string>
        <key>PIKMIN_BUILD_INFO</key>
        <string>${build_info_xml}</string>
    </dict>
</dict>
</plist>
EOF
    printf '{\n  "sha": "%s",\n  "branch": "%s"\n}\n' "${sha_json}" "${branch_json}" \
        > "${resources}/build-info.json"
    prepare_test_runtime "${data_root}" "${config_home}" "${cache_home}"
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
    central_save="${home}/Library/Application Support/Open Nectar/save"
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
    HOME="${home}"
    write_test_app "${app}" "${data_home}" "${config_home}" "${cache_home}" \
        "${data_root}" "${central_save}" "${tmp}/bin/nectar" "${fake}" \
        "0123456789abcdef" "fixture" "0123456789ab" "0"
    plist="${app}/Contents/Info.plist"
    entry="${app}/Contents/MacOS/nectar-launcher"
    [[ -x "${entry}" ]]
    [[ ! -e "${app}/Contents/MacOS/open-nectar" ]]
    grep -q '<key>CFBundleExecutable</key>' "${plist}"
    grep -A1 '<key>CFBundleExecutable</key>' "${plist}" | grep -q '<string>nectar-launcher</string>'
    grep -q "XDG_DATA_HOME" "${plist}"
    grep -F -q "${data_home}" "${plist}"
    grep -F -q "${data_root}" "${plist}"
    grep -F -q "${central_save}" "${plist}"
    grep -F -q "${tmp}/bin/nectar" "${plist}"
    grep -F -q 'Open Nectar Test — fixture [0123456789ab]' "${plist}"
    grep -q '<key>PIKMIN_BUNDLE_MANAGED</key>' "${plist}"
    if grep -q 'NECTAR_EXECUTABLE_PATH' "${plist}"; then
        printf 'test app plist sets NECTAR_EXECUTABLE_PATH\n' >&2
        return 1
    fi
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
    if command -v cc >/dev/null 2>&1; then
        local fresh_app="${root}/fresh/Open Nectar Test.app" fresh_entry
        PIKMIN_SAVE_MODE=fresh write_test_app "${fresh_app}" "${data_home}" "${config_home}" "${cache_home}" \
            "${data_root}" "${central_save}" "${tmp}/bin/nectar" "${fake}" \
            "0123456789abcdef" "fixture" "fresh-snapshot" "0"
        fresh_entry="${fresh_app}/Contents/MacOS/open-nectar"
        [[ -x "${fresh_entry}" ]]
        if head -c 2 "${fresh_entry}" | grep -q '#!'; then
            printf 'fresh-save entry is still a shell script\n' >&2
            return 1
        fi
        grep -A1 '<key>CFBundleExecutable</key>' "${fresh_app}/Contents/Info.plist" | grep -q '<string>open-nectar</string>'
    fi
    local other_app="${root}/other/Open Nectar Test.app" fixture_id other_id
    write_test_app "${other_app}" "${data_home}" "${config_home}" "${cache_home}" \
        "${data_root}" "${central_save}" "${tmp}/bin/nectar" "${fake}" "0123456789abcdef" "fixture" "another-snapshot" "0"
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

if [[ "${1:-}" == "--save-self-test" ]]; then
    bash "${repo}/games/pikmin1/packaging/macos/build-app.sh" --save-self-test
    exit $?
fi

if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    exit 0
fi

save_flag_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --fresh-save|--sandbox-save|--read-only-save|--choose-save)
            save_flag_args+=("$1")
            shift
            ;;
        --save-name|--save-dir)
            save_flag_args+=("$1" "$2")
            shift 2
            ;;
        *)
            printf 'usage: %s [--self-test | --save-self-test] [save-mode flags]\n' "${BASH_SOURCE[0]}" >&2
            exit 2
            ;;
    esac
done
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
# shellcheck disable=SC1090
eval "$(bash "${resolve_save_sh}" --home "${HOME}" --build-output "${dest}" --non-interactive -- ${save_flag_args[@]+"${save_flag_args[@]}"})"
data_home="${root}/data"
config_home="${data_home}/config"
cache_home="${data_home}/cache"
data_root="${data_home}/pikmin-native"
configured_save_dir="${NECTAR_SAVE_DIR}"
save_mode_label="${PIKMIN_SAVE_MODE_LABEL}"
save_read_only="${NECTAR_SAVE_READ_ONLY:-0}"
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
    "${data_root}" "${configured_save_dir}" "${game_bin}" "${launcher_bin}" "${sha}" "${identity}" "${snapshot}" "${save_read_only}"
python3 "${script_dir}/build_identity.py" --repo "${repo}" \
    --output "${app}/Contents/Resources/build-info.json" --game "${game_bin}" \
    --launcher "${launcher_bin}" --cache "${build_dir}/CMakeCache.txt" --data "${data_root}" --role feature \
    --save-mode "${PIKMIN_SAVE_MODE}" --save-mode-label "${save_mode_label}" \
    --save-directory "${configured_save_dir}"
if [[ "${PIKMIN_SAVE_MODE:-central}" == "central" && "${save_read_only}" != "1" ]]; then
    python3 "${save_mode_py}" backup-if-needed --home "${HOME}" \
        --build-info "${app}/Contents/Resources/build-info.json" >/dev/null 2>&1 || true
fi
adhoc_sign_app "${app}"
finish_test_build "${root}" "${dest}" "${app}" "${HOME}/Desktop/Open Nectar"

printf 'branch: %s\n' "${branch}"
printf 'sha: %s\n' "${sha}"
printf 'path: %s\n' "${app}"
