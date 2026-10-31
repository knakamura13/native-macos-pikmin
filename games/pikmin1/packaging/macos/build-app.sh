#!/usr/bin/env bash
# build-app.sh [--clean] [--build-dir DIR] [--output-dir DIR] [--install [DIR]]
#              [--fresh-save | --sandbox-save | --read-only-save | --save-name NAME | --save-dir PATH | --choose-save]
# build-app.sh --self-test
# build-app.sh --save-self-test
#
# Builds the Open Nectar app for macOS (arm64, USA build). The launcher inside
# plays both Pikmin 1 and Pikmin 2, so every app is named after Open Nectar.
# The role picks the name, bundle id and data folder:
#
#   role      app                       bundle id               data folder
#   upstream  Open Nectar.app           org.opennectar.upstream ~/.local/share/pikmin-native-upstream
#   develop   Open Nectar Develop.app   org.opennectar.develop  ~/.local/share/pikmin-native
#   unknown   Open Nectar Local.app     org.opennectar.local    ~/.local/share/pikmin-native
#
# The role comes from PIKMIN_BUILD_ROLE. A kyle-develop rebuild (its last commit
# is the kyle-develop-rebuild-v1 marker) is develop. tools/mac/build-upstream.sh
# builds the upstream role. Test builds come from tools/mac/test-build.sh.
#
# The app is built in packaging/macos/out.noindex/ (".noindex" keeps Spotlight
# from listing that copy). A default build replaces the app already there; a
# custom --output-dir refuses to replace one.
#
# --install puts the app in DIR (default: /Applications), for the upstream and
# develop roles only. It:
#   - exits 75 without changing anything while the game or launcher is running;
#   - refuses to overwrite an app whose bundle id or recorded role differs;
#   - moves the previous app to ~/OpenNectar-archive.noindex/app-backups/<role>/
#     (PIKMIN_BACKUP_ROOT overrides the app-backups folder; the last 5 are kept);
#   - checks the installed copy, and puts the previous app back if that fails;
#   - for develop, then moves a legacy DIR/Pikmin 1.app (bundle id
#     org.opennectar.pikmin1) to ~/OpenNectar-archive.noindex/legacy/, which is
#     never pruned. Saves are never touched.
#   - merges controls.conf (the Mac key layout) into the role's data folder.
#
# The upstream role never uses the real *data* folder for settings (see
# refuse_real_data_root). Every role can share the central save folder under
# ~/Library/Application Support/Open Nectar/save via NECTAR_SAVE_DIR.
#
# The game links SDL2 from Homebrew (sdl2-compat). The app runs only on Macs
# that have that library at the same path.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
game_dir="$(cd -- "${script_dir}/../.." && pwd)"
repo_root="$(cd -- "${game_dir}/../.." && pwd)"
save_mode_py="${repo_root}/tools/mac/save_mode.py"
resolve_save_sh="${repo_root}/tools/mac/resolve_save_for_build.sh"
legacy_app_name="Pikmin 1.app"
legacy_bundle_id="org.opennectar.pikmin1"
keep_backups=5

# --- helpers (also used by --self-test) -------------------------------------

real_data_root() {
    printf '%s\n' "${HOME}/.local/share/pikmin-native"
}

# True when the two paths are the same folder or one contains the other,
# after resolving symlinks (also through parts that do not exist yet).
paths_overlap() {
    python3 - "$1" "$2" <<'PY'
import os, sys
a, b = (os.path.realpath(os.path.expanduser(p)) for p in sys.argv[1:3])
inside = lambda x, y: x == y or x.startswith(y.rstrip(os.sep) + os.sep)
sys.exit(0 if inside(a, b) or inside(b, a) else 1)
PY
}

refuse_real_data_root() {
    local candidate="$1" real
    real="$(real_data_root)"
    if paths_overlap "${candidate}" "${real}"; then
        printf 'Refusing to use %s for an upstream build: it overlaps the real data folder %s.\n' \
            "${candidate}" "${real}" >&2
        return 1
    fi
    if [[ -n "${XDG_DATA_HOME:-}" ]] && paths_overlap "${candidate}" "${XDG_DATA_HOME}/pikmin-native"; then
        printf 'Refusing to use %s for an upstream build: it overlaps %s.\n' \
            "${candidate}" "${XDG_DATA_HOME}/pikmin-native" >&2
        return 1
    fi
}

hash_save_tree() {
    python3 "${save_mode_py}" hash-tree "$1"
}

assert_central_save_unchanged() {
    local central="$1" label="$2" before after
    shift 2
    [[ -d "${central}" ]] || { "$@"; return $?; }
    before="$(hash_save_tree "${central}")"
    if ! "$@"; then
        return 1
    fi
    after="$(hash_save_tree "${central}")"
    if [[ "${before}" != "${after}" ]]; then
        printf 'Refusing %s: the central save folder changed under %s.\n' "${label}" "${central}" >&2
        return 1
    fi
}

