#pragma once

// Ventana del creador de modelos: un asistente de cinco pasos a la izquierda
// (modelo, colocar, esqueleto, animaciones, guardar) y la vista 3D con Olimar
// al lado.

#include "config.h"
#include "export_nhm.h"
#include "import.h"
#include "olimar.h"
#include "accessories.h"
#include "paint.h"
#include "rig.h"
#include "view.h"

#include "imgui.h"
#include "nectar_assets/charpack.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace mm {

/// Lo que se puede deshacer, aparte de la pintura: los retoques de color (por
/// material), las piezas del modelo movidas (por material del modelo) y los
/// accesorios (con su propio retoque).
struct EditSnapshot {
	std::vector<MaterialLook> looks;
	std::vector<PartEdit> parts;
	std::vector<Attachment> accessories;
	bool operator==(const EditSnapshot& o) const { return looks == o.looks && parts == o.parts && accessories == o.accessories; }
	bool operator!=(const EditSnapshot& o) const { return !(*this == o); }
};

/// Un paso de deshacer o rehacer: cómo quedan los retoques y, si fue un trazo
/// o un relleno, los trozos de capa que cambió (se intercambian con la capa).
struct EditStep {
	EditSnapshot edits;
	std::vector<PaintPatch> paint;
};

/// Un retoque de pieza en el proyecto (part_edit_ui.cpp).
nlohmann::json partEditJson(const PartEdit& e);
PartEdit partEditFromJson(const nlohmann::json& o);

class App {
public:
	bool init(const std::string& openPath, std::string& error);
	void frame();
	void dropFile(const std::string& path);
	bool wantsQuit() const { return quit_; }
	void requestQuit() { quit_ = true; }
	~App();

private:
	// Carpeta del juego y Olimar.
	nm::Config cfg_;
	std::string assetsDir_;
	bool gameReady_ = false;
	OlimarRig olimar_;
	std::vector<Anim> anims_;
	std::vector<int> animOrder_; ///< índices de anims_: primero las conocidas

	// El modelo (y el original, para simplificarlo otra vez).
	bool hasModel_ = false;
	ImpModel model_, source_;
	int targetTris_ = 30000;

	// Abrir y simplificar va en otro hilo (un modelo de 2 millones de
	// triángulos tarda medio minuto).
	std::thread worker_;
	std::atomic<bool> working_ { false }, workDone_ { false };
	ImpModel pending_, pendingSource_;
	std::string pendingError_, pendingPath_;
	bool pendingRestore_ = false, pendingResimplify_ = false;
	FitSettings fit_;
	RigResult rig_;
	ExportOptions exportOpt_;
	ExportResult export_;
	bool rigDirty_ = false, exportDirty_ = false;
	bool looksDirty_ = false; ///< retoques de color: volver a subir las texturas al soltar el ratón
	bool partsDirty_ = false; ///< piezas o accesorios cambiados: guardar el paso al soltar el ratón
	// Deshacer (EditStep): los retoques de color, las piezas, los accesorios y
	// la pintura en una sola historia. Un cambio cuenta al soltar el ratón (un
	// arrastre entero es un solo paso).
	std::vector<EditStep> editUndo_, editRedo_;
	EditSnapshot editCommitted_;
	double lastRebuild_ = 0;
	std::vector<std::size_t> order_; ///< triángulos ordenados por material

	// Accesorios (accessories_ui.cpp). Los materiales de model_ son los del
	// modelo [0, baseMaterials_), el tallo de los Pikmin y los de cada
	// accesorio. Cada uno tiene una clave: los del modelo, su índice (no se
	// mueven); los de detrás, la de tailKeys_.
	std::vector<Attachment> attachments_;
	PartLibrary accessoryLibrary_;
	std::size_t baseMaterials_ = 0;
	std::vector<std::string> tailKeys_;  ///< quién es cada material añadido ("stem", "part<uid>.<i>")
	std::map<std::string, std::pair<MaterialLook, char>> tailMemory_; ///< su color y tinte, aunque se quite
	std::vector<int> accessoryMaterial_; ///< primer material de cada accesorio (-1: no se pudo abrir)
	int nextAccessoryUid_ = 1;
	std::string restoreDir_; ///< carpeta del proyecto que se abre (rutas relativas de la pintura y los accesorios)
	bool stemMaterial(ImpMaterial& stem) const;
	void modelReplaced(bool keepAccessories);
	void layoutMaterials();
	void appendAttachments();
	bool accessoryMaterials(int uid, int& first, int& end) const;
	void accessoriesChanged();
	bool addAttachment(const std::string& part, int point);
	void removeAttachment(std::size_t index);
	std::string accessoriesDir() const;
	void drawAccessories();
	nlohmann::json accessoriesJson() const;
	void restoreAccessories(const nlohmann::json& project);
	void bundleAccessoryFiles(nlohmann::json& project, std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out) const;

