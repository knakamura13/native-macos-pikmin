// Pintar en la ventana: la sección Pintar del paso 1, el pincel en la vista
// 3D, las texturas de la vista mientras se pinta, preparar una parte sin UV
// que sirvan (en otro hilo) y guardar la pintura con el proyecto.

#include "app.h"

#include "lang.h"
#include "simplify.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace mm {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {

void hintText(const char* text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

std::vector<std::uint8_t> readFile(const fs::path& p)
{
	std::ifstream f(p, std::ios::binary);
	return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

std::string paintName(const std::string& dir, std::size_t m, const char* suffix) { return dir + "/" + std::to_string(m) + suffix; }

} // namespace

bool App::needsPaintSetup(int m)
{
	if (m < 0 || std::size_t(m) >= model_.materials.size()) return false;
	// Las UV se miran una vez por malla (cambia al abrir, simplificar o preparar).
	if (paint_.uvKey != model_.verts.data() || paint_.uvKeySize != model_.verts.size() || paint_.uvUse.size() != model_.materials.size()) {
		paint_.uvUse.assign(model_.materials.size(), UvUse::Good);
		for (std::size_t i = 0; i < model_.materials.size(); ++i) paint_.uvUse[i] = uvUse(model_.verts, model_.triMaterial, int(i));
		paint_.uvKey     = model_.verts.data();
		paint_.uvKeySize = model_.verts.size();
	}
	const ImpMaterial& mat = model_.materials[std::size_t(m)];
	// Colores de vértice sin textura: hay que pasarlos a una textura primero.
	if (mat.texture.empty() && mat.vertexColors) return true;
	return paint_.uvUse[std::size_t(m)] != UvUse::Good;
}

// Se puede pintar: es del modelo (no el tallo añadido), se ve y sus UV sirven.
bool App::paintable(int m)
{
	if (m < 0 || std::size_t(m) >= source_.materials.size() || std::size_t(m) >= model_.materials.size()) return false;
	if (std::size_t(m) < exportOpt_.looks.size() && exportOpt_.looks[std::size_t(m)].hidden) return false;
	return !needsPaintSetup(m);
}

// Rellenar no necesita UV propias (va a toda la capa): basta con que sea del
// modelo, se vea y tenga textura o color liso (los colores de vértice van
// sin textura hasta prepararla).
bool App::fillable(int m)
{
	if (m < 0 || std::size_t(m) >= source_.materials.size() || std::size_t(m) >= model_.materials.size()) return false;
	if (std::size_t(m) < exportOpt_.looks.size() && exportOpt_.looks[std::size_t(m)].hidden) return false;
	const ImpMaterial& mat = model_.materials[std::size_t(m)];
	return !(mat.texture.empty() && mat.vertexColors);
}

// UV nuevas para una parte, en el modelo original y en otro hilo; después se
// vuelve a simplificar a los mismos triángulos (finishLoad).
void App::preparePaint(int m)
{
	if (working_ || !hasModel_ || m < 0 || std::size_t(m) >= source_.materials.size()) return;
	if (worker_.joinable()) worker_.join();
	if (paint_.stroking) endStroke();
	pendingResimplify_   = true;
	working_             = true;
	workDone_            = false;
	paint_.preparing     = m;
	pendingSource_       = source_;
	paint_.preparedLayer = std::size_t(m) < exportOpt_.paint.size() ? exportOpt_.paint[std::size_t(m)] : nectar::Image();
	const std::size_t target = source_.triangles() > model_.triangles() ? model_.triangles() : 0;
	worker_ = std::thread([this, m, target] {
		std::string error;
		ImpModel out;
		if (unwrapMaterial(pendingSource_, m, error, &paint_.preparedLayer)) {
			out = pendingSource_;
			if (target) simplifyModel(out, target);
		}
		pending_      = std::move(out);
		pendingError_ = error;
		workDone_     = true;
	});
}

void App::finishPreparePaint(int m)
{
	source_ = std::move(pendingSource_);
	paint_.autoUv.resize(source_.materials.size(), 0);
	paint_.bakedBase.resize(source_.materials.size(), 0);
	paint_.autoUv[std::size_t(m)]    = 1;
	paint_.bakedBase[std::size_t(m)] = !source_.materials[std::size_t(m)].texture.empty();
	if (std::size_t(m) < exportOpt_.paint.size()) exportOpt_.paint[std::size_t(m)] = std::move(paint_.preparedLayer);
	paint_.preparedLayer = nectar::Image();
	uploadTextures();
	say(std::string(L("Lista para pintar: ", "Ready to paint: ")) + source_.materials[std::size_t(m)].name);
}

namespace {
template <typename F>
std::vector<PaintTarget> targetsFor(const ImpModel& model, F&& paintable)
{
	std::vector<PaintTarget> out(model.materials.size());
	for (std::size_t i = 0; i < out.size(); ++i) {
		out[i].paintable = paintable(int(i));
		paintLayerSize(model.materials[i], out[i].width, out[i].height);
	}
	return out;
}
} // namespace

// El cubo (o borrar todo): la capa entera del material, un paso de deshacer.
void App::fillPaint(int m, bool erase)
{
	if (!fillable(m)) {
		if (needsPaintSetup(m)) preparePaint(m);
		return;
	}
	if (paint_.stroking) endStroke();
	commitEdits();
	Brush b = paint_.brush;
	b.erase = erase;
	if (erase) b.opacity = 1;
	PaintStroke s;
	s.begin(b, rig_.verts, rig_.triMaterial, targetsFor(model_, [this](int i) { return fillable(i); }));
	s.fill(exportOpt_.paint, m);
	std::vector<PaintPatch> patches = s.end(exportOpt_.paint);
	if (patches.empty()) return;
	pushEditStep({ edits(), std::move(patches) });
	exportDirty_ = true;
	uploadTextures();
}

void App::endStroke()
{
	std::vector<PaintPatch> patches = paint_.stroke.end(exportOpt_.paint);
	paint_.stroking = false;
	if (patches.empty()) return;
	pushEditStep({ edits(), std::move(patches) });
	// Al soltar: el pack y las texturas a su tamaño de verdad.
	exportDirty_ = true;
	looksDirty_  = true;
}

// La textura de la vista de un material pintado: color, retoque y pintura,
// como en el pack. 0 si no tiene pintura.
GLuint App::paintedTexture(std::size_t i)
{
	if (i < paint_.live.size()) paint_.live[i] = 0;
	if (i >= exportOpt_.paint.size() || i >= model_.materials.size() || paintEmpty(exportOpt_.paint[i])) return 0;
	nectar::Image img = materialImage(model_.materials[i], 2048);
	if (i < exportOpt_.looks.size()) applyLook(exportOpt_.looks[i], img);
	compositePaint(img, exportOpt_.paint[i]);
	return uploadTexture(img, true);
}

// Mientras se pinta: solo los rectángulos que cambiaron, sobre una base al
// tamaño de la capa.
void App::updatePaintTextures()
{
	for (const PaintStroke::Rect& r : paint_.stroke.takeDirty()) {
		const std::size_t m = std::size_t(r.material);
		if (m >= exportOpt_.paint.size() || m >= modelTex_.size() || m >= model_.materials.size()) continue;
		const nectar::Image& layer = exportOpt_.paint[m];
		if (paint_.previewBase.size() <= m) paint_.previewBase.resize(m + 1);
		if (paint_.live.size() <= m) paint_.live.resize(m + 1, 0);
		nectar::Image& base = paint_.previewBase[m];
		if (!paint_.live[m] || base.width != layer.width || base.height != layer.height) {
			nectar::Image img = materialImage(model_.materials[m], kPaintMaxSide);
			if (m < exportOpt_.looks.size()) applyLook(exportOpt_.looks[m], img);
			base              = resizeImage(img, layer.width, layer.height);
			nectar::Image all = base;
			for (std::size_t p = 0; p < all.rgba.size(); p += 4) blendPaint(&base.rgba[p], &layer.rgba[p], &all.rgba[p]);
			if (modelTex_[m]) glDeleteTextures(1, &modelTex_[m]);
			modelTex_[m] = uploadTexture(all, true);
			if (m < modelTexBaked_.size()) modelTexBaked_[m] = 2;
			paint_.live[m] = 1;
			continue;
		}
		const int w = r.x1 - r.x0, h = r.y1 - r.y0;
		std::vector<std::uint8_t> px(std::size_t(w) * std::size_t(h) * 4);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) blendPaint(base.at(r.x0 + x, r.y0 + y), layer.at(r.x0 + x, r.y0 + y), &px[(std::size_t(y) * std::size_t(w) + std::size_t(x)) * 4]);
		glBindTexture(GL_TEXTURE_2D, modelTex_[m]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, r.x0, r.y0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
		glGenerateMipmap(GL_TEXTURE_2D);
	}
}

// Ratón en la vista en modo pintar. true si se lo queda (el resto, girar,
// desplazar y acercar, sigue en drawView).
bool App::paintMouse(bool hovered)
{
	if (!paint_.on) {
		if (paint_.stroking) endStroke();
		return false;
	}
	ImGuiIO& io = ImGui::GetIO();
	paint_.brush.radius = paint_.size * std::max(rig_.max.y - rig_.min.y, 1e-3f);
	// Lo translúcido (un visor) no para el pincel: se pinta lo de detrás. El
	// visor se puede rellenar desde la lista de partes.
	std::vector<char> visible(model_.materials.size(), 1);
	for (std::size_t i = 0; i < visible.size(); ++i)
		visible[i] = model_.materials[i].alpha != ImpMaterial::Blend && !(i < exportOpt_.looks.size() && exportOpt_.looks[i].hidden);
	const nm::M4 inv = viewProj_.inverse();
	auto ray         = [&](float mx, float my, PaintHit& hit) {
        const float nx = (mx - viewOrigin_.x) / viewSize_.x * 2 - 1, ny = 1 - (my - viewOrigin_.y) / viewSize_.y * 2;
        const nm::V3 a = inv.point({ nx, ny, -1 }), b = inv.point({ nx, ny, 1 });
        const Vec3 o { a.x, a.y, a.z };
        return paintRaycast(rig_.verts, rig_.triMaterial, visible, o, normalize(Vec3 { b.x - a.x, b.y - a.y, b.z - a.z }), hit);
	};

	PaintHit hit;
	const bool over      = hovered && ray(io.MousePos.x, io.MousePos.y, hit);
	paint_.hoverMaterial = over ? rig_.triMaterial[hit.tri] : -1;
	if (over) {
		// El pincel: un círculo del tamaño que pinta, donde está el ratón.
		const Vec3 right = normalize(cross(normalize(cam_.target - cam_.eye()), { 0, 1, 0 }));
		ImVec2 c, e;
		if (project(hit.point, c) && project(hit.point + right * paint_.brush.radius, e)) paint_.radiusPx = std::max(2.0f, std::hypot(e.x - c.x, e.y - c.y));
		ImDrawList* dl  = ImGui::GetWindowDrawList();
		const ImVec2 mp = io.MousePos;
		if (paint_.tool == 2 ? !fillable(paint_.hoverMaterial) : !paintable(paint_.hoverMaterial)) {
			dl->AddCircle(mp, 8, IM_COL32(240, 200, 70, 255), 0, 2);
			if (needsPaintSetup(paint_.hoverMaterial))
				ImGui::SetTooltip("%s", L("Esta parte no tiene UV para pintar: haz clic para prepararla.", "This part has no UVs to paint on: click to get it ready."));
		} else if (paint_.tool == 2) {
			dl->AddCircleFilled(mp, 7, ImGui::ColorConvertFloat4ToU32(ImVec4(paint_.brush.colour[0], paint_.brush.colour[1], paint_.brush.colour[2], 1)));
			dl->AddCircle(mp, 8, IM_COL32(255, 255, 255, 230), 0, 1.5f);
		} else {
			dl->AddCircle(mp, paint_.radiusPx, IM_COL32(20, 20, 20, 200), 0, 3);
			dl->AddCircle(mp, paint_.radiusPx, paint_.tool == 1 ? IM_COL32(255, 255, 255, 230) : ImGui::ColorConvertFloat4ToU32(ImVec4(paint_.brush.colour[0], paint_.brush.colour[1], paint_.brush.colour[2], 1)), 0, 1.5f);
		}
	}

	// Opción (o Alt) y Mayús dejan el botón izquierdo para girar y desplazar.
	const bool navigate = io.KeyAlt || io.KeyShift;
	if (!paint_.stroking && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !navigate) {
		const int m = paint_.hoverMaterial;
		if (paint_.tool == 2) {
			if (fillable(m)) fillPaint(m, false);
			else if (over && needsPaintSetup(m)) preparePaint(m);
			return true;
		}
		if (over && !paintable(m)) {
			if (needsPaintSetup(m)) preparePaint(m);
			return true;
		}
		// El trazo empieza aunque sea fuera del modelo: pinta al pasar por encima.
		commitEdits();
		Brush b = paint_.brush;
		b.erase = paint_.tool == 1;
		paint_.stroke.begin(b, rig_.verts, rig_.triMaterial, targetsFor(model_, [this](int i) { return paintable(i); }));
		if (over) paint_.stroke.stamp(exportOpt_.paint, hit.point, hit.normal);
		paint_.stroking = true;
		paint_.lastX = io.MousePos.x, paint_.lastY = io.MousePos.y;
		updatePaintTextures();
		return true;
	}
	if (!paint_.stroking) return false;
	if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
		endStroke();
		return true;
	}
	// De donde estaba el ratón a donde está: toques cada ~30% del radio, para
	// que un arrastre rápido no deje puntos sueltos.
	const float dx = io.MousePos.x - paint_.lastX, dy = io.MousePos.y - paint_.lastY, dist = std::hypot(dx, dy);
	if (dist > 0.5f) {
		const float step = std::max(1.5f, paint_.radiusPx * 0.3f);
		const int n      = std::min(256, std::max(1, int(std::ceil(dist / step))));
		for (int i = 1; i <= n; ++i) {
			const float t = float(i) / float(n);
			PaintHit h;
			if (ray(paint_.lastX + dx * t, paint_.lastY + dy * t, h)) paint_.stroke.stamp(exportOpt_.paint, h.point, h.normal);
		}
		paint_.lastX = io.MousePos.x, paint_.lastY = io.MousePos.y;
	}
	updatePaintTextures();
	return true;
}

