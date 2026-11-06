# Chappy rest rig and combat timing (#154)

Measured 2026-11-06 on macOS arm64, `VERSION_GPIE01_01`, include base `c34b4e617`.
The source install was read-only. No disc binaries, meshes, textures, animation
names or animation curves are included in this change. Numeric joint SRT and
motion lengths/keys are the measured gameplay geometry/timing inputs.

## Reproduce the decode

```sh
python3 games/pikmin1/tools/scenarios/dump_chappy_geometry.py /path/to/install/assets
```

The decoder follows `nectar_assets/src/mod.cpp`'s chunk `0x60`: joint count,
32-byte alignment, then parent, flags, bounds, unused radius, scale, XYZ rotation
in radians, translation and material/poly references. Only parent and SRT are
reported. The `.anm` bundle header gives the entry count; each entry is type,
size, length-prefixed name and payload. The first two payload integers give joint
and frame counts. The report deliberately omits names, geometry and curves.

| Disc file (under assets/) | SHA-256 |
|---|---|
| `dataDir/tekis/chappy/chappy.mod` | `ed5d4e76e0aafbfc0dadfaaf0b0a155a9402d5880a22a715466297e9bc569806` |
| `dataDir/tekis/chappy/chappy.anm` | `eb05e8ff4bd269b7ddae77036d927173c22f7b2fa6f33018f98660f7b3217e39` |
| `dataDir/tekikeys/chappy.key` | `224f2bbe2e19e2132696a0dc61ae1ecd2f2f2aa63cabf34e74a551510cde2565` |
| `dataDir/pikis/animMgr.bin` | `cbc5adc871b842c1379e18ebe55ebf85ca148bf79a290b1bdca29a23081b7c14` |
| `dataDir/pikis/bluModel.anm` | `5456d937c1d6f15b590767a6382b803ead5140b9aa20b13b57a248f14013df07` |

The 17 decoded SRTs are transcribed in `buildObjectModelSpecs()` in
`libs/nectar_assets/src/standin_objects.cpp`. The model and every generated Chappy
DCK use this same pose. Other models retain identity poses. The artwork is
rebound through inverse cumulative bind transforms so its evaluated rest
positions and normal directions stay as authored. Collision text, IDs, parents,
radii and joint-local offsets are unchanged.

## Geometry

The measured root is `(0, 13.9998598, -0.500132978)`, not `(0,0,0)`.
The enemy scale is 0.8. At rest, before heading rotation and world translation:

| Sphere | Scaled rest centre x, y, z |
|---|---|
| cent | 0.000000, 11.199888, -0.400106 |
| body | 0.000000, 13.160990, -1.310405 |
| head | -0.000002, 13.744143, 7.301113 |
| slot | -0.000001, 13.847671, 5.704466 |
| rleg | -3.441318, 3.893143, 1.293945 |
| lleg | 3.470659, 3.891233, 1.275842 |

This is **rest geometry**, not the moving disc skeleton. Rotations matter as
well as heights: adding 11 to every collision offset would not reproduce it.

## Motion audit

`PIKMIN_SCENARIO_COMBAT_TRACE=1` with `--scenario bulborb_fight` reports the
Pikmin AnimMgr frame counts/flags/keys, infos actually reached, and per-tick
Bulborb motion/frame, health and sphere centres relative to the enemy origin.
It only reads state. It does not alter AI, RNG, phases, health or collisions.

The disc Chappy bundle has ten distinct payloads, frame counts
`[90,90,80,55,106,45,40,75,25,25]`. Its AnimMgr has 15 infos because several
reuse payloads. All 15 existing object profiles already have measured lengths
and keys; none needed a new guessed frame count.

Run (info 0), Nigeru (4), and Walk (25), reached during the fight, were still
using generic one-shot keys. All three measure 40 frames, flags 6, speed 30,
anim keys `[0,10,30,39]`, LoopStart at key index 1, LoopEnd at index 2.
These measured loop keys are applied. Attack (18), StillJump (13), Kuttuku (71),
Flick (58), JKoke (57) and GetUp (59) already match measured timing. Sagasu2 (90)
is a lost-target motion (disc 60 frames, flags 5, keys `[0,59]`); it appears
after the kill in these runs, so it is not changed. Noboru (88) is the climbable
platform branch, not exercised by this Chappy's sphere-only collisions.

