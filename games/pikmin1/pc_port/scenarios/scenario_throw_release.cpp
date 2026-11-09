// throw_release: one captain, Pikmin in grab range. Four cases (a calibration
// that holds A until the grab completes and measures K, a release one tick
// after the press, a release 2 ticks after that Pikmin's own grab completes,
// and a release one tick after the press while the selected Pikmin is still
// walking over to the hand). Each asserts its preconditions, then only
// behaviour that holds whatever the RNG order (K is the grab-completion tick
// count and depends on it): the Pikmin is Hanged before the captain enters
// Throw (cases 0 and 2), the captain enters Throw, the Pikmin flies and lands
// (Normal), and the captain returns to Walk. All within tick bounds.
// Timing: the early release (case 1) enters Throw no sooner than the grab
// keyframe, measured in the calibration from the start of the ThrowWait
// motion; the walked-over Pikmin (case 3) is thrown on the tick it reaches
// the hand.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>

#include "CPlate.h"
#include "Dolphin/pad.h"
#include "GameStat.h"
#include "MapMgr.h"
#include "Navi.h"
#include "NaviMgr.h"
#include "NaviState.h"
#include "Piki.h"
#include "PikiMgr.h"
#include "PikiState.h"
#include "KeyConfig.h"

namespace {

constexpr int kPikminCount = 3;
constexpr int kBlue        = 0;
constexpr unsigned kAwaySeconds = 4;
// The walk away starts no earlier than this many ticks after the stage is
// ready (intro plus hold). The calibration's trajectory depends on the sim
// state and the world clock when the squad is born, and the intro's length
// depends on the data: the intro gate presses START every 15 ticks while a
// movie plays, and the disc's landing movie ends on the third press (45 ticks at
// 30 and at 60 Hz), a stand-in tree's on the first (15). Holding the stand-in
// run to the disc's 45 makes both trees start the calibration on the same
// tick. On the disc the hold is 0, so the disc run is unchanged
// (see the README entry for throw_release).
constexpr unsigned kWalkStartTicks = 45;
// NaviThrowWaitState::init picks the thrown Pikmin from the plate within 80.
constexpr float kSelectDistance = 80.0f;
// The thrown Pikmin must end up at least this far from where it hung when Throw began.
constexpr float kMinThrowMove = 5.0f;
constexpr int kCases = 4;
constexpr int kWalkOverCase = 3;
// Case 3 narrows the hand-over range (naviMgr p37) to this, a test control: the
// stand-in parameters keep the source default, 80, the same as the selection
// distance, so every selected Pikmin would already be in the hand.
constexpr float kWalkOverHandRange = 20.0f;
// Case 3 places the plate's Pikmin this far behind the captain (and 10 units
// further apart each): outside the narrowed hand-over range, inside the selection distance.
constexpr float kWalkOverDistance = 45.0f;

class ThrowRelease : public PcScenario {
public:
	const char* name() const override { return "throw_release"; }

	void onStageReady() override
	{
		if (KeyConfig::_instance == nullptr || KeyConfig::_instance->mThrowKey.mBind != KBBTN_A
		    || KeyConfig::_instance->mExtractKey.mBind != KBBTN_A) {
			scenarioFailf(0, "throw and extract bound to A (parms/key.bin)", "throw 0x%x extract 0x%x",
			              KeyConfig::_instance ? KeyConfig::_instance->mThrowKey.mBind : -1,
			              KeyConfig::_instance ? KeyConfig::_instance->mExtractKey.mBind : -1);
			return;
		}
		Navi* navi = naviMgr->getNavi();
		if (navi == nullptr) {
			scenarioFailf(0, "a captain", "none");
			return;
		}
		const unsigned intro = pc_scenario_intro_ticks();
		mHold                = intro < kWalkStartTicks ? kWalkStartTicks - intro : 0;
		std::printf("[scenario] throw_release: intro %u ticks, hold %u ticks before the walk away\n", intro, mHold);
		if (mHold == 0) {
			spawnSquad(navi);
			mPhase = Away;
		} else {
			mPhase = Hold; // the captain stands in Walk with neutral input
		}
	}

