# Chappy collision rig and combat timing (#154)

Measured 2026-11-06 on macOS arm64, `VERSION_GPIE01_01`, current include base
`205b7e393`. The source install is read-only. No raw disc binaries, meshes,
textures or original animation names are committed. The Chappy exception below
contains measured numeric collision-driving motion channels, not fitted values.

## Source and decode

```sh
python3 games/pikmin1/tools/scenarios/dump_chappy_geometry.py /path/to/install/assets
python3 games/pikmin1/tools/scenarios/extract_chappy_collision_motion.py /path/to/install/assets \
    --output /tmp/chappy-motion.inc
cmp /tmp/chappy-motion.inc libs/nectar_assets/src/standin_chappy_motion_data.inc
```

The model decoder follows `nectar_assets/src/mod.cpp`, chunk `0x60`: joint count,
32-byte alignment, parent, flags, bounds, unused radius, scale, XYZ rotation in
radians, translation and material/poly references. Only parent/SRT are reported.
The bundle decoder follows `bundle.cpp`: entry type, size, length-prefixed name,
then DCA/DCK payload. Payload layout follows `anim.cpp` and the game's
`AnimDca::read`/`AnimDck::read` in `src/sysCommon/shapeBase.cpp`.

| Disc file (under assets/) | SHA-256 |
|---|---|
| `dataDir/tekis/chappy/chappy.mod` | `ed5d4e76e0aafbfc0dadfaaf0b0a155a9402d5880a22a715466297e9bc569806` |
| `dataDir/tekis/chappy/chappy.anm` | `eb05e8ff4bd269b7ddae77036d927173c22f7b2fa6f33018f98660f7b3217e39` |
| `dataDir/tekikeys/chappy.key` | `224f2bbe2e19e2132696a0dc61ae1ecd2f2f2aa63cabf34e74a551510cde2565` |
| `dataDir/pikis/animMgr.bin` | `cbc5adc871b842c1379e18ebe55ebf85ca148bf79a290b1bdca29a23081b7c14` |
| `dataDir/pikis/bluModel.anm` | `5456d937c1d6f15b590767a6382b803ead5140b9aa20b13b57a248f14013df07` |

## Correction: actor scale and rest height

The initial PR incorrectly assumed an enemy scale of 0.8 and called 11.199888
its scaled rest height. **Runtime traces show both actors at scale 1.0.** The
measured MOD rest root is `(0,13.9998598,-0.500132978)`, so its rest centre is
near 14. The disc's observed centre near 11 comes from its animated idle pose,
not an actor scale factor. Damage vibration later makes scale nonuniform.

The measured rest SRTs of all 17 joints remain in `buildObjectModelSpecs()` in
`standin_objects.cpp`. The generated MOD uses them. Collision IDs, joint
parents, radii and joint-local offsets are unchanged. Original stand-in art is
rebound through inverse cumulative rest transforms, preserving its authored
rest positions and normal directions. The moving rig now animates that art.

At the disc's idle LoopStart frame 10, the root has rotation
`(0.759088755,0,0)` and translation `(0,10.569417,3.7900579)`; head/body world
positions also change. Adding a fixed 11 to collision offsets would be wrong.

## Narrow animated-collision exception

The earlier object contract used static payloads. Chappy alone now keeps measured
channels for the ancestors of collision joints:
`0,1,3,4,7,8,10,11,13,14,15,16`. The other joints `2,5,6,9,12` retain their MOD
rest SRT. The selection is ancestor-closed, so every collision joint evaluates
with the disc's transforms; unrelated visual tracks are omitted.

The generated text table contains 18,420 binary32 values, deduplicated by channel
and shared across ten payloads aliased by the existing 15 AnimMgr infos. DCA
samples remain DCA; DCK time/value/tangent records, counts and flags remain DCK.
There is no resampling, fitted tangent, guessed frame count or interpolation
replacement. Existing frame counts and all timing/event profiles are unchanged.
The writer rejects a mismatch between motion length and the measured profile.

The extractor regenerates the `.inc` byte-for-byte. Its source/repacked check
and C++ writer readback check verify selected channels binary32-exactly. The
independent payload files can be checked with `--verify-payload-dir` (files
`info00.bin` through `info14.bin`). Evaluated selected-joint matrices have maximum
error 0 at every integer and half frame; the verification rejects error >1e-5.
The disc-free reader test checks every generated payload type/length/parents,
active motion, and the idle root's 75 measured samples and frame-10 height.

