// Look of the gallery window's own chrome (#215): a light, Storybook-like
// theme. Only the tool is styled here; stories keep the launcher's look.
#pragma once

#include "imgui.h"

namespace pikmin::launcher::gallery::tool {

namespace color {
constexpr ImU32 rgb(unsigned hex, unsigned alpha = 255)
{
    return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, alpha);
}
inline constexpr ImU32 kAppBg = rgb(0xF6F9FC);     // sidebar, panels
inline constexpr ImU32 kContent = rgb(0xFFFFFF);   // toolbar, addon panel
inline constexpr ImU32 kBorder = rgb(0xDDE3EA);
inline constexpr ImU32 kText = rgb(0x2E3438);
inline constexpr ImU32 kMuted = rgb(0x5C6870);
inline constexpr ImU32 kFaint = rgb(0x8A959E);
inline constexpr ImU32 kAccent = rgb(0x029CFD);
inline constexpr ImU32 kAccentSoft = rgb(0x029CFD, 26); // hover / selected chips
inline constexpr ImU32 kRed = rgb(0xE4380E);
inline constexpr ImU32 kGreen = rgb(0x4CA02C);
inline constexpr ImU32 kOrange = rgb(0xE68A00);
inline constexpr ImU32 kCanvasLight = rgb(0xEEF1F4);
inline constexpr ImU32 kCanvasDark = rgb(0x24282C);
} // namespace color

struct Fonts {
    ImFont* body = nullptr;      // 14 pt
    ImFont* bold = nullptr;      // 14 pt bold
    ImFont* small = nullptr;     // 12 pt
    ImFont* smallBold = nullptr; // 11 pt bold, section headers
    ImFont* title = nullptr;     // 16 pt bold
};

// Rebuilds the font atlas. `pixelScale` = framebuffer pixels per window point
// (2 on Retina); `uiScale` = extra size for high-DPI displays that report
// points as pixels. Glyphs are rasterized at the pixel size, so text is sharp.
Fonts loadFonts(float pixelScale, float uiScale);
void applyTheme(float uiScale);

} // namespace pikmin::launcher::gallery::tool
