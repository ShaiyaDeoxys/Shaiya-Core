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
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <util/util.h>
#include "include/game_data_archive.h"
#include "include/texture_util.h"
#include "include/main.h"
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/CItem.h"
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
    };

    ElementTexture g_textures[kElementCount]{};
    bool g_sahScanned = false;

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    void scan_sah()
    {
        if (g_sahScanned)
            return;
        g_sahScanned = true;

        game_data::scan_sah_files([](const game_data::SahFileEntry& entry) {
            bool isTarget = (entry.lowerPath == kAssetFolder)
                || (entry.lowerPath == std::string("data\\") + kAssetFolder);
            if (!isTarget)
                return;

            for (int e = 0; e < kElementCount; ++e)
            {
                if (!g_textures[e].found && entry.lowerFileName == kElementFileNames[e])
                {
                    g_textures[e].sahOffset = entry.offset;
                    g_textures[e].sahSize = entry.size;
                    g_textures[e].found = true;
                    break;
                }
            }
        });
    }

    void ensure_texture_loaded(int index, LPDIRECT3DDEVICE9 device)
    {
        auto& et = g_textures[index];
        if (et.texture || et.loadAttempted)
            return;
        et.loadAttempted = true;

        if (!et.found || et.sahSize == 0 || !device)
            return;

        std::vector<char> fileData;
        if (!game_data::read_saf_file(et.sahOffset, et.sahSize, fileData))
            return;

        et.texture = texture_util::create_from_image_memory(device, fileData.data(), fileData.size());
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

        auto* device = g_var ? g_var->camera.device : nullptr;
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

    bool draw_texture(LPDIRECT3DTEXTURE9 tex, float x, float y, float w, float h, D3DCOLOR diffuse)
    {
        auto device = g_var ? g_var->camera.device : nullptr;
        return texture_util::draw_screen_quad(device, tex, x, y, w, h, diffuse);
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
