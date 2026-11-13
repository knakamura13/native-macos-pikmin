// UI gallery session (#215). Included at the end of launcher_gui.cpp, inside
// namespace pikmin::launcher, only when PIKMIN_UI_GALLERY is set: stories
// reach the hub (anonymous namespace above) and HubWindow::Impl directly, so
// every pixel comes from the launcher's own drawing code.
//
// Not reproduced: the __ANDROID__ branches of launcher_gui.cpp (the gallery
// is a desktop build; phone viewports use the Android scaling rule on the
// desktop layout), the cover art and slideshow photos (downloaded at run time)
// and the native file pickers.

namespace gallery {

const char* inputModeName(InputMode mode)
{
    switch (mode) {
    case InputMode::None: return "none";
    case InputMode::Hover: return "hover";
    case InputMode::Focus: return "focus";
    case InputMode::Touch: return "touch";
    }
    return "none";
}

Session*& activeSession()
{
    static Session* session = nullptr;
    return session;
}

namespace {

void probeSink(const probe::Report& r)
{
    if (Session* s = activeSession()) s->probeReport(int(r.kind), r.x0, r.y0, r.x1, r.y1, r.text, r.shown, r.truncated, r.shrunk);
}

// Launcher widgets for component stories, drawn by the hub itself.
class HubCanvas : public ComponentCanvas {
public:
    HubCanvas(Hub& hub, ImVec2 size) : mHub(hub), mSize(size) {}
    ImVec2 size() const override { return mSize; }
    void at(float x, float y) override { ImGui::SetCursorScreenPos(ImVec2(x, y)); }
    bool button(const char* label, float width, float height, bool selected, bool disabled) override
    {
        return mHub.glassButton(label, ImVec2(width, height), selected, disabled);
    }
    bool standardButton(const char* label, int size, float height, bool selected, bool disabled) override
    {
        return mHub.glassButton(label, buttons::kAllSizes[std::clamp(size, 0, buttons::kButtonSizeCount - 1)], height,
                                selected, disabled);
    }
    void tabs(float y) override { mHub.drawTabs(y, mSize.x); }
    void caption(float x, float y, const char* text) override
    {
        ImGui::GetWindowDrawList()->AddText(mHub.fonts.caption, 16.0f, ImVec2(x, y), col(kOrange), text);
    }

private:
    Hub& mHub;
    ImVec2 mSize;
};

} // namespace

struct Session::State {
    SoftRenderer renderer;
    ImGuiContext* ctx = nullptr;
    ImGuiContext* previous = nullptr;
    HubWindow window; // only its Impl is used; never opened
    Fixture fixture;
    unsigned fontTexture = 0;
    float fontDensity = 0.0f;
    float pointerX = -1, pointerY = -1;

    HubWindow::Impl& impl() { return *window.mImpl; }
    Hub& hub() { return *window.mImpl->hub; }

