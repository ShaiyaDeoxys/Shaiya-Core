#include "include/imgui_layer_internal.h"
#include "include/shaiya/CNetwork.h"
#include "include/shaiya/CPlayerData.h"
#include <shaiya/include/network/game/incoming/0800.h>

namespace imgui_layer {

    LPDIRECT3DTEXTURE9 load_teleport_icon_texture()
    {
        return load_saf_texture(g_teleportIconTexture, g_teleportIconLoadAttempted,
            g_teleportIconFound, g_teleportIconDataOffset, g_teleportIconDataSize);
    }

    void request_teleport_list()
    {
        auto hwnd = g_var->hwnd;
        if (hwnd && IsWindow(hwnd))
            PostMessageA(hwnd, kClientTeleportListWindowMessage, 0, 0);
    }

    void request_teleport_move(uint8_t index)
    {
        auto hwnd = g_var->hwnd;
        if (hwnd && IsWindow(hwnd))
            PostMessageA(hwnd, kClientTeleportMoveWindowMessage, static_cast<WPARAM>(index), 0);
    }

    static const char* teleport_result_text(GameTeleportMoveResult result)
    {
        switch (result)
        {
        case GameTeleportMoveResult::LevelTooLow:       return "Level too low";
        case GameTeleportMoveResult::LevelTooHigh:      return "Level too high";
        case GameTeleportMoveResult::AlreadyInMap:       return "Already in this map";
        case GameTeleportMoveResult::InvalidDestination: return "Invalid destination";
        case GameTeleportMoveResult::Dead:               return "Cannot teleport while dead";
        case GameTeleportMoveResult::Busy:               return "Busy";
        default:                                         return "";
        }
    }

    void draw_teleport_panel()
    {
        if (!g_showTeleportPanel)
            return;

        auto& io = ImGui::GetIO();
        if (g_teleportPanelPosition.x < 0.0f || g_teleportPanelPosition.y < 0.0f)
        {
            auto panelPos = ImVec2(g_teleportButtonPosition.x + kTeleportButtonSize.x + 4.0f,
                                   g_teleportButtonPosition.y - 120.0f);
            g_teleportPanelPosition = clamp_window_position(panelPos, kTeleportPanelSize, io.DisplaySize);
            mark_imgui_settings_dirty();
        }
        else
        {
            g_teleportPanelPosition = clamp_window_position(g_teleportPanelPosition, kTeleportPanelSize, io.DisplaySize);
        }

        // Dynamically size the panel based on destination count
        auto count = teleport_event::destinationCount;
        float contentHeight = 52.0f + static_cast<float>(count > 0 ? count : 1) * 30.0f;
        if (contentHeight < 80.0f)
            contentHeight = 80.0f;
        if (contentHeight > kTeleportPanelSize.y)
            contentHeight = kTeleportPanelSize.y;

        auto panelSize = ImVec2(kTeleportPanelSize.x, contentHeight);

        ImGui::SetNextWindowPos(g_teleportPanelPosition, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(panelSize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.94f);

        bool panelOpen = g_showTeleportPanel;
        if (ImGui::Begin(
            "Teleport",
            &panelOpen,
            ImGuiWindowFlags_NoCollapse
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            auto pos = ImGui::GetWindowPos();
            auto size = ImGui::GetWindowSize();
            remember_rect(g_teleportPanelRect, pos, ImVec2(pos.x + size.x, pos.y + size.y));
            if (std::fabs(pos.x - g_teleportPanelPosition.x) >= 0.5f
                || std::fabs(pos.y - g_teleportPanelPosition.y) >= 0.5f)
            {
                g_teleportPanelPosition = pos;
                mark_imgui_settings_dirty();
            }

            // Show result message briefly after a failed move
            if (teleport_event::lastMoveRequested
                && teleport_event::lastMoveResult != GameTeleportMoveResult::Success)
            {
                auto elapsed = GetTickCount() - teleport_event::lastMoveResultTick;
                if (elapsed < 3000)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255));
                    ImGui::TextWrapped("%s", teleport_result_text(teleport_event::lastMoveResult));
                    ImGui::PopStyleColor();
                }
            }

            if (!teleport_event::listReceived)
            {
                ImGui::TextDisabled("Loading...");
            }
            else if (count == 0)
            {
                ImGui::TextDisabled("No destinations configured");
            }
            else
            {
                auto playerLevel = g_pPlayerData ? g_pPlayerData->level : 0;
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 5.0f));

