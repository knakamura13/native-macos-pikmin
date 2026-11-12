// gate_break: a gate worn to zero health goes through its stages one at a time, opens its
// waypoint and refuses further hits, on
// stages/sandbox_all_objects.ini (the soft gate, OBJTYPE_SluiceSoft).
//
// What the code does (GameCube original; characterised): InteractAttack::actItem
// (itemMgr.cpp:990) on a sluice subtracts the damage from mHealth; each time the health falls to
// or under the next stage's threshold (mMaxHealth - (stage + 1) * mMaxHealth / mNumStages) it
// sends SluiceAI MsgUser(0) and advances mCurrStage, and at the last stage sets the health to 0.
// A hit above the threshold sends MsgUser(1) (the damage shake) only. Once mCurrStage reaches
// mNumStages the gate is completed and every hit is refused (returns false). SluiceAI opens the
// gate's waypoint (WayPoint::setFlag(true)) when the last break motion starts or, completed, in
// WaitInit. The reinforced and bomb gates (breakable_gates setting) are not on a stand-in stage.
//
// A destroyed gate keeps its model on screen: gt_soft.mod and gt_hard.mod sink the centre of the
// wall and leave the side pieces up, in the last pose of the sink (BuildingItem::refresh,
// pc_wall_model_should_draw). The runner reads the draw from a scenario note that refresh
// sends when it submits a wall's model ("wall_drawn").
//
// In order, each within a bound in seconds:
//   Find     The soft gate: stages >= 1, stage 0, waypoint closed, health 300 (the stand-in
//            generator writes days 12 and hours 12: 12 + 12 * 24; a chosen value, not the disc's).
//            The gate is drawn within 1 s (it is in view from the captain's start).
//   Scratch  A 1-damage hit is taken but the stage stays 0 and the waypoint closed.
//   Break    One hit per second of one stage's worth of health: each moves the stage by exactly 1;
//            the last leaves health 0 and the gate completed.
//   Open     The waypoint opens within 10 s of the last stage.
//   Refuse   A further hit is refused and the health stays 0.
//   Sunk     The sink motion stops within 10 s (motion speed 0). The finished pose (animation
//            counter and key index) is recorded, and the model is still drawn on every tick of
//            the next second.
//
// gate_break_next_day starts on Day 2 and goes on: the clock jumps to just before the end hour
// (as day_end), A closes the texts and the results, and the day end soft-resets to MapSelect on
// Day 3, which enters the same stage again (onMapSelect). The gate is then rebuilt from the
// generator cache (Generator::loadCreature, BuildingItem::doLoad). Asserted on Day 3: the gate is
// completed with health 0 and its waypoint open, its animator holds the pose recorded after the
// live sink (the same counter, the same key index), its motion is stopped, and the model is drawn
// within 1 s.
//
// Negative controls: Scratch is Break's contrast (a hit below one stage's worth moves nothing).
// Mutation checks: with the Break hits at a third of a stage's worth it
// fails "each stage's worth of damage advances the gate one stage" (stage 0 -> 0, health
// 265.7); with the gate's stage set
// back by one before the Refuse hit it fails "a completed gate refuses hits" (taken 1), both at 30 Hz.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "BuildingItem.h"
#include "Dolphin/pad.h"
#include "Interactions.h"
#include "ItemMgr.h"
#include "MoviePlayer.h"
#include "NaviMgr.h"
#include "ObjType.h"
#include "Route.h"
#include "Section.h"
#include "gameflow.h"
#include "scenarios/pc_scenario_note.h"
#include "scenarios/scenario_progress.h"
#include "system.h"

namespace {

constexpr float kGateHealth = 300.0f;
constexpr const char* kStage = "stages/sandbox_all_objects.ini";
// stages.ini id 2 on the stand-in tree: a story course (PlayerState::isGameCourse), so the day end
// stores its generators, with one soft gate whose generator keeps it (standin_objects_stage.h).
constexpr const char* kCourseStage = "stages/course2.ini";
// The day end: the clock jumps this many ticks before the end hour (world_map, day_end).
constexpr unsigned kSunsetTicks = 60;
// Seconds from the clock jump to the soft reset: about 13 s measured on the stand-in tree (world_map).
constexpr unsigned kToResetSeconds = 75;

enum Phase { Find, Break, Open, Sunk, Drawn, Settle, ToReset, Between, Day3, Done };

class GateBreak : public PcScenario {
public:
	GateBreak(const char* name, bool nextDay)
	    : mName(name)
	    , mNextDay(nextDay)
	{
	}

