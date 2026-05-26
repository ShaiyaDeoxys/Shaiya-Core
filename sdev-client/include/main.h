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
inline constexpr unsigned kClientRouletteListWindowMessage = 0x8000 + 0x4A4;
inline constexpr unsigned kClientRouletteRollWindowMessage = 0x8000 + 0x4A3;
inline constexpr unsigned kClientEmojiTokenWindowMessage = 0x8000 + 0x4A5;
inline constexpr unsigned kClientTeleportListWindowMessage = 0x8000 + 0x4A6;
inline constexpr unsigned kClientTeleportMoveWindowMessage = 0x8000 + 0x4A7;
void tick_client_welcome_sysmsg();
void ensure_client_sysmsg_dispatch_ready();
bool is_client_sysmsg_dispatch_ready();
void open_remote_npc_shortcut(int index);
void sync_textbox_utf8_display(void* textBox);
bool append_utf8_textbox_wchar(void* textBox, wchar_t wideChar);
bool append_utf8_textbox_text(void* textBox, const char* utf8);
bool backspace_utf8_textbox_char(void* textBox);
bool handle_imgui_layer_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);

namespace hook
{
    void blacksmith();
    void camera_limit();
    void character();
    void command();
    void custom_game();
    void discord();
    void equipment();
    void client_features();
    void exp_view();
    void imgui_layer();
    void input();
    void item_icon();
    void main_stats();
    void name_color();
    void npc_button();
    void packet();
    void patch();
    void quick_slot();
    void raid();
    void resolutions();
    void select_screen();
    void target_view();
    void title();
    void ui_image();
    void unicode();
    void vehicle();
    void weapon_step();
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
