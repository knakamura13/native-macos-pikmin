#include "storybook.h"

#include "theme.h"
#include "widgets.h"

#include "../capture.h"
#include "../gallery_host.h"
#include "../soft_raster.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_sdlrenderer2.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace pikmin::launcher::gallery::tool {

namespace {

// Sizes in the toolbar's dropdown.
struct SizeOption {
    const char* label;
    int w, h;
    bool phone;
    float pixelsPerDp;
};
const SizeOption kSizes[] = {
    { "800 × 600", 800, 600, false, 1.0f },
    { "1180 × 780 (design size)", 1180, 780, false, 1.0f },
    { "1280 × 720", 1280, 720, false, 1.0f },
    { "1440 × 900", 1440, 900, false, 1.0f },
    { "1920 × 1080", 1920, 1080, false, 1.0f },
    { "2560 × 1440", 2560, 1440, false, 1.0f },
    { "Phone 2400 × 1080 px", 2400, 1080, true, 2.625f },
    { "Phone 1600 × 720 px", 1600, 720, true, 2.0f },
};
constexpr int kSizeCount = int(sizeof kSizes / sizeof kSizes[0]);

std::string lower(std::string s)
{
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "update-notice" -> "Update notice"
std::string pretty(const std::string& slug)
{
    static const std::map<std::string, std::string> names = {
        { "whats-new", "What's new" }, { "hd-models", "HD models" }, { "hd-models-installed", "HD models installed" },
        { "pikmin2-not-installed", "Pikmin 2 not installed" }, { "update-notice-no-notes", "Update notice, no notes" },
        { "launch-fallback-missing-exe", "Launch fallback, missing exe" },
    };
    const auto it = names.find(slug);
    if (it != names.end()) return it->second;
    std::string s = slug;
    for (char& c : s)
        if (c == '-' || c == '_') c = ' ';
    if (!s.empty()) s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string folderLabel(const std::string& prefix)
{
    static const std::map<std::string, std::string> names = {
        { "hub", "Home" }, { "move", "Move Install" }, { "settings", "Settings" }, { "install", "Install" },
        { "dialog", "Dialogs" }, { "components", "Components" },
    };
    const auto it = names.find(prefix);
    return it != names.end() ? it->second : pretty(prefix);
}

// What a widget shows, without ImGui's "##id" suffix.
std::string visible(const Widget& w)
{
    std::string s = w.kind == Widget::Kind::Clickable ? w.label : (w.shown.empty() ? w.label : w.shown);
    const std::size_t hash = s.find("##");
    if (hash != std::string::npos) s.resize(hash);
    if (s.empty()) s = w.label;
    return s;
}

std::string argsKey(const Args& args)
{
    std::string k;
    for (const Arg& a : args) {
        char num[32];
        std::snprintf(num, sizeof num, "%g", double(a.number));
        k += a.name + "=" + (a.flag ? "1" : "0") + a.text + "|" + num + "|" + std::to_string(a.choice) + ";";
    }
    return k;
}

ImTextureID texId(SDL_Texture* t) { return ImTextureID(reinterpret_cast<intptr_t>(t)); }

struct Tex {
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    void upload(SDL_Renderer* r, const Image& img)
    {
        if (img.width <= 0 || img.height <= 0) return;
        if (!tex || w != img.width || h != img.height) {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, img.width, img.height);
            SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
            w = img.width;
            h = img.height;
        }
        SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4);
    }
    void reset()
    {
        if (tex) SDL_DestroyTexture(tex);
        tex = nullptr;
        w = h = 0;
    }
};

struct Action {
    double time;
    std::string kind, detail;
};

struct TestResult {
    bool pass = true;
    std::string why;
    int problems = 0, known = 0;
};

class App {
public:
    explicit App(const StorybookOptions& o) : mOptions(o) {}
    int run();

private:
    StorybookOptions mOptions;
    SDL_Window* mWindow = nullptr;
    SDL_Renderer* mRenderer = nullptr;
    ImGuiContext* mChrome = nullptr;
    Fonts mFonts;
    float mPixelScale = 1.0f, mUiScale = 1.0f;
    std::vector<KnownIssue> mKnown;
    std::string mKnownError;

    int mStory = 0;
    Args mArgs;
    int mSize = 1;
    bool mHiDpi = false;
    InputMode mInput = InputMode::Hover;
    std::unique_ptr<Session> mSession;
    bool mRebuild = true;
    double mRebuildAt = 0.0;
    Tex mStoryTex;
    std::vector<Finding> mLive; // lint of the live frame (+ pointer/focus results of the last test run)
    int mSelectedFinding = -1;

    float mZoom = 1.0f;
    bool mFit = true;
    int mBackground = 0; // 0 light, 1 dark, 2 checkerboard
    bool mOutline = false;
    bool mFullscreen = false;
    float mSidebarW = 270.0f, mPanelH = 300.0f;
    bool mPanelOpen = true;
    int mTab = 0; // 0 Controls, 1 Visual tests, 2 Layout, 3 Actions
    std::string mSearch;
    bool mFocusSearch = false, mScrollToSelected = true;
    enum class Area { Sidebar, Canvas } mArea = Area::Sidebar;
    std::set<std::string> mCollapsed;

    // Canvas -> story input, applied before the next story frame.
    struct Pending {
        bool move = false;
        float x = -1, y = -1;
        int button = 0; // 1 down, -1 up
        float wheel = 0;
        std::vector<std::pair<ImGuiKey, bool>> keys;
    } mPending;
    float mLastX = -2, mLastY = -2;
    bool mCanvasHeld = false;
    unsigned mDownOn = 0;
    float mPointerX = -1, mPointerY = -1;

    std::vector<Action> mActions;
    bool mLogHover = false;
    unsigned mLastFocus = 0, mLastHover = 0;
    double mStart = 0.0;
    bool mActionsScroll = false;

    struct Visual {
        std::string key;
        bool ran = false;
        ReferenceCheck check;
        Image shot;
        std::string layout;
        Tex ref, cur, diff;
    } mVisual;
    bool mVisualWanted = false;

    struct Job {
        int story;
        Viewport vp;
    };
    std::deque<Job> mJobs;
    int mJobsTotal = 0;
    std::string mRunLabel;
    std::map<std::string, TestResult> mResults; // "story|viewport"
    std::map<std::string, std::vector<Finding>> mResultFindings;
    int mRunPassed = 0, mRunFailed = 0, mRunKnown = 0;

    std::string mStatus;
    double mStatusUntil = 0.0;
    int mFrame = 0;

    const Story& story() const { return stories()[std::size_t(mStory)]; }
    bool isComponent() const { return story().canvasW > 0; }
    static double now() { return double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency()); }
    void status(std::string s)
    {
        mStatus = std::move(s);
        mStatusUntil = now() + 4.0;
    }
    void log(std::string kind, std::string detail)
    {
        mActions.push_back({ now() - mStart, std::move(kind), std::move(detail) });
        if (mActions.size() > 500) mActions.erase(mActions.begin());
        mActionsScroll = true;
    }

    Viewport viewport() const;
    bool inReferenceSet(const Viewport& vp) const;
    void select(int story);
    void rebuildSession();
    void applyPending();
    void runJob();
    void runVisual();
    void startTests(bool all);
    const Widget* widgetById(unsigned id) const;
    CaptureOptions captureOptions() const;

    void draw();
    void drawSidebar(float h);
    void drawTree();
    void drawTests();
    void drawToolbar();
    void drawCanvas(ImVec2 size);
    void drawOverlays(ImDrawList* dl, ImVec2 origin, float zoom);
    void drawPanel(float h);
    void drawControls();
    void drawVisual();
    void drawLayout();
    void drawActions();
    void savePngNow();
    bool screenshot(const std::string& path);
};

Viewport App::viewport() const
{
    if (isComponent()) {
        const std::vector<Viewport> v = viewportsFor(story());
        return v[mHiDpi && v.size() > 1 ? 1 : 0];
    }
    const SizeOption& o = kSizes[mSize];
    const std::string wh = std::to_string(o.w) + "x" + std::to_string(o.h);
    const std::string name = o.phone ? "phone-" + wh : wh + (mHiDpi ? "@2x" : "");
    if (const Viewport* v = findViewport(name)) return *v;
    Viewport v;
    v.name = name;
    v.width = o.w;
    v.height = o.h;
    v.dpi = !o.phone && mHiDpi ? 2.0f : 1.0f;
    v.phone = o.phone;
    v.pixelsPerDp = o.pixelsPerDp;
    return v;
}

bool App::inReferenceSet(const Viewport& vp) const
{
    for (const Viewport& v : viewportsFor(story()))
        if (v.name == vp.name) return true;
    return false;
}

