// attack_rate: how often a Pikmin attacking a Dwarf Bulborb at close range decides to jump, per game
// second, on the stand-in object stage (the first Dwarf Bulborb, as in bulborb_fight).
//
// ActJumpAttack's ground state (state 0), with the Pikmin inside the target's reach (dist2D below
// the attack size) and not facing it closely enough to attack, draws one random number per AI step
// and moves to the jump approach (state 2) when it is above 0.7: a 30% chance per original 30 Hz
// frame, 9 per second. At 60 Hz the port draws twice per 30 Hz frame, so the threshold is scaled to
// keep the chance per second (PC_ATTACK_RATE in aiAttack.cpp). The same macro scales the ground turn
// and the touch-and-jump chance; the touch flag (_2C) is set only against a Pikmin, so against an
// enemy this draw is the only random one in state 0. The turn is not asserted: other code turns the
// Pikmin in the same tick.
//
// Set-up: one blue Pikmin is born 36 units from the Bulborb and handed to actOnSituaton (as in
// bulborb_fight; AIConstant p08 and p10 off, so it keeps attacking). Once its jump attack on the
// Bulborb is in state 0, the scenario holds it every tick (test controls): at half the attack size
// from the attack position, with no velocity, facing 90 degrees away from it, so the attack branch
// (angle below PI/10) never runs and every tick is one draw. A tick that moves to state 2 counts as
// a jump, and the scenario sets the state back to 0. No attack lands, so the Bulborb stays asleep.
// Each measured tick must leave the Pikmin alive, in the same jump attack on the Bulborb, in state 0
// or 2.
//
// Asserted: over kSeconds of game time, jumps per game second within kTolerance of 9. The count is
// binomial (1200 draws at 60 Hz, about 180 jumps, a standard deviation near 7%), so the bounds are
// more than four deviations from 9; a chance not scaled to the rate (30% per 60 Hz tick, 18 per
// second) is far outside them. Registered at 60 Hz only: at 30 Hz the macro returns the original
// constants. Measured (stand-in tree, macOS arm64): 8.30 per second at 30 Hz, 8.35 at 60 Hz.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>
#include <vector>

#include "AIConstant.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "Piki.h"
#include "PikiAI.h"
#include "scenarios/scenario_carry_common.h"
#include "sysMath.h"
#include "teki.h"

namespace {

constexpr float kSeconds      = 20.0f; // measured game time
constexpr float kStartS       = 10.0f; // bound for the jump attack to start
constexpr float kExpectedRate = 9.0f;  // jumps per second: 30% of 30 frames
constexpr float kTolerance    = 0.3f;  // relative
constexpr float kHalfPi       = 1.5707963f;

// Read and reset ActJumpAttack's protected state (member pointers through a derived name).
struct JumpPeek : ActJumpAttack {
	static int& state(ActJumpAttack& a) { return a.*&JumpPeek::mState; }
	static Creature* target(ActJumpAttack& a) { return (a.*&JumpPeek::mTarget).getPtr(); }
};

// The Pikmin's jump attack, or null when it is doing something else.
ActJumpAttack* jumpAttack(Piki* p)
{
	TopAction* top = p->mActiveAction;
	if (!top || top->mCurrActionIdx != PikiAction::Attack) return nullptr;
	Action* attack = top->getCurrAction();
	if (!attack || attack->mCurrActionIdx != ActAttack::CHILD_JumpAttack) return nullptr;
	return static_cast<ActJumpAttack*>(attack->mChildActions[ActAttack::CHILD_JumpAttack].mAction);
}

class AttackRate : public PcScenario {
public:
	const char* name() const override { return "attack_rate"; }

	void onStageReady() override
	{
		Iterator it(tekiMgr);
		CI_LOOP(it)
		{
			Teki* t = static_cast<Teki*>(*it);
			if (t->mTekiType == TEKI_Chappy && !mBulb) mBulb = t;
		}
		Navi* navi = naviMgr->getNavi();
		if (!mBulb || !navi) {
			fail(0, "a Dwarf Bulborb and a captain", "bulborb %d, captain %d", (int)(mBulb != nullptr), (int)(navi != nullptr));
			return;
		}
		const Vector3f at = mBulb->mSRT.t;
		navi->mSRT.t.set(at.x + 90.0f, mapMgr->getMinY(at.x + 90.0f, at.z, true), at.z);
		std::vector<Piki*> landing;
		carry::spawnRow(navi, Blue, 1, at.x + 36.0f, at.z, landing);
		if (landing.empty()) {
			fail(0, "one Pikmin born", "none", 0);
			return;
		}
		mPiki = landing[0];
		carry::land(landing);
		// As in bulborb_fight: the source defaults of p08 and p10 send a Pikmin back after one attack.
		AICONST._A4() = 0;
		AICONST._C4() = 0;
	}

