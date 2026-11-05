#include "paint.h"

#include "lang.h"
#include "stb_image.h"
#include "xatlas.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>

namespace mm {

namespace fs = std::filesystem;

namespace {

struct P2 {
	float x = 0, y = 0;
};

// Normal de un triángulo: la media de las de sus vértices (más fiable que el
// orden de los vértices); si no tiene, la de la cara.
Vec3 triangleNormal(const SkinVertex* v)
{
	const Vec3 n = v[0].n + v[1].n + v[2].n;
	if (dot(n, n) > 1e-12f) return normalize(n);
	return normalize(cross(v[1].p - v[0].p, v[2].p - v[0].p));
}

// Punto del segmento a-b más cerca de p: parámetro y distancia al cuadrado.
float closestOnSegment(P2 p, P2 a, P2 b, float& d2)
{
	const float ex = b.x - a.x, ey = b.y - a.y, l = ex * ex + ey * ey;
	const float t = l > 0 ? std::clamp(((p.x - a.x) * ex + (p.y - a.y) * ey) / l, 0.0f, 1.0f) : 0.0f;
	const float dx = a.x + ex * t - p.x, dy = a.y + ey * t - p.y;
	d2 = dx * dx + dy * dy;
	return t;
}

// Recorre los texels de un triángulo (en texels) dentro de [cx0,cx1)x[cy0,cy1),
// y los de fuera a menos de `margin` (para que no quede una raya en las
// costuras). f(x, y, b0, b1, b2) con las baricéntricas del punto más cercano.
template <typename F>
void rasterTriangle(const P2 q[3], float margin, int cx0, int cy0, int cx1, int cy1, F&& f)
{
	const float minX = std::min({ q[0].x, q[1].x, q[2].x }) - margin, maxX = std::max({ q[0].x, q[1].x, q[2].x }) + margin;
	const float minY = std::min({ q[0].y, q[1].y, q[2].y }) - margin, maxY = std::max({ q[0].y, q[1].y, q[2].y }) + margin;
	const int x0 = std::max(cx0, int(std::floor(minX))), x1 = std::min(cx1, int(std::ceil(maxX)) + 1);
	const int y0 = std::max(cy0, int(std::floor(minY))), y1 = std::min(cy1, int(std::ceil(maxY)) + 1);
	const float d1x = q[1].x - q[0].x, d1y = q[1].y - q[0].y, d2x = q[2].x - q[0].x, d2y = q[2].y - q[0].y;
	const float den = d1x * d2y - d1y * d2x;
	if (std::fabs(den) < 1e-9f) return;
	const float m2 = margin * margin;
	for (int y = y0; y < y1; ++y) {
		for (int x = x0; x < x1; ++x) {
			const P2 p { float(x) + 0.5f, float(y) + 0.5f };
			const float sx = p.x - q[0].x, sy = p.y - q[0].y;
			const float a = (sx * d2y - sy * d2x) / den, b = (d1x * sy - d1y * sx) / den, c = 1 - a - b;
			if (a >= 0 && b >= 0 && c >= 0) {
				f(x, y, c, a, b);
				continue;
			}
			if (margin <= 0) continue;
			float best = 1e30f, d2;
			float bw[3] = {};
			for (int e = 0; e < 3; ++e) {
				const int i = e, j = (e + 1) % 3;
				const float t = closestOnSegment(p, q[i], q[j], d2);
				if (d2 < best) {
					best = d2;
					bw[0] = bw[1] = bw[2] = 0;
					bw[i] = 1 - t;
					bw[j] = t;
				}
			}
			if (best <= m2) f(x, y, bw[0], bw[1], bw[2]);
		}
	}
}

// Bilineal en texels (centros en .5). `premul`: el color por su alfa.
void sampleBilinear(const nectar::Image& img, float x, float y, bool wrap, bool premul, float out[4])
{
	x -= 0.5f, y -= 0.5f;
	const int ix = int(std::floor(x)), iy = int(std::floor(y));
	const float fx = x - float(ix), fy = y - float(iy);
	auto fetch = [&](int px, int py) {
		if (wrap) {
			px = ((px % img.width) + img.width) % img.width;
			py = ((py % img.height) + img.height) % img.height;
		} else {
			px = std::clamp(px, 0, img.width - 1);
			py = std::clamp(py, 0, img.height - 1);
		}
		return img.at(px, py);
	};
	const std::uint8_t* t[4] = { fetch(ix, iy), fetch(ix + 1, iy), fetch(ix, iy + 1), fetch(ix + 1, iy + 1) };
	const float w[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
	for (int k = 0; k < 4; ++k) out[k] = 0;
	for (int s = 0; s < 4; ++s) {
		const float a = t[s][3] / 255.0f;
		for (int k = 0; k < 3; ++k) out[k] += w[s] * t[s][k] / 255.0f * (premul ? a : 1.0f);
		out[3] += w[s] * a;
	}
}

std::uint8_t toByte(float v) { return std::uint8_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// Rellena lo que no cubre ningún triángulo con la media de sus vecinos (si
// no, el filtrado y los mipmaps traen el fondo a las costuras).
void dilate(nectar::Image& img, std::vector<char>& filled, int passes)
{
	const int w = img.width, h = img.height;
	for (int pass = 0; pass < passes; ++pass) {
		std::vector<char> next = filled;
		nectar::Image out      = img;
		bool any               = false;
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				if (filled[std::size_t(y * w + x)]) continue;
				float sum[4] = {};
				int n        = 0;
				for (int dy = -1; dy <= 1; ++dy)
					for (int dx = -1; dx <= 1; ++dx) {
						const int nx = x + dx, ny = y + dy;
						if (nx < 0 || ny < 0 || nx >= w || ny >= h || !filled[std::size_t(ny * w + nx)]) continue;
						for (int k = 0; k < 4; ++k) sum[k] += img.at(nx, ny)[k];
						++n;
					}
				if (!n) continue;
				for (int k = 0; k < 4; ++k) out.at(x, y)[k] = std::uint8_t(sum[k] / float(n) + 0.5f);
				next[std::size_t(y * w + x)] = 1;
				any                          = true;
			}
		}
		img    = std::move(out);
		filled = std::move(next);
		if (!any) break;
	}
	// Lo que quede, del color medio.
	double sum[4] = {};
	std::size_t n = 0;
	for (std::size_t i = 0; i < filled.size(); ++i)
		if (filled[i]) {
			for (int k = 0; k < 4; ++k) sum[k] += img.rgba[i * 4 + std::size_t(k)];
			++n;
		}
	if (!n) return;
	for (std::size_t i = 0; i < filled.size(); ++i)
		if (!filled[i])
			for (int k = 0; k < 4; ++k) img.rgba[i * 4 + std::size_t(k)] = std::uint8_t(sum[k] / double(n) + 0.5);
}

int pow2Side(int side)
{
	int s = 1;
	while (s < side) s *= 2;
	return std::clamp(s, kPaintMinSide, kPaintMaxSide);
}

} // namespace

void paintLayerSize(const ImpMaterial& mat, int& w, int& h)
{
	if (mat.texture.empty()) {
		w = h = kPaintSolidSide;
		return;
	}
	w = mat.texture.width, h = mat.texture.height;
	while (std::max(w, h) < kPaintMinSide) w *= 2, h *= 2;
	while (std::max(w, h) > kPaintMaxSide) w = std::max(1, w / 2), h = std::max(1, h / 2);
}

UvUse uvUse(const std::vector<SkinVertex>& verts, const std::vector<int>& triMaterial, int m)
{
	// Se pinta cada triángulo en una rejilla de 256x256 (repetida: una UV de
	// mosaico vuelve a caer en los mismos texels). Si cada texel se pinta de
	// media más de 1,5 veces, las piezas se pisan (espejo, mosaico, paleta).
	constexpr int N = 256;
	std::vector<std::uint16_t> hits(std::size_t(N * N), 0);
	double area      = 0;
	std::size_t tris = 0;
	const std::size_t count = std::min(verts.size() / 3, triMaterial.size());
	for (std::size_t t = 0; t < count; ++t) {
		if (triMaterial[t] != m) continue;
		++tris;
		const SkinVertex* v = &verts[t * 3];
		const float fu = std::floor(std::min({ v[0].uv.x, v[1].uv.x, v[2].uv.x })), fv = std::floor(std::min({ v[0].uv.y, v[1].uv.y, v[2].uv.y }));
		P2 q[3];
		for (int k = 0; k < 3; ++k) q[k] = { (v[k].uv.x - fu) * N, (v[k].uv.y - fv) * N };
		area += std::fabs((q[1].x - q[0].x) * (q[2].y - q[0].y) - (q[1].y - q[0].y) * (q[2].x - q[0].x)) * 0.5 / double(N * N);
		// Uno que cruza muchas veces la textura ya es un mosaico.
		if (std::max({ q[0].x, q[1].x, q[2].x }) > 4 * N || std::max({ q[0].y, q[1].y, q[2].y }) > 4 * N) return UvUse::Overlapping;
		rasterTriangle(q, 0.0f, 0, 0, 4 * N, 4 * N, [&](int x, int y, float, float, float) {
			std::uint16_t& h = hits[std::size_t((y % N) * N + (x % N))];
			if (h < 65535) ++h;
		});
	}
	if (!tris) return UvUse::Good;
	if (area < 1e-6) return UvUse::Missing;
	std::size_t covered = 0, total = 0;
	for (std::uint16_t h : hits) {
		covered += h ? 1 : 0;
		total += h;
	}
	if (!covered) return UvUse::Missing;
	return double(total) / double(covered) > 1.5 ? UvUse::Overlapping : UvUse::Good;
}

bool paintEmpty(const nectar::Image& layer)
{
	if (layer.empty()) return true;
	for (std::size_t i = 3; i < layer.rgba.size(); i += 4)
		if (layer.rgba[i]) return false;
	return true;
}

void blendPaint(const std::uint8_t base[4], const std::uint8_t paint[4], std::uint8_t out[4])
{
	if (!paint[3]) {
		std::memcpy(out, base, 4);
		return;
	}
	const float a = paint[3] / 255.0f, ab = base[3] / 255.0f, ao = a + ab * (1 - a);
	for (int k = 0; k < 3; ++k) out[k] = toByte((paint[k] / 255.0f * a + base[k] / 255.0f * ab * (1 - a)) / ao);
	out[3] = toByte(ao);
}

nectar::Image resizeImage(const nectar::Image& img, int w, int h)
{
	if (img.width == w && img.height == h) return img;
	nectar::Image out;
	out.resize(w, h);
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			float c[4];
			sampleBilinear(img, (float(x) + 0.5f) * float(img.width) / float(w), (float(y) + 0.5f) * float(img.height) / float(h), false, false, c);
			for (int k = 0; k < 4; ++k) out.at(x, y)[k] = toByte(c[k]);
		}
	return out;
}