CaptureOptions App::captureOptions() const
{
    CaptureOptions o;
    o.referenceDir = mOptions.referenceDir;
    o.knownIssues = mOptions.knownIssues;
    return o;
}

void App::select(int s)
{
    if (s < 0 || s >= int(stories().size())) return;
    if (s != mStory) log("story", stories()[std::size_t(s)].name);
    mStory = s;
    mArgs = story().args;
    mRebuild = true;
    mRebuildAt = 0.0;
    mScrollToSelected = true;
    mSelectedFinding = -1;
}

bool inputRule(const std::string& rule)
{
    return rule.find("cursor") != std::string::npos || rule.find("hover") != std::string::npos
        || rule.find("focus") != std::string::npos;
}

void App::rebuildSession()
{
    mSession.reset(); // sessions nest their texture backends: drop the old one first
    mSession = std::make_unique<Session>(story(), viewport(), &mArgs);
    mSession->setInputMode(mInput);
    mSession->settle(30);
    if (mInput == InputMode::Focus)
        if (const Widget* t = mSession->target()) mSession->focus(*t);
    std::vector<LowRes> lowRes;
    mSession->raster(&lowRes);
    mLive = lintFrame(*mSession, story(), mSession->viewport());
    for (Finding& f : lowResFindings(lowRes, story(), mSession->viewport())) mLive.push_back(std::move(f));
    // Pointer and focus checks need many frames: they come from Run tests.
    const auto it = mResultFindings.find(story().name + "|" + mSession->viewport().name);
    if (it != mResultFindings.end() && sameArgs(mArgs, story().args))
        for (const Finding& f : it->second)
            if (inputRule(f.rule)) mLive.push_back(f);
    std::vector<KnownIssue> known = mKnown;
    for (Finding& f : mLive) matchKnown(f, known);
    mLastFocus = mSession->focused();
    mLastHover = mSession->hovered();
    mLastX = mLastY = -2;
    mRebuild = false;
    mVisual.key.clear();
    if (mSelectedFinding >= int(mLive.size())) mSelectedFinding = -1;
}

const Widget* App::widgetById(unsigned id) const
{
    if (!mSession || !id) return nullptr;
    for (const Widget& w : mSession->widgets())
        if (w.kind == Widget::Kind::Clickable && w.id == id) return &w;
    return nullptr;
}

void App::applyPending()
{
    if (!mSession) return;
    Pending p = std::move(mPending);
    mPending = Pending();
    if (p.move) mSession->pointer(p.x, p.y);
    if (p.button == 1) mSession->button(true);
    else if (p.button == -1) mSession->button(false);
    if (p.wheel != 0.0f) mSession->wheel(p.wheel);
    for (const auto& k : p.keys) mSession->key(k.first, k.second);
}

void App::startTests(bool all)
{
    mJobs.clear();
    mRunPassed = mRunFailed = mRunKnown = 0;
    for (int i = 0; i < int(stories().size()); ++i) {
        if (!all && i != mStory) continue;
        for (const Viewport& vp : viewportsFor(stories()[std::size_t(i)])) mJobs.push_back({ i, vp });
    }
    mJobsTotal = int(mJobs.size());
    mRunLabel = all ? "all stories" : story().name;
    log("tests", "run " + mRunLabel + " (" + std::to_string(mJobsTotal) + " views)");
}

// One view of a test run: what `ui_gallery --capture` checks for it.
void App::runJob()
{
    if (mJobs.empty()) return;
    const Job job = mJobs.front();
    mJobs.pop_front();
    const Story& s = stories()[std::size_t(job.story)];
    const CaptureOptions o = captureOptions();
    std::vector<Finding> findings;
    ReferenceCheck ref;
    {
        Session t(s, job.vp);
        t.settle(90);
        std::vector<LowRes> lowRes;
        const Image shot = t.raster(&lowRes);
        findings = lintFrame(t, s, job.vp);
        for (Finding& f : lowResFindings(lowRes, s, job.vp)) findings.push_back(std::move(f));
        if (!o.referenceDir.empty()) ref = checkReference(o.referenceDir, s, job.vp, shot, layoutText(t), o);
    }
    if (job.vp.name == "1180x780" || job.vp.name == "1x")
        for (Finding& f : lintInput(s, job.vp)) findings.push_back(std::move(f));
    std::vector<KnownIssue> known = mKnown;
    TestResult r;
    for (Finding& f : findings) {
        matchKnown(f, known);
        if (!f.error) continue;
        if (f.known.empty()) {
            ++r.problems;
            r.pass = false;
            if (r.why.empty()) r.why = f.rule + ": " + f.detail;
        } else {
            ++r.known;
        }
    }
    if (!o.referenceDir.empty() && (!ref.hasReference || !ref.imageOk || !ref.layoutOk)) {
        r.pass = false;
        r.why = ref.why.empty() ? "reference differs" : ref.why;
    }
    const std::string key = s.name + "|" + job.vp.name;
    mResults[key] = r;
    mResultFindings[key] = std::move(findings);
    (r.pass ? mRunPassed : mRunFailed)++;
    mRunKnown += r.known;
    if (mJobs.empty()) {
        log("tests", mRunLabel + ": " + std::to_string(mRunPassed) + " passed, " + std::to_string(mRunFailed) + " failed");
        mRebuild = true; // pick up the pointer and focus findings
    }
}

void App::runVisual()
{
    mVisualWanted = false;
    const Viewport vp = viewport();
    const std::string key = story().name + "|" + vp.name + "|" + argsKey(mArgs);
    if (key == mVisual.key) return;
    mVisual.key = key;
    mVisual.ran = false;
    if (mOptions.referenceDir.empty() || !inReferenceSet(vp)) return;
    {
        Session t(story(), vp, &mArgs);
        t.settle(90);
        mVisual.shot = t.raster();
        mVisual.layout = layoutText(t);
    }
    mVisual.check = checkReference(mOptions.referenceDir, story(), vp, mVisual.shot, mVisual.layout, captureOptions());
    mVisual.ref.reset();
    mVisual.diff.reset();
    if (mVisual.check.hasReference) mVisual.ref.upload(mRenderer, mVisual.check.reference);
    mVisual.cur.upload(mRenderer, mVisual.check.current);
    if (!mVisual.check.diff.rgba.empty()) mVisual.diff.upload(mRenderer, mVisual.check.diff);
    mVisual.ran = true;
}

// Sidebar.
void App::drawSidebar(float h)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * mUiScale, 14 * mUiScale));
    ImGui::BeginChild("##sidebar", ImVec2(mSidebarW, h), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(0)) mArea = Area::Sidebar;

    // Title: a mark and the name.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float s = ImGui::GetFrameHeight() * 1.05f;
        dl->AddRectFilled(at, ImVec2(at.x + s, at.y + s), color::rgb(0xFF4785), 6.0f * mUiScale);
        drawIcon(dl, Icon::Story, ImVec2(at.x + s * 0.5f, at.y + s * 0.5f), s * 0.5f, IM_COL32_WHITE);
        ImGui::Dummy(ImVec2(s, s));
        ImGui::SameLine(0, 10 * mUiScale);
        ImGui::BeginGroup();
        text(mFonts.title, color::kText, "Open Nectar");
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - st.ItemSpacing.y + 1 * mUiScale);
        text(mFonts.small, color::kMuted, "Launcher UI gallery");
        ImGui::EndGroup();
    }
    ImGui::Dummy(ImVec2(0, 2 * mUiScale));

    // Search: "Find components"; '/' focuses it.
    {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(30 * mUiScale, st.FramePadding.y + 1 * mUiScale));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 20 * mUiScale);
        if (mFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            mFocusSearch = false;
        }
        const std::string before = mSearch;
        inputText("##search", mSearch, "Find components", w);
        const bool active = ImGui::IsItemActive();
        ImGui::PopStyleVar(2);
        const float fh = ImGui::GetItemRectSize().y;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        drawIcon(dl, Icon::Search, ImVec2(at.x + 16 * mUiScale, at.y + fh * 0.5f), fh * 0.42f, color::kFaint);
        if (!active && mSearch.empty()) {
            ImGui::PushFont(mFonts.small);
            const ImVec2 ts = ImGui::CalcTextSize("/");
            const float kw = ts.x + 12 * mUiScale, kh = ts.y + 4 * mUiScale;
            const ImVec2 k(at.x + w - kw - 10 * mUiScale, at.y + (fh - kh) * 0.5f);
            dl->AddRect(k, ImVec2(k.x + kw, k.y + kh), color::kBorder, 4 * mUiScale);
            dl->AddText(ImVec2(k.x + (kw - ts.x) * 0.5f, k.y + 2 * mUiScale), color::kFaint, "/");
            ImGui::PopFont();
        } else if (!mSearch.empty()) {
            const float s = fh * 0.6f;
            const ImVec2 c(at.x + w - s, at.y + fh * 0.5f);
            const ImVec2 back = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f));
            if (ImGui::InvisibleButton("##clear", ImVec2(s, s))) mSearch.clear();
            handCursor();
            const bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetItemTooltip("Clear search");
            drawIcon(dl, Icon::Cross, c, s * 0.6f, hov ? color::kText : color::kFaint);
            ImGui::SetCursorScreenPos(back);
        }
        if (active && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            mSearch.clear();
            ImGui::SetWindowFocus(nullptr);
        }
        if (mSearch != before) mScrollToSelected = true;
    }
    ImGui::Dummy(ImVec2(0, 2 * mUiScale));

    const float testsH = ImGui::GetFrameHeight() + ImGui::GetTextLineHeightWithSpacing() * 3.0f + 28 * mUiScale;
    ImGui::BeginChild("##tree", ImVec2(0, std::max(80.0f, ImGui::GetContentRegionAvail().y - testsH - st.ItemSpacing.y)), 0,
                      ImGuiWindowFlags_HorizontalScrollbar);
    drawTree();
    ImGui::EndChild();
    drawTests();
    ImGui::EndChild();
}

