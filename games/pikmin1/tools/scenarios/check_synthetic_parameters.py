#!/usr/bin/env python3
"""Check the fixture contract: tagged files delegate values to source constructors.

Parameters::read consumes one 0xffffffff terminator per list without changing
constructor defaults (covered independently by nectar_parms_writer_test).
"""
import pathlib
import sys

LISTS = {
    'gamePrms.bin': 1,
    'aiconst.bin': 1,
    'demo.bin': 1,
    'cunit.bin': 1,
    'naviMgr.bin': 2,
    'pikiMgr.bin': 2,
}


def check(root):
    problems = []
    for name, count in LISTS.items():
        path = pathlib.Path(root) / 'assets/dataDir/parms' / name
        expected = b'\xff\xff\xff\xff' * count
        if not path.is_file() or path.read_bytes() != expected:
            problems.append(f'{name}: expected {count} empty tagged list(s), using engine defaults')
    return problems


if __name__ == '__main__':
    failures = check(sys.argv[1])
    for failure in failures:
        print(f'FAIL {failure}')
    if not failures:
        print('Synthetic parameter contract: all six files use source constructor defaults')
    sys.exit(bool(failures))