void compositePaint(nectar::Image& base, const nectar::Image& layer)
{
	if (paintEmpty(layer) || base.empty()) return;
	const int w = std::max(base.width, layer.width), h = std::max(base.height, layer.height);
	nectar::Image out = base.width == w && base.height == h ? std::move(base) : resizeImage(base, w, h);
	const bool same   = layer.width == w && layer.height == h;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			if (same) {
				blendPaint(out.at(x, y), layer.at(x, y), out.at(x, y));
				continue;
			}
			float p[4]; // color por alfa
			sampleBilinear(layer, (float(x) + 0.5f) * float(layer.width) / float(w), (float(y) + 0.5f) * float(layer.height) / float(h), false, true, p);
			if (p[3] <= 0) continue;
			std::uint8_t* o = out.at(x, y);
			const float ab = o[3] / 255.0f, ao = p[3] + ab * (1 - p[3]);
			for (int k = 0; k < 3; ++k) o[k] = toByte((p[k] + o[k] / 255.0f * ab * (1 - p[3])) / ao);
			o[3] = toByte(ao);
		}
	}
	base = std::move(out);
}

bool unwrapMaterial(ImpModel& model, int m, std::string& error, nectar::Image* paint)
{
	std::vector<std::size_t> tris;
	for (std::size_t t = 0; t < model.triMaterial.size() && t * 3 + 2 < model.verts.size(); ++t)
		if (model.triMaterial[t] == m) tris.push_back(t);
	if (tris.empty() || m < 0 || std::size_t(m) >= model.materials.size()) {
		error = L("esa parte no tiene triángulos", "that part has no triangles");
		return false;
	}
	// Vértices soldados por posición: xatlas necesita saber qué caras se tocan.
	std::vector<float> pos;
	std::vector<std::uint32_t> index;
	std::map<std::array<float, 3>, std::uint32_t> weld;
	for (std::size_t t : tris)
		for (int k = 0; k < 3; ++k) {
			const Vec3& p = model.verts[t * 3 + std::size_t(k)].p;
			const auto it = weld.emplace(std::array<float, 3> { p.x, p.y, p.z }, std::uint32_t(weld.size())).first;
			if (it->second == pos.size() / 3) pos.insert(pos.end(), { p.x, p.y, p.z });
			index.push_back(it->second);
		}
	xatlas::Atlas* atlas = xatlas::Create();
	xatlas::MeshDecl decl;
	decl.vertexPositionData   = pos.data();
	decl.vertexCount          = std::uint32_t(pos.size() / 3);
	decl.vertexPositionStride = sizeof(float) * 3;
	decl.indexData            = index.data();
	decl.indexCount           = std::uint32_t(index.size());
	decl.indexFormat          = xatlas::IndexFormat::UInt32;
	if (xatlas::AddMesh(atlas, decl) != xatlas::AddMeshError::Success) {
		xatlas::Destroy(atlas);
		error = L("xatlas no acepta esta malla", "xatlas rejected this mesh");
		return false;
	}
	xatlas::PackOptions pack;
	pack.padding  = 4; // unos 2 texels en una capa de 512
	pack.bilinear = true;
	xatlas::Generate(atlas, xatlas::ChartOptions(), pack);
	if (atlas->meshCount != 1 || !atlas->width || !atlas->height || atlas->meshes[0].indexCount != index.size()) {
		xatlas::Destroy(atlas);
		error = L("xatlas no pudo desplegar esta parte", "xatlas could not unwrap this part");
		return false;
	}
	const xatlas::Mesh& out = atlas->meshes[0];
	std::vector<Vec2> fresh(index.size());
	for (std::size_t i = 0; i < index.size(); ++i) {
		const xatlas::Vertex& v = out.vertexArray[out.indexArray[i]];
		// Redondeadas a 16 bits: lo mismo que se guarda en el proyecto.
		const float u = std::round(std::clamp(v.uv[0] / float(atlas->width), 0.0f, 1.0f) * 65535.0f) / 65535.0f;
		const float w = std::round(std::clamp(v.uv[1] / float(atlas->height), 0.0f, 1.0f) * 65535.0f) / 65535.0f;
		fresh[i]      = { u, w };
	}
	xatlas::Destroy(atlas);

	// Pasa una imagen a la distribución nueva: cada texel nuevo toma el color
	// del punto del triángulo que le corresponde con las UV de antes.
	auto bake = [&](int w, int h, nectar::Image& out, bool fillGaps, auto&& colourAt) {
		out.resize(w, h);
		std::vector<char> filled(std::size_t(w) * std::size_t(h), 0);
		std::vector<float> dist(std::size_t(w) * std::size_t(h), 1e9f);
		for (std::size_t i = 0; i < tris.size(); ++i) {
			const SkinVertex* v = &model.verts[tris[i] * 3];
			P2 q[3];
			for (int k = 0; k < 3; ++k) q[k] = { fresh[i * 3 + std::size_t(k)].x * float(w), fresh[i * 3 + std::size_t(k)].y * float(h) };
			rasterTriangle(q, 1.0f, 0, 0, w, h, [&](int x, int y, float b0, float b1, float b2) {
				// Dentro del triángulo gana a un margen de otro.
				const float inside = (b0 > 0 && b1 > 0 && b2 > 0) ? 0.0f : 1.0f;
				float& d           = dist[std::size_t(y) * std::size_t(w) + std::size_t(x)];
				if (inside > d) return;
				d = inside;
				float c[4];
				colourAt(v, b0, b1, b2, c);
				for (int k = 0; k < 4; ++k) out.at(x, y)[k] = toByte(c[k]);
				filled[std::size_t(y) * std::size_t(w) + std::size_t(x)] = 1;
			});
		}
		if (fillGaps) dilate(out, filled, 8);
	};
	auto oldUv = [](const SkinVertex* v, float b0, float b1, float b2) {
		return Vec2 { b0 * v[0].uv.x + b1 * v[1].uv.x + b2 * v[2].uv.x, b0 * v[0].uv.y + b1 * v[1].uv.y + b2 * v[2].uv.y };
	};

	// La textura (o los colores de vértice) pasan a la distribución nueva.
	ImpMaterial& mat = model.materials[std::size_t(m)];
	if (!mat.texture.empty() || mat.vertexColors) {
		const int side = mat.texture.empty() ? kPaintSolidSide : pow2Side(std::max(mat.texture.width, mat.texture.height));
		nectar::Image baked;
		bake(side, side, baked, true, [&](const SkinVertex* v, float b0, float b1, float b2, float c[4]) {
			if (mat.texture.empty()) {
				for (int k = 0; k < 4; ++k) c[k] = b0 * v[0].col[k] + b1 * v[1].col[k] + b2 * v[2].col[k];
				return;
			}
			const Vec2 uv = oldUv(v, b0, b1, b2);
			sampleBilinear(mat.texture, uv.x * float(mat.texture.width), uv.y * float(mat.texture.height), true, false, c);
		});
		mat.texture = std::move(baked);
	}
	// Lo ya pintado (un relleno) también, al tamaño de la capa nueva.
	if (paint && !paintEmpty(*paint)) {
		int w = 0, h = 0;
		paintLayerSize(mat, w, h);
		const nectar::Image old = *paint;
		bake(w, h, *paint, false, [&](const SkinVertex* v, float b0, float b1, float b2, float c[4]) {
			const Vec2 uv = oldUv(v, b0, b1, b2);
			sampleBilinear(old, uv.x * float(old.width), uv.y * float(old.height), true, true, c);
			if (c[3] > 0)
				for (int k = 0; k < 3; ++k) c[k] /= c[3];
		});
	} else if (paint) {
		*paint = nectar::Image();
	}
	for (std::size_t i = 0; i < tris.size(); ++i)
		for (int k = 0; k < 3; ++k) model.verts[tris[i] * 3 + std::size_t(k)].uv = fresh[i * 3 + std::size_t(k)];
	return true;
}

