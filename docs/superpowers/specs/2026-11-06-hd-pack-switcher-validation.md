# HD pack switcher validation

Base: upstream main `822238e1`; no dependency on PR #184.

- Linux Debug full game, launcher and host-target build passed.
- CTest: 66/66 passed, including catalog, settings metadata, toast layout and disposable CLI integration tests.
- Catalog/deletion fixtures passed under AddressSanitizer and UndefinedBehaviorSanitizer.
- CLI tests cover separate variants, legacy marker/case migration, persisted selection, unsafe/repeated install rollback, Off after active deletion, and traversal rejection.
- Production SDL2/ImGui launcher rendered headlessly at 1280x800 and 640x480. The first narrow render exposed a cramped panel; compact controls and removal of the redundant help panel fixed it. Inspected final renders show all three choices, sizes, Delete buttons and ZIP installation without overlapping labels.
- Headless input probes verified hand cursors for Off and Delete. A real CLI-backed disposable UI run clicked Delete, confirmed it, removed only 4K, retained 1080p, persisted Off, and showed a bounded two-line toast with removed bytes and folder.
- Screenshot data is synthetic except that disposable end-to-end run. No disc-backed game or user saves were opened. Audio was muted and remains muted.

Local evidence: `/workspace/pack-switcher-validation/` (build/test logs, render fixture source and screenshots). These artifacts have not been uploaded.

Not tested: real GB-sized archives/network download, GPU texture replacement after restart with a disc, macOS, Windows, Android. Variant storage is desktop Pikmin 1 only; other platforms/games retain legacy layout. Existing downloaded folders are not overwritten automatically; use Delete first where supported, or manage legacy folders separately. Space figures are logical file bytes, not measured filesystem block allocation.

Publication is separate from implementation. Draft PR text and prefilled upstream compare URL are prepared under `/workspace/upstream-prep/`; no push, PR, or upstream write was performed.

## Delete-label follow-up

Reproduced the compact-button bug with the production Fredoka/font fitter: `Delete...` became `Delet...` at 76x26. Delete now uses at least the standard 120-point action width (expanded further from text measurement if needed), with a 30-point compact height. Added full-label/no-fallback font tests at 1x and 2x for both Delete sizes and the compact ZIP-install control.

Re-rendered and inspected pack, confirmation and toast states at 640x480 and 1280x800. All rendered button labels passed an in-process no-truncation/inner-rectangle audit; hand-cursor probes passed for Off and Delete. The confirmation body uses the compact font at both sizes so the complete tested folder path is visible in its field. Longer path values remain selectable/scrollable with a full-path tooltip. Both toast layouts remain within bounds. Full build and all 66 CTest tests passed.

Evidence: `/workspace/pack-switcher-validation/label-fix/` (`control-audit.log`, `ctest.log`, `final-build.log`, and `packs-{narrow,wide}.png`, `confirm-{narrow,wide}.png`, `toast-{narrow,wide}.png`). No push performed for this follow-up.