	// The squad is born when the walk away starts (tick 0 when there is no hold).
	void spawnSquad(Navi* navi)
	{
		// The intro gate hands tick 0 over with the captain in Walk.
		// About 45 units from the captain on a circle, so selection finds them
		// (grab range is 80) and they do not sit on each other.
		for (int i = 0; i < kPikminCount; ++i) {
			Piki* piki = static_cast<Piki*>(pikiMgr->birth());
			if (piki == nullptr) {
				scenarioFailf(0, "pikmin spawned", "birth returned null at %d", i);
				return;
			}
			GameStat::workPikis.inc(kBlue);
			piki->init(navi);
			const float a = 6.2831853f * (float)i / (float)kPikminCount;
			Vector3f at   = navi->mSRT.t;
			at.x += 45.0f * std::cos(a);
			at.z += 45.0f * std::sin(a);
			at.y = mapMgr->getMinY(at.x, at.z, true);
			piki->Creature::init(at);
			piki->initColor(kBlue);
			piki->changeMode(PikiMode::FormationMode, navi);
		}
		GameStat::update();
	}

	void providePads(unsigned, PcInputPad& pad) override
	{
		// The captain starts beside the ship, where A enters it. Walk away first.
		if (mPhase == Away) pad.stickY = 100;
		pad.button = mHoldA ? PAD_BUTTON_A : 0;
	}