std::vector<std::uint8_t> packUvs(const ImpModel& model, int m)
{
	std::vector<std::uint8_t> out = { 'M', 'M', 'U', 'V' };
	std::vector<std::uint16_t> q;
	for (std::size_t t = 0; t < model.triMaterial.size() && t * 3 + 2 < model.verts.size(); ++t) {
		if (model.triMaterial[t] != m) continue;
		for (int k = 0; k < 3; ++k) {
			const Vec2& uv = model.verts[t * 3 + std::size_t(k)].uv;
			q.push_back(std::uint16_t(std::round(std::clamp(uv.x, 0.0f, 1.0f) * 65535.0f)));
			q.push_back(std::uint16_t(std::round(std::clamp(uv.y, 0.0f, 1.0f) * 65535.0f)));
		}
	}
	const std::uint32_t corners = std::uint32_t(q.size() / 2);
	for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(corners >> (8 * i)));
	for (std::uint16_t v : q) {
		out.push_back(std::uint8_t(v));
		out.push_back(std::uint8_t(v >> 8));
	}
	return out;
}

bool unpackUvs(ImpModel& model, int m, const std::vector<std::uint8_t>& data)
{
	if (data.size() < 8 || std::memcmp(data.data(), "MMUV", 4) != 0) return false;
	std::uint32_t corners = 0;
	for (int i = 0; i < 4; ++i) corners |= std::uint32_t(data[std::size_t(4 + i)]) << (8 * i);
	std::vector<std::size_t> tris;
	for (std::size_t t = 0; t < model.triMaterial.size() && t * 3 + 2 < model.verts.size(); ++t)
		if (model.triMaterial[t] == m) tris.push_back(t);
	// Otro modelo (o el original cambió): no se aplican.
	if (corners != tris.size() * 3 || data.size() != 8 + std::size_t(corners) * 4) return false;
	auto get = [&](std::size_t i) { return float(data[8 + i * 2] | (data[8 + i * 2 + 1] << 8)) / 65535.0f; };
	for (std::size_t i = 0; i < tris.size(); ++i)
		for (int k = 0; k < 3; ++k) {
			const std::size_t c                              = i * 3 + std::size_t(k);
			model.verts[tris[i] * 3 + std::size_t(k)].uv = { get(c * 2), get(c * 2 + 1) };
		}
	return true;
}

