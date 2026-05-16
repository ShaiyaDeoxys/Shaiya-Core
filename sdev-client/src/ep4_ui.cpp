#include <windows.h>
#include <util/util.h>
#include <cstdint>
#include "include/ep4_ui.h"

namespace
{
    struct F2
    {
        float x;
        float y;
    };

    constexpr char kEp4InterfaceDataPath[] = "data/interface";
    constexpr char kEp4ArrowFileName[] = "arrow.png";
    constexpr char kEp4LevelFormat[] = "%2d";
    constexpr char kEp4ClockFormat[] = "%d/%m/%Y %H:%M:%S";
    constexpr double kEp4MainStatsBarLength = 145.0;
    float g_ep4MainServerTimeX = 7.0f;
    float g_ep4MainServerTimeY = 150.0f;
    float g_ep4ClockX = 5.0f;
    float g_ep4ClockY = 163.0f;
    float g_ep4ArrowSizePlus = 16.0f;
    float g_ep4ArrowSizeMinus = -16.0f;

    const F2 kEp4MainStatsPatchXY[10] =
    {
        { 256.0f, 128.0f }, // circle
        { 120.0f,  17.0f }, // name
        {  20.0f,  14.0f }, // level
        {  17.0f,  24.0f }, // secondary circle
        {  60.0f,  40.0f }, // HP bar
        {  81.0f,  43.0f }, // HP text
        {  60.0f,  56.0f }, // MP bar
        {  81.0f,  59.0f }, // MP text
        {  60.0f,  72.0f }, // SP bar
        {  81.0f,  75.0f }, // SP text
    };

    const F2 kEp4EnemyBarPatchXY[3] =
    {
        {   0.0f,  5.0f }, // element
        {  40.0f, 37.0f }, // HP
        { 110.0f, 18.0f }, // name
    };

    const F2 kEp4MainMapButtonPatchXY[4] =
    {
        { 135.0f,  19.0f }, // plus
        { 145.0f,  43.0f }, // minus
        { 142.0f,  93.0f }, // map
        {   0.0f, 112.0f }, // invasion
    };

    const F2 kEp4MainMapClockPatchXY[1] =
    {
        { 55.0f, 142.0f },
    };

    const F2 kEp4MainBottomA[2] = { { 92.0f, 49.0f }, { 501.0f, 49.0f } };
    const float kEp4MainBottomB[3] = { 646.0f, 35.0f, 10.0f };
    const F2 kEp4MainBottom8A[2] = { { 86.0f, 52.0f }, { 376.0f, 52.0f } };
    const float kEp4MainBottom8B[3] = { 505.0f, 27.0f, 22.0f };
    const F2 kEp4MainBottom12A[2] = { { 98.0f, 108.0f }, { 645.0f, 108.0f } };
    const float kEp4MainBottom12B[3] = { 808.0f, 43.0f, 58.0f };
}

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

// EP4 UI support.
// Ported as a single visual layout block, intentionally excluding inventory
// changes and the server-time format override. The existing CONFIG/clock text
// format stays in control while this block moves the surrounding HUD pieces.
unsigned u0x57B560 = 0x57B560;
unsigned u0x631BE0 = 0x631BE0;
unsigned u0x532024 = 0x532024;
void __declspec(naked) naked_ep4_main_stats()
{
    __asm
    {
        pushad
        lea edi,[esi+0xA4]
        lea esi,kEp4MainStatsPatchXY
        mov ecx,20
        cld
        rep movsd
        popad
        fld dword ptr [esi+0xA8]
        jmp u0x532024
    }
}

unsigned u0x53234B = 0x53234B;
void __declspec(naked) naked_ep4_main_stats_bar_hp()
{
    __asm
    {
        fmul qword ptr [kEp4MainStatsBarLength]
        jmp u0x53234B
    }
}

unsigned u0x532487 = 0x532487;
void __declspec(naked) naked_ep4_main_stats_bar_mp()
{
    __asm
    {
        fmul qword ptr [kEp4MainStatsBarLength]
        jmp u0x532487
    }
}

unsigned u0x5325CC = 0x5325CC;
void __declspec(naked) naked_ep4_main_stats_bar_sp()
{
    __asm
    {
        fmul qword ptr [kEp4MainStatsBarLength]
        jmp u0x5325CC
    }
}

