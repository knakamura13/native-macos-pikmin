// ui_gallery (#215): Storybook-style browser for the launcher's screens and
// components, and the headless capture + compare + lint run CI uses.
//
//   ui_gallery [window options]         Storybook-style window (tool/storybook.cpp)
//       --story GLOB --viewport NAME   open this story at this size ("1440x900@2x", "phone-2400x1080")
//       --references DIR --known-issues FILE   default: the source tree's
//       --ui-scale N  --size WxH       chrome scale (default: display DPI), window size
//       --screenshot FILE [--frames N] [--tab controls|visual|layout|actions] [--outline]
//                                      [--run-tests story|all]   save the window and quit
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

#include "tool/storybook.h"

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
              "                  [--known-issues FILE] [--story GLOB] [--viewport GLOB] [--no-lint] [--no-input-lint]]\n"
              "       ui_gallery [--story GLOB] [--viewport NAME] [--references DIR] [--known-issues FILE]\n"
              "                  [--ui-scale N] [--size WxH] [--screenshot FILE [--frames N] [--tab NAME]\n"
              "                  [--outline] [--run-tests story|all]]");
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    for (int sig : { SIGSEGV, SIGABRT, SIGFPE, SIGILL }) std::signal(sig, onCrash);
#ifdef SIGBUS
    std::signal(SIGBUS, onCrash);
#endif
    CaptureOptions options;
    pikmin::launcher::gallery::tool::StorybookOptions window;
    bool capture = false;
    bool referencesGiven = false, knownGiven = false;
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
            referencesGiven = true;
        } else if (a == "--update-references") {
            options.updateReferences = true;
        } else if (a == "--known-issues") {
            options.knownIssues = next();
            knownGiven = true;
        } else if (a == "--story") {
            options.storyFilter = next();
        } else if (a == "--viewport") {
            options.viewportFilter = next();
        } else if (a == "--no-lint") {
            options.lint = false;
        } else if (a == "--no-input-lint") {
            options.inputLint = false;
        } else if (a == "--ui-scale") {
            window.uiScale = float(std::atof(next().c_str()));
        } else if (a == "--size") {
            const std::string v = next();
            if (std::sscanf(v.c_str(), "%dx%d", &window.width, &window.height) != 2) return usage();
        } else if (a == "--screenshot") {
            window.screenshot = next();
        } else if (a == "--frames") {
            window.frames = std::atoi(next().c_str());
        } else if (a == "--tab") {
            window.tab = next();
        } else if (a == "--outline") {
            window.outline = true;
        } else if (a == "--run-tests") {
            window.runTests = next();
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
#ifdef UI_GALLERY_SOURCE_DIR
    // The window compares with (and accepts into) the source tree's references.
    if (!referencesGiven) options.referenceDir = UI_GALLERY_SOURCE_DIR "/references";
    if (!knownGiven) options.knownIssues = UI_GALLERY_SOURCE_DIR "/known_issues.txt";
#endif
    if (!fs::is_directory(fs::u8path(options.referenceDir), ec)) options.referenceDir.clear();
    if (!fs::is_regular_file(fs::u8path(options.knownIssues), ec)) options.knownIssues.clear();
    window.referenceDir = options.referenceDir;
    window.knownIssues = options.knownIssues;
    window.story = options.storyFilter;
    window.viewport = options.viewportFilter;
    return pikmin::launcher::gallery::tool::runStorybook(window);
}