void App::drawTree()
{
    struct Folder {
        std::string key, label;
        std::vector<int> leaves;
    };
    std::vector<Folder> sections[2]; // screens, components
    const std::string q = lower(mSearch);
    for (int i = 0; i < int(stories().size()); ++i) {
        const Story& s = stories()[std::size_t(i)];
        const std::size_t slash = s.name.find('/');
        const std::string prefix = s.name.substr(0, slash);
        const std::string leaf = slash == std::string::npos ? s.name : s.name.substr(slash + 1);
        if (!q.empty()) {
            const std::string hay = lower(s.name + " " + pretty(leaf) + " " + folderLabel(prefix) + " " + s.summary);
            if (hay.find(q) == std::string::npos) continue;
        }
        auto& sec = sections[s.canvasW > 0 ? 1 : 0];
        auto it = std::find_if(sec.begin(), sec.end(), [&](const Folder& f) { return f.key == prefix; });
        if (it == sec.end()) {
            sec.push_back({ prefix, folderLabel(prefix), {} });
            it = sec.end() - 1;
        }
        it->leaves.push_back(i);
    }

    // Visible stories in order, for Up / Down.
    std::vector<int> order;
    for (auto& sec : sections)
        for (auto& f : sec)
            if (!q.empty() || !mCollapsed.count(f.key))
                for (int i : f.leaves) order.push_back(i);

    const ImGuiIO& io = ImGui::GetIO();
    const bool navigate = mArea == Area::Sidebar && (!io.WantTextInput || !mSearch.empty());
    if (navigate && !order.empty()) {
        const auto pos = std::find(order.begin(), order.end(), mStory);
        int idx = pos == order.end() ? -1 : int(pos - order.begin());
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) idx = std::min(int(order.size()) - 1, idx + 1);
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) idx = std::max(0, idx - 1);
        else if (ImGui::IsKeyPressed(ImGuiKey_Enter) && pos == order.end()) idx = 0;
        if (idx >= 0 && order[std::size_t(idx)] != mStory) select(order[std::size_t(idx)]);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH = ImGui::GetFrameHeight() + 2 * mUiScale;
    const float indent = 16 * mUiScale;
    const auto row = [&](const char* id, float depth, Icon icon, ImU32 iconCol, const std::string& label, bool selected, ImFont* font,
                         ImU32 dot) {
        ImGui::PushFont(font);
        const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
        const float iconW = ImGui::GetFontSize() + 6 * mUiScale;
        const float x0 = 4 * mUiScale + depth * indent;
        const float need = x0 + iconW + ts.x + (dot ? 24 : 10) * mUiScale;
        const float w = std::max(ImGui::GetContentRegionAvail().x, need);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, rowH));
        handCursor();
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 b(at.x + w, at.y + rowH);
        if (selected) dl->AddRectFilled(at, b, color::kAccent, 4 * mUiScale);
        else if (hovered) dl->AddRectFilled(at, b, color::rgb(0x029CFD, 18), 4 * mUiScale);
        const float cy = at.y + rowH * 0.5f;
        drawIcon(dl, icon, ImVec2(at.x + x0 + iconW * 0.4f, cy), ImGui::GetFontSize() * 0.72f, selected ? IM_COL32_WHITE : iconCol);
        dl->AddText(ImVec2(at.x + x0 + iconW, cy - ts.y * 0.5f), selected ? IM_COL32_WHITE : color::kText, label.c_str());
        if (dot) dl->AddCircleFilled(ImVec2(b.x - 12 * mUiScale, cy), 3.5f * mUiScale, selected ? IM_COL32_WHITE : dot);
        ImGui::PopFont();
        return clicked;
    };

    static const char* const kSectionNames[] = { "SCREENS", "COMPONENTS" };
    for (int si = 0; si < 2; ++si) {
        if (sections[si].empty()) continue;
        if (si) ImGui::Dummy(ImVec2(0, 6 * mUiScale));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6 * mUiScale);
        text(mFonts.smallBold, color::kFaint, kSectionNames[si]);
        for (const Folder& f : sections[si]) {
            const bool open = !q.empty() || !mCollapsed.count(f.key);
            ImGui::PushID(f.key.c_str());
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1 * mUiScale));
            {
                const ImVec2 at = ImGui::GetCursorScreenPos();
                if (row("##folder", 1.0f, si ? Icon::Component : Icon::Screen, si ? color::rgb(0x1EA7FD) : color::rgb(0x6F2CAC), f.label,
                        false, mFonts.bold, 0)
                    && q.empty()) {
                    if (open) mCollapsed.insert(f.key);
                    else mCollapsed.erase(f.key);
                }
                drawIcon(dl, open ? Icon::ChevronDown : Icon::ChevronRight, ImVec2(at.x + 11 * mUiScale, at.y + rowH * 0.5f),
                         ImGui::GetFontSize() * 0.55f, color::kFaint);
            }
            if (open) {
                for (int i : f.leaves) {
                    const Story& s = stories()[std::size_t(i)];
                    const std::size_t slash = s.name.find('/');
                    const std::string leaf = pretty(slash == std::string::npos ? s.name : s.name.substr(slash + 1));
                    // Test status: red if a view failed, green if every view run passed.
                    ImU32 dot = 0;
                    bool any = false, fail = false;
                    for (const Viewport& vp : viewportsFor(s)) {
                        const auto it = mResults.find(s.name + "|" + vp.name);
                        if (it == mResults.end()) continue;
                        any = true;
                        fail |= !it->second.pass;
                    }
                    if (any) dot = fail ? color::kRed : color::kGreen;
                    ImGui::PushID(i);
                    if (row("##story", 2.0f, Icon::Story, color::rgb(0x37A5A3), leaf, i == mStory, mFonts.body, dot)) {
                        select(i);
                        mArea = Area::Sidebar;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("%s\n%s", s.name.c_str(), s.summary.c_str());
                    if (i == mStory && mScrollToSelected) {
                        ImGui::SetScrollHereY(0.4f);
                        mScrollToSelected = false;
                    }
                    ImGui::PopID();
                }
            }
            ImGui::PopStyleVar();
            ImGui::PopID();
        }
    }
    if (order.empty() && !q.empty()) {
        ImGui::Dummy(ImVec2(0, 8 * mUiScale));
        textWrapped(mFonts.body, color::kMuted, ("No stories match \"" + mSearch + "\".").c_str());
    }
}