unsigned u0x5328A4 = 0x5328A4;
void __declspec(naked) naked_ep4_main_stats_level()
{
    __asm
    {
        push offset kEp4LevelFormat
        jmp u0x5328A4
    }
}

unsigned u0x5350EB = 0x5350EB;
void __declspec(naked) naked_ep4_enemy_bar()
{
    __asm
    {
        pushad
        lea edi,[esi+0x17C]
        lea esi,kEp4EnemyBarPatchXY
        mov ecx,6
        cld
        rep movsd
        popad
        fld dword ptr [esi+0x17C]
        jmp u0x5350EB
    }
}

unsigned u0x532BCD = 0x532BCD;
void __declspec(naked) naked_ep4_enemy_bar_bg()
{
    __asm
    {
        mov dword ptr [esi+0xC],187
        mov dword ptr [esi+0x10],55
        jmp u0x532BCD
    }
}

unsigned u0x534F2E = 0x534F2E;
void __declspec(naked) naked_ep4_enemy_bar_buff()
{
    __asm
    {
        add ecx,10
        push ecx
        push edx
        push 0xFFFFFFFF
        lea ecx,[esi+0xB4]
        jmp u0x534F2E
    }
}

unsigned u0x534F4D = 0x534F4D;
void __declspec(naked) naked_ep4_enemy_bar_debuff()
{
    __asm
    {
        push 16
        push 16
        add eax,10
        push eax
        jmp u0x534F4D
    }
}

unsigned u0x534F7C = 0x534F7C;
unsigned u0x534F9D = 0x534F9D;
void __declspec(naked) naked_ep4_enemy_bar_buff_mouse_over()
{
    __asm
    {
        mov eax,[0x7C3C0C]
        mov eax,[eax]
        mov ecx,[esp+0xC]
        cmp eax,ecx
        jl exit_code
        lea edx,[ecx+0x10]
        cmp eax,edx
        jg exit_code

        mov eax,[0x7C3C10]
        mov eax,[eax]
        mov edx,[esp+0x24]
        add edx,10
        cmp eax,edx
        jl exit_code
        lea edx,[edx+0x10]
        cmp eax,edx
        jg exit_code

        jmp u0x534F7C

    exit_code:
        jmp u0x534F9D
    }
}

unsigned u0x4DE68B = 0x4DE68B;
void __declspec(naked) naked_ep4_main_map_button()
{
    __asm
    {
        pushad
        mov ebp,esi

        lea edi,[ebp+0x242C]
        lea esi,kEp4MainMapButtonPatchXY
        mov ecx,8
        cld
        rep movsd
        add edi,8

        lea esi,kEp4MainMapClockPatchXY
        mov ecx,2
        rep movsd

        popad
        fld dword ptr [esi+0x2430]
        jmp u0x4DE68B
    }
}

unsigned u0x4DF4B4 = 0x4DF4B4;
void __declspec(naked) naked_ep4_main_map_bg()
{
    __asm
    {
        mov dword ptr [esi+0xC],180
        mov dword ptr [esi+0x10],160
        jmp u0x4DF4B4
    }
}

unsigned u0x4E125A = 0x4E125A;
void __declspec(naked) naked_ep4_map_clock()
{
    __asm
    {
        push offset kEp4ClockFormat
        jmp u0x4E125A
    }
}

unsigned u0x4DDF22 = 0x4DDF22;
void __declspec(naked) naked_ep4_main_map_servertime()
{
    __asm
    {
        mov [ecx+0x2424],edx
        fld dword ptr [g_ep4MainServerTimeX]
        mov edx,[esp+0x4]
        fstp dword ptr [esp]
        mov [ecx+0x2428],eax
        fld dword ptr [g_ep4MainServerTimeY]
        mov eax,[esp]
        fstp dword ptr [esp+0x4]
        mov [ecx+0x242C],edx
        fld dword ptr [g_ep4ClockX]
        mov edx,[esp+0x4]
        fstp dword ptr [esp]
        mov [ecx+0x2430],eax
        fld dword ptr [g_ep4ClockY]
        mov eax,[esp]
        fstp dword ptr [esp+0x4]
        jmp u0x4DDF22
    }
}