game_running() {
    pgrep -x "nectar" >/dev/null 2>&1 || pgrep -x "nectar-launcher" >/dev/null 2>&1 \
        || pgrep -x "pikmin2" >/dev/null 2>&1
}

plist_value() {
    local plist="$1" key="$2"
    [[ -f "${plist}" ]] || return 0
    if command -v plutil >/dev/null 2>&1; then
        plutil -extract "${key}" raw -o - "${plist}" 2>/dev/null && return 0
    fi
    sed -n "/<key>${key}<\\/key>/{n;s:.*<string>\\(.*\\)</string>.*:\\1:p;q;}" "${plist}"
}

manifest_value() {
    local manifest="$1" key="$2"
    [[ -f "${manifest}" ]] || return 0
    python3 - "${manifest}" "${key}" <<'PY' 2>/dev/null || true
import json, sys
value = json.load(open(sys.argv[1])).get(sys.argv[2])
print('' if value is None else value)
PY
}

# Refuses to replace an app that belongs to another role.
check_replaceable() {
    local installed="$1" want_id="$2" want_role="$3" id role
    id="$(plist_value "${installed}/Contents/Info.plist" CFBundleIdentifier)"
    if [[ "${id}" != "${want_id}" ]]; then
        printf 'Refusing to replace %s: its bundle id is "%s", not %s.\n' "${installed}" "${id}" "${want_id}" >&2
        return 1
    fi
    role="$(manifest_value "${installed}/Contents/Resources/build-info.json" role)"
    if [[ -n "${role}" && "${role}" != "${want_role}" ]]; then
        printf 'Refusing to replace %s: it was built as "%s", not %s.\n' "${installed}" "${role}" "${want_role}" >&2
        return 1
    fi
}

verify_installed() {
    local installed="$1" source_app="$2" want_id="$3" id file
    id="$(plist_value "${installed}/Contents/Info.plist" CFBundleIdentifier)"
    [[ "${id}" == "${want_id}" ]] || { printf 'Installed app has bundle id "%s".\n' "${id}" >&2; return 1; }
    for file in Contents/MacOS/nectar Contents/MacOS/nectar-launcher Contents/MacOS/pikmin2_pc \
        Contents/MacOS/pikmin2_pc-usa Contents/Resources/build-info.json; do
        cmp -s "${source_app}/${file}" "${installed}/${file}" \
            || { printf 'Installed app differs from the build at %s.\n' "${file}" >&2; return 1; }
    done
}

prune_backups() {
    local dir="$1" keep="$2" old
    [[ -d "${dir}" ]] || return 0
    # Backup folders are named by UTC time, so the name order is the age order.
    while IFS= read -r old; do
        if [[ -n "${old}" ]]; then rm -rf -- "${old}"; fi
    done < <(find "${dir}" -mindepth 1 -maxdepth 1 -type d -name '20*' | LC_ALL=C sort -r | tail -n "+$((keep + 1))")
}

# Moves a legacy Pikmin 1.app out of the install folder, only if it is ours.
retire_legacy_app() {
    local install_dir="$1" archive_root="$2" legacy id dest
    legacy="${install_dir}/${legacy_app_name}"
    [[ -e "${legacy}" || -L "${legacy}" ]] || return 0
    id="$(plist_value "${legacy}/Contents/Info.plist" CFBundleIdentifier)"
    if [[ "${id}" != "${legacy_bundle_id}" ]]; then
        printf 'Leaving %s in place: its bundle id is "%s", not %s.\n' "${legacy}" "${id}" "${legacy_bundle_id}" >&2
        return 0
    fi
    mkdir -p "${archive_root}/legacy" || return 1
    dest="$(mktemp -d "${archive_root}/legacy/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")" || return 1
    mv "${legacy}" "${dest}/${legacy_app_name}" || { rmdir "${dest}" 2>/dev/null; return 1; }
    echo "Moved the old ${legacy_app_name} to ${dest}/ (kept; delete it yourself once Develop works)."
}

