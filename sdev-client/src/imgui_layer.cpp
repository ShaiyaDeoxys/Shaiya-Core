#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d9.h>
#include <algorithm>
using std::max;
using std::min;
#include <gdiplus.h>
#include <objidl.h>
#include <util/util.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cctype>
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
#pragma comment(lib, "dwmapi.lib")
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
using namespace shaiya;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void naked_chat_add_token_filter();
void naked_chat_balloon_text_create();
void naked_capture_chat_balloon_text();
void naked_floating_text_create();
void naked_capture_floating_static_text();
void naked_floating_static_text_draw();

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
- F8 toggles the modular user panel. Panel features are registered as modules
  so new user-facing ImGui features do not need to rewrite the panel shell.
- F7 toggles the realtime performance-mode bundle outside the panel module system.

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
    constexpr const char* kPanelWidthKey = "PANEL_W";
    constexpr const char* kPanelHeightKey = "PANEL_H";
    constexpr const char* kPanelModuleKey = "PANEL_MODULE";
    constexpr const char* kEmojiButtonPosXKey = "EMOJI_BUTTON_X";
    constexpr const char* kEmojiButtonPosYKey = "EMOJI_BUTTON_Y";
    constexpr const char* kEmojiPickerPosXKey = "EMOJI_PICKER_X";
    constexpr const char* kEmojiPickerPosYKey = "EMOJI_PICKER_Y";
    constexpr const char* kEmojiButtonLockedKey = "EMOJI_BUTTON_LOCKED";
    constexpr const char* kEmojisEnabledKey = "EMOJIS_ENABLED";
    constexpr const char* kGifsEnabledKey = "GIFS_ENABLED";
    constexpr const char* kEmojiPickerIniSection = "EMOJI_PICKER";
    constexpr const char* kEmojiButtonXKey = "BUTTON_X";
    constexpr const char* kEmojiButtonYKey = "BUTTON_Y";
    constexpr const char* kEmojiPickerXKey = "PICKER_X";
    constexpr const char* kEmojiPickerYKey = "PICKER_Y";
    constexpr auto kFixedEmojiButtonPosition = ImVec2(321.0f, 935.0f);
    constexpr auto kFixedEmojiPickerPosition = ImVec2(359.0f, 773.0f);

    inline std::atomic_bool g_running = false;
    inline bool g_f7Down = false;
    inline bool g_f8Down = false;
    inline bool g_closeRequested = false;
    inline bool g_showPanel = false;
    inline bool g_sentWelcomeMessage = false;
    inline bool g_waitingWelcomeMessage = false;
    inline bool g_restorePanelLayout = true;
    inline bool g_f7BundleUsed = false;
    inline bool g_imguiSettingsLoaded = false;
    inline bool g_imguiSettingsDirty = false;
    inline DWORD g_f7MessageTick = 0;
    inline DWORD g_welcomeStartTick = 0;
    inline DWORD g_lastPanelSaveTick = 0;
    inline DWORD g_imguiSettingsDirtyTick = 0;
    inline HWND g_overlayHwnd = nullptr;
    inline LPDIRECT3D9 g_d3d9 = nullptr;
    inline LPDIRECT3DDEVICE9 g_device = nullptr;
    inline D3DPRESENT_PARAMETERS g_presentParameters{};
    inline RECT g_lastSyncedGameWindowRect{};
    inline UINT g_pendingBackBufferWidth = 0;
    inline UINT g_pendingBackBufferHeight = 0;
    inline bool g_deviceResetPending = false;
    inline RECT g_panelDragRect{};
    inline RECT g_panelWindowRect{};
    inline RECT g_emojiButtonRect{};
    inline RECT g_emojiPickerRect{};
    inline ImVec2 g_panelPosition = ImVec2(80.0f, 80.0f);
    inline ImVec2 g_panelSize = ImVec2(520.0f, 380.0f);
    inline ImVec2 g_emojiButtonPosition = kFixedEmojiButtonPosition;
    inline ImVec2 g_emojiPickerPosition = kFixedEmojiPickerPosition;
    inline DWORD g_lastRouletteRollTick = 0;
    inline DWORD g_lastRouletteListTick = 0;
    inline bool g_showEmojiPicker = false;
    inline bool g_chatEmojiHookInstalled = false;
    inline bool g_draggedEmojiButton = false;
    inline bool g_draggedEmojiPicker = false;
    inline bool g_draggingEmojiButton = false;
    inline bool g_emojiButtonMouseWasDown = false;
    inline ImVec2 g_emojiButtonDragOffset = ImVec2(0.0f, 0.0f);
    inline ImVec2 g_emojiButtonDragStart = ImVec2(0.0f, 0.0f);
    inline bool g_draggedPanel = false;
    inline bool g_draggingPanel = false;
    inline bool g_panelMouseWasDown = false;
    inline ImVec2 g_panelDragOffset = ImVec2(0.0f, 0.0f);
    inline bool g_emojiButtonLocked = false;
    inline bool g_emojisEnabled = true;
    inline bool g_gifsEnabled = true;
    inline bool g_hasSavedEmojiButtonPosition = false;
    inline bool g_clearImguiActiveId = false;
    inline bool g_rollMouseWasDown = false;
    inline int g_activePanelModule = 0;

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
        std::vector<ChatEmojiTokenOverlay> tokens;
    };

    struct PanelModule
    {
        const char* id;
        const char* title;
        const char* description;
        void (*draw)();
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
    inline std::deque<ChatEmojiLineOverlay> g_chatEmojiLines;
    inline std::mutex g_floatingEmojiMutex;
    inline bool g_hasPendingFloatingEmojiLine = false;
    inline ChatEmojiLineOverlay g_pendingFloatingEmojiLine{};
    inline std::unordered_map<void*, ChatEmojiLineOverlay> g_floatingEmojiLines;
    inline std::unordered_map<void*, FloatingEmojiRenderOverlay> g_floatingEmojiRenders;
    inline std::string g_sanitizedChatText;
    inline std::string g_sanitizedFloatingText;
    inline std::map<int, NativeItemIconEntry> g_nativeItemIcons;
    inline std::map<std::string, ItemIconAtlasEntry> g_itemIconAtlases;
    inline ULONG_PTR g_gdiplusToken = 0;
    inline bool g_gdiplusStartAttempted = false;

    FARPROC get_d3dx_proc(const char* name);

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

    float read_emoji_float(const char* key, const char* legacyKey, float fallback)
    {
        char buffer[64]{};
        if (read_profile_string_any(kEmojiPickerIniSection, key, buffer, sizeof(buffer)))
            return static_cast<float>(std::atof(buffer));

        return read_imgui_float(legacyKey, fallback);
    }

    bool read_emoji_float_if_present(const char* key, const char* legacyKey, float& value)
    {
        char buffer[64]{};
        if (read_profile_string_any(kEmojiPickerIniSection, key, buffer, sizeof(buffer))
            || read_profile_string_any(kImguiIniSection, legacyKey, buffer, sizeof(buffer)))
        {
            value = static_cast<float>(std::atof(buffer));
            return true;
        }

        return false;
    }

    void write_emoji_float(const char* key, const char* legacyKey, float value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        write_profile_string_all(kEmojiPickerIniSection, key, buffer);
        write_profile_string_all(kImguiIniSection, legacyKey, buffer);
    }

    float read_emoji_picker_float(const char* key, const char* legacyKey, float fallback)
    {
        char buffer[64]{};
        if (read_profile_string_any(kEmojiPickerIniSection, key, buffer, sizeof(buffer)))
            return static_cast<float>(std::atof(buffer));

        return read_imgui_float(legacyKey, fallback);
    }

    void write_emoji_picker_float(const char* key, const char* legacyKey, float value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
        write_profile_string_all(kEmojiPickerIniSection, key, buffer);
        write_profile_string_all(kImguiIniSection, legacyKey, buffer);
    }

    void load_imgui_settings()
    {
        if (g_imguiSettingsLoaded)
            return;

        g_panelPosition.x = read_imgui_float(kPanelPosXKey, g_panelPosition.x);
        g_panelPosition.y = read_imgui_float(kPanelPosYKey, g_panelPosition.y);
        g_panelSize.x = read_imgui_float(kPanelWidthKey, g_panelSize.x);
        g_panelSize.y = read_imgui_float(kPanelHeightKey, g_panelSize.y);
        g_emojiButtonPosition = kFixedEmojiButtonPosition;
        g_emojiPickerPosition = kFixedEmojiPickerPosition;
        g_hasSavedEmojiButtonPosition = true;
        write_emoji_float(kEmojiButtonXKey, kEmojiButtonPosXKey, g_emojiButtonPosition.x);
        write_emoji_float(kEmojiButtonYKey, kEmojiButtonPosYKey, g_emojiButtonPosition.y);
        write_emoji_picker_float(kEmojiPickerXKey, kEmojiPickerPosXKey, g_emojiPickerPosition.x);
        write_emoji_picker_float(kEmojiPickerYKey, kEmojiPickerPosYKey, g_emojiPickerPosition.y);
        g_emojiButtonLocked = read_imgui_bool(kEmojiButtonLockedKey, g_emojiButtonLocked);
        g_emojisEnabled = read_imgui_bool(kEmojisEnabledKey, g_emojisEnabled);
        g_gifsEnabled = read_imgui_bool(kGifsEnabledKey, g_gifsEnabled);
        g_activePanelModule = read_imgui_int(kPanelModuleKey, g_activePanelModule);
        g_imguiSettingsLoaded = true;
    }

    void save_imgui_settings()
    {
        write_imgui_float(kPanelPosXKey, g_panelPosition.x);
        write_imgui_float(kPanelPosYKey, g_panelPosition.y);
        write_imgui_float(kPanelWidthKey, g_panelSize.x);
        write_imgui_float(kPanelHeightKey, g_panelSize.y);
        g_emojiButtonPosition = kFixedEmojiButtonPosition;
        g_emojiPickerPosition = kFixedEmojiPickerPosition;
        write_emoji_float(kEmojiButtonXKey, kEmojiButtonPosXKey, g_emojiButtonPosition.x);
        write_emoji_float(kEmojiButtonYKey, kEmojiButtonPosYKey, g_emojiButtonPosition.y);
        g_hasSavedEmojiButtonPosition = true;
        write_emoji_picker_float(kEmojiPickerXKey, kEmojiPickerPosXKey, g_emojiPickerPosition.x);
        write_emoji_picker_float(kEmojiPickerYKey, kEmojiPickerPosYKey, g_emojiPickerPosition.y);
        write_imgui_bool(kEmojiButtonLockedKey, g_emojiButtonLocked);
        write_imgui_bool(kEmojisEnabledKey, g_emojisEnabled);
        write_imgui_bool(kGifsEnabledKey, g_gifsEnabled);
        write_imgui_int(kPanelModuleKey, g_activePanelModule);
        g_imguiSettingsDirty = false;
        g_lastPanelSaveTick = GetTickCount();
    }

    void save_emoji_button_position()
    {
        g_emojiButtonPosition = kFixedEmojiButtonPosition;
        write_emoji_float(kEmojiButtonXKey, kEmojiButtonPosXKey, g_emojiButtonPosition.x);
        write_emoji_float(kEmojiButtonYKey, kEmojiButtonPosYKey, g_emojiButtonPosition.y);
        g_hasSavedEmojiButtonPosition = true;
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
            && g_pPlayerData->charId != 0;
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
        POINT point{};
        if (!GetCursorPos(&point) || !g_overlayHwnd)
            return false;

        ScreenToClient(g_overlayHwnd, &point);
        pos = ImVec2(static_cast<float>(point.x), static_cast<float>(point.y));
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
        using CreateTextureFromFileInMemory = HRESULT(WINAPI*)(LPDIRECT3DDEVICE9, LPCVOID, UINT, LPDIRECT3DTEXTURE9*);
        auto createTexture = reinterpret_cast<CreateTextureFromFileInMemory>(
            get_d3dx_proc("D3DXCreateTextureFromFileInMemory"));
        if (!createTexture || !g_device)
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

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (SUCCEEDED(createTexture(g_device, resourceData, resourceSize, &texture)) && texture)
            return texture;

        return nullptr;
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

    void draw_roulette_wheel()
    {
        auto drawList = ImGui::GetWindowDrawList();
        auto origin = ImGui::GetCursorScreenPos();
        auto width = ImGui::GetContentRegionAvail().x;
        auto height = 178.0f;
        auto itemCount = roulette_item_count();
        auto now = GetTickCount();

        ImGui::Dummy(ImVec2(width, height));
        drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(16, 17, 20, 210), 6.0f);
        drawList->AddRect(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(96, 111, 132, 180), 6.0f);

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

        auto centerX = origin.x + width * 0.5f;
        auto centerY = origin.y + height * 0.5f + 9.0f;
        constexpr auto kPi = 3.1415926535f;
        constexpr auto kSlotSize = 34.0f;
        auto wheelRadius = std::min(width * 0.34f, 60.0f);
        auto step = (kPi * 2.0f) / static_cast<float>(itemCount);

        drawList->PushClipRect(origin, ImVec2(origin.x + width, origin.y + height), true);
        drawList->AddCircleFilled(ImVec2(centerX, centerY), wheelRadius + 24.0f, IM_COL32(31, 34, 42, 230), 48);
        drawList->AddCircle(ImVec2(centerX, centerY), wheelRadius + 24.0f, IM_COL32(116, 134, 164, 190), 48, 2.0f);
        drawList->AddCircleFilled(ImVec2(centerX, centerY), 22.0f, IM_COL32(22, 24, 29, 240), 32);
        drawList->AddCircle(ImVec2(centerX, centerY), 22.0f, IM_COL32(212, 178, 96, 230), 32, 2.0f);

        for (auto i = 0; i < itemCount; ++i)
        {
            auto angle = -kPi * 0.5f + (static_cast<float>(i) - progress) * step;
            auto slotCenter = ImVec2(centerX + std::cos(angle) * wheelRadius, centerY + std::sin(angle) * wheelRadius);
            ImVec2 iconMin(slotCenter.x - kSlotSize * 0.5f, slotCenter.y - kSlotSize * 0.5f);
            ImVec2 iconMax(iconMin.x + kSlotSize, iconMin.y + kSlotSize);

            auto alignment = std::fabs(std::atan2(std::sin(angle + kPi * 0.5f), std::cos(angle + kPi * 0.5f)));
            auto selected = alignment < step * 0.45f;
            drawList->AddLine(ImVec2(centerX, centerY), slotCenter, IM_COL32(78, 88, 108, 135), 1.0f);
            drawList->AddRectFilled(iconMin, iconMax, selected ? IM_COL32(65, 72, 88, 250) : IM_COL32(43, 48, 58, 235), 5.0f);
            drawList->AddRect(iconMin, iconMax, selected ? IM_COL32(255, 219, 118, 245) : IM_COL32(132, 150, 178, 220), 5.0f, 0, selected ? 2.0f : 1.0f);
            draw_item_icon_at(drawList, iconMin, iconMax, roulette_event::rewardType[i], roulette_event::rewardTypeId[i]);
            draw_item_count_badge(drawList, iconMin, iconMax, roulette_event::rewardCount[i]);
        }
        drawList->PopClipRect();

        drawList->AddTriangleFilled(
            ImVec2(centerX, origin.y + 13.0f),
            ImVec2(centerX - 8.0f, origin.y + 28.0f),
            ImVec2(centerX + 8.0f, origin.y + 28.0f),
            IM_COL32(255, 226, 126, 255));

        if (static_cast<int32_t>(now - roulette_event::celebrationUntilTick) < 0)
        {
            for (int i = 0; i < 14; ++i)
            {
                auto angle = (static_cast<float>(i) / 14.0f) * kPi * 2.0f + static_cast<float>(now) * 0.006f;
                auto start = ImVec2(centerX + std::cos(angle) * 24.0f, centerY + std::sin(angle) * 24.0f);
                auto end = ImVec2(centerX + std::cos(angle) * (wheelRadius + 32.0f), centerY + std::sin(angle) * (wheelRadius + 32.0f));
                drawList->AddLine(start, end, IM_COL32(255, 214, 92, 185), 2.0f);
            }
        }
    }

    void draw_roulette_reward_row(int index)
    {
        auto type = roulette_event::rewardType[index];
        auto typeId = roulette_event::rewardTypeId[index];
        auto count = roulette_event::rewardCount[index] ? roulette_event::rewardCount[index] : 1;
        auto chance = roulette_event::rewardChance[index];
        auto* itemInfo = CDataFile::GetItemInfo(type, typeId);
        auto drawList = ImGui::GetWindowDrawList();
        auto min = ImGui::GetCursorScreenPos();
        auto max = ImVec2(min.x + 26.0f, min.y + 26.0f);
        ImGui::Dummy(ImVec2(26.0f, 26.0f));
        draw_item_icon_at(drawList, min, max, type, typeId);
        draw_item_count_badge(drawList, min, max, count);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", itemInfo && itemInfo->name ? itemInfo->name : "Reward");
        ImGui::TextDisabled("Chance: %.2f%%", static_cast<float>(chance) / 100.0f);
        ImGui::EndGroup();
    }

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

        ImGui::SeparatorText("Roulette");
        if (roulette_event::hasList)
        {
            auto* tokenInfo = CDataFile::GetItemInfo(tokenType, tokenTypeId);
            ImGui::Text("Token: %s", tokenInfo && tokenInfo->name ? tokenInfo->name : "Token");
            ImGui::SameLine();
            ImGui::TextDisabled("(%d/%d)", tokenCount, requiredTokenCount);
        }
        else if (roulette_event::listReceived)
        {
            ImGui::TextDisabled("Roulette is not configured.");
        }
        else
        {
            ImGui::TextDisabled("Waiting for server data...");
        }

        draw_roulette_wheel();
        auto rollPressed = draw_manual_panel_button("Roll", canRoll, ImVec2(ImGui::GetContentRegionAvail().x, 24.0f));
        if (rollPressed)
            request_roulette_spin();

        auto resultLabel = roulette_result_label(roulette_event::lastResult);
        if (!is_game_scene())
            ImGui::TextDisabled("Enter the game to roll.");
        else if (roulette_event::spinActive)
            ImGui::TextDisabled("Spinning...");
        else if (roulette_event::listReceived && !roulette_event::hasList)
            ImGui::TextDisabled("The server returned an empty reward list.");
        else if (!roulette_event::hasList)
            ImGui::TextDisabled("Requesting reward list.");
        else if (tokenCount < requiredTokenCount)
            ImGui::TextDisabled("Not enough tokens.");
        else if (roulette_event::lastGrantSuccess)
        {
            auto* rewardInfo = CDataFile::GetItemInfo(roulette_event::rewardTypeCurrent, roulette_event::rewardTypeIdCurrent);
            ImGui::TextColored(ImVec4(0.45f, 1.0f, 0.45f, 1.0f), "Reward: %s x%d",
                rewardInfo && rewardInfo->name ? rewardInfo->name : "Reward",
                roulette_event::rewardCountCurrent ? roulette_event::rewardCountCurrent : 1);
        }
        else if (resultLabel[0])
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.42f, 1.0f), "%s", resultLabel);
        else if (g_lastRouletteRollTick && now - g_lastRouletteRollTick < 8000)
            ImGui::TextDisabled("Waiting for server response.");

        if (itemCount > 0)
        {
            ImGui::SeparatorText("Rewards");
            for (auto i = 0; i < itemCount; ++i)
                draw_roulette_reward_row(i);
        }
    }

    void draw_empty_feature_slot()
    {
        ImGui::TextDisabled("This feature slot is ready for implementation.");
    }

    const std::vector<PanelModule>& panel_modules()
    {
        static const std::vector<PanelModule> modules = {
            // To add a user-facing panel feature, implement draw_<feature>_section()
            // and register it here with a stable id and short description.
            {
                "roulette",
                "Roulette",
                "Roll roulette rewards.",
                draw_roulette_section
            },
            { "feature_slot_1", "Feature Slot 1", "Ready for a future feature.", draw_empty_feature_slot },
            { "feature_slot_2", "Feature Slot 2", "Ready for a future feature.", draw_empty_feature_slot },
            { "feature_slot_3", "Feature Slot 3", "Ready for a future feature.", draw_empty_feature_slot },
            { "feature_slot_4", "Feature Slot 4", "Ready for a future feature.", draw_empty_feature_slot },
            { "feature_slot_5", "Feature Slot 5", "Ready for a future feature.", draw_empty_feature_slot },
            { "feature_slot_6", "Feature Slot 6", "Ready for a future feature.", draw_empty_feature_slot },
            { "feature_slot_7", "Feature Slot 7", "Ready for a future feature.", draw_empty_feature_slot },
            { "feature_slot_8", "Feature Slot 8", "Ready for a future feature.", draw_empty_feature_slot },
            { "feature_slot_9", "Feature Slot 9", "Ready for a future feature.", draw_empty_feature_slot },
        };
        return modules;
    }

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
        g_panelDragRect.right = static_cast<LONG>(max.x - 30.0f);
        g_panelDragRect.bottom = static_cast<LONG>(max.y);

        ImGui::SetCursorScreenPos(ImVec2(min.x + 8.0f, min.y + 5.0f));
        ImGui::TextUnformatted("User Panel");

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
                std::clamp(newPosition.x, 8.0f, std::max(8.0f, displaySize.x - g_panelSize.x - 8.0f)),
                std::clamp(newPosition.y, 8.0f, std::max(8.0f, displaySize.y - g_panelSize.y - 8.0f)));

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

        ImGui::SetCursorScreenPos(ImVec2(max.x - 24.0f, min.y + 3.0f));
        if (ImGui::Button("X", ImVec2(20.0f, 20.0f)))
            panelOpen = false;

        ImGui::SetCursorScreenPos(ImVec2(min.x, max.y + 6.0f));
    }

    bool is_visible_panel_module(const PanelModule& module)
    {
        return module.draw && module.draw != draw_empty_feature_slot;
    }

    int visible_panel_module_count(const std::vector<PanelModule>& modules)
    {
        auto count = 0;
        for (auto& module : modules)
        {
            if (is_visible_panel_module(module))
                ++count;
        }

        return count;
    }

    int first_visible_panel_module_index(const std::vector<PanelModule>& modules)
    {
        for (auto index = 0; index < static_cast<int>(modules.size()); ++index)
        {
            if (is_visible_panel_module(modules[index]))
                return index;
        }

        return 0;
    }

    int next_visible_panel_module_index(const std::vector<PanelModule>& modules, int current, int direction)
    {
        if (modules.empty())
            return 0;

        auto index = current;
        for (auto attempts = 0; attempts < static_cast<int>(modules.size()); ++attempts)
        {
            index += direction;
            if (index < 0)
                index = static_cast<int>(modules.size()) - 1;
            else if (index >= static_cast<int>(modules.size()))
                index = 0;

            if (is_visible_panel_module(modules[index]))
                return index;
        }

        return current;
    }

    void draw_panel_module_navigation(const std::vector<PanelModule>& modules)
    {
        auto visibleCount = visible_panel_module_count(modules);
        auto hasMultipleModules = visibleCount > 1;
        auto& module = modules[g_activePanelModule];

        auto arrowSize = ImVec2(28.0f, 24.0f);
        auto availableWidth = ImGui::GetContentRegionAvail().x;
        auto titleWidth = std::max(40.0f, availableWidth - (arrowSize.x * 2.0f) - 12.0f);

        if (!hasMultipleModules)
            ImGui::BeginDisabled();

        if (ImGui::ArrowButtonEx(
            "##panel_prev_module",
            ImGuiDir_Left,
            arrowSize,
            ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_NoHoldingActiveId))
        {
            g_activePanelModule = next_visible_panel_module_index(modules, g_activePanelModule, -1);
            mark_imgui_settings_dirty();
            g_clearImguiActiveId = true;
        }

        ImGui::SameLine();

        if (!hasMultipleModules)
            ImGui::EndDisabled();

        auto titleMin = ImGui::GetCursorScreenPos();
        auto titleMax = ImVec2(titleMin.x + titleWidth, titleMin.y + arrowSize.y);
        ImGui::GetWindowDrawList()->AddRectFilled(titleMin, titleMax, IM_COL32(18, 20, 24, 205), 4.0f);
        auto textSize = ImGui::CalcTextSize(module.title);
        ImGui::SetCursorScreenPos(ImVec2(
            titleMin.x + std::max(0.0f, (titleWidth - textSize.x) * 0.5f),
            titleMin.y + std::max(0.0f, (arrowSize.y - textSize.y) * 0.5f)));
        ImGui::TextUnformatted(module.title);
        ImGui::SetCursorScreenPos(ImVec2(titleMax.x + 6.0f, titleMin.y));

        if (!hasMultipleModules)
            ImGui::BeginDisabled();

        if (ImGui::ArrowButtonEx(
            "##panel_next_module",
            ImGuiDir_Right,
            arrowSize,
            ImGuiButtonFlags_PressedOnClick | ImGuiButtonFlags_NoHoldingActiveId))
        {
            g_activePanelModule = next_visible_panel_module_index(modules, g_activePanelModule, 1);
            mark_imgui_settings_dirty();
            g_clearImguiActiveId = true;
        }

        if (!hasMultipleModules)
            ImGui::EndDisabled();

        ImGui::Spacing();
        ImGui::Separator();
    }

    void draw_panel_shell()
    {
        auto& modules = panel_modules();
        if (modules.empty())
            return;

        g_activePanelModule = std::clamp(g_activePanelModule, 0, static_cast<int>(modules.size()) - 1);
        if (!is_visible_panel_module(modules[g_activePanelModule]))
            g_activePanelModule = first_visible_panel_module_index(modules);

        ImGui::SetNextWindowBgAlpha(0.94f);
        ImGui::SetNextWindowPos(g_panelPosition, ImGuiCond_Always);
        if (g_restorePanelLayout)
        {
            ImGui::SetNextWindowSize(g_panelSize, ImGuiCond_Always);
            g_restorePanelLayout = false;
        }

        bool panelOpen = g_showPanel;
        if (ImGui::Begin(
            "User Panel",
            &panelOpen,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            auto windowPos = ImGui::GetWindowPos();
            auto windowSize = ImGui::GetWindowSize();
            g_panelPosition = windowPos;
            g_panelSize = windowSize;
            g_panelWindowRect.left = static_cast<LONG>(windowPos.x);
            g_panelWindowRect.top = static_cast<LONG>(windowPos.y);
            g_panelWindowRect.right = static_cast<LONG>(windowPos.x + windowSize.x);
            g_panelWindowRect.bottom = static_cast<LONG>(windowPos.y + windowSize.y);

            draw_panel_header(panelOpen);

            draw_panel_module_navigation(modules);

            ImGui::BeginChild("##module_body", ImVec2(0.0f, 0.0f), false);
            auto& module = modules[g_activePanelModule];
            if (module.draw)
                module.draw();
            ImGui::EndChild();
        }
        ImGui::End();
        g_showPanel = panelOpen;
    }

    bool should_run_overlay_session()
    {
        return g_showPanel || is_game_scene();
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

    bool is_point_in_rect(LPARAM lParam, const RECT& rect)
    {
        if (rect.left == rect.right || rect.top == rect.bottom)
            return false;

        POINT point{
            static_cast<LONG>(static_cast<short>(LOWORD(lParam))),
            static_cast<LONG>(static_cast<short>(HIWORD(lParam)))
        };
        ScreenToClient(g_overlayHwnd, &point);
        return PtInRect(&rect, point) == TRUE;
    }

    bool get_overlay_mouse_pos(ImVec2& pos)
    {
        POINT point{};
        if (!GetCursorPos(&point) || !g_overlayHwnd)
            return false;

        ScreenToClient(g_overlayHwnd, &point);
        pos = ImVec2(static_cast<float>(point.x), static_cast<float>(point.y));
        return true;
    }

    bool is_pos_in_rect(const ImVec2& pos, const RECT& rect)
    {
        return rect.left != rect.right
            && rect.top != rect.bottom
            && pos.x >= static_cast<float>(rect.left)
            && pos.x < static_cast<float>(rect.right)
            && pos.y >= static_cast<float>(rect.top)
            && pos.y < static_cast<float>(rect.bottom);
    }

    bool is_point_in_interactive_area(LPARAM lParam)
    {
        return is_point_in_rect(lParam, g_panelWindowRect)
            || is_point_in_rect(lParam, g_emojiButtonRect)
            || is_point_in_rect(lParam, g_emojiPickerRect);
    }

    bool is_cursor_in_rect(const RECT& rect)
    {
        if (rect.left == rect.right || rect.top == rect.bottom)
            return false;

        POINT point{};
        if (!GetCursorPos(&point) || !g_overlayHwnd)
            return false;

        ScreenToClient(g_overlayHwnd, &point);
        return PtInRect(&rect, point) == TRUE;
    }

    bool has_interactive_overlay()
    {
        return g_showPanel
            || is_cursor_in_rect(g_emojiButtonRect)
            || (g_showEmojiPicker && is_cursor_in_rect(g_emojiPickerRect));
    }

    void remember_rect(RECT& rect, const ImVec2& min, const ImVec2& max)
    {
        rect.left = static_cast<LONG>(min.x);
        rect.top = static_cast<LONG>(min.y);
        rect.right = static_cast<LONG>(max.x);
        rect.bottom = static_cast<LONG>(max.y);
    }

    bool has_saved_position(const ImVec2& position)
    {
        return position.x >= 0.0f && position.y >= 0.0f;
    }

    ImVec2 clamp_window_position(const ImVec2& position, const ImVec2& size, const ImVec2& displaySize)
    {
        return ImVec2(
            std::clamp(position.x, 8.0f, std::max(8.0f, displaySize.x - size.x - 8.0f)),
            std::clamp(position.y, 8.0f, std::max(8.0f, displaySize.y - size.y - 8.0f)));
    }

    void handle_emoji_button_interaction(const ImVec2& buttonSize, const ImVec2& displaySize)
    {
        g_emojiButtonPosition = clamp_window_position(kFixedEmojiButtonPosition, buttonSize, displaySize);

        ImVec2 mousePos{};
        auto hasMousePos = get_overlay_mouse_pos(mousePos);
        auto mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        auto mousePressed = mouseDown && !g_emojiButtonMouseWasDown;
        auto mouseReleased = !mouseDown && g_emojiButtonMouseWasDown;

        if (hasMousePos && mousePressed && is_pos_in_rect(mousePos, g_emojiButtonRect))
        {
            g_draggingEmojiButton = true;
            g_draggedEmojiButton = false;
            g_emojiButtonDragStart = g_emojiButtonPosition;
            g_emojiButtonDragOffset = ImVec2(
                mousePos.x - g_emojiButtonPosition.x,
                mousePos.y - g_emojiButtonPosition.y);
        }

        if (mouseReleased && g_draggingEmojiButton)
        {
            if (hasMousePos && is_pos_in_rect(mousePos, g_emojiButtonRect))
                g_showEmojiPicker = !g_showEmojiPicker;

            g_draggingEmojiButton = false;
            g_draggedEmojiButton = false;
        }

        g_emojiButtonMouseWasDown = mouseDown;
    }

    constexpr const char* kEmojiSahFolder = "emojis";
    constexpr const char* kGifSahFolder = "gifs";
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
                try_add_visual_token_from_sah_file(VisualTokenKind::Emoji, fileName, fileOffset, fileSize);
            else if (is_sah_visual_token_folder(lowerPath, VisualTokenKind::Gif))
                try_add_visual_token_from_sah_file(VisualTokenKind::Gif, fileName, fileOffset, fileSize);
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

    FARPROC get_d3dx_proc(const char* name)
    {
        auto d3dx9 = GetModuleHandleA("d3dx9_43.dll");
        if (!d3dx9)
            d3dx9 = LoadLibraryA("d3dx9_43.dll");
        if (!d3dx9)
            return nullptr;

        return GetProcAddress(d3dx9, name);
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
        using CreateTextureFromFileInMemory = HRESULT(WINAPI*)(LPDIRECT3DDEVICE9, LPCVOID, UINT, LPDIRECT3DTEXTURE9*);
        auto createTexture = reinterpret_cast<CreateTextureFromFileInMemory>(
            get_d3dx_proc("D3DXCreateTextureFromFileInMemory"));
        if (!createTexture || !g_device)
            return nullptr;

        std::vector<char> fileData;
        if (!read_client_data_file(emoji, fileData))
            return nullptr;

        LPDIRECT3DTEXTURE9 texture = nullptr;
        if (SUCCEEDED(createTexture(g_device, fileData.data(), static_cast<UINT>(fileData.size()), &texture)) && texture)
            return texture;

        return nullptr;
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
        if (chatType == 0 || chatType == 49)
            return true;

        return chatType >= 35 && chatType <= 47;
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

    void remember_chat_emoji_line(int chatType, ChatEmojiLineOverlay&& line)
    {
        if (!is_lower_chat_type(chatType))
            return;

        std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
        g_chatEmojiLines.push_back(std::move(line));
        while (g_chatEmojiLines.size() > 80)
            g_chatEmojiLines.pop_front();
    }

    const char* __cdecl prepare_chat_text_for_emojis(int chatType, const char* text)
    {
        if (!text || text[0] == '\0')
            return text;

        ChatEmojiLineOverlay line{};
        line.tick = GetTickCount();
        g_sanitizedChatText.clear();

        auto changed = false;
        auto textLength = std::strlen(text);
        g_sanitizedChatText.reserve(textLength);

        for (auto index = std::size_t{ 0 }; index < textLength;)
        {
            auto emoji = find_emoji_by_token(text + index);
            if (emoji)
            {
                auto tokenLength = emoji->token.size();
                if (is_visual_token_enabled(emoji->kind))
                {
                    if (is_lower_chat_type(chatType))
                        line.tokens.push_back({ emoji, measure_chat_prefix_width(g_sanitizedChatText) });

                    g_sanitizedChatText.append(5, ' ');
                }
                index += tokenLength;
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

            g_sanitizedChatText.push_back(text[index]);
            ++index;
        }

        remember_chat_emoji_line(chatType, std::move(line));

        if (!changed)
            return text;

        return g_sanitizedChatText.c_str();
    }

    const char* __cdecl prepare_floating_text_for_emojis(const char* text)
    {
        if (!text || text[0] == '\0')
            return text;

        ChatEmojiLineOverlay line{};
        line.tick = GetTickCount();
        auto changed = false;
        auto textLength = std::strlen(text);
        g_sanitizedFloatingText.clear();
        g_sanitizedFloatingText.reserve(textLength);

        for (auto index = std::size_t{ 0 }; index < textLength;)
        {
            auto emoji = find_emoji_by_token(text + index);
            if (emoji)
            {
                if (is_visual_token_enabled(emoji->kind))
                {
                    line.tokens.push_back({ emoji, measure_chat_prefix_width(g_sanitizedFloatingText) });
                    g_sanitizedFloatingText.append(4, ' ');
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

            g_sanitizedFloatingText.push_back(text[index]);
            ++index;
        }

        {
            std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
            g_hasPendingFloatingEmojiLine = changed && !line.tokens.empty();
            if (g_hasPendingFloatingEmojiLine)
                g_pendingFloatingEmojiLine = std::move(line);
        }

        return changed ? g_sanitizedFloatingText.c_str() : text;
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

    void __cdecl capture_chat_balloon_text(void* chatBalloon)
    {
        if (!chatBalloon)
            return;

        auto staticText = *reinterpret_cast<void**>(reinterpret_cast<std::uintptr_t>(chatBalloon) + 0x8);
        capture_floating_static_text(staticText);
    }

    void __cdecl record_floating_static_text_render(void* staticText, int x, int y)
    {
        std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
        auto line = g_floatingEmojiLines.find(staticText);
        if (line == g_floatingEmojiLines.end() || line->second.tokens.empty())
            return;

        g_floatingEmojiRenders[staticText] = {
            GetTickCount(),
            x,
            y,
            line->second.tokens
        };
    }

    void draw_chat_emoji_overlays()
    {
        if (!is_game_scene())
            return;

        std::vector<ChatEmojiLineOverlay> lines;
        {
            std::lock_guard<std::mutex> lock(g_chatEmojiMutex);
            lines.assign(g_chatEmojiLines.begin(), g_chatEmojiLines.end());
        }

        if (lines.empty())
            return;

        constexpr auto kMaxVisibleLines = std::size_t{ 12 };
        auto first = lines.size() > kMaxVisibleLines ? lines.size() - kMaxVisibleLines : std::size_t{ 0 };
        auto visibleCount = lines.size() - first;
        auto& io = ImGui::GetIO();
        auto drawList = ImGui::GetBackgroundDrawList();
        auto chatLeft = 26.0f;
        auto chatBottom = std::max(84.0f, io.DisplaySize.y - 72.0f);
        auto lineHeight = 18.0f;
        auto iconSize = 16.0f;

        for (auto i = std::size_t{ 0 }; i < visibleCount; ++i)
        {
            auto& line = lines[first + i];
            auto y = chatBottom - static_cast<float>(visibleCount - i) * lineHeight - 1.0f;

            for (auto& token : line.tokens)
            {
                if (!is_visual_token_enabled(token.emoji->kind))
                    continue;

                auto x = chatLeft + token.xOffset;
                auto texture = get_emoji_texture(*token.emoji);
                auto min = ImVec2(x, y);
                auto max = ImVec2(x + iconSize, y + iconSize);
                if (texture)
                {
                    drawList->AddImage(reinterpret_cast<ImTextureID>(texture), min, max);
                }
            }
        }
    }

    void draw_floating_emoji_overlays()
    {
        if (!is_game_scene())
            return;

        std::vector<FloatingEmojiRenderOverlay> renders;
        {
            std::lock_guard<std::mutex> lock(g_floatingEmojiMutex);
            auto now = GetTickCount();
            for (auto it = g_floatingEmojiRenders.begin(); it != g_floatingEmojiRenders.end();)
            {
                if (now - it->second.tick > 100)
                {
                    it = g_floatingEmojiRenders.erase(it);
                    continue;
                }

                renders.push_back(it->second);
                ++it;
            }
        }

        if (renders.empty())
            return;

        auto drawList = ImGui::GetBackgroundDrawList();
        auto iconSize = 16.0f;
        for (auto& render : renders)
        {
            auto emojiIndex = 0;
            for (auto& token : render.tokens)
            {
                if (!is_visual_token_enabled(token.emoji->kind))
                    continue;

                auto texture = get_emoji_texture(*token.emoji);
                if (!texture)
                    continue;

                auto x = static_cast<float>(render.x) + token.xOffset + static_cast<float>(emojiIndex) * 2.0f;
                auto y = static_cast<float>(render.y) + 11.0f;
                drawList->AddImage(
                    reinterpret_cast<ImTextureID>(texture),
                    ImVec2(x, y),
                    ImVec2(x + iconSize, y + iconSize));
                ++emojiIndex;
            }
        }
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
        drawList->AddCircleFilled(ImVec2(center.x - radius * 0.35f, center.y - radius * 0.18f), 1.4f, IM_COL32(35, 31, 26, 230), 8);
        drawList->AddCircleFilled(ImVec2(center.x + radius * 0.35f, center.y - radius * 0.18f), 1.4f, IM_COL32(35, 31, 26, 230), 8);
        drawList->AddBezierCubic(
            ImVec2(center.x - radius * 0.42f, center.y + radius * 0.18f),
            ImVec2(center.x - radius * 0.22f, center.y + radius * 0.46f),
            ImVec2(center.x + radius * 0.22f, center.y + radius * 0.46f),
            ImVec2(center.x + radius * 0.42f, center.y + radius * 0.18f),
            IM_COL32(35, 31, 26, 230),
            1.7f);
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

        ImGui::Separator();
    }

    void draw_emoji_overlay()
    {
        if (!is_game_scene())
            return;

        auto& io = ImGui::GetIO();
        if (!is_overlay_display_usable(io.DisplaySize))
            return;

        auto buttonSize = ImVec2(18.0f, 18.0f);
        g_emojiButtonPosition = clamp_window_position(kFixedEmojiButtonPosition, buttonSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_emojiButtonPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
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
        handle_emoji_button_interaction(buttonSize, io.DisplaySize);
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, IM_COL32(28, 23, 18, 215), 5.0f);
        draw_emoji_fallback(min, max, IM_COL32(246, 199, 63, 255));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Open emojis");

        ImGui::End();

        if (!g_showEmojiPicker)
            return;

        ensure_emoji_catalog_loaded();
        auto pickerSize = ImVec2(190.0f, 198.0f);
        g_emojiPickerPosition = clamp_window_position(kFixedEmojiPickerPosition, pickerSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_emojiPickerPosition, ImGuiCond_Always);
        ImGui::SetNextWindowSize(pickerSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.42f);
        bool pickerOpen = g_showEmojiPicker;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 3.0f));
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

            const auto iconSize = ImVec2(18.0f, 18.0f);
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

    void cleanup_device()
    {
        release_emoji_textures();
        release_item_icon_textures();

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

    void request_device_reset(UINT width, UINT height)
    {
        if (width == 0 || height == 0)
            return;

        g_pendingBackBufferWidth = width;
        g_pendingBackBufferHeight = height;
        g_deviceResetPending = true;
    }

    bool reset_device(UINT width, UINT height)
    {
        if (!g_device || width <= 0 || height <= 0)
            return false;

        release_item_icon_textures();
        ImGui_ImplDX9_InvalidateDeviceObjects();
        g_presentParameters.BackBufferWidth = width;
        g_presentParameters.BackBufferHeight = height;
        auto result = g_device->Reset(&g_presentParameters);
        if (FAILED(result))
        {
            request_device_reset(width, height);
            return false;
        }

        ImGui_ImplDX9_CreateDeviceObjects();
        g_deviceResetPending = false;
        g_pendingBackBufferWidth = 0;
        g_pendingBackBufferHeight = 0;
        return true;
    }

    void sync_overlay_to_game()
    {
        if (!g_overlayHwnd || !g_var->hwnd)
            return;

        if (IsIconic(g_var->hwnd) || !IsWindowVisible(g_var->hwnd) || !is_game_window_foreground())
        {
            if (IsWindowVisible(g_overlayHwnd))
                ShowWindow(g_overlayHwnd, SW_HIDE);
            g_lastSyncedGameWindowRect = {};
            return;
        }

        RECT rect{};
        if (!GetWindowRect(g_var->hwnd, &rect))
            return;

        auto width = rect.right - rect.left;
        auto height = rect.bottom - rect.top;
        if (width < 320 || height < 240)
            return;

        if (!IsWindowVisible(g_overlayHwnd))
            ShowWindow(g_overlayHwnd, SW_SHOWNA);

        if (!EqualRect(&g_lastSyncedGameWindowRect, &rect))
        {
            SetWindowPos(
                g_overlayHwnd,
                HWND_TOP,
                rect.left,
                rect.top,
                width,
                height,
                SWP_NOACTIVATE);
            g_lastSyncedGameWindowRect = rect;
        }

        if (g_presentParameters.BackBufferWidth != static_cast<UINT>(width)
            || g_presentParameters.BackBufferHeight != static_cast<UINT>(height))
        {
            request_device_reset(static_cast<UINT>(width), static_cast<UINT>(height));
        }
    }

    void sync_overlay_input_passthrough()
    {
        if (!g_overlayHwnd)
            return;

        auto exStyle = GetWindowLongPtrA(g_overlayHwnd, GWL_EXSTYLE);
        auto wantsClickThrough = !has_interactive_overlay();
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
                request_device_reset(LOWORD(lParam), HIWORD(lParam));
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
            WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
            wndClass.lpszClassName,
            "Shaiya ImGui Overlay",
            WS_POPUP,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            1280,
            720,
            g_var->hwnd,
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

        ShowWindow(g_overlayHwnd, SW_HIDE);
        UpdateWindow(g_overlayHwnd);
        g_closeRequested = false;
        g_restorePanelLayout = true;
        g_panelWindowRect = {};
        g_emojiButtonRect = {};
        g_emojiPickerRect = {};
        g_pendingBackBufferWidth = 0;
        g_pendingBackBufferHeight = 0;
        g_deviceResetPending = false;
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

            if (consume_toggle(VK_F7, g_f7Down))
                toggle_realtime_feature_bundle();

            if (consume_toggle(VK_F8, g_f8Down))
                g_showPanel = !g_showPanel;

            update_roulette_spin_state();

            if (!should_run_overlay_session())
                g_closeRequested = true;

            if (!g_var->hwnd || !IsWindow(g_var->hwnd))
                g_closeRequested = true;

            sync_overlay_to_game();
            sync_overlay_input_passthrough();

            auto cooperativeLevel = g_device->TestCooperativeLevel();
            if (cooperativeLevel == D3DERR_DEVICELOST)
            {
                Sleep(10);
                continue;
            }

            if (cooperativeLevel == D3DERR_DEVICENOTRESET)
            {
                if (!g_deviceResetPending)
                {
                    auto width = g_presentParameters.BackBufferWidth;
                    auto height = g_presentParameters.BackBufferHeight;
                    if (width == 0 || height == 0)
                    {
                        RECT rect{};
                        if (g_overlayHwnd && GetClientRect(g_overlayHwnd, &rect))
                        {
                            width = static_cast<UINT>(std::max<LONG>(1, rect.right - rect.left));
                            height = static_cast<UINT>(std::max<LONG>(1, rect.bottom - rect.top));
                        }
                    }
                    request_device_reset(width, height);
                }
            }
            else if (cooperativeLevel != D3D_OK)
            {
                Sleep(10);
                continue;
            }

            if (g_deviceResetPending)
            {
                auto width = g_pendingBackBufferWidth ? g_pendingBackBufferWidth : g_presentParameters.BackBufferWidth;
                auto height = g_pendingBackBufferHeight ? g_pendingBackBufferHeight : g_presentParameters.BackBufferHeight;
                if (!reset_device(width, height))
                {
                    Sleep(10);
                    continue;
                }
            }

            ImGui_ImplDX9_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            if (g_clearImguiActiveId)
            {
                ImGui::ClearActiveID();
                g_clearImguiActiveId = false;
            }

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
            draw_chat_emoji_overlays();
            draw_emoji_overlay();

            if (g_showPanel)
                draw_panel_shell();

            sync_overlay_input_passthrough();

            ImGui::EndFrame();
            ImGui::Render();

            g_device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
            if (SUCCEEDED(g_device->BeginScene()))
            {
                ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
                g_device->EndScene();
            }

            auto presentResult = g_device->Present(nullptr, nullptr, nullptr, nullptr);
            if (presentResult == D3DERR_DEVICELOST || presentResult == D3DERR_DEVICENOTRESET)
                request_device_reset(g_presentParameters.BackBufferWidth, g_presentParameters.BackBufferHeight);

            save_imgui_settings_if_dirty(750);
            Sleep(1);
        }
    }

    DWORD WINAPI render_thread(LPVOID)
    {
        while (g_running)
        {
            if (!g_var->hwnd || !IsWindow(g_var->hwnd))
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

    void install_chat_emoji_hook()
    {
        if (g_chatEmojiHookInstalled)
            return;

        util::detour((void*)0x422B90, naked_chat_add_token_filter, 6);
        patch_call((void*)0x412744, naked_chat_balloon_text_create);
        util::detour((void*)0x41274D, naked_capture_chat_balloon_text, 6);
        patch_call((void*)0x453DEF, naked_floating_text_create);
        util::detour((void*)0x453DF4, naked_capture_floating_static_text, 9);
        patch_call((void*)0x41FFF9, naked_floating_static_text_draw);
        patch_call((void*)0x4202A7, naked_floating_static_text_draw);
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
    imgui_layer::install_chat_emoji_hook();

    if (imgui_layer::g_running.exchange(true))
        return;

    auto thread = CreateThread(nullptr, 0, imgui_layer::render_thread, nullptr, 0, nullptr);
    if (thread)
        CloseHandle(thread);
}
