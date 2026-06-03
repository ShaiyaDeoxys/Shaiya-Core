#pragma once
// imgui_layer_internal.h — shared state, types, and constants for the
// imgui_layer subsystem.  Every imgui_layer_*.cpp includes this file
// instead of redeclaring shared variables and helpers.
//
// All inline variables live in the imgui_layer namespace so there is
// exactly one copy across translation units (C++17 inline).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <d3d9.h>
#include <algorithm>
using std::max;
using std::min;
#include <gdiplus.h>
#include <objidl.h>
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
#include <external/imgui/imgui.h>
#include <external/imgui/imgui_internal.h>
#include <external/imgui/backends/imgui_impl_dx9.h>
#include <external/imgui/backends/imgui_impl_win32.h>
#include "include/main.h"
#include "include/config.h"
#include "include/shaiya/CCharacter.h"
#include "include/shaiya/CPlayerData.h"
#include "include/shaiya/CTexture.h"
#include "include/shaiya/CWorldMgr.h"
#include "include/shaiya/Static.h"
#include "include/shaiya/Roulette.h"
#include "include/shaiya/RewardItemEvent.h"
#include "include/shaiya/Teleport.h"
using namespace shaiya;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

int command_handler(char* text);
void ensure_client_sysmsg_dispatch_ready();
void tick_client_welcome_sysmsg();

namespace imgui_layer
{
    // -----------------------------------------------------------------------
    //  INI keys
    // -----------------------------------------------------------------------
    constexpr const char* kImguiIniSection = "IMGUI";
    constexpr const char* kPanelPosXKey = "PANEL_X";
    constexpr const char* kPanelPosYKey = "PANEL_Y";
    constexpr auto kEmojiButtonSize = ImVec2(20.0f, 20.0f);
    constexpr auto kEmojiPickerSize = ImVec2(190.0f, 200.0f);
    constexpr auto kEmojiPickerIconSize = ImVec2(20.0f, 20.0f);
    constexpr auto kDefaultRewardButtonPosition = ImVec2(350.0f, 938.0f);
    constexpr const char* kRewardBtnXKey = "REWARD_BTN_X";
    constexpr const char* kRewardBtnYKey = "REWARD_BTN_Y";
    constexpr const char* kRewardBarXKey = "REWARD_BAR_X";
    constexpr const char* kRewardBarYKey = "REWARD_BAR_Y";
    constexpr auto kRewardButtonSize = ImVec2(32.0f, 33.0f);
    constexpr auto kDefaultRouletteButtonPosition = ImVec2(386.0f, 938.0f);
    constexpr const char* kRouletteBtnXKey = "ROULETTE_BTN_X";
    constexpr const char* kRouletteBtnYKey = "ROULETTE_BTN_Y";
    constexpr auto kRouletteButtonSize = ImVec2(32.0f, 33.0f);
    constexpr auto kDefaultSettingsButtonPosition = ImVec2(422.0f, 938.0f);
    constexpr const char* kSettingsBtnXKey = "SETTINGS_BTN_X";
    constexpr const char* kSettingsBtnYKey = "SETTINGS_BTN_Y";
    constexpr const char* kSettingsPanelXKey = "SETTINGS_PANEL_X";
    constexpr const char* kSettingsPanelYKey = "SETTINGS_PANEL_Y";
    constexpr auto kSettingsButtonSize = ImVec2(32.0f, 33.0f);
    constexpr auto kSettingsPanelSize = ImVec2(210.0f, 270.0f);
    constexpr auto kDefaultNpcButtonPosition = ImVec2(458.0f, 938.0f);
    constexpr const char* kNpcBtnXKey = "NPC_BTN_X";
    constexpr const char* kNpcBtnYKey = "NPC_BTN_Y";
    constexpr const char* kNpcPanelXKey = "NPC_PANEL_X";
    constexpr const char* kNpcPanelYKey = "NPC_PANEL_Y";
    constexpr auto kNpcButtonSize = ImVec2(32.0f, 33.0f);
    constexpr auto kNpcPanelSize = ImVec2(210.0f, 270.0f);
    constexpr auto kDefaultTeleportButtonPosition = ImVec2(494.0f, 938.0f);
    constexpr const char* kTeleportBtnXKey = "TELEPORT_BTN_X";
    constexpr const char* kTeleportBtnYKey = "TELEPORT_BTN_Y";
    constexpr const char* kTeleportPanelXKey = "TELEPORT_PANEL_X";
    constexpr const char* kTeleportPanelYKey = "TELEPORT_PANEL_Y";
    constexpr auto kTeleportButtonSize = ImVec2(32.0f, 33.0f);
    constexpr auto kTeleportPanelSize = ImVec2(210.0f, 270.0f);
    constexpr const char* kIdViewEnabledKey = "ID_VIEW_ENABLED";
    constexpr DWORD kEmojiSceneGraceMs = 4000;

