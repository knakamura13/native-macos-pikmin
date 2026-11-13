#include "widgets.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace pikmin::launcher::gallery::tool {

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col)
{
    const float r = s * 0.5f, t = std::max(1.0f, s * 0.11f);
    const auto line = [&](float x0, float y0, float x1, float y1) {
        dl->AddLine(ImVec2(c.x + x0 * r, c.y + y0 * r), ImVec2(c.x + x1 * r, c.y + y1 * r), col, t);
    };
    switch (icon) {
    case Icon::Reload:
        dl->PathArcTo(c, r * 0.75f, 0.6f, 5.6f, 24);
        dl->PathStroke(col, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.95f, c.y - r * 0.05f), ImVec2(c.x + r * 0.30f, c.y - r * 0.05f),
                              ImVec2(c.x + r * 0.62f, c.y + r * 0.45f), col);
        break;
    case Icon::ZoomIn:
    case Icon::ZoomOut:
    case Icon::Search:
        dl->AddCircle(ImVec2(c.x - r * 0.15f, c.y - r * 0.15f), r * 0.58f, col, 20, t);
        line(0.30f, 0.30f, 0.85f, 0.85f);
        if (icon != Icon::Search) line(-0.45f, -0.15f, 0.15f, -0.15f);
        if (icon == Icon::ZoomIn) line(-0.15f, -0.45f, -0.15f, 0.15f);
        break;
    case Icon::ZoomReset:
        dl->AddCircle(ImVec2(c.x - r * 0.15f, c.y - r * 0.15f), r * 0.58f, col, 20, t);
        line(0.30f, 0.30f, 0.85f, 0.85f);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.15f, c.y - r * 0.15f), r * 0.16f, col);
        break;
    case Icon::Background: {
        const ImVec2 a(c.x - r * 0.8f, c.y - r * 0.8f), b(c.x + r * 0.8f, c.y + r * 0.8f);
        dl->AddRect(a, b, col, r * 0.2f, 0, t);
        const float h = (b.x - a.x) * 0.5f;
        dl->AddRectFilled(a, ImVec2(a.x + h, a.y + h), col, r * 0.2f, ImDrawFlags_RoundCornersTopLeft);
        dl->AddRectFilled(ImVec2(a.x + h, a.y + h), b, col, r * 0.2f, ImDrawFlags_RoundCornersBottomRight);
        break;
    }
    case Icon::Outline: {
        const float k = 0.8f;
        for (int i = 0; i < 4; ++i) { // dashed square
            const float p = -k + i * 0.45f;
            line(p, -k, std::min(k, p + 0.25f), -k);
            line(p, k, std::min(k, p + 0.25f), k);
            line(-k, p, -k, std::min(k, p + 0.25f));
            line(k, p, k, std::min(k, p + 0.25f));
        }
        break;
    }
    case Icon::Fullscreen:
        line(-0.8f, -0.8f, -0.3f, -0.8f); line(-0.8f, -0.8f, -0.8f, -0.3f);
        line(0.8f, -0.8f, 0.3f, -0.8f);   line(0.8f, -0.8f, 0.8f, -0.3f);
        line(-0.8f, 0.8f, -0.3f, 0.8f);   line(-0.8f, 0.8f, -0.8f, 0.3f);
        line(0.8f, 0.8f, 0.3f, 0.8f);     line(0.8f, 0.8f, 0.8f, 0.3f);
        break;
    case Icon::Save:
        line(0.0f, -0.85f, 0.0f, 0.35f);
        line(-0.42f, -0.07f, 0.0f, 0.35f);
        line(0.42f, -0.07f, 0.0f, 0.35f);
        line(-0.8f, 0.85f, 0.8f, 0.85f);
        break;
    case Icon::ChevronRight:
        line(-0.25f, -0.5f, 0.25f, 0.0f); line(0.25f, 0.0f, -0.25f, 0.5f);
        break;
    case Icon::ChevronDown:
        line(-0.5f, -0.25f, 0.0f, 0.25f); line(0.0f, 0.25f, 0.5f, -0.25f);
        break;
    case Icon::ChevronUp:
        line(-0.5f, 0.25f, 0.0f, -0.25f); line(0.0f, -0.25f, 0.5f, 0.25f);
        break;
    case Icon::Component: // four squares
        for (int i = 0; i < 4; ++i) {
            const float x = (i % 2) ? 0.1f : -0.8f, y = (i / 2) ? 0.1f : -0.8f;
            dl->AddRectFilled(ImVec2(c.x + x * r, c.y + y * r), ImVec2(c.x + (x + 0.7f) * r, c.y + (y + 0.7f) * r), col,
                              r * 0.12f);
        }
        break;
    case Icon::Screen:
        dl->AddRect(ImVec2(c.x - r * 0.85f, c.y - r * 0.7f), ImVec2(c.x + r * 0.85f, c.y + r * 0.45f), col, r * 0.15f, 0, t);
        line(-0.35f, 0.8f, 0.35f, 0.8f);
        break;
    case Icon::Story: // bookmark
        dl->PathLineTo(ImVec2(c.x - r * 0.55f, c.y - r * 0.85f));
        dl->PathLineTo(ImVec2(c.x + r * 0.55f, c.y - r * 0.85f));
        dl->PathLineTo(ImVec2(c.x + r * 0.55f, c.y + r * 0.85f));
        dl->PathLineTo(ImVec2(c.x, c.y + r * 0.4f));
        dl->PathLineTo(ImVec2(c.x - r * 0.55f, c.y + r * 0.85f));
        dl->PathFillConvex(col);
        break;
    case Icon::Play:
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.5f, c.y - r * 0.7f), ImVec2(c.x + r * 0.7f, c.y),
                              ImVec2(c.x - r * 0.5f, c.y + r * 0.7f), col);
        break;
    case Icon::Stop:
        dl->AddRectFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.55f), ImVec2(c.x + r * 0.55f, c.y + r * 0.55f), col, r * 0.1f);
        break;
    case Icon::Check:
        line(-0.6f, 0.0f, -0.15f, 0.5f); line(-0.15f, 0.5f, 0.65f, -0.5f);
        break;
    case Icon::Cross:
        line(-0.5f, -0.5f, 0.5f, 0.5f); line(0.5f, -0.5f, -0.5f, 0.5f);
        break;
    case Icon::Warning:
        dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x + r * 0.9f, c.y + r * 0.75f),
                              ImVec2(c.x - r * 0.9f, c.y + r * 0.75f), col);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.3f), ImVec2(c.x, c.y + r * 0.2f), IM_COL32_WHITE, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.45f), t * 0.6f, IM_COL32_WHITE);
        break;
    case Icon::Info:
        dl->AddCircleFilled(c, r * 0.85f, col);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.1f), ImVec2(c.x, c.y + r * 0.45f), IM_COL32_WHITE, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.4f), t * 0.6f, IM_COL32_WHITE);
        break;
    }
}

