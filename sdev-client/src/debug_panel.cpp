#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <string>
#include <external/imgui/imgui.h>
#include "include/imgui_layer_internal.h"

using namespace shaiya;

// ---------------------------------------------------------------------------
// Minimal GM panel: ID View plus chat options for the parallel chat overlay.
// ---------------------------------------------------------------------------

namespace debug_panel
{
    using namespace imgui_layer;

    // -----------------------------------------------------------------------
    // Panel state
    // -----------------------------------------------------------------------
    static bool g_showDebugPanel = false;
    static bool g_f9Down = false;

    static constexpr float kPanelW = 546.0f;
    static constexpr float kPanelH = 400.0f;
    // -----------------------------------------------------------------------
    static bool g_parallelStateLoaded = false;

    static void load_parallel_persisted_state()
    {
        if (g_parallelStateLoaded) return;
        g_parallelStateLoaded = true;
        using namespace imgui_layer;
        g_parallelChatActive = read_imgui_bool(kParallelChatActiveKey, false);
        g_ingameChatActive = read_imgui_bool(kIngameChatActiveKey, false);
        g_hideNativeChatVisuals = read_imgui_bool(kHideNativeChatKey, false);
        g_idViewEnabled = read_imgui_bool(kIdViewEnabledKey, true);
        load_chat_window_layout();
    }

    struct ParallelChatMsg
    {
        int  chatType;
        DWORD tick;
        char text[512];
    };

    static constexpr int kMaxParallelMsgs = 200;
    static constexpr int kMaxVisiblePerWindow = 50;  // render at most ~50 msgs per panel (native feel)
    static ParallelChatMsg g_parallelRing[kMaxParallelMsgs]{};
    static int g_parallelHead  = 0;
    static int g_parallelCount = 0;
    static bool g_parallelColorCustom[100]{};
    static ImVec4 g_parallelColor[100]{};

    static ImFont* get_parallel_font()
    {
        using namespace imgui_layer;
        if (g_parallelFontLoaded)
            return g_parallelFont;

        g_parallelFontLoaded = true;

        char windowsDir[MAX_PATH]{};
        if (!GetWindowsDirectoryA(windowsDir, MAX_PATH))
            return nullptr;

        std::string path(windowsDir);
        if (!path.empty() && path.back() != '\\')
            path += "\\";
        path += "Fonts\\tahoma.ttf";

        DWORD attrs = GetFileAttributesA(path.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return nullptr;

        auto& io = ImGui::GetIO();
        ImFontConfig cfg{};
        cfg.FontDataOwnedByAtlas = true;
        snprintf(cfg.Name, sizeof(cfg.Name), "Tahoma##ParallelChat");
        g_parallelFont = io.Fonts->AddFontFromFileTTF(
            path.c_str(), 14.0f, &cfg, io.Fonts->GetGlyphRangesDefault());
        return g_parallelFont;
    }

    static void push_parallel_font()
    {
        ImGui::PushFont(get_parallel_font(), 14.0f);
    }

    static void pop_parallel_font()
    {
        ImGui::PopFont();
    }

    static void push_parallel_msg(int chatType, const char* text)
    {
        auto& msg = g_parallelRing[g_parallelHead];
        msg.chatType = chatType;
        msg.tick = GetTickCount();

        std::string clean;
        if (text)
        {
            for (std::size_t i = 0; text[i] != '\0';)
            {
                if (text[i] == '<' && text[i + 1] == '<')
                {
                    int hexCount = 0;
                    while (hexCount < 6 && std::isxdigit(static_cast<unsigned char>(text[i + 2 + hexCount])))
                        ++hexCount;

                    if (hexCount >= 5)
                    {
                        i += 2 + hexCount;
                        continue;
                    }
                }

                clean.push_back(text[i]);
                ++i;
            }
        }

        if (!clean.empty())
            strncpy_s(msg.text, clean.c_str(), _TRUNCATE);
        else if (text && text[0] != '\0')
            msg.text[0] = '\0';
        else
            msg.text[0] = '\0';

        g_parallelHead = (g_parallelHead + 1) % kMaxParallelMsgs;
        if (g_parallelCount < kMaxParallelMsgs)
            ++g_parallelCount;
    }

    void record_chat_type(int chatType, const char* text)
    {
        load_parallel_persisted_state();
        if (g_parallelChatActive)
            push_parallel_msg(chatType, text);
    }

    bool hide_native_chat_visuals()
    {
        return g_hideNativeChatVisuals;
    }

    // -----------------------------------------------------------------------
    // Parallel chat rendering helpers and draw function
    // -----------------------------------------------------------------------
    static ImVec4 parallel_chat_default_color(int ct)
    {
        // Lower chat types â€” game palette
        if (ct == 34) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Normal — white
        if (ct == 35) return ImVec4(0.60f, 1.0f, 0.60f, 1.0f);      // Party â€” light green
        if (ct == 36) return ImVec4(1.0f, 0.65f, 0.65f, 1.0f);      // Whisper â€” very light red
        if (ct == 37) return ImVec4(0.85f, 0.55f, 1.0f, 1.0f);      // Guild â€” pink-violet
        if (ct == 38) return ImVec4(1.0f, 0.808f, 0.490f, 1.0f);     // Trade â€” rgb(255,206,125)
        if (ct == 39) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);        // Area/Shout â€” white
        if (ct == 40) return ImVec4(1.0f, 1.0f, 0.30f, 1.0f);       // Chat40 â€” yellow
        if (ct == 41) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);        // Admin/GM â€” white (no distinction)
        if (ct == 42) return ImVec4(1.0f, 0.50f, 0.20f, 1.0f);      // Warning â€” orange
        if (ct == 44) return ImVec4(0.0f, 0.851f, 1.0f, 1.0f);      // rgb(0,217,255)
        if (ct == 49) return ImVec4(0.431f, 0.443f, 1.0f, 1.0f);    // Raid â€” rgb(110,113,255)

