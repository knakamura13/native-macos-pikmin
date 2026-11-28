// Small Storybook-like controls for the gallery window's chrome (#215).
// Every clickable one shows the hand cursor; none ever cuts its label.
#pragma once

#include "theme.h"

#include "imgui.h"

#include <string>

namespace pikmin::launcher::gallery::tool {

enum class Icon {
    Reload, ZoomIn, ZoomOut, ZoomReset, Background, Outline, Fullscreen, Save, Search,
    ChevronRight, ChevronDown, ChevronUp, Component, Screen, Story, Play, Stop, Check, Cross, Warning, Info,
};

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 centre, float size, ImU32 col);

// Shows the hand cursor over the last item when it can be clicked.
void handCursor();

// Square toolbar button with an icon; `on` draws it highlighted. Tooltip on hover.
bool iconButton(const char* id, Icon icon, const char* tooltip, bool on = false, bool enabled = true);
// Rounded text button. `primary` = filled accent.
bool textButton(const char* label, bool primary = false, bool enabled = true, Icon icon = Icon::Info, bool withIcon = false);
// Joined options ("1x | 2x"); returns true when `value` changed.
bool segmented(const char* id, const char* const* labels, int count, int& value);
// Text field over a std::string. `hint` shows when empty.
bool inputText(const char* id, std::string& text, const char* hint = nullptr, float width = -1.0f, bool multiline = false,
               float height = 0.0f);
// Storybook's boolean control: "False | True".
bool toggle(const char* id, bool& value);
// Small rounded count, e.g. on a tab.
void badge(ImDrawList* dl, ImVec2 at, const char* text, ImU32 bg, ImU32 fg);
float badgeWidth(const char* text);
// Tab in a tab row: label, optional count, accent underline when selected.
bool tab(const char* label, int count, bool selected, ImU32 countColour);
// Thin draggable bar between panes. Returns the drag delta along `vertical ? x : y`.
float splitter(const char* id, bool vertical, ImVec2 size);
// Plain text in a colour and font.
void text(ImFont* font, ImU32 col, const char* s);
void textWrapped(ImFont* font, ImU32 col, const char* s);

} // namespace pikmin::launcher::gallery::tool
