import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get('KYL59_TEST_LAUNCHER'), 'set KYL59_TEST_LAUNCHER to the built launcher')
class ManagedBundleTests(unittest.TestCase):
    def test_selected_assets_do_not_publish_or_launch_standalone_binaries(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            app = root / 'bundle/Contents/MacOS'
            app.mkdir(parents=True)
            launcher = app / 'nectar-launcher'
            shutil.copy2(os.environ['KYL59_TEST_LAUNCHER'], launcher)
            game = app / 'nectar'
            marker = root / 'launched-from'
            game.write_text('#!/bin/sh\npwd > "$KYL59_TEST_MARKER"\n')
            game.chmod(0o755)
            selected = root / 'chosen-assets'
            archives = selected / 'assets/dataDir/archives'
            archives.mkdir(parents=True)
            (selected / 'assets/.pikmin-assets').touch()
            parms = selected / 'assets/dataDir/parms'
            parms.mkdir()
            (parms / 'gamePrms.bin').touch()
            names = ('tekis bosses tekipara tekikey ufopartsbin bridges gates bomb rope water '
                     'fl_water pelletsbin pikihead effshapes weeds goal blo_eng').split()
            for name in names:
                for suffix in ('.arc', '.dir'):
                    (archives / (name + suffix)).touch()
            home = root / 'home'
            home.mkdir()
            env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME=str(root / 'config'),
                       XDG_DATA_HOME=str(root / 'xdg-data'), XDG_CACHE_HOME=str(root / 'cache'),
                       NECTAR_PIKMIN1_DIR=str(root / 'default-data'), NECTAR_PIKMIN1_EXECUTABLE=str(game),
                       NECTAR_SAVE_DIR=str(root / 'save'), PIKMIN_BUNDLE_MANAGED='1', KYL59_TEST_MARKER=str(marker))
            subprocess.run([str(launcher), '--data-dir', str(selected), '--extract-only'], env=env,
                           check=True, capture_output=True, timeout=20)
            remembered = root / 'config/open-nectar/install_dir'
            self.assertEqual(pathlib.Path(remembered.read_text().strip()), selected)
            self.assertFalse((selected / 'nectar').exists())
            self.assertFalse((selected / 'nectar-launcher').exists())
            self.assertFalse((home / 'Desktop/Open Nectar').exists())
            subprocess.run([str(launcher), '--data-dir', str(selected)], env=env,
                           check=True, capture_output=True, timeout=20)
            self.assertEqual(pathlib.Path(marker.read_text().strip()).resolve(), selected.resolve())
            standalone = selected / 'nectar-launcher'
            standalone.write_text('#!/bin/sh\nprintf unexpected > "$KYL59_TEST_MARKER"\n')
            standalone.chmod(0o755)
            marker.unlink()
            env['SDL_VIDEODRIVER'] = 'dummy'
            try:
                subprocess.run([str(launcher)], env=env, capture_output=True, timeout=3)
            except subprocess.TimeoutExpired:
                # A launcher waiting for interaction is expected in headless mode.
                pass
            self.assertFalse(marker.exists(), 'restart must not relaunch the standalone copy')
            self.assertTrue(standalone.read_text().startswith('#!/bin/sh'), 'restart must not replace the standalone copy')


if __name__ == '__main__':
    unittest.main()
