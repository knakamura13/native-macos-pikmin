#pragma once

// Piezas del modelo: cada material se puede mover, girar, escalar y pasar o
// copiar al otro lado. Es una capa aparte, como los retoques de color: se
// aplica en buildRig sobre la malla ya colocada y antes de repartir los pesos,
// así que aguanta volver a simplificar, vale con los pesos del modelo y con los
// automáticos, y sale en el pack.

#include "mm_math.h"
#include "olimar.h"

#include <cstdint>
#include <vector>

namespace mm {

struct PartEdit {
	/// En el espacio de Olimar, alrededor del centro de la pieza sin mover
	/// (RigResult::partCentre): p' = c + local · (p − c).
	Mat34 local;
	bool flipped    = false; ///< pasada al otro lado (reflejo en x = 0), antes de `local`
	bool mirrorCopy = false; ///< y además una copia reflejada en x = 0
	bool identity() const;
	bool operator==(const PartEdit& o) const;
	bool operator!=(const PartEdit& o) const { return !(*this == o); }
};

/// La articulación del otro lado (brazos y piernas); las del centro, igual.
int mirrorJoint(int joint);
/// Reflejo de `local` en x = 0: la misma pieza vista en el espejo.
Mat34 mirrorLocal(const Mat34& local);
/// Para las normales: la inversa traspuesta de la parte 3x3.
Mat34 normalMatrix(const Mat34& m);

struct PartEditResult {
	std::vector<Vec3> centre;          ///< por material: centro de su caja sin mover (ya reflejada si va al otro lado)
	std::vector<std::uint32_t> source; ///< por vértice: el del modelo de donde sale
	std::vector<char> mirrored;        ///< por vértice: reflejado (sus pesos van al otro lado)
	std::vector<char> triCopy;         ///< por triángulo: copia reflejada añadida
};

/// Aplica `edits` (por material; los que faltan no cambian) a la malla ya
/// colocada: mueve los triángulos de cada pieza y añade al final las copias.
void applyPartEdits(const std::vector<PartEdit>& edits, std::vector<SkinVertex>& verts, std::vector<int>& triMaterial, PartEditResult& out);

} // namespace mm
