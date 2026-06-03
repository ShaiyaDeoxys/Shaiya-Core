#include "include/imgui_layer_internal.h"
#include <array>
#include <shaiya/include/common/NpcTypes.h>
#include <shaiya/include/network/game/incoming/0200.h>
#include "include/shaiya/CNetwork.h"

// -- Remote NPC shortcut dispatcher --
namespace
{
    enum class RemoteNpcAction { Vet, Bank, Guild };
    enum class ShortcutActionType { LegacyNpcId, RemoteNpc };
    struct RemoteNpcShortcut { ShortcutActionType type; int value; };

    constexpr std::array<RemoteNpcShortcut, 6> kNpcShortcuts{{
        { ShortcutActionType::LegacyNpcId, 0x65 },
        { ShortcutActionType::LegacyNpcId, 0x66 },
        { ShortcutActionType::LegacyNpcId, 0x79 },
        { ShortcutActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Vet) },
        { ShortcutActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Bank) },
        { ShortcutActionType::RemoteNpc, static_cast<int>(RemoteNpcAction::Guild) },
    }};

    void open_remote_npc(RemoteNpcAction action)
    {
        using namespace shaiya;
        if (g_pPlayerData->windowType != WindowType::None)
        {
            Static::SysMsgToChatBox(ChatType::Acquire31, 806, 12);
            return;
        }
        switch (action)
        {
        case RemoteNpcAction::Vet:
        {
            GameStatusResultInfoIncoming pkt{};
            CNetwork::Send(&pkt, sizeof(pkt));
            g_var->killLv = 0;
            g_var->deathLv = 0;
            g_pPlayerData->npcType = std::to_underlying(NpcType::VetManager);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 1 : 2;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            break;
        }
        case RemoteNpcAction::Bank:
            g_pPlayerData->npcType = std::to_underlying(NpcType::Merchant);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 179 : 180;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            g_pPlayerData->windowType = WindowType::BankTeller;
            break;
        case RemoteNpcAction::Guild:
            g_pPlayerData->npcType = std::to_underlying(NpcType::GuildMaster);
            g_pPlayerData->npcTypeId = g_pPlayerData->country == Country::Light ? 1 : 2;
            g_pPlayerData->npcIcon = 55;
            g_pPlayerData->textBuffer[0] = '\0';
            g_pPlayerData->windowType = WindowType::GuildMaster;
            break;
        default: break;
        }
    }
}

void open_remote_npc_shortcut(int index)
{
    if (index < 0 || index >= static_cast<int>(kNpcShortcuts.size()))
        return;
    auto& s = kNpcShortcuts[static_cast<std::size_t>(index)];
    if (s.type == ShortcutActionType::LegacyNpcId)
    {
        *reinterpret_cast<int*>(0x91AD44) = 0x1;
        *reinterpret_cast<int*>(0x91AD40) = 0x12C;
        *reinterpret_cast<int*>(0x9144F0) = -1;
        *reinterpret_cast<int*>(0x22AB7B8) = 0x0;
        *reinterpret_cast<int*>(0x9144E4) = s.value;
    }
    else
        open_remote_npc(static_cast<RemoteNpcAction>(s.value));
}

namespace imgui_layer {

