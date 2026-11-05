#pragma once

// Luz del juego para la vista previa: las luces y el ambiente de un escenario
// (stages/*.ini) a una hora, colocadas como las coloca DayMgr.

#include "mm_math.h"

#include <string>
#include <vector>

namespace mm {

struct GameLook {
	struct Light {
		Vec3 pos;           ///< en el espacio del modelo (el de Olimar)
		float colour[3] {}; ///< 0..1
	};
	std::vector<Light> lights; ///< como mucho 4, igual que el shader del juego
	float ambient[3] {};
	// Del dibujo, no del escenario.
	float tint[3]  = { 1, 1, 1 }; ///< color del material (el tinte de los Pikmin de los tres colores)
	bool perPixel  = false;       ///< opción "Smooth" del juego; por defecto, por vértice
};

/// The Impact Site (stages/practice.ini) a mediodía. false si falta el
/// archivo o no tiene esa hora.
bool loadImpactSiteMidday(const std::string& dataDir, GameLook& out, std::string& error);

} // namespace mm