	const char* name() const override { return mName; }
	const char* stageFile() const override { return mNextDay ? kCourseStage : kStage; }
	int startDay() const override { return mNextDay ? 2 : 1; }
	bool crossesDays() const override { return mNextDay; }
	const char* onMapSelect() override { return mPhase == Between ? kCourseStage : nullptr; }

	void providePads(unsigned t, PcInputPad& pad) override
	{
		if (mPhase != ToReset) return;
		// The texts before sunset; after the day advance the diary page, the results and the
		// save prompt, each waiting for A (world_map, day_end).
		const bool advanced = gameflow.mWorldClock.mCurrentDay == 3;
		if (advanced ? t % 6 == 0 : (gameflow.mIsUIOverlayActive && t % 15 == 0)) pad.button |= PAD_BUTTON_A;
	}

	void onStageReady() override
	{
		mGate = findGate();
		pc_scenario_note_clear("wall_drawn");
		if (pc_scenario_stage_number() > 1) {
			mPhase     = Day3;
			mPhaseTick = mLastTick;
			return;
		}
		if (!mGate || !mGate->mWayPoint || mGate->mNumStages < 1 || mGate->mCurrStage != 0 || mGate->mWayPoint->mIsOpen) {
			fail(0, "a soft gate at stage 0, full health, waypoint closed", "gate %p", (void*)mGate);
			return;
		}
		// GenObjectItem gives a gate the generator's parameters C + B * hours as its health; the
		// stand-in default.gen writes 12 and 12 (the disc's gate values were not read).
		std::printf("[scenario] %s: generator health %.1f of %.1f\n", mName, mGate->mHealth, mGate->mMaxHealth);
		if (mGate->mHealth != kGateHealth || mGate->mMaxHealth != kGateHealth) {
			fail(0, "the stand-in generator gives the soft gate health 300", "health %.1f of %.1f", mGate->mHealth, mGate->mMaxHealth);
			return;
		}
		mStep = mGate->mMaxHealth / float(mGate->mNumStages);
		std::printf("[scenario] %s: soft gate, %d stages, health %.1f (%.1f a stage), waypoint %d closed\n", mName, mGate->mNumStages,
		            mGate->mMaxHealth, mStep, mGate->mWayPoint->mIndex);
		const bool taken = hit(1.0f);
		std::printf("[scenario] %s: scratch: taken %d, stage %d, health %.1f, waypoint open %d\n", mName, (int)taken, mGate->mCurrStage,
		            mGate->mHealth, (int)mGate->mWayPoint->mIsOpen);
		if (!taken || mGate->mCurrStage != 0 || mGate->mWayPoint->mIsOpen || std::fabs(mGate->mHealth - (kGateHealth - 1.0f)) > 0.01f) {
			fail(0, "a 1-damage hit is taken, takes 1 health and moves no stage", "taken %d, stage %d, health %.1f", (int)taken, mGate->mCurrStage,
			     mGate->mHealth);
		}
	}

