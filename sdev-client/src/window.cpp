#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <util/util.h>
#include <array>
#include <algorithm>
#include <shaiya/include/network/game/incoming/0800.h>
#include <shaiya/include/network/game/outgoing/0800.h>
#include "include/main.h"
#include "include/config.h"
#include "include/shaiya/CNetwork.h"
#include "include/shaiya/CQuickSlot.h"
#include "include/shaiya/Static.h"
#include "include/shaiya/Unknown.h"
using namespace shaiya;

namespace window
{
    inline HWND g_hookedGameHwnd = nullptr;
    inline WNDPROC g_originalGameWndProc = nullptr;
    inline DWORD g_nextTitleRefreshTick = 0;

    // -- Window title (custom title + "Playing as CharName") --

    using SetWindowTextAProc = BOOL(WINAPI*)(HWND, LPCSTR);
    inline SetWindowTextAProc g_originalSetWindowTextA = nullptr;

    void refresh_game_window_title(HWND hwnd, bool force)
    {
        if (!hwnd)
            return;

        auto now = GetTickCount();
        if (!force && now < g_nextTitleRefreshTick)
            return;

        g_nextTitleRefreshTick = now + 1000;
        char title[256]{};
        config::build_window_title(title, sizeof(title));
        DefWindowProcA(hwnd, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(title));
    }

    BOOL WINAPI hooked_set_window_text_a(HWND hwnd, LPCSTR text)
    {
        if (g_hookedGameHwnd && hwnd == g_hookedGameHwnd)
        {
            char title[256]{};
            config::build_window_title(title, sizeof(title));
            return g_originalSetWindowTextA
                ? g_originalSetWindowTextA(hwnd, title)
                : SetWindowTextA(hwnd, title);
        }
        return g_originalSetWindowTextA
            ? g_originalSetWindowTextA(hwnd, text)
            : SetWindowTextA(hwnd, text);
    }

    void install_window_title_hook()
    {
        auto importSlot = reinterpret_cast<SetWindowTextAProc*>(0x746494);
        if (!g_originalSetWindowTextA)
            g_originalSetWindowTextA = *importSlot;
        auto hook = &hooked_set_window_text_a;
        util::write_memory(importSlot, &hook, sizeof(hook));
    }

    void append_textbox_text(void* textBox, const char* text)
    {
        if (!textBox || !text)
            return;

        auto count = static_cast<uint32_t>(std::strlen(text));
        if (!count)
            return;

        auto base = reinterpret_cast<uint8_t*>(textBox);
        auto maxLen = *reinterpret_cast<uint32_t*>(base + 0x30);
        auto size = *reinterpret_cast<uint32_t*>(base + 0xC0);
        auto cap = *reinterpret_cast<uint32_t*>(base + 0xC4);
        if (size >= 2048 || cap >= 0x100000)
            return;

        auto textObj = base + 0xAC;
        auto cur = cap >= 0x10
            ? *reinterpret_cast<const char**>(textObj + 0x04)
            : reinterpret_cast<const char*>(textObj + 0x04);
        if (!cur)
            return;

        auto next = size + count;
        if (next >= 2048 || (maxLen && next > maxLen))
            return;

        char merged[2048]{};
        if (size)
            std::memcpy(merged, cur, size);
        std::memcpy(merged + size, text, count);

        using StringAssign = void*(__thiscall*)(void*, const char*, uint32_t);
        auto assignString = reinterpret_cast<StringAssign>(0x405670);
        assignString(textObj, merged, next);
    }

    void assign_windows1(Unknown* unknown)
    {
        g_uiRoot1 = unknown;
        auto quickSlot3 = g_pQuickSlot3;
        unknown->windows1.quickSlot3 = quickSlot3;
    }

    void assign_windows2(Unknown* unknown)
    {
        g_uiRoot2 = unknown;
        auto quickSlot3 = g_pQuickSlot3;
        unknown->windows2.quickSlot3 = quickSlot3;
    }

    LRESULT CALLBACK game_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        LRESULT imguiResult = 0;
        if (handle_imgui_layer_wnd_proc(hwnd, msg, wParam, lParam, imguiResult))
            return imguiResult;

        // Refresh the custom window title on low-frequency messages where the
        // stock client may have overwritten it with a truncated default.
        if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP || msg == WM_SHOWWINDOW
            || msg == WM_DISPLAYCHANGE || msg == WM_SIZE)
            refresh_game_window_title(hwnd, false);

        if (msg == kClientRouletteListWindowMessage)
        {
            GameRouletteListIncoming outgoing{};
            CNetwork::Send(&outgoing, sizeof(outgoing));
            return 0;
        }

        if (msg == kClientRouletteRollWindowMessage)
        {
            GameRouletteSpinIncoming outgoing{};
            CNetwork::Send(&outgoing, sizeof(outgoing));
            return 0;
        }

        if (msg == kClientEmojiTokenWindowMessage)
        {
            auto token = reinterpret_cast<const char*>(lParam);
            append_textbox_text(&g_var->input.textBox, token);
            return 0;
        }

        if (msg == kClientTeleportListWindowMessage)
        {
            GameTeleportListIncoming outgoing{};
            CNetwork::Send(&outgoing, sizeof(outgoing));
            return 0;
        }

        if (msg == kClientTeleportMoveWindowMessage)
        {
            GameTeleportMoveIncoming outgoing{};
            outgoing.index = static_cast<uint8_t>(wParam);
            CNetwork::Send(&outgoing, sizeof(outgoing));
            return 0;
        }

        return CallWindowProcA(g_originalGameWndProc, hwnd, msg, wParam, lParam);
    }

    void ensure_client_sysmsg_dispatch_ready()
    {
        auto hwnd = g_var->hwnd;
        if (!hwnd || !IsWindow(hwnd))
            return;

        if (g_hookedGameHwnd == hwnd && g_originalGameWndProc)
            return;

        auto previousProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(game_wnd_proc)));
        if (!previousProc)
            return;

        g_originalGameWndProc = previousProc;
        g_hookedGameHwnd = hwnd;

        install_window_title_hook();
        refresh_game_window_title(hwnd, true);
    }

    bool is_client_sysmsg_dispatch_ready()
    {
        auto hwnd = g_var->hwnd;
        return hwnd
            && IsWindow(hwnd)
            && g_hookedGameHwnd == hwnd
            && g_originalGameWndProc != nullptr;
    }
}

unsigned u0x42B826 = 0x42B826;
void __declspec(naked) naked_0x42B820() 
{
    __asm
    {
        // original
        mov [esi+0x2A0],ecx

        pushad

        push esi
        call window::assign_windows1
        add esp,0x4

        popad

        jmp u0x42B826
    }
}

unsigned u0x42B9D7 = 0x42B9D7;
void __declspec(naked) naked_0x42B9D1() 
{
    __asm
    {
        // original
        mov [esi+0x370],ecx

        pushad

        push esi
        call window::assign_windows2
        add esp,0x4

        popad

        jmp u0x42B9D7
    }
}

void hook::window()
{
    // assign windows
    util::detour((void*)0x42B820, naked_0x42B820, 6);
    util::detour((void*)0x42B9D1, naked_0x42B9D1, 6);
}

void ensure_client_sysmsg_dispatch_ready()
{
    window::ensure_client_sysmsg_dispatch_ready();
}

bool is_client_sysmsg_dispatch_ready()
{
    return window::is_client_sysmsg_dispatch_ready();
}