void handCursor()
{
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}

bool iconButton(const char* id, Icon icon, const char* tooltip, bool on, bool enabled)
{
    const float h = ImGui::GetFrameHeight();
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(h, h));
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    if (enabled) handCursor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (on) dl->AddRectFilled(at, ImVec2(at.x + h, at.y + h), color::kAccentSoft, 4.0f);
    else if (hovered && enabled) dl->AddRectFilled(at, ImVec2(at.x + h, at.y + h), color::rgb(0x029CFD, 14), 4.0f);
    const ImU32 col = !enabled ? color::rgb(0xB8C2CA) : (on || (hovered && enabled)) ? color::kAccent : color::kMuted;
    drawIcon(dl, icon, ImVec2(at.x + h * 0.5f, at.y + h * 0.5f), h * 0.46f, col);
    if (hovered && tooltip) ImGui::SetItemTooltip("%s", tooltip);
    return clicked && enabled;
}

bool textButton(const char* label, bool primary, bool enabled, Icon icon, bool withIcon)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    const float iconW = withIcon ? ts.y + st.ItemInnerSpacing.x : 0.0f;
    const ImVec2 size(ts.x + iconW + st.FramePadding.x * 2.0f, ImGui::GetFrameHeight());
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::BeginDisabled(!enabled);
    const bool clicked = ImGui::InvisibleButton(label, size);
    ImGui::EndDisabled();
    const bool hovered = enabled && ImGui::IsItemHovered();
    const bool held = enabled && ImGui::IsItemActive();
    if (enabled) handCursor();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 b(at.x + size.x, at.y + size.y);
    ImU32 bg = primary ? color::kAccent : color::kContent, fg = primary ? IM_COL32_WHITE : color::kText;
    if (primary && hovered) bg = color::rgb(0x0290EA);
    if (primary && held) bg = color::rgb(0x0280D0);
    if (!primary && hovered) bg = color::rgb(0xF3F8FC);
    if (!enabled) {
        bg = primary ? color::rgb(0xB9DDF5) : color::kContent;
        fg = primary ? IM_COL32_WHITE : color::kFaint;
    }
    dl->AddRectFilled(at, b, bg, 4.0f);
    if (!primary) dl->AddRect(at, b, hovered ? color::rgb(0x029CFD, 120) : color::kBorder, 4.0f);
    float x = at.x + st.FramePadding.x;
    if (withIcon) {
        drawIcon(dl, icon, ImVec2(x + ts.y * 0.5f, at.y + size.y * 0.5f), ts.y * 0.8f, primary ? fg : color::kAccent);
        x += iconW;
    }
    dl->AddText(ImVec2(x, at.y + (size.y - ts.y) * 0.5f), fg, label, end);
    return clicked && enabled;
}

