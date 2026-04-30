#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d9.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cmath>
#include <string>
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "dwmapi.lib")
#include <external/imgui/imgui.h>
#include <external/imgui/backends/imgui_impl_dx9.h>
#include <external/imgui/backends/imgui_impl_win32.h>
#include "include/main.h"
#include "include/shaiya/CCharacter.h"
#include "include/shaiya/CPlayerData.h"
#include "include/shaiya/Roulette.h"
#include "include/shaiya/CWorldMgr.h"
#include "include/shaiya/Static.h"
using namespace shaiya;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

/*
Mini wiki for future client features
====================================

Core runtime anchors
- g_var       -> 0x7AB000  (.data root, see Static.h)
- g_pWorldMgr -> 0x7C4A68  (world state, local user pointer, effect helpers)
- g_pPlayerData -> 0x90D1D0 (character stats, server time, map/window state)

Useful places to build from
- CWorldMgr::RenderEffect(...) can spawn client-side visuals without server packets.
- g_pPlayerData->serverTime stores the in-game clock time received from the server.
- g_pPlayerData->windowType / npcType / npcTypeId are useful when extending UI or NPC flows.
- g_pWorldMgr->user gives access to the local CCharacter, including position and orientation.

UI notes
- The client already exposes many stable static pointers in Static.h and CPlayerData.h.
- If a future feature needs custom panels, prefer patching UI only when required.
- If an instruction must be patched, always patch the immediate operand bytes, not the opcode.

Overlay policy
- The overlay can run passively for always-on, click-through client HUD features.
- F8 still toggles the interactive placeholder window with one effect button.
- The goal is to keep the ImGui layer minimal and low-risk until new features are ready.
- F7 toggles a realtime client feature bundle used for live testing.

Chat type notes
- Upper bar: 15 orange, 16 red, 17 red, 18 yellow, 19 high-tone green, 20 violet, 21 light blue, 22 light green, 34 light grey.
- Lower bar (chat window): 0 white, 35 light green, 36 light red, 37 light violet, 38 normal brownish, 39 red, 40 yellow, 41 white, 42 red, 43 greyish-white, 44 same as 43, 45 darker red, 46 white, 47 violet, 49 light blue.
- On-screen notice-like messages: 23 to 33.
- Special case: 48 and 50 behave like an alternate on-screen raid-style light violet message.
*/

namespace imgui_layer
{
    constexpr const char* kImguiIniSection = "IMGUI";
    constexpr const char* kPlaceholderPosXKey = "PLACEHOLDER_X";
    constexpr const char* kPlaceholderPosYKey = "PLACEHOLDER_Y";
    constexpr const char* kPlaceholderWidthKey = "PLACEHOLDER_W";
    constexpr const char* kPlaceholderHeightKey = "PLACEHOLDER_H";

    inline std::atomic_bool g_running = false;
    inline bool g_f7Down = false;
    inline bool g_f8Down = false;
    inline bool g_closeRequested = false;
    inline bool g_showPlaceholder = false;
    inline bool g_sentWelcomeMessage = false;
    inline bool g_waitingWelcomeMessage = false;
    inline bool g_restorePlaceholderLayout = true;
    inline bool g_f7BundleUsed = false;
    inline bool g_imguiSettingsLoaded = false;
    inline DWORD g_f7MessageTick = 0;
    inline DWORD g_welcomeStartTick = 0;
    inline HWND g_overlayHwnd = nullptr;
    inline LPDIRECT3D9 g_d3d9 = nullptr;
    inline LPDIRECT3DDEVICE9 g_device = nullptr;
    inline D3DPRESENT_PARAMETERS g_presentParameters{};
    inline RECT g_placeholderWindowRect{};
    inline ImVec2 g_placeholderPosition = ImVec2(80.0f, 80.0f);
    inline ImVec2 g_placeholderSize = ImVec2(460.0f, 380.0f);
    inline DWORD g_lastRouletteRollTick = 0;
    inline DWORD g_lastRouletteListTick = 0;

