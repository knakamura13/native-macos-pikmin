# Build Identity Implementation Plan

**Goal:** Provide truthful, reproducible identity for upstream, develop and feature macOS builds.

**Architecture:** Extend bundle metadata while preserving the existing `sha` field consumed by auto-install. Generate identity from the source checkout and the develop rebuild marker, with unknown values explicit. Expose the bundled metadata in the launcher. Keep installation/data migration in KYL-60.

1. Inventory both app packaging and test-build paths and launcher asset resolution; identify a shared manifest contract.
2. Add focused tests for marker parsing, dirty checkout handling, unknown provenance, and binary hash verification. Run them red.
3. Implement manifest generation at each packaging entry point, retaining backwards compatibility; record role, exact revision, dirty state, upstream and patch SHAs, merged/skipped inputs and reasons, build settings, executable hashes and data location.
4. Add a launcher Build Information view that reads the bundle manifest and displays missing information as unknown. Verify accessible, readable behavior for absent or malformed manifests.
5. Build the app and relevant host tests, inspect the manifest and compare binary hashes. Run the launcher with isolated data, capture its identity screen, and verify role and revision against source. Do not replace an open game or overwrite saves.
6. Review changes, document verification and remaining gaps, and update KYL-59 with evidence. Installation and closure require verified installed artifact and visible UI.
