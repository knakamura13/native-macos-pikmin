#include "parts.h"

#include "lang.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

namespace mm {

namespace fs = std::filesystem;

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr std::size_t kMaxPartTriangles = 20000; ///< un accesorio, no otro personaje

const char* const kPointIds[P_COUNT] = { "head_top", "face", "chest", "back", "left_hand", "right_hand" };

// Tamaño de una pieza del usuario (llevada a 1) en unidades del punto.
float userScale(int point)
{
	switch (point) {
	case P_HEADTOP: return 0.8f;
	case P_FACE: return 0.6f;
	case P_CHEST: return 0.45f;
	case P_BACK: return 0.8f;
	default: return 0.9f;
	}
}

// ── Formas de serie ─────────────────────────────────────────────────────

struct Shape {
	ImpModel& m;
	int mat = 0;

	int material(const char* es, const char* en, float r, float g, float b, bool doubleSided = false)
	{
		ImpMaterial im;
		im.name        = L(es, en);
		im.color[0]    = r, im.color[1] = g, im.color[2] = b;
		im.doubleSided = doubleSided;
		mat            = int(m.materials.size());
		m.materials.push_back(im);
		return mat;
	}
	// De frente hacia fuera (antihorario, como glTF): la normal dice cuál es fuera.
	void tri(Vec3 a, Vec3 b, Vec3 c, Vec3 na, Vec3 nb, Vec3 nc)
	{
		const Vec3 f = cross(b - a, c - a);
		if (length(f) < 1e-9f) return;
		if (dot(f, na + nb + nc) < 0) {
			std::swap(b, c);
			std::swap(nb, nc);
		}
		const Vec3 p[3] = { a, b, c }, n[3] = { na, nb, nc };
		for (int k = 0; k < 3; ++k) {
			SkinVertex v;
			v.p  = p[k];
			v.n  = normalize(n[k]);
			v.uv = { 0.5f, 0.5f };
			m.verts.push_back(v);
		}
		m.triMaterial.push_back(mat);
	}
	void flat(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& out)
	{
		Vec3 n = normalize(cross(b - a, c - a));
		if (dot(n, out) < 0) n = n * -1.0f;
		tri(a, b, c, n, n, n);
	}
	void quad(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, const Vec3& out)
	{
		flat(a, b, c, out);
		flat(a, c, d, out);
	}
	void box(const Vec3& mn, const Vec3& mx)
	{
		const Vec3 c = (mn + mx) * 0.5f;
		auto P       = [&](int i) { return Vec3 { i & 1 ? mx.x : mn.x, i & 2 ? mx.y : mn.y, i & 4 ? mx.z : mn.z }; };
		const int faces[6][4] = { { 0, 2, 6, 4 }, { 1, 3, 7, 5 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 1, 3, 2 }, { 4, 5, 7, 6 } };
		for (const auto& f : faces) {
			const Vec3 mid = (P(f[0]) + P(f[2])) * 0.5f;
			quad(P(f[0]), P(f[1]), P(f[2]), P(f[3]), mid - c);
		}
	}
	// Superficie de revolución: perfil (radio, altura, normal en el perfil)
	// girado alrededor de `axis` (con `u`, `v` perpendiculares) desde `centre`.
	struct Prof {
		float r, y, nr, ny;
	};
	void lathe(const std::vector<Prof>& prof, int segments, const Vec3& centre, const Vec3& u, const Vec3& v, const Vec3& axis)
	{
		auto at = [&](const Prof& p, int s, Vec3& pos, Vec3& n) {
			const float a = 2 * kPi * float(s) / float(segments);
			const Vec3 d  = u * std::cos(a) + v * std::sin(a);
			pos           = centre + d * p.r + axis * p.y;
			n             = d * p.nr + axis * p.ny;
		};
		for (std::size_t i = 0; i + 1 < prof.size(); ++i) {
			for (int s = 0; s < segments; ++s) {
				Vec3 a, b, c, d, na, nb, nc, nd;
				at(prof[i], s, a, na);
				at(prof[i], s + 1, b, nb);
				at(prof[i + 1], s + 1, c, nc);
				at(prof[i + 1], s, d, nd);
				tri(a, b, c, na, nb, nc);
				tri(a, c, d, na, nc, nd);
			}
		}
	}
	void lathe(const std::vector<Prof>& prof, int segments, const Vec3& centre)
	{
		lathe(prof, segments, centre, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 });
	}
	void sphere(const Vec3& c, float r, int segments, int rings)
	{
		std::vector<Prof> p;
		for (int i = 0; i <= rings; ++i) {
			const float t = kPi * float(i) / float(rings);
			p.push_back({ r * std::sin(t), -r * std::cos(t), std::sin(t), -std::cos(t) });
		}
		lathe(p, segments, c);
	}
	void cylinder(const Vec3& base, float r, float h, int segments)
	{
		lathe({ { 0, 0, 0, -1 }, { r, 0, 0, -1 }, { r, 0, 1, 0 }, { r, h, 1, 0 }, { r, h, 0, 1 }, { 0, h, 0, 1 } }, segments, base);
	}
	// Prisma con la cara `poly` (en XY) de grosor `depth` en Z.
	void prism(const std::vector<Vec2>& poly, float z0, float z1)
	{
		Vec3 c {};
		for (const Vec2& p : poly) c += Vec3 { p.x, p.y, (z0 + z1) * 0.5f };
		c = c * (1.0f / float(poly.size()));
		for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
			flat({ poly[0].x, poly[0].y, z1 }, { poly[i].x, poly[i].y, z1 }, { poly[i + 1].x, poly[i + 1].y, z1 }, { 0, 0, 1 });
			flat({ poly[0].x, poly[0].y, z0 }, { poly[i].x, poly[i].y, z0 }, { poly[i + 1].x, poly[i + 1].y, z0 }, { 0, 0, -1 });
		}
		for (std::size_t i = 0; i < poly.size(); ++i) {
			const Vec2 &a = poly[i], &b = poly[(i + 1) % poly.size()];
			const Vec3 mid { (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, c.z };
			quad({ a.x, a.y, z0 }, { b.x, b.y, z0 }, { b.x, b.y, z1 }, { a.x, a.y, z1 }, mid - c);
		}
	}
};

