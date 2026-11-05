// Piezas del modelo en el paso de colocación: elegir un material (en la lista
// o pinchando en la vista), moverlo, girarlo o escalarlo con el gizmo y
// pasarlo o copiarlo al otro lado. Las cuentas están en parts.cpp.

#include "app.h"

#include "lang.h"

#include "ImGuizmo.h"

#include <cmath>
#include <string>

namespace mm {

using json = nlohmann::json;

namespace {

void partHint(const char* text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

} // namespace

void App::selectPart(int material)
{
	if (material == selectedPart_) return;
	// Con una pieza elegida se edita en la pose de reposo (la del gizmo); al
	// soltarla vuelve la animación que había.
	if (material >= 0 && selectedPart_ < 0) {
		partAnimBefore_ = anim_;
		anim_           = -1;
	} else if (material < 0 && anim_ < 0) {
		anim_ = partAnimBefore_;
	}
	selectedPart_ = material;
}

void App::drawParts()
{
	undoShortcuts();
	ImGui::Dummy(ImVec2(0, 4));
	if (!ImGui::CollapsingHeader(L("Piezas", "Parts"), ImGuiTreeNodeFlags_DefaultOpen)) {
		selectPart(-1);
		return;
	}
	partHint(L("Elige una pieza (aquí o pinchando en el modelo) para moverla, girarla o cambiarle el tamaño.",
	           "Pick a part (here or by clicking the model) to move, turn or resize it."));
	drawUndoButtons();

	// Solo los materiales con triángulos del modelo (el tallo añadido no).
	std::vector<int> tris(model_.materials.size(), 0);
	for (int m : model_.triMaterial)
		if (m >= 0 && std::size_t(m) < tris.size()) ++tris[std::size_t(m)];
	int rows = 0;
	for (std::size_t i = 0; i < tris.size() && i < fit_.parts.size(); ++i) rows += tris[i] > 0;
	if (selectedPart_ >= 0 && (std::size_t(selectedPart_) >= fit_.parts.size() || !tris[std::size_t(selectedPart_)])) selectPart(-1);
	const float rowH = ImGui::GetTextLineHeightWithSpacing();
	if (ImGui::BeginListBox("##parts", ImVec2(-1, float(std::min(std::max(rows, 1), 6)) * rowH + 8))) {
		for (std::size_t i = 0; i < tris.size() && i < fit_.parts.size(); ++i) {
			if (!tris[i]) continue;
			std::string label = model_.materials[i].name.empty() ? L("(sin nombre)", "(unnamed)") : model_.materials[i].name;
			if (!fit_.parts[i].identity()) label += L("  · cambiada", "  · edited");
			if (i < exportOpt_.looks.size() && exportOpt_.looks[i].hidden) label += L("  (oculta)", "  (hidden)");
			ImGui::PushID(int(i));
			if (ImGui::Selectable(label.c_str(), selectedPart_ == int(i))) selectPart(selectedPart_ == int(i) ? -1 : int(i));
			ImGui::PopID();
		}
		ImGui::EndListBox();
	}
	if (selectedPart_ < 0) return;

	anim_                  = -1;
	PartEdit& e            = fit_.parts[std::size_t(selectedPart_)];
	bool changed           = false;
	const ImGuiIO& io      = ImGui::GetIO();
	if (!io.WantTextInput && !io.KeyCtrl && !io.KeySuper) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) gizmoOp_ = 0;
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) gizmoOp_ = 1;
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) gizmoOp_ = 2;
	}
	const char* ops[3] = { L("Mover (W)", "Move (W)"), L("Girar (E)", "Rotate (E)"), L("Tamaño (R)", "Scale (R)") };
	const float third  = (ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
	for (int i = 0; i < 3; ++i) {
		if (i) ImGui::SameLine();
		if (i == gizmoOp_) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.43f, 0.57f, 0.19f, 1.0f));
		if (ImGui::Button(ops[i], ImVec2(third, 0))) gizmoOp_ = i;
		if (i == gizmoOp_) ImGui::PopStyleColor();
	}
	bool copy = e.mirrorCopy;
	if (ImGui::Checkbox(L("Copia en el otro lado (espejo)", "Mirrored copy on the other side"), &copy)) {
		e.mirrorCopy = copy;
		changed      = true;
	}
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("%s", L("Para una oreja, un guante o un adorno que solo está en un lado. La copia sigue a la pieza.",
		                          "For an ear, glove or accessory that is only on one side. The copy follows the part."));
	// Si la pieza ya cruza el centro, la copia caería encima.
	if (e.mirrorCopy && std::size_t(selectedPart_) < rig_.partCentre.size()) {
		const Vec3 c = rig_.partCentre[std::size_t(selectedPart_)] + translationOf(e.local);
		if (std::fabs(c.x) < 0.05f * std::max(rig_.max.x - rig_.min.x, 1e-3f))
			partHint(L("Esta pieza está en el centro: la copia queda encima. Muévela a un lado.",
			           "This part sits in the middle, so the copy overlaps it. Move it to one side."));
	}
	const float half = ImGui::GetContentRegionAvail().x * 0.5f - 4;
	if (ImGui::Button(L("Cambiar de lado", "Flip side"), ImVec2(half, 0))) {
		// Se ve igual que en un espejo: también el movimiento que ya llevaba.
		e.flipped = !e.flipped;
		e.local   = mirrorLocal(e.local);
		changed   = true;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(e.identity());
	if (ImGui::Button(L("Quitar cambios", "Reset part"), ImVec2(-1, 0))) {
		e       = PartEdit();
		changed = true;
	}
	ImGui::EndDisabled();
	partHint(L("Arrastra las flechas o los aros del gizmo en la vista. Pincha fuera del modelo para soltar la pieza.",
	           "Drag the gizmo's arrows or rings in the view. Click away from the model to let go of the part."));
	if (changed) rigDirty_ = partsDirty_ = true;
}

