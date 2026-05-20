#pragma once

namespace debug_panel
{
    // Call from the chat hook to record every chat type seen.
    void record_chat_type(int chatType, const char* text);
    bool hide_native_chat_visuals();

    // Call from render_integrated_frame() after ImGui::NewFrame().
    void render();

    // In-game chat overlay — call from render_integrated_frame() when game scene is stable.
    void render_ingame_chat();
}