// Medidas en unidades del punto (1 = ancho de la cabeza en la cabeza,
// ancho de hombros en el pecho y la espalda).
void partyHat(ImpModel& m)
{
	Shape s { m };
	s.material("gorro", "hat", 0.93f, 0.33f, 0.55f);
	const float r = 0.3f, h = 0.78f, l = std::sqrt(r * r + h * h);
	s.lathe({ { 0, 0, 0, -1 }, { r, 0, 0, -1 }, { r, 0, h / l, r / l }, { 0, h, h / l, r / l } }, 18, { 0, 0, 0 });
	// Una franja de otro color hace que se lea como gorro de fiesta.
	s.material("franja", "stripe", 1.0f, 0.95f, 0.85f);
	const float y0 = 0.22f, y1 = 0.33f, r0 = r * (1 - y0 / h) + 0.006f, r1 = r * (1 - y1 / h) + 0.006f;
	s.lathe({ { r0, y0, h / l, r / l }, { r1, y1, h / l, r / l } }, 18, { 0, 0, 0 });
	s.material("pompón", "pompom", 1.0f, 0.84f, 0.25f);
	s.sphere({ 0, h, 0 }, 0.085f, 10, 6);
}

void antenna(ImpModel& m)
{
	Shape s { m };
	s.material("varilla", "stalk", 0.62f, 0.64f, 0.68f);
	s.cylinder({ 0, 0, 0 }, 0.03f, 0.5f, 8);
	s.material("bola", "bulb", 1.0f, 0.42f, 0.22f);
	s.sphere({ 0, 0.6f, 0 }, 0.12f, 12, 8);
}

