// CPU renderer checks (#215): shared triangle edges are drawn once, fills are
// exact, textures sample like GL_LINEAR at texel centres, clip rects cut.
#include "soft_raster.h"

#include "imgui.h"

#define STB_IMAGE_IMPLEMENTATION // soft_raster.cpp reads PNGs; launcher_gui.cpp provides this in the gallery
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdio>
#include <cstdlib>

using namespace pikmin::launcher::gallery;

namespace {

int failures = 0;

void check(bool ok, const char* what)
{
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

const unsigned char* px(const Image& img, int x, int y) { return &img.rgba[(std::size_t(y) * img.width + x) * 4]; }

} // namespace

int main()
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(64, 32);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    SoftRenderer renderer;
    unsigned char* atlas = nullptr;
    int aw = 0, ah = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlas, &aw, &ah);
    io.Fonts->SetTexID(ImTextureID(renderer.create(atlas, aw, ah)));
    const unsigned char texel[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
    const unsigned tex = renderer.create(texel, 2, 2);

    ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->Flags &= ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines);
    // Two half-transparent quads side by side (4 triangles, shared edges).
    dl->AddRectFilled(ImVec2(0, 0), ImVec2(16, 16), IM_COL32(255, 255, 255, 128));
    dl->AddRectFilled(ImVec2(16, 0), ImVec2(32, 16), IM_COL32(255, 255, 255, 128));
    // A 2x2 texture drawn 1:1: each pixel is one texel.
    dl->AddImage(ImTextureID(tex), ImVec2(40, 0), ImVec2(42, 2));
    // Clipped: only the left half of this rect may show.
    dl->PushClipRect(ImVec2(0, 20), ImVec2(8, 32));
    dl->AddRectFilled(ImVec2(0, 20), ImVec2(16, 28), IM_COL32(255, 0, 0, 255));
    dl->PopClipRect();
    ImGui::Render();

    Image out;
    const float black[4] = { 0, 0, 0, 1 };
    renderer.render(ImGui::GetDrawData(), out, black);
    check(out.width == 64 && out.height == 32, "framebuffer is the display size");
    bool uniform = true;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x)
            if (px(out, x, y)[0] != 128) uniform = false;
    check(uniform, "every pixel of the two quads is blended exactly once (128)");
    check(px(out, 32, 0)[0] == 0 && px(out, 0, 16)[0] == 0, "nothing outside the quads");
    check(px(out, 40, 0)[0] == 255 && px(out, 40, 0)[1] == 0, "texel 0,0 red");
    check(px(out, 41, 0)[1] == 255 && px(out, 41, 0)[0] == 0, "texel 1,0 green");
    check(px(out, 40, 1)[2] == 255 && px(out, 40, 1)[0] == 0, "texel 0,1 blue");
    check(px(out, 41, 1)[0] == 255 && px(out, 41, 1)[1] == 255, "texel 1,1 white");
    check(px(out, 7, 24)[0] == 255 && px(out, 8, 24)[0] == 0, "clip rect cuts at x = 8");

    // 2x framebuffer scale: same picture, twice the pixels.
    io.DisplayFramebufferScale = ImVec2(2, 2);
    ImGui::NewFrame();
    ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(1, 1), ImVec2(3, 3), IM_COL32(0, 255, 0, 255));
    ImGui::Render();
    renderer.render(ImGui::GetDrawData(), out, black);
    check(out.width == 128 && out.height == 64, "2x framebuffer");
    check(px(out, 2, 2)[1] == 255 && px(out, 5, 5)[1] == 255 && px(out, 6, 6)[1] == 0 && px(out, 1, 1)[1] == 0,
          "2x rect covers pixels 2..5");

    const Image small = downscale(out, 32);
    check(small.width == 32 && small.height == 16, "downscale keeps the aspect ratio");

    ImGui::DestroyContext();
    if (failures) return 1;
    std::puts("soft_raster_test: ok");
    return 0;
}
