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
}