Ground-attack damage is enabled by Attack Action0 at frame 3 and disabled by
Action1 at frame 5 or a successful hit. It checks Pikmin world position/facing
against the enemy collision centre/radius; it does not use Pikmin hand joints.

## Measured fights

Same binary, ten blue Pikmin, 30 seconds, dummy audio. Stand-ins use
`stages/sandbox_objects.ini`; disc uses the runner default Forest of Hope.
The scenario's existing bounds are kill <=10 s and mean Flick <=4 s. Passing
these broad bounds **does not establish disc timing parity**.

| Assets | 30 Hz kill | 30 Hz mean Flick (ended) | 60 Hz kill | 60 Hz mean Flick (ended) |
|---|---:|---:|---:|---:|
| Baseline stand-in | 2.4 s | 3.33 s (5) | 1.6 s | no Flick (0) |
| Measured rest rig | 3.2333 s (tick 97) | no Flick (0) | 2.3667 s (tick 142) | 2.71 s (2) |
| Rest rig + measured locomotion loops | 3.2 s | no Flick (0) | 2.4 s | 2.71 s (2) |
| Disc | 3.3667 s (tick 101) | 2.89 s (3) | 2.5333 s (tick 152) | 2.78 s (2) |

The geometry change closes most of the kill gap; loop keys do not change these
fight outcomes. There is still no stand-in Flick at 30 Hz, so #154 is not resolved.
No radii, health, attack power, phase or recovery timer was fitted to these times.

A read-only-overlay probe using the disc Chappy animation bundle and table on
the stand-in object stage gave kills 3.5/3.0 s and mean Flick 3.20/2.99 s at
30/60 Hz. That is not the full disc comparison and is not committed. It shows
that animated geometry changes the result, but does not alone reproduce the
disc-stage encounter. At tick 0, disc-stage motion 2 is at frame 27.088;
stand-in-stage motion 2 with the disc bundle is at frame 37.368. Different stage
boot history gives a different phase before the scenario starts. Terrain,
heading, phase and RNG history remain comparison variables.

The 60 Hz cause is not fully isolated. Both trees run faster at 60 Hz after the
rig change. The evidence does not justify changing gameplay's tick-rate logic.
Next measurement: matched encounter initialization and per-tick hit/flick
conditions plus moving disc sphere positions, without fitting a static height
or changing the existing scenario to make its numbers agree.

## Verification and hashes

- Root-height regression failed before implementation in the model and all 15
  generated DCK entries; passes afterwards. Readback checks every SRT channel.
- Authored art positions and renderer normal directions are checked after
  rebinding. The renderer uses the model matrix for these untextured materials
  (`DGXGraphics::useMatrixQuick`), and measured occupied joints have unit scale.
- `ctest -E _disc`: 287/288 pass. Only `scenario_screenshot_goldens` fails on
  `boot_smoke_tick100` (0.159% of pixels over tolerance 4). A capture using the
  pre-change tree is byte-identical to the current shot, SHA-256
  `5f57af6a0abaef31f44d579a649db2af9f7c466bd00b85de18b5266f02468f33`.
  This failure is reproduced independently of this rig; goldens are not refreshed.
- `ctest -R _disc` on the read-only install: 23/23 pass (20 game-assets tests
  plus three names containing `_disc`). Direct disc `bulborb_fight` runs at
  both rates also pass; it is not registered as a `_disc` CTest entry.
- Baseline control: 178 stand-in scenario executions, 0 failed, using the same
  binary and generated tree with only Chappy MOD/ANM and Pikmin AnimMgr replaced
  by their pre-change versions. 176 scenario hash logs common to all passes.
  Rig-only pass: 8 changed, 168 identical. Adding locomotion loops changes one
  of those eight further. Final: 8 changed, 168 identical.

