#include "theme.h"

#include "ui_gallery_font_bold.h"    // tool/fonts/NotoSans-Bold.ttf (Latin subset, OFL)
#include "ui_gallery_font_regular.h" // tool/fonts/NotoSans-Regular.ttf

namespace pikmin::launcher::gallery::tool {

namespace {
ImVec4 v4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

ImFont* add(const unsigned char* data, std::size_t size, float points, float pixelScale, float uiScale)
{
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = false;
    ImFont* font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(data), int(size),
                                                              points * uiScale * pixelScale, &cfg);
    font->Scale = 1.0f / pixelScale; // laid out in points, drawn at the pixel size
    return font;
}
} // namespace

Fonts loadFonts(float pixelScale, float uiScale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    Fonts f;
    f.body = add(kNotoSansRegular, kNotoSansRegularSize, 14.0f, pixelScale, uiScale);
    f.bold = add(kNotoSansBold, kNotoSansBoldSize, 14.0f, pixelScale, uiScale);
    f.small = add(kNotoSansRegular, kNotoSansRegularSize, 12.0f, pixelScale, uiScale);
    f.smallBold = add(kNotoSansBold, kNotoSansBoldSize, 11.0f, pixelScale, uiScale);
    f.title = add(kNotoSansBold, kNotoSansBoldSize, 16.0f, pixelScale, uiScale);
    io.FontDefault = f.body;
    io.Fonts->Build();
    return f;
}

void applyTheme(float uiScale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(10, 6);
    s.ItemSpacing = ImVec2(8, 8);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.CellPadding = ImVec2(10, 7);
    s.WindowRounding = 0;
    s.ChildRounding = 0;
    s.FrameRounding = 4;
    s.PopupRounding = 6;
    s.GrabRounding = 4;
    s.TabRounding = 0;
    s.ScrollbarRounding = 6;
    s.ScrollbarSize = 10;
    s.GrabMinSize = 12;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 1;
    s.PopupBorderSize = 1;
    s.TabBorderSize = 0;
    s.TabBarBorderSize = 1;
    s.SelectableTextAlign = ImVec2(0, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = v4(color::kText);
    c[ImGuiCol_TextDisabled] = v4(color::kFaint);
    c[ImGuiCol_WindowBg] = v4(color::kContent);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = v4(color::kContent);
    c[ImGuiCol_Border] = v4(color::kBorder);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = v4(color::kContent);
    c[ImGuiCol_FrameBgHovered] = v4(color::rgb(0xF3F7FA));
    c[ImGuiCol_FrameBgActive] = v4(color::rgb(0xEAF4FD));
    c[ImGuiCol_TitleBg] = v4(color::kAppBg);
    c[ImGuiCol_TitleBgActive] = v4(color::kAppBg);
    c[ImGuiCol_MenuBarBg] = v4(color::kAppBg);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = v4(color::rgb(0xC5CED6));
    c[ImGuiCol_ScrollbarGrabHovered] = v4(color::rgb(0xA9B5BF));
    c[ImGuiCol_ScrollbarGrabActive] = v4(color::rgb(0x8A959E));
    c[ImGuiCol_CheckMark] = v4(color::kAccent);
    c[ImGuiCol_SliderGrab] = v4(color::kAccent);
    c[ImGuiCol_SliderGrabActive] = v4(color::rgb(0x0284D8));
    c[ImGuiCol_Button] = v4(color::kContent);
    c[ImGuiCol_ButtonHovered] = v4(color::rgb(0xEAF4FD));
    c[ImGuiCol_ButtonActive] = v4(color::rgb(0xD6ECFC));
    c[ImGuiCol_Header] = v4(color::kAccentSoft);
    c[ImGuiCol_HeaderHovered] = v4(color::rgb(0x029CFD, 18));
    c[ImGuiCol_HeaderActive] = v4(color::rgb(0x029CFD, 40));
    c[ImGuiCol_Separator] = v4(color::kBorder);
    c[ImGuiCol_SeparatorHovered] = v4(color::kAccent);
    c[ImGuiCol_SeparatorActive] = v4(color::kAccent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = v4(color::rgb(0x029CFD, 14));
    c[ImGuiCol_TabSelected] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmed] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabDimmedSelected] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableHeaderBg] = v4(color::kAppBg);
    c[ImGuiCol_TableBorderStrong] = v4(color::kBorder);
    c[ImGuiCol_TableBorderLight] = v4(color::kBorder);
    c[ImGuiCol_TableRowBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TextSelectedBg] = v4(color::rgb(0x029CFD, 70));
    c[ImGuiCol_NavCursor] = v4(color::kAccent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.1f, 0.12f, 0.14f, 0.25f);
    s.ScaleAllSizes(uiScale);
}

} // namespace pikmin::launcher::gallery::tool