unsigned u0x4D9A37 = 0x4D9A37;
void __declspec(naked) naked_ep4_arrow_size_map()
{
    __asm
    {
        fld dword ptr [g_ep4ArrowSizeMinus]
        fst dword ptr [esp+0x70]
        add esi,0x1C
        fst dword ptr [esp+0x60]
        lea edx,[esp+0x54]
        fld dword ptr [g_ep4ArrowSizePlus]
        jmp u0x4D9A37
    }
}

unsigned u0x4E0573 = 0x4E0573;
void __declspec(naked) naked_ep4_arrow_size_minimap()
{
    __asm
    {
        fld dword ptr [g_ep4ArrowSizeMinus]
        fst dword ptr [esp+0x20]
        lea edx,[esp+0x74]
        fst dword ptr [esp+0x38]
        push edx
        fld dword ptr [g_ep4ArrowSizePlus]
        jmp u0x4E0573
    }
}

unsigned u0x4D7A66 = 0x4D7A66;
void __declspec(naked) naked_ep4_load_arrow_map()
{
    __asm
    {
        push 32
        push 32
        push offset kEp4ArrowFileName
        push offset kEp4InterfaceDataPath
        lea ecx,[esi+0x84]
        call u0x57B560
        jmp u0x4D7A66
    }
}

unsigned u0x4DE517 = 0x4DE517;
void __declspec(naked) naked_ep4_load_arrow_minimap()
{
    __asm
    {
        push 32
        push 32
        push offset kEp4ArrowFileName
        push offset kEp4InterfaceDataPath
        lea ecx,[esi+0x3C]
        call u0x57B560
        jmp u0x4DE517
    }
}

unsigned u0x493CC4 = 0x493CC4;
void __declspec(naked) naked_ep4_main_bottom()
{
    __asm
    {
        pushad
        mov ebp,esi

        lea edi,[ebp+0x302C]
        lea esi,kEp4MainBottomA
        mov ecx,4
        cld
        rep movsd

        lea edi,[ebp+0x304C]
        lea esi,kEp4MainBottomB
        mov ecx,3
        rep movsd

        popad
        fld dword ptr [esi+0x3054]
        jmp u0x493CC4
    }
}

unsigned u0x493552 = 0x493552;
void __declspec(naked) naked_ep4_main_bottom_8()
{
    __asm
    {
        pushad
        mov ebp,esi

        lea edi,[ebp+0x2FE4]
        lea esi,kEp4MainBottom8A
        mov ecx,4
        cld
        rep movsd

        lea edi,[ebp+0x3000]
        lea esi,kEp4MainBottom8B
        mov ecx,3
        rep movsd

        popad
        fld dword ptr [esi+0x3008]
        jmp u0x493552
    }
}

unsigned u0x494415 = 0x494415;
void __declspec(naked) naked_ep4_main_bottom_12()
{
    __asm
    {
        pushad
        mov ebp,esi

        lea edi,[ebp+0x3078]
        lea esi,kEp4MainBottom12A
        mov ecx,4
        cld
        rep movsd

        lea edi,[ebp+0x3094]
        lea esi,kEp4MainBottom12B
        mov ecx,3
        rep movsd

        popad
        fld dword ptr [esi+0x309C]
        jmp u0x494415
    }
}

unsigned u0x495B6D = 0x495B6D;
void __declspec(naked) naked_ep4_main_bottom_exp_length()
{
    __asm
    {
        sub eax,33
        push eax
        call u0x631BE0
        jmp u0x495B6D
    }
}

unsigned u0x495B56 = 0x495B56;
void __declspec(naked) naked_ep4_main_bottom_exp_width()
{
    __asm
    {
        sub edi,3
        push edi
        fmul qword ptr ds:[0x74E998]
        jmp u0x495B56
    }
}

unsigned u0x494D76 = 0x494D76;
void __declspec(naked) naked_ep4_main_bottom_exp_text()
{
    __asm
    {
        mov [esp+0xC],220
        jmp u0x494D76
    }
}

unsigned u0x495CA1 = 0x495CA1;
void __declspec(naked) naked_ep4_main_bottom_bless()
{
    __asm
    {
        sub eax,5
        add edi,67
        push eax
        fld dword ptr [esp+0x24]
        jmp u0x495CA1
    }
}