    float read_imgui_float(const char* key, float fallback)
    {
        std::string buffer(64, '\0');
        GetPrivateProfileStringA(kImguiIniSection, key, "", buffer.data(), static_cast<DWORD>(buffer.size()), g_var->iniFileName.data());
        if (buffer[0] == '\0')
            return fallback;

        return static_cast<float>(std::atof(buffer.c_str()));
    }

    void write_imgui_float(const char* key, float value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        WritePrivateProfileStringA(kImguiIniSection, key, buffer, g_var->iniFileName.data());
    }

    void load_imgui_settings()
    {
        if (g_imguiSettingsLoaded)
            return;

        g_placeholderPosition.x = read_imgui_float(kPlaceholderPosXKey, g_placeholderPosition.x);
        g_placeholderPosition.y = read_imgui_float(kPlaceholderPosYKey, g_placeholderPosition.y);
        g_placeholderSize.x = read_imgui_float(kPlaceholderWidthKey, g_placeholderSize.x);
        g_placeholderSize.y = read_imgui_float(kPlaceholderHeightKey, g_placeholderSize.y);
        g_imguiSettingsLoaded = true;
    }

    void save_imgui_settings()
    {
        write_imgui_float(kPlaceholderPosXKey, g_placeholderPosition.x);
        write_imgui_float(kPlaceholderPosYKey, g_placeholderPosition.y);
        write_imgui_float(kPlaceholderWidthKey, g_placeholderSize.x);
        write_imgui_float(kPlaceholderHeightKey, g_placeholderSize.y);
    }

    bool consume_toggle(int virtualKey, bool& wasDown)
    {
        auto isDown = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
        auto toggled = isDown && !wasDown;
        wasDown = isDown;
        return toggled;
    }

    bool is_game_scene()
    {
        return g_pWorldMgr->user != nullptr
            && g_pPlayerData->charId != 0
            && g_pPlayerData->mapId != 0;
    }

    int count_inventory_item(uint8_t type, uint8_t typeId)
    {
        auto count = 0;
        auto maxBag = g_pPlayerData->inventory.size() - 1;

        for (size_t bag = 1; bag <= maxBag; ++bag)
        {
            for (const auto& item : g_pPlayerData->inventory[bag])
            {
                if (item.type == type && item.typeId == typeId)
                    count += item.count;
            }
        }

        return count;
    }

    bool is_inventory_full()
    {
        auto maxBag = g_pPlayerData->inventory.size() - 1;

        for (size_t bag = 1; bag <= maxBag; ++bag)
        {
            for (const auto& item : g_pPlayerData->inventory[bag])
            {
                if (!item.type)
                    return false;
            }
        }

        return true;
    }

    void request_roulette_list()
    {
        ensure_client_sysmsg_dispatch_ready();
        if (g_var->hwnd && IsWindow(g_var->hwnd))
            PostMessageA(g_var->hwnd, kClientRouletteListWindowMessage, 0, 0);

        g_lastRouletteListTick = GetTickCount();
    }

    void request_roulette_spin()
    {
        ensure_client_sysmsg_dispatch_ready();
        if (g_var->hwnd && IsWindow(g_var->hwnd))
            PostMessageA(g_var->hwnd, kClientRouletteRollWindowMessage, 0, 0);

        constexpr uint8_t kWheelSegmentCount = 8;
        roulette_event::spinActive = true;
        roulette_event::spinStartTick = GetTickCount();
        roulette_event::spinDurationMs = 4500;
        roulette_event::rewardIndex = static_cast<uint8_t>((roulette_event::spinStartTick / 97) % kWheelSegmentCount);
        roulette_event::lastResult = 0;
        ++roulette_event::spinSerial;
        g_lastRouletteRollTick = GetTickCount();
    }

