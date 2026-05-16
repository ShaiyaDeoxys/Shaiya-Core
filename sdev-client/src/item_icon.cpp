// ===========================================================================
// item_icon.cpp — Elemental icon overlay on inventory / quickslot items
//
// Draws element badge icons (fire, water, earth, wind) in the bottom-right
// corner of item icons at full opacity.
//
// Element detection priority:
//   1. Inserted lapis gems (CItem::gems[6])
//   2. Base item attribute (ItemInfo::attribute)
//
// Textures: Assets/General/{fire,water,earth,wind}.png  (inside data.saf)
//
// Hook sites:
//   0x51826D — inventory item render (8-byte detour)
//   0x4FFCE3 — quickslot item render (5-byte detour)
// ===========================================================================
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <external/stb/stb_image.h>
#include <util/util.h>
#include "include/main.h"
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/CItem.h"
#include "include/shaiya/CPlayerData.h"
#include "include/shaiya/ItemInfo.h"
#include "include/shaiya/Static.h"
using namespace shaiya;

namespace
{
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    constexpr int kLargeItemIconSize    = 32;
    constexpr int kLargeElementIconSize = 16;
    constexpr int kSmallElementIconSize = 8;
    constexpr int kElementIconInset     = 1;

    constexpr const char* kAssetFolder = "assets\\general";

    // -----------------------------------------------------------------------
    // Element texture indices
    // -----------------------------------------------------------------------
    constexpr int kElementCount = 4;
    constexpr int kIdxFire  = 0;
    constexpr int kIdxWater = 1;
    constexpr int kIdxEarth = 2;
    constexpr int kIdxWind  = 3;

    constexpr const char* kElementFileNames[kElementCount] =
    {
        "fire.png",
        "water.png",
        "earth.png",
        "wind.png",
    };

    // -----------------------------------------------------------------------
    // Eligible item types for element badge display
    // -----------------------------------------------------------------------
    constexpr std::array<bool, 151> kEligibleTypes = []()
    {
        std::array<bool, 151> table{};
        constexpr int allowed[] = {
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
            17, 32, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55,
            56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 67, 73, 82, 88
        };
        for (auto t : allowed) table[t] = true;
        return table;
    }();

    // -----------------------------------------------------------------------
    // Element texture state (loaded once from PNG in data.saf)
    // -----------------------------------------------------------------------
    struct ElementTexture
    {
        uint64_t sahOffset     = 0;
        uint64_t sahSize       = 0;
        bool     found         = false;
        bool     loadAttempted = false;
        LPDIRECT3DTEXTURE9 texture = nullptr;
        int      width         = 0;
        int      height        = 0;
    };

    ElementTexture g_textures[kElementCount]{};
    bool g_sahScanned = false;

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    std::string get_game_path(const char* relativePath)
    {
        char buf[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, buf, sizeof(buf)) == 0)
            return relativePath ? relativePath : "";

        std::string path(buf);
        auto slash = path.find_last_of("\\/");
        if (slash == std::string::npos)
            return relativePath ? relativePath : "";

