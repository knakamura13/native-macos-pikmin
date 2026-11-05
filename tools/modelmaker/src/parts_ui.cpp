// Piezas enganchadas en el creador: sus materiales (detrás de los del modelo
// y del tallo), sus triángulos al montar el modelo, el panel, deshacer y el
// proyecto.

#include "app.h"

#include "lang.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace mm {

namespace fs = std::filesystem;
using json   = nlohmann::json;

namespace {

void hint(const char* text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	ImGui::TextWrapped("%s", text);
	ImGui::PopStyleColor();
}

std::string partPrefix(int uid) { return "part" + std::to_string(uid) + "."; }

json lookJson(const MaterialLook& lk)
{
	return { { "colour", { lk.colour[0], lk.colour[1], lk.colour[2] } }, { "hue", lk.hue }, { "brightness", lk.brightness }, { "hidden", lk.hidden } };
}

MaterialLook lookFromJson(const json& o)
{
	MaterialLook lk;
	if (!o.is_object()) return lk;
	if (o.contains("colour") && o["colour"].is_array() && o["colour"].size() == 3)
		for (int k = 0; k < 3; ++k) lk.colour[k] = o["colour"][std::size_t(k)].get<float>();
	lk.hue        = o.value("hue", 0.0f);
	lk.brightness = o.value("brightness", 1.0f);
	lk.hidden     = o.value("hidden", false);
	return lk;
}

std::string readText(const std::string& path)
{
	std::ifstream f(fs::u8path(path), std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(f)), {});
}

// El lector de OBJ apunta todo lo que hay a su lado; en la carpeta de piezas
// eso son las de otras piezas. Solo lo que nombran el .obj y sus .mtl.
void keepObjFiles(const std::string& obj, std::vector<std::string>& files)
{
	std::string text = readText(obj);
	std::vector<std::string> kept { obj };
	auto ext = [](const std::string& f) {
		std::string e = fs::u8path(f).extension().u8string();
		for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
		return e;
	};
	std::string mtlText;
	for (const std::string& f : files)
		if (f != obj && ext(f) == ".mtl" && text.find(fs::u8path(f).filename().u8string()) != std::string::npos) {
			kept.push_back(f);
			mtlText += readText(f);
		}
	text += mtlText;
	for (const std::string& f : files)
		if (f != obj && ext(f) != ".mtl" && text.find(fs::u8path(f).filename().u8string()) != std::string::npos) kept.push_back(f);
	files = std::move(kept);
}

// Como el botón de las partidas del launcher: una URL file:// que abre el
// explorador del sistema.
bool openFolder(const fs::path& folder)
{
	const std::string path = folder.generic_u8string();
	std::string url        = path.empty() || path[0] != '/' ? "file:///" : "file://";
	for (const char c : path) {
		if (c == ' ' || c == '%' || c == '#' || c == '?') {
			char escaped[4];
			std::snprintf(escaped, sizeof(escaped), "%%%02X", static_cast<unsigned char>(c));
			url += escaped;
		} else {
			url += c;
		}
	}
	return SDL_OpenURL(url.c_str()) == 0;
}

} // namespace

std::string App::partsDir() const { return (fs::path(cfg_.gameDir()) / "Load" / "Models" / "Parts").u8string(); }

void App::modelReplaced(bool keepParts)
{
	baseMaterials_ = model_.materials.size();
	if (keepParts) return;
	attachments_.clear();
	tailKeys_.clear();
	tailMemory_.clear();
	partMaterial_.clear();
}