	void onTick(unsigned t) override
	{
		Navi* navi = naviMgr->getNavi();
		const int cap = navi->getCurrState()->getID();
		const int rate = pc_scenario_ticks_per_second();
		const unsigned bound = 5u * rate; // five seconds per step

		// While the grab is held, note the Pikmin in the hand and whether it was Hanged.
		if (cap == NAVISTATE_ThrowWait) {
			NaviThrowWaitState* wait = static_cast<NaviThrowWaitState*>(navi->getCurrState());
			// The ThrowWait motion starts once a Pikmin is held (a pending pick waits for grab range).
			if (!mCheckedWait && wait->mHeldThrowPiki) {
				if (!scenarioCheckThrowWaitPreconditions(t, navi)) return;
				mCheckedWait = true;
			}
			if (wait->mIsHoldingThrowPiki && wait->mHeldThrowPiki && wait->mHeldThrowPiki->getState() == PIKISTATE_Hanged) mSawHanged = true;
			// The ThrowWait motion starts on the tick a Pikmin is in the hand range.
			if (wait->mHeldThrowPiki && !mMotionSeen) {
				mMotionSeen = true;
				mMotionTick = t;
			}
			if (wait->mPendingThrowPiki && !wait->mHeldThrowPiki) mLastPendingTick = t;
		}
		switch (mPhase) {
		case Hold:
			if (t + 1 >= mHold) {
				spawnSquad(navi);
				mPhase     = Away;
				mAwayStart = t + 1;
			}
			break;
		case Away:
			if (t + 1 >= mAwayStart + kAwaySeconds * (unsigned)rate) {
				mPhase     = Settle;
				mStepStart = t;
			}
			break;
		case Settle:
			// Captain in Walk and a Normal Pikmin in the plate within the distance
			// NaviThrowWaitState::init selects from (80). The walk away can leave
			// the squad behind on open ground; on the disc's Forest of Hope terrain
			// stops the captain and they catch up during the walk.
			if (cap == NAVISTATE_Walk && plateNormalCount(navi, kSelectDistance) > 0) {
				if (mCase == kWalkOverCase) placeBehind(navi);
				mPress = t + 1;
				mHoldA = true;
				mPhase = Held;
				mThrown = nullptr;
				mSawHanged = false;
				mCheckedWait = false;
				mSawFlying = false;
				mThrownEvent = false;
				mGrabSeen = false;
				mMotionSeen = false;
				mLastPendingTick = 0;
				mStepStart = t;
			} else if (t - mStepStart > 2 * bound) {
				scenarioFailf(t, "captain Walk with a Normal Pikmin in the plate within reach", "captain state %d, %d in plate, %d within %.0f", cap,
				              plateNormalCount(navi, 1e9f), plateNormalCount(navi, kSelectDistance), kSelectDistance);
			}
			break;
		case Held: {
			if (t == mPress && cap != NAVISTATE_ThrowWait) {
				scenarioFailf(t, "ThrowWait the tick after the press", "captain state %d", cap);
				return;
			}
			if (t == mPress) {
				NaviThrowWaitState* wait = static_cast<NaviThrowWaitState*>(navi->getCurrState());
				// The timing checks need a known start: case 1 a Pikmin already in the
				// hand range (its ThrowWait motion starts on the press), case 3 one
				// still walking over. A hand-over range that covers the selection
				// distance (the stand-in parameters) puts every selected Pikmin in the
				// hand; a narrower one (a real install) can leave case 1's walking over,
				// and then case 1 has no keyframe check.
				const bool walkOver = mCase == kWalkOverCase;
				const bool pending  = wait->mPendingThrowPiki && !wait->mHeldThrowPiki;
				const bool mustHold = mCase == 1 && C_NAVI_PARM(navi, mPluckGrabRange) >= kSelectDistance;
				mEarlyPending       = mCase == 1 && pending;
				if (mEarlyPending) std::printf("[scenario] throw_release case 1: the Pikmin walks over, no grab keyframe check\n");
				if ((mustHold && pending) || (walkOver && !pending)) {
					scenarioFailf(t, walkOver ? "the selected Pikmin outside the hand range (walking over)" : "the selected Pikmin in the hand range on the press",
					              "held %d, pending %d", (int)(wait->mHeldThrowPiki != nullptr), (int)(wait->mPendingThrowPiki != nullptr));
					return;
				}
			}
			bool release = false;
			if (mCase == 0) {
				if (cap == NAVISTATE_ThrowWait && static_cast<NaviThrowWaitState*>(navi->getCurrState())->mIsHoldingThrowPiki) {
					mK = t - mPress;
					release = true;
					if (mK < 3) {
						scenarioFailf(t, "K >= 3 (grab takes at least 3 ticks)", "K = %u", mK);
						return;
					}
					if (!mMotionSeen) {
						scenarioFailf(t, "the ThrowWait motion started before the grab", "no Pikmin in the hand range");
						return;
					}
					mGrabTicks = t - mMotionTick;
					std::printf("[scenario] throw_release calibration: K = %u ticks (grab keyframe %u ticks after the ThrowWait motion starts)\n", mK, mGrabTicks);
				} else if (t - mPress > bound) {
					scenarioFailf(t, "grab completes", "mIsHoldingThrowPiki never set within %u ticks", bound);
					return;
				}
			} else if (mCase == 1 || mCase == kWalkOverCase) {
				release = (t == mPress); // A down on tick P only
			} else {
				// Released two ticks after this Pikmin's own grab completes. Not K + 2
				// after the press: a Pikmin standing nearer than the calibration one
				// would be released before its grab.
				if (!mGrabSeen && cap == NAVISTATE_ThrowWait && static_cast<NaviThrowWaitState*>(navi->getCurrState())->mIsHoldingThrowPiki) {
					mGrabSeen = true;
					mGrabTick = t;
				}
				if (mGrabSeen) {
					release = (t == mGrabTick + 2);
				} else if (t - mPress > bound) {
					scenarioFailf(t, "grab completes", "mIsHoldingThrowPiki never set within %u ticks", bound);
					return;
				}
			}
			if (release) {
				mHoldA     = false;
				mPhase     = WaitThrow;
				mStepStart = t;
			}
			break;
		}
		case WaitThrow:
			if (cap == NAVISTATE_Throw) {
				// The Throw state names the Pikmin it throws.
				mThrown = static_cast<NaviThrowState*>(navi->getCurrState())->mTargetPiki;
				if (mThrown == nullptr) {
					scenarioFailf(t, "the Throw state names a Pikmin", "none");
					return;
				}
				// A release before the grab keyframe completes the grab inside the
				// same tick that enters Throw, so Hanged is visible only when the
				// release came after the grab (cases 0 and 2).
				if ((mCase == 0 || mCase == 2) && !mSawHanged) {
					scenarioFailf(t, "Pikmin Hanged before the captain enters Throw", "never Hanged during ThrowWait (state %d)", mThrown->getState());
					return;
				}
				if (mThrown->getState() == PIKISTATE_Flying) {
					scenarioFailf(t, "Pikmin not yet Flying when Throw begins", "Flying");
					return;
				}
				if (mCase == 1 && !mEarlyPending) {
					// Released before the grab keyframe: Throw waits for it.
					const unsigned since = mMotionSeen ? t - mMotionTick : 0;
					std::printf("[scenario] throw_release case 1: Throw %u ticks after the ThrowWait motion starts (grab keyframe %u)\n", since, mGrabTicks);
					if (!mMotionSeen || since < mGrabTicks) {
						scenarioFailf(t, "an early release enters Throw no sooner than the grab keyframe", "Throw %u ticks after the ThrowWait motion starts, keyframe at %u", since,
						              mGrabTicks);
						return;
					}
				}
				if (mCase == kWalkOverCase) {
					// Released while the Pikmin walks over: thrown on the tick it reaches the hand.
					std::printf("[scenario] throw_release case 3: last tick walking over %u, Throw at tick %u\n", mLastPendingTick, t);
					if (mLastPendingTick == 0 || t != mLastPendingTick + 1) {
						scenarioFailf(t, "a walked-over Pikmin is thrown on the tick it reaches the hand", "last tick walking over %u, Throw at tick %u", mLastPendingTick, t);
						return;
					}
				}
				mThrowFrom = mThrown->mSRT.t;
				mPhase     = WaitFlying;
				mStepStart = t;
			} else if (t - mStepStart > bound) {
				scenarioFailf(t, "captain enters Throw", "captain state %d after %u ticks", cap, bound);
			}
			break;
		case WaitFlying:
			// The throw flies from the captain's feet (the cursor sits there), so the
			// flight can be shorter than a tick. NaviThrowState::mHasThrownPiki is
			// set on KEY_Action0, the same event that sends the Pikmin to Flying.
			if (mThrown->getState() == PIKISTATE_Flying) mSawFlying = true;
			if (cap == NAVISTATE_Throw && static_cast<NaviThrowState*>(navi->getCurrState())->mHasThrownPiki) mThrownEvent = true;
			if (mThrownEvent || mSawFlying) {
				mPhase     = WaitLanded;
				mStepStart = t;
			} else if (t - mStepStart > bound) {
				scenarioFailf(t, "Pikmin leaves the hand (thrown)", "Pikmin state %d, captain state %d, no throw event", mThrown->getState(), cap);
			}
			break;
		case WaitLanded:
			if (mThrown->getState() == PIKISTATE_Normal) {
				// Normal alone is not a throw: the Pikmin must have left the hand.
				const float moved = (mThrown->mSRT.t - mThrowFrom).length();
				std::printf("[scenario] throw_release case %d: thrown Pikmin moved %.1f units\n", mCase, moved);
				if (moved < kMinThrowMove) {
					scenarioFailf(t, "the thrown Pikmin leaves the hand", "moved %.2f units (< %.1f) before landing", moved, kMinThrowMove);
					return;
				}
				mPhase     = WaitWalk;
				mStepStart = t;
			} else if (t - mStepStart > 2 * bound) {
				scenarioFailf(t, "Pikmin lands (Normal)", "Pikmin state %d", mThrown->getState());
			}
			break;
		case WaitWalk:
			if (cap == NAVISTATE_Walk) {
				std::printf("[scenario] throw_release case %d done at tick %u\n", mCase, t);
				if (++mCase == kCases) {
					C_NAVI_PARM(navi, mPluckGrabRange) = mHandRange;
					scenarioPass();
					return;
				}
				mPhase     = Settle;
				mStepStart = t;
			} else if (t - mStepStart > bound) {
				scenarioFailf(t, "captain returns to Walk", "captain state %d", cap);
			}
			break;
		}
	}

private:
	enum Phase { Hold, Away, Settle, Held, WaitThrow, WaitFlying, WaitLanded, WaitWalk };