    void draw_roulette_wheel()
    {
        auto drawList = ImGui::GetWindowDrawList();
        auto origin = ImGui::GetCursorScreenPos();
        auto width = ImGui::GetContentRegionAvail().x;
        auto height = 132.0f;
        auto centerX = origin.x + width * 0.5f;
        auto centerY = origin.y + height * 0.5f;
        auto now = GetTickCount();
        constexpr int kWheelSegmentCount = 8;
        constexpr float kPi = 3.1415926535f;
        constexpr float kTwoPi = kPi * 2.0f;
        const ImU32 colors[kWheelSegmentCount] = {
            IM_COL32(244, 83, 92, 255),
            IM_COL32(246, 130, 76, 255),
            IM_COL32(247, 191, 82, 255),
            IM_COL32(91, 199, 118, 255),
            IM_COL32(76, 176, 221, 255),
            IM_COL32(102, 131, 229, 255),
            IM_COL32(178, 91, 218, 255),
            IM_COL32(232, 103, 171, 255),
        };
        auto angle = -kPi * 0.5f;

        if (roulette_event::spinActive && roulette_event::spinDurationMs > 0)
        {
            auto t = static_cast<float>(now - roulette_event::spinStartTick) / static_cast<float>(roulette_event::spinDurationMs);
            t = std::clamp(t, 0.0f, 1.0f);
            auto eased = 1.0f - std::pow(1.0f - t, 3.0f);
            auto target = static_cast<float>(roulette_event::rewardIndex) * (kTwoPi / static_cast<float>(kWheelSegmentCount));
            angle += eased * (kTwoPi * 5.0f + target);
        }

        ImGui::InvisibleButton("##roulette_wheel", ImVec2(width, height));
        drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(16, 17, 20, 210), 6.0f);

        auto radius = std::min(width, height) * 0.42f;
        auto center = ImVec2(centerX - 8.0f, centerY);
        for (int i = 0; i < kWheelSegmentCount; ++i)
        {
            auto start = angle + static_cast<float>(i) * (kTwoPi / static_cast<float>(kWheelSegmentCount));
            auto end = angle + static_cast<float>(i + 1) * (kTwoPi / static_cast<float>(kWheelSegmentCount));
            drawList->PathClear();
            drawList->PathLineTo(center);
            for (int step = 0; step <= 8; ++step)
            {
                auto a = start + (end - start) * (static_cast<float>(step) / 8.0f);
                drawList->PathLineTo(ImVec2(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius));
            }
            drawList->PathFillConvex(colors[i]);
        }