bool App::drawPartGizmo(bool hovered)
{
	if (editorStep_ != 1 || !hasModel_) return false;
	ImGuiIO& io    = ImGui::GetIO();
	bool overGizmo = false;
	const int sel  = selectedPart_;
	if (sel >= 0 && anim_ < 0 && std::size_t(sel) < fit_.parts.size() && std::size_t(sel) < rig_.partCentre.size()) {
		PartEdit& e = fit_.parts[std::size_t(sel)];
		const Vec3 c = rig_.partCentre[std::size_t(sel)];
		ImGuizmo::BeginFrame();
		ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
		ImGuizmo::SetRect(viewOrigin_.x, viewOrigin_.y, viewSize_.x, viewSize_.y);
		ImGuizmo::SetOrthographic(false);
		ImGuizmo::SetGizmoSizeClipSpace(0.16f); // algo más grande que el de serie: más fácil de agarrar
		const nm::M4 view = cam_.view(), proj = cam_.projection(viewSize_.x / viewSize_.y);
		// El gizmo va en el centro de la pieza: T(c) · local.
		nm::M4 m = toM4(translation(c) * e.local);
		static const ImGuizmo::OPERATION kOps[3] = { ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE };
		const int op = std::clamp(gizmoOp_, 0, 2);
		if (ImGuizmo::Manipulate(view.m, proj.m, kOps[op], op == 2 ? ImGuizmo::LOCAL : ImGuizmo::WORLD, m.m)) {
			Mat34 g;
			for (int row = 0; row < 3; ++row)
				for (int col = 0; col < 4; ++col) g.m[row][col] = m.at(row, col);
			e.local   = translation(c * -1.0f) * g;
			rigDirty_ = partsDirty_ = true;
		}
		overGizmo = ImGuizmo::IsOver() || ImGuizmo::IsUsing();
	}
	// Un clic sin arrastrar (ni empezar en el gizmo o en un botón de la vista)
	// elige la pieza de debajo; en el vacío, la suelta.
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) pressOnGizmo_ = overGizmo;
	if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !pressOnGizmo_ && !ImGui::IsAnyItemHovered() && io.MouseDragMaxDistanceSqr[0] < 16.0f)
		selectPart(pickPart(io.MousePos));
	return overGizmo;
}

