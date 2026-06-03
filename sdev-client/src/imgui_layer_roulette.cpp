#include "include/imgui_layer_internal.h"
#include <shaiya/include/network/game/outgoing/0800.h>
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/CNetwork.h"
#include "include/shaiya/ItemInfo.h"

namespace imgui_layer {

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
        auto itemCount = roulette_item_count();
        auto width = ImGui::GetContentRegionAvail().x;
        // Square area for the PNG background — use width as both dimensions
        auto bgSize = width;
        auto height = bgSize;
        auto now = GetTickCount();

        ImGui::Dummy(ImVec2(width, height));

        // Try to draw the PNG background; fall back to solid dark rect
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

    void draw_roulette_button_overlay()
    {
        auto& io = ImGui::GetIO();
        if (!is_overlay_display_usable(io.DisplaySize))
            return;

        auto buttonSize = kRouletteButtonSize;
        g_rouletteButtonPosition = clamp_window_position(g_rouletteButtonPosition, buttonSize, io.DisplaySize);

        ImGui::SetNextWindowPos(g_rouletteButtonPosition, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(
            "##roulette_button_overlay",
            nullptr,
            ImGuiWindowFlags_NoDecoration
                | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoFocusOnAppearing
                | ImGuiWindowFlags_NoBringToFrontOnFocus
                | ImGuiWindowFlags_AlwaysAutoResize);

        ImGui::InvisibleButton("##roulette_toggle", buttonSize);

        auto min = ImGui::GetItemRectMin();
        auto max = ImGui::GetItemRectMax();
        remember_rect(g_rouletteButtonRect, min, max);

        auto hovered = ImGui::IsItemHovered() || is_cursor_in_rect(g_rouletteButtonRect);
        auto mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        auto mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            g_draggingRouletteButton = true;
            g_draggedRouletteButton = false;
            g_rouletteButtonDragOffset = ImVec2(
                io.MousePos.x - g_rouletteButtonPosition.x,
                io.MousePos.y - g_rouletteButtonPosition.y);
        }

        if (g_draggingRouletteButton && mouseDown)
        {
            auto newPos = ImVec2(
                io.MousePos.x - g_rouletteButtonDragOffset.x,
                io.MousePos.y - g_rouletteButtonDragOffset.y);
            auto clamped = clamp_window_position(newPos, buttonSize, io.DisplaySize);
            if (std::fabs(clamped.x - g_rouletteButtonPosition.x) > 1.0f
                || std::fabs(clamped.y - g_rouletteButtonPosition.y) > 1.0f)
            {
                g_draggedRouletteButton = true;
                g_rouletteButtonPosition = clamped;
            }
        }

        if (mouseReleased && g_draggingRouletteButton)
        {
            if (g_draggedRouletteButton)
                mark_imgui_settings_dirty();
            else if (hovered)
                g_showPanel = !g_showPanel;

            g_draggingRouletteButton = false;
            g_draggedRouletteButton = false;
            release_imgui_capture();
        }

        auto drawList = ImGui::GetWindowDrawList();
        auto* texture = load_roulette_icon_texture();
        auto tint = IM_COL32(255, 255, 255, hovered ? 255 : 230);
        if (texture)
        {
            drawList->AddImage(reinterpret_cast<ImTextureID>(texture), min, max,
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
        }
        else
        {
            draw_icon_button_fallback(drawList, min, max, tint, IM_COL32(31, 24, 37, 225), "O");
        }

        if (hovered)
            drawList->AddRectFilled(min, max, IM_COL32(255, 255, 255, 28), 4.0f);

        if (hovered)
        {
            ImGui::SetNextWindowPos(ImVec2(min.x - 2.0f, min.y - 24.0f), ImGuiCond_Always);
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(g_showPanel ? "Hide roulette" : "Show roulette");
            ImGui::EndTooltip();
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
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

} // namespace imgui_layer
