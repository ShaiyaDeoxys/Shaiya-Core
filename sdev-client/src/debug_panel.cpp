#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <external/imgui/imgui.h>
#include "include/custom_chat.h"
#include "include/debug_panel.h"
#include "include/imgui_layer_internal.h"
#include "include/speed_monitor.h"
#include "include/shaiya/CDataFile.h"
#include "include/shaiya/ItemInfo.h"
#include "include/shaiya/Vehicle.h"

namespace vehicle
{
    shaiya::Vehicle* get_vehicle(int model);
}

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
            kModuleMountUtility,
            kModuleEffectPlayer,
            kModuleSceneDetector,
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
            case kModuleMountUtility:
                return "Mounts";
            case kModuleEffectPlayer:
                return "Effects";
            case kModuleSceneDetector:
                return "Scene";
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

        // --- Effect Player ---
        // Play any game effect by ID on the local player's position.

        void render_effect_player()
        {
            using namespace shaiya;

            static int effectId = 1;
            static int effectSub = 0;
            static int playMode = 0;    // 0 = world pos, 1 = attached to char

            auto* user = g_pWorldMgr ? g_pWorldMgr->user : nullptr;

            ImGui::TextDisabled("Play client-side effects by ID.");
            ImGui::Spacing();

            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Effect ID", &effectId, 1, 10);
            effectId = std::max(0, effectId);

            ImGui::SetNextItemWidth(120.0f);
            ImGui::InputInt("Sub ID", &effectSub, 1, 5);
            effectSub = std::max(0, effectSub);

            ImGui::RadioButton("World position", &playMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Attached to character", &playMode, 1);

            ImGui::Spacing();

            bool canPlay = user != nullptr;
            ImGui::BeginDisabled(!canPlay);

            if (ImGui::Button("Play effect", ImVec2(140.0f, 28.0f)) && user)
            {
                if (playMode == 1)
                {
                    // Attach to local character — follows movement
                    CCharacter::RenderEffect(
                        user, effectId, effectSub,
                        0.0f,
                        &user->pos, &user->dir, &user->up, 0);
                }
                else
                {
                    // Spawn at world position — stays in place
                    CWorldMgr::RenderEffect(
                        effectId, effectSub,
                        &user->pos, &user->dir, &user->up, 0);
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Clear effects", ImVec2(140.0f, 28.0f)) && user)
            {
                CCharacter::ClearEffects(user);
            }

            ImGui::EndDisabled();

            if (!canPlay)
                ImGui::TextDisabled("Enter the game world first.");
            else
            {
                ImGui::Spacing();
                ImGui::TextDisabled(
                    "Pos: %.0f, %.0f, %.0f",
                    user->pos.x, user->pos.y, user->pos.z);
            }
        }

        // --- Scene Detector ---
        // Live readout of the game screen state at 0x7C48F8.

        const char* screen_state_label(int state)
        {
            switch (state)
            {
            case 1:  return "Unknown (1)";
            case 2:  return "Unknown (2)";
            case 3:  return "Character select";
            case 4:  return "Character creation";
            case 5:  return "Loading screen";
            case 6:  return "In-game";
            default: return "Unknown";
            }
        }

        void render_scene_detector()
        {
            auto state = *reinterpret_cast<const int*>(
                imgui_layer::kGameScreenStateAddr);

            ImGui::TextDisabled("Live readout of 0x7C48F8 (game screen state).");
            ImGui::Spacing();

            auto* label = screen_state_label(state);
            ImGui::Text("Screen state: %d  —  %s", state, label);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Text("is_game_scene():        %s",
                imgui_layer::is_game_scene() ? "true" : "false");
            ImGui::Text("is_game_scene_stable(): %s",
                imgui_layer::is_game_scene_stable() ? "true" : "false");
            ImGui::Text("Stable frames:          %d / %d",
                imgui_layer::g_sceneStableFrames,
                imgui_layer::kMinStableFrames);
            ImGui::Text("Native UI hidden (F11): %s",
                imgui_layer::g_nativeUIHidden ? "yes" : "no");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextDisabled("Known values:");
            ImGui::BulletText("3 = Character select");
            ImGui::BulletText("4 = Character creation");
            ImGui::BulletText("5 = Loading screen");
            ImGui::BulletText("6 = In-game");
        }

        // --- Mount Utility ---
        // Edit bone1 (reqRec) and bone2 (reqInt) on vehicle items live.
        // Changes update both the Vehicle runtime vector and the in-memory
        // ItemInfo, so the rider position updates on the next mount cycle.

        void render_mount_utility()
        {
            using namespace shaiya;

            auto& vehicles = g_vehicles;
            if (vehicles.empty())
            {
                ImGui::TextDisabled("No custom mounts loaded (model >= 33).");
                return;
            }

            ImGui::TextDisabled(
                "Edit bone indices for rider positioning.  Changes apply live.");
            ImGui::Spacing();

            // Column layout: Model | Bone1 (reqRec) | Bone2 (reqInt)
            if (ImGui::BeginTable(
                    "##mount_table", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                        | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                    ImVec2(0.0f, 0.0f)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Model", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableSetupColumn("Bone1 (reqRec)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Bone2 (reqInt)", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 50.0f);
                ImGui::TableHeadersRow();

                for (std::size_t i = 0; i < vehicles.size(); ++i)
                {
                    auto& v = vehicles[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::TableNextRow();

                    // Model column
                    ImGui::TableNextColumn();
                    ImGui::Text("%d", v.model);

                    // Bone1 column
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    int b1 = v.bone1;
                    if (ImGui::InputInt("##b1", &b1, 1, 5))
                    {
                        b1 = std::clamp(b1, 0, 255);
                        v.bone1 = b1;

                        // Sync back to the in-memory ItemInfo so vehicle::init
                        // data and the game's item DB stay consistent.
                        for (int tid = 1; tid <= 255; ++tid)
                        {
                            auto* info = CDataFile::GetItemInfo(
                                static_cast<int>(ItemType::Vehicle), tid);
                            if (info && info->vehicleModel == static_cast<uint8_t>(v.model))
                            {
                                info->reqRec = static_cast<uint16_t>(b1);
                                break;
                            }
                        }
                    }

                    // Bone2 column
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    int b2 = v.bone2;
                    if (ImGui::InputInt("##b2", &b2, 1, 5))
                    {
                        b2 = std::clamp(b2, 0, 255);
                        v.bone2 = b2;

                        for (int tid = 1; tid <= 255; ++tid)
                        {
                            auto* info = CDataFile::GetItemInfo(
                                static_cast<int>(ItemType::Vehicle), tid);
                            if (info && info->vehicleModel == static_cast<uint8_t>(v.model))
                            {
                                info->reqInt = static_cast<uint16_t>(b2);
                                break;
                            }
                        }
                    }

                    // Reset button
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("Reset"))
                    {
                        // Re-read original values from ItemInfo
                        for (int tid = 1; tid <= 255; ++tid)
                        {
                            auto* info = CDataFile::GetItemInfo(
                                static_cast<int>(ItemType::Vehicle), tid);
                            if (info && info->vehicleModel == static_cast<uint8_t>(v.model))
                            {
                                v.bone1 = info->reqRec;
                                v.bone2 = info->reqInt;
                                break;
                            }
                        }
                    }

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            // Current mount info for the local player
            auto* user = g_pWorldMgr ? g_pWorldMgr->user : nullptr;
            if (user && user->vehicleModel > 0)
            {
                ImGui::Separator();
                ImGui::Text("Current mount: model %d", user->vehicleModel);

                auto* veh = vehicle::get_vehicle(user->vehicleModel);
                if (veh)
                    ImGui::Text("Active bones: %d / %d", veh->bone1, veh->bone2);
                else
                    ImGui::TextDisabled("(not a custom mount)");
            }
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
        case kModuleMountUtility:
            render_mount_utility();
            break;
        case kModuleEffectPlayer:
            render_effect_player();
            break;
        case kModuleSceneDetector:
            render_scene_detector();
            break;
        }

        ImGui::EndChild();
        ImGui::End();
    }
}
