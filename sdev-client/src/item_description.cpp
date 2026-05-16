// ===========================================================================
// item_description.cpp — OJ reroll info & grade viewer
//
// Augments item descriptions with:
//   * Maximum OJs / Highest OJ  (or "Can not be rerolled" when 0)
//   * Grade value               (admin-only, derived from dropGrade)
//
// Strategy:  NO runtime hook on GetItemInfo.
//
// A background thread waits until the game has finished loading item data,
// then iterates every (type, typeId) combination once, modifying each
// ItemInfo::description pointer in-place.  After the scan completes the
// thread exits — there is zero per-frame overhead because GetItemInfo
// (0x46CB30) is never hooked.
//
// Thread safety: ItemInfo::description is a char* at offset 0x04 (4-byte
// aligned).  On x86 a naturally aligned 4-byte write is atomic, so the
// main thread will either see the old or the new pointer — never a torn
// value.  The old pointer is in the game's data segment and remains valid,
// so any in-progress read finishes safely.
// ===========================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include "include/main.h"
#include "include/shaiya/CPlayerData.h"
#include "include/shaiya/ItemInfo.h"

using namespace shaiya;

namespace
{
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------

    // Scan bounds.  type 1-150 covers all known Shaiya item categories.
    // typeId up to 2047 is a generous upper bound.
    constexpr int kMaxType   = 151;
    constexpr int kMaxTypeId = 2048;

    // Original GetItemInfo — called directly, never hooked.
    using GetItemInfoFn = ItemInfo*(__thiscall*)(void*, int, int);

    // Item types whose tooltip can show OJ reroll information (O(1) table).
    constexpr std::array<bool, 151> kRerollableItemTypes = []()
    {
        std::array<bool, 151> table{};
        constexpr int allowed[] = {
             1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
            21, 22, 23, 24, 31, 32, 33, 34, 35, 36, 37, 39, 40, 45, 46, 47, 48, 49, 50, 51,
            52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71,
            72, 73, 74, 76, 77, 82, 83, 84, 85, 86, 87, 88, 89, 91, 92, 96, 97,120,121,150
        };
        for (auto t : allowed) table[t] = true;
        return table;
    }();

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------
    bool is_rerollable_type(std::uint8_t itemType)
    {
        return itemType < kRerollableItemTypes.size() && kRerollableItemTypes[itemType];
    }

    // -----------------------------------------------------------------------
    // Build & apply augmented description
    // -----------------------------------------------------------------------
    void augment_description(ItemInfo* info, bool isAdmin)
    {
        bool showGrade  = isAdmin && info->type >= 1 && info->type <= 150;
        bool showReroll = is_rerollable_type(info->type);

        if (!showGrade && !showReroll)
            return;

        std::string text;
        bool hasLines = false;

        // Grade line (admin-only).
        if (showGrade)
        {
            auto grade = static_cast<std::uint16_t>(info->dropGrade & 0xFFFF);
            text += "Grade : ";
            text += std::to_string(grade);
            hasLines = true;
        }

        // OJ / reroll line.
        if (showReroll)
        {
            if (hasLines)
                text += "\n";

            text += "{c3}";
            if (info->craftExpansions == 0)
            {
                text += "Can not be rerolled";
            }
            else
            {
                text += "Maximum Ojs : ";
                text += std::to_string(info->craftExpansions);
                text += "\nHighest OJ : ";
                text += std::to_string(info->reqWis);
            }
            text += "{/c}";
            hasLines = true;
        }

        // Append the original description after a blank line separator.
        if (info->description && info->description[0])
        {
            if (hasLines)
                text += "\n\n";
            text += info->description;
        }

        // Replace the description pointer in-place.
        auto size = text.size() + 1;
        auto* buf = new char[size];
        std::memcpy(buf, text.c_str(), size);

        // Atomic on x86 (aligned 4-byte write).
        info->description = buf;
    }

    // -----------------------------------------------------------------------
    // Background scan thread
    //
    // 1. Wait for item data to be loaded (GetItemInfo returns non-null).
    // 2. Wait for player data to be available (login complete).
    // 3. Scan every (type, typeId) once and augment descriptions.
    // 4. Thread exits — zero permanent overhead.
    // -----------------------------------------------------------------------
    DWORD WINAPI description_scan_thread(LPVOID)
    {
        auto getItemInfo = reinterpret_cast<GetItemInfoFn>(0x46CB30);
        auto* dataFile   = reinterpret_cast<void*>(0x91AD64);

        // Phase 1: wait until item data is loaded.
        for (;;)
        {
            auto* test = getItemInfo(dataFile, 1, 1);
            if (test)
                break;
            Sleep(200);
        }

        // Phase 2: wait until player data is ready (post-login).
        while (!g_pPlayerData)
            Sleep(200);

        // Small stabilization delay so admin flag is populated.
        Sleep(500);

        // Phase 3: one-time scan — modify descriptions in-place.
        // Track processed pointers because different (type, typeId) values
        // can map to the same ItemInfo* (typeId is uint8_t internally, so
        // values above 255 wrap and return the same struct).
        bool isAdmin = g_pPlayerData && g_pPlayerData->isAdmin;
        std::unordered_set<const ItemInfo*> processed;

        for (int type = 1; type < kMaxType; ++type)
        {
            for (int typeId = 0; typeId < kMaxTypeId; ++typeId)
            {
                auto* info = getItemInfo(dataFile, type, typeId);
                if (info && processed.insert(info).second)
                    augment_description(info, isAdmin);
            }
        }

        // Done.  Thread exits, GetItemInfo was never hooked.
        return 0;
    }
}

// ===========================================================================
// hook::item_description — public entry point
//
// Launches the background scan thread.  No runtime hooks are installed.
// ===========================================================================
void hook::item_description()
{
    CreateThread(nullptr, 0, description_scan_thread, nullptr, 0, nullptr);
}
