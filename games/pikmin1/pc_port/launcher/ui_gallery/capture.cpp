#include "capture.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace pikmin::launcher::gallery {

const std::vector<Viewport>& captureViewports()
{
    static const std::vector<Viewport> all = {
        { "800x600", 800, 600, 1.0f, false, 1.0f },
        { "1180x780", 1180, 780, 1.0f, false, 1.0f },
        { "1280x720", 1280, 720, 1.0f, false, 1.0f },
        { "1920x1080", 1920, 1080, 1.0f, false, 1.0f },
        { "2560x1440", 2560, 1440, 1.0f, false, 1.0f },
        { "1180x780@2x", 1180, 780, 2.0f, false, 1.0f },
        { "1440x900@2x", 1440, 900, 2.0f, false, 1.0f },
        // Landscape phones in pixels (the Android launcher is landscape only).
        { "phone-2400x1080", 2400, 1080, 1.0f, true, 2.625f },
        { "phone-1600x720", 1600, 720, 1.0f, true, 2.0f },
    };
    return all;
}

const Viewport* findViewport(const std::string& name)
{
    for (const Viewport& v : captureViewports())
        if (v.name == name) return &v;
    return nullptr;
}

std::vector<Viewport> viewportsFor(const Story& story)
{
    if (story.canvasW <= 0) return captureViewports();
    return { { "1x", story.canvasW, story.canvasH, 1.0f, false, 1.0f },
             { "2x", story.canvasW, story.canvasH, 2.0f, false, 1.0f } };
}

bool globMatch(const std::string& pattern, const std::string& text)
{
    std::size_t p = 0, t = 0, star = std::string::npos, mark = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

std::string storyFileName(const std::string& story)
{
    std::string out = story;
    for (std::size_t at = out.find('/'); at != std::string::npos; at = out.find('/', at + 2)) out.replace(at, 1, "__");
    return out;
}

namespace {

struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float w() const { return std::max(0.0f, x1 - x0); }
    float h() const { return std::max(0.0f, y1 - y0); }
    bool empty() const { return w() <= 0.0f || h() <= 0.0f; }
};

Rect rectOf(const Widget& w) { return { w.x0, w.y0, w.x1, w.y1 }; }
Rect clipOf(const Widget& w) { return { w.cx0, w.cy0, w.cx1, w.cy1 }; }
Rect intersect(const Rect& a, const Rect& b)
{
    return { std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1) };
}

Rect visibleRect(const Widget& w, ImVec2 display)
{
    return intersect(intersect(rectOf(w), clipOf(w)), Rect { 0, 0, display.x, display.y });
}

std::string visibleLabel(const std::string& label)
{
    const std::size_t hidden = label.find("##");
    std::string out = hidden == std::string::npos ? label : label.substr(0, hidden);
    if (out.empty()) out = label; // "##release": only an id
    return out;
}

std::string fmt(const char* f, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, f);
    std::vsnprintf(buffer, sizeof buffer, f, args);
    va_end(args);
    return buffer;
}

std::string q(const std::string& s) { return "'" + visibleLabel(s) + "'"; }

std::string sizeText(const Rect& r) { return fmt("%.0fx%.0f", r.w(), r.h()); }

} // namespace