	void providePads(unsigned, PcInputPad&) override {}

	void onTick(unsigned t) override
	{
		if (mDone) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		if (!mPiki->isAlive()) {
			fail(t, "the Pikmin stays alive", "dead (state %d)", mPiki->getState());
			return;
		}
		ActJumpAttack* ja = jumpAttack(mPiki);
		const int action  = mPiki->mActiveAction ? mPiki->mActiveAction->mCurrActionIdx : -2;
		if (!mJump) {
			if (ja && JumpPeek::state(*ja) == 0 && JumpPeek::target(*ja) == mBulb) {
				mJump = ja;
				const Vector3f pos = ja->getAttackPos();
				const float size   = ja->getAttackSize();
				Vector3f away      = mPiki->mSRT.t - pos;
				away.y             = 0.0f;
				const float d      = away.length();
				if (d < 0.001f) away.set(1.0f, 0.0f, 0.0f);
				else away = away * (1.0f / d);
				mHold.set(pos.x + away.x * size * 0.5f, 0.0f, pos.z + away.z * size * 0.5f);
				mHold.y = mapMgr->getMinY(mHold.x, mHold.z, true);
				std::printf("[scenario] attack_rate: jump attack in state 0 at tick %u; attack size %.1f, held %.1f from the attack position\n", t, size, size * 0.5f);
				hold();
			} else if (t > unsigned(kStartS * rate)) {
				fail(t, "the Pikmin's jump attack on the Bulborb in state 0 within 10 s", "action %d, jump attack %d", action, (int)(ja != nullptr));
			}
			return;
		}
		if (ja != mJump || JumpPeek::target(*ja) != mBulb) {
			fail(t, "the Pikmin keeps its jump attack on the Bulborb", "action %d, same jump attack %d", action, (int)(ja == mJump));
			return;
		}
		int& state = JumpPeek::state(*ja);
		if (state != 0 && state != 2) {
			fail(t, "a held Pikmin stays in attack state 0 or moves to 2", "state %d", state);
			return;
		}
		mDraws++;
		if (state == 2) {
			mJumps++;
			state = 0;
		}
		if (mDraws >= unsigned(kSeconds * rate)) {
			finish(t, rate);
			return;
		}
		hold();
	}

private:
	void hold()
	{
		mPiki->mSRT.t = mHold;
		mPiki->mVelocity.set(0.0f, 0.0f, 0.0f);
		mPiki->mTargetVelocity.set(0.0f, 0.0f, 0.0f);
		const Vector3f d      = mJump->getAttackPos() - mHold;
		mPiki->mFaceDirection = roundAng(atan2f(d.x, d.z) + kHalfPi);
	}

	void finish(unsigned t, unsigned rate)
	{
		mDone              = true;
		const float secs   = mDraws / float(rate);
		const float perSec = mJumps / secs;
		std::printf("[scenario] attack_rate: %u jumps in %u draws (%.2f s): %.4f per tick, %.2f per second (expected %.1f +/- %.0f%%)\n", mJumps, mDraws, secs, mJumps / float(mDraws), perSec,
		            kExpectedRate, kTolerance * 100.0f);
		if (std::fabs(perSec - kExpectedRate) > kTolerance * kExpectedRate) {
			scenarioFailf(t, "close-range jumps per game second within 30% of 9 (30% per 30 Hz frame) at any tick rate", "%.2f per second (%u jumps in %u draws, %.4f per tick)", perSec,
			              mJumps, mDraws, mJumps / float(mDraws));
			return;
		}
		scenarioPass();
	}

	void fail(unsigned t, const char* expected, const char* fmt, int a, int b = 0)
	{
		mDone = true;
		scenarioFailf(t, expected, fmt, a, b);
	}

	Teki* mBulb          = nullptr;
	Piki* mPiki          = nullptr;
	ActJumpAttack* mJump = nullptr;
	Vector3f mHold;
	bool mDone      = false;
	unsigned mDraws = 0, mJumps = 0;
};

} // namespace

PcScenario* pc_scenario_make_attack_rate(void)
{
	return new AttackRate();
}

#endif