	// Case 3 setup: narrow the hand-over range and stand the plate's Normal
	// Pikmin in a line behind the captain, the nearest kWalkOverDistance away.
	void placeBehind(Navi* navi)
	{
		mHandRange                         = C_NAVI_PARM(navi, mPluckGrabRange);
		C_NAVI_PARM(navi, mPluckGrabRange) = kWalkOverHandRange;
		const float back                   = navi->mFaceDirection + 3.14159265f;
		CPlate* plate                      = navi->mPlateMgr;
		int n                              = 0;
		for (int i = plate->getFirst(); !plate->isDone(i); i = plate->getNext(i)) {
			Creature* c = plate->getCreature(i);
			if (c == nullptr || c->mObjType != OBJTYPE_Piki || static_cast<Piki*>(c)->getState() != PIKISTATE_Normal) continue;
			const float d = kWalkOverDistance + 10.0f * (float)n++;
			Vector3f at   = navi->mSRT.t;
			at.x += d * std::sin(back);
			at.z += d * std::cos(back);
			at.y        = mapMgr->getMinY(at.x, at.z, true);
			c->mSRT.t   = at;
			c->mVelocity.set(0.0f, 0.0f, 0.0f);
		}
	}

	// Normal Pikmin in the plate closer to the captain than `within`.
	int plateNormalCount(Navi* navi, float within)
	{
		int n = 0;
		CPlate* plate = navi->mPlateMgr;
		for (int i = plate->getFirst(); !plate->isDone(i); i = plate->getNext(i)) {
			Creature* c = plate->getCreature(i);
			if (c == nullptr || c->mObjType != OBJTYPE_Piki || static_cast<Piki*>(c)->getState() != PIKISTATE_Normal) continue;
			Vector3f d = c->mSRT.t - navi->mSRT.t;
			if (d.length() < within) ++n;
		}
		return n;
	}