# install_app APP INSTALL_DIR APP_NAME BUNDLE_ID ROLE ARCHIVE_ROOT BACKUP_ROOT
install_app() {
    local app="$1" install_dir="$2" app_name="$3" want_id="$4" role="$5" archive_root="$6" backup_root="$7"
    local installed="${install_dir}/${app_name}" staging backup=""
    # Refuse rather than kill: quitting a running game would lose unsaved progress.
    if game_running; then
        echo "Open Nectar is running; not installing now. Quit it, then rerun." >&2
        return 75
    fi
    if [[ -e "${installed}" || -L "${installed}" ]]; then
        check_replaceable "${installed}" "${want_id}" "${role}" || return 1
    fi
    # Every step checks its own result: callers run this in a condition, where
    # set -e does not apply.
    mkdir -p "${install_dir}" || return 1
    staging="${install_dir}/.${app_name%.app}.installing"
    rm -rf -- "${staging}"
    cp -R "${app}" "${staging}" || { rm -rf -- "${staging}"; return 1; }
    if [[ -e "${installed}" || -L "${installed}" ]]; then
        mkdir -p "${backup_root}/${role}" || { rm -rf -- "${staging}"; return 1; }
        backup="$(mktemp -d "${backup_root}/${role}/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")" \
            || { rm -rf -- "${staging}"; return 1; }
        if ! mv "${installed}" "${backup}/${app_name}"; then
            rm -rf -- "${staging}"
            rmdir "${backup}" 2>/dev/null || true
            return 1
        fi
    fi
    if [[ -e "${installed}" ]] || ! mv "${staging}" "${installed}"; then
        rm -rf -- "${staging}"
        [[ -n "${backup}" ]] && mv "${backup}/${app_name}" "${installed}"
        echo "Could not move the new app into place; previous app kept." >&2
        return 1
    fi
    if ! verify_installed "${installed}" "${app}" "${want_id}"; then
        rm -rf -- "${installed}"
        if [[ -n "${backup}" ]]; then
            mv "${backup}/${app_name}" "${installed}"
            echo "Install check failed; put the previous app back." >&2
        fi
        return 1
    fi
    echo "Installed: ${installed}"
    if [[ -n "${backup}" ]]; then echo "Previous app: ${backup}/${app_name}"; fi
    prune_backups "${backup_root}/${role}" "${keep_backups}"
    if [[ "${role}" == "develop" ]]; then
        retire_legacy_app "${install_dir}" "${archive_root}"
    fi
    return 0
}

# Moves apps left in the pre-.noindex output folder (out/) where Spotlight
# would list them, then removes out/ if nothing else is in it.
retire_stale_output() {
    local old_out="${script_dir}/out" name moved=""
    [[ -d "${old_out}" ]] || return 0
    for name in "Pikmin 1.app" "Open Nectar.app" "Open Nectar Develop.app" "Open Nectar Local.app"; do
        if [[ -e "${old_out}/${name}" || -L "${old_out}/${name}" ]]; then
            if [[ -z "${moved}" ]]; then
                moved="${script_dir}/out.noindex/stale-out/$(date -u +%Y%m%dT%H%M%SZ)"
                mkdir -p "${moved}"
            fi
            mv "${old_out}/${name}" "${moved}/"
        fi
    done
    if [[ -n "${moved}" ]]; then echo "Moved old out/ apps to ${moved}/"; fi
    rmdir "${old_out}" 2>/dev/null || true
}

# Merges controls.conf into the device settings file in DATA_ROOT. The game
# reads pikmin_settings.conf from the data folder the launcher uses. Only the
# keys in controls.conf change. Other settings are kept.
merge_controls() {
    local data_root="$1" role="$2"
    local controls="${script_dir}/controls.conf"
    local settings="${data_root}/pikmin_settings.conf"
    if [[ "${role}" == "upstream" ]]; then
        refuse_real_data_root "${data_root}" || return 1
    fi
    mkdir -p "${data_root}"
    if [[ ! -f "${settings}" ]]; then
        cp "${controls}" "${settings}"
    else
        local merged
        merged="$(mktemp)"
        awk -F' *= *' '
            NR == FNR {
                if ($0 ~ /^[A-Za-z0-9_]+ *=/) { want[$1] = $0; order[++n] = $1 }
                next
            }
            {
                if ($1 in want) {
                    if (!($1 in done)) print want[$1]
                    done[$1] = 1
                } else print
            }
            END {
                for (i = 1; i <= n; i++) if (!(order[i] in done)) print want[order[i]]
            }
        ' "${controls}" "${settings}" > "${merged}"
        cat "${merged}" > "${settings}"
        rm -f "${merged}"
    fi
    echo "Controls: ${settings}"
}

