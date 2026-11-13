#include "soft_raster.h"

#include "launcher_texture_hooks.h"

#include "stb_image.h" // decoder compiled in launcher_gui.cpp (PNG, from memory)

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "stb_image_write.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <thread>

namespace pikmin::launcher::gallery {
namespace {

constexpr int kSub = 256; // subpixel steps per pixel (8 bits)

struct Vtx {
    std::int64_t x, y; // subpixel fixed point
    float u, v;
    float r, g, b, a; // 0..1
};

Vtx makeVtx(const ImDrawVert& in, ImVec2 off, ImVec2 scale)
{
    Vtx v;
    v.x = std::llround(double(in.pos.x - off.x) * scale.x * kSub);
    v.y = std::llround(double(in.pos.y - off.y) * scale.y * kSub);
    v.u = in.uv.x;
    v.v = in.uv.y;
    v.r = float((in.col >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
    v.g = float((in.col >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
    v.b = float((in.col >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
    v.a = float((in.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
    return v;
}

// Edge from a to b; inside when cross(b - a, p - a) > 0 (after the winding
// is made positive). Ties go to top and left edges only, so two triangles
// sharing an edge never both draw a pixel (no double-blended seams).
struct Edge {
    std::int64_t dx, dy, ax, ay;
    bool topLeft;
    void set(const Vtx& a, const Vtx& b)
    {
        dx = b.x - a.x;
        dy = b.y - a.y;
        ax = a.x;
        ay = a.y;
        // With y down and this winding, interior is on the right of a->b.
        topLeft = (dy == 0 && dx < 0) || dy > 0;
    }
    std::int64_t at(std::int64_t px, std::int64_t py) const { return dx * (py - ay) - dy * (px - ax); }
    bool inside(std::int64_t e) const { return e > 0 || (e == 0 && topLeft); }
};

inline void sample(const Image& tex, float u, float v, float out[4])
{
    const float fx = u * float(tex.width) - 0.5f, fy = v * float(tex.height) - 0.5f;
    const float flx = std::floor(fx), fly = std::floor(fy);
    const float tx = fx - flx, ty = fy - fly;
    const int x0 = int(flx), y0 = int(fly);
    const auto clampX = [&](int x) { return std::clamp(x, 0, tex.width - 1); };
    const auto clampY = [&](int y) { return std::clamp(y, 0, tex.height - 1); };
    const int xa = clampX(x0), xb = clampX(x0 + 1), ya = clampY(y0), yb = clampY(y0 + 1);
    const std::uint8_t* p00 = &tex.rgba[(std::size_t(ya) * tex.width + xa) * 4];
    const std::uint8_t* p10 = &tex.rgba[(std::size_t(ya) * tex.width + xb) * 4];
    const std::uint8_t* p01 = &tex.rgba[(std::size_t(yb) * tex.width + xa) * 4];
    const std::uint8_t* p11 = &tex.rgba[(std::size_t(yb) * tex.width + xb) * 4];
    for (int c = 0; c < 4; ++c) {
        const float top = float(p00[c]) + (float(p10[c]) - float(p00[c])) * tx;
        const float bottom = float(p01[c]) + (float(p11[c]) - float(p01[c])) * tx;
        out[c] = (top + (bottom - top) * ty) * (1.0f / 255.0f);
    }
}

inline std::uint8_t toByte(float v)
{
    return std::uint8_t(std::clamp(int(v * 255.0f + 0.5f), 0, 255));
}

inline void blend(std::uint8_t* dst, const float src[4])
{
    const float a = std::clamp(src[3], 0.0f, 1.0f);
    if (a <= 0.0f) return;
    const float k = 1.0f - a;
    dst[0] = toByte(src[0] * a + float(dst[0]) * (1.0f / 255.0f) * k);
    dst[1] = toByte(src[1] * a + float(dst[1]) * (1.0f / 255.0f) * k);
    dst[2] = toByte(src[2] * a + float(dst[2]) * (1.0f / 255.0f) * k);
    dst[3] = toByte(a + float(dst[3]) * (1.0f / 255.0f) * k);
}

struct Clip {
    int x0, y0, x1, y1; // pixels, x1/y1 exclusive
};

void drawTriangle(Vtx v0, Vtx v1, Vtx v2, const Image* tex, const Clip& clip, Image& out)
{
    std::int64_t area = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
    if (area == 0) return;
    if (area < 0) {
        std::swap(v1, v2);
        area = -area;
    }
    Edge e0, e1, e2; // e0: v1->v2 (weight of v0), e1: v2->v0, e2: v0->v1
    e0.set(v1, v2);
    e1.set(v2, v0);
    e2.set(v0, v1);
    const std::int64_t minX = std::min({ v0.x, v1.x, v2.x }), maxX = std::max({ v0.x, v1.x, v2.x });
    const std::int64_t minY = std::min({ v0.y, v1.y, v2.y }), maxY = std::max({ v0.y, v1.y, v2.y });
    const int px0 = std::max(clip.x0, int(minX / kSub) - 1), px1 = std::min(clip.x1, int(maxX / kSub) + 2);
    const int py0 = std::max(clip.y0, int(minY / kSub) - 1), py1 = std::min(clip.y1, int(maxY / kSub) + 2);
    if (px0 >= px1 || py0 >= py1) return;

    const bool flatUv = v0.u == v1.u && v1.u == v2.u && v0.v == v1.v && v1.v == v2.v;
    const bool flatCol = v0.r == v1.r && v1.r == v2.r && v0.g == v1.g && v1.g == v2.g && v0.b == v1.b && v1.b == v2.b
                      && v0.a == v1.a && v1.a == v2.a;
    float flatTex[4] = { 1, 1, 1, 1 };
    if (tex && flatUv) sample(*tex, v0.u, v0.v, flatTex);
    const double inv = 1.0 / double(area);

    for (int py = py0; py < py1; ++py) {
        const std::int64_t cy = std::int64_t(py) * kSub + kSub / 2;
        // Column range where all three edges can be inside, from E(x) = C + A x.
        double lo = double(px0), hi = double(px1 - 1);
        bool none = false;
        for (const Edge* e : { &e0, &e1, &e2 }) {
            const double a = -double(e->dy);                  // dE / d(subpixel x)
            const double c = double(e->at(0, cy));            // E at subpixel x = 0
            if (a == 0) {
                if (c < 0) none = true;
                continue;
            }
            const double xs = (-c / a - kSub / 2) / kSub;    // pixel where E crosses 0
            if (a > 0) lo = std::max(lo, std::floor(xs) - 1);
            else hi = std::min(hi, std::ceil(xs) + 1);
        }
        if (none || lo > hi) continue;
        std::uint8_t* row = &out.rgba[std::size_t(py) * out.width * 4];
        for (int px = int(lo); px <= int(hi); ++px) {
            const std::int64_t cx = std::int64_t(px) * kSub + kSub / 2;
            const std::int64_t w0 = e0.at(cx, cy), w1 = e1.at(cx, cy), w2 = e2.at(cx, cy);
            if (!e0.inside(w0) || !e1.inside(w1) || !e2.inside(w2)) continue;
            const float b0 = float(double(w0) * inv), b1 = float(double(w1) * inv), b2 = 1.0f - b0 - b1;
            float src[4];
            if (flatCol) {
                src[0] = v0.r; src[1] = v0.g; src[2] = v0.b; src[3] = v0.a;
            } else {
                src[0] = v0.r * b0 + v1.r * b1 + v2.r * b2;
                src[1] = v0.g * b0 + v1.g * b1 + v2.g * b2;
                src[2] = v0.b * b0 + v1.b * b1 + v2.b * b2;
                src[3] = v0.a * b0 + v1.a * b1 + v2.a * b2;
            }
            if (tex) {
                float t[4];
                if (flatUv) {
                    t[0] = flatTex[0]; t[1] = flatTex[1]; t[2] = flatTex[2]; t[3] = flatTex[3];
                } else {
                    sample(*tex, v0.u * b0 + v1.u * b1 + v2.u * b2, v0.v * b0 + v1.v * b1 + v2.v * b2, t);
                }
                src[0] *= t[0]; src[1] *= t[1]; src[2] *= t[2]; src[3] *= t[3];
            }
            blend(&row[std::size_t(px) * 4], src);
        }
    }
}

// Texels per screen pixel for a textured triangle (sqrt of the area ratio).
float texelsPerPixel(const ImDrawVert& a, const ImDrawVert& b, const ImDrawVert& c, ImVec2 scale, const Image& tex,
                     float& pixelArea)
{
    const double ax = a.pos.x * scale.x, ay = a.pos.y * scale.y;
    const double bx = b.pos.x * scale.x, by = b.pos.y * scale.y;
    const double cx = c.pos.x * scale.x, cy = c.pos.y * scale.y;
    const double screen = std::abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
    const double texels = std::abs((double(b.uv.x) - a.uv.x) * (double(c.uv.y) - a.uv.y)
                                   - (double(b.uv.y) - a.uv.y) * (double(c.uv.x) - a.uv.x))
                        * double(tex.width) * double(tex.height);
    pixelArea = float(screen * 0.5);
    if (screen <= 0 || texels <= 0) return 0.0f;
    return float(std::sqrt(texels / screen));
}

SoftRenderer* gBackendRenderer = nullptr;

unsigned backendCreate(const unsigned char* rgba, int w, int h) { return gBackendRenderer->create(rgba, w, h); }
void backendUpdate(unsigned id, const unsigned char* rgba, int w, int h) { gBackendRenderer->update(id, rgba, w, h); }
void backendDestroy(unsigned id) { gBackendRenderer->destroy(id); }
const TextureBackend kBackend { backendCreate, backendUpdate, backendDestroy };

} // namespace

unsigned SoftRenderer::create(const std::uint8_t* rgba, int width, int height)
{
    Image image;
    image.width = width;
    image.height = height;
    image.rgba.assign(rgba, rgba + std::size_t(width) * height * 4);
    if (!mFree.empty()) {
        const unsigned id = mFree.back();
        mFree.pop_back();
        mTextures[id - 1] = std::move(image);
        return id;
    }
    mTextures.push_back(std::move(image));
    return unsigned(mTextures.size());
}

void SoftRenderer::update(unsigned id, const std::uint8_t* rgba, int width, int height)
{
    if (id == 0 || id > mTextures.size()) return;
    Image& image = mTextures[id - 1];
    image.width = width;
    image.height = height;
    image.rgba.assign(rgba, rgba + std::size_t(width) * height * 4);
}

void SoftRenderer::destroy(unsigned id)
{
    if (id == 0 || id > mTextures.size() || mTextures[id - 1].rgba.empty()) return;
    mTextures[id - 1] = Image();
    mFree.push_back(id);
}

const Image* SoftRenderer::texture(unsigned id) const
{
    if (id == 0 || id > mTextures.size() || mTextures[id - 1].rgba.empty()) return nullptr;
    return &mTextures[id - 1];
}

std::size_t SoftRenderer::liveTextures() const { return mTextures.size() - mFree.size(); }

void SoftRenderer::render(const ImDrawData* data, Image& out, const float clear[4], std::vector<TextureUse>* uses) const
{
    const ImVec2 scale = data->FramebufferScale;
    out.width = std::max(1, int(std::lround(data->DisplaySize.x * scale.x)));
    out.height = std::max(1, int(std::lround(data->DisplaySize.y * scale.y)));
    out.rgba.assign(std::size_t(out.width) * out.height * 4, 0);
    const std::uint8_t fill[4] = { toByte(clear[0]), toByte(clear[1]), toByte(clear[2]), toByte(clear[3]) };
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) std::copy(fill, fill + 4, &out.rgba[i]);

    const ImVec2 off = data->DisplayPos;
    const auto bandWork = [&](int bandY0, int bandY1) {
        for (int n = 0; n < data->CmdListsCount; ++n) {
            const ImDrawList* list = data->CmdLists[n];
            for (const ImDrawCmd& cmd : list->CmdBuffer) {
                if (cmd.UserCallback) continue; // the launcher draws no callbacks
                Clip clip;
                clip.x0 = std::max(0, int(std::floor((cmd.ClipRect.x - off.x) * scale.x)));
                clip.y0 = std::max(bandY0, int(std::floor((cmd.ClipRect.y - off.y) * scale.y)));
                clip.x1 = std::min(out.width, int(std::ceil((cmd.ClipRect.z - off.x) * scale.x)));
                clip.y1 = std::min(bandY1, int(std::ceil((cmd.ClipRect.w - off.y) * scale.y)));
                if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1) continue;
                const Image* tex = texture(unsigned(cmd.GetTexID()));
                const ImDrawIdx* idx = list->IdxBuffer.Data + cmd.IdxOffset;
                const ImDrawVert* vtx = list->VtxBuffer.Data + cmd.VtxOffset;
                for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                    drawTriangle(makeVtx(vtx[idx[i]], off, scale), makeVtx(vtx[idx[i + 1]], off, scale),
                                 makeVtx(vtx[idx[i + 2]], off, scale), tex, clip, out);
                }
            }
        }
    };
    const int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
    if (threads == 1 || out.height < 64) {
        bandWork(0, out.height);
    } else {
        std::vector<std::thread> workers;
        const int band = (out.height + threads - 1) / threads;
        for (int t = 0; t < threads; ++t) {
            const int y0 = t * band, y1 = std::min(out.height, y0 + band);
            if (y0 < y1) workers.emplace_back(bandWork, y0, y1);
        }
        for (std::thread& w : workers) w.join();
    }

    if (!uses) return;
    uses->clear();
    for (int n = 0; n < data->CmdListsCount; ++n) {
        const ImDrawList* list = data->CmdLists[n];
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) continue;
            const unsigned id = unsigned(cmd.GetTexID());
            const Image* tex = texture(id);
            if (!tex) continue;
            const ImDrawIdx* idx = list->IdxBuffer.Data + cmd.IdxOffset;
            const ImDrawVert* vtx = list->VtxBuffer.Data + cmd.VtxOffset;
            for (unsigned i = 0; i + 2 < cmd.ElemCount; i += 3) {
                const ImDrawVert &a = vtx[idx[i]], &b = vtx[idx[i + 1]], &c = vtx[idx[i + 2]];
                float pixelArea = 0;
                const float ratio = texelsPerPixel(a, b, c, scale, *tex, pixelArea);
                // Big textured triangles only: glyph fringes and slivers say nothing.
                if (ratio <= 0.0f || pixelArea < 400.0f || ratio >= 0.95f) continue;
                const float x0 = std::min({ a.pos.x, b.pos.x, c.pos.x }) * scale.x;
                const float y0 = std::min({ a.pos.y, b.pos.y, c.pos.y }) * scale.y;
                const float x1 = std::max({ a.pos.x, b.pos.x, c.pos.x }) * scale.x;
                const float y1 = std::max({ a.pos.y, b.pos.y, c.pos.y }) * scale.y;
                auto found = std::find_if(uses->begin(), uses->end(), [&](const TextureUse& u) { return u.id == id; });
                if (found == uses->end()) {
                    uses->push_back({ id, tex->width, tex->height, ratio, x0, y0, x1, y1 });
                } else {
                    found->texelsPerPixel = std::min(found->texelsPerPixel, ratio);
                    found->x0 = std::min(found->x0, x0);
                    found->y0 = std::min(found->y0, y0);
                    found->x1 = std::max(found->x1, x1);
                    found->y1 = std::max(found->y1, y1);
                }
            }
        }
    }
}

ScopedTextureBackend::ScopedTextureBackend(SoftRenderer& renderer)
{
    gBackendRenderer = &renderer;
    textureBackend() = &kBackend;
}

ScopedTextureBackend::~ScopedTextureBackend()
{
    textureBackend() = nullptr;
    gBackendRenderer = nullptr;
}

bool savePng(const std::string& path, const Image& image, int compression)
{
    std::vector<std::uint8_t> opaque = image.rgba;
    for (std::size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 255;
    stbi_write_png_compression_level = compression;
    return stbi_write_png(path.c_str(), image.width, image.height, 4, opaque.data(), image.width * 4) != 0;
}

bool loadPng(const std::string& path, Image& image)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, 4);
    if (!pixels) return false;
    image.width = w;
    image.height = h;
    image.rgba.assign(pixels, pixels + std::size_t(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

Image downscale(const Image& image, int width)
{
    Image out;
    if (image.width <= 0 || image.height <= 0 || width <= 0) return out;
    out.width = std::min(width, image.width);
    out.height = std::max(1, int(std::lround(double(image.height) * out.width / image.width)));
    out.rgba.assign(std::size_t(out.width) * out.height * 4, 0);
    const double sx = double(image.width) / out.width, sy = double(image.height) / out.height;
    for (int y = 0; y < out.height; ++y) {
        const int y0 = int(y * sy), y1 = std::max(y0 + 1, int((y + 1) * sy));
        for (int x = 0; x < out.width; ++x) {
            const int x0 = int(x * sx), x1 = std::max(x0 + 1, int((x + 1) * sx));
            unsigned sum[4] = { 0, 0, 0, 0 };
            for (int yy = y0; yy < y1 && yy < image.height; ++yy)
                for (int xx = x0; xx < x1 && xx < image.width; ++xx)
                    for (int c = 0; c < 4; ++c) sum[c] += image.rgba[(std::size_t(yy) * image.width + xx) * 4 + c];
            const unsigned count = unsigned((std::min(y1, image.height) - y0) * (std::min(x1, image.width) - x0));
            for (int c = 0; c < 4; ++c) out.rgba[(std::size_t(y) * out.width + x) * 4 + c] = std::uint8_t((sum[c] + count / 2) / count);
        }
    }
    return out;
}

} // namespace pikmin::launcher::gallery
