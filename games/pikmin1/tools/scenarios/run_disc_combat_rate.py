#!/usr/bin/env python3
"""Local exact lane: bulborb_fight on a user-supplied GPIE01 rev 1 install must kill the Bulborb at 60 Hz
about as fast as at 30 Hz (issues #154 and #180).

The bounds are the measured divergence after the per-tick fixes in aiAttack.cpp, recorded in
combat-timing.md: they stop it growing back, they do not claim parity. Exit 77 without an install.
"""

import argparse
import os
from pathlib import Path

from sample_combat_rate import run

MAX_KILL_GAP_S = 0.35     # measured 0.2 s (30 Hz 3.13 s, 60 Hz 2.93 s)
MAX_EARLY_DAMAGE_GAP = 20  # damage in the first 1.5 s; measured 10


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    args = parser.parse_args()
    data = os.environ.get("PIKMIN_SCENARIO_DATA_DIR")
    if not data:
        print("Set PIKMIN_SCENARIO_DATA_DIR to a local GPIE01 rev 1 install.")
        return 77
    data_dir = Path(data).resolve()
    if not (data_dir / "assets/dataDir/parms/gamePrms.bin").is_file():
        print(f"Missing game assets in {data_dir}")
        return 1
    rows = {rate: run(args.game.resolve(), data_dir, "stages/stage1.ini", rate, 0) for rate in (30, 60)}
    for rate, row in rows.items():
        print(f"{rate} Hz: exit {row['exit']}, kill {row['kill']} s, damage in 1.5 s {row['early']}, flicks {row['flicks']}")
    problems = []
    for rate, row in rows.items():
        if row["exit"] != 0 or row["kill"] is None:
            problems.append(f"{rate} Hz: the fight did not finish (exit {row['exit']})")
    if not problems:
        gap = abs(rows[60]["kill"] - rows[30]["kill"])
        if gap > MAX_KILL_GAP_S:
            problems.append(f"kill time differs by {gap:.2f} s between 30 and 60 Hz (bound {MAX_KILL_GAP_S} s)")
        early = abs(rows[60]["early"] - rows[30]["early"])
        if early > MAX_EARLY_DAMAGE_GAP:
            problems.append(f"damage in the first 1.5 s differs by {early:.0f} (bound {MAX_EARLY_DAMAGE_GAP})")
        if rows[60]["flicks"] != rows[30]["flicks"]:
            problems.append(f"flick count {rows[30]['flicks']} at 30 Hz, {rows[60]['flicks']} at 60 Hz")
    for problem in problems:
        print(f"FAIL {problem}")
    return int(bool(problems))


if __name__ == "__main__":
    raise SystemExit(main())
