#include "include/imgui_layer_internal.h"
#include "include/debug_panel.h"
#include "include/custom_chat.h"
#include "include/shaiya/RewardItemEvent.h"
#include "include/shaiya/Roulette.h"

void naked_chat_add_token_filter();
void naked_chat_balloon_text_create();
void naked_capture_chat_balloon_text();
void naked_floating_text_create();
void naked_capture_floating_static_text();
void naked_static_text_create();
void naked_floating_static_text_draw();
void naked_native_text_draw_probe();

namespace imgui_layer
{
    void reset_overlay_rects()
    {
        g_panelDragRect = {};
        g_panelWindowRect = {};
        g_emojiButtonRect = {};
        g_emojiPickerRect = {};
        g_rewardButtonRect = {};
        g_rewardBarRect = {};
        g_rouletteButtonRect = {};
        g_settingsButtonRect = {};
        g_settingsPanelRect = {};
        g_npcButtonRect = {};
        g_npcPanelRect = {};
        g_teleportButtonRect = {};
        g_teleportPanelRect = {};
    }

    HRESULT __stdcall hooked_mouse_get_device_state(IDirectInputDevice8A* device, DWORD cbData, LPVOID lpvData)
    {
        auto* original = g_originalMouseGetDeviceState;
        // Determine which mouse this call is for.
        if (g_var && g_var->input.mouse2 &&
            g_var->input.mouse2->device == device &&
            g_originalMouse2GetDeviceState)
        {
            original = g_originalMouse2GetDeviceState;
        }

        auto hr = original ? original(device, cbData, lpvData) : E_FAIL;
        if (FAILED(hr) || !lpvData)
            return hr;

        // If ImGui wants the mouse, zero the left button in the polled state
        // so the game never sees the click for movement / targeting.
        if (g_imguiInitialized && ImGui::GetCurrentContext())
        {
            auto& io = ImGui::GetIO();
            if (io.WantCaptureMouse && cbData >= sizeof(DIMOUSESTATE2))
            {
                auto* ms = static_cast<DIMOUSESTATE2*>(lpvData);
                ms->rgbButtons[0] = 0;   // left button
                ms->rgbButtons[1] = 0;   // right button
            }
        }

        return hr;
    }

    void install_dinput_mouse_hook()
    {
        if (g_mouseDeviceHooked || !g_var)
            return;

        auto hookDevice = [](shaiya::Mouse* m, GetDeviceStateFn* outOriginal) {
            if (!m || !m->device)
                return;
            void* orig = nullptr;
            if (hook_vtable(m->device, 9, reinterpret_cast<void*>(hooked_mouse_get_device_state), &orig) && orig)
                *outOriginal = reinterpret_cast<GetDeviceStateFn>(orig);
        };

        hookDevice(g_var->input.mouse,  &g_originalMouseGetDeviceState);
        hookDevice(g_var->input.mouse2, &g_originalMouse2GetDeviceState);

        // If both devices share the same vtable (common), mouse2 hook
        // returns the already-hooked function.  Fall back to mouse1's original.
        if (g_originalMouse2GetDeviceState == reinterpret_cast<GetDeviceStateFn>(hooked_mouse_get_device_state))
            g_originalMouse2GetDeviceState = g_originalMouseGetDeviceState;

        g_mouseDeviceHooked = (g_originalMouseGetDeviceState != nullptr);
    }

    // -----------------------------------------------------------------------
    //  Core utilities
    // -----------------------------------------------------------------------

    bool consume_toggle(int virtualKey, bool& wasDown)
    {
        auto isDown = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
        auto toggled = isDown && !wasDown;
        wasDown = isDown;
        return toggled;
    }

    bool is_game_scene()
    {
        if (*reinterpret_cast<const int*>(kGameScreenStateAddr) != kScreenStateInGame)
            return false;

        return g_pWorldMgr->user != nullptr
            && g_pWorldMgr->mapSize > 0
            && g_pPlayerData->charId != 0;
    }

