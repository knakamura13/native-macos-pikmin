#include "parts.h"

#include <numeric>
#include <utility>

namespace mm {

bool PartEdit::identity() const { return *this == PartEdit(); }

bool PartEdit::operator==(const PartEdit& o) const
{
	if (flipped != o.flipped || mirrorCopy != o.mirrorCopy) return false;
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 4; ++c)
			if (local.m[r][c] != o.local.m[r][c]) return false;
	return true;
}

int mirrorJoint(int joint)
{
	switch (joint) {
	case J_LARM: return J_RARM;
	case J_RARM: return J_LARM;
	case J_LLEG: return J_RLEG;
	case J_RLEG: return J_LLEG;
	default: return joint;
	}
}

Mat34 mirrorLocal(const Mat34& local)
{
	// R · local · R con R = diag(-1, 1, 1): cambia de signo lo que mezcla x con y/z.
	static const float s[3] = { -1, 1, 1 };
	Mat34 r = local;
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) r.m[row][col] *= s[row] * s[col];
		r.m[row][3] *= s[row];
	}
	return r;
}

Mat34 normalMatrix(const Mat34& m)
{
	const Mat34 inv = inverse(m);
	Mat34 n;
	for (int row = 0; row < 3; ++row)
		for (int col = 0; col < 3; ++col) n.m[row][col] = inv.m[col][row];
	return n;
}

namespace {

float det3(const Mat34& m)
{
	return m.m[0][0] * (m.m[1][1] * m.m[2][2] - m.m[1][2] * m.m[2][1]) - m.m[0][1] * (m.m[1][0] * m.m[2][2] - m.m[1][2] * m.m[2][0])
	     + m.m[0][2] * (m.m[1][0] * m.m[2][1] - m.m[1][1] * m.m[2][0]);
}

void reflect(SkinVertex& v)
{
	v.p.x = -v.p.x;
	v.n.x = -v.n.x;
}

} // namespace

void applyPartEdits(const std::vector<PartEdit>& edits, std::vector<SkinVertex>& verts, std::vector<int>& triMaterial, PartEditResult& out)
{
	const std::size_t tris = std::min(triMaterial.size(), verts.size() / 3);
	int mats               = int(edits.size());
	for (int m : triMaterial) mats = std::max(mats, m + 1);
	out.source.resize(verts.size());
	std::iota(out.source.begin(), out.source.end(), 0u);
	out.mirrored.assign(verts.size(), 0);
	out.triCopy.assign(triMaterial.size(), 0);
	auto editOf = [&](int m) -> const PartEdit* {
		return m >= 0 && std::size_t(m) < edits.size() && !edits[std::size_t(m)].identity() ? &edits[std::size_t(m)] : nullptr;
	};

	// Centro de cada pieza: el de su caja tal como llega (reflejada si va al
	// otro lado), para girar y escalar alrededor de él.
	const float big = 3.4e38f;
	std::vector<Vec3> mn(std::size_t(mats), Vec3 { big, big, big }), mx(std::size_t(mats), Vec3 { -big, -big, -big });
	for (std::size_t t = 0; t < tris; ++t) {
		const int m = triMaterial[t];
		if (m < 0) continue;
		const bool flip = std::size_t(m) < edits.size() && edits[std::size_t(m)].flipped;
		for (int k = 0; k < 3; ++k) {
			Vec3 p = verts[t * 3 + std::size_t(k)].p;
			if (flip) p.x = -p.x;
			mn[std::size_t(m)] = vmin(mn[std::size_t(m)], p);
			mx[std::size_t(m)] = vmax(mx[std::size_t(m)], p);
		}
	}
	out.centre.assign(std::size_t(mats), Vec3 {});
	for (std::size_t m = 0; m < std::size_t(mats); ++m)
		if (mn[m].x <= mx[m].x) out.centre[m] = (mn[m] + mx[m]) * 0.5f;

	// Matrices de cada pieza (posición y normales).
	const std::size_t count = static_cast<std::size_t>(mats);
	std::vector<Mat34> move(count), normal(count);
	std::vector<char> flipWinding(count, 0);
	bool any = false;
	for (std::size_t m = 0; m < std::size_t(mats); ++m) {
		const PartEdit* e = editOf(int(m));
		if (!e) continue;
		any            = true;
		move[m]        = about(out.centre[m], e->local);
		normal[m]      = normalMatrix(e->local);
		flipWinding[m] = det3(e->local) < 0; // escala negativa: el triángulo se da la vuelta
	}
	if (!any) return;

	for (std::size_t t = 0; t < tris; ++t) {
		const PartEdit* e = editOf(triMaterial[t]);
		if (!e) continue;
		const std::size_t m = std::size_t(triMaterial[t]);
		SkinVertex* v       = &verts[t * 3];
		bool swapWinding    = flipWinding[m] != 0;
		if (e->flipped) {
			for (int k = 0; k < 3; ++k) {
				reflect(v[k]);
				out.mirrored[t * 3 + std::size_t(k)] = 1;
			}
			swapWinding = !swapWinding;
		}
		for (int k = 0; k < 3; ++k) {
			v[k].p = move[m].point(v[k].p);
			v[k].n = normalize(normal[m].dir(v[k].n));
		}
		// Un reflejo cambia el sentido de giro: se cambian dos vértices para que
		// siga de frente en sentido antihorario.
		if (swapWinding) {
			std::swap(v[1], v[2]);
			std::swap(out.source[t * 3 + 1], out.source[t * 3 + 2]);
		}
	}

	// Copias reflejadas, al final (con el sentido de giro corregido).
	std::size_t copies = 0;
	for (std::size_t t = 0; t < tris; ++t) {
		const PartEdit* e = editOf(triMaterial[t]);
		copies += e && e->mirrorCopy;
	}
	verts.reserve(verts.size() + copies * 3);
	for (std::size_t t = 0; t < tris; ++t) {
		const PartEdit* e = editOf(triMaterial[t]);
		if (!e || !e->mirrorCopy) continue;
		static const int kOrder[3] = { 0, 2, 1 };
		for (int k : kOrder) {
			SkinVertex c = verts[t * 3 + std::size_t(k)];
			reflect(c);
			verts.push_back(c);
			out.source.push_back(out.source[t * 3 + std::size_t(k)]);
			out.mirrored.push_back(char(!out.mirrored[t * 3 + std::size_t(k)]));
		}
		triMaterial.push_back(triMaterial[t]);
		out.triCopy.push_back(1);
	}
}

} // namespace mm