std::vector<Finding> lintFrame(const Session& session, const Story& story, const Viewport& viewport)
{
    std::vector<Finding> out;
    const auto add = [&](const char* rule, std::string detail, bool error = true) {
        out.push_back({ rule, story.name, viewport.name, std::move(detail), error, {} });
    };
    const ImVec2 display = session.uiSize();
    const std::vector<Widget>& all = session.widgets();
    std::vector<const Widget*> clickables;
    const Widget* logo = nullptr;
    for (const Widget& w : all) {
        if (w.kind == Widget::Kind::Clickable && !rectOf(w).empty()) clickables.push_back(&w);
        if (w.kind == Widget::Kind::Logo) logo = &w;
    }

    // Clickables on the same layer must not overlap (a modal over the
    // disabled home screen is a different layer, and fine).
    for (std::size_t i = 0; i < clickables.size(); ++i) {
        const Widget& a = *clickables[i];
        const Rect va = visibleRect(a, display);
        if (va.empty()) continue;
        for (std::size_t j = i + 1; j < clickables.size(); ++j) {
            const Widget& b = *clickables[j];
            if (b.layer != a.layer) continue;
            const Rect vb = visibleRect(b, display);
            const Rect both = intersect(va, vb);
            if (both.w() > 1.0f && both.h() > 1.0f)
                add("overlap", q(a.label) + " and " + q(b.label) + " overlap by " + sizeText(both));
        }
        if (logo && a.layer == "##hub") {
            const Rect both = intersect(va, rectOf(*logo));
            if (both.w() > 1.0f && both.h() > 1.0f) add("covers-logo", q(a.label) + " covers the logo by " + sizeText(both));
        }
    }

    // Toasts and notices are drawn over everything: nothing clickable under them.
    for (const Widget& t : all) {
        if (t.kind != Widget::Kind::Toast) continue;
        for (const Widget* c : clickables) {
            if (c->disabled) continue;
            const Rect both = intersect(visibleRect(*c, display), rectOf(t));
            if (both.w() > 1.0f && both.h() > 1.0f)
                add("covered-by-toast", "toast " + q(t.label) + " covers " + q(c->label) + " by " + sizeText(both));
        }
        if (t.truncated) add("truncated-toast", "toast " + q(t.label) + " is cut to " + q(t.shown));
        if (t.x0 < -0.5f || t.y0 < -0.5f || t.x1 > display.x + 0.5f || t.y1 > display.y + 0.5f)
            add("off-screen", "toast " + q(t.label) + " goes past the window");
    }

    // On screen and inside the box they are drawn in, unless they scroll there.
    for (const Widget* c : clickables) {
        if (c->scrolls) continue;
        const Rect r = rectOf(*c);
        if (r.x0 < -0.5f || r.y0 < -0.5f || r.x1 > display.x + 0.5f || r.y1 > display.y + 0.5f) {
            add("off-screen", q(c->label) + fmt(" at %.0f,%.0f-%.0f,%.0f is outside the %.0fx%.0f window", r.x0, r.y0,
                                                  r.x1, r.y1, display.x, display.y));
            continue;
        }
        const Rect clip = clipOf(*c);
        if (r.x0 < clip.x0 - 1 || r.y0 < clip.y0 - 1 || r.x1 > clip.x1 + 1 || r.y1 > clip.y1 + 1)
            add("clipped", q(c->label) + " is cut off by its panel (" + sizeText(visibleRect(*c, display)) + " of "
                               + sizeText(r) + " visible)");
    }

    // Text fitted into a box.
    for (const Widget& w : all) {
        if (w.kind == Widget::Kind::Label && w.truncated)
            add("truncated-label", q(w.label) + " is cut to " + q(w.shown) + " in a " + sizeText(rectOf(w)) + " button");
        if (w.kind == Widget::Kind::Label && w.shrunk && !w.truncated)
            add("shrunk-label", q(w.label) + " is drawn smaller to fit a " + sizeText(rectOf(w)) + " button", false);
        if (w.kind == Widget::Kind::Path)
            add("shortened-path", q(w.label) + " is shown as " + q(w.shown), false);
    }

    // Phones: fingers need about 48 dp (Android) / 44 pt (iOS).
    if (viewport.phone) {
        int small = 0, total = 0;
        float smallest = 1e9f;
        std::string smallestLabel;
        const float dpPerPoint = session.framebufferScale() / viewport.pixelsPerDp;
        for (const Widget* c : clickables) {
            if (c->disabled || visibleRect(*c, display).empty()) continue;
            ++total;
            const Rect r = rectOf(*c);
            const float dp = std::min(r.w(), r.h()) * dpPerPoint;
            if (dp >= 48.0f) continue;
            ++small;
            if (dp < smallest) {
                smallest = dp;
                smallestLabel = c->label;
            }
        }
        if (small > 0)
            add("small-touch-target", fmt("%d of %d controls are under 48 dp; smallest ", small, total) + q(smallestLabel)
                                          + fmt(" is %.0f dp", smallest),
                false);
    }
    return out;
}