void App::drawTests()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetContentRegionAvail().y;
    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), color::kContent, 8 * mUiScale);
    dl->AddRect(at, ImVec2(at.x + w, at.y + h), color::kBorder, 8 * mUiScale);
    const float pad = 10 * mUiScale;
    ImGui::SetCursorScreenPos(ImVec2(at.x + pad, at.y + pad));
    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8 * mUiScale, 6 * mUiScale));
    text(mFonts.smallBold, color::kFaint, "TESTS");
    if (!mJobs.empty()) {
        if (textButton("Stop", false, true, Icon::Stop, true)) {
            mJobs.clear();
            log("tests", "stopped");
        }
        ImGui::SameLine();
        const int done = mJobsTotal - int(mJobs.size());
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d / %d views", done, mJobsTotal);
        ImGui::AlignTextToFramePadding();
        text(mFonts.small, color::kMuted, buf);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float bw = w - pad * 2, bh = 4 * mUiScale;
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), color::rgb(0xE4EAF0), bh);
        dl->AddRectFilled(p, ImVec2(p.x + bw * float(done) / float(std::max(1, mJobsTotal)), p.y + bh), color::kAccent, bh);
        ImGui::Dummy(ImVec2(bw, bh));
    } else {
        if (textButton("Run story", true, true, Icon::Play, true)) startTests(false);
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Lint and compare the selected story at every size it is tested at");
        ImGui::SameLine();
        if (textButton("Run all", false, true, Icon::Play, true)) startTests(true);
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("What ui_gallery_test checks: every story at every size");
    }
    if (mJobsTotal > 0) {
        ImGui::PushFont(mFonts.small);
        const auto count = [&](Icon icon, ImU32 col, int n, const char* label) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float s = ImGui::GetFontSize();
            drawIcon(dl, icon, ImVec2(p.x + s * 0.5f, p.y + ImGui::GetTextLineHeight() * 0.5f), s * 0.85f, col);
            ImGui::Dummy(ImVec2(s, ImGui::GetTextLineHeight()));
            ImGui::SameLine(0, 4 * mUiScale);
            ImGui::Text("%d %s", n, label);
        };
        count(Icon::Check, color::kGreen, mRunPassed, "passed");
        ImGui::SameLine(0, 12 * mUiScale);
        count(Icon::Cross, color::kRed, mRunFailed, "failed");
        ImGui::SameLine(0, 12 * mUiScale);
        count(Icon::Warning, color::kOrange, mRunKnown, "known");
        ImGui::PushStyleColor(ImGuiCol_Text, color::kMuted);
        ImGui::Text("Ran: %s", mRunLabel.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();
    } else {
        textWrapped(mFonts.small, color::kMuted, "Layout lint and reference images, as CI runs them.");
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();
}

// Toolbar.
void App::drawToolbar()
{
    const float h = ImGui::GetFrameHeight() + 12 * mUiScale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * mUiScale, 6 * mUiScale));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color::kContent);
    ImGui::BeginChild("##toolbar", ImVec2(0, h), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const auto sep = [&]() {
        ImGui::SameLine(0, 8 * mUiScale);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(p.x, p.y + 6 * mUiScale), ImVec2(p.x, p.y + ImGui::GetFrameHeight() - 6 * mUiScale), color::kBorder);
        ImGui::Dummy(ImVec2(1, ImGui::GetFrameHeight()));
        ImGui::SameLine(0, 8 * mUiScale);
    };
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2 * mUiScale, 0));
    if (iconButton("##reload", Icon::Reload, "Reload the story (re-render with the current controls)")) {
        mRebuild = true;
        log("story", "reloaded");
    }
    sep();
    if (iconButton("##zin", Icon::ZoomIn, "Zoom in (Ctrl + wheel)")) {
        mZoom = std::min(8.0f, mZoom * 1.25f);
        mFit = false;
    }
    ImGui::SameLine();
    if (iconButton("##zout", Icon::ZoomOut, "Zoom out")) {
        mZoom = std::max(0.1f, mZoom / 1.25f);
        mFit = false;
    }
    ImGui::SameLine();
    if (iconButton("##zreset", Icon::ZoomReset, mFit ? "Fitting the canvas. Click for 100%" : "Reset zoom (fit the canvas)")) {
        if (mFit) {
            mFit = false;
            mZoom = 1.0f;
        } else {
            mFit = true;
        }
    }
    ImGui::SameLine(0, 6 * mUiScale);
    {
        char z[32];
        std::snprintf(z, sizeof z, "%s%d%%", mFit ? "Fit " : "", int(std::lround(mZoom * 100.0f)));
        ImGui::AlignTextToFramePadding();
        text(mFonts.small, color::kMuted, z);
    }
    sep();

    // Size: the dropdown (screens) or the canvas size (components).
    if (isComponent()) {
        ImGui::AlignTextToFramePadding();
        char c[64];
        std::snprintf(c, sizeof c, "Canvas %d × %d", story().canvasW, story().canvasH);
        text(mFonts.body, color::kMuted, c);
    } else {
        float w = 0;
        for (const SizeOption& o : kSizes) w = std::max(w, ImGui::CalcTextSize(o.label).x);
        ImGui::SetNextItemWidth(w + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2.0f);
        if (ImGui::BeginCombo("##size", kSizes[mSize].label)) {
            handCursor();
            for (int i = 0; i < kSizeCount; ++i) {
                if (i == 6) ImGui::Separator();
                if (ImGui::Selectable(kSizes[i].label, i == mSize) && i != mSize) {
                    mSize = i;
                    mRebuild = true;
                    log("viewport", kSizes[i].label);
                }
                handCursor();
            }
            ImGui::EndCombo();
        } else {
            handCursor();
        }
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Window size in points, or a phone screen in pixels (Android scaling)");
    }
    ImGui::SameLine(0, 8 * mUiScale);
    if (!isComponent() && kSizes[mSize].phone) {
        ImGui::AlignTextToFramePadding();
        char d[48];
        std::snprintf(d, sizeof d, "%.4g px per dp", double(kSizes[mSize].pixelsPerDp));
        text(mFonts.small, color::kMuted, d);
    } else {
        static const char* const dpi[] = { "1x", "2x" };
        int v = mHiDpi ? 1 : 0;
        if (segmented("##dpi", dpi, 2, v)) {
            mHiDpi = v == 1;
            mRebuild = true;
            log("viewport", mHiDpi ? "2x display scale" : "1x display scale");
        }
        if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Display scale (2x = Retina / 200%%)");
    }
    sep();
    {
        static const char* const modes[] = { "Mouse", "Pad / keys", "Touch" };
        int m = mInput == InputMode::Focus ? 1 : mInput == InputMode::Touch ? 2 : 0;
        if (segmented("##input", modes, 3, m)) {
            mInput = m == 1 ? InputMode::Focus : m == 2 ? InputMode::Touch : InputMode::Hover;
            log("input", modes[m]);
            if (mSession) {
                mSession->setInputMode(mInput);
                if (mInput == InputMode::Focus)
                    if (const Widget* t = mSession->target()) mSession->focus(*t);
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetItemTooltip("Input the story gets. Pad / keys: click the canvas, then the arrows and Tab move focus");
    }
    sep();
    if (iconButton("##bg", Icon::Background, "Background", mBackground != 0)) ImGui::OpenPopup("##bgmenu");
    if (ImGui::BeginPopup("##bgmenu")) {
        static const char* const names[] = { "Light", "Dark", "Transparent (checkerboard)" };
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(names[i], mBackground == i)) mBackground = i;
            handCursor();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (iconButton("##outline", Icon::Outline, "Outline and measure: widget boxes from the layout data, overlaps in red", mOutline))
        mOutline = !mOutline;

    // Right: full screen, Save PNG.
    const ImGuiStyle& st = ImGui::GetStyle();
    const float saveW = ImGui::CalcTextSize("Save PNG").x + st.FramePadding.x * 2.0f + ImGui::GetTextLineHeight() + st.ItemInnerSpacing.x;
    const float rightW = ImGui::GetFrameHeight() + 8 * mUiScale + saveW;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 8 * mUiScale, ImGui::GetWindowContentRegionMax().x - rightW));
    if (iconButton("##full", Icon::Fullscreen, mFullscreen ? "Exit full screen (Esc)" : "Full screen canvas", mFullscreen))
        mFullscreen = !mFullscreen;
    ImGui::SameLine(0, 8 * mUiScale);
    if (textButton("Save PNG", false, mSession != nullptr, Icon::Save, true)) savePngNow();
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Save the story at full resolution to the current folder");
    ImGui::PopStyleVar();
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    dl->AddLine(ImVec2(wp.x, wp.y + ws.y - 1), ImVec2(wp.x + ws.x, wp.y + ws.y - 1), color::kBorder);
    ImGui::EndChild();
}

void App::savePngNow()
{
    if (!mSession) return;
    const std::string name = storyFileName(story().name) + "-" + mSession->viewport().name + ".png";
    const bool ok = savePng(name, mSession->raster());
    status(ok ? "Saved " + name : "Could not save " + name);
    log("save", ok ? name : "failed: " + name);
}