void domeHelmet(ImpModel& m)
{
	Shape s { m };
	s.material("casco", "shell", 0.27f, 0.56f, 0.92f, true);
	const float r = 0.57f;
	std::vector<Shape::Prof> dome;
	for (int i = 0; i <= 6; ++i) {
		const float t = 0.5f * kPi * float(i) / 6.0f; // del borde a la cima
		dome.push_back({ r * std::cos(t), r * std::sin(t), std::cos(t), std::sin(t) });
	}
	s.lathe(dome, 16, { 0, 0, 0 });
	s.material("borde", "rim", 0.98f, 0.8f, 0.28f);
	const float r2 = 0.64f, t = 0.05f;
	s.lathe({ { r, 0, 0, 1 }, { r2, 0, 0, 1 }, { r2, 0, 1, 0 }, { r2, -t, 1, 0 }, { r2, -t, 0, -1 }, { r, -t, 0, -1 } }, 16, { 0, 0, 0 });
	s.sphere({ 0, r, 0 }, 0.07f, 8, 5);
}

void roundGlasses(ImpModel& m)
{
	Shape s { m };
	s.material("montura", "frame", 0.13f, 0.13f, 0.16f);
	const float R = 0.15f, rr = 0.028f, gap = 0.2f;
	std::vector<Shape::Prof> ring;
	for (int i = 0; i <= 5; ++i) {
		const float a = 2 * kPi * float(i) / 5.0f;
		ring.push_back({ R + rr * std::cos(a), rr * std::sin(a), std::cos(a), std::sin(a) });
	}
	for (float x : { -gap, gap }) s.lathe(ring, 14, { x, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
	// Puente.
	s.box({ -gap + R, -0.015f, -0.015f }, { gap - R, 0.02f, 0.015f });
	// Cristales: discos claros un poco hacia dentro.
	s.material("cristal", "lens", 0.78f, 0.9f, 0.97f);
	m.materials.back().alpha    = ImpMaterial::Blend;
	m.materials.back().color[3] = 0.35f;
	for (float x : { -gap, gap }) s.lathe({ { 0, 0, 0, 1 }, { R, 0, 0, 1 } }, 16, { x, 0, -0.005f }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 });
}

void bowTie(ImpModel& m)
{
	Shape s { m };
	s.material("pajarita", "bow tie", 0.84f, 0.14f, 0.22f);
	const float w = 0.3f, h = 0.17f, k = 0.05f;
	s.prism({ { 0, -k }, { w, -h }, { w + 0.03f, 0 }, { w, h }, { 0, k } }, 0.0f, 0.07f);
	s.prism({ { 0, k }, { -w, h }, { -w - 0.03f, 0 }, { -w, -h }, { 0, -k } }, 0.0f, 0.07f);
	s.box({ -0.055f, -0.07f, 0.0f }, { 0.055f, 0.07f, 0.095f });
}

void backpack(ImpModel& m)
{
	Shape s { m };
	s.material("mochila", "bag", 0.33f, 0.62f, 0.38f);
	s.box({ -0.32f, 0.0f, -0.3f }, { 0.32f, 0.62f, 0.0f });
	// Tapa y bolsillo de otro color.
	s.material("bolsillo", "pocket", 0.97f, 0.76f, 0.33f);
	s.box({ -0.34f, 0.5f, -0.32f }, { 0.34f, 0.7f, 0.0f });
	s.box({ -0.22f, 0.1f, -0.37f }, { 0.22f, 0.36f, -0.3f });
}

const std::vector<BuiltinPart> kBuiltins = {
	{ "party_hat", "Gorro de fiesta", "Party hat", P_HEADTOP, -0.04f },
	{ "antenna", "Antena", "Antenna", P_HEADTOP, -0.02f },
	{ "dome_helmet", "Casco", "Dome helmet", P_HEADTOP, -0.42f },
	{ "round_glasses", "Gafas redondas", "Round glasses", P_FACE, 0.08f },
	{ "bow_tie", "Pajarita", "Bow tie", P_CHEST, 0.0f },
	{ "backpack", "Mochila", "Backpack", P_BACK, -0.25f },
};

bool generateBuiltin(const std::string& id, ImpModel& m)
{
	m = ImpModel();
	if (id == "party_hat") partyHat(m);
	else if (id == "antenna") antenna(m);
	else if (id == "dome_helmet") domeHelmet(m);
	else if (id == "round_glasses") roundGlasses(m);
	else if (id == "bow_tie") bowTie(m);
	else if (id == "backpack") backpack(m);
	else return false;
	m.format = "builtin";
	return true;
}

bool supportedPartFile(const fs::path& p)
{
	std::string ext = p.extension().u8string();
	for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
	return ext == ".glb" || ext == ".gltf" || ext == ".vrm" || ext == ".fbx" || ext == ".obj";
}

} // namespace

