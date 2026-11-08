# Combat timing against the original game (issues #154 and #180)

Status: **measured, cause found in three per-tick behaviours, divergence cut from 0.9 s to 0.2 s at 60 Hz on a fork branch; not fully resolved.** The public scenarios assert only smoke
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

## Cause and fix (branch `knakamura/combat-rate-parity-154`)

`ActJumpAttack` (`aiAttack.cpp`) has per-tick behaviour that the port's tick-rate feature did not scale:

1. facing smoothing `mFaceDirection += 0.2f * angle` in the approach and jump-setup states;
2. the per-tick 10% roll to jump from a ground approach and the per-tick 30% roll to adjust the jump;
3. the jump's contact test (`procCollideMsg`) runs every tick, so at 60 and 120 Hz a jumping Pikmin sticks to contacts
   that a 30 Hz tick moves past (the trace shows up to 5 Pikmin on the Bulborb's back against 3 at 30 Hz).

The branch scales (1) with `1 - 0.8^(1/PC_TICKS30)` and (2) to `p / PC_TICKS30`, and takes the stick decision only on
the original 30 Hz boundaries (`PcBallisticClock::boundary`), where the position equals the 30 Hz one.

| Tick rate | Bulborb dead before | after | Damage in first 1.5 s before | after |
| --- | --- | --- | --- | --- |
| 30 Hz | 3.10 s | 3.13 s | 50 | 50 |
| 60 Hz | 2.20 s | 2.93 s | 70 | 40 |
| 120 Hz | aborts after the kill | 2.72 s (then aborts) | 110 | 60 |

Contact sampling alone moved 60 Hz from 2.20 s to 2.80 s; the smoothing and roll scaling alone, to 2.70 s; both,
to 2.93 s. A divergence of about 0.2 s per doubling of the rate remains (mean Flick 3.19 s at 30 Hz, 3.68 s at 60 Hz;
the knock-down time is a random draw), unexplained: the other per-tick behaviours of the Pikmin and enemy AI were not
audited. The exact-lane test `scenario_combat_rate_disc` records the measured bounds (kill within 0.35 s, damage in
the first 1.5 s within 20, equal flick count) so the gap cannot grow back; it does not claim parity. The change alters
gameplay at 60 and 120 Hz and its stand-in hash logs, so it is not in the upstream package and needs Kyle's decision.
The 120 Hz abort in `ActTransport::crInit` after the kill is a separate finding.

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
