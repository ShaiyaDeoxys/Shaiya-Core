#pragma once

namespace debug_panel
{
    // Call from the chat hook to record every chat type seen.
    void record_chat_type(int chatType, const char* text);

    // Call from render_integrated_frame() after ImGui::NewFrame().
    void render();
}