bool encodePng(const nectar::Image& img, std::vector<std::uint8_t>& out)
{
	static std::atomic<int> counter { 0 };
	std::error_code ec;
	const fs::path tmp = fs::temp_directory_path(ec) / ("nectar_modelmaker_paint_" + std::to_string(counter++) + ".png");
	if (!nectar::writePng(tmp.u8string(), img)) return false;
	std::ifstream f(tmp, std::ios::binary);
	out.assign(std::istreambuf_iterator<char>(f), {});
	f.close();
	fs::remove(tmp, ec);
	return !out.empty();
}

bool decodePng(const std::vector<std::uint8_t>& data, nectar::Image& out)
{
	int w = 0, h = 0, n = 0;
	unsigned char* px = data.empty() ? nullptr : stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &n, 4);
	if (!px) return false;
	out.resize(w, h);
	std::copy(px, px + std::size_t(w) * std::size_t(h) * 4, out.rgba.begin());
	stbi_image_free(px);
	return true;
}

void PaintPatch::swap(nectar::Image& layer)
{
	if (layer.empty() || x < 0 || y < 0 || x + w > layer.width || y + h > layer.height || rgba.size() != std::size_t(w) * std::size_t(h) * 4) return;
	for (int row = 0; row < h; ++row)
		std::swap_ranges(rgba.begin() + std::ptrdiff_t(std::size_t(row) * std::size_t(w) * 4),
		                 rgba.begin() + std::ptrdiff_t(std::size_t(row + 1) * std::size_t(w) * 4), layer.at(x, y + row));
}