    void sync_map_transition_state()
    {
        // --- Native UI visibility (F11 toggle) ---
        // Track F11 press to toggle our overlay in sync with the game's
        // native UI hide/show.  We mirror the game's own toggle so both
        // native elements and our ImGui overlay disappear together.
        static bool f11WasDown = false;
        if (consume_toggle(VK_F11, f11WasDown))
            g_nativeUIHidden = !g_nativeUIHidden;

        // --- Frame debounce ---
        // The game screen state at 0x7C48F8 goes to 5 during loading and
        // back to 6 only when the world is fully loaded, so there is no need
        // for a timed cooldown.  We just count consecutive in-game frames to
        // avoid a single-frame flicker right on the transition edge.
        if (!is_game_scene())
        {
            g_sceneStableFrames = 0;
            return;
        }

        if (g_sceneStableFrames < kMinStableFrames + 1)
        {
            ++g_sceneStableFrames;

            // Stealth GM utility: force run mode on the first stable frame
            // after a map transition or login so invisible GMs don't have to
            // manually toggle run each time.
            if (g_sceneStableFrames == kMinStableFrames && g_pWorldMgr->user)
            {
                if (g_pWorldMgr->user->shapeType == ShapeType::Stealth)
                    g_pWorldMgr->user->run = true;
            }
        }
    }


    bool is_map_transition_active()
    {
        return g_sceneStableFrames < kMinStableFrames;
    }

    bool is_game_scene_stable()
    {
        return is_game_scene() && !is_map_transition_active();
    }

    void update_anchored_button_positions()
    {
        if (!g_mainMapObjectPtr)
            return;

        auto ptr = reinterpret_cast<const unsigned*>(g_mainMapObjectPtr);
        float x = static_cast<float>(ptr[1]) + kButtonAnchorOffsetX;
        float y = static_cast<float>(ptr[2] + ptr[4]) + kButtonAnchorOffsetY;

        g_rewardButtonPosition   = ImVec2(x, y);
        g_rouletteButtonPosition = ImVec2(x + kButtonAnchorStride, y);
        g_settingsButtonPosition = ImVec2(x + kButtonAnchorStride * 2, y);
        g_npcButtonPosition      = ImVec2(x + kButtonAnchorStride * 3, y);
        g_teleportButtonPosition = ImVec2(x + kButtonAnchorStride * 4, y);
    }

    bool is_overlay_display_usable(const ImVec2& size)
    {
        return size.x >= 320.0f && size.y >= 240.0f;
    }

    bool get_overlay_mouse_pos_raw(ImVec2& pos)
    {
        // Use ImGui's already-cached mouse position instead of syscalls.
        auto& io = ImGui::GetIO();
        if (io.MousePos.x < 0.0f || io.MousePos.y < 0.0f)
            return false;

        pos = io.MousePos;
        return true;
    }

    bool is_pos_in_rect_raw(const ImVec2& pos, const RECT& rect)
    {
        return rect.left != rect.right
            && rect.top != rect.bottom
            && pos.x >= static_cast<float>(rect.left)
            && pos.x < static_cast<float>(rect.right)
            && pos.y >= static_cast<float>(rect.top)
            && pos.y < static_cast<float>(rect.bottom);
    }

    void release_imgui_capture()
    {
        g_clearImguiActiveId = true;
    }

    bool is_cursor_in_rect(const RECT& rect)
    {
        ImVec2 pos;
        return get_overlay_mouse_pos_raw(pos) && is_pos_in_rect_raw(pos, rect);
    }

    void remember_rect(RECT& rect, const ImVec2& min, const ImVec2& max)
    {
        rect.left = static_cast<LONG>(min.x);
        rect.top = static_cast<LONG>(min.y);
        rect.right = static_cast<LONG>(max.x);
        rect.bottom = static_cast<LONG>(max.y);
    }

    ImVec2 clamp_window_position(const ImVec2& position, const ImVec2& size, const ImVec2& displaySize)
    {
        return ImVec2(
            std::clamp(position.x, 8.0f, std::max(8.0f, displaySize.x - size.x - 8.0f)),
            std::clamp(position.y, 8.0f, std::max(8.0f, displaySize.y - size.y - 8.0f)));
    }