    void buildFontTexture()
    {
        ImGuiIO& io = ImGui::GetIO();
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        if (fontTexture) renderer.destroy(fontTexture);
        fontTexture = renderer.create(pixels, w, h);
        io.Fonts->SetTexID(ImTextureID(fontTexture));
    }
};

Session::Session(const Story& story, const Viewport& viewport, const Args* args) : mState(new State), mViewport(viewport)
{
    State& s = *mState;
    s.fixture = makeFixture(story, args);
    if (story.canvasW > 0 && !viewport.phone) {
        // Components draw on a canvas of their own size at the viewport's scale.
        mViewport.width = story.canvasW;
        mViewport.height = story.canvasH;
    }
    // Every texture this session's hub makes or frees goes to its own CPU
    // renderer: here, in frame() and in the destructor (input lint runs other
    // sessions while this one is alive).
    ScopedTextureBackend textures(s.renderer);
    s.previous = ImGui::GetCurrentContext();
    s.ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(s.ctx);
    s.ctx->TestEngineHookItems = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    applyTheme();

    HubWindow::Impl& m = s.impl();
    const Fixture& f = s.fixture;
    m.state = f.state;
    if (f.launcherPage) {
        const SettingsFolderInfo info = f.launcherFolder;
        m.state.settingsFolder = [info] { return info; };
        m.state.setKeepSettingsInGameFolder = [](bool, std::string& message) {
            message = "The gallery does not change settings.";
            return false;
        };
    }
    applyScale(); // font density before the hub loads its fonts
    gFontDensity = mState->fontDensity;
    m.createHub();
    s.buildFontTexture();
    Hub& hub = s.hub();
    // A dialog asked for from inside a frame shows as the modal of the next
    // one instead of running its own loop.
    hub.askModal = [&m](const std::string& title, const std::string& text, const std::string& primary,
                        const std::string& secondary, bool isError, const std::string& cancel) {
        m.modal = HubWindow::Impl::Modal::Message;
        m.messageTitle = title;
        m.messageText = text;
        m.primaryLabel = primary;
        m.secondaryLabel = secondary;
        m.messageIsError = isError;
        m.cancelLabel = cancel;
        return -1;
    };

    hub.tab = Tab(std::clamp(f.tab, 0, 2));
    hub.settingsGame = f.settingsGame;
    hub.settingsGroup = f.settingsGroup;
    for (int g = 0; g < kHubGameCount; ++g) {
        if (f.settingsDump[g].empty() && f.settingsError[g].empty() && !f.settingsLoading[g]) continue;
        auto client = std::make_shared<SettingsClient>();
        // Never run: the gallery sends no requests (clicks do nothing).
        client->game = "nectar-ui-gallery-no-game";
        client->directory = m.state.games[g].directory;
        if (!f.settingsDump[g].empty()) {
            std::string error;
            client->loaded = parseSettingsDump(f.settingsDump[g], client->snapshot, error);
            if (!client->loaded) client->error = "Story settings did not parse: " + error;
        }
        if (!f.settingsError[g].empty()) client->error = f.settingsError[g];
        if (f.settingsLoading[g]) client->busy = true;
        hub.settingsClients[g] = client;
    }
    hub.launcherMessage = f.launcherMessage;
    hub.launcherMessageIsError = f.launcherMessageIsError;
    if (f.releaseNewer) {
        hub.releaseCheck = std::make_shared<ReleaseCheck>();
        hub.releaseCheck->latest.version = f.releaseVersion;
        hub.releaseCheck->latest.notes = f.releaseNotes;
        hub.releaseCheck->newer = true;
        hub.releaseCheck->done = true;
    }
    if (f.whatsNewOpen) {
        hub.whatsNewOpen = true;
        hub.whatsNewVersion = f.releaseVersion;
        hub.whatsNewNotes = f.releaseNotes;
    }
    if (!f.toast.empty()) hub.notify(f.toast);
    hub.moveTarget = f.moveTarget;
    hub.moveNote = f.moveNote;

    m.installGame = HubGame(std::clamp(f.installGame, 0, kHubGameCount - 1));
    m.rom = f.rom;
    m.installDirectory = f.installDirectory;
    // Empty: looked up on disk, where the gallery's sandboxed home has none.
    m.pathsLegacyOverride = f.legacySave;
    m.percent = f.percent;
    m.phase = f.phase;
    m.currentFile = f.currentFile;
    m.messageTitle = f.messageTitle;
    m.messageText = f.messageText;
    m.primaryLabel = f.primary;
    m.secondaryLabel = f.secondary;
    m.cancelLabel = f.cancel;
    m.messageIsError = f.messageIsError;
    m.dialogWaiting = f.dialogWaiting;
    switch (f.modal) {
    case Fixture::Modal::None: m.modal = HubWindow::Impl::Modal::None; break;
    case Fixture::Modal::Paths: m.modal = HubWindow::Impl::Modal::Paths; break;
    case Fixture::Modal::Progress: m.modal = HubWindow::Impl::Modal::Progress; break;
    case Fixture::Modal::Message: m.modal = HubWindow::Impl::Modal::Message; break;
    }
    ImGui::SetCurrentContext(s.previous);
}

Session::~Session()
{
    State& s = *mState;
    ScopedTextureBackend textures(s.renderer);
    ImGui::SetCurrentContext(s.ctx);
    HubWindow::Impl& m = s.impl();
    if (m.hub) {
        m.hub->release();
        m.hub.reset();
    }
    ImGui::DestroyContext(s.ctx);
    ImGui::SetCurrentContext(s.previous == s.ctx ? nullptr : s.previous);
}

[[noreturn]] void noOpenGl(const char* where)
{
    std::fprintf(stderr, "ui_gallery: %s reached OpenGL: no CPU texture backend is active\n", where);
    std::abort();
}

void Session::applyScale()
{
    State& s = *mState;
    const float w = float(mViewport.width), h = float(mViewport.height);
    float density;
    if (mViewport.phone) {
        // launcher_gui.cpp, Android: the 1180x780 design scaled to the screen,
        // fonts rasterized at that scale (pixels = points on Android).
        gUiScale = std::max(1.0f, std::min(std::min(w / 1180.0f, h / 780.0f), 4.0f));
        density = gUiScale;
        mFbScale = gUiScale;
    } else {
        // updateDesktopScale(): the UI grows with the window; the display
        // scale (Retina, 200 %) comes on top.
        gUiScale = desktopUiScale(w, h);
        density = fontDensityFor(gUiScale, mViewport.dpi);
        mFbScale = gUiScale * mViewport.dpi;
    }
    mUiSize = ImVec2(w / gUiScale, h / gUiScale);
    if (density != s.fontDensity) {
        s.fontDensity = density;
        gFontDensity = density;
        if (s.impl().hub) {
            ImGui::GetIO().Fonts->Clear();
            loadFonts(s.hub().fonts);
            s.buildFontTexture();
        }
    }
}

void Session::setInputMode(InputMode mode)
{
    ImGui::SetCurrentContext(mState->ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseSourceEvent(mode == InputMode::Touch ? ImGuiMouseSource_TouchScreen : ImGuiMouseSource_Mouse);
    if (mode == InputMode::Focus) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    else io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
    if (mode != InputMode::Hover) pointer(-1, -1);
    ImGui::SetCurrentContext(mState->previous);
}

void Session::pointer(float x, float y)
{
    ImGui::SetCurrentContext(mState->ctx);
    mState->pointerX = x;
    mState->pointerY = y;
    if (x < 0 || y < 0) ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    else ImGui::GetIO().AddMousePosEvent(x, y);
    ImGui::SetCurrentContext(mState->previous);
}

void Session::button(bool down)
{
    ImGui::SetCurrentContext(mState->ctx);
    ImGuiIO& io = ImGui::GetIO();
    if (down) {
        io.AddMouseButtonEvent(0, true);
    } else {
        // The pointer leaves before the release, so buttons show their pressed
        // look but never fire: a story cannot install, delete or run anything.
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        io.AddMouseButtonEvent(0, false);
        if (mState->pointerX >= 0) io.AddMousePosEvent(mState->pointerX, mState->pointerY);
    }
    ImGui::SetCurrentContext(mState->previous);
}

void Session::wheel(float dy)
{
    ImGui::SetCurrentContext(mState->ctx);
    ImGui::GetIO().AddMouseWheelEvent(0.0f, dy);
    ImGui::SetCurrentContext(mState->previous);
}

void Session::key(ImGuiKey key, bool down)
{
    // Moving focus only: Enter, Space and the gamepad's A would press the
    // focused button for real.
    if (key == ImGuiKey_Enter || key == ImGuiKey_KeypadEnter || key == ImGuiKey_Space || key == ImGuiKey_GamepadFaceDown)
        return;
    ImGui::SetCurrentContext(mState->ctx);
    ImGui::GetIO().AddKeyEvent(key, down);
    ImGui::SetCurrentContext(mState->previous);
}

void Session::focus(const Widget& widget)
{
    if (!widget.id || !widget.window) return;
    ImGui::SetCurrentContext(mState->ctx);
    ImGuiContext& g = *GImGui;
    ImGui::SetFocusID(widget.id, static_cast<ImGuiWindow*>(widget.window));
    g.NavCursorVisible = true;
    g.NavHighlightItemUnderNav = true;
    ImGui::SetCurrentContext(mState->previous);
}

void Session::frame(float dt)
{
    State& s = *mState;
    ScopedTextureBackend textures(s.renderer);
    ImGui::SetCurrentContext(s.ctx);
    activeSession() = this;
    probe::sink() = &probeSink;
    applyScale();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = mUiSize;
    io.DisplayFramebufferScale = ImVec2(mFbScale, mFbScale);
    io.DeltaTime = std::max(dt, 1e-4f);
    mTime += dt;
    Hub& hub = s.hub();
    if (!s.fixture.toast.empty()) hub.toastTime = s.fixture.toastTime; // the story keeps it on screen
    mPending.clear();
    ImGui::NewFrame();
    if (s.fixture.screen == Fixture::Screen::Component) drawComponent();
    else s.impl().drawFrame(mUiSize, mTime, dt);
    ImGuiContext& g = *GImGui;
    mCursor = ImGui::GetMouseCursor();
    mHovered = g.HoveredId;
    mFocused = g.NavCursorVisible ? g.NavId : 0;
    ImGui::Render();
    collect();
    probe::sink() = nullptr;
    activeSession() = nullptr;
    ImGui::SetCurrentContext(s.previous);
}

void Session::settle(int frames)
{
    for (int i = 0; i < frames; ++i) frame(1.0f / 60.0f);
}

void Session::drawComponent()
{
    State& s = *mState;
    Hub& hub = s.hub();
    const ImVec2 size = mUiSize;
    hub.drawBackground(ImGui::GetBackgroundDrawList(), size, 1.0f / 60.0f);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##hub", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                     | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
    if (s.fixture.component) {
        HubCanvas canvas(hub, size);
        s.fixture.component(canvas);
    }
    if (s.fixture.releaseNewer) hub.drawReleaseNotice(size, mTime);
    ImGui::End();
    hub.drawToast(size);
}

void Session::itemAdd(unsigned id, float x0, float y0, float x1, float y1, void* window, bool disabled)
{
    if (ImGui::FindWindowByID(id)) return; // Begin() reports the window itself: not a widget
    ImGuiWindow* win = static_cast<ImGuiWindow*>(window);
    // Scrollbars are ImGui's own, and look and behave alike everywhere.
    // (GetWindowScrollbarID() hashes from the ID stack, which can be empty here;
    // Begin() draws scrollbars with only the window's own ID on it.)
    if (win && (id == ImHashStr("#SCROLLX", 0, win->ID) || id == ImHashStr("#SCROLLY", 0, win->ID))) return;
    Widget w;
    w.kind = Widget::Kind::Clickable;
    w.id = id;
    w.x0 = x0; w.y0 = y0; w.x1 = x1; w.y1 = y1;
    if (win) {
        w.cx0 = win->ClipRect.Min.x; w.cy0 = win->ClipRect.Min.y;
        w.cx1 = win->ClipRect.Max.x; w.cy1 = win->ClipRect.Max.y;
    }
    w.window = window;
    w.disabled = disabled;
    char fallback[24];
    std::snprintf(fallback, sizeof fallback, "#%08x", id);
    w.label = fallback;
    mPending.push_back(std::move(w));
}

void Session::itemInfo(unsigned id, const char* label)
{
    for (auto it = mPending.rbegin(); it != mPending.rend(); ++it) {
        if (it->kind != Widget::Kind::Clickable || it->id != id) continue;
        if (label) it->label = label;
        return;
    }
}

void Session::probeReport(int kind, float x0, float y0, float x1, float y1, const std::string& text, const std::string& shown,
                          bool truncated, bool shrunk)
{
    Widget w;
    switch (probe::Kind(kind)) {
    case probe::Kind::Label: w.kind = Widget::Kind::Label; break;
    case probe::Kind::Toast: w.kind = Widget::Kind::Toast; break;
    case probe::Kind::Notice: w.kind = Widget::Kind::Notice; break;
    case probe::Kind::Logo: w.kind = Widget::Kind::Logo; break;
    case probe::Kind::Path: w.kind = Widget::Kind::Path; break;
    }
    if (probe::Kind(kind) == probe::Kind::Path && text.empty()) return;
    w.x0 = x0; w.y0 = y0; w.x1 = x1; w.y1 = y1;
    w.label = text;
    w.shown = shown;
    w.truncated = truncated;
    w.shrunk = shrunk;
    if (ImGuiWindow* win = ImGui::GetCurrentWindowRead()) {
        w.window = win;
        w.cx0 = win->ClipRect.Min.x; w.cy0 = win->ClipRect.Min.y;
        w.cx1 = win->ClipRect.Max.x; w.cy1 = win->ClipRect.Max.y;
    }
    mPending.push_back(std::move(w));
}

void Session::collect()
{
    mWidgets = std::move(mPending);
    mPending.clear();
    ImGuiContext& g = *GImGui;
    const auto isChildRegion = [&](unsigned id) {
        // EndChild() adds the child's whole region as one item of the parent.
        for (const ImGuiWindow* x : g.Windows)
            if ((x->Flags & ImGuiWindowFlags_ChildWindow) && x->ChildId == id) return true;
        return false;
    };
    const auto isDebugWindow = [](const ImGuiWindow* win) {
        // ImGui's implicit "Debug" window lays out its title bar every frame
        // but is never drawn: those items are not the launcher's.
        return win && std::strncmp(win->RootWindow ? win->RootWindow->Name : win->Name, "Debug##Default", 14) == 0;
    };
    mWidgets.erase(std::remove_if(mWidgets.begin(), mWidgets.end(),
                                  [&](const Widget& w) {
                                      // (Toasts are drawn outside any window, so their probe
                                      // lands in the Debug window too: keep those.)
                                      if (w.kind != Widget::Kind::Clickable) return false;
                                      return isDebugWindow(static_cast<const ImGuiWindow*>(w.window)) || isChildRegion(w.id);
                                  }),
                   mWidgets.end());
    for (Widget& w : mWidgets) {
        const ImGuiWindow* win = static_cast<const ImGuiWindow*>(w.window);
        if (w.kind == Widget::Kind::Toast) {
            w.layer = "foreground"; // drawn over every window
            w.cx0 = 0; w.cy0 = 0; w.cx1 = mUiSize.x; w.cy1 = mUiSize.y;
            continue;
        }
        if (!win) continue;
        w.layer = win->RootWindow ? win->RootWindow->Name : win->Name;
        for (const ImGuiWindow* x = win; x; x = x->ParentWindow) {
            if (x->ScrollMax.y > 0.5f || x->ScrollMax.x > 0.5f) w.scrolls = true;
            if (x == x->RootWindow) break;
        }
    }
}

const Widget* Session::target() const
{
    const std::string& want = mState->fixture.target;
    const Widget* topmost = nullptr;
    for (const Widget& w : mWidgets) {
        if (w.kind != Widget::Kind::Clickable || w.disabled) continue;
        if (!want.empty() && w.label.find(want) != std::string::npos) return &w;
        topmost = &w;
    }
    if (!topmost) return nullptr;
    for (const Widget& w : mWidgets) // first clickable on the topmost layer
        if (w.kind == Widget::Kind::Clickable && !w.disabled && w.layer == topmost->layer) return &w;
    return topmost;
}

Image Session::raster(std::vector<LowRes>* lowRes)
{
    State& s = *mState;
    ImGui::SetCurrentContext(s.ctx);
    Image out;
    const float clear[4] = { 0.035f, 0.13f, 0.055f, 1.0f }; // as HubWindow::Impl::frame()
    std::vector<TextureUse> uses;
    s.renderer.render(ImGui::GetDrawData(), out, clear, lowRes ? &uses : nullptr);
    if (lowRes) {
        lowRes->clear();
        Hub& hub = s.hub();
        const auto name = [&](unsigned id) -> std::string {
            if (id == s.fontTexture) return "font atlas";
            if (id == hub.logo.id) return "logo";
            if (id == hub.dewdropSparkle.id) return "button sparkle";
            for (const auto& entry : hub.dewdropSets)
                for (const Texture& t : entry.second.textures)
                    if (t.id == id) return "button art";
            for (const auto& entry : hub.dewdropButtons)
                if (entry.second.blend.id == id) return "button art";
            for (const Texture& t : hub.covers)
                if (t.id == id) return "cover art";
            return "texture";
        };
        for (const TextureUse& u : uses) lowRes->push_back({ name(u.id), u.texelsPerPixel, u.texW, u.texH });
    }
    ImGui::SetCurrentContext(s.previous);
    return out;
}

} // namespace gallery