// Canvas.
void App::drawCanvas(ImVec2 size)
{
    ImGuiIO& io = ImGui::GetIO();
    const ImU32 bg = mBackground == 1 ? color::kCanvasDark : color::kCanvasLight;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##canvas", size, 0, ImGuiWindowFlags_HorizontalScrollbar | (io.KeyCtrl ? ImGuiWindowFlags_NoScrollWithMouse : 0));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 wp = ImGui::GetWindowPos();
    if (mBackground == 2) {
        const float c = 10 * mUiScale;
        for (float y = 0; y < size.y; y += c)
            for (float x = 0; x < size.x; x += c)
                if ((int(x / c) + int(y / c)) % 2)
                    dl->AddRectFilled(ImVec2(wp.x + x, wp.y + y), ImVec2(wp.x + x + c, wp.y + y + c), color::rgb(0xDCE1E6));
    }
    if (!mSession || !mStoryTex.tex) {
        ImGui::EndChild();
        return;
    }
    const ImVec2 ui = mSession->uiSize();
    const float margin = 28 * mUiScale;
    if (mFit) mZoom = std::clamp(std::min((size.x - margin * 2) / ui.x, (size.y - margin * 2) / ui.y), 0.1f, 1.0f);
    const ImVec2 img(ui.x * mZoom, ui.y * mZoom);
    const ImVec2 pos(std::max(margin, (size.x - img.x) * 0.5f), std::max(margin, (size.y - img.y) * 0.5f));
    ImGui::SetCursorPos(pos);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(a.x - 1, a.y + 1), ImVec2(a.x + img.x + 1, a.y + img.y + 3), IM_COL32(0, 0, 0, 26), 3.0f);
    ImGui::Image(texId(mStoryTex.tex), img);
    const bool over = ImGui::IsItemHovered();
    ImGui::SetCursorPos(ImVec2(pos.x + img.x, pos.y + img.y));
    ImGui::Dummy(ImVec2(margin, margin)); // room past the edges when zoomed in

    // Pointer -> story.
    const float ux = over ? (io.MousePos.x - a.x) / mZoom : -1.0f, uy = over ? (io.MousePos.y - a.y) / mZoom : -1.0f;
    mPointerX = ux;
    mPointerY = uy;
    if (mInput != InputMode::Focus && (ux != mLastX || uy != mLastY)) {
        mPending.move = true;
        mPending.x = ux;
        mPending.y = uy;
        mLastX = ux;
        mLastY = uy;
    }
    if (over && ImGui::IsMouseClicked(0)) {
        mArea = Area::Canvas;
        ImGui::SetWindowFocus(nullptr);
        if (mInput != InputMode::Focus) {
            mPending.button = 1;
            mCanvasHeld = true;
            mDownOn = mSession->hovered();
            const Widget* w = widgetById(mDownOn);
            log("pointerdown", w ? "\"" + visible(*w) + "\"" : "background");
        }
    }
    if (mCanvasHeld && ImGui::IsMouseReleased(0)) {
        mPending.button = -1;
        mCanvasHeld = false;
        const Widget* w = widgetById(mDownOn);
        if (w && over && mSession->hovered() == mDownOn)
            log("click", "\"" + visible(*w) + "\"" + (w->disabled ? " (disabled)" : "") + ", not run in the gallery");
    }
    if (over && io.MouseWheel != 0.0f) {
        if (io.KeyCtrl) {
            mZoom = std::clamp(mZoom * (io.MouseWheel > 0 ? 1.1f : 1.0f / 1.1f), 0.1f, 8.0f);
            mFit = false;
        } else {
            mPending.wheel += io.MouseWheel;
        }
    }
    if (mArea == Area::Canvas && mInput == InputMode::Focus && !io.WantTextInput) {
        for (ImGuiKey k : { ImGuiKey_UpArrow, ImGuiKey_DownArrow, ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_Tab }) {
            if (ImGui::IsKeyPressed(k, true)) {
                mPending.keys.push_back({ k, true });
                mPending.keys.push_back({ k, false });
                log("key", ImGui::GetKeyName(k));
            }
        }
    }
    if (over && mInput == InputMode::Hover) ImGui::SetMouseCursor(mSession->cursor());

    drawOverlays(dl, a, mZoom);
    if (mArea == Area::Canvas && mInput == InputMode::Focus)
        dl->AddRect(ImVec2(a.x - 3, a.y - 3), ImVec2(a.x + img.x + 3, a.y + img.y + 3), color::rgb(0x029CFD, 150), 4.0f, 0, 2.0f);

    // Bottom corners: what is shown, and the last status.
    {
        const Viewport& vp = mSession->viewport();
        char info[200];
        std::snprintf(info, sizeof info, "%s  ·  %s  ·  %d × %d pt", story().name.c_str(), vp.name.c_str(), int(ui.x), int(ui.y));
        ImGui::PushFont(mFonts.small);
        const ImVec2 ts = ImGui::CalcTextSize(info);
        const ImVec2 pa(wp.x + 14 * mUiScale, wp.y + size.y - ts.y - 12 * mUiScale);
        dl->AddRectFilled(ImVec2(pa.x - 8 * mUiScale, pa.y - 3 * mUiScale), ImVec2(pa.x + ts.x + 8 * mUiScale, pa.y + ts.y + 3 * mUiScale),
                          IM_COL32(255, 255, 255, 225), 10 * mUiScale);
        dl->AddText(pa, color::kMuted, info);
        if (!mStatus.empty() && now() < mStatusUntil) {
            const ImVec2 ss = ImGui::CalcTextSize(mStatus.c_str());
            const ImVec2 sp(wp.x + size.x - ss.x - 22 * mUiScale, pa.y);
            dl->AddRectFilled(ImVec2(sp.x - 8 * mUiScale, sp.y - 3 * mUiScale), ImVec2(sp.x + ss.x + 8 * mUiScale, sp.y + ss.y + 3 * mUiScale),
                              color::rgb(0x2E3438, 235), 10 * mUiScale);
            dl->AddText(sp, IM_COL32_WHITE, mStatus.c_str());
        }
        ImGui::PopFont();
    }
    ImGui::EndChild();
}

void App::drawOverlays(ImDrawList* dl, ImVec2 o, float z)
{
    const auto P = [&](float x, float y) { return ImVec2(o.x + x * z, o.y + y * z); };
    const std::vector<Widget>& ws = mSession->widgets();
    if (mOutline) {
        for (const Widget& w : ws) {
            ImU32 c = color::rgb(0x1EA7FD, 210);
            if (w.kind == Widget::Kind::Label || w.kind == Widget::Kind::Path) c = color::rgb(0x4CA02C, 210);
            if (w.kind == Widget::Kind::Toast || w.kind == Widget::Kind::Notice) c = color::rgb(0x9B4DCA, 220);
            if (w.kind == Widget::Kind::Logo) c = color::rgb(0x7A8590, 210);
            dl->AddRect(P(w.x0, w.y0), P(w.x1, w.y1), c, 0, 0, 1.0f);
        }
        // Clickables on one layer that overlap.
        for (std::size_t i = 0; i < ws.size(); ++i) {
            const Widget& a = ws[i];
            if (a.kind != Widget::Kind::Clickable) continue;
            for (std::size_t j = i + 1; j < ws.size(); ++j) {
                const Widget& b = ws[j];
                if (b.kind != Widget::Kind::Clickable || b.layer != a.layer) continue;
                const float x0 = std::max(a.x0, b.x0), y0 = std::max(a.y0, b.y0), x1 = std::min(a.x1, b.x1), y1 = std::min(a.y1, b.y1);
                if (x1 - x0 <= 0.5f || y1 - y0 <= 0.5f) continue;
                dl->AddRectFilled(P(x0, y0), P(x1, y1), color::rgb(0xE4380E, 90));
                dl->AddRect(P(a.x0, a.y0), P(a.x1, a.y1), color::kRed, 0, 0, 1.5f);
                dl->AddRect(P(b.x0, b.y0), P(b.x1, b.y1), color::kRed, 0, 0, 1.5f);
            }
        }
        // Measure: the smallest box under the pointer.
        if (mPointerX >= 0) {
            const Widget* best = nullptr;
            for (const Widget& w : ws)
                if (mPointerX >= w.x0 && mPointerX < w.x1 && mPointerY >= w.y0 && mPointerY < w.y1
                    && (!best || (w.x1 - w.x0) * (w.y1 - w.y0) < (best->x1 - best->x0) * (best->y1 - best->y0)))
                    best = &w;
            if (best) {
                const ImVec2 r0 = P(best->x0, best->y0), r1 = P(best->x1, best->y1);
                dl->AddRectFilled(r0, r1, color::rgb(0x1EA7FD, 50));
                dl->AddRect(r0, r1, color::kAccent, 0, 0, 2.0f);
                const std::string name = visible(*best);
                char m[160];
                std::snprintf(m, sizeof m, "%s  %.0f × %.0f", name.c_str(), double(best->x1 - best->x0), double(best->y1 - best->y0));
                ImGui::PushFont(mFonts.small);
                const ImVec2 ts = ImGui::CalcTextSize(m);
                ImVec2 p(r0.x, r0.y - ts.y - 8 * mUiScale);
                if (p.y < o.y) p.y = r1.y + 4 * mUiScale;
                dl->AddRectFilled(p, ImVec2(p.x + ts.x + 10 * mUiScale, p.y + ts.y + 4 * mUiScale), color::rgb(0xFF4785), 3 * mUiScale);
                dl->AddText(ImVec2(p.x + 5 * mUiScale, p.y + 2 * mUiScale), IM_COL32_WHITE, m);
                ImGui::PopFont();
            }
        }
    }
    // The finding picked in the Layout tab.
    if (mSelectedFinding >= 0 && mSelectedFinding < int(mLive.size())) {
        const Finding& f = mLive[std::size_t(mSelectedFinding)];
        for (const ImVec4& b : f.rects) {
            dl->AddRectFilled(P(b.x, b.y), P(b.z, b.w), color::rgb(0xE68A00, 55));
            dl->AddRect(P(b.x, b.y), P(b.z, b.w), color::kOrange, 0, 0, 2.0f);
        }
        if (f.rects.size() >= 2) {
            const ImVec4 &a = f.rects[0], &b = f.rects[1];
            const float x0 = std::max(a.x, b.x), y0 = std::max(a.y, b.y), x1 = std::min(a.z, b.z), y1 = std::min(a.w, b.w);
            if (x1 > x0 && y1 > y0) dl->AddRectFilled(P(x0, y0), P(x1, y1), color::rgb(0xE4380E, 120));
        }
    }
}