        // Upper bar system messages
        if (ct == 15) return ImVec4(1.0f, 0.60f, 0.0f, 1.0f);       // Damage â€” orange
        if (ct == 16) return ImVec4(1.0f, 0.30f, 0.30f, 1.0f);
        if (ct == 17) return ImVec4(1.0f, 0.0f, 0.0f, 1.0f);        // rgb(255,0,0)
        if (ct == 18) return ImVec4(1.0f, 0.824f, 0.349f, 1.0f);  // rgb(255,210,89)
        if (ct == 19) return ImVec4(0.30f, 1.0f, 0.30f, 1.0f);
        if (ct == 20) return ImVec4(0.957f, 0.302f, 1.0f, 1.0f);    // rgb(244,77,255)
        if (ct == 21) return ImVec4(0.50f, 0.80f, 1.0f, 1.0f);
        if (ct == 22) return ImVec4(0.50f, 1.0f, 0.50f, 1.0f);

        // Notices / system — individual types for easier customisation
        if (ct == 23) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Notice23 — white
        if (ct == 24) return ImVec4(1.0f, 1.0f, 0.30f, 1.0f);        // Notice24 — yellow
        if (ct == 25) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Notice25 — white
        if (ct == 26) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Notice26 — white
        if (ct == 27) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Notice27 — white
        if (ct == 28) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Notice28 — white
        if (ct == 29) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Notice29 — white
        if (ct == 30) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Notice30 — white
        if (ct == 31) return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);         // Acquire31 — white
        if (ct >= 50) return ImVec4(0.65f, 0.65f, 0.65f, 1.0f);

        return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
    }

    static ImVec4 parallel_chat_color(int ct)
    {
        if (ct >= 0 && ct < static_cast<int>(sizeof(g_parallelColorCustom) / sizeof(g_parallelColorCustom[0])) &&
            g_parallelColorCustom[ct])
            return g_parallelColor[ct];

        return parallel_chat_default_color(ct);
    }

    static const char* chat_type_tag(int ct)
    {
        switch (ct)
        {
        case 15: return "[Dmg15]";
        case 16: return "[Dmg16]";
        case 17: return "[Death]";
        case 18: return "[Acq18]";
        case 19: return "[Acq19]";
        case 20: return "[Dmg20]";
        case 21: return "[Acq21]";
        case 22: return "[Acq22]";
        case 23: return "[Not23]";
        case 24: return "[Not24]";
        case 25: return "[Not25]";
        case 26: return "[Not26]";
        case 27: return "[Not27]";
        case 28: return "[Not28]";
        case 29: return "[Not29]";
        case 30: return "[Not30]";
        case 31: return "[Acq31]";
        case 34: return "[Normal]";
        case 35: return "[Party]";
        case 36: return "[Whisper]";
        case 37: return "[Guild]";
        case 38: return "[Trade]";
        case 39: return "[Area]";
        case 40: return "[Chat40]";
        case 41: return "(GM)";
        case 42: return "[Warning]";
        case 43: return "[Chat43]";
        case 44: return "[Chat44]";
        case 45: return "[Chat45]";
        case 46: return "[Chat46]";
        case 47: return "[All]";
        case 48: return "[Zone]";
        case 49: return "[Raid]";
        case 50: return "[Not50]";
        default: return nullptr;
        }
    }

    static bool is_upper_parallel_chat_type(int ct)
    {
        return (ct >= 15 && ct <= 33) || ct == 50;
    }

    static void draw_parallel_color_controls()
    {
        if (!ImGui::CollapsingHeader("Colors"))
            return;

        static int selectedType = 34;
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputInt("Chat Type", &selectedType, 1, 5);
        selectedType = std::clamp(selectedType, 0, 99);

        auto defaultColor = parallel_chat_default_color(selectedType);
        if (!g_parallelColorCustom[selectedType])
            g_parallelColor[selectedType] = defaultColor;

        ImGui::SameLine();
        if (ImGui::SmallButton("Use current##parallel_color"))
            g_parallelColorCustom[selectedType] = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset##parallel_color"))
        {
            g_parallelColorCustom[selectedType] = false;
            g_parallelColor[selectedType] = defaultColor;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset all##parallel_colors"))
        {
            for (auto& custom : g_parallelColorCustom)
                custom = false;
        }

        bool custom = g_parallelColorCustom[selectedType];
        if (ImGui::Checkbox("Custom color", &custom))
        {
            g_parallelColorCustom[selectedType] = custom;
            if (custom)
                g_parallelColor[selectedType] = defaultColor;
        }

        ImGui::SameLine();
        ImGui::ColorButton("Default##parallel_color_default", defaultColor);

        ImGui::BeginDisabled(!g_parallelColorCustom[selectedType]);
        ImGui::SetNextItemWidth(240.0f);
        ImGui::ColorEdit4("Color", reinterpret_cast<float*>(&g_parallelColor[selectedType]),
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        ImGui::EndDisabled();

        ImGui::TextDisabled("Quick select:");
        const int commonTypes[] = { 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50 };
        for (int i = 0; i < static_cast<int>(sizeof(commonTypes) / sizeof(commonTypes[0])); ++i)
        {
            if (i > 0)
                ImGui::SameLine();

            int ct = commonTypes[i];
            auto col = parallel_chat_color(ct);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(col.x, col.y, col.z, 0.35f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(col.x, col.y, col.z, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(col.x, col.y, col.z, 0.75f));
            char label[16];
            snprintf(label, sizeof(label), "%d##pct", ct);
            if (ImGui::SmallButton(label))
                selectedType = ct;
            ImGui::PopStyleColor(3);

            if ((i + 1) % 9 == 0)
                ImGui::NewLine();
        }
    }

    // Draw a single chat message with inline emoji/gif rendering.
    // Uses ImDrawList directly so text + emoji positioning is exact
    // even when the message wraps across multiple lines.
    static void draw_parallel_line(const char* text, const ImVec4& color)
    {
        using namespace imgui_layer;
        if (!text || text[0] == '\0')
        {
            ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
            return;
        }

        auto* drawList = ImGui::GetWindowDrawList();
        auto  origin   = ImGui::GetCursorScreenPos();
        float availW   = ImGui::GetContentRegionAvail().x;
        float lineH    = ImGui::GetTextLineHeight();
        float emojiSz  = lineH;
        ImU32 col32    = ImGui::ColorConvertFloat4ToU32(color);
        ImU32 shadow32 = IM_COL32(0, 0, 0, 170);
        auto* font     = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize();

        float cx = 0.0f;
        float cy = 0.0f;
        auto  len = std::strlen(text);
        std::size_t idx = 0;

        while (idx < len)
        {
            auto* emoji = (text[idx] == ':') ? find_emoji_by_token(text + idx) : nullptr;
            if (emoji)
            {
                auto tex = get_emoji_texture(*emoji);
                if (tex)
                {
                    if (cx + emojiSz > availW && cx > 0.0f)
                    { cx = 0.0f; cy += lineH; }

                    drawList->AddImage(reinterpret_cast<ImTextureID>(tex),
                        ImVec2(origin.x + cx, origin.y + cy),
                        ImVec2(origin.x + cx + emojiSz, origin.y + cy + emojiSz));
                    cx += emojiSz;
                }
                idx += emoji->token.size();
                continue;
            }

            auto runStart = idx;
            while (idx < len && !(text[idx] == ':' && find_emoji_by_token(text + idx)))
                ++idx;

            const char* p    = text + runStart;
            const char* pEnd = text + idx;

            while (p < pEnd)
            {
                const char* batchStart = p;
                float batchW = 0.0f;

                while (p < pEnd)
                {
                    float cw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, p, p + 1).x;
                    if (cx + batchW + cw > availW && (batchW > 0.0f || cx > 0.0f))
                        break;
                    batchW += cw;
                    ++p;
                }

                if (batchStart == p)
                {
                    if (cx > 0.0f) { cx = 0.0f; cy += lineH; }
                    else
                    {
                        float cw = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, p, p + 1).x;
                        drawList->AddText(ImVec2(origin.x + 1.0f, origin.y + cy + 1.0f), shadow32, p, p + 1);
                        drawList->AddText(ImVec2(origin.x, origin.y + cy), col32, p, p + 1);
                        cx = cw; ++p;
                    }
                    continue;
                }

                drawList->AddText(ImVec2(origin.x + cx + 1.0f, origin.y + cy + 1.0f), shadow32, batchStart, p);
                drawList->AddText(ImVec2(origin.x + cx, origin.y + cy), col32, batchStart, p);
                cx += batchW;
            }
        }

        ImGui::Dummy(ImVec2(availW, cy + lineH));
    }

    static void draw_parallel_message_row(const ParallelChatMsg& msg)
    {
        auto color = parallel_chat_color(msg.chatType);
        push_parallel_font();
        draw_parallel_line(msg.text, color);
        pop_parallel_font();
    }


    static void draw_parallel_chat()
    {
        using namespace imgui_layer;

        if (ImGui::Checkbox("Active##parallel", &g_parallelChatActive))
            write_imgui_bool(kParallelChatActiveKey, g_parallelChatActive);
        ImGui::SameLine();
        if (ImGui::Checkbox("In-game overlay", &g_ingameChatActive))
            write_imgui_bool(kIngameChatActiveKey, g_ingameChatActive);
        ImGui::SameLine();
        if (ImGui::Checkbox("Hide native chat", &g_hideNativeChatVisuals))
            write_imgui_bool(kHideNativeChatKey, g_hideNativeChatVisuals);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##parallel"))
        {
            g_parallelHead = 0;
            g_parallelCount = 0;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &g_parallelAutoScroll);

        ImGui::Separator();
        draw_parallel_color_controls();
    }

    // -----------------------------------------------------------------------
    // In-game chat overlay — renders upper + lower as standalone ImGui windows
    // -----------------------------------------------------------------------
    struct ChatPanelRuntimeState
    {
        float lastContentH = 0.0f;
        bool layoutDirty = false;
        bool dragging = false;
        bool resizing = false;
    };

    static ChatPanelRuntimeState g_upperChatPanel{};
    static ChatPanelRuntimeState g_lowerChatPanel{};

    static ChatPanelRuntimeState& chat_panel_state(bool upper)
    {
        return upper ? g_upperChatPanel : g_lowerChatPanel;
    }

    // Only this top strip accepts mouse input. The message window itself is
    // click-through so gameplay clicks below the strip keep reaching the client.
    static void draw_ingame_chat_handle(const char* windowId, bool upper, const ImVec2& pos, const ImVec2& size)
    {
        using namespace imgui_layer;

        auto& io = ImGui::GetIO();
        auto& normPos = upper ? g_upperChatPosition : g_lowerChatPosition;
        auto& normSize = upper ? g_upperChatSize : g_lowerChatSize;
        auto& state = chat_panel_state(upper);

        const float handleH = std::max(18.0f, ImGui::GetFrameHeight());
        const float resizeW = std::clamp(size.x * 0.18f, 24.0f, 56.0f);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(size.x, handleH), ImGuiCond_Always);
        const bool active = state.dragging || state.resizing;
        ImGui::SetNextWindowBgAlpha(active ? 0.45f : 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, active ? 1.0f : 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, active ? 1.0f : 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, active ? 1.0f : 0.0f));

        auto flags = ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse;

        if (ImGui::Begin(windowId, nullptr, flags))
        {
            ImGui::InvisibleButton("##move", ImVec2(size.x, handleH));
            auto min = ImGui::GetItemRectMin();
            auto max = ImGui::GetItemRectMax();
            const bool hovered = ImGui::IsItemHovered();

            if (ImGui::IsItemActivated())
            {
                const bool overResizeZone = io.MousePos.x >= max.x - resizeW;
                state.resizing = overResizeZone;
                state.dragging = !overResizeZone;
            }
            if (!ImGui::IsItemActive())
            {
                state.dragging = false;
                state.resizing = false;
            }

            if ((state.dragging || state.resizing) && io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
            {
                if (state.resizing)
                {
                    const float minW = 180.0f / io.DisplaySize.x;
                    const float minH = 80.0f / io.DisplaySize.y;
                    normSize.x = std::clamp(normSize.x + io.MouseDelta.x / io.DisplaySize.x, minW, 1.0f - normPos.x);
                    normSize.y = std::clamp(normSize.y + io.MouseDelta.y / io.DisplaySize.y, minH, 1.0f - normPos.y);
                }
                else
                {
                    normPos.x += io.MouseDelta.x / io.DisplaySize.x;
                    normPos.y += io.MouseDelta.y / io.DisplaySize.y;
                }
                state.layoutDirty = true;
            }

            if (active || hovered)
            {
                auto* dl = ImGui::GetWindowDrawList();
                const auto resizeMin = ImVec2(max.x - resizeW, min.y);
                dl->AddRectFilled(min, max, IM_COL32(20, 20, 24, active ? 125 : 45), 0.0f);
                dl->AddRectFilled(resizeMin, max, IM_COL32(230, 190, 70, state.resizing ? 115 : 55), 0.0f);
                dl->AddRect(min, max, IM_COL32(0, 0, 0, active ? 180 : 90), 0.0f);
                dl->AddLine(ImVec2(resizeMin.x, min.y + 3.0f), ImVec2(resizeMin.x, max.y - 3.0f),
                    IM_COL32(255, 255, 255, 80), 1.0f);
            }
        }
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    // Draws the text surface for one parallel chat panel. Position is persisted
    // in normalized screen coordinates so resolution changes keep it proportional.
    static void draw_ingame_chat_window(const char* windowId, bool upper)
    {
        using namespace imgui_layer;

        auto& io = ImGui::GetIO();
        auto dw = io.DisplaySize.x;
        auto dh = io.DisplaySize.y;
        auto& normPos = upper ? g_upperChatPosition : g_lowerChatPosition;
        auto& normSize = upper ? g_upperChatSize : g_lowerChatSize;
        auto pos = ImVec2(normPos.x * dw, normPos.y * dh);
        auto size = ImVec2(normSize.x * dw, normSize.y * dh);

        bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        auto& state = chat_panel_state(upper);
        bool manipulating = state.dragging || state.resizing;

        draw_ingame_chat_handle(upper ? "##UpperChatHandle" : "##LowerChatHandle", upper, pos, size);
        pos = ImVec2(normPos.x * dw, normPos.y * dh);
        size = ImVec2(normSize.x * dw, normSize.y * dh);

        ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(manipulating ? 0.45f : 0.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(g_chatTextIndent, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, manipulating ? 1.0f : 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, manipulating ? 1.0f : 0.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.10f, 0.10f, 0.12f, manipulating ? 1.0f : 0.0f));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.14f, 0.14f, 0.16f, manipulating ? 1.0f : 0.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, manipulating ? 1.0f : 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ResizeGrip, ImVec4(0.3f, 0.3f, 0.3f, manipulating ? 0.3f : 0.0f));
        auto flags = ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing
            | ImGuiWindowFlags_NoBringToFrontOnFocus
            | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoScrollbar
            | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoMouseInputs;

        if (!ImGui::Begin(windowId, nullptr, flags))
        {
            ImGui::End();
            ImGui::PopStyleColor(5);
            ImGui::PopStyleVar(2);
            return;
        }

        auto curPos = ImGui::GetWindowPos();
        auto curSize = ImGui::GetWindowSize();

        if (dw > 0.0f && dh > 0.0f)
        {
            ImVec2 newNormPos(curPos.x / dw, curPos.y / dh);
            ImVec2 newNormSize(curSize.x / dw, curSize.y / dh);
            if (newNormPos.x != normPos.x || newNormPos.y != normPos.y ||
                newNormSize.x != normSize.x || newNormSize.y != normSize.y)
            {
                normPos = newNormPos;
                normSize = newNormSize;
                state.layoutDirty = true;
            }
        }

        if (state.layoutDirty && !mouseDown)
        {
            save_chat_window_layout();
            state.layoutDirty = false;
        }

        ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMouseInputs;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##msgs", ImVec2(0, 0), false, childFlags);

        // Bottom-align: push messages down so they grow upward like the native chat.
        // Uses the previous frame's content height to compute how much top spacer is needed.
        float availH = ImGui::GetContentRegionAvail().y;
        float spacer = availH - state.lastContentH;
        if (spacer > 0.0f)
            ImGui::Dummy(ImVec2(1.0f, spacer));

        float contentStartY = ImGui::GetCursorPosY();

        // Render messages — show at most kMaxVisiblePerWindow per panel.
        // First pass: count how many messages belong to this window type.
        int start = (g_parallelCount < kMaxParallelMsgs) ? 0 : g_parallelHead;
        int typeCount = 0;
        for (int i = 0; i < g_parallelCount; ++i)
        {
            int idx = (start + i) % kMaxParallelMsgs;
            if (is_upper_parallel_chat_type(g_parallelRing[idx].chatType) == upper)
                ++typeCount;
        }
        int skip = (typeCount > kMaxVisiblePerWindow) ? typeCount - kMaxVisiblePerWindow : 0;

        // Second pass: render, skipping the oldest excess messages.
        int shown = 0, skipped = 0;
        for (int i = 0; i < g_parallelCount; ++i)
        {
            int idx = (start + i) % kMaxParallelMsgs;
            auto& msg = g_parallelRing[idx];
            if (is_upper_parallel_chat_type(msg.chatType) != upper)
                continue;
            if (skipped < skip) { ++skipped; continue; }

            draw_parallel_message_row(msg);
            ++shown;
        }

        if (!shown)
            ImGui::TextDisabled(upper ? "No system messages." : "No chat messages.");

        // Update cached content height for next frame's bottom-align spacer
        state.lastContentH = ImGui::GetCursorPosY() - contentStartY;

        // --- Scroll handling (inside child) ---
        float scrollY  = ImGui::GetScrollY();
        float scrollMax = ImGui::GetScrollMaxY();

        // Manual scroll via mouse wheel — works even in click-through mode
        // because io.MouseWheel is global raw input, not gated by NoMouseInputs.
        if (io.MouseWheel != 0.0f)
        {
            if (io.MousePos.x >= curPos.x && io.MousePos.x <= curPos.x + curSize.x &&
                io.MousePos.y >= curPos.y && io.MousePos.y <= curPos.y + curSize.y)
            {
                float step = io.MouseWheel * ImGui::GetTextLineHeight() * 3.0f;
                float target = scrollY - step;
                ImGui::SetScrollY(std::clamp(target, 0.0f, std::max(0.0f, scrollMax)));
                // Scrolling up disables auto-scroll; scrolling back to bottom re-enables it
                if (io.MouseWheel > 0.0f)  // scrolling up
                    g_parallelAutoScroll = false;
                else if (scrollMax > 0.0f && target >= scrollMax - 1.0f)
                    g_parallelAutoScroll = true;
            }
        }

        // Re-enable auto-scroll if user is at the very bottom (e.g. after dragging scrollbar)
        if (!g_parallelAutoScroll && scrollMax > 0.0f && scrollY >= scrollMax - 1.0f)
            g_parallelAutoScroll = true;

        // Auto-scroll — always keep at bottom when enabled
        if (g_parallelAutoScroll)
            ImGui::SetScrollHereY(1.0f);

        // Read scroll state for custom scrollbar (may lag 1 frame — imperceptible)
        scrollY  = ImGui::GetScrollY();
        scrollMax = ImGui::GetScrollMaxY();

        ImGui::EndChild();
        ImGui::PopStyleVar();  // child WindowPadding

        // --- Custom left-side scrollbar, visible only while manipulating the window ---
        if (manipulating)
        {
            constexpr float kBarW = 4.0f;
            auto* dl = ImGui::GetWindowDrawList();
            float headerH = ImGui::GetFrameHeight();
            float tTop  = curPos.y + headerH + 2.0f;
            float tBot  = curPos.y + curSize.y - 2.0f;
            float tH    = tBot - tTop;

            if (tH > 20.0f)
            {
                // Track background — always drawn
                dl->AddRectFilled(
                    ImVec2(curPos.x + 2.0f, tTop),
                    ImVec2(curPos.x + 2.0f + kBarW, tBot),
                    IM_COL32(255, 255, 255, 10), 2.0f);

                // Thumb — full height when no overflow, proportional otherwise
                float thumbH, thumbY;
                if (scrollMax > 0.0f)
                {
                    float visRatio = tH / (scrollMax + tH);
                    thumbH = std::max(12.0f, tH * visRatio);
                    float sRatio = scrollY / scrollMax;
                    thumbY = tTop + (tH - thumbH) * sRatio;
                }
                else
                {
                    thumbH = tH;    // full track = nothing to scroll
                    thumbY = tTop;
                }

                dl->AddRectFilled(
                    ImVec2(curPos.x + 2.0f, thumbY),
                    ImVec2(curPos.x + 2.0f + kBarW, thumbY + thumbH),
                    IM_COL32(180, 180, 180, 70), 2.0f);
            }
        }

        ImGui::End();
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);  // WindowPadding + WindowBorderSize
    }

    void render_ingame_chat()
    {
        using namespace imgui_layer;

        if (!g_ingameChatActive || !g_parallelChatActive)
            return;
        if (!is_game_scene_stable())
            return;

        draw_ingame_chat_window("##UpperChatIngame", true);
        draw_ingame_chat_window("##LowerChatIngame", false);
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

        // --- Global GM toggles ---
        {
            using namespace imgui_layer;
            if (ImGui::Checkbox("ID View", &g_idViewEnabled))
                write_imgui_bool(kIdViewEnabledKey, g_idViewEnabled);
            ImGui::SameLine();
            ImGui::TextDisabled("(mob/NPC IDs)");
        }
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Chat options", ImGuiTreeNodeFlags_DefaultOpen))
            draw_parallel_chat();

        ImGui::End();
    }
}
