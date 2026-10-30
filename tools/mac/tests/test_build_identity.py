import hashlib
import importlib.util
import pathlib
import json
import os
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

    def test_marker_keeps_resolved_prs(self):
        marker = ('kyle-develop-rebuild-v1\n\nupstream_main abc\nmerged_pr 206 123\n'
                  'resolved_pr 206 123 both-sides: games/pikmin1/CMakeLists.txt AGENTS.md\n')
        result = identity.parse_marker(marker)
        self.assertEqual(result['resolved_prs'], [{'number': 206, 'sha': '123', 'resolution': 'both-sides',
                                                   'files': ['games/pikmin1/CMakeLists.txt', 'AGENTS.md']}])
        self.assertEqual(result['merged_prs'], [{'number': 206, 'sha': '123'}])

    def test_source_state(self):
        self.assertEqual(identity.source_state('', set()), (False, 'clean'))
        self.assertEqual(identity.source_state(None, set()), (None, None))
        status = ' M games/pikmin1/pc_port/pc_window.cpp\n?? tools/mac/build_identity.py\n'
        allowed = {'games/pikmin1/pc_port/pc_window.cpp', 'tools/mac/build_identity.py'}
        self.assertEqual(identity.source_state(status, allowed), (False, 'upstream + packaging patches'))
        self.assertEqual(identity.source_state(status + ' M games/pikmin1/src/main.cpp\n', allowed), (True, 'dirty'))
        self.assertEqual(identity.source_state(status, set()), (True, 'dirty'))

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
            app = output / 'Open Nectar Local.app'
            app.mkdir(parents=True)
            sentinel = app / 'keep'
            sentinel.write_text('keep')
            script = path.parents[2] / 'games/pikmin1/packaging/macos/build-app.sh'
            result = subprocess.run(['bash', str(script), '--output-dir', str(output)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Refusing to replace an existing output app', result.stderr)
            self.assertEqual(sentinel.read_text(), 'keep')

    def test_install_needs_a_role(self):
        script = path.parents[2] / 'games/pikmin1/packaging/macos/build-app.sh'
        with tempfile.TemporaryDirectory() as tmp:
            env = dict(os.environ, PIKMIN_BUILD_ROLE='unknown')
            result = subprocess.run(['bash', str(script), '--install', tmp], capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 2)
            self.assertIn('Refusing --install for a build with no role', result.stderr)

    def test_upstream_refuses_real_data_folder(self):
        script = path.parents[2] / 'games/pikmin1/packaging/macos/build-app.sh'
        with tempfile.TemporaryDirectory() as tmp:
            real = pathlib.Path(tmp) / '.local/share/pikmin-native'
            real.mkdir(parents=True)
            (pathlib.Path(tmp) / 'link').symlink_to(real)
            for data in (real, real / 'sub', pathlib.Path(tmp) / 'link'):
                env = dict(os.environ, HOME=tmp, PIKMIN_BUILD_ROLE='upstream', PIKMIN_DATA_ROOT=str(data))
                env.pop('XDG_DATA_HOME', None)
                result = subprocess.run(['bash', str(script)], capture_output=True, text=True, env=env)
                self.assertEqual(result.returncode, 1, data)
                self.assertIn('overlaps the real data folder', result.stderr)

    def test_build_app_self_test(self):
        script = path.parents[2] / 'games/pikmin1/packaging/macos/build-app.sh'
        result = subprocess.run(['bash', str(script), '--self-test'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

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
            self.assertEqual(manifest['source_state'], 'dirty')

    def test_upstream_with_only_packaging_patches_is_not_dirty(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = pathlib.Path(tmp) / 'repo'
            repo.mkdir()
            subprocess.run(['git', 'init', '-q', str(repo)], check=True)
            (repo / 'window.cpp').write_text('source')
            subprocess.run(['git', '-C', str(repo), 'add', 'window.cpp'], check=True)
            subprocess.run(['git', '-C', str(repo), '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                            'commit', '-qm', 'fixture'], check=True)
            (repo / 'window.cpp').write_text('patched')
            (repo / 'build-app.sh').write_text('copied')
            game, launcher = pathlib.Path(tmp) / 'game', pathlib.Path(tmp) / 'launcher'
            game.write_bytes(b'g')
            launcher.write_bytes(b'l')
            output = pathlib.Path(tmp) / 'build-info.json'
            base = [sys.executable, str(path), '--repo', str(repo), '--output', str(output), '--game', str(game),
                    '--launcher', str(launcher), '--data', '/d', '--role', 'upstream']
            subprocess.run(base + ['--allowed-change', 'window.cpp', '--allowed-change', 'build-app.sh'], check=True)
            manifest = json.loads(output.read_text())
            self.assertFalse(manifest['dirty'])
            self.assertEqual(manifest['source_state'], 'upstream + packaging patches')
            self.assertEqual(manifest['packaging_patches'], ['build-app.sh', 'window.cpp'])
            (repo / 'other.cpp').write_text('unexpected')
            subprocess.run(base + ['--allowed-change', 'window.cpp', '--allowed-change', 'build-app.sh'], check=True)
            manifest = json.loads(output.read_text())
            self.assertTrue(manifest['dirty'])
            self.assertEqual(manifest['source_state'], 'dirty')

if __name__ == '__main__': unittest.main()