| Changed hash log | First rig divergence tick / fields | Traced input |
|---|---|---|
| `bulborb_fight_30` | 110 / total,piki | Real attacks/sticking against raised and rotated Chappy parts. |
| `bulborb_fight_60` | 200 / total,piki | Same geometry path at 60 Hz. |
| `enemy_kills_30` | 143 / total,item | Chappy death/corpse placement uses the model joint pose. |
| `enemy_kills_60` | 262 / total,item | Same corpse placement path at 60 Hz. |
| `flick_out_of_range_30` | 119 / total,piki | Real attack/stick approach and flick from the Chappy. |
| `flick_out_of_range_60` | 209 / total,piki | Same approach/flick path at 60 Hz. |
| `piki_hazard_deaths_30` | 460 / total,piki | Swallow test attaches the victim to Chappy slot/mouth joints. |
| `piki_hazard_deaths_60` | 998 / total,piki | Same swallow attachment path at 60 Hz. |

The additional loop-only change is `enemy_kills_30`, first differing at hash
tick 917 (scenario tick 895), initially `total,rng` only. A temporary bounded
RNG backtrace and causal rerun reproduced the mismatch: generic locomotion
reaches `KEY_Finished`, dispatches through `Navi::animationKeyUpdated` to
`NaviIdleState::init`, and consumes seed `258d46b2` to choose another idle
motion (`e85d8603` afterwards). Measured locomotion loops suppress that Finished
callback. With the measured keys the same RNG state is consumed later, at
scenario tick 925, by `TAIAsetTargetPointCircleRandom::start`. This is captain
idle scheduling, not damage cadence. The temporary RNG instrumentation was
removed and the game rebuilt; it is not part of the change.

Full-log SHA-256 values from the original `c34b4e617` experiment (before the
include-base refresh; generated artifacts are not committed):

| Log | SHA-256 |
|---|---|
| `bulborb_fight_30` | `cfb77a91e41d66cfa4bbd05edaa6943c73f40f8f4528a9c7ed0b7d021e8e7fde` |
| `bulborb_fight_60` | `b4338dcc9241a44c25b6d075f3a62eed8c87d53c68e27e47cbcba216a67b0d5c` |
| `enemy_kills_30` | `c9181250f96ed8c876c7745e6c26c8e9887171ad79fac502b4ed8d6acbbdd922` |
| `enemy_kills_60` | `78a45bfdb13ba0237d1fe230eaf71dec21f11831e1c70c2dec408d7eb96d178b` |
| `flick_out_of_range_30` | `a17c4311bc247121f64284bfcbc2ab5bb8b9b8dc0532981960c871a4e520e24a` |
| `flick_out_of_range_60` | `ec1537b2f6c7d187550d0b5533d455f438ccc808e300f7d53852d23f0cf7df21` |
| `piki_hazard_deaths_30` | `798ab4b32821b95cb15dc301ae1252277a99c37b6d7c6ae7dabcef684ca4d3c3` |
| `piki_hazard_deaths_60` | `16ab5fe880f5af522f22cfcb05a685306857c6ef837e4ee943a52eeeeb2676f4` |

## Current include-base refresh

Before publication, merged `origin/knakamura/disc-free-gameplay-develop` at
`205b7e393` (includes #173–175) into this branch without rewriting history.
Rebuilt all targets and reran `ctest -E _disc`: **297/298 pass**; the sole failure
is the same pre-existing `scenario_screenshot_goldens` boot shot, with identical
pixel metrics. `ctest -R _disc` on the disc install: **29/29 pass** (26
`game-assets`, plus three other names matched by the regex). Fight metrics are
unchanged. The eight hash logs listed above remain byte-identical to the earlier
experiment, so their recorded SHA-256 values still apply.

The refreshed-base control has **184 scenario executions, all passing**, with
only the two Chappy assets restored and only infos 0/4/25 of Pikmin AnimMgr
restored to the base's generic keys. All other newer include-base inputs remain
in both trees. Of **184 common hash logs, 8 change and 176 are identical**;
first divergence ticks/fields are the same eight rows above.

A second refreshed-base control retained the new Chappy rig but restored only
the locomotion infos: another **184 executions, all passing**. It reproduces
exactly the eight geometry changes, and rig-only versus final again differs
only in `enemy_kills_30`. Thus the isolation and the traced idle-RNG explanation
also hold after the base refresh.
