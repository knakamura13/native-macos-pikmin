// Dear ImGui's test-engine hooks (IMGUI_ENABLE_TEST_ENGINE, imgui_internal.h):
// every widget ImGui lays out reports its id, rect and label here, so the
// gallery's lint sees all clickables without changes to the launcher.
#include "gallery_host.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdarg>

using pikmin::launcher::gallery::activeSession;

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item)
{
    if (!activeSession() || id == 0) return;
    // `bb` is the nav rect; the item's own rect is in `item` when there is one.
    const ImRect r = item ? item->Rect : bb;
    const bool disabled = item && (item->ItemFlags & ImGuiItemFlags_Disabled);
    activeSession()->itemAdd(id, r.Min.x, r.Min.y, r.Max.x, r.Max.y, ctx->CurrentWindow, disabled);
}

void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID id, const char* label, ImGuiItemStatusFlags)
{
    if (activeSession()) activeSession()->itemInfo(id, label);
}

void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}

const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }
