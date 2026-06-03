#include "include/imgui_layer_internal.h"
#include <shaiya/include/network/game/incoming/1F00.h>
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/CNetwork.h"
#include "include/shaiya/ItemInfo.h"

namespace imgui_layer
{
    // =======================================================================
    //  Reward Bar — time-played reward system
    // =======================================================================

    float reward_item_progress_ratio()
    {
        if (!reward_item_event::hasList || reward_item_event::itemCount == 0)
            return 0.0f;
        if (reward_item_event::claimReady)
            return 1.0f;
        if (reward_item_event::timerStartTick == 0 || reward_item_event::timerDurationMs == 0)
            return 0.0f;

        uint32_t now = GetTickCount();
        uint32_t elapsed = now - reward_item_event::timerStartTick;
        if (elapsed >= reward_item_event::timerDurationMs)
            return 1.0f;

        return static_cast<float>(elapsed) / static_cast<float>(reward_item_event::timerDurationMs);
    }

    uint32_t reward_item_remaining_ms()
    {
        if (!reward_item_event::hasList || reward_item_event::itemCount == 0)
            return 0;
        if (reward_item_event::claimReady)
            return 0;
        if (reward_item_event::timerStartTick == 0 || reward_item_event::timerDurationMs == 0)
            return 0;

        uint32_t now = GetTickCount();
        uint32_t elapsed = now - reward_item_event::timerStartTick;
        if (elapsed >= reward_item_event::timerDurationMs)
            return 0;

        return reward_item_event::timerDurationMs - elapsed;
    }

    void update_reward_auto_claim()
    {
        if (!reward_item_event::claimReady)
        {
            g_rewardReadyTick = 0;
            g_rewardNextAutoClaimTick = 0;
            return;
        }

        uint32_t now = GetTickCount();
        if (g_rewardReadyTick == 0)
        {
            g_rewardReadyTick = now;
            g_rewardNextAutoClaimTick = now + 5000U;
            return;
        }

        if (!g_rewardAutoClaimEnabled)
            return;

        if (static_cast<int32_t>(now - g_rewardNextAutoClaimTick) < 0)
            return;

        GameRewardItemGetIncoming outgoing{};
        CNetwork::Send(&outgoing, sizeof(outgoing));
        g_rewardNextAutoClaimTick = now + 3600000U;
    }

    // Draw a single reward row: icon + name + progress bar.
    // Returns true if the icon was hovered (for tooltip).
    static void draw_reward_row(uint32_t rowIndex, uint32_t currentIndex,
                                float currentProgress, uint32_t currentRemainingMs,
                                bool currentClaimReady)
    {
        const auto& item = reward_item_event::items[rowIndex];
        auto* info = CDataFile::GetItemInfo(item.type, item.typeId);

        bool claimed = rowIndex < currentIndex;
        bool current = rowIndex == currentIndex;
        bool future  = rowIndex > currentIndex;

        constexpr float kIconSize = 22.0f;
        constexpr float kRowBarH  = 10.0f;

        // Dim future rows
        if (future)
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.45f);

        // --- Icon ---
        char iconId[32];
        std::snprintf(iconId, sizeof(iconId), "##rw_icon_%u", rowIndex);
        ImVec2 iconMin = ImGui::GetCursorScreenPos();
        ImVec2 iconMax(iconMin.x + kIconSize, iconMin.y + kIconSize);
        ImGui::InvisibleButton(iconId, ImVec2(kIconSize, kIconSize));

        auto* dl = ImGui::GetWindowDrawList();
        draw_item_icon_at(dl, iconMin, iconMax, item.type, item.typeId);
        if (item.count > 1)
            draw_item_count_badge(dl, iconMin, iconMax, item.count);

        // Completed checkmark overlay
        if (claimed)
        {
            dl->AddRectFilled(iconMin, iconMax, IM_COL32(30, 80, 30, 140), 2.0f);
            auto checkColor = IM_COL32(150, 255, 150, 255);
            dl->AddLine(
                ImVec2(iconMin.x + 5.0f, iconMin.y + 12.0f),
                ImVec2(iconMin.x + 9.0f, iconMin.y + 17.0f),
                checkColor, 2.0f);
            dl->AddLine(
                ImVec2(iconMin.x + 9.0f, iconMin.y + 17.0f),
                ImVec2(iconMin.x + 17.0f, iconMin.y + 6.0f),
                checkColor, 2.0f);
        }

        // Tooltip on icon hover
        if (info && ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::TextColored(ImVec4(0.93f, 0.77f, 0.28f, 1.0f),
                "%s", info->name ? info->name : "Reward");
            if (item.count > 1)
                ImGui::Text("x%u", static_cast<unsigned>(item.count));
            if (info->description && info->description[0])
            {
                ImGui::Separator();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
                ImGui::TextUnformatted(info->description);
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTooltip();
        }

        // --- Name + bar to the right of the icon ---
        ImGui::SameLine();
        ImGui::BeginGroup();

        const char* name = (info && info->name) ? info->name : "Reward";
        auto textAvail = std::max(40.0f, ImGui::GetContentRegionAvail().x);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textAvail);
        ImGui::TextUnformatted(name);
        ImGui::PopTextWrapPos();

