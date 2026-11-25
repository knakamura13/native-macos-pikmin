// rng_draw_order: one of the expressions that draw two random numbers, run in the game binary,
// draws them in the original order on this compiler. Stand-in sandbox, 30 Hz.
//
// ViewPiki::init places a new Pikmin at a random point: x and z are each (getRand(1) - 0.5) * 300.
// As two arguments of one call their order is unspecified in C++ (clang evaluates left to right,
// GCC right to left), so the port draws them into named locals: z first, then x, the original
// game's order. The same holds for the other seven sites that tools/rng-sequence/check.py lists;
// that check compiles the extracted expressions on its own, outside the game.
//
// On the stage-ready tick the scenario takes a Pikmin from the pool that has not been born, saves
// the sim RNG state, runs ViewPiki::init on it again (the pool runs it once per object at stage
// load), reads the point it chose (mLastEffectPosition, set from mSRT.t at the end of init), then
// restores the saved state and draws two numbers the same way. Asserted: z came from the first
// draw and x from the second (within 0.001 units), and the two draws differ, so the opposite order
// could not match as well. The RNG state is left as init left it.
//
// The re-run init registers the object's update contexts again and allocates a new collision
// info; the scenario takes them back out (UpdateContext::exit) and ends on the same tick.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cmath>
#include <cstdio>

#include "PikiMgr.h"
#include "ViewPiki.h"
#include "netplay/pc_sim_rng.h"
#include "system.h"

namespace {

constexpr float kTolerance = 0.001f;

float spawnCoordinate(float draw) { return (draw - 0.5f) * 300.0f; }

// The object pool's protected arrays (member pointers through a derived name).
struct PoolPeek : MonoObjectMgr {
	static Creature* object(MonoObjectMgr& m, int i) { return (m.*&PoolPeek::mObjectList)[i]; }
	static int status(MonoObjectMgr& m, int i) { return (m.*&PoolPeek::mEntryStatus)[i]; }
	static int size(MonoObjectMgr& m) { return m.*&PoolPeek::mMaxElements; }
};

class RngDrawOrder : public PcScenario {
public:
	const char* name() const override { return "rng_draw_order"; }

	void onStageReady() override
	{
		ViewPiki* piki = nullptr;
		const int poolSize = PoolPeek::size(*pikiMgr);
		for (int i = 0; i < poolSize && !piki; i++) {
			if (PoolPeek::status(*pikiMgr, i) == -1) piki = static_cast<ViewPiki*>(PoolPeek::object(*pikiMgr, i));
		}
		if (!piki) {
			scenarioFailf(0, "a Pikmin in the pool that has not been born", "none of %d", poolSize);
			return;
		}
		const unsigned before = pc_sim_rng_state();
		piki->init(pikiMgr->mPikiShape, pikiMgr->mMapMgr, pikiMgr->mNavi);
		const unsigned after = pc_sim_rng_state();
		piki->mPikiUpdateContext.exit();
		piki->mPikiLookUpdateContext.exit();
		piki->mOptUpdateContext.exit();
		const Vector3f at = piki->mLastEffectPosition;

		pc_sim_rng_set_state(before);
		const float first  = gsys->getRand(1.0f);
		const float second = gsys->getRand(1.0f);
		pc_sim_rng_set_state(after);

		std::printf("[scenario] rng_draw_order: ViewPiki::init chose x %.3f, z %.3f; the first two draws give %.3f then %.3f\n", at.x, at.z, spawnCoordinate(first),
		            spawnCoordinate(second));
		if (first == second) {
			scenarioFailf(0, "two different draws", "both %.6f", first);
			return;
		}
		if (std::fabs(at.z - spawnCoordinate(first)) > kTolerance || std::fabs(at.x - spawnCoordinate(second)) > kTolerance) {
			scenarioFailf(0, "ViewPiki::init draws z first, then x (the original order)", "x %.3f, z %.3f; first draw %.3f, second %.3f", at.x, at.z, spawnCoordinate(first),
			              spawnCoordinate(second));
			return;
		}
		scenarioPass();
	}

	void providePads(unsigned, PcInputPad&) override {}
	void onTick(unsigned) override {}
};

} // namespace

PcScenario* pc_scenario_make_rng_draw_order(void)
{
	return new RngDrawOrder();
}

#endif
