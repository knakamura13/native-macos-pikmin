#!/usr/bin/env bash
# build-app.sh [--clean] [--build-dir DIR] [--install [DIR]]
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

clean=0
install=0
install_dir="/Applications"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean) clean=1; shift ;;
        --build-dir) build_dir="$2"; shift 2 ;;
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

if [[ "${clean}" -eq 1 ]]; then
    rm -rf "${build_dir}" "${output_dir}"
fi

# Older builds wrote packaging/macos/out/Pikmin 1.app. Spotlight indexes that
# folder, so drop the leftover bundle. Leave anything else in out/ alone.
legacy_app="${script_dir}/out/${app_name}"
if [[ -e "${legacy_app}" ]]; then
    rm -rf "${legacy_app}"
    rmdir "${script_dir}/out" 2>/dev/null || true
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
app="${output_dir}/${app_name}"
contents="${app}/Contents"

rm -rf "${app}"
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
    <string>org.opennectar.pikmin1</string>
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
        <key>NECTAR_PIKMIN1_DIR</key>
        <string>${HOME}/.local/share/pikmin-native</string>
        <key>NECTAR_PIKMIN1_EXECUTABLE</key>
        <string>${game_app}/Contents/MacOS/nectar</string>
    </dict>
</dict>
</plist>
PLIST

# auto-install compares this sha with origin/kyle-develop. "unknown" means the
# next auto-install run will build again.
write_build_info() {
    local sha branch sha_json branch_json text
    sha="$(git -C "${game_dir}" rev-parse HEAD 2>/dev/null || true)"
    branch="$(git -C "${game_dir}" rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
    if [[ -z "${sha}" ]]; then
        sha="unknown"
    fi
    if [[ -z "${branch}" || "${branch}" == "HEAD" ]]; then
        branch=""
    fi
    sha_json="$(json_escape "${sha}")"
    branch_json="$(json_escape "${branch}")"
    text="$(printf '{\n  "sha": "%s",\n  "branch": "%s"\n}\n' "${sha_json}" "${branch_json}")"
    printf '%s' "${text}" > "${contents}/Resources/build-info.json"
}

json_escape() {
    local text=$1
    text=${text//\\/\\\\}
    text=${text//\"/\\\"}
    text=${text//$'\n'/}
    text=${text//$'\r'/}
    printf '%s' "${text}"
}

write_build_info

echo "Built: ${app}"

# Merges controls.conf into the device settings file. The game reads
# pikmin_settings.conf from the data folder, the same folder the launcher uses.
# Only the keys in controls.conf change. Other settings are kept.
merge_controls() {
    local controls="${script_dir}/controls.conf"
    local data_root="${XDG_DATA_HOME:-${HOME}/.local/share}/pikmin-native"
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
    # cp -R onto an existing bundle nests the copy inside it, so remove the old one first.
    rm -rf "${installed}"
    mkdir -p "${install_dir}"
    cp -R "${app}" "${install_dir}/"
    echo "Installed: ${installed}"
    merge_controls
fi
