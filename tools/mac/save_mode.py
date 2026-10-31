#!/usr/bin/env python3
"""Resolve Mac Open Nectar save folders, migrate legacy saves, and guard the central save."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Sequence

MIGRATION_MARKER = '.open-nectar-central-save-v1'
SUPPORT_FOLDER_NAME = 'Open Nectar'
NAMED_SAVES_DIRNAME = 'named-saves'
BACKUP_STATE_NAME = 'central-save-backup-by-build.json'
BACKUP_KEEP = 5

SAVE_MODES = ('central', 'fresh', 'sandbox', 'named', 'readonly')

PIKMIN_CARD_CONFLICT_FILES = ('Pikmin dataFile', 'Pikmin2_SaveData')


@dataclass(frozen=True)
class SaveResolution:
    mode: str
    save_dir: Path
    read_only: bool
    uses_central: bool
    mode_label: str
    title_suffix: str
    needs_fresh_wrapper: bool
    sandbox_source: Path | None = None
    save_name: str | None = None


def support_root(home: Path) -> Path:
    # Kyle's Mac apps use Application Support; Linux CI uses the same layout under fake HOME.
    return home / 'Library' / 'Application Support' / SUPPORT_FOLDER_NAME


def central_save_dir(home: Path) -> Path:
    return support_root(home) / 'save'


def migration_marker_path(home: Path) -> Path:
    return support_root(home) / MIGRATION_MARKER


def backup_state_path(home: Path) -> Path:
    config = os.environ.get('XDG_CONFIG_HOME')
    base = Path(config).expanduser() if config else home / '.config'
    return base / 'open-nectar' / BACKUP_STATE_NAME


def pikmin2_install_save(home: Path, config_home: Path | None = None) -> Path | None:
    base = config_home if config_home is not None else (
        Path(os.environ['XDG_CONFIG_HOME']).expanduser()
        if os.environ.get('XDG_CONFIG_HOME') else home / '.config'
    )
    file_path = base / 'open-nectar' / 'pikmin2_dir'
    if not file_path.is_file():
        return None
    line = file_path.read_text(encoding='utf-8', errors='replace').splitlines()
    if not line:
        return None
    install = line[0].strip()
    if not install.startswith('/'):
        return None
    save = Path(install) / 'save'
    return save if save.is_dir() else None


def legacy_save_sources(home: Path, config_home: Path | None = None) -> list[Path]:
    sources: list[Path] = []
    for candidate in (
        home / '.local' / 'share' / 'pikmin-native' / 'save',
        home / '.local' / 'share' / 'pikmin-native-upstream' / 'save',
    ):
        if candidate.is_dir():
            sources.append(candidate)
    p2 = pikmin2_install_save(home, config_home)
    if p2 is not None:
        sources.append(p2)
    return sources


def _relative_files(root: Path) -> set[str]:
    if not root.is_dir():
        return set()
    result: set[str] = set()
    for path in root.rglob('*'):
        if path.is_file() or path.is_symlink():
            result.add(str(path.relative_to(root)).replace('\\', '/'))
    return result


def _newest_mtime(root: Path) -> float:
    if not root.is_dir():
        return 0.0
    newest = 0.0
    for path in root.rglob('*'):
        if path.is_file():
            try:
                newest = max(newest, path.stat().st_mtime)
            except OSError:
                pass
    return newest


def _copy_tree(src: Path, dest: Path) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    for item in src.rglob('*'):
        rel = item.relative_to(src)
        target = dest / rel
        if item.is_dir():
            target.mkdir(parents=True, exist_ok=True)
        elif item.is_file() or item.is_symlink():
            target.parent.mkdir(parents=True, exist_ok=True)
            if item.is_symlink():
                if target.exists() or target.is_symlink():
                    target.unlink()
                target.symlink_to(os.readlink(item))
            else:
                shutil.copy2(item, target)


def _backup_dir(home: Path, label: str) -> Path:
    archive = home / 'OpenNectar-archive.noindex' / 'save-migration-backups'
    archive.mkdir(parents=True, exist_ok=True)
    stamp = time.strftime('%Y%m%dT%H%M%SZ', time.gmtime())
    dest = archive / f'{stamp}-{label}'
    dest.mkdir(parents=True, exist_ok=False)
    return dest


def _file_mtime(path: Path) -> float:
    if not path.is_file():
        return 0.0
    try:
        return path.stat().st_mtime
    except OSError:
        return 0.0


def _copy_card_file(src_card: Path, dest_card: Path, name: str) -> None:
    src = src_card / name
    if not src.is_file():
        return
    dest_card.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dest_card / name)
    meta = src_card / f'.meta_{name}'
    if meta.is_file():
        shutil.copy2(meta, dest_card / meta.name)


def merge_pikmin2_cards(central: Path, p2_save: Path, home: Path) -> None:
    central_card = central / 'card0'
    p2_card = p2_save / 'card0'
    if not p2_card.is_dir():
        return
    if not central_card.is_dir():
        _copy_tree(p2_save, central)
        return

    backup_root: Path | None = None

    def backup_losing_file(name: str, loser: Path) -> None:
        nonlocal backup_root
        if backup_root is None:
            backup_root = _backup_dir(home, 'pikmin2-conflict') / 'save' / 'card0'
            backup_root.mkdir(parents=True, exist_ok=True)
        _copy_card_file(loser.parent, backup_root, name)

    for name in PIKMIN_CARD_CONFLICT_FILES:
        central_file = central_card / name
        p2_file = p2_card / name
        if not p2_file.is_file():
            continue
        if not central_file.is_file():
            _copy_card_file(p2_card, central_card, name)
            continue
        central_mtime = _file_mtime(central_file)
        p2_mtime = _file_mtime(p2_file)
        if p2_mtime <= central_mtime:
            continue
        backup_losing_file(name, central_file)
        _copy_card_file(p2_card, central_card, name)

    for rel in sorted(_relative_files(p2_save)):
        if rel.startswith('card0/'):
            tail = rel[len('card0/'):]
            if tail.split('/')[0] in PIKMIN_CARD_CONFLICT_FILES or tail in PIKMIN_CARD_CONFLICT_FILES:
                continue
        target = central / rel
        if target.exists():
            continue
        source_file = p2_save / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        if source_file.is_symlink():
            target.symlink_to(os.readlink(source_file))
        else:
            shutil.copy2(source_file, target)


def copy_merge_sources(home: Path, dest: Path, *, config_home: Path | None = None) -> None:
    dest.mkdir(parents=True, exist_ok=True)
    p2_source = pikmin2_install_save(home, config_home)
    sources = legacy_save_sources(home, config_home)
    ordinary = [s for s in sources if p2_source is None or s.resolve() != p2_source.resolve()]
    for src in ordinary:
        if src.resolve() == dest.resolve():
            continue
        for rel in sorted(_relative_files(src)):
            target = dest / rel
            if target.exists():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source_file = src / rel
            if source_file.is_symlink():
                target.symlink_to(os.readlink(source_file))
            else:
                shutil.copy2(source_file, target)
    if p2_source is not None:
        merge_pikmin2_cards(dest, p2_source, home)


def migrate_central_save(home: Path, *, dry_run: bool = False, config_home: Path | None = None) -> bool:
    """Copy legacy saves into the central folder once. Returns True if migration ran or was already done."""
    marker = migration_marker_path(home)
    dest = central_save_dir(home)
    if marker.is_file():
        return True
    sources = legacy_save_sources(home, config_home)
    if not sources and not dest.is_dir():
        if dry_run:
            return False
        support_root(home).mkdir(parents=True, exist_ok=True)
        dest.mkdir(parents=True, exist_ok=True)
        marker.write_text('migrated\n', encoding='utf-8')
        return True
    if dry_run:
        return bool(sources)
    support_root(home).mkdir(parents=True, exist_ok=True)
    copy_merge_sources(home, dest, config_home=config_home)
    dest.mkdir(parents=True, exist_ok=True)
    marker.write_text('migrated\n', encoding='utf-8')
    return True


def hash_save_tree(root: Path) -> str:
    if not root.exists():
        return hashlib.sha256(b'empty').hexdigest()
    digest = hashlib.sha256()
    for path in sorted(root.rglob('*'), key=lambda p: str(p).replace('\\', '/')):
        if path.is_file():
            rel = str(path.relative_to(root)).replace('\\', '/').encode()
            digest.update(rel)
            digest.update(path.read_bytes())
    return digest.hexdigest()


def automation_or_cheat_forces_throwaway(env: dict[str, str], cmake_cache: str | None = None) -> bool:
    env_keys = (
        'PIKMIN_REPLAY_TEST', 'PIKMIN_NETPLAY_TEST_KNOBS', 'PIKMIN_TEST_AUTOMATION',
        'PIKMIN_SAVE_FORCE_THROWAWAY', 'OPEN_NECTAR_TEST_AUTOMATION',
    )
    if any(env.get(key, '').strip() in ('1', 'true', 'TRUE', 'yes', 'YES') for key in env_keys):
        return True
    if env.get('PIKMIN_SAVE_ALLOW_CENTRAL_WITH_CHEATS', '').strip() in ('1', 'true', 'TRUE'):
        return False
    if cmake_cache:
        cheat_markers = (
            'PIKMIN_ENABLE_CHEATS:BOOL=ON', 'PIKMIN_TEST_AUTOMATION:BOOL=ON',
            'PIKMIN_CHEAT_BUILD:BOOL=ON',
        )
        if any(marker in cmake_cache for marker in cheat_markers):
            return True
    return False


def _mode_label(mode: str, *, read_only: bool = False, save_name: str | None = None) -> str:
    if read_only or mode == 'readonly':
        return 'read-only central save'
    labels = {
        'central': 'central save',
        'fresh': 'fresh save',
        'sandbox': 'sandbox save',
        'named': f'named save ({save_name})' if save_name else 'named save',
    }
    return labels.get(mode, mode)


def choose_save_menu(default_mode: str = 'central') -> str:
    options = [
        ('central', 'Shared central save (default)'),
        ('fresh', 'Fresh throwaway save every launch'),
        ('sandbox', 'Sandbox copy of central (never writes back)'),
        ('named', 'Named save folder (reused)'),
        ('readonly', 'Read-only central save'),
    ]
    print('Choose a save mode:')
    for index, (mode, label) in enumerate(options, start=1):
        mark = ' (default)' if mode == default_mode else ''
        print(f'  {index}. {label}{mark}')
    while True:
        try:
            line = input('> ').strip()
        except EOFError:
            return default_mode
        if not line:
            return default_mode
        if line.isdigit():
            idx = int(line)
            if 1 <= idx <= len(options):
                return options[idx - 1][0]
        for mode, _ in options:
            if line == mode:
                return mode
        print('Enter a number or mode name.')


def resolve_save_mode(
    *,
    home: Path,
    args: argparse.Namespace,
    env: dict[str, str],
    build_output: Path | None,
    cmake_cache: str | None,
    interactive: bool,
) -> SaveResolution:
    migrate_central_save(home)

    mode = 'central'
    save_name = args.save_name or env.get('PIKMIN_SAVE_NAME', '').strip() or None
    custom_dir = args.save_dir or env.get('PIKMIN_SAVE_DIR', '').strip() or None
    read_only = bool(args.read_only_save or env.get('PIKMIN_SAVE_READ_ONLY', '').strip() in ('1', 'true', 'TRUE'))

    selected = sum(1 for item in (
        args.fresh_save, args.sandbox_save, bool(save_name), bool(custom_dir), read_only, args.choose_save,
    ) if item)
    if selected > 1:
        raise SystemExit('Pick at most one save mode flag.')

    if args.choose_save:
        if not interactive:
            raise SystemExit('--choose-save requires an interactive terminal.')
        mode = choose_save_menu()
        read_only = mode == 'readonly'
    elif args.fresh_save:
        mode = 'fresh'
    elif args.sandbox_save:
        mode = 'sandbox'
    elif custom_dir:
        mode = 'named'
    elif save_name:
        mode = 'named'
    elif read_only:
        mode = 'readonly'

    force_throwaway = automation_or_cheat_forces_throwaway(env, cmake_cache)
    if force_throwaway and mode in ('central', 'readonly', 'named'):
        mode = 'sandbox' if args.sandbox_save or env.get('PIKMIN_SAVE_SANDBOX_ON_AUTOMATION', '1') != '0' else 'fresh'
        read_only = False

    central = central_save_dir(home)
    if mode == 'central':
        save_dir = central
        uses_central = True
        needs_wrapper = False
    elif mode == 'readonly':
        save_dir = central
        uses_central = True
        read_only = True
        needs_wrapper = False
    elif mode == 'fresh':
        save_dir = build_output / 'fresh-save' if build_output else home / '.cache' / 'open-nectar-fresh'
        uses_central = False
        needs_wrapper = True
    elif mode == 'sandbox':
        sandbox_root = (build_output / 'sandbox-save') if build_output else (home / 'OpenNectar-sandbox.noindex' / 'save')
        if sandbox_root.exists():
            shutil.rmtree(sandbox_root)
        sandbox_root.parent.mkdir(parents=True, exist_ok=True)
        if central.is_dir():
            _copy_tree(central, sandbox_root)
        else:
            sandbox_root.mkdir(parents=True, exist_ok=True)
            (sandbox_root / 'card0').mkdir(exist_ok=True)
        save_dir = sandbox_root
        uses_central = False
        needs_wrapper = False
    else:  # named
        if custom_dir:
            save_dir = Path(custom_dir).expanduser()
            if not save_dir.is_absolute():
                raise SystemExit('--save-dir must be absolute.')
        else:
            slug = re.sub(r'[^A-Za-z0-9._-]+', '-', save_name or 'save').strip('-') or 'save'
            save_dir = support_root(home) / NAMED_SAVES_DIRNAME / slug
        save_dir.mkdir(parents=True, exist_ok=True)
        uses_central = save_dir.resolve() == central.resolve()
        needs_wrapper = False

    label = _mode_label(mode, read_only=read_only, save_name=save_name)
    title_suffix = label
    return SaveResolution(
        mode='readonly' if read_only and mode != 'fresh' else mode,
        save_dir=save_dir,
        read_only=read_only,
        uses_central=uses_central,
        mode_label=label,
        title_suffix=title_suffix,
        needs_fresh_wrapper=needs_wrapper,
        sandbox_source=central if mode == 'sandbox' else None,
        save_name=save_name,
    )


def append_save_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument('--fresh-save', action='store_true')
    parser.add_argument('--sandbox-save', action='store_true')
    parser.add_argument('--read-only-save', action='store_true')
    parser.add_argument('--save-name', metavar='NAME')
    parser.add_argument('--save-dir', metavar='PATH')
    parser.add_argument('--choose-save', action='store_true')
    parser.add_argument('--save-self-test', action='store_true',
                        help='Run central-save hash guard checks and exit.')


def emit_shell_kv(resolution: SaveResolution, *, home: Path) -> str:
    values = {
        'PIKMIN_SAVE_MODE': resolution.mode,
        'PIKMIN_SAVE_MODE_LABEL': resolution.mode_label,
        'NECTAR_SAVE_DIR': str(resolution.save_dir),
        'PIKMIN_SAVE_USES_CENTRAL': '1' if resolution.uses_central else '0',
        'PIKMIN_SAVE_NEEDS_FRESH_WRAPPER': '1' if resolution.needs_fresh_wrapper else '0',
        'NECTAR_SAVE_READ_ONLY': '1' if resolution.read_only else '0',
        'OPEN_NECTAR_CENTRAL_SAVE_DIR': str(central_save_dir(home)),
    }
    return '\n'.join(f'export {key}={shlex.quote(val)}' for key, val in values.items())


def _card_backup_root(card0: Path) -> Path:
    return card0.parent / 'card0-backups'


def _backup_stamp() -> str:
    return time.strftime('%Y%m%d-%H%M%S', time.gmtime())


def snapshot_card0(card0: Path) -> Path | None:
    if not card0.is_dir():
        return None
    files = [p for p in card0.rglob('*') if p.is_file()]
    if not files:
        return None
    root = _card_backup_root(card0)
    root.mkdir(parents=True, exist_ok=True)
    stamp = _backup_stamp()
    dest = root / stamp
    suffix = 0
    while dest.exists():
        suffix += 1
        dest = root / f'{stamp}-{suffix:02d}'
    dest.mkdir(parents=True)
    for item in files:
        rel = item.relative_to(card0)
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, target)
    backups = sorted([p for p in root.iterdir() if p.is_dir()], key=lambda p: p.name, reverse=True)
    for old in backups[BACKUP_KEEP:]:
        shutil.rmtree(old, ignore_errors=True)
    return dest


def maybe_backup_on_first_launch(home: Path, build_info: Path) -> bool:
    try:
        info = json.loads(build_info.read_text(encoding='utf-8'))
    except (OSError, ValueError, json.JSONDecodeError):
        return False
    mode = info.get('save_mode')
    if mode != 'central':
        return False
    sha = info.get('sha')
    if not isinstance(sha, str) or not sha:
        return False
    save_dir = info.get('configured_save_directory')
    if not isinstance(save_dir, str) or not save_dir:
        return False
    state_path = backup_state_path(home)
    state_path.parent.mkdir(parents=True, exist_ok=True)
    state: dict[str, str] = {}
    if state_path.is_file():
        try:
            loaded = json.loads(state_path.read_text(encoding='utf-8'))
            if isinstance(loaded, dict):
                state = {str(k): str(v) for k, v in loaded.items()}
        except json.JSONDecodeError:
            state = {}
    key = f'{save_dir}|{sha}'
    if state.get('last') == key:
        return False
    card0 = Path(save_dir) / 'card0'
    snapshot_card0(card0)
    state['last'] = key
    state_path.write_text(json.dumps(state, indent=2) + '\n', encoding='utf-8')
    return True


def run_save_self_test(home: Path, runners: Iterable[tuple[str, Callable[[], None]]]) -> None:
    central = central_save_dir(home)
    central.mkdir(parents=True, exist_ok=True)
    probe = central / 'card0' / 'self-test.gci'
    probe.parent.mkdir(parents=True, exist_ok=True)
    probe.write_text('probe', encoding='utf-8')
    before = hash_save_tree(central)
    for label, fn in runners:
        fn()
        after = hash_save_tree(central)
        if before != after:
            raise SystemExit(f'central save folder changed during {label}')
    probe.unlink(missing_ok=True)


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='command')

    resolve = sub.add_parser('resolve')
    append_save_args(resolve)
    resolve.add_argument('--home', type=Path, default=Path(os.environ.get('HOME', '.')))
    resolve.add_argument('--build-output', type=Path)
    resolve.add_argument('--cmake-cache', type=Path)
    resolve.add_argument('--non-interactive', action='store_true')

    migrate = sub.add_parser('migrate')
    migrate.add_argument('--home', type=Path, default=Path(os.environ.get('HOME', '.')))
    migrate.add_argument('--dry-run', action='store_true')

    hash_cmd = sub.add_parser('hash-tree')
    hash_cmd.add_argument('path', type=Path)

    backup = sub.add_parser('backup-if-needed')
    backup.add_argument('--home', type=Path, default=Path(os.environ.get('HOME', '.')))
    backup.add_argument('--build-info', type=Path, required=True)

    args = parser.parse_args(argv)
    if args.command == 'migrate':
        migrate_central_save(args.home, dry_run=args.dry_run)
        return 0
    if args.command == 'hash-tree':
        print(hash_save_tree(args.path))
        return 0
    if args.command == 'backup-if-needed':
        maybe_backup_on_first_launch(args.home, args.build_info)
        return 0
    if args.command == 'resolve':
        cache_text = args.cmake_cache.read_text(errors='replace') if args.cmake_cache and args.cmake_cache.is_file() else None
        resolution = resolve_save_mode(
            home=args.home,
            args=args,
            env=dict(os.environ),
            build_output=args.build_output,
            cmake_cache=cache_text,
            interactive=not args.non_interactive and sys.stdin.isatty(),
        )
        print(emit_shell_kv(resolution, home=args.home))
        return 0
    parser.print_help()
    return 2


if __name__ == '__main__':
    raise SystemExit(main())
