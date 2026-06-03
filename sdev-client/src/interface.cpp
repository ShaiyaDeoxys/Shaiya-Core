#include <windows.h>
#include <util/util.h>
#include <cstdint>
#include "include/config.h"
#include "include/interface.h"

namespace
{
    // Jump targets
    unsigned n0x41F9ED = 0x41F9ED, u0x41F9C9 = 0x41F9C9;
    unsigned u0x41BB40 = 0x41BB40, u0x4110F0 = 0x4110F0, u0x41F5E6 = 0x41F5E6, u0x41E2CD = 0x41E2CD;
    unsigned u0x44EC64 = 0x44EC64, u0x44ECC3 = 0x44ECC3;
    unsigned u0x57B560 = 0x57B560, u0x631BE0 = 0x631BE0;
    unsigned u0x532024 = 0x532024, u0x53234B = 0x53234B, u0x532487 = 0x532487, u0x5325CC = 0x5325CC, u0x5328A4 = 0x5328A4;
    unsigned u0x5350EB = 0x5350EB, u0x532BCD = 0x532BCD;
    unsigned u0x534F2E = 0x534F2E, u0x534F4D = 0x534F4D, u0x534F7C = 0x534F7C, u0x534F9D = 0x534F9D;
    unsigned u0x4DE68B = 0x4DE68B, u0x4DF4B4 = 0x4DF4B4, u0x4E125A = 0x4E125A, u0x4DDF22 = 0x4DDF22;
    unsigned u0x4D9A37 = 0x4D9A37, u0x4E0573 = 0x4E0573, u0x4D7A66 = 0x4D7A66, u0x4DE517 = 0x4DE517;
    unsigned u0x493CC4 = 0x493CC4, u0x493552 = 0x493552, u0x494415 = 0x494415;
    unsigned u0x495B6D = 0x495B6D, u0x495B56 = 0x495B56, u0x494D76 = 0x494D76, u0x495CA1 = 0x495CA1, u0x495D7E = 0x495D7E;
    unsigned u0x495693 = 0x495693, u0x49567C = 0x49567C, u0x494D68 = 0x494D68, u0x4957BA = 0x4957BA, u0x49588E = 0x49588E;
    unsigned u0x496077 = 0x496077, u0x496060 = 0x496060, u0x494D90 = 0x494D90, u0x4961AB = 0x4961AB, u0x49626C = 0x49626C;
    unsigned u0x4CFE60 = 0x4CFE60;
    unsigned u0x475C88 = 0x475C88, u0x475C98 = 0x475C98;

