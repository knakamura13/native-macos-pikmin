// playthrough_day1: one chained day of play on the stand-in tree, from the boot on the
// tutorial stand-in stage (stages/practice.ini, tools/scenarios/standin_practice_stage.h)
// to the next day on the stand-in Forest course (stages/stage1.ini). Each step is driven
// with pad input where the game reads one (walk, pluck, throw, whistle, the results
// screen and the world map) and asserted on game state, each within a bound in seconds
// (the same at 30 and 60 Hz), never on exact tick numbers:
//
//   Walk      The captain walks to the red Onion; DEMOFLAG_DiscoverRedOnyon and the
//             discovery movie boot it (GOAL_Wait).
//   Pluck     He plucks the seed it plants: one red Pikmin, in his party (Formation).
//   Throw     He walks to 60 units from the red 1-pellet and throws that Pikmin at it (A,
//             the cursor set just short of the pellet's edge as throw_flight sets a cursor).
//             The Pikmin flies, lands and takes
//             the pellet by itself (PikiFlyingState::procBounceMsg, Piki::actOnSituaton).
//   Carry     It carries the pellet into the red Onion: DEMOFLAG_CollectFirstPellet with the
//             Onion as the goal; the Onion plants one seed and the captain plucks it.
//   Whistle   The carrier waits by the Onion, out of the party; the captain whistles it
//             (B, the cursor on it): both red Pikmin are in his party.
//   Find      He walks to the Main Engine: within demoParms' trigger radius he sets
//             DEMOFLAG_ApproachEngine himself (Navi::demoCheck), the part's discovery.
//   Deliver   He throws both Pikmin at the engine: both land and take it, and two are not
//             enough (it needs kStandInEngineCarryMin, 12): for 2 s it stays put. The rest
//             of the carriers are born free beside it, as tutorial_day1 does (the stand-in
//             practice stage has one pellet, so the Onion cannot grow twelve), and handed to
//             Piki::actOnSituaton. They carry it to the ship: DEMOFLAG_CollectEngine, the
//             engine collected as a visible part (1 part, ship level 1, Forest open).
//   Day end   The ship-upgrade movie ends the tutorial day (as tutorial_day1); the day
//             advances once to Day 2. The twelve Pikmin on the field all go into the red
//             Onion and none is lost (GameCoreSection::exitDayEnd); none is left on the field.
//   Save      On the results screen the scenario answers Yes to the save and A elsewhere
//             (scen_save::ResultsDriver): the save manager runs (5, 6, 14), the save does
//             not fail, and the soft reset goes to MapSelect. The game in memory is
//             snapshotted when the save runs.
//   Reload    At map select the card file is read back from disk (the in-memory image
//             cleared first) and slot A loaded as the file select does (CardSelectSection's
//             reset, then loadCurrentGame). Slot A's quick info shows Day 2, one part and
//             the twelve red Pikmin, and the loaded game equals the snapshot field by field
//             (scen_save::compareSnapshots).
//   Next day  The real world map runs; the stick moves the cursor down to Forest of Hope
//             (as onion_day3) and the course is requested. Day 2 loads on stages/stage1.ini:
//             day 2, not the tutorial, the engine still on the ship (1 part), the red Onion
//             owned, booted and holding exactly the Pikmin that went in at the day end, no
//             Pikmin on the field; for 2 s the captain walks and the clock runs.
//
// playthrough_day1_nosave is the negative control: the same chain answering No to the
// save. It must fail at the Save step ("a save before the results screen closes").
//
// Evidence captures: with PIKMIN_SCENARIO_EVIDENCE_DIR set (CTest sets it), four PNGs of
// the step boundaries are written there (pc_scenario_screenshot_evidence); see the README.
// They are not compared with goldens.
//
// Not covered: the push box (tutorial_day1), the carriers' route and speed (only that they
// arrive within the bound), take-off and landing movies on a non-tutorial day (the stand-in
// stages play none).
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "BaseInf.h"
#include "Dolphin/pad.h"
#include "FlowController.h"
#include "GoalItem.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "OnePlayerSection.h"
#include "Parameters.h"
#include "Pellet.h"
#include "PelletState.h"
#include "PikiState.h"
#include "UfoItem.h"
#include "WorldClock.h"
#include "scenarios/pc_scenario_note.h"
#include "scenarios/pc_scenario_screenshot.h"
#include "scenarios/scenario_save_common.h"
#include "scenarios/scenario_tutorial_common.h"
#include "settings/pc_settings.h"
#include "system.h"

namespace {

using namespace tutorial;

constexpr float kThrowStand = 60.0f; // the captain throws from this far (the cursor reaches p46 = 100)
constexpr float kWhistleStand = 40.0f;
constexpr float kPartySpread  = 80.0f; // a party standing behind the captain (two Pikmin stood 33 and 54 away)
constexpr const char* kNextCourse = "stages/stage1.ini";

class Playthrough : public PcScenario {
public:
	Playthrough(const char* name, bool save)
	    : mName(name)
	    , mSave(save)
	{
	}