                for (uint8_t i = 0; i < count; ++i)
                {
                    const auto& dest = teleport_event::destinations[i];

                    // Null-terminate safely
                    char name[kTeleportNameLength + 1]{};
                    std::memcpy(name, dest.name, kTeleportNameLength);
                    name[kTeleportNameLength] = '\0';

                    // Check level requirements
                    bool levelOk = true;
                    if (dest.levelMin > 0 && playerLevel < dest.levelMin)
                        levelOk = false;
                    if (dest.levelMax > 0 && playerLevel > dest.levelMax)
                        levelOk = false;

                    // Build label with level range
                    char label[128]{};
                    if (dest.levelMin > 0 && dest.levelMax > 0)
                        std::snprintf(label, sizeof(label), "%s (Lv%u-%u)##tp_%u", name, dest.levelMin, dest.levelMax, i);
                    else if (dest.levelMin > 0)
                        std::snprintf(label, sizeof(label), "%s (Lv%u+)##tp_%u", name, dest.levelMin, i);
                    else if (dest.levelMax > 0)
                        std::snprintf(label, sizeof(label), "%s (Lv1-%u)##tp_%u", name, dest.levelMax, i);
                    else
                        std::snprintf(label, sizeof(label), "%s##tp_%u", name, i);

                    if (!levelOk)
                    {
                        ImGui::BeginDisabled();
                        ImGui::Button(label, ImVec2(-1.0f, 24.0f));
                        ImGui::EndDisabled();
                    }
                    else
                    {
                        if (ImGui::Button(label, ImVec2(-1.0f, 24.0f)))
                        {
                            request_teleport_move(i);
                            release_imgui_capture();
                        }
                    }
                }

                ImGui::PopStyleVar();
            }
        }
        ImGui::End();

        g_showTeleportPanel = panelOpen;
    }

    void draw_teleport_button_overlay()
    {
        auto& io = ImGui::GetIO();
        if (!is_overlay_display_usable(io.DisplaySize))
            return;

        auto buttonSize = kTeleportButtonSize;
        g_teleportButtonPosition = clamp_window_position(g_teleportButtonPosition, buttonSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_teleportButtonPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            "##teleport_button_overlay",
            nullptr,
            ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::InvisibleButton("##teleport_toggle", buttonSize);

        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        remember_rect(g_teleportButtonRect, min, max);

        auto hovered = ImGui::IsItemHovered() || is_cursor_in_rect(g_teleportButtonRect);
        auto mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        auto mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            g_draggingTeleportButton = true;
            g_draggedTeleportButton = false;
            g_teleportButtonDragOffset = ImVec2(
                io.MousePos.x - g_teleportButtonPosition.x,
                io.MousePos.y - g_teleportButtonPosition.y);
        }

        if (g_draggingTeleportButton && mouseDown)
        {
            auto newPos = ImVec2(
                io.MousePos.x - g_teleportButtonDragOffset.x,
                io.MousePos.y - g_teleportButtonDragOffset.y);
            auto clamped = clamp_window_position(newPos, buttonSize, io.DisplaySize);
            if (std::fabs(clamped.x - g_teleportButtonPosition.x) > 1.0f
                || std::fabs(clamped.y - g_teleportButtonPosition.y) > 1.0f)
            {
                g_draggedTeleportButton = true;
                g_teleportButtonPosition = clamped;
            }
        }

        if (mouseReleased && g_draggingTeleportButton)
        {
            if (g_draggedTeleportButton)
            {
                mark_imgui_settings_dirty();
            }
            else if (hovered)
            {
                g_showTeleportPanel = !g_showTeleportPanel;
                // Request fresh destination list when opening the panel
                if (g_showTeleportPanel)
                    request_teleport_list();
            }

            g_draggingTeleportButton = false;
            g_draggedTeleportButton = false;
            release_imgui_capture();
        }

        auto drawList = ImGui::GetWindowDrawList();
        auto* texture = load_teleport_icon_texture();
        auto tint = IM_COL32(255, 255, 255, hovered ? 255 : 230);
        if (texture)
        {
            drawList->AddImage(reinterpret_cast<ImTextureID>(texture), min, max,
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
        }
        else
        {
            draw_icon_button_fallback(drawList, min, max, tint, IM_COL32(34, 28, 42, 225), "T");
        }

        if (hovered)
            drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 28), 4.0f);

        if (hovered)
        {
            ImGui::SetNextWindowPos(ImVec2(min.x - 2.0f, min.y - 24.0f), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(g_showTeleportPanel ? "Hide teleport" : "Show teleport");
            ImGui::EndTooltip();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

} // namespace imgui_layer
