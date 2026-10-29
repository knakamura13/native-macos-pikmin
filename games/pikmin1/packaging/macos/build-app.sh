#!/usr/bin/env bash
# build-app.sh [--clean] [--build-dir DIR] [--output-dir DIR] [--install [DIR]]
#
# Builds "Pikmin 1.app" for macOS (arm64, USA build) and leaves it in:
#
#   packaging/macos/out.noindex/Pikmin 1.app
#
# The ".noindex" suffix keeps Spotlight from listing that intermediate copy.
# The installed app in /Applications is the one that should show up in search.
#
# --install replaces DIR/Pikmin 1.app (default DIR: /Applications) with the new
# build. It refuses to run while the app is open, so an open game is not cut off.
#
# --install also merges controls.conf (the Mac key layout) into the device settings
# file, ~/.local/share/pikmin-native/pikmin_settings.conf.
#
# The bundle's main executable is the launcher, which
# asks for the user's own disc image on first run, extracts it under
# ~/.local/share/pikmin-native, and installs the game beside itself.
#
# The game links SDL2 from Homebrew (sdl2-compat). The app runs only on Macs
# that have that library at the same path.

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
game_dir="$(cd -- "${script_dir}/../.." && pwd)"
build_dir="${game_dir}/build-macos"
output_dir="${script_dir}/out.noindex"
app_name="Pikmin 1.app"
icon_src="${game_dir}/packaging/icon/open_nectar.png"
bundle_id="org.opennectar.pikmin1"
launcher_title="Open Nectar"
configured_data_root="${PIKMIN_DATA_ROOT:-${HOME}/.local/share/pikmin-native}"
build_role="${PIKMIN_BUILD_ROLE:-unknown}"
marker_subject="$(git -C "${game_dir}" log -1 --format=%s 2>/dev/null || true)"
if [[ "${build_role}" == "develop" && "${marker_subject}" != "kyle-develop-rebuild-v1" ]]; then
    printf 'Refusing to label a build Develop without a develop rebuild marker.\n' >&2
    exit 1
fi
if [[ "${build_role}" == "unknown" && "${marker_subject}" == "kyle-develop-rebuild-v1" ]]; then
    build_role="develop"
fi
if [[ "${build_role}" == "upstream" ]]; then
    bundle_id="org.opennectar.pikmin1.upstream"
    launcher_title="Pikmin 1 — Upstream"
elif [[ "${build_role}" == "develop" ]]; then
    launcher_title="Pikmin 1 — Develop"
fi

clean=0
install=0
custom_output=0
install_dir="/Applications"
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
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

app="${output_dir}/${app_name}"
if [[ "${install}" -eq 1 ]] && python3 -c 'import os,sys; sys.exit(0 if os.path.realpath(sys.argv[1]) == os.path.realpath(sys.argv[2]) else 1)' "${app}" "${install_dir}/${app_name}"; then
    printf '%s\n' 'Output and installation apps must be different paths.' >&2
    exit 2
fi
if [[ "${custom_output}" -eq 1 && ( -e "${app}" || -L "${app}" ) ]]; then
    printf 'Refusing to replace an existing output app: %s\n' "${app}" >&2
    exit 1
fi
if [[ "${clean}" -eq 1 ]]; then
    if [[ "${build_dir}" != "${game_dir}/build-macos" ]]; then
        printf '%s\n' '--clean is limited to the default build directory.' >&2
        exit 2
    fi
    rm -rf "${build_dir}"
fi

# ccache makes a --clean rebuild fast. Its cache lives outside build-macos, so it
# survives --clean.
cache_flags=()
if command -v ccache >/dev/null 2>&1; then
    cache_flags+=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

cmake -S "${game_dir}" -B "${build_dir}" -DCMAKE_BUILD_TYPE=Release ${cache_flags[@]+"${cache_flags[@]}"}
cmake --build "${build_dir}" --target pikmin_pc pikmin_launcher -j "$(sysctl -n hw.ncpu)"

bin_dir="${build_dir}/bin"
contents="${app}/Contents"

if [[ -e "${app}" || -L "${app}" ]]; then
    if [[ "${custom_output}" -eq 1 ]]; then
        printf 'Refusing to replace an existing output app: %s\n' "${app}" >&2
        exit 1
    fi
    if pgrep -x "nectar" >/dev/null || pgrep -x "nectar-launcher" >/dev/null; then
        printf 'Pikmin is running; refusing to replace the existing output app.\n' >&2
        exit 1
    fi
    rm -rf "${app}"