	const char* name() const override { return mName; }
	bool crossesDays() const override { return true; }
	const char* stageFile() const override { return "stages/practice.ini"; }

	void onStageReady() override
	{
		if (pc_scenario_stage_number() == 1) {
			day1Ready();
		} else {
			day2Ready();
		}
	}

	const char* onMapSelect() override
	{
		const unsigned t = mLastTick;
		if (mPhase != Between) {
			fail(t, "map select only after the Day 1 results", "phase %d", (int)mPhase);
			return nullptr;
		}
		reload(t);
		return nullptr; // the real world map; providePadsBetweenStages picks the course
	}

	void providePadsBetweenStages(unsigned ticks, PcInputPad& pad) override
	{
		if (mPhase != Between) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		// As onion_day3: DrawWorldMap::modeStart takes about 1 s; from Impact Site (point 0)
		// Forest of Hope is LINK_Down (WorldMapCoursePointMgr::init); stick edges are clicks.
		if (ticks < rate) return;
		if (ticks < 6u * rate) {
			if (ticks % 20 == 0) pad.stickY = -72;
			return;
		}
		if (ticks >= 245u * rate / 30u) pc_scenario_request_map_course(kNextCourse);
	}

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase == Done || mPhase == Between) return;
		if (mPhase == Results) {
			mDriver.fill(pad);
			return;
		}
		Navi* navi = naviMgr ? naviMgr->getNavi() : nullptr;
		if (!navi) return;
		if (gameflow.mIsUIOverlayActive) {
			if (t % 6 == 0) pad.button = PAD_BUTTON_A; // text windows
			return;
		}
		const int state = navi->getCurrState()->getID();
		const bool free = (state == NAVISTATE_Walk || state == NAVISTATE_Idle) && currentMovie() == -1;
		switch (mPhase) {
		case ToOnion:
			if (free) steer(navi, mOnion->mSRT.t, pad, 0.0f);
			return;
		case Pluck:
			if (free && mSeed && mSeed->canPullout()) {
				const float len = steer(navi, mSeed->mSRT.t, pad);
				if (state == NAVISTATE_Walk && len < 12.0f && t % 4 == 0) pad.button = PAD_BUTTON_A; // in reach (15), released between clicks
			}
			return;
		case ToEngine:
			if (free) steer(navi, mEngine->mSRT.t, pad, 0.0f);
			return;
		case ThrowWalk:
			if (free) steer(navi, mStand, pad, 4.0f);
			return;
		case ThrowPress:
			// Aim as the stick would (throw_flight): the offset, its target and the world point.
			navi->mCursorPosition.set(mAim.x - navi->mSRT.t.x, 0.0f, mAim.z - navi->mSRT.t.z);
			navi->mCursorTargetPosition = navi->mCursorPosition;
			navi->mCursorWorldPos       = mAim;
			if (!mGrabbed) pad.button = PAD_BUTTON_A; // down until the grab, then up: the throw
			return;
		case Regroup:
			if (free && flatDistance(navi->mSRT.t, mCallAt) > kWhistleStand) {
				steer(navi, mCallAt, pad, kWhistleStand);
				return;
			}
			// The whistle (whistle_delay): the cursor on the waiting Pikmin, B held while the circle grows.
			navi->mCursorPosition.set(mCallAt.x - navi->mSRT.t.x, 0.0f, mCallAt.z - navi->mSRT.t.z);
			navi->mCursorTargetPosition = navi->mCursorPosition;
			if (t - mPhaseTick < 2u * unsigned(pc_scenario_ticks_per_second())) pad.button = PAD_BUTTON_B;
			return;
		case Day2Walk:
			if (free) steer(navi, mDay2Target, pad, 0.0f);
			return;
		default: return;
		}
	}

	void onTick(unsigned t) override
	{
		mLastTick = t;
		if (mPhase == Done || mPhase == Between) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		const unsigned span = t - mPhaseTick;
		const int movie     = mPhase < Results || mPhase == Day2Walk ? currentMovie() : -1;
		const int day       = gameflow.mWorldClock.mCurrentDay;
		Navi* navi          = mPhase < DayOver || mPhase == Day2Walk ? naviMgr->getNavi() : nullptr;

		switch (mPhase) {
		case ToOnion:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_DiscoverRedOnyon)) {
				std::printf("[scenario] %s: Walk: DEMOFLAG_DiscoverRedOnyon at t=%u\n", mName, t);
				enter(OnionMovie, t);
			} else if (span > 15 * rate) {
				fail(t, "Walk: the captain reaches the red Onion within 15 s", "%.1f from it", flatDistance(mOnion->mSRT.t, navi->mSRT.t));
			}
			return;
		case OnionMovie:
			if (movie == -1 && !gameflow.mIsUIOverlayActive && C_SAI(mOnion)->getCurrID(mOnion) == GoalAI::GOAL_Wait) {
				std::printf("[scenario] %s: Walk: the red Onion is booted (GOAL_Wait) at t=%u\n", mName, t);
				startPluck(t, AfterFirstPluck);
			} else if (span > 40 * rate) {
				fail(t, "Walk: the red Onion discovery ends with the Onion booted within 40 s", "movie %d, Onion state %d", movie, C_SAI(mOnion)->getCurrID(mOnion));
			}
			return;
		case Pluck: tickPluck(t, span, rate, movie); return;
		case ThrowWalk:
			if (flatDistance(navi->mSRT.t, mStand) <= 6.0f && navi->getCurrState()->getID() == NAVISTATE_Walk && movie == -1 && !gameflow.mIsUIOverlayActive) {
				// The party catches up before the throw: as many Pikmin as are still to be thrown, in formation behind
				// him. A brings the nearest one to his hand (NaviThrowWaitState).
				if (++mStill >= rate && partyNear(navi, kPartySpread) >= mThrowsLeft) {
					mGrabbed = false;
					mThrown  = nullptr;
					enter(ThrowPress, t);
				}
			} else {
				mStill = 0;
			}
			if (mPhase == ThrowWalk && span > 30 * rate) {
				fail(t, "Throw: the captain reaches his throwing spot with his party within 30 s", "%.1f from it, %d of the party near; %s",
				     flatDistance(navi->mSRT.t, mStand), partyNear(navi, kPartySpread), describePikmin(navi).c_str());
			}
			return;
		case ThrowPress:
			if (!mGrabbed && navi->getCurrState()->getID() == NAVISTATE_ThrowWait
			    && static_cast<NaviThrowWaitState*>(navi->getCurrState())->mIsHoldingThrowPiki) {
				mGrabbed = true;
			}
			if (Piki* p = flyingPiki()) {
				mThrown = p;
				std::printf("[scenario] %s: Throw: a Pikmin is in the air at t=%u, aimed at (%.0f, %.0f)\n", mName, t, mAim.x, mAim.z);
				enter(ThrowFly, t);
			} else if (span > 5 * rate) {
				fail(t, "Throw: A throws a Pikmin within 5 s (it is Flying)", "captain state %d, grabbed %d", navi->getCurrState()->getID(), (int)mGrabbed);
			}
			return;
		case ThrowFly:
			if (mThrown->getState() != PIKISTATE_Flying) {
				std::printf("[scenario] %s: Throw: landed at (%.0f, %.0f), %.1f from the aim, mode %d, after %u ticks\n", mName, mThrown->mSRT.t.x,
				            mThrown->mSRT.t.z, flatDistance(mThrown->mSRT.t, mAim), (int)mThrown->mMode, span);
				enter(ThrowTake, t);
			} else if (span > 5 * rate) {
				fail(t, "Throw: the thrown Pikmin lands within 5 s", "still Flying");
			}
			return;
		case ThrowTake:
			// The landing hands the Pikmin to Piki::actOnSituaton: it takes the object it landed by.
			if (mThrown->mMode == PikiMode::TransportMode && mTarget->mCarrierCounter >= mCarriersBefore + 1) {
				mCarriersBefore = mTarget->mCarrierCounter;
				std::printf("[scenario] %s: Throw: the Pikmin took the %s at t=%u (%d carrying)\n", mName, mTarget == mPellet ? "pellet" : "engine", t,
				            (int)mTarget->mCarrierCounter);
				if (--mThrowsLeft > 0) {
					enter(ThrowWalk, t);
					return;
				}
				if (mTarget == mPellet) {
					evidence("02_pellet_carry.png");
					enter(PelletCarry, t);
				} else {
					enter(EngineFew, t);
				}
			} else if (span > 3 * rate) {
				fail(t, "Throw: the landed Pikmin takes the object it was thrown at within 3 s", "mode %d, %d carrying", (int)mThrown->mMode,
				     (int)mTarget->mCarrierCounter);
			}
			return;
		case PelletCarry:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_CollectFirstPellet)) {
				std::printf("[scenario] %s: Carry: DEMOFLAG_CollectFirstPellet at t=%u, goal %s\n", mName, t, mPellet->mTargetGoal == mOnion ? "the red Onion" : "other");
				if (mPellet->mTargetGoal != mOnion) {
					fail(t, "Carry: the pellet goes into the red Onion", "another goal");
					return;
				}
				mCarrier = mThrown;
				startPluck(t, AfterSecondPluck);
			} else if (span > 40 * rate) {
				fail(t, "Carry: the pellet reaches the red Onion within 40 s", "pellet state %d, carriers %d, at (%.0f, %.0f)", mPellet->getState(),
				     (int)mPellet->mCarrierCount, mPellet->mSRT.t.x, mPellet->mSRT.t.z);
			}
			return;
		case Regroup:
			if (partyCount() == 2 && liveCount(Red) == 2) {
				std::printf("[scenario] %s: Whistle: both red Pikmin in the party at t=%u\n", mName, t);
				enter(ToEngine, t);
			} else if (span > 20 * rate) {
				fail(t, "Whistle: the whistle puts the waiting carrier in the party within 20 s", "%d in the party, %d red alive", partyCount(), liveCount(Red));
			}
			return;
		case ToEngine:
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_ApproachEngine)) {
				const float edge = flatDistance(mEngine->mSRT.t, navi->mSRT.t) - mEngine->getBottomRadius();
				std::printf("[scenario] %s: Find: DEMOFLAG_ApproachEngine at t=%u, the captain %.0f from the engine's edge (radius %.0f)\n", mName, t, edge,
				            demoParms->mParms.mDemoTriggerRadius());
				if (edge > demoParms->mParms.mDemoTriggerRadius() + 1.0f) {
					fail(t, "Find: the captain sets the engine's discovery within the trigger radius", "%.1f from its edge", edge);
					return;
				}
				startThrow(t, mEngine, 2);
			} else if (span > 40 * rate) {
				fail(t, "Find: the captain reaches the Main Engine within 40 s", "%.0f from it", flatDistance(mEngine->mSRT.t, navi->mSRT.t));
			}
			return;
		case EngineFew:
			if (flatDistance(mEngine->mSRT.t, mEngineStart) > 0.5f || playerState->mDemoFlags.isFlag(DEMOFLAG_CollectEngine)) {
				fail(t, "Deliver: two Pikmin do not move the engine (12 needed)", "moved %.1f", flatDistance(mEngine->mSRT.t, mEngineStart));
				return;
			}
			if (span >= 2 * rate) {
				const int more = mEngine->mConfig->mCarryMinPikis() - (int)mEngine->mCarrierCounter;
				std::printf("[scenario] %s: Deliver: 2 s with %d carrying, the engine still; %d more born beside it\n", mName, (int)mEngine->mCarrierCounter, more);
				if (mEngine->mCarrierCounter != 2 || more <= 0) {
					fail(t, "Deliver: the two thrown Pikmin take carrier places on the engine, which needs more", "%d carrying, %d more", (int)mEngine->mCarrierCounter, more);
					return;
				}
				spawnFree(more, mEngine->mSRT.t.x, mEngine->mSRT.t.z + 60.0f);
				enter(EngineCarry, t);
			}
			return;
		case EngineCarry:
			if (!mEngineMoving && flatDistance(mEngine->mSRT.t, mEngineStart) > 20.0f) {
				mEngineMoving = true;
				evidence("03_engine_carry.png");
			}
			if (playerState->mDemoFlags.isFlag(DEMOFLAG_CollectEngine)) {
				std::printf("[scenario] %s: Deliver: DEMOFLAG_CollectEngine at t=%u, %d carrying\n", mName, t, (int)mEngine->mCarrierCount);
				if (mEngine->mTargetGoal != (Suckable*)mUfo) {
					fail(t, "Deliver: the engine goes to the ship", "another goal");
					return;
				}
				enter(Suck, t);
			} else if (span > 60 * rate) {
				fail(t, "Deliver: the engine reaches the ship within 60 s", "pellet state %d, carriers %d, at (%.0f, %.0f)", mEngine->getState(),
				     (int)mEngine->mCarrierCount, mEngine->mSRT.t.x, mEngine->mSRT.t.z);
			}
			return;
		case Suck:
			if (gameflow.mIsDayEndTriggered || gameflow.mIsDayEndActive) {
				std::printf("[scenario] %s: Day end: forced at t=%u by movie %d\n", mName, t, movie);
				if (movie != DEMOID_ShipUpgradePractice) {
					fail(t, "Day end: the ship-upgrade movie ends the tutorial day", "movie %d", movie);
					return;
				}
				mFieldAtEnd = liveCount(Red);
				mInBefore   = GameStat::containerPikis[Red];
				mDeadBefore = GameStat::deadPikis[Red];
				enter(DayOver, t);
			} else if (span > 30 * rate) {
				fail(t, "Day end: the ship-upgrade movie ends the day within 30 s of the delivery", "movie %d", movie);
			}
			return;
		case DayOver:
			if (day == 2) {
				const int in   = GameStat::containerPikis[Red] - mInBefore;
				const int lost = GameStat::deadPikis[Red] - mDeadBefore;
				std::printf("[scenario] %s: Day end: Day 2 at t=%u; %d red on the field at the day end, %d into the Onion, %d lost, %d alive\n", mName, t,
				            mFieldAtEnd, in, lost, liveCount(Red));
				if (!checkEngine(t)) return;
				// GameCoreSection::exitDayEnd: every live Pikmin whose colour has an Onion enters it.
				if (in != mFieldAtEnd || lost != 0 || liveCount(Red) != 0 || mFieldAtEnd != mEngine->mConfig->mCarryMinPikis()) {
					fail(t, "Day end: all the engine's carriers on the field go into the red Onion, none lost", "%d on the field, %d in, %d lost, %d alive",
					     mFieldAtEnd, in, lost, liveCount(Red));
					return;
				}
				mInOnion = GameStat::containerPikis[Red];
				mDriver.setPlan({ mSave ? scen_save::Yes : scen_save::No });
				enter(Results, t);
			} else if (span > 40 * rate) {
				fail(t, "Day end: the day advances within 40 s", "day %d", day);
			}
			return;
		case Results:
		{
			mDriver.update(t);
			if (!mSaved && gameflow.mGamePrefs.mHasSaveGame) {
				mSaved = true;
				if (gameflow.mMemoryCard.mDidSaveFail) {
					fail(t, "Save: the save succeeds", "mDidSaveFail");
					return;
				}
				mAtSave = scen_save::capture();
				scen_save::printSnapshot(mName, "at the save", mAtSave);
			}
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: Save: soft reset at t=%u toward section %d, save manager %s\n", mName, t, next,
				            scen_save::noteHistory("savemgr").c_str());
				if (!mSaved) {
					fail(t, "Save: a save before the results screen closes", "none; save manager %s", scen_save::noteHistory("savemgr").c_str());
					return;
				}
				for (int status : { 5, 6, 14 }) { // SavingInProgress, SaveComplete, SaveCompletedSuccess
					if (!scen_save::noteSeen("savemgr", status)) {
						fail(t, "Save: the save manager runs the save (5, 6, 14)", "%s", scen_save::noteHistory("savemgr").c_str());
						return;
					}
				}
				if (next != ONEPLAYER_MapSelect || day != 2) {
					fail(t, "Save: the results screen leaves for MapSelect (6) on Day 2", "section %d, day %d", next, day);
					return;
				}
				enter(Between, t); // the runner stops ticking until the next stage is ready
			} else if (span > 90 * rate) {
				fail(t, "Save: the results screen closes within 90 s", "save manager %s", scen_save::noteHistory("savemgr").c_str());
			}
			return;
		}
		case Day2Walk:
			if (span >= 2 * rate) {
				const float moved = flatDistance(navi->mSRT.t, mDay2Start);
				const float hours = gameflow.mWorldClock.mTimeOfDay - mDay2Hour;
				std::printf("[scenario] %s: Next day: after 2 s the captain moved %.1f, the clock ran %.4f h, day %d\n", mName, moved, hours, day);
				if (moved < 30.0f || hours <= 0.0f || day != 2) {
					fail(t, "Next day: on Day 2 the captain walks and the clock runs", "moved %.1f, clock %+.4f h, day %d", moved, hours, day);
					return;
				}
				enter(Done, t);
				scenarioPass();
			}
			return;
		default: return;
		}
	}