// El triángulo más cercano bajo el ratón, en la malla tal como se ve.
int App::pickPart(const ImVec2& mouse) const
{
	const nm::M4 inv = viewProj_.inverse();
	const float nx   = (mouse.x - viewOrigin_.x) / viewSize_.x * 2 - 1;
	const float ny   = 1 - (mouse.y - viewOrigin_.y) / viewSize_.y * 2;
	const nm::V3 a = inv.point({ nx, ny, -1 }), b = inv.point({ nx, ny, 1 });
	const Vec3 o { a.x, a.y, a.z }, d { b.x - a.x, b.y - a.y, b.z - a.z };
	float best    = 2.0f; // a lo largo de d: 0 cerca, 1 lejos
	int bestMat   = -1;
	std::size_t k = 0; // triángulo de modelMesh_ (fillModelMesh se salta las piezas ocultas)
	const std::vector<GpuVertex>& data = modelMesh_.data;
	for (std::size_t t : order_) {
		if (t * 3 + 2 >= rig_.verts.size()) continue;
		const int mat = rig_.triMaterial[t];
		if (std::size_t(mat) < exportOpt_.looks.size() && exportOpt_.looks[std::size_t(mat)].hidden) continue;
		if (k * 3 + 2 >= data.size()) break;
		const GpuVertex* v = &data[k * 3];
		++k;
		// Möller–Trumbore, por las dos caras.
		const Vec3 p0 { v[0].p[0], v[0].p[1], v[0].p[2] }, p1 { v[1].p[0], v[1].p[1], v[1].p[2] }, p2 { v[2].p[0], v[2].p[1], v[2].p[2] };
		const Vec3 e1 = p1 - p0, e2 = p2 - p0, h = cross(d, e2);
		const float det = dot(e1, h);
		if (std::fabs(det) < 1e-12f) continue;
		const float f = 1.0f / det;
		const Vec3 s  = o - p0;
		const float u = f * dot(s, h);
		if (u < 0 || u > 1) continue;
		const Vec3 q  = cross(s, e1);
		const float w = f * dot(d, q);
		if (w < 0 || u + w > 1) continue;
		const float dist = f * dot(e2, q);
		if (dist > 0 && dist < best) {
			best    = dist;
			bestMat = mat;
		}
	}
	if (bestMat == fit_.stemMaterial || std::size_t(bestMat) >= fit_.parts.size()) return -1;
	return bestMat;
}

// Mientras se arrastra, la malla montada va un poco por detrás: lo que falta
// para llegar a fit_.parts, alrededor del centro de la pieza.
bool App::partPreview(Mat34 move[2], Mat34 normal[2]) const
{
	const int sel = selectedPart_;
	if (sel < 0 || std::size_t(sel) >= fit_.parts.size() || std::size_t(sel) >= rig_.parts.size() || std::size_t(sel) >= rig_.partCentre.size())
		return false;
	const PartEdit& now   = fit_.parts[std::size_t(sel)];
	const PartEdit& built = rig_.parts[std::size_t(sel)];
	if (now == built || now.flipped != built.flipped || now.mirrorCopy != built.mirrorCopy) return false;
	const Mat34 delta = now.local * inverse(built.local);
	move[0]           = about(rig_.partCentre[std::size_t(sel)], delta);
	normal[0]         = normalMatrix(delta);
	move[1]           = mirrorLocal(move[0]);
	normal[1]         = mirrorLocal(normal[0]);
	return true;
}

json App::partsJson() const
{
	json out = json::array();
	for (const PartEdit& e : fit_.parts) {
		json o = json::object();
		if (!e.identity()) {
			json m = json::array();
			for (int row = 0; row < 3; ++row)
				for (int col = 0; col < 4; ++col) m.push_back(e.local.m[row][col]);
			o["local"]      = m;
			o["flipped"]    = e.flipped;
			o["mirrorCopy"] = e.mirrorCopy;
		}
		out.push_back(o);
	}
	return out;
}

void App::restoreParts(const json& j)
{
	if (!j.is_array()) return;
	for (std::size_t i = 0; i < j.size() && i < fit_.parts.size(); ++i) {
		const json& o = j[i];
		if (!o.is_object()) continue;
		PartEdit e;
		if (o.contains("local") && o["local"].is_array() && o["local"].size() == 12)
			for (std::size_t k = 0; k < 12; ++k) e.local.m[k / 4][k % 4] = o["local"][k].get<float>();
		e.flipped      = o.value("flipped", false);
		e.mirrorCopy   = o.value("mirrorCopy", false);
		fit_.parts[i]  = e;
	}
}

} // namespace mm
