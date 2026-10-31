#!/usr/bin/env bash
# Ensure upstream-central-save.patch applies after the other upstream overlays.
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
repo="$(cd -- "${root}/../.." && pwd)"

if ! git -C "${repo}" rev-parse --verify refs/remotes/upstream/main^{commit} >/dev/null 2>&1; then
    git -C "${repo}" fetch --quiet upstream main
fi

tmp="$(mktemp -d)"
cleanup() { git -C "${repo}" worktree remove --force "${tmp}/src" 2>/dev/null || true; rm -rf "${tmp}"; }
trap cleanup EXIT

git -C "${repo}" worktree add --quiet --detach "${tmp}/src" refs/remotes/upstream/main
git -C "${tmp}/src" apply --check "${root}/upstream-launcher.patch"
git -C "${tmp}/src" apply --check "${root}/upstream-window-title.patch"
git -C "${tmp}/src" apply "${root}/upstream-launcher.patch"
git -C "${tmp}/src" apply "${root}/upstream-window-title.patch"
git -C "${tmp}/src" apply --check "${root}/upstream-central-save.patch"

printf 'test_upstream_central_save_patch: ok\n'