// Sección Pintar (paso 1, debajo de Colores): abierta, la vista pinta.
void App::drawPaint()
{
	ImGui::SetNextItemOpen(paint_.open, ImGuiCond_Always);
	paint_.open = ImGui::CollapsingHeader(L("Pintar", "Paint"));
	if (!paint_.open) return;
	paint_.on = true;
	hintText(L("Pinta encima del modelo. Izquierdo: pintar; derecho (u Opción): girar. Cada trazo se deshace como un color.",
	           "Paint right on the model. Left drag paints; right drag (or Alt) turns the view. Each stroke undoes like a colour change."));
	ImGui::RadioButton(L("Pincel", "Brush"), &paint_.tool, 0);
	ImGui::SameLine();
	ImGui::RadioButton(L("Goma", "Eraser"), &paint_.tool, 1);
	ImGui::SameLine();
	ImGui::RadioButton(L("Cubo", "Fill"), &paint_.tool, 2);
	ImGui::ColorEdit3(L("Color", "Colour"), paint_.brush.colour, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel);
	ImGui::SetNextItemWidth(-1);
	float pct = paint_.size * 100.0f;
	if (ImGui::SliderFloat("##psize", &pct, 1.0f, 25.0f, L("Tamaño %.0f%%", "Size %.0f%%"))) paint_.size = pct / 100.0f;
	ImGui::SetNextItemWidth(-1);
	float hard = paint_.brush.hardness * 100.0f;
	if (ImGui::SliderFloat("##phard", &hard, 0.0f, 100.0f, L("Dureza %.0f%%", "Hardness %.0f%%"))) paint_.brush.hardness = hard / 100.0f;
	ImGui::SetNextItemWidth(-1);
	float opacity = paint_.brush.opacity * 100.0f;
	if (ImGui::SliderFloat("##popacity", &opacity, 5.0f, 100.0f, L("Opacidad %.0f%%", "Opacity %.0f%%"))) paint_.brush.opacity = opacity / 100.0f;

	// Las partes: rellenar, borrar o preparar las que no tienen UV.
	for (std::size_t i = 0; i < source_.materials.size() && i < model_.materials.size(); ++i) {
		if (i < exportOpt_.looks.size() && exportOpt_.looks[i].hidden) continue;
		ImGui::PushID(int(i) + 1000);
		const bool hot = paint_.hoverMaterial == int(i);
		if (hot) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 205, 90, 255));
		ImGui::TextUnformatted(model_.materials[i].name.empty() ? L("(sin nombre)", "(unnamed)") : model_.materials[i].name.c_str());
		if (hot) ImGui::PopStyleColor();
		// Los botones a la derecha, en la misma línea si caben.
		const bool canFill = fillable(int(i)), needs = needsPaintSetup(int(i));
		const ImGuiStyle& st = ImGui::GetStyle();
		auto width = [&](const char* label) { return ImGui::CalcTextSize(label).x + st.FramePadding.x * 2 + st.ItemSpacing.x; };
		const float buttons = (canFill ? width(L("Rellenar", "Fill")) + width(L("Borrar", "Clear")) : 0.0f) + (needs ? width(L("Preparar", "Make paintable")) : 0.0f);
		const float right   = ImGui::GetWindowContentRegionMax().x - buttons + st.ItemSpacing.x;
		if (right > ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + st.ItemSpacing.x) ImGui::SameLine(right);
		else ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right));
		ImGui::BeginDisabled(working_);
		if (canFill) {
			if (ImGui::SmallButton(L("Rellenar", "Fill"))) fillPaint(int(i), false);
			ImGui::SameLine();
			ImGui::BeginDisabled(i >= exportOpt_.paint.size() || paintEmpty(exportOpt_.paint[i]));
			if (ImGui::SmallButton(L("Borrar", "Clear"))) fillPaint(int(i), true);
			ImGui::EndDisabled();
		}
		if (needs) {
			if (canFill) ImGui::SameLine();
			if (ImGui::SmallButton(L("Preparar", "Make paintable"))) preparePaint(int(i));
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", model_.materials[i].texture.empty() && model_.materials[i].vertexColors
				                            ? L("Sus colores van en los vértices: se pasan a una textura.", "Its colours live in the vertices: they move to a texture.")
				                        : paint_.uvUse[i] == UvUse::Missing
				                            ? L("No tiene UV: se le hacen unas nuevas.", "It has no UVs: new ones are made.")
				                            : L("Sus UV se pisan (espejo o mosaico): para el pincel se le hacen unas nuevas.",
				                                "Its UVs overlap (mirrored or tiled): the brush needs new ones."));
		}
		ImGui::EndDisabled();
		ImGui::PopID();
	}
}

