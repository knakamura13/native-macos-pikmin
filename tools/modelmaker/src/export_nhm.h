#pragma once

// El pack que lee el juego (NHM1 versión 3, pc_port/mods/pc_hd_models.cpp):
// huesos con su inversa de reposo (con los pivotes propios), y hasta 8 piezas
// de triángulos sueltos con una textura RGBA cada una.

#include "import.h"
#include "rig.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mm {

struct NhmPart {
	std::vector<SkinVertex> verts; ///< joint[] = articulación de Olimar
	nectar::Image texture;
	unsigned flags = 0; ///< 1 repetir, 2 sin culling, 4 sin tinte
	std::vector<int> materials; ///< de dónde sale (para el informe)
};

/// Lo que dice el semáforo.
struct Check {
	enum Level { Ok, Warn, Bad } level = Ok;
	std::string text;
};

struct ExportResult {
	std::vector<NhmPart> parts;
	std::vector<Check> checks;
	std::size_t triangles = 0, bytes = 0;
	bool atlas = false;
	bool ok() const
	{
		for (const Check& c : checks)
			if (c.level == Check::Bad) return false;
		return !parts.empty();
	}
};

/// Retoque de color de un material: se aplica al hornear su textura (y en la
/// vista), sin tocar el modelo importado.
struct MaterialLook {
	float colour[3]  = { 1, 1, 1 }; ///< multiplica
	float hue        = 0;           ///< grados, -180..180
	float brightness = 1;
	bool hidden      = false; ///< fuera del pack
	bool identity() const { return colour[0] == 1 && colour[1] == 1 && colour[2] == 1 && hue == 0 && brightness == 1; }
	bool operator==(const MaterialLook& o) const
	{
		return colour[0] == o.colour[0] && colour[1] == o.colour[1] && colour[2] == o.colour[2] && hue == o.hue && brightness == o.brightness
		    && hidden == o.hidden;
	}
	bool operator!=(const MaterialLook& o) const { return !(*this == o); }
};
/// Color 0..1: tono girado y luego por `colour` y `brightness`.
void applyLook(const MaterialLook& look, float rgb[3]);
void applyLook(const MaterialLook& look, nectar::Image& img);

struct ExportOptions {
	int textureMax = 1024;
	bool pikmin    = false; ///< hasta 100 en pantalla: presupuesto de triángulos mucho menor
	std::vector<char> noTint; ///< por material: no lo tiñe el color del J2 en cooperativo
	std::vector<MaterialLook> looks; ///< por material
};

ExportResult buildParts(const ImpModel& model, const RigResult& rig, const OlimarRig& olimar, const FitSettings& fit, const ExportOptions& opt);

/// El .nhm en memoria (para un personaje .nectarchar) o en un archivo.
bool buildNhm(const ExportResult& result, const RigResult& rig, const OlimarRig& olimar, std::vector<std::uint8_t>& out, std::string& error);
bool writeNhm(const std::string& path, const ExportResult& result, const RigResult& rig, const OlimarRig& olimar, std::string& error);

} // namespace mm
