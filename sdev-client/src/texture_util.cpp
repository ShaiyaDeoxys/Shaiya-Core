#include "include/texture_util.h"
#include <climits>
#include <external/stb/stb_image.h>

namespace texture_util
{
    LPDIRECT3DTEXTURE9 create_from_image_memory(
        LPDIRECT3DDEVICE9 device, const void* data, std::size_t dataSize)
    {
        if (!device || !data || dataSize == 0 || dataSize > INT_MAX)
            return nullptr;

        int width = 0, height = 0, channels = 0;
        auto* pixels = stbi_load_from_memory(
            static_cast<const stbi_uc*>(data),
            static_cast<int>(dataSize),
            &width, &height, &channels, 4);
        if (!pixels || width <= 0 || height <= 0)
        {
            if (pixels)
                stbi_image_free(pixels);
            return nullptr;
        }

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (FAILED(device->CreateTexture(
                static_cast<UINT>(width), static_cast<UINT>(height),
                1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
        {
            stbi_image_free(pixels);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            stbi_image_free(pixels);
            return nullptr;
        }

        // stb_image outputs RGBA, D3D9 A8R8G8B8 expects BGRA — swizzle R<->B.
        for (int y = 0; y < height; ++y)
        {
            auto* src = pixels + y * width * 4;
            auto* dst = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
            for (int x = 0; x < width; ++x)
            {
                dst[x * 4 + 0] = src[x * 4 + 2]; // B
                dst[x * 4 + 1] = src[x * 4 + 1]; // G
                dst[x * 4 + 2] = src[x * 4 + 0]; // R
                dst[x * 4 + 3] = src[x * 4 + 3]; // A
            }
        }

        texture->UnlockRect(0);
        stbi_image_free(pixels);
        return texture;
    }

    namespace
    {
        struct ScreenQuadVertex
        {
            float x, y, z, rhw;
            D3DCOLOR color;
            float u, v;
        };
    }

    bool draw_screen_quad(
        LPDIRECT3DDEVICE9 device, LPDIRECT3DTEXTURE9 texture,
        float x, float y, float width, float height, D3DCOLOR diffuse)
    {
        if (!device || !texture || width <= 0.0f || height <= 0.0f)
            return false;

        DWORD sFvf, sAB, sSrc, sDst, sLit, sCull, sZ, sAddrU, sAddrV;
        DWORD sColOp, sColA1, sColA2, sAlpOp, sAlpA1, sAlpA2;
        LPDIRECT3DTEXTURE9 sTex = nullptr;

        device->GetFVF(&sFvf);
        device->GetTexture(0, reinterpret_cast<IDirect3DBaseTexture9**>(&sTex));
        device->GetRenderState(D3DRS_ALPHABLENDENABLE, &sAB);
        device->GetRenderState(D3DRS_SRCBLEND, &sSrc);
        device->GetRenderState(D3DRS_DESTBLEND, &sDst);
        device->GetRenderState(D3DRS_LIGHTING, &sLit);
        device->GetRenderState(D3DRS_CULLMODE, &sCull);
        device->GetRenderState(D3DRS_ZENABLE, &sZ);
        device->GetSamplerState(0, D3DSAMP_ADDRESSU, &sAddrU);
        device->GetSamplerState(0, D3DSAMP_ADDRESSV, &sAddrV);
        device->GetTextureStageState(0, D3DTSS_COLOROP, &sColOp);
        device->GetTextureStageState(0, D3DTSS_COLORARG1, &sColA1);
        device->GetTextureStageState(0, D3DTSS_COLORARG2, &sColA2);
        device->GetTextureStageState(0, D3DTSS_ALPHAOP, &sAlpOp);
        device->GetTextureStageState(0, D3DTSS_ALPHAARG1, &sAlpA1);
        device->GetTextureStageState(0, D3DTSS_ALPHAARG2, &sAlpA2);

        ScreenQuadVertex quad[4] = {
            { x - 0.5f,         y - 0.5f,          0, 1, diffuse, 0, 0 },
            { x + width - 0.5f, y - 0.5f,          0, 1, diffuse, 1, 0 },
            { x - 0.5f,         y + height - 0.5f, 0, 1, diffuse, 0, 1 },
            { x + width - 0.5f, y + height - 0.5f, 0, 1, diffuse, 1, 1 },
        };

        constexpr DWORD fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
        device->SetTexture(0, texture);
        device->SetFVF(fvf);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        device->SetRenderState(D3DRS_LIGHTING, FALSE);
        device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
        device->SetRenderState(D3DRS_ZENABLE, FALSE);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
        auto hr = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(ScreenQuadVertex));

        device->SetTextureStageState(0, D3DTSS_ALPHAARG2, sAlpA2);
        device->SetTextureStageState(0, D3DTSS_ALPHAARG1, sAlpA1);
        device->SetTextureStageState(0, D3DTSS_ALPHAOP, sAlpOp);
        device->SetTextureStageState(0, D3DTSS_COLORARG2, sColA2);
        device->SetTextureStageState(0, D3DTSS_COLORARG1, sColA1);
        device->SetTextureStageState(0, D3DTSS_COLOROP, sColOp);
        device->SetSamplerState(0, D3DSAMP_ADDRESSU, sAddrU);
        device->SetSamplerState(0, D3DSAMP_ADDRESSV, sAddrV);
        device->SetRenderState(D3DRS_ZENABLE, sZ);
        device->SetRenderState(D3DRS_CULLMODE, sCull);
        device->SetRenderState(D3DRS_LIGHTING, sLit);
        device->SetRenderState(D3DRS_DESTBLEND, sDst);
        device->SetRenderState(D3DRS_SRCBLEND, sSrc);
        device->SetRenderState(D3DRS_ALPHABLENDENABLE, sAB);
        device->SetFVF(sFvf);
        device->SetTexture(0, sTex);
        if (sTex) sTex->Release();

        return SUCCEEDED(hr);
    }
}
