#!/usr/bin/env bash
# Compile the native test-app stub and check it execs a sibling open-nectar.sh.
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
    printf 'test_open_nectar_stub.sh runs on macOS.\n' >&2
    exit 1
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
src="${script_dir}/../open-nectar-stub.c"
tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

mkdir -p "${tmp}/MacOS"
cc -O2 -Wall -Werror -o "${tmp}/MacOS/open-nectar" "${src}"
cat > "${tmp}/MacOS/open-nectar.sh" <<'SH'
#!/bin/bash
printf 'stub-ok argc=%s\n' "$#"
printf 'arg1=%s\n' "${1:-}"
printf 'arg2=%s\n' "${2:-}"
SH
chmod +x "${tmp}/MacOS/open-nectar.sh"

output="$("${tmp}/MacOS/open-nectar" hello "world with spaces")"
[[ "${output}" == *$'\nstub-ok argc=2\narg1=hello\narg2=world with spaces' ]] || [[ "${output}" == $'stub-ok argc=2\narg1=hello\narg2=world with spaces' ]]

file "${tmp}/MacOS/open-nectar" | grep -q 'Mach-O'

printf 'test_open_nectar_stub ok\n'
