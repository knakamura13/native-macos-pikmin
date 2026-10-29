# macOS build identity

`build-info.json` records the source commit, role, working-tree state, source versions from `versions.json`, configured Open Nectar version from CMake, build options, configured data path, build time, and SHA-256 hashes of the game and launcher executables. The source and configured version can differ; neither proves that the build is an official release. Develop builds also record the inputs and skipped items from the rebuild marker. The configured data path is not necessarily the launcher's current data folder after a user changes it.

The launcher's Build information button shows and copies the manifest. If a manifest is missing or invalid, the view says so. Feature test apps have branch-specific bundle IDs and titles, with detached commits identified by SHA. Each test build stores its own binary copies alongside its manifest; it is published to the Desktop shortcut only after the build succeeds. The legacy Develop bundle keeps its existing ID; Upstream uses a separate ID. Managed bundles use their own executables even when assets live in a separately selected folder.

## Upstream-based build

Fetch `upstream/main`, then run the helper from a checkout containing these tools:

```sh
git fetch upstream main
tools/mac/build-upstream.sh \
  --output-dir /absolute/path/to/output \
  --build-dir /absolute/path/to/build-cache \
  --data-dir /absolute/path/to/isolated-data
```

The output directory must not already contain `Pikmin 1.app`; choose a fresh output directory for each build. The helper accepts `--ref` for an ancestor of the fetched upstream branch and `--check` to test overlay applicability without building. It builds from that upstream commit in a temporary worktree. It applies `upstream-launcher.patch` to the launcher for the build-information view and `upstream-window-title.patch` plus a small helper to label the game window. It also copies the fork's packaging tools into the worktree. Consequently this is an **upstream-based app with fork identity overlays**, not an unmodified upstream app. The manifest reports `dirty: true` and records SHA-256 hashes for both overlays and the title helper. The helper fails if the overlays do not apply or change other tracked source files.

For an isolated installer check, add `--install-dir /absolute/path/to/empty-test-directory`. The helper refuses to replace an existing app. Config, cache, and saves are scoped beneath the supplied data directory. Installing or migrating the real Upstream and Develop apps is separate work under KYL-60; this helper does not perform that migration.
