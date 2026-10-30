# macOS build identity

`build-info.json` records the source commit, role, working-tree state, source versions from `versions.json`, configured Open Nectar version from CMake, build options, configured data path, build time, and SHA-256 hashes of the game and launcher executables. The source and configured version can differ; neither proves that the build is an official release. Develop builds also record the inputs and skipped items from the rebuild marker. The configured data path is not necessarily the launcher's current data folder after a user changes it.

The launcher's Build information button shows and copies the manifest. If a manifest is missing or invalid, the view says so. Feature test apps have branch-specific bundle IDs and titles, with detached commits identified by SHA. Each test build stores its own binary copies alongside its manifest; it is published to the Desktop shortcut only after the build succeeds. Each role has its own name and bundle ID: Upstream is `Open Nectar.app` (`org.opennectar.upstream`), Develop is `Open Nectar Develop.app` (`org.opennectar.develop`), a local build without a role is `Open Nectar Local.app` (`org.opennectar.local`, never installed), and test builds are `Open Nectar Test.app` (`org.opennectar.testbuild.<hash>`). The old `org.opennectar.pikmin1` ID is only recognised so the legacy `/Applications/Pikmin 1.app` can be retired; see `AGENTS.md`. Managed bundles use their own executables even when assets live in a separately selected folder.

## Upstream-based build

One command fetches upstream `main` and builds it:

```sh
tools/mac/build-upstream.sh            # fetch and build
tools/mac/build-upstream.sh --install  # fetch, build, install /Applications/Open Nectar.app
tools/mac/build-upstream.sh --check    # do the overlays still apply?
```

It needs no arguments. It adds the `upstream` remote if it is missing, runs `git fetch upstream main`, and resets a fixed worktree (`~/OpenNectar-builds.noindex/upstream-source`) to that commit. It then applies `upstream-launcher.patch` (the build-information view) and `upstream-window-title.patch` plus `pc_build_window_title.h` (the window title), and copies in the fork's packaging files (`build-app.sh`, `controls.conf`, `build_identity.py`). No commits are cherry-picked, and the build fails if the overlays change any other tracked file. So this is an **upstream-based app with fork packaging**, not an unmodified upstream app.

The manifest says so plainly: `source_state` is `upstream + packaging patches` (not `dirty`), `packaging_patches` lists the patched and added files, and `packaging_fingerprint` hashes them together with the overlay files, so a change to the overlays forces a rebuild. Anything else changed in the worktree makes it `dirty` again.

Defaults live under `~/OpenNectar-builds.noindex` (Spotlight skips it): the build cache, the worktree, and `upstream-out/` (the newest three outputs are kept). Saves, settings and caches go to `~/.local/share/pikmin-native-upstream`, which starts empty; a data folder that is, contains or sits inside the real `~/.local/share/pikmin-native` is refused, also through symlinks. To carry real saves over, run `tools/mac/copy-saves-to-upstream.sh`: it copies them and never moves or changes the originals.

`--install` replaces `/Applications/Open Nectar.app` only if the app there has the upstream bundle ID and role. The old app is moved to `~/OpenNectar-archive.noindex/app-backups/upstream/` first (the newest five are kept) and restored if the new one fails its check. While the game is running, or another upstream build holds the lock, the command exits 75 and changes nothing. `--if-changed` skips the build when the installed app already has this upstream commit and the same packaging fingerprint; the hourly LaunchAgent (`auto-install-upstream.sh`, installed by `install-launchd.sh`) runs `build-upstream.sh --install --if-changed`. That agent uses the packaging tools from the checkout `install-launchd.sh` was run from.

Other options: `--no-fetch`, `--ref REF` (an ancestor of `upstream/main`), `--output-dir`, `--build-dir`, `--data-dir`, `--install-dir`.

## Default builds

Without `--output-dir`, `build-app.sh` replaces the app already in `packaging/macos/out.noindex/`. Older apps left in `packaging/macos/out/` (from before `.noindex`) are moved to `out.noindex/stale-out/` so Spotlight stops listing them.