bool segmented(const char* id, const char* const* labels, int count, int& value)
{
    ImGui::PushID(id);
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h = ImGui::GetFrameHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    float total = 0;
    for (int i = 0; i < count; ++i) total += ImGui::CalcTextSize(labels[i]).x + st.FramePadding.x * 2.0f;
    dl->AddRectFilled(start, ImVec2(start.x + total, start.y + h), color::rgb(0xEDF1F5), 4.0f);
    bool changed = false;
    float x = start.x;
    for (int i = 0; i < count; ++i) {
        const float w = ImGui::CalcTextSize(labels[i]).x + st.FramePadding.x * 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(x, start.y));
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##seg", ImVec2(w, h)) && value != i) {
            value = i;
            changed = true;
        }
        handCursor();
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();
        if (value == i) {
            dl->AddRectFilled(ImVec2(x + 2, start.y + 2), ImVec2(x + w - 2, start.y + h - 2), color::kContent, 3.0f);
            dl->AddRect(ImVec2(x + 2, start.y + 2), ImVec2(x + w - 2, start.y + h - 2), color::rgb(0x029CFD, 90), 3.0f);
        }
        const ImU32 fg = value == i ? color::kAccent : hovered ? color::kText : color::kMuted;
        const ImVec2 ts = ImGui::CalcTextSize(labels[i]);
        dl->AddText(ImVec2(x + (w - ts.x) * 0.5f, start.y + (h - ts.y) * 0.5f), fg, labels[i]);
        x += w;
    }
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(total, h));
    ImGui::PopID();
    return changed;
}

namespace {
int resizeCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* str = static_cast<std::string*>(data->UserData);
        str->resize(std::size_t(data->BufTextLen));
        data->Buf = str->data();
    }
    return 0;
}
} // namespace

bool inputText(const char* id, std::string& text, const char* hint, float width, bool multiline, float height)
{
    if (width != 0.0f) ImGui::SetNextItemWidth(width);
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (multiline)
        return ImGui::InputTextMultiline(id, text.data(), text.capacity() + 1, ImVec2(width, height),
                                         flags, resizeCallback, &text);
    if (hint) return ImGui::InputTextWithHint(id, hint, text.data(), text.capacity() + 1, flags, resizeCallback, &text);
    return ImGui::InputText(id, text.data(), text.capacity() + 1, flags, resizeCallback, &text);
}

bool toggle(const char* id, bool& value)
{
    static const char* const labels[] = { "False", "True" };
    int v = value ? 1 : 0;
    const bool changed = segmented(id, labels, 2, v);
    value = v == 1;
    return changed;
}

float badgeWidth(const char* s)
{
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * 0.86f;
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, s).x + size * 0.9f;
}

void badge(ImDrawList* dl, ImVec2 at, const char* s, ImU32 bg, ImU32 fg)
{
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * 0.86f;
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, s);
    const float w = ts.x + size * 0.9f, h = size * 1.45f;
    dl->AddRectFilled(at, ImVec2(at.x + w, at.y + h), bg, h * 0.5f);
    dl->AddText(font, size, ImVec2(at.x + (w - ts.x) * 0.5f, at.y + (h - ts.y) * 0.5f), fg, s);
}

bool tab(const char* label, int count, bool selected, ImU32 countColour)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    const char* end = ImGui::FindRenderedTextEnd(label);
    const ImVec2 ts = ImGui::CalcTextSize(label, end);
    char countText[16] = "";
    if (count > 0) std::snprintf(countText, sizeof countText, "%d", count);
    const float bw = count > 0 ? badgeWidth(countText) + st.ItemInnerSpacing.x : 0.0f;
    const float h = ImGui::GetFrameHeight() + st.FramePadding.y * 2.0f;
    const ImVec2 size(ts.x + bw + st.FramePadding.x * 2.0f, h);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, size);
    handCursor();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fg = selected ? color::kAccent : hovered ? color::kText : color::kMuted;
    dl->AddText(ImVec2(at.x + st.FramePadding.x, at.y + (h - ts.y) * 0.5f), fg, label, end);
    if (count > 0) {
        const float size2 = ImGui::GetFontSize() * 0.86f;
        badge(dl, ImVec2(at.x + st.FramePadding.x + ts.x + st.ItemInnerSpacing.x, at.y + (h - size2 * 1.45f) * 0.5f),
              countText, countColour, IM_COL32_WHITE);
    }
    if (selected) dl->AddRectFilled(ImVec2(at.x + 2, at.y + h - 3), ImVec2(at.x + size.x - 2, at.y + h), color::kAccent, 1.5f);
    return clicked;
}

float splitter(const char* id, bool vertical, ImVec2 size)
{
    const ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, size);
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if (hovered || active) ImGui::SetMouseCursor(vertical ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (hovered || active)
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), color::rgb(0x029CFD, active ? 120 : 60));
    if (!active) return 0.0f;
    return vertical ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
}

void text(ImFont* font, ImU32 col, const char* s)
{
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void textWrapped(ImFont* font, ImU32 col, const char* s)
{
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(s);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

} // namespace pikmin::launcher::gallery::tool
