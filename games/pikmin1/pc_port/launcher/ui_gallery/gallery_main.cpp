// ui_gallery (#215): Storybook-style browser for the launcher's screens and
// components, and the headless capture + compare + lint run CI uses.
//
//   ui_gallery                          window: pick a story, size, scale, input
//   ui_gallery --list                   story names
//   ui_gallery --capture OUT [options]  every story at every viewport, to PNGs
//       --references DIR       compare with DIR (thumbnails + layout), fail on change
//       --update-references    write the current pictures to DIR instead
//       --known-issues FILE    lint errors listed there do not fail (#214)
//       --story GLOB --viewport GLOB   only some
//       --no-lint --no-input-lint      skip the layout lint / pointer and focus checks
#include "capture.h"
#include "gallery_host.h"
#include "soft_raster.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"

#include <SDL2/SDL.h>

#include <csignal>
#if defined(__APPLE__) || defined(__GLIBC__)
#include <execinfo.h>
#include <unistd.h>
#define UI_GALLERY_BACKTRACE 1
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace pikmin::launcher::gallery;

namespace {

// Stories read nothing from the real home folder: no real saves, settings,
// Downloads or caches show up (or get touched) in a story.
void sandboxHome(const fs::path& base)
{
    std::error_code ec;
    fs::create_directories(base / "config", ec);
    fs::create_directories(base / "data", ec);
    fs::create_directories(base / "cache", ec);
    const std::string home = base.string();
#ifdef _WIN32
    _putenv_s("USERPROFILE", home.c_str());
    _putenv_s("APPDATA", (base / "config").string().c_str());
    _putenv_s("LOCALAPPDATA", (base / "data").string().c_str());
#else
    setenv("HOME", home.c_str(), 1);
    setenv("XDG_CONFIG_HOME", (base / "config").string().c_str(), 1);
    setenv("XDG_DATA_HOME", (base / "data").string().c_str(), 1);
    setenv("XDG_CACHE_HOME", (base / "cache").string().c_str(), 1);
    setenv("XDG_DOWNLOAD_DIR", (base / "Downloads").string().c_str(), 1);
#endif
}

// A crash in CI says which story it was drawing, and where.
extern "C" void onCrash(int sig)
{
    const char* what = currentCapture();
    std::fprintf(stderr, "ui_gallery: crashed (signal %d) while drawing %s\n", sig, what && *what ? what : "(no story)");
    std::fflush(stderr);
#ifdef UI_GALLERY_BACKTRACE
    void* frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
#endif
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

int usage()
{
    std::puts("usage: ui_gallery [--list] [--capture OUT [--references DIR] [--update-references]\n"
              "                  [--known-issues FILE] [--story GLOB] [--viewport GLOB] [--no-lint] [--no-input-lint]]");
    return 2;
}

// ── Window ──────────────────────────────────────────────────────────────────
struct Gallery {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* chromeTex = nullptr;
    SDL_Texture* storyTex = nullptr;
    int chromeTexW = 0, chromeTexH = 0, storyTexW = 0, storyTexH = 0;
    SoftRenderer chromeRenderer;
    ImGuiContext* chrome = nullptr;

    int story = 0;
    int viewport = 1; // 1180x780
    bool customSize = false;
    int customW = 1180, customH = 780;
    bool hiDpi = false;
    bool phone = false;
    InputMode input = InputMode::Hover;
    bool fit = true;
    std::unique_ptr<Session> session;
    bool rebuild = true;
    std::vector<Finding> findings;
    Image storyImage;
    SDL_Rect storyRect { 0, 0, 0, 0 };
    float storyZoom = 1.0f;
    std::string status;
    std::map<ImGuiMouseCursor, SDL_Cursor*> cursors;

    Viewport currentViewport() const
    {
        if (!customSize) return captureViewports()[std::size_t(viewport)];
        Viewport v;
        v.name = std::to_string(customW) + "x" + std::to_string(customH) + (hiDpi ? "@2x" : "") + (phone ? "-phone" : "");
        v.width = customW;
        v.height = customH;
        v.dpi = hiDpi && !phone ? 2.0f : 1.0f;
        v.phone = phone;
        v.pixelsPerDp = phone ? 2.625f : 1.0f;
        return v;
    }

    void makeSession()
    {
        const Story& s = stories()[std::size_t(story)];
        session = std::make_unique<Session>(s, currentViewport());
        session->setInputMode(input);
        session->settle(30);
        findings = lintFrame(*session, s, session->viewport());
        rebuild = false;
    }

    SDL_Cursor* cursor(ImGuiMouseCursor c)
    {
        auto it = cursors.find(c);
        if (it != cursors.end()) return it->second;
        SDL_SystemCursor sys = SDL_SYSTEM_CURSOR_ARROW;
        if (c == ImGuiMouseCursor_Hand) sys = SDL_SYSTEM_CURSOR_HAND;
        if (c == ImGuiMouseCursor_TextInput) sys = SDL_SYSTEM_CURSOR_IBEAM;
        return cursors[c] = SDL_CreateSystemCursor(sys);
    }

    static void upload(SDL_Renderer* r, SDL_Texture*& tex, int& tw, int& th, const Image& img)
    {
        if (!tex || tw != img.width || th != img.height) {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, img.width, img.height);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            tw = img.width;
            th = img.height;
        }
        SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
    }

    void drawChrome(int winW, int winH)
    {
        const float panelW = 330.0f;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(panelW, float(winH)));
        ImGui::Begin("Stories", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::TextDisabled("Open Nectar UI gallery (#215)");
        ImGui::SeparatorText("View");
        const auto& vps = captureViewports();
        if (ImGui::BeginCombo("Size", customSize ? "Custom" : vps[std::size_t(viewport)].name.c_str())) {
            for (int i = 0; i < int(vps.size()); ++i)
                if (ImGui::Selectable(vps[std::size_t(i)].name.c_str(), !customSize && i == viewport)) {
                    viewport = i;
                    customSize = false;
                    rebuild = true;
                }
            if (ImGui::Selectable("Custom", customSize)) {
                customSize = true;
                rebuild = true;
            }
            ImGui::EndCombo();
        }
        if (customSize) {
            rebuild |= ImGui::InputInt("Width", &customW, 10, 100);
            rebuild |= ImGui::InputInt("Height", &customH, 10, 100);
            customW = std::clamp(customW, 320, 3840);
            customH = std::clamp(customH, 240, 2160);
            rebuild |= ImGui::Checkbox("2x display scale", &hiDpi);
            rebuild |= ImGui::Checkbox("Phone layout (pixels, Android scaling)", &phone);
        }
        int mode = int(input);
        ImGui::TextUnformatted("Input");
        ImGui::SameLine();
        bool changed = ImGui::RadioButton("Mouse", &mode, int(InputMode::Hover));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Pad/keys", &mode, int(InputMode::Focus));
        ImGui::SameLine();
        changed |= ImGui::RadioButton("Touch", &mode, int(InputMode::Touch));
        if (changed) {
            input = InputMode(mode);
            if (session) {
                session->setInputMode(input);
                if (input == InputMode::Focus)
                    if (const Widget* t = session->target()) session->focus(*t);
            }
        }
        if (input == InputMode::Focus) ImGui::TextWrapped("Arrows / Tab move focus (Enter and Space are not sent).");
        ImGui::Checkbox("Fit to window", &fit);
        if (ImGui::Button("Save PNG") && session) {
            const std::string name = storyFileName(stories()[std::size_t(story)].name) + "-" + session->viewport().name + ".png";
            status = savePng(name, session->raster()) ? "Saved " + name : "Could not save " + name;
        }
        if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        ImGui::SeparatorText("Stories");
        ImGui::BeginChild("##list", ImVec2(0, float(winH) * 0.5f), ImGuiChildFlags_Borders);
        std::string group;
        for (int i = 0; i < int(stories().size()); ++i) {
            const Story& s = stories()[std::size_t(i)];
            if (s.group != group) {
                group = s.group;
                ImGui::TextDisabled("%s", group.c_str());
            }
            if (ImGui::Selectable(s.name.c_str(), i == story)) {
                story = i;
                rebuild = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.summary.c_str());
        }
        ImGui::EndChild();
        ImGui::SeparatorText("Lint");
        ImGui::BeginChild("##lint");
        if (findings.empty()) ImGui::TextDisabled("Nothing found.");
        for (const Finding& f : findings) {
            ImGui::PushStyleColor(ImGuiCol_Text, f.error ? ImVec4(1.0f, 0.55f, 0.45f, 1) : ImVec4(0.8f, 0.8f, 0.6f, 1));
            ImGui::TextWrapped("%s: %s", f.rule.c_str(), f.detail.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::End();
        storyRect = { int(panelW) + 8, 8, winW - int(panelW) - 16, winH - 16 };
    }

    int run()
    {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "ui_gallery: %s\n", SDL_GetError());
            return 1;
        }
        window = SDL_CreateWindow("Open Nectar UI gallery", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1600, 960,
                                  SDL_WINDOW_RESIZABLE);
        if (!window) {
            std::fprintf(stderr, "ui_gallery: %s\n", SDL_GetError());
            return 1;
        }
        renderer = SDL_CreateRenderer(window, -1, 0);
        if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer) {
            std::fprintf(stderr, "ui_gallery: %s\n", SDL_GetError());
            return 1;
        }
        chrome = ImGui::CreateContext();
        ImGui::SetCurrentContext(chrome);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::StyleColorsDark();
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
        {
            unsigned char* px = nullptr;
            int w = 0, h = 0;
            ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&px, &w, &h);
            ImGui::GetIO().Fonts->SetTexID(ImTextureID(chromeRenderer.create(px, w, h)));
        }
        bool quit = false;
        Uint64 last = SDL_GetPerformanceCounter();
        bool pointerInStory = false;
        while (!quit) {
            ImGui::SetCurrentContext(chrome);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) quit = true;
                ImGui_ImplSDL2_ProcessEvent(&e);
                if (!session) continue;
                const float z = storyZoom, sc = session->framebufferScale();
                const auto toUi = [&](int x, int y, float& ux, float& uy) {
                    ux = float(x - storyRect.x) / z / sc;
                    uy = float(y - storyRect.y) / z / sc;
                    return x >= storyRect.x && y >= storyRect.y && ux < session->uiSize().x && uy < session->uiSize().y;
                };
                if (e.type == SDL_MOUSEMOTION) {
                    float ux, uy;
                    pointerInStory = toUi(e.motion.x, e.motion.y, ux, uy);
                    if (input != InputMode::Focus) session->pointer(pointerInStory ? ux : -1, pointerInStory ? uy : -1);
                } else if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.button == SDL_BUTTON_LEFT
                           && input != InputMode::Focus) {
                    float ux, uy;
                    if (toUi(e.button.x, e.button.y, ux, uy) || e.type == SDL_MOUSEBUTTONUP) session->button(e.type == SDL_MOUSEBUTTONDOWN);
                } else if (e.type == SDL_MOUSEWHEEL && pointerInStory) {
                    session->wheel(float(e.wheel.y));
                } else if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && input == InputMode::Focus
                           && !ImGui::GetIO().WantCaptureKeyboard) {
                    ImGuiKey key = ImGuiKey_None;
                    switch (e.key.keysym.sym) {
                    case SDLK_UP: key = ImGuiKey_UpArrow; break;
                    case SDLK_DOWN: key = ImGuiKey_DownArrow; break;
                    case SDLK_LEFT: key = ImGuiKey_LeftArrow; break;
                    case SDLK_RIGHT: key = ImGuiKey_RightArrow; break;
                    case SDLK_TAB: key = ImGuiKey_Tab; break;
                    default: break;
                    }
                    if (key != ImGuiKey_None) session->key(key, e.type == SDL_KEYDOWN);
                }
            }
            const Uint64 now = SDL_GetPerformanceCounter();
            const float dt = std::min(0.1f, float(now - last) / float(SDL_GetPerformanceFrequency()));
            last = now;
            if (rebuild) makeSession();
            session->frame(dt);
            storyImage = session->raster();

            int winW = 0, winH = 0;
            SDL_GetRendererOutputSize(renderer, &winW, &winH);
            ImGui::SetCurrentContext(chrome);
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            drawChrome(winW, winH);
            ImGui::Render();
            Image chromeImage;
            const float clear[4] = { 0.08f, 0.09f, 0.10f, 1.0f };
            chromeRenderer.render(ImGui::GetDrawData(), chromeImage, clear);
            if (pointerInStory) SDL_SetCursor(cursor(input == InputMode::Hover ? session->cursor() : ImGuiMouseCursor_Arrow));

            upload(renderer, chromeTex, chromeTexW, chromeTexH, chromeImage);
            upload(renderer, storyTex, storyTexW, storyTexH, storyImage);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, chromeTex, nullptr, nullptr);
            storyZoom = 1.0f;
            if (fit && storyImage.width > 0)
                storyZoom = std::min({ 1.0f, float(storyRect.w) / float(storyImage.width), float(storyRect.h) / float(storyImage.height) });
            const SDL_Rect dst { storyRect.x, storyRect.y, int(float(storyImage.width) * storyZoom),
                                 int(float(storyImage.height) * storyZoom) };
            SDL_RenderCopy(renderer, storyTex, nullptr, &dst);
            SDL_RenderPresent(renderer);
        }
        session.reset();
        ImGui::SetCurrentContext(chrome);
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext(chrome);
        for (auto& c : cursors) SDL_FreeCursor(c.second);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }
};

} // namespace