bool paintRaycast(const std::vector<SkinVertex>& verts, const std::vector<int>& triMaterial, const std::vector<char>& visible, const Vec3& origin,
                  const Vec3& dir, PaintHit& hit)
{
	float best              = 1e30f;
	bool found              = false;
	const std::size_t count = std::min(verts.size() / 3, triMaterial.size());
	for (std::size_t t = 0; t < count; ++t) {
		const int m = triMaterial[t];
		if (m < 0 || std::size_t(m) >= visible.size() || !visible[std::size_t(m)]) continue;
		const SkinVertex* v = &verts[t * 3];
		const Vec3 e1 = v[1].p - v[0].p, e2 = v[2].p - v[0].p;
		const Vec3 pv   = cross(dir, e2);
		const float det = dot(e1, pv);
		if (std::fabs(det) < 1e-12f) continue;
		const float inv = 1.0f / det;
		const Vec3 tv   = origin - v[0].p;
		const float u   = dot(tv, pv) * inv;
		if (u < 0 || u > 1) continue;
		const Vec3 qv = cross(tv, e1);
		const float w = dot(dir, qv) * inv;
		if (w < 0 || u + w > 1) continue;
		const float d = dot(e2, qv) * inv;
		if (d <= 1e-5f || d >= best) continue;
		best    = d;
		hit.tri = t;
		found   = true;
	}
	if (!found) return false;
	hit.point  = origin + dir * best;
	hit.normal = triangleNormal(&verts[hit.tri * 3]);
	return true;
}

