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
    result = {'upstream_main': None, 'kyle_patches': None, 'merged_prs': [], 'resolved_prs': [],
              'merged_includes': [], 'skipped': []}
    for line in lines[1:]:
        parts = line.split(maxsplit=3)
        if len(parts) < 2:
            continue
        kind = parts[0]
        if kind in ('upstream_main', 'kyle_patches'):
            result[kind] = parts[1]
        elif kind == 'merged_pr' and len(parts) >= 3 and parts[1].isdigit():
            result['merged_prs'].append({'number': int(parts[1]), 'sha': parts[2]})
        elif kind == 'resolved_pr' and len(parts) >= 3 and parts[1].isdigit():
            # resolved_pr <number> <sha> both-sides: <path> <path>...
            rest = parts[3] if len(parts) > 3 else ''
            label, _, files = rest.partition(':')
            result['resolved_prs'].append({'number': int(parts[1]), 'sha': parts[2],
                                           'resolution': label.strip() or None, 'files': files.split()})
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


def changed_paths(status):
    """Paths named by `git status --porcelain` output (renames give both sides)."""
    paths = []
    for line in status.splitlines():
        if len(line) < 4:
            continue
        entry = line[3:]
        for part in entry.split(' -> '):
            part = part.strip()
            if part.startswith('"') and part.endswith('"'):
                part = part[1:-1]
            paths.append(part)
    return paths


def source_state(status, allowed):
    """Classify the checkout: clean, packaging-patched upstream, dirty, or unknown.

    `allowed` lists the files the upstream build helper is expected to patch or
    copy in. A checkout whose only changes are those files is upstream plus
    packaging patches, not a dirty build."""
    if status is None:
        return None, None
    paths = changed_paths(status)
    if not paths:
        return False, 'clean'
    if allowed and all(path in allowed for path in paths):
        return False, 'upstream + packaging patches'
    return True, 'dirty'


def git(repo, *args, strip=True):
    result = subprocess.run(['git', '-C', str(repo), *args], text=True, capture_output=True)
    if result.returncode != 0:
        return None
    return result.stdout.strip() if strip else result.stdout


def main():
    p = argparse.ArgumentParser()
    for arg in ('repo', 'output', 'game', 'launcher', 'data', 'role'):
        p.add_argument('--' + arg, required=True)
    p.add_argument('--cache')
    p.add_argument('--launcher-overlay')
    p.add_argument('--window-overlay')
    p.add_argument('--window-title-header')
    p.add_argument('--allowed-change', action='append', default=[],
                   help='a path the packaging step is expected to patch or add; may repeat')
    p.add_argument('--packaging-fingerprint', help='sha256 over the packaging files an upstream build adds')
    p.add_argument('--save-mode')
    p.add_argument('--save-mode-label')
    p.add_argument('--save-directory')
    a = p.parse_args()
    sha = git(a.repo, 'rev-parse', 'HEAD') or 'unknown'
    branch = git(a.repo, 'symbolic-ref', '--quiet', '--short', 'HEAD') or ''
    # Porcelain lines start with a status column that may be a space; keep it.
    status = git(a.repo, 'status', '--porcelain', '--untracked-files=all', strip=False)
    marker = parse_marker(git(a.repo, 'log', '-1', '--format=%B') or '')
    cache = pathlib.Path(a.cache) if a.cache else None
    options = parse_cache(cache.read_text(errors='replace')) if cache and cache.is_file() else None
    role = 'develop' if a.role == 'unknown' and marker else a.role
    allowed = set(a.allowed_change) if role == 'upstream' else set()
    dirty, state = source_state(status, allowed)
    obj = {'sha': sha, 'branch': branch, 'role': role, 'dirty': dirty, 'source_state': state,
           'packaging_patches': sorted(set(changed_paths(status))) if state == 'upstream + packaging patches' else [],
           'packaging_fingerprint': a.packaging_fingerprint,
           'open_nectar_configured_version': options.get('OPEN_NECTAR_VERSION') if options else None,
           'source_versions': source_versions(a.repo),
           'launcher_overlay_sha256': hash_file(a.launcher_overlay) if a.launcher_overlay else None,
           'window_overlay_sha256': hash_file(a.window_overlay) if a.window_overlay else None,
           'window_title_header_sha256': hash_file(a.window_title_header) if a.window_title_header else None,
           'integration': marker, 'build_options': options, 'configured_data_directory': a.data,
           'save_mode': a.save_mode, 'save_mode_label': a.save_mode_label,
           'configured_save_directory': a.save_directory,
           'built_at_utc': datetime.now(timezone.utc).isoformat(),
           'binaries': {'nectar': hash_file(a.game), 'nectar-launcher': hash_file(a.launcher)}}
    pathlib.Path(a.output).write_text(json.dumps(obj, indent=2) + '\n')

if __name__ == '__main__':
    main()
