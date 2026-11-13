// UI gallery (#215): a story is a fixture (a hub state, a modal, or a single
// component) drawn by the real launcher code (launcher_gui.cpp) without a
// window. A Session holds one story at one viewport and runs frames on it;
// the gallery window, the capture/compare run and the layout lint all use it.
#pragma once

#include "launcher_gui.h"
#include "soft_raster.h"

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct ImGuiContext;

namespace pikmin::launcher::gallery {

enum class InputMode { None, Hover, Focus, Touch };
const char* inputModeName(InputMode mode);

// Window size in points and the display scale on top (2 = Retina / 200 %).
// Phone viewports are pixels, laid out like the Android launcher: the UI is
// designed for 1180x780 and scaled up to fill the screen.
struct Viewport {
    std::string name;
    int width = 1180, height = 780;
    float dpi = 1.0f;
    bool phone = false;
    float pixelsPerDp = 1.0f; // phone: physical density, for touch-target size
};

// One item on screen, in UI points (ImGui coordinates).
struct Widget {
    enum class Kind { Clickable, Label, Toast, Notice, Logo, Path };
    Kind kind = Kind::Clickable;
    unsigned id = 0;     // ImGui id (clickables)
    std::string label;   // clickables: ImGui label; others: the text
    std::string layer;   // root ImGui window ("##hub", a modal...)
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0; // clip rect in effect
    bool disabled = false;
    bool scrolls = false; // inside a window that can scroll (reachable when clipped)
    std::string shown;    // Label/Toast/Path: what was drawn
    bool truncated = false, shrunk = false;
    void* window = nullptr; // ImGuiWindow*, for focus
};

// Lets component stories call the launcher's own widgets.
class ComponentCanvas {
public:
    virtual ~ComponentCanvas() = default;
    virtual ImVec2 size() const = 0;
    virtual void at(float x, float y) = 0; // next widget's top-left corner
    virtual bool button(const char* label, float width, float height, bool selected = false, bool disabled = false) = 0;
    // Standard width 0..3 (Small, Medium, Large, ExtraLarge).
    virtual bool standardButton(const char* label, int size, float height, bool selected = false, bool disabled = false) = 0;
    virtual void tabs(float y) = 0;
    virtual void caption(float x, float y, const char* text) = 0;
};

struct Fixture {
    enum class Screen { Hub, Component };
    enum class Modal { None, Paths, Progress, Message };
    Screen screen = Screen::Hub;
    HubState state;
    int tab = 0; // 0 Games, 1 Settings, 2 Move Install
    int settingsGame = 0, settingsGroup = 0;
    // Per game: the game's --settings-dump JSON. Empty and no error = no
    // settings client (game not installed). `settingsLoading` = still asking.
    std::string settingsDump[kHubGameCount];
    std::string settingsError[kHubGameCount];
    bool settingsLoading[kHubGameCount] = { false, false };
    bool launcherPage = false; // Settings > Launcher
    SettingsFolderInfo launcherFolder;
    std::string launcherMessage;
    bool launcherMessageIsError = false;
    // Release check finished with a newer release.
    bool releaseNewer = false;
    std::string releaseVersion, releaseNotes;
    bool whatsNewOpen = false;
    std::string toast; // stays fully visible while the story shows
    float toastTime = 3.5f;
    std::string moveTarget, moveNote;
    // Installer modals (HubWindow::Impl).
    Modal modal = Modal::None;
    int installGame = 0;
    std::string rom, installDirectory, legacySave;
    unsigned percent = 0;
    std::string phase = "Extracting", currentFile;
    std::string messageTitle, messageText, primary, secondary, cancel;
    bool messageIsError = false;
    std::string dialogWaiting; // "Waiting for the ..." veil
    // Component stories: drawn on the hub background, inside the hub window.
    std::function<void(ComponentCanvas&)> component;
    // Hover / focus target: first clickable whose label contains this.
    std::string target;
};

// A story setting the gallery window shows as an editable control (like
// Storybook's args). Captures always use the defaults.
struct Arg {
    enum class Kind { Bool, Text, Number, Choice };
    std::string name;
    Kind kind = Kind::Text;
    bool flag = false;
    std::string text;
    float number = 0.0f, min = 0.0f, max = 100.0f, step = 1.0f;
    int choice = 0;
    std::vector<std::string> choices;
};
using Args = std::vector<Arg>;

Arg boolArg(std::string name, bool value);
Arg textArg(std::string name, std::string value);
Arg numberArg(std::string name, float value, float min, float max, float step = 1.0f);
Arg choiceArg(std::string name, std::vector<std::string> choices, int value);
// Values by name; a missing name gives the type's zero.
bool argFlag(const Args& args, const std::string& name);
const std::string& argText(const Args& args, const std::string& name);
float argNumber(const Args& args, const std::string& name);
int argChoice(const Args& args, const std::string& name);
bool sameArgs(const Args& a, const Args& b);

struct Story {
    std::string name;  // "hub/update-notice"; also the file name
    std::string group; // "Screens", "Settings", "Dialogs", "Components"
    std::string summary;
    std::function<Fixture()> make; // with the default args
    // Components: fixed canvas in points, drawn at 1x and 2x only.
    int canvasW = 0, canvasH = 0;
    Args args;                                   // defaults; empty = no controls
    std::function<Fixture(const Args&)> makeWith; // set when `args` is not empty
};

// The story's fixture, with `args` when given (else the defaults).
Fixture makeFixture(const Story& story, const Args* args = nullptr);

const std::vector<Story>& stories();
const Story* findStory(const std::string& name);

struct LowRes {
    std::string what;     // "logo", "button art", "font atlas"...
    float texelsPerPixel = 1.0f;
    int texW = 0, texH = 0;
};

class Session {
public:
    // `args`: the story's controls as edited in the window (nullptr = defaults).
    Session(const Story& story, const Viewport& viewport, const Args* args = nullptr);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    ImVec2 uiSize() const { return mUiSize; }           // points the hub lays out in
    float framebufferScale() const { return mFbScale; }  // pixels per UI point
    const Viewport& viewport() const { return mViewport; }