void PaintStroke::begin(const Brush& brush, const std::vector<SkinVertex>& verts, const std::vector<int>& triMaterial, const std::vector<PaintTarget>& targets)
{
	brush_       = brush;
	verts_       = &verts;
	triMaterial_ = &triMaterial;
	targets_     = targets;
	layers_.clear();
	const std::size_t count = std::min(verts.size() / 3, triMaterial.size());
	triMin_.resize(count);
	triMax_.resize(count);
	triNormal_.resize(count);
	for (std::size_t t = 0; t < count; ++t) {
		const SkinVertex* v = &verts[t * 3];
		triMin_[t]          = vmin(v[0].p, vmin(v[1].p, v[2].p));
		triMax_[t]          = vmax(v[0].p, vmax(v[1].p, v[2].p));
		triNormal_[t]       = triangleNormal(v);
	}
	active_ = true;
}

namespace {
void grow(PaintStroke::Rect& r, int x0, int y0, int x1, int y1)
{
	r.x0 = std::min(r.x0, x0), r.y0 = std::min(r.y0, y0);
	r.x1 = std::max(r.x1, x1), r.y1 = std::max(r.y1, y1);
}
PaintStroke::Rect emptyRect(int material) { return { material, INT_MAX, INT_MAX, INT_MIN, INT_MIN }; }
bool isEmpty(const PaintStroke::Rect& r) { return r.x1 <= r.x0 || r.y1 <= r.y0; }
} // namespace