xml_escape() {
    local value=$1
    value=${value//&/&amp;}
    value=${value//</&lt;}
    value=${value//>/&gt;}
    printf '%s' "${value}"
}

write_fresh_wrapper() {
    local file="$1" launcher_bin="$2" title="$3"
    local q_launcher q_title
    q_launcher="$(printf '%q' "${launcher_bin}")"
    q_title="$(printf '%q' "${title}")"
    cat > "${file}" <<EOF
#!/bin/bash
set -euo pipefail
fresh="\$(mktemp -d "\${TMPDIR:-/tmp}/open-nectar-fresh.XXXXXX")"
mkdir -p "\$fresh/card0"
export NECTAR_SAVE_DIR="\$fresh"
export PIKMIN_LAUNCHER_TITLE=${q_title}
exec ${q_launcher} "\$@"
EOF
    chmod +x "${file}"
}

# write_info_plist FILE DISPLAY_NAME BUNDLE_ID TITLE DATA_ROOT GAME_APP ROLE SAVE_DIR SAVE_READ_ONLY EXECUTABLE
write_info_plist() {
    local file="$1" display="$2" id="$3" title="$4" data="$5" game_app="$6" role="$7" save="$8" save_ro="$9" executable="${10}"
    local data_xml game_app_xml save_xml upstream_env="" save_ro_xml=""
    data_xml="$(xml_escape "${data}")"
    game_app_xml="$(xml_escape "${game_app}")"
    save_xml="$(xml_escape "${save}")"
    if [[ "${save_ro}" == "1" ]]; then
        save_ro_xml="<key>NECTAR_SAVE_READ_ONLY</key><string>1</string>"
    fi
    if [[ "${role}" == "upstream" ]]; then
        # Point every per-user folder at the upstream data folder, so neither game
        # nor the launcher reads or writes the real saves, settings or Pikmin 2 path.
        upstream_env="<key>XDG_DATA_HOME</key><string>${data_xml}/xdg-data</string>
        <key>XDG_CONFIG_HOME</key><string>${data_xml}/config</string>
        <key>XDG_CACHE_HOME</key><string>${data_xml}/cache</string>"
    fi
    cat > "${file}" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>$(xml_escape "${display}")</string>
    <key>CFBundleDisplayName</key>
    <string>$(xml_escape "${display}")</string>
    <key>CFBundleIdentifier</key>
    <string>${id}</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleExecutable</key>
    <string>$(xml_escape "${executable}")</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>LSMinimumSystemVersion</key>
    <string>13.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSEnvironment</key>
    <dict>
        <key>PIKMIN_BUNDLE_MANAGED</key>
        <string>1</string>
        <key>PIKMIN_LAUNCHER_TITLE</key>
        <string>$(xml_escape "${title}")</string>
        <key>NECTAR_PIKMIN1_DIR</key>
        <string>${data_xml}</string>
        <key>NECTAR_PIKMIN1_EXECUTABLE</key>
        <string>${game_app_xml}/Contents/MacOS/nectar</string>
        <key>NECTAR_SAVE_DIR</key><string>${save_xml}</string>
        ${save_ro_xml}
        ${upstream_env}
    </dict>
</dict>
</plist>
PLIST
}

# --- self-test ----------------------------------------------------------------

# The checks are strings run by eval, and the stubs are called indirectly.
# shellcheck disable=SC2016,SC2329
self_test() {
    local tmp fail=0
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp}"' RETURN
    check() { if ! eval "$2"; then printf 'FAIL: %s\n' "$1" >&2; fail=1; fi; }
    fake_app() { # DIR NAME ID ROLE CONTENT
        mkdir -p "$1/$2/Contents/MacOS" "$1/$2/Contents/Resources"
        printf '%s' "$5" > "$1/$2/Contents/MacOS/nectar"
        printf '%s' "$5" > "$1/$2/Contents/MacOS/nectar-launcher"
        printf '%s' "$5" > "$1/$2/Contents/MacOS/pikmin2_pc"
        printf '%s' "$5" > "$1/$2/Contents/MacOS/pikmin2_pc-usa"
        printf '{"role": "%s", "sha": "%s"}\n' "$4" "$5" > "$1/$2/Contents/Resources/build-info.json"
        write_info_plist "$1/$2/Contents/Info.plist" "${2%.app}" "$3" "t" "/d" "/a" "$4" "/d/save" "0" "nectar-launcher"
    }
    local home="${tmp}/home" apps="${tmp}/Applications" out="${tmp}/out" archive backups
    mkdir -p "${home}" "${apps}" "${out}"
    archive="${home}/OpenNectar-archive.noindex"
    backups="${archive}/app-backups"
    game_running() { return 1; }

    # Data folder guard, symlinks included.
    HOME="${home}"
    mkdir -p "${home}/.local/share/pikmin-native/save" "${home}/elsewhere"
    ln -s "${home}/.local/share/pikmin-native" "${home}/elsewhere/link"
    check "real folder refused" '! refuse_real_data_root "${home}/.local/share/pikmin-native" 2>/dev/null'
    check "subfolder refused" '! refuse_real_data_root "${home}/.local/share/pikmin-native/up" 2>/dev/null'
    check "parent refused" '! refuse_real_data_root "${home}/.local/share" 2>/dev/null'
    check "symlink refused" '! refuse_real_data_root "${home}/elsewhere/link/x" 2>/dev/null'
    check "upstream folder allowed" 'refuse_real_data_root "${home}/.local/share/pikmin-native-upstream"'
    check "merge_controls refuses real folder" '! merge_controls "${home}/elsewhere/link" upstream 2>/dev/null'
    check "merge_controls left real folder alone" '[[ ! -e "${home}/.local/share/pikmin-native/pikmin_settings.conf" ]]'

    # Fresh install, replace with backup, refusal on mismatch.
    fake_app "${out}" "Open Nectar Develop.app" org.opennectar.develop develop one
    check "fresh install" 'install_app "${out}/Open Nectar Develop.app" "${apps}" "Open Nectar Develop.app" org.opennectar.develop develop "${archive}" "${backups}" >/dev/null'
    rm -rf "${out:?}/Open Nectar Develop.app"
    fake_app "${out}" "Open Nectar Develop.app" org.opennectar.develop develop two
    check "replace" 'install_app "${out}/Open Nectar Develop.app" "${apps}" "Open Nectar Develop.app" org.opennectar.develop develop "${archive}" "${backups}" >/dev/null'
    check "new copy installed" '[[ "$(cat "${apps}/Open Nectar Develop.app/Contents/MacOS/nectar")" == two ]]'
    check "previous copy backed up" '[[ "$(cat "${backups}"/develop/*/"Open Nectar Develop.app/Contents/MacOS/nectar")" == one ]]'
    check "no staging left" '[[ ! -e "${apps}/.Open Nectar Develop.installing" ]]'

    fake_app "${out}" "Open Nectar.app" org.opennectar.upstream upstream up
    fake_app "${apps}" "Open Nectar.app" org.opennectar.develop develop wrong
    check "id mismatch refused" '! install_app "${out}/Open Nectar.app" "${apps}" "Open Nectar.app" org.opennectar.upstream upstream "${archive}" "${backups}" 2>/dev/null'
    check "mismatched app untouched" '[[ "$(cat "${apps}/Open Nectar.app/Contents/MacOS/nectar")" == wrong ]]'
    rm -rf "${apps:?}/Open Nectar.app"
    fake_app "${apps}" "Open Nectar.app" org.opennectar.upstream develop wrong
    check "role mismatch refused" '! install_app "${out}/Open Nectar.app" "${apps}" "Open Nectar.app" org.opennectar.upstream upstream "${archive}" "${backups}" 2>/dev/null'
    rm -rf "${apps:?}/Open Nectar.app"

    # Running game defers with 75 and changes nothing.
    game_running() { return 0; }
    local rc=0
    install_app "${out}/Open Nectar.app" "${apps}" "Open Nectar.app" org.opennectar.upstream upstream "${archive}" "${backups}" 2>/dev/null || rc=$?
    check "running game defers" '[[ "${rc}" -eq 75 && ! -e "${apps}/Open Nectar.app" ]]'
    game_running() { return 1; }

    # Legacy app: moved only after Develop is verified, and only if it is ours.
    fake_app "${apps}" "Pikmin 1.app" org.opennectar.pikmin1 develop legacy
    mkdir -p "${home}/.local/share/pikmin-native/save/card0"
    printf 'save' > "${home}/.local/share/pikmin-native/save/card0/file.gci"
    rm -rf "${out:?}/Open Nectar Develop.app"
    fake_app "${out}" "Open Nectar Develop.app" org.opennectar.develop develop three
    check "develop install retires legacy" 'install_app "${out}/Open Nectar Develop.app" "${apps}" "Open Nectar Develop.app" org.opennectar.develop develop "${archive}" "${backups}" >/dev/null'
    check "legacy gone from Applications" '[[ ! -e "${apps}/Pikmin 1.app" ]]'
    check "legacy kept in archive" '[[ "$(cat "${archive}"/legacy/*/"Pikmin 1.app/Contents/MacOS/nectar")" == legacy ]]'
    check "saves untouched" '[[ "$(cat "${home}/.local/share/pikmin-native/save/card0/file.gci")" == save ]]'
    fake_app "${apps}" "Pikmin 1.app" com.example.other develop foreign
    retire_legacy_app "${apps}" "${archive}" 2>/dev/null
    check "foreign Pikmin 1.app left alone" '[[ -e "${apps}/Pikmin 1.app" ]]'
    rm -rf "${apps:?}/Pikmin 1.app"

    # A failed check puts the previous app back.
    verify_installed() { return 1; }
    rm -rf "${out:?}/Open Nectar Develop.app"
    fake_app "${out}" "Open Nectar Develop.app" org.opennectar.develop develop four
    check "failed verify fails" '! install_app "${out}/Open Nectar Develop.app" "${apps}" "Open Nectar Develop.app" org.opennectar.develop develop "${archive}" "${backups}" >/dev/null 2>&1'
    check "previous app restored" '[[ "$(cat "${apps}/Open Nectar Develop.app/Contents/MacOS/nectar")" == three ]]'
    unset -f verify_installed

    # Backups keep the newest five.
    mkdir -p "${tmp}/prune"
    local n
    for n in 01 02 03 04 05 06 07; do mkdir -p "${tmp}/prune/202601${n}T000000Z-abcdef"; done
    prune_backups "${tmp}/prune" 5
    check "prune keeps five" '[[ "$(find "${tmp}/prune" -mindepth 1 -maxdepth 1 | wc -l)" -eq 5 && ! -e "${tmp}/prune/20260102T000000Z-abcdef" && -e "${tmp}/prune/20260103T000000Z-abcdef" ]]'

    # Stale out/ apps move into out.noindex.
    local saved_script_dir="${script_dir}"
    script_dir="${tmp}/pkg"
    mkdir -p "${script_dir}/out/Pikmin 1.app" "${script_dir}/out/Open Nectar.app"
    retire_stale_output >/dev/null
    check "stale out/ removed" '[[ ! -e "${script_dir}/out" ]]'
    check "stale apps kept in out.noindex" '[[ -d "$(echo "${script_dir}"/out.noindex/stale-out/*/"Pikmin 1.app")" ]]'
    script_dir="${saved_script_dir}"

    # Central save migration copies legacy folders and leaves sources in place.
    local mig_home="${tmp}/mig-home"
    mkdir -p "${mig_home}/.local/share/pikmin-native/save/card0" \
        "${mig_home}/.local/share/pikmin-native-upstream/save/card0"
    printf 'legacy1' > "${mig_home}/.local/share/pikmin-native/save/card0/a.gci"
    printf 'legacy2' > "${mig_home}/.local/share/pikmin-native-upstream/save/card0/b.gci"
    HOME="${mig_home}" python3 "${save_mode_py}" migrate >/dev/null
    check "migration copied legacy saves" '[[ "$(cat "${mig_home}/Library/Application Support/Open Nectar/save/card0/a.gci")" == legacy1 ]]'
    check "migration left legacy folder" '[[ "$(cat "${mig_home}/.local/share/pikmin-native/save/card0/a.gci")" == legacy1 ]]'

    if [[ "${fail}" -ne 0 ]]; then return 1; fi
    echo "build-app.sh self-test: ok"
}

save_self_test() {
    local tmp fail=0
    tmp="$(mktemp -d)"
    trap 'rm -rf "${tmp}"' RETURN
    check() { if ! eval "$2"; then printf 'FAIL: %s\n' "$1" >&2; fail=1; fi; }
    local home="${tmp}/home"
    mkdir -p "${home}/Library/Application Support/Open Nectar/save/card0"
    printf 'probe' > "${home}/Library/Application Support/Open Nectar/save/card0/probe.gci"
    HOME="${home}"
    local before after
    before="$(hash_save_tree "${home}/Library/Application Support/Open Nectar/save")"
    merge_controls() { mkdir -p "$1"; :; return 0; }
    assert_central_save_unchanged "${home}/Library/Application Support/Open Nectar/save" "merge_controls" \
        merge_controls "${home}/.local/share/pikmin-native-upstream" upstream
    after="$(hash_save_tree "${home}/Library/Application Support/Open Nectar/save")"
    check "hash guard blocked mutation" '[[ "${before}" == "${after}" ]]'
    if [[ "${fail}" -ne 0 ]]; then return 1; fi
    echo "build-app.sh save-self-test: ok"
}

if [[ "${1:-}" == "--save-self-test" ]]; then
    save_self_test
    exit $?
fi

if [[ "${1:-}" == "--self-test" ]]; then
    self_test
    exit $?
fi

# --- role ---------------------------------------------------------------------

build_role="${PIKMIN_BUILD_ROLE:-unknown}"
marker_subject="$(git -C "${game_dir}" log -1 --format=%s 2>/dev/null || true)"
if [[ "${build_role}" == "develop" && "${marker_subject}" != "kyle-develop-rebuild-v1" ]]; then
    printf 'Refusing to label a build Develop without a develop rebuild marker.\n' >&2
    exit 1
fi
if [[ "${build_role}" == "unknown" && "${marker_subject}" == "kyle-develop-rebuild-v1" ]]; then
    build_role="develop"
fi
case "${build_role}" in
    upstream)
        app_name="Open Nectar.app"; display_name="Open Nectar"
        bundle_id="org.opennectar.upstream"; launcher_title="Open Nectar — Upstream"
        configured_data_root="${PIKMIN_DATA_ROOT:-${HOME}/.local/share/pikmin-native-upstream}"
        refuse_real_data_root "${configured_data_root}" || exit 1
        ;;
    develop)
        app_name="Open Nectar Develop.app"; display_name="Open Nectar Develop"
        bundle_id="org.opennectar.develop"; launcher_title="Open Nectar — Develop"
        configured_data_root="${PIKMIN_DATA_ROOT:-$(real_data_root)}"
        ;;
    unknown)
        app_name="Open Nectar Local.app"; display_name="Open Nectar Local"
        bundle_id="org.opennectar.local"; launcher_title="Open Nectar — Local build"
        configured_data_root="${PIKMIN_DATA_ROOT:-$(real_data_root)}"
        ;;
    *) printf 'Unknown PIKMIN_BUILD_ROLE: %s\n' "${build_role}" >&2; exit 2 ;;
