# Working in this fork

This fork (`knakamura13/native-macos-pikmin-1`) tracks the upstream project `SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port`. The upstream remote is named `upstream`. The fork remote is `origin`.

## Branches

- `main` mirrors `upstream/main`. Change it only by fast-forward. Never commit to it.
- `kyle-patches` is a small linear stack of fork-only commits on top of `main`: the macOS app bundle, fork notes, and anything else that is not upstream and not in an open upstream PR. Commit fork-only work here.
- `kyle-develop` is generated. A rebuild starts from `upstream/main`, cherry-picks `kyle-patches`, then merges every open upstream PR whose head repository is this fork. The result is force-pushed. Never commit to `kyle-develop` directly. A local checkout updates with:

  ```sh
  git fetch origin
  git switch kyle-develop
  git reset --hard origin/kyle-develop
  ```

- `fix/<name>` holds one upstream-bound change. It branches from `main`. Its PR goes to upstream. To fix an in-flight PR, commit on that PR's own branch. The next rebuild picks the new head up. Do not merge the fix into `kyle-patches` or `kyle-develop`.

`tools/kyle-develop/exclude.txt` lists PR head branches to skip. `tools/kyle-develop/include.txt` lists extra fork branches to merge after those PRs. Both files live on `kyle-patches`. A `#` line is a comment.

## Rebuild

From a checkout of the latest `kyle-patches` (the script on `kyle-develop` is the previous rebuild's copy):

```sh
git fetch origin kyle-patches
git switch --detach origin/kyle-patches
tools/kyle-develop/rebuild.sh
```

`--dry-run` does the same work, including the test suites, and does not push.

The script reads the public GitHub API with no token. It needs `origin` (this fork, with push access) and `upstream` (added if missing). It builds and runs the Pikmin 1 and Pikmin 2 test suites from each game's README (`cmake --build`, then `ctest`). A conflict aborts that one step, records the files, and continues. If the tests fail, the script rebuilds without the step that introduced the failure and leaves that step out. It does not push a result whose tests fail.

The last commit on `kyle-develop` is empty. Its message is the rebuild marker. A scheduled job reads it with `git log -1 --format=%B origin/kyle-develop` and rebuilds only when an input SHA below no longer matches the public API.

### Marker format

The first line is exactly `kyle-develop-rebuild-v1`. The second line is blank. Every later line is one record, in this order:

```
upstream_main <40-hex>
kyle_patches <40-hex>
merged_pr <number> <40-hex>
merged_include <branch> <40-hex>
skipped_pr <number> <40-hex> <reason>
skipped_include <branch> <40-hex-or-none> <reason>
skipped_patch <40-hex> <reason>
```

- `upstream_main` is `upstream/main` at build time.
- `kyle_patches` is the tip of `origin/kyle-patches` that was cherry-picked.
- `merged_pr` is an open upstream PR whose head repository is this fork, in ascending PR number, with the head SHA that was merged. Drafts are included. A PR whose commits were already contained is still listed.
- `merged_include` is a branch from `include.txt`. The rebuild merges these in file order, after the PRs. The marker sorts the records by branch name.
- `skipped_pr` is an open fork PR that was not merged. The SHA is the head that was considered. The reason is the rest of the line (`excluded`, `conflict: path path`, `tests failed when this step was included`, or a fetch error).
- `skipped_include` and `skipped_patch` are the same idea for include-file branches and for individual `kyle-patches` commits.
- SHAs are full 40 hex digits. `none` is used only when a skipped include branch could not be resolved. Reasons are a single line.

Compare, and rebuild when any of these differ:

1. `upstream_main` against upstream `main`.
2. `kyle_patches` against `origin/kyle-patches`.
3. The set of open upstream PRs whose head repository is `knakamura13/native-macos-pikmin-1` (number and head SHA, drafts included) against the `merged_pr` and `skipped_pr` lines together.
4. Each branch named in `include.txt` on `origin/kyle-patches` against the `merged_include` and `skipped_include` line for that branch.

`exclude.txt` is covered by (2): it lives on `kyle-patches`. A closed PR drops out of (3), so the next run starts from upstream, which now contains it.

The script prints one JSON object on stdout as its last output, with the same SHAs plus `tests`, `pushed`, `dry_run`, and `kyle_develop` (the new tip, or null when nothing was pushed).

## What goes upstream

Goal: a Mac Pikmin 1 that matches GameCube as closely as possible. Classify each change first:

1. GameCube fidelity: original logic, content, progression, timing, and unlocks. Original bugs stay, unless a mouse or keyboard feature cannot work without the fix.
2. Port bugfixes: fixes for bugs the port introduced. Upstream PR.
3. Input features: features that exist because of the mouse or keyboard, such as cursor-aimed throw. Upstream PR, active only while that device drives the game. Controller players keep GameCube behavior.
4. Upstream-introduced features (Whistle Pluck, Infinite Day, No Day Limit, Hide Olimar's Messages): fix, support, and enhance them upstream. They have no input gate. Removing one stays fork-only, on `kyle-patches`.
5. Fork-only additions: `kyle-patches` only.

Bugs:

- A bug the upstream introduced: fix it, and open the upstream PR from a `fix/<name>` branch cut from `main` (synced to `upstream/main`).
- A platform-agnostic fix covers every platform in one change. Before claiming it works on all platforms, compile it on Windows and Linux.
- A macOS-only fix that certainly also affects Linux or Windows: open the upstream PR first, then one upstream issue per affected OS. Each issue links the PR and claims no fix for its OS.

## Sync `main` with upstream

```sh
git fetch upstream
git switch main
git merge --ff-only upstream/main
git push origin main
```

## Start an upstream fix

```sh
git switch main
git switch -c fix/<name>
```

Keep the change minimal. Do not include `kyle-patches` work. Open the PR upstream with head `knakamura13:fix/<name>`.

## Personal changes

Commit them on `kyle-patches`, on top of current `main`. Prefer new files, or code behind a build flag that is off by default. Avoid edits to shared files such as `pc_window.cpp` and `pc_settings.cpp` where you can, because upstream changes them often and an in-flight PR may touch them too. If a `kyle-patches` commit conflicts during the rebuild, that commit is skipped until it is retargeted onto current `main`.

## Checks

- macOS app: `games/pikmin1/packaging/macos/build-app.sh`. Add `--install` to replace `/Applications/Pikmin 1.app`. Quit the game first.
- The rebuild runs the Pikmin 1 and Pikmin 2 `ctest` suites before it pushes.
- Windows and Linux builds of an upstream PR are not run on this Mac unless the rebuild host is Linux. Before opening an upstream PR, confirm that the change compiles there, or say in the PR that it was not built.
