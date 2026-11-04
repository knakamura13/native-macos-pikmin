#!/usr/bin/env python3
"""Negative controls for golden_shots.py (compare, tolerance, advisory, update)."""
import json
import os
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import golden_shots as gs  # noqa: E402
from verify_scenario_png import read_png_rgb  # noqa: E402


def picture(shift=0, delta=0):
    """A 640x480 picture with many colours; `shift` moves a bar, `delta` brightens the left half."""
    px = []
    for y in range(gs.HEIGHT):
        for x in range(gs.WIDTH):
            v = (x * 7 + y * 3) % 200
            if shift and 100 <= x < 200:
                v = (v + shift) % 200
            d = delta if x < gs.WIDTH // 2 else 0
            px.append((v + d, (v * 2) % 200 + d, (v * 5) % 200 + d))
    return px


class CheckOne(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.golden = os.path.join(self.d, "g.png")
        gs.write_png_rgb(self.golden, gs.WIDTH, gs.HEIGHT, picture())

    def cap(self, px, w=gs.WIDTH, h=gs.HEIGHT):
        p = os.path.join(self.d, "c.png")
        gs.write_png_rgb(p, w, h, px)
        return p

    def test_identical_passes(self):
        ok, _ = gs.check_one(self.cap(picture()), self.golden, 0, 0.0)
        self.assertTrue(ok)

    def test_small_delta_within_tolerance(self):
        ok, _ = gs.check_one(self.cap(picture(delta=3)), self.golden, 4, 0.0)
        self.assertTrue(ok)

    def test_small_delta_over_tolerance_fails(self):
        ok, line = gs.check_one(self.cap(picture(delta=3)), self.golden, 2, 0.01)
        self.assertFalse(ok)
        self.assertIn("max_delta=3", line)

    def test_moved_bar_fails(self):
        ok, _ = gs.check_one(self.cap(picture(shift=90)), self.golden, 10, 0.03)
        self.assertFalse(ok)

    def test_blank_fails(self):
        ok, line = gs.check_one(self.cap([(0, 0, 0)] * (gs.WIDTH * gs.HEIGHT)), self.golden, 10, 1.0)
        self.assertFalse(ok)
        self.assertIn("distinct", line)

    def test_wrong_size_fails(self):
        ok, _ = gs.check_one(self.cap(picture()[: 64 * 36], 64, 36), self.golden, 10, 1.0)
        self.assertFalse(ok)

    def test_missing_golden_fails(self):
        ok, line = gs.check_one(self.cap(picture()), os.path.join(self.d, "none.png"), 10, 1.0)
        self.assertFalse(ok)
        self.assertIn("no golden", line)


class RunCheck(unittest.TestCase):
    """run_check through a fake game: a script that writes a fixed picture for every shot."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.pic = os.path.join(self.d, "pic.png")
        gs.write_png_rgb(self.pic, gs.WIDTH, gs.HEIGHT, picture())
        self.game = os.path.join(self.d, "fake_game.py")
        with open(self.game, "w", encoding="utf-8") as f:
            f.write(
                "#!%s\nimport shutil, sys\n"
                "a = sys.argv\n"
                "for i, v in enumerate(a):\n"
                "    if v == '--scenario-screenshot':\n"
                "        phase, _, path = a[i + 1].partition(':')\n"
                "        if phase in ('boot', 'between'):\n"
                "            _, _, path = path.partition(':')\n"
                "        shutil.copyfile(%r, path)\n"
                % (sys.executable, self.pic)
            )
        self.fake_script = self.game
        os.chmod(self.game, 0o755)
        if os.name == "nt":
            # CreateProcess cannot execute a Python shebang. Keep the fake game
            # executable-shaped for the same capture path as the real game.
            launcher = os.path.join(self.d, "fake_game.cmd")
            with open(launcher, "w", encoding="utf-8") as f:
                f.write(f'@"{sys.executable}" "{self.game}" %*\n')
            self.game = launcher
        self.golden_dir = os.path.join(self.d, "golden")
        os.makedirs(os.path.join(self.golden_dir, "linux"))
        with open(os.path.join(self.golden_dir, "tolerance.json"), "w", encoding="utf-8") as f:
            json.dump({"linux": {"channel": 4, "max_diff_ratio": 0.0}}, f)

    def run_it(self, env_extra):
        env = dict(os.environ)
        env.pop("PIKMIN_UPDATE_GOLDENS", None)
        env.pop("PIKMIN_GOLDEN_ADVISORY", None)
        env.update(env_extra)
        return subprocess.run(
            [sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden_shots.py"), "check",
             "--game", self.game, "--data", self.d, "--work-dir", os.path.join(self.d, "work"),
             "--golden-dir", self.golden_dir, "--os", "linux"],
            env=env, capture_output=True, text=True)

    def fill_goldens(self, px):
        for n in gs.shot_names():
            gs.write_png_rgb(os.path.join(self.golden_dir, "linux", n + ".png"), gs.WIDTH, gs.HEIGHT, px)

    def test_fixture_preserves_windows_drive_path(self):
        path = r"C:\golden shots\capture.png"
        for spec in ("100", "boot:314", "between:70"):
            argv = [self.fake_script, "--scenario-screenshot", f"{spec}:{path}"]
            with self.subTest(spec=spec), patch("sys.argv", argv), patch("shutil.copyfile") as copy:
                runpy.run_path(self.fake_script, run_name="__main__")
                copy.assert_called_once_with(self.pic, path)

    def test_matching_goldens_pass(self):
        self.fill_goldens(picture())
        r = self.run_it({})
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_differing_goldens_fail_then_advisory_passes(self):
        self.fill_goldens(picture(shift=90))
        self.assertEqual(self.run_it({}).returncode, 1)
        r = self.run_it({"PIKMIN_GOLDEN_ADVISORY": "1"})
        self.assertEqual(r.returncode, 0)
        self.assertIn("::warning", r.stdout)

    def test_missing_goldens_fail(self):
        self.assertEqual(self.run_it({}).returncode, 1)

    def test_update_writes_goldens_and_skips_compare(self):
        out = os.path.join(self.d, "out")
        r = self.run_it({"PIKMIN_UPDATE_GOLDENS": "1", "PIKMIN_GOLDEN_OUT": out})
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(sorted(os.listdir(out)), sorted(n + ".png" for n in gs.shot_names()))


class BeforeAfter(unittest.TestCase):
    def test_same_and_changed_shots(self):
        d = tempfile.mkdtemp()
        for sub in ("b", "a"):
            os.makedirs(os.path.join(d, sub))
        gs.write_png_rgb(os.path.join(d, "b", "same.png"), gs.WIDTH, gs.HEIGHT, picture())
        gs.write_png_rgb(os.path.join(d, "a", "same.png"), gs.WIDTH, gs.HEIGHT, picture())
        gs.write_png_rgb(os.path.join(d, "b", "moved.png"), gs.WIDTH, gs.HEIGHT, picture())
        gs.write_png_rgb(os.path.join(d, "a", "moved.png"), gs.WIDTH, gs.HEIGHT, picture(shift=90))
        gs.write_png_rgb(os.path.join(d, "a", "extra.png"), gs.WIDTH, gs.HEIGHT, picture())
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "make_before_after.py")
        r = subprocess.run(
            [sys.executable, script, "--before", os.path.join(d, "b"), "--after", os.path.join(d, "a"),
             "--out", os.path.join(d, "o")], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("| moved | changed |", r.stdout)
        self.assertIn("| same | same |", r.stdout)
        self.assertIn("| extra | new on the head |", r.stdout)
        self.assertIn("1 of 2 shots differ", r.stdout)
        w, h, _ = read_png_rgb(os.path.join(d, "o", "moved_before_after.png"))
        self.assertEqual((w, h), (gs.WIDTH * 3 + gs.GUTTER * 2, gs.HEIGHT))


if __name__ == "__main__":
    unittest.main()