esac

build_dir="${game_dir}/build-macos"
output_dir="${script_dir}/out.noindex"
icon_src="${game_dir}/packaging/icon/open_nectar.png"
archive_root="${PIKMIN_ARCHIVE_ROOT:-${HOME}/OpenNectar-archive.noindex}"
backup_root="${PIKMIN_BACKUP_ROOT:-${archive_root}/app-backups}"

clean=0
install=0
custom_output=0
install_dir="/Applications"
save_flag_args=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean) clean=1; shift ;;
        --build-dir) build_dir="$2"; shift 2 ;;
        --output-dir) output_dir="$2"; custom_output=1; shift 2 ;;
        --install)
            install=1
            # Optional directory argument: take it only if it is not another flag.
            if [[ $# -gt 1 && "$2" != --* ]]; then
                install_dir="$2"
                shift
            fi
            shift
            ;;
        --fresh-save|--sandbox-save|--read-only-save|--choose-save)
            save_flag_args+=("$1")
            shift
            ;;
        --save-name|--save-dir)
            save_flag_args+=("$1" "$2")
            shift 2
            ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

app="${output_dir}/${app_name}"
cache_for_save="${build_dir}/CMakeCache.txt"
resolve_args=(--home "${HOME}" --build-output "${output_dir}" --non-interactive)
[[ -f "${cache_for_save}" ]] && resolve_args+=(--cmake-cache "${cache_for_save}")
# shellcheck disable=SC1090
eval "$(bash "${resolve_save_sh}" "${resolve_args[@]}" -- ${save_flag_args[@]+"${save_flag_args[@]}"})"
configured_save_dir="${NECTAR_SAVE_DIR}"
save_read_only="${NECTAR_SAVE_READ_ONLY:-0}"
bundle_executable="nectar-launcher"
if [[ "${PIKMIN_SAVE_NEEDS_FRESH_WRAPPER:-0}" == "1" ]]; then
    bundle_executable="open-nectar"