std::vector<Finding> lintInput(const Story& story, const Viewport& viewport)
{
    std::vector<Finding> out;
    const auto add = [&](const char* rule, std::string detail, bool error = true) {
        out.push_back({ rule, story.name, viewport.name, std::move(detail), error, {} });
    };
    {
        Session session(story, viewport);
        session.setInputMode(InputMode::Hover);
        session.settle(30);
        const Widget* top = session.target();
        if (!top) return out;
        const std::string layer = top->layer;
        const ImVec2 display = session.uiSize();
        struct Probe { unsigned id; std::string label; float x, y; };
        std::vector<Probe> probes;
        for (const Widget& w : session.widgets()) {
            if (w.kind != Widget::Kind::Clickable || w.disabled || w.layer != layer) continue;
            const Rect v = visibleRect(w, display);
            if (v.w() < 2 || v.h() < 2) continue;
            probes.push_back({ w.id, w.label, (v.x0 + v.x1) * 0.5f, (v.y0 + v.y1) * 0.5f });
        }
        std::map<unsigned, std::string> labels;
        for (const Widget& w : session.widgets())
            if (w.kind == Widget::Kind::Clickable) labels[w.id] = w.label;
        for (const Probe& p : probes) {
            session.pointer(p.x, p.y);
            for (int i = 0; i < 3; ++i) session.frame(1.0f / 60.0f);
            if (session.hovered() != p.id) {
                const auto other = labels.find(session.hovered());
                add("hover-blocked", q(p.label) + " cannot be pointed at: "
                                         + (other != labels.end() ? q(other->second) + " is on top" : std::string("nothing reacts")));
                continue;
            }
            if (session.cursor() != ImGuiMouseCursor_Hand) add("no-hand-cursor", q(p.label) + " shows the arrow cursor");
        }
    }
    {
        // Controller / keyboard: is the focused control drawn any differently?
        Session session(story, viewport);
        session.settle(60);
        const Widget* target = session.target();
        if (!target) return out;
        const Widget focusOn = *target;
        const Image before = session.raster();
        session.setInputMode(InputMode::Focus);
        session.focus(focusOn);
        session.settle(30);
        const Image after = session.raster();
        const float s = session.framebufferScale();
        const int x0 = std::max(0, int((focusOn.x0 - 6) * s)), y0 = std::max(0, int((focusOn.y0 - 6) * s));
        const int x1 = std::min(before.width, int((focusOn.x1 + 6) * s)), y1 = std::min(before.height, int((focusOn.y1 + 6) * s));
        int changed = 0;
        if (before.width == after.width && before.height == after.height)
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    for (int c = 0; c < 3; ++c) {
                        const std::size_t i = (std::size_t(y) * before.width + x) * 4 + c;
                        if (std::abs(int(before.rgba[i]) - int(after.rgba[i])) > 24) { ++changed; c = 3; }
                    }
        if (session.focused() != focusOn.id)
            add("focus-unreachable", q(focusOn.label) + " cannot take controller/keyboard focus", false);
        else if (changed < 20)
            add("no-focus-indicator", q(focusOn.label) + " looks the same with controller/keyboard focus", false);
    }
    return out;
}

std::vector<KnownIssue> loadKnownIssues(const std::string& path, std::string& error)
{
    std::vector<KnownIssue> out;
    std::ifstream in(path);
    if (!in) {
        error = "cannot read " + path;
        return out;
    }
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos || line[start] == '#') continue;
        // rule | story glob | viewport glob | detail glob | note
        std::vector<std::string> parts;
        std::stringstream ss(line);
        std::string part;
        while (std::getline(ss, part, '|')) {
            const std::size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
            parts.push_back(a == std::string::npos ? std::string() : part.substr(a, b - a + 1));
        }
        if (parts.size() < 5) {
            error = path + ":" + std::to_string(number) + ": expected rule | story | viewport | detail | note";
            return {};
        }
        out.push_back({ parts[0], parts[1], parts[2], parts[3], parts[4], number, false });
    }
    return out;
}

void matchKnown(Finding& f, std::vector<KnownIssue>& known)
{
    for (KnownIssue& k : known) {
        if (k.rule != f.rule || !globMatch(k.story, f.story) || !globMatch(k.viewport, f.viewport)
            || !globMatch(k.detail, f.detail))
            continue;
        k.seen = true;
        f.known = k.note;
        return;
    }
}

