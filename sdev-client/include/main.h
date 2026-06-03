#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstddef>
#include <cstdint>

namespace shaiya
{
    struct Unknown;
}

void Main();
extern "C" __declspec(dllexport) void DllExport();
void init_discord_rpc();
inline constexpr unsigned kClientRouletteListWindowMessage = 0x8000 + 0x4A4;
inline constexpr unsigned kClientRouletteRollWindowMessage = 0x8000 + 0x4A3;
inline constexpr unsigned kClientEmojiTokenWindowMessage = 0x8000 + 0x4A5;
inline constexpr unsigned kClientTeleportListWindowMessage = 0x8000 + 0x4A6;
inline constexpr unsigned kClientTeleportMoveWindowMessage = 0x8000 + 0x4A7;
void tick_client_welcome_sysmsg();
void ensure_client_sysmsg_dispatch_ready();
bool is_client_sysmsg_dispatch_ready();
bool handle_imgui_layer_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

namespace hook
{
    void command();
    void imgui_layer();
    void item_icon();
    void name_color();
    void packet();
    void patch();
    void quick_slot();
    void raid();
    void resolutions();
    void select_screen();
    void title();
    void vehicle();
    void window();
}

inline int g_showCostumes = false;
inline int g_showPets = false;
inline int g_showWings = false;
inline int g_showEffects = false;
inline int g_showMobEffects = false;
inline int g_fpsBoost = false;
inline int g_showTitles = true;
inline int g_showNameColors = true;
inline float g_cameraLimit = 30.0f;
inline shaiya::Unknown* g_uiRoot1 = nullptr;
inline shaiya::Unknown* g_uiRoot2 = nullptr;