unsigned u0x495D7E = 0x495D7E;
void __declspec(naked) naked_ep4_main_bottom_bless_glow()
{
    __asm
    {
        fld dword ptr [esp+0x24]
        fstp dword ptr [esp]
        sub eax,5
        jmp u0x495D7E
    }
}

unsigned u0x495693 = 0x495693;
void __declspec(naked) naked_ep4_main_bottom_8_exp_length()
{
    __asm
    {
        sub eax,45
        push eax
        call u0x631BE0
        jmp u0x495693
    }
}

unsigned u0x49567C = 0x49567C;
void __declspec(naked) naked_ep4_main_bottom_8_exp_width()
{
    __asm
    {
        sub edi,1
        push edi
        fmul qword ptr ds:[0x74E9B8]
        jmp u0x49567C
    }
}

unsigned u0x494D68 = 0x494D68;
void __declspec(naked) naked_ep4_main_bottom_8_exp_text()
{
    __asm
    {
        mov [esp+0xC],174
        jmp u0x494D68
    }
}

unsigned u0x4957BA = 0x4957BA;
void __declspec(naked) naked_ep4_main_bottom_8_bless()
{
    __asm
    {
        sub eax,1
        add edi,53
        push eax
        fld dword ptr [esp+0x24]
        jmp u0x4957BA
    }
}

unsigned u0x49588E = 0x49588E;
void __declspec(naked) naked_ep4_main_bottom_8_bless_glow()
{
    __asm
    {
        fld dword ptr [esp+0x24]
        fstp dword ptr [esp]
        sub eax,1
        jmp u0x49588E
    }
}

unsigned u0x496077 = 0x496077;
void __declspec(naked) naked_ep4_main_bottom_12_exp_length()
{
    __asm
    {
        sub eax,21
        push eax
        call u0x631BE0
        jmp u0x496077
    }
}

unsigned u0x496060 = 0x496060;
void __declspec(naked) naked_ep4_main_bottom_12_exp_width()
{
    __asm
    {
        sub ebx,3
        push ebx
        fmul qword ptr ds:[0x74E988]
        jmp u0x496060
    }
}

unsigned u0x494D90 = 0x494D90;
void __declspec(naked) naked_ep4_main_bottom_12_exp_text()
{
    __asm
    {
        mov dword ptr [esp+0xC],275
        mov dword ptr [esp+0x8],109
        jmp u0x494D90
    }
}

unsigned u0x4961AB = 0x4961AB;
void __declspec(naked) naked_ep4_main_bottom_12_bless()
{
    __asm
    {
        sub edx,3
        add ebx,79
        push edx
        fld dword ptr [esp+0x24]
        jmp u0x4961AB
    }
}

unsigned u0x49626C = 0x49626C;
void __declspec(naked) naked_ep4_main_bottom_12_bless_glow()
{
    __asm
    {
        fld dword ptr [esp+0x2C]
        fstp dword ptr [esp]
        sub eax,3
        jmp u0x49626C
    }
}

unsigned u0x51F420 = 0x51F420;
void __declspec(naked) naked_ep4_option_main_button()
{
    __asm
    {
        pushad
        lea eax,[esi+0x1AC38]
        mov dword ptr [eax],64
        popad
        mov ecx,[esi+0x1AC38]
        jmp u0x51F420
    }
}

unsigned u0x4CFE60 = 0x4CFE60;
void __declspec(naked) naked_ep4_loadbar()
{
    __asm
    {
        pushad
        lea eax,[esi+0x6610]
        mov dword ptr [eax],0x44020000
        add eax,20
        mov dword ptr [eax],0x42DC0000
        add eax,4
        mov dword ptr [eax],450
        add eax,4
        mov dword ptr [eax],0x425C0000
        add eax,8
        mov dword ptr [eax],0x41F00000
        popad
        fsub dword ptr [esi+0x6624]
        jmp u0x4CFE60
    }
}