int main(int argc, char** argv)
{
    for (int sig : { SIGSEGV, SIGABRT, SIGFPE, SIGILL }) std::signal(sig, onCrash);
#ifdef SIGBUS
    std::signal(SIGBUS, onCrash);
#endif
    CaptureOptions options;
    bool capture = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--list") {
            for (const Story& s : stories()) std::printf("%-36s %s\n", s.name.c_str(), s.summary.c_str());
            return 0;
        } else if (a == "--capture") {
            capture = true;
            options.outDir = next();
        } else if (a == "--references") {
            options.referenceDir = next();
        } else if (a == "--update-references") {
            options.updateReferences = true;
        } else if (a == "--known-issues") {
            options.knownIssues = next();
        } else if (a == "--story") {
            options.storyFilter = next();
        } else if (a == "--viewport") {
            options.viewportFilter = next();
        } else if (a == "--no-lint") {
            options.lint = false;
        } else if (a == "--no-input-lint") {
            options.inputLint = false;
        } else {
            return usage();
        }
    }
    if (capture && options.outDir.empty()) return usage();
    if (options.updateReferences && options.referenceDir.empty()) {
        std::fputs("ui_gallery: --update-references needs --references DIR\n", stderr);
        return 2;
    }
    std::error_code ec;
    const fs::path sandbox = (capture ? fs::absolute(fs::u8path(options.outDir), ec) : fs::temp_directory_path(ec) / "nectar-ui-gallery")
                           / "home";
    sandboxHome(sandbox);
    if (capture) return runCapture(options);
    return Gallery().run();
}