fi
mkdir -p "${contents}/MacOS" "${contents}/Resources"

# The binaries keep the upstream names (nectar, nectar-launcher). Only the app
# bundle is called "Pikmin 1", so the code and the install format stay unchanged.
cp "${bin_dir}/nectar" "${contents}/MacOS/nectar"
cp "${bin_dir}/nectar-launcher" "${contents}/MacOS/nectar-launcher"

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

xml_escape() {
    local value=$1
    value=${value//&/&amp;}
    value=${value//</&lt;}
    value=${value//>/&gt;}
    printf '%s' "${value}"
}
data_xml="$(xml_escape "${configured_data_root}")"
game_app_xml="$(xml_escape "${game_app}")"
upstream_env=""
if [[ "${build_role}" == "upstream" ]]; then
    upstream_env="<key>XDG_DATA_HOME</key><string>${data_xml}/xdg-data</string>
        <key>XDG_CONFIG_HOME</key><string>${data_xml}/config</string>
        <key>XDG_CACHE_HOME</key><string>${data_xml}/cache</string>
        <key>NECTAR_SAVE_DIR</key><string>${data_xml}/save</string>"
fi

cat > "${contents}/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>Pikmin 1</string>
    <key>CFBundleDisplayName</key>
    <string>Pikmin 1</string>
    <key>CFBundleIdentifier</key>
    <string>${bundle_id}</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleExecutable</key>
    <string>nectar-launcher</string>
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
        <string>${launcher_title}</string>
        <key>NECTAR_PIKMIN1_DIR</key>
        <string>${data_xml}</string>
        <key>NECTAR_PIKMIN1_EXECUTABLE</key>
        <string>${game_app_xml}/Contents/MacOS/nectar</string>
        ${upstream_env}
    </dict>
</dict>
</plist>
PLIST

overlay_args=()
if [[ -n "${PIKMIN_LAUNCHER_OVERLAY:-}" ]]; then
    overlay_args+=(--launcher-overlay "${PIKMIN_LAUNCHER_OVERLAY}")
fi
if [[ -n "${PIKMIN_WINDOW_OVERLAY:-}" ]]; then
    overlay_args+=(--window-overlay "${PIKMIN_WINDOW_OVERLAY}" --window-title-header "${game_dir}/pc_port/pc_build_window_title.h")
fi
python3 "${game_dir}/../../tools/mac/build_identity.py" \
    --repo "${game_dir}/../.." --output "${contents}/Resources/build-info.json" \
    --game "${contents}/MacOS/nectar" --launcher "${contents}/MacOS/nectar-launcher" \
    --cache "${build_dir}/CMakeCache.txt" --data "${configured_data_root}" --role "${build_role}" \
    ${overlay_args[@]+"${overlay_args[@]}"}

echo "Built: ${app}"

# Merges controls.conf into the device settings file. The game reads
# pikmin_settings.conf from the data folder, the same folder the launcher uses.
# Only the keys in controls.conf change. Other settings are kept.
merge_controls() {
    local controls="${script_dir}/controls.conf"
    local data_root="${configured_data_root}"
    local settings="${data_root}/pikmin_settings.conf"
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

if [[ "${install}" -eq 1 ]]; then
    installed="${install_dir}/${app_name}"
    # Refuse rather than kill: quitting a running game would lose unsaved progress.
    if pgrep -x "nectar" >/dev/null || pgrep -x "nectar-launcher" >/dev/null; then
        echo "Pikmin 1 is running. Quit it, then rerun with --install." >&2
        exit 1
    fi
    if [[ "${PIKMIN_INSTALL_NEW_ONLY:-0}" == "1" && ( -e "${installed}" || -L "${installed}" ) ]]; then
        echo "Refusing to replace an existing app: ${installed}" >&2
        exit 1
    fi
    # cp -R onto an existing bundle nests the copy inside it, so remove the old one first.
    rm -rf "${installed}"
    mkdir -p "${install_dir}"
    cp -R "${app}" "${install_dir}/"
    echo "Installed: ${installed}"
    merge_controls
fi
