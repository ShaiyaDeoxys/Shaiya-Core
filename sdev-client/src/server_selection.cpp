#include <windows.h>
#include <util/util.h>
#include <cstdint>
#include "include/server_selection.h"

namespace
{
    std::uint32_t g_skipServerSelectionWindow = 0;
}

unsigned u0x50CA15 = 0x50CA15;
unsigned u0x50C5F0 = 0x50C5F0;
unsigned u0x50BEA6 = 0x50BEA6;
unsigned u0x50D16D = 0x50D16D;

void __declspec(naked) naked_skip_server_selection()
{
    __asm
    {
        // Skip server selection screen.
        // If the login server returned exactly one available server, select
        // index 0 and call the original CSelectServer::OnSelect path. This
        // keeps the stock connection/state transition logic intact instead of
        // fabricating packets or jumping to character select directly.
        mov dword ptr ds:[g_skipServerSelectionWindow], ecx
        mov eax, dword ptr ds:[0x22EED30]
        test eax, eax
        je original
        cmp dword ptr [eax + 0x274], 1
        jne original
        cmp dword ptr [ecx + 0xBD8], 0xFFFFFFFF
        jne original
        mov dword ptr [ecx + 0xBD8], 0
        call u0x50C5F0
        ret

    original:
        jmp u0x50CA15
    }
}

void __declspec(naked) naked_auto_select_single_server_after_init()
{
    __asm
    {
        // Delayed single-server auto-selection.
        // Run after the server-selection UI object is fully initialized, but
        // before its first normal update/render pass. This keeps the original
        // CSelectServer::OnSelect connection path and only moves *when* it is
        // invoked. If this misbehaves, remove only the 0050D164 detour below.
        pushad
        mov eax, dword ptr ds:[0x22EED30]
        test eax, eax
        je done
        cmp dword ptr [eax + 0x274], 1
        jne done
        cmp dword ptr [esi + 0xBD8], 0xFFFFFFFF
        jne done
        mov dword ptr ds:[g_skipServerSelectionWindow], esi
        mov dword ptr [esi + 0xBD8], 0
        mov ecx, esi
        call u0x50C5F0

    done:
        popad
        // Original constructor epilogue instructions overwritten at 0050D164.
        mov eax, esi
        mov ecx, dword ptr [esp + 0x120]
        jmp u0x50D16D
    }
}

void __declspec(naked) naked_hide_single_server_selection_render()
{
    __asm
    {
        mov eax, dword ptr ds:[0x22EED30]
        test eax, eax
        je original
        cmp dword ptr [eax + 0x274], 1
        jne original
        ret

    original:
        // Original CSelectServer render prologue overwritten at 0050BEA0.
        sub esp, 0x18
        push ebx
        push ebp
        push esi
        jmp u0x50BEA6
    }
}

void server_selection::install_skip_hooks()
{
    util::detour((void*)0x50CA10, naked_skip_server_selection, 5);
    util::detour((void*)0x50BEA0, naked_hide_single_server_selection_render, 6);
    util::detour((void*)0x50D164, naked_auto_select_single_server_after_init, 9);
}
