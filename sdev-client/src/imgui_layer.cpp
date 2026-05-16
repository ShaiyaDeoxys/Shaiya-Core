#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <d3d9.h>
#include <algorithm>
using std::max;
using std::min;
#include <gdiplus.h>
#include <objidl.h>
#include <util/util.h>
#include <external/stb/stb_image.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cctype>
#include <cfloat>
#include <cstring>
#include <cmath>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "gdiplus.lib")
#include <external/imgui/imgui.h>
#include <external/imgui/imgui_internal.h>
#include <external/imgui/backends/imgui_impl_dx9.h>
#include <external/imgui/backends/imgui_impl_win32.h>
#include <shaiya/include/network/game/outgoing/0800.h>
#include "include/main.h"
#include "include/shaiya/CCharacter.h"
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/CItem.h"
#include "include/shaiya/CPlayerData.h"
#include "include/shaiya/CTexture.h"
#include "include/shaiya/ItemInfo.h"
#include "include/shaiya/Roulette.h"
#include "include/shaiya/CWorldMgr.h"
#include "include/shaiya/Static.h"
#include "resources/resource.h"
#include "include/config.h"
#include "include/debug_panel.h"
using namespace shaiya;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void naked_chat_add_token_filter();
void naked_chat_balloon_text_create();
void naked_capture_chat_balloon_text();
void naked_floating_text_create();
void naked_capture_floating_static_text();
void naked_static_text_create();
void naked_floating_static_text_draw();
void naked_native_text_draw_probe();

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
- The overlay runs passively for always-on, click-through client HUD features.
- F8 toggles the roulette panel. F7 toggles performance mode.

Chat type notes
- Upper bar: 15 orange, 16 red, 17 red, 18 yellow, 19 high-tone green, 20 violet, 21 light blue, 22 light green, 34 light grey.
- Lower bar (chat window): 0 white, 35 light green, 36 light red, 37 light violet, 38 normal brownish, 39 red, 40 yellow, 41 white, 42 red, 43 greyish-white, 44 same as 43, 45 darker red, 46 white, 47 violet, 49 light blue.
- On-screen notice-like messages: 23 to 33.
- Special case: 48 and 50 behave like an alternate on-screen raid-style light violet message.
*/

namespace imgui_layer
{
    constexpr const char* kImguiIniSection = "IMGUI";
    constexpr const char* kPanelPosXKey = "PANEL_X";
    constexpr const char* kPanelPosYKey = "PANEL_Y";
    // Emoji button default position and picker offset from button
    constexpr auto kDefaultEmojiButtonPosition = ImVec2(321.0f, 939.0f);
    constexpr auto kEmojiPickerOffset = ImVec2(32.0f, 0.0f);
    constexpr const char* kEmojiBtnXKey = "EMOJI_X";
    constexpr const char* kEmojiBtnYKey = "EMOJI_Y";
    constexpr auto kEmojiButtonSize = ImVec2(20.0f, 20.0f);
    constexpr auto kEmojiPickerSize = ImVec2(190.0f, 200.0f);
    constexpr auto kEmojiPickerIconSize = ImVec2(20.0f, 20.0f);
    constexpr DWORD kEmojiSceneGraceMs = 4000;
    constexpr DWORD kEmojiMapChangeGraceMs = 8000;

    inline std::atomic_bool g_running = false;
    inline bool g_f7Down = false;
    inline bool g_f8Down = false;
    inline bool g_closeRequested = false;
    inline bool g_showPanel = false;
    inline bool g_sentWelcomeMessage = false;
    inline bool g_waitingWelcomeMessage = false;
    inline bool g_f7BundleUsed = false;
    inline bool g_imguiSettingsLoaded = false;
    inline bool g_imguiSettingsDirty = false;
    inline DWORD g_f7MessageTick = 0;
    inline DWORD g_welcomeStartTick = 0;
    inline DWORD g_lastPanelSaveTick = 0;
    inline DWORD g_imguiSettingsDirtyTick = 0;
    inline HWND g_overlayHwnd = nullptr;
    inline LPDIRECT3DDEVICE9 g_device = nullptr;
    inline bool g_imguiInitialized = false;

    // Scale factors mapping window-client coordinates to DX9 backbuffer space.
    // Updated each render frame; consumed by handle_wnd_proc to scale mouse
    // events before they enter ImGui's input queue.
    inline float g_mouseScaleX = 1.0f;
    inline float g_mouseScaleY = 1.0f;
    inline LPDIRECT3DDEVICE9 g_hookedDevice = nullptr;
    using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    using PresentFn = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    inline ResetFn g_originalReset = nullptr;
    inline PresentFn g_originalPresent = nullptr;

    // Forward declaration — defined further down alongside the DX9 vtable hooks.
    bool hook_vtable(void* instance, std::size_t index, void* hook, void** original);

    // DirectInput GetDeviceState hook — suppresses mouse clicks at poll time
    // when ImGui owns the cursor, preventing click-to-move behind panels.
    // IDirectInputDevice8::GetDeviceState is vtable slot 9.
    using GetDeviceStateFn = HRESULT(__stdcall*)(IDirectInputDevice8A*, DWORD, LPVOID);
    inline GetDeviceStateFn g_originalMouseGetDeviceState = nullptr;
    inline GetDeviceStateFn g_originalMouse2GetDeviceState = nullptr;
    inline bool g_mouseDeviceHooked = false;

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

    inline RECT g_panelDragRect{};
    inline RECT g_panelWindowRect{};
    inline RECT g_emojiButtonRect{};
    inline RECT g_emojiPickerRect{};
    inline ImVec2 g_panelPosition = ImVec2(80.0f, 80.0f);
    inline ImVec2 g_emojiButtonPosition = kDefaultEmojiButtonPosition;
    inline ImVec2 g_emojiPickerPosition = ImVec2(0.0f, 0.0f);
    inline bool g_emojiRepositionMode = false;
    inline DWORD g_lastRouletteRollTick = 0;
    inline DWORD g_lastRouletteListTick = 0;
    inline bool g_showEmojiPicker = false;
    inline bool g_chatEmojiHookInstalled = false;
    inline bool g_draggedPanel = false;
    inline bool g_draggingPanel = false;
    inline bool g_panelMouseWasDown = false;
    inline ImVec2 g_panelDragOffset = ImVec2(0.0f, 0.0f);
    inline bool g_emojisEnabled = true;
    inline bool g_gifsEnabled = true;
    inline bool g_clearImguiActiveId = false;
    inline bool g_rollMouseWasDown = false;

    // Roulette background PNG loaded from data.sah (Assets/General/Roulette.png)
    inline LPDIRECT3DTEXTURE9 g_rouletteBgTexture = nullptr;
    inline uint64_t g_rouletteBgDataOffset = 0;
    inline uint64_t g_rouletteBgDataSize = 0;
    inline bool g_rouletteBgFound = false;
    inline bool g_rouletteBgLoadAttempted = false;

    // Roll button position (fixed overlay — relative to wheel bottom)
    constexpr float kRollButtonOffsetX = -1.0f;
    constexpr float kRollButtonOffsetY = -47.0f;
    constexpr float kRollButtonH = 30.0f;
    // Width is derived from wheel: inset 28px from each side to avoid overflow
    constexpr float kRollButtonInset = 95.0f;
    constexpr float kPanelFixedW = 546.0f;
    constexpr float kPanelFixedH = 577.0f;

    enum class VisualTokenKind
    {
        Emoji,
        Gif
    };

    struct AnimatedVisualFrame
    {
        LPDIRECT3DTEXTURE9 texture;
        DWORD delayMs;
    };

    struct EmojiEntry
    {
        VisualTokenKind kind;
        int index;
        std::string token;
        std::string fileName;
        uint64_t dataOffset;
        uint64_t dataSize;
        LPDIRECT3DTEXTURE9 texture;
        LPDIRECT3DTEXTURE9 previewTexture;
        std::vector<AnimatedVisualFrame> frames;
        bool loadAttempted;
        bool previewLoadAttempted;
        DWORD previewLastUsedTick;
    };

    inline std::vector<EmojiEntry> g_emojis;
    inline bool g_emojiCatalogLoaded = false;

    struct ChatEmojiTokenOverlay
    {
        EmojiEntry* emoji;
        float xOffset;
    };

    struct ChatEmojiLineOverlay
    {
        DWORD tick;
        std::vector<ChatEmojiTokenOverlay> tokens;
    };

    struct FloatingEmojiRenderOverlay
    {
        DWORD tick;
        int x;
        int y;
        bool lowerChat;
        std::vector<ChatEmojiTokenOverlay> tokens;
    };

    struct LowerChatEmojiLine
    {
        std::string text;
        std::vector<ChatEmojiTokenOverlay> tokens;
        int visualLines = 1;
    };

    struct NativeItemIconEntry
    {
        int iconId = 0;
        CTexture texture{};
        bool loadAttempted = false;
    };

    struct ItemIconAtlasEntry
    {
        std::string fileName;
        LPDIRECT3DTEXTURE9 texture = nullptr;
        bool loadAttempted = false;
        int width = 0;
        int height = 0;
    };


    inline std::mutex g_chatEmojiMutex;
    inline std::deque<LowerChatEmojiLine> g_lowerChatEmojiLines;
    inline std::mutex g_floatingEmojiMutex;
    inline bool g_hasPendingFloatingEmojiLine = false;
    inline ChatEmojiLineOverlay g_pendingFloatingEmojiLine{};
    inline std::unordered_map<void*, ChatEmojiLineOverlay> g_floatingEmojiLines;
    inline std::vector<FloatingEmojiRenderOverlay> g_floatingEmojiRenders;
    inline thread_local std::vector<ChatEmojiLineOverlay> g_staticTextCreateStack;
    inline bool g_emojiSceneStateInitialized = false;
    inline bool g_lastEmojiGameScene = false;
    inline DWORD g_emojiSceneLostTick = 0;
    inline DWORD g_emojiSceneTransitionUntilTick = 0;
    inline uint16_t g_lastEmojiMapId = 0;
    inline DWORD g_emojiMapGraceUntilTick = 0;
    inline uint32_t g_lastEmojiCharId = 0;
    inline CCharacter* g_lastEmojiUser = nullptr;
    inline std::string g_sanitizedChatText;
    inline std::string g_sanitizedFloatingText;
    inline std::map<int, NativeItemIconEntry> g_nativeItemIcons;
    inline std::map<std::string, ItemIconAtlasEntry> g_itemIconAtlases;
    inline ULONG_PTR g_gdiplusToken = 0;
    inline bool g_gdiplusStartAttempted = false;

    // Chat overlay tuning constants
    struct ChatTuning
    {
        float chatBottomOffset;
        float lineHeight;
        float chatTextX;
        float chatRightPct;
        float iconSize;
        float chatTopPct;
        float floatingIconSize;
        float floatingYAdjust;
        float lowerChatMaxX;
        float lowerChatMinY;
    };

    constexpr ChatTuning kDefaultChatTuning = {
        78.0f, 15.9f, 26.0f, 0.26f, 17.0f, 0.25f, 16.0f, -3.0f, 360.0f, 650.0f
    };

    inline ChatTuning g_tune = kDefaultChatTuning;
    constexpr int kScrollMinLines = 1;

    inline int g_chatScrollOffset = 0;

