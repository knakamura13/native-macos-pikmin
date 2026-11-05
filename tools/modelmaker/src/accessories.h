#pragma once

// Piezas para enganchar (sombreros, antenas, cascos...): las de serie se
// generan aquí con código (formas simples de un color, nada sacado del juego)
// y las del usuario se leen de <juego>/Load/Models/Parts con los mismos
// importadores. Cada pieza va rígida con una articulación (peso 1), en un
// punto sacado de los marcadores del personaje.

#include "export_nhm.h"
#include "import.h"
#include "rig.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace mm {

/// Dónde se engancha.
enum AttachPoint : int {
	P_HEADTOP,
	P_FACE,
	P_CHEST,
	P_BACK,
	P_LHAND,
	P_RHAND,
	P_COUNT
};
const char* attachPointName(int point);
const char* attachPointId(int point); ///< para el proyecto
int attachPointFromId(const std::string& id);
int attachPointJoint(int point);

/// Una pieza puesta en el personaje. Vive fuera de model_: se añade a sus
/// materiales y triángulos cada vez que se monta el modelo.
struct Attachment {
	std::string part; ///< "builtin:<id>" o la ruta del archivo
	int point     = P_HEADTOP;
	float size    = 1;     ///< sobre el tamaño automático
	float up      = 0;     ///< en unidades del punto (ancho de la cabeza, del pecho...)
	float forward = 0;
	int uid       = 0; ///< para seguir a sus materiales (colores) aunque cambien de sitio
	PartEdit edit;     ///< movida, girada o reflejada con el gizmo, entera (part_edit.h)
	bool operator==(const Attachment& o) const
	{
		return part == o.part && point == o.point && size == o.size && up == o.up && forward == o.forward && uid == o.uid && edit == o.edit;
	}
	bool operator!=(const Attachment& o) const { return !(*this == o); }
};

/// Pieza de serie: id, nombre y dónde va por defecto.
struct BuiltinPart {
	const char* id;
	const char* es;
	const char* en;
	int point;
	float up; ///< hundida (el casco envuelve la cabeza)
};
const std::vector<BuiltinPart>& builtinParts();
bool isBuiltinPart(const std::string& part);

/// La malla de una pieza en su propio espacio (Y arriba, mira hacia +Z).
struct PartMesh {
	ImpModel mesh;
	bool normalize = false; ///< del usuario: se lleva a tamaño 1
	std::string name;       ///< para mostrar
	std::string error;
	bool ok() const { return error.empty() && !mesh.verts.empty(); }
};

/// Las piezas: las de serie y las del usuario (leídas una vez y guardadas).
class PartLibrary {
public:
	/// La malla de `part` (nullptr si no se pudo abrir: el motivo en `error`).
	const PartMesh* get(const std::string& part, std::string& error);
	/// Archivos de la carpeta del usuario (se vuelve a mirar cada poco).
	const std::vector<std::string>& userFiles(const std::string& dir, double now);
	/// Solo si ya se abrió bien.
	const PartMesh* find(const std::string& part) const;

private:
	std::map<std::string, PartMesh> cache_;
	PartMesh builtin_;
	std::vector<std::string> files_;
	std::string filesDir_;
	double scanned_ = -1e9;
};

/// Nombre corto de la pieza (el de serie o el nombre del archivo).
std::string partLabel(const std::string& part);

/// Dónde va cada punto en el personaje montado (antes de las piezas).
struct AttachSite {
	Vec3 rest, posed; ///< sin enderezar (para editar los puntos) y enderezado
	float ref = 1;    ///< tamaño de referencia: ancho de la cabeza, del pecho...
	int joint = J_HEAD;
};
std::array<AttachSite, P_COUNT> attachSites(const RigResult& rig, const FitSettings& fit);

/// Añade la pieza a `rig` (vértices rígidos con la articulación del punto),
/// con sus materiales desde `firstMaterial`, y con `a.edit` alrededor de su
/// centro sin mover, que va en `centre` (en reposo, como RigResult::partCentre).
void placePart(const PartMesh& part, const Attachment& a, const AttachSite& site, int firstMaterial, RigResult& rig, Vec3& centre);

} // namespace mm
