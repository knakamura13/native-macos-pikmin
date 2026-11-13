// Headless capture, reference comparison and layout lint for the UI gallery (#215).
#pragma once

#include "gallery_host.h"

#include <string>
#include <vector>

namespace pikmin::launcher::gallery {

// The size matrix every screen story is drawn at (#214).
const std::vector<Viewport>& captureViewports();
const Viewport* findViewport(const std::string& name);
// Components draw at their own canvas size, at 1x and 2x.
std::vector<Viewport> viewportsFor(const Story& story);

struct Finding {
    std::string rule;     // "overlap", "covered-by-toast", "truncated-label"...
    std::string story, viewport;
    std::string detail;
    bool error = true;    // false: reported for #214, never fails the run
    std::string known;    // matching known_issues.txt note, if any
    std::vector<ImVec4> rects; // the boxes involved, in UI points (x0, y0, x1, y1)
};

// Rules on one settled frame: overlaps, covered controls, off-screen or
// clipped controls, truncated text, small touch targets, shortened paths.
std::vector<Finding> lintFrame(const Session& session, const Story& story, const Viewport& viewport);
// Rules that need the pointer or the keyboard: every control under the
// pointer shows the hand cursor and is the one hovered; focus is visible.
std::vector<Finding> lintInput(const Story& story, const Viewport& viewport);

struct KnownIssue {
    std::string rule, story, viewport, detail, note;
    int line = 0;
    bool seen = false;
};
std::vector<KnownIssue> loadKnownIssues(const std::string& path, std::string& error);
// Marks `f.known` when a known issue matches (globs: * and ?).
void matchKnown(Finding& f, std::vector<KnownIssue>& known);

struct CaptureOptions {
    std::string outDir;
    std::string referenceDir;   // empty: no comparison
    std::string knownIssues;    // empty: every lint error fails
    bool updateReferences = false;
    bool lint = true;
    bool inputLint = true;
    std::string storyFilter = "*";
    std::string viewportFilter = "*";
    int thumbnailWidth = 256;   // reference images are stored at this width
    float tolerancePercent = 0.5f; // share of thumbnail pixels allowed to differ
    int channelTolerance = 40;     // per-channel difference that counts as a changed pixel
};

// Returns the process exit code: 0 when every comparison and lint rule passes.
int runCapture(const CaptureOptions& options);

// What runCapture is drawing right now ("story @ viewport"), for crash reports.
const char* currentCapture();

// Layout snapshot as stored next to each reference (.layout.txt).
std::string layoutText(const Session& session);
// Same widgets, each coordinate within one point (the comparison the test uses).
bool sameLayout(const std::string& reference, const std::string& now);

// One story at one viewport against the stored reference.
struct ReferenceCheck {
    bool hasReference = false;
    bool imageOk = false, layoutOk = false;
    std::string why;               // empty when both match
    Image reference, current, diff; // thumbnails (diff: changed pixels in red)
};
ReferenceCheck checkReference(const std::string& referenceDir, const Story& story, const Viewport& viewport,
                              const Image& shot, const std::string& layout, const CaptureOptions& options);
// Writes `shot` (as a thumbnail) and `layout` as the new reference.
bool acceptReference(const std::string& referenceDir, const Story& story, const Viewport& viewport, const Image& shot,
                     const std::string& layout, const CaptureOptions& options, std::string& error);
// Low-res texture notes, as runCapture lists them.
std::vector<Finding> lowResFindings(const std::vector<LowRes>& lowRes, const Story& story, const Viewport& viewport);

bool globMatch(const std::string& pattern, const std::string& text);
std::string storyFileName(const std::string& story); // "hub/home" -> "hub__home"

} // namespace pikmin::launcher::gallery
