#pragma once
#include <cstddef>
#include <d3d9.h>

namespace texture_util
{
    // Decodes an in-memory image (PNG/JPEG/BMP/TGA — anything stb_image
    // supports) into a managed A8R8G8B8 D3D9 texture, swizzling stb_image's
    // RGBA output to the BGRA order D3D9 expects. Returns nullptr on any
    // failure. Shared by the item-icon, title, and interface texture loaders.
    LPDIRECT3DTEXTURE9 create_from_image_memory(
        LPDIRECT3DDEVICE9 device, const void* data, std::size_t dataSize);

    // Draws a screen-space (pre-transformed) textured quad at (x, y) with the
    // given size, modulated by `diffuse` (use 0xFFFFFFFF for untinted). Saves
    // and restores every render/sampler/texture-stage state it touches so the
    // caller's fixed-function pipeline state is left unchanged. Returns false
    // on invalid args or draw failure. Shared by the item-icon and title
    // overlays.
    bool draw_screen_quad(
        LPDIRECT3DDEVICE9 device, LPDIRECT3DTEXTURE9 texture,
        float x, float y, float width, float height, D3DCOLOR diffuse);
}
