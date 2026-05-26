#include <windows.h>
#include <util/util.h>
#include <cstdint>
#include "include/interface_hooks.h"

namespace
{
    constexpr char kClockFormat[] = "%d/%m/%Y %H:%M:%S";
}

// ---------------------------------------------------------------------------
// General interface hooks
// ---------------------------------------------------------------------------

// Dungeon wings shadow workaround.
unsigned n0x41F9ED = 0x41F9ED;
unsigned u0x41F9C9 = 0x41F9C9;
void __declspec(naked) naked_0x41F9C0()
{
    __asm
    {
        // character->wings
        cmp dword ptr[esi+0x434],0x0
        jne _0x41F9ED

        // original
        mov edx,[esi+0x10]
        fld dword ptr ds:[0x748160]
        jmp u0x41F9C9

        _0x41F9ED:
        jmp n0x41F9ED
    }
}

// Evolution bug fix.
unsigned u0x41BB40 = 0x41BB40;
unsigned u0x4110F0 = 0x4110F0;
unsigned u0x41F5E6 = 0x41F5E6;
unsigned u0x41E2CD = 0x41E2CD;
void __declspec(naked) naked_0x41E2BB()
{
    __asm
    {
        // original
        mov ecx,esi
        call u0x41BB40
        test eax,eax
        jne original
        // continue
        jmp u0x41E2CD

        original:
        mov ecx,esi
        call u0x4110F0
        // exit
        jmp u0x41F5E6
    }
}

// Clock text format override.
unsigned u0x4E125A = 0x4E125A;
void __declspec(naked) naked_map_clock()
{
    __asm
    {
        push offset kClockFormat
        jmp u0x4E125A
    }
}

// Character select screen texture/text adjustments.
unsigned u0x631BE0 = 0x631BE0;

unsigned u0x475C88 = 0x475C88;
void __declspec(naked) naked_0x475C83()
{
    __asm
    {
        add eax, 0xFFFFFC00
        sar eax, 0x02
        jmp u0x475C88
    }
}

unsigned u0x475C98 = 0x475C98;
void __declspec(naked) naked_0x475C90()
{
    __asm
    {
        pushad
        lea eax, [ebx-0x04]
        mov dword ptr [eax], 0xC37B0000
        add eax, 0x08
        call u0x631BE0
        popad
        jmp u0x475C98
    }
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

void interface_hooks::install_hooks()
{
    // Dungeon wings shadow workaround.
    util::detour((void*)0x41F9C0, naked_0x41F9C0, 9);
    // Evolution bug fix.
    util::detour((void*)0x41E2BB, naked_0x41E2BB, 7);
    // Clock text format override.
    util::detour((void*)0x4E1255, naked_map_clock, 5);
    // Clock X position: stock `add edx, 0x15` (21px) at 0x4E1299.
    // Reduce to 0x00 (0px) to shift the clock text left.
    util::write_memory((void*)0x4E129B, 0x00, 1);
}

void interface_hooks::install_select_screen_hooks()
{
    // Character select screen texture/text adjustments.
    util::detour((void*)0x475C83, naked_0x475C83, 5);
    util::detour((void*)0x475C90, naked_0x475C90, 8);
}