unsigned u0x475C88 = 0x475C88;
void __declspec(naked) naked_0x475C83()
{
    __asm
    {
        // Select screen texture layout tweak.
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

void ep4_ui::install_hooks(bool customUiEnabled)
{
    // dungeon wings shadow workaround
    util::detour((void*)0x41F9C0, naked_0x41F9C0, 9);
    // evolution bug
    util::detour((void*)0x41E2BB, naked_0x41E2BB, 7);
    // Server-time clock text format for both standard and EP6 interface packs.
    util::detour((void*)0x4E1255, naked_ep4_map_clock, 5);

    if (!customUiEnabled)
    {
        // EP4 UI support.
        // Applies the EP4 HUD/layout package while intentionally leaving inventory
        // and the existing server-time text format untouched. Disabled for
        // ADVANCED -> UI=1/2 so the custom intf_epi6/8 layout stays coherent.
        util::detour((void*)0x53201E, naked_ep4_main_stats, 6);
        util::detour((void*)0x532345, naked_ep4_main_stats_bar_hp, 6);
        util::detour((void*)0x532481, naked_ep4_main_stats_bar_mp, 6);
        util::detour((void*)0x5325C6, naked_ep4_main_stats_bar_sp, 6);
        util::detour((void*)0x53289F, naked_ep4_main_stats_level, 5);
        util::detour((void*)0x5350E5, naked_ep4_enemy_bar, 6);
        util::detour((void*)0x532BBF, naked_ep4_enemy_bar_bg, 7);
        util::detour((void*)0x534F24, naked_ep4_enemy_bar_buff, 10);
        util::detour((void*)0x534F48, naked_ep4_enemy_bar_debuff, 5);
        util::detour((void*)0x534F57, naked_ep4_enemy_bar_buff_mouse_over, 5);
        util::detour((void*)0x4DE685, naked_ep4_main_map_button, 6);
        util::detour((void*)0x4DF4AD, naked_ep4_main_map_bg, 7);
        util::detour((void*)0x4DDEDA, naked_ep4_main_map_servertime, 6);
        util::detour((void*)0x4D9A1C, naked_ep4_arrow_size_map, 6);
        util::detour((void*)0x4E055A, naked_ep4_arrow_size_minimap, 6);
        util::detour((void*)0x4D7A47, naked_ep4_load_arrow_map, 5);
        util::detour((void*)0x4DE4FB, naked_ep4_load_arrow_minimap, 5);
        util::detour((void*)0x493CBE, naked_ep4_main_bottom, 6);
        util::detour((void*)0x495B67, naked_ep4_main_bottom_exp_length, 6);
        util::detour((void*)0x495B4F, naked_ep4_main_bottom_exp_width, 7);
        util::detour((void*)0x494D6E, naked_ep4_main_bottom_exp_text, 8);
        util::detour((void*)0x495C9C, naked_ep4_main_bottom_bless, 5);
        util::detour((void*)0x495D77, naked_ep4_main_bottom_bless_glow, 7);
        util::detour((void*)0x49354C, naked_ep4_main_bottom_8, 6);
        util::detour((void*)0x49568D, naked_ep4_main_bottom_8_exp_length, 6);
        util::detour((void*)0x495675, naked_ep4_main_bottom_8_exp_width, 7);
        util::detour((void*)0x494D60, naked_ep4_main_bottom_8_exp_text, 8);
        util::detour((void*)0x4957B5, naked_ep4_main_bottom_8_bless, 5);
        util::detour((void*)0x495887, naked_ep4_main_bottom_8_bless_glow, 7);
        util::detour((void*)0x49440F, naked_ep4_main_bottom_12, 6);
        util::detour((void*)0x496071, naked_ep4_main_bottom_12_exp_length, 6);
        util::detour((void*)0x496059, naked_ep4_main_bottom_12_exp_width, 7);
        util::detour((void*)0x494D80, naked_ep4_main_bottom_12_exp_text, 8);
        util::detour((void*)0x4961A6, naked_ep4_main_bottom_12_bless, 5);
        util::detour((void*)0x496265, naked_ep4_main_bottom_12_bless_glow, 7);
        util::detour((void*)0x51F41A, naked_ep4_option_main_button, 6);
        util::detour((void*)0x4CFE5A, naked_ep4_loadbar, 6);
        util::detour((void*)0x493362, (void*)0x493442, 7);
    }
}

void ep4_ui::install_select_screen_hooks()
{
    // Character select screen texture/text adjustments.
    util::detour((void*)0x475C83, naked_0x475C83, 5);
    util::detour((void*)0x475C90, naked_0x475C90, 8);
}