const char* attachPointName(int point)
{
	switch (point) {
	case P_HEADTOP: return L("Coronilla", "Head top");
	case P_FACE: return L("Cara", "Face front");
	case P_CHEST: return L("Pecho", "Chest");
	case P_BACK: return L("Espalda", "Back");
	case P_LHAND: return L("Mano izquierda", "Left hand");
	case P_RHAND: return L("Mano derecha", "Right hand");
	default: return "?";
	}
}

const char* attachPointId(int point) { return point >= 0 && point < P_COUNT ? kPointIds[point] : kPointIds[0]; }

int attachPointFromId(const std::string& id)
{
	for (int i = 0; i < P_COUNT; ++i)
		if (id == kPointIds[i]) return i;
	return P_HEADTOP;
}

int attachPointJoint(int point)
{
	switch (point) {
	case P_CHEST:
	case P_BACK: return J_SPINE;
	case P_LHAND: return J_LARM; // la izquierda del personaje está en +X
	case P_RHAND: return J_RARM;
	default: return J_HEAD;
	}
}

const std::vector<BuiltinPart>& builtinParts() { return kBuiltins; }

bool isBuiltinPart(const std::string& part) { return part.rfind("builtin:", 0) == 0; }

std::string partLabel(const std::string& part)
{
	if (isBuiltinPart(part)) {
		const std::string id = part.substr(8);
		for (const BuiltinPart& b : kBuiltins)
			if (id == b.id) return L(b.es, b.en);
		return id;
	}
	return fs::u8path(part).stem().u8string();
}

const PartMesh* PartLibrary::get(const std::string& part, std::string& error)
{
	if (isBuiltinPart(part)) {
		// Se generan cada vez (son unos cientos de triángulos) para que los
		// nombres sigan el idioma.
		builtin_ = PartMesh();
		if (!generateBuiltin(part.substr(8), builtin_.mesh)) {
			error = std::string(L("pieza desconocida: ", "unknown part: ")) + part;
			return nullptr;
		}
		builtin_.name = partLabel(part);
		return &builtin_;
	}
	auto it = cache_.find(part);
	if (it == cache_.end()) {
		PartMesh pm;
		pm.normalize = true;
		pm.name      = partLabel(part);
		std::string why;
		if (!importModel(part, pm.mesh, why)) {
			pm.error = why.empty() ? L("no se pudo abrir", "could not open") : why;
		} else if (pm.mesh.triangles() > kMaxPartTriangles) {
			pm.error = std::to_string(pm.mesh.triangles()) + L(" triángulos: demasiados para un accesorio (máximo 20.000)",
			                                                   " triangles: too many for an accessory (20,000 at most)");
		} else if (pm.mesh.verts.empty()) {
			pm.error = L("no tiene triángulos", "it has no triangles");
		} else {
			// Rígida: sin huesos. Los VRM 0.x miran hacia -Z: media vuelta.
			for (SkinVertex& v : pm.mesh.verts) {
				if (pm.mesh.facesMinusZ) {
					v.p = { -v.p.x, v.p.y, -v.p.z };
					v.n = { -v.n.x, v.n.y, -v.n.z };
				}
				for (int k = 0; k < 4; ++k) v.joint[k] = 0, v.weight[k] = k == 0 ? 1.0f : 0.0f;
			}
			if (pm.mesh.materials.empty()) pm.mesh.materials.push_back(ImpMaterial());
			for (int& m : pm.mesh.triMaterial) m = std::clamp(m, 0, int(pm.mesh.materials.size()) - 1);
			pm.mesh.triMaterial.resize(pm.mesh.triangles(), 0);
		}
		it = cache_.emplace(part, std::move(pm)).first;
	}
	if (!it->second.ok()) {
		error = fs::u8path(part).filename().u8string() + ": " + it->second.error;
		return nullptr;
	}
	return &it->second;
}