// Los materiales detrás de los del modelo: el tallo (Pikmin) y los de cada
// pieza. Sus colores y tinte siguen a su dueño por la clave, así que quitar
// una pieza, cambiar de objetivo o deshacer no los mezcla con los de otro.
void App::layoutMaterials()
{
	for (std::size_t i = 0; i < tailKeys_.size(); ++i) {
		const std::size_t m = baseMaterials_ + i;
		auto& mem           = tailMemory_[tailKeys_[i]];
		if (m < exportOpt_.looks.size()) mem.first = exportOpt_.looks[m];
		if (m < exportOpt_.noTint.size()) mem.second = exportOpt_.noTint[m];
	}
	baseMaterials_ = std::min(baseMaterials_, model_.materials.size());
	model_.materials.resize(baseMaterials_);
	tailKeys_.clear();
	partMaterial_.clear();
	fit_.stemMaterial = -1;
	ImpMaterial stem;
	if (stemMaterial(stem)) {
		fit_.stemMaterial = int(model_.materials.size());
		model_.materials.push_back(stem);
		tailKeys_.push_back("stem");
	}
	for (const Attachment& a : attachments_) {
		std::string error;
		const PartMesh* pm = parts_.get(a.part, error);
		if (!pm) {
			partMaterial_.push_back(-1);
			say(error, true);
			continue;
		}
		partMaterial_.push_back(int(model_.materials.size()));
		const std::string label = partLabel(a.part);
		for (std::size_t i = 0; i < pm->mesh.materials.size(); ++i) {
			ImpMaterial mat = pm->mesh.materials[i];
			if (pm->mesh.materials.size() > 1) mat.name = label + " · " + (mat.name.empty() ? std::to_string(i + 1) : mat.name);
			else mat.name = label;
			model_.materials.push_back(std::move(mat));
			tailKeys_.push_back(partPrefix(a.uid) + std::to_string(i));
		}
	}
	exportOpt_.looks.resize(baseMaterials_);
	exportOpt_.noTint.resize(baseMaterials_, 0);
	for (const std::string& key : tailKeys_) {
		const auto it = tailMemory_.find(key);
		exportOpt_.looks.push_back(it != tailMemory_.end() ? it->second.first : MaterialLook());
		exportOpt_.noTint.push_back(it != tailMemory_.end() ? it->second.second : 0);
	}
}

// Detrás de buildRig: sin simplificar y rígidas con su articulación.
void App::appendAttachments()
{
	if (attachments_.empty() || rig_.verts.empty()) return;
	const std::array<AttachSite, P_COUNT> sites = attachSites(rig_, fit_);
	for (std::size_t i = 0; i < attachments_.size() && i < partMaterial_.size(); ++i) {
		if (partMaterial_[i] < 0) continue;
		const Attachment& a = attachments_[i];
		std::string error;
		const PartMesh* pm = parts_.get(a.part, error);
		if (pm) placePart(*pm, a, sites[std::size_t(std::clamp(a.point, 0, P_COUNT - 1))], partMaterial_[i], rig_);
	}
}

void App::partsChanged()
{
	layoutMaterials();
	uploadTextures();
	rebuild();
}

bool App::addAttachment(const std::string& part, int point)
{
	std::string error;
	if (!parts_.get(part, error)) {
		say(error, true);
		return false;
	}
	commitLooks(); // un retoque de color pendiente es su propio paso
	Attachment a;
	a.part  = part;
	a.point = std::clamp(point, 0, P_COUNT - 1);
	a.uid   = nextPartUid_++;
	for (const BuiltinPart& b : builtinParts())
		if (part == std::string("builtin:") + b.id && b.point == a.point) a.up = b.up;
	attachments_.push_back(a);
	partsChanged();
	commitLooks();
	say(std::string(L("Añadido: ", "Added: ")) + partLabel(part) + " (" + attachPointName(a.point) + ")");
	return true;
}

void App::removeAttachment(std::size_t index)
{
	if (index >= attachments_.size()) return;
	commitLooks();
	attachments_.erase(attachments_.begin() + std::ptrdiff_t(index));
	partsChanged();
	commitLooks();
}

EditState App::editState() const { return { exportOpt_.looks, attachments_ }; }

void App::applyEditState(const EditState& s)
{
	const bool changed = attachments_ != s.parts;
	if (changed) {
		attachments_ = s.parts;
		layoutMaterials();
	}
	// Mismas piezas que cuando se guardó: los mismos materiales en el mismo orden.
	if (s.looks.size() == exportOpt_.looks.size()) exportOpt_.looks = s.looks;
	if (changed) {
		uploadTextures();
		rebuild();
	}
}