// Addon panel.
void App::drawPanel(float h)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, color::kContent);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##panel", ImVec2(0, h), 0, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int errors = 0, known = 0;
    for (const Finding& f : mLive)
        if (f.error) (f.known.empty() ? errors : known)++;
    const ImU32 layoutCol = errors ? color::kRed : known ? color::kOrange : color::kFaint;
    const float tabsH = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
    ImGui::BeginChild("##tabs", ImVec2(0, tabsH), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPosX(8 * mUiScale);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2 * mUiScale, 0));
    const char* names[] = { "Controls", "Visual tests", "Layout", "Actions" };
    const int counts[] = { int(story().args.size()), 0, int(mLive.size()), int(mActions.size()) };
    const ImU32 cols[] = { color::kFaint, color::kFaint, layoutCol, color::kFaint };
    for (int i = 0; i < 4; ++i) {
        if (i) ImGui::SameLine();
        ImGui::PushID(i);
        if (tab(names[i], counts[i], mTab == i, cols[i])) {
            mTab = i;
            mPanelOpen = true;
        }
        ImGui::PopID();
    }
    const bool showClear = mTab == 3 && mPanelOpen;
    float rightW = ImGui::GetFrameHeight();
    if (showClear) rightW += ImGui::CalcTextSize("Clear").x + ImGui::GetStyle().FramePadding.x * 2 + 8 * mUiScale;
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 8 * mUiScale, ImGui::GetWindowContentRegionMax().x - rightW - 10 * mUiScale));
    ImGui::SetCursorPosY((tabsH - ImGui::GetFrameHeight()) * 0.5f);
    if (showClear) {
        if (textButton("Clear")) mActions.clear();
        ImGui::SameLine(0, 8 * mUiScale);
    }
    if (iconButton("##collapse", mPanelOpen ? Icon::ChevronDown : Icon::ChevronUp, mPanelOpen ? "Hide the addon panel" : "Show the addon panel"))
        mPanelOpen = !mPanelOpen;
    ImGui::PopStyleVar();
    ImGui::EndChild();
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(p.x, p.y - 1), ImVec2(p.x + ImGui::GetWindowWidth(), p.y - 1), color::kBorder);
    }
    if (mPanelOpen) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16 * mUiScale, 12 * mUiScale));
        ImGui::BeginChild("##addon", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PopStyleVar();
        switch (mTab) {
        case 0: drawControls(); break;
        case 1: drawVisual(); break;
        case 2: drawLayout(); break;
        default: drawActions(); break;
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}

void App::drawControls()
{
    const Story& s = story();
    if (s.args.empty()) {
        textWrapped(mFonts.body, color::kMuted,
                    "This story has no controls. A story gets controls from its args (addArgs in ui_gallery/stories.cpp).");
        return;
    }
    const bool modified = !sameArgs(mArgs, s.args);
    if (textButton("Reset controls", false, modified, Icon::Reload, true)) {
        mArgs = s.args;
        mRebuild = true;
        log("controls", "reset");
    }
    ImGui::SameLine(0, 12 * mUiScale);
    ImGui::AlignTextToFramePadding();
    text(mFonts.small, color::kMuted, "Edits re-render the story live. Tests and references always use the defaults.");
    bool changed = false, immediate = false;
    if (ImGui::BeginTable("##args", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_PadOuterX)) {
        float nameW = 0;
        ImGui::PushFont(mFonts.bold);
        for (const Arg& a : mArgs) nameW = std::max(nameW, ImGui::CalcTextSize(a.name.c_str()).x);
        ImGui::PopFont();
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, nameW + 12 * mUiScale);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int c = 0; c < 3; ++c) {
            ImGui::TableSetColumnIndex(c);
            text(mFonts.smallBold, color::kFaint, c == 0 ? "NAME" : c == 1 ? "CONTROL" : "DEFAULT");
        }
        for (std::size_t i = 0; i < mArgs.size(); ++i) {
            Arg& a = mArgs[i];
            const Arg& d = s.args[i];
            ImGui::PushID(int(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            text(mFonts.bold, color::kText, a.name.c_str());
            ImGui::TableSetColumnIndex(1);
            const float w = std::min(ImGui::GetContentRegionAvail().x, 560 * mUiScale);
            std::string def;
            switch (a.kind) {
            case Arg::Kind::Bool:
                if (toggle("##b", a.flag)) changed = immediate = true;
                def = d.flag ? "True" : "False";
                break;
            case Arg::Kind::Text: {
                const bool multi = a.text.find('\n') != std::string::npos || d.text.find('\n') != std::string::npos;
                if (inputText("##t", a.text, nullptr, w, multi, ImGui::GetTextLineHeight() * 4.5f)) changed = true;
                def = d.text.empty() ? "(empty)" : d.text;
                break;
            }
            case Arg::Kind::Number: {
                ImGui::SetNextItemWidth(w);
                const char* fmt = a.step >= 1.0f ? "%.0f" : "%.2f";
                if (ImGui::SliderFloat("##n", &a.number, a.min, a.max, fmt)) {
                    if (a.step > 0) a.number = std::round(a.number / a.step) * a.step;
                    changed = true;
                }
                handCursor();
                char buf[32];
                std::snprintf(buf, sizeof buf, fmt, double(d.number));
                def = buf;
                break;
            }
            case Arg::Kind::Choice: {
                std::vector<const char*> labels;
                float total = 0;
                for (const std::string& c : a.choices) {
                    labels.push_back(c.c_str());
                    total += ImGui::CalcTextSize(c.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
                }
                if (total <= ImGui::GetContentRegionAvail().x) {
                    if (segmented("##c", labels.data(), int(labels.size()), a.choice)) changed = immediate = true;
                } else {
                    float cw = 0;
                    for (const std::string& c : a.choices) cw = std::max(cw, ImGui::CalcTextSize(c.c_str()).x);
                    ImGui::SetNextItemWidth(cw + ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.x * 2);
                    if (ImGui::BeginCombo("##cc", a.choices[std::size_t(a.choice)].c_str())) {
                        handCursor();
                        for (int c = 0; c < int(a.choices.size()); ++c) {
                            if (ImGui::Selectable(a.choices[std::size_t(c)].c_str(), c == a.choice)) {
                                a.choice = c;
                                changed = immediate = true;
                            }
                            handCursor();
                        }
                        ImGui::EndCombo();
                    } else {
                        handCursor();
                    }
                }
                def = d.choices[std::size_t(d.choice)];
                break;
            }
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(mFonts.small);
            ImGui::PushStyleColor(ImGuiCol_Text, color::kMuted);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(def.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            ImGui::PopFont();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (changed) {
        mRebuild = true;
        mRebuildAt = immediate ? 0.0 : now() + 0.15; // typing: wait for a pause
        log("controls", "changed");
    }
}

void App::drawVisual()
{
    const Viewport vp = viewport();
    if (mOptions.referenceDir.empty()) {
        textWrapped(mFonts.body, color::kMuted, "No reference folder. Start ui_gallery with --references DIR.");
        return;
    }
    if (!inReferenceSet(vp)) {
        std::string sizes;
        for (const Viewport& v : viewportsFor(story())) sizes += (sizes.empty() ? "" : ", ") + v.name;
        textWrapped(mFonts.body, color::kMuted, ("This size has no reference. Tested sizes: " + sizes + ".").c_str());
        return;
    }
    const std::string key = story().name + "|" + vp.name + "|" + argsKey(mArgs);
    if (key != mVisual.key) mVisualWanted = true;
    if (!mVisual.ran || key != mVisual.key) {
        text(mFonts.body, color::kMuted, "Comparing with the reference");
        return;
    }
    const ReferenceCheck& c = mVisual.check;
    const bool ok = c.hasReference && c.imageOk && c.layoutOk;
    const ImU32 col = ok ? color::kGreen : !c.hasReference ? color::kOrange : color::kRed;
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float s = ImGui::GetFrameHeight();
        drawIcon(ImGui::GetWindowDrawList(), ok ? Icon::Check : !c.hasReference ? Icon::Warning : Icon::Cross,
                 ImVec2(p.x + s * 0.4f, p.y + s * 0.5f), s * 0.55f, col);
        ImGui::Dummy(ImVec2(s * 0.8f, s));
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        const std::string msg = ok ? "Matches the reference at " + vp.name
                              : !c.hasReference ? "No reference yet at " + vp.name
                                                : "Changed at " + vp.name + ": " + c.why;
        text(mFonts.bold, col, msg.c_str());
    }
    const bool defaults = sameArgs(mArgs, story().args);
    ImGui::SameLine(0, 16 * mUiScale);
    if (textButton("Accept as new reference", true, !ok && defaults, Icon::Check, true)) {
        std::string error;
        if (acceptReference(mOptions.referenceDir, story(), vp, mVisual.shot, mVisual.layout, captureOptions(), error)) {
            status("Saved the new reference for " + story().name + " at " + vp.name);
            log("reference", "accepted " + story().name + " at " + vp.name);
            mVisual.key.clear();
            mResults.erase(story().name + "|" + vp.name);
        } else {
            status("Could not save: " + error);
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetItemTooltip("%s", !defaults ? "Reset controls first: references use the default args"
                                     : ok     ? "Already matches"
                                              : "Overwrite the reference picture and layout in the source tree");
    ImGui::SameLine();
    if (textButton("Compare again", false, true, Icon::Reload, true)) mVisual.key.clear();
    if (!defaults) textWrapped(mFonts.small, color::kOrange, "Controls differ from the defaults, so this compares the edited story.");

    const float gap = 16 * mUiScale;
    const float avW = ImGui::GetContentRegionAvail().x;
    const float avH = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() - 6 * mUiScale;
    const Tex* texes[] = { &mVisual.ref, &mVisual.cur, &mVisual.diff };
    const char* caps[] = { "REFERENCE", "CURRENT", "DIFFERENCE" };
    const float aspect = mVisual.cur.w > 0 ? float(mVisual.cur.h) / float(mVisual.cur.w) : 0.66f;
    float w = std::min((avW - gap * 2) / 3.0f, 420 * mUiScale);
    if (w * aspect > avH) w = std::max(80.0f, avH / aspect);
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        text(mFonts.smallBold, color::kFaint, caps[i]);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (texes[i]->tex) {
            ImGui::Image(texId(texes[i]->tex), ImVec2(w, w * aspect));
        } else {
            ImGui::Dummy(ImVec2(w, w * aspect));
            const char* none = i == 0 ? "No reference yet" : "No changed pixels";
            const ImVec2 ts = ImGui::CalcTextSize(none);
            dl->AddRectFilled(p, ImVec2(p.x + w, p.y + w * aspect), color::kAppBg, 4 * mUiScale);
            dl->AddText(ImVec2(p.x + (w - ts.x) * 0.5f, p.y + (w * aspect - ts.y) * 0.5f), color::kFaint, none);
        }
        dl->AddRect(p, ImVec2(p.x + w, p.y + w * aspect), color::kBorder);
        ImGui::EndGroup();
    }
}

void App::drawLayout()
{
    int errors = 0, known = 0, notes = 0;
    for (const Finding& f : mLive) {
        if (!f.error) ++notes;
        else if (f.known.empty()) ++errors;
        else ++known;
    }
    char sum[160];
    std::snprintf(sum, sizeof sum, "%d problem%s  ·  %d known (#214)  ·  %d note%s", errors, errors == 1 ? "" : "s", known, notes,
                  notes == 1 ? "" : "s");
    text(mFonts.bold, errors ? color::kRed : color::kText, sum);
    ImGui::SameLine(0, 14 * mUiScale);
    text(mFonts.small, color::kMuted, "Click a result to show its boxes on the canvas. Pointer and focus checks come from Run tests.");
    if (mLive.empty()) {
        ImGui::Dummy(ImVec2(0, 6 * mUiScale));
        text(mFonts.body, color::kMuted, "No layout problems in this frame.");
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float h = ImGui::GetFrameHeight() + 2 * mUiScale;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * mUiScale));
    for (int i = 0; i < int(mLive.size()); ++i) {
        const Finding& f = mLive[std::size_t(i)];
        ImGui::PushID(i);
        const std::string tag = !f.error ? "note" : f.known.empty() ? "problem" : "known: " + f.known;
        ImGui::PushFont(mFonts.bold);
        const float ruleW = ImGui::CalcTextSize(f.rule.c_str()).x;
        ImGui::PopFont();
        const float detailW = ImGui::CalcTextSize(f.detail.c_str()).x;
        ImGui::PushFont(mFonts.small);
        const float tagW = badgeWidth(tag.c_str());
        ImGui::PopFont();
        const float iconW = h;
        const float w = std::max(ImGui::GetContentRegionAvail().x, iconW + ruleW + detailW + tagW + 48 * mUiScale);
        const ImVec2 at = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##f", ImVec2(w, h))) {
            mSelectedFinding = mSelectedFinding == i ? -1 : i;
            log("layout", (mSelectedFinding == i ? "show " : "hide ") + f.rule);
        }
        handCursor();
        const bool hovered = ImGui::IsItemHovered();
        if (mSelectedFinding == i) dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), color::rgb(0xE68A00, 34), 4 * mUiScale);
        else if (hovered) dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), color::rgb(0x029CFD, 14), 4 * mUiScale);
        const unsigned hex = !f.error ? 0x8A959E : f.known.empty() ? 0xE4380E : 0xE68A00;
        const ImU32 col = color::rgb(hex);
        drawIcon(dl, !f.error ? Icon::Info : f.known.empty() ? Icon::Cross : Icon::Warning, ImVec2(at.x + iconW * 0.5f, at.y + h * 0.5f),
                 h * 0.42f, col);
        const float ty = at.y + (h - ImGui::GetFontSize()) * 0.5f;
        ImGui::PushFont(mFonts.bold);
        dl->AddText(ImVec2(at.x + iconW, ty), color::kText, f.rule.c_str());
        ImGui::PopFont();
        dl->AddText(ImVec2(at.x + iconW + ruleW + 10 * mUiScale, ty), color::kMuted, f.detail.c_str());
        ImGui::PushFont(mFonts.small);
        badge(dl, ImVec2(at.x + iconW + ruleW + detailW + 24 * mUiScale, at.y + (h - ImGui::GetFontSize() * 0.86f * 1.45f) * 0.5f),
              tag.c_str(), color::rgb(hex, 34), col);
        ImGui::PopFont();
        if (hovered && !f.rects.empty())
            ImGui::SetItemTooltip("%d box%s on the canvas", int(f.rects.size()), f.rects.size() == 1 ? "" : "es");
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
}

void App::drawActions()
{
    if (textButton(mLogHover ? "Logging hover" : "Log hover too")) mLogHover = !mLogHover;
    if (ImGui::IsItemHovered()) ImGui::SetItemTooltip("Also log the control under the pointer");
    ImGui::SameLine(0, 12 * mUiScale);
    ImGui::AlignTextToFramePadding();
    text(mFonts.small, color::kMuted, "Clicks, focus and keys the story gets. Clicks are not run in the gallery.");
    if (mActions.empty()) {
        ImGui::Dummy(ImVec2(0, 6 * mUiScale));
        text(mFonts.body, color::kMuted, "Nothing yet. Click or hover the story, or switch the input to Pad / keys.");
        return;
    }
    ImGui::BeginChild("##log", ImVec2(0, 0), 0, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4 * mUiScale));
    for (const Action& a : mActions) {
        char t[32];
        std::snprintf(t, sizeof t, "%02d:%06.3f", int(a.time / 60.0), std::fmod(a.time, 60.0));
        ImGui::PushFont(mFonts.small);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, color::kFaint);
        ImGui::TextUnformatted(t);
        ImGui::PopStyleColor();
        ImGui::PopFont();
        ImGui::SameLine(0, 14 * mUiScale);
        const ImU32 col = a.kind == "click" ? color::kAccent : a.kind == "focus" ? color::rgb(0x6F2CAC)
                        : a.kind == "tests" ? color::kGreen : color::kText;
        text(mFonts.bold, col, a.kind.c_str());
        ImGui::SameLine(0, 10 * mUiScale);
        text(mFonts.body, color::kText, a.detail.c_str());
    }
    ImGui::PopStyleVar();
    if (mActionsScroll) {
        ImGui::SetScrollHereY(1.0f);
        mActionsScroll = false;
    }
    ImGui::EndChild();
}

