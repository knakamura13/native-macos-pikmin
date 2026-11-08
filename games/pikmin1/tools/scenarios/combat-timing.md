# Combat timing against the original game (issues #154 and #180)

Status: **measured, cause found in three per-tick behaviours, divergence cut from 0.9 s to 0.2 s at 60 Hz on the default start, but only from 0.76 s to 0.60 s averaged over 16 starts; not resolved.** The public scenarios assert only smoke
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
The 120 Hz abort after the kill is explained: it is the original `ERROR("zannnen")` in `ActTransport::decideGoal` (an lldb break on `_Error`), raised when the Pikmin carrying the corpse are blue and the stage has no blue Onion. The scenario puts blue Pikmin on the Day-1 disc stage, so this is a scenario artifact reached sooner because the kill comes sooner; it is not a tick-rate bug.

## Sampling over start positions (2026-11-08, corrects the single-start numbers above)

One deterministic start is a single trajectory, so the 3.13 / 2.93 / 2.72 s above overstate what the fix does. Sixteen
runs per rate with the squad's start moved 0 to 45 units along x in steps of 3 (`PIKMIN_FIGHT_SHIFT`,
`sample_combat_rate.py --seeds 16 --shift-step 3`), mean kill time on the disc install:

| Tick rate | Before the fix | After the fix |
| --- | --- | --- |
| 30 Hz | 3.44 s (sd 0.72) | 3.44 s (unchanged by design) |
| 60 Hz | 2.68 s (sd 0.45) | 2.84 s (sd 0.47) |
| 120 Hz | not run | 2.71 s (sd 0.40) |

Averaged over starts the gap at 60 Hz falls from 0.76 s to 0.60 s, about 3 standard errors from zero: a real divergence of
roughly 17% remains, and its cause is **not** found. The fix is correct as far as it goes (three per-tick behaviours that
were demonstrably unscaled) but does not achieve parity; `scenario_combat_rate_disc` pins the default start only.

## Where the remaining gap sits (state trace, 8 starts, parity fix, disc install)

A tick-stamped trace of `ActJumpAttack`'s state changes per Pikmin shows the time inside its states is the same at both
rates (jump 0.47 s and 0.47 s; jump set-up 0.37 s and 0.33 s; ground attack 0.73 s and 0.71 s), so the gap is not in them.
It sits outside: the first ground attack starts at 0.70 s at 30 Hz and 0.57 s at 60 Hz (the approach is 0.13 s shorter), and
a run enters the jump set-up 12.6 times at 30 Hz against 10.1 at 60 Hz (kill 3.23 s against 2.59 s). The next place to
look is the walk and approach before and between attacks (`ActAttack`, `ActFree` target search, walking and facing in
`Piki`/`Creature::moveVelocity`, whose velocity lag `v += (target - v) * dt / acceleration` is an explicit Euler step and
so differs between 30 and 60 Hz), and what the Bulborb does in the same time. Not tested.

## Residual bisect (2026-11-08, disc install, parity fix plus each experiment)

Mean kill time over 16 starts (`--seeds 16 --shift-step 3`), 30 Hz = 3.44 s throughout:

| Experiment | 60 Hz mean | Result |
| --- | --- | --- |
| Parity fix alone | 2.84 s | baseline for the rows below |
| `Creature::moveVelocity` velocity lag compounded per 30 Hz step | 2.83 s | refuted: no effect |
| Hit test in `ActJumpAttack` states 4 and 5 only on 30 Hz ticks (retries inside the attack window) | 2.93 s | refuted: inside the noise (se about 0.12 s) |
| `UpdateMgr` slot rotation advanced once per 30 Hz step (see below) | 3.18 s | real effect: gap 0.60 s to 0.26 s |

`UpdateMgr::update()` advances its slot index once per tick. Pikmin decisions that are spread over slots
(`pikiUpdateMgr` has 10 slots, so a Pikmin's target search in `ActFree::exec` runs once per 10 frames; the look manager 20,
the optimisation managers 2) therefore ran twice as often in real time at 60 Hz and four times at 120 Hz. This is the
"approach is 0.13 s shorter" effect above. The change advances the index once per `PC_TICKS30` ticks and reports a slot
updatable only on the tick it advanced, so each client still runs once per rotation; at 30 Hz both are the original code.
Evidence it leaves 30 Hz alone: all 114 `_30` state-hash logs are byte-identical with and without the change, and
`scenario_combat_rate_disc` passes. The patch is on the parity branch (not in the package).

