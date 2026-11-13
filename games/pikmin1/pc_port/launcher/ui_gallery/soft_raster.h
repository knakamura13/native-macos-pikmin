// CPU renderer for Dear ImGui draw data (UI gallery, #215). It follows the
// OpenGL3 backend: bilinear textures clamped to the edge, straight alpha
// blending, scissor = clip rect. Same input gives the same pixels on every
// platform, so captures can be compared with checked-in references without a
// GPU, a display server or a window.
#pragma once

#include "imgui.h"
#include "launcher_texture_hooks.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pikmin::launcher::gallery {

struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba; // straight RGBA8, top row first
};

// A texture drawn larger than it is: fewer texels than screen pixels.
struct TextureUse {
    unsigned id = 0;
    int texW = 0, texH = 0;
    float texelsPerPixel = 1.0f; // < 1: upscaled (soft)
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0; // where, in framebuffer pixels
};

class SoftRenderer {
public:
    unsigned create(const std::uint8_t* rgba, int width, int height);
    void update(unsigned id, const std::uint8_t* rgba, int width, int height);
    void destroy(unsigned id);
    const Image* texture(unsigned id) const;
    std::size_t liveTextures() const;

    // Clears `out` to `clear` (RGBA 0..1, like glClearColor) and draws.
    // `uses` (optional) gets the textures drawn upscaled.
    void render(const ImDrawData* data, Image& out, const float clear[4], std::vector<TextureUse>* uses = nullptr) const;

private:
    std::vector<Image> mTextures; // index = id - 1; empty = freed
    std::vector<unsigned> mFree;
};

// Makes the CPU renderer the launcher's texture backend (launcher_texture_hooks.h)
// for as long as it lives. Scopes nest: the destructor puts back whatever
// renderer was active before, so one session's teardown never leaves
// another's textures to fall through to OpenGL.
class ScopedTextureBackend {
public:
    explicit ScopedTextureBackend(SoftRenderer& renderer);
    ~ScopedTextureBackend();
    ScopedTextureBackend(const ScopedTextureBackend&) = delete;
    ScopedTextureBackend& operator=(const ScopedTextureBackend&) = delete;

private:
    SoftRenderer* mPreviousRenderer;
    const TextureBackend* mPreviousBackend;
};

// compression: 0..9 (zlib level). Big screenshots favour speed; checked-in
// references favour size.
bool savePng(const std::string& path, const Image& image, int compression = 3);
bool loadPng(const std::string& path, Image& image);
// Box-filtered copy `width` pixels wide (height keeps the aspect ratio).
Image downscale(const Image& image, int width);

} // namespace pikmin::launcher::gallery
