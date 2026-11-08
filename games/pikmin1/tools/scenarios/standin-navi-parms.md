# Stand-in `parms/naviMgr.bin`

`nectar_standin_generator` writes `dataDir/parms/naviMgr.bin` (`parmFiles` in
`standin_generator.cpp`). The game reads it into the captain's `CreatureProp`
list and then `NaviProp::Parms` (`include/NaviMgr.h`, `Parameters::read` in
`src/plugPikiColin/parameters.cpp`): an entry whose ID is not in the file keeps
the code default. An ID that appears twice is applied to the first matching
parm only.

Since 2026-11-06 the stand-in file carries every ID the disc's file sets, with
the disc's value, in the disc's order: the five CreatureProp values and all 61
`NaviProp::Parms` entries (60 distinct IDs; `p48` is stored twice). The
generated file is byte-identical to the disc's (536 bytes, SHA-256
`39c32d995cd16433be3dd03ae9cd5f695dee6b1d9cf070aa3ab732839f3d6601`, checked
with `cmp`). Before, it carried the five CreatureProp values and 27 Parms IDs
(movement, throw, grab, collision and cursor) and left the rest at the code
defaults. Issue #168.

Source: the VERSION_GPIE01_01 install's `parms/naviMgr.bin` (Pikmin-USA-Rev1),
`~/OpenNectar-builds.noindex/disc-assets/assets/dataDir/parms/naviMgr.bin`. The
same bytes were read on 2026-10-27 for the 27 IDs already written. On
2026-11-06 the file was decoded again with the `Parameters::read` layout
(big-endian header, 3-character ID in the top 24 bits, payload size in the low
8 bits, then a big-endian float or int32, `0xFFFFFFFF` ending each list). Only
the numbers are written by the generator; no disc file is copied.

`p62` (`mPluckDistanceOutsideOnyon`, code 15) is not in the disc file, so the
stand-in leaves it at 15 too.

## Table (disc order)

Bold: the disc differs from the code default. "Before" is what the stand-in
file had until 2026-11-06: `disc` (already written), `code` (left at a
differing code default, now the disc's) or `code (equal)` (not written, and
the disc equals the code default). Int payloads are marked; every other value
is a float. The two `p48` rows are two entries in the file.