private:
	enum Phase {
		ToOnion,
		OnionMovie,
		Pluck,
		ThrowWalk,
		ThrowPress,
		ThrowFly,
		ThrowTake,
		PelletCarry,
		Regroup,
		ToEngine,
		EngineFew,
		EngineCarry,
		Suck,
		DayOver,
		Results,
		Between,
		Day2Walk,
		Done
	};
	enum AfterPluck { AfterFirstPluck, AfterSecondPluck };

	void day1Ready()
	{
		applyClassicKeys();
		pc_settings_scenario_set_infinite_day(0);
		mOnion = itemMgr->getContainer(Red);
		mUfo   = itemMgr->getUfo();
		Iterator it(pelletMgr);
		CI_LOOP(it)
		{
			Pellet* p = static_cast<Pellet*>(*it);
			if (p->mConfig->mPelletType() == PELTYPE_UfoPart) mEngine = p;
			else mPellet = p;
		}
		std::printf("[scenario] %s: day %d stage '%s', tutorial %d, Onion %d, ship %d, engine %d, pellet %d\n", mName, gameflow.mWorldClock.mCurrentDay,
		            flowCont.mCurrentStage->mFileName, (int)playerState->isTutorial(), (int)(mOnion != nullptr), (int)(mUfo != nullptr), (int)(mEngine != nullptr),
		            (int)(mPellet != nullptr));
		if (gameflow.mWorldClock.mCurrentDay != 1 || !playerState->isTutorial() || !mOnion || !mUfo || !mEngine || !mPellet || liveCount(Red) != 0
		    || playerState->mCurrParts != 0) {
			fail(0, "a new game on Day 1 of the tutorial stage: the red Onion, the ship, the Main Engine and a pellet, no Pikmin, no parts", "see above");
			return;
		}
		mEngineStart = mEngine->mSRT.t;
		enter(ToOnion, 0);
	}

	void day2Ready()
	{
		const unsigned t = mLastTick;
		if (mPhase != Between || mFailed) {
			if (!mFailed) fail(t, "Next day: Day 2 is loaded through map select", "phase %d", (int)mPhase);
			return;
		}
		pc_settings_scenario_set_infinite_day(0);
		GoalItem* onion = itemMgr->getContainer(Red);
		const int day   = gameflow.mWorldClock.mCurrentDay;
		const int held  = onion ? onion->getTotalStorePikis() : -1;
		const int state = onion ? C_SAI(onion)->getCurrID(onion) : -1;
		std::printf("[scenario] %s: Next day: day %d stage '%s', tutorial %d, parts %d engine %d, container 0x%02x, red Onion state %d holding %d "
		            "(store %d, GameStat %d), %d red on the field\n",
		            mName, day, flowCont.mCurrentStage->mFileName, (int)playerState->isTutorial(), playerState->mCurrParts,
		            (int)playerState->hasUfoParts(UFOID_MainEngine), playerState->mContainerFlag, state, held, pikiInfMgr.getColorTotal(Red),
		            (int)GameStat::containerPikis[Red], liveCount(Red));
		if (day != 2 || std::strcmp(flowCont.mCurrentStage->mFileName, kNextCourse) != 0 || playerState->isTutorial()) {
			fail(t, "Next day: Day 2 on the Forest course, not the tutorial", "day %d, stage '%s', tutorial %d", day, flowCont.mCurrentStage->mFileName,
			     (int)playerState->isTutorial());
			return;
		}
		if (playerState->mCurrParts != 1 || !playerState->hasUfoParts(UFOID_MainEngine) || !playerState->hasContainer(Red) || !playerState->hasBootContainer(Red)) {
			fail(t, "Next day: the engine is on the ship (1 part) and the red Onion is owned and booted", "parts %d engine %d container 0x%02x",
			     playerState->mCurrParts, (int)playerState->hasUfoParts(UFOID_MainEngine), playerState->mContainerFlag);
			return;
		}
		if (!onion || state != GoalAI::GOAL_Wait || held != mInOnion || pikiInfMgr.getColorTotal(Red) != mInOnion
		    || (int)GameStat::containerPikis[Red] != mInOnion || liveCount(Red) != 0) {
			fail(t, "Next day: the red Onion is booted and holds the Pikmin that went in at the day end, none on the field", "Onion state %d holding %d (want %d), "
			     "store %d, GameStat %d, %d on the field",
			     state, held, mInOnion, pikiInfMgr.getColorTotal(Red), (int)GameStat::containerPikis[Red], liveCount(Red));
			return;
		}
		Navi* navi  = naviMgr->getNavi();
		mDay2Start  = navi->mSRT.t;
		mDay2Target = mDay2Start + Vector3f(0.0f, 0.0f, 200.0f);
		mDay2Hour   = gameflow.mWorldClock.mTimeOfDay;
		evidence("04_day2.png");
		enter(Day2Walk, t);
	}

	/// The captain plucks the next red sprout (one not seen before), then the run goes on.
	void startPluck(unsigned t, AfterPluck after)
	{
		Iterator it(pikiMgr);
		CI_LOOP(it) mKnown.insert(static_cast<Piki*>(*it));
		mAfter = after;
		mSeed  = newSprout();
		enter(Pluck, t);
	}

	void tickPluck(unsigned t, unsigned span, unsigned rate, int movie)
	{
		if (!mSeed) mSeed = newSprout();
		Piki* p = newPiki();
		if (p) {
			// Settled: the new Pikmin is in the party, the captain walks, no movie or window.
			if (naviMgr->getNavi()->getCurrState()->getID() == NAVISTATE_Walk && movie == -1 && !gameflow.mIsUIOverlayActive && p->getState() == PIKISTATE_Normal
			    && p->mMode == PikiMode::FormationMode) {
				mKnown.insert(p);
				mSeed = nullptr;
				std::printf("[scenario] %s: Pluck: a red Pikmin in the party at t=%u (%d red alive, %d plucked in all)\n", mName, t, liveCount(Red),
				            playerState->mTotalPluckedPikiCount);
				if (mAfter == AfterFirstPluck) {
					if (liveCount(Red) != 1 || partyCount() != 1 || !playerState->mDemoFlags.isFlag(DEMOFLAG_PluckRedPikmin)) {
						fail(t, "Pluck: one red Pikmin, in the party, DEMOFLAG_PluckRedPikmin", "%d alive, %d in the party", liveCount(Red), partyCount());
						return;
					}
					evidence("01_first_pluck.png");
					startThrow(t, mPellet, 1);
				} else {
					if (liveCount(Red) != 2 || partyCount() != 1 || !mCarrier || mCarrier->mMode == PikiMode::FormationMode) {
						fail(t, "Carry: the Onion's seed plucked: two red Pikmin, the new one in the party, the carrier out of it", "%d alive, %d in the party",
						     liveCount(Red), partyCount());
						return;
					}
					mCallAt = mCarrier->mSRT.t;
					std::printf("[scenario] %s: Whistle: calling the carrier at (%.0f, %.0f)\n", mName, mCallAt.x, mCallAt.z);
					enter(Regroup, t);
				}
			}
		} else if (span > 90 * rate) {
			fail(t, "Pluck: the sprout appears, is planted and plucked within 90 s", "sprout %d, state %d, Onion state %d, red born %d", (int)(mSeed != nullptr),
			     mSeed ? mSeed->getCurrState()->getID() : -1, C_SAI(mOnion)->getCurrID(mOnion), GameStat::bornPikis[Red]);
		}
	}

	/// Walks to kThrowStand short of `target` (on the line from the captain) and throws `count` Pikmin at it, one at a time.
	void startThrow(unsigned t, Pellet* target, int count)
	{
		Navi* navi     = naviMgr->getNavi();
		mTarget        = target;
		mThrowsLeft    = count;
		mCarriersBefore = target->mCarrierCounter;
		const Vector3f at = target->mSRT.t;
		const float len   = flatDistance(at, navi->mSRT.t);
		const float ux = (navi->mSRT.t.x - at.x) / len, uz = (navi->mSRT.t.z - at.z) / len;
		const float edge = target->getBottomRadius();
		mStand = Vector3f(at.x + ux * (edge + kThrowStand), 0.0f, at.z + uz * (edge + kThrowStand));
		// The aim: just short of the object's edge, so the Pikmin lands beside it.
		mAim   = Vector3f(at.x + ux * (edge + 8.0f), 0.0f, at.z + uz * (edge + 8.0f));
		mAim.y = mapMgr->getMinY(mAim.x, mAim.z, true);
		mStill = 0;
		enter(ThrowWalk, t);
	}

	PikiHeadItem* newSprout()
	{
		Iterator it(itemMgr->getPikiHeadMgr());
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType == OBJTYPE_Pikihead && static_cast<PikiHeadItem*>(c)->mSeedColor == Red && c->isAlive()) return static_cast<PikiHeadItem*>(c);
		}
		return nullptr;
	}

	Piki* newPiki()
	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (!mKnown.count(p)) return p;
		}
		return nullptr;
	}

	Piki* flyingPiki()
	{
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (p->getState() == PIKISTATE_Flying) return p;
		}
		return nullptr;
	}

	int partyCount()
	{
		int n = 0;
		Iterator it(pikiMgr);
		CI_LOOP(it) n += static_cast<Piki*>(*it)->mMode == PikiMode::FormationMode ? 1 : 0;
		return n;
	}

	/// Each live Pikmin's mode, state and distance from the captain, for failure messages.
	std::string describePikmin(Navi* navi)
	{
		std::string out;
		char buf[64];
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			std::snprintf(buf, sizeof buf, "%smode %d state %d at %.0f", out.empty() ? "" : ", ", (int)p->mMode, p->getState(), flatDistance(p->mSRT.t, navi->mSRT.t));
			out += buf;
		}
		return out.empty() ? "no Pikmin" : out;
	}

	int partyNear(Navi* navi, float range)
	{
		int n = 0;
		Iterator it(pikiMgr);
		CI_LOOP(it)
		{
			Piki* p = static_cast<Piki*>(*it);
			if (p->mMode == PikiMode::FormationMode && flatDistance(p->mSRT.t, navi->mSRT.t) <= range) n++;
		}
		return n;
	}

	/// `n` free red Pikmin in a short row at (x, z), each handed to Piki::actOnSituaton on the spot (tutorial_day1's spawnAt and land).
	void spawnFree(int n, float x, float z)
	{
		Navi* navi = naviMgr->getNavi();
		std::vector<Piki*> born;
		for (int i = 0; i < n; i++) {
			const float dx = x - navi->mSRT.t.x + 6.0f * float(i - n / 2);
			const float dz = z - navi->mSRT.t.z + 4.0f * float(i % 2);
			if (Piki* p = spawnPiki(navi, Red, PikiMode::FreeMode, dx, dz)) born.push_back(p);
		}
		GameStat::update();
		for (Piki* p : born) p->actOnSituaton();
	}

	bool checkEngine(unsigned t)
	{
		int vis = -1;
		for (int i = 0; i < playerState->mTotalRegisteredParts; i++) {
			if (playerState->mUfoParts[i].mModelID == UFOID_MainEngine) vis = playerState->mUfoParts[i].mPartVisType;
		}
		const int course = (int)gameflow.mPlayState.mCourseOpenFlags;
		std::printf("[scenario] %s: Deliver: engine vis %d, parts %d, level %d, course 0x%x\n", mName, vis, playerState->mCurrParts,
		            (int)playerState->mShipUpgradeLevel, course);
		if (!playerState->hasUfoParts(UFOID_MainEngine) || vis != PARTVIS_Visible || playerState->mCurrParts != 1 || playerState->mShipUpgradeLevel != 1
		    || course != ((1 << STAGE_Practice) | (1 << STAGE_Forest))) {
			fail(t, "Deliver: the ship took the engine as a visible part: 1 part, level 1, Forest open", "vis %d, parts %d, level %d, course 0x%x", vis,
			     playerState->mCurrParts, (int)playerState->mShipUpgradeLevel, course);
			return false;
		}
		return true;
	}

	/// The card file read back from disk and slot A loaded as the file select does; the loaded game equals the saved one.
	void reload(unsigned t)
	{
		CardQuickInfo infos[4];
		if (!scen_save::reloadCard(t, mName, infos)) {
			mFailed = true;
			return;
		}
		const CardQuickInfo& a = infos[0];
		std::printf("[scenario] %s: Reload: slot A from disk: block %d, day %d, parts %d, red %d yellow %d blue %d\n", mName, a.mMemCardSaveIndex, a.mCurrentDay,
		            a.mCurrentPartsCount, a.mRedPikiCount, a.mYellowPikiCount, a.mBluePikiCount);
		if (a.mMemCardSaveIndex < 0 || a.mSaveStatus != PlayState::ReadyToSave || a.mCurrentDay != mAtSave.day || a.mCurrentPartsCount != mAtSave.shipParts
		    || a.mRedPikiCount != mAtSave.red || a.mCurrentDay != 2 || a.mCurrentPartsCount != 1 || a.mRedPikiCount != mInOnion) {
			fail(t, "Reload: slot A read from disk shows Day 2, one part and the red Pikmin in the Onion", "block %d status %u day %d parts %d red %d (Onion %d)",
			     a.mMemCardSaveIndex, a.mSaveStatus, a.mCurrentDay, a.mCurrentPartsCount, a.mRedPikiCount, mInOnion);
			return;
		}
		scen_save::loadSlot(a);
		const scen_save::Snapshot loaded = scen_save::capture();
		scen_save::printSnapshot(mName, "loaded", loaded);
		if (!scen_save::compareSnapshots(t, mName, mAtSave, loaded)) {
			mFailed = true;
			return;
		}
		std::printf("[scenario] %s: Reload: the loaded game equals the saved one\n", mName);
	}

	void evidence(const char* file)
	{
		if (pc_scenario_screenshot_evidence(file)) std::printf("[scenario] %s: evidence capture %s on the next tick\n", mName, file);
	}

	void enter(Phase p, unsigned t)
	{
		mPhase     = p;
		mPhaseTick = t;
	}

	template <typename... A>
	void fail(unsigned t, const char* expected, const char* fmt, A... args)
	{
		mPhase  = Done;
		mFailed = true;
		scenarioFailf(t, expected, fmt, args...);
	}

	const char* mName;
	bool mSave;
	Phase mPhase        = ToOnion;
	AfterPluck mAfter   = AfterFirstPluck;
	unsigned mPhaseTick = 0, mLastTick = 0, mStill = 0;
	bool mFailed = false, mGrabbed = false, mEngineMoving = false, mSaved = false;
	int mThrowsLeft = 0, mCarriersBefore = 0;
	int mFieldAtEnd = 0, mInBefore = 0, mDeadBefore = 0, mInOnion = 0;
	float mDay2Hour = 0.0f;
	GoalItem* mOnion    = nullptr;
	UfoItem* mUfo       = nullptr;
	Pellet* mEngine     = nullptr;
	Pellet* mPellet     = nullptr;
	Pellet* mTarget     = nullptr;
	Piki* mThrown       = nullptr;
	Piki* mCarrier      = nullptr;
	PikiHeadItem* mSeed = nullptr;
	Vector3f mStand, mAim, mCallAt, mEngineStart, mDay2Start, mDay2Target;
	std::set<Piki*> mKnown;
	scen_save::ResultsDriver mDriver;
	scen_save::Snapshot mAtSave;
};

} // namespace

PcScenario* pc_scenario_make_playthrough_day1(void)
{
	static Playthrough s("playthrough_day1", true);
	return &s;
}

PcScenario* pc_scenario_make_playthrough_day1_nosave(void)
{
	static Playthrough s("playthrough_day1_nosave", false);
	return &s;
}

#endif