    void draw_icon_button_fallback(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 tint, ImU32 bgColor, const char* label)
    {
        drawList->AddRectFilled(min, max, bgColor, 4.0f);
        drawList->AddRect(min, max, tint, 4.0f);

        auto textSize = ImGui::CalcTextSize(label);
        drawList->AddText(
            ImVec2(min.x + (max.x - min.x - textSize.x) * 0.5f,
                   min.y + (max.y - min.y - textSize.y) * 0.5f),
            tint,
            label);
    }

    // -----------------------------------------------------------------------
    //  WndProc helpers and handler
    // -----------------------------------------------------------------------

    // Returns true for mouse messages whose lParam carries client-area (x, y).
    // WM_MOUSEWHEEL/MOUSEHWHEEL carry *screen* coords, and leave/setcursor
    // don't carry positional data, so they are excluded.
    static bool has_client_area_coords(UINT msg)
    {
        switch (msg)
        {
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_NCMOUSEMOVE:
            return true;
        default:
            return false;
        }
    }

    static bool is_mouse_message(UINT msg)
    {
        if (has_client_area_coords(msg))
            return true;

        switch (msg)
        {
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
        case WM_NCMOUSELEAVE:
        case WM_MOUSELEAVE:
        case WM_SETCURSOR:
            return true;
        default:
            return false;
        }
    }

    // Hit-test the mouse position against every visible ImGui window from
    // the previous frame.  This covers panels, roulette, emoji picker, debug
    // overlays -- anything ImGui drew -- without maintaining a manual rect list.
    // Used as a fallback when io.WantCaptureMouse hasn't caught up yet (the
    // classic 1-frame delay between mouse movement and NewFrame hover update).
    static bool is_point_over_any_imgui_window(float x, float y)
    {
        auto* ctx = ImGui::GetCurrentContext();
        if (!ctx)
            return false;

        for (auto* window : ctx->Windows)
        {
            if (!window->WasActive || window->Hidden || window->IsFallbackWindow)
                continue;
            // Skip click-through windows. The parallel chat message surfaces
            // are inputless; their small top handles remain normal ImGui windows.
            if (window->Flags & ImGuiWindowFlags_NoMouseInputs)
                continue;
            if (x >= window->Pos.x && x < window->Pos.x + window->Size.x &&
                y >= window->Pos.y && y < window->Pos.y + window->Size.y)
                return true;
        }
        return false;
    }

