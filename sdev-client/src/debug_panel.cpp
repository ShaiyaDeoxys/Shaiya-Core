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
        constexpr float kPanelW = 680.0f;
        constexpr float kPanelH = 480.0f;

        bool g_showDebugPanel = false;
        bool g_f9Down = false;
        bool g_stateLoaded = false;
        int g_openModule = 1;

        enum DebugModule
        {
            kModuleIdView,
            kModuleSpeedMonitor,
            kModuleChatOptions,
            kModuleCount
        };

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

        const char* module_title(int module)
        {
            switch (module)
            {
            case kModuleIdView:
                return "ID View";
            case kModuleSpeedMonitor:
                return "Speed Monitor";
            case kModuleChatOptions:
                return "Chat options";
            default:
                return "";
            }
        }

        void render_module_button(int module, float width)
        {
            auto selected = g_openModule == module;

            ImGui::PushID(module);
            if (selected)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));

            if (ImGui::Button(module_title(module), ImVec2(width, 28.0f)))
                g_openModule = module;

            if (selected)
                ImGui::PopStyleColor();

            ImGui::PopID();
        }

        void render_id_view_options()
        {
            if (ImGui::Checkbox("Enabled", &imgui_layer::g_idViewEnabled))
                imgui_layer::write_imgui_bool(
                    imgui_layer::kIdViewEnabledKey,
                    imgui_layer::g_idViewEnabled);

            ImGui::SameLine();
            ImGui::TextDisabled("mob/NPC IDs");
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

        auto spacing = ImGui::GetStyle().ItemSpacing.x;
        auto availW = ImGui::GetContentRegionAvail().x;
        auto moduleButtonW = (availW - spacing * static_cast<float>(kModuleCount - 1)) / static_cast<float>(kModuleCount);

        for (auto module = 0; module < kModuleCount; ++module)
        {
            render_module_button(module, moduleButtonW);
            if (module + 1 < kModuleCount)
                ImGui::SameLine();
        }

        ImGui::Separator();

        ImGui::TextUnformatted(module_title(g_openModule));
        ImGui::BeginChild("module_content", ImVec2(0.0f, 0.0f), true);

        switch (g_openModule)
        {
        case kModuleIdView:
            render_id_view_options();
            break;
        case kModuleSpeedMonitor:
            speed_monitor::render_options();
            break;
        case kModuleChatOptions:
            custom_chat::render_options();
            break;
        }

        ImGui::EndChild();
        ImGui::End();
    }
}