	// Vista.
	View view_;
	OrbitCamera cam_;
	nm::M4 viewProj_;
	ImVec2 viewOrigin_, viewSize_;
	DynamicMesh modelMesh_, olimarMesh_;
	// Aspecto del juego (paso de animaciones): la luz de un escenario y las
	// piezas del pack tal como se instalan, en lugar del modelo importado.
	bool gameLook_ = false, smoothLighting_ = false, gameLookLoaded_ = false;
	GameLook gameLookData_;
	DynamicMesh exportMesh_;
	std::vector<GLuint> exportTex_;
	bool exportTexDirty_ = true;
	bool gameLookActive() const { return gameLook_ && gameLookLoaded_ && editorStep_ == 3 && hasModel_; }
	std::vector<GLuint> modelTex_, olimarTex_;
	std::vector<char> modelTexBaked_; ///< por material: su textura ya lleva el color y el retoque
	GLuint startPortrait_[4] = {}; ///< Olimar y los tres Pikmin, renderizados desde los assets del juego
	int anim_      = -1; ///< -1: pose de reposo, con los puntos para editar
	float frame_   = 0;
	bool playing_  = true;
	float speed_   = 1.0f;
	int olimarMode_ = 1; ///< 0 oculto, 1 al lado, 2 encima (transparente)
	bool showSkeleton_ = true, showWeights_ = false;
	int dragMarker_ = -1, hoverMarker_ = -1;
	bool orbiting_  = false, panning_ = false;
	bool showStart_ = true;
	int editorStep_ = 0; ///< modelo, colocar, esqueleto, animar, comprobar, publicar

	// A quién reemplaza el modelo: un capitán (Olimar o un personaje propio)
	// o los Pikmin de un color (o los tres, teñidos por el juego).
	enum Target { T_CAPTAIN, T_RED, T_YELLOW, T_BLUE, T_ALL };
	int target_ = T_CAPTAIN;
	LeafModel leaves_[3]; ///< hoja, capullo, flor (los pone el juego en la punta del tallo)
	std::vector<GLuint> leafTex_[3];
	int leafKind_ = 0;
	DynamicMesh leafMesh_;
	std::vector<nectar::CharacterInfo> installedPikmin_;
	bool isPikmin() const { return target_ != T_CAPTAIN; }
	bool loadReference(std::string& error);
	void setTarget(int target);
	void ensureStemMaterial();
	void speciesColour(float out[3]) const;
	void appendLeaf(DynamicMesh& mesh, const Mat34& world, float offsetX, const float tint[3]);
	bool buildStartPortraits(std::string& error);
	void useNowInGame(const nectar::CharacterInfo& info);

	// Personaje propio: su nombre en el selector de capitán, el autor y los
	// que ya hay instalados en el juego (Load/Characters).
	char charName_[41]   = {};
	char charAuthor_[41] = {};
	std::vector<nectar::CharacterInfo> installed_;
	std::string removeId_; ///< el que se va a quitar (pide confirmación)
	// Volver a editar uno ya creado: el id con el que se reinstala (aunque
	// cambie el nombre) y los ajustes de su proyecto para cuando acabe de abrir.
	std::string editingId_, editingName_;
	bool editingOlimar_ = false;
	std::string restoreJson_;
	std::vector<nectar::CharacterInfo> creations_; ///< capitanes y Pikmin instalados (para editar)