const PartMesh* PartLibrary::find(const std::string& part) const
{
	const auto it = cache_.find(part);
	return it == cache_.end() || !it->second.ok() ? nullptr : &it->second;
}

const std::vector<std::string>& PartLibrary::userFiles(const std::string& dir, double now)
{
	if (dir == filesDir_ && now - scanned_ < 2.0) return files_;
	filesDir_ = dir;
	scanned_  = now;
	files_.clear();
	std::error_code ec;
	// En la carpeta y un nivel por debajo (una pieza con sus texturas en su carpeta).
	for (const fs::directory_entry& e : fs::directory_iterator(fs::u8path(dir), ec)) {
		if (e.is_regular_file(ec) && supportedPartFile(e.path())) {
			files_.push_back(e.path().u8string());
		} else if (e.is_directory(ec)) {
			std::error_code ec2;
			for (const fs::directory_entry& f : fs::directory_iterator(e.path(), ec2))
				if (f.is_regular_file(ec2) && supportedPartFile(f.path())) files_.push_back(f.path().u8string());
		}
	}
	std::sort(files_.begin(), files_.end());
	return files_;
}

std::array<AttachSite, P_COUNT> attachSites(const RigResult& rig, const FitSettings& fit)
{
	const std::array<Vec3, M_COUNT>& mr = fit.markers; // sin enderezar
	const std::array<Vec3, M_COUNT>& mk = rig.markers; // enderezados (como el pack)
	// La cabeza y el cuerpo: los vértices que van sobre todo con cada articulación.
	auto dominant = [](const SkinVertex& v, int joint) { return v.joint[0] == joint && v.weight[0] >= 0.5f; };
	Vec3 hmn { 1e9f, 1e9f, 1e9f }, hmx = hmn * -1.0f;
	int heads = 0;
	for (const SkinVertex& v : rig.verts)
		if (dominant(v, J_HEAD)) hmn = vmin(hmn, v.p), hmx = vmax(hmx, v.p), ++heads;
	// Lo alto de la cabeza: el marcador estimado es lo más alto de la malla
	// (puede ser una antena o un tallo), así que no pasa de la cabeza.
	const float headTop = heads > 2 ? std::min(mk[M_HEADTOP].y, hmx.y) : mk[M_HEADTOP].y;
	const float headH   = std::max(headTop - mk[M_NECK].y, 1e-3f);
	const float headW = heads > 2 && hmx.x > hmn.x ? hmx.x - hmn.x : headH;
	float torsoW      = std::fabs(mk[M_LSHOULDER].x - mk[M_RSHOULDER].x);
	if (torsoW < headW * 0.3f) torsoW = headW;
	// La superficie de `joint` (o `joint2`) por delante (o por detrás) en la
	// línea de x = centro y altura `y`: el primer triángulo que corta un rayo
	// a lo largo de Z; si ninguno, los vértices cercanos.
	auto surface = [&](int joint, int joint2, float y, float band, bool front, float fallback) {
		const float x = mk[M_NECK].x;
		float z       = front ? -1e9f : 1e9f;
		bool hit      = false;
		for (std::size_t t = 0; t + 2 < rig.verts.size(); t += 3) {
			const SkinVertex &a = rig.verts[t], &b = rig.verts[t + 1], &c = rig.verts[t + 2];
			if (!dominant(a, joint) && !dominant(a, joint2)) continue;
			const float d = (b.p.y - c.p.y) * (a.p.x - c.p.x) + (c.p.x - b.p.x) * (a.p.y - c.p.y);
			if (std::fabs(d) < 1e-12f) continue;
			const float u = ((b.p.y - c.p.y) * (x - c.p.x) + (c.p.x - b.p.x) * (y - c.p.y)) / d;
			const float v = ((c.p.y - a.p.y) * (x - c.p.x) + (a.p.x - c.p.x) * (y - c.p.y)) / d;
			if (u < 0 || v < 0 || u + v > 1) continue;
			const float zz = a.p.z * u + b.p.z * v + c.p.z * (1 - u - v);
			z              = front ? std::max(z, zz) : std::min(z, zz);
			hit            = true;
		}
		if (hit) return z;
		for (const SkinVertex& v : rig.verts) {
			if (!(dominant(v, joint) || dominant(v, joint2)) || std::fabs(v.p.y - y) > band || std::fabs(v.p.x - mk[M_NECK].x) > torsoW * 0.35f) continue;
			z   = front ? std::max(z, v.p.z) : std::min(z, v.p.z);
			hit = true;
		}
		return hit ? z : fallback;
	};

	std::array<AttachSite, P_COUNT> s;
	const float torsoH = std::max(mk[M_NECK].y - mk[M_PELVIS].y, 1e-3f);
	// Coronilla: el marcador, en el centro de la cabeza (de frente a atrás).
	s[P_HEADTOP].posed = { mk[M_HEADTOP].x, headTop, heads > 2 ? (hmn.z + hmx.z) * 0.5f : mk[M_HEADTOP].z };
	s[P_HEADTOP].ref   = headW;
	// Cara: a media cabeza, en su frente.
	{
		const float y = heads > 2 ? hmn.y + (hmx.y - hmn.y) * 0.5f : mk[M_NECK].y + headH * 0.5f;
		s[P_FACE].posed = { mk[M_NECK].x, y, surface(J_HEAD, J_HEAD, y, headH * 0.15f, true, mk[M_NECK].z + headW * 0.5f) };
		s[P_FACE].ref   = headW;
	}
	// Pecho: debajo del cuello o, si la cabeza cuelga por delante (cabezones
	// como Olimar), debajo de la barbilla. Espalda: a media espalda, por
	// fuera de lo que ya lleve (una mochila, un casco que baja).
	{
		float chin = 1e9f;
		for (const SkinVertex& v : rig.verts)
			if (dominant(v, J_HEAD) && v.p.z > mk[M_NECK].z && std::fabs(v.p.x - mk[M_NECK].x) < torsoW * 0.35f) chin = std::min(chin, v.p.y);
		const float y    = std::max(std::min(mk[M_NECK].y - torsoH * 0.12f, chin - torsoW * 0.2f), mk[M_PELVIS].y + torsoH * 0.3f);
		s[P_CHEST].posed = { mk[M_NECK].x, y, surface(J_SPINE, J_SPINE, y, torsoH * 0.12f, true, mk[M_NECK].z + torsoW * 0.3f) };
		s[P_CHEST].ref   = torsoW;
		const float yb   = mk[M_PELVIS].y + torsoH * 0.55f;
		s[P_BACK].posed  = { mk[M_NECK].x, yb, surface(J_SPINE, J_HEAD, yb, torsoH * 0.25f, false, mk[M_NECK].z - torsoW * 0.3f) };
		s[P_BACK].ref    = torsoW;
	}
	for (int p = 0; p < P_COUNT; ++p) {
		s[std::size_t(p)].rest  = s[std::size_t(p)].posed;
		s[std::size_t(p)].joint = attachPointJoint(p);
	}
	// Manos: se enderezan con los brazos.
	// Lo que se lleva en la mano se ve a la escala de la cabeza (la mano es pequeña).
	const float handRef = 0.8f * headW;
	// El marcador si está sobre el brazo; si no (marcadores estimados sin
	// manos, como al reeditar un .nhm sin original), la punta del brazo.
	auto hand = [&](int marker, int shoulder, int joint, AttachSite& site) {
		site.posed = mk[std::size_t(marker)], site.rest = mr[std::size_t(marker)], site.ref = handRef;
		float armLen = 0, nearest = 1e9f;
		std::size_t tip = rig.verts.size();
		for (std::size_t i = 0; i < rig.verts.size(); ++i) {
			if (!dominant(rig.verts[i], joint)) continue;
			const float d = length(rig.verts[i].p - mk[std::size_t(shoulder)]);
			if (d > armLen) armLen = d, tip = i;
			nearest = std::min(nearest, length(rig.verts[i].p - mk[std::size_t(marker)]));
		}
		if (tip == rig.verts.size() || nearest < 0.25f * armLen) return;
		Vec3 c {}, cr {};
		int n = 0;
		for (std::size_t i = 0; i < rig.verts.size(); ++i) {
			if (!dominant(rig.verts[i], joint) || length(rig.verts[i].p - rig.verts[tip].p) > 0.2f * armLen) continue;
			c += rig.verts[i].p;
			cr += i < rig.unbent.size() ? rig.unbent[i] : rig.verts[i].p;
			++n;
		}
		site.posed = c * (1.0f / float(n)), site.rest = cr * (1.0f / float(n));
	};
	hand(M_LHAND, M_LSHOULDER, J_LARM, s[P_LHAND]);
	hand(M_RHAND, M_RSHOULDER, J_RARM, s[P_RHAND]);
	return s;
}