namespace {

std::string layoutText(const Session& session)
{
    std::ostringstream out;
    const ImVec2 size = session.uiSize();
    out << "# ui " << int(std::lround(size.x)) << "x" << int(std::lround(size.y)) << " scale "
        << fmt("%.3f", session.framebufferScale()) << "\n";
    for (const Widget& w : session.widgets()) {
        const char* kind = nullptr;
        switch (w.kind) {
        case Widget::Kind::Clickable: kind = w.disabled ? "button-disabled" : "button"; break;
        case Widget::Kind::Label: kind = w.truncated ? "label-truncated" : (w.shrunk ? "label-shrunk" : "label"); break;
        case Widget::Kind::Toast: kind = w.truncated ? "toast-truncated" : "toast"; break;
        case Widget::Kind::Notice: kind = "notice"; break;
        case Widget::Kind::Logo: kind = "logo"; break;
        case Widget::Kind::Path: break; // text differs per platform (separators)
        }
        if (!kind) continue;
        out << kind << " " << int(std::lround(w.x0)) << " " << int(std::lround(w.y0)) << " " << int(std::lround(w.x1))
            << " " << int(std::lround(w.y1)) << " " << w.layer << " | " << w.label << "\n";
    }
    return out.str();
}

std::vector<std::string> lines(const std::string& text)
{
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}

// Same kind, label and layer, each coordinate within one point.
bool sameLayoutLine(const std::string& a, const std::string& b)
{
    if (a == b) return true;
    std::istringstream sa(a), sb(b);
    std::string ka, kb;
    long ca[4] = {}, cb[4] = {};
    sa >> ka >> ca[0] >> ca[1] >> ca[2] >> ca[3];
    sb >> kb >> cb[0] >> cb[1] >> cb[2] >> cb[3];
    if (!sa || !sb || ka != kb) return false;
    for (int i = 0; i < 4; ++i)
        if (std::abs(ca[i] - cb[i]) > 1) return false;
    std::string ra, rb;
    std::getline(sa, ra);
    std::getline(sb, rb);
    return ra == rb;
}

std::string readText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeText(const fs::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary);
    out << text;
    return bool(out);
}

struct CompareResult {
    bool ok = true;
    std::string why;
};

CompareResult compareImages(const Image& before, const Image& after, const CaptureOptions& o, Image& diff)
{
    if (before.width != after.width || before.height != after.height)
        return { false, fmt("size changed from %dx%d to %dx%d", before.width, before.height, after.width, after.height) };
    diff = after;
    std::size_t changed = 0;
    for (std::size_t i = 0; i < after.rgba.size(); i += 4) {
        int d = 0;
        for (int c = 0; c < 3; ++c) d = std::max(d, std::abs(int(before.rgba[i + c]) - int(after.rgba[i + c])));
        const int grey = (int(after.rgba[i]) + int(after.rgba[i + 1]) + int(after.rgba[i + 2])) / 6;
        if (d > o.channelTolerance) {
            ++changed;
            diff.rgba[i] = 255;
            diff.rgba[i + 1] = 0;
            diff.rgba[i + 2] = 64;
        } else {
            diff.rgba[i] = diff.rgba[i + 1] = diff.rgba[i + 2] = std::uint8_t(grey);
        }
        diff.rgba[i + 3] = 255;
    }
    const float percent = 100.0f * float(changed) / float(std::max<std::size_t>(1, after.rgba.size() / 4));
    if (percent > o.tolerancePercent) return { false, fmt("%.2f%% of pixels changed (tolerance %.2f%%)", percent, o.tolerancePercent) };
    return {};
}

std::string htmlEscape(const std::string& s)
{
    std::string out;
    for (char c : s) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}

} // namespace

