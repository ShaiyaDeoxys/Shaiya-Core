#pragma once

#include <cstdint>

namespace game_addr
{
    // Native text/chat hooks used by visual tokens and the custom chat overlay.
    constexpr auto ChatTextFilter = std::uintptr_t{ 0x422B90 };
    constexpr auto ChatBalloonCreateCall = std::uintptr_t{ 0x412744 };
    constexpr auto ChatBalloonCapture = std::uintptr_t{ 0x41274D };
    constexpr auto FloatingTextCreateCall = std::uintptr_t{ 0x453DEF };
    constexpr auto FloatingStaticTextCapture = std::uintptr_t{ 0x453DF4 };
    constexpr auto StaticTextCreate = std::uintptr_t{ 0x57C280 };
    constexpr auto FloatingStaticTextDraw = std::uintptr_t{ 0x57CA20 };
    constexpr auto NativeTextDraw = std::uintptr_t{ 0x573C00 };

    constexpr auto TextMeasureObject = std::uintptr_t{ 0x22B69B0 };
    constexpr auto TextMeasureWidth = std::uintptr_t{ 0x575740 };

    // Runtime data roots populated by the native SData loaders.
    constexpr auto CDataFile = std::uintptr_t{ 0x91AD64 };
    constexpr auto NpcSkillDataFile = std::uintptr_t{ 0x91ADA8 };
    constexpr auto NpcQuestData = std::uintptr_t{ 0x91ADEC };
    constexpr auto CashProductTable = std::uintptr_t{ 0x90C980 };
    constexpr auto CashProductCount = std::uintptr_t{ 0x90C984 };
    constexpr auto GameAllocator = std::uintptr_t{ 0x632AB5 };
}
