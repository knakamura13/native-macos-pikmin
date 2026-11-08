#!/usr/bin/env python3
"""Sample bulborb_fight across RNG streams at each tick rate (combat timing, #154/#180).

One deterministic run per rate cannot tell a tick-rate effect from a different random stream:
the streams differ between rates. This runs the scenario for many RNG prerolls
(PIKMIN_NETPLAY_TEST_PREROLL_RAND) at every rate and prints, per rate, the kill time, the
damage landed in the first second and a half, the flick count and the average Flick length.

  --data DIR     a user-supplied GPIE01 rev 1 install (read-only); the disc's first Dwarf
                 Bulborb on stages/stage1.ini. Without it the stand-in tree is used.
  --standin DIR  the generated stand-in tree (build/<dir>/scenario-data), stage sandbox_objects.

Local exact lane: the disc numbers are measured expected outputs and are never fixtures.
"""
import argparse
import concurrent.futures
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile

HIT = re.compile(r'bulborb_fight: HIT t=([\d.]+) s damage ([\d.]+)')
KILL = re.compile(r'Bulborb dead at ([\d.]+) s')
FLICK = re.compile(r'Flick lasted ([\d.]+) s on average, [\d.]+ s at most \((\d+) ended\)')
FLICKS = re.compile(r'flicks (\d+), re-attacks')


def run(game, data, stage, rate, preroll, shift=0):
    env = dict(os.environ, PIKMIN_SCENARIO_DATA_DIR=str(data), PIKMIN_SCENARIO_STAGE=stage,
               PIKMIN_NETPLAY_TEST_PREROLL_RAND=str(preroll), PIKMIN_FIGHT_SHIFT=str(shift), SDL_AUDIODRIVER='dummy')
    with tempfile.TemporaryDirectory(prefix='combat-rate-') as scratch:
        result = subprocess.run([str(game), '--scenario', 'bulborb_fight', '--scenario-tick-rate', str(rate)],
                                cwd=scratch, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, timeout=300)
    out = result.stdout
    hits = [(float(t), float(d)) for t, d in HIT.findall(out)]
    kill = KILL.search(out)
    dead = [float(tm) for tm, d in re.findall(r'HIT t=([\d.]+) s damage [\d.]+ health (-?[\d.]+)', out) if float(d) <= 0.0]
    flick = FLICK.search(out)
    flicks = FLICKS.search(out)
    return {
        'rate': rate, 'preroll': preroll, 'exit': result.returncode,
        'kill': dead[0] if dead else (float(kill.group(1)) if kill else None),
        'early': sum(d for t, d in hits if t <= 1.5),
        'flicks': int(flicks.group(1)) if flicks else None,
        'flick_len': float(flick.group(1)) if flick and int(flick.group(2)) else None,
    }


def summary(values):
    values = [v for v in values if v is not None]
    if not values:
        return 'n/a'
    return f'mean {statistics.mean(values):.2f}  median {statistics.median(values):.2f}  sd {statistics.pstdev(values):.2f}  n {len(values)}'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('game', type=Path)
    parser.add_argument('--data', type=Path, help='disc install folder (holds assets/)')
    parser.add_argument('--standin', type=Path, help='stand-in tree folder (holds assets/)')
    parser.add_argument('--rates', type=int, nargs='+', default=[30, 60, 120])
    parser.add_argument('--seeds', type=int, default=24, help='number of runs per rate; with --shift-step each run starts the squad shift-step units further along x')
    parser.add_argument('--shift-step', type=float, default=0.0)
    parser.add_argument('--jobs', type=int, default=3)
    args = parser.parse_args()
    if bool(args.data) == bool(args.standin):
        parser.error('give exactly one of --data and --standin')
    data = (args.data or args.standin).resolve()
    stage = 'stages/stage1.ini' if args.data else 'stages/sandbox_objects.ini'
    jobs = [(r, s) for r in args.rates for s in range(args.seeds)]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        rows = list(pool.map(lambda j: run(args.game.resolve(), data, stage, j[0], 0, j[1] * args.shift_step), jobs))
    for rate in args.rates:
        mine = [r for r in rows if r['rate'] == rate]
        bad = [r for r in mine if r['exit'] != 0]
        print(f'{rate:>3} Hz  kill s   {summary([r["kill"] for r in mine])}')
        print(f'        dmg<=1.5s {summary([r["early"] for r in mine])}')
        print(f'        flicks    {summary([r["flicks"] for r in mine])}')
        print(f'        flick s   {summary([r["flick_len"] for r in mine])}')
        if bad:
            print(f'        {len(bad)} run(s) exited non-zero (kill limit or flick limit)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
