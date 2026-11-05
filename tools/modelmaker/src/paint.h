#pragma once

// Pintar sobre el personaje: una capa RGBA por material encima de su textura
// (sin tocarla), como los retoques de color. El pincel es una esfera en 3D
// sobre la malla en reposo: cada texel de los triángulos que toca se pinta
// según su distancia al centro, así que el trazo no se estira con las UV y
// pasa de una pieza a otra. Los materiales cuyas UV no sirven (no tienen, o
// se pisan) reciben unas nuevas (xatlas) en el modelo original, para que no
// cambien al simplificarlo otra vez.

#include "import.h"
#include "mm_math.h"
#include "nectar_assets/image.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mm {

/// Lados de una capa de pintura: los de la textura del material, ampliada
/// hasta 256 (en una de 64x64 un trazo serían cuatro píxeles) y reducida
/// hasta 1024 (memoria para deshacer y para el juego). Sin textura, 512.
constexpr int kPaintMinSide = 256, kPaintMaxSide = 1024, kPaintSolidSide = 512;
void paintLayerSize(const ImpMaterial& mat, int& w, int& h);

/// ¿Sirven las UV de los triángulos del material `m` para pintar?
enum class UvUse { Good, Missing, Overlapping };
UvUse uvUse(const std::vector<SkinVertex>& verts, const std::vector<int>& triMaterial, int m);

/// Sin ningún píxel pintado (nunca pintada, borrada o deshecha).
bool paintEmpty(const nectar::Image& layer);

/// La pintura encima de `base` (alfa normal). Si no son del mismo tamaño, el
/// resultado tiene el mayor de cada lado (bilineal).
void compositePaint(nectar::Image& base, const nectar::Image& layer);
/// Un píxel de pintura encima de uno de la base.
void blendPaint(const std::uint8_t base[4], const std::uint8_t paint[4], std::uint8_t out[4]);
/// La imagen a otro tamaño (bilineal).
nectar::Image resizeImage(const nectar::Image& img, int w, int h);

/// UV nuevas (xatlas) para los triángulos del material `m`. Si tenía textura
/// o colores de vértice, se hornean con las UV nuevas en una textura propia.
/// Las UV se redondean a 16 bits (como se guardan en el proyecto). Lo que
/// ya hubiera en `paint` (un relleno) se pasa igual a las UV nuevas.
bool unwrapMaterial(ImpModel& model, int m, std::string& error, nectar::Image* paint = nullptr);

/// Las UV de los triángulos de `m` (para guardarlas con el proyecto).
std::vector<std::uint8_t> packUvs(const ImpModel& model, int m);
bool unpackUvs(ImpModel& model, int m, const std::vector<std::uint8_t>& data);

bool encodePng(const nectar::Image& img, std::vector<std::uint8_t>& out);
bool decodePng(const std::vector<std::uint8_t>& data, nectar::Image& out);

struct Brush {
	float colour[3] = { 0.95f, 0.25f, 0.2f };
	float radius    = 1;    ///< unidades del modelo
	float hardness  = 0.6f; ///< 0 borde suave, 1 duro
	float opacity   = 1;
	bool erase      = false;
};

/// Lo que había en un rectángulo de una capa: deshacer y rehacer lo
/// intercambian con la capa.
struct PaintPatch {
	int material = 0, x = 0, y = 0, w = 0, h = 0;
	std::vector<std::uint8_t> rgba;
	void swap(nectar::Image& layer);
};

/// Dónde se puede pintar (por material) y el tamaño de su capa.
struct PaintTarget {
	bool paintable = false;
	int width = 0, height = 0;
};

/// El triángulo bajo el ratón.
struct PaintHit {
	std::size_t tri = 0;
	Vec3 point, normal;
};
/// Rayo contra la malla (solo materiales con `visible`): el más cercano.
bool paintRaycast(const std::vector<SkinVertex>& verts, const std::vector<int>& triMaterial, const std::vector<char>& visible, const Vec3& origin,
                  const Vec3& dir, PaintHit& hit);

/// Un trazo: cada capa que toca guarda cómo estaba y una máscara (lo más
/// fuerte que ha pasado el pincel por cada texel), así repasar no acumula
/// más que la opacidad elegida.
class PaintStroke {
public:
	void begin(const Brush& brush, const std::vector<SkinVertex>& verts, const std::vector<int>& triMaterial, const std::vector<PaintTarget>& targets);
	/// La esfera del pincel en `centre`; no pasa a las caras que miran al
	/// otro lado de `normal` (la espalda de una capa fina).
	void stamp(std::vector<nectar::Image>& layers, const Vec3& centre, const Vec3& normal);
	/// Todo el material de una vez.
	void fill(std::vector<nectar::Image>& layers, int material);
	/// Lo de antes de cada capa tocada (vacío si no cambió nada).
	std::vector<PaintPatch> end(const std::vector<nectar::Image>& layers);
	bool active() const { return active_; }

	/// Rectángulos cambiados desde la última vez (para subir solo eso).
	struct Rect {
		int material = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0; ///< x1, y1 fuera
	};
	std::vector<Rect> takeDirty();

private:
	struct Layer {
		int material = 0;
		nectar::Image before;
		std::vector<float> mask;
		Rect all, recent, stamp; ///< todo el trazo, desde takeDirty, este toque
	};
	Layer& layerFor(std::vector<nectar::Image>& layers, int m);
	void apply(const Layer& l, nectar::Image& layer, const Rect& r) const;

	Brush brush_;
	const std::vector<SkinVertex>* verts_ = nullptr;
	const std::vector<int>* triMaterial_  = nullptr;
	std::vector<PaintTarget> targets_;
	std::vector<Vec3> triMin_, triMax_, triNormal_;
	std::vector<Layer> layers_;
	bool active_ = false;
};

/// El estado del modo pintar en la ventana (paint_ui.cpp).
struct PaintState {
	bool open   = false; ///< sección abierta (en el paso 1 es el modo pintar)
	bool on     = false; ///< modo pintar en este fotograma
	int tool    = 0;     ///< 0 pincel, 1 goma, 2 cubo
	float size  = 0.06f; ///< radio del pincel, parte de la altura del modelo
	Brush brush;
	PaintStroke stroke;
	bool stroking = false;
	float lastX = 0, lastY = 0;
	int hoverMaterial = -1;
	/// Por material del modelo original: UV de xatlas y textura horneada.
	std::vector<char> autoUv, bakedBase;
	/// UV y texturas guardadas con el proyecto, para el hilo que lo abre.
	struct Setup {
		int material = 0;
		std::vector<std::uint8_t> uv;
		nectar::Image base;
	};
	std::vector<Setup> setups;
	std::vector<int> applied; ///< materiales a los que se aplicó su Setup
	int preparing = -1;       ///< material al que se le hacen UV (en otro hilo)
	nectar::Image preparedLayer; ///< su pintura, pasada a las UV nuevas por ese hilo
	/// Vista mientras se pinta: la base de cada material al tamaño de su capa,
	/// y si su textura ya es de ese tamaño (se sube solo lo que cambia).
	std::vector<nectar::Image> previewBase;
	std::vector<char> live;
	float radiusPx = 10;
	/// Análisis de UV por material, de la malla actual.
	std::vector<UvUse> uvUse;
	const void* uvKey = nullptr;
	std::size_t uvKeySize = 0;
};

} // namespace mm