void App::drawParts()
{
	ImGui::Dummy(ImVec2(0, 4));
	if (!ImGui::CollapsingHeader(L("Accesorios", "Accessories"))) return;
	hint(L("Sombreros, antenas y más: se enganchan en su sitio y se mueven con el personaje. Su color se cambia en Colores.",
	       "Hats, antennas and more: they snap into place and move with the character. Recolour them in Colours."));
	const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	int column       = 0;
	for (const BuiltinPart& b : builtinParts()) {
		ImGui::PushID(b.id);
		if (column++ % 2) ImGui::SameLine();
		if (ImGui::Button(L(b.es, b.en), ImVec2(half, 0))) addAttachment(std::string("builtin:") + b.id, b.point);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip(L("Añadir en: %s", "Add at: %s"), attachPointName(b.point));
		ImGui::PopID();
	}

	// Las del usuario: su carpeta, que se crea al abrirla.
	const std::string dir                 = partsDir();
	const std::vector<std::string>& files = parts_.userFiles(dir, ImGui::GetTime());
	ImGui::Spacing();
	ImGui::TextDisabled("%s", L("TUYOS", "YOUR OWN"));
	for (const std::string& f : files) {
		ImGui::PushID(f.c_str());
		if (ImGui::Button(fs::u8path(f).filename().u8string().c_str(), ImVec2(-1, 0))) addAttachment(f, P_HEADTOP);
		ImGui::PopID();
	}
	hint(files.empty() ? L("Pon archivos .glb, .gltf, .fbx u .obj en esta carpeta y saldrán aquí:",
	                       "Put .glb, .gltf, .fbx or .obj files in this folder and they show up here:")
	                   : L("De esta carpeta (van a la coronilla; luego se cambia):", "From this folder (they go on the head top; change it below):"));
	hint(dir.c_str());
	if (ImGui::Button(L("Abrir carpeta", "Open folder"))) {
		std::error_code ec;
		fs::create_directories(fs::u8path(dir), ec);
		if (ec || !openFolder(fs::u8path(dir))) say(std::string(L("No se pudo abrir la carpeta: ", "Could not open the folder: ")) + dir, true);
	}

	if (attachments_.empty()) return;
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::TextDisabled("%s", L("EN EL PERSONAJE", "ON THE CHARACTER"));
	std::size_t remove = attachments_.size();
	bool changed       = false;
	for (std::size_t i = 0; i < attachments_.size(); ++i) {
		Attachment& a = attachments_[i];
		ImGui::PushID(a.uid);
		ImGui::AlignTextToFramePadding();
		if (i < partMaterial_.size() && partMaterial_[i] < 0)
			ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s  %s", partLabel(a.part).c_str(), L("(no se encuentra)", "(missing)"));
		else
			ImGui::TextUnformatted(partLabel(a.part).c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 70);
		if (ImGui::Button(L("Quitar", "Remove"), ImVec2(70, 0))) remove = i;
		ImGui::SetNextItemWidth(-1);
		if (ImGui::BeginCombo("##point", attachPointName(a.point))) {
			for (int p = 0; p < P_COUNT; ++p)
				if (ImGui::Selectable(attachPointName(p), p == a.point) && p != a.point) {
					a.point = p;
					changed = true;
				}
			ImGui::EndCombo();
		}
		ImGui::SetNextItemWidth(-1);
		changed |= ImGui::SliderFloat("##size", &a.size, 0.25f, 3.0f, L("Tamaño %.2fx", "Size %.2fx"), ImGuiSliderFlags_Logarithmic);
		const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
		ImGui::SetNextItemWidth(w);
		changed |= ImGui::SliderFloat("##up", &a.up, -1.0f, 1.0f, L("Subir %+.2f", "Up %+.2f"));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1);
		changed |= ImGui::SliderFloat("##forward", &a.forward, -1.0f, 1.0f, L("Adelante %+.2f", "Forward %+.2f"));
		ImGui::PopID();
		ImGui::Spacing();
	}
	// Mover una pieza solo cambia sus triángulos; cuenta para deshacer al soltar.
	if (changed) {
		rigDirty_   = true;
		looksDirty_ = true;
	}
	if (remove < attachments_.size()) removeAttachment(remove);
}

json App::partsJson() const
{
	json arr = json::array();
	for (const Attachment& a : attachments_) {
		json o = { { "part", a.part }, { "point", attachPointId(a.point) }, { "size", a.size }, { "up", a.up }, { "forward", a.forward } };
		// Su color va con la pieza: no depende de que las de antes se abran.
		json looks = json::array(), noTint = json::array();
		const std::string prefix = partPrefix(a.uid);
		for (std::size_t k = 0; k < tailKeys_.size(); ++k) {
			const std::size_t m = baseMaterials_ + k;
			if (tailKeys_[k].rfind(prefix, 0) != 0 || m >= exportOpt_.looks.size()) continue;
			looks.push_back(lookJson(exportOpt_.looks[m]));
			noTint.push_back(m < exportOpt_.noTint.size() && exportOpt_.noTint[m] ? 1 : 0);
		}
		o["looks"]  = looks;
		o["noTint"] = noTint;
		arr.push_back(o);
	}
	return arr;
}

