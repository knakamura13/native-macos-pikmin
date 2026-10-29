#!/usr/bin/env python3
"""Create build metadata; absent provenance is represented by null."""
import argparse
import hashlib
import json
import pathlib
import subprocess
from datetime import datetime, timezone


def parse_marker(message):
    lines = message.splitlines()
    if not lines or lines[0] != 'kyle-develop-rebuild-v1':
        return None
    result = {'upstream_main': None, 'kyle_patches': None, 'merged_prs': [], 'merged_includes': [], 'skipped': []}
    for line in lines[1:]:
        parts = line.split(maxsplit=3)
        if len(parts) < 2:
            continue
        kind = parts[0]
        if kind in ('upstream_main', 'kyle_patches'):
            result[kind] = parts[1]
        elif kind == 'merged_pr' and len(parts) >= 3 and parts[1].isdigit():
            result['merged_prs'].append({'number': int(parts[1]), 'sha': parts[2]})
        elif kind == 'merged_include' and len(parts) >= 3:
            result['merged_includes'].append({'branch': parts[1], 'sha': parts[2]})
        elif kind in ('skipped_pr', 'skipped_include', 'skipped_patch') and len(parts) >= 3:
            if kind == 'skipped_patch':
                result['skipped'].append({'kind': kind, 'sha': parts[1], 'reason': ' '.join(parts[2:])})
            else:
                result['skipped'].append({'kind': kind, 'id': parts[1], 'sha': parts[2], 'reason': parts[3] if len(parts) > 3 else ''})
    return result


def parse_cache(text):
    names = ('OPEN_NECTAR_VERSION', 'CMAKE_BUILD_TYPE', 'PIKMIN_NETPLAY_BUILD', 'PIKMIN_NATIVE_OPTIMIZE', 'PIKMIN_NATIVE_JAUDIO', 'NECTAR_ONLINE')
    result = {name: None for name in names}
    for line in text.splitlines():
        if line.startswith(('#', '//')) or '=' not in line or ':' not in line:
            continue
        key, value = line.split('=', 1)
        name = key.split(':', 1)[0]
        if name in result:
            result[name] = value
    return result


def hash_file(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def source_versions(repo):
    try:
        versions = json.loads((pathlib.Path(repo) / 'versions.json').read_text())
        if not isinstance(versions, dict):
            return None
        return {key: versions.get(key) if isinstance(versions.get(key), str) else None
                for key in ('release', 'launcher', 'pikmin1', 'pikmin2')}
    except (OSError, ValueError):
        return None


def git(repo, *args):
    result = subprocess.run(['git', '-C', str(repo), *args], text=True, capture_output=True)
    return result.stdout.strip() if result.returncode == 0 else None


def main():
    p = argparse.ArgumentParser()
    for arg in ('repo', 'output', 'game', 'launcher', 'data', 'role'):
        p.add_argument('--' + arg, required=True)
    p.add_argument('--cache')
    p.add_argument('--launcher-overlay')
    p.add_argument('--window-overlay')
    p.add_argument('--window-title-header')
    a = p.parse_args()
    sha = git(a.repo, 'rev-parse', 'HEAD') or 'unknown'
    branch = git(a.repo, 'symbolic-ref', '--quiet', '--short', 'HEAD') or ''
    status = git(a.repo, 'status', '--porcelain', '--untracked-files=all')
    marker = parse_marker(git(a.repo, 'log', '-1', '--format=%B') or '')
    cache = pathlib.Path(a.cache) if a.cache else None
    options = parse_cache(cache.read_text(errors='replace')) if cache and cache.is_file() else None
    role = 'develop' if a.role == 'unknown' and marker else a.role
    obj = {'sha': sha, 'branch': branch, 'role': role, 'dirty': None if status is None else bool(status),
           'open_nectar_configured_version': options.get('OPEN_NECTAR_VERSION') if options else None,
           'source_versions': source_versions(a.repo),
           'launcher_overlay_sha256': hash_file(a.launcher_overlay) if a.launcher_overlay else None,
           'window_overlay_sha256': hash_file(a.window_overlay) if a.window_overlay else None,
           'window_title_header_sha256': hash_file(a.window_title_header) if a.window_title_header else None,
           'integration': marker, 'build_options': options, 'configured_data_directory': a.data,
           'built_at_utc': datetime.now(timezone.utc).isoformat(),
           'binaries': {'nectar': hash_file(a.game), 'nectar-launcher': hash_file(a.launcher)}}
    pathlib.Path(a.output).write_text(json.dumps(obj, indent=2) + '\n')

if __name__ == '__main__':
    main()
