#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <external/imgui/imgui.h>
#include "include/custom_chat.h"
#include "include/debug_panel.h"
#include "include/imgui_layer_internal.h"
#include "include/speed_monitor.h"

namespace debug_panel
{
    namespace
    {
        constexpr float kPanelW = 546.0f;
        constexpr float kPanelH = 400.0f;

        bool g_showDebugPanel = false;
        bool g_f9Down = false;
        bool g_stateLoaded = false;

        bool is_admin()
        {
            return g_pPlayerData && g_pPlayerData->isAdmin;
        }

        void load_state()
        {
            if (g_stateLoaded)
                return;

            g_stateLoaded = true;
            imgui_layer::g_idViewEnabled = imgui_layer::read_imgui_bool(
                imgui_layer::kIdViewEnabledKey,
                true);
        }

        void toggle_if_f9()
        {
            auto isDown = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
            if (isDown && !g_f9Down && is_admin())
                g_showDebugPanel = !g_showDebugPanel;
            g_f9Down = isDown;
        }
    }

    void render()
    {
        load_state();
        toggle_if_f9();

        if (!g_showDebugPanel || !is_admin())
            return;

        ImGui::SetNextWindowSize(ImVec2(kPanelW, kPanelH), ImGuiCond_FirstUseEver);
        auto flags = ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (!ImGui::Begin("GM Debug", &g_showDebugPanel, flags))
        {
            ImGui::End();
            return;
        }

        if (ImGui::Checkbox("ID View", &imgui_layer::g_idViewEnabled))
            imgui_layer::write_imgui_bool(
                imgui_layer::kIdViewEnabledKey,
                imgui_layer::g_idViewEnabled);
        ImGui::SameLine();
        ImGui::TextDisabled("(mob/NPC IDs)");

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Speed Monitor", ImGuiTreeNodeFlags_DefaultOpen))
            speed_monitor::render_options();

        ImGui::Separator();
        if (ImGui::CollapsingHeader("Chat options", ImGuiTreeNodeFlags_DefaultOpen))
            custom_chat::render_options();

        ImGui::End();
    }
}