    bool handle_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result)
    {
        if (!g_imguiInitialized || !ImGui::GetCurrentContext())
            return false;

        if (is_mouse_message(msg))
        {
            // WM_SETCURSOR fires before every WM_MOUSEMOVE. Skip it entirely
            // -- NoMouseCursorChange already prevents ImGui from calling
            // LoadCursor+SetCursor, and forwarding it added no value.
            if (msg == WM_SETCURSOR)
                return false;

            // Scale mouse coordinates from window-client space to DX9
            // backbuffer space before ImGui sees them.  This keeps the input
            // queue consistent with io.DisplaySize (set to backbuffer dims in
            // render_integrated_frame).
            LPARAM scaledLParam = lParam;
            if (has_client_area_coords(msg) &&
                (g_mouseScaleX != 1.0f || g_mouseScaleY != 1.0f))
            {
                int x = static_cast<int>(GET_X_LPARAM(lParam) * g_mouseScaleX);
                int y = static_cast<int>(GET_Y_LPARAM(lParam) * g_mouseScaleY);
                scaledLParam = MAKELPARAM(x, y);
            }

            // Always forward mouse messages to ImGui so it can track hover
            // state across all windows (panels, roulette, emoji picker, etc.).
            // This is cheap now that NoMouseCursorChange is set.
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, scaledLParam);

            // Let ImGui decide if it owns the mouse this frame.
            auto& io = ImGui::GetIO();
            if (io.WantCaptureMouse)
            {
                result = TRUE;
                return true;
            }

            // Fallback for the 1-frame hover delay: io.WantCaptureMouse is
            // set during NewFrame, so a click that arrives between frames can
            // slip through before the hover state catches up.  For button-down
            // events, hit-test directly against the previous frame's window
            // rects to prevent the click from reaching the game and
            // accidentally moving the character.
            if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN ||
                msg == WM_MBUTTONDOWN || msg == WM_LBUTTONDBLCLK)
            {
                float mx = static_cast<float>(GET_X_LPARAM(scaledLParam));
                float my = static_cast<float>(GET_Y_LPARAM(scaledLParam));
                if (is_point_over_any_imgui_window(mx, my))
                {
                    result = TRUE;
                    return true;
                }
            }

            // Intercept mouse wheel over chat area for emoji scroll sync.
            if (msg == WM_MOUSEWHEEL && g_var)
            {
                LONG cx, cy;
                POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &pt);
                cx = pt.x;
                cy = pt.y;

                auto clientH = static_cast<float>(g_var->client.height);
                auto clientW = static_cast<float>(g_var->client.width);
                if (clientH > 0 && clientW > 0)
                {
                    auto chatBottomY = clientH - g_tune.chatBottomOffset;
                    auto chatTopY = chatBottomY - clientH * g_tune.chatTopPct;
                    auto chatRightX = clientW * g_tune.chatRightPct;

                    if (cx >= 0 && cx < static_cast<LONG>(chatRightX) &&
                        cy >= static_cast<LONG>(chatTopY) && cy <= static_cast<LONG>(chatBottomY + 30))
                    {
                        auto delta = GET_WHEEL_DELTA_WPARAM(wParam);
                        auto ticks = delta / WHEEL_DELTA;
                        set_chat_scroll_offset(g_chatScrollOffset + ticks * 2);
                    }
                }
            }

            // Mouse is not over any ImGui element -- don't consume the message.
            return false;
        }

        // Non-mouse messages (keyboard, etc.) -- always forward to ImGui.
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
        return false;
    }

    // -----------------------------------------------------------------------
    //  DX9 hooks and render infrastructure
    // -----------------------------------------------------------------------

    bool hook_vtable(void* instance, std::size_t index, void* hook, void** original)
    {
        if (!instance)
            return false;

        auto vtable = *reinterpret_cast<void***>(instance);
        if (!vtable)
            return false;

        DWORD oldProtect{};
        if (!VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;

        if (vtable[index] == hook)
        {
            DWORD unused{};
            VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &unused);
            return true;
        }

        *original = vtable[index];
        vtable[index] = hook;

        DWORD unused{};
        VirtualProtect(&vtable[index], sizeof(void*), oldProtect, &unused);
        return true;
    }

    void release_texture(LPDIRECT3DTEXTURE9& texture, bool& loadAttempted)
    {
        if (texture)
        {
            texture->Release();
            texture = nullptr;
        }
        loadAttempted = false;
    }

    void release_device_textures_for_reset()
    {
        release_emoji_textures();
        release_item_icon_textures();
        release_texture(g_rouletteBgTexture, g_rouletteBgLoadAttempted);
        release_texture(g_rewardIconTexture, g_rewardIconLoadAttempted);
        release_texture(g_rouletteIconTexture, g_rouletteIconLoadAttempted);
        release_texture(g_settingsIconTexture, g_settingsIconLoadAttempted);
        release_texture(g_npcIconTexture, g_npcIconLoadAttempted);
        release_texture(g_teleportIconTexture, g_teleportIconLoadAttempted);
        release_texture(g_windowBgTexture, g_windowBgLoadAttempted);
    }

    void init_game_imgui(IDirect3DDevice9* device)
    {
        if (g_imguiInitialized)
            return;

        g_overlayHwnd = g_var->hwnd;
        g_device = device;
        load_imgui_settings();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();

        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        ImGui_ImplWin32_Init(g_var->hwnd);
        ImGui_ImplDX9_Init(device);
        g_imguiInitialized = true;
        reset_overlay_rects();

        // Hook DirectInput mouse polling so clicks are suppressed at the
        // earliest point in the game loop when ImGui owns the cursor.
        install_dinput_mouse_hook();
    }

    void shutdown_game_imgui()
    {
        if (!g_imguiInitialized)
            return;

        save_imgui_settings();
        release_device_textures_for_reset();
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imguiInitialized = false;
        g_device = nullptr;
        g_overlayHwnd = nullptr;
    }

    void render_integrated_frame(IDirect3DDevice9* device)
    {
        if (!g_running || !g_var->hwnd || !IsWindow(g_var->hwnd))
            return;

        init_game_imgui(device);
        g_device = device;
        g_overlayHwnd = g_var->hwnd;

        sync_map_transition_state();
        update_roulette_spin_state();
        sync_emoji_overlay_scene_state();

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();

        // --- Backbuffer / window coordinate synchronization ---
        // ImGui_ImplWin32_NewFrame sets io.DisplaySize from GetClientRect,
        // which is the window's client-area size in pixels.  When the game
        // runs at an internal resolution that differs from the window size
        // (common at non-native resolutions), DX9 renders at backbuffer
        // dimensions while ImGui positions and hit-tests at window dimensions
        // -- panels appear visually offset from their clickable area.
        //
        // Fix: (a) override DisplaySize to the backbuffer before NewFrame so
        //      ImGui's frame setup, rendering, and hit-testing all use the
        //      same coordinate space;  (b) update the per-frame scale factors
        //      consumed by handle_wnd_proc, which scales mouse messages
        //      entering ImGui's input queue;  (c) re-inject the current
        //      cursor position in backbuffer space so that any position event
        //      added by the Win32 backend's UpdateMouseData is superseded.
        {
            auto& io = ImGui::GetIO();
            float winW = io.DisplaySize.x;
            float winH = io.DisplaySize.y;
            float bbW  = static_cast<float>(g_var->client.width);
            float bbH  = static_cast<float>(g_var->client.height);

            if (bbW > 0.0f && bbH > 0.0f)
            {
                io.DisplaySize = ImVec2(bbW, bbH);

                if (winW > 0.0f && winH > 0.0f &&
                    (bbW != winW || bbH != winH))
                {
                    g_mouseScaleX = bbW / winW;
                    g_mouseScaleY = bbH / winH;

                    // Re-inject the cursor position in backbuffer space.
                    // The Win32 backend may have queued a position event
                    // from GetCursorPos+ScreenToClient (window space);
                    // adding a corrected event here ensures the last
                    // queued position -- the one NewFrame will finalize --
                    // is in backbuffer space, matching DisplaySize.
                    POINT pt;
                    if (::GetCursorPos(&pt) &&
                        ::ScreenToClient(g_var->hwnd, &pt))
                    {
                        io.AddMousePosEvent(pt.x * g_mouseScaleX,
                                            pt.y * g_mouseScaleY);
                    }
                }
                else
                {
                    g_mouseScaleX = 1.0f;
                    g_mouseScaleY = 1.0f;
                }
            }
        }

        ImGui::NewFrame();

        if (g_clearImguiActiveId)
        {
            ImGui::ClearActiveID();
            g_clearImguiActiveId = false;
        }

        // DirectInput click suppression is handled by the GetDeviceState
        // vtable hook (install_dinput_mouse_hook), which intercepts mouse
        // button state at poll time -- before the game loop processes input.
        // No additional clearing is needed here in the present hook.

        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0 && !g_draggingPanel)
        {
            g_panelMouseWasDown = false;
            g_rollMouseWasDown = false;
        }

        reset_overlay_rects();
        ensure_emoji_catalog_loaded();

        // Pre-cache overlay textures right after the SAH catalog scan so
        // the first panel open doesn't stall the frame with file I/O.
        // Each loader is internally guarded by loadAttempted, so this is
        // a no-op on every frame after the first.
        load_roulette_bg_texture();
        load_roulette_icon_texture();
        load_reward_icon_texture();
        load_settings_icon_texture();
        load_npc_icon_texture();
        load_teleport_icon_texture();
        load_window_bg_texture();

        // When the native UI is hidden (F11), suppress all overlay drawing
        // so our elements disappear in sync with the game's own UI.
        if (!g_nativeUIHidden)
        {
            draw_floating_emoji_overlays();
            draw_lower_chat_emoji_overlay();

            // Emoji button + picker are always visible during gameplay.
            draw_emoji_overlay();

            if (is_game_scene_stable())
            {
                update_anchored_button_positions();
                if (roulette_event::hasList)
                    draw_roulette_button_overlay();
                if (reward_item_event::hasList)
                    draw_reward_button_overlay();
                draw_settings_button_overlay();
                draw_npc_button_overlay();

                // Request the teleport destination list once per login.
                if (!teleport_event::listReceived)
                    request_teleport_list();
                draw_teleport_button_overlay();
            }

            debug_panel::render();
            custom_chat::render_ingame_chat();

            if (g_showPanel)
                draw_panel_shell();

            if (is_game_scene_stable())
            {
                if (reward_item_event::hasList)
                {
                    draw_reward_bar();
                    update_reward_auto_claim();
                }
                draw_settings_panel();
                draw_npc_panel();
                draw_teleport_panel();
            }
        }

        // Flush D3DX chat text before ImGui renders its draw lists so overlay
        // panels stay above chat text.
        if (!g_nativeUIHidden)
            custom_chat::flush_d3dx_text();

        ImGui::EndFrame();
        ImGui::Render();

        // Only call into the DX9 backend when there are actual draw commands.
        // CreateStateBlock(D3DSBT_ALL) + Capture + Apply runs every
        // RenderDrawData call and is the single heaviest per-frame cost in
        // the overlay. Skipping it when the draw lists are empty avoids the
        // full DX9 state save/restore cycle on frames with nothing to draw.
        auto* drawData = ImGui::GetDrawData();
        if (drawData && drawData->CmdListsCount > 0)
            ImGui_ImplDX9_RenderDrawData(drawData);

        save_imgui_settings_if_dirty(750);
    }

    HRESULT __stdcall present_hook(IDirect3DDevice9* device, const RECT* src, const RECT* dst, HWND hwnd, const RGNDATA* dirty)
    {
        render_integrated_frame(device);
        return g_originalPresent ? g_originalPresent(device, src, dst, hwnd, dirty) : D3D_OK;
    }

    HRESULT __stdcall reset_hook(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* params)
    {
        if (g_imguiInitialized)
        {
            ImGui_ImplDX9_InvalidateDeviceObjects();
            release_device_textures_for_reset();
        }

        auto result = g_originalReset ? g_originalReset(device, params) : D3D_OK;

        if (g_imguiInitialized && SUCCEEDED(result))
            ImGui_ImplDX9_CreateDeviceObjects();

        return result;
    }

    void hook_game_device(IDirect3DDevice9* device)
    {
        if (!device)
            return;

        if (g_hookedDevice != device)
        {
            shutdown_game_imgui();
            g_hookedDevice = device;
            g_originalReset = nullptr;
            g_originalPresent = nullptr;
        }

        void* originalReset = nullptr;
        if (hook_vtable(device, 16, reinterpret_cast<void*>(reset_hook), &originalReset) && originalReset)
            g_originalReset = reinterpret_cast<ResetFn>(originalReset);

        void* originalPresent = nullptr;
        if (hook_vtable(device, 17, reinterpret_cast<void*>(present_hook), &originalPresent) && originalPresent)
            g_originalPresent = reinterpret_cast<PresentFn>(originalPresent);

    }

    DWORD WINAPI render_thread(LPVOID)
    {
        while (g_running)
        {
            if (!g_var->hwnd || !IsWindow(g_var->hwnd) || !g_var->camera.device)
            {
                Sleep(250);
                continue;
            }

            ensure_client_sysmsg_dispatch_ready();

            sync_emoji_overlay_scene_state();

            hook_game_device(g_var->camera.device);
            Sleep(250);
        }

        return 0;
    }
}