// Un modelo nuevo: sin pintura; las UV y texturas que el hilo aplicó desde el
// proyecto (applyPaintSetups) quedan marcadas para volver a guardarlas.
void App::resetPaint()
{
	if (paint_.stroking) paint_.stroke.end(exportOpt_.paint);
	paint_.stroking = false;
	exportOpt_.paint.assign(model_.materials.size(), nectar::Image());
	paint_.previewBase.clear();
	paint_.live.clear();
	paint_.autoUv.assign(source_.materials.size(), 0);
	paint_.bakedBase.assign(source_.materials.size(), 0);
	for (int m : paint_.applied) {
		if (m < 0 || std::size_t(m) >= source_.materials.size()) continue;
		for (const PaintState::Setup& s : paint_.setups) {
			if (s.material != m) continue;
			paint_.autoUv[std::size_t(m)]    = paint_.autoUv[std::size_t(m)] || !s.uv.empty();
			paint_.bakedBase[std::size_t(m)] = paint_.bakedBase[std::size_t(m)] || !s.base.empty();
		}
	}
}

// Antes de abrir: las UV y texturas hechas para pintar que trae el proyecto.
void App::readPaintSetups(bool restoreProject)
{
	paint_.setups.clear();
	paint_.applied.clear();
	paint_.loadDir.clear();
	const std::string restoreDir = std::exchange(paint_.restoreDir, std::string());
	if (!restoreProject) return;
	std::string text = restoreJson_;
	std::string dir  = restoreDir;
	if (text.empty()) {
		std::ifstream f(fs::u8path(projectPath()));
		text.assign(std::istreambuf_iterator<char>(f), {});
		dir = customDir();
	}
	paint_.loadDir = dir;
	try {
		const json j = json::parse(text);
		if (!j.contains("paint") || !j["paint"].is_array()) return;
		for (const json& e : j["paint"]) {
			PaintState::Setup s;
			s.material             = e.value("material", -1);
			const std::string uv   = e.value("uv", std::string());
			const std::string base = e.value("base", std::string());
			if (s.material < 0) continue;
			if (!uv.empty() && nectar::safeProjectPath(uv)) s.uv = readFile(fs::u8path(dir) / fs::u8path(uv));
			if (!base.empty() && nectar::safeProjectPath(base)) decodePng(readFile(fs::u8path(dir) / fs::u8path(base)), s.base);
			if (!s.uv.empty() || !s.base.empty()) paint_.setups.push_back(std::move(s));
		}
	} catch (const std::exception&) {
	}
}