## Motion keys and tracing

Run (info 0), Nigeru (4) and Walk (25) measure 40 frames, flags 6, speed 30,
keys `[0,10,30,39]`, LoopStart at key index 1 and LoopEnd at index 2. These keys
replace generic one-shots. Attack (18), StillJump (13), Kuttuku (71), Flick (58),
JKoke (57) and GetUp (59) already have measured timing. Esa (info 8), reached
when the Bulborb captures a Pikmin, was still generic: it now has the measured
20 frames, flags 5, speed 30, keys [0,19] and loop keys at indices 0/1. The
Swallowed state exits on mouth linkage/enemy life, not animation Finished. Other Chappy profiles
also already match disc frame counts/keys; none is replaced by a guessed count.

`PIKMIN_SCENARIO_COMBAT_TRACE=1` reports frame counts/keys and reached infos,
then per tick: enemy motion/frame, health, sphere centres, actor scale, heading,
AI state, damage count, pending damage, nearby Pikmin, flick threshold and RNG
state. `PIKMIN_SCENARIO_COMBAT_PIKI_TRACE=1` adds each Pikmin's position, centre,
state/mode, motion/frame, attachment and RNG state. The enemy trace also
reports the captain's initial lower/upper motion and collision centre. Both traces only read state;
a state-hash comparison confirmed trace-on and trace-off are byte-identical.

## Default scenario measurements

Same binary, ten blue Pikmin, 30 seconds, dummy audio. Stand-ins use
`stages/sandbox_objects.ini`; disc uses the runner's default Forest of Hope.
The default encounter setup, AI and scenario bounds are unchanged.

| Assets | 30 Hz kill | 30 Hz mean Flick (ended) | 60 Hz kill | 60 Hz mean Flick (ended) |
|---|---:|---:|---:|---:|
| Original stand-in | 2.4 s | 3.33 s (5) | 1.6 s | none |
| MOD rest rig + measured locomotion keys | 3.2 s | none | 2.4 s | 2.71 s (2) |
| Measured moving collision rig (current) | 3.5 s | 3.20 s (1) | 3.0 s | 2.99 s (4) |
| Disc | 3.4 s | 2.89 s (3) | 2.5 s | 2.78 s (2) |

The current default scenario passes its existing bounds (kill <=10 s, mean Flick
<=4 s), and Flick occurs at both rates. **Those broad bounds do not establish
full timing parity.** Residual differences remain, especially 60 Hz kill time.
No phase, health, radius, damage, recovery timer or gameplay tick logic was fitted.

Against an overlay using the full disc Chappy bundle/table on the stand-in
stage, the collision-only export has **identical runtime traces**, including
sphere centres, AI/damage state and all Pikmin traces: 9,911 entries at 30 Hz,
19,811 at 60 Hz. Its kill/Flick results also match that overlay exactly. This
proves the retained collision channels reproduce the complete Bulborb animation's
gameplay behavior in this encounter; omitted visual tracks are not the residual.

## Controlled experiments and limits

- A single measured idle-loop pose for all motions gave kills 3.3/1.6 s and Flick
  2.88 s/none. Per-motion idle/attack/flick key poses gave 3.9/2.7 s and
  none/3.58 s. These static choices do not close parity and were not committed.
- The disc/default-stage idle phase at tick 0 is 27.088; stand-in stage 37.368.
  A temporary probe delayed Pikmin spawning until tick 0, reseeded both runs,
  restarted the enemy AI and reset its counter. This still leaves stale first
  draw matrices and unrelated stage/actor behavior; it is not called a fully
  matched benchmark. All state-changing probe code was removed.
- With only one enemy on the same flat stage and real Bulborb animation in both
  trees, the early enemy sphere traces agree. Pikmin 9's position first differs
  at tick 9 while RNG agrees. Moving the captain to the opposite side delays
  that divergence to tick 15. Other actor collision geometry is a remaining input.
- Disc Pikmin MOD root y is 3.5500741 while the stand-in's root animation is
  identity. A scratch root-height overlay changes Flick to 2.87/2.77 s but kills
  to 3.1/3.1 s; it is not a complete motion match or a committed change.
