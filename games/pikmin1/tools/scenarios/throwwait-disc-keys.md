# ThrowWait disc motion keys (W3-7b)

Fresh read on 2026-11-06, source `c34b4e6177681ac4834b5ba646c0ed7960e65020`,
USA Rev 1 (`VERSION_GPIE01_01`). The earlier Action0-only ThrowWait note was
incorrect: the disc has LoopStart, LoopEnd and Action1. No game logic is changed.
The stand-in keeps its own static DCK geometry; only motion timing and key
records are matched to the disc.

## Provenance

Read-only install: `/Users/kylenakamura/OpenNectar-builds.noindex/disc-assets`,
with the files below under `assets/dataDir/pikis/`. Both were compared byte for
byte with their FST entries in
`/Users/kylenakamura/documents-local/development-local/side-projects/pikmin/games/pikmin1/local/Pikmin-USA-Rev1.iso`.
The ISO header is `4750494530310001` (GPIE01, revision 1).

| File | Bytes | SHA-256 | ISO offset |
|---|---:|---|---|
| `animMgr.bin` | 8228 | `cbc5adc871b842c1379e18ebe55ebf85ca148bf79a290b1bdca29a23081b7c14` | `0x51a70c30` (FST 1335) |
| `bluModel.anm` | 706192 | `5456d937c1d6f15b590767a6382b803ead5140b9aa20b13b57a248f14013df07` | `0x51a76cec` (FST 1338) |
| ISO | — | `db013398ec77299e307ef61ec33e82b07e4b21cb676b18a0be712fe55e9775f2` | — |

No raw disc files are checked in.

## Decode tool and reproduction

The dump uses the in-tree `AnimMgr::read` / `AnimInfo::doread` and
`Parameters::read`, rather than a separate binary-format interpretation.
The temporary instrumentation is preserved in [throwwait-dump.patch](throwwait-dump.patch).
It logs lists immediately after `doread`, before `checkAnimData` can clamp
indices; frame counts are printed after the real bundle loader loads the payloads.
The patch is evidence tooling only and is removed from the normal executable.
Its SHA-256 is `0bb5985af48ecf27b03d4eeb8fd4ddaae8c8adcdf9b76f29735a42bfb2980a87`.

From the repository root, in an isolated checkout (replace the install path):

```sh
git apply games/pikmin1/tools/scenarios/throwwait-dump.patch
cmake -S games/pikmin1 -B build/pikmin1-scenarios \
  -DPIKMIN_NATIVE_JAUDIO=OFF -DPIKMIN_BUILD_SCENARIOS=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/pikmin1-scenarios --target pikmin_pc -j 4
NECTAR_DUMP_THROW_KEYS=1 \
  PIKMIN_SCENARIO_DATA_DIR=/path/to/disc-install \
  build/pikmin1-scenarios/bin/nectar --scenario throw_release --scenario-tick-rate 30
# Remove the probe and rebuild before running the final suite.
git apply -R games/pikmin1/tools/scenarios/throwwait-dump.patch
```

The dump run passed `throw_release` at tick 389 (45 intro ticks).
The AnimMgr is version 1, has 100 infos and base path `pikis/motion`.

## Decoded records

Info keys store **anim-key indices**, while event keys store **frame numbers**.
Each tuple below is `(index-or-frame, eventType, commandId, keyType)` in file
order. `PaniAnimator::checkConstantKey` maps key types 0/1/2/3 to
LoopStart/LoopEnd/Action0/Action1. Event type 0 maps to `KEY_PlaySound`.

| Motion | Disc name | Frames | Anim keys | Flags | Speed |
|---|---|---:|---|---:|---:|
| ThrowWait (info 63) | `trwwait.dca` | 21 | `[0, 1, 20]` | 5 | 30 |
| Throw (info 54) | `throw.dca` | 10 | `[0, 1, 9]` | 5 | 30 |

ThrowWait info keys, exactly in this order:

```text
(1, 0, 0, 2)  Action0   frame 1
(1, 0, 0, 0)  LoopStart frame 1
(2, 0, 0, 3)  Action1   frame 20
(2, 0, 0, 1)  LoopEnd   frame 20
```

ThrowWait has **zero event keys**. Throw has one info key `(1, 0, 0, 2)`
(Action0 at frame 1), and one event key `(9, 0, 0, 0)` (notify at frame 9).
There are no Throw LoopStart, LoopEnd or Action1 info keys.

The prior notes recorded an incomplete ThrowWait info-key list. This
investigation does not establish why that earlier read omitted three keys.

## Behaviour and regression checks

`NaviThrowWaitState::procAnimMsg` increments charge only on `KEY_LoopEnd`, up
to level 3. `PaniAnimator::checkConstantKey` loops frame 20 back to frame 1.
Action0 precedes LoopStart, so grabbing happens once; subsequent loops repeat
Action1/LoopEnd and do not repeat Action0. The separate Throw notify at frame 9
is a sound event, not the release key (Action0 at frame 1).

`throw_levels` now holds A until each requested charge level is reached, checks
one-level increments and never assigns `mThrowChargeLevel`. It waits one tick
after an increment for `exec` to consume the new level before releasing.
EC-42 still characterises equal throw min/max parameters and includes the
raised-height control. It does not change the original throw trajectory.

Before updating stand-ins, this scenario failed at both 30 and 60 Hz: charge
remained 0 when level 1 was requested after 5.5 seconds. The updated animation
writer test also failed on flags, missing ThrowWait keys, missing Throw notify
and a ThrowWait motion that finished instead of looping. Those are the negative
controls for this change.

Focused verification: the animation writer, stand-in generator, `throw_levels`
and `throw_release` passed at both 30 and 60 Hz. Natural level 3 was reached
at 2.13 seconds held at 30 Hz and 2.12 seconds at 60 Hz (throw 3 in each run,
including grab approach time). The equal-parameter peaks remain +61.3 and
+68.0 respectively; the raised-height control still produces higher peaks.
The two registered `throw_release_*_disc` tests also passed on this install.

Full macOS run: `ctest -E _disc --output-on-failure -j 3`, 303/304 passed
in 269 seconds. Only `scenario_screenshot_goldens` failed: boot-smoke has
0.159% of pixels over tolerance against the hosted macOS golden. Rebuilding the
generator with the base `standin_anims.cpp` reproduces the same mismatch; all
six decoded before/after screenshots are pixel-identical. No goldens were
changed. After restoring the new generator, the final six focused checks pass.
Linux and Windows checks are left to CI.