| ID | Field | Meaning (header comment) | Code | Disc | Before |
|----|-------|--------------------------|------|------|--------|
| s00 | `mFriction` | captain friction (constructor override) | 1 | 1 | disc |
| s01 | `mWallReflection` | wall reflection | 0.5 | 0.5 | disc |
| s02 | `mFaceDirAdjust` | face direction (constructor override) | 0.5 | 0.5 | disc |
| s03 | `mAcceleration` | acceleration (constructor override) | 0.2 | **0.1** | disc |
| s04 | `mBounceFactor` | bounce | 0.3 | 0.3 | disc |
| p00 | `mActionRadius` | action radius | 120 | **25** | disc |
| p60 | `mContinuousPluckDistance` | continuous pluck distance | 200 | 200 | code (equal) |
| p01 | `mWhistleMaxRadius` | whistle grows out to this radius | 100 | **90** | code |
| p53 | `mWhistleMinRadius` | whistle starts at this radius | 5 | 5 | code (equal) |
| p02 | `mWhistleExpandTime` | seconds to grow from min to max | 0.2 | **0.45** | code |
| p03 | `mWhistleHoldTime` | full circle linger, then the whistle ends | 0.15 | **1** | code |
| p04 | `mMoveSpeed` | movement speed | 170 | **160** | disc |
| p56 | `mRunSpeed` | run speed in good condition | 210 | **160** | disc |
| p38 | `mDisplayScale` | display scale | 1 | **1.2** | disc |
| p05 | `_FC` | stick 0 | 0.001 | 0.001 | code (equal) |
| p20 | `_10C` | stick 01 | 0.1 | 0.1 | code (equal) |
| p06 | `_11C` | stick 1 | 0.8 | 0.8 | code (equal) |
| p07 | `_12C` | stick 2 | 1 | 1 | code (equal) |
| p08 | `_13C` | slip angle | 120 | 120 | code (equal) |
| p09 | `mThrowHoldMaxTime` | throw charge limit | 2.5 | 2.5 | code (equal) |
| p10 | `mThrowMaxDistance` | throw distance max | 600 | **140** | disc |
| p11 | `mThrowMinDistance` | throw distance min | 150 | **140** | disc |
| p24 | `mThrowMaxHeight` | throw height max | 100 | **75** | disc |
| p25 | `mThrowMinHeight` | throw height min | 80 | **75** | disc |
| p54 | `mYellowThrowHeight` | yellow throw height | 200 | **115** | disc |
| p26 | `mThrowFlightTime` | landing time | 1 | 1 | code (equal) |
| p37 | `mPluckGrabRange` | range to grab a Pikmin | 80 | **15** | disc |
| p42 | `mPluckLoopCount` | pluck loop count (int) | 1 | **0** | code |
| p39 | `mPikiWaitRange` | range where a Pikmin waits | 30 | **15** | disc |
| p40 | `mPikiFormationChangeRange` | formation-change range | 60 | **40** | disc |
| p21 | `mBodyCollisionRadius` | body collision radius | 15 | **8** | disc |
| p41 | `mGroundCollisionRadius` | ground collision radius | 16 | **8.5** | disc |
| p22 | `mInverseMass` | inverse mass | 4 | **1** | disc |
| p23 | `_22C` | aiming rotation speed | 0.04 | **0.03** | disc |
| p14 | `mAsibumiStartSpeed` | asibumi start speed | 5 | 5 | code (equal) |
| p15 | `mWalkStartSpeed` | walk start speed | 35 | **8** | disc |
| p16 | `mRunStartSpeed` | run start speed | 70 | **20** | disc |
| p17 | `mEscapeStartSpeed` | escape start speed | 160 | **95** | disc |
| p18 | `mWalkAnimMinFrames` | walk animation frames min | 30 | **60** | disc |
| p19 | `mWalkAnimMaxFrames` | walk animation frames max | 60 | **90** | disc |
| p27 | `mRunAnimMinFrames` | run animation frames min | 30 | **40** | disc |
| p28 | `mRunAnimMaxFrames` | run animation frames max | 60 | 60 | code (equal) |
| p29 | `mEscapeAnimMinFrames` | Nigeru playback speed (`Navi::updateWalkAnimation`) | 30 | **60** | code |
| p30 | `_2CC` | escape frames max; no reader | 60 | **90** | code |
| p31 | `mPressedTotalTime` | pressed total time | 3 | **2** | code |
| p32 | `mPressedFlatTime` | pressed flat time | 1 | 1 | code (equal) |
| p33 | `_2FC` | push start time; no reader | 0.5 | **0.8** | code |
| p34 | `_30C` | push start stick amount | 0.3 | 0.3 | code (equal) |
| p35 | `mShakePreventionAngle` | shake-prevention angle | 10 | 10 | code (equal) |
| p36 | `_32C` | shake-prevention size | 0.1 | 0.1 | code (equal) |
| p48 | `mCursorMoveDelayTime` | first `p48`; the read applies here | 2 | **0.85** | disc |
| p49 | `mPikiImpatienceTime` | time before a squad Pikmin loses patience | 8 | **5** | code |
| p43 | `mNeutralStickThreshold` | neutral stick | 0.1 | 0.1 | code (equal) |
| p44 | `mCursorMoveStickThreshold` | cursor-move stick | 0.75 | **0.65** | disc |
| p48 | `mClampStickToMaxThreshold` | second `p48`; unread, first match wins | 0.85 | 0.85 | code (equal) |
| p45 | `mCursorMinRadius` | cursor min radius | 50 | **0** | disc |
| p46 | `mCursorMaxRadius` | cursor max radius | 300 | **100** | disc |
| p47 | `mCursorMoveSpeed` | cursor move speed | 200 | **300** | disc |
| p50 | `mHealth` | captain health | 100 | 100 | code (equal) |
| p51 | `mPunchDamage` | punch damage | 1 | **10** | code |
| p52 | `mPunchRadius` | punch radius | 10 | 10 | code (equal) |
| p55 | `mPluckCursorCount` | cursor count (int) | 1 | 1 | code (equal) |
| p57 | `mMinKinokoFlickActions` | kinoko flick count (int) | 4 | **3** | code |
| p58 | `mBuryEscapeKeyCount` | bury key count (int) | 3 | **2** | code |
| p59 | `mBuryEscapeSuccessCount` | bury exit count (int) | 3 | **2** | code |
| p61 | `mPostPluckZoomOutTime` | pluck camera zoom-out frames (int) | 100 | **80** | code |

