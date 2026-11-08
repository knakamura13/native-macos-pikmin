# Combat timing against the original game (issues #154 and #180)

Status: **measured and partly explained; not resolved.** The public scenarios assert only smoke
limits (`bulborb_fight`: the Dwarf Bulborb dies within 10 s, a Flick lasts 4 s or less). Those limits
do not establish parity with the original game and nothing here claims it.

## What is compared

`bulborb_fight` (ten blue Pikmin, one Dwarf Bulborb, the real enemy AI, no state poked) on a
user-supplied GPIE01 revision 1 install (stage 1, the stage's first Dwarf Bulborb), run through the
local exact lane at 30, 60 and 120 Hz. The 30 Hz run follows the original game's tick; 60 and 120 Hz
exist only in this port, so the question is whether the faster ticks keep the 30 Hz behaviour.
Tool: `sample_combat_rate.py <game> --data <install>`; the scenario prints one `HIT` line per
point of damage (time, damage, Pikmin on the Bulborb's back).

The runs are fully deterministic: 24 RNG prerolls (`PIKMIN_NETPLAY_TEST_PREROLL_RAND`) give
byte-identical results (standard deviation 0.00 for every column), so the differences below are
systematic, not sampling noise.

## Measured on the disc install (before any change)

| Tick rate | Bulborb dead | Damage in the first 1.5 s | Flicks | Mean Flick | Hits on the ground / stuck on its back |
| --- | --- | --- | --- | --- | --- |
| 30 Hz | 3.10 s | 50 | 3 | 3.19 s | 11 / 2 |
| 60 Hz | 2.20 s | 70 | 3 | 3.22 s | 9 / 4 |
| 120 Hz | not finished (see below) | 110 | not finished | not finished | not measured |

Every hit lands (no `stimulate` returned false), and the total is 13 hits at 30 and 60 Hz, so
retries inside the attack key window are not the cause. Flick length is rate independent. The
damage rate is not: the same Pikmin land the same hits 0.9 s sooner at 60 Hz.

At 120 Hz the scenario aborts after the kill with `ActTransport::crInit` raising its `ERROR` (a
Pikmin starts carrying the corpse). `bulborb_fight` is registered at 30 and 60 Hz only; the abort
is recorded here and not investigated further.

## Cause found so far

`ActJumpAttack::exec` (`aiAttack.cpp`) has three per-tick behaviours that the port's tick-rate
feature does not scale (the convention elsewhere is `PC_TICKS30`, for example `aiCrowd.cpp`):

1. facing smoothing `mFaceDirection += 0.2f * angle` in the approach and jump-setup states;
2. the per-tick 10% roll to jump from a ground approach;
3. the per-tick 30% roll to adjust the jump when close.

Experiment (fork branch `knakamura/combat-rate-scaling-experiment-154`, one commit on `aiAttack.cpp`): scaling (1) with `1 - 0.8^(1/PC_TICKS30)` and (2), (3) to `p / PC_TICKS30` moves the
60 Hz kill time from 2.20 s to 2.70 s (30 Hz: 3.10 s) and the hit mix from 9/4 to 7/7. The residual
0.4 s is **not explained**. A per-Pikmin state trace of the experiment (2026-11-08) shows more Pikmin
stuck on the Bulborb at 60 Hz (up to 5 against 3 at 30 Hz, hits from the back 7 against 2), which fits
finer contact sampling during the jump (`wantToStick`, collision tested every tick) catching contacts a
30 Hz tick tunnels past; this is a hypothesis, not tested. Velocity smoothing is already
`deltaTime`-scaled (`Creature::moveVelocity`), so it is not the cause. The experiment is not part of the upstream package because it is incomplete and changes
gameplay at 60/120 Hz; it needs Kyle's decision before any upstream action.

## Public stand-in fixture

The authored fixture (source-default parameters, authored attack schedules, `standin_anims.h`)
shows the same direction: the Bulborb dies at 1.9 s (30 Hz) and 1.1 s (60 Hz), with no Flick. That
is a characterisation of the fixture, not a measurement of the game.

## Next steps

1. Bisect the residual with the same per-hit trace: Pikmin walk smoothing first, then the stick path.
2. When the 30 and 60 Hz disc runs agree within an agreed bound, add an exact-lane test with that
   bound (the disc-relative limits are a measured expected output and stay out of the public
   fixtures), then port the same scaling to the other per-tick rolls and smoothing in the AI.
3. Investigate the 120 Hz `ActTransport::crInit` abort separately.
