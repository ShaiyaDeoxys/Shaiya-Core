#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include "include/main.h"
#include "include/shaiya/CWindow.h"
#include "include/shaiya/Static.h"
using namespace shaiya;

namespace
{
    void on_main_stats_draw(CWindow*)
    {
        // Tick the welcome system message (post-login notice). The teleport
        // panel is now fully handled by ImGui; this hook only keeps the
        // welcome message timer running.
        tick_client_welcome_sysmsg();
    }

    unsigned u0x532A2A = 0x532A2A;
    void __declspec(naked) naked_0x532A23()
    {
        __asm
        {
            pushad
            push esi
            call on_main_stats_draw
            add esp,0x4
            popad

            mov ecx,[esp+0x108]
            jmp u0x532A2A
        }
    }
}

void hook::main_stats()
{
    // Hook the main stats draw path to tick the welcome message timer.
    util::detour((void*)0x532A23, naked_0x532A23, 7);
}