    struct F2 { float x; float y; };

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
        { 256.0f, 128.0f }, { 120.0f,  17.0f }, {  20.0f,  14.0f },
        {  17.0f,  24.0f }, {  60.0f,  40.0f }, {  81.0f,  43.0f },
        {  60.0f,  56.0f }, {  81.0f,  59.0f }, {  60.0f,  72.0f },
        {  81.0f,  75.0f },
    };
    const F2 kEp4EnemyBarPatchXY[3] = { { 0.0f, 5.0f }, { 40.0f, 37.0f }, { 110.0f, 18.0f } };
    const F2 kEp4MainMapButtonPatchXY[4] = { { 135.0f, 19.0f }, { 145.0f, 43.0f }, { 142.0f, 93.0f }, { 0.0f, 112.0f } };
    const F2 kEp4MainMapClockPatchXY[1] = { { 55.0f, 142.0f } };
    const F2 kEp4MainBottomA[2] = { { 92.0f, 49.0f }, { 501.0f, 49.0f } };
    const float kEp4MainBottomB[3] = { 646.0f, 35.0f, 10.0f };
    const F2 kEp4MainBottom8A[2] = { { 86.0f, 52.0f }, { 376.0f, 52.0f } };
    const float kEp4MainBottom8B[3] = { 505.0f, 27.0f, 22.0f };
    const F2 kEp4MainBottom12A[2] = { { 98.0f, 108.0f }, { 645.0f, 108.0f } };
    const float kEp4MainBottom12B[3] = { 808.0f, 43.0f, 58.0f };

    // -- Wings shadow workaround --
    void __declspec(naked) naked_0x41F9C0()
    {
        __asm
        {
            cmp dword ptr[esi+0x434],0x0
            jne _0x41F9ED
            mov edx,[esi+0x10]
            fld dword ptr ds:[0x748160]
            jmp u0x41F9C9
        _0x41F9ED:
            jmp n0x41F9ED
        }
    }

    // -- Evolution bug fix --
    void __declspec(naked) naked_0x41E2BB()
    {
        __asm
        {
            mov ecx,esi
            call u0x41BB40
            test eax,eax
            jne original
            jmp u0x41E2CD
        original:
            mov ecx,esi
            call u0x4110F0
            jmp u0x41F5E6
        }
    }

    // -- PvP rank icon alignment --
    void __declspec(naked) naked_pvp_rank_icon_x()
    {
        __asm
        {
            mov eax,dword ptr [esp+0x28]
            add eax,3
            fild dword ptr [esp+0x30]
            jmp u0x44EC64
        }
    }
    void __declspec(naked) naked_pvp_rank_icon_y()
    {
        __asm
        {
            mov ecx,dword ptr [esp+0x30]
            add ecx,5
            fstp dword ptr [esp]
            jmp u0x44ECC3
        }
    }

    // -- Clock format and position --
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

    void __declspec(naked) naked_ep4_map_clock()
    {
        __asm
        {
            push offset kEp4ClockFormat
            jmp u0x4E125A
        }
    }

    // -- EP4 HUD layout --
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
    void __declspec(naked) naked_ep4_main_stats_bar_hp()
    { __asm fmul qword ptr [kEp4MainStatsBarLength] __asm jmp u0x53234B }
    void __declspec(naked) naked_ep4_main_stats_bar_mp()
    { __asm fmul qword ptr [kEp4MainStatsBarLength] __asm jmp u0x532487 }
    void __declspec(naked) naked_ep4_main_stats_bar_sp()
    { __asm fmul qword ptr [kEp4MainStatsBarLength] __asm jmp u0x5325CC }
    void __declspec(naked) naked_ep4_main_stats_level()
    { __asm push offset kEp4LevelFormat __asm jmp u0x5328A4 }

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
    void __declspec(naked) naked_ep4_enemy_bar_bg()
    { __asm mov dword ptr [esi+0xC],187 __asm mov dword ptr [esi+0x10],55 __asm jmp u0x532BCD }
    void __declspec(naked) naked_ep4_enemy_bar_buff()
    { __asm add ecx,10 __asm push ecx __asm push edx __asm push 0xFFFFFFFF __asm lea ecx,[esi+0xB4] __asm jmp u0x534F2E }
    void __declspec(naked) naked_ep4_enemy_bar_debuff()
    { __asm push 16 __asm push 16 __asm add eax,10 __asm push eax __asm jmp u0x534F4D }

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
    void __declspec(naked) naked_ep4_main_map_bg()
    { __asm mov dword ptr [esi+0xC],180 __asm mov dword ptr [esi+0x10],160 __asm jmp u0x4DF4B4 }

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
    void __declspec(naked) naked_ep4_load_arrow_map()
    { __asm push 32 __asm push 32 __asm push offset kEp4ArrowFileName __asm push offset kEp4InterfaceDataPath __asm lea ecx,[esi+0x84] __asm call u0x57B560 __asm jmp u0x4D7A66 }
    void __declspec(naked) naked_ep4_load_arrow_minimap()
    { __asm push 32 __asm push 32 __asm push offset kEp4ArrowFileName __asm push offset kEp4InterfaceDataPath __asm lea ecx,[esi+0x3C] __asm call u0x57B560 __asm jmp u0x4DE517 }

    // -- Bottom bar layouts --
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

    void __declspec(naked) naked_ep4_main_bottom_exp_length()
    { __asm sub eax,33 __asm push eax __asm call u0x631BE0 __asm jmp u0x495B6D }
    void __declspec(naked) naked_ep4_main_bottom_exp_width()
    { __asm sub edi,3 __asm push edi __asm fmul qword ptr ds:[0x74E998] __asm jmp u0x495B56 }
    void __declspec(naked) naked_ep4_main_bottom_exp_text()
    { __asm mov [esp+0xC],220 __asm jmp u0x494D76 }
    void __declspec(naked) naked_ep4_main_bottom_bless()
    { __asm sub eax,5 __asm add edi,67 __asm push eax __asm fld dword ptr [esp+0x24] __asm jmp u0x495CA1 }
    void __declspec(naked) naked_ep4_main_bottom_bless_glow()
    { __asm fld dword ptr [esp+0x24] __asm fstp dword ptr [esp] __asm sub eax,5 __asm jmp u0x495D7E }

    void __declspec(naked) naked_ep4_main_bottom_8_exp_length()
    { __asm sub eax,45 __asm push eax __asm call u0x631BE0 __asm jmp u0x495693 }
    void __declspec(naked) naked_ep4_main_bottom_8_exp_width()
    { __asm sub edi,1 __asm push edi __asm fmul qword ptr ds:[0x74E9B8] __asm jmp u0x49567C }
    void __declspec(naked) naked_ep4_main_bottom_8_exp_text()
    { __asm mov [esp+0xC],174 __asm jmp u0x494D68 }
    void __declspec(naked) naked_ep4_main_bottom_8_bless()
    { __asm sub eax,1 __asm add edi,53 __asm push eax __asm fld dword ptr [esp+0x24] __asm jmp u0x4957BA }
    void __declspec(naked) naked_ep4_main_bottom_8_bless_glow()
    { __asm fld dword ptr [esp+0x24] __asm fstp dword ptr [esp] __asm sub eax,1 __asm jmp u0x49588E }

    void __declspec(naked) naked_ep4_main_bottom_12_exp_length()
    { __asm sub eax,21 __asm push eax __asm call u0x631BE0 __asm jmp u0x496077 }
    void __declspec(naked) naked_ep4_main_bottom_12_exp_width()
    { __asm sub ebx,3 __asm push ebx __asm fmul qword ptr ds:[0x74E988] __asm jmp u0x496060 }
    void __declspec(naked) naked_ep4_main_bottom_12_exp_text()
    { __asm mov dword ptr [esp+0xC],275 __asm mov dword ptr [esp+0x8],109 __asm jmp u0x494D90 }
    void __declspec(naked) naked_ep4_main_bottom_12_bless()
    { __asm sub edx,3 __asm add ebx,79 __asm push edx __asm fld dword ptr [esp+0x24] __asm jmp u0x4961AB }
    void __declspec(naked) naked_ep4_main_bottom_12_bless_glow()
    { __asm fld dword ptr [esp+0x2C] __asm fstp dword ptr [esp] __asm sub eax,3 __asm jmp u0x49626C }

    // -- Loadbar --
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

    // -- Select screen --
    void __declspec(naked) naked_0x475C83()
    {
        __asm
        {
            add eax, 0xFFFFFC00
            sar eax, 0x02
            jmp u0x475C88
        }
    }
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

    // -- Stats window colors --
