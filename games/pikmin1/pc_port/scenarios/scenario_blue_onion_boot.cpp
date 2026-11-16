// Blue-Onion boot-then-carry reproduction (#100 W2): do not skip the boot,
// call pcSnapLanded, or write waypoint flags. Start the original boot API and
// let the real animator/GoalAI emit the first seed and return to Wait. Its route
// must then be open and two Pikmin must deliver a pellet. The separate red
// control runs the same checks with the red boot's known waypoint effect key.
// This isolates animation/routing, not the discovery trigger or movie replay.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "Demo.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "GoalItem.h"
#include "ItemAI.h"
#include "NaviMgr.h"
#include "Pellet.h"
#include "PelletState.h"
#include "PlayerState.h"
#include "Route.h"
#include "gameflow.h"
#include "scenarios/scenario_carry_common.h"

namespace {

class OnionBootCarry : public PcScenario {
public:
	explicit OnionBootCarry(int colour) : mColour(colour) { }
	const char* name() const override { return mColour == Blue ? "blue_onion_boot" : "red_onion_boot"; }
	int startDay() const override { return 2; }
	const char* stageFile() const override
	{
		return mColour == Blue ? "stages/sandbox_objects.ini" : "stages/sandbox_onions.ini";
	}

	void onStageReady() override
	{
		if (!carry::isStandInTree()) {
			scenarioFailf(0, "the selected stand-in stage", "%s", pc_scenario_requested_stage_file());
			return;
		}
		mOnion = itemMgr->getContainer(mColour);
		if (!mOnion) {
			scenarioFailf(0, "the stage's target Onion", "colour %d missing", mColour);
			return;
		}
		mWay = routeMgr->getWayPoint('test', mOnion->getRouteIndex());
		const int state = C_SAI(mOnion)->getCurrID(mOnion);
		if (!mWay || mWay->mIsOpen || playerState->hasBootContainer(mColour)) {
			scenarioFailf(0, "an Onion with no boot bit and its route closed", "colour %d state %d waypoint %d open %d boot bit %d",
			              mColour, state, (int)(mWay != nullptr), mWay ? (int)mWay->mIsOpen : -1,
			              (int)playerState->hasBootContainer(mColour));
			return;
		}
		std::printf("[scenario] %s: pre-boot AI state %d, boot bit clear, route closed\n", name(), state);
		mBornBeforeBoot = GameStat::bornPikis[mColour];
		// The normal Boot movie event calls this API. No state or waypoint poke.
		mOnion->startBoot();
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		// Close unrelated first-seed tutorial text, without skipping a movie.
		if (gameflow.mIsTutorialTextActive && t % 6 == 0) pad.button = PAD_BUTTON_A;
	}

	void onTick(unsigned t) override
	{
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		if (mPhase == Boot) {
			const int state = C_SAI(mOnion)->getCurrID(mOnion);
			const int seeds = GameStat::bornPikis[mColour] - mBornBeforeBoot;
			if (state == GoalAI::GOAL_Wait && seeds == 1) {
				std::printf("[scenario] %s: natural boot finished at %.2f s, one seed, waypoint open %d\n",
				            name(), t / float(rate), (int)mWay->mIsOpen);
				if (!mWay->mIsOpen) {
					scenarioFailf(t, "natural Onion boot opens its route before carry", "colour %d returned to Wait after one seed but waypoint stayed closed", mColour);
					return;
				}
				// Same open-floor carry spot/helpers as carry_blocked/pellet_carry.
				mPellet = carry::spawnPellet(carry::id("pr01"), -300.0f, -60.0f);
				if (!mPellet) {
					scenarioFailf(t, "a red 1-pellet", "spawn failed");
					return;
				}
				carry::spawnRow(naviMgr->getNavi(), mColour, 2, mPellet->mSRT.t.x + 25.0f, mPellet->mSRT.t.z, mLanding);
				if (mLanding.size() != 2) {
					scenarioFailf(t, "two same-colour carriers", "%zu born", mLanding.size());
					return;
				}
				mPhase = Carry;
				mCarryTick = t;
				return;
			}
			if (seeds > 1 || t > 30 * rate) {
				scenarioFailf(t, "natural boot returns to Wait with exactly one seed within 30 s", "colour %d state %d seeds %d waypoint open %d",
				              mColour, state, seeds, (int)mWay->mIsOpen);
			}
			return;
		}
		carry::land(mLanding);
		if (!mWay->mIsOpen) {
			scenarioFailf(t, "naturally opened route stays open during carry", "closed after boot");
			return;
		}
		if (!mPellet->isAlive() || mPellet->getState() == PELSTATE_Dead) {
			if (mPellet->mTargetGoal != mOnion) {
				scenarioFailf(t, "pellet routed to the naturally booted Onion", "different target");
				return;
			}
			std::printf("[scenario] %s: pellet delivered %.2f s after carry started\n", name(), (t - mCarryTick) / float(rate));
			scenarioPass();
		} else if (t - mCarryTick > 90 * rate) {
			scenarioFailf(t, "two Pikmin deliver the pellet within 90 s after natural boot", "colour %d state %d carriers %d at (%.0f, %.0f)",
			              mColour, mPellet->getState(), (int)mPellet->mCarrierCounter, mPellet->mSRT.t.x, mPellet->mSRT.t.z);
		}
	}

private:
	enum Phase { Boot, Carry };
	int mColour;
	Phase mPhase = Boot;
	GoalItem* mOnion = nullptr;
	WayPoint* mWay = nullptr;
	Pellet* mPellet = nullptr;
	int mBornBeforeBoot = 0;
	unsigned mCarryTick = 0;
	std::vector<Piki*> mLanding;
};

} // namespace

PcScenario* pc_scenario_make_blue_onion_boot(void)
{
	static OnionBootCarry s(Blue);
	return &s;
}

PcScenario* pc_scenario_make_red_onion_boot(void)
{
	static OnionBootCarry s(Red);
	return &s;
}

#endif