    // Game screen state — address 0x7C48F8 holds an int that tells us
    // exactly what screen the client is on.  Verified in-game:
    //   3 = character select,  5 = loading screen,  6 = in-game world.
    // Using this as the primary gate eliminates the old timer-based
    // cooldown entirely.  We still keep a short frame debounce so the
    // overlay doesn't flicker on the exact frame the state flips to 6.
    constexpr uintptr_t kGameScreenStateAddr = 0x7C48F8;
    constexpr int       kScreenStateInGame  = 6;
    constexpr int   kMinStableFrames     = 30;    // ~0.5 s at 60 fps

    // -----------------------------------------------------------------------
    //  Shared inline state
    // -----------------------------------------------------------------------
    inline std::atomic_bool g_running = false;
    inline bool g_idViewEnabled = true;

    // Scene stability tracking — frame debounce after screen state → in-game
    inline int      g_sceneStableFrames = 0;       // consecutive in-game frames

    // Native UI visibility tracking (F11 toggles all game windows)
    inline bool     g_nativeUIHidden = false;
    inline bool g_showPanel = false;
    inline bool g_sentWelcomeMessage = false;
    inline bool g_waitingWelcomeMessage = false;
    inline bool g_imguiSettingsLoaded = false;
    inline bool g_imguiSettingsDirty = false;
    inline DWORD g_welcomeStartTick = 0;
    inline DWORD g_lastPanelSaveTick = 0;
    inline DWORD g_imguiSettingsDirtyTick = 0;
    inline HWND g_overlayHwnd = nullptr;
    inline LPDIRECT3DDEVICE9 g_device = nullptr;
    inline bool g_imguiInitialized = false;