        // Progress bar
        if (claimed)
        {
            ImGui::Dummy(ImVec2(-1.0f, kRowBarH));
        }
        else if (current)
        {
            if (currentClaimReady)
            {
                // Ready to claim: pulsing gold bar
                float pulse = (std::sin(static_cast<float>(GetTickCount()) * 0.004f) + 1.0f) * 0.5f;
                float r = 0.70f + pulse * 0.15f;
                float g = 0.55f + pulse * 0.15f;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(r, g, 0.15f, 1.0f));
                ImGui::ProgressBar(1.0f, ImVec2(-1.0f, kRowBarH), "Claim!");
                ImGui::PopStyleColor();
            }
            else
            {
                // In progress: timer
                uint32_t mins = currentRemainingMs / 60000U;
                uint32_t secs = (currentRemainingMs / 1000U) % 60U;
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%02u:%02u", mins, secs);
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.72f, 0.52f, 0.18f, 1.0f));
                ImGui::ProgressBar(currentProgress, ImVec2(-1.0f, kRowBarH), buf);
                ImGui::PopStyleColor();
            }
        }
        else
        {
            // Future: empty bar with time requirement
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%u min", item.minutes);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.30f, 0.30f, 0.30f, 0.6f));
            ImGui::ProgressBar(0.0f, ImVec2(-1.0f, kRowBarH), buf);
            ImGui::PopStyleColor();
        }

        ImGui::EndGroup();

        if (future)
            ImGui::PopStyleVar();
    }

    void draw_reward_bar()
    {
        if (!g_showRewardBar)
            return;

        constexpr float kRewardBarW = 210.0f;
        constexpr float kRewardBarH = 270.0f;
        const auto rewardBarSizeFixed = ImVec2(kRewardBarW, kRewardBarH);

        auto& io = ImGui::GetIO();
        if (g_rewardBarPosition.x < 0.0f || g_rewardBarPosition.y < 0.0f)
        {
            auto panelPos = ImVec2(g_rewardButtonPosition.x + kRewardButtonSize.x + 4.0f,
                                   g_rewardButtonPosition.y - 90.0f);
            g_rewardBarPosition = clamp_window_position(panelPos, rewardBarSizeFixed, io.DisplaySize);
            mark_imgui_settings_dirty();
        }
        else
        {
            g_rewardBarPosition = clamp_window_position(g_rewardBarPosition, rewardBarSizeFixed, io.DisplaySize);
        }

        ImGui::SetNextWindowSize(rewardBarSizeFixed, ImGuiCond_Always);
        ImGui::SetNextWindowPos(g_rewardBarPosition, ImGuiCond_Appearing);
        ImGui::SetNextWindowBgAlpha(g_windowBgTexture ? 0.0f : 0.94f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
        if (!ImGui::Begin(
            "##reward_panel",
            nullptr,
            ImGuiWindowFlags_NoTitleBar
                | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus))
        {
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
        draw_window_background();

        auto rewardBarPos = ImGui::GetWindowPos();
        auto rewardBarSize = ImGui::GetWindowSize();
        remember_rect(g_rewardBarRect, rewardBarPos,
            ImVec2(rewardBarPos.x + rewardBarSize.x, rewardBarPos.y + rewardBarSize.y));
        if (std::fabs(rewardBarPos.x - g_rewardBarPosition.x) >= 0.5f
            || std::fabs(rewardBarPos.y - g_rewardBarPosition.y) >= 0.5f)
        {
            g_rewardBarPosition = rewardBarPos;
            mark_imgui_settings_dirty();
        }

        bool panelOpen = g_showRewardBar;
        draw_panel_title_with_close("Rewards", panelOpen);
        if (!panelOpen)
            g_showRewardBar = false;
        ImGui::SetCursorPosY(std::max(0.0f, ImGui::GetCursorPosY() - 6.0f));

        if (!reward_item_event::hasList || reward_item_event::itemCount == 0)
        {
            ImGui::TextDisabled("Waiting for server rewards...");
            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }

        uint32_t safeIndex = reward_item_event::index;
        if (safeIndex >= reward_item_event::itemCount)
            safeIndex = 0;

        float progress = reward_item_progress_ratio();
        uint32_t remainingMs = reward_item_remaining_ms();

        // --- Claim button + auto-claim at the top ---
        if (reward_item_event::claimReady)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.36f, 0.60f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.44f, 0.72f, 0.34f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.48f, 0.22f, 1.0f));
            if (ImGui::Button("Claim Reward", ImVec2(-1.0f, 24.0f)))
            {
                GameRewardItemGetIncoming pkt{};
                CNetwork::Send(&pkt, sizeof(pkt));
            }
            ImGui::PopStyleColor(3);
        }
        else
        {
            ImGui::BeginDisabled();
            ImGui::Button("Claim Reward", ImVec2(-1.0f, 24.0f));
            ImGui::EndDisabled();
        }

        // Auto-claim row
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 2.0f));
        if (ImGui::Checkbox("Auto-claim", &g_rewardAutoClaimEnabled))
            mark_imgui_settings_dirty();
        ImGui::PopStyleVar();

        if (g_rewardAutoClaimEnabled && reward_item_event::claimReady && g_rewardNextAutoClaimTick != 0)
        {
            uint32_t now = GetTickCount();
            if (static_cast<int32_t>(g_rewardNextAutoClaimTick - now) > 0)
            {
                uint32_t remSec = (g_rewardNextAutoClaimTick - now) / 1000U;
                ImGui::SameLine();
                ImGui::TextDisabled("(%us)", remSec);
            }
        }

        ImGui::Separator();

        // --- Scrollable reward list ---
        ImGui::BeginChild("##reward_list", ImVec2(0.0f, 0.0f), false);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 4.0f));

        for (uint32_t i = 0; i < reward_item_event::itemCount && i < 20; ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            draw_reward_row(i, safeIndex, progress, remainingMs,
                            reward_item_event::claimReady);
            ImGui::PopID();

            if (i + 1 < reward_item_event::itemCount)
                ImGui::Separator();
        }

        ImGui::PopStyleVar();
        ImGui::EndChild();

        // Auto-scroll to keep the current reward visible
        // (scroll the child so the current row is roughly centered)
        // Only on first open or when index changes.

        ImGui::End();
        ImGui::PopStyleVar();
    }



    void draw_reward_button_overlay()
    {
        auto& io = ImGui::GetIO();
        if (!is_overlay_display_usable(io.DisplaySize))
            return;

        auto buttonSize = kRewardButtonSize;
        g_rewardButtonPosition = clamp_window_position(g_rewardButtonPosition, buttonSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_rewardButtonPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            "##reward_button_overlay",
            nullptr,
            ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::InvisibleButton("##reward_toggle", buttonSize);

        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        remember_rect(g_rewardButtonRect, min, max);

        auto now = GetTickCount();
        auto hovered = ImGui::IsItemHovered() || is_cursor_in_rect(g_rewardButtonRect);
        auto mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        auto mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            g_draggingRewardButton = true;
            g_draggedRewardButton = false;
            g_rewardButtonDragOffset = ImVec2(
                io.MousePos.x - g_rewardButtonPosition.x,
                io.MousePos.y - g_rewardButtonPosition.y);
        }

        if (g_draggingRewardButton && mouseDown)
        {
            auto newPos = ImVec2(
                io.MousePos.x - g_rewardButtonDragOffset.x,
                io.MousePos.y - g_rewardButtonDragOffset.y);
            auto clamped = clamp_window_position(newPos, buttonSize, io.DisplaySize);
            if (std::fabs(clamped.x - g_rewardButtonPosition.x) > 1.0f
                || std::fabs(clamped.y - g_rewardButtonPosition.y) > 1.0f)
            {
                g_draggedRewardButton = true;
                g_rewardButtonPosition = clamped;
            }
        }

        if (mouseReleased && g_draggingRewardButton)
        {
            if (g_draggedRewardButton)
                mark_imgui_settings_dirty();
            else if (hovered)
                g_showRewardBar = !g_showRewardBar;

            g_draggingRewardButton = false;
            g_draggedRewardButton = false;
            release_imgui_capture();
        }

        auto drawList = ImGui::GetWindowDrawList();
        auto* texture = load_reward_icon_texture();
        int blink = 0;
        if (reward_item_event::claimReady)
        {
            auto wave = (std::sin(static_cast<float>(now) * 0.005f) + 1.0f) * 0.5f;
            blink = static_cast<int>(wave * 45.0f);
        }

        auto alpha = static_cast<int>(hovered ? 255 : 230) + blink;
        if (alpha > 255)
            alpha = 255;

        auto tint = IM_COL32(255, 255, 255, alpha);
        if (texture)
        {
            drawList->AddImage(reinterpret_cast<ImTextureID>(texture), min, max,
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
        }
        else
        {
            draw_icon_button_fallback(drawList, min, max, tint, IM_COL32(38, 31, 20, 225), "R");
        }

        if (hovered)
            drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 28), 4.0f);

        if (reward_item_event::claimReady)
            drawList->AddRect(min, max, IM_COL32(255, 220, 120, 90 + blink), 4.0f, 0, 1.5f);

        if (hovered)
        {
            ImGui::SetNextWindowPos(ImVec2(min.x - 2.0f, min.y - 24.0f), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(g_showRewardBar ? "Hide rewards" : "Show rewards");
            ImGui::EndTooltip();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

} // namespace imgui_layer
