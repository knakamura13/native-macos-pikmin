#!/usr/bin/env bash
# Rebuild origin/kyle-develop from upstream main, kyle-patches, and open fork PRs.
# Run this from a checkout of origin/kyle-patches. See AGENTS.md.
set -euo pipefail
root="$(git rev-parse --show-toplevel)"
cd "${root}"
exec python3 "${root}/tools/kyle-develop/rebuild.py" "$@"
