#!/usr/bin/env python3
"""Checks for plan_shards.py: every test lands in exactly one shard, the split is the same on
every run, long tests spread out, and unknown tests and bad input are handled."""
import io
import json
import re
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

import plan_shards


def names_of(regex):
    return regex[2:-2].split("|")


class SplitTests(unittest.TestCase):
    def setUp(self):
        self.names = [f"scenario_t{i:02d}" for i in range(20)]
        self.costs = {n: float(20 - i) for i, n in enumerate(self.names)}  # t00 is longest

    def test_every_test_in_exactly_one_shard(self):
        for shards in (1, 2, 3, 7):
            parts, _ = plan_shards.split(self.names, self.costs, shards)
            flat = [n for p in parts for n in p]
            self.assertEqual(sorted(flat), sorted(self.names))
            self.assertEqual(len(flat), len(set(flat)))

    def test_deterministic(self):
        a = plan_shards.split(self.names, self.costs, 3)
        b = plan_shards.split(list(reversed(self.names)), self.costs, 3)
        self.assertEqual(a, b)

    def test_balanced_better_than_stride(self):
        parts, load = plan_shards.split(self.names, self.costs, 3)
        stride = [sum(self.costs[n] for n in self.names[i::3]) for i in range(3)]
        self.assertLessEqual(max(load) - min(load), max(stride) - min(stride))
        self.assertLessEqual(max(load) - min(load), 20.0)  # no worse than the longest test

    def test_long_tests_spread(self):
        parts, _ = plan_shards.split(self.names, self.costs, 3)
        firsts = [p for p in parts if self.names[0] in p or self.names[1] in p or self.names[2] in p]
        self.assertEqual(len(firsts), 3)

    def test_unknown_test_costs_the_median(self):
        names = self.names + ["scenario_new"]
        parts, load = plan_shards.split(names, self.costs, 2)
        self.assertIn("scenario_new", [n for p in parts for n in p])
        known_total = sum(self.costs.values())
        self.assertAlmostEqual(sum(load), known_total + 10.5)  # median of 1..20 is 10.5

    def test_preload_moves_work_off_that_shard(self):
        _, even = plan_shards.split(self.names, self.costs, 2)
        parts, load = plan_shards.split(self.names, self.costs, 2, {1: 60.0})
        self.assertGreater(sum(self.costs[n] for n in parts[1]), sum(self.costs[n] for n in parts[0]))
        self.assertAlmostEqual(sum(load), sum(even) + 60.0)


class CommandTests(unittest.TestCase):
    def run_main(self, argv):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            plan_shards.main(argv)
        return out.getvalue().strip(), err.getvalue()

    def test_plan_prints_a_regex_that_matches_exactly_this_shards_tests(self):
        with tempfile.TemporaryDirectory() as d:
            tests = Path(d, "tests.json")
            names = [f"scenario_t{i}" for i in range(10)]
            tests.write_text(json.dumps({"tests": [{"name": n} for n in names]}))
            timings = Path(d, "timings.json")
            timings.write_text(json.dumps({"Linux": {n: float(i + 1) for i, n in enumerate(names)}}))
            picked = []
            for idx in (1, 2, 3):
                rx, _ = self.run_main(["plan", "--tests", str(tests), "--timings", str(timings),
                                       "--os", "Linux", "--shards", "3", "--index", str(idx)])
                matched = [n for n in names if re.search(rx, n)]
                self.assertEqual(sorted(matched), sorted(names_of(rx)))
                picked += matched
            self.assertEqual(sorted(picked), sorted(names))

    def test_plan_with_no_timings_still_splits(self):
        with tempfile.TemporaryDirectory() as d:
            tests = Path(d, "tests.json")
            tests.write_text(json.dumps({"tests": [{"name": f"scenario_a{i}"} for i in range(6)]}))
            rx, _ = self.run_main(["plan", "--tests", str(tests), "--timings", str(Path(d, "none.json")),
                                   "--os", "Windows", "--shards", "2", "--index", "2"])
            self.assertEqual(len(names_of(rx)), 3)

    def test_empty_shard_and_bad_index_fail(self):
        with tempfile.TemporaryDirectory() as d:
            tests = Path(d, "tests.json")
            tests.write_text(json.dumps({"tests": [{"name": "scenario_only"}]}))
            base = ["plan", "--tests", str(tests), "--timings", str(Path(d, "none.json")), "--os", "Linux", "--shards", "2"]
            with self.assertRaises(SystemExit):
                self.run_main(base + ["--index", "2"])  # the single test goes to shard 1
            with self.assertRaises(SystemExit):
                self.run_main(base + ["--index", "3"])

    def test_learn_reads_junit_and_keeps_other_oses(self):
        junit = """<testsuite><testcase name="scenario_a" time="1.5"/><testcase name="scenario_b" time="30"/>
                   <testcase name="pc_unit" time="9"/></testsuite>"""
        with tempfile.TemporaryDirectory() as d:
            shard = Path(d, "scenario-shard-Linux-1", "shard")
            shard.mkdir(parents=True)
            shard.joinpath("junit.xml").write_text(junit)
            out = Path(d, "t.json")
            out.write_text(json.dumps({"macOS": {"scenario_a": 5.0}}))
            self.run_main(["learn", "--os", "Linux", "--out", str(out), str(shard.parent)])
            data = json.loads(out.read_text())
            self.assertEqual(data["Linux"], {"scenario_a": 1.5, "scenario_b": 30.0})
            self.assertEqual(data["macOS"], {"scenario_a": 5.0})

    def test_learn_blend_averages_old_and_new(self):
        junit = '<testsuite><testcase name="scenario_a" time="20"/><testcase name="scenario_new" time="7"/></testsuite>'
        with tempfile.TemporaryDirectory() as d:
            shard = Path(d, "s", "shard")
            shard.mkdir(parents=True)
            shard.joinpath("junit.xml").write_text(junit)
            out = Path(d, "t.json")
            out.write_text(json.dumps({"Linux": {"scenario_a": 10.0, "scenario_gone": 3.0}}))
            self.run_main(["learn", "--os", "Linux", "--out", str(out), "--blend", "0.5", str(shard.parent)])
            self.assertEqual(json.loads(out.read_text())["Linux"], {"scenario_a": 15.0, "scenario_new": 7.0})
            with self.assertRaises(SystemExit):
                self.run_main(["learn", "--os", "Linux", "--out", str(out), "--blend", "1.5", str(shard.parent)])

    def test_learn_without_junit_fails(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(SystemExit):
                self.run_main(["learn", "--os", "Linux", "--out", str(Path(d, "t.json")), d])


if __name__ == "__main__":
    unittest.main()
