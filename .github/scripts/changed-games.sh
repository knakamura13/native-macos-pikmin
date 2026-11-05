#!/usr/bin/env bash
# Which game builds a change needs (ci.yml). Reads the changed paths, one per line, on stdin and
# prints `pikmin1=true|false` and `pikmin2=true|false` (GITHUB_OUTPUT format).
#
# A path counts for one game only when it is that game's own:
#   Pikmin 2 only: games/pikmin2/**, PIKMIN2_*.md at the top level
#   Pikmin 1 only: games/pikmin1/**, except games/pikmin1/pc_port/dolphin_stubs/**, which
#                  games/pikmin2/CMakeLists.txt compiles into Pikmin 2 as well
# Every other path (libs/, tests/, tools/, scripts/, versions.json, .github/, top-level CMake,
# docs, ...) counts for both. Pikmin 2's CMake pulls in libs/nectar_net, tests/, versions.json
# and the dolphin_stubs above; Pikmin 1's pulls in libs/, tests/ and versions.json. When a
# new cross-game reference appears, add it here. No paths at all (an unknown change) means both.
#
# disc-free-scenarios.yml (Pikmin 1 only) skips a pull request whose paths are all Pikmin 2's
# own through its paths-ignore list, which must match the Pikmin 2 rule above.
set -euo pipefail
p1=false
p2=false
seen=false
while IFS= read -r path || [ -n "$path" ]; do
  path="${path%$'\r'}"
  [ -n "$path" ] || continue
  seen=true
  case "$path" in
    games/pikmin2/*|PIKMIN2_*.md) p2=true ;;
    games/pikmin1/pc_port/dolphin_stubs/*) p1=true; p2=true ;;
    games/pikmin1/*) p1=true ;;
    *) p1=true; p2=true ;;
  esac
done
if [ "$seen" = false ]; then p1=true; p2=true; fi
echo "pikmin1=$p1"
echo "pikmin2=$p2"