	Phase mPhase        = Away;
	unsigned mHold      = 0; // ticks of neutral input before the walk away
	unsigned mAwayStart = 0; // tick the walk away starts
	int mCase           = 0;
	unsigned mPress     = 0;
	unsigned mK         = 0;
	bool mGrabSeen      = false;
	unsigned mGrabTick  = 0;
	bool mMotionSeen    = false; // a Pikmin reached the hand range during this hold
	unsigned mMotionTick = 0;    // the tick it did (the ThrowWait motion starts)
	unsigned mGrabTicks = 0;     // calibration: ticks from the motion start to the grab keyframe
	unsigned mLastPendingTick = 0; // last tick of this hold with the Pikmin still walking over
	float mHandRange    = 0.0f;  // the hand-over range before case 3 narrowed it
	bool mEarlyPending  = false; // case 1's Pikmin was still walking over on the press
	unsigned mStepStart = 0;
	bool mHoldA         = false;
	bool mSawHanged     = false;
	bool mCheckedWait   = false; // ThrowWait preconditions asserted for this hold
	bool mSawFlying     = false;
	bool mThrownEvent   = false;
	Piki* mThrown       = nullptr;
	Vector3f mThrowFrom;
};

} // namespace

PcScenario* pc_scenario_make_throw_release(void)
{
	static ThrowRelease s;
	return &s;
}

#endif