void App::restoreParts(const json& project)
{
	attachments_.clear();
	std::vector<const json*> saved;
	if (project.contains("parts") && project["parts"].is_array()) {
		for (const json& o : project["parts"]) {
			if (!o.is_object()) continue;
			Attachment a;
			a.part = o.value("part", std::string());
			if (a.part.empty()) continue;
			if (!isBuiltinPart(a.part)) {
				// Relativa: dentro del proyecto (un personaje que se vuelve a
				// editar); si falta, la de la carpeta de piezas con ese nombre.
				const fs::path p = fs::u8path(a.part);
				std::error_code ec;
				fs::path found;
				if (p.is_relative() && !restoreBase_.empty() && nectar::safeProjectPath(a.part) && fs::is_regular_file(fs::u8path(restoreBase_) / p, ec))
					found = fs::u8path(restoreBase_) / p;
				else if (p.is_absolute() && fs::is_regular_file(p, ec))
					found = p;
				else
					found = fs::u8path(partsDir()) / p.filename();
				a.part = found.u8string();
			}
			a.point   = attachPointFromId(o.value("point", std::string()));
			a.size    = std::clamp(o.value("size", 1.0f), 0.05f, 10.0f);
			a.up      = std::clamp(o.value("up", 0.0f), -5.0f, 5.0f);
			a.forward = std::clamp(o.value("forward", 0.0f), -5.0f, 5.0f);
			a.uid     = nextPartUid_++;
			attachments_.push_back(a);
			saved.push_back(&o);
		}
	}
	restoreBase_.clear();
	layoutMaterials();
	for (std::size_t i = 0; i < attachments_.size(); ++i) {
		const json& o            = *saved[i];
		const std::string prefix = partPrefix(attachments_[i].uid);
		std::size_t n            = 0;
		for (std::size_t k = 0; k < tailKeys_.size(); ++k) {
			const std::size_t m = baseMaterials_ + k;
			if (tailKeys_[k].rfind(prefix, 0) != 0 || m >= exportOpt_.looks.size()) continue;
			if (o.contains("looks") && o["looks"].is_array() && n < o["looks"].size()) exportOpt_.looks[m] = lookFromJson(o["looks"][n]);
			if (o.contains("noTint") && o["noTint"].is_array() && n < o["noTint"].size()) exportOpt_.noTint[m] = char(o["noTint"][n].get<int>() != 0);
			++n;
		}
	}
}

// Las piezas del usuario van con el personaje (parts/<n>/...), para que al
// volver a editarlo, aquí o en otro ordenador, sigan puestas.
void App::bundlePartFiles(json& project, std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out) const
{
	if (!project.contains("parts") || !project["parts"].is_array()) return;
	json& arr = project["parts"];
	std::map<std::string, std::string> bundled; // archivo → ruta en el proyecto
	std::size_t total = 0;
	for (std::size_t i = 0; i < attachments_.size() && i < arr.size(); ++i) {
		const Attachment& a = attachments_[i];
		if (isBuiltinPart(a.part)) continue;
		const auto done = bundled.find(a.part);
		if (done != bundled.end()) {
			arr[i]["part"] = done->second;
			continue;
		}
		const PartMesh* pm = parts_.find(a.part);
		if (!pm) continue; // no se abre: se queda con su ruta
		std::vector<std::string> files = pm->mesh.files;
		if (std::find(files.begin(), files.end(), a.part) == files.end()) files.insert(files.begin(), a.part);
		if (pm->mesh.format == "OBJ") keepObjFiles(a.part, files);
		const fs::path base   = fs::u8path(a.part).parent_path();
		const std::string dir = "parts/" + std::to_string(i) + "/";
		std::string mainRel;
		for (const std::string& file : files) {
			std::error_code ec;
			std::string rel = fs::relative(fs::u8path(file), base, ec).generic_u8string();
			if (ec || rel.empty() || rel.rfind("..", 0) == 0) rel = fs::u8path(file).filename().u8string();
			rel = dir + rel;
			if (!nectar::safeProjectPath(rel)) continue;
			std::ifstream f(fs::u8path(file), std::ios::binary);
			std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)), {});
			if (data.empty()) continue;
			total += data.size();
			if (total > 64u * 1024u * 1024u) break; // piezas enormes: se quedan con su ruta
			if (file == a.part) mainRel = rel;
			out.push_back({ rel, std::move(data) });
		}
		if (!mainRel.empty()) {
			arr[i]["part"]   = mainRel;
			bundled[a.part] = mainRel;
		}
	}
}

} // namespace mm
