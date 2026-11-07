# Texture pack reinstall implementation plan

**Approved design:** Kyle's “Make it so” annotated rows: missing known packs stay gray and unselectable, with Install replacing Delete. Installation does not select a pack.

**Architecture:** Preserve installed-only discovery for the renderer and selection, add a settings catalog containing missing known variants. Both the existing HD textures dialog and row installs use the transfer engine in texture_pack_download.cpp. A settings-client worker owns the busy state and refreshes metadata on completion.

- [x] Add failing catalog/model tests for absent variants, disabled selection and Install/Delete/Working actions.
- [x] Add installed metadata and an available catalog; expose it in settings JSON without changing installed-only discovery.
- [x] Separate the existing dialog from its reusable transfer engine; inject fetch/install callbacks for tiny mock-network tests, retain staged installer rollback.
- [x] Wire row Install to the settings worker; block duplicate starts; retain selection, tooltips, failure/success toasts.
- [x] Render missing/downloading/reinstalled states at both sizes; inspect PNGs and measure row pitch.
- [x] Run full CTest suite, review diff and commit locally without pushing.

**Constraints:** No real texture downloads, no ISOs or saves, no auto-selection, no upstream actions. Existing uniform-height rows and hand cursor behavior are retained.

## Storage additions (Kyle t250u–t252u)

Click-time capacity is queried on the destination's nearest existing parent. The pure check uses named hard **2.2x** and soft **3.0x** archive-size factors: below hard blocks (OK only), hard-inclusive through soft-exclusive warns (Cancel default / Install anyway), soft-inclusive starts immediately. Unknown size/capacity logs and proceeds. Escape cancels either modal.

Evidence: a 64 MiB ZIP_STORED fixture through the real staged CLI held a 67,109,026-byte ZIP and 67,108,864 unpacked bytes simultaneously. Observed peak was 134,239,341 logical bytes / 134,254,592 allocated bytes (2.0003x archive size). Thus 1.1x archive alone is inadequate: 2.2x adds 10% to the baseline peak; 3.0x leaves a useful warning band above that minimum.

Catalog sizes are compressed ZIP sizes, not installed sizes. These factors are estimates, not a guaranteed bound for compressed packs. After downloading, the actual ZIP central-directory uncompressed sizes are summed; the existing game installer checks remaining extraction space with 10% headroom **before** creating staging. Failure leaves no new pack/staging directory; the transfer engine removes its ZIP workspace.

## Verification

- Linux build and all 68 CTests; includes mock-network transfer/cleanup, delete/reinstall without selection, missing catalog/model rows, storage boundaries/unknown, button label fitting, and disabled-radio cursor.
- UI storage tests drive the real Install click with injected capacity: block/OK and warning/Cancel start no fetch; warning/Install anyway starts exactly once.
- UI fixture additionally drives an actual Install click, two rapid clicks, mocked fetch, real staged CLI extraction, metadata refresh and success toast; repeats with a mocked connection failure and verifies cleanup/error toast.
- Actual PNG row pitches in deleted/downloading/reinstalled states: narrow **34 / 34 px**, wide **51 / 51 px**. Physical pitches at 2x are **68 / 68 px** and **102 / 102 px**, respectively, in all three states. Production-font row tests also cover both densities.
- Screenshots and measurements: `/workspace/pack-switcher-validation/reinstall/` (`deleted`, `downloading`, `reinstalled`, `low-space`, `blocked`, each `-narrow.png` / `-wide.png`).
- No production packs/network traffic; no Mac/Android runtime verification. Windows-target compile is checked separately with Zig/MinGW headers, not a complete Windows application link.
- The annotated attachment path supplied by the parent was absent on the box; implementation followed the supplied annotations, not a claimed inspection of the missing mockup.

Android retains its existing generic PNG download rather than advertising unavailable desktop DDS variants. No Android runtime test was performed.
