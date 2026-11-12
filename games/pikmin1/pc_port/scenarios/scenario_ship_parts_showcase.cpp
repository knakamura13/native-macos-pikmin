// Original ship-part art capture: real pellets, fixed camera, no simulation overrides.
#include "scenarios/pc_scenario_list.h"

#if PIKI_SCENARIOS
#include "NaviMgr.h"
#include "scenarios/scenario_carry_common.h"

namespace {
class ShipPartsShowcase : public PcScenario {
public:
	const char* name() const override { return "ship_parts_showcase"; }
	const char* stageFile() const override { return "stages/sandbox_showcase.ini"; }

	void onStageReady() override
	{
		// Away from the landing-site props; the same terrain and object draw as play.
		mEngine = carry::spawnPellet(carry::id("ust5"), 260.0f, 240.0f);
		mRadar = carry::spawnPellet(carry::id("uf01"), 315.0f, 240.0f);
		if (!mEngine || !mRadar) {
			scenarioFail(0, "engine and radar pellets spawn", "a pellet is missing");
			return;
		}
		mEngine->mFaceDirection = mEngine->mSRT.r.y = 0.0f;
		mRadar->mFaceDirection = mRadar->mSRT.r.y = 0.0f;
	}

	void providePads(unsigned, PcInputPad&) override
	{
		PcScenarioDebugCamera cam {};
		cam.active = true;
		cam.eyeX = 287.5f; cam.eyeY = 65.0f; cam.eyeZ = 360.0f;
		cam.focusX = 287.5f; cam.focusY = 5.0f; cam.focusZ = 240.0f;
		cam.fov = 40.0f;
		pc_scenario_set_debug_camera(cam);
	}

	void onTick(unsigned tick) override
	{
		if (tick >= 149) scenarioPass();
	}

private:
	Pellet* mEngine = nullptr;
	Pellet* mRadar = nullptr;
};
}

PcScenario* pc_scenario_make_ship_parts_showcase(void)
{
	static ShipPartsShowcase s;
	return &s;
}
#endif
