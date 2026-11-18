// update_rotation: the five UpdateMgr slot rotations (Pikmin AI, search, head look, Pikmin and
// enemy optimisation) advance once per original 30 Hz frame at any tick rate, on the stand-in
// sandbox.
//
// An UpdateMgr spreads its clients over mSlotCount slots and lets one slot run per step
// (GameCoreSection::updateAI calls update() once per tick, clients ask updatable()). The original
// game steps 30 times a second, so a client runs 30 / mSlotCount times a second. At 60 Hz the port
// advances the slot index once per 30 Hz step and reports the slot as updatable only on the tick
// it advanced (UpdateMgr::update, mPcSubTick and mPcFresh), so the per-second rate is kept.
//
// Each tick for kSeconds after the intro, the scenario asks every manager's updatable() for every
// slot, with a probe UpdateContext that names the slot (updatable() reads only the slot index).
// Asserted per manager:
//   - at most one slot is updatable on a tick;
//   - the ticks with an updatable slot number 30 per game second (60 in 2 s), one per 30 Hz frame;
//   - successive updatable slots follow the rotation (index + 1, wrapping at mSlotCount);
//   - each slot is updatable 60 / mSlotCount times, rounded down or up.
// Registered at 60 Hz only: at 30 Hz every tick is a 30 Hz step and the port code does nothing.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS

#include <cstdio>
#include <vector>

#include "UpdateMgr.h"

namespace {

constexpr float kSeconds = 2.0f;
constexpr int kStepsPerSecond = 30; // the original frame rate

struct Probe {
	const char* name;
	UpdateMgr** mgr;
	std::vector<int> perSlot;
	int steps    = 0;
	int lastSlot = -1;
};

class UpdateRotation : public PcScenario {
public:
	const char* name() const override { return "update_rotation"; }

	void onStageReady() override
	{
		for (Probe& p : mProbes) {
			if (*p.mgr == nullptr || (*p.mgr)->mSlotCount <= 0) {
				fail(0, "an UpdateMgr with slots", "%s: %d slots", p.name, *p.mgr ? (*p.mgr)->mSlotCount : -1);
				return;
			}
			p.perSlot.assign((*p.mgr)->mSlotCount, 0);
		}
	}

	void providePads(unsigned, PcInputPad&) override {}

	void onTick(unsigned t) override
	{
		if (mDone) return;
		const unsigned rate = unsigned(pc_scenario_ticks_per_second());
		for (Probe& p : mProbes) {
			UpdateMgr* mgr = *p.mgr;
			UpdateContext probe;
			int slot = -1;
			for (int s = 0; s < mgr->mSlotCount; s++) {
				probe.mMgrSlotIndex = s;
				if (!mgr->updatable(&probe)) continue;
				if (slot >= 0) {
					fail(t, "at most one updatable slot per tick", "%s: slots %d and %d", p.name, slot, s);
					return;
				}
				slot = s;
			}
			if (slot < 0) continue;
			if (p.lastSlot >= 0 && slot != (p.lastSlot + 1) % mgr->mSlotCount) {
				fail(t, "the updatable slot advances by one", "%s: slot %d after %d", p.name, slot, p.lastSlot);
				return;
			}
			p.lastSlot = slot;
			p.perSlot[slot]++;
			p.steps++;
		}
		if (++mTicks < unsigned(kSeconds * rate)) return;

		const int steps = int(kSeconds * kStepsPerSecond);
		for (Probe& p : mProbes) {
			const int n  = int(p.perSlot.size());
			const int lo = steps / n;
			const int hi = (steps + n - 1) / n;
			std::printf("[scenario] update_rotation: %s, %d slots: %d updatable ticks in %u ticks; per slot", p.name, n, p.steps, mTicks);
			for (int c : p.perSlot) std::printf(" %d", c);
			std::printf("\n");
			if (p.steps != steps) {
				fail(t, "one updatable tick per 30 Hz frame (60 in 2 s)", "%s: %d in %u ticks", p.name, p.steps, mTicks);
				return;
			}
			for (int s = 0; s < n; s++) {
				if (p.perSlot[s] < lo || p.perSlot[s] > hi) {
					fail(t, "each slot updatable 60 / slot count times in 2 s (rounded down or up)", "%s: slot %d %d times", p.name, s, p.perSlot[s]);
					return;
				}
			}
		}
		mDone = true;
		scenarioPass();
	}

private:
	void fail(unsigned t, const char* expected, const char* fmt, const char* a, int b, int c = 0)
	{
		mDone = true;
		scenarioFailf(t, expected, fmt, a, b, c);
	}

	Probe mProbes[5] = {
		{ "pikiUpdateMgr", &pikiUpdateMgr, {} },     { "searchUpdateMgr", &searchUpdateMgr, {} }, { "pikiLookUpdateMgr", &pikiLookUpdateMgr, {} },
		{ "pikiOptUpdateMgr", &pikiOptUpdateMgr, {} }, { "tekiOptUpdateMgr", &tekiOptUpdateMgr, {} },
	};
	unsigned mTicks = 0;
	bool mDone      = false;
};

} // namespace

PcScenario* pc_scenario_make_update_rotation(void)
{
	return new UpdateRotation();
}

#endif
