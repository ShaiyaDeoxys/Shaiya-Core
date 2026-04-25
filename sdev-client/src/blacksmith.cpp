#include <util/util.h>
#include "include/main.h"

namespace
{
    unsigned u0x4C4426 = 0x4C4426;
    unsigned u0x4C4440 = 0x4C4440;

    void __declspec(naked) naked_0x4C4421()
    {
        __asm
        {
            // itemInfo->effect
            mov al,[eax+0x2C]
            cmp al,0xDC
            jl _original
            cmp al,0xF0
            jg _original
            jmp u0x4C4440

        _original:
            cmp al,0x3E
            jmp u0x4C4426
        }
    }
}

void hook::blacksmith()
{
    // Extend the NPC recreation window item filter to custom rune effects 220..240.
    util::detour((void*)0x4C4421, naked_0x4C4421, 5);
}