// One chrome frame.
void App::draw()
{
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 ds = io.DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, color::kAppBg);
    ImGui::Begin("##storybook", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                             | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Slash, false)) {
            mFullscreen = false;
            mFocusSearch = true;
            mArea = Area::Sidebar;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && mFullscreen) mFullscreen = false;
    }

    const float splitW = 5 * mUiScale;
    mSidebarW = std::clamp(mSidebarW, 220 * mUiScale, std::max(220 * mUiScale, ds.x * 0.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (!mFullscreen) {
        drawSidebar(ds.y);
        ImGui::SameLine(0, 0);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        mSidebarW += splitter("##vsplit", true, ImVec2(splitW, ds.y));
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + splitW - 1, p.y), ImVec2(p.x + splitW - 1, p.y + ds.y), color::kBorder);
        ImGui::SameLine(0, 0);
    }
    ImGui::BeginGroup();
    drawToolbar();
    const float rest = ImGui::GetContentRegionAvail().y;
    const float tabsH = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y * 2.0f + 1.0f;
    float panelH = 0;
    if (!mFullscreen) {
        mPanelH = std::clamp(mPanelH, 150 * mUiScale, std::max(150 * mUiScale, rest - 200 * mUiScale));
        panelH = mPanelOpen ? mPanelH : tabsH;
    }
    const float canvasH = std::max(80.0f, rest - panelH - (mFullscreen ? 0 : splitW));
    const float mainW = ImGui::GetContentRegionAvail().x;
    drawCanvas(ImVec2(mainW, canvasH));
    if (!mFullscreen) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float d = splitter("##hsplit", false, ImVec2(mainW, splitW));
        if (mPanelOpen) mPanelH -= d;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + splitW - 1), ImVec2(p.x + mainW, p.y + splitW - 1), color::kBorder);
        drawPanel(panelH);
    }
    ImGui::EndGroup();
    ImGui::PopStyleVar();
    ImGui::End();
}