PaintStroke::Layer& PaintStroke::layerFor(std::vector<nectar::Image>& layers, int m)
{
	for (Layer& l : layers_)
		if (l.material == m) return l;
	if (layers.size() <= std::size_t(m)) layers.resize(std::size_t(m) + 1);
	nectar::Image& img = layers[std::size_t(m)];
	// Primera pintura de este material: la capa nace transparente.
	if (img.empty()) img.resize(std::max(1, targets_[std::size_t(m)].width), std::max(1, targets_[std::size_t(m)].height));
	Layer l;
	l.material = m;
	l.before   = img;
	l.mask.assign(std::size_t(img.width) * std::size_t(img.height), 0.0f);
	l.all = l.recent = l.stamp = emptyRect(m);
	layers_.push_back(std::move(l));
	return layers_.back();
}

void PaintStroke::apply(const Layer& l, nectar::Image& img, const Rect& r) const
{
	for (int y = r.y0; y < r.y1; ++y) {
		for (int x = r.x0; x < r.x1; ++x) {
			const float a         = std::min(1.0f, brush_.opacity * l.mask[std::size_t(y) * std::size_t(img.width) + std::size_t(x)]);
			const std::uint8_t* b = l.before.at(x, y);
			std::uint8_t* o       = img.at(x, y);
			if (a <= 0) {
				std::memcpy(o, b, 4);
				continue;
			}
			const float ab = b[3] / 255.0f;
			if (brush_.erase) {
				std::memcpy(o, b, 3);
				o[3] = toByte(ab * (1 - a));
				continue;
			}
			const float ao = a + ab * (1 - a);
			for (int k = 0; k < 3; ++k) o[k] = toByte((brush_.colour[k] * a + b[k] / 255.0f * ab * (1 - a)) / ao);
			o[3] = toByte(ao);
		}
	}
}

void PaintStroke::stamp(std::vector<nectar::Image>& layers, const Vec3& centre, const Vec3& normal)
{
	const float R = brush_.radius;
	if (!active_ || R <= 0) return;
	for (Layer& l : layers_) l.stamp = emptyRect(l.material);
	const std::vector<SkinVertex>& V = *verts_;
	const float hardness             = std::clamp(brush_.hardness, 0.0f, 0.99f);
	for (std::size_t t = 0; t < triMin_.size(); ++t) {
		const int m = (*triMaterial_)[t];
		if (m < 0 || std::size_t(m) >= targets_.size() || !targets_[std::size_t(m)].paintable) continue;
		if (centre.x + R < triMin_[t].x || centre.x - R > triMax_[t].x || centre.y + R < triMin_[t].y || centre.y - R > triMax_[t].y
		    || centre.z + R < triMin_[t].z || centre.z - R > triMax_[t].z)
			continue;
		if (dot(triNormal_[t], normal) < -0.5f) continue; // la otra cara de algo fino
		const SkinVertex* v = &V[t * 3];
		const Vec3 e1 = v[1].p - v[0].p, e2 = v[2].p - v[0].p;
		const float g11 = dot(e1, e1), g12 = dot(e1, e2), g22 = dot(e2, e2), det = g11 * g22 - g12 * g12;
		const Vec3 n = cross(e1, e2);
		if (det <= 1e-20f || dot(n, n) <= 1e-20f) continue;
		const Vec3 dc     = centre - v[0].p;
		const float above = dot(dc, normalize(n));
		if (std::fabs(above) >= R) continue;
		const float rIn = std::sqrt(R * R - above * above); // radio del pincel en el plano del triángulo

		Layer& l           = layerFor(layers, m);
		nectar::Image& img = layers[std::size_t(m)];
		const int W = img.width, H = img.height;
		// Al mosaico [0,W)x[0,H) donde cae el centro del triángulo.
		const float su = std::floor((v[0].uv.x + v[1].uv.x + v[2].uv.x) / 3.0f), sv = std::floor((v[0].uv.y + v[1].uv.y + v[2].uv.y) / 3.0f);
		P2 q[3];
		for (int k = 0; k < 3; ++k) q[k] = { (v[k].uv.x - su) * float(W), (v[k].uv.y - sv) * float(H) };
		const float d1x = q[1].x - q[0].x, d1y = q[1].y - q[0].y, d2x = q[2].x - q[0].x, d2y = q[2].y - q[0].y;
		// Del plano a las UV: el centro del pincel y cuánto mide en cada eje.
		const float ia = (g22 * dot(e1, dc) - g12 * dot(e2, dc)) / det, ib = (g11 * dot(e2, dc) - g12 * dot(e1, dc)) / det;
		const P2 c { q[0].x + d1x * ia + d2x * ib, q[0].y + d1y * ia + d2y * ib };
		const Vec3 gu = e1 * ((g22 * d1x - g12 * d2x) / det) + e2 * ((g11 * d2x - g12 * d1x) / det);
		const Vec3 gv = e1 * ((g22 * d1y - g12 * d2y) / det) + e2 * ((g11 * d2y - g12 * d1y) / det);
		const float eu = rIn * length(gu) + 1.0f, ev = rIn * length(gv) + 1.0f;
		const int cx0 = int(std::floor(c.x - eu)), cx1 = int(std::ceil(c.x + eu)) + 1;
		const int cy0 = int(std::floor(c.y - ev)), cy1 = int(std::ceil(c.y + ev)) + 1;
		rasterTriangle(q, 0.75f, std::max(cx0, -W), std::max(cy0, -H), std::min(cx1, 2 * W), std::min(cy1, 2 * H), [&](int x, int y, float b0, float b1, float b2) {
			const Vec3 p   = v[0].p * b0 + v[1].p * b1 + v[2].p * b2;
			const Vec3 dd  = p - centre;
			const float d2 = dot(dd, dd);
			if (d2 >= R * R) return;
			const float r = std::sqrt(d2) / R;
			float w       = 1;
			if (r > hardness) {
				const float s = (r - hardness) / (1 - hardness);
				w             = 1 - s * s * (3 - 2 * s);
			}
			const int xi = ((x % W) + W) % W, yi = ((y % H) + H) % H;
			float& mk    = l.mask[std::size_t(yi) * std::size_t(W) + std::size_t(xi)];
			if (w <= mk) return;
			mk = w;
			grow(l.stamp, xi, yi, xi + 1, yi + 1);
		});
	}
	for (Layer& l : layers_) {
		if (isEmpty(l.stamp)) continue;
		apply(l, layers[std::size_t(l.material)], l.stamp);
		grow(l.all, l.stamp.x0, l.stamp.y0, l.stamp.x1, l.stamp.y1);
		grow(l.recent, l.stamp.x0, l.stamp.y0, l.stamp.x1, l.stamp.y1);
	}
}