Wider sample, 48 starts (`--seeds 48 --shift-step 1`) with the `UpdateMgr` change: kill 3.64 s (sd 1.18) at 30 Hz, 3.19 s
(sd 0.70) at 60 Hz, 2.86 s (sd 0.54) at 120 Hz. The 60 Hz gap is 0.45 s (12%), the 120 Hz gap 0.78 s (21%); standard error
of a difference is about 0.19 s, so the 60 Hz gap is about 2.4 standard errors. Parity is **not** reached. New and
unexplained: the Flick count per kill is 2.8 at 30 Hz but 4.2 at 60 Hz and 4.1 at 120 Hz (Flick needs a damage count,
which is event-based, so a rate effect in who is on the Bulborb or in the Flick window is suspected). There is no 48-start
baseline without the `UpdateMgr` change, so the improvement is quoted from the 16-start table only. The 120 Hz runs end
non-zero in 32 of 48 cases from the scenario artifact above (blue Pikmin, no blue Onion), not from the kill.

Largest sample, 96 starts (`--seeds 96 --shift-step 0.5`, same build): kill 3.59 s (sd 1.12, median 3.35) at 30 Hz and
3.17 s (sd 0.73, median 3.12) at 60 Hz. The mean gap of 0.42 s (12%) is about 3.1 standard errors, the median gap 0.23 s
(7%); damage in the first 1.5 s is 45.5 against 50.7 (11% higher at 60 Hz). The 30 Hz distribution has a heavy tail, which
is part of the gap. The residual is real and not explained. The Flick figures (2.6 against 4.0 per run) come from the
scenario's end-of-run summary, which aborted runs omit (5 against 11 runs), so they are not a clean comparison; the one-start
trace at shift 7 shows 3 Flicks at both rates.

## Where the gap sits after the slot-rotation fix (2026-11-08, 64 starts, disc install)

Mean time of the n-th hit (`bulborb_fight` HIT trace; 30 Hz then 60 Hz): first 0.917 / 0.928 s, third 1.110 / 1.191 s,
seventh 1.786 / 1.590 s, kill 3.115 / 2.971 s. The first hit is now equal, so the approach gap found earlier is closed.
The gap opens in the middle of the fight: hits 3 to 7 take 0.68 s at 30 Hz and 0.40 s at 60 Hz.

Pikmin with `mStickTarget == Bulborb` at each hit (the scenario's "on its back", hits 1 to 13): at 30 Hz 0.3 0.9 1.4 1.7 2.1
2.1 2.0 2.3 2.5 2.5 2.9 2.6 4.0; at 60 Hz 1.8 2.2 2.6 2.8 3.2 3.6 3.8 3.8 3.4 3.3 3.1 2.8 2.5. So at 60 Hz more Pikmin are
attached earlier and for longer, which is the mid-fight damage rate. Not the cause: gating the jump-contact decision in
`ActJumpAttack::procCollideMsg` to a plain global 30 Hz tick cadence (instead of the ballistic clock, which never advances
for a Pikmin off the reference-motion path) leaves the counts and the mean kill time unchanged (60 Hz mean 3.15 s against
3.17 s over 96 starts). The animation clock is not the cause either: `pc_anim_dt` returns the raw frame time in deterministic
scenario runs, and key events fire once on crossing.

Next: find which code attaches the extra Pikmin (a tick-rate-dependent path other than that handler, or contact geometry
that differs because a finer step samples positions the 30 Hz step skips) by logging each `startStick`/`startStickObject`
call with its caller and tick at both rates for one start.

## Public stand-in fixture

The authored fixture (source-default parameters, authored attack schedules, `standin_anims.h`)
shows the same direction: the Bulborb dies at 1.9 s (30 Hz) and 1.1 s (60 Hz), with no Flick. That
is a characterisation of the fixture, not a measurement of the game.

## Next steps

1. Explain the Flick count difference (4.2 against 2.8), then re-measure the gap with a 48-start baseline taken before and after the `UpdateMgr` change.
2. When the 30 and 60 Hz disc runs agree within an agreed bound, add an exact-lane test with that
   bound (the disc-relative limits are a measured expected output and stay out of the public
   fixtures), then port the same scaling to the other per-tick rolls and smoothing in the AI.
3. The 120 Hz abort needs no port change; the scenario should carry the Onion colour the stage owns (or stop at the kill).