bool App::screenshot(const std::string& path)
{
    int w = 0, h = 0;
    SDL_GetRendererOutputSize(mRenderer, &w, &h);
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(std::size_t(w) * std::size_t(h) * 4);
    if (SDL_RenderReadPixels(mRenderer, nullptr, SDL_PIXELFORMAT_ABGR8888, img.rgba.data(), w * 4) != 0) return false;
    for (std::size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    return savePng(path, img);
}

int App::run()
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "ui_gallery: %s\n", SDL_GetError());
        return 1;
    }
    mUiScale = mOptions.uiScale;
#ifndef __APPLE__
    // Screens that report points as pixels (Windows / Linux at 150 %...).
    if (mUiScale <= 0.0f) {
        float ddpi = 0;
        if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 120.0f) mUiScale = std::round(ddpi / 96.0f * 4.0f) / 4.0f;
    }
#endif
    if (mUiScale <= 0.0f) mUiScale = 1.0f;
    mWindow = SDL_CreateWindow("Open Nectar UI gallery", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, mOptions.width, mOptions.height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!mWindow) {
        std::fprintf(stderr, "ui_gallery: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(mWindow, int(960 * mUiScale), int(600 * mUiScale));
    mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!mRenderer) mRenderer = SDL_CreateRenderer(mWindow, -1, SDL_RENDERER_SOFTWARE);
    if (!mRenderer) {
        std::fprintf(stderr, "ui_gallery: %s\n", SDL_GetError());
        return 1;
    }
    IMGUI_CHECKVERSION();
    mChrome = ImGui::CreateContext();
    ImGui::SetCurrentContext(mChrome);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplSDL2_InitForSDLRenderer(mWindow, mRenderer);
    ImGui_ImplSDLRenderer2_Init(mRenderer);
    {
        int ww = 0, wh = 0, pw = 0, ph = 0;
        SDL_GetWindowSize(mWindow, &ww, &wh);
        SDL_GetRendererOutputSize(mRenderer, &pw, &ph);
        mPixelScale = ww > 0 ? float(pw) / float(ww) : 1.0f;
    }
    mFonts = loadFonts(mPixelScale, mUiScale);
    applyTheme(mUiScale);
    mSidebarW *= mUiScale;
    mPanelH *= mUiScale;

    if (!mOptions.knownIssues.empty()) mKnown = loadKnownIssues(mOptions.knownIssues, mKnownError);
    if (!mKnownError.empty()) std::fprintf(stderr, "ui_gallery: %s\n", mKnownError.c_str());

    if (!mOptions.story.empty() && mOptions.story != "*")
        for (int i = 0; i < int(stories().size()); ++i)
            if (globMatch(mOptions.story, stories()[std::size_t(i)].name)) {
                mStory = i;
                break;
            }
    mArgs = story().args;
    if (!mOptions.viewport.empty() && mOptions.viewport != "*") {
        std::string v = mOptions.viewport;
        if (v.size() > 3 && v.compare(v.size() - 3, 3, "@2x") == 0) {
            mHiDpi = true;
            v.resize(v.size() - 3);
        }
        if (v == "2x") mHiDpi = true;
        for (int i = 0; i < kSizeCount; ++i) {
            const std::string wh = std::to_string(kSizes[i].w) + "x" + std::to_string(kSizes[i].h);
            if ((!kSizes[i].phone && v == wh) || (kSizes[i].phone && v == "phone-" + wh)) mSize = i;
        }
    }
    if (mOptions.tab == "visual") mTab = 1;
    else if (mOptions.tab == "layout") mTab = 2;
    else if (mOptions.tab == "actions") mTab = 3;
    mOutline = mOptions.outline;
    mStart = now();
    log("story", story().name);
    if (mOptions.runTests == "story") startTests(false);
    else if (mOptions.runTests == "all") startTests(true);

    bool quit = false;
    Uint64 last = SDL_GetPerformanceCounter();
    int exitCode = 0;
    while (!quit) {
        ImGui::SetCurrentContext(mChrome);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID(mWindow))
                quit = true;
        }
        const Uint64 t = SDL_GetPerformanceCounter();
        const float dt = std::min(0.1f, float(t - last) / float(SDL_GetPerformanceFrequency()));
        last = t;

        // Story work happens between chrome frames (sessions switch the ImGui context).
        if (mRebuild && now() >= mRebuildAt) rebuildSession();
        applyPending();
        mSession->frame(dt);
        mStoryTex.upload(mRenderer, mSession->raster());
        if (mSession->focused() != mLastFocus) {
            mLastFocus = mSession->focused();
            if (const Widget* w = widgetById(mLastFocus)) log("focus", "\"" + visible(*w) + "\"");
        }
        if (mSession->hovered() != mLastHover) {
            mLastHover = mSession->hovered();
            const Widget* w = widgetById(mLastHover);
            if (mLogHover && w) log("hover", "\"" + visible(*w) + "\"");
        }
        runJob();
        if (mVisualWanted) runVisual();
        ImGui::SetCurrentContext(mChrome);

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        const float scale = io.DisplayFramebufferScale.x > 0 ? io.DisplayFramebufferScale.x : 1.0f;
        if (std::fabs(scale - mPixelScale) > 0.01f) { // moved to a screen with another scale
            mPixelScale = scale;
            ImGui_ImplSDLRenderer2_DestroyFontsTexture();
            mFonts = loadFonts(mPixelScale, mUiScale);
            ImGui_ImplSDLRenderer2_CreateFontsTexture();
        }
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        SDL_SetRenderDrawColor(mRenderer, 0xF6, 0xF9, 0xFC, 0xFF);
        SDL_RenderClear(mRenderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), mRenderer);
        ++mFrame;
        if (!mOptions.screenshot.empty() && mFrame >= mOptions.frames && mJobs.empty() && !mVisualWanted && !mRebuild) {
            if (!screenshot(mOptions.screenshot)) {
                std::fprintf(stderr, "ui_gallery: could not save %s\n", mOptions.screenshot.c_str());
                exitCode = 1;
            }
            quit = true;
        }
        SDL_RenderPresent(mRenderer);
    }
    mSession.reset();
    mStoryTex.reset();
    mVisual.ref.reset();
    mVisual.cur.reset();
    mVisual.diff.reset();
    ImGui::SetCurrentContext(mChrome);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext(mChrome);
    SDL_DestroyRenderer(mRenderer);
    SDL_DestroyWindow(mWindow);
    SDL_Quit();
    return exitCode;
}

} // namespace

int runStorybook(const StorybookOptions& options)
{
    App app(options);
    return app.run();
}

} // namespace pikmin::launcher::gallery::tool