#define DEFINE_STATS_COLOR(NAME, RETURN_ADDR, BLUE, GREEN, RED) \
    unsigned NAME##Return = RETURN_ADDR; \
    void __declspec(naked) NAME() \
    { \
        __asm push BLUE \
        __asm fild dword ptr ds:[0x22B1954] \
        __asm push GREEN \
        __asm push RED \
        __asm jmp NAME##Return \
    }

    DEFINE_STATS_COLOR(naked_stats_str, 0x52A1AA, 0x00, 0x00, 0xFF)
    DEFINE_STATS_COLOR(naked_stats_rec, 0x52A56A, 0xCE, 0x00, 0xFF)
    DEFINE_STATS_COLOR(naked_stats_int, 0x52A92A, 0xFF, 0x80, 0x80)
    DEFINE_STATS_COLOR(naked_stats_wis, 0x52ACEA, 0x00, 0xFF, 0x00)
    DEFINE_STATS_COLOR(naked_stats_dex, 0x52B0AA, 0x00, 0x80, 0xFF)
    DEFINE_STATS_COLOR(naked_stats_luc, 0x52B46A, 0xFF, 0xFF, 0x00)

#undef DEFINE_STATS_COLOR
}

void interface_patches::install_common()
{
    // Clock text format (applies to all UI modes)
    util::detour((void*)0x4E1255, naked_ep4_map_clock, 5);
    // Clock X offset — EP6/EP7 need it further left to fit the wider date format
    uint8_t clockXOffset = config::ui_needs_layout_patches() ? 0x0E : 0x05;
    util::write_memory((void*)0x4E129B, clockXOffset, 1);
}

void interface_patches::install()
{
    // Wings shadow workaround
    util::detour((void*)0x41F9C0, naked_0x41F9C0, 9);
    // Evolution bug fix
    util::detour((void*)0x41E2BB, naked_0x41E2BB, 7);
    // PvP rank icon alignment
    util::detour((void*)0x0044EC5C, naked_pvp_rank_icon_x, 8);
    util::detour((void*)0x0044ECBC, naked_pvp_rank_icon_y, 7);
    // Clock position (coordinates only — format and X offset are in install_common)
    util::detour((void*)0x4DDEDA, naked_ep4_main_map_servertime, 6);
    // EP4 HUD layout
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
    util::detour((void*)0x4D9A1C, naked_ep4_arrow_size_map, 6);
    util::detour((void*)0x4E055A, naked_ep4_arrow_size_minimap, 6);
    util::detour((void*)0x4D7A47, naked_ep4_load_arrow_map, 5);
    util::detour((void*)0x4DE4FB, naked_ep4_load_arrow_minimap, 5);
    // Bottom bar layouts
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
    // Loadbar
    util::detour((void*)0x4CFE5A, naked_ep4_loadbar, 6);
    util::detour((void*)0x493362, (void*)0x493442, 7);
    // Stats window colors
    util::detour((void*)0x52A195, naked_stats_str, 5);
    util::detour((void*)0x52A555, naked_stats_rec, 5);
    util::detour((void*)0x52A915, naked_stats_int, 5);
    util::detour((void*)0x52ACD5, naked_stats_wis, 5);
    util::detour((void*)0x52B095, naked_stats_dex, 5);
    util::detour((void*)0x52B455, naked_stats_luc, 5);
}

void interface_patches::install_select_screen()
{
    util::detour((void*)0x475C83, naked_0x475C83, 5);
    util::detour((void*)0x475C90, naked_0x475C90, 8);
}