	void onTick(unsigned t) override
	{
		mLastTick = t;
		if (!mGate || mPhase == Done) return;
		const int rate = pc_scenario_ticks_per_second();
		const float s  = float(t - mPhaseTick) / float(rate);
		int drawnType  = 0;
		const bool drawn = pc_scenario_note_take("wall_drawn", &drawnType) && drawnType == OBJTYPE_SluiceSoft;
		switch (mPhase) {
		case Find:
			// The scratch ran in onStageReady; the gate must be seen drawn before it is broken.
			if (drawn) {
				mPhase     = Break;
				mPhaseTick = t;
			} else if (s > 1.0f) {
				fail(t, "the intact gate is drawn within 1 s", "not drawn");
			}
			break;
		case Break:
			if (t % rate == 0 && t > 0) {
				const int before = mGate->mCurrStage;
				// The scratch took 1; a full stage's worth from here crosses exactly one threshold.
				const bool taken = hit(mStep);
				std::printf("[scenario] %s: tick %u hit: taken %d, stage %d -> %d, health %.1f\n", mName, t, (int)taken, before, mGate->mCurrStage,
				            mGate->mHealth);
				if (!taken || mGate->mCurrStage != before + 1) {
					fail(t, "each stage's worth of damage advances the gate one stage", "taken %d, stage %d -> %d", (int)taken, before,
					     mGate->mCurrStage);
				} else if (mGate->mCurrStage >= mGate->mNumStages) {
					if (mGate->mHealth != 0.0f || !mGate->isCompleted()) {
						fail(t, "the last stage leaves the gate at health 0, completed", "health %.1f, completed %d", mGate->mHealth,
						     (int)mGate->isCompleted());
						return;
					}
					mPhase     = Open;
					mPhaseTick = t;
				}
			} else if (float(t) / float(rate) > float(mGate->mNumStages) + 5.0f) {
				fail(t, "the gate breaks within its stage count plus 5 s", "stage %d", mGate->mCurrStage);
			}
			break;
		case Open:
			if (mGate->mWayPoint->mIsOpen) {
				const bool taken = hit(mStep);
				std::printf("[scenario] %s: waypoint open %.2f s after the last stage; a further hit taken %d, health %.1f\n", mName, s, (int)taken,
				            mGate->mHealth);
				if (taken || mGate->mHealth != 0.0f) {
					fail(t, "a completed gate refuses hits", "taken %d, health %.1f", (int)taken, mGate->mHealth);
					return;
				}
				mPhase     = Sunk;
				mPhaseTick = t;
			} else if (s > 10.0f) {
				fail(t, "the broken gate's waypoint opens within 10 s", "closed");
			}
			break;
		case Sunk:
			if (mGate->mMotionSpeed == 0.0f) {
				mSunkCounter = mGate->mItemAnimator.mAnimationCounter;
				mSunkKey     = mGate->mItemAnimator.mCurrentKeyIndex;
				std::printf("[scenario] %s: sink stopped %.2f s after the waypoint opened: counter %.2f, key index %d\n", mName, s, mSunkCounter,
				            mSunkKey);
				mPhase      = Drawn;
				mPhaseTick  = t;
				mDrawnTicks = 0;
			} else if (s > 10.0f) {
				fail(t, "the sink motion stops within 10 s of the waypoint opening", "motion speed %.2f", mGate->mMotionSpeed);
			}
			break;
		case Drawn:
			// The note was taken above, so each tick counts once.
			if (drawn) mDrawnTicks++;
			if (t - mPhaseTick >= unsigned(rate)) {
				std::printf("[scenario] %s: destroyed gate drawn on %d of %d ticks\n", mName, mDrawnTicks, rate);
				if (mDrawnTicks != rate) {
					fail(t, "the destroyed gate's model is still drawn on every tick (its side pieces stay)", "drawn on %d of %d ticks", mDrawnTicks,
					     rate);
					return;
				}
				if (!mNextDay) {
					mPhase = Done;
					scenarioPass();
					return;
				}
				mPhase = Settle;
			}
			break;
		case Settle:
		{
			const bool movie = gameflow.mMoviePlayer && gameflow.mMoviePlayer->mIsActive;
			if (movie || gameflow.mIsUIOverlayActive) return;
			scenario_progress::jumpToSunset(kSunsetTicks);
			mPhase     = ToReset;
			mPhaseTick = t;
			break;
		}
		case ToReset:
			if (gsys->mSoftResetPending) {
				const int next = gameflow.mNextOnePlayerSectionID;
				std::printf("[scenario] %s: soft reset at tick %u toward section %d on day %d\n", mName, t, next, gameflow.mWorldClock.mCurrentDay);
				if (next != ONEPLAYER_MapSelect || gameflow.mWorldClock.mCurrentDay != 3) {
					fail(t, "the day end soft-resets to MapSelect (6) on Day 3", "section %d, day %d", next, gameflow.mWorldClock.mCurrentDay);
					return;
				}
				mPhase = Between; // the runner stops ticking until the next stage is ready
				mGate  = nullptr;
			} else if (s > float(kToResetSeconds)) {
				fail(t, "the day end and its soft reset within 75 s of the clock jump", "day %d", gameflow.mWorldClock.mCurrentDay);
			}
			break;
		case Day3: checkDay3(t, s, drawn); break;
		default: break;
		}
	}

private:
	static BuildingItem* findGate()
	{
		BuildingItem* gate = nullptr;
		Iterator it(itemMgr->getMeltingPotMgr());
		CI_LOOP(it)
		{
			Creature* c = *it;
			if (c->mObjType == OBJTYPE_SluiceSoft) gate = static_cast<BuildingItem*>(c);
		}
		return gate;
	}

