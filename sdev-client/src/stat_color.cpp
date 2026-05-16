#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include <cstdint>

#include "include/stat_color.h"

// ---------------------------------------------------------------------------
// Stats color hooks
// ---------------------------------------------------------------------------
// Colour on stats at (T) window.
// Each hook keeps the original stat value load and only replaces the B/G/R
// color arguments used by the stock render call.

#define DEFINE_STATS_COLOR_HOOK(NAME, RETURN_ADDR, BLUE, GREEN, RED) \
    unsigned NAME##Return = RETURN_ADDR; \
    void __declspec(naked) NAME() \
    { \
        __asm push BLUE \
        __asm fild dword ptr ds:[0x22B1954] \
        __asm push GREEN \
        __asm push RED \
        __asm jmp NAME##Return \
    }

DEFINE_STATS_COLOR_HOOK(naked_stats_color_strength, 0x52A1AA, 0x00, 0x00, 0xFF)
DEFINE_STATS_COLOR_HOOK(naked_stats_color_reaction, 0x52A56A, 0xCE, 0x00, 0xFF)
DEFINE_STATS_COLOR_HOOK(naked_stats_color_intelligence, 0x52A92A, 0xFF, 0x80, 0x80)
DEFINE_STATS_COLOR_HOOK(naked_stats_color_wisdom, 0x52ACEA, 0x00, 0xFF, 0x00)
DEFINE_STATS_COLOR_HOOK(naked_stats_color_dexterity, 0x52B0AA, 0x00, 0x80, 0xFF)
DEFINE_STATS_COLOR_HOOK(naked_stats_color_lucky, 0x52B46A, 0xFF, 0xFF, 0x00)

#undef DEFINE_STATS_COLOR_HOOK

namespace
{
    void patch_stats_window_colors()
    {
        util::detour((void*)0x52A195, naked_stats_color_strength, 5);
        util::detour((void*)0x52A555, naked_stats_color_reaction, 5);
        util::detour((void*)0x52A915, naked_stats_color_intelligence, 5);
        util::detour((void*)0x52ACD5, naked_stats_color_wisdom, 5);
        util::detour((void*)0x52B095, naked_stats_color_dexterity, 5);
        util::detour((void*)0x52B455, naked_stats_color_lucky, 5);
    }
}

void stat_color::install_stats_color_hooks()
{
    patch_stats_window_colors();
}