- A scratch overlay of the disc Pikmin animation table and bundle, with current
  Bulborb collision motion, gives kills 3.2/2.3 s and mean Flick 3.12/3.22 s.
  Wider motion/actor differences and stage history remain; this is evidence
  against treating the Bulborb rig as the only remaining cause. No Pikmin asset
  import or unrelated generic-motion sweep is included in this PR.

Next measurement: replay a complete comparable encounter snapshot (including
actor collision centres, lower/upper animation state, AI state, RNG and terrain)
and trace the first contact/attack divergence. The remaining 60 Hz cause is not
isolated; #154 remains open rather than fitting values to this one fight.

## Verification and state hashes

- New motion regression failed on all 15 static entries, then passed with
  measured motion. Model rest-height/art invariants still pass.
- `ctest -E _disc`: 297/298 pass. Only pre-existing `scenario_screenshot_goldens`
  fails at boot_smoke_tick100, 0.159% over tolerance 4. The prior base/current
  captures are byte-identical, SHA-256
  `5f57af6a0abaef31f44d579a649db2af9f7c466bd00b85de18b5266f02468f33`.
- Previous include-base disc check: 29/29 passed. Direct disc fights pass at
  both rates. The new animation only changes generated stand-in data.
- Esa's measured timing changed 0/184 state-hash logs; fight metrics are unchanged.
  The final writer cleanup regenerated all 475 asset files byte-identically.
- 184-scenario baseline and rest-rig controls each passed before the motion
  addition. Of 184 common logs, the current motion has eight changed and 176
  identical against both controls. First divergence and SHA-256 below.

| Changed log | First divergence tick / fields | Traced input |
|---|---|---|
| `bulborb_fight_30` | 110 / total,piki | Raised, rotated and moving attack/stick collision parts. |
| `bulborb_fight_60` | 200 / total,piki | Same combat geometry path at 60 Hz. |
| `enemy_kills_30` | 143 / total,item | Chappy death/corpse placement uses animated model joints. |
| `enemy_kills_60` | 262 / total,item | Same corpse placement path at 60 Hz. |
| `flick_out_of_range_30` | 119 / total,piki | Real attack/stick approach and flick from Chappy. |
| `flick_out_of_range_60` | 209 / total,piki | Same approach/flick path at 60 Hz. |
| `piki_hazard_deaths_30` | 460 / total,piki | Swallow victim attaches to animated slot/mouth joints. |
| `piki_hazard_deaths_60` | 998 / total,piki | Same swallow attachment path at 60 Hz. |

The isolated locomotion-key change was also traced: `enemy_kills_30`, hash tick
917/scenario tick 895, initially total/RNG only. A temporary RNG backtrace
showed `NaviIdleState::init` reached through `Navi::animationKeyUpdated` on
`KEY_Finished`, consuming seed `258d46b2` for idle-motion reselection. Measured
loops suppress that callback. The unchanged seed is consumed later at scenario
tick 925 by `TAIAsetTargetPointCircleRandom::start`. This is captain idle
scheduling, not attack damage. Temporary RNG instrumentation was removed.

Current full-log SHA-256 (generated logs remain in the build directory):

| Log | SHA-256 |
|---|---|
| `bulborb_fight_30` | `3002e3ce7cda2a074452e65ed2ac9f844864b5901d746ccbd313feb5d6f90781` |
| `bulborb_fight_60` | `c012fba149c00af01a4d378833fb75997f9404d3219d164f1a38766b51e4a994` |
| `enemy_kills_30` | `42b07f2f83c4f46a40ae03daeb1d52a0b17613feb78d6c3caf233685cbc5f69c` |
| `enemy_kills_60` | `432c1948cb1c685fd785b554c110ae0e7a9ef115bb5aee743cc5e257ce5cc3c5` |
| `flick_out_of_range_30` | `b4d042eceb255ede464e965d900d425ef4ed268eeed29b155a5cbe5ad5452549` |
| `flick_out_of_range_60` | `09c756a1196108c2fb0b2f3d6a16ffce51afc47ca294125736c00007ca05bb5c` |
| `piki_hazard_deaths_30` | `4b2dadc515ca18ec5e0f32030247c0b36fc243831c2f36d628a017ef22574470` |
| `piki_hazard_deaths_60` | `0ac95ddbabe595c3264c8fc95d69275643b6f9cf178c669eac676e36dbed5c3f` |