	void checkDay3(unsigned t, float s, bool drawn)
	{
		if (!mCheckedPose) {
			mCheckedPose = true;
			const int day = gameflow.mWorldClock.mCurrentDay;
			if (!mGate || !mGate->mWayPoint) {
				fail(t, "the soft gate on Day 3", "day %d, gate %p", day, (void*)mGate);
				return;
			}
			const float counter = mGate->mItemAnimator.mAnimationCounter;
			const int key       = mGate->mItemAnimator.mCurrentKeyIndex;
			std::printf("[scenario] %s: day %d gate: stage %d of %d, health %.1f, waypoint open %d, motion speed %.2f, counter %.2f, key index %d\n",
			            mName, day, mGate->mCurrStage, mGate->mNumStages, mGate->mHealth, (int)mGate->mWayPoint->mIsOpen, mGate->mMotionSpeed, counter,
			            key);
			if (day != 3 || !mGate->isCompleted() || mGate->mHealth != 0.0f || !mGate->mWayPoint->mIsOpen) {
				fail(t, "on Day 3 the gate is completed, health 0, waypoint open", "day %d, stage %d of %d, health %.1f, waypoint open %d", day,
				     mGate->mCurrStage, mGate->mNumStages, mGate->mHealth, (int)mGate->mWayPoint->mIsOpen);
				return;
			}
			if (mGate->mMotionSpeed != 0.0f || std::fabs(counter - mSunkCounter) > 0.01f || key != mSunkKey) {
				fail(t, "on Day 3 the gate holds the pose the live sink stopped on, motion stopped",
				     "counter %.2f key %d speed %.2f; after the sink counter %.2f key %d", counter, key, mGate->mMotionSpeed, mSunkCounter, mSunkKey);
				return;
			}
		}
		if (drawn) {
			std::printf("[scenario] %s: destroyed gate drawn on Day 3, %.2f s after the stage start\n", mName, s);
			mPhase = Done;
			scenarioPass();
		} else if (s > 1.0f) {
			fail(t, "the destroyed gate is drawn within 1 s of the Day 3 start", "not drawn");
		}
	}

	bool hit(float damage) { return mGate->stimulate(InteractAttack(naviMgr->getNavi(), nullptr, damage, false)); }

	void fail(unsigned t, const char* expected, const char* fmt, ...) __attribute__((format(printf, 4, 5)))
	{
		char buf[256];
		va_list ap;
		va_start(ap, fmt);
		std::vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);
		mPhase = Done;
		scenarioFailf(t, expected, "%s", buf);
	}

	const char* mName;
	bool mNextDay;
	BuildingItem* mGate = nullptr;
	Phase mPhase        = Find;
	unsigned mPhaseTick = 0;
	unsigned mLastTick  = 0;
	float mStep         = 0.0f;
	float mSunkCounter  = 0.0f;
	int mSunkKey        = 0;
	int mDrawnTicks     = 0;
	bool mCheckedPose   = false;
};

} // namespace

PcScenario* pc_scenario_make_gate_break(void)
{
	static GateBreak s("gate_break", false);
	return &s;
}

PcScenario* pc_scenario_make_gate_break_next_day(void)
{
	static GateBreak s("gate_break_next_day", true);
	return &s;
}

#endif
