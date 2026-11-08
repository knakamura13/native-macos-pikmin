// save_load_repair: a save whose yellow Onion was booted but never unlocked, with no
// yellow Pikmin, seed or sprout anywhere, loaded and played. The load repair
// (pc_port/save/pc_progression_game.h) must give that Onion one seed.
//
// Such a save shows the Onion as found and booted, so the discovery never plays again,
// while no yellow Pikmin exists that could ever be plucked: the colour can never be
// unlocked. The repair's rule for it (pc_progression_repair.h, "Population") is one
// seed from the Onion once the stage is built, never the unlock itself.
//
// On the Onion stand-in stage (stages/sandbox_onions.ini), as onion_day3:
//   Day 2    Once the intro is over, the scenario sets the yellow boot bit and
//            DEMOFLAG_DiscoverYellowOnyon (what the discovery movie sets), and leaves the
//            container and display bits clear. Nothing yellow exists on this stage. The
//            clock jumps to just before the end hour; A closes the texts, the diary, the
//            results and answers Yes to the save (the memory-card check makes the card file,
//            slot A). Asserted: the soft reset goes to MapSelect on Day 3 and the save ran.
//   Load     At map select the card file is read back from disk and slot A loaded as the
//            file select does (scen_save::reloadCard, scen_save::loadSlot). Asserted: the
//            loaded game is the broken one (Day 3, yellow booted and discovered, not
//            unlocked, no yellow in the Onion store). The course is the same stage again.
//   Day 3    The stage is built from the load: the yellow Onion comes from the `next`
//            generator (yellow is not owned) and, booted, starts in GOAL_Wait. Asserted
//            within 10 s of the stage start: exactly one yellow seed exists, and yellow is
//            still not unlocked (the seed is the repair; the first pluck unlocks the colour
//            as in normal play).
//
// Not covered: the other repair rules (the unit test pc_progression_scenario_test runs
// them on .scn fixtures), the note the repair shows, and the backup made before the next save.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>

#include "BaseInf.h"
#include "Demo.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "GoalItem.h"
#include "ItemMgr.h"
#include "MemoryCard.h"
#include "MoviePlayer.h"
#include "OnePlayerSection.h"
#include "Piki.h"
#include "PikiHeadItem.h"
#include "PikiMgr.h"
#include "PlayerState.h"
#include "SimpleAI.h"
#include "gameflow.h"
#include "system.h"
#include "scenarios/scenario_progress.h"
#include "scenarios/scenario_save_common.h"

namespace {

constexpr const char* kStage    = "stages/sandbox_onions.ini";
constexpr unsigned kSunsetTicks = 60;
constexpr unsigned kToResetSeconds = 75;
constexpr unsigned kSeedSeconds    = 10;

int yellowSeeds()
{
	int n = 0;
	Iterator it(itemMgr->getPikiHeadMgr());
	CI_LOOP(it)
	{
		PikiHeadItem* seed = static_cast<PikiHeadItem*>(*it);
		if (seed && seed->mSeedColor == Yellow) n++;
	}
	return n;
}

int yellowPikmin()
{
	int n = 0;
	Iterator it(pikiMgr);
	CI_LOOP(it)
	{
		Piki* p = static_cast<Piki*>(*it);
		if (p && p->isAlive() && p->mColor == Yellow) n++;
	}
	return n;
}

class SaveLoadRepair : public PcScenario {
public:
	const char* name() const override { return "save_load_repair"; }
	int startDay() const override { return 2; }
	bool crossesDays() const override { return true; }
	const char* stageFile() const override { return kStage; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase != ToReset) return;
		// The first-noon text before sunset; after the day advance the diary page, the
		// results, the save prompt and the card screens, each waiting for A (world_map_day_end).
		const bool advanced = gameflow.mWorldClock.mCurrentDay == 3;
		if (advanced ? t % 6 == 0 : (gameflow.mIsUIOverlayActive && t % 15 == 0)) pad.button |= PAD_BUTTON_A;
	}

	void onStageReady() override
	{
		if (pc_scenario_stage_number() == 2) enter(Check, mLastTick);
	}

	const char* onMapSelect() override
	{
		const unsigned t = mLastTick;
		if (mPhase != Between) {
			fail(t, "map select only after the Day 2 day end", "phase %d", (int)mPhase);
			return nullptr;
		}
		CardQuickInfo infos[4];
		if (!scen_save::reloadCard(t, name(), infos)) {
			mPhase = Done;
			return nullptr;
		}
		const CardQuickInfo& a = infos[0];
		if (a.mMemCardSaveIndex < 0 || a.mSaveStatus != PlayState::ReadyToSave || a.mCurrentDay != 3) {
			fail(t, "slot A read from disk holds the Day 3 save", "block %d status %u day %d", a.mMemCardSaveIndex, a.mSaveStatus, a.mCurrentDay);
			return nullptr;
		}
		scen_save::loadSlot(a);
		const int store = pikiInfMgr.getColorTotal(Yellow);
		std::printf("[scenario] %s: slot A loaded: day %d, container 0x%02x display 0x%02x, yellow discovered %d, %d yellow in the Onion store\n", name(),
		            gameflow.mWorldClock.mCurrentDay, playerState->mContainerFlag, playerState->mDisplayPikiFlag,
		            (int)playerState->mDemoFlags.isFlag(DEMOFLAG_DiscoverYellowOnyon), store);
		if (gameflow.mWorldClock.mCurrentDay != 3 || !playerState->hasBootContainer(Yellow) || playerState->hasContainer(Yellow)
		    || !playerState->mDemoFlags.isFlag(DEMOFLAG_DiscoverYellowOnyon) || store != 0) {
			fail(t, "the loaded game is Day 3 with yellow booted and discovered, not unlocked, and no yellow in the Onion store",
			     "day %d, container 0x%02x, discovered %d, store %d", gameflow.mWorldClock.mCurrentDay, playerState->mContainerFlag,
			     (int)playerState->mDemoFlags.isFlag(DEMOFLAG_DiscoverYellowOnyon), store);
			return nullptr;
		}
		return kStage;
	}