fi
launcher_title="${launcher_title}, ${PIKMIN_SAVE_MODE_LABEL}"
if [[ "${install}" -eq 1 && "${build_role}" == "unknown" ]]; then
    printf '%s\n' 'Refusing --install for a build with no role. /Applications holds only Open Nectar (tools/mac/build-upstream.sh --install) and Open Nectar Develop (a kyle-develop rebuild). Use tools/mac/test-build.sh for anything else.' >&2
    exit 2
fi
if [[ "${install}" -eq 1 ]] && python3 -c 'import os,sys; sys.exit(0 if os.path.realpath(sys.argv[1]) == os.path.realpath(sys.argv[2]) else 1)' "${app}" "${install_dir}/${app_name}"; then
    printf '%s\n' 'Output and installation apps must be different paths.' >&2
    exit 2
fi
if [[ "${custom_output}" -eq 1 && ( -e "${app}" || -L "${app}" ) ]]; then
    printf 'Refusing to replace an existing output app: %s\n' "${app}" >&2
    exit 1
fi
if [[ "${install}" -eq 1 && -e "${install_dir}/${app_name}" ]]; then
    # Fail before the build, not after it.
    check_replaceable "${install_dir}/${app_name}" "${bundle_id}" "${build_role}" || exit 1
fi
if [[ "${clean}" -eq 1 ]]; then
    if [[ "${build_dir}" != "${game_dir}/build-macos" ]]; then
        printf '%s\n' '--clean is limited to the default build directory.' >&2
        exit 2
    fi
    rm -rf "${build_dir}"
