#!/usr/bin/env python3
"""Compare actual ballistic scenario trajectories with their 30 Hz reference.

All sampled peaks, landing times and heights at shared reference times must agree.
Only the airborne part is compared: post-contact AI and animation timing are separate
contracts. No expected disc values are invented; the 30 Hz scenario log is the reference.
"""
import argparse
from pathlib import Path
import re

CASES = ('fall', 'flick', 'launch', 'impulse', 'zero_gravity', 'drag', 'volatile', 'scripted', 'enemy')
TOLERANCE = 0.1
TRACE = re.compile(r'^\[ballistic\] TRACE ballistic_(\w+) (\d+) ([-\d.]+) ([-\d.]+)$', re.M)
END = re.compile(r'^\[ballistic\] END ballistic_(\w+) peak ([-\d.]+) landing (\d+)$', re.M)


def collect(path, case, rate):
    text = Path(path).read_text()
    if not re.search(rf'^\[scenario\] PASS ballistic_{case}\b', text, re.M) or re.search(r'^\[scenario\] FAIL\b', text, re.M):
        raise ValueError(f'{path}: missing PASS or reported FAIL')
    rows = [(int(t), float(y), float(v)) for c, t, y, v in TRACE.findall(text) if c == case]
    if len(rows) != 2 * rate + 1 or [r[0] for r in rows] != list(range(2 * rate + 1)):
        raise ValueError(f'{path}: expected complete ordered 0..{2 * rate} trajectory')
    end = [(float(pk), int(land)) for c, pk, land in END.findall(text) if c == case]
    if len(end) != 1:
        raise ValueError(f'{path}: expected exactly one summary')
    peak, landing = end[0]
    if abs(peak - max(row[1] for row in rows)) > 0.00002:
        raise ValueError(f'{path}: summary peak differs from trace')
    if case not in ('zero_gravity', 'scripted') and not 0 < landing <= 2 * rate:
        raise ValueError(f'{path}: missing airborne landing')
    return rows, peak, landing


def compare(directory):
    problems = []
    for case in CASES:
        data = {rate: collect(Path(directory) / f'scenario_ballistic_{case}_{rate}.log', case, rate) for rate in (30, 60, 120)}
        ref, peak, landing = data[30]
        for rate in (60, 120):
            rows, got_peak, got_landing = data[rate]
            if abs(got_peak - peak) > TOLERANCE:
                problems.append(f'{case} {rate} Hz peak {got_peak:.6f}, 30 Hz {peak:.6f}')
            # Collision tracing stays at high frequency; allow one original tick of contact quantization.
            if landing and abs(got_landing / rate - landing / 30) > 1 / 30 + 0.00001:
                problems.append(f'{case} {rate} Hz landing {got_landing / rate:.6f}s, 30 Hz {landing / 30:.6f}s')
            for tick, y, _ in ref:
                if landing and tick >= landing:
                    break
                got_y = rows[tick * (rate // 30)][1]
                if abs(got_y - y) > TOLERANCE:
                    problems.append(f'{case} {rate} Hz t={tick}/30 height {got_y:.6f}, 30 Hz {y:.6f}')
                    break
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory')
    args = parser.parse_args()
    try:
        problems = compare(args.directory)
    except (OSError, ValueError) as error:
        parser.exit(1, f'FAIL: {error}\n')
    if problems:
        parser.exit(1, '\n'.join('FAIL: ' + p for p in problems) + '\n')
    print('ballistic fall/flick/launch/impulse/zero-gravity/drag/volatile/scripted/enemy match 30 Hz at 60/120 Hz')


if __name__ == '__main__':
    main()
