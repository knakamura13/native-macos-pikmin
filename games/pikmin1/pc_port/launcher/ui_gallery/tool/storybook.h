// The ui_gallery window (#215): a Storybook-like browser for the launcher's
// stories. Sidebar with search and a story tree, a toolbar over the canvas,
// and an addon panel (Controls, Visual tests, Layout, Actions). Only the
// tool's chrome lives here; the stories themselves are in ../stories.cpp.
#pragma once

#include <string>

namespace pikmin::launcher::gallery::tool {

struct StorybookOptions {
    std::string referenceDir; // reference thumbnails for Visual tests / Run tests
    std::string knownIssues;  // known_issues.txt
    std::string story;        // story to open (glob; first match)
    std::string viewport;     // e.g. "1440x900@2x", "phone-2400x1080"
    float uiScale = 0.0f;     // 0: from the display DPI
    int width = 1440, height = 900;
    // Scripted screenshot of the window (docs, smoke test): open with these,
    // draw `frames` frames, save the window to `screenshot` and quit.
    std::string screenshot;
    int frames = 30;
    std::string tab;          // controls | visual | layout | actions
    bool outline = false;
    std::string runTests;     // "story" or "all": start a test run first
};

int runStorybook(const StorybookOptions& options);

} // namespace pikmin::launcher::gallery::tool
