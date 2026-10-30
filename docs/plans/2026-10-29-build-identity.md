# Build Identity Implementation Plan

**Goal:** Provide truthful, reproducible identity for upstream, develop and feature macOS builds.

**Architecture:** Extend bundle metadata while preserving the existing `sha` field consumed by auto-install. Generate identity from the source checkout and the develop rebuild marker, with unknown values explicit. Expose the bundled metadata in the launcher. Installation and the data split were pulled into this change after review (see below).

1. Inventory both app packaging and test-build paths and launcher asset resolution; identify a shared manifest contract.
2. Add focused tests for marker parsing, dirty checkout handling, unknown provenance, and binary hash verification. Run them red.
3. Implement manifest generation at each packaging entry point, retaining backwards compatibility; record role, exact revision, dirty state, upstream and patch SHAs, merged/skipped inputs and reasons, build settings, executable hashes and data location.
4. Add a launcher Build Information view that reads the bundle manifest and displays missing information as unknown. Verify accessible, readable behavior for absent or malformed manifests.
5. Build the app and relevant host tests, inspect the manifest and compare binary hashes. Run the launcher with isolated data, capture its identity screen, and verify role and revision against source. Do not replace an open game or overwrite saves.
6. Review changes, document verification and remaining gaps, and update KYL-59 with evidence. Installation and closure require verified installed artifact and visible UI.

## Review follow-up (PR #81)

- Names: Open Nectar is the base name. Upstream `Open Nectar.app` (`org.opennectar.upstream`), Develop `Open Nectar Develop.app` (`org.opennectar.develop`), test builds `Open Nectar Test.app` (`org.opennectar.testbuild.<hash>`) in `.noindex` folders. Spotlight shows exactly two apps.
- Legacy `/Applications/Pikmin 1.app` (`org.opennectar.pikmin1`) is moved to `~/OpenNectar-archive.noindex/legacy/` only after Develop is installed and verified. That copy is never pruned; saves are untouched.
- Install: in place with a backup, a check, and restore on failure; skipped while the game runs; refused for a mismatched app; one lock per upstream build.
- Upstream: `tools/mac/build-upstream.sh --install` fetches `main` and builds with overlays only; the hourly upstream agent runs the same command with `--if-changed`. Its data is `~/.local/share/pikmin-native-upstream`; saves are only copied in on request (`copy-saves-to-upstream.sh`).
- Not yet run on a Mac: real installs, launchd agents, Spotlight, and the legacy retirement. The logic has self-tests that run on Linux.
