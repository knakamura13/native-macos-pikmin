import hashlib
import importlib.util
import pathlib
import json
import subprocess
import sys
import tempfile
import unittest

path = pathlib.Path(__file__).resolve().parents[1] / 'build_identity.py'
spec = importlib.util.spec_from_file_location('build_identity', path)
identity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(identity)

class IdentityTests(unittest.TestCase):
    def test_marker_keeps_inputs_and_skip_reason(self):
        marker = 'kyle-develop-rebuild-v1\n\nupstream_main abc\nkyle_patches def\nmerged_pr 205 123\nskipped_pr 212 456 tests failed\n'
        result = identity.parse_marker(marker)
        self.assertEqual(result['upstream_main'], 'abc')
        self.assertEqual(result['merged_prs'], [{'number': 205, 'sha': '123'}])
        self.assertEqual(result['skipped'][0]['reason'], 'tests failed')

    def test_unrecognized_marker_is_unknown(self):
        self.assertIsNone(identity.parse_marker('ordinary commit message'))

    def test_cache_options_are_explicit(self):
        text = 'OPEN_NECTAR_VERSION:STRING=1.0.9\nCMAKE_BUILD_TYPE:STRING=Release\nPIKMIN_NETPLAY_BUILD:BOOL=OFF\nPIKMIN_NATIVE_OPTIMIZE:BOOL=ON\n'
        self.assertEqual(identity.parse_cache(text)['OPEN_NECTAR_VERSION'], '1.0.9')
        self.assertEqual(identity.parse_cache(text)['PIKMIN_NETPLAY_BUILD'], 'OFF')
        self.assertEqual(identity.parse_cache(text)['CMAKE_BUILD_TYPE'], 'Release')

    def test_hash_binary(self):
        self.assertEqual(identity.hash_file(__file__), hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest())

    def test_missing_or_invalid_source_versions_are_unknown(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(identity.source_versions(tmp))
            (pathlib.Path(tmp) / 'versions.json').write_text('{broken')
            self.assertIsNone(identity.source_versions(tmp))

    def test_custom_output_does_not_remove_existing_app(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = pathlib.Path(tmp) / 'output'
            app = output / 'Pikmin 1.app'
            app.mkdir(parents=True)
            sentinel = app / 'keep'
            sentinel.write_text('keep')
            script = path.parents[2] / 'games/pikmin1/packaging/macos/build-app.sh'
            result = subprocess.run(['bash', str(script), '--output-dir', str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Refusing to replace an existing output app', result.stderr)
            self.assertEqual(sentinel.read_text(), 'keep')

    def test_manifest_records_built_binaries_and_dirty_checkout(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = pathlib.Path(tmp) / 'repo'
            repo.mkdir()
            subprocess.run(['git', 'init', '-q', str(repo)], check=True)
            (repo / 'source').write_text('source')
            subprocess.run(['git', '-C', str(repo), 'add', 'source'], check=True)
            subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                            'commit', '-qm', 'fixture'], check=True)
            (repo / 'source').write_text('changed')
            (repo / 'versions.json').write_text('{"release":"1.1","launcher":"1.1","pikmin1":"1.1"}')
            game, launcher = pathlib.Path(tmp) / 'game', pathlib.Path(tmp) / 'launcher'
            game.write_bytes(b'game binary')
            launcher.write_bytes(b'launcher binary')
            cache = pathlib.Path(tmp) / 'CMakeCache.txt'
            cache.write_text('OPEN_NECTAR_VERSION:STRING=1.0.9\n')
            overlay = pathlib.Path(tmp) / 'launcher.patch'
            overlay.write_bytes(b'launcher overlay')
            window_overlay = pathlib.Path(tmp) / 'window.patch'
            window_overlay.write_bytes(b'window overlay')
            window_header = pathlib.Path(tmp) / 'title.h'
            window_header.write_bytes(b'title helper')
            output = pathlib.Path(tmp) / 'build-info.json'
            subprocess.run([sys.executable, str(path), '--repo', str(repo), '--output', str(output),
                            '--game', str(game), '--launcher', str(launcher), '--cache', str(cache),
                            '--data', '/test/data', '--role', 'feature', '--launcher-overlay', str(overlay),
                            '--window-overlay', str(window_overlay), '--window-title-header', str(window_header)], check=True)
            manifest = json.loads(output.read_text())
            self.assertTrue(manifest['dirty'])
            self.assertEqual(manifest['open_nectar_configured_version'], '1.0.9')
            self.assertEqual(manifest['source_versions']['release'], '1.1')
            self.assertEqual(manifest['configured_data_directory'], '/test/data')
            self.assertEqual(manifest['binaries']['nectar'], hashlib.sha256(b'game binary').hexdigest())
            self.assertEqual(manifest['binaries']['nectar-launcher'], hashlib.sha256(b'launcher binary').hexdigest())
            self.assertEqual(manifest['role'], 'feature')
            self.assertEqual(manifest['launcher_overlay_sha256'], hashlib.sha256(b'launcher overlay').hexdigest())
            self.assertEqual(manifest['window_overlay_sha256'], hashlib.sha256(b'window overlay').hexdigest())
            self.assertEqual(manifest['window_title_header_sha256'], hashlib.sha256(b'title helper').hexdigest())

if __name__ == '__main__': unittest.main()