	std::string status_;
	bool statusError_ = false;
	bool quit_        = false;
	bool askGameDir_  = false;

	bool setupGame(std::string& error);
	void loadModel(const std::string& path, bool restoreProject);
	void resimplify();
	void finishLoad();
	void rebuild();
	void uploadTextures();
	void fillMeshes();
	void fillModelMesh(DynamicMesh& mesh, const std::vector<Mat34>* skin, bool unbent, bool weights);
	void fillExportMesh(const std::vector<Mat34>* skin);
	void resetCamera();

	void drawStartScreen(float width, float height);
	void drawTopBar(float width);
	void drawWorkflow(float x, float y, float width, float height);
	void drawPanel(float x, float y, float width, float height);
	void drawColours();
	EditSnapshot edits() const { return { exportOpt_.looks, fit_.parts, attachments_ }; }
	void commitEdits();
	void pushEditStep(EditStep step);
	void undoEdits(bool redo);
	void resetEditHistory();
	void undoShortcuts();
	void drawUndoButtons();

	// Piezas (part_edit_ui.cpp): elegir un material del modelo o un accesorio
	// y moverlo con el gizmo.
	int selectedPart_      = -1; ///< material del modelo elegido
	int selectedAccessory_ = 0;  ///< o el uid del accesorio elegido
	int gizmoOp_      = 0; ///< 0 mover, 1 girar, 2 escalar
	int partAnimBefore_ = -1; ///< la animación que había al elegir una pieza
	bool partSelected() const { return selectedPart_ >= 0 || selectedAccessory_ != 0; }
	void drawParts();
	void selectPart(int material, int accessory);
	void selectMaterial(int material);
	PartEdit* selectedEdit();
	bool selectedMaterials(int& first, int& end) const;
	bool pressOnGizmo_ = false;
	bool drawPartGizmo(bool hovered); ///< true si el ratón es del gizmo; un clic en la vista elige pieza
	int pickPart(const ImVec2& mouse) const;
	/// [1]: para las copias reflejadas; [first, end): los materiales que mueve.
	bool partPreview(Mat34 move[2], Mat34 normal[2], int& first, int& end);
	nlohmann::json partEditsJson() const;
	void restorePartEdits(const nlohmann::json& j);

	// Pintar sobre el modelo (paint_ui.cpp).
	PaintState paint_;
	void drawPaint();
	bool paintMouse(bool hovered);
	bool paintable(int material);
	bool needsPaintSetup(int material);
	bool fillable(int material);
	void preparePaint(int material);
	void finishPreparePaint(int material);
	void fillPaint(int material, bool erase);
	void endStroke();
	void updatePaintTextures();
	GLuint paintedTexture(std::size_t material);
	void resetPaint();
	void readPaintSetups(bool restoreProject);
	void applyPaintSetups(ImpModel& model);
	void restorePaint(const nlohmann::json& j);
	void paintJson(nlohmann::json& j, const std::string& dir) const;
	void paintFiles(const std::string& dir, std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out) const;
	void drawView(float x, float y, float width, float height);
	void drawOverlay(const std::vector<Mat34>& skin);
	bool project(const Vec3& p, ImVec2& out) const;
	void dragMarker(const ImVec2& mouse);

	std::string customDir() const;
	std::string projectPath() const;
	void saveProject() const;
	nlohmann::json projectJson(const std::string& source, const std::string& paintDir) const;
	/// El proyecto para guardarlo con el personaje: project.json y una copia
	/// del modelo original con todo lo que abrió (texturas, .bin, .mtl).
	void projectFiles(std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& out) const;
	void editCreation(const std::string& dir, const nectar::CharacterInfo& info, bool olimar);
	void openPackForEdit(const std::string& path);
	void drawCreations();
	bool loadProject(std::string& source, int& target);

	bool characterFiles(nectar::CharacterFiles& files, std::string& error);
	bool renderPortrait(std::vector<std::uint8_t>& png);
	void installAsCharacter();
	void exportCharacter();
	void installPack(const std::string& path);
	void refreshInstalled();
	void drawCharacters();
	void say(const std::string& text, bool error = false);
};

} // namespace mm