	void onTick(unsigned t) override
	{
		mLastTick           = t;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		WorldClock& clock   = gameflow.mWorldClock;
		switch (mPhase) {
		case Settle:
		{
			if ((gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive) || gameflow.mIsUIOverlayActive) return;
			if (clock.mCurrentDay != 2 || playerState->isTutorial() || playerState->hasContainer(Yellow) || GameStat::allPikis[Yellow] != 0
			    || yellowSeeds() != 0 || pikiInfMgr.getColorTotal(Yellow) != 0) {
				fail(t, "Day 2, not the tutorial, yellow not owned and nothing yellow anywhere", "day %d, tutorial %d, container 0x%02x, yellow %d, seeds %d, store %d",
				     clock.mCurrentDay, (int)playerState->isTutorial(), playerState->mContainerFlag, (int)GameStat::allPikis[Yellow], yellowSeeds(),
				     pikiInfMgr.getColorTotal(Yellow));
				return;
			}
			playerState->setBootContainer(Yellow);
			playerState->mDemoFlags.setFlagOnly(DEMOFLAG_DiscoverYellowOnyon);
			scenario_progress::jumpToSunset(kSunsetTicks);
			std::printf("[scenario] %s: day 2: yellow booted and discovered, not unlocked (container 0x%02x); clock %.4f h\n", name(),
			            playerState->mContainerFlag, clock.mTimeOfDay);
			enter(ToReset, t);
			return;
		}
		case ToReset:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: soft reset at tick %u toward section %d on day %d, save game %d\n", name(), t, next, clock.mCurrentDay,
				            (int)gameflow.mGamePrefs.mHasSaveGame);
				if (next != ONEPLAYER_MapSelect || clock.mCurrentDay != 3 || !gameflow.mGamePrefs.mHasSaveGame || gameflow.mMemoryCard.mDidSaveFail) {
					fail(t, "the day end saves and soft-resets to MapSelect (6) on Day 3", "section %d, day %d, saved %d, failed %d", next, clock.mCurrentDay,
					     (int)gameflow.mGamePrefs.mHasSaveGame, (int)gameflow.mMemoryCard.mDidSaveFail);
					return;
				}
				mPhase = Between; // the runner stops ticking until the next stage is ready
			} else if (t - mPhaseTick > kToResetSeconds * rate) {
				fail(t, "the day end and its soft reset within 75 s of the clock jump", "day %d, %.4f h", clock.mCurrentDay, clock.mTimeOfDay);
			}
			return;
		case Check:
		{
			const int seeds = yellowSeeds(), pikmin = yellowPikmin();
			if (seeds + pikmin == 0) {
				if (t - mPhaseTick > kSeedSeconds * rate) {
					GoalItem* onion = itemMgr->getContainer(Yellow);
					fail(t, "the load repair gives the booted yellow Onion with no yellow anywhere one seed within 10 s of the stage start",
					     "no yellow seed or Pikmin; yellow Onion %d, state %d, container 0x%02x", (int)(onion != nullptr),
					     onion && onion->mSAICtx.mStateMachine ? C_SAI(onion)->getCurrID(onion) : -1, playerState->mContainerFlag);
				}
				return;
			}
			std::printf("[scenario] %s: day 3, %.2f s after the stage start: %d yellow seed(s), %d yellow Pikmin, container 0x%02x\n", name(),
			            (t - mPhaseTick) / float(rate), seeds, pikmin, playerState->mContainerFlag);
			if (seeds != 1 || pikmin != 0 || playerState->hasContainer(Yellow)) {
				fail(t, "exactly one yellow seed, no yellow Pikmin, yellow still not unlocked", "seeds %d, Pikmin %d, container 0x%02x", seeds, pikmin,
				     playerState->mContainerFlag);
				return;
			}
			mPhase = Done;
			scenarioPass();
			return;
		}
		default: return;
		}
	}

private:
	enum Phase { Settle, ToReset, Between, Check, Done };

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mPhase = Done;
		scenarioFailf(t, expected, fmt, args...);
	}

	Phase mPhase        = Settle;
	unsigned mPhaseTick = 0, mLastTick = 0;
};

} // namespace

PcScenario* pc_scenario_make_save_load_repair(void)
{
	static SaveLoadRepair s;
	return &s;
}

#endif