    // Forward declarations — full definitions later
    void set_chat_scroll_offset(int value);
    LPDIRECT3DTEXTURE9 create_texture_from_image_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize);

    std::string get_client_config_ini_path()
    {
        char moduleFileName[MAX_PATH]{};
        if (!GetModuleFileNameA(nullptr, moduleFileName, MAX_PATH))
            return ".\\CONFIG.ini";

        std::string path(moduleFileName);
        auto slashPos = path.find_last_of("\\/");
        if (slashPos != std::string::npos)
            path.resize(slashPos + 1);

        path += "CONFIG.ini";
        return path;
    }

    const char* get_imgui_ini_path()
    {
        static std::string path = get_client_config_ini_path();
        return path.c_str();
    }

    const char* get_game_ini_path()
    {
        return g_var->iniFileName.data();
    }

    bool has_ini_path(const char* path)
    {
        return path && path[0] != '\0';
    }

    bool same_ini_path(const char* lhs, const char* rhs)
    {
        return has_ini_path(lhs) && has_ini_path(rhs) && _stricmp(lhs, rhs) == 0;
    }

    bool read_profile_string_any(const char* section, const char* key, char* buffer, DWORD bufferSize)
    {
        if (!buffer || bufferSize == 0)
            return false;

        buffer[0] = '\0';
        auto primaryPath = get_imgui_ini_path();
        if (has_ini_path(primaryPath))
        {
            GetPrivateProfileStringA(section, key, "", buffer, bufferSize, primaryPath);
            if (buffer[0] != '\0')
                return true;
        }

        auto gamePath = get_game_ini_path();
        if (has_ini_path(gamePath) && !same_ini_path(primaryPath, gamePath))
        {
            GetPrivateProfileStringA(section, key, "", buffer, bufferSize, gamePath);
            if (buffer[0] != '\0')
                return true;
        }

        return false;
    }

    void write_profile_string_all(const char* section, const char* key, const char* value)
    {
        auto primaryPath = get_imgui_ini_path();
        if (has_ini_path(primaryPath))
        {
            WritePrivateProfileStringA(section, key, value, primaryPath);
            WritePrivateProfileStringA(nullptr, nullptr, nullptr, primaryPath);
        }

        auto gamePath = get_game_ini_path();
        if (has_ini_path(gamePath) && !same_ini_path(primaryPath, gamePath))
        {
            WritePrivateProfileStringA(section, key, value, gamePath);
            WritePrivateProfileStringA(nullptr, nullptr, nullptr, gamePath);
        }
    }

    float read_imgui_float(const char* key, float fallback)
    {
        char buffer[64]{};
        if (!read_profile_string_any(kImguiIniSection, key, buffer, sizeof(buffer)))
            return fallback;

        return static_cast<float>(std::atof(buffer));
    }

    int read_imgui_int(const char* key, int fallback)
    {
        char buffer[32]{};
        if (!read_profile_string_any(kImguiIniSection, key, buffer, sizeof(buffer)))
            return fallback;

        return std::atoi(buffer);
    }

    void write_imgui_float(const char* key, float value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        write_profile_string_all(kImguiIniSection, key, buffer);
    }

    void write_imgui_int(const char* key, int value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%d", value);
        write_profile_string_all(kImguiIniSection, key, buffer);
    }

    bool read_imgui_bool(const char* key, bool fallback)
    {
        char buffer[16]{};
        if (!read_profile_string_any(kImguiIniSection, key, buffer, sizeof(buffer)))
            strcpy_s(buffer, fallback ? "TRUE" : "FALSE");

        return _stricmp(buffer, "TRUE") == 0
            || std::strcmp(buffer, "1") == 0
            || _stricmp(buffer, "ON") == 0;
    }

    void write_imgui_bool(const char* key, bool value)
    {
        write_profile_string_all(kImguiIniSection, key, value ? "TRUE" : "FALSE");
    }

    void load_imgui_settings()
    {
        if (g_imguiSettingsLoaded)
            return;

        g_panelPosition.x = read_imgui_float(kPanelPosXKey, g_panelPosition.x);
        g_panelPosition.y = read_imgui_float(kPanelPosYKey, g_panelPosition.y);
        g_emojiButtonPosition.x = read_imgui_float(kEmojiBtnXKey, g_emojiButtonPosition.x);
        g_emojiButtonPosition.y = read_imgui_float(kEmojiBtnYKey, g_emojiButtonPosition.y);
        g_imguiSettingsLoaded = true;
    }

    void save_imgui_settings()
    {
        write_imgui_float(kPanelPosXKey, g_panelPosition.x);
        write_imgui_float(kPanelPosYKey, g_panelPosition.y);
        write_imgui_float(kEmojiBtnXKey, g_emojiButtonPosition.x);
        write_imgui_float(kEmojiBtnYKey, g_emojiButtonPosition.y);
        g_imguiSettingsDirty = false;
        g_lastPanelSaveTick = GetTickCount();
    }

    void mark_imgui_settings_dirty()
    {
        g_imguiSettingsDirty = true;
        g_imguiSettingsDirtyTick = GetTickCount();
    }

    void save_imgui_settings_if_dirty(DWORD debounceMs)
    {
        if (!g_imguiSettingsDirty)
            return;

        auto now = GetTickCount();
        if (debounceMs != 0 && now - g_imguiSettingsDirtyTick < debounceMs)
            return;

        if (debounceMs != 0 && g_lastPanelSaveTick != 0 && now - g_lastPanelSaveTick < debounceMs)
            return;

        save_imgui_settings();
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
            && g_pWorldMgr->mapSize > 0
            && g_pPlayerData->charId != 0;
    }

    bool is_emoji_transition_grace_active()
    {
        return g_emojiMapGraceUntilTick != 0
            && static_cast<int32_t>(GetTickCount() - g_emojiMapGraceUntilTick) < 0;
    }

    void clear_emoji_text_overlays()
    {
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            g_lowerChatEmojiLines.clear();
        }

        {
            std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
            g_hasPendingFloatingEmojiLine = false;
            g_pendingFloatingEmojiLine = {};
            g_floatingEmojiLines.clear();
            g_floatingEmojiRenders.clear();
        }
    }

    void sync_emoji_overlay_scene_state()
    {
        auto now = GetTickCount();
        auto gameScene = is_game_scene();
        auto charId = g_pPlayerData ? g_pPlayerData->charId : uint32_t{ 0 };
        auto mapId = gameScene ? g_pPlayerData->mapId : uint16_t{ 0 };
        auto* user = gameScene ? g_pWorldMgr->user : nullptr;

        if (!g_emojiSceneStateInitialized)
        {
            g_emojiSceneStateInitialized = true;
            g_lastEmojiGameScene = gameScene;
            g_lastEmojiCharId = charId;
            g_lastEmojiMapId = mapId;
            g_lastEmojiUser = user;
            return;
        }

        if (!gameScene)
        {
            if (g_lastEmojiGameScene && g_emojiSceneLostTick == 0)
            {
                g_emojiSceneLostTick = now;
                g_emojiMapGraceUntilTick = now + kEmojiSceneGraceMs;
                g_emojiSceneTransitionUntilTick = now + kEmojiSceneGraceMs;
                g_showEmojiPicker = false;
                clear_emoji_text_overlays();
            }

            if (g_lastEmojiGameScene && now - g_emojiSceneLostTick < kEmojiSceneGraceMs)
                return;

            if (g_lastEmojiGameScene || g_lastEmojiCharId != 0)
                clear_emoji_text_overlays();

            g_lastEmojiGameScene = false;
            g_lastEmojiCharId = 0;
            g_lastEmojiMapId = 0;
            g_lastEmojiUser = nullptr;
            return;
        }

        g_emojiSceneLostTick = 0;

        if ((g_lastEmojiCharId != 0 && g_lastEmojiCharId != charId)
            || (!g_lastEmojiGameScene && g_lastEmojiCharId == 0))
        {
            clear_emoji_text_overlays();
        }

        if (mapId != 0 && g_lastEmojiMapId != 0 && mapId != g_lastEmojiMapId)
        {
            g_lastEmojiMapId = mapId;
            g_emojiMapGraceUntilTick = now + kEmojiMapChangeGraceMs;
            g_emojiSceneTransitionUntilTick = now + kEmojiMapChangeGraceMs;
            g_showEmojiPicker = false;
            clear_emoji_text_overlays();
        }
        else
        {
            g_lastEmojiMapId = mapId;
        }

        if (!g_lastEmojiGameScene || g_lastEmojiUser != user)
            g_emojiSceneTransitionUntilTick = now + 1000;

        g_lastEmojiGameScene = true;
        g_lastEmojiCharId = charId;
        g_lastEmojiUser = user;
    }

    bool is_overlay_display_usable(const ImVec2& size)
    {
        return size.x >= 320.0f && size.y >= 240.0f;
    }

    bool is_game_window_foreground()
    {
        if (!g_var->hwnd)
            return false;

        auto foreground = GetForegroundWindow();
        if (!foreground)
            return false;

        return foreground == g_var->hwnd
            || GetAncestor(foreground, GA_ROOT) == g_var->hwnd;
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

    bool is_cursor_in_rect(const RECT& rect);

    void release_imgui_capture()
    {
        g_clearImguiActiveId = true;
    }

    // Lightweight point-in-RECT test using coordinates already available in the message.
    static bool point_in_rect_fast(LONG x, LONG y, const RECT& r)
    {
        return r.left != r.right
            && r.top != r.bottom
            && x >= r.left && x < r.right
            && y >= r.top && y < r.bottom;
    }

    // Returns true when the cursor is over any ImGui-owned region (panel, emoji, picker).
    static bool is_mouse_over_imgui(LONG cx, LONG cy)
    {
        return point_in_rect_fast(cx, cy, g_panelWindowRect)
            || point_in_rect_fast(cx, cy, g_emojiButtonRect)
            || point_in_rect_fast(cx, cy, g_emojiPickerRect);
    }

    // Tracks whether ImGui needs mouse input this frame.
    // When false we skip ImGui_ImplWin32_WndProcHandler entirely for mouse
    // messages, avoiding TrackMouseEvent syscalls and ImGui input-queue work.
    inline bool g_imguiWantsMouse = false;

    static bool is_mouse_message(UINT msg)
    {
        switch (msg)
        {
        case WM_MOUSEMOVE:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
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
        case WM_NCMOUSELEAVE:
        case WM_MOUSELEAVE:
        case WM_SETCURSOR:
            return true;
        default:
            return false;
        }
    }

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

    // Hit-test the mouse position against every visible ImGui window from
    // the previous frame.  This covers panels, roulette, emoji picker, debug
    // overlays — anything ImGui drew — without maintaining a manual rect list.
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
            // — NoMouseCursorChange already prevents ImGui from calling
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

            // Mouse is not over any ImGui element — don't consume the message.
            return false;
        }

        // Non-mouse messages (keyboard, etc.) — always forward to ImGui.
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
        return false;
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

    int resolve_item_icon_index(int type, int typeId)
    {
        auto* itemInfo = CDataFile::GetItemInfo(type, typeId);
        if (!itemInfo)
            return -1;

        return static_cast<int>(itemInfo->icon);
    }

    const char* get_native_item_icon_folder()
    {
        static constexpr const char* kDefaultIconFolder = "data/interface/icon";
        auto kConfiguredIconFolder = reinterpret_cast<const char*>(0x7AC104);
        if (GetFileAttributesA(kDefaultIconFolder) != INVALID_FILE_ATTRIBUTES)
            return kDefaultIconFolder;
        if (kConfiguredIconFolder && kConfiguredIconFolder[0])
            return kConfiguredIconFolder;
        return kDefaultIconFolder;
    }

    CTexture* get_or_load_native_item_icon_texture(int iconId)
    {
        if (iconId < 0)
            return nullptr;

        auto& entry = g_nativeItemIcons[iconId];
        if (!entry.loadAttempted)
        {
            entry.iconId = iconId;
            entry.loadAttempted = true;

            char fileName[32]{};
            std::snprintf(fileName, sizeof(fileName), iconId < 100 ? "%02d.dds" : "%d.dds", iconId);
            if (!CTexture::CreateFromFile(&entry.texture, get_native_item_icon_folder(), fileName, 32, 32))
            {
                std::snprintf(fileName, sizeof(fileName), iconId < 100 ? "%02d.tga" : "%d.tga", iconId);
                CTexture::CreateFromFile(&entry.texture, get_native_item_icon_folder(), fileName, 32, 32);
            }
        }

        return entry.texture.texture ? &entry.texture : nullptr;
    }

    bool get_item_icon_atlas_config(int type, std::string& outFileName, int& outCols, int& outRows, int& outWidth, int& outHeight)
    {
        if (type >= 1 && type <= 24)
        {
            char fileName[16]{};
            std::snprintf(fileName, sizeof(fileName), "%02d.dds", type);
            outFileName = fileName;
            outCols = 4;
            outRows = 16;
            outWidth = 128;
            outHeight = 512;
            return true;
        }

        if (type >= 31 && type <= 40)
        {
            char fileName[16]{};
            std::snprintf(fileName, sizeof(fileName), "%d.dds", type);
            outFileName = fileName;
            outCols = 4;
            outRows = 16;
            outWidth = 128;
            outHeight = 512;
            return true;
        }

        switch (type)
        {
        case 25:
        case 26:
        case 27:
        case 28:
        case 29:
        case 42:
        case 43:
        case 44:
        case 78:
        case 79:
        case 80:
        case 94:
        case 99:
            outFileName = "icon_somo.dds";
            outCols = 16;
            outRows = 16;
            outWidth = 512;
            outHeight = 512;
            return true;
        case 30:
        case 95:
            outFileName = "icon_rapis.dds";
            outCols = 8;
            outRows = 16;
            outWidth = 256;
            outHeight = 512;
            return true;
        case 150:
            outFileName = "icon_DualLayer.dds";
            outCols = 16;
            outRows = 16;
            outWidth = 512;
            outHeight = 512;
            return true;
        case 100:
        case 101:
        case 102:
        case 103:
            outFileName = "icon_somo2.dds";
            outCols = 16;
            outRows = 16;
            outWidth = 512;
            outHeight = 512;
            return true;
        default:
            return false;
        }
    }

    int get_item_icon_atlas_resource_id(const std::string& fileName)
    {
        if (fileName.size() == 6 && fileName[2] == '.' && _stricmp(fileName.c_str() + 2, ".dds") == 0)
        {
            auto type = (fileName[0] - '0') * 10 + (fileName[1] - '0');
            if (type >= 1 && type <= 24)
                return IDR_ITEM_ICON_01 + type - 1;
            if (type >= 31 && type <= 40)
                return IDR_ITEM_ICON_31 + type - 31;
        }

        if (_stricmp(fileName.c_str(), "icon_DualLayer.dds") == 0)
            return IDR_ITEM_ICON_DUALLAYER;
        if (_stricmp(fileName.c_str(), "icon_pet.dds") == 0)
            return IDR_ITEM_ICON_PET;
        if (_stricmp(fileName.c_str(), "icon_rapis.dds") == 0)
            return IDR_ITEM_ICON_RAPIS;
        if (_stricmp(fileName.c_str(), "icon_somo.dds") == 0)
            return IDR_ITEM_ICON_SOMO;
        if (_stricmp(fileName.c_str(), "icon_somo2.dds") == 0)
            return IDR_ITEM_ICON_SOMO2;
        if (_stricmp(fileName.c_str(), "icon_Wing.dds") == 0)
            return IDR_ITEM_ICON_WING;

        return 0;
    }

    LPDIRECT3DTEXTURE9 load_item_icon_resource_texture(const std::string& fileName)
    {
        if (!g_device)
            return nullptr;

        auto resourceId = get_item_icon_atlas_resource_id(fileName);
        if (!resourceId)
            return nullptr;

        auto module = GetModuleHandleA("sdev.dll");
        if (!module)
            module = GetModuleHandleA(nullptr);

        auto resource = FindResourceA(module, MAKEINTRESOURCEA(resourceId), MAKEINTRESOURCEA(10));
        if (!resource)
            return nullptr;

        auto resourceHandle = LoadResource(module, resource);
        auto resourceData = resourceHandle ? LockResource(resourceHandle) : nullptr;
        auto resourceSize = SizeofResource(module, resource);
        if (!resourceData || !resourceSize)
            return nullptr;

        return create_texture_from_image_memory(g_device, resourceData, resourceSize);
    }

    LPDIRECT3DTEXTURE9 get_or_load_item_icon_atlas_texture(const std::string& fileName, int width, int height)
    {
        auto& entry = g_itemIconAtlases[fileName];
        if (entry.fileName.empty())
        {
            entry.fileName = fileName;
            entry.width = width;
            entry.height = height;
        }

        if (!entry.loadAttempted)
        {
            entry.loadAttempted = true;
            entry.texture = load_item_icon_resource_texture(fileName);
        }

        return entry.texture;
    }

    void draw_item_icon_at(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, int type, int typeId)
    {
        if (!drawList)
            return;

        auto iconIndex = resolve_item_icon_index(type, typeId);
        std::string atlasFileName;
        int atlasCols = 0;
        int atlasRows = 0;
        int atlasWidth = 0;
        int atlasHeight = 0;
        if (get_item_icon_atlas_config(type, atlasFileName, atlasCols, atlasRows, atlasWidth, atlasHeight))
        {
            if (auto atlasTexture = get_or_load_item_icon_atlas_texture(atlasFileName, atlasWidth, atlasHeight))
            {
                if (iconIndex > 0)
                {
                    auto atlasIndex = iconIndex - 1;
                    if (atlasIndex >= 0 && atlasIndex < atlasCols * atlasRows)
                    {
                        auto col = static_cast<float>(atlasIndex % atlasCols);
                        auto row = static_cast<float>(atlasIndex / atlasCols);
                        ImVec2 uvMin(col / static_cast<float>(atlasCols), row / static_cast<float>(atlasRows));
                        ImVec2 uvMax((col + 1.0f) / static_cast<float>(atlasCols), (row + 1.0f) / static_cast<float>(atlasRows));
                        drawList->AddImage(reinterpret_cast<ImTextureID>(atlasTexture), min, max, uvMin, uvMax);
                        return;
                    }
                }
            }
        }

        drawList->AddRectFilled(min, max, IM_COL32(72, 54, 34, 230), 3.0f);
        drawList->AddRect(min, max, IM_COL32(170, 135, 78, 210), 3.0f);
    }

    void draw_item_count_badge(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, int count)
    {
        if (!drawList || count <= 1)
            return;

        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%d", count);
        auto textSize = ImGui::CalcTextSize(buffer);
        auto textPos = ImVec2(max.x - textSize.x - 3.0f, max.y - textSize.y - 1.0f);
        drawList->AddText(ImVec2(textPos.x - 1.0f, textPos.y), IM_COL32(0, 0, 0, 210), buffer);
        drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y), IM_COL32(0, 0, 0, 210), buffer);
        drawList->AddText(ImVec2(textPos.x, textPos.y - 1.0f), IM_COL32(0, 0, 0, 210), buffer);
        drawList->AddText(ImVec2(textPos.x, textPos.y + 1.0f), IM_COL32(0, 0, 0, 210), buffer);
        drawList->AddText(textPos, IM_COL32(255, 244, 210, 255), buffer);
    }

    const char* roulette_result_label(uint8_t result)
    {
        switch (static_cast<GameRouletteResult>(result))
        {
        case GameRouletteResult::NoToken:
            return "Not enough tokens";
        case GameRouletteResult::InventoryFull:
            return "Inventory is full";
        case GameRouletteResult::Busy:
            return "Roulette is already spinning";
        case GameRouletteResult::NotConfigured:
            return "Roulette is not configured";
        case GameRouletteResult::Failure:
            return "Could not complete the roll";
        default:
            return "";
        }
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

        roulette_event::lastResult = 0;
        roulette_event::lastSpinSuccess = false;
        roulette_event::lastGrantSuccess = false;
        g_lastRouletteRollTick = GetTickCount();
        release_imgui_capture();
    }

    void update_roulette_spin_state()
    {
        if (!roulette_event::spinActive)
            return;

        auto now = GetTickCount();
        auto duration = roulette_event::spinDurationMs ? roulette_event::spinDurationMs : 4500;
        if (now - roulette_event::spinStartTick <= duration + 500)
            return;

        roulette_event::spinActive = false;
        roulette_event::spinDurationMs = 0;
        release_imgui_capture();
    }

    bool draw_manual_panel_button(const char* label, bool enabled, const ImVec2& size)
    {
        auto drawList = ImGui::GetWindowDrawList();
        auto min = ImGui::GetCursorScreenPos();
        auto max = ImVec2(min.x + size.x, min.y + size.y);

        ImVec2 mousePos{};
        auto hasMousePos = get_overlay_mouse_pos_raw(mousePos);
        auto hovered = hasMousePos
            && mousePos.x >= min.x
            && mousePos.x < max.x
            && mousePos.y >= min.y
            && mousePos.y < max.y;

        auto mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        auto clicked = enabled && hovered && mouseDown && !g_rollMouseWasDown;
        g_rollMouseWasDown = mouseDown;

        auto color = IM_COL32(54, 65, 82, 235);
        if (!enabled)
            color = IM_COL32(38, 42, 50, 170);
        else if (hovered && mouseDown)
            color = IM_COL32(75, 92, 118, 245);
        else if (hovered)
            color = IM_COL32(64, 78, 100, 245);

        drawList->AddRectFilled(min, max, color, 4.0f);
        drawList->AddRect(min, max, enabled ? IM_COL32(118, 150, 190, 210) : IM_COL32(80, 85, 92, 150), 4.0f);

        auto textSize = ImGui::CalcTextSize(label);
        drawList->AddText(
            ImVec2(min.x + std::max(0.0f, (size.x - textSize.x) * 0.5f), min.y + std::max(0.0f, (size.y - textSize.y) * 0.5f)),
            enabled ? IM_COL32(245, 247, 250, 255) : IM_COL32(150, 154, 162, 220),
            label);

        ImGui::Dummy(size);
        if (clicked)
            release_imgui_capture();

        return clicked;
    }

    uint8_t roulette_item_count()
    {
        auto count = roulette_event::itemCount;
        if (count > roulette_event::rewardType.size())
            count = static_cast<uint8_t>(roulette_event::rewardType.size());
        return count;
    }

    // Forward declarations (defined later in file)
    void ensure_emoji_catalog_loaded();
    LPDIRECT3DTEXTURE9 load_roulette_bg_texture();

    void draw_roulette_wheel()
    {
        auto drawList = ImGui::GetWindowDrawList();
        auto origin = ImGui::GetCursorScreenPos();
        auto itemCount = roulette_item_count();
        auto width = ImGui::GetContentRegionAvail().x;
        // Square area for the PNG background — use width as both dimensions
        auto bgSize = width;
        auto height = bgSize;
        auto now = GetTickCount();

        ImGui::Dummy(ImVec2(width, height));

        // Try to draw the PNG background; fall back to solid dark rect
        ensure_emoji_catalog_loaded();
        auto bgTex = load_roulette_bg_texture();
        if (bgTex)
        {
            drawList->AddImage(
                reinterpret_cast<ImTextureID>(bgTex),
                origin,
                ImVec2(origin.x + bgSize, origin.y + bgSize),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, 255));
        }
        else
        {
            drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(16, 17, 20, 210), 6.0f);
            drawList->AddRect(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(96, 111, 132, 180), 6.0f);
        }

        if (!itemCount)
        {
            auto label = "Waiting for server rewards...";
            auto textSize = ImGui::CalcTextSize(label);
            drawList->AddText(ImVec2(origin.x + (width - textSize.x) * 0.5f, origin.y + (height - textSize.y) * 0.5f), IM_COL32(180, 185, 194, 230), label);
            return;
        }

        auto progress = static_cast<float>(roulette_event::rewardIndex);

        if (roulette_event::spinActive && roulette_event::spinDurationMs > 0)
        {
            auto t = static_cast<float>(now - roulette_event::spinStartTick) / static_cast<float>(roulette_event::spinDurationMs);
            t = std::clamp(t, 0.0f, 1.0f);
            auto eased = 1.0f - std::pow(1.0f - t, 3.0f);
            progress = eased * (static_cast<float>(itemCount) * 5.0f + static_cast<float>(roulette_event::rewardIndex));
        }

        // Center the wheel within the PNG's circular area
        // The PNG's circle is roughly centered with ~4% top offset for the ornament
        auto centerX = origin.x + bgSize * 0.5f;
        auto centerY = origin.y + bgSize * 0.5f + bgSize * 0.02f;
        constexpr auto kPi = 3.1415926535f;
        // Items sized relative to the background
        auto slotSize = itemCount > 12 ? (bgSize * 0.07f) : (bgSize * 0.09f);
        slotSize = std::max(slotSize, 24.0f);
        // Wheel radius fits inside the PNG's circular track (~38% of bg size)
        auto wheelRadius = bgSize * 0.34f;
        wheelRadius = std::max(wheelRadius, slotSize * 1.8f);
        auto step = (kPi * 2.0f) / static_cast<float>(itemCount);

        auto mousePos = ImGui::GetMousePos();
        int hoveredSlot = -1;

        drawList->PushClipRect(origin, ImVec2(origin.x + width, origin.y + height), true);

        // When using PNG background, skip drawing the circles (PNG provides them)
        if (!bgTex)
        {
            drawList->AddCircleFilled(ImVec2(centerX, centerY), wheelRadius + 30.0f, IM_COL32(31, 34, 42, 230), 48);
            drawList->AddCircle(ImVec2(centerX, centerY), wheelRadius + 30.0f, IM_COL32(116, 134, 164, 190), 48, 2.0f);
            drawList->AddCircleFilled(ImVec2(centerX, centerY), 26.0f, IM_COL32(22, 24, 29, 240), 32);
            drawList->AddCircle(ImVec2(centerX, centerY), 26.0f, IM_COL32(212, 178, 96, 230), 32, 2.0f);
        }

        for (auto i = 0; i < itemCount; ++i)
        {
            auto angle = -kPi * 0.5f + (static_cast<float>(i) - progress) * step;
            auto slotCenter = ImVec2(centerX + std::cos(angle) * wheelRadius, centerY + std::sin(angle) * wheelRadius);
            ImVec2 iconMin(slotCenter.x - slotSize * 0.5f, slotCenter.y - slotSize * 0.5f);
            ImVec2 iconMax(iconMin.x + slotSize, iconMin.y + slotSize);

            auto alignment = std::fabs(std::atan2(std::sin(angle + kPi * 0.5f), std::cos(angle + kPi * 0.5f)));
            auto selected = alignment < step * 0.45f;

            // Hover detection for this slot
            bool slotHovered = mousePos.x >= iconMin.x && mousePos.x <= iconMax.x
                            && mousePos.y >= iconMin.y && mousePos.y <= iconMax.y;
            if (slotHovered)
                hoveredSlot = i;

            if (!bgTex)
                drawList->AddLine(ImVec2(centerX, centerY), slotCenter, IM_COL32(78, 88, 108, 135), 1.0f);
            auto bgColor = slotHovered ? IM_COL32(80, 88, 108, 250) :
                           (selected ? IM_COL32(65, 72, 88, 250) : IM_COL32(43, 48, 58, 235));
            auto borderColor = slotHovered ? IM_COL32(255, 230, 140, 255) :
                               (selected ? IM_COL32(255, 219, 118, 245) : IM_COL32(132, 150, 178, 220));
            auto borderWidth = (slotHovered || selected) ? 2.0f : 1.0f;
            drawList->AddRectFilled(iconMin, iconMax, bgColor, 5.0f);
            drawList->AddRect(iconMin, iconMax, borderColor, 5.0f, 0, borderWidth);
            draw_item_icon_at(drawList, iconMin, iconMax, roulette_event::rewardType[i], roulette_event::rewardTypeId[i]);
            draw_item_count_badge(drawList, iconMin, iconMax, roulette_event::rewardCount[i]);
        }
        drawList->PopClipRect();

        // Only draw the triangle pointer when no PNG background (PNG has its own top ornament)
        if (!bgTex)
        {
            drawList->AddTriangleFilled(
                ImVec2(centerX, origin.y + 13.0f),
                ImVec2(centerX - 9.0f, origin.y + 30.0f),
                ImVec2(centerX + 9.0f, origin.y + 30.0f),
                IM_COL32(255, 226, 126, 255));
        }

        if (static_cast<int32_t>(now - roulette_event::celebrationUntilTick) < 0)
        {
            for (int i = 0; i < 14; ++i)
            {
                auto angle = (static_cast<float>(i) / 14.0f) * kPi * 2.0f + static_cast<float>(now) * 0.006f;
                auto start = ImVec2(centerX + std::cos(angle) * 28.0f, centerY + std::sin(angle) * 28.0f);
                auto end = ImVec2(centerX + std::cos(angle) * (wheelRadius + 38.0f), centerY + std::sin(angle) * (wheelRadius + 38.0f));
                drawList->AddLine(start, end, IM_COL32(255, 214, 92, 185), 2.0f);
            }
        }

        // Tooltip on hovered wheel item
        if (hoveredSlot >= 0 && hoveredSlot < itemCount)
        {
            auto type = roulette_event::rewardType[hoveredSlot];
            auto typeId = roulette_event::rewardTypeId[hoveredSlot];
            auto count = roulette_event::rewardCount[hoveredSlot] ? roulette_event::rewardCount[hoveredSlot] : 1;
            auto chance = roulette_event::rewardChance[hoveredSlot];
            auto* itemInfo = CDataFile::GetItemInfo(type, typeId);
            auto itemName = (itemInfo && itemInfo->name) ? itemInfo->name : "Reward";
            auto itemDesc = (itemInfo && itemInfo->description && itemInfo->description[0]) ? itemInfo->description : nullptr;

            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(220.0f, FLT_MAX));
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(220.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.92f, 0.60f, 1.0f));
            ImGui::Text("%s", itemName);
            ImGui::PopStyleColor();
            if (itemDesc)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.78f, 0.80f, 0.82f, 1.0f));
                ImGui::TextUnformatted(itemDesc);
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
            if (count > 1)
                ImGui::Text("Quantity: %d", count);
            ImGui::TextDisabled("Chance: %.2f%%", static_cast<float>(chance) / 100.0f);
            ImGui::EndTooltip();
        }
    }

    // draw_roulette_reward_cell and draw_roulette_reward_grid removed — tooltips on wheel items

    void draw_roulette_section()
    {
        auto now = GetTickCount();
        if (is_game_scene() && (!roulette_event::listReceived || now - g_lastRouletteListTick > 15000))
            request_roulette_list();

        update_roulette_spin_state();

        auto tokenType = roulette_event::tokenType;
        auto tokenTypeId = roulette_event::tokenTypeId;
        auto requiredTokenCount = roulette_event::tokenCount ? roulette_event::tokenCount : 1;
        auto tokenCount = tokenType ? count_inventory_item(tokenType, tokenTypeId) : 0;
        auto itemCount = roulette_item_count();
        auto canRoll = is_game_scene() && roulette_event::hasList && itemCount > 0 && tokenCount >= requiredTokenCount && !roulette_event::spinActive;

        // Token info is now shown in the panel header
        draw_roulette_wheel();

        // Invisible roll button — fixed position, inset from edges
        auto drawList = ImGui::GetWindowDrawList();
        auto wheelOrigin = ImGui::GetCursorScreenPos();
        auto availW = ImGui::GetContentRegionAvail().x;
        auto btnW = availW - kRollButtonInset * 2.0f;
        auto btnPos = ImVec2(wheelOrigin.x + kRollButtonOffsetX + kRollButtonInset, wheelOrigin.y + kRollButtonOffsetY);
        auto btnMax = ImVec2(btnPos.x + btnW, btnPos.y + kRollButtonH);

        auto mousePos = ImGui::GetMousePos();
        auto btnHovered = mousePos.x >= btnPos.x && mousePos.x <= btnMax.x
                       && mousePos.y >= btnPos.y && mousePos.y <= btnMax.y;
        auto leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        auto leftPressed = leftDown && !g_rollMouseWasDown;

        // Subtle golden glow on hover, brighter on click
        if (btnHovered && leftDown)
        {
            drawList->AddRectFilled(btnPos, btnMax,
                IM_COL32(255, 210, 90, 120), 4.0f);
        }
        else if (btnHovered)
        {
            auto alpha = static_cast<int>(60.0f + 30.0f * std::sin(static_cast<float>(now) * 0.004f));
            drawList->AddRectFilled(btnPos, btnMax,
                IM_COL32(255, 220, 120, alpha), 4.0f);
        }

        if (btnHovered && leftPressed && canRoll)
            request_roulette_spin();
    }

    // Module system removed — roulette is drawn directly

    void draw_panel_header(bool& panelOpen)
    {
        constexpr auto kHeaderHeight = 26.0f;
        auto drawList = ImGui::GetWindowDrawList();
        auto min = ImGui::GetCursorScreenPos();
        auto width = ImGui::GetContentRegionAvail().x;
        auto max = ImVec2(min.x + width, min.y + kHeaderHeight);
        drawList->AddRectFilled(min, max, IM_COL32(24, 27, 31, 235), 4.0f);
        g_panelDragRect.left = static_cast<LONG>(min.x);
        g_panelDragRect.top = static_cast<LONG>(min.y);
        g_panelDragRect.right = static_cast<LONG>(max.x);
        g_panelDragRect.bottom = static_cast<LONG>(max.y);

        // Token count left-aligned: "Token:" in default color, count in gold
        if (roulette_event::hasList)
        {
            auto requiredCount = roulette_event::tokenCount ? roulette_event::tokenCount : 1;
            auto haveCount = roulette_event::tokenType ? count_inventory_item(roulette_event::tokenType, roulette_event::tokenTypeId) : 0;
            auto labelY = min.y + std::max(0.0f, (kHeaderHeight - ImGui::GetTextLineHeight()) * 0.5f);
            ImGui::SetCursorScreenPos(ImVec2(min.x + 6.0f, labelY));
            ImGui::TextUnformatted("Token:");
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.86f, 0.38f, 1.0f));
            ImGui::Text("%d", haveCount);
            ImGui::PopStyleColor();
        }

        // "Roulette" title centered
        auto titleSize = ImGui::CalcTextSize("Roulette");
        drawList->AddText(
            ImVec2(min.x + (width - titleSize.x) * 0.5f,
                   min.y + (kHeaderHeight - titleSize.y) * 0.5f),
            IM_COL32(235, 238, 244, 255), "Roulette");

        // Close button
        ImGui::SetCursorScreenPos(ImVec2(max.x - 24.0f, min.y + 3.0f));
        if (ImGui::Button("X", ImVec2(20.0f, 20.0f)))
            panelOpen = false;

        // Drag handling
        ImVec2 mousePos{};
        auto hasMousePos = get_overlay_mouse_pos_raw(mousePos);
        auto mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        auto mousePressed = mouseDown && !g_panelMouseWasDown;
        auto mouseReleased = !mouseDown && g_panelMouseWasDown;

        if (hasMousePos && mousePressed && is_pos_in_rect_raw(mousePos, g_panelDragRect))
        {
            g_draggingPanel = true;
            g_draggedPanel = false;
            g_panelDragOffset = ImVec2(mousePos.x - g_panelPosition.x, mousePos.y - g_panelPosition.y);
            release_imgui_capture();
        }

        if (g_draggingPanel && mouseDown && hasMousePos)
        {
            auto displaySize = ImGui::GetIO().DisplaySize;
            auto newPosition = ImVec2(mousePos.x - g_panelDragOffset.x, mousePos.y - g_panelDragOffset.y);
            newPosition = ImVec2(
                std::clamp(newPosition.x, 8.0f, std::max(8.0f, displaySize.x - kPanelFixedW - 8.0f)),
                std::clamp(newPosition.y, 8.0f, std::max(8.0f, displaySize.y - kPanelFixedH - 8.0f)));

            if (std::fabs(newPosition.x - g_panelPosition.x) >= 0.5f || std::fabs(newPosition.y - g_panelPosition.y) >= 0.5f)
            {
                g_panelPosition = newPosition;
                ImGui::SetWindowPos(g_panelPosition);
                g_draggedPanel = true;
            }
        }

        if (mouseReleased && g_draggingPanel)
        {
            if (g_draggedPanel)
                save_imgui_settings();

            g_draggingPanel = false;
            g_draggedPanel = false;
        }

        g_panelMouseWasDown = mouseDown;
        ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 2.0f));
    }

    void draw_panel_shell()
    {
        ImGui::SetNextWindowBgAlpha(0.94f);
        ImGui::SetNextWindowPos(g_panelPosition, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(kPanelFixedW, kPanelFixedH), ImGuiCond_Always);

        bool panelOpen = g_showPanel;
        if (ImGui::Begin(
            "##roulette_panel",
            &panelOpen,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            auto windowPos = ImGui::GetWindowPos();
            g_panelPosition = windowPos;
            g_panelWindowRect.left = static_cast<LONG>(windowPos.x);
            g_panelWindowRect.top = static_cast<LONG>(windowPos.y);
            g_panelWindowRect.right = static_cast<LONG>(windowPos.x + kPanelFixedW);
            g_panelWindowRect.bottom = static_cast<LONG>(windowPos.y + kPanelFixedH);

            draw_panel_header(panelOpen);

            ImGui::BeginChild("##roulette_body", ImVec2(0.0f, 0.0f), false);
            draw_roulette_section();
            ImGui::EndChild();
        }
        ImGui::End();
        g_showPanel = panelOpen;
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
        if (!is_game_scene())
        {
            g_waitingWelcomeMessage = false;
            g_welcomeStartTick = 0;
            return;
        }

        if (g_sentWelcomeMessage)
            return;

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

        Static::SysMsgToChatBox(ChatType::Notice25, 12001, 12);
        g_sentWelcomeMessage = true;
        g_waitingWelcomeMessage = false;
    }

    bool is_cursor_in_rect(const RECT& rect)
    {
        if (rect.left == rect.right || rect.top == rect.bottom)
            return false;

        // Use ImGui's already-cached mouse position instead of syscalls.
        auto& io = ImGui::GetIO();
        if (io.MousePos.x < 0.0f || io.MousePos.y < 0.0f)
            return false;

        POINT point;
        point.x = static_cast<LONG>(io.MousePos.x);
        point.y = static_cast<LONG>(io.MousePos.y);
        return PtInRect(&rect, point) == TRUE;
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

    void handle_emoji_button_interaction()
    {
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            g_showEmojiPicker = !g_showEmojiPicker;
    }

    constexpr const char* kEmojiSahFolder   = "assets\\emojis";
    constexpr const char* kGifSahFolder     = "assets\\gifs";
    constexpr const char* kGeneralSahFolder = "assets\\general";
    constexpr PROPID kGdiplusFrameDelayProperty = 0x5100;

    std::string get_game_relative_path(const char* relativePath)
    {
        char modulePath[MAX_PATH]{};
        if (GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath)) == 0)
            return relativePath ? relativePath : "";

        auto path = std::string(modulePath);
        auto slash = path.find_last_of("\\/");
        if (slash == std::string::npos)
            return relativePath ? relativePath : "";

        path.resize(slash + 1);
        if (relativePath)
            path += relativePath;

        return path;
    }

    std::string to_lower_ascii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    bool read_sah_u32(const std::vector<char>& data, std::size_t& offset, uint32_t& value)
    {
        if (offset + sizeof(value) > data.size())
            return false;

        std::memcpy(&value, data.data() + offset, sizeof(value));
        offset += sizeof(value);
        return true;
    }

    bool read_sah_u64(const std::vector<char>& data, std::size_t& offset, uint64_t& value)
    {
        if (offset + sizeof(value) > data.size())
            return false;

        std::memcpy(&value, data.data() + offset, sizeof(value));
        offset += sizeof(value);
        return true;
    }

    bool read_sah_string(const std::vector<char>& data, std::size_t& offset, std::string& value)
    {
        uint32_t length = 0;
        if (!read_sah_u32(data, offset, length) || offset + length > data.size())
            return false;

        value.assign(data.data() + offset, data.data() + offset + length);
        while (!value.empty() && value.back() == '\0')
            value.pop_back();

        offset += length;
        return true;
    }

    bool parse_visual_token_file_name(const char* fileName, const char* prefix, const char* extension, int& index)
    {
        auto prefixLength = std::strlen(prefix);
        if (!fileName || _strnicmp(fileName, prefix, prefixLength) != 0)
            return false;

        auto* current = fileName + prefixLength;
        if (*current < '1' || *current > '9')
            return false;

        auto parsed = 0;
        while (*current >= '0' && *current <= '9')
        {
            parsed = parsed * 10 + (*current - '0');
            ++current;
        }

        if (parsed <= 0 || _stricmp(current, extension) != 0)
            return false;

        index = parsed;
        return true;
    }

    const char* visual_token_prefix(VisualTokenKind kind)
    {
        return kind == VisualTokenKind::Gif ? "gif" : "emoji";
    }

    bool is_visual_token_enabled(VisualTokenKind kind)
    {
        return kind == VisualTokenKind::Gif ? g_gifsEnabled : g_emojisEnabled;
    }

    bool match_visual_token_text(const char* text, VisualTokenKind& kind, std::size_t& tokenLength)
    {
        if (!text || text[0] != ':')
            return false;

        struct TokenPattern
        {
            VisualTokenKind kind;
            const char* prefix;
        };

        constexpr TokenPattern kPatterns[] = {
            { VisualTokenKind::Emoji, "emoji" },
            { VisualTokenKind::Gif, "gif" }
        };

        for (auto& pattern : kPatterns)
        {
            auto prefixLength = std::strlen(pattern.prefix);
            if (_strnicmp(text + 1, pattern.prefix, prefixLength) != 0)
                continue;

            auto* current = text + 1 + prefixLength;
            if (*current < '1' || *current > '9')
                continue;

            do
            {
                ++current;
            } while (*current >= '0' && *current <= '9');

            if (*current != ':')
                continue;

            kind = pattern.kind;
            tokenLength = static_cast<std::size_t>(current - text + 1);
            return true;
        }

        return false;
    }

    bool has_visual_token_index(VisualTokenKind kind, int index)
    {
        return std::find_if(g_emojis.begin(), g_emojis.end(), [kind, index](const EmojiEntry& emoji) {
            return emoji.kind == kind && emoji.index == index;
        }) != g_emojis.end();
    }

    bool is_sah_visual_token_folder(const std::string& lowerPath, VisualTokenKind kind)
    {
        auto folder = kind == VisualTokenKind::Gif ? kGifSahFolder : kEmojiSahFolder;
        return lowerPath == folder || lowerPath == std::string("data\\") + folder;
    }

    bool is_sah_general_folder(const std::string& lowerPath)
    {
        return lowerPath == kGeneralSahFolder
            || lowerPath == std::string("data\\") + kGeneralSahFolder;
    }

    bool try_add_visual_token_from_sah_file(VisualTokenKind kind, const std::string& fileName, uint64_t fileOffset, uint64_t fileSize)
    {
        auto index = 0;
        auto prefix = visual_token_prefix(kind);
        auto extension = kind == VisualTokenKind::Gif ? ".gif" : ".png";
        if (!parse_visual_token_file_name(fileName.c_str(), prefix, extension, index) || has_visual_token_index(kind, index))
            return false;

        char token[32]{};
        std::snprintf(token, sizeof(token), ":%s%d:", prefix, index);
        g_emojis.push_back({ kind, index, token, fileName, fileOffset, fileSize, nullptr, nullptr, {}, false, false, 0 });
        return true;
    }

    bool scan_sah_directory_for_visual_tokens(const std::vector<char>& data, std::size_t& offset, const std::string& parentPath)
    {
        std::string name;
        if (!read_sah_string(data, offset, name))
            return false;

        auto path = parentPath;
        if (!name.empty())
            path = path.empty() ? name : path + "\\" + name;

        uint32_t fileCount = 0;
        if (!read_sah_u32(data, offset, fileCount))
            return false;

        auto lowerPath = to_lower_ascii(path);
        for (uint32_t i = 0; i < fileCount; ++i)
        {
            std::string fileName;
            uint64_t fileOffset = 0;
            uint64_t fileSize = 0;
            if (!read_sah_string(data, offset, fileName)
                || !read_sah_u64(data, offset, fileOffset)
                || !read_sah_u64(data, offset, fileSize))
            {
                return false;
            }

            if (is_sah_visual_token_folder(lowerPath, VisualTokenKind::Emoji))
            {
                try_add_visual_token_from_sah_file(VisualTokenKind::Emoji, fileName, fileOffset, fileSize);
            }
            else if (is_sah_visual_token_folder(lowerPath, VisualTokenKind::Gif))
            {
                try_add_visual_token_from_sah_file(VisualTokenKind::Gif, fileName, fileOffset, fileSize);
            }
            else if (is_sah_general_folder(lowerPath))
            {
                // Roulette background PNG lives in Assets/General.
                auto lowerFileName = to_lower_ascii(fileName);
                if (lowerFileName == "roulette.png" && !g_rouletteBgFound)
                {
                    g_rouletteBgFound = true;
                    g_rouletteBgDataOffset = fileOffset;
                    g_rouletteBgDataSize = fileSize;
                }
            }
        }

        uint32_t directoryCount = 0;
        if (!read_sah_u32(data, offset, directoryCount))
            return false;

        for (uint32_t i = 0; i < directoryCount; ++i)
        {
            if (!scan_sah_directory_for_visual_tokens(data, offset, path))
                return false;
        }

        return true;
    }

    void ensure_emoji_catalog_loaded()
    {
        if (g_emojiCatalogLoaded)
            return;

        g_emojiCatalogLoaded = true;
        auto sahPath = get_game_relative_path("data.sah");
        std::ifstream stream(sahPath, std::ios::binary);
        if (!stream)
            return;

        std::vector<char> data(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>());
        if (data.size() <= 0x34 || std::memcmp(data.data(), "SAH", 3) != 0)
            return;

        auto offset = std::size_t{ 0x34 };
        scan_sah_directory_for_visual_tokens(data, offset, "");

        std::sort(g_emojis.begin(), g_emojis.end(), [](const EmojiEntry& lhs, const EmojiEntry& rhs) {
            if (lhs.kind != rhs.kind)
                return lhs.kind < rhs.kind;
            return lhs.index < rhs.index;
        });
    }

    // ---------------------------------------------------------------------------
    // Texture loading via stb_image (PNG) and raw DDS parsing — no d3dx9 needed
    // ---------------------------------------------------------------------------

    // DDS header structures (subset needed for DXT-compressed icon atlases)
    #pragma pack(push, 1)
    struct DDS_PIXELFORMAT
    {
        DWORD dwSize;
        DWORD dwFlags;
        DWORD dwFourCC;
        DWORD dwRGBBitCount;
        DWORD dwRBitMask;
        DWORD dwGBitMask;
        DWORD dwBBitMask;
        DWORD dwABitMask;
    };

    struct DDS_HEADER
    {
        DWORD dwMagic;       // 'DDS '
        DWORD dwSize;        // 124
        DWORD dwFlags;
        DWORD dwHeight;
        DWORD dwWidth;
        DWORD dwPitchOrLinearSize;
        DWORD dwDepth;
        DWORD dwMipMapCount;
        DWORD dwReserved1[11];
        DDS_PIXELFORMAT ddspf;
        DWORD dwCaps;
        DWORD dwCaps2;
        DWORD dwCaps3;
        DWORD dwCaps4;
        DWORD dwReserved2;
    };
    #pragma pack(pop)

    constexpr DWORD kDdsMagic = 0x20534444; // 'DDS '
    constexpr DWORD kDdpfFourCC = 0x4;

    static DWORD make_fourcc(char a, char b, char c, char d)
    {
        return static_cast<DWORD>(a) | (static_cast<DWORD>(b) << 8)
            | (static_cast<DWORD>(c) << 16) | (static_cast<DWORD>(d) << 24);
    }

    LPDIRECT3DTEXTURE9 create_texture_from_dds_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize)
    {
        if (!device || !data || dataSize < sizeof(DDS_HEADER))
            return nullptr;

        auto* header = static_cast<const DDS_HEADER*>(data);
        if (header->dwMagic != kDdsMagic)
            return nullptr;

        auto width = header->dwWidth;
        auto height = header->dwHeight;
        if (width == 0 || height == 0)
            return nullptr;

        D3DFORMAT format = D3DFMT_UNKNOWN;
        UINT blockSize = 16;
        if (header->ddspf.dwFlags & kDdpfFourCC)
        {
            auto fourCC = header->ddspf.dwFourCC;
            if (fourCC == make_fourcc('D', 'X', 'T', '1'))      { format = D3DFMT_DXT1; blockSize = 8; }
            else if (fourCC == make_fourcc('D', 'X', 'T', '3')) { format = D3DFMT_DXT3; }
            else if (fourCC == make_fourcc('D', 'X', 'T', '5')) { format = D3DFMT_DXT5; }
        }

        if (format == D3DFMT_UNKNOWN)
            return nullptr;

        auto pixelDataOffset = static_cast<UINT>(sizeof(DWORD) + header->dwSize);
        if (pixelDataOffset >= dataSize)
            return nullptr;

        auto pixelDataSize = dataSize - pixelDataOffset;
        auto* pixelData = static_cast<const BYTE*>(data) + pixelDataOffset;

        // Compute expected size for mip level 0
        auto blocksWide = (width + 3) / 4;
        auto blocksHigh = (height + 3) / 4;
        auto expectedSize = blocksWide * blocksHigh * blockSize;
        if (pixelDataSize < expectedSize)
            return nullptr;

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (FAILED(device->CreateTexture(width, height, 1, 0, format, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
            return nullptr;

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            return nullptr;
        }

        // For block-compressed formats, copy row-of-blocks at a time
        auto srcPitch = blocksWide * blockSize;
        for (UINT row = 0; row < blocksHigh; ++row)
        {
            auto* dst = static_cast<BYTE*>(locked.pBits) + row * locked.Pitch;
            auto* src = pixelData + row * srcPitch;
            std::memcpy(dst, src, srcPitch);
        }

        texture->UnlockRect(0);
        return texture;
    }

    LPDIRECT3DTEXTURE9 create_texture_from_png_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize)
    {
        if (!device || !data || dataSize == 0)
            return nullptr;

        int width = 0, height = 0, channels = 0;
        auto* pixels = stbi_load_from_memory(
            static_cast<const stbi_uc*>(data),
            static_cast<int>(dataSize),
            &width, &height, &channels, 4); // force RGBA
        if (!pixels)
            return nullptr;

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (FAILED(device->CreateTexture(
            static_cast<UINT>(width), static_cast<UINT>(height),
            1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
        {
            stbi_image_free(pixels);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            stbi_image_free(pixels);
            return nullptr;
        }

        // stb_image outputs RGBA, D3D9 expects BGRA (A8R8G8B8) — swizzle R<->B
        for (int y = 0; y < height; ++y)
        {
            auto* src = pixels + y * width * 4;
            auto* dst = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
            for (int x = 0; x < width; ++x)
            {
                dst[x * 4 + 0] = src[x * 4 + 2]; // B
                dst[x * 4 + 1] = src[x * 4 + 1]; // G
                dst[x * 4 + 2] = src[x * 4 + 0]; // R
                dst[x * 4 + 3] = src[x * 4 + 3]; // A
            }
        }

        texture->UnlockRect(0);
        stbi_image_free(pixels);
        return texture;
    }

    // Auto-detect format by header and load accordingly (replaces D3DXCreateTextureFromFileInMemory)
    LPDIRECT3DTEXTURE9 create_texture_from_image_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize)
    {
        if (!device || !data || dataSize < 4)
            return nullptr;

        auto magic = *static_cast<const DWORD*>(data);

        // DDS: 'DDS ' magic
        if (magic == kDdsMagic)
            return create_texture_from_dds_memory(device, data, dataSize);

        // PNG / JPEG / BMP / TGA — anything stb_image supports
        return create_texture_from_png_memory(device, data, dataSize);
    }

    bool read_client_data_file(const EmojiEntry& emoji, std::vector<char>& fileData)
    {
        if (emoji.dataSize == 0 || emoji.dataSize > UINT_MAX)
            return false;

        auto safPath = get_game_relative_path("data.saf");
        std::ifstream stream(safPath, std::ios::binary);
        if (!stream)
            return false;

        stream.seekg(static_cast<std::streamoff>(emoji.dataOffset), std::ios::beg);
        if (!stream)
            return false;

        fileData.assign(static_cast<std::size_t>(emoji.dataSize), 0);
        stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        return stream.gcount() == static_cast<std::streamsize>(fileData.size());
    }

    LPDIRECT3DTEXTURE9 load_png_texture(EmojiEntry& emoji)
    {
        if (!g_device)
            return nullptr;

        std::vector<char> fileData;
        if (!read_client_data_file(emoji, fileData))
            return nullptr;

        return create_texture_from_image_memory(g_device, fileData.data(), static_cast<UINT>(fileData.size()));
    }

    LPDIRECT3DTEXTURE9 load_roulette_bg_texture()
    {
        if (g_rouletteBgLoadAttempted)
            return g_rouletteBgTexture;
        g_rouletteBgLoadAttempted = true;

        if (!g_rouletteBgFound || g_rouletteBgDataSize == 0 || g_rouletteBgDataSize > UINT_MAX)
            return nullptr;

        if (!g_device)
        {
            g_rouletteBgLoadAttempted = false; // retry later when device is ready
            return nullptr;
        }

        auto safPath = get_game_relative_path("data.saf");
        std::ifstream stream(safPath, std::ios::binary);
        if (!stream)
            return nullptr;

        stream.seekg(static_cast<std::streamoff>(g_rouletteBgDataOffset), std::ios::beg);
        if (!stream)
            return nullptr;

        std::vector<char> fileData(static_cast<std::size_t>(g_rouletteBgDataSize), 0);
        stream.read(fileData.data(), static_cast<std::streamsize>(fileData.size()));
        if (stream.gcount() != static_cast<std::streamsize>(fileData.size()))
            return nullptr;

        g_rouletteBgTexture = create_texture_from_image_memory(g_device, fileData.data(), static_cast<UINT>(fileData.size()));
        return g_rouletteBgTexture;
    }

    bool ensure_gdiplus_started()
    {
        if (g_gdiplusToken)
            return true;

        if (g_gdiplusStartAttempted)
            return false;

        g_gdiplusStartAttempted = true;
        Gdiplus::GdiplusStartupInput input;
        return Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) == Gdiplus::Ok;
    }

    DWORD read_gif_frame_delay_ms(Gdiplus::Bitmap& bitmap, UINT frameIndex)
    {
        auto propertySize = bitmap.GetPropertyItemSize(kGdiplusFrameDelayProperty);
        if (propertySize == 0)
            return 100;

        std::vector<BYTE> propertyData(propertySize);
        auto* property = reinterpret_cast<Gdiplus::PropertyItem*>(propertyData.data());
        if (bitmap.GetPropertyItem(kGdiplusFrameDelayProperty, propertySize, property) != Gdiplus::Ok)
            return 100;

        if (property->type != PropertyTagTypeLong || property->length < sizeof(UINT) * (frameIndex + 1))
            return 100;

        auto delays = static_cast<UINT*>(property->value);
        auto delayMs = static_cast<DWORD>(delays[frameIndex]) * 10;
        return delayMs >= 20 ? delayMs : 100;
    }

    LPDIRECT3DTEXTURE9 create_texture_from_argb_bitmap(Gdiplus::Bitmap& bitmap)
    {
        if (!g_device)
            return nullptr;

        auto width = bitmap.GetWidth();
        auto height = bitmap.GetHeight();
        if (width == 0 || height == 0)
            return nullptr;

        Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
        Gdiplus::BitmapData bitmapData{};
        if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bitmapData) != Gdiplus::Ok)
            return nullptr;

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (FAILED(g_device->CreateTexture(width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr)) || !texture)
        {
            bitmap.UnlockBits(&bitmapData);
            return nullptr;
        }

        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0)))
        {
            texture->Release();
            bitmap.UnlockBits(&bitmapData);
            return nullptr;
        }

        auto* sourceBase = static_cast<BYTE*>(bitmapData.Scan0);
        auto sourceStride = bitmapData.Stride;
        for (UINT y = 0; y < height; ++y)
        {
            auto* source = sourceStride >= 0
                ? sourceBase + y * sourceStride
                : sourceBase + (height - 1 - y) * (-sourceStride);
            auto* destination = static_cast<BYTE*>(locked.pBits) + y * locked.Pitch;
            std::memcpy(destination, source, width * 4);
        }

        texture->UnlockRect(0);
        bitmap.UnlockBits(&bitmapData);
        return texture;
    }

    LPDIRECT3DTEXTURE9 load_gif_preview_texture(EmojiEntry& emoji)
    {
        if (!ensure_gdiplus_started())
            return nullptr;

        std::vector<char> fileData;
        if (!read_client_data_file(emoji, fileData) || fileData.empty())
            return nullptr;

        auto memory = GlobalAlloc(GMEM_MOVEABLE, fileData.size());
        if (!memory)
            return nullptr;

        auto* destination = GlobalLock(memory);
        if (!destination)
        {
            GlobalFree(memory);
            return nullptr;
        }

        std::memcpy(destination, fileData.data(), fileData.size());
        GlobalUnlock(memory);

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)) || !stream)
        {
            GlobalFree(memory);
            return nullptr;
        }

        std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream));
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            bitmap.reset();
            stream->Release();
            return nullptr;
        }

        auto texture = create_texture_from_argb_bitmap(*bitmap);
        bitmap.reset();
        stream->Release();
        return texture;
    }

    void load_gif_frames(EmojiEntry& emoji)
    {
        if (!ensure_gdiplus_started())
            return;

        std::vector<char> fileData;
        if (!read_client_data_file(emoji, fileData) || fileData.empty())
            return;

        auto memory = GlobalAlloc(GMEM_MOVEABLE, fileData.size());
        if (!memory)
            return;

        auto* destination = GlobalLock(memory);
        if (!destination)
        {
            GlobalFree(memory);
            return;
        }
        std::memcpy(destination, fileData.data(), fileData.size());
        GlobalUnlock(memory);

        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)) || !stream)
        {
            GlobalFree(memory);
            return;
        }

        std::unique_ptr<Gdiplus::Bitmap> bitmap(Gdiplus::Bitmap::FromStream(stream));
        if (!bitmap || bitmap->GetLastStatus() != Gdiplus::Ok)
        {
            bitmap.reset();
            stream->Release();
            return;
        }

        auto dimensionCount = bitmap->GetFrameDimensionsCount();
        if (dimensionCount == 0)
        {
            bitmap.reset();
            stream->Release();
            return;
        }

        std::vector<GUID> dimensions(dimensionCount);
        if (bitmap->GetFrameDimensionsList(dimensions.data(), dimensionCount) != Gdiplus::Ok)
        {
            bitmap.reset();
            stream->Release();
            return;
        }

        auto frameCount = bitmap->GetFrameCount(&dimensions[0]);
        if (frameCount == 0)
        {
            bitmap.reset();
            stream->Release();
            return;
        }

        for (UINT i = 0; i < frameCount; ++i)
        {
            if (bitmap->SelectActiveFrame(&dimensions[0], i) != Gdiplus::Ok)
                continue;

            auto texture = create_texture_from_argb_bitmap(*bitmap);
            if (!texture)
                continue;

            emoji.frames.push_back({ texture, read_gif_frame_delay_ms(*bitmap, i) });
        }
        bitmap.reset();
        stream->Release();
    }

    LPDIRECT3DTEXTURE9 get_emoji_texture(EmojiEntry& emoji)
    {
        if (!emoji.loadAttempted)
        {
            if (emoji.kind == VisualTokenKind::Gif)
                load_gif_frames(emoji);
            else
                emoji.texture = load_png_texture(emoji);
            emoji.loadAttempted = true;
        }

        if (emoji.kind != VisualTokenKind::Gif)
            return emoji.texture;

        if (emoji.frames.empty())
            return nullptr;

        DWORD totalDelay = 0;
        for (auto& frame : emoji.frames)
            totalDelay += frame.delayMs ? frame.delayMs : 100;

        if (totalDelay == 0)
            return emoji.frames.front().texture;

        auto position = GetTickCount() % totalDelay;
        DWORD accumulated = 0;
        for (auto& frame : emoji.frames)
        {
            accumulated += frame.delayMs ? frame.delayMs : 100;
            if (position < accumulated)
                return frame.texture;
        }

        return emoji.frames.back().texture;
    }

    EmojiEntry* find_emoji_by_token(const char* text)
    {
        if (!text)
            return nullptr;

        ensure_emoji_catalog_loaded();
        for (auto& emoji : g_emojis)
        {
            auto tokenLength = emoji.token.size();
            if (tokenLength > 0 && std::strncmp(text, emoji.token.c_str(), tokenLength) == 0)
                return &emoji;
        }

        return nullptr;
    }

    bool is_lower_chat_type(int chatType)
    {
        // Upper bar: 15=orange, 16=red, 17=red, 18=yellow, 19=green, 20=violet, 21=light blue, 22=light green, 34=light grey
        if (chatType == 15 || chatType == 16 || chatType == 17 || chatType == 18
            || chatType == 19 || chatType == 20 || chatType == 21 || chatType == 22
            || chatType == 34)
            return false;

        // Lower chat: types 0-49
        return chatType >= 0 && chatType <= 49;
    }

    float measure_chat_prefix_width(const std::string& prefix)
    {
        if (prefix.empty())
            return 0.0f;

        using MeasureTextWidth = int(__thiscall*)(void*, const char*, int, int);
        auto measureTextWidth = reinterpret_cast<MeasureTextWidth>(0x575740);

        __try
        {
            auto width = measureTextWidth(
                reinterpret_cast<void*>(0x22B69B0),
                prefix.c_str(),
                static_cast<int>(prefix.size()),
                0);

            if (width > 0)
                return static_cast<float>(width);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }

        auto fallbackWidth = 0.0f;
        for (auto ch : prefix)
            fallbackWidth += ch == '\t' ? 24.0f : 5.5f;

        return fallbackWidth;
    }

    bool sanitize_visual_tokens(const char* text, std::string& output, ChatEmojiLineOverlay& line, int replacementSpaces)
    {
        if (!text || text[0] == '\0')
            return false;

        auto changed = false;
        auto textLength = std::strlen(text);
        output.clear();
        output.reserve(textLength);
        line = {};
        line.tick = GetTickCount();

        for (auto index = std::size_t{ 0 }; index < textLength;)
        {
            auto emoji = find_emoji_by_token(text + index);
            if (emoji)
            {
                if (is_visual_token_enabled(emoji->kind))
                {
                    line.tokens.push_back({ emoji, measure_chat_prefix_width(output) });
                    output.append(replacementSpaces, ' ');
                }

                index += emoji->token.size();
                changed = true;
                continue;
            }

            VisualTokenKind tokenKind{};
            std::size_t tokenLength = 0;
            if (match_visual_token_text(text + index, tokenKind, tokenLength) && !is_visual_token_enabled(tokenKind))
            {
                index += tokenLength;
                changed = true;
                continue;
            }

            output.push_back(text[index]);
            ++index;
        }

        return changed;
    }

    int estimate_visual_lines(const char* text)
    {
        if (!text || text[0] == '\0' || !g_var)
            return 1;

        auto len = std::strlen(text);
        auto textWidth = measure_chat_prefix_width(std::string(text, len));
        auto clientW = static_cast<float>(g_var->client.width);
        // Conservative margin: the game's word-wrap width is slightly tighter than the
        // geometric chat area, so subtract extra padding to avoid underestimating lines.
        auto chatAreaWidth = clientW * g_tune.chatRightPct - g_tune.chatTextX - 14.0f;
        if (chatAreaWidth < 50.0f) chatAreaWidth = 50.0f;

        auto lines = static_cast<int>(std::ceil(textWidth / chatAreaWidth));
        if (lines < 1) lines = 1;
        if (lines > 5) lines = 5;
        return lines;
    }

    void remember_chat_emoji_line(int chatType, const char* text, ChatEmojiLineOverlay&& line)
    {
        if (!is_lower_chat_type(chatType) || !text || text[0] == '\0')
            return;

        auto vLines = estimate_visual_lines(text);

        std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
        g_lowerChatEmojiLines.push_back({ text, std::move(line.tokens), vLines });
        while (g_lowerChatEmojiLines.size() > 100)
            g_lowerChatEmojiLines.pop_front();

        // Auto-scroll to bottom when new message arrives (like the game does)
        g_chatScrollOffset = 0;
    }

    bool matches_sanitized_static_text(const char* text, const std::string& expected, std::size_t* outPrefixLen = nullptr)
    {
        if (outPrefixLen)
            *outPrefixLen = 0;

        if (!text || expected.empty())
            return false;

        auto textLength = std::strlen(text);
        auto expLen = expected.size();

        if (textLength == 0 || expLen == 0)
            return false;

        if (textLength <= expLen)
        {
            if (std::strncmp(text, expected.c_str(), textLength) != 0)
                return false;
            for (auto i = textLength; i < expLen; ++i)
            {
                if (expected[i] != ' ')
                    return false;
            }
            return true;
        }

        auto found = std::strstr(text, expected.c_str());
        if (found)
        {
            if (outPrefixLen)
                *outPrefixLen = static_cast<std::size_t>(found - text);
            return true;
        }

        auto nonSpaceEnd = expLen;
        while (nonSpaceEnd > 0 && expected[nonSpaceEnd - 1] == ' ')
            --nonSpaceEnd;

        if (nonSpaceEnd >= 4)
        {
            std::string trimmed = expected.substr(0, nonSpaceEnd);
            found = std::strstr(text, trimmed.c_str());
            if (found)
            {
                if (outPrefixLen)
                    *outPrefixLen = static_cast<std::size_t>(found - text);
                return true;
            }
        }

        return false;
    }

    bool safe_matches_sanitized_static_text(const char* text, const std::string& expected, std::size_t* outPrefixLen = nullptr)
    {
        __try
        {
            return matches_sanitized_static_text(text, expected, outPrefixLen);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const char* __cdecl prepare_chat_text_for_emojis(int chatType, const char* text)
    {
        // Record every chat type for the GM debug panel
        debug_panel::record_chat_type(chatType, text);

        if (!text || text[0] == '\0')
            return text;

        ChatEmojiLineOverlay line{};
        auto changed = sanitize_visual_tokens(text, g_sanitizedChatText, line, 5);

        if (!changed)
        {
            ChatEmojiLineOverlay emptyLine{};
            remember_chat_emoji_line(chatType, text, std::move(emptyLine));
            return text;
        }

        remember_chat_emoji_line(chatType, g_sanitizedChatText.c_str(), std::move(line));
        return g_sanitizedChatText.c_str();
    }

    const char* prepare_text_for_emojis(const char* text, bool pushStaticCreateContext)
    {
        if (!text || text[0] == '\0')
            return text;

        ChatEmojiLineOverlay line{};
        auto changed = sanitize_visual_tokens(text, g_sanitizedFloatingText, line, 4);
        auto* result = changed ? g_sanitizedFloatingText.c_str() : text;

        if (!changed)
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            for (auto it = g_lowerChatEmojiLines.rbegin(); it != g_lowerChatEmojiLines.rend(); ++it)
            {
                std::size_t prefixLen = 0;
                if (matches_sanitized_static_text(text, it->text, &prefixLen))
                {
                    line.tick = GetTickCount();
                    line.tokens = it->tokens;

                    if (prefixLen > 0)
                    {
                        std::string prefix(text, prefixLen);
                        auto prefixWidth = measure_chat_prefix_width(prefix);
                        for (auto& token : line.tokens)
                            token.xOffset += prefixWidth;
                    }
                    break;
                }
            }
        }

        if (pushStaticCreateContext)
        {
            g_staticTextCreateStack.push_back(std::move(line));
        }
        else
        {
            std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
            g_hasPendingFloatingEmojiLine = !line.tokens.empty();
            if (g_hasPendingFloatingEmojiLine)
                g_pendingFloatingEmojiLine = std::move(line);
        }

        return result;
    }

    const char* __cdecl prepare_static_text_for_emojis(const char* text)
    {
        return prepare_text_for_emojis(text, true);
    }

    constexpr std::size_t kMaxBubbleTextLength = 30;

    const char* __cdecl prepare_floating_text_for_emojis(const char* text)
    {
        // Skip emoji rendering in bubbles for long messages — they wrap and look broken
        if (text && std::strlen(text) > kMaxBubbleTextLength)
            return text;
        return prepare_text_for_emojis(text, false);
    }

    void __cdecl capture_floating_static_text(void* staticText)
    {
        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        if (!g_hasPendingFloatingEmojiLine)
            return;

        if (staticText)
            g_floatingEmojiLines[staticText] = g_pendingFloatingEmojiLine;

        g_pendingFloatingEmojiLine = {};
        g_hasPendingFloatingEmojiLine = false;
    }

    void __cdecl capture_created_static_text(void* staticText)
    {
        if (g_staticTextCreateStack.empty())
        {
            capture_floating_static_text(staticText);
            return;
        }

        auto line = std::move(g_staticTextCreateStack.back());
        g_staticTextCreateStack.pop_back();

        if (!staticText || line.tokens.empty())
            return;

        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        g_floatingEmojiLines[staticText] = std::move(line);
    }

    void __cdecl capture_chat_balloon_text(void* chatBalloon)
    {
        if (!chatBalloon)
            return;

        auto staticText = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(chatBalloon) + 0x8);
        capture_floating_static_text(staticText);
    }

    bool is_lower_chat_render_position(int x, int y)
    {
        return x >= 0
            && x <= static_cast<int>(g_tune.lowerChatMaxX)
            && y >= static_cast<int>(g_tune.lowerChatMinY);
    }

    void __cdecl record_native_text_draw_probe(std::uintptr_t returnAddress, const char* text, int x, int y)
    {
        if (!is_lower_chat_render_position(x, y))
            return;

        std::vector<ChatEmojiTokenOverlay> tokens;
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            for (auto it = g_lowerChatEmojiLines.rbegin(); it != g_lowerChatEmojiLines.rend(); ++it)
            {
                std::size_t prefixLen = 0;
                if (safe_matches_sanitized_static_text(text, it->text, &prefixLen))
                {
                    tokens = it->tokens;
                    if (prefixLen > 0)
                    {
                        std::string prefix(text, prefixLen);
                        auto prefixWidth = measure_chat_prefix_width(prefix);
                        for (auto& token : tokens)
                            token.xOffset += prefixWidth;
                    }
                    break;
                }
            }
        }

        if (tokens.empty())
            return;

        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        g_floatingEmojiRenders.push_back({
            GetTickCount(),
            x,
            y,
            true,
            std::move(tokens) });

        if (g_floatingEmojiRenders.size() > 512)
            g_floatingEmojiRenders.erase(g_floatingEmojiRenders.begin(), g_floatingEmojiRenders.begin() + (g_floatingEmojiRenders.size() - 512));
    }

    void __cdecl record_floating_static_text_render(void* staticText, int x, int y)
    {
        if (!staticText)
            return;

        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        auto line = g_floatingEmojiLines.find(staticText);
        auto lowerChat = is_lower_chat_render_position(x, y);
        if (line == g_floatingEmojiLines.end())
            return;

        if (line->second.tokens.empty())
            return;

        auto now = GetTickCount();
        for (auto& render : g_floatingEmojiRenders)
        {
            if (render.tick == now
                && render.x == x
                && render.y == y
                && render.tokens.size() == line->second.tokens.size())
            {
                return;
            }
        }

        g_floatingEmojiRenders.push_back({
            now,
            x,
            y,
            lowerChat,
            line->second.tokens });

        if (g_floatingEmojiRenders.size() > 512)
            g_floatingEmojiRenders.erase(g_floatingEmojiRenders.begin(), g_floatingEmojiRenders.begin() + (g_floatingEmojiRenders.size() - 512));
    }

    void draw_visual_token_run(ImDrawList* drawList, const std::vector<ChatEmojiTokenOverlay>& tokens, float baseX, float baseY, float iconSize)
    {
        auto emojiIndex = 0;
        for (auto& token : tokens)
        {
            if (!token.emoji || !is_visual_token_enabled(token.emoji->kind))
                continue;

            auto texture = get_emoji_texture(*token.emoji);
            if (!texture)
                continue;

            auto x = baseX + token.xOffset + static_cast<float>(emojiIndex) * 2.0f;
            drawList->AddImage(
                reinterpret_cast<ImTextureID>(texture),
                ImVec2(x, baseY),
                ImVec2(x + iconSize, baseY + iconSize));
            ++emojiIndex;
        }
    }

    void draw_floating_emoji_overlays()
    {
        if (!is_game_scene() || is_emoji_transition_grace_active())
            return;

        std::vector<FloatingEmojiRenderOverlay> renders;
        {
            std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
            auto now = GetTickCount();

            // Only keep very recent entries (single frame)
            for (auto it = g_floatingEmojiRenders.begin(); it != g_floatingEmojiRenders.end();)
            {
                if (now - it->tick > 35)
                    it = g_floatingEmojiRenders.erase(it);
                else
                    ++it;
            }

            renders = g_floatingEmojiRenders;
        }

        auto drawList = ImGui::GetBackgroundDrawList();

        if (renders.empty())
            return;

        auto iconSize = g_tune.floatingIconSize;
        auto yAdjust = g_tune.floatingYAdjust;
        for (auto& render : renders)
        {
            if (render.lowerChat)
                continue; // Lower chat emojis are drawn by draw_lower_chat_emoji_overlay

            draw_visual_token_run(
                drawList,
                render.tokens,
                static_cast<float>(render.x),
                static_cast<float>(render.y) + yAdjust,
                iconSize);
        }
    }

    int read_chat_scroll_offset()
    {
        return g_chatScrollOffset;
    }

    void set_chat_scroll_offset(int value)
    {
        g_chatScrollOffset = value;
        if (g_chatScrollOffset < 0) g_chatScrollOffset = 0;

        // Max scroll = total visual lines - minimum visible threshold
        int totalVisual = 0;
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            for (auto& l : g_lowerChatEmojiLines)
                totalVisual += l.visualLines;
        }
        auto maxScroll = totalVisual - kScrollMinLines;
        if (maxScroll < 0) maxScroll = 0;
        if (g_chatScrollOffset > maxScroll) g_chatScrollOffset = maxScroll;
    }

    void draw_lower_chat_emoji_overlay()
    {
        if (!g_var || !is_game_scene())
            return;
        if (is_emoji_transition_grace_active())
            return;

        auto drawList = ImGui::GetBackgroundDrawList();
        auto iconSize = g_tune.iconSize;

        auto clientW = static_cast<float>(g_var->client.width);
        auto clientH = static_cast<float>(g_var->client.height);

        auto chatLeftX = 9.0f;
        auto chatRightX = clientW * g_tune.chatRightPct;
        auto chatTextX = g_tune.chatTextX;
        auto lineHeight = g_tune.lineHeight;
        auto chatBottomY = clientH - g_tune.chatBottomOffset;
        auto chatTopY = chatBottomY - clientH * g_tune.chatTopPct;

        auto maxVisibleLines = static_cast<int>((chatBottomY - chatTopY) / lineHeight);
        if (maxVisibleLines < 4) maxVisibleLines = 4;
        if (maxVisibleLines > 30) maxVisibleLines = 30;

        auto scrollOffset = read_chat_scroll_offset();

        std::lock_guard<std::mutex> lock(g_chatEmojiMutex);

        auto totalLines = static_cast<int>(g_lowerChatEmojiLines.size());
        if (totalLines == 0)
            return;

        // Clip to chat area bounds — emojis outside this rect are invisible
        drawList->PushClipRect(
            ImVec2(chatLeftX, chatTopY),
            ImVec2(chatRightX, chatBottomY + iconSize));

        // Walk from newest to oldest, tracking visual line position.
        // Each message can occupy 1-3 visual lines due to word wrap.
        auto visualRow = 0;  // visual rows from bottom (0 = bottom-most)
        for (auto it = g_lowerChatEmojiLines.rbegin();
            it != g_lowerChatEmojiLines.rend();
            ++it)
        {
            auto msgVisualLines = it->visualLines;

            // This message occupies visual rows [visualRow .. visualRow + msgVisualLines - 1]
            // The TEXT of this message starts at the TOP visual row of the message
            auto msgTopRow = visualRow + msgVisualLines - 1;

            // Apply scroll: skip rows below the scroll window
            if (msgTopRow < scrollOffset)
            {
                visualRow += msgVisualLines;
                continue;
            }

            // Stop if we're past the visible area
            auto displayRow = msgTopRow - scrollOffset;
            if (displayRow >= maxVisibleLines)
                break;

            if (!it->tokens.empty())
            {
                // Emojis render on the FIRST visual line of the message (top of wrapped block)
                auto y = chatBottomY - static_cast<float>(displayRow + 1) * lineHeight;

                for (auto& token : it->tokens)
                {
                    if (!token.emoji || !is_visual_token_enabled(token.emoji->kind))
                        continue;

                    auto texture = get_emoji_texture(*token.emoji);
                    if (!texture)
                        continue;

                    auto x = chatTextX + token.xOffset;
                    drawList->AddImage(
                        reinterpret_cast<ImTextureID>(texture),
                        ImVec2(x, y),
                        ImVec2(x + iconSize, y + iconSize));
                }
            }

            visualRow += msgVisualLines;
        }

        drawList->PopClipRect();
    }

    void release_emoji_textures()
    {
        ensure_emoji_catalog_loaded();
        for (auto& emoji : g_emojis)
        {
            if (emoji.texture)
            {
                emoji.texture->Release();
                emoji.texture = nullptr;
            }
            if (emoji.previewTexture)
            {
                emoji.previewTexture->Release();
                emoji.previewTexture = nullptr;
            }
            for (auto& frame : emoji.frames)
            {
                if (frame.texture)
                {
                    frame.texture->Release();
                    frame.texture = nullptr;
                }
            }
            emoji.frames.clear();
            emoji.loadAttempted = false;
            emoji.previewLoadAttempted = false;
            emoji.previewLastUsedTick = 0;
        }
    }

    void release_item_icon_textures()
    {
        for (auto& [_, entry] : g_itemIconAtlases)
        {
            if (entry.texture)
            {
                entry.texture->Release();
                entry.texture = nullptr;
            }
            entry.loadAttempted = false;
        }
    }

    void post_emoji_token(const char* token)
    {
        ensure_client_sysmsg_dispatch_ready();
        if (!token || !g_var->hwnd || !IsWindow(g_var->hwnd))
            return;

        PostMessageA(g_var->hwnd, kClientEmojiTokenWindowMessage, 0, reinterpret_cast<LPARAM>(token));
    }

    void draw_emoji_fallback(const ImVec2& min, const ImVec2& max, ImU32 color)
    {
        auto drawList = ImGui::GetWindowDrawList();
        auto center = ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
        auto radius = std::min(max.x - min.x, max.y - min.y) * 0.36f;
        drawList->AddCircleFilled(center, radius, color, 20);
        auto eyeRadius = std::max(1.0f, radius * 0.14f);
        drawList->AddCircleFilled(ImVec2(center.x - radius * 0.35f, center.y - radius * 0.18f), eyeRadius, IM_COL32(35, 31, 26, 230), 8);
        drawList->AddCircleFilled(ImVec2(center.x + radius * 0.35f, center.y - radius * 0.18f), eyeRadius, IM_COL32(35, 31, 26, 230), 8);
        drawList->AddBezierCubic(
            ImVec2(center.x - radius * 0.42f, center.y + radius * 0.18f),
            ImVec2(center.x - radius * 0.22f, center.y + radius * 0.46f),
            ImVec2(center.x + radius * 0.22f, center.y + radius * 0.46f),
            ImVec2(center.x + radius * 0.42f, center.y + radius * 0.18f),
            IM_COL32(35, 31, 26, 230),
            std::max(1.0f, radius * 0.17f));
    }

    bool emoji_image_button(const char* id, EmojiEntry& emoji, const ImVec2& size)
    {
        auto texture = get_emoji_texture(emoji);
        bool clicked = false;
        if (texture)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
            clicked = ImGui::ImageButton(
                id,
                reinterpret_cast<ImTextureID>(texture),
                size,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PopStyleColor(3);
        }
        else
        {
            return false;
        }

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", emoji.token.c_str());

        return clicked;
    }

    bool is_visual_token_picker_load_attempted(const EmojiEntry& emoji)
    {
        return emoji.kind == VisualTokenKind::Gif
            ? emoji.previewLoadAttempted
            : emoji.loadAttempted;
    }

    LPDIRECT3DTEXTURE9 get_visual_token_picker_texture(EmojiEntry& emoji, bool allowLoad)
    {
        if (emoji.kind != VisualTokenKind::Gif)
        {
            if (!allowLoad && !emoji.loadAttempted)
                return nullptr;

            return get_emoji_texture(emoji);
        }

        if (!emoji.previewLoadAttempted && allowLoad)
        {
            emoji.previewTexture = load_gif_preview_texture(emoji);
            emoji.previewLoadAttempted = true;
        }

        if (emoji.previewTexture)
            emoji.previewLastUsedTick = GetTickCount();

        return emoji.previewTexture;
    }

    // The picker may expose hundreds of GIFs; keep browsing cheap by caching
    // static previews only and evicting older previews as the user scrolls.
    void prune_gif_picker_preview_textures()
    {
        constexpr std::size_t kMaxResidentGifPickerPreviews = 96;
        std::vector<EmojiEntry*> residentPreviews;
        residentPreviews.reserve(g_emojis.size());

        for (auto& emoji : g_emojis)
        {
            if (emoji.kind == VisualTokenKind::Gif && emoji.previewTexture)
                residentPreviews.push_back(&emoji);
        }

        if (residentPreviews.size() <= kMaxResidentGifPickerPreviews)
            return;

        std::sort(
            residentPreviews.begin(),
            residentPreviews.end(),
            [](const EmojiEntry* lhs, const EmojiEntry* rhs)
            {
                return lhs->previewLastUsedTick < rhs->previewLastUsedTick;
            });

        auto texturesToRelease = residentPreviews.size() - kMaxResidentGifPickerPreviews;
        for (std::size_t i = 0; i < texturesToRelease; ++i)
        {
            auto* emoji = residentPreviews[i];
            if (!emoji || !emoji->previewTexture)
                continue;

            emoji->previewTexture->Release();
            emoji->previewTexture = nullptr;
            emoji->previewLoadAttempted = false;
            emoji->previewLastUsedTick = 0;
        }
    }

    bool image_texture_button(const char* id, LPDIRECT3DTEXTURE9 texture, const ImVec2& size)
    {
        if (!texture)
            return false;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
        auto clicked = ImGui::ImageButton(
            id,
            reinterpret_cast<ImTextureID>(texture),
            size,
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PopStyleColor(3);
        return clicked;
    }

    bool visual_token_slot_button(const char* id, EmojiEntry& emoji, const ImVec2& size, bool allowLoad)
    {
        auto attempted = is_visual_token_picker_load_attempted(emoji);
        auto texture = (allowLoad || attempted) ? get_visual_token_picker_texture(emoji, allowLoad) : nullptr;
        if (texture)
        {
            auto clicked = image_texture_button(id, texture, size);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", emoji.token.c_str());

            return clicked;
        }

        ImGui::InvisibleButton(id, size);
        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        auto drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(min, max, IM_COL32(31, 34, 40, 120), 4.0f);
        drawList->AddRect(min, max, IM_COL32(105, 116, 132, 120), 4.0f);

        attempted = is_visual_token_picker_load_attempted(emoji);
        auto label = attempted ? "!" : "...";
        auto textSize = ImGui::CalcTextSize(label);
        drawList->AddText(
            ImVec2(min.x + (size.x - textSize.x) * 0.5f, min.y + (size.y - textSize.y) * 0.5f),
            IM_COL32(185, 192, 204, 185),
            label);

        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", attempted ? emoji.token.c_str() : "Loading...");

        return false;
    }

    bool draw_visual_token_picker_grid(VisualTokenKind kind, const ImVec2& iconSize)
    {
        if (!is_visual_token_enabled(kind))
        {
            ImGui::TextDisabled("Off");
            return false;
        }

        constexpr int kColumns = 8;
        constexpr int kMaxGifLoadsPerFrame = 3;
        constexpr int kMaxEmojiLoadsPerFrame = 16;
        auto loadBudget = kind == VisualTokenKind::Gif ? kMaxGifLoadsPerFrame : kMaxEmojiLoadsPerFrame;
        auto loadsThisFrame = 0;
        auto clicked = false;
        std::vector<std::size_t> entries;
        entries.reserve(g_emojis.size());
        for (std::size_t i = 0; i < g_emojis.size(); ++i)
        {
            if (g_emojis[i].kind == kind)
                entries.push_back(i);
        }

        ImGui::BeginChild(
            kind == VisualTokenKind::Gif ? "##gif_picker_scroll" : "##emoji_picker_scroll",
            ImVec2(0.0f, 0.0f),
            false,
            ImGuiWindowFlags_AlwaysVerticalScrollbar);

        if (entries.empty())
        {
            ImGui::TextDisabled("Empty");
            ImGui::EndChild();
            return false;
        }

        auto rowCount = static_cast<int>((entries.size() + kColumns - 1) / kColumns);
        auto rowHeight = iconSize.y + ImGui::GetStyle().ItemSpacing.y;
        ImGuiListClipper clipper;
        clipper.Begin(rowCount, rowHeight);
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                for (int col = 0; col < kColumns; ++col)
                {
                    auto entryOffset = static_cast<std::size_t>(row * kColumns + col);
                    if (entryOffset >= entries.size())
                        break;

                    auto emojiIndex = entries[entryOffset];
                    auto& emoji = g_emojis[emojiIndex];
                    auto attempted = is_visual_token_picker_load_attempted(emoji);
                    auto allowLoad = attempted || loadsThisFrame < loadBudget;
                    if (!attempted && allowLoad)
                        ++loadsThisFrame;

                    ImGui::PushID(static_cast<int>(emojiIndex));
                    if (visual_token_slot_button("##visual_token", emoji, iconSize, allowLoad))
                    {
                        post_emoji_token(emoji.token.c_str());
                        g_showEmojiPicker = false;
                        clicked = true;
                    }
                    ImGui::PopID();

                    if (col + 1 < kColumns)
                        ImGui::SameLine();
                }
            }
        }

        ImGui::EndChild();
        if (kind == VisualTokenKind::Gif)
            prune_gif_picker_preview_textures();

        return clicked;
    }

    void draw_visual_token_picker_controls()
    {
        auto changed = false;
        changed |= ImGui::Checkbox("Emojis", &g_emojisEnabled);
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Gifs", &g_gifsEnabled);

        if (changed)
            save_imgui_settings();

        // Reposition controls
        if (g_emojiRepositionMode)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.35f, 0.1f, 0.9f));
            if (ImGui::Button("Click anywhere to place", ImVec2(-1.0f, 0.0f)))
                g_emojiRepositionMode = false;
            ImGui::PopStyleColor();
        }
        else
        {
            auto availW = ImGui::GetContentRegionAvail().x;
            auto spacing = ImGui::GetStyle().ItemSpacing.x;
            auto btnW = (availW - spacing) * 0.5f;
            if (ImGui::Button("Move", ImVec2(btnW, 0.0f)))
                g_emojiRepositionMode = true;
            ImGui::SameLine();
            if (ImGui::Button("Reset", ImVec2(btnW, 0.0f)))
            {
                g_emojiButtonPosition = kDefaultEmojiButtonPosition;
                g_emojiRepositionMode = false;
                save_imgui_settings();
            }
        }

        ImGui::Separator();
    }

    void draw_emoji_overlay()
    {
        if (!is_game_scene() || is_emoji_transition_grace_active())
            return;

        auto& io = ImGui::GetIO();
        if (!is_overlay_display_usable(io.DisplaySize))
            return;

        auto now = GetTickCount();
        if (static_cast<int32_t>(now - g_emojiSceneTransitionUntilTick) < 0)
            return;

        auto buttonSize = kEmojiButtonSize;
        g_emojiButtonPosition = clamp_window_position(g_emojiButtonPosition, buttonSize, io.DisplaySize);

        // In reposition mode the button follows the cursor until clicked
        if (g_emojiRepositionMode)
        {
            auto hasMousePos = io.MousePos.x >= 0.0f && io.MousePos.y >= 0.0f;
            if (hasMousePos)
            {
                auto newPos = ImVec2(io.MousePos.x - buttonSize.x * 0.5f,
                                     io.MousePos.y - buttonSize.y * 0.5f);
                g_emojiButtonPosition = clamp_window_position(newPos, buttonSize, io.DisplaySize);
            }

            // Click anywhere (outside the picker) to confirm placement
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
                && !is_cursor_in_rect(g_emojiPickerRect))
            {
                g_emojiRepositionMode = false;
                save_imgui_settings();
            }
        }

        ImGui::SetNextWindowPos(g_emojiButtonPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            "##emoji_button_overlay",
            nullptr,
            ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::InvisibleButton("##emoji_toggle", buttonSize);

        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        remember_rect(g_emojiButtonRect, min, max);
        if (!g_emojiRepositionMode)
            handle_emoji_button_interaction();

        draw_emoji_fallback(min, max,
            g_emojiRepositionMode ? IM_COL32(28, 23, 18, 255) : IM_COL32(246, 199, 63, 255));
        if (ImGui::IsItemHovered() || is_cursor_in_rect(g_emojiButtonRect))
        {
            ImGui::SetNextWindowPos(ImVec2(min.x - 2.0f, min.y - 24.0f), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(g_emojiRepositionMode ? "Click to place here" : "Open emojis");
            ImGui::EndTooltip();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);

        if (!g_showEmojiPicker)
            return;

        ensure_emoji_catalog_loaded();
        auto pickerSize = kEmojiPickerSize;
        g_emojiPickerPosition = clamp_window_position(
            ImVec2(g_emojiButtonPosition.x + kEmojiPickerOffset.x,
                   g_emojiButtonPosition.y + kEmojiPickerOffset.y),
            pickerSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_emojiPickerPosition, ImGuiCond_Always);
        ImGui::SetNextWindowSize(pickerSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.55f);
        bool pickerOpen = g_showEmojiPicker;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
        if (ImGui::Begin(
            "##emoji_picker",
            &pickerOpen,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            auto size = ImGui::GetWindowSize();

            auto pos = ImGui::GetWindowPos();
            g_emojiPickerPosition = pos;
            remember_rect(g_emojiPickerRect, pos, ImVec2(pos.x + size.x, pos.y + size.y));

            const auto iconSize = kEmojiPickerIconSize;
            draw_visual_token_picker_controls();
            if (ImGui::BeginTabBar("##visual_token_tabs", ImGuiTabBarFlags_NoCloseWithMiddleMouseButton))
            {
                if (ImGui::BeginTabItem("Emojis"))
                {
                    draw_visual_token_picker_grid(VisualTokenKind::Emoji, iconSize);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Gifs"))
                {
                    draw_visual_token_picker_grid(VisualTokenKind::Gif, iconSize);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
        g_showEmojiPicker = pickerOpen;
    }

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

    void release_device_textures_for_reset()
    {
        release_emoji_textures();
        release_item_icon_textures();

        if (g_rouletteBgTexture)
        {
            g_rouletteBgTexture->Release();
            g_rouletteBgTexture = nullptr;
        }
        g_rouletteBgLoadAttempted = false;
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
        g_panelWindowRect = {};
        g_emojiButtonRect = {};
        g_emojiPickerRect = {};

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

        if (consume_toggle(VK_F7, g_f7Down))
            toggle_realtime_feature_bundle();

        if (consume_toggle(VK_F8, g_f8Down))
            g_showPanel = !g_showPanel;

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
        // — panels appear visually offset from their clickable area.
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
                    // queued position — the one NewFrame will finalize —
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
        // button state at poll time — before the game loop processes input.
        // No additional clearing is needed here in the present hook.

        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0 && !g_draggingPanel)
        {
            g_panelMouseWasDown = false;
            g_rollMouseWasDown = false;
        }

        g_panelDragRect = {};
        g_panelWindowRect = {};
        g_emojiButtonRect = {};
        g_emojiPickerRect = {};

        draw_floating_emoji_overlays();
        draw_lower_chat_emoji_overlay();
        draw_emoji_overlay();

        debug_panel::render();

        if (g_showPanel)
            draw_panel_shell();

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
            g_f8Down = false;
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

            if (consume_toggle(VK_F7, g_f7Down))
                toggle_realtime_feature_bundle();

            auto togglePanel = consume_toggle(VK_F8, g_f8Down);
            if (togglePanel)
                g_showPanel = !g_showPanel;

            sync_emoji_overlay_scene_state();

            hook_game_device(g_var->camera.device);
            Sleep(250);
        }

        return 0;
    }

    void patch_call(void* address, void* destination)
    {
#pragma pack(push, 1)
        struct CallInstruction
        {
            uint8_t opcode;
            uint32_t operand;
        } instruction{ 0xE8, 0 };
#pragma pack(pop)

        static_assert(sizeof(CallInstruction) == 5);

        instruction.operand = static_cast<uint32_t>(
            reinterpret_cast<std::uintptr_t>(destination) - reinterpret_cast<std::uintptr_t>(address) - sizeof(instruction));
        util::write_memory(address, &instruction, sizeof(instruction));
    }

    void patch_calls_to(std::uintptr_t target, void* destination)
    {
        constexpr auto kTextStart = std::uintptr_t{ 0x401000 };
        constexpr auto kTextEnd = std::uintptr_t{ 0x745000 };

        for (auto address = kTextStart; address + 5 <= kTextEnd; ++address)
        {
            auto bytes = reinterpret_cast<std::uint8_t*>(address);
            if (bytes[0] != 0xE8)
                continue;

            auto operand = *reinterpret_cast<std::int32_t*>(bytes + 1);
            auto callTarget = address + 5 + operand;
            if (callTarget == target)
                patch_call(reinterpret_cast<void*>(address), destination);
        }
    }

    void install_chat_emoji_hook()
    {
        if (g_chatEmojiHookInstalled)
            return;

        util::detour((void*)0x422B90, naked_chat_add_token_filter, 6);
        patch_call((void*)0x412744, naked_chat_balloon_text_create);
        util::detour((void*)0x41274D, naked_capture_chat_balloon_text, 6);
        patch_call((void*)0x453DEF, naked_floating_text_create);
        util::detour((void*)0x453DF4, naked_capture_floating_static_text, 9);
        patch_calls_to(0x57C280, naked_static_text_create);
        patch_calls_to(0x57CA20, naked_floating_static_text_draw);
        patch_calls_to(0x573C00, naked_native_text_draw_probe);

        g_chatEmojiHookInstalled = true;
    }
}

unsigned u0x422B96 = 0x422B96;
void __declspec(naked) naked_chat_add_token_filter()
{
    __asm
    {
        push eax
        push ecx
        push edx

        mov eax, dword ptr[esp+0x10]
        mov edx, dword ptr[esp+0x14]
        push edx
        push eax
        call imgui_layer::prepare_chat_text_for_emojis
        add esp, 0x8
        mov dword ptr[esp+0x14], eax

        pop edx
        pop ecx
        pop eax

        sub esp, 0xA4C
        jmp u0x422B96
    }
}

unsigned u0x41FCC0 = 0x41FCC0;
void __declspec(naked) naked_chat_balloon_text_create()
{
    __asm
    {
        push eax
        push ecx
        push edx

        mov eax, dword ptr[esp+0x10]
        push eax
        call imgui_layer::prepare_floating_text_for_emojis
        add esp, 0x04
        mov dword ptr[esp+0x10], eax

        pop edx
        pop ecx
        pop eax

        jmp u0x41FCC0
    }
}

unsigned u0x412753 = 0x412753;
void __declspec(naked) naked_capture_chat_balloon_text()
{
    __asm
    {
        pushad
        push eax
        call imgui_layer::capture_chat_balloon_text
        add esp, 0x04
        popad

        fld dword ptr ds:[0x747538]
        jmp u0x412753
    }
}

unsigned u0x41FCF0 = 0x41FCF0;
void __declspec(naked) naked_floating_text_create()
{
    __asm
    {
        push eax
        push ecx
        push edx

        mov eax, dword ptr[esp+0x10]
        push eax
        call imgui_layer::prepare_floating_text_for_emojis
        add esp, 0x04
        mov dword ptr[esp+0x10], eax

        pop edx
        pop ecx
        pop eax

        jmp u0x41FCF0
    }
}

unsigned u0x453DFD = 0x453DFD;
void __declspec(naked) naked_capture_floating_static_text()
{
    __asm
    {
        add esp, 0x0C

        pushad
        push eax
        call imgui_layer::capture_floating_static_text
        add esp, 0x04
        popad

        mov dword ptr[esi+0x324], eax
        jmp u0x453DFD
    }
}

unsigned u0x57C280 = 0x57C280;
void __declspec(naked) naked_static_text_create()
{
    __asm
    {
        push ecx
        push dword ptr[esp+0x08]
        call imgui_layer::prepare_static_text_for_emojis
        add esp, 0x04
        pop ecx

        push eax
        call u0x57C280

        push eax
        push eax
        call imgui_layer::capture_created_static_text
        add esp, 0x04
        pop eax

        ret 0x04
    }
}

unsigned u0x57CA20 = 0x57CA20;
void __declspec(naked) naked_floating_static_text_draw()
{
    __asm
    {
        pushad
        mov eax, dword ptr[esp+0x24]
        mov edx, dword ptr[esp+0x28]
        mov ecx, dword ptr[esp+0x2C]
        push ecx
        push edx
        push eax
        call imgui_layer::record_floating_static_text_render
        add esp, 0x0C
        popad

        jmp u0x57CA20
    }
}

unsigned u0x573C00 = 0x573C00;
void __declspec(naked) naked_native_text_draw_probe()
{
    __asm
    {
        pushad
        mov eax, dword ptr[esp+0x20]
        mov edx, dword ptr[esp+0x28]
        mov ecx, dword ptr[esp+0x2C]
        mov ebx, dword ptr[esp+0x38]
        push ecx
        push edx
        push ebx
        push eax
        call imgui_layer::record_native_text_draw_probe
        add esp, 0x10
        popad

        jmp u0x573C00
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

void tick_client_welcome_sysmsg()
{
    imgui_layer::send_welcome_sysmsg_once();
}

void hook::imgui_layer()
{
    if (!config::load_imgui_overlay())
        return;

    imgui_layer::install_chat_emoji_hook();

    if (imgui_layer::g_running.exchange(true))
        return;

    auto thread = CreateThread(nullptr, 0, imgui_layer::render_thread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
}

bool handle_imgui_layer_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
    return imgui_layer::handle_wnd_proc(hwnd, msg, wParam, lParam, result);
}