        drawList->AddCircle(center, radius, IM_COL32(255, 255, 255, 170), 64, 2.0f);
        drawList->AddCircleFilled(center, radius * 0.16f, IM_COL32(20, 22, 26, 245), 32);
        drawList->AddTriangleFilled(
            ImVec2(center.x + radius + 13.0f, center.y),
            ImVec2(center.x + radius - 7.0f, center.y - 11.0f),
            ImVec2(center.x + radius - 7.0f, center.y + 11.0f),
            IM_COL32(255, 255, 255, 245));
    }

    void draw_roulette_section()
    {
        auto now = GetTickCount();
        if (is_game_scene() && (!roulette_event::hasList || now - g_lastRouletteListTick > 15000))
            request_roulette_list();

        if (roulette_event::spinActive && roulette_event::spinDurationMs && now - roulette_event::spinStartTick > roulette_event::spinDurationMs + 500)
            roulette_event::spinActive = false;

        auto tokenType = roulette_event::tokenType ? roulette_event::tokenType : 100;
        auto tokenTypeId = roulette_event::tokenTypeId ? roulette_event::tokenTypeId : 200;
        auto requiredTokenCount = roulette_event::tokenCount ? roulette_event::tokenCount : 1;
        auto tokenCount = count_inventory_item(tokenType, tokenTypeId);
        auto canRoll = is_game_scene() && tokenCount >= requiredTokenCount && !roulette_event::spinActive;

        ImGui::SeparatorText("Roulette");
        ImGui::Text("Token %u/%u: %d", tokenType, tokenTypeId, tokenCount);
        draw_roulette_wheel();
        ImGui::BeginDisabled(!canRoll);
        if (ImGui::Button("Roll", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
            request_roulette_spin();
        ImGui::EndDisabled();

        if (!is_game_scene())
            ImGui::TextDisabled("Enter the game to roll.");
        else if (roulette_event::spinActive)
            ImGui::TextDisabled("Spinning...");
        else if (tokenCount < requiredTokenCount)
            ImGui::TextDisabled("Missing token.");
        else if (g_lastRouletteRollTick && now - g_lastRouletteRollTick < 8000)
            ImGui::TextDisabled("Reward delivered.");
    }

    bool should_run_overlay_session()
    {
        return g_showPlaceholder;
    }

    void toggle_realtime_feature_bundle()
    {
        if (!g_f7BundleUsed)
        {
            set_performance_mode(true);
            g_f7BundleUsed = true;
        }
        else
        {
            set_performance_mode(!is_performance_mode_enabled());
        }

        auto now = GetTickCount();
        if (g_f7MessageTick == 0 || now - g_f7MessageTick >= 10000)
        {
            queue_client_sysmsg(25, 12000);
            g_f7MessageTick = now;
        }
    }

    void send_welcome_sysmsg_once()
    {
        if (g_sentWelcomeMessage || !g_var->hwnd || !IsWindow(g_var->hwnd))
            return;

        if (!is_game_scene())
        {
            g_waitingWelcomeMessage = false;
            g_welcomeStartTick = 0;
            return;
        }

        auto now = GetTickCount();
        if (!g_waitingWelcomeMessage)
        {
            g_waitingWelcomeMessage = true;
            g_welcomeStartTick = now;
            return;
        }

        // Give the game scene time to settle before posting the welcome notice.
        if (now - g_welcomeStartTick < 4000)
            return;

        queue_client_sysmsg(25, 12001);
        g_sentWelcomeMessage = true;
        g_waitingWelcomeMessage = false;
    }

    bool is_point_in_rect(LPARAM lParam, const RECT& rect)
    {
        POINT point{
            static_cast<LONG>(static_cast<short>(LOWORD(lParam))),
            static_cast<LONG>(static_cast<short>(HIWORD(lParam)))
        };
        return PtInRect(&rect, point) == TRUE;
    }

    bool is_point_in_interactive_area(LPARAM lParam)
    {
        return is_point_in_rect(lParam, g_placeholderWindowRect);
    }

    void cleanup_device()
    {
        if (g_device)
        {
            g_device->Release();
            g_device = nullptr;
        }

        if (g_d3d9)
        {
            g_d3d9->Release();
            g_d3d9 = nullptr;
        }
    }

    bool create_device(HWND hwnd)
    {
        g_d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
        if (!g_d3d9)
            return false;

        RECT rect{};
        GetClientRect(hwnd, &rect);

        ZeroMemory(&g_presentParameters, sizeof(g_presentParameters));
        g_presentParameters.Windowed = TRUE;
        g_presentParameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        g_presentParameters.BackBufferFormat = D3DFMT_A8R8G8B8;
        g_presentParameters.EnableAutoDepthStencil = FALSE;
        g_presentParameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        g_presentParameters.BackBufferWidth = rect.right - rect.left;
        g_presentParameters.BackBufferHeight = rect.bottom - rect.top;
        g_presentParameters.hDeviceWindow = hwnd;

        auto result = g_d3d9->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hwnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
            &g_presentParameters,
            &g_device);

        if (FAILED(result))
        {
            result = g_d3d9->CreateDevice(
                D3DADAPTER_DEFAULT,
                D3DDEVTYPE_HAL,
                hwnd,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED,
                &g_presentParameters,
                &g_device);
        }

        if (FAILED(result))
        {
            cleanup_device();
            return false;
        }

        return true;
    }

    void reset_device(int width, int height)
    {
        if (!g_device || width <= 0 || height <= 0)
            return;

        ImGui_ImplDX9_InvalidateDeviceObjects();
        g_presentParameters.BackBufferWidth = width;
        g_presentParameters.BackBufferHeight = height;
        g_device->Reset(&g_presentParameters);
        ImGui_ImplDX9_CreateDeviceObjects();
    }

    void sync_overlay_to_game()
    {
        if (!g_overlayHwnd || !g_var->hwnd)
            return;

        RECT rect{};
        if (!GetWindowRect(g_var->hwnd, &rect))
            return;

        auto width = rect.right - rect.left;
        auto height = rect.bottom - rect.top;
        if (width <= 0 || height <= 0)
            return;

        SetWindowPos(
            g_overlayHwnd,
            HWND_TOPMOST,
            rect.left,
            rect.top,
            width,
            height,
            SWP_NOACTIVATE);

        if (g_presentParameters.BackBufferWidth != static_cast<UINT>(width)
            || g_presentParameters.BackBufferHeight != static_cast<UINT>(height))
        {
            reset_device(width, height);
        }
    }

    void sync_overlay_input_passthrough()
    {
        if (!g_overlayHwnd)
            return;

        auto exStyle = GetWindowLongPtrA(g_overlayHwnd, GWL_EXSTYLE);
        auto wantsClickThrough = !g_showPlaceholder;
        auto hasClickThrough = (exStyle & WS_EX_TRANSPARENT) != 0;

        if (wantsClickThrough == hasClickThrough)
            return;

        if (wantsClickThrough)
            exStyle |= WS_EX_TRANSPARENT;
        else
            exStyle &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);

        SetWindowLongPtrA(g_overlayHwnd, GWL_EXSTYLE, exStyle);
    }

    LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            if (is_point_in_interactive_area(lParam))
                return HTCLIENT;
            return HTTRANSPARENT;
        default:
            break;
        }

        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
            return TRUE;

        switch (msg)
        {
        case WM_SIZE:
            if (g_device && wParam != SIZE_MINIMIZED)
                reset_device(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }

    bool begin_session()
    {
        WNDCLASSEXA wndClass{};
        wndClass.cbSize = sizeof(wndClass);
        wndClass.lpfnWndProc = wnd_proc;
        wndClass.hInstance = GetModuleHandleA(nullptr);
        wndClass.lpszClassName = "ShaiyaImguiOverlay";
        RegisterClassExA(&wndClass);

        g_overlayHwnd = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            wndClass.lpszClassName,
            "Shaiya ImGui Overlay",
            WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            720,
            nullptr,
            nullptr,
            wndClass.hInstance,
            nullptr);

        if (!g_overlayHwnd || !create_device(g_overlayHwnd))
            return false;

        load_imgui_settings();

        SetLayeredWindowAttributes(g_overlayHwnd, RGB(0, 0, 0), 255, LWA_ALPHA);
        ImGui_ImplWin32_EnableAlphaCompositing(g_overlayHwnd);
        const MARGINS margins{ -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(g_overlayHwnd, &margins);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

        ImGui_ImplWin32_Init(g_overlayHwnd);
        ImGui_ImplDX9_Init(g_device);

        ShowWindow(g_overlayHwnd, SW_SHOWNA);
        UpdateWindow(g_overlayHwnd);
        g_closeRequested = false;
        g_restorePlaceholderLayout = true;
        g_placeholderWindowRect = {};
        return true;
    }

    void end_session()
    {
        save_imgui_settings();
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        cleanup_device();

        if (g_overlayHwnd)
        {
            auto instance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrA(g_overlayHwnd, GWLP_HINSTANCE));
            DestroyWindow(g_overlayHwnd);
            g_overlayHwnd = nullptr;
            UnregisterClassA("ShaiyaImguiOverlay", instance);
        }
    }

    void run_session()
    {
        MSG msg{};
        while (g_running && !g_closeRequested)
        {
            while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }

            if (consume_toggle(VK_F8, g_f8Down))
                g_showPlaceholder = !g_showPlaceholder;

            if (!should_run_overlay_session())
                g_closeRequested = true;

            if (!g_var->hwnd || !IsWindow(g_var->hwnd))
                g_closeRequested = true;

            sync_overlay_to_game();
            sync_overlay_input_passthrough();

            if (g_device->TestCooperativeLevel() == D3DERR_DEVICELOST)
            {
                Sleep(10);
                continue;
            }

            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            g_placeholderWindowRect = {};

            if (g_showPlaceholder)
            {
                ImGui::SetNextWindowBgAlpha(0.94f);
                if (g_restorePlaceholderLayout)
                {
                    ImGui::SetNextWindowPos(g_placeholderPosition, ImGuiCond_Always);
                    ImGui::SetNextWindowSize(g_placeholderSize, ImGuiCond_Always);
                    g_restorePlaceholderLayout = false;
                }

                bool placeholderOpen = g_showPlaceholder;
                if (ImGui::Begin(
                    "User Panel",
                    &placeholderOpen,
                    ImGuiWindowFlags_NoCollapse
                        | ImGuiWindowFlags_NoFocusOnAppearing
                        | ImGuiWindowFlags_NoBringToFrontOnFocus))
                {
                    auto windowPos = ImGui::GetWindowPos();
                    auto windowSize = ImGui::GetWindowSize();
                    g_placeholderPosition = windowPos;
                    g_placeholderSize = windowSize;
                    g_placeholderWindowRect.left = static_cast<LONG>(windowPos.x);
                    g_placeholderWindowRect.top = static_cast<LONG>(windowPos.y);
                    g_placeholderWindowRect.right = static_cast<LONG>(windowPos.x + windowSize.x);
                    g_placeholderWindowRect.bottom = static_cast<LONG>(windowPos.y + windowSize.y);

                    draw_roulette_section();
                }
                ImGui::End();
                g_showPlaceholder = placeholderOpen;
            }

            ImGui::EndFrame();
            ImGui::Render();

            g_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
            if (SUCCEEDED(g_device->BeginScene()))
            {
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
                g_device->EndScene();
            }

            g_device->Present(nullptr, nullptr, nullptr, nullptr);
            Sleep(1);
        }
    }

    DWORD WINAPI render_thread(LPVOID)
    {
        while (g_running)
        {
            send_welcome_sysmsg_once();

            if (!g_var->hwnd || !IsWindow(g_var->hwnd))
            {
                Sleep(250);
                continue;
            }

            ensure_client_sysmsg_dispatch_ready();

            if (consume_toggle(VK_F7, g_f7Down))
                toggle_realtime_feature_bundle();

            auto togglePlaceholder = consume_toggle(VK_F8, g_f8Down);
            if (togglePlaceholder)
                g_showPlaceholder = !g_showPlaceholder;

            if (!should_run_overlay_session())
            {
                Sleep(25);
                continue;
            }

            if (begin_session())
            {
                run_session();
                end_session();
            }
            else
            {
                end_session();
                Sleep(250);
            }
        }

        return 0;
    }
}

void queue_client_sysmsg(int chatType, int messageNumber)
{
    ensure_client_sysmsg_dispatch_ready();
    if (!g_var->hwnd || !IsWindow(g_var->hwnd))
        return;

    PostMessageA(
        g_var->hwnd,
        kClientSysMsgWindowMessage,
        static_cast<WPARAM>(chatType),
        static_cast<LPARAM>(messageNumber));
}

void flush_client_sysmsg_queue()
{
}

void hook::imgui_layer()
{
    if (imgui_layer::g_running.exchange(true))
        return;

    auto thread = CreateThread(nullptr, 0, imgui_layer::render_thread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
}