void PaintStroke::fill(std::vector<nectar::Image>& layers, int material)
{
	if (!active_ || material < 0 || std::size_t(material) >= targets_.size() || !targets_[std::size_t(material)].paintable) return;
	Layer& l           = layerFor(layers, material);
	nectar::Image& img = layers[std::size_t(material)];
	std::fill(l.mask.begin(), l.mask.end(), 1.0f);
	const Rect r { material, 0, 0, img.width, img.height };
	apply(l, img, r);
	grow(l.all, r.x0, r.y0, r.x1, r.y1);
	grow(l.recent, r.x0, r.y0, r.x1, r.y1);
}

std::vector<PaintStroke::Rect> PaintStroke::takeDirty()
{
	std::vector<Rect> out;
	for (Layer& l : layers_) {
		if (!isEmpty(l.recent)) out.push_back(l.recent);
		l.recent = emptyRect(l.material);
	}
	return out;
}

std::vector<PaintPatch> PaintStroke::end(const std::vector<nectar::Image>& layers)
{
	std::vector<PaintPatch> out;
	for (const Layer& l : layers_) {
		if (isEmpty(l.all)) continue;
		const nectar::Image& now = layers[std::size_t(l.material)];
		PaintPatch p;
		p.material = l.material;
		p.x = l.all.x0, p.y = l.all.y0, p.w = l.all.x1 - l.all.x0, p.h = l.all.y1 - l.all.y0;
		bool changed = false;
		for (int row = 0; row < p.h; ++row) {
			const std::uint8_t* b = l.before.at(p.x, p.y + row);
			p.rgba.insert(p.rgba.end(), b, b + std::size_t(p.w) * 4);
			changed = changed || std::memcmp(b, now.at(p.x, p.y + row), std::size_t(p.w) * 4) != 0;
		}
		if (changed) out.push_back(std::move(p));
	}
	layers_.clear();
	active_ = false;
	return out;
}

} // namespace mm