14 IDs differ from the code default and were not written before: `p01`, `p02`,
`p03`, `p29`, `p30`, `p31`, `p33`, `p42`, `p49`, `p51`, `p57`, `p58`, `p59`,
`p61`. The other new entries equal the code default and are written so the
file matches the disc's byte for byte. `p30` and `p33` have no reader in the
Pikmin 1 sources, so they cannot move a state hash by themselves.

## What the new values change

The whistle the #145 `whistle_delay` trace measured (stand-in 5→100 in 0.20 s,
disc 5→90 in 0.45 s) is `p53` (5, already the code default), `p01` (90) and
`p02` (0.45). `p03` is the linger after the circle is full (1 s on the disc,
0.15 s in the code). A full-speed walk is above `p17` (95), so
`Navi::updateWalkAnimation` plays Nigeru at `p29` (60 frames, code 30).

Whistle scenarios read the live radius, expand time and hold time
(`whistle_radius`, `whistle_gather_release`, `whistle_pluck_held`,
`whistle_delay`). `navi_invincible` reads `mPressedTotalTime`. None of them
asserts the code-default 100 or 0.20. `whistle_gather_release`'s 0.2 s bound
is the Fue motion's `KEY_LoopStart`, not `p02`. `throw_levels` asserts the
throw parms that were already the disc's (distance 140..140, height 75..75).

## Hash logs

177 stand-in hash logs (30 and 60 Hz), compared on the same binary: the
previous file, then the disc file, then the previous file plus one group of
new disc values at a time. 139 logs are byte-identical. 38 change, and each
one matches the disc file when only its group is added. `p30`, `p33`, `p42`,
`p51`, `p57`, `p58`, `p59` and `p61` changed no log (`p30` and `p33` have no
reader; the stand-in Nuku motion has no `KEY_Action0`, so `p42` never counts
down). Screenshot goldens did not change.

- **Whistle** (`p01` 90, `p02` 0.45, `p03` 1): `whistle_delay`,
  `whistle_gather_release`, `whistle_radius`, `piki_hazard_deaths`.
- **Whistle and impatience together**: `piki_drown`, `whistle_pluck_held`.
  Each group alone moves the log; both together match the disc file.
- **Escape playback** (`p29` 60; `p30` is in the same trial and is unread):
  `throw_levels`. A full-speed walk plays Nigeru at this speed
  (`Navi::updateWalkAnimation`).
- **Pressed** (`p31` 2): `navi_invincible`. The scenario reads
  `mPressedTotalTime`, so the shorter press still passes.
- **Impatience** (`p49` 5, code 8): `bomb_firsts`, `bulborb_fight`,
  `demo_timers`, `onion_menu_transfer`, `pellet_carry`, `pikmin_cap`,
  `pikmin_cap_1`, `pikmin_cap_250`, `pikmin_cap_999`, `tutorial_day1`,
  `tutorial_firsts`. A squad Pikmin runs out of patience sooner
  (`aiCrowd.cpp`).

`whistle_pluck_held` was the only scenario whose assertion named the
code-default expand. It required sprout c exactly one pluck-interval after b.
With the 0.2 s expand the circle already covers c at that interval; with 0.45 s
c is still outside, so the pluck waits for the radius. The check is now "at
least one interval after b". The per-tick check still requires the plucked
sprout to be the one inside the circle. The same binary passes that scenario
with the previous file and with the disc file. `throw_levels` still asserts
the disc throw parms that were already written (distance 140..140, height
75..75).
