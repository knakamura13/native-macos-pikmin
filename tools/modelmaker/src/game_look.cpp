#include "game_look.h"

#include "nectar_assets/ini.h"

namespace mm {

namespace {

constexpr int kTimeDay = 2; // TIME_Day de DayMgr.h: el que se usa entre las 8 y las 15

// DayMgr::refresh a las 12:00: el giro del sol es 12/24·π − π/2 = 0, así que
// queda justo encima, en (0, 2500, 0).
const Vec3 kSunMidday { 0, 2500, 0 };

} // namespace

bool loadImpactSiteMidday(const std::string& dataDir, GameLook& out, std::string& error)
{
	nectar::Scene scene;
	if (!nectar::loadScene(dataDir + "/stages/practice.ini", scene, error)) return false;
	if (scene.times.size() <= std::size_t(kTimeDay)) {
		error = "stages/practice.ini: no day time setting";
		return false;
	}
	const nectar::SceneTime& t = scene.times[std::size_t(kTimeDay)];
	out.lights.clear();
	for (std::size_t i = 0; i < t.lights.size() && out.lights.size() < 4; ++i) {
		const nectar::SceneLight& l = t.lights[i];
		GameLook::Light g;
		// Las paralelas (tipo 1) no usan la posición del .ini: las pares son el
		// sol y las impares lo iluminan desde abajo (DayMgr::refresh). El resto
		// va donde dice el .ini, con el capitán en el origen. Sin atenuación:
		// el port del juego tampoco aplica la del foco.
		if (l.type == 1) g.pos = (i % 2 == 0) ? kSunMidday : kSunMidday * -1.0f;
		else g.pos = l.position;
		g.colour[0] = l.colour.r / 255.0f;
		g.colour[1] = l.colour.g / 255.0f;
		g.colour[2] = l.colour.b / 255.0f;
		out.lights.push_back(g);
	}
	out.ambient[0] = t.ambient.r / 255.0f;
	out.ambient[1] = t.ambient.g / 255.0f;
	out.ambient[2] = t.ambient.b / 255.0f;
	return true;
}

} // namespace mm
