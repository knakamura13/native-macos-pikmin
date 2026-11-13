// Layout probe for the UI gallery's lint (ui_gallery/, #215). The launcher
// reports what ImGui cannot see by itself: text it fitted into a box (button
// labels, toasts, paths), notices drawn on top of everything and the logo.
// Nothing listens in the launcher, so each report is one null check.
#pragma once

#include <string>

namespace pikmin::launcher::probe {

enum class Kind {
    Label,  // button label fitted into its button (rect = the button)
    Toast,  // short notice at the bottom, drawn over everything
    Notice, // release notice pill at the top
    Logo,   // the Open Nectar logo
    Path,   // a path shortened from the front ("...folder/file.iso"), no rect
};

struct Report {
    Kind kind = Kind::Label;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    std::string text;  // what the launcher wanted to show
    std::string shown; // what it drew
    bool truncated = false;
    bool shrunk = false; // label drawn smaller than the standard size
};

using Sink = void (*)(const Report&);

inline Sink& sink()
{
    static Sink s = nullptr;
    return s;
}

inline void report(Kind kind, float x0, float y0, float x1, float y1, const std::string& text, const std::string& shown,
                   bool truncated, bool shrunk = false)
{
    if (Sink s = sink()) s(Report { kind, x0, y0, x1, y1, text, shown, truncated, shrunk });
}

} // namespace pikmin::launcher::probe