void placePart(const PartMesh& part, const Attachment& a, const AttachSite& site, int firstMaterial, RigResult& rig)
{
	const ImpModel& m = part.mesh;
	if (m.verts.empty()) return;
	Vec3 mn = m.verts[0].p, mx = mn;
	for (const SkinVertex& v : m.verts) mn = vmin(mn, v.p), mx = vmax(mx, v.p);
	// Ancla: la cara de la caja que toca al personaje.
	Vec3 anchor = (mn + mx) * 0.5f;
	switch (a.point) {
	case P_HEADTOP: anchor.y = mn.y; break;
	case P_FACE:
	case P_CHEST: anchor.z = mn.z; break;
	case P_BACK: anchor.z = mx.z; break;
	default: break;
	}
	float k = 1;
	if (part.normalize) k = userScale(a.point) / std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-6f });
	const float scale = site.ref * k * std::max(a.size, 0.01f);
	const Vec3 shift  = Vec3 { 0, a.up, a.forward } * site.ref;
	const int mats    = std::max(int(m.materials.size()), 1);
	for (std::size_t t = 0; t < m.triangles(); ++t) {
		for (int c = 0; c < 3; ++c) {
			SkinVertex v       = m.verts[t * 3 + std::size_t(c)];
			const Vec3 local   = (v.p - anchor) * scale + shift;
			v.p                = site.posed + local;
			v.n                = normalize(v.n);
			for (int i = 0; i < 4; ++i) v.joint[i] = site.joint, v.weight[i] = i == 0 ? 1.0f : 0.0f;
			rig.verts.push_back(v);
			rig.unbent.push_back(site.rest + local);
			rig.min = vmin(rig.min, v.p);
			rig.max = vmax(rig.max, v.p);
		}
		const int tm = t < m.triMaterial.size() ? m.triMaterial[t] : 0;
		rig.triMaterial.push_back(firstMaterial + std::clamp(tm, 0, mats - 1));
	}
}

} // namespace mm