    inline float g_mouseScaleX = 1.0f;
    inline float g_mouseScaleY = 1.0f;
    inline LPDIRECT3DDEVICE9 g_hookedDevice = nullptr;
    using ResetFn = HRESULT(__stdcall*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    using PresentFn = HRESULT(__stdcall*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    inline ResetFn g_originalReset = nullptr;
    inline PresentFn g_originalPresent = nullptr;

    // DirectInput mouse hook state
    using GetDeviceStateFn = HRESULT(__stdcall*)(IDirectInputDevice8A*, DWORD, LPVOID);
    inline GetDeviceStateFn g_originalMouseGetDeviceState = nullptr;
    inline GetDeviceStateFn g_originalMouse2GetDeviceState = nullptr;
    inline bool g_mouseDeviceHooked = false;

    // Hit-test rects for every overlay element
    // Main map anchor — object pointer captured once from init; pos/size read live each frame.
    inline uintptr_t g_mainMapObjectPtr = 0;
    constexpr float kButtonAnchorOffsetX = 14.0f;
    constexpr float kButtonAnchorOffsetY = 19.0f;
    constexpr float kButtonAnchorStride = 26.5f;

    void update_anchored_button_positions();

    // Chat panel object (0x75E0-byte UI container) — captured at runtime
    // from the chat-init function at VA 0x47D1F0 (ecx = this).
    inline uintptr_t g_chatPanelPtr = 0;

    inline RECT g_panelDragRect{};
    inline RECT g_panelWindowRect{};
    inline RECT g_emojiButtonRect{};
    inline RECT g_emojiPickerRect{};
    inline RECT g_rewardButtonRect{};
    inline RECT g_rewardBarRect{};
    inline RECT g_rouletteButtonRect{};
    inline RECT g_settingsButtonRect{};
    inline RECT g_settingsPanelRect{};
    inline RECT g_npcButtonRect{};
    inline RECT g_npcPanelRect{};
    inline RECT g_teleportButtonRect{};
    inline RECT g_teleportPanelRect{};

    // Per-element positions
    inline ImVec2 g_panelPosition = ImVec2(80.0f, 80.0f);
    inline ImVec2 g_emojiButtonPosition = ImVec2(321.0f, 939.0f);
    inline ImVec2 g_emojiPickerPosition = ImVec2(0.0f, 0.0f);
    inline ImVec2 g_rewardButtonPosition = kDefaultRewardButtonPosition;
    inline ImVec2 g_rewardBarPosition = ImVec2(-1.0f, -1.0f);
    inline ImVec2 g_rouletteButtonPosition = kDefaultRouletteButtonPosition;
    inline ImVec2 g_settingsButtonPosition = kDefaultSettingsButtonPosition;
    inline ImVec2 g_settingsPanelPosition = ImVec2(-1.0f, -1.0f);
    inline ImVec2 g_npcButtonPosition = kDefaultNpcButtonPosition;
    inline ImVec2 g_npcPanelPosition = ImVec2(-1.0f, -1.0f);
    inline ImVec2 g_teleportButtonPosition = kDefaultTeleportButtonPosition;
    inline ImVec2 g_teleportPanelPosition = ImVec2(-1.0f, -1.0f);
    inline ImFont* g_parallelFont = nullptr;
    inline bool    g_parallelFontLoaded = false;
    // Drag state
    inline DWORD g_lastRouletteRollTick = 0;
    inline DWORD g_lastRouletteListTick = 0;
    inline bool g_showEmojiPicker = false;
    inline bool g_chatEmojiHookInstalled = false;
    inline bool g_draggedPanel = false;
    inline bool g_draggingPanel = false;
    inline bool g_draggingRewardButton = false;
    inline bool g_draggedRewardButton = false;
    inline bool g_draggingRouletteButton = false;
    inline bool g_draggedRouletteButton = false;
    inline bool g_draggingSettingsButton = false;
    inline bool g_draggedSettingsButton = false;
    inline bool g_draggingNpcButton = false;
    inline bool g_draggedNpcButton = false;
    inline bool g_draggingTeleportButton = false;
    inline bool g_draggedTeleportButton = false;
    inline bool g_panelMouseWasDown = false;
    inline ImVec2 g_panelDragOffset = ImVec2(0.0f, 0.0f);
    inline ImVec2 g_rewardButtonDragOffset = ImVec2(0.0f, 0.0f);
    inline ImVec2 g_rouletteButtonDragOffset = ImVec2(0.0f, 0.0f);
    inline ImVec2 g_settingsButtonDragOffset = ImVec2(0.0f, 0.0f);
    inline ImVec2 g_npcButtonDragOffset = ImVec2(0.0f, 0.0f);
    inline ImVec2 g_teleportButtonDragOffset = ImVec2(0.0f, 0.0f);
    inline bool g_clearImguiActiveId = false;
    inline bool g_rollMouseWasDown = false;
    inline bool g_showSettingsPanel = false;
    inline bool g_showNpcPanel = false;
    inline bool g_showTeleportPanel = false;

    // Reward bar state
    constexpr const char* kRewardAutoClaimKey = "REWARD_AUTOCLAIM";
    inline bool g_showRewardBar = false;
    inline bool g_rewardAutoClaimEnabled = false;
    inline uint32_t g_rewardReadyTick = 0;
    inline uint32_t g_rewardNextAutoClaimTick = 0;

    // SAF texture state — icons loaded from data.sah/data.saf
    inline LPDIRECT3DTEXTURE9 g_rouletteBgTexture = nullptr;
    inline uint64_t g_rouletteBgDataOffset = 0;
    inline uint64_t g_rouletteBgDataSize = 0;
    inline bool g_rouletteBgFound = false;
    inline bool g_rouletteBgLoadAttempted = false;
    inline LPDIRECT3DTEXTURE9 g_rewardIconTexture = nullptr;
    inline uint64_t g_rewardIconDataOffset = 0;
    inline uint64_t g_rewardIconDataSize = 0;
    inline bool g_rewardIconFound = false;
    inline bool g_rewardIconLoadAttempted = false;
    inline LPDIRECT3DTEXTURE9 g_rouletteIconTexture = nullptr;
    inline uint64_t g_rouletteIconDataOffset = 0;
    inline uint64_t g_rouletteIconDataSize = 0;
    inline bool g_rouletteIconFound = false;
    inline bool g_rouletteIconLoadAttempted = false;
    inline LPDIRECT3DTEXTURE9 g_settingsIconTexture = nullptr;
    inline uint64_t g_settingsIconDataOffset = 0;
    inline uint64_t g_settingsIconDataSize = 0;
    inline bool g_settingsIconFound = false;
    inline bool g_settingsIconLoadAttempted = false;
    inline LPDIRECT3DTEXTURE9 g_npcIconTexture = nullptr;
    inline uint64_t g_npcIconDataOffset = 0;
    inline uint64_t g_npcIconDataSize = 0;
    inline bool g_npcIconFound = false;
    inline bool g_npcIconLoadAttempted = false;
    inline LPDIRECT3DTEXTURE9 g_teleportIconTexture = nullptr;
    inline uint64_t g_teleportIconDataOffset = 0;
    inline uint64_t g_teleportIconDataSize = 0;
    inline bool g_teleportIconFound = false;
    inline bool g_teleportIconLoadAttempted = false;
    inline LPDIRECT3DTEXTURE9 g_windowBgTexture = nullptr;
    inline uint64_t g_windowBgDataOffset = 0;
    inline uint64_t g_windowBgDataSize = 0;
    inline bool g_windowBgFound = false;
    inline bool g_windowBgLoadAttempted = false;

    // Roulette panel layout
    constexpr float kRollButtonOffsetX = -1.0f;
    constexpr float kRollButtonOffsetY = -47.0f;
    constexpr float kRollButtonH = 30.0f;
    constexpr float kRollButtonInset = 95.0f;
    constexpr float kPanelFixedW = 546.0f;
    constexpr float kPanelFixedH = 577.0f;

    // -----------------------------------------------------------------------
    //  Shared types
    // -----------------------------------------------------------------------
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
        void* source;
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

    // Emoji state
    inline std::vector<EmojiEntry> g_emojis;
    inline bool g_emojiCatalogLoaded = false;
    // Token prefix index for O(1) emoji lookup — built after catalog loads.
    // Key: first 2 chars after ':' (e.g. ":smile:" → "sm").
    // Value: all emoji entries whose token starts with that prefix.
    inline std::unordered_map<uint16_t, std::vector<EmojiEntry*>> g_emojiTokenIndex;

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

    // Item icon state
    inline std::map<int, NativeItemIconEntry> g_nativeItemIcons;
    inline std::map<std::string, ItemIconAtlasEntry> g_itemIconAtlases;

    // GDI+ state (for GIF decoding)
    inline ULONG_PTR g_gdiplusToken = 0;
    inline bool g_gdiplusStartAttempted = false;

    // Chat overlay tuning
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

    // SAH folder constants
    constexpr const char* kEmojiSahFolder   = "assets\\emojis";
    constexpr const char* kGifSahFolder     = "assets\\gifs";
    constexpr const char* kGeneralSahFolder = "assets\\general";
    constexpr PROPID kGdiplusFrameDelayProperty = 0x5100;

    // -----------------------------------------------------------------------
    //  Shared function declarations (defined across imgui_layer_*.cpp files)
    // -----------------------------------------------------------------------

    // imgui_layer_settings.cpp
    const char* get_imgui_ini_path();
    void load_imgui_settings();
    void save_imgui_settings();
    void mark_imgui_settings_dirty();
    void save_imgui_settings_if_dirty(DWORD debounceMs);
    float read_imgui_float(const char* key, float fallback);
    void write_imgui_float(const char* key, float value);
    bool read_imgui_bool(const char* key, bool fallback);
    void write_imgui_bool(const char* key, bool value);
    void send_welcome_sysmsg_once();
    void run_settings_command(const char* text);
    void draw_settings_panel();
    void draw_settings_button_overlay();
    void draw_npc_panel();
    void draw_npc_button_overlay();

    // imgui_layer_items.cpp
    int count_inventory_item(uint8_t type, uint8_t typeId);
    int resolve_item_icon_index(int type, int typeId);
    CTexture* get_or_load_native_item_icon_texture(int iconId);
    bool get_item_icon_atlas_config(int type, std::string& outFileName, int& outCols, int& outRows, int& outWidth, int& outHeight);
    LPDIRECT3DTEXTURE9 get_or_load_item_icon_atlas_texture(const std::string& fileName, int width, int height);
    void draw_item_icon_at(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, int type, int typeId);
    void draw_item_count_badge(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, int count);
    void release_item_icon_textures();
    LPDIRECT3DTEXTURE9 create_texture_from_dds_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize);
    LPDIRECT3DTEXTURE9 create_texture_from_image_memory(LPDIRECT3DDEVICE9 device, const void* data, UINT dataSize);

    // imgui_layer_roulette.cpp
    void update_roulette_spin_state();
    void draw_roulette_wheel();
    void draw_panel_header(bool& panelOpen);
    void draw_panel_shell();
    void draw_roulette_button_overlay();
    uint8_t roulette_item_count();
    const char* roulette_result_label(uint8_t result);
    void request_roulette_list();
    void request_roulette_spin();

    // imgui_layer_reward.cpp
    void draw_reward_bar();
    void draw_reward_button_overlay();
    void update_reward_auto_claim();
    float reward_item_progress_ratio();

    // imgui_layer_teleport.cpp
    void draw_teleport_button_overlay();
    void draw_teleport_panel();
    void request_teleport_list();
    void request_teleport_move(uint8_t index);
    LPDIRECT3DTEXTURE9 load_teleport_icon_texture();

    // imgui_layer_emoji.cpp
    void ensure_emoji_catalog_loaded();
    void release_emoji_textures();
    EmojiEntry* find_emoji_by_token(const char* text);
    bool is_visual_token_enabled(VisualTokenKind kind);
    LPDIRECT3DTEXTURE9 get_emoji_texture(EmojiEntry& emoji);
    float get_chat_text_width();
    float measure_chat_prefix_width(const std::string& prefix);
    void draw_floating_emoji_overlays();
    void draw_lower_chat_emoji_overlay();
    void draw_emoji_overlay();
    void install_chat_emoji_hook();
    LPDIRECT3DTEXTURE9 load_saf_texture(LPDIRECT3DTEXTURE9& texture, bool& loadAttempted, bool found, uint64_t offset, uint64_t size);
    LPDIRECT3DTEXTURE9 load_roulette_bg_texture();
    LPDIRECT3DTEXTURE9 load_reward_icon_texture();
    LPDIRECT3DTEXTURE9 load_roulette_icon_texture();
    LPDIRECT3DTEXTURE9 load_settings_icon_texture();
    LPDIRECT3DTEXTURE9 load_npc_icon_texture();
    LPDIRECT3DTEXTURE9 load_window_bg_texture();
    void draw_window_background();
    void draw_panel_title_with_close(const char* title, bool& open);
    const char* __cdecl prepare_chat_text_for_emojis(int chatType, const char* text);
    const char* __cdecl prepare_static_text_for_emojis(const char* text);
    const char* __cdecl prepare_floating_text_for_emojis(const char* text);
    void __cdecl capture_floating_static_text(void* staticText);
    void __cdecl capture_created_static_text(void* staticText);
    void __cdecl capture_chat_balloon_text(void* chatBalloon);
    void __cdecl record_native_text_draw_probe(std::uintptr_t returnAddress, const char* text, int x, int y);
    void __cdecl record_floating_static_text_render(void* staticText, int x, int y);
    void set_chat_scroll_offset(int value);

    // imgui_layer.cpp  (core)
    bool hook_vtable(void* instance, std::size_t index, void* hook, void** original);
    void release_texture(LPDIRECT3DTEXTURE9& texture, bool& loadAttempted);
    bool is_game_scene();
    bool is_game_scene_stable();
    void sync_map_transition_state();
    bool is_map_transition_active();
    bool is_overlay_display_usable(const ImVec2& size);
    bool get_overlay_mouse_pos_raw(ImVec2& pos);
    bool is_pos_in_rect_raw(const ImVec2& pos, const RECT& rect);
    bool is_cursor_in_rect(const RECT& rect);
    void remember_rect(RECT& rect, const ImVec2& min, const ImVec2& max);
    ImVec2 clamp_window_position(const ImVec2& position, const ImVec2& size, const ImVec2& displaySize);
    void release_imgui_capture();
    bool consume_toggle(int virtualKey, bool& wasDown);
    bool handle_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result);
    DWORD WINAPI render_thread(LPVOID);
    void render_integrated_frame(IDirect3DDevice9* device);

    // Shared helpers used across multiple files
    void draw_icon_button_fallback(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, ImU32 tint, ImU32 bgColor, const char* label);
    bool is_emoji_transition_grace_active();
    void sync_emoji_overlay_scene_state();
    void clear_emoji_text_overlays();
    void handle_emoji_button_interaction();
}
