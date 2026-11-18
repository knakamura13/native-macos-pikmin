// Where the launcher's textures live. By default (no backend set) they are
// OpenGL textures. The UI gallery (ui_gallery/, #215) sets a CPU backend so it
// can draw the real launcher screens without a GPU or a window.
#pragma once

namespace pikmin::launcher {

struct TextureBackend {
    unsigned (*create)(const unsigned char* rgba, int width, int height);
    void (*update)(unsigned id, const unsigned char* rgba, int width, int height);
    void (*destroy)(unsigned id);
};

// nullptr = OpenGL.
inline const TextureBackend*& textureBackend()
{
    static const TextureBackend* backend = nullptr;
    return backend;
}

} // namespace pikmin::launcher