// En el hilo que abre el modelo, antes de simplificarlo.
void App::applyPaintSetups(ImpModel& model)
{
	for (const PaintState::Setup& s : paint_.setups) {
		if (std::size_t(s.material) >= model.materials.size()) continue;
		if (!s.uv.empty() && !unpackUvs(model, s.material, s.uv)) continue; // el original cambió
		if (!s.base.empty()) model.materials[std::size_t(s.material)].texture = s.base;
		paint_.applied.push_back(s.material);
	}
}

// Las capas guardadas (finishLoad, con el proyecto ya leído).
void App::restorePaint(const json& j)
{
	if (!j.contains("paint") || !j["paint"].is_array()) return;
	for (const json& e : j["paint"]) {
		const int m             = e.value("material", -1);
		const std::string layer = e.value("layer", std::string());
		if (m < 0 || std::size_t(m) >= exportOpt_.paint.size() || layer.empty() || !nectar::safeProjectPath(layer)) continue;
		nectar::Image img;
		if (decodePng(readFile(fs::u8path(paint_.loadDir) / fs::u8path(layer)), img)) exportOpt_.paint[std::size_t(m)] = std::move(img);
	}
}

void App::paintJson(json& j, const std::string& dir) const
{
	json list = json::array();
	for (std::size_t m = 0; m < model_.materials.size(); ++m) {
		json e;
		if (m < exportOpt_.paint.size() && !paintEmpty(exportOpt_.paint[m])) e["layer"] = paintName(dir, m, ".png");
		if (m < paint_.autoUv.size() && paint_.autoUv[m]) e["uv"] = paintName(dir, m, "_uv.bin");
		if (m < paint_.bakedBase.size() && paint_.bakedBase[m]) e["base"] = paintName(dir, m, "_base.png");
		if (e.empty()) continue;
		e["material"] = m;
		list.push_back(e);
	}
	// Sin pintura, el proyecto queda como antes.
	if (!list.empty()) j["paint"] = list;
}

void App::paintFiles(const std::string& dir, std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out) const
{
	for (std::size_t m = 0; m < model_.materials.size(); ++m) {
		std::vector<std::uint8_t> data;
		if (m < exportOpt_.paint.size() && !paintEmpty(exportOpt_.paint[m]) && encodePng(exportOpt_.paint[m], data)) out.push_back({ paintName(dir, m, ".png"), std::move(data) });
		if (m < paint_.autoUv.size() && paint_.autoUv[m] && m < source_.materials.size()) out.push_back({ paintName(dir, m, "_uv.bin"), packUvs(source_, int(m)) });
		if (m < paint_.bakedBase.size() && paint_.bakedBase[m] && m < source_.materials.size() && encodePng(source_.materials[m].texture, data))
			out.push_back({ paintName(dir, m, "_base.png"), std::move(data) });
	}
}

} // namespace mm