    // Input, in UI points. Position (-1, -1) = no pointer.
    void setInputMode(InputMode mode);
    void pointer(float x, float y);
    void button(bool down); // left button; a release never clicks (see .inl)
    void wheel(float dy);
    void key(ImGuiKey key, bool down);
    void focus(const Widget& widget);

    // One frame, `dt` seconds after the last.
    void frame(float dt);
    // Frames until animations settle (deterministic clock).
    void settle(int frames = 90);

    const std::vector<Widget>& widgets() const { return mWidgets; }
    ImGuiMouseCursor cursor() const { return mCursor; }
    unsigned hovered() const { return mHovered; }
    unsigned focused() const { return mFocused; }
    // First enabled clickable to hover or focus (fixture target, else topmost layer).
    const Widget* target() const;

    // Pixels of the last frame. `lowRes` gets textures drawn upscaled.
    Image raster(std::vector<LowRes>* lowRes = nullptr);

    // Internal: ImGui test-engine and probe hooks report here.
    void itemAdd(unsigned id, float x0, float y0, float x1, float y1, void* window, bool disabled);
    void itemInfo(unsigned id, const char* label);
    void probeReport(int kind, float x0, float y0, float x1, float y1, const std::string& text, const std::string& shown,
                     bool truncated, bool shrunk);

private:
    struct State;
    void applyScale();
    void drawComponent();
    void collect();

    std::unique_ptr<State> mState;
    Viewport mViewport;
    ImVec2 mUiSize { 0, 0 };
    float mFbScale = 1.0f;
    float mTime = 0.0f;
    std::vector<Widget> mWidgets, mPending;
    ImGuiMouseCursor mCursor = ImGuiMouseCursor_Arrow;
    unsigned mHovered = 0, mFocused = 0;
};

// ImGui's test-engine hooks (imgui_internal.h) forward here while a session runs.
Session*& activeSession();

// The gallery has no GL context: a texture call that would fall through to
// OpenGL is a bug (it crashes on macOS), so it stops the run instead.
[[noreturn]] void noOpenGl(const char* where);

} // namespace pikmin::launcher::gallery