        path.resize(slash + 1);
        if (relativePath)
            path += relativePath;
        return path;
    }

    std::string to_lower(std::string s)
    {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // -----------------------------------------------------------------------
    // SAH parsing
    // -----------------------------------------------------------------------
    bool sah_read_u32(const std::vector<char>& d, std::size_t& o, uint32_t& v)
    {
        if (o + 4 > d.size()) return false;
        std::memcpy(&v, d.data() + o, 4); o += 4;
        return true;
    }

    bool sah_read_u64(const std::vector<char>& d, std::size_t& o, uint64_t& v)
    {
        if (o + 8 > d.size()) return false;
        std::memcpy(&v, d.data() + o, 8); o += 8;
        return true;
    }

    bool sah_read_string(const std::vector<char>& d, std::size_t& o, std::string& v)
    {
        uint32_t len = 0;
        if (!sah_read_u32(d, o, len) || o + len > d.size()) return false;
        v.assign(d.data() + o, d.data() + o + len);
        while (!v.empty() && v.back() == '\0') v.pop_back();
        o += len;
        return true;
    }

    bool scan_sah_directory(const std::vector<char>& data, std::size_t& offset,
                            const std::string& parentPath)
    {
        std::string name;
        if (!sah_read_string(data, offset, name))
            return false;

        auto path = parentPath;
        if (!name.empty())
            path = path.empty() ? name : path + "\\" + name;

        uint32_t fileCount = 0;
        if (!sah_read_u32(data, offset, fileCount))
            return false;

        auto lowerPath = to_lower(path);
        bool isTarget = (lowerPath == kAssetFolder)
                     || (lowerPath == std::string("data\\") + kAssetFolder);

        for (uint32_t i = 0; i < fileCount; ++i)
        {
            std::string fileName;
            uint64_t fileOffset = 0, fileSize = 0;
            if (!sah_read_string(data, offset, fileName)
                || !sah_read_u64(data, offset, fileOffset)
                || !sah_read_u64(data, offset, fileSize))
                return false;

            if (isTarget)
            {
                auto lowerFile = to_lower(fileName);
                for (int e = 0; e < kElementCount; ++e)
                {
                    if (!g_textures[e].found && lowerFile == kElementFileNames[e])
                    {
                        g_textures[e].sahOffset = fileOffset;
                        g_textures[e].sahSize   = fileSize;
                        g_textures[e].found     = true;
                        break;
                    }
                }
            }
        }

        uint32_t dirCount = 0;
        if (!sah_read_u32(data, offset, dirCount))
            return false;

        for (uint32_t i = 0; i < dirCount; ++i)
        {
            if (!scan_sah_directory(data, offset, path))
                return false;
        }
        return true;
    }

    void scan_sah()
    {
        if (g_sahScanned)
            return;
        g_sahScanned = true;

        auto sahPath = get_game_path("data.sah");
        std::ifstream stream(sahPath, std::ios::binary);
        if (!stream)
            return;

        std::vector<char> data(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
        if (data.size() <= 0x34 || std::memcmp(data.data(), "SAH", 3) != 0)
            return;

        auto offset = std::size_t{ 0x34 };
        scan_sah_directory(data, offset, "");
    }

    // -----------------------------------------------------------------------
    // PNG -> D3D9 texture (stb_image, RGBA -> BGRA swizzle)
    // -----------------------------------------------------------------------
    LPDIRECT3DTEXTURE9 create_texture_from_png(LPDIRECT3DDEVICE9 device,
                                                const void* data, UINT dataSize,
                                                int& outW, int& outH)
    {
        if (!device || !data || dataSize == 0)
            return nullptr;

        int channels = 0;
        auto* pixels = stbi_load_from_memory(
            static_cast<const stbi_uc*>(data),
            static_cast<int>(dataSize),
            &outW, &outH, &channels, 4);
        if (!pixels)
            return nullptr;

        LPDIRECT3DTEXTURE9 tex = nullptr;
        if (FAILED(device->CreateTexture(
                static_cast<UINT>(outW), static_cast<UINT>(outH),
                1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)) || !tex)
        {
            stbi_image_free(pixels);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(tex->LockRect(0, &locked, nullptr, 0)))
        {
            tex->Release();
            stbi_image_free(pixels);
            return nullptr;
        }

        for (int y = 0; y < outH; ++y)
        {
            auto* src = pixels + y * outW * 4;
            auto* dst = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
            for (int x = 0; x < outW; ++x)
            {
                dst[x * 4 + 0] = src[x * 4 + 2]; // B
                dst[x * 4 + 1] = src[x * 4 + 1]; // G
                dst[x * 4 + 2] = src[x * 4 + 0]; // R
                dst[x * 4 + 3] = src[x * 4 + 3]; // A
            }
        }

        tex->UnlockRect(0);
        stbi_image_free(pixels);
        return tex;
    }

    void ensure_texture_loaded(int index, LPDIRECT3DDEVICE9 device)
    {
        auto& et = g_textures[index];
        if (et.texture || et.loadAttempted)
            return;
        et.loadAttempted = true;

        if (!et.found || et.sahSize == 0 || !device)
            return;

        auto safPath = get_game_path("data.saf");
        std::ifstream stream(safPath, std::ios::binary);
        if (!stream)
            return;

        stream.seekg(static_cast<std::streamoff>(et.sahOffset), std::ios::beg);
        if (!stream)
            return;

        std::vector<char> fileData(static_cast<std::size_t>(et.sahSize), 0);
        stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        if (stream.gcount() != static_cast<std::streamsize>(fileData.size()))
            return;

        et.texture = create_texture_from_png(device, fileData.data(),
                                              static_cast<UINT>(fileData.size()),
                                              et.width, et.height);
    }

    // -----------------------------------------------------------------------
    // Element detection
    // -----------------------------------------------------------------------
    int get_element_index(Attribute attr)
    {
        switch (attr)
        {
        case Attribute::Fire1:  case Attribute::Fire2:  return kIdxFire;
        case Attribute::Water1: case Attribute::Water2: return kIdxWater;
        case Attribute::Earth1: case Attribute::Earth2: return kIdxEarth;
        case Attribute::Wind1:  case Attribute::Wind2:  return kIdxWind;
        default: return -1;
        }
    }

    LPDIRECT3DTEXTURE9 get_element_texture(Attribute attr)
    {
        auto idx = get_element_index(attr);
        if (idx < 0) return nullptr;

        auto* device = g_var ? g_var->device : nullptr;
        if (device)
        {
            scan_sah();
            ensure_texture_loaded(idx, device);
        }

        return g_textures[idx].texture;
    }

    bool is_elemental(Attribute attr)
    {
        return get_element_index(attr) >= 0;
    }

    bool is_eligible_type(int type)
    {
        return type >= 0
            && type < static_cast<int>(kEligibleTypes.size())
            && kEligibleTypes[type];
    }

    // -----------------------------------------------------------------------
    // DX9 textured-quad drawing with state save/restore
    // -----------------------------------------------------------------------
    struct ScreenVertex
    {
        float x, y, z, rhw;
        D3DCOLOR color;
        float u, v;
    };

    bool draw_texture(LPDIRECT3DTEXTURE9 tex, float x, float y, float w, float h, D3DCOLOR diffuse)
    {
        auto device = g_var ? g_var->camera.device : nullptr;
        if (!device || !tex || w <= 0.0f || h <= 0.0f)
            return false;

        DWORD sFvf, sLit, sCull, sZ, sAB, sSrc, sDst;
        DWORD sColOp, sColA1, sColA2, sAlpOp, sAlpA1, sAlpA2;
        DWORD sAddrU, sAddrV;
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

        ScreenVertex quad[4] = {
            { x - 0.5f,     y - 0.5f,     0, 1, diffuse, 0, 0 },
            { x + w - 0.5f, y - 0.5f,     0, 1, diffuse, 1, 0 },
            { x - 0.5f,     y + h - 0.5f, 0, 1, diffuse, 0, 1 },
            { x + w - 0.5f, y + h - 0.5f, 0, 1, diffuse, 1, 1 },
        };

        constexpr DWORD fvf = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
        device->SetTexture(0, tex);
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
        auto hr = device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(ScreenVertex));

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

    // -----------------------------------------------------------------------
    // Element badge drawing
    // -----------------------------------------------------------------------
    void draw_element_badge(Attribute attr, long x, long y, int itemIconSize)
    {
        auto* tex = get_element_texture(attr);
        if (!tex)
            return;

        auto iconSize = (itemIconSize <= 16) ? kSmallElementIconSize : kLargeElementIconSize;
        draw_texture(
            tex,
            static_cast<float>(x + itemIconSize - iconSize - kElementIconInset),
            static_cast<float>(y + itemIconSize - iconSize - kElementIconInset),
            static_cast<float>(iconSize),
            static_cast<float>(iconSize),
            0xFFFFFFFF);
    }

    // -----------------------------------------------------------------------
    // Element detection from CItem (gems first, then base attribute)
    // -----------------------------------------------------------------------
    void draw_element_overlay_for_item(CItem* item, long x, long y)
    {
        if (!item)
            return;

        auto* info = CDataFile::GetItemInfo(item->type, item->typeId);
        if (!info || !is_eligible_type(info->type))
            return;

        for (const auto& gemTypeId : item->gems)
        {
            auto* gemInfo = CDataFile::GetItemInfo(static_cast<int>(ItemType::Lapis), gemTypeId);
            if (gemInfo && is_elemental(gemInfo->attribute))
            {
                draw_element_badge(gemInfo->attribute, x, y, kLargeItemIconSize);
                return;
            }
        }

        if (is_elemental(info->attribute))
            draw_element_badge(info->attribute, x, y, kLargeItemIconSize);
    }
}

// ===========================================================================
// Item render function — called from inventory / quickslot hooks
// ===========================================================================
void draw_item_icon(void* unknown, CItem* item, long x, long y, D3DCOLOR color)
{
    if (!item)
        return;

    typedef void(__thiscall* LPFN)(void*, D3DCOLOR, long, long, int, int, int, bool, bool);
    (*(LPFN)0x4B7240)(unknown, color, x, y, item->type, item->typeId, item->count, false, true);

    draw_element_overlay_for_item(item, x, y);
}

// ===========================================================================
// Naked detours — inventory and quickslot render paths
// ===========================================================================
unsigned u0x518287 = 0x518287;
void __declspec(naked) naked_0x51826D()
{
    __asm
    {
        pushad

        mov edx, dword ptr [esp+0x30]

        push ecx        // color
        push edx        // y
        push ebp        // x

        lea edx, [eax+0x90E2F8]
        push edx        // item

        lea ecx, [esi+0x30]
        push ecx        // unknown
        call draw_item_icon
        add esp, 0x14

        popad
        jmp u0x518287
    }
}

unsigned u0x4FFCF7 = 0x4FFCF7;
void __declspec(naked) naked_0x4FFCE3()
{
    __asm
    {
        pushad

        mov edi, dword ptr [esp+0x30]
        mov eax, dword ptr [esp+0x38]
        mov ecx, dword ptr [esp+0x3C]

        push -0x1       // color
        push eax        // y
        push ecx        // x

        lea edx, [esi+0x90E2F8]
        push edx        // item
        push edi        // unknown
        call draw_item_icon
        add esp, 0x14

        popad
        jmp u0x4FFCF7
    }
}

// ===========================================================================
// hook::item_icon — public entry point
// ===========================================================================
void hook::item_icon()
{
    util::detour((void*)0x51826D, naked_0x51826D, 8);
    util::detour((void*)0x4FFCE3, naked_0x4FFCE3, 5);
}