fi
retire_stale_output

# ccache makes a --clean rebuild fast. Its cache lives outside build-macos, so it
# survives --clean.
cache_flags=()
if command -v ccache >/dev/null 2>&1; then
    cache_flags+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

cmake -S "${game_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release ${cache_flags[@]+"${cache_flags[@]}"}
cmake --build "${build_dir}" --target pikmin_pc pikmin_launcher -j "$(sysctl -n hw.ncpu)"

# Pikmin 2 in both regions, as upstream scripts/release-macos.sh ships it. The
# launcher installs Pikmin 2 by copying pikmin2_pc (Europe disc) or
# pikmin2_pc-usa (USA disc) from beside nectar-launcher; without them the
# install fails with "The package is incomplete" (fork issue #40).
p2_dir="${game_dir}/../pikmin2"
cmake -S "${p2_dir}" -B "${build_dir}/p2" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    ${cache_flags[@]+"${cache_flags[@]}"}
cmake --build "${build_dir}/p2" --target pikmin2_pc -j "$(sysctl -n hw.ncpu)"
cmake -S "${p2_dir}" -B "${build_dir}/p2-usa" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DPIKMIN_GAME_VERSION=VERSION_GPVE01 ${cache_flags[@]+"${cache_flags[@]}"}
cmake --build "${build_dir}/p2-usa" --target pikmin2_pc -j "$(sysctl -n hw.ncpu)"