    // -- Panel helpers --
    void draw_window_background()
    {
        auto* tex = g_windowBgTexture;
        if (!tex) return;
        auto pos = ImGui::GetWindowPos();
        auto size = ImGui::GetWindowSize();
        ImGui::GetWindowDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(tex), pos,
            ImVec2(pos.x + size.x, pos.y + size.y),
            ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
            IM_COL32(255, 255, 255, 255));
    }

    void draw_panel_title_with_close(const char* title, bool& open)
    {
        constexpr auto kH = 22.0f;
        auto* dl = ImGui::GetWindowDrawList();
        auto start = ImGui::GetCursorScreenPos();
        auto w = ImGui::GetContentRegionAvail().x;
        auto ts = ImGui::CalcTextSize(title);
        dl->AddText(ImVec2(start.x + (w - ts.x) * 0.5f, start.y + (kH - ts.y) * 0.5f),
                    IM_COL32(235, 238, 244, 255), title);
        ImGui::SetCursorScreenPos(ImVec2(start.x + w - 22.0f, start.y + 1.0f));
        ImGui::PushID(title);
        if (ImGui::Button("X", ImVec2(20.0f, 20.0f))) open = false;
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + kH + 12.0f));
    }

    const char* get_imgui_ini_path()
    {
        return config::ini_path().c_str();
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

    void write_imgui_float(const char* key, float value)
    {
        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%.1f", value);
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
        g_rewardButtonPosition.x = read_imgui_float(kRewardBtnXKey, g_rewardButtonPosition.x);
        g_rewardButtonPosition.y = read_imgui_float(kRewardBtnYKey, g_rewardButtonPosition.y);
        g_rewardBarPosition.x = read_imgui_float(kRewardBarXKey, -1.0f);
        g_rewardBarPosition.y = read_imgui_float(kRewardBarYKey, -1.0f);
        g_rouletteButtonPosition.x = read_imgui_float(kRouletteBtnXKey, g_rouletteButtonPosition.x);
        g_rouletteButtonPosition.y = read_imgui_float(kRouletteBtnYKey, g_rouletteButtonPosition.y);
        g_settingsButtonPosition.x = read_imgui_float(kSettingsBtnXKey, g_settingsButtonPosition.x);
        g_settingsButtonPosition.y = read_imgui_float(kSettingsBtnYKey, g_settingsButtonPosition.y);
        g_settingsPanelPosition.x = read_imgui_float(kSettingsPanelXKey, -1.0f);
        g_settingsPanelPosition.y = read_imgui_float(kSettingsPanelYKey, -1.0f);
        g_npcButtonPosition.x = read_imgui_float(kNpcBtnXKey, g_npcButtonPosition.x);
        g_npcButtonPosition.y = read_imgui_float(kNpcBtnYKey, g_npcButtonPosition.y);
        g_npcPanelPosition.x = read_imgui_float(kNpcPanelXKey, -1.0f);
        g_npcPanelPosition.y = read_imgui_float(kNpcPanelYKey, -1.0f);
        g_teleportButtonPosition.x = read_imgui_float(kTeleportBtnXKey, g_teleportButtonPosition.x);
        g_teleportButtonPosition.y = read_imgui_float(kTeleportBtnYKey, g_teleportButtonPosition.y);
        g_teleportPanelPosition.x = read_imgui_float(kTeleportPanelXKey, -1.0f);
        g_teleportPanelPosition.y = read_imgui_float(kTeleportPanelYKey, -1.0f);
        g_rewardAutoClaimEnabled = read_imgui_bool(kRewardAutoClaimKey, false);
        g_imguiSettingsLoaded = true;
    }

    void save_imgui_settings()
    {
        write_imgui_float(kPanelPosXKey, g_panelPosition.x);
        write_imgui_float(kPanelPosYKey, g_panelPosition.y);
        write_imgui_float(kRewardBtnXKey, g_rewardButtonPosition.x);
        write_imgui_float(kRewardBtnYKey, g_rewardButtonPosition.y);
        write_imgui_float(kRewardBarXKey, g_rewardBarPosition.x);
        write_imgui_float(kRewardBarYKey, g_rewardBarPosition.y);
        write_imgui_float(kRouletteBtnXKey, g_rouletteButtonPosition.x);
        write_imgui_float(kRouletteBtnYKey, g_rouletteButtonPosition.y);
        write_imgui_float(kSettingsBtnXKey, g_settingsButtonPosition.x);
        write_imgui_float(kSettingsBtnYKey, g_settingsButtonPosition.y);
        write_imgui_float(kSettingsPanelXKey, g_settingsPanelPosition.x);
        write_imgui_float(kSettingsPanelYKey, g_settingsPanelPosition.y);
        write_imgui_float(kNpcBtnXKey, g_npcButtonPosition.x);
        write_imgui_float(kNpcBtnYKey, g_npcButtonPosition.y);
        write_imgui_float(kNpcPanelXKey, g_npcPanelPosition.x);
        write_imgui_float(kNpcPanelYKey, g_npcPanelPosition.y);
        write_imgui_float(kTeleportBtnXKey, g_teleportButtonPosition.x);
        write_imgui_float(kTeleportBtnYKey, g_teleportButtonPosition.y);
        write_imgui_float(kTeleportPanelXKey, g_teleportPanelPosition.x);
        write_imgui_float(kTeleportPanelYKey, g_teleportPanelPosition.y);
        write_imgui_bool(kRewardAutoClaimKey, g_rewardAutoClaimEnabled);
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

    void run_settings_command(const char* text)
    {
        if (!text || !text[0])
            return;

        char buffer[32]{};
        std::snprintf(buffer, sizeof(buffer), "%s", text);
        command_handler(buffer);
    }

    static bool draw_settings_toggle_row(const char* label, bool enabled, const char* onCommand, const char* offCommand)
    {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(118.0f);

        auto color = enabled
            ? ImVec4(0.30f, 0.64f, 0.32f, 1.0f)
            : ImVec4(0.58f, 0.22f, 0.22f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            enabled ? ImVec4(0.38f, 0.74f, 0.40f, 1.0f) : ImVec4(0.70f, 0.28f, 0.28f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            enabled ? ImVec4(0.24f, 0.52f, 0.26f, 1.0f) : ImVec4(0.46f, 0.16f, 0.16f, 1.0f));

        char buttonLabel[64]{};
        std::snprintf(buttonLabel, sizeof(buttonLabel), "%s##settings_%s", enabled ? "ON" : "OFF", label);
        bool clicked = ImGui::Button(buttonLabel, ImVec2(56.0f, 22.0f));
        ImGui::PopStyleColor(3);

        if (clicked)
        {
            run_settings_command(enabled ? offCommand : onCommand);
            release_imgui_capture();
        }

        return clicked;
    }

    void draw_settings_panel()
    {
        if (!g_showSettingsPanel)
            return;

        auto& io = ImGui::GetIO();
        if (g_settingsPanelPosition.x < 0.0f || g_settingsPanelPosition.y < 0.0f)
        {
            auto panelPos = ImVec2(g_settingsButtonPosition.x + kSettingsButtonSize.x + 4.0f,
                                   g_settingsButtonPosition.y - 90.0f);
            g_settingsPanelPosition = clamp_window_position(panelPos, kSettingsPanelSize, io.DisplaySize);
            mark_imgui_settings_dirty();
        }
        else
        {
            g_settingsPanelPosition = clamp_window_position(g_settingsPanelPosition, kSettingsPanelSize, io.DisplaySize);
        }

        ImGui::SetNextWindowPos(g_settingsPanelPosition, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(kSettingsPanelSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(g_windowBgTexture ? 0.0f : 0.94f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
        if (ImGui::Begin(
            "##settings_panel",
            nullptr,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            draw_window_background();
            auto pos = ImGui::GetWindowPos();
            auto size = ImGui::GetWindowSize();
            remember_rect(g_settingsPanelRect, pos, ImVec2(pos.x + size.x, pos.y + size.y));
            if (std::fabs(pos.x - g_settingsPanelPosition.x) >= 0.5f
                || std::fabs(pos.y - g_settingsPanelPosition.y) >= 0.5f)
            {
                g_settingsPanelPosition = pos;
                mark_imgui_settings_dirty();
            }

            bool panelOpen = g_showSettingsPanel;
            draw_panel_title_with_close("Quick Settings", panelOpen);
            if (!panelOpen)
                g_showSettingsPanel = false;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));
            draw_settings_toggle_row("Wings", g_showWings != 0, "/wings on", "/wings off");
            draw_settings_toggle_row("Pets", g_showPets != 0, "/pets on", "/pets off");
            draw_settings_toggle_row("Costumes", g_showCostumes != 0, "/costumes on", "/costumes off");
            draw_settings_toggle_row("Titles", g_showTitles != 0, "/titles on", "/titles off");
            draw_settings_toggle_row("Colours", g_showNameColors != 0, "/colour on", "/colour off");
            draw_settings_toggle_row("FPS Boost", g_fpsBoost != 0, "/fpsboost on", "/fpsboost off");
            draw_settings_toggle_row("Effects", g_showEffects != 0, "/effects on", "/effects off");
            ImGui::PopStyleVar();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void draw_settings_button_overlay()
    {
        auto& io = ImGui::GetIO();
        if (!is_overlay_display_usable(io.DisplaySize))
            return;

        auto buttonSize = kSettingsButtonSize;
        g_settingsButtonPosition = clamp_window_position(g_settingsButtonPosition, buttonSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_settingsButtonPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            "##settings_button_overlay",
            nullptr,
            ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::InvisibleButton("##settings_toggle", buttonSize);

        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        remember_rect(g_settingsButtonRect, min, max);

        auto hovered = ImGui::IsItemHovered() || is_cursor_in_rect(g_settingsButtonRect);
        auto mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        auto mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            g_draggingSettingsButton = true;
            g_draggedSettingsButton = false;
            g_settingsButtonDragOffset = ImVec2(
                io.MousePos.x - g_settingsButtonPosition.x,
                io.MousePos.y - g_settingsButtonPosition.y);
        }

        if (g_draggingSettingsButton && mouseDown)
        {
            auto newPos = ImVec2(
                io.MousePos.x - g_settingsButtonDragOffset.x,
                io.MousePos.y - g_settingsButtonDragOffset.y);
            auto clamped = clamp_window_position(newPos, buttonSize, io.DisplaySize);
            if (std::fabs(clamped.x - g_settingsButtonPosition.x) > 1.0f
                || std::fabs(clamped.y - g_settingsButtonPosition.y) > 1.0f)
            {
                g_draggedSettingsButton = true;
                g_settingsButtonPosition = clamped;
            }
        }

        if (mouseReleased && g_draggingSettingsButton)
        {
            if (g_draggedSettingsButton)
                mark_imgui_settings_dirty();
            else if (hovered)
                g_showSettingsPanel = !g_showSettingsPanel;

            g_draggingSettingsButton = false;
            g_draggedSettingsButton = false;
            release_imgui_capture();
        }

        auto drawList = ImGui::GetWindowDrawList();
        auto* texture = load_settings_icon_texture();
        auto tint = IM_COL32(255, 255, 255, hovered ? 255 : 230);
        if (texture)
        {
            drawList->AddImage(reinterpret_cast<ImTextureID>(texture), min, max,
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
        }
        else
        {
            draw_icon_button_fallback(drawList, min, max, tint, IM_COL32(26, 31, 36, 225), "S");
        }

        if (hovered)
            drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 28), 4.0f);

        if (hovered)
        {
            ImGui::SetNextWindowPos(ImVec2(min.x - 2.0f, min.y - 24.0f), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(g_showSettingsPanel ? "Hide settings" : "Show settings");
            ImGui::EndTooltip();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    static bool draw_npc_panel_button(const char* label, int shortcutIndex)
    {
        if (ImGui::Button(label, ImVec2(-1.0f, 24.0f)))
        {
            open_remote_npc_shortcut(shortcutIndex);
            release_imgui_capture();
            return true;
        }

        return false;
    }

    void draw_npc_panel()
    {
        if (!g_showNpcPanel)
            return;

        auto& io = ImGui::GetIO();
        if (g_npcPanelPosition.x < 0.0f || g_npcPanelPosition.y < 0.0f)
        {
            auto panelPos = ImVec2(g_npcButtonPosition.x + kNpcButtonSize.x + 4.0f,
                                   g_npcButtonPosition.y - 90.0f);
            g_npcPanelPosition = clamp_window_position(panelPos, kNpcPanelSize, io.DisplaySize);
            mark_imgui_settings_dirty();
        }
        else
        {
            g_npcPanelPosition = clamp_window_position(g_npcPanelPosition, kNpcPanelSize, io.DisplaySize);
        }

        ImGui::SetNextWindowPos(g_npcPanelPosition, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(kNpcPanelSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(g_windowBgTexture ? 0.0f : 0.94f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
        if (ImGui::Begin(
            "##npc_panel",
            nullptr,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            draw_window_background();
            auto pos = ImGui::GetWindowPos();
            auto size = ImGui::GetWindowSize();
            remember_rect(g_npcPanelRect, pos, ImVec2(pos.x + size.x, pos.y + size.y));
            if (std::fabs(pos.x - g_npcPanelPosition.x) >= 0.5f
                || std::fabs(pos.y - g_npcPanelPosition.y) >= 0.5f)
            {
                g_npcPanelPosition = pos;
                mark_imgui_settings_dirty();
            }

            bool panelOpen = g_showNpcPanel;
            draw_panel_title_with_close("Remote NPC", panelOpen);
            if (!panelOpen)
                g_showNpcPanel = false;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 7.0f));
            draw_npc_panel_button("Market", 0);
            draw_npc_panel_button("Blacksmith", 1);
            draw_npc_panel_button("RR Blacksmith", 2);
            draw_npc_panel_button("Vet Manager", 3);
            draw_npc_panel_button("Bank", 4);
            draw_npc_panel_button("Guild Manager", 5);
            ImGui::PopStyleVar();
        }
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void draw_npc_button_overlay()
    {
        auto& io = ImGui::GetIO();
        if (!is_overlay_display_usable(io.DisplaySize))
            return;

        auto buttonSize = kNpcButtonSize;
        g_npcButtonPosition = clamp_window_position(g_npcButtonPosition, buttonSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_npcButtonPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            "##npc_button_overlay",
            nullptr,
            ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::InvisibleButton("##npc_toggle", buttonSize);

        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        remember_rect(g_npcButtonRect, min, max);

        auto hovered = ImGui::IsItemHovered() || is_cursor_in_rect(g_npcButtonRect);
        auto mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        auto mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            g_draggingNpcButton = true;
            g_draggedNpcButton = false;
            g_npcButtonDragOffset = ImVec2(
                io.MousePos.x - g_npcButtonPosition.x,
                io.MousePos.y - g_npcButtonPosition.y);
        }

        if (g_draggingNpcButton && mouseDown)
        {
            auto newPos = ImVec2(
                io.MousePos.x - g_npcButtonDragOffset.x,
                io.MousePos.y - g_npcButtonDragOffset.y);
            auto clamped = clamp_window_position(newPos, buttonSize, io.DisplaySize);
            if (std::fabs(clamped.x - g_npcButtonPosition.x) > 1.0f
                || std::fabs(clamped.y - g_npcButtonPosition.y) > 1.0f)
            {
                g_draggedNpcButton = true;
                g_npcButtonPosition = clamped;
            }
        }

        if (mouseReleased && g_draggingNpcButton)
        {
            if (g_draggedNpcButton)
                mark_imgui_settings_dirty();
            else if (hovered)
                g_showNpcPanel = !g_showNpcPanel;

            g_draggingNpcButton = false;
            g_draggedNpcButton = false;
            release_imgui_capture();
        }

        auto drawList = ImGui::GetWindowDrawList();
        auto* texture = load_npc_icon_texture();
        auto tint = IM_COL32(255, 255, 255, hovered ? 255 : 230);
        if (texture)
        {
            drawList->AddImage(reinterpret_cast<ImTextureID>(texture), min, max,
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
        }
        else
        {
            draw_icon_button_fallback(drawList, min, max, tint, IM_COL32(28, 28, 24, 225), "N");
        }

        if (hovered)
            drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 28), 4.0f);

        if (hovered)
        {
            ImGui::SetNextWindowPos(ImVec2(min.x - 2.0f, min.y - 24.0f), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(g_showNpcPanel ? "Hide NPCs" : "Show NPCs");
            ImGui::EndTooltip();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
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

        Static::SysMsgToChatBox(ChatType::Notice24, 12001, 12);
        g_sentWelcomeMessage = true;
        g_waitingWelcomeMessage = false;
    }

} // namespace imgui_layer
