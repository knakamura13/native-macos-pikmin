import pathlib
import tempfile
import unittest
from check_ballistic import CASES, compare, collect


class BallisticCheckerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = pathlib.Path(self.temp.name)
        for case in CASES:
            for rate in (30, 60, 120):
                # A piecewise-linear known trajectory with apex at 1/30 and landing at 1s.
                values = [200 + 10 * min(t / rate * 30, 1) if case == 'zero_gravity' else max(0, 200 * (1 - t / rate)) for t in range(2 * rate + 1)]
                rows = [f'[ballistic] TRACE ballistic_{case} {t} {y:.6f} 0.000000\n' for t, y in enumerate(values)]
                rows.append(f'[ballistic] END ballistic_{case} peak {max(values):.6f} landing {0 if case in ("zero_gravity", "scripted") else rate}\n')
                rows.append(f'[scenario] PASS ballistic_{case} at tick 999\n')
                self.path(case, rate).write_text(''.join(rows))

    def path(self, case='fall', rate=60):
        return self.directory / f'scenario_ballistic_{case}_{rate}.log'

    def test_equal_reference_trajectories_pass(self):
        self.assertEqual(compare(self.directory), [])

    def test_height_divergence_fails(self):
        p = self.path(); p.write_text(p.read_text().replace(' 193.333333 ', ' 195.333333 '))
        self.assertTrue(compare(self.directory))

    def test_subtick_peak_divergence_fails(self):
        p = self.path('zero_gravity'); s = p.read_text().replace(' 205.000000 ', ' 212.000000 ').replace('peak 210.000000', 'peak 212.000000'); p.write_text(s)
        self.assertRegex(' '.join(compare(self.directory)), 'peak')

    def test_missing_pass_or_samples_fails(self):
        p = self.path(); p.write_text(p.read_text().replace('[scenario] PASS', '[scenario] FAIL'))
        with self.assertRaisesRegex(ValueError, 'missing PASS'): compare(self.directory)

    def test_duplicate_tick_fails(self):
        p = self.path(); s = p.read_text(); p.write_text(s.replace('fall 1 ', 'fall 0 '))
        with self.assertRaisesRegex(ValueError, 'ordered'): collect(p, 'fall', 60)


if __name__ == '__main__': unittest.main()