bin_dir="${build_dir}/bin"
contents="${app}/Contents"

if [[ -e "${app}" || -L "${app}" ]]; then
    if [[ "${custom_output}" -eq 1 ]]; then
        printf 'Refusing to replace an existing output app: %s\n' "${app}" >&2
        exit 1
    fi
    # The default output folder is scratch space: its app is replaced.
    rm -rf "${app}"
fi
mkdir -p "${contents}/MacOS" "${contents}/Resources"

cp "${save_mode_py}" "${contents}/Resources/save_mode.py"

# The binaries keep the upstream names (nectar, nectar-launcher). Only the app
# bundle is named, so the code and the install format stay unchanged.
cp "${bin_dir}/nectar" "${contents}/MacOS/nectar"
cp "${bin_dir}/nectar-launcher" "${contents}/MacOS/nectar-launcher"
cp "${build_dir}/p2/pikmin2_pc" "${contents}/MacOS/pikmin2_pc"
cp "${build_dir}/p2-usa/pikmin2_pc" "${contents}/MacOS/pikmin2_pc-usa"

# App icon: build an .icns from the 256px PNG. iconutil needs the full size set.
iconset="$(mktemp -d)/AppIcon.iconset"
mkdir -p "${iconset}"
for size in 16 32 128 256; do
    sips -z "${size}" "${size}" "${icon_src}" --out "${iconset}/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z "${double}" "${double}" "${icon_src}" --out "${iconset}/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "${iconset}" -o "${contents}/Resources/AppIcon.icns"

# The launcher only looks for game files next to its own executable, so point
# it at the data folder and at the game binary inside the app it will run from.
# Info.plist values are literal, so the home path is fixed at build time.
game_app="${app}"
if [[ "${install}" -eq 1 ]]; then
    game_app="${install_dir}/${app_name}"
fi
write_info_plist "${contents}/Info.plist" "${display_name}" "${bundle_id}" "${launcher_title}" \
    "${configured_data_root}" "${game_app}" "${build_role}" "${configured_save_dir}" "${save_read_only}" "${bundle_executable}"
if [[ "${bundle_executable}" == "open-nectar" ]]; then
    write_fresh_wrapper "${contents}/MacOS/open-nectar" "${contents}/MacOS/nectar-launcher" "${launcher_title}"
fi

overlay_args=()
if [[ -n "${PIKMIN_LAUNCHER_OVERLAY:-}" ]]; then
    overlay_args+=(--launcher-overlay "${PIKMIN_LAUNCHER_OVERLAY}")
fi
if [[ -n "${PIKMIN_WINDOW_OVERLAY:-}" ]]; then
    overlay_args+=(--window-overlay "${PIKMIN_WINDOW_OVERLAY}" --window-title-header "${game_dir}/pc_port/pc_build_window_title.h")
fi
# PIKMIN_ALLOWED_CHANGES: newline-separated paths the upstream helper patched or
# copied in, so the manifest says "upstream + packaging patches", not dirty.
if [[ -n "${PIKMIN_ALLOWED_CHANGES:-}" ]]; then
    while IFS= read -r allowed; do
        [[ -n "${allowed}" ]] && overlay_args+=(--allowed-change "${allowed}")
    done <<< "${PIKMIN_ALLOWED_CHANGES}"
fi
if [[ -n "${PIKMIN_PACKAGING_FINGERPRINT:-}" ]]; then
    overlay_args+=(--packaging-fingerprint "${PIKMIN_PACKAGING_FINGERPRINT}")
fi
python3 "${game_dir}/../../tools/mac/build_identity.py" \
    --repo "${game_dir}/../.." --output "${contents}/Resources/build-info.json" \
    --game "${contents}/MacOS/nectar" --launcher "${contents}/MacOS/nectar-launcher" \
    --cache "${build_dir}/CMakeCache.txt" --data "${configured_data_root}" --role "${build_role}" \
    --save-mode "${PIKMIN_SAVE_MODE}" --save-mode-label "${PIKMIN_SAVE_MODE_LABEL}" \
    --save-directory "${configured_save_dir}" \
    ${overlay_args[@]+"${overlay_args[@]}"}

echo "Built: ${app}"

if [[ "${install}" -eq 1 ]]; then
    rc=0
    assert_central_save_unchanged "${OPEN_NECTAR_CENTRAL_SAVE_DIR}" "install" \
        install_app "${app}" "${install_dir}" "${app_name}" "${bundle_id}" "${build_role}" "${archive_root}" "${backup_root}" || rc=$?
    [[ "${rc}" -eq 0 ]] || exit "${rc}"
    assert_central_save_unchanged "${OPEN_NECTAR_CENTRAL_SAVE_DIR}" "merge_controls" \
        merge_controls "${configured_data_root}" "${build_role}"
fi