int runCapture(const CaptureOptions& o)
{
    std::error_code ec;
    const fs::path out = fs::u8path(o.outDir);
    fs::create_directories(out / "shots", ec);
    if (ec) {
        std::fprintf(stderr, "ui_gallery: cannot create %s: %s\n", o.outDir.c_str(), ec.message().c_str());
        return 2;
    }
    fs::remove_all(out / "failures", ec);
    std::vector<KnownIssue> known;
    if (!o.knownIssues.empty()) {
        std::string error;
        known = loadKnownIssues(o.knownIssues, error);
        if (!error.empty()) {
            std::fprintf(stderr, "ui_gallery: %s\n", error.c_str());
            return 2;
        }
    }
    const fs::path refs = o.referenceDir.empty() ? fs::path() : fs::u8path(o.referenceDir);
    std::vector<Finding> findings;
    std::vector<std::string> failures;
    int captured = 0;
    std::ostringstream html;
    html << "<!doctype html><meta charset=utf-8><title>UI gallery</title><style>body{font:14px sans-serif;background:#102;"
            "color:#eee}figure{display:inline-block;margin:6px;vertical-align:top}img{max-width:420px;max-height:260px;"
            "border:1px solid #555}figcaption{font-size:12px}h2{margin-top:28px}</style>\n<h1>Open Nectar UI gallery</h1>\n";
    const auto started = std::chrono::steady_clock::now();

    for (const Story& story : stories()) {
        if (!globMatch(o.storyFilter, story.name)) continue;
        const std::string file = storyFileName(story.name);
        html << "<h2>" << htmlEscape(story.name) << "</h2><p>" << htmlEscape(story.summary) << "</p>\n";
        const auto storyStart = std::chrono::steady_clock::now();
        for (const Viewport& vp : viewportsFor(story)) {
            if (!globMatch(o.viewportFilter, vp.name)) continue;
            Session session(story, vp);
            session.settle(90);
            std::vector<LowRes> lowRes;
            const Image shot = session.raster(&lowRes);
            ++captured;
            const fs::path shotDir = out / "shots" / file;
            fs::create_directories(shotDir, ec);
            const fs::path shotPath = shotDir / (vp.name + ".png");
            if (!savePng(shotPath.string(), shot)) failures.push_back(story.name + " " + vp.name + ": cannot write " + shotPath.string());
            const std::string layout = layoutText(session);
            writeText(shotDir / (vp.name + ".layout.txt"), layout);
            html << "<figure><a href=\"shots/" << file << "/" << vp.name << ".png\"><img loading=lazy src=\"shots/" << file
                 << "/" << vp.name << ".png\"></a><figcaption>" << vp.name << "</figcaption></figure>\n";

            if (o.lint) {
                for (Finding& f : lintFrame(session, story, vp)) findings.push_back(std::move(f));
                for (const LowRes& l : lowRes) {
                    findings.push_back({ "low-res", story.name, vp.name,
                                         fmt("%s (%dx%d texture) drawn at %.2f texels per pixel", l.what.c_str(), l.texW,
                                             l.texH, l.texelsPerPixel),
                                         false, {} });
                }
                if (o.inputLint && (vp.name == "1180x780" || vp.name == "1x"))
                    for (Finding& f : lintInput(story, vp)) findings.push_back(std::move(f));
            }

            if (refs.empty()) continue;
            const Image thumb = downscale(shot, o.thumbnailWidth);
            const fs::path refDir = refs / file;
            const fs::path refPng = refDir / (vp.name + ".png"), refLayout = refDir / (vp.name + ".layout.txt");
            if (o.updateReferences) {
                fs::create_directories(refDir, ec);
                Image old;
                Image unused;
                // Rewrite only what changed, so unchanged references keep their bytes.
                if (!loadPng(refPng.string(), old) || !compareImages(old, thumb, o, unused).ok) savePng(refPng.string(), thumb, 9);
                if (readText(refLayout) != layout) writeText(refLayout, layout);
                continue;
            }
            const std::string key = file + "__" + vp.name;
            const fs::path failDir = out / "failures";
            Image before;
            if (!loadPng(refPng.string(), before)) {
                failures.push_back(story.name + " " + vp.name + ": no reference image (run with --update-references)");
                fs::create_directories(failDir, ec);
                savePng((failDir / (key + ".after.png")).string(), thumb);
            } else {
                Image diff;
                const CompareResult r = compareImages(before, thumb, o, diff);
                if (!r.ok) {
                    failures.push_back(story.name + " " + vp.name + ": " + r.why);
                    fs::create_directories(failDir, ec);
                    savePng((failDir / (key + ".before.png")).string(), before);
                    savePng((failDir / (key + ".after.png")).string(), thumb);
                    if (!diff.rgba.empty()) savePng((failDir / (key + ".diff.png")).string(), diff);
                }
            }
            const std::vector<std::string> a = lines(readText(refLayout)), b = lines(layout);
            bool same = a.size() == b.size();
            for (std::size_t i = 0; same && i < a.size(); ++i) same = sameLayoutLine(a[i], b[i]);
            if (!same) {
                failures.push_back(story.name + " " + vp.name + ": layout changed (see failures/" + key + ".layout.txt)");
                fs::create_directories(failDir, ec);
                std::string text = "--- reference\n";
                for (const std::string& l : a) text += "- " + l + "\n";
                text += "+++ now\n";
                for (const std::string& l : b) text += "+ " + l + "\n";
                writeText(failDir / (key + ".layout.txt"), text);
            }
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - storyStart).count();
        std::printf("ui_gallery: %-36s %5.1f s\n", story.name.c_str(), seconds);
        std::fflush(stdout);
    }
    writeText(out / "index.html", html.str());

    int unknownErrors = 0, knownErrors = 0, info = 0;
    std::ostringstream report;
    report << "# UI gallery lint\n\n";
    for (Finding& f : findings) {
        matchKnown(f, known);
        if (!f.error) ++info;
        else if (f.known.empty()) ++unknownErrors;
        else ++knownErrors;
    }
    const auto section = [&](const char* title, auto pick) {
        report << "## " << title << "\n\n";
        int n = 0;
        for (const Finding& f : findings) {
            if (!pick(f)) continue;
            ++n;
            report << "- " << f.rule << " | " << f.story << " | " << f.viewport << " | " << f.detail;
            if (!f.known.empty()) report << "  (known: " << f.known << ")";
            report << "\n";
        }
        if (!n) report << "(none)\n";
        report << "\n";
    };
    section("New problems (fail the run)", [](const Finding& f) { return f.error && f.known.empty(); });
    section("Known problems (#214)", [](const Finding& f) { return f.error && !f.known.empty(); });
    section("For the #214 audit (never fail)", [](const Finding& f) { return !f.error; });
    int stale = 0;
    report << "## Known issues that no longer occur\n\n";
    const bool fullRun = o.storyFilter == "*" && o.viewportFilter == "*" && o.lint && o.inputLint;
    for (const KnownIssue& k : known) {
        if (k.seen || !fullRun) continue;
        ++stale;
        report << "- line " << k.line << ": " << k.rule << " | " << k.story << " | " << k.viewport << " | " << k.detail << "\n";
    }
    if (!stale) report << "(none)\n";
    report << "\n## Reference comparison\n\n";
    for (const std::string& f : failures) report << "- " << f << "\n";
    if (failures.empty()) report << (refs.empty() ? "(not compared)\n" : o.updateReferences ? "(references updated)\n" : "(all match)\n");
    writeText(out / "lint.md", report.str());

    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("ui_gallery: %d captures in %.1f s -> %s\n", captured, seconds, (out / "index.html").string().c_str());
    std::printf("ui_gallery: lint: %d new problems, %d known (#214), %d notes, %d stale known entries (see lint.md)\n",
                unknownErrors, knownErrors, info, stale);
    for (const Finding& f : findings)
        if (f.error && f.known.empty())
            std::printf("  NEW %s | %s | %s | %s\n", f.rule.c_str(), f.story.c_str(), f.viewport.c_str(), f.detail.c_str());
    if (!refs.empty() && !o.updateReferences)
        std::printf("ui_gallery: references: %zu differences%s\n", failures.size(),
                    failures.empty() ? "" : " (before/after/diff in failures/)");
    for (const std::string& f : failures) std::printf("  DIFF %s\n", f.c_str());
    if (captured == 0) {
        std::fprintf(stderr, "ui_gallery: no story matched\n");
        return 2;
    }
    return (unknownErrors > 0 || !failures.empty()) ? 1 : 0;
}

} // namespace pikmin::launcher::gallery
