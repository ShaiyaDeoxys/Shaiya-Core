#include <util/util.h>
#include "include/main.h"

// Improve pvp rank icons display
unsigned u0x44EC64 = 0x0044EC64;
void __declspec(naked) naked_0x44EC5C()
{
    __asm
    {
        mov eax,dword ptr [esp+0x28]
        add eax,3

        // original
        fild dword ptr [esp+0x30]
        jmp u0x44EC64
    }
}

unsigned u0x44ECC3 = 0x0044ECC3;
void __declspec(naked) naked_0x44ECBC()
{
    __asm
    {
        mov ecx,dword ptr [esp+0x30]
        add ecx,5

        // original
        fstp dword ptr [esp]
        jmp u0x44ECC3
    }
}

void hook::ui_image()
{
    util::detour((void*)0x0044EC5C, naked_0x44EC5C, 8);
    util::detour((void*)0x0044ECBC, naked_0x44ECBC, 7);
}
