#!/usr/bin/env python3
"""Split the scenario tests over CI shards by measured run time.

CTest's own `-I first,,stride` selection splits by test number, so a shard that happens to
draw several long tests runs far longer than its neighbours. This script instead deals the
tests out longest first, each to the shard with the least work so far, using the test times
recorded in shard_timings.json (seconds per test, per operating system).

  plan_shards.py plan  --tests tests.json --timings shard_timings.json --os macOS \\
                       --shards 3 --index 1 [--preload 1=150]
      prints a CTest -R regex that selects this shard's tests. Every shard computes the whole
      split and keeps its own part, so the parts always cover each test exactly once.
      tests.json is `ctest --show-only=json-v1 -R ^scenario_ -LE game-assets`. A test with no
      recorded time costs the median of the recorded ones. --preload INDEX=SECONDS adds work
      that a shard does besides these tests (the macOS shard that also runs the JAudio build).

  plan_shards.py learn --os macOS --out shard_timings.json [--blend 0.5] SHARD_DIR [SHARD_DIR ...]
      reads the junit.xml in each SHARD_DIR (an unpacked scenario-shard-* artifact keeps it
      under shard/) and stores the test times for that OS, keeping the other OSes' entries.
      Hosted runners vary by 30% or more from run to run, so --blend W keeps (1 - W) of an
      already stored time and takes W of the new one; a test with no stored time takes the
      new one. Without --blend the new times replace the stored ones.

Refresh the times when scenarios are added or change a lot; stale or missing times only make
the split less even, never wrong.
"""
import argparse
import json
import re
import statistics
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

DEFAULT_COST = 10.0


def test_names(tests_json):
    data = json.loads(Path(tests_json).read_text())
    return sorted(t["name"] for t in data.get("tests", []))


def split(names, costs, shards, preload=None):
    """Deal names out longest first to the least loaded shard. Returns a list of name lists."""
    known = [c for c in (costs.get(n) for n in names) if c is not None]
    fallback = statistics.median(known) if known else DEFAULT_COST
    cost = {n: costs.get(n, fallback) for n in names}
    load = [float((preload or {}).get(i + 1, 0.0)) for i in range(shards)]
    parts = [[] for _ in range(shards)]
    for n in sorted(names, key=lambda n: (-cost[n], n)):
        i = min(range(shards), key=lambda k: (load[k], k))
        parts[i].append(n)
        load[i] += cost[n]
    return [sorted(p) for p in parts], load


def regex_for(names):
    if not names:
        raise SystemExit("plan_shards: this shard would run no tests")
    return "^(" + "|".join(re.escape(n) for n in names) + ")$"


def cmd_plan(args):
    names = test_names(args.tests)
    if not names:
        raise SystemExit(f"plan_shards: no tests in {args.tests}")
    timings = json.loads(Path(args.timings).read_text()) if Path(args.timings).exists() else {}
    preload = {}
    for item in args.preload or []:
        k, _, v = item.partition("=")
        preload[int(k)] = float(v)
    parts, load = split(names, timings.get(args.os, {}), args.shards, preload)
    if not 1 <= args.index <= args.shards:
        raise SystemExit("plan_shards: --index must be 1..--shards")
    print(f"plan_shards: {args.os} shard {args.index}/{args.shards}: {len(parts[args.index - 1])} tests, "
          f"planned load {load[args.index - 1]:.0f} s (loads: {[round(x) for x in load]})", file=sys.stderr)
    print(regex_for(parts[args.index - 1]))


def cmd_learn(args):
    if args.blend is not None and not 0.0 <= args.blend <= 1.0:
        raise SystemExit("plan_shards: --blend must be between 0 and 1")
    out = Path(args.out)
    timings = json.loads(out.read_text()) if out.exists() else {}
    seen = {}
    for d in args.dirs:
        found = sorted(Path(d).rglob("junit.xml"))
        if not found:
            raise SystemExit(f"plan_shards: no junit.xml under {d}")
        for f in found:
            for case in ET.parse(f).getroot().iter("testcase"):
                name, secs = case.get("name"), case.get("time")
                if name and secs is not None and name.startswith("scenario_"):
                    seen[name] = round(float(secs), 2)
    if not seen:
        raise SystemExit("plan_shards: no scenario test times found")
    old = timings.get(args.os, {})
    if args.blend is not None:
        seen = {n: round((1 - args.blend) * old[n] + args.blend * v, 2) if n in old else v for n, v in seen.items()}
    timings[args.os] = dict(sorted(seen.items()))
    out.write_text(json.dumps(timings, indent=1, sort_keys=True) + "\n")
    print(f"plan_shards: {len(seen)} test times stored for {args.os} in {out}")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan")
    p.add_argument("--tests", required=True)
    p.add_argument("--timings", required=True)
    p.add_argument("--os", required=True)
    p.add_argument("--shards", type=int, required=True)
    p.add_argument("--index", type=int, required=True)
    p.add_argument("--preload", action="append")
    p.set_defaults(fn=cmd_plan)
    p = sub.add_parser("learn")
    p.add_argument("--os", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--blend", type=float, help="weight of the new time, 0 to 1 (default: replace)")
    p.add_argument("dirs", nargs="+")
    p.set_defaults(fn=cmd_learn)
    args = ap.parse_args(argv)
    args.fn(args)


if __name__ == "__main__":
    main()
