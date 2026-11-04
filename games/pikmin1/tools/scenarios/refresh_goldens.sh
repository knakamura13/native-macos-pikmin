#!/usr/bin/env bash
# Regenerate every OS's golden screenshots from CI (issue #87 P2). One command:
#
#   games/pikmin1/tools/scenarios/refresh_goldens.sh [--ref BRANCH] [--run RUN_ID]
#
# It starts the "Disc-free scenarios" workflow on BRANCH (default: the current branch, which must
# be pushed) with update_goldens=true, waits for it, downloads the golden-update-<OS> artifacts of
# the Linux, macOS and Windows runners and copies them into tools/scenarios/golden/<os>/. The
# goldens come from the CI runners, never from this machine's GPU. Review the pictures, then commit
# golden/. With --run, skips the dispatch and downloads from an existing update run.
# Needs the GitHub CLI (`gh`) signed in to the fork.
set -euo pipefail

repo="${PIKMIN_FORK_REPO:-knakamura13/native-macos-pikmin-1}"
ref="$(git rev-parse --abbrev-ref HEAD)"
run=""
while [ $# -gt 0 ]; do
  case "$1" in
    --ref) ref="$2"; shift 2 ;;
    --run) run="$2"; shift 2 ;;
    *) echo "usage: $0 [--ref BRANCH] [--run RUN_ID]" >&2; exit 2 ;;
  esac
done

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
golden="$here/golden"

if [ -z "$run" ]; then
  since="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  gh workflow run disc-free-scenarios.yml --repo "$repo" --ref "$ref" -f update_goldens=true
  echo "dispatched on $ref; waiting for the run to appear"
  for _ in $(seq 1 30); do
    sleep 5
    run="$(gh run list --repo "$repo" --workflow disc-free-scenarios.yml --branch "$ref" \
      --event workflow_dispatch --limit 5 --json databaseId,createdAt \
      --jq "[.[] | select(.createdAt >= \"$since\")] | first | .databaseId // empty")"
    [ -n "$run" ] && break
  done
  [ -n "$run" ] || { echo "no dispatched run found" >&2; exit 1; }
  echo "run $run"
  # The scenarios job writes the goldens even when an unrelated job in the run fails.
  gh run watch "$run" --repo "$repo" --interval 30 >/dev/null || true
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for pair in Linux:linux macOS:macos Windows:windows; do
  art="golden-update-${pair%%:*}"
  dir="$golden/${pair##*:}"
  gh run download "$run" --repo "$repo" --name "$art" --dir "$tmp/$art"
  mkdir -p "$dir"
  cp "$tmp/$art"/* "$dir/"
  echo "updated $dir ($(ls "$tmp/$art" | wc -l | tr -d ' ') files)"
done
echo "Done. Look at the pictures in golden/, then commit them."
