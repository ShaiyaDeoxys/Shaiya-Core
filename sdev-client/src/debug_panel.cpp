#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include <external/imgui/imgui.h>
#include "include/main.h"
#include "include/shaiya/CPlayerData.h"
#include "include/shaiya/Static.h"

using namespace shaiya;

// ---------------------------------------------------------------------------
// F9 GM debug panel — admin-only chat type monitor.
//
// Records incoming chat types with preview text so GMs can identify which
// chat type IDs correspond to which visual messages.  Useful for tuning
// upper-bar exclusions and verifying custom chat routing.
//
// The monitor only runs when the user explicitly activates it from the
// panel (per-session toggle, no CONFIG.ini persistence).
// ---------------------------------------------------------------------------

namespace debug_panel
{
    // -----------------------------------------------------------------------
    // Panel state
    // -----------------------------------------------------------------------
    static bool g_showDebugPanel = false;
    static bool g_f9Down = false;

    static constexpr float kPanelW = 546.0f;
    static constexpr float kPanelH = 400.0f;

    // -----------------------------------------------------------------------
    // Chat Type Monitor
    // -----------------------------------------------------------------------
    static bool g_chatMonitorActive = false;

    struct ChatTypeEntry
    {
        int chatType;
        DWORD tick;
        char preview[64];
    };

    static constexpr int kMaxChatEntries = 64;
    static ChatTypeEntry g_chatTypeRing[kMaxChatEntries]{};
    static int g_chatTypeHead = 0;
    static int g_chatTypeCount = 0;

    void record_chat_type(int chatType, const char* text)
    {
        if (!g_chatMonitorActive)
            return;

        auto& entry = g_chatTypeRing[g_chatTypeHead % kMaxChatEntries];
        entry.chatType = chatType;
        entry.tick = GetTickCount();
        if (text)
        {
            strncpy_s(entry.preview, text, _TRUNCATE);
            entry.preview[sizeof(entry.preview) - 1] = '\0';
        }
        else
        {
            entry.preview[0] = '\0';
        }
        g_chatTypeHead = (g_chatTypeHead + 1) % kMaxChatEntries;
        if (g_chatTypeCount < kMaxChatEntries)
            ++g_chatTypeCount;
    }

    static void clear_chat_entries()
    {
        g_chatTypeHead = 0;
        g_chatTypeCount = 0;
    }

    static void draw_chat_monitor()
    {
        ImGui::Checkbox("Active##chat", &g_chatMonitorActive);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##chat"))
            clear_chat_entries();

        ImGui::Separator();

        if (!g_chatMonitorActive && g_chatTypeCount == 0)
        {
            ImGui::TextDisabled("Enable to start recording chat types.");
            return;
        }

        if (g_chatTypeCount == 0)
        {
            ImGui::TextDisabled("Waiting for chat messages...");
            return;
        }

        ImGui::Columns(3, "chat_types_cols", true);
        ImGui::SetColumnWidth(0, 50.0f);
        ImGui::SetColumnWidth(1, 80.0f);
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Type");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Ago");
        ImGui::NextColumn();
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Preview");
        ImGui::NextColumn();
        ImGui::Separator();

        auto now = GetTickCount();
        for (int i = 0; i < g_chatTypeCount; ++i)
        {
            auto idx = (g_chatTypeHead - 1 - i + kMaxChatEntries * 2) % kMaxChatEntries;
            auto& entry = g_chatTypeRing[idx];

            auto type = entry.chatType;
            ImVec4 typeColor(1.0f, 1.0f, 1.0f, 1.0f);
            if (type == 15)      typeColor = ImVec4(1.0f, 0.6f, 0.0f, 1.0f);
            else if (type == 16 || type == 17) typeColor = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
            else if (type == 18) typeColor = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);
            else if (type == 19) typeColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
            else if (type == 20) typeColor = ImVec4(0.7f, 0.3f, 1.0f, 1.0f);
            else if (type == 21) typeColor = ImVec4(0.5f, 0.8f, 1.0f, 1.0f);
            else if (type == 22) typeColor = ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
            else if (type == 34) typeColor = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

            ImGui::TextColored(typeColor, "%d", type);
            ImGui::NextColumn();

            auto elapsed = now - entry.tick;
            if (elapsed < 60000)
                ImGui::Text("%ds", elapsed / 1000);
            else
                ImGui::Text("%dm%ds", elapsed / 60000, (elapsed % 60000) / 1000);
            ImGui::NextColumn();

            ImGui::TextUnformatted(entry.preview);
            ImGui::NextColumn();
        }

        ImGui::Columns(1);
    }

    // -----------------------------------------------------------------------
    // Panel entry points
    // -----------------------------------------------------------------------
    static bool is_admin()
    {
        return g_pPlayerData && g_pPlayerData->isAdmin;
    }

    void toggle_if_f9(bool& wasDown)
    {
        auto isDown = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        if (isDown && !wasDown && is_admin())
            g_showDebugPanel = !g_showDebugPanel;
        wasDown = isDown;
    }

    void render()
    {
        toggle_if_f9(g_f9Down);

        if (!g_showDebugPanel || !is_admin())
            return;

        ImGui::SetNextWindowSize(ImVec2(kPanelW, kPanelH), ImGuiCond_FirstUseEver);
        auto flags = ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (!ImGui::Begin("GM Debug", &g_showDebugPanel, flags))
        {
            ImGui::End();
            return;
        }

        draw_chat_monitor();

        ImGui::End();
    }
}
